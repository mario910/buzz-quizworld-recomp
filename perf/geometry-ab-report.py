"""Summarize a complete same-EXE menu/round A-B-B-A measurement set."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics


def summarize(path, low, high):
    raw = path.read_bytes()
    all_rows = list(csv.DictReader(raw.decode().splitlines()))
    if not all_rows or float(all_rows[-1]["elapsed_ms"]) < high * 1000:
        raise ValueError(f"Incomplete timing window: {path}")
    rows = [r for r in all_rows if low*1000 <= float(r["elapsed_ms"]) < high*1000]
    if len(rows) < 2:
        raise ValueError(f"No useful frames: {path}")
    elapsed = [float(r["elapsed_ms"]) for r in rows]
    if any(b <= a for a, b in zip(elapsed, elapsed[1:])):
        raise ValueError(f"Nonmonotonic timestamps: {path}")
    intervals = sorted(float(r["interval_ms"]) for r in rows)
    return dict(csv=str(path), csv_sha256=hashlib.sha256(raw).hexdigest(), frames=len(rows),
                fps=(len(rows)-1)*1000/(elapsed[-1]-elapsed[0]),
                median_ms=statistics.median(intervals), p90_ms=intervals[int(.9*(len(rows)-1))],
                p99_ms=intervals[int(.99*(len(rows)-1))],
                mean_drain_ms=statistics.mean(float(r["drain_ms"]) for r in rows))


def report(progress, csv_directory, windows):
    events = [json.loads(line) for line in progress.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    starts = {r["tag"]: r for r in events if r["state"] == "start"}
    done = [r for r in events if r["state"] == "done"]
    if len(starts) != 8 or len(done) != 8 or len({r["tag"] for r in done}) != 8:
        raise ValueError("Need all eight completed runs, without duplicates")
    hashes = {r["exe_sha256"].lower() for r in events}
    if len(hashes) != 1:
        raise ValueError("A/B runs used different executables")
    result = dict(exe_sha256=hashes.pop(), scenes={})
    masks = None
    for scene in ("Menu", "Round"):
        runs = [r for r in done if r["scene"] == scene]
        if len(runs) != 4 or [r["mask"] for r in runs] != [runs[0]["mask"], runs[1]["mask"], runs[1]["mask"], runs[0]["mask"]] or runs[0]["mask"] == runs[1]["mask"]:
            raise ValueError(f"Need A-B-B-A order in {scene}")
        scene_masks = [runs[0]["mask"], runs[1]["mask"]]
        if masks is not None and masks != scene_masks:
            raise ValueError("Scenes use different kernel masks")
        masks = scene_masks
        summaries = []
        for r in runs:
            if any(starts[r["tag"]][key] != r[key] for key in ("mask", "scene", "exe_sha256")):
                raise ValueError("Run metadata changed between start and end")
            summaries.append(dict(tag=r["tag"], mask=r["mask"], **summarize(csv_directory/f"vk-scene-{r['tag']}.csv", *windows[scene])))
        pairs = []
        for off, on in ((0, 1), (3, 2)):
            pairs.append(dict(baseline_fps=summaries[off]["fps"], native_fps=summaries[on]["fps"],
                              change_percent=100*(summaries[on]["fps"]/summaries[off]["fps"]-1)))
        baseline = statistics.mean(summaries[i]["fps"] for i in (0, 3))
        native = statistics.mean(summaries[i]["fps"] for i in (1, 2))
        result["scenes"][scene] = dict(window_seconds=windows[scene], runs=summaries, pairs=pairs,
            baseline_fps=baseline, native_fps=native, change_percent=100*(native/baseline-1))
    result["baseline_mask"], result["native_mask"] = masks
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("progress", type=Path)
    parser.add_argument("--csv-directory", type=Path, default=Path("analysis"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = report(args.progress, args.csv_directory, {"Menu": (40, 98), "Round": (150, 220)})
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    for name, scene in result["scenes"].items():
        print(f"{name}: {scene['baseline_fps']:.2f} -> {scene['native_fps']:.2f} FPS ({scene['change_percent']:+.2f}%), pairs " +
              ", ".join(f"{p['change_percent']:+.2f}%" for p in scene["pairs"]))


if __name__ == "__main__":
    main()
