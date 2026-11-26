"""Build the packaged desktop game host with synthetic guest functions."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("sources", type=Path)
parser.add_argument("build", type=Path)
args = parser.parse_args()
sources = args.sources.resolve()
build = args.build.resolve()
lift = build / "lift"
lift.mkdir(parents=True)
spu = build / "spu"
(spu / "buzz_job_6eba").mkdir(parents=True)
(spu / "buzz_job_6eba/spu_recomp.c").write_text("/* Synthetic SPU image */\n")
sys.path.insert(0, str(sources / "ps3recomp/tools"))
from ppu_lifter import HEADER_PREAMBLE

# These Lua hooks are the only named guest calls used by the reference host.
hooks = ("0046B8F8", "0046B388", "0047CD10", "0046C3F0", "0046B3A0")
(lift / "ppu_recomp.h").write_text(HEADER_PREAMBLE + "".join(
    f"void func_{pc}(ppu_context*);\n" for pc in hooks), encoding="utf-8")
(lift / "fixture.cpp").write_text('#include "ppu_recomp.h"\n' + "".join(
    f"void func_{pc}(ppu_context*) {{}}\n" for pc in hooks) +
    "const func_entry function_table[] = {{0, nullptr, nullptr}};\n"
    "const uint64_t function_table_count = 0;\n", encoding="utf-8")

sys.path.insert(0, str(sources))
from tools.desktop_adapters import desktop_options

# The fixture supplies the guest entry points. The port and renderer are real.
windows = sys.platform == "win32"
flags = desktop_options(renderer_cpu=windows, renderer_bundle=windows, geometry_cpu=windows)
if windows:
    flags["PORT_PPU_KERNEL_DIR"] = lift  # Also compile the native studio command handler.
    platform_flags = [
        "-DCMAKE_C_COMPILER=clang-cl", "-DCMAKE_CXX_COMPILER=clang-cl",
        "-DBUZZ_EXPERIMENTAL_VULKAN=ON", "-DBUZZ_DMA_DIAGNOSTICS_OPTIN=ON",
        "-DPORT_SPU_ISA=avx2", "-DPORT_NATIVE_SPU_FLOAT=ON", "-DPORT_RTZ_RESULT_GUARD=ON",
    ]
else:
    platform_flags = ["-DCMAKE_OSX_ARCHITECTURES=arm64"]
subprocess.run(["cmake", "-S", sources / "port", "-B", build, "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release", "-DBUZZ_STANDALONE=ON",
                f"-DPS3RECOMP_DIR={sources / 'ps3recomp'}", f"-DRECOMP_DIR={lift}", f"-DSPU_DIR={spu}",
                *platform_flags,
                *(f"-D{k}={v}" for k, v in flags.items())], check=True)
subprocess.run(["cmake", "--build", build, "--target", "BuzzQuizWorldRecomp", "--parallel", "4"], check=True)
exe = build / ("BuzzQuizWorldRecomp.exe" if windows else "BuzzQuizWorldRecomp")
if not windows:
    assert subprocess.check_output(["lipo", "-archs", exe], text=True).strip() == "arm64"
    result = subprocess.run([exe], cwd=sources, text=True, capture_output=True)
    assert result.returncode != 0 and "runtime folder is missing" in result.stderr, result.stderr
env = os.environ.copy()
for key in ("BUZZ_STUDIO_DETAIL", "BUZZ_PRESENT_HOLD_MS"):
    env.pop(key, None)
for overrides in ({}, {"BUZZ_STUDIO_DETAIL": "original", "BUZZ_PRESENT_HOLD_MS": "8"}):
    output = subprocess.check_output([exe, "--runtime-defaults"], env={**env, **overrides}, text=True)
    expected = {"BUZZ_STUDIO_DETAIL": "balanced", "BUZZ_PRESENT_HOLD_MS": "0", **overrides}
    for key, value in expected.items():
        assert f"{key}={value}" in output.splitlines()
