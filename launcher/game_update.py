"""Rebuild a prepared standalone game using its installed disc, firmware and ELF."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import tempfile

from . import model, update_sources
from .game_profiles import identify
from .setup_pipeline import (build_workspace, check_cancel, fingerprint,
                             reject_links_below)


@contextmanager
def game_lock(dist):
    lock = dist / "runtime/.game.lock"
    reject_links_below(dist, lock)
    try:
        handle = lock.open("ab")
    except PermissionError as error:
        raise ValueError("Close the game and check that the installation is writable.") from error
    with handle:
        if os.name == "nt":
            import msvcrt
            handle.seek(0)
            try:
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise ValueError("Close the game or other updater and retry.") from error
        else:
            import fcntl
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise ValueError("Close the game or other updater and retry.") from error
        yield


def manifest_for(exe, elf_hash):
    manifest = json.loads((exe.parent / "game-build.json").read_text(encoding="utf-8-sig"))
    if (manifest.get("standalone") is not True or manifest.get("renderer") != "vulkan"
            or manifest.get("elf_sha256") != elf_hash
            or manifest.get("exe_sha256") != fingerprint(exe)["sha256"]):
        raise ValueError("The executable and its standalone build manifest do not match this game.")
    return manifest


def recover_installation(dist):
    previous = dist / ".buzz-update-previous"
    if not previous.exists():
        return
    reject_links_below(dist, previous / "files.json")
    names = json.loads((previous / "files.json").read_text())
    with tempfile.TemporaryDirectory(prefix=".restore-", dir=dist) as temp:
        work = Path(temp)
        for name in names:
            if (Path(name).name != name or name not in ("BuzzQuizWorldRecomp", "BuzzQuizWorldRecomp.exe", "game-build.json")
                    and not name.lower().endswith(".dll")):
                raise ValueError("Invalid file in the interrupted game update.")
            target, saved = dist / name, previous / name
            reject_links_below(dist, target)
            reject_links_below(previous, saved)
            if saved.exists():
                shutil.copy2(saved, work / name)
                os.replace(work / name, target)
            else:
                target.unlink(missing_ok=True)
        os.replace(previous, work / "recovered")


def install_build(dist, exe, elf_hash):
    manifest_for(exe, elf_hash)
    files = [*sorted(exe.parent.glob("*.dll")), exe, exe.parent / "game-build.json"]
    with tempfile.TemporaryDirectory(prefix=".update-", dir=dist) as temp:
        stage = Path(temp)
        backup = stage / "previous"
        backup.mkdir()
        for source in files:
            target = dist / source.name
            reject_links_below(dist, target)
            shutil.copy2(source, stage / source.name)
            if target.exists():
                shutil.copy2(target, backup / target.name)
        manifest_for(stage / exe.name, elf_hash)
        # Publish the complete backup before replacing the first live file.
        # A killed updater can restore this set on the next run.
        (backup / "files.json").write_text(json.dumps([source.name for source in files]))
        recovery = dist / ".buzz-update-previous"
        os.replace(backup, recovery)
        try:
            for source in files:
                os.replace(stage / source.name, dist / source.name)
            os.replace(recovery, backup)
        except OSError:
            recover_installation(dist)
            raise


def update_game(project, dist, jobs, log, cancel, *, bundled_only=False):
    project = Path(project).resolve(strict=True)
    dist = Path(dist).resolve(strict=True)
    runtime = dist / "runtime"
    exe = dist / ("BuzzQuizWorldRecomp.exe" if os.name == "nt" else "BuzzQuizWorldRecomp")
    paths = dict(game_root=runtime / "disc", dev_flash=runtime / "dev_flash", eboot_elf=runtime / "EBOOT.elf")
    def stage(name, action):
        check_cancel(cancel)
        log(name)
        action()
        check_cancel(cancel)

    with game_lock(dist):
        recover_installation(dist)
        elf_hash = fingerprint(paths["eboot_elf"], cancel)["sha256"]
        profile = identify(model.title_id((paths["game_root"] / "PS3_GAME/PARAM.SFO").read_bytes()), elf_hash)
        installed = manifest_for(exe, elf_hash)
        if not paths["dev_flash"].is_dir():
            raise ValueError("The prepared game is missing its dev_flash folder.")
        host = project / "runtime/setup"
        host.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="update-", dir=host) as temp:
            workspace = Path(temp)
            if bundled_only:
                revision, tag = update_sources.bundled_upgrade(project, workspace, installed)
            else:
                revision, tag = update_sources.select(project, workspace, installed.get("source_revision"), log)
            if (revision or bundled_only) and revision == installed.get("source_revision"):
                log("No game update is needed.")
                return
            extra = []
            pgo_hash = installed.get("pgo_profile_sha256")
            if os.name != "nt" and pgo_hash:
                pgo = Path(installed.get("cmake_options", {}).get("BUZZ_PGO_PROFILE", ""))
                if pgo.is_file() and fingerprint(pgo)["sha256"] == pgo_hash:
                    extra = ["--pgo-profile", str(pgo.resolve())]
                else:
                    log("The previous compiler profile is unavailable. Using the standard optimized build.")
            model.atomic_text(workspace / "buzz-paths.json", json.dumps({k: str(v) for k, v in paths.items()}))
            built = build_workspace(project, workspace, profile, jobs, log, cancel, stage, True, os.name != "nt", extra)
            check_cancel(cancel)
            manifest = manifest_for(built, elf_hash)
            if revision:
                manifest["source_revision"] = revision
                if tag:
                    manifest["source_release"] = tag
                model.atomic_text(built.parent / "game-build.json", json.dumps(manifest))
            install_build(dist, built, elf_hash)
    log("Game updated. Your existing profiles, saves and settings are ready to use.")
