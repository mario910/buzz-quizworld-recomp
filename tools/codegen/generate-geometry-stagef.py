"""Route image 41's hot SPU kernels through port/native_stagef.cpp.

Leaf kernels (entered by brsl or a tail jump, leaving through `bi $0`):
  0x6338  stage-F vertex loop (0x6338/0x63E8), entered from 0x60E8/0x653C/0x654C
  0xC3A0  float4 array x 4x4 matrix (loop 0xC488), called from 0x33C/0x400/0x454
  0x2C10  normalize an array of vectors (loop 0x2D00), called from 0x470/0x12E0
For each, the generated translation unit keeps the lifted body as
`..._reference` and puts a wrapper under the original name that hands the
call to buzz_stagef_run(ctx, reference, native, kernel). The native code
(port/native_stagef_loop.h) is included into this translation unit so it is
compiled with the lift's own float helpers and flags.

The cull loop (0x6D28 -> 0x6E00/0x6DF8 -> 0x6888) is a mid-function region:
its entry wrapper routes to buzz_stagef_cull_run. Analysis probes sit on the
cull loop's ends and on the normal decoder case (0x8FD4 -> 0x82B8).

Reproducible derived output: never edit port/spu. Modes are chosen at run
time (BUZZ_STAGEF, BUZZ_STAGEF_KERNELS; see native_stagef.cpp).
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from pathlib import Path
import re
import hashlib

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba"
DEST = ROOT / "analysis/stagef-geometry"
PREFIX = "buzz_job_6eba_spu_func_0000"
# (entry pc, native function, kernel id) -- the id is a bit in BUZZ_STAGEF_KERNELS
KERNELS = (("6338", "buzz_stagef_native_loop", 0),
           ("C3A0", "buzz_stagef_native_xform", 1),
           ("2C10", "buzz_stagef_native_normalize", 2))

source = (SOURCE / "spu_recomp.c").read_text()
digest = hashlib.sha256((SOURCE / "spu_recomp.c").read_bytes()).hexdigest()
if digest not in {"5ae9232d4f2d47004b687251b479b4515c1f2ea46e9f868332e3305a4a13434e",
                  "7727abc0b151a75b9d480102192f2aac6b623da117d23e20662b8b7e02b3bd82"}:
    raise SystemExit("Unreviewed geometry image; do not infer the native ABI from addresses")
disc100 = digest == "5ae9232d4f2d47004b687251b479b4515c1f2ea46e9f868332e3305a4a13434e"
if disc100:
    # The vertex algorithm is shared; its input register allocation is mapped
    # explicitly in the native header. Cull stays reference until reviewed.
    KERNELS = (("63E0", "buzz_stagef_native_loop", 0),
               ("C518", "buzz_stagef_native_xform", 1),
               ("2C00", "buzz_stagef_native_normalize", 2))
header = (ROOT / "port/native_stagef_loop.h").as_posix()
included = False
for pc, native, kernel in sorted(KERNELS, key=lambda k: int(k[0], 16)):
    name = PREFIX + pc
    match = re.search(rf"(?m)^void {name}\(spu_context\* ctx\) \{{", source)
    if not match:
        raise SystemExit(f"missing {name}")
    body_end = source.index("\n}", match.end())
    body = source[match.end():body_end]
    # Only LS and registers: any channel or DMA access would widen the contract.
    if re.search(r"spu_(?:wrch|rdch|stop|mfc)", body):
        raise SystemExit(f"0x{pc} has side effects beyond LS and registers")
    wrapper = ""
    if not included:   # the first kernel in address order precedes every user
        if disc100:
            wrapper += "\n#define BUZZ_STAGEF_DISC100 1\n"
        wrapper += f'\n#include "{header}"\n'
        wrapper += ("extern void buzz_stagef_run(spu_context* ctx, void (*reference)(spu_context*),\n"
                    "                            int (*native)(spu_context*), int kernel);\n"
                    "extern int buzz_stagef_mode;\n")
        included = True
    wrapper += f"""void {name}(spu_context* ctx) {{
    if (!buzz_stagef_mode) {{ {name}_reference(ctx); return; }}
    buzz_stagef_run(ctx, {name}_reference, {native}, {kernel});
}}
"""
    source = (source[:match.start()] + f"void {name}_reference(spu_context* ctx) {{"
              + body + "\n}\n" + wrapper + source[body_end + 2:])

for pc in (("6E08",) if disc100 else ("6D28", "6888", "8FD4", "82B8")):
    probe = PREFIX + pc
    m = re.search(rf"(?m)^void {probe}\(spu_context\* ctx\) \{{", source)
    if not m:
        raise SystemExit(f"missing {probe}")
    route = ""
    if pc in ("6D28", "6E08"):
        exit_pc, base_reg, kept_reg = ("6958", 37, 50) if disc100 else ("6888", 39, 51)
        route = (f"    if (buzz_stagef_mode) {{ buzz_stagef_cull_run(ctx, {probe}_body,"
                 f" buzz_stagef_native_cull, {PREFIX}{exit_pc}, {base_reg}, {kept_reg}); return; }}\n")
    source = (source[:m.start()] + "extern void buzz_stagef_probe(spu_context*, unsigned);\n"
              "extern void buzz_stagef_cull_run(spu_context*, void (*)(spu_context*),\n"
              "    int (*)(spu_context*), void (*)(spu_context*), unsigned, unsigned);\n"
              "extern int buzz_stagef_probe_on;\n"
              f"static void {probe}_body(spu_context* ctx);\n"
              f"void {probe}(spu_context* ctx) {{\n"
              + ("" if disc100 else f"    if (buzz_stagef_probe_on) buzz_stagef_probe(ctx, 0x{pc});\n")
              + route +
              f"    {probe}_body(ctx);\n}}\n"
              f"static void {probe}_body(spu_context* ctx) {{" + source[m.end():])
DEST.mkdir(parents=True, exist_ok=True)
(DEST / "spu_recomp.c").write_text(source)
(DEST / "spu_recomp.h").write_text((SOURCE / "spu_recomp.h").read_text())
print(f"Generated stage F wrappers ({', '.join(k[0] for k in KERNELS)}) in {DEST}")
