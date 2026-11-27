"""Package the Linux BuzzSetup wizard, Question Studio and the pinned build sources.

Run on Ubuntu 24.04 x86-64 (CI or the BuzzSetup build container) after
scripts/build-preparation-linux.sh and the Question Studio build, with qt6-base-dev and patchelf installed. The archive is self-contained apart from libraries every desktop Linux provides (glibc, libstdc++, X11/XCB core, GL,
fontconfig/freetype, OpenSSL 3, SDL2 and the Vulkan loader for the game).

Like the Windows ZIP it contains no game code: BuzzSetup builds the game on
the player's PC from the bundled sources and the player's own disc/firmware.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

from preparation_version import base_release, release_revisions

ROOT = Path(__file__).resolve().parents[1]
NAME = "BuzzPrepare"
PACKAGE = "BuzzPrepare-linux-x86_64.tar.gz"
APPIMAGE = "BuzzSetup-x86_64.AppImage"
# The AppImage is read-only: its sources are copied to a writable folder in
# the home directory (BUZZ_HOME, default ~/BuzzQuizWorld) where BuzzSetup
# builds the game. A newer AppImage refreshes the sources there and keeps the
# prepared game, saves, keys and the local Python environment.
APP_RUN = r"""#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
data="${BUZZ_HOME:-$HOME/BuzzQuizWorld}"
src="$here/usr/share/buzz/build-files"
dst="$data/build-files"
revision() { sed -n 's/^  "revision": "\([0-9a-f]*\)".*/\1/p' "$1/release-sources.json" 2>/dev/null; }
mkdir -p "$dst" || exit 1
(
    flock -x 9 || exit 1
    if [ "$(revision "$src")" != "$(revision "$dst")" ]; then
        rm -f "$dst/release-sources.json" || exit 1
        for tree in cmake config docs launcher port preparation scripts third_party tools ps3recomp; do
            rm -rf "${dst:?}/$tree" || exit 1
        done
        for item in "$src"/*; do
            [ "${item##*/}" = release-sources.json ] && continue
            cp -R "$item" "$dst/" && chmod -R u+w "$dst/${item##*/}" || exit 1
        done
        cp "$here/usr/share/buzz/README.txt" "$data/README.txt" || exit 1
        cp "$src/release-sources.json" "$dst/.release-sources.tmp" &&
            mv -f "$dst/.release-sources.tmp" "$dst/release-sources.json" || exit 1
    fi
) 9>"$data/.buzz-sources.lock" || exit 1
case "$1" in
    --play)
        shift
        if [ -x "$dst/dist/BuzzQuizWorldRecomp" ]; then exec "$dst/Launch-Buzz.sh" "$@"; fi
        if [ "${1:-}" = "--steam-deck" ]; then shift; fi ;;
    --studio)
        shift
        export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
        exec "$here/usr/bin/BuzzQuestionStudio" --game-dir "$dst/dist" "$@" ;;
esac
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
cd "$dst" && exec "$here/usr/bin/BuzzPrepare" "$@"
"""
DESKTOP = """[Desktop Entry]
Type=Application
Name=Buzz! Quiz World Setup
Comment=Prepare and play your own copy of Buzz! Quiz World
Exec=BuzzSetup
Icon=buzzsetup
Categories=Game;
Terminal=false
"""
APPIMAGE_README = """
AppImage: run BuzzSetup-x86_64.AppImage (make it executable first, or on the
Steam Deck right-click > Properties > Permissions > Is executable). The game is
prepared in ~/BuzzQuizWorld/build-files (set BUZZ_HOME to choose another
folder on a regular drive; about 25 GB). Start the prepared game with
  BuzzSetup-x86_64.AppImage --play
which is also the launch option to use when adding it to Steam as a non-Steam
game. Balanced studio settings are automatic on supported game builds.
--studio opens Buzz! Question Studio for your own question packs.
"""
PORT_PATHS = (
    "LICENSE", "README.md", "THIRD_PARTY_NOTICES.md",
    "Launch-Buzz.sh", "requirements.txt", "cmake", "config", "docs", "launcher", "port", "scripts",
    "preparation/CMakeLists.txt", "preparation/README.md", "preparation/src", "tools",
    "third_party/cpp-httplib", "third_party/qrcodegen",
)
SUBMODULES = ("ps3recomp", "third_party/zlib")
QT_PLUGINS = ("platforms/libqxcb.so", "imageformats/libqico.so", "tls/libqopensslbackend.so",
              # Question Studio: pictures and compose/dead keys for accented text.
              "imageformats/libqjpeg.so", "imageformats/libqgif.so",
              "platforminputcontexts/libcomposeplatforminputcontextplugin.so")
# Provided by every desktop distribution (and must match the host's drivers,
# fonts and X server); never bundled.
SYSTEM_LIBRARIES = re.compile(
    r"^(ld-linux.*|linux-vdso.*|lib(c|m|dl|pthread|rt|resolv|util|anl|mvec)\.so.*|libstdc\+\+\.so.*|libgcc_s\.so.*|"
    r"libGL.*|libEGL.*|libOpenGL.*|libGLX.*|libGLdispatch.*|libdrm.*|libgbm.*|libvulkan.*|"
    r"libX11.*|libxcb\.so.*|libXau.*|libXdmcp.*|libXext.*|libXrender.*|libwayland-.*|"
    r"libfontconfig.*|libfreetype.*|libharfbuzz.*|libgraphite2.*|libpng16.*|libbrotli.*|libbz2.*|"
    r"libz\.so.*|libzstd.*|liblzma.*|libexpat.*|libglib-2.*|libgio-2.*|libgthread-2.*|libgobject-2.*|libgmodule-2.*|"
    r"libpcre2-8.*|libffi.*|libdbus-1.*|libsystemd.*|libudev.*|libcap.*|libgcrypt.*|libgpg-error.*|liblz4.*|"
    r"libssl.*|libcrypto.*|libmount.*|libblkid.*|libuuid.*)$")
# GLib (with GIO) always comes from the host: one copy per process. SELinux is
# bundled: SteamOS and Arch do not ship libselinux.

LAUNCH_SETUP = """#!/bin/sh
# Open BuzzSetup: choose your disc/ISO and firmware, then Prepare game.
here=$(dirname "$(readlink -f "$0")")
# The bundled Qt platform plugin is XCB (X11 and XWayland desktops alike).
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$here/bin/BuzzPrepare" "$@"
"""
LAUNCH_GAME = """#!/bin/sh
# Play the copy prepared with BuzzSetup, or open BuzzSetup if there is none yet.
here=$(dirname "$(readlink -f "$0")")
if [ -x "$here/build-files/dist/BuzzQuizWorldRecomp" ]; then
    exec "$here/build-files/Launch-Buzz.sh" "$@"
fi
exec "$here/BuzzSetup"
"""
LAUNCH_STUDIO = """#!/bin/sh
# Buzz! Question Studio: write questions and install them as a quiz pack.
here=$(dirname "$(readlink -f "$0")")
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$here/bin/BuzzQuestionStudio" --game-dir "$here/build-files/dist" "$@"
"""
README = """Buzz! Quiz World - BuzzSetup for Linux x86-64
================================================

1. Extract this whole archive to a writable folder in your home directory.
2. Run ./BuzzSetup (or double-click it and choose Run).
3. Choose your own Buzz! Quiz World disc or ISO and the PS3 firmware PUP.
   Use the firmware and key file buttons or select your own files.
4. Select Prepare game. If this system has no compiler (SteamOS, Bazzite and
   other image-based systems) BuzzSetup offers to set up a distrobox build
   container first; nothing is installed on the system itself. On Debian,
   Ubuntu, Fedora and Arch it can install the missing build packages instead.
5. When it finishes, select Play Buzz!. Later, start the game with
   ./Play-Buzz.sh.
   You can add Play-Buzz.sh to Steam as a non-Steam game.
6. Optional: Question Studio (in BuzzSetup or ./Question-Studio.sh) writes
   your own questions and installs them as a quiz pack for this copy.

Requirements: an x86-64 CPU with AVX2 and FMA, a Vulkan driver, SDL2, and
glibc 2.39 or newer for BuzzSetup itself (SteamOS 3.6+, Ubuntu 24.04+,
Fedora 40+, current Arch). Preparation needs an Internet connection and about
25 GB of free space; it takes 20-60 minutes.

This package contains no game code, firmware or keys. The game is built on
this PC from the sources in build-files/ and your own files. Bundled Qt and
support libraries are listed in licenses/; their sources are available from
the Ubuntu 24.04 archive (apt-get source <package>) at the versions listed.
"""


def run(*args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kwargs).stdout


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args])


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def dependencies(binary):
    """Resolved non-system shared libraries of one ELF file."""
    found = {}
    for line in run("ldd", str(binary)).splitlines():
        match = re.match(r"\s*(\S+) => (\S+) ", line)
        if not match:
            if "not found" in line:
                raise RuntimeError(f"{binary}: {line.strip()}")
            continue
        name, path = match.groups()
        if not SYSTEM_LIBRARIES.match(name):
            found[name] = Path(path)
    return found


def owner_package(path):
    try:
        return run("dpkg", "-S", str(Path(path).resolve())).split(":", 1)[0]
    except subprocess.CalledProcessError:
        return run("dpkg", "-S", str(path)).split(":", 1)[0]


def add_sources(staging, repo, ref, prefix="", paths=()):
    data = git(repo, "archive", "--format=tar", ref, "--", *paths)
    names = []
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as sources:
        for member in sources:
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError(f"Unexpected source archive entry: {prefix}{member.name}")
            name = prefix + member.name
            target = staging / "build-files" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with sources.extractfile(member) as stream:
                target.write_bytes(stream.read())
            target.chmod(0o755 if member.mode & 0o111 else 0o644)
            names.append(name)
    return names


def icon_png(path):
    """The largest PNG image embedded in the wizard's Windows icon."""
    import struct
    data = path.read_bytes()
    best = None
    for i in range(struct.unpack_from("<H", data, 4)[0]):
        width, _, _, _, _, _, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        image = data[offset:offset + size]
        if image.startswith(b"\x89PNG") and (best is None or (width or 256) > best[0]):
            best = (width or 256, image)
    if not best:
        raise ValueError("No PNG image in the wizard icon")
    return best[1]


def build_appimage(staging, scratch, tool, runtime, output):
    appdir = Path(scratch) / "BuzzSetup.AppDir"
    usr = appdir / "usr"
    usr.mkdir(parents=True)
    for name in ("bin", "lib", "plugins"):
        shutil.copytree(staging / name, usr / name, symlinks=True)
    share = usr / "share/buzz"
    share.mkdir(parents=True)
    shutil.copytree(staging / "build-files", share / "build-files", symlinks=True)
    shutil.copytree(staging / "licenses", share / "licenses", symlinks=True)
    (share / "README.txt").write_text(README + APPIMAGE_README)
    (appdir / "AppRun").write_text(APP_RUN)
    (appdir / "AppRun").chmod(0o755)
    (appdir / "buzzsetup.desktop").write_text(DESKTOP)
    (appdir / "buzzsetup.png").write_bytes(icon_png(ROOT / "preparation/src/buzz-setup.ico"))
    target = Path(scratch) / APPIMAGE
    env = dict(os.environ, ARCH="x86_64", APPIMAGE_EXTRACT_AND_RUN="1")
    subprocess.run([str(tool), "--no-appstream", "--runtime-file", str(runtime), str(appdir), str(target)],
                   check=True, env=env)
    target.chmod(0o755)
    checksum = sha256(target)
    target.replace(output / APPIMAGE)
    return checksum


def package(wizard, studio, output, appimage_tool=None, appimage_runtime=None):
    revision = git(ROOT, "rev-parse", "HEAD").decode().strip()
    if git(ROOT, "status", "--porcelain", "--untracked-files=no", "--", *PORT_PATHS):
        raise ValueError("Commit the source changes before packaging: the package holds HEAD.")
    pins = {module: git(ROOT, "rev-parse", f"{revision}:{module}").decode().strip() for module in SUBMODULES}
    qt_plugins = Path(run("qtpaths6", "--query", "QT_INSTALL_PLUGINS").strip())
    media_plugins = tuple(p.relative_to(qt_plugins) for p in sorted((qt_plugins / "multimedia").glob("*.so")))
    plugins = QT_PLUGINS + media_plugins
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as scratch:
        staging = Path(scratch) / NAME
        (staging / "bin").mkdir(parents=True)
        (staging / "lib").mkdir()
        shutil.copy2(wizard, staging / "bin/BuzzPrepare")
        shutil.copy2(studio, staging / "bin/BuzzQuestionStudio")
        (staging / "bin/qt.conf").write_text("[Paths]\nPrefix = ..\nPlugins = plugins\n")
        elves = [staging / "bin/BuzzPrepare", staging / "bin/BuzzQuestionStudio"]
        for plugin in plugins:
            target = staging / "plugins" / plugin
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(qt_plugins / plugin, target)
            elves.append(target)
        libraries = {}
        pending = list(elves)
        while pending:
            for name, path in dependencies(pending.pop()).items():
                if name not in libraries:
                    libraries[name] = path
                    shutil.copy2(path, staging / "lib" / name)
                    pending.append(staging / "lib" / name)
        # Relative search paths: nothing leaks to programs BuzzSetup starts.
        for program in ("BuzzPrepare", "BuzzQuestionStudio"):
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/../lib", staging / "bin" / program], check=True)
        for name in libraries:
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", staging / "lib" / name], check=True)
        for plugin in plugins:
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/../../lib", staging / "plugins" / plugin], check=True)
        for elf in [*elves, *(staging / "lib" / name for name in libraries)]:
            env = dict(os.environ, LD_LIBRARY_PATH="")
            if "not found" in subprocess.run(["ldd", elf], capture_output=True, text=True, env=env).stdout:
                raise RuntimeError(f"Unresolved bundled dependency in {elf}")
        packages = {}
        for name, path in sorted(libraries.items()):
            packages.setdefault(owner_package(path), []).append(name)
        for plugin in media_plugins:
            packages.setdefault(owner_package(qt_plugins / plugin), []).append(str(plugin))
        licenses = staging / "licenses"
        licenses.mkdir()
        inventory = []
        for package_name, names in sorted(packages.items()):
            base = package_name.split(":")[0]
            version = run("dpkg-query", "-W", "-f=${Version}", package_name)
            copyright_file = Path("/usr/share/doc") / base / "copyright"
            if copyright_file.is_file():
                shutil.copy2(copyright_file, licenses / f"{base}.copyright")
            inventory.append(f"{base} {version}: {', '.join(names)}")
        for common in ("LGPL-3", "GPL-3", "GPL-2", "LGPL-2.1"):
            source = Path("/usr/share/common-licenses") / common
            if source.is_file():
                shutil.copy2(source, licenses / common)
        (licenses / "BUNDLED-LIBRARIES.txt").write_text(
            "Shared libraries bundled from Ubuntu 24.04 (apt-get source <package> for their sources):\n"
            + "\n".join(inventory) + "\n")
        for name, text in (("BuzzSetup", LAUNCH_SETUP), ("Play-Buzz.sh", LAUNCH_GAME),
                           ("Question-Studio.sh", LAUNCH_STUDIO)):
            (staging / name).write_text(text)
            (staging / name).chmod(0o755)
        (staging / "README.txt").write_text(README)
        files = add_sources(staging, ROOT, revision, paths=PORT_PATHS)
        for module, pin in pins.items():
            module_root = ROOT / module
            if git(module_root, "rev-parse", "HEAD").decode().strip() != pin:
                raise ValueError(f"Initialize {module} at the pinned revision before packaging.")
            files.extend(add_sources(staging, module_root, pin, prefix=module + "/"))
        (staging / "build-files/release-sources.json").write_text(json.dumps(
            {"revision": revision, "base_release": base_release(ROOT), "releases": release_revisions(ROOT),
             "submodules": pins, "files": sorted(files)}, indent=2) + "\n")
        archive = Path(scratch) / PACKAGE
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(staging, arcname=NAME)
        checksum = sha256(archive)
        archive.replace(output / PACKAGE)
        sums = f"{checksum}  {PACKAGE}\n"
        print(f"{output / PACKAGE}  {checksum}")
        if appimage_tool:
            image = build_appimage(staging, scratch, appimage_tool, appimage_runtime, output)
            sums += f"{image}  {APPIMAGE}\n"
            print(f"{output / APPIMAGE}  {image}")
        (output / "SHA256SUMS-linux.txt").write_text(sums)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wizard", type=Path, default=ROOT / "dist-preparation/linux/bin/BuzzPrepare")
    parser.add_argument("--studio", type=Path, default=ROOT / "build-studio/linux/BuzzQuestionStudio")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--appimage-tool", type=Path, help="appimagetool; also builds BuzzSetup-x86_64.AppImage")
    parser.add_argument("--appimage-runtime", type=Path, help="Pinned AppImage type2 runtime for --appimage-tool")
    args = parser.parse_args()
    if bool(args.appimage_tool) != bool(args.appimage_runtime):
        parser.error("--appimage-tool and --appimage-runtime go together")
    package(args.wizard, args.studio, args.output, args.appimage_tool, args.appimage_runtime)
