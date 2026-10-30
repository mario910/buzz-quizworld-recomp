"""Private, resumable import/build pipeline. No game downloads or desktop input.

RPCS3 is a separately supplied preparation helper, never the game runtime.
Its executable, firmware and game files are not copied into public sources.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import threading
import time
import uuid

from . import model
from .game_profiles import identify, KNOWN_TITLE_IDS

MAX_GAME_BYTES = 64 * 1024**3
MAX_FILES = 200000


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
    helper: str
    update: str = ""
    jobs: int = 6

    def validate(self):
        if type(self.jobs) is not int or not 1 <= self.jobs <= 64:
            raise ValueError("Compiler jobs must be 1–64.")
        for value in (self.game, self.firmware, self.helper):
            if not value or not Path(value).is_absolute() or not Path(value).exists():
                raise ValueError("Choose full paths to an ISO/dump, PS3UPDAT.PUP/dev_flash and RPCS3 executable.")
        if Path(self.game).is_file() and Path(self.game).suffix.lower() != ".iso":
            raise ValueError("Game input must be an ISO or a directory containing PS3_GAME.")
        if not Path(self.helper).is_file():
            raise ValueError("RPCS3 helper must be an executable file.")
        if self.update and (not Path(self.update).is_absolute() or not Path(self.update).is_file()):
            raise ValueError("Optional game update must be a local PKG, EBOOT.BIN or EBOOT.elf.")
        if self.update and Path(self.update).suffix.lower() not in (".pkg", ".bin", ".elf"):
            raise ValueError("Unsupported update file type.")


def fingerprint(path, cancel=None, log=None):
    path = Path(path).resolve(strict=True)
    if path.is_dir():
        # A folder is used read-only, not copied. Track its executable and SFO.
        return {"path": str(path), "sfo": fingerprint(path / "PS3_GAME/PARAM.SFO", cancel, log),
                "eboot": fingerprint(path / "PS3_GAME/USRDIR/EBOOT.BIN", cancel, log)}
    size = path.stat().st_size
    hasher, done, last = hashlib.sha256(), 0, time.monotonic()
    with path.open("rb") as stream:
        while chunk := stream.read(8 * 1024**2):
            if cancel is not None and cancel.is_set():
                raise Cancelled("Input verification cancelled.")
            hasher.update(chunk)
            done += len(chunk)
            if log and time.monotonic() - last > 2:
                log(f"Hash: {path.name}: {done / 1024**2:.0f}/{size / 1024**2:.0f} MiB")
                last = time.monotonic()
    if path.stat().st_size != size:
        raise ValueError("Input changed during hashing; retry after it stops changing.")
    return {"path": str(path), "size": size, "sha256": hasher.hexdigest()}


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
        raise ValueError("No readable PS3_GAME/PARAM.SFO. The ISO may be encrypted or unsupported; "
                         "supply your own decrypted ISO/dump. No keys are fetched automatically.")
    return mode, plan, total


def extract_iso(path, destination, log, cancel):
    from .disc_image import DiscImage
    image = DiscImage()
    opened = False
    try:
        image.open(str(path))
        opened = True
        if image.fallback_reason:
            log(image.fallback_reason)
        mode, plan, total = iso_plan(image)
        sfo = next(item for item in plan if item[0].as_posix() == "PS3_GAME/PARAM.SFO")
        if sfo[2] > 1024 * 1024:
            raise ValueError("Oversized PARAM.SFO.")
        buffer = io.BytesIO()
        image.get_file_from_iso_fp(buffer, **{mode: sfo[1]})
        if model.title_id(buffer.getvalue()) not in KNOWN_TITLE_IDS:
            raise ValueError("This title ID has no tested import/build profile yet.")
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
            with partial.open("xb") as output:
                image.get_file_from_iso_fp(Writer(output, size), **{mode: source})
            if partial.stat().st_size != size:
                raise ValueError("Truncated ISO extraction.")
            os.replace(partial, target)
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


def helper(inputs, arguments, home, log, cancel):
    home.mkdir(parents=True, exist_ok=True)
    (home / "log").mkdir(exist_ok=True)
    env = dict(os.environ)
    # The trailing separator matters: some builds normalize this as a path's parent.
    env["RPCS3_CONFIG_DIR"] = str(home.resolve()) + os.sep
    process([inputs.helper, "--headless", *arguments], home, log, cancel, env=env, timeout=600)


def firmware_validator(path):
    import importlib.util
    spec = importlib.util.spec_from_file_location("verify_game", Path(__file__).resolve().parents[1] / "tools/verify-game.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    verifier.verify_firmware(path)
    if not any((Path(path) / "data/font").glob("*.TTF")):
        raise ValueError("Firmware extraction has no PS3 fonts.")


def install_firmware(inputs, home, log, cancel):
    try:
        helper(inputs, ["--installfw", inputs.firmware], home, log, cancel)
    except ProcessFailure as exc:
        # RPCS3 0.0.41-19667 finishes installation, then aborts in the headless
        # fixed_typemap destructor. Accept ONLY this specific teardown failure,
        # an explicit success record, and independently verified required files.
        output = exc.logfile.read_text(encoding="utf-8", errors="replace")
        rpcslog = home / "log/RPCS3.log"
        success = rpcslog.is_file() and "Successfully installed PS3 firmware version" in rpcslog.read_text(encoding="utf-8", errors="replace")
        if exc.code != 0xC0000409 or "manual_typemap" not in output or "process teardown" not in output or not success:
            raise
        firmware_validator(home / "dev_flash")
        log("WARNING: RPCS3 completed firmware installation but crashed during its own teardown. "
            "Required audio-module hash and font files were verified independently; keeping the valid import. "
            f"The helper failure remains recorded in {exc.logfile}.")
        return
    firmware_validator(home / "dev_flash")


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


def copy_sources(project, destination):
    """Copy public working source, not .git, VFS, binaries or private analyses."""
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
            shutil.copy2(original, dest)


def prepare(inputs, project, log, cancel, *, resume=None, build=True, accept_rebuilt=False):
    inputs.validate()
    project = Path(project).resolve()
    sessions = project / "runtime/setup/sessions"
    sessions.mkdir(parents=True, exist_ok=True)
    session = Path(resume).resolve() if resume else sessions / uuid.uuid4().hex
    if session.parent != sessions.resolve() or session.is_symlink():
        raise ValueError("Resume path must be one of this project's own setup sessions.")
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
        identities = {"game": fingerprint(inputs.game, cancel, log), "helper": fingerprint(inputs.helper, cancel, log),
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
            stage("extract ISO", lambda: extract_iso(inputs.game, game, log, cancel))
            reject_links_below(session, game / "PS3_GAME/PARAM.SFO")
        disc_title = model.title_id((game / "PS3_GAME/PARAM.SFO").read_bytes())
        if disc_title not in KNOWN_TITLE_IDS:
            raise ValueError("Unsupported title ID; no files in the source dump were changed.")
        flash = Path(inputs.firmware)
        home = session / "helper"
        reject_links_below(session, home)
        if flash.is_file():
            stage("install firmware", lambda: install_firmware(inputs, home, log, cancel))
            flash = home / "dev_flash"
        executable = game / "PS3_GAME/USRDIR/EBOOT.BIN"
        if inputs.update:
            update = Path(inputs.update)
            if update.suffix.lower() == ".pkg":
                stage("install game update", lambda: helper(inputs, ["--installpkg", inputs.update], home, log, cancel))
                executable = home / f"dev_hdd0/game/{disc_title}/USRDIR/EBOOT.BIN"
            else:
                executable = update
        staged = session / "executable"
        reject_links_below(session, staged)
        staged.mkdir(exist_ok=True)
        elf = staged / "EBOOT.elf"
        reject_links_below(session, elf)
        def decode():
            with executable.open("rb") as stream:
                magic = stream.read(4)
            if magic == b"\x7fELF":
                shutil.copy2(executable, elf)
            elif magic == b"SCE\0":
                binary = staged / "EBOOT.BIN"
                shutil.copy2(executable, binary)
                helper(inputs, ["--decrypt", str(binary)], home, log, cancel)
            else:
                raise ValueError("Executable is not readable ELF/SELF. The ISO may still be encrypted.")
            if not elf.is_file() or elf.read_bytes()[:6] != b"\x7fELF\x02\x02":
                raise ValueError("Helper did not produce a big-endian ELF64. See the private helper log.")
        stage("prepare executable", decode)
        profile = identify(disc_title, fingerprint(elf)["sha256"])
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
        if not vswhere.is_file():
            raise ValueError("Install Visual Studio 2022 Build Tools with Desktop development with C++ and a Windows SDK, then resume. "
                             "The wizard does not silently install system-wide/admin components.")
        stage("snapshot sources", lambda: copy_sources(project, workspace))
        if profile.adapter_family == "quizworld-102-verified":
            stage("stage update content", lambda: stage_update_content(executable.parent / "Patched", workspace, cancel, log))
        model.atomic_text(workspace / "buzz-paths.json", json.dumps(state["paths"], indent=2))
        env = dict(os.environ, BUZZ_TOOL_ENV=str(project / "toolenv"))
        powershell = shutil.which("powershell.exe")
        if not powershell:
            raise ValueError("Windows PowerShell is required by the current Windows build.")
        def ps(script, *arguments):
            process([powershell, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", workspace / script, *arguments],
                    workspace, log, cancel, env=env, timeout=7200)
        stage("generate translations", lambda: ps("scripts/generate.ps1", "-Jobs", str(inputs.jobs)))
        stage("build native game", lambda: ps("scripts/Build-Buzz.ps1", "-SkipGeneration", "-Jobs", str(inputs.jobs)))
        exe = workspace / "build-windows/BuzzQuizWorldRecomp.exe"
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
