"""Generate and build the standalone Linux Vulkan game in a preparation workspace."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
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


def build(paths, profile, jobs):
    analysis = ROOT / "analysis"
    generation = json.loads((analysis / "game-generation.json").read_text(encoding="utf-8-sig"))
    if generation["elf_sha256"] != profile.elf_sha256 or generation["family"] != profile.adapter_family:
        raise ValueError("The generated translations do not match the selected executable.")
    disc = profile.adapter_family != "quizworld-102-verified"
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
        "BUZZ_EXPERIMENTAL_VULKAN": "ON", "PORT_SPU_ISA": "avx2",
        "PORT_NATIVE_SPU_FLOAT": "ON", "PORT_RTZ_RESULT_GUARD": "ON",
        "PORT_COMPLETION_SOURCE": analysis / "native-completion" / completion,
        "PORT_HOST_PROFILE_DIR": analysis / "host-disc100/port" if disc else "",
        "PORT_MSTREAM_DIR": analysis / "mstream-modules",
        "CMAKE_C_COMPILER": "clang", "CMAKE_CXX_COMPILER": "clang++",
        "CMAKE_BUILD_TYPE": "Release", "CMAKE_C_FLAGS_RELEASE": "-O1 -DNDEBUG", "CMAKE_CXX_FLAGS_RELEASE": "-O1 -DNDEBUG",
    }
    directory = ROOT / "build-vulkan-linux"
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
    (directory / "game-build.json").write_text(json.dumps(dict(
        renderer="vulkan", standalone=True, elf_sha256=profile.elf_sha256,
        exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=("generate", "build"))
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    paths = load()
    elf = Path(paths["eboot_elf"])
    profile = identify_elf(hashlib.sha256(elf.read_bytes()).hexdigest())
    if args.stage == "generate":
        generate(elf, profile, args.jobs)
    else:
        build(paths, profile, args.jobs)


if __name__ == "__main__":
    main()
