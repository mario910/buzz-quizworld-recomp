"""Pinned complete weighted skin data oracle; all derived code stays private."""
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
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed weighted skin source")
    parent=ROOT/"tools/codegen/generate-geometry-phase2.py"
    if text_sha256(parent)!=PARENT_PIN:raise SystemExit("Unreviewed weighted skin constants")
    spec=importlib.util.spec_from_file_location("weighted_skin_constants",parent)
    constants=importlib.util.module_from_spec(spec);spec.loader.exec_module(constants)
    functions={int(pc,16):body for pc,body in re.findall(r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0xb880,0xb934,0xb940}
    references={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if references-selected!={0xb4c0,0xbb58}:raise SystemExit("Unreviewed weighted skin boundaries")
    output="namespace weighted_skin_oracle {\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(references|selected))
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{abort();}}" for pc in sorted(references-selected))
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output+="\nstatic void seed_constants(spu_context& c){\n"
    for reg,words in constants.SKIN.items():
        for lane,word in enumerate(words):output+=f"c.gpr[{reg}]._u32[{lane}]={word:#x}u;\n"
    output+="}\n}\n"
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-weighted-skin-reference.inc").write_text(output,encoding="utf-8")
    print("Pinned weighted skin oracle:3 complete original blocks;two exact data continuations trapped")


if __name__=="__main__":main()
