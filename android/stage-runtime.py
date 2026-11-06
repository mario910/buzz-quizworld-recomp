"""Stage a verified EU 1.00 desktop runtime for the Android hardware smoke test."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import stat
import subprocess
import sys
import tarfile
import tempfile
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from launcher.game_profiles import identify
from launcher.model import title_id

PACKAGE = "org.buzzrecomp.game"
ACTIVITY = PACKAGE + "/.BuzzActivity"
STAGE = """set -eu
mkdir -p files
if [ -e files/runtime ]; then
    echo 'Runtime already exists; preserve it before staging another copy.' >&2
    exit 1
fi
available=$(df -Pk files | awk 'NR == 2 { print $4 }')
if [ -z "$available" ] || [ "$available" -lt "$1" ]; then
    echo 'Insufficient space in the app storage for the prepared runtime.' >&2
    exit 1
fi
stage=$(mktemp -d files/.buzz-stage.XXXXXXXX)
trap 'rm -rf -- "$stage"' EXIT
tar -xf - -C "$stage"
expected=$2
actual=$(sha256sum "$stage/runtime/EBOOT.elf")
test "${actual%% *}" = "$expected" || {
    echo 'Staged EBOOT SHA-256 does not match the APK profile.' >&2
    exit 1
}
test -f "$stage/runtime/disc/PS3_GAME/PARAM.SFO"
test -d "$stage/runtime/dev_flash"
# Serialize publication so concurrent staging cannot replace existing data.
mkdir files/.buzz-publish
trap 'rmdir files/.buzz-publish; rm -rf -- "$stage"' EXIT
test ! -e files/runtime
mv "$stage/runtime" files/runtime
"""


def files_under(path):
    info = path.lstat()
    if path.is_symlink() or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError(f"Runtime contains a link: {path}")
    if stat.S_ISDIR(info.st_mode):
        for child in path.iterdir():
            yield from files_under(child)
    elif stat.S_ISREG(info.st_mode):
        yield path, info.st_size
    else:
        raise ValueError(f"Unsupported runtime entry: {path}")


def validate(dist):
    runtime = dist / "runtime"
    info = runtime.lstat()
    if not stat.S_ISDIR(info.st_mode) or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError("The runtime root must be a regular directory.")
    entries = [runtime / name for name in ("EBOOT.elf", "disc", "dev_flash")]
    size = sum(size for path in entries for _, size in files_under(path))
    elf = entries[0]
    with elf.open("rb") as stream:
        digest = hashlib.sha256()
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    title = title_id((runtime / "disc/PS3_GAME/PARAM.SFO").read_bytes())
    profile = identify(title, digest.hexdigest())
    if profile.adapter_family != "quizworld-eu100":
        raise ValueError("The debug APK expects the original EU 1.00 profile.")
    if not (runtime / "dev_flash/sys/external/flashATRAC.pic").is_file():
        raise ValueError("The prepared firmware is missing flashATRAC.pic.")
    return entries, size, profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist", type=Path)
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--serial")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--check", action="store_true", help="Verify local inputs without using ADB.")
    args = parser.parse_args()
    if not args.check and not args.apk:
        parser.error("--apk is required when staging to a device")
    entries, size, profile = validate(args.dist.resolve(strict=True))
    print(f"{profile.key}: {size:,} bytes of game and firmware data.", flush=True)
    if args.check:
        return
    adb = [args.adb, *(["-s", args.serial] if args.serial else [])]
    apk = args.apk.resolve(strict=True)
    subprocess.run([*adb, "get-state"], check=True)
    abi = subprocess.check_output([*adb, "shell", "getprop", "ro.product.cpu.abilist"], text=True).strip()
    api = subprocess.check_output([*adb, "shell", "getprop", "ro.build.version.sdk"], text=True).strip()
    if "arm64-v8a" not in abi.split(",") or not api.isdecimal() or int(api) < 29:
        raise ValueError(f"Expected an ARM64 device on Android API 29 or newer (ABI: {abi}, API: {api}).")
    subprocess.run([*adb, "install", "-r", apk], check=True)
    # Allow for filesystem allocation overhead and the first boot's caches.
    required_kib = (size + 1023) // 1024 + 256 * 1024
    remote = f"run-as {PACKAGE} sh -c {shlex.quote(STAGE)} buzz-stage {required_kib} {profile.elf_sha256}"
    if os.name == "nt":
        # Windows adb reads shell stdin in text mode. A firmware or ELF byte
        # 0x1a truncates that stream. Push the archive through adb's binary
        # sync protocol, then pipe it entirely on the device.
        remote_archive = f"/data/local/tmp/buzz-stage-{uuid.uuid4().hex}.tar"
        try:
            with tempfile.TemporaryDirectory(prefix=".android-stage-", dir=args.dist) as scratch:
                archive_path = Path(scratch) / "runtime.tar"
                print("Creating the binary runtime archive for ADB push.", flush=True)
                with tarfile.open(archive_path, mode="w") as archive:
                    for path in entries:
                        archive.add(path, arcname=f"runtime/{path.name}")
                subprocess.run([*adb, "push", archive_path, remote_archive], check=True)
                subprocess.run([*adb, "shell", "-T", f"cat {remote_archive} | {remote}"], check=True)
        finally:
            subprocess.run([*adb, "shell", "rm", "-f", remote_archive], check=False)
        subprocess.run([*adb, "shell", "am", "start", "-n", ACTIVITY], check=True)
        return
    with subprocess.Popen([*adb, "shell", "-T", remote], stdin=subprocess.PIPE) as child:
        try:
            with tarfile.open(fileobj=child.stdin, mode="w|") as archive:
                for path in entries:
                    archive.add(path, arcname=f"runtime/{path.name}")
        except OSError:
            child.stdin.close()
            child.wait()
            raise
        child.stdin.close()
        if code := child.wait():
            raise subprocess.CalledProcessError(code, child.args)
    subprocess.run([*adb, "shell", "am", "start", "-n", ACTIVITY], check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"Android staging stopped: {error}")
