"""Identify the stable release a preparation source snapshot was built from."""
import os
import re
import subprocess


def base_release(repo):
    tag = os.environ.get("GITHUB_REF_NAME", "") if os.environ.get("GITHUB_REF_TYPE") == "tag" else ""
    if not tag:
        result = subprocess.run(["git", "-C", str(repo), "describe", "--tags", "--abbrev=0",
                                 "--match", "v[0-9]*"], text=True, capture_output=True)
        tag = result.stdout.strip()
    return tag if re.fullmatch(r"v[0-9]+[.][0-9]+[.][0-9]+", tag) else None


def release_revisions(repo):
    """Resolve lightweight and annotated stable tags for older game manifests."""
    refs = subprocess.check_output([
        "git", "-C", str(repo), "for-each-ref", "--merged=HEAD", "--sort=version:refname",
        "--format=%(refname:short) %(objectname) %(*objectname)", "refs/tags/v*"
    ], text=True)
    releases = {}
    for line in refs.splitlines():
        tag, *objects = line.split()
        if re.fullmatch(r"v[0-9]+[.][0-9]+[.][0-9]+", tag):
            releases[objects[-1]] = tag
    return releases
