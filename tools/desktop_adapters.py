"""Generate reviewed desktop adapters and their CMake options."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.regional_ppu import generate_regional


def python(tool, *args):
    subprocess.run([sys.executable, ROOT / tool, *args], cwd=ROOT, check=True)


def desktop_options(*, native_ppu=False, studio=False, completion=None, native_records=False, ppu_columns="none", media_cpu=False, video_cpu=False, audio_x86_rtz=False,
                    renderer_cpu=False, renderer_bundle=False, geometry_cpu=False, geometry_index_cache=False):
    analysis = ROOT / "analysis"
    options = {"PORT_PPU_KERNEL_DIR": ""}
    options["PORT_SPU_RECORDS_DIR"] = ""
    options["PORT_STUDIO_SOURCE"] = ""
    options["PORT_REGIONAL_PPU_SOURCES"] = ""
    options["PORT_REGIONAL_STUDIO"] = "OFF"
    if studio:
        sources, dispatcher = generate_regional(ROOT / "port/recompiled", analysis / "native-regional-ppu", completion, ppu_columns)
        options["PORT_REGIONAL_PPU_SOURCES"] = ";".join(path.as_posix() for path in sources)
        options["PORT_REGIONAL_STUDIO"] = "ON" if dispatcher else "OFF"
    if native_ppu or native_records:
        records_dir = analysis / "native-spu-records"
        python("tools/codegen/generate-spu-records.py", ROOT / "port/spu", records_dir)
        options["PORT_SPU_RECORDS_DIR"] = records_dir
    if native_ppu:
        kernel_dir = analysis / "native-ppu"
        python("tools/codegen/generate-ppu-kernels.py", ROOT / "port/recompiled", kernel_dir, "--ppu-columns", ppu_columns)
        options["PORT_PPU_KERNEL_DIR"] = kernel_dir
    options.update({key: "" for key in ("PORT_VIDEO_IDCT_SOURCE", "PORT_AUDIO_COMPRESSOR_SOURCE",
                    "PORT_AUDIO_MIXER_SOURCE", "PORT_AUDIO_ATRAC_SOURCE")})
    if media_cpu or video_cpu:
        python("tools/codegen/generate-video-idct.py")
        options["PORT_VIDEO_IDCT_SOURCE"] = analysis / "native-video-idct/spu_recomp.c"
    if media_cpu:
        python("tools/codegen/generate-audio-compressor.py", "--include-mixer", "--include-atrac",
               *(["--x86-rtz"] if audio_x86_rtz else []))
        options.update({
            "PORT_AUDIO_COMPRESSOR_SOURCE": analysis / "native-audio-compressor/spu_recomp.c",
            "PORT_AUDIO_MIXER_SOURCE": analysis / "native-audio-mixer/spu_recomp.c",
            "PORT_AUDIO_ATRAC_SOURCE": analysis / "native-audio-atrac/spu_recomp.c",
        })
    for option in ("BUZZ_RENDERER_CPU_REFINEMENTS", "BUZZ_RENDERER_VERTEX_FETCH",
                   "BUZZ_RENDERER_TEXTURE_SNAPSHOT", "BUZZ_RENDERER_DIRECT_DRAW",
                   "BUZZ_RENDERER_CONSTANT_RANGES", "BUZZ_RENDERER_VERTEX_STREAM",
                   "BUZZ_RENDERER_INDEX_FUSION", "BUZZ_RENDERER_PACKED_FUSION",
                   "BUZZ_VK_DESCRIPTOR_BATCH", "BUZZ_VK_UNIFORM_REUSE",
                   "BUZZ_RSX_READY_CHECK", "BUZZ_PPU_WORD_WAIT"):
        options[option] = "ON" if renderer_cpu else "OFF"
    for option in ("BUZZ_GEOMETRY_CULL_COMPACT", "BUZZ_GEOMETRY_INDEX_CACHE",
                   "BUZZ_GEOMETRY_BOUNDED_SKIN", "BUZZ_GEOMETRY_COARSE_CLIP",
                   "BUZZ_GEOMETRY_HOST_COMMANDS"):
        options[option] = "ON" if geometry_cpu else "OFF"
    options["BUZZ_GEOMETRY_INDEX_CACHE"] = "ON" if geometry_cpu or geometry_index_cache else "OFF"
    # Direct CMake profiling builds share this directory. Keep unselected
    # experiments out of a later helper build instead of inheriting cached ONs.
    for option in ("BUZZ_RSX_METHOD_FASTPATH", "BUZZ_RSX_PARK_WAKE",
                   "BUZZ_RENDERER_STATE_PACKETS", "BUZZ_RENDERER_PIPELINE_LOOKUP",
                   "BUZZ_RENDERER_INDEX_PREPARE", "BUZZ_VK_DESCRIPTOR_WRITES",
                   "BUZZ_VK_DRAW_STATE", "BUZZ_VK_BATCH_UPLOAD",
                   "BUZZ_GEOMETRY_INDEX_SEQUENTIAL", "BUZZ_GEOMETRY_INDEX_BATCHED",
                   "BUZZ_GEOMETRY_DEFERRED_CLIP", "BUZZ_GEOMETRY_LAZY_ATTRIBUTES",
                   "BUZZ_RENDERER_WAIT_TRACE", "BUZZ_SPURS_EXECUTION_TRACE",
                   "BUZZ_PRODUCER_DEPENDENCY_TRACE", "BUZZ_PPU_WORD_WAIT_TRACE"):
        options[option] = "OFF"
    # Deck ABBA (2026-11-13): menu +0.9%, question rounds +1.7%, identical captures.
    if renderer_bundle:
        for option in ("BUZZ_RSX_METHOD_FASTPATH", "BUZZ_RENDERER_STATE_PACKETS",
                       "BUZZ_RENDERER_PIPELINE_LOOKUP", "BUZZ_RENDERER_INDEX_PREPARE",
                       "BUZZ_VK_DESCRIPTOR_WRITES", "BUZZ_VK_DRAW_STATE"):
            options[option] = "ON"
    return options


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", action="store_true")
    parser.add_argument("--native-video", action="store_true")
    parser.add_argument("--studio", action="store_true")
    parser.add_argument("--completion", type=Path)
    parser.add_argument("--renderer", action="store_true")
    parser.add_argument("--optimized-vulkan", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    optimized = args.optimized_vulkan
    renderer = args.renderer or optimized
    options = desktop_options(native_ppu=args.scene, studio=args.studio, completion=args.completion,
                              native_records=optimized or args.native_video, media_cpu=optimized, video_cpu=args.native_video,
                              audio_x86_rtz=optimized,
                              renderer_cpu=renderer, renderer_bundle=renderer,
                              geometry_cpu=optimized)
    args.output.write_text("".join(f"-D{key}={value}\n" for key, value in options.items()), encoding="utf-8")
