"""Select only a completed, unchanged build owned by this preparation tree."""
import json
import hashlib
from pathlib import Path
from .setup_pipeline import fingerprint
from launcher.paths import EXPECTED_ELF_SHA256


def ready_workspace(project):
    project = Path(project).resolve()
    try:
        ready = json.loads((project / "runtime/setup/last-ready.json").read_text(encoding="utf-8"))
        session = Path(ready["session"]).resolve(strict=True)
        if session.parent != (project / "runtime/setup/sessions").resolve():
            return None
        workspace = (session / "workspace").resolve(strict=True)
        if workspace.parent != session or Path(ready["workspace"]).resolve() != workspace:
            return None
        state = json.loads((session / "state.json").read_text(encoding="utf-8"))
        if fingerprint(workspace / "build-windows/BuzzQuizWorldRecomp.exe") != state.get("built_exe"):
            return None
        return workspace
    except (OSError, ValueError, KeyError, TypeError):
        return None


def matching_workspace(project, elf_sha256, title_id):
    """Choose an existing matching build, never run an ELF on another family.

    A GUI opened in a managed workspace can discover its sibling builds, but
    not unrelated directories or paths named by an arbitrary state file.
    """
    project = Path(project).resolve()
    catalog = project
    if (project.name == "workspace" and len(project.parents) >= 5 and
            project.parents[1].name == "sessions" and project.parents[2].name == "setup" and
            project.parents[3].name == "runtime"):
        catalog = project.parents[4]

    def checked(workspace):
        exe = workspace / "build-windows/BuzzQuizWorldRecomp.exe"
        if not exe.is_file():
            return False
        try:
            manifest = workspace / "build-windows/game-build.json"
            if manifest.is_file():
                metadata = json.loads(manifest.read_text(encoding="utf-8-sig"))
                if metadata["elf_sha256"] != elf_sha256:
                    return False
                with exe.open("rb") as stream:
                    return hashlib.file_digest(stream, "sha256").hexdigest() == metadata["exe_sha256"]
            # Earlier prepared 1.02 builds predate game-build.json but have an
            # independently recorded source/EXE identity in their session.
            session = workspace.parent
            if session.parent == (catalog / "runtime/setup/sessions").resolve():
                state = json.loads((session / "state.json").read_text(encoding="utf-8"))
                return (state["profile"]["elf_sha256"] == elf_sha256 and
                        state["profile"]["title_id"] == title_id and
                        fingerprint(exe) == state.get("built_exe"))
            return workspace == catalog and elf_sha256 == EXPECTED_ELF_SHA256 and title_id == "BCES00647"
        except (OSError, ValueError, TypeError, KeyError):
            return False

    if checked(project):
        return project
    sessions = catalog / "runtime/setup/sessions"
    if sessions.is_dir():
        for session in sorted(sessions.iterdir(), key=lambda p: p.name, reverse=True):
            workspace = (session / "workspace").resolve()
            if session.resolve().parent == sessions.resolve() and workspace.parent == session.resolve() and checked(workspace):
                return workspace
    return None
