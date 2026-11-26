"""Private, resumable in-process import/build pipeline for local game media."""
from __future__ import annotations

from dataclasses import asdict, dataclass
import hashlib
import io
import json
import os
import platform
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import threading
import time
import uuid

from . import model
from . import disc_keys
from .game_profiles import identify, KNOWN_TITLE_IDS

MAX_GAME_BYTES = 64 * 1024**3
MAX_FILES = 200000
# Deepest path the Windows Vulkan build creates below the session workspace
# (SPIRV-Tools export files under build-vulkan-windows/_deps, 153 characters),
# plus a small margin. Without long-path support Windows rejects paths of 260+.
WINDOWS_WORKSPACE_DEPTH = 156
WINDOWS_MAX_PATH = 259


def windows_long_paths_enabled():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\FileSystem") as key:
            return winreg.QueryValueEx(key, "LongPathsEnabled")[0] == 1
    except OSError:
        return False


def check_build_path_length(workspace, log, long_paths=None):
    """Refuse a Windows build folder whose nested CMake paths exceed MAX_PATH.

    Otherwise CMake fails minutes later with an unhelpful "Build step for
    ps3_vk_headers failed" while it downloads its dependencies."""
    length = len(str(workspace))
    if length + WINDOWS_WORKSPACE_DEPTH <= WINDOWS_MAX_PATH:
        return
    if long_paths is None:
        long_paths = windows_long_paths_enabled()
    if long_paths:
        log(f"Long build path ({length + WINDOWS_WORKSPACE_DEPTH} characters); continuing because Windows long paths are enabled.")
        return
    excess = length + WINDOWS_WORKSPACE_DEPTH - WINDOWS_MAX_PATH
    raise ValueError(
        f"The installation folder path is too long for the Windows build by {excess} characters. "
        f"Move the whole folder to a shorter path, for example C:\\Games\\Buzz, and prepare again; "
        f"or enable Windows long paths (LongPathsEnabled). Build folder: {workspace}")


def new_session(sessions):
    """A short, unused session folder name (keeps nested build paths short)."""
    while True:
        session = sessions / uuid.uuid4().hex[:8]
        if not os.path.lexists(session):
            return session


class Cancelled(RuntimeError):
    pass


class ProcessFailure(RuntimeError):
    def __init__(self, code, logfile):
        self.code, self.logfile = code, Path(logfile)
        super().__init__(f"Preparation command failed ({code}). Log: {logfile}")


@dataclass
class Inputs:
    game: str
    firmware: str
    keys: str = ""
    update: str = ""
    jobs: int = 6
    upscaler: bool = False

    def validate(self):
        if type(self.jobs) is not int or not 1 <= self.jobs <= 64:
            raise ValueError("Compiler jobs must be 1–64.")
        if type(self.upscaler) is not bool:
            raise ValueError("The upscaler choice must be yes or no.")
        for value in (self.game, self.firmware):
            if not value or not Path(value).is_absolute() or not Path(value).exists():
                raise ValueError("Choose full paths to an ISO/dump and PS3UPDAT.PUP/dev_flash.")
        if Path(self.game).is_file() and Path(self.game).suffix.lower() != ".iso":
            raise ValueError("Game input must be an ISO or a directory containing PS3_GAME.")
        if self.keys and (not Path(self.keys).is_absolute() or not Path(self.keys).is_file()):
            raise ValueError("Decryption keys must be a local file with a full path.")
        if Path(self.firmware).is_file() and Path(self.firmware).suffix.lower() != ".pup":
            raise ValueError("Firmware must be a PS3UPDAT.PUP file or dev_flash directory.")
        from .ps3_crypto import KeyStore
        keys = KeyStore(self.keys)
        if Path(self.firmware).is_file():
            keys.named_key("pup_hmac", 64)
            if not keys.candidates(3, 0, 0):
                raise ValueError("Private key file is missing the firmware package root (category 3).")
        if self.update and (not Path(self.update).is_absolute() or not Path(self.update).is_file()):
            raise ValueError("Optional game update must be a local PKG, EBOOT.BIN or EBOOT.elf.")
        if self.update and Path(self.update).suffix.lower() not in (".pkg", ".bin", ".elf"):
            raise ValueError("Unsupported update file type.")



def fingerprint(path, cancel=None, log=None, *, catalog=False):
    path = Path(path).resolve(strict=True)
    if path.is_dir():
        # A folder is used read-only, not copied. Track its executable and SFO.
        return {"path": str(path), "sfo": fingerprint(path / "PS3_GAME/PARAM.SFO", cancel, log),
                "eboot": fingerprint(path / "PS3_GAME/USRDIR/EBOOT.BIN", cancel, log)}
    before = path.stat()
    size = before.st_size
    hasher, done, last = hashlib.sha256(), 0, time.monotonic()
    disc_hash = hashlib.sha1(usedforsecurity=False) if catalog and any(size == key[0] for key in disc_keys.DISCS) else None
    with path.open("rb") as stream:
        while chunk := stream.read(8 * 1024**2):
            if cancel is not None and cancel.is_set():
                raise Cancelled("Input verification cancelled.")
            hasher.update(chunk)
            if disc_hash is not None:
                disc_hash.update(chunk)
            done += len(chunk)
            if log and time.monotonic() - last > 2:
                log(f"Hash: {path.name}: {done / 1024**2:.0f}/{size / 1024**2:.0f} MiB")
                last = time.monotonic()
    after = path.stat()
    if done != size or (after.st_size, after.st_mtime_ns) != (size, before.st_mtime_ns):
        raise ValueError("Input changed during hashing; retry after it stops changing.")
    identity = {"path": str(path), "size": size, "sha256": hasher.hexdigest()}
    if disc_hash is not None:
        identity["disc_sha1"] = disc_hash.hexdigest()
    return identity


def safe_component(name):
    if not name or name in (".", "..") or any(ord(c) < 32 or c in '<>:"/\\|?*' for c in name) or name.endswith((".", " ")):
        raise ValueError("Unsafe ISO filename; extraction refused.")
    if name.split(".")[0].upper() in {"CON", "PRN", "AUX", "NUL", *[f"COM{n}" for n in range(1, 10)], *[f"LPT{n}" for n in range(1, 10)]}:
        raise ValueError("Reserved Windows filename in ISO.")
    return name


def reject_links_below(root, target):
    root, target = Path(root).absolute(), Path(target).absolute()
    relative = target.relative_to(root)
    at = root
    for part in (None, *relative.parts):
        if part is not None:
            at = at / part
        if at.exists() or at.is_symlink():
            info = at.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("Symlink/reparse point in managed output; operation refused.")


def iso_file_size(record):
    """Sum ISO9660 multi-extent records; UDF's record already has the full size."""
    total, seen = 0, set()
    while record is not None:
        if id(record) in seen or len(seen) >= MAX_FILES:
            raise ValueError("Cyclic/oversized ISO multi-extent chain.")
        seen.add(id(record))
        size = record.get_data_length()
        if size < 0 or total + size > MAX_GAME_BYTES or record.is_symlink():
            raise ValueError("Invalid/oversized ISO file extent (safety limits).")
        total += size
        record = getattr(record, "data_continuation", None)
    return total


def iso_plan(image):
    mode = "udf_path" if image.has_udf() else "joliet_path" if image.has_joliet() else "iso_path"
    plan, seen, total, directories = [], set(), 0, 0
    for root, dirs, files in image.walk(**{mode: "/"}):
        directories += 1
        if directories > MAX_FILES or len(root.split("/")) > 64:
            raise ValueError("ISO directory tree exceeds safety limits.")
        if mode in ("iso_path", "joliet_path"):
            clean = lambda s: s.rsplit(";", 1)[0] if ";" in s and s.rsplit(";", 1)[1].isdigit() else s
        else:
            clean = lambda s: s
        parts = [safe_component(clean(x)) for x in root.split("/") if x]
        if not parts or parts[0] != "PS3_GAME":
            continue
        for name in files:
            relative = Path(*parts, safe_component(clean(name)))
            key = str(relative).casefold()
            if key in seen:
                raise ValueError("Colliding ISO paths on Windows; extraction refused.")
            seen.add(key)
            source = root.rstrip("/") + "/" + name
            record = image.get_record(**{mode: source})
            if record.is_symlink():
                raise ValueError("ISO symlinks are not accepted.")
            size = iso_file_size(record)
            if size < 0:
                raise ValueError("Invalid ISO file size.")
            total += size
            if total > MAX_GAME_BYTES or len(plan) >= MAX_FILES:
                raise ValueError("ISO exceeds import safety limits.")
            plan.append((relative, source, size))
    if not any(p.as_posix() == "PS3_GAME/PARAM.SFO" for p, _, _ in plan):
        raise ValueError("No readable PS3_GAME/PARAM.SFO in the ISO.")
    return mode, plan, total


def iso_game_title(image, mode, plan, *, disc=None):
    from .ps3_crypto import SCE_V2

    sfo_path = "PS3_GAME/PARAM.SFO"
    eboot_path = "PS3_GAME/USRDIR/EBOOT.BIN"
    icon_path = "PS3_GAME/ICON0.PNG"
    files = {path: (source, size) for relative, source, size in plan
             if (path := relative.as_posix()) in (sfo_path, eboot_path, icon_path)}
    if disc is not None or image.has_ps3_regions:
        def readable(path, signatures):
            if path not in files:
                return False
            with image.open_file_from_iso(**{mode: files[path][0]}) as stream:
                return stream.read(8).startswith(signatures)

        for path, signatures in ((eboot_path, (SCE_V2, b"\x7fELF\x02\x02")),
                                 (icon_path, (b"\x89PNG\r\n\x1a\n",))):
            if path == icon_path and path not in files:
                continue
            if not readable(path, signatures):
                if disc is None:
                    raise ValueError(f"The PS3 ISO appears encrypted or damaged ({Path(path).name} is unreadable). "
                                     "Its size and SHA-1 do not match a supported catalog image. Use a decrypted disc dump.")
                raise ValueError(f"The verified disc key for {disc.title_id} did not produce a readable {Path(path).name}.")

    source, size = files[sfo_path]
    if size > 1024 * 1024:
        raise ValueError("Oversized PARAM.SFO.")
    buffer = io.BytesIO()
    image.get_file_from_iso_fp(buffer, **{mode: source})
    try:
        title = model.title_id(buffer.getvalue())
    except ValueError as exc:
        if image.has_ps3_regions and disc is None:
            raise ValueError("The PS3 ISO has an unreadable PARAM.SFO. No exact catalog image matched. "
                             "Use a decrypted disc dump.") from exc
        raise
    if disc is not None and title != disc.title_id:
        raise ValueError(f"Decrypted disc title ID does not match the catalog entry for {disc.title_id}.")
    if title not in KNOWN_TITLE_IDS:
        raise ValueError("This title ID has no tested import/build profile yet.")
    return title


def extract_iso(path, destination, log, cancel, *, disc=None):
    from .disc_image import DiscImage
    from pycdlib.pycdlibexception import PyCdlibException

    check_cancel(cancel)
    key = None
    if disc is not None:
        log(f"Matched encrypted PS3 disc {disc.title_id}. Fetching its public disc key…")
        key = disc_keys.download_disc_key(disc, lambda: check_cancel(cancel))
    image = DiscImage()
    opened = False
    try:
        try:
            image.open(str(path), disc_key=key)
            opened = True
            if image.fallback_reason:
                log(image.fallback_reason)
            mode, plan, total = iso_plan(image)
            title = iso_game_title(image, mode, plan, disc=disc)
        except PyCdlibException as exc:
            if disc is None and image.has_ps3_regions:
                raise ValueError("Cannot read this PS3 disc filesystem. If the ISO is encrypted, it has no exact "
                                 "match in the supported catalog. Use a decrypted disc dump.") from exc
            raise ValueError(f"Cannot read the disc filesystem: {exc}") from exc
        if disc is not None:
            log(f"Verified disc key and game headers for {title}. Decrypting sectors during extraction…")
        check_cancel(cancel)
        destination.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(destination).free < total + 2 * 1024**3:
            raise ValueError("Not enough disk space for extraction plus build headroom (more space may be needed to compile).")
        copied, progress_at = 0, time.monotonic()
        class Writer:
            def __init__(self, output, size):
                self.output = output
                self.remaining = size
            def write(self, chunk):
                nonlocal copied, progress_at
                if cancel.is_set():
                    raise Cancelled("ISO extraction cancelled; original ISO is untouched.")
                if len(chunk) > self.remaining:
                    raise ValueError("ISO extraction exceeds the planned file size.")
                count = self.output.write(chunk)
                self.remaining -= count
                copied += count
                if time.monotonic() - progress_at > 2:
                    log(f"ISO: {copied / 1024**2:.0f}/{total / 1024**2:.0f} MiB")
                    progress_at = time.monotonic()
                return count
            def __getattr__(self, name):
                return getattr(self.output, name)
        for index, (relative, source, size) in enumerate(plan):
            if cancel.is_set():
                raise Cancelled("Import cancelled; original ISO is untouched.")
            target = destination / relative
            reject_links_below(destination, target)
            target.parent.mkdir(parents=True, exist_ok=True)
            partial = target.with_name(target.name + ".import-partial-" + uuid.uuid4().hex)
            output = partial.open("xb")
            try:
                with output:
                    image.get_file_from_iso_fp(Writer(output, size), **{mode: source})
                    if output.tell() != size:
                        raise ValueError("Truncated ISO extraction.")
                os.replace(partial, target)
            finally:
                partial.unlink(missing_ok=True)
            if index % 50 == 0 or index == len(plan) - 1:
                log(f"ISO: {index + 1}/{len(plan)} files, {total / 1024**3:.1f} GiB total")
    finally:
        if opened:
            image.close()


def process(command, home, log, cancel, *, env=None, timeout=1800):
    home = Path(home)
    home.mkdir(parents=True, exist_ok=True)
    logfile = home / ("process-" + uuid.uuid4().hex + ".log")
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    started = time.monotonic()
    with logfile.open("wb") as output, logfile.open("rb") as tail:
        child = subprocess.Popen([str(x) for x in command], cwd=home, env=env,
                                 stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=flags, start_new_session=os.name != "nt")
        try:
            while True:
                chunk = tail.read(8192)
                if chunk:
                    log(chunk.decode("utf-8", errors="replace"))
                if child.poll() is not None:
                    while rest := tail.read(65536):
                        log(rest.decode("utf-8", errors="replace"))
                    break
                if cancel.is_set() or time.monotonic() - started > timeout:
                    raise Cancelled("Operation cancelled or timed out; only this preparation's child processes will be stopped.")
                time.sleep(0.1)
            if child.returncode:
                raise ProcessFailure(child.returncode, logfile)
        finally:
            if child.poll() is None:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"],
                                   capture_output=True, creationflags=flags, timeout=15)
                else:
                    import signal
                    os.killpg(child.pid, signal.SIGTERM)
                child.wait(timeout=15)


def firmware_validator(path):
    import importlib.util
    spec = importlib.util.spec_from_file_location("verify_game", Path(__file__).resolve().parents[1] / "tools/verify-game.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    verifier.verify_firmware(path)
    if not any((Path(path) / "data/font").glob("*.TTF")):
        raise ValueError("Firmware extraction has no PS3 fonts.")


def install_firmware(inputs, home, log, cancel):
    from .ps3_crypto import KeyStore
    from .ps3_import import extract_firmware
    extract_firmware(inputs.firmware, home, KeyStore(inputs.keys), log, lambda: check_cancel(cancel))


def check_cancel(cancel):
    if cancel.is_set():
        raise Cancelled("Preparation cancelled; source files are unchanged.")


UPDATE_102_FILES = tuple(language + suffix for language in ("GLOBAL", "CRO", "CZE", "GRC", "POL", "RUS")
                         for suffix in (".EDAT", "SCRIPTS.SDAT"))


def stage_update_content(source, workspace, cancel, log):
    """EU 1.02 code needs its own scripts; never copy profiles or cached plaintext."""
    source, workspace = Path(source), Path(workspace)
    destination = workspace / "runtime/dev_hdd0/game/BCES00647/USRDIR/Patched"
    reject_links_below(workspace, destination)
    for name in UPDATE_102_FILES:
        file = source / name
        if file.is_symlink() or not file.is_file() or not 0 < file.stat().st_size <= 128 * 1024**2:
            raise ValueError("EU 1.02 requires complete Patched assets, not only EBOOT. "
                             "Supply your own update PKG or EBOOT with a sibling Patched folder. Missing: " + str(file))
    # Preflight all inputs before writing; resume never replaces a modified file.
    for name in UPDATE_102_FILES:
        if cancel.is_set():
            raise Cancelled("Update content staging cancelled.")
        target = destination / name
        reject_links_below(workspace, target)
        if target.exists():
            a, b = fingerprint(target, cancel), fingerprint(source / name, cancel)
            if (a["size"], a["sha256"]) != (b["size"], b["sha256"]):
                raise ValueError("Prepared update content changed; start a new session instead of overwriting: " + str(target))
    destination.mkdir(parents=True, exist_ok=True)
    for name in UPDATE_102_FILES:
        if cancel.is_set():
            raise Cancelled("Update content staging cancelled.")
        target = destination / name
        if not target.exists():
            shutil.copy2(source / name, target)
    log("Staged EU 1.02 update assets only (no profiles, saves, or plaintext caches).")


def copy_source_file(original, dest, now):
    """Copy one build source, never leaving it dated in the future.

    ZIP entries carry a wall-clock time without a time zone: the release is
    packaged in UTC, so west of UTC its sources look hours newer than the
    build files CMake generates and Ninja regenerates build.ninja forever
    ("still dirty after 100 tries")."""
    shutil.copy2(original, dest)
    if dest.stat().st_mtime > now:
        os.utime(dest, (now, now))


def copy_sources(project, destination):
    """Copy public working source, not .git, VFS, binaries or private analyses."""
    now = time.time()
    bundled = project / "release-sources.json"
    if bundled.is_file() and not (project / ".git").exists():
        reject_links_below(project, bundled)
        manifest = json.loads(bundled.read_text(encoding="utf-8"))
        files = manifest.get("files") if isinstance(manifest, dict) else None
        if not isinstance(files, list) or not files or not all(isinstance(name, str) for name in files):
            raise ValueError("Invalid build source list. Extract the complete BuzzPrepare ZIP.")
        for name in files:
            relative = Path(*(safe_component(part) for part in name.split("/")))
            original, dest = project / relative, destination / relative
            reject_links_below(project, original)
            reject_links_below(destination, dest)
            if not original.is_file():
                raise ValueError(f"Missing bundled build source: {name}. Extract the complete BuzzPrepare ZIP.")
            dest.parent.mkdir(parents=True, exist_ok=True)
            copy_source_file(original, dest, now)
        return

    for source, target in ((project, destination), (project / "ps3recomp", destination / "ps3recomp"),
                           (project / "third_party/zlib", destination / "third_party/zlib")):
        names = subprocess.check_output(["git", "-C", str(source), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]).split(b"\0")
        for encoded in names:
            if not encoded:
                continue
            relative = Path(os.fsdecode(encoded))
            original = source / relative
            if original.is_symlink():
                raise ValueError("Source symlinks are not supported by the workspace copier.")
            if not original.is_file():
                continue  # Submodule gitlinks are copied explicitly above.
            if any(part in {"analysis", "runtime", "logs", "toolenv", ".git", "build-windows", "build-launcher", "build-vulkan-smoke", "build-vulkan-windows", "dist-launcher"} for part in relative.parts) and source == project:
                continue
            dest = target / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            copy_source_file(original, dest, now)


def build_workspace(project, workspace, profile, jobs, log, cancel, stage, standalone, linux, extra_linux_options=()):
    macos = sys.platform == "darwin"
    if linux or macos:
        def posix_build(stage_name):
            process([sys.executable, workspace / "tools/build-linux.py", stage_name, "--jobs", str(jobs),
                     *(["--profile", "desktop", *extra_linux_options] if stage_name == "build" else ())],
                    workspace, log, cancel, timeout=7200)
        stage("generate translations", lambda: posix_build("generate"))
        renderer = "macOS Metal" if macos else "Linux Vulkan"
        stage(f"build standalone {renderer} game", lambda: posix_build("build"))
        exe = workspace / ("build-metal-macos" if macos else "build-vulkan-linux") / "BuzzQuizWorldRecomp"
    else:
        env = dict(os.environ, BUZZ_TOOL_ENV=os.environ.get("BUZZ_TOOL_ENV") or str(project / "toolenv"))
        powershell = shutil.which("powershell.exe")
        if not powershell:
            raise ValueError("Windows PowerShell is required by the current Windows build.")
        def ps(script, *arguments):
            process([powershell, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", workspace / script, *arguments],
                    workspace, log, cancel, env=env, timeout=7200)
        stage("generate translations", lambda: ps("scripts/generate.ps1", "-Jobs", str(jobs)))
        if standalone:
            stage("build standalone Vulkan game", lambda: ps(
                "scripts/Build-Buzz.ps1", "-SkipGeneration", "-Standalone", "-Renderer", "Vulkan",
                "-Jobs", str(jobs)))
            exe = workspace / "build-vulkan-windows/BuzzQuizWorldRecomp.exe"
        else:
            stage("build native game", lambda: ps("scripts/Build-Buzz.ps1", "-SkipGeneration", "-Jobs", str(jobs)))
            exe = workspace / "build-windows/BuzzQuizWorldRecomp.exe"
    return exe


def publish_distribution(project, workspace, paths, exe, log, cancel, source_revision=None, source_release=None):
    """Publish a self-contained local copy only after all files have been staged."""
    dist = project / "dist"
    if os.path.lexists(dist):
        raise ValueError(f"Move the existing distribution before preparing another: {dist}")
    staging = project / (".dist-" + uuid.uuid4().hex)
    staging.mkdir()
    runtime = staging / "runtime"

    def copy_file(source, target):
        check_cancel(cancel)
        reject_links(source.parent, [source.name])
        return shutil.copy2(source, target)

    def reject_links(directory, names):
        for name in names:
            path = Path(directory) / name
            info = path.lstat()
            flags = getattr(info, "st_file_attributes", 0)
            if stat.S_ISLNK(info.st_mode) or flags & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError(f"Distribution input contains a link: {path}")
        return []

    def copy_tree(source, target):
        if staging.resolve().is_relative_to(source.resolve()):
            raise ValueError("The output folder cannot be inside an input tree.")
        reject_links(source.parent, [source.name])
        shutil.copytree(source, target, ignore=reject_links,
                        copy_function=lambda a, b: copy_file(Path(a), b))

    try:
        log("=== assemble portable dist ===")
        runtime.mkdir()
        copy_file(exe, staging / exe.name)
        # The build binding lets the native launcher validate this copy itself.
        if (exe.parent / "game-build.json").is_file():
            target = staging / "game-build.json"
            copy_file(exe.parent / "game-build.json", target)
            if source_revision:
                record = json.loads(target.read_text(encoding="utf-8-sig"))
                record["source_revision"] = source_revision
                if source_release:
                    record["source_release"] = source_release
                model.atomic_text(target, json.dumps(record))
        for dll in exe.parent.glob("*.dll"):
            copy_file(dll, staging / dll.name)
        copy_file(Path(paths["eboot_elf"]), runtime / "EBOOT.elf")
        disc = Path(paths["game_root"])
        copy_tree(disc / "PS3_GAME", runtime / "disc/PS3_GAME")
        if (disc / "PS3_DISC.SFB").is_file():
            copy_file(disc / "PS3_DISC.SFB", runtime / "disc/PS3_DISC.SFB")
        copy_tree(Path(paths["dev_flash"]), runtime / "dev_flash")
        for name in ("dev_hdd0", "textures"):
            source = workspace / "runtime" / name
            if source.is_dir():
                copy_tree(source, runtime / name)
        check_cancel(cancel)
        # A concurrently published nonempty dist cannot be replaced by rename.
        staging.rename(dist)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    log(f"Ready: {dist / exe.name}")
    return dist


def prepare(inputs, project, log, cancel, *, resume=None, build=True, accept_rebuilt=False, standalone=False):
    inputs.validate()
    project = Path(project).resolve()
    linux = sys.platform == "linux"
    macos = sys.platform == "darwin"
    windows = os.name == "nt"
    arch = platform.machine().lower()
    supported = macos and arch == "arm64" or (linux or windows) and arch in {"amd64", "x86_64"}
    if build and standalone and not supported:
        raise ValueError("Standalone preparation requires Windows/Linux x64 or macOS arm64.")
    if standalone and os.path.lexists(project / "dist"):
        raise ValueError("Move the existing dist folder before preparing another copy.")
    sessions = project / "runtime/setup/sessions"
    sessions.mkdir(parents=True, exist_ok=True)
    session = Path(resume).resolve() if resume else new_session(sessions)
    if session.parent != sessions.resolve() or session.is_symlink():
        raise ValueError("Resume path must be one of this project's own setup sessions.")
    if build and (linux or macos) and not standalone:
        raise ValueError("Use --dist to build on this platform.")
    if build and windows:
        check_build_path_length(session / "workspace", log)
    session.mkdir(exist_ok=True)
    reject_links_below(sessions, session)
    lock = session / "running.lock"
    try:
        lockfd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise ValueError("This session is already running (or has a stale lock after a crash). Choose a new session; no lock is deleted automatically.") from None
    os.close(lockfd)
    ledger = session / "state.json"
    try:
        log(f"Session: {session}")
        log("Checking input fingerprints (a large ISO can take a while)…")
        game_identity = fingerprint(inputs.game, cancel, log, catalog=True)
        disc = disc_keys.DISCS.get((game_identity.get("size"), game_identity.pop("disc_sha1", None)))
        identities = {"game": game_identity,
                      "keys": fingerprint(inputs.keys, cancel, log) if inputs.keys else None,
                      "importer": 1,
                      "firmware": firmware_identity(inputs.firmware),
                      "update": fingerprint(inputs.update, cancel, log) if inputs.update else None}
        state = json.loads(ledger.read_text(encoding="utf-8")) if ledger.exists() else {"inputs": asdict(inputs), "identities": identities, "done": []}
        if state["identities"] != identities:
            raise ValueError("Inputs changed since this session began. Start a new session; existing data is preserved.")
        def save():
            model.atomic_text(ledger, json.dumps(state, ensure_ascii=False, indent=2) + "\n")
        def stage(name, function):
            if cancel.is_set():
                raise Cancelled("Preparation cancelled.")
            if name not in state["done"]:
                log(f"=== {name} ===")
                function()
                state["done"].append(name)
                save()
        save()
        game = Path(inputs.game)
        if game.is_file():
            game = session / "game"
            stage("extract ISO", lambda: extract_iso(inputs.game, game, log, cancel, disc=disc))
            reject_links_below(session, game / "PS3_GAME/PARAM.SFO")
        disc_title = model.title_id((game / "PS3_GAME/PARAM.SFO").read_bytes())
        if disc_title not in KNOWN_TITLE_IDS:
            raise ValueError("Unsupported title ID; no files in the source dump were changed.")
        home = session / "import"
        reject_links_below(session, home)
        executable = game / "PS3_GAME/USRDIR/EBOOT.BIN"
        if inputs.update:
            from .ps3_crypto import PKG_MAGIC
            update = Path(inputs.update)
            with update.open("rb") as stream:
                is_package = stream.read(len(PKG_MAGIC)) == PKG_MAGIC
            if is_package:
                from .ps3_import import extract_update
                update_home = home / f"dev_hdd0/game/{disc_title}"
                reject_links_below(session, update_home)
                stage("install game update", lambda: extract_update(inputs.update, update_home, disc_title, log,
                                                                    lambda: check_cancel(cancel)))
                executable = update_home / "USRDIR/EBOOT.BIN"
            else:
                executable = update
        staged = session / "executable"
        reject_links_below(session, staged)
        staged.mkdir(exist_ok=True)
        elf = staged / "EBOOT.elf"
        reject_links_below(session, elf)
        def decode():
            from .ps3_crypto import KeyStore, decrypt_elf
            from .ps3_import import atomic_bytes
            plain = decrypt_elf(executable, KeyStore(inputs.keys), lambda: check_cancel(cancel))
            # Verify identity before publishing plaintext as a completed stage.
            identify(disc_title, hashlib.sha256(plain).hexdigest())
            atomic_bytes(elf, plain, session)
            if not elf.is_file() or elf.read_bytes()[:6] != b"\x7fELF\x02\x02":
                raise ValueError("Import did not produce a big-endian ELF64.")
        stage("prepare executable", decode)
        profile = identify(disc_title, fingerprint(elf)["sha256"])
        flash = Path(inputs.firmware)
        if flash.is_file():
            stage("install firmware", lambda: install_firmware(inputs, home, log, cancel))
            flash = home / "dev_flash"
        firmware_validator(flash)
        state["profile"] = asdict(profile)
        state["paths"] = dict(game_root=str(game.resolve()), dev_flash=str(flash.resolve()), eboot_elf=str(elf.resolve()))
        save()
        log(f"Detected {profile.key}: {profile.note}")
        if not build or not profile.buildable:
            if build and not profile.buildable:
                raise ValueError("This executable is recognized but has no build profile yet. The import is retained for analysis.")
            return session, state
        workspace = session / "workspace"
        reject_links_below(session, workspace)
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        if windows and not vswhere.is_file():
            raise ValueError("Install Visual Studio 2022 Build Tools with Desktop development with C++ and a Windows SDK, then resume. "
                             "The wizard does not silently install system-wide/admin components.")
        def snapshot():
            copy_sources(project, workspace)
            inventory = project / "release-sources.json"
            if inventory.is_file() and not (project / ".git").exists():
                bundle = json.loads(inventory.read_text())
                state["source_revision"] = bundle.get("revision")
                state["source_release"] = bundle.get("releases", {}).get(state["source_revision"])
        stage("snapshot sources", snapshot)
        if profile.adapter_family == "quizworld-102-verified":
            stage("stage update content", lambda: stage_update_content(executable.parent / "Patched", workspace, cancel, log))
        model.atomic_text(workspace / "buzz-paths.json", json.dumps(state["paths"], indent=2))
        exe = build_workspace(project, workspace, profile, inputs.jobs, log, cancel, stage, standalone, linux)
        result = fingerprint(exe)
        if state.get("built_exe") and state["built_exe"] != result:
            if not accept_rebuilt:
                raise ValueError("The completed session's executable changed; it is not silently trusted. "
                                 "Start a new session, or explicitly verify a rebuild with --accept-rebuilt.")
            manifest = json.loads((exe.parent / "game-build.json").read_text(encoding="utf-8-sig"))
            if manifest.get("elf_sha256") != profile.elf_sha256 or manifest.get("exe_sha256") != result["sha256"]:
                raise ValueError("Rebuild does not match the executable/build manifest; registration refused.")
            log("Verified explicitly rebuilt EXE against its version-bound build manifest.")
        state["built_exe"] = result
        save()
        if inputs.upscaler:
            from . import upscaler_install
            # Shared download cache: later sessions reuse the verified archive.
            stage("install texture upscaler", lambda: upscaler_install.install(
                workspace / "runtime", log, cancel, cache=project / "runtime/setup/downloads"))
        if standalone:
            manifest = json.loads((exe.parent / "game-build.json").read_text(encoding="utf-8-sig"))
            if (manifest.get("renderer") != ("metal" if macos else "vulkan") or manifest.get("elf_sha256") != profile.elf_sha256 or
                    manifest.get("exe_sha256") != result["sha256"] or not manifest.get("standalone")):
                raise ValueError("The executable does not match the standalone build manifest.")
            publish_distribution(project, workspace, state["paths"], exe, log, cancel,
                                 state.get("source_revision"), state.get("source_release"))
            return session, state
        model.atomic_text(project / "runtime/setup/last-ready.json", json.dumps({"session": str(session), "workspace": str(workspace)}, indent=2))
        log("Ready. Use Play to open this build's controller/game launcher.")
        return session, state
    finally:
        lock.unlink()


def firmware_identity(path):
    path = Path(path)
    if path.is_file():
        return fingerprint(path)
    return {"path": str(path.resolve()), "atrac": fingerprint(path / "sys/external/flashATRAC.pic")}
