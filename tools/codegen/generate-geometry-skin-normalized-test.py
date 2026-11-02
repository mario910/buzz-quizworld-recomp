"""Pinned complete normalized skin directions data oracle; all derived code stays private."""
import hashlib
import importlib.util
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
import sys;sys.path.insert(0,str(ROOT/'tools/codegen'));from geometry_fixture import text_sha256
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PARENT_PIN="84de9d0c5b2788aa74f3e6756ed4e9ea03ada7bad8b047d272f50ec2f29bbe97"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed normalized skin directions source")
    parent=ROOT/"tools/codegen/generate-geometry-phase2.py"
    if text_sha256(parent)!=PARENT_PIN:raise SystemExit("Unreviewed normalized skin directions constants")
    spec=importlib.util.spec_from_file_location("skin_normalized_constants",parent)
    constants=importlib.util.module_from_spec(spec);spec.loader.exec_module(constants)
    functions={int(pc,16):body for pc,body in re.findall(r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0xc14c,0xc154,0xc158,0xc280,0xc288,0xc290,0xb5a0,0xb5a8,0xb5b0}
    references={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if references-selected!={0xb868}:raise SystemExit("Unreviewed normalized skin directions boundaries")
    output="namespace skin_normalized_oracle {\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(references|selected))
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{abort();}}" for pc in sorted(references-selected))
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output+="\nstatic void seed_constants(spu_context& c){\n"
    for reg,words in constants.SKIN.items():
        for lane,word in enumerate(words):output+=f"c.gpr[{reg}]._u32[{lane}]={word:#x}u;\n"
    fixture=(ROOT/"analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if hashlib.sha256(fixture).hexdigest()!="99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f":raise SystemExit("Unreviewed LS table fixture")
    ls=fixture[2048:]
    quad=lambda at:[int.from_bytes(ls[at+i:at+i+4],"big") for i in range(0,16,4)]
    r91=quad(0xd930);r92=quad(0xd920);r45=quad(0xda70)
    extra={70:[0,0,0xffffffff,0xffffffff],78:[0,0xffffffff,0xffffffff,0],91:r91,92:r92,79:quad(0xd940)[2:]+quad(0xd940)[:2],72:r91[2:]+r45[2:],73:[x^0x10101010 for x in quad(0xdaa0)],74:[x|0x0c0c0c0c for x in r92],75:[x|0x08080808 for x in r92],76:[x|0x04040404 for x in r92]}
    for reg,words in extra.items():
        for lane,word in enumerate(words):output+=f"c.gpr[{reg}]._u32[{lane}]={word:#x}u;\n"
    output+="}\nstatic buzz_geometry_job_fn roots[]={"+",".join(f"{PREFIX}{pc:08X}" for pc in (0xc14c,0xc280,0xb5a0))+"};\n}\n"
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-skin-normalized-reference.inc").write_text(output,encoding="utf-8")
    print("Pinned normalized skin directions oracle:9 complete original blocks; pinned LS shuffle tables; B868 boundary trapped")


if __name__=="__main__":main()
