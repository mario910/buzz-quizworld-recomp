"""Check every expanded trace DMA against the ordered corpus transfer stream."""
import argparse
import collections
import csv
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location("corpus", Path(__file__).with_name("corpus-inspect.py"))
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("corpus", type=Path)
    p.add_argument("prefix", type=Path)
    p.add_argument("--map", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    files = sorted(a.corpus.glob("*.bjc.gz"))
    if not files:
        raise SystemExit("No corpus jobs found")
    rows = collections.defaultdict(list)
    with Path(str(a.prefix) + ".dma.csv").open() as f:
        for row in csv.DictReader(f):
            rows[row["job"]].append(row)
    issue_counts, transfer_counts = collections.Counter(), collections.Counter()
    with Path(str(a.prefix) + ".commands.csv").open() as f:
        for row in csv.DictReader(f):
            issue_counts[(row["pc"], row["cmd"])] += 1
    n_jobs = n_transfers = 0
    failures = []
    for path in files:
        job = corpus.load(path)
        actual = rows.pop(path.name, [])
        expected = job["transfers"]
        if len(actual) != len(expected):
            failures.append(f"{path.name}: {len(actual)} trace / {len(expected)} corpus transfers")
            continue
        n_jobs += 1
        for i, (r, (kind, lsa, ea, size, _)) in enumerate(zip(actual, expected)):
            cmd = int(r["cmd"], 16)
            kinds = {0x20: (2,), 0x21: (2,), 0x22: (2,), 0x40: (1,), 0x41: (1,),
                     0x42: (1,), 0xd0: (3,), 0xb0: (6,), 0xb8: (6,), 0xb4: (4, 5)}
            if kind not in kinds.get(cmd, ()) or (lsa, ea, size) != (int(r["lsa"], 16), int(r["ea"], 16), int(r["size"])):
                failures.append(f"{path.name} transfer {i}: trace {r} != corpus {(kind, lsa, ea, size)}")
                break
            n_transfers += 1
            transfer_counts[(r["pc"], r["cmd"])] += 1
    if rows:
        failures.append(f"{len(rows)} trace jobs absent from corpus")
    mapping = json.loads(a.map.read_text())
    possible = {pc.lstrip("0") or "0" for pc, b in mapping["blocks"].items()
                if any(c.startswith("MFC_Cmd,") for c in b["channels"])}
    observed = {pc for pc, cmd in issue_counts}
    result = dict(jobs=n_jobs, transfers=n_transfers, failures=failures,
        commands=[dict(pc=pc, cmd=cmd, count=n) for (pc, cmd), n in sorted(issue_counts.items())],
        expanded_transfers=[dict(pc=pc, cmd=cmd, count=n) for (pc, cmd), n in sorted(transfer_counts.items())],
        static_issue_blocks=sorted(possible), observed_issue_blocks=sorted(observed),
        unobserved_issue_blocks=sorted(possible - observed), source_sha256=mapping["source_sha256"])
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{n_jobs} jobs, {n_transfers} transfers, {len(failures)} failures; {len(observed)}/{len(possible)} static issue blocks exercised")
    if failures:
        print("\n".join(failures[:5]))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
