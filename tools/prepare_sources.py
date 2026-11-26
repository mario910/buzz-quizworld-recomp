"""Stage the pinned public game-build sources for desktop packages."""
import argparse
import io
import json
from pathlib import Path
import subprocess
import tarfile

from preparation_version import base_release, release_revisions

ROOT = Path(__file__).resolve().parents[1]
PORT_PATHS = (
    "LICENSE", "README.md", "THIRD_PARTY_NOTICES.md",
    "Launch-Buzz.sh", "requirements.txt", "cmake", "config", "docs", "launcher", "port", "scripts",
    "preparation/CMakeLists.txt", "preparation/README.md", "preparation/src", "tools",
    "third_party/cpp-httplib", "third_party/qrcodegen",
)
SUBMODULES = ("ps3recomp", "third_party/zlib")


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def add_sources(destination, repo, ref, prefix="", paths=()):
    data = git(repo, "archive", "--format=tar", ref, "--", *paths)
    names = []
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as sources:
        for member in sources:
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError(f"Unexpected source archive entry: {prefix}{member.name}")
            name = prefix + member.name
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with sources.extractfile(member) as stream:
                target.write_bytes(stream.read())
            target.chmod(0o755 if member.mode & 0o111 else 0o644)
            names.append(name)
    return names


def prepare_sources(destination):
    revision = git(ROOT, "rev-parse", "HEAD").decode().strip()
    if git(ROOT, "status", "--porcelain", "--untracked-files=no", "--", *PORT_PATHS):
        raise ValueError("Commit the build sources before packaging.")
    pins = {module: git(ROOT, "rev-parse", f"{revision}:{module}").decode().strip() for module in SUBMODULES}
    files = add_sources(destination, ROOT, revision, paths=PORT_PATHS)
    for module, pin in pins.items():
        module_root = ROOT / module
        if git(module_root, "rev-parse", "HEAD").decode().strip() != pin:
            raise ValueError(f"Initialize {module} at the pinned revision before packaging.")
        files.extend(add_sources(destination, module_root, pin, prefix=module + "/"))
    (destination / "release-sources.json").write_text(json.dumps(
        {"revision": revision, "base_release": base_release(ROOT), "releases": release_revisions(ROOT),
         "submodules": pins, "files": sorted(files)}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    prepare_sources(parser.parse_args().destination)
