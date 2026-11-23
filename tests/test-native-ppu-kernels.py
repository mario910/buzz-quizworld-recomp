"""Differential test on a Zen 2 host with a prepared EU 1.00 workspace."""
import argparse
import os
from hashlib import sha256
from pathlib import Path
import runpy
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
gen = runpy.run_path(str(ROOT / "tools/codegen/generate-ppu-kernels.py"))


def comparison_source(translations):
    records = (translations / "ppu_recomp_015.cpp").read_text()
    matrices = (translations / "ppu_recomp_016.cpp").read_text()
    preamble = records[:records.index("void func_", records.index("static inline uint64_t ppc_rldicl"))]
    kernels = []
    for address, name, source in [(gen["SEARCH"], "mask", records), (gen["SLOTS"], "slots", records),
                                  (gen["MATRIX"], "matrix", matrices)]:
        original = gen["function"](source, address)[0]
        assert sha256(original.encode()).hexdigest() == gen["DIGESTS"][address], address
        for label, body in [("ref", original), ("new", gen["adapt"](original, address))]:
            # Exercise the replacement and the join with the original guest code.
            if name == "matrix":
                start = gen["FIRST_LOAD"] if label == "ref" else "#if"
                body = body[body.index(start):body.index("loc_002CBCD8:")]
            elif name == "slots":
                start = gen["SLOT_BRANCH"] if label == "ref" else "        if (ctx->gpr[5] <= 4)"
                body = body[body.index(start):body.index(gen["SLOT_CONTINUATION"])] + gen["SLOT_CONTINUATION"] + " return;"
            else:
                body = body[body.index("{") + 1:body.rindex("}")]
            kernels.append(f"static void {label}_{name}(ppu_context* ctx) {{\n{body}\n}}")
    for address, name, chunk in [(gen["OBJECT"], "object", "014"), (gen["UNPACK"], "unpack", "014"),
                                 (gen["WEIGHTED"], "weighted", "017"), (gen["PLANES"], "planes", "016"),
                                 (gen["POSE"], "pose", "014")]:
        source = (translations / f"ppu_recomp_{chunk}.cpp").read_text()
        original = gen["function"](source, address)[0]
        assert sha256(original.encode()).hexdigest() == gen["DIGESTS"][address], address
        start, end, _ = gen["transform_span"](original, address)
        adapted = gen["adapt_existing"](original, address)
        for label, block in [("ref", original[start:end]), ("new", adapted.removeprefix(original[:start]).removesuffix(original[end:]))]:
            kernels.append(f"static void {label}_{name}(ppu_context* ctx) {{\n{block}\n}}")
    for address in gen["COLUMNS"]:
        chunk = "015" if address == gen["ROTATION"] else "014"
        source = (translations / f"ppu_recomp_{chunk}.cpp").read_text()
        original = gen["function"](source, address)[0]
        assert sha256(original.encode()).hexdigest() == gen["DIGESTS"][address], address
        for start, end, kernel in gen["column_spans"](original, address):
            reference = original[start:end]
            for label, block in [("ref", reference), ("new", gen["guarded_simd_block"](reference, kernel))]:
                kernels.append(f"static void {label}_{kernel}_{address}(ppu_context* ctx) {{\n{block}\n}}")
    for address, name, chunk in [(gen["PACK"], "pack", "014"), (gen["STREAM"], "stream", "014"),
                                 (gen["COLLECT"], "collect", "014"), (gen["TRACKS"], "tracks", "016"),
                                 (gen["FILTER"], "filter", "016")]:
        source = (translations / f"ppu_recomp_{chunk}.cpp").read_text()
        original = gen["function"](source, address)[0]
        assert sha256(original.encode()).hexdigest() == gen["DIGESTS"][address], address
        start, end, _ = gen["job_span"](original, address)
        adapted = gen["adapt"](original, address)
        for label, span in [("ref", original[start:end]),
                            ("new", adapted.removeprefix(original[:start]).removesuffix(original[end:]))]:
            if name == "filter":
                span = ("loc_002C02F0:\n" + span + "return;\n" +
                        original[original.index("loc_002C02D8:"):original.index("loc_002C02F0:")] +
                        "goto loc_002C02F0;\nloc_002C0384: note(1,0,false); return;\n"
                        "loc_002C0404: note(2,0,false); return;")
            elif name == "stream":
                span += "return;\n" + original[original.index("loc_0028A880:"):original.rindex("}")]
            elif name == "collect":
                span = "loc_00288160:\n" + span.replace("func_002B6EF0(ctx)", "ref_mask(ctx)") + "return;"
            elif name == "tracks":
                span = ("loc_002DD3B0:\n" + span + "return;\n" +
                        original[original.index("loc_002DD728:"):original.index("loc_002DD794:")] +
                        "loc_002DD7E4: note(3,0,false); return;")
            kernels.append(f"static void {label}_{name}(ppu_context* ctx) {{\n{span}\n;}}")
    for key, name, chunk in [("SCENE_JOB", "scene", "014"), ("ANIM_SLOT", "animation", "016"),
                             ("DMA_RANGES", "ranges", "015"), ("COMMAND", "command", "015")]:
        address = gen[key]
        source = (translations / f"ppu_recomp_{chunk}.cpp").read_text()
        original = gen["function"](source, address)[0]
        assert sha256(original.encode()).hexdigest() == gen["DIGESTS"][address], address
        start, end, _ = gen["job_span"](original, address)
        for label, body in [("ref", original), ("new", gen["adapt"](original, address))]:
            if key in ("ANIM_SLOT", "DMA_RANGES"):
                kernels.append(body.replace("func_" + address + "(", f"{label}_{name}(", 1))
                continue
            span = body.removeprefix(original[:start]).removesuffix(original[end:])
            if key == "COMMAND":
                span = ("loc_00293078:\n" + span +
                        "switch (uint32_t(ctx->ctr)) { case 0x293254: goto loc_00293254;"
                        "case 0x293288: goto loc_00293288; case 0x293128: goto loc_00293128; default: return; }\n" +
                        original[original.index("loc_00293254:"):original.index("loc_00293268:")] +
                        original[original.index("loc_00293288:"):original.index("loc_0029329C:")] +
                        original[original.index("loc_00293128:"):original.index("loc_00293138:")])
            kernels.append(f"static void {label}_{name}(ppu_context* ctx) {{\n{span}\n;}}")
    checks = []
    for address in gen["PERMUTE_COUNTS"]:
        chunk = "014" if int(address, 16) < 0x2A0000 else "016"
        original = gen["function"]((translations / f"ppu_recomp_{chunk}.cpp").read_text(), address)[0]
        assert sha256(original.encode()).hexdigest() == gen["DIGESTS"][address]
        for site, match in enumerate(gen["permute_regions"](original)):
            a, b, c, dest = match.groups()
            checks.append(f"{{ ppu_context old = seed, actual = seed; ppu_context* ctx = &old; {match[0]} "
                          f"buzz::ppu_permute_bytes<{'true' if a == b else 'false'}>(actual.vr[{dest}], "
                          f"actual.vr[{a}], actual.vr[{b}], actual.vr[{c}]); "
                          f"if (memcmp(&old,&actual,sizeof old)) std::abort(); }}")
    kernels.append('static void check_permutations(const ppu_context& seed) {\n' + '\n'.join(checks) + '\n}')
    template = (ROOT / "tests/ppu-native-kernels-test.cpp.in").read_text()
    return template.replace("/*LIFT_HEADERS*/", preamble).replace("/*REFERENCE*/", "\n".join(kernels))


def check_column_groups(translations, output):
    # Reusing a build directory must also remove previously enabled regions.
    for group in ("all", "rotations", "matrices", "none"):
        args = [] if group == "none" else ["--ppu-columns", group]
        subprocess.run([sys.executable, ROOT / "tools/codegen/generate-ppu-kernels.py",
                        translations, output, *args], check=True)
        for address in gen["COLUMNS"]:
            chunk = "015" if address == gen["ROTATION"] else "014"
            name = f"ppu_recomp_{chunk}.cpp"
            reference = gen["function"]((translations / name).read_text(), address)[0]
            actual = gen["function"]((output / name).read_text(), address)[0]
            for _, _, kernel in gen["column_spans"](reference, address):
                assert actual.count(f"buzz::{kernel}(*ctx);") == int(address in gen["COLUMN_GROUPS"][group])
            if address == gen["UNPACK"]:
                assert actual.count("buzz::ppu_unpack_transform_rows(*ctx);") == 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    args = parser.parse_args()
    translations = args.workspace / "port/recompiled"
    with TemporaryDirectory() as directory:
        check_column_groups(translations, Path(directory) / "generated")
        source = Path(directory) / "test.cpp"
        program = Path(directory) / "test"
        subprocess.run(["clang++", "-std=c++17", "-I", ROOT / "port",
                        ROOT / "tests/studio-effects-test.cpp", "-o", program], check=True)
        environment = os.environ.copy()
        environment.pop("BUZZ_STUDIO_DETAIL", None)
        for mode in (None, "original", "effects", "balanced", "unknown"):
            if mode is not None:
                environment["BUZZ_STUDIO_DETAIL"] = mode
            subprocess.run([program, mode or "balanced"], env=environment, check=True)
        source.write_text(comparison_source(translations))
        for flags in [("-O1", "-fwrapv", "-fno-strict-aliasing"), ("-O2", "-frounding-math")]:
            subprocess.run(["clang++", *flags, "-std=gnu++20", "-march=znver2", "-ffp-contract=off",
                            "-I", ROOT / "port", "-I", translations, "-I",
                            args.workspace / "ps3recomp/runtime/ppu", source, "-o", program], check=True)
            subprocess.run([program], check=True)


if __name__ == "__main__":
    main()
