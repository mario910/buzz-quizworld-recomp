"""Opt-in install of the Real-ESRGAN texture upscaler (realesrgan-ncnn-vulkan).

The game's automatic texture upscaling (pc-settings ``texture_auto_upscale``)
runs it from ``<runtime>/textures/upscaler``. One pinned release archive is
downloaded only when asked for, checked against its size and SHA-256 before
anything is extracted, and cached for later preparations; a local copy of the
same archive installs offline. Only the executable, its OpenMP runtime and
the models are installed, by fixed name.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile
import urllib.request
import uuid
import zipfile

URL = ("https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/"
       "realesrgan-ncnn-vulkan-20220424-windows.zip")
SHA256 = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d"
SIZE = 45474481
EXECUTABLE = "realesrgan-ncnn-vulkan.exe"
LINUX_EXECUTABLE = "realesrgan-ncnn-vulkan"
LINUX_URL = URL.replace("-windows.zip", "-ubuntu.zip")
LINUX_SHA256 = "e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96"
LINUX_SIZE = 46931474
MEMBERS = (EXECUTABLE, "vcomp140.dll", "vcomp140d.dll", "README_windows.md",
           *(f"models/{model}.{kind}" for model in ("realesrgan-x4plus", "realesrgan-x4plus-anime",
                                                    "realesr-animevideov3-x2", "realesr-animevideov3-x3",
                                                    "realesr-animevideov3-x4")
             for kind in ("bin", "param")))


class Cancelled(RuntimeError):
    pass


def target(runtime):
    return Path(runtime) / "textures" / "upscaler"


def installed(runtime):
    return any((target(runtime) / name).is_file() for name in (EXECUTABLE, LINUX_EXECUTABLE))


def _check(cancel):
    if cancel is not None and cancel.is_set():
        raise Cancelled("Upscaler install cancelled.")


def _sha256(path, cancel):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        while chunk := source.read(1 << 20):
            _check(cancel)
            digest.update(chunk)
    return digest.hexdigest()


def fetch(cache, log, cancel=None, *, url=URL, sha256=SHA256, size=SIZE, opener=urllib.request.urlopen):
    """The verified release archive in ``cache``, downloaded only when absent."""
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / url.rsplit("/", 1)[1]
    if archive.is_file() and archive.stat().st_size == size and _sha256(archive, cancel) == sha256:
        log(f"Using the downloaded upscaler archive {archive}")
        return archive
    part = archive.with_name(archive.name + ".part")
    log(f"Downloading the texture upscaler ({size / 1e6:.0f} MB): {url}")
    digest, received, reported = hashlib.sha256(), 0, 0
    try:
        with opener(url, timeout=60) as response, open(part, "wb") as out:
            while chunk := response.read(1 << 20):
                _check(cancel)
                received += len(chunk)
                if received > size:
                    raise ValueError("The upscaler download is larger than the pinned release; nothing was installed.")
                digest.update(chunk)
                out.write(chunk)
                if received - reported >= 10 << 20:
                    reported = received
                    log(f"  {received / 1e6:.0f} / {size / 1e6:.0f} MB")
        if received != size or digest.hexdigest() != sha256:
            raise ValueError("The upscaler download does not match the pinned Real-ESRGAN release (size/SHA-256); "
                             "nothing was installed.")
        os.replace(part, archive)
    finally:
        if part.exists():
            part.unlink()
    return archive


def install(runtime, log, cancel=None, *, archive=None, cache=None, url=None, sha256=None, size=None,
            opener=urllib.request.urlopen, windows=None):
    """Install into ``<runtime>/textures/upscaler``, replacing an older install whole."""
    linux = windows is None and sys.platform == "linux"
    if not linux and not (os.name == "nt" if windows is None else windows):
        raise ValueError("The pinned upscaler release is available for Windows and Linux. Elsewhere, put "
                         "realesrgan-ncnn-vulkan and its models/ folder in textures/upscaler yourself.")
    url = url or (LINUX_URL if linux else URL)
    sha256 = sha256 or (LINUX_SHA256 if linux else SHA256)
    size = size if size is not None else (LINUX_SIZE if linux else SIZE)
    members = (LINUX_EXECUTABLE, "README_ubuntu.md", *MEMBERS[4:]) if linux else MEMBERS
    if archive is None:
        if cache is None:
            raise ValueError("Give a download cache folder or a local archive.")
        archive = fetch(cache, log, cancel, url=url, sha256=sha256, size=size, opener=opener)
    else:
        archive = Path(archive)
        if not archive.is_file() or archive.stat().st_size != size or _sha256(archive, cancel) != sha256:
            raise ValueError(f"{archive} is not the pinned Real-ESRGAN release (size/SHA-256); nothing was installed.")
    destination = target(runtime)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Staged beside the destination and renamed into place, so the game never
    # sees a partial install, and an interrupted one leaves the old intact.
    staging = Path(tempfile.mkdtemp(prefix=".upscaler-", dir=destination.parent))
    try:
        with zipfile.ZipFile(archive) as release:
            names = set(release.namelist())
            missing = [member for member in members if member not in names]
            if missing:
                raise ValueError("The upscaler archive lacks: " + ", ".join(missing))
            for member in members:
                _check(cancel)
                out = staging / member
                out.parent.mkdir(parents=True, exist_ok=True)
                with release.open(member) as source, open(out, "wb") as sink:
                    shutil.copyfileobj(source, sink)
                if linux and member == LINUX_EXECUTABLE:
                    out.chmod(0o755)
        if destination.exists():
            old = destination.with_name(f".upscaler-old-{uuid.uuid4().hex}")
            os.replace(destination, old)
            os.replace(staging, destination)
            shutil.rmtree(old, ignore_errors=True)
        else:
            os.replace(staging, destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging, ignore_errors=True)
    log(f"Texture upscaler installed: {destination}. Enable 'Auto texture upscaling' in the in-game PC settings.")
    return destination
