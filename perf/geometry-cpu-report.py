"""Validate and summarize a reversed single-scene native-kernel CPU series."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics

spec = importlib.util.spec_from_file_location("process_work", Path(__file__).with_name("measure-process-work.py"))
work = importlib.util.module_from_spec(spec)
spec.loader.exec_module(work)


def report(progress):
    raw = progress.read_bytes()
    events = [json.loads(line) for line in raw.decode("utf-8-sig").splitlines() if line.strip()]
    starts = [e for e in events if e["state"] == "start"]
    runs = [e for e in events if e["state"] == "done"]
    if len(starts) != len(runs) or len(runs) not in (4, 6, 8) or len(events) != 2*len(runs):
        raise ValueError("Need a complete single-scene reversed CPU series")
    if len({e["tag"] for e in starts}) != len(runs) or [e["tag"] for e in starts] != [e["tag"] for e in runs]:
        raise ValueError("Duplicate or incomplete CPU runs")
    for field in ("scene", "exe_sha256", "save_sha256", "cpu_binding_sha256", "cpu_tool_sha256", "dump_every"):
        if len({e[field] for e in events}) != 1:
            raise ValueError(f"CPU series differs in {field}")
    if runs[0]["dump_every"]:
        raise ValueError("CPU series requires readbacks disabled in every arm")
    baseline = runs[0]["mask"]
    arms = len(runs)//2-1
    targets = [r["mask"] for r in runs[1:1+arms]]
    bits = [target ^ baseline for target in targets]
    if len(set(targets)) != arms or any(not bit or bit & (bit-1) or bit & baseline or target != baseline | bit
                                      for bit, target in zip(bits, targets)):
        raise ValueError("CPU targets must add one independent bit")
    if [r["mask"] for r in runs] != [baseline] + targets + list(reversed(targets)) + [baseline]:
        raise ValueError("CPU series is not symmetric")
    summaries = []
    expected_window = {"Menu": [40, 98], "Round": [150, 220]}[runs[0]["scene"]]
    methods = set()
    for start, run in zip(starts, runs):
        for field in ("tag", "scene", "mask", "exe_sha256", "save_sha256", "cpu_binding_sha256", "cpu_tool_sha256", "dump_every"):
            if start[field] != run[field]:
                raise ValueError("Run metadata changed")
        cpu_path = progress.parent / f"{run['tag']}.cpu.json"
        cpu_raw = cpu_path.read_bytes()
        if hashlib.sha256(cpu_raw).hexdigest() != run["cpu_sha256"]:
            raise ValueError("CPU file differs from completed-run hash")
        cpu = json.loads(cpu_raw)
        methods.add(cpu["method"])
        if (cpu["status"] != "complete" or cpu["exe_sha256"].lower() != run["exe_sha256"].lower() or
                cpu["binding_sha256"].lower() != run["cpu_binding_sha256"].lower() or
                cpu["window_seconds"] != expected_window):
            raise ValueError("CPU executable, binding or window mismatch")
        if len(cpu["snapshots"]) != 2 or any(abs(s["elapsed_ns"]/1e9-t) > .05
                                             for s, t in zip(cpu["snapshots"], expected_window)):
            raise ValueError("CPU snapshots missed the scene window")
        # Recompute the counters; do not trust a cached normalized result.
        measured = work.summarize(*cpu["snapshots"])
        summaries.append(dict(tag=run["tag"], mask=run["mask"], cpu_sha256=run["cpu_sha256"], **measured))
    if len(methods) != 1:
        raise ValueError("CPU observer methods differ")
    kernels = {}
    for bit, target in zip(bits, targets):
        selected = [s for s in summaries if s["mask"] in (baseline, target)]
        metrics = {}
        for owner in ("pool", "process"):
            metrics[owner] = {}
            for metric in ("cycles_per_frame", "cpu_ms_per_frame"):
                off = statistics.mean(selected[i][owner][metric] for i in (0, 3))
                on = statistics.mean(selected[i][owner][metric] for i in (1, 2))
                if off <= 0 or any(selected[i][owner][metric] <= 0 for i in (0, 3)):
                    raise ValueError("CPU baseline has no measured work")
                metrics[owner][metric] = dict(baseline=off, native=on, change_percent=100*(on/off-1),
                    pair_changes_percent=[100*(selected[b][owner][metric]/selected[a][owner][metric]-1)
                                          for a, b in ((0, 1), (3, 2))])
        kernels[f"0x{bit:X}"] = dict(native_mask=target, runs=selected, **metrics)
    return dict(progress=str(progress), progress_sha256=hashlib.sha256(raw).hexdigest(),
                scene=runs[0]["scene"], exe_sha256=runs[0]["exe_sha256"],
                binding_sha256=runs[0]["cpu_binding_sha256"], save_sha256=runs[0]["save_sha256"],
                cpu_tool_sha256=runs[0]["cpu_tool_sha256"], method=methods.pop(),
                window_seconds=expected_window, baseline_mask=baseline, shared_reference=True, kernels=kernels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("progress", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = report(args.progress)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2); stream.write("\n")
    for bit, entry in result["kernels"].items():
        for owner in ("pool", "process"):
            for name, values in entry[owner].items():
                print(f"{bit} {owner} {name}: {values['baseline']:.3f} -> {values['native']:.3f} "
                      f"({values['change_percent']:+.2f}%), pairs " +
                      ", ".join(f"{v:+.2f}%" for v in values["pair_changes_percent"]))


if __name__ == "__main__":
    main()
