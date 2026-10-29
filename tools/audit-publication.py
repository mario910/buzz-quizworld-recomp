"""Check ONE named ref (full history by default; explicit tip-only mode available).

Heuristic source-package guard, not a legal opinion or a complete secret scan.
Does not print matched secret values. Submodules must be audited separately.
"""
import argparse
import json
from pathlib import PurePosixPath
import re
import subprocess
import sys

BAD_SUFFIXES = set(".elf .self .prx .sprx .bin .iso .pkg .pup .rap .rif .edat .keys .pem .p12 .pfx .exe .dll .obj .pdb .trace .log .zip .7z .pdf".split())
BAD_DIRS = {"analysis", "logs", "runtime", "build-windows", "toolenv", "fw_spu", "dev_flash", "dev_hdd0", "dev_hdd1", "ps3_game", ".claude", ".codex"}
SECRETS = [
    re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----"),
    re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{30,}\b"),
    re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{40,}\b"),
    re.compile(rb"\bAKIA[0-9A-Z]{16}\b"),
    re.compile(rb"https?://[^\s/@:]+:[^\s/@]+@"),
]


def path_issue(path, profile):
    p = PurePosixPath(path)
    parts = [x.lower() for x in p.parts]
    if p.suffix.lower() in BAD_SUFFIXES:
        return "excluded binary/asset/local-output extension"
    # SDK's runtime/ is ORIGINAL library source, not the port's private VFS.
    forbidden = BAD_DIRS - ({"runtime"} if profile == "sdk" else set())
    if any(x in forbidden for x in parts):
        return "excluded local/derived/game directory"
    if profile == "port" and path.startswith(("port/recompiled/", "port/spu/")):
        return "game-derived translation"
    if p.name.lower() in {"buzz-paths.json", "buzz-input.ini", ".env", "ps3recomp-buzz.patch"} or p.name.startswith(".env."):
        return "private local configuration"
    if path in {"docs/rsxfifo.txt", "tools/stderr_revert2.txt", "tools/stderr_vt1.txt"}:
        return "upstream copied reference/log, excluded from source export"
    return None


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def audit(repo, ref, profile, scope="history"):
    if scope not in ("history", "tip"):
        raise ValueError("Unknown audit scope")
    if scope == "history" and git(repo, "rev-parse", "--is-shallow-repository").strip() != b"false":
        raise ValueError("Full reachable-history audit requires a non-shallow local checkout.")
    sha = git(repo, "rev-parse", "--verify", ref + "^{commit}").decode().strip()
    lines = git(repo, "rev-list", "--objects", sha).decode().splitlines() if scope == "history" else [
        entry.split()[2] + " " + entry.split("\t", 1)[1]
        for entry in git(repo, "-c", "core.quotepath=false", "ls-tree", "-r", sha).decode().splitlines()
        if entry.split()[1] == "blob"]
    objects = {}
    for line in lines:
        oid, _, path = line.partition(" ")
        objects.setdefault(oid, path)
    metadata = subprocess.check_output(
        ["git", "-C", str(repo), "cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)"],
        input=("\n".join(objects) + "\n").encode()).decode().splitlines()
    blobs = []
    issues = []
    # rev-list prints only one path for a reused blob. Inspect every changed
    # path as well, so renaming a known blob to a forbidden name cannot hide it.
    changed = git(repo, "-c", "core.quotepath=false", "log", sha, "--format=", "--name-only", "--no-renames").decode() if scope == "history" else "\n".join(line.partition(" ")[2] for line in lines)
    for path in sorted(set(changed.splitlines()) - {""}):
        reason = path_issue(path, profile)
        if reason:
            issues.append({"path": path, "reason": reason})
    for item in metadata:
        oid, kind, size = item.split()
        if kind != "blob":
            continue
        path = objects[oid]
        if int(size) > 8 * 1024 * 1024:
            issues.append({"path": path, "reason": "large blob requires manual review"})
        blobs.append((oid, path))
    reader = subprocess.Popen(["git", "-C", str(repo), "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for oid, path in blobs:
            reader.stdin.write((oid + "\n").encode())
            reader.stdin.flush()
            header = reader.stdout.readline().split()
            if len(header) != 3 or header[1] != b"blob":
                raise ValueError("Unexpected git blob response")
            size = int(header[2])
            contents = reader.stdout.read(size)
            if len(contents) != size or reader.stdout.read(1) != b"\n":
                raise ValueError("Truncated git blob response")
            allowed_binary = profile == "sdk" and path in {"packaging/macos/icon_1024.png", "packaging/macos/ps3recomp.icns"}
            if b"\0" in contents and not allowed_binary:
                issues.append({"path": path, "reason": "unexpected binary blob; manual provenance review required"})
            for pattern in SECRETS:
                if pattern.search(contents):
                    issues.append({"path": path, "reason": "possible credential/private key (value suppressed)"})
    finally:
        reader.stdin.close()
        reader.stdout.close()
        if reader.wait(timeout=10):
            raise ValueError("git cat-file failed")
    return {"ref": ref, "commit": sha, "scope": scope, "profile": profile, "blobs_checked": len(blobs), "issues": issues}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--ref", required=True, help="Only this ref and its ancestors are audited/published.")
    parser.add_argument("--profile", choices=("port", "sdk"), default="port")
    parser.add_argument("--scope", choices=("history", "tip"), default="history",
                        help="Default: all reachable history. tip checks only the current source tree; it does NOT clear inherited history.")
    args = parser.parse_args()
    try:
        report = audit(args.repo, args.ref, args.profile, args.scope)
    except (ValueError, subprocess.CalledProcessError) as exc:
        print(f"Audit failed: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2))
    return int(bool(report["issues"]))


if __name__ == "__main__":
    sys.exit(main())
