"""Package the tested Windows BuzzPrepare, Question Studio and the pinned build source trees."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile


from preparation_version import base_release

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "BuzzPrepare-windows-x64.zip"
# Sources of the bundled Qt libraries (LGPLv3), published next to the package.
QT_SOURCES = {
    "qtbase-everywhere-src-6.8.3.tar.xz": "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80",
}
QT_LICENSE_SOURCE = "qtbase-everywhere-src-6.8.3.tar.xz"
# Question Studio (Qt Widgets, like BuzzSetup) sits next to BuzzSetup and shares
# its Qt libraries; these are the files only Studio needs.
STUDIO_FILES = ("BuzzQuestionStudio.exe", "imageformats/qjpeg.dll", "imageformats/qgif.dll")
# Studio was deployed against these; they must be the very files BuzzSetup ships.
SHARED_QT = ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms/qwindows.dll")
PORT_PATHS = (
    "LICENSE", "README.md", "THIRD_PARTY_NOTICES.md",
    "Launch-Buzz.sh", "requirements.txt", "cmake", "config", "docs", "launcher", "port", "scripts",
    "preparation/CMakeLists.txt", "preparation/README.md", "preparation/src", "tools",
    "third_party/cpp-httplib", "third_party/qrcodegen",
)
SUBMODULES = ("ps3recomp", "third_party/zlib")
WIZARD_FILES = (
    "BuzzSetup.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6Widgets.dll",
    "platforms/qwindows.dll", "styles/qmodernwindowsstyle.dll", "imageformats/qico.dll",
    "tls/qschannelbackend.dll",
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
                archive.writestr(f"BuzzPrepare/build-files/{name}", stream.read())
            names.append(name)
    return names


def studio_files(studio):
    missing = [name for name in STUDIO_FILES + SHARED_QT if not (studio / name).is_file()]
    if missing:
        raise ValueError(f"Build Question Studio first (scripts/build-question-studio.ps1); missing {', '.join(missing)}.")
    wizard = ROOT / "dist-preparation/bin"
    for name in SHARED_QT:
        if sha256(studio / name) != sha256(wizard / name):
            raise ValueError(f"Question Studio and BuzzSetup were deployed with different Qt builds ({name}).")
    return [(studio / name, name) for name in STUDIO_FILES]


def package(qt_sources, output, studio):
    sources = {path.name: path for path in qt_sources}
    if set(sources) != set(QT_SOURCES):
        raise ValueError(f"Pass the pinned Qt source archives: {', '.join(sorted(QT_SOURCES))}.")
    for name, path in sources.items():
        if sha256(path) != QT_SOURCES[name]:
            raise ValueError(f"{name} does not match its pinned SHA-256.")
    qt_source = sources[QT_LICENSE_SOURCE]
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
            archive.writestr("BuzzPrepare/build-files/release-sources.json", json.dumps(
                {"revision": revision, "base_release": base_release(ROOT), "submodules": pins, "files": sorted(files)}, indent=2) + "\n")
            for name in WIZARD_FILES:
                archive.write(ROOT / "dist-preparation/bin" / name, f"BuzzPrepare/{name}")
            for source, name in studio_files(studio):
                archive.write(source, f"BuzzPrepare/{name}")
            with tarfile.open(qt_source, "r:xz") as source:
                for member in source:
                    if member.isfile() and member.name.startswith("qtbase-everywhere-src-6.8.3/LICENSES/"):
                        with source.extractfile(member) as stream:
                            archive.writestr(f"BuzzPrepare/licenses/Qt/{Path(member.name).name}", stream.read())
        checksum = sha256(path)
        path.replace(output / PACKAGE)
        (output / "SHA256SUMS.txt").write_bytes(
            (f"{checksum}  {PACKAGE}\n" + "".join(f"{digest}  {name}\n" for name, digest in QT_SOURCES.items()))
            .encode("ascii"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-source", type=Path, nargs="+", required=True,
                        help="The pinned qtbase source archive")
    parser.add_argument("--studio", type=Path, default=ROOT / "dist-studio/bin",
                        help="Deployed Question Studio (scripts/build-question-studio.ps1)")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    package(args.qt_source, args.output, args.studio)
