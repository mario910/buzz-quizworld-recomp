"""Find relocation-only candidates between locally lifted SPU images.

Analysis only: this does not authorize or generate native replacements. Data
addresses, register numbers, instruction immediates and LS offsets are retained.
Only code symbols, local labels and explicit ctx->pc assignments are normalized.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re


def functions(path):
    text = Path(path).read_text(encoding="utf-8")
    return dict(re.findall(r"(?ms)^void (\w+_spu_func_[0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", text))


def canonical(body):
    names = {}
    def symbol(match):
        token = match.group()
        if token not in names:
            names[token] = f"code_{len(names)}"
        return names[token]
    body = re.sub(r"\w+_spu_func_[0-9A-F]+|loc_[0-9A-F]+", symbol, body)
    return re.sub(r"ctx->pc = 0x[0-9a-fA-F]+;", "ctx->pc = CODE_PC;", body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("pcs", nargs="+")
    args = parser.parse_args()
    old, new = functions(args.old), functions(args.new)
    normalized = {name: canonical(body) for name, body in new.items()}
    report = []
    for pc in args.pcs:
        name, body = next((name, body) for name, body in old.items() if name.endswith(f"_{int(pc,16):08X}"))
        key = canonical(body)
        exact = [name for name, candidate in normalized.items() if candidate == key]
        candidates = []
        if not exact:
            shortlist = sorted(normalized, key=lambda n: abs(len(normalized[n])-len(key)))[:40]
            candidates = sorted(((difflib.SequenceMatcher(None, key.splitlines(), normalized[n].splitlines()).ratio(), n)
                                 for n in shortlist), reverse=True)[:3]
        report.append(dict(old=name, body_sha256=hashlib.sha256(body.encode()).hexdigest(),
                           exact=exact, nearest=candidates))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
