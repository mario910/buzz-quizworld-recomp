"""Add read-only function scopes to pinned 1.02 PPU lifts in analysis/ only."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
import argparse
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
SOURCES={
    'ppu_recomp_015.cpp':('2d85024cd4c04ced54cca4685e9ee6f7e5025bb30a50382b565cc6f7a9f8a680',(0x26BEEC,0x26A7C4,0x26BAB4,0x26BE44,0x26C258)),
    'ppu_recomp_016.cpp':('5f3720457aa8eb2e8c997c49533c098fcb1c90eb31bc4e07d8921c20c8cfb34b',(0x28A61C,0x2898A4,0x28A3E0,0x2876A0,0x28ABA8,0x28B1AC)),
}
INCLUDE='\n#include "../../port/producer_probe.h"'


def body(source,pc):
    match=re.search(rf'(?ms)^void func_{pc:08X}\(ppu_context\* ctx\) \{{.*?^\}}',source)
    if not match:raise ValueError(f'Missing function {pc:08X}')
    return match.group()


def instrument(source,targets):
    if 'BuzzProducerProbeScope' in source:raise ValueError('Already instrumented')
    header='#include "ppu_recomp.h"'
    if source.count(header)!=1:raise ValueError('Unexpected header count')
    original=source
    source=source.replace(header,header+INCLUDE,1)
    insertions=[]
    for pc in targets:
        marker=f'void func_{pc:08X}(ppu_context* ctx) {{'
        if source.count(marker)!=1:raise ValueError(f'Ambiguous function {pc:08X}')
        insertion=f'\n        BuzzProducerProbeScope producer_probe_scope(ctx,0x{pc:08X});'
        source=source.replace(marker,marker+insertion,1)
        insertions.append(insertion)
    # Verify byte-for-byte round trip: no lifted instruction was replaced.
    restored=source.replace(INCLUDE,'',1)
    for insertion in insertions:restored=restored.replace(insertion,'',1)
    if restored!=original:raise ValueError('Instrumentation changed the original body')
    return source


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--completion-source',type=Path)
    args=parser.parse_args()
    destination=ROOT/'analysis/producer-probe';destination.mkdir(exist_ok=True)
    for name,(expected,targets) in SOURCES.items():
        reference=(ROOT/'port/recompiled'/name).read_text()
        if hashlib.sha256(reference.encode()).hexdigest()!=expected:
            raise ValueError(f'{name}: reference changed; re-audit before probing')
        source=args.completion_source.read_text() if name.endswith('015.cpp') and args.completion_source else reference
        for pc in targets:
            candidate=body(source,pc)
            if pc==0x26BEEC and args.completion_source:
                allowed='        if (buzz_wait_completed_slot(ctx)) goto loc_0026BFB8;\n'
                if candidate.count(allowed)!=1:raise ValueError('Missing audited queue adapter')
                candidate=candidate.replace(allowed,'',1)
            if candidate!=body(reference,pc):raise ValueError(f'{pc:08X}: adapter changed the target body')
        output=destination/name;content=instrument(source,targets).encode()
        if not output.exists() or output.read_bytes()!=content:output.write_bytes(content)
        print(f'Producer scope diagnostic: {output}')


if __name__=='__main__':main()
