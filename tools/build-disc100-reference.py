"""Hash-bound EU/US original-disc generation and developer reference probe.

All derived work stays under analysis. Reuses real SPURS jobs; no completion
is fabricated and no 1.02 native arithmetic/polling kernels are enabled.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "ps3recomp/tools"))
from launcher.setup_pipeline import copy_sources, process
from tools.disc100_profiles import EU100_SHA256, US100_SHA256, US_RAW_ROLES, canonicalize_embedded
from spu_disasm import disassemble_spu
from build_spu_workloads import debase_includes, fnv1a64
from extract_spu_images import find_spu_images
from find_spu_functions import (collect_branch_targets, collect_link_register_targets,
                               collect_computed_branch_targets, collect_jump_table_targets)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


compare = load("compare_versions", ROOT / "tools/compare-game-versions.py")
callers_module = load("import_callers", ROOT / "tools/inspect-import-callers.py")
EXPECTED = EU100_SHA256


def bindings(elf, name, toc):
    found = []
    start = 0
    while True:
        pos = elf.data.find(name.encode() + b"\0", start)
        if pos < 0:
            break
        start = pos + 1
        string = elf.address(pos)
        if string is None:
            continue
        for reference in compare.matches(elf.data, struct.pack(">I", string)):
            try:
                opd = struct.unpack_from(">I", elf.data, reference + 4)[0]
                code, table_toc = struct.unpack(">II", elf.read(opd, 8))
                if table_toc == toc and 0x10000 <= code < 0x700000:
                    found.append(dict(code=code, opd=opd, reference=elf.address(reference), string=string))
            except (ValueError, struct.error):
                pass
    return {item["code"]: item for item in found}


def rebind_host(work, elf, imports, toc, us=False):
    account = bindings(elf, "HasUserNpAccount", toc)
    if len(account) != 1:
        raise ValueError("Ambiguous offline binding")
    account = next(iter(account.values()))
    source = (work / "port/frontend_policy.cpp").read_text()
    source = source.replace('hide_store=option("BUZZ_HIDE_PSN_STORE");', 'hide_store=false; /* not validated on 1.00 */')
    source = re.sub(r"(?ms)^void buzz_frontend_after_include\(.*?^}",
                    "void buzz_frontend_after_include(ppu_context*,uint32_t) {}", source)
    push_boolean = "func_0047E1B8" if us else "func_0047E590"
    replacements = {"func_0046B8F8": push_boolean, "0x833098": hex(account["reference"]),
                    "0x6e5980": hex(account["string"]), "0x83f008": hex(account["opd"]), "0x12b760": hex(account["code"])}
    for old, new in replacements.items():
        if old not in source:
            raise ValueError("Frontend source changed; review the profile generator")
        source = source.replace(old, new)
    (work / "port/frontend_policy.cpp").write_text(source)
    source = (work / "port/boot_trace.cpp").read_text()
    evidence = {}
    video_anchor = bindings(elf, "OpenStreamLocalised", toc)
    if len(video_anchor) != 1:
        raise ValueError("Ambiguous video binding table")
    video_table = next(iter(video_anchor.values()))["reference"]
    def probe(match):
        old, name = match.group(1), match.group(2)
        candidates = bindings(elf, name.split(".")[-1], toc)
        if name.startswith("Video."):
            candidates = {code: item for code, item in candidates.items() if abs(item["reference"] - video_table) < 0x200}
        code = next(iter(candidates)) if len(candidates) == 1 else 0
        evidence[name] = dict(old_pc=old, code=code, candidates=list(candidates))
        if not code and name in ("Video.OpenStreamLocalised", "Video.PlayStream", "Video.IsStreamPlaying", "Video.IsStreamErrored", "NaviSlide.PresentMenu"):
            raise ValueError("Missing essential no-FMV probe " + name)
        return '{' + hex(code) + ',"' + name + '"}'
    source = re.sub(r'\{0x([0-9a-fA-F]+),"([^"]+)"}', probe, source)
    source = source.replace("func_0046B8F8", push_boolean)
    (work / "port/boot_trace.cpp").write_text(source)
    calls = callers_module.callers(elf, imports)
    pushes = calls["cellSpurs:0x8a85674d"]
    initializers = calls["cellSpurs:0x11ee38b"]
    if len(pushes) != 3 or len(initializers) != 2:
        raise ValueError("Unexpected queue call graph; do not guess return PCs")
    path = work / "port/edge_decompress.cpp"
    path.write_text(path.read_text().replace("0x00333340u", f"0x{pushes[0]:08x}u"))
    path = work / "port/video_queue.cpp"
    text = path.read_text()
    for old, pc in (("0x4906d4", pushes[1]), ("0x490a30", pushes[2]), ("0x490d84", initializers[1])):
        if old not in text:
            raise ValueError("Video queue source changed")
        text = text.replace(old, hex(pc))
    path.write_text(text)
    path = work / "port/stubs.cpp"
    text = path.read_text()
    anchor = "sys_timer_register_usleep_wait(0x0028AA8Cu, 30, 0x1A0u);"
    if text.count(anchor) != 1:
        raise ValueError("Polling source changed; review the 1.00 wait-site binding")
    wait_pc = 0x288870 if us else 0x288BE0
    path.write_text(text.replace(anchor, f"sys_timer_register_usleep_wait(0x{wait_pc:08X}u, 30, 0x1A0u);"))
    return dict(offline=account, boot_probes=evidence, queue_push_return_pcs=pushes, queue_init_return_pcs=initializers)


def raw_jobs(work, elf, us=False):
    output = work / "analysis/raw-reference"
    output.mkdir(parents=True, exist_ok=True)
    # Canonical role filenames may intentionally differ from container offsets.
    embedded = [(start, start + size) for start, size in find_spu_images(elf.data)]
    starts = []
    for offset in range(0x700000, min(len(elf.data) - 16, 0x850000), 128):
        if any(lo <= offset < hi for lo, hi in embedded):
            continue
        if all(word & 0xFE00007F == 0x42000002 for word in struct.unpack_from(">4I", elf.data, offset)):
            starts.append(offset)
    if len(starts) != 16:
        raise ValueError(f"Expected the reviewed 16 raw images, found {len(starts)}")
    boundaries = sorted(starts + [lo for lo, _ in embedded] + [len(elf.data)])
    # Canonical ROLE aliases retain the established tooling names. They are
    # not claims that the old fingerprint or native kernel matches this binary.
    roles = {0x73A780: ("buzz_job_abe1", 40), 0x745300: ("buzz_job_2b4b", 33),
             0x74ED80: ("buzz_job_b3ae", 42), 0x759280: ("buzz_job_19d0", 43),
             0x763F00: ("buzz_job_c0bc", 39), 0x76B680: ("buzz_job_6874", 34),
             0x786900: ("buzz_job_6eba", 41), 0x794F80: ("buzz_job_14b4", 36),
             0x7C2500: ("buzz_job_39d9", 38), 0x7C2F80: ("buzz_job_d2c1", 35),
             0x7DA080: ("buzz_job_d690", 44), 0x7E3700: ("buzz_job_40b5", 37),
             0x7F2100: ("buzz_signal", 32)}
    if us:
        roles = US_RAW_ROLES
        if set(starts) != set(roles):
            raise ValueError("US raw image layout changed")
    declarations, registrations, report = [], [], []
    fallback_id = 45
    for start in starts:
        end = next(boundary for boundary in boundaries if boundary > start)
        blob = elf.data[start:end]
        if len(blob) > 256 * 1024:
            raise ValueError("Raw job extent exceeds local store")
        name, image_id = roles.get(start, (f"disc_job_{start:08x}", fallback_id))
        if start not in roles:
            fallback_id += 1
        returns = [offset for offset in range(16, len(blob) - 3, 4) if blob[offset:offset + 4] == b"\x35\0\0\0"]
        if not returns:
            raise ValueError("No return boundary in raw job")
        code_end = min(len(blob), (max(returns) + 0x100 + 15) & ~15)
        raw = output / (name + ".bin")
        raw.write_bytes(blob)
        instructions = list(disassemble_spu(blob[:code_end], 0))
        entries = {0x10}
        entries.update(address for address in (collect_branch_targets(instructions) | collect_link_register_targets(instructions)
                       | collect_computed_branch_targets(instructions) | collect_jump_table_targets(instructions, blob, 0, code_end))
                       if 0x10 <= address < code_end)
        entries = sorted(entries)
        functions = output / (name + ".functions.json")
        functions.write_text(json.dumps([dict(start=hex(a), end=hex(b)) for a, b in zip(entries, entries[1:] + [code_end])]))
        dest = work / "port/spu" / name
        subprocess.run([sys.executable, str(work / "ps3recomp/tools/spu_lifter.py"), str(raw), "--functions", str(functions),
                        "--output", str(dest), "--symbol-prefix", name + "_", "--code-end", hex(code_end)], check=True)
        for source in (dest / "spu_recomp.c", dest / "spu_recomp.h"):
            debase_includes(source)
        symbol = name + "_spu_func_00000010"
        declarations += [f"extern void {symbol}(spu_context*);", f"extern void {name}_spu_recomp_register(void);"]
        registrations += [f"spu_begin_image({image_id}); {name}_spu_recomp_register();"]
        sizes = []
        # Probe the at-most-224-byte alignment tail, never change the runtime's
        # descriptor size or staged payload. Only exact byte fingerprints match.
        for padding in range(0, 240, 16):
            size = len(blob) - padding
            if size <= max(returns):
                continue
            sizes.append(size)
            registrations.append(f'spu_workload_register_img(0x{fnv1a64(blob[:size]):016X}ULL,{symbol},{image_id},"{name}");')
        report.append(dict(name=name, image_id=image_id, offset=start, span=len(blob), probe_sizes=sizes, code_end=code_end))
    if sum(len(item["probe_sizes"]) for item in report) + len(embedded) > 256:
        raise ValueError("Workload registration budget exceeded")
    (work / "port/spu/jobmods_register.c").write_text('\n'.join(['#include "spu_workload.h"', 'extern void spu_begin_image(int);',
        *declarations, '__attribute__((constructor)) static void disc_reference_jobs(void) {', *registrations, 'spu_begin_image(0);', '}']))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--game", type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--work", default="disc100-reference")
    parser.add_argument("--resume", action="store_true", help="Reuse this probe's already generated PPU/embedded ELF sources")
    parser.add_argument("--in-place", action="store_true", help="Generate into this checkout; host adaptations remain under analysis")
    parser.add_argument("--generate-only", action="store_true", help="Generate without invoking a compiler")
    parser.add_argument("--host-only", action="store_true", help="Refresh host bindings of an already generated in-place 1.00 workspace")
    args = parser.parse_args()
    digest = hashlib.sha256(args.elf.read_bytes()).hexdigest()
    if digest not in (EU100_SHA256, US100_SHA256):
        raise ValueError("Not a reviewed EU/US original-disc ELF")
    us = digest == US100_SHA256
    family = "quizworld-us100" if us else "quizworld-eu100"
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,64}", args.work):
        raise ValueError("Work name must be a single safe directory name")
    work = ROOT if args.in_place else ROOT / "analysis" / args.work / "workspace"
    if args.in_place and not args.generate_only:
        raise ValueError("In-place generation must use --generate-only; build through Build-Buzz.ps1")
    if args.host_only:
        marker = work / "analysis/game-generation.json"
        if not args.in_place or not marker.is_file() or json.loads(marker.read_text())["elf_sha256"] != digest:
            raise ValueError("Host-only mode requires an existing hash-bound in-place 1.00 generation")
        args.resume = True
    if not args.generate_only and (args.game is None or args.firmware is None):
        raise ValueError("A probe build requires --game and --firmware")
    if (work / "port/spu/spu_0006_at_007E7500").exists():
        raise ValueError("This workspace contains 1.02 translations. Use a separate preparation session; they will not be deleted.")
    marker = work / "analysis/game-generation.json"
    if marker.exists() and json.loads(marker.read_text(encoding="utf-8-sig"))["elf_sha256"] != digest:
        raise ValueError("Existing generation belongs to a different executable; use a new workspace")
    receipt = work / "analysis/disc100-input.json"
    if receipt.exists() and json.loads(receipt.read_text())["elf_sha256"] != digest:
        raise ValueError("Partial generation belongs to a different executable; use a new workspace")
    if args.resume and not receipt.exists() and not marker.exists():
        raise ValueError("Cannot resume an unbound legacy/partial generation; use a new workspace")
    if not args.in_place and work.exists() and not args.resume:
        raise ValueError("Probe workspace exists; do not overwrite it")
    if not args.in_place and not args.resume:
        copy_sources(ROOT, work)
    elif not args.in_place:
        for filename in ("frontend_policy.cpp", "boot_trace.cpp", "edge_decompress.cpp", "video_queue.cpp", "stubs.cpp",
                         "virtual_buzzer.cpp", "native_stagef.cpp", "native_image36.cpp"):
            shutil.copy2(ROOT / "port" / filename, work / "port" / filename)
        for filename in ("runtime/ppu/ppu_tls_image.h", "runtime/ppu/ppu_loader.cpp"):
            shutil.copy2(ROOT / "ps3recomp" / filename, work / "ps3recomp" / filename)
        for filename in ("generate-image36-native.py", "test-image36.ps1"):
            shutil.copy2(ROOT / filename, work / filename)
    analysis = work / "analysis"
    analysis.mkdir(exist_ok=True)
    receipt.write_text(json.dumps(dict(family=family, elf_sha256=digest)))
    elf = compare.Elf(args.elf)
    def python(tool, *arguments):
        subprocess.run([sys.executable, str(work / "ps3recomp/tools" / tool), *map(str, arguments)], check=True)
    embedded_aliases = []
    if not args.resume:
        python("ppu_loader.py", args.elf, "-o", analysis)
        python("find_functions.py", args.elf, "--seed-json", analysis / "EBOOT.functions.json", "--output", analysis / "functions-full.json")
    imports = json.loads((analysis / "EBOOT.imports.json").read_text())
    code_end = max(int(item["stub"], 16) for item in imports) + 32
    if not args.resume:
        python("ppu_lifter.py", args.elf, "--functions", analysis / "functions-full.json", "--hle-stubs", analysis / "EBOOT.imports.json",
               "--output", work / "port/recompiled", "--jobs", args.jobs, "--chunks", 96, "--code-end", hex(code_end))
        python("extract_spu_images.py", args.elf, "--output", analysis / "spu-images")
        if us:
            embedded_aliases = canonicalize_embedded(analysis / "spu-images")
        python("build_spu_workloads.py", "--images", analysis / "spu-images", "--lifted", work / "port/spu", "--out", work / "port/spu/spu_workloads.c",
               "--constructor", "--title", "BuzzDisc100Probe")
    loader = json.loads((analysis / "EBOOT.loader.json").read_text())
    toc = int(loader["entry_toc"], 16)
    host = analysis / "host-disc100"
    (host / "port").mkdir(parents=True, exist_ok=True)
    for filename in ("frontend_policy.cpp", "boot_trace.cpp", "edge_decompress.cpp", "video_queue.cpp", "stubs.cpp"):
        shutil.copy2(work / "port" / filename, host / "port" / filename)
    evidence = rebind_host(host, elf, imports, toc, us)
    evidence["embedded_aliases"] = embedded_aliases
    evidence["raw_jobs"] = (json.loads((analysis / "disc100-profile-evidence.json").read_text())["raw_jobs"]
                            if args.host_only else raw_jobs(work, elf, us))
    (analysis / "disc100-profile-evidence.json").write_text(json.dumps(evidence, indent=2))
    (analysis / "game-generation.json").write_text(json.dumps(dict(family=family, elf_sha256=digest)))
    if args.generate_only:
        print("GENERATED_DISC100=" + str(work))
        return
    (work / "buzz-paths.json").write_text(json.dumps(dict(game_root=str(args.game.resolve()), dev_flash=str(args.firmware.resolve()),
                                                        eboot_elf=str(args.elf.resolve())), indent=2))
    env = dict(os.environ, BUZZ_TOOL_ENV=str(ROOT / "toolenv"))
    process(["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", work / "build-windows.ps1", "-ReferenceOnly", "-Jobs", str(args.jobs)],
            work, lambda line: print(line, flush=True), threading.Event(), env=env, timeout=7200)
    print("REFERENCE_PROBE=" + str(work))


if __name__ == "__main__":
    main()
