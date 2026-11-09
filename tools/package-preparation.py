"""Package the tested Windows BuzzPrepare and its pinned build source trees."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "BuzzPrepare-windows-x64.zip"
QT_SOURCE = "qtbase-everywhere-src-6.8.3.tar.xz"
QT_SHA256 = "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80"
PORT_PATHS = (
    "LICENSE", "README.md", "THIRD_PARTY_NOTICES.md", "Prepare-Buzz.cmd", "Launch-Buzz.cmd",
    "requirements.txt", "cmake", "config", "docs", "launcher", "port", "scripts",
    "preparation/CMakeLists.txt", "preparation/README.md", "preparation/src", "tools",
    "third_party/cpp-httplib", "third_party/qrcodegen",
)
SUBMODULES = ("ps3recomp", "third_party/zlib")
WIZARD_FILES = (
    "BuzzPrepare.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll",
    "platforms/qwindows.dll", "styles/qmodernwindowsstyle.dll",
    "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    "vcruntime140.dll", "vcruntime140_1.dll",
)


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def add_sources(archive, repo, ref, prefix="", paths=()):
    data = git(repo, "archive", "--format=tar", ref, "--", *paths)
    names = []
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as sources:
        for member in sources:
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError(f"Unexpected source archive entry: {prefix}{member.name}")
            name = prefix + member.name
            with sources.extractfile(member) as stream:
                archive.writestr(f"BuzzPrepare/{name}", stream.read())
            names.append(name)
    return names


def package(qt_source, output):
    if qt_source.name != QT_SOURCE or sha256(qt_source) != QT_SHA256:
        raise ValueError(f"Expected the pinned Qt source archive {QT_SOURCE}.")
    revision = git(ROOT, "rev-parse", "HEAD").decode().strip()
    pins = {module: git(ROOT, "rev-parse", f"{revision}:{module}").decode().strip()
            for module in SUBMODULES}
    # The preparation executable must be built separately from this same revision.
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as staging:
        path = Path(staging) / PACKAGE
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            files = add_sources(archive, ROOT, revision, paths=PORT_PATHS)
            for module, pin in pins.items():
                module_root = ROOT / module
                if git(module_root, "rev-parse", "HEAD").decode().strip() != pin:
                    raise ValueError(f"Initialize {module} at the pinned revision before packaging.")
                files.extend(add_sources(archive, module_root, pin, prefix=module + "/"))
            if len({name.casefold() for name in files}) != len(files):
                raise ValueError("The source trees contain paths that collide on Windows.")
            archive.writestr("BuzzPrepare/release-sources.json", json.dumps(
                {"revision": revision, "submodules": pins, "files": sorted(files)}, indent=2) + "\n")
            for name in WIZARD_FILES:
                archive.write(ROOT / "dist-preparation/bin" / name, f"BuzzPrepare/dist-preparation/bin/{name}")
            with tarfile.open(qt_source, "r:xz") as source:
                for member in source:
                    if member.isfile() and member.name.startswith("qtbase-everywhere-src-6.8.3/LICENSES/"):
                        with source.extractfile(member) as stream:
                            archive.writestr(f"BuzzPrepare/licenses/Qt/{Path(member.name).name}", stream.read())
        checksum = sha256(path)
        path.replace(output / PACKAGE)
        (output / "SHA256SUMS.txt").write_bytes(
            f"{checksum}  {PACKAGE}\n{QT_SHA256}  {QT_SOURCE}\n".encode("ascii"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    package(args.qt_source, args.output)
