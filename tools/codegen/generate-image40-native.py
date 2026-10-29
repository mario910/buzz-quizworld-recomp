"""Reproducible opt-in arithmetic adapter; the original lifted TU is untouched."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from pathlib import Path
import hashlib
import re
import shutil

root = Path(__file__).resolve().parents[2]
source_dir = root / "port/spu/buzz_job_abe1"
raw = (source_dir / "spu_recomp.c").read_bytes()
profiles = {
    "29af00779cbaacc132c46947811f43ab2e9ac78564c82952fc6f0e27744b5fed": "6D78",  # 1.02
    "6b4483d57acf7a145d0bf3e4f70c83875f7f67b3dd6931d52a9d980ca65cf7df": "6DB8",  # EU 1.00
}
pc = profiles.get(hashlib.sha256(raw).hexdigest())
if pc is None:
    raise SystemExit("Image 40 reference changed: re-audit before generating")
source = raw.decode("utf-8")
if re.search(r"spu_(?:df|fesd|frds|fscr)", source):
    raise SystemExit("Double-precision/FPSCR instruction found: RTZ unsafe")
anchor = '#include "spu_helpers.h"'
if source.count(anchor) != 1:
    raise SystemExit("Unexpected helper include count")
name = "buzz_job_abe1_spu_func_0000" + pc
match = re.search(r"(?ms)^void " + name + r"\(spu_context\* ctx\) \{(.*?)^\}", source)
if not match:
    raise SystemExit("Missing audited loop 6D78")
body = match[1]
fields = set(re.findall(r"ctx->(\w+)", body))
if fields - {"gpr", "pc"} or re.search(r"spu_(?:wrch|rdch|stop|indirect|drain|halt)", body):
    raise SystemExit("Unexpected loop side effects")
used = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\]", body)})
written = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\] =", body)})
if 0 in written or body.count("return;") != 1:
    raise SystemExit("Loop exit/register contract changed")
local = re.sub(r"ctx->gpr\[(\d+)\]", r"r\1", body)
flush = " ".join(f"ctx->gpr[{n}] = r{n};" for n in written)
local = local.replace("return;", flush + " return;")
decls = "\n".join(f"    u128 r{n} = ctx->gpr[{n}];" for n in used)
kernels = (f"static void {name}_reference(spu_context* ctx) {{{body}}}\n"
           f"static void {name}_locals(spu_context* ctx) {{\n{decls}{local}}}\n")
support = r'''
#include <stdatomic.h>
static int image40_locals_mode;
__attribute__((constructor)) static void image40_locals_init(void) {
    const char* e=getenv("BUZZ_IMAGE40_LOCALS");
    if(e && (e[0]=='1'||e[0]=='2') && !e[1])image40_locals_mode=e[0]-'0';
    fprintf(stderr,"[image40-locals] mode=%d\n",image40_locals_mode);
}
static __attribute__((noinline)) void image40_compare(spu_context* ctx,
    void (*native)(spu_context*),void (*reference)(spu_context*)) {
    spu_context* copy=(spu_context*)malloc(sizeof(*copy));
    if(!copy) {reference(ctx);return;}
    memcpy(copy,ctx,sizeof(*copy));copy->ls=copy->ls_store;
    void (*incoming)(spu_context*)=g_spu_trampoline_fn;
    native(ctx);
    void (*outgoing)(spu_context*)=g_spu_trampoline_fn;
    g_spu_trampoline_fn=incoming;
    reference(copy);
    if(memcmp(ctx->gpr,copy->gpr,sizeof ctx->gpr) ||
       memcmp(ctx->ls,copy->ls,SPU_LS_SIZE) || ctx->pc!=copy->pc ||
       ctx->status!=copy->status || outgoing!=g_spu_trampoline_fn) {
        fprintf(stderr,"[image40-locals] MISMATCH full state 6D78\n");abort();
    }
    g_spu_trampoline_fn=outgoing;free(copy);
}
'''
wrapper = f'''void {name}(spu_context* ctx) {{
    static atomic_uint checked;
    if(!image40_locals_mode || ctx->ls!=ctx->ls_store ||
       g_spu_ls_probe || g_spu_ls_watch_n || g_spu_smc_watch) {{
        {name}_reference(ctx);return;
    }}
    if(image40_locals_mode==2 && atomic_load(&checked)<256) {{
        unsigned n=atomic_fetch_add(&checked,1);
        image40_compare(ctx,{name}_locals,{name}_reference);
        if(n==255)fprintf(stderr,"[image40-locals] 256 full states 6D78 PASS\\n");
    }} else {name}_locals(ctx);
}}'''
source = source[:match.start()] + kernels + wrapper + source[match.end():]
out = root / "analysis/native-image40"
out.mkdir(parents=True, exist_ok=True)
(out / "spu_recomp.c").write_text(
    source.replace(anchor, anchor + '\n#include "../../port/native_image40_fp.h"\n' + support),
    encoding="utf-8", newline="\n")
(out / "test-kernels.inc").write_text(kernels, encoding="utf-8")
shutil.copyfile(source_dir / "spu_recomp.h", out / "spu_recomp.h")
print(f"Image 40: pinned reference + runtime-selectable checked FP adapter -> {out}")
