"""Pinned image 39 arithmetic adapter, independent opt-in and checked mode.

Only the six scalar-reference FP operations change implementation. Reciprocal
estimates/interpolation/conversions remain their integer reference helpers.
No local-store, register, DMA, branch or job-completion changes are made.
"""
from pathlib import Path
import hashlib
import re
import shutil

root = Path(__file__).resolve().parent
source_dir = root / 'port/spu/buzz_job_c0bc'
raw = (source_dir / 'spu_recomp.c').read_bytes()
if hashlib.sha256(raw).hexdigest() != '599125db31e1195056c4d176e3344cdd453afaab073ae1794d4bdddf7b976854':
    raise SystemExit('Image 39 reference changed: re-audit before generating')
source = raw.decode('utf-8')
if re.search(r'spu_(?:df|fesd|frds|fscr)', source):
    raise SystemExit('Double-precision/FPSCR instruction found: RTZ unsafe')
anchor = '#include "spu_helpers.h"'
if source.count(anchor) != 1:
    raise SystemExit('Unexpected helper include count')
adapter = '''
#define BUZZ_NATIVE_FP_MODE spu_native_image39_fp_mode
#define BUZZ_NATIVE_FP_LABEL "image39-fp"
#include "../../port/native_image40_fp.h"
'''
out = root / 'analysis/native-image39'
out.mkdir(parents=True, exist_ok=True)
(out / 'spu_recomp.c').write_text(source.replace(anchor, anchor + adapter), encoding='utf-8', newline='\n')
shutil.copyfile(source_dir / 'spu_recomp.h', out / 'spu_recomp.h')
print(f'Image 39: pinned reference + shared checked FP adapter -> {out}')
