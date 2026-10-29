"""Add scoped, non-mutating diagnostics to a generated TU in a separate folder."""
from pathlib import Path

ROOT=Path(__file__).resolve().parent
TARGETS=(0x00305550,0x00305470,0x00305A08,0x00305C04,0x0030617C,0x00306ABC,0x00306AFC,0x00306FAC,0x00306690,0x00307240)
CODEC_TARGETS=(0x0048EB0C,0x0049229C,0x00494A6C,0x00496130,0x00496990,0x004AB3E8)
WORKER_TARGETS=(0x00484F94,0x00486034,0x0048605C,0x00486084)
SOURCES=(('ppu_recomp_019.cpp',TARGETS),('ppu_recomp_027.cpp',WORKER_TARGETS),('ppu_recomp_028.cpp',CODEC_TARGETS))


def instrument(source,targets=TARGETS):
    if 'BuzzVideoProbeScope' in source:
        raise ValueError("Input is already instrumented")
    for pc in targets:
        marker=f"void func_{pc:08X}(ppu_context* ctx) {{"
        if source.count(marker)!=1:
            raise ValueError(f"Expected exactly one entry {pc:08X}")
        source=source.replace(marker,marker+f"\n        BuzzVideoProbeScope video_probe_scope(ctx,0x{pc:08X});",1)
    include='#include "ppu_recomp.h"'
    if source.count(include)!=1:raise ValueError("Unexpected header include")
    return source.replace(include,include+'\n#include "../../port/video_probe.h"',1)


def main():
    for name,targets in SOURCES:
        output=ROOT/'analysis'/'video-probe'/name
        output.parent.mkdir(parents=True,exist_ok=True)
        content=instrument((ROOT/'port'/'recompiled'/name).read_text(),targets).encode()
        if not output.exists() or output.read_bytes()!=content:output.write_bytes(content)
        print(output)


if __name__=='__main__':main()
