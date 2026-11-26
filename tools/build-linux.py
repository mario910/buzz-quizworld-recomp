"""Generate and build the standalone desktop game on Linux or macOS."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.desktop_adapters import desktop_options
from launcher.game_profiles import identify_elf
from launcher.paths import load


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


def python(tool, *args):
    run(sys.executable, ROOT / tool, *args)


def generate(elf, profile, jobs):
    if profile.adapter_family != "quizworld-102-verified":
        python("tools/codegen/build-disc100-reference.py", "--elf", elf, "--in-place", "--generate-only", "--jobs", str(jobs))
        return
    analysis = ROOT / "analysis"
    spu = ROOT / "port/spu"
    images = analysis / "spu-images"
    python("ps3recomp/tools/ppu_loader.py", elf, "-o", analysis)
    functions = analysis / "functions-full.json"
    python("ps3recomp/tools/find_functions.py", elf, "--seed-json", analysis / "EBOOT.functions.json", "--output", functions)
    imports = analysis / "EBOOT.imports.json"
    stubs = json.loads(imports.read_text())
    code_end = max(int(entry["stub"], 16) for entry in stubs) + 32
    python("ps3recomp/tools/ppu_lifter.py", elf, "--functions", functions, "--hle-stubs", imports,
           "--output", ROOT / "port/recompiled", "--jobs", str(jobs), "--chunks", "96", "--code-end", hex(code_end))
    python("ps3recomp/tools/extract_spu_images.py", elf, "--output", images)
    python("ps3recomp/tools/build_spu_workloads.py", "--images", images, "--lifted", spu, "--out", spu / "spu_workloads.c",
           "--constructor", "--title", "BuzzQuizWorld102")
    python("tools/codegen/generate-raw-jobs.py")
    (analysis / "game-generation.json").write_text(json.dumps(dict(family=profile.adapter_family, elf_sha256=profile.elf_sha256)))


def native_adapters(analysis, disc, image39_fast=False, image39_native=False):
    """Generate the same hash-checked x86 adapters as the desktop native build."""
    for tool in ("image36-native", "image7-native", "image39-native", "image39-kernels",
                 "image40-native", "geometry-phase3" if disc else "geometry-stagef"):
        python(f"tools/codegen/generate-{tool}.py")
    image40 = analysis / "native-image40/spu_recomp.c"
    if disc:
        animation = analysis / "native-geometry/phase4" / ("linux-runtime-" + uuid.uuid4().hex)
        python("tools/codegen/generate-animation-job-runtime.py", "--fallback", image40, "--output", animation)
        image40 = animation / "spu_recomp.c"
    if image39_fast:
        python("tools/codegen/generate-image39-fast.py")
    return {
        "PORT_GEOMETRY_SOURCE": analysis / ("native-geometry/phase3" if disc else "stagef-geometry") / "spu_recomp.c",
        "PORT_IMAGE7_SOURCE": analysis / "native-image7/spu_recomp.c",
        "PORT_IMAGE36_SOURCE": analysis / "native-image36/spu_recomp.c",
        "PORT_IMAGE39_SOURCE": analysis / ("fast-image39" if image39_fast else
                                           "native-image39-kernels" if image39_native or not disc else "native-image39") / "spu_recomp.c",
        "PORT_IMAGE40_SOURCE": image40,
    }


def build(paths, profile, jobs, *, native_jobs=False, renderer_cpu=False, geometry_cpu=False, media_cpu=False,
          spurs_input_clear=False, spu_job_return=False, zen2_tune=False, geometry_zero_padding=False, dma_diagnostics_optin=False,
          geometry_index_cache=False, geometry_scalar_gather=False, audio_x86_rtz=False, pgo_profile=None,
          renderer_bundle=False, image39_fast=False, image39_native=False, ppu_columns="none"):
    macos = sys.platform == "darwin"
    if pgo_profile is not None:
        if not native_jobs:
            raise ValueError("Scoped PGO requires --native-jobs.")
        pgo_profile = Path(pgo_profile).resolve(strict=True)
        if not pgo_profile.is_file():
            raise ValueError("PGO profile must be a merged Clang profile file.")
    analysis = ROOT / "analysis"
    generation = json.loads((analysis / "game-generation.json").read_text(encoding="utf-8-sig"))
    if generation["elf_sha256"] != profile.elf_sha256 or generation["family"] != profile.adapter_family:
        raise ValueError("The generated translations do not match the selected executable.")
    disc = profile.adapter_family != "quizworld-102-verified"
    native_ppu = native_jobs and profile.adapter_family == "quizworld-eu100"
    if ppu_columns != "none" and not native_jobs:
        raise ValueError("PPU column kernels require --native-jobs.")
    if not disc and (image39_fast or image39_native or geometry_cpu or geometry_zero_padding
                     or geometry_scalar_gather or geometry_index_cache):
        raise ValueError("The requested disc-specific geometry or image 39 adapter requires a 1.00 executable.")
    if geometry_cpu and not native_jobs:
        raise ValueError("Geometry CPU experiments require --native-jobs.")
    if geometry_scalar_gather and not native_jobs:
        raise ValueError("Geometry scalar gather requires --native-jobs.")
    if geometry_index_cache and not native_jobs:
        raise ValueError("Geometry index cache requires --native-jobs.")
    if geometry_zero_padding and not native_jobs:
        raise ValueError("Zero vertex padding requires --native-jobs.")
    if audio_x86_rtz and not media_cpu:
        raise ValueError("Scoped x86 audio arithmetic requires --media-cpu.")
    if media_cpu and not disc:
        raise ValueError("Bundled audio kernels require a supported 1.00 executable.")
    python("tools/codegen/generate-mstream-modules.py", "--dev-flash", paths["dev_flash"])
    python("tools/codegen/generate-native-completion.py")
    completion = json.loads((analysis / "native-completion/profile.json").read_text())["source"]
    tools = Path(sys.executable).parent
    cmake = tools / "cmake"
    ninja = tools / "ninja"
    if not cmake.is_file() or not ninja.is_file():
        raise ValueError("Run tools/setup-linux.py and use its toolenv/bin/python.")
    options = {
        "CMAKE_MAKE_PROGRAM": ninja,
        "PS3RECOMP_DIR": ROOT / "ps3recomp", "Python3_EXECUTABLE": sys.executable,
        "BUZZ_STANDALONE": "ON", "BUZZ_DISC_COMPLETION": "ON" if disc else "OFF",
        "PORT_SPU_LS_DEBUG_HOOKS": "OFF",
        "BUZZ_EXPERIMENTAL_VULKAN": "OFF" if macos else "ON", "PORT_SPU_ISA": "sse2" if macos else "avx2",
        "PORT_NATIVE_SPU_FLOAT": "OFF" if macos else "ON", "PORT_RTZ_RESULT_GUARD": "OFF" if macos else "ON",
        "PORT_NATIVE_JOB_DEFAULTS": "ON" if native_jobs and disc else "OFF",
        "PORT_NATIVE_JOB_PROFILE": "us100-vulkan-native-v1" if profile.adapter_family == "quizworld-us100" else "eu100-vulkan-native-v1",
        "PORT_SPU_OPT": "-O2", "PORT_OPTIMIZE_PPU_HOT_PATHS": "ON",
        "BUZZ_SPURS_INPUT_CLEAR": "ON" if spurs_input_clear else "OFF",
        "BUZZ_SPU_JOB_RETURN": "ON" if spu_job_return else "OFF",
        "BUZZ_ZEN2_TUNE": "ON" if zen2_tune else "OFF",
        "BUZZ_ZEN2_ARCH": "ON" if zen2_tune else "OFF",
        "BUZZ_DMA_DIAGNOSTICS_OPTIN": "ON" if dma_diagnostics_optin else "OFF",
        "BUZZ_GEOMETRY_SCALAR_GATHER": "ON" if geometry_scalar_gather else "OFF",
        "BUZZ_AUDIO_X86_RTZ": "ON" if audio_x86_rtz else "OFF",
        "BUZZ_IMAGE39_NATIVE_REQUEST": "ON" if image39_native else "OFF",
        "BUZZ_IMAGE39_NATIVE_BOUNDS": "ON" if image39_native else "OFF",
        "BUZZ_PGO_MODE": "USE" if pgo_profile is not None else "OFF",
        "BUZZ_PGO_PROFILE": pgo_profile if pgo_profile is not None else "",
        "BUZZ_GEOMETRY_ZERO_VERTEX_PADDING": "ON" if geometry_zero_padding else "OFF",
        "BUZZ_SPU_DIRECT_HOPS": "ON" if native_jobs else "OFF",
        "PORT_COMPLETION_SOURCE": analysis / "native-completion" / completion,
        "PORT_HOST_PROFILE_DIR": analysis / "host-disc100/port" if disc else "",
        "PORT_MSTREAM_DIR": analysis / "mstream-modules",
        "CMAKE_C_COMPILER": "clang", "CMAKE_CXX_COMPILER": "clang++",
        "CMAKE_BUILD_TYPE": "Release", "CMAKE_C_FLAGS_RELEASE": "-O1 -DNDEBUG", "CMAKE_CXX_FLAGS_RELEASE": "-O1 -DNDEBUG",
    }
    # Explicitly clear adapter settings when returning to the reference build;
    # a reused CMake cache must not silently keep a candidate's native paths.
    options.update({key: "" for key in ("PORT_GEOMETRY_SOURCE", "PORT_IMAGE7_SOURCE",
                    "PORT_IMAGE36_SOURCE", "PORT_IMAGE39_SOURCE", "PORT_IMAGE40_SOURCE")})
    options.update(desktop_options(native_ppu=native_ppu, native_records=native_jobs,
                                   studio=native_jobs and not native_ppu, completion=options["PORT_COMPLETION_SOURCE"],
                                   ppu_columns=ppu_columns, media_cpu=media_cpu, video_cpu=native_jobs and not disc,
                                   audio_x86_rtz=audio_x86_rtz, renderer_cpu=renderer_cpu,
                                   renderer_bundle=renderer_bundle, geometry_cpu=geometry_cpu,
                                   geometry_index_cache=geometry_index_cache))
    if native_jobs:
        options.update(native_adapters(analysis, disc, image39_fast, image39_native))
    directory = ROOT / ("build-metal-macos" if macos else "build-vulkan-linux")
    if macos:
        options["CMAKE_OSX_ARCHITECTURES"] = "arm64"
        options["CMAKE_PREFIX_PATH"] = subprocess.check_output(["brew", "--prefix"], text=True).strip()
    # The vendored CMake logic reads fixed offsets from this header.
    # Normalize only the isolated workspace copy when it came from CRLF.
    zconf = ROOT / "third_party/zlib/zconf.h"
    header = zconf.read_bytes()
    reset = []
    if b"\r\n" in header:
        zconf.write_bytes(header.replace(b"\r\n", b"\n"))
        reset = ["-U", "ZLIB_CONF_WRITTEN"]
    run(cmake, "-S", ROOT / "port", "-B", directory, "-G", "Ninja", *reset,
        *(f"-D{k}={v}" for k, v in options.items()))
    run(cmake, "--build", directory, "--parallel", str(jobs))
    exe = directory / "BuzzQuizWorldRecomp"
    # Same build binding as the Windows manifest: launchers select the runtime
    # set from the EXE's own profile, not from the directory it was copied to.
    binding = dict(family=profile.adapter_family)
    if native_jobs and disc:
        binding.update(geometry_job_profile="host-groups-v1", animation_job_profile="host-soa-v1",
                       embedded_runtime_profile=options["PORT_NATIVE_JOB_PROFILE"], runtime_defaults={})
    else:
        plan = subprocess.run([sys.executable, str(ROOT / "tools/verify-game.py"), "--runtime-options"],
                              cwd=ROOT, check=True, capture_output=True, text=True).stdout
        binding.update(runtime_defaults=json.loads(plan))
    (directory / "game-build.json").write_text(json.dumps(dict(
        **binding, renderer="metal" if macos else "vulkan", standalone=True, elf_sha256=profile.elf_sha256,
        exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        native_jobs=native_jobs, renderer_cpu=renderer_cpu, geometry_cpu=geometry_cpu, media_cpu=media_cpu,
        spurs_input_clear=spurs_input_clear, spu_job_return=spu_job_return,
        zen2_tune=zen2_tune, ppu_columns=ppu_columns, dma_diagnostics_optin=dma_diagnostics_optin,
        geometry_zero_padding=geometry_zero_padding, geometry_index_cache=geometry_index_cache,
        geometry_scalar_gather=geometry_scalar_gather, audio_x86_rtz=audio_x86_rtz,
        renderer_bundle=renderer_bundle, image39_fast=image39_fast, image39_native=image39_native,
        pgo_profile_sha256=hashlib.sha256(pgo_profile.read_bytes()).hexdigest() if pgo_profile else None,
        cmake_options={key: str(value) for key, value in options.items()})))


def linux_build_defaults(profile):
    disc = profile.adapter_family in ("quizworld-eu100", "quizworld-us100")
    if not disc and profile.adapter_family != "quizworld-102-verified":
        return {}
    try:
        info = Path("/proc/cpuinfo").read_text()
    except OSError:
        info = ""
    family = re.search(r"^cpu family\s*:\s*(\d+)", info, re.M)
    model = re.search(r"^model\s*:\s*(\d+)", info, re.M)
    zen2 = bool("AuthenticAMD" in info and " avx2" in info and family and model and
                (int(family[1]) > 23 or (int(family[1]) == 23 and int(model[1]) >= 0x30)))
    return dict(native_jobs=True, renderer_cpu=True, media_cpu=disc, audio_x86_rtz=disc,
                geometry_zero_padding=disc, dma_diagnostics_optin=True, geometry_scalar_gather=disc,
                geometry_cpu=disc, renderer_bundle=True, image39_native=disc, zen2_tune=zen2)


def main():
    parser = argparse.ArgumentParser(description=__doc__, argument_default=argparse.SUPPRESS)
    parser.add_argument("stage", choices=("generate", "build"))
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--profile", choices=("desktop", "custom"), default=None,
                        help="A plain build uses desktop defaults. Use custom for the reference build or to select individual experiments.")
    parser.add_argument("--native-jobs", action="store_true", help="Opt in to the profile-specific native SPU adapters")
    parser.add_argument("--renderer-cpu", action="store_true", help="Opt in to the retained renderer CPU experiments; benchmark separately")
    parser.add_argument("--geometry-cpu", action="store_true", help="Opt in to exact geometry CPU experiments; requires --native-jobs")
    parser.add_argument("--audio-x86-rtz", action="store_true", help="Experimental checked audio arithmetic with scoped MXCSR; requires --media-cpu")
    parser.add_argument("--media-cpu", action="store_true", help="Opt in to profile-specific media adapters")
    parser.add_argument("--spurs-input-clear", action="store_true", help="Skip zero stores to staged input bytes that are immediately overwritten")
    parser.add_argument("--spu-job-return", action="store_true", help="Opt in to the guarded native SPURS job-return adapter")
    parser.add_argument("--pgo-profile", type=Path, help="Use a private merged Clang profile for runtime/native optimization; requires --native-jobs")
    parser.add_argument("--zen2-tune", action="store_true", help="Tune Linux runtime/native host paths for Zen 2 with -O3, preserving strict FP")
    parser.add_argument("--ppu-columns", choices=("none", "matrices", "rotations", "all"),
                        help="Experimental PPU matrix and rotation kernels for Zen 2")
    parser.add_argument("--geometry-zero-padding", action="store_true", help="Zero only unindexed vertex alignment bytes to avoid padding-only native fallbacks; requires --native-jobs")
    parser.add_argument("--dma-diagnostics-optin", action="store_true", help="Make the DMA poison logging scan opt-in via BUZZ_DMA_POISON_SCAN=1")
    parser.add_argument("--image39-native", action="store_true", help="Image 39 native object bounds + descriptor requests (kernels adapter) and checked FP by default; Deck ABBA: heavy menu +2.5%%, reveal +3.3%%")
    parser.add_argument("--image39-fast", action="store_true", help="Image 39 as one register-promoted host function (generate-image39-fast.py); BUZZ_IMAGE39_FAST=0 keeps the reference lift")
    parser.add_argument("--renderer-bundle", action="store_true", help="Renderer CPU bundle: FIFO method fast path, state packets, pipeline lookup, index prepare, descriptor writes, draw state")
    parser.add_argument("--geometry-index-cache", action="store_true", help="Enable the content-verified decoded index cache alone; requires --native-jobs")
    parser.add_argument("--geometry-scalar-gather", action="store_true", help="Use exact ordinary indexed loads for geometry culling; requires --native-jobs")
    args = parser.parse_args()
    paths = load()
    elf = Path(paths["eboot_elf"])
    profile = identify_elf(hashlib.sha256(elf.read_bytes()).hexdigest())
    if args.stage == "generate":
        generate(elf, profile, args.jobs)
    else:
        options = {k: v for k, v in vars(args).items() if k not in {"stage", "jobs", "profile"}}
        if sys.platform == "darwin":
            if options:
                parser.error("Linux build experiments are unavailable on macOS.")
        elif args.profile == "desktop" or (args.profile is None and not options):
            options = {**linux_build_defaults(profile), **options}
        build(paths, profile, args.jobs, **options)


if __name__ == "__main__":
    main()
