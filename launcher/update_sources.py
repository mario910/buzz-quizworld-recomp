"""Select the build sources for an installed game's update."""
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path
from urllib.request import Request, urlopen

from .setup_pipeline import copy_sources, safe_component

REPO = "mario910/buzz-quizworld-recomp"
# Both platform packages contain the same pinned build sources. The ZIP can
# be read directly on either OS without installing Git or an archive utility.
ASSET = "BuzzPrepare-windows-x64.zip"


def release_url(url):
    return urlopen(Request(url, headers={"User-Agent": "BuzzPrepare-updater"}), timeout=30)


def latest_sources(destination, installed_revision, log, bundled_project=None):
    with release_url(f"https://api.github.com/repos/{REPO}/releases/latest") as response:
        data = response.read(1024 * 1024 + 1)
    if len(data) > 1024 * 1024:
        raise ValueError("Release information exceeds the expected size.")
    release = json.loads(data)
    tag = release.get("tag_name", "")
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag) or release.get("draft") or release.get("prerelease"):
        raise ValueError("No stable Buzz Prepare release was found.")
    if bundled_project is not None:
        inventory = json.loads((bundled_project / "release-sources.json").read_text())
        base = inventory.get("base_release") or ""
        if re.fullmatch(r"v[0-9]+[.][0-9]+[.][0-9]+", base):
            version = lambda value: tuple(int(part) for part in value[1:].split("."))
            if version(base) >= version(tag):
                revision = inventory["revision"]
                if revision != installed_revision:
                    copy_sources(bundled_project, destination)
                return revision
    asset = next((a for a in release.get("assets", []) if a.get("name") == ASSET), {})
    size, digest = asset.get("size", 0), asset.get("digest") or ""
    if not 0 < size <= 512 * 1024**2 or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise ValueError("The release lacks verified build sources.")
    log(f"Downloading verified build sources for {tag}")
    with tempfile.TemporaryFile() as archive:
        h = hashlib.sha256()
        with release_url(f"https://github.com/{REPO}/releases/download/{tag}/{ASSET}") as stream:
            while chunk := stream.read(1024**2):
                if archive.tell() + len(chunk) > size:
                    raise ValueError("Release download exceeds its published size.")
                archive.write(chunk)
                h.update(chunk)
        if archive.tell() != size or h.hexdigest() != digest[7:]:
            raise ValueError("The release download did not match its published SHA-256.")
        prefix = "BuzzPrepare/build-files/"
        with zipfile.ZipFile(archive) as bundle:
            inventory = json.loads(bundle.read(prefix + "release-sources.json"))
            revision = inventory["revision"]
            if not re.fullmatch(r"[0-9a-f]{40}", revision):
                raise ValueError("Invalid release source revision.")
            if revision != installed_revision:
                for name in inventory["files"]:
                    relative = Path(*(safe_component(part) for part in name.split("/")))
                    output = destination / relative
                    output.parent.mkdir(parents=True, exist_ok=True)
                    info = bundle.getinfo(prefix + name)
                    with bundle.open(info) as source, output.open("xb") as target:
                        shutil.copyfileobj(source, target)
                    if info.external_attr >> 16 & 0o111 or output.suffix == ".sh":
                        output.chmod(0o755)
    return revision


def git(repo, *args, check=True, timeout=120):
    return subprocess.run(["git", "-C", str(repo), *args], check=check, text=True,
                          capture_output=True, timeout=timeout)


def select(project, workspace, installed_revision, log):
    if not (project / ".git").exists():
        return latest_sources(workspace, installed_revision, log, project)
    revision = git(project, "rev-parse", "HEAD").stdout.strip()
    if git(project, "status", "--porcelain").stdout:
        log("Building from this checkout, including local changes.")
        copy_sources(project, workspace)
        return None
    branch = git(project, "symbolic-ref", "--quiet", "--short", "HEAD", check=False).stdout.strip()
    remote = git(project, "config", "--get", f"branch.{branch}.remote", check=False).stdout.strip()
    ref = git(project, "config", "--get", f"branch.{branch}.merge", check=False).stdout.strip()
    if remote and remote != "." and ref.startswith("refs/heads/"):
        log(f"Checking {remote}/{ref.removeprefix('refs/heads/')} for updates")
        upstream = git(project, "ls-remote", "--exit-code", remote, ref, check=False, timeout=30)
        if upstream.returncode:
            raise ValueError("Could not check the tracked branch. Check your connection and retry.")
        if upstream.stdout.split()[0] != revision:
            url = git(project, "remote", "get-url", remote).stdout.strip()
            # Use a separate checkout so an update never changes local files,
            # branch pointers or submodule revisions in the developer's tree.
            with tempfile.TemporaryDirectory(dir=workspace.parent, prefix="upstream-") as tmp:
                checkout = Path(tmp)
                git(project, "clone", "--quiet", "--single-branch", "--branch",
                    ref.removeprefix("refs/heads/"), "--", url, str(checkout))
                if git(checkout, "merge-base", "--is-ancestor", revision, "HEAD", check=False).returncode == 0:
                    tip = git(checkout, "rev-parse", "HEAD").stdout.strip()
                    if tip != installed_revision:
                        git(checkout, "submodule", "update", "--init", "--recursive", timeout=600)
                        copy_sources(checkout, workspace)
                    return tip
                log("The local checkout is ahead of or has diverged from its upstream. Using local sources.")
    if installed_revision != revision:
        copy_sources(project, workspace)
    return revision
