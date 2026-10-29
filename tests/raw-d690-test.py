"""Regression checks for the newly reached gameplay SPURS image.

No generation or game execution: inspect the captured bytes and generator
catalog without importing the generator (which has intentional write effects).
"""
import ast
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ps3recomp/tools"))
from spu_disasm import disassemble_spu
from find_spu_functions import collect_branch_targets

tree = ast.parse((ROOT / "generate-raw-jobs.py").read_text())
catalog = next(ast.literal_eval(n.value) for n in tree.body
               if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == "captured" for t in n.targets))
assert catalog[-1] == ("buzz_job_d690", "D6902C586B38C6E9", 38112, 0x5198)
assert 33 + len(catalog) - 1 == 44, "Do not renumber existing SPU images"
path = ROOT / "analysis/raw-jobs/runtime/spujob_D6902C586B38C6E9_38112.bin"
data = path.read_bytes()
assert len(data) == 38112
assert hashlib.sha256(data).hexdigest() == "fd1b89879e5952c8b66eae79c000895dc8a0e840a362912842810d12915bb0f1"
# The SDK registry uses this historic seed, not standard FNV's offset basis.
h = 1469598103934665603
for b in data:
    h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
assert h == 0xD6902C586B38C6E9
end = 0x5198
code = list(disassemble_spu(data[:end], 0))[4:]
assert all(i.mnemonic not in (".word", "???") for i in code)
assert all(0x10 <= a < end for a in collect_branch_targets(code))
assert code[-2].addr == 0x5190 and code[-2].mnemonic == "br"
assert code[-1].addr == 0x5194 and code[-1].mnemonic == "lnop"
registered = (ROOT / "port/spu/jobmods_register.c").read_text()
assert '0xD6902C586B38C6E9ULL, buzz_job_d690_spu_func_00000010, 44, "buzz_job_d690"' in registered
print("PASS: raw image 44 fingerprint/SHA256, stable catalog ID, decoded code boundary + branch targets, registration")
