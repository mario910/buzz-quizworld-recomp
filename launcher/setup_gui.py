"""Preparation wizard; all expensive work is off the Tk thread."""
from pathlib import Path
import json
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import model
from .setup_pipeline import Inputs, Cancelled, prepare, fingerprint


class Wizard:
    def __init__(self, root, project=model.ROOT):
        self.root, self.project = root, Path(project)
        self.events = queue.Queue()
        self.cancel = threading.Event()
        self.busy = False
        self.session = None
        self.ready = None
        try:
            initial = model.load(self.project).paths
        except (ValueError, OSError):
            initial = {}
        keys = self.project / "runtime/keys/ps3.keys"
        self.fields = {key: tk.StringVar(value=value) for key, value in dict(
            game=initial.get("game_root", ""), firmware=initial.get("dev_flash", ""),
            keys=str(keys.resolve()) if keys.is_file() else "", update="").items()}
        self.jobs = tk.StringVar(value="6")
        self.import_only = tk.BooleanVar(value=False)
        self.no_update = tk.BooleanVar(value=False)
        self.status = tk.StringVar(value="Your source files stay unchanged. Imports and builds are stored in runtime/setup.")
        root.title("Buzz! Quiz World • Prepare for PC")
        root.geometry("980x740")
        root.minsize(850, 620)
        frame = ttk.Frame(root, padding=20)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Prepare your copy of Buzz! Quiz World", font=("Segoe UI", 18, "bold")).pack(anchor="w")
        ttk.Label(frame, text="Prepare your ISO/dump and firmware directly. No RPCS3 installation is required. Encrypted inputs need a private local key file (see docs/game-import.md).", wraplength=920).pack(anchor="w", pady=(6, 14))
        labels = dict(game="Game: ISO / folder", firmware="Firmware: PUP / dev_flash", keys="Decryption keys (local file)", update="Game update: PKG / EBOOT (optional)")
        self.entries = []
        for key, variable in self.fields.items():
            row = ttk.Frame(frame)
            row.pack(fill="x", pady=5)
            ttk.Label(row, text=labels[key], width=34).pack(side="left")
            entry = ttk.Entry(row, textvariable=variable)
            entry.pack(side="left", fill="x", expand=True, padx=6)
            self.entries.append(entry)
            button = ttk.Button(row, text="File…", command=lambda k=key: self.browse(k))
            button.pack(side="left")
            self.entries.append(button)
            if key in ("game", "firmware"):
                directory = ttk.Button(row, text="Folder…", command=lambda k=key: self.browse(k, True))
                directory.pack(side="left", padx=(4, 0))
                self.entries.append(directory)
        options = ttk.Frame(frame)
        options.pack(fill="x", pady=12)
        for text, variable in (("Use the disc EBOOT (ignore the selected update)", self.no_update),
                               ("Import only, without building", self.import_only)):
            check = ttk.Checkbutton(options, text=text, variable=variable)
            check.pack(anchor="w")
            self.entries.append(check)
        row = ttk.Frame(options)
        row.pack(fill="x", pady=5)
        ttk.Label(row, text="Parallel compiler jobs:").pack(side="left")
        jobs = ttk.Spinbox(row, from_=1, to=64, textvariable=self.jobs, width=5)
        jobs.pack(side="left", padx=8)
        self.entries.append(jobs)
        ttk.Label(options, text="Profiles: EU 1.00 (BCES00644/645/647), US 1.00 (BCUS98209) and BCES00647 1.02. Compatibility is checked by executable hash; see README for the tested gameplay scope.", wraplength=910).pack(anchor="w", pady=4)
        self.output = tk.Text(frame, wrap="word", height=15, state="disabled")
        self.output.pack(fill="both", expand=True, pady=(5, 10))
        ttk.Label(frame, textvariable=self.status, wraplength=910).pack(anchor="w", fill="x")
        controls = ttk.Frame(frame)
        controls.pack(fill="x", pady=(10, 0))
        self.start_button = ttk.Button(controls, text="Prepare game", command=self.start)
        self.start_button.pack(side="left")
        self.resume_button = ttk.Button(controls, text="Resume session…", command=self.resume)
        self.resume_button.pack(side="left", padx=6)
        self.stop_button = ttk.Button(controls, text="Cancel", command=self.cancel.set, state="disabled")
        self.stop_button.pack(side="left")
        self.play_button = ttk.Button(controls, text="Play / configure controllers", command=self.play, state="disabled")
        self.play_button.pack(side="right")
        self.load_ready()
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.poll_id = root.after(100, self.poll)

    def browse(self, key, directory=False):
        if self.busy:
            return
        titles = dict(game="Select your game ISO or dump folder",
                      firmware="Select PS3 firmware or dev_flash",
                      keys="Select your private PS3 decryption key file",
                      update="Select an optional game update")
        if directory:
            value = filedialog.askdirectory(parent=self.root, title=titles[key])
        else:
            types = dict(game=[("PS3 ISO", "*.iso")], firmware=[("PS3 firmware", "*.PUP *.pup")],
                         keys=[("PS3 key file", "*.keys *.json"), ("All files", "*")], update=[("Game update", "*.pkg *.elf *.BIN *.bin")])
            value = filedialog.askopenfilename(parent=self.root, title=titles[key], filetypes=types[key])
        if value:
            self.fields[key].set(value)

    def append(self, text):
        self.output.configure(state="normal")
        self.output.insert("end", text.rstrip() + "\n")
        if int(self.output.index("end-1c").split(".")[0]) > 2000:
            self.output.delete("1.0", "500.0")
        self.output.see("end")
        self.output.configure(state="disabled")

    def set_busy(self, value):
        self.busy = value
        for widget in (*self.entries, self.start_button, self.resume_button):
            widget.configure(state="disabled" if value else "normal")
        self.stop_button.configure(state="normal" if value else "disabled")
        self.play_button.configure(state="normal" if self.ready and not value else "disabled")

    def start(self, resume=None):
        if self.busy:
            return
        try:
            inputs = Inputs(**{key: variable.get().strip() for key, variable in self.fields.items()}, jobs=int(self.jobs.get()))
            if self.no_update.get():
                inputs.update = ""
            inputs.validate()
        except ValueError as exc:
            messagebox.showerror("Check files", str(exc), parent=self.root)
            return
        self.cancel.clear()
        self.set_busy(True)
        self.status.set("Preparation in progress — see the log for details.")
        build = not self.import_only.get()
        def worker():
            try:
                result = prepare(inputs, self.project, lambda text: self.events.put(("log", text)), self.cancel,
                                 resume=resume, build=build)
                self.events.put(("done", result))
            except Exception as exc:
                self.events.put(("error", str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def resume(self):
        if self.busy:
            return
        statefile = filedialog.askopenfilename(parent=self.root, initialdir=self.project / "runtime/setup/sessions",
                                               filetypes=[("Preparation state", "state.json")])
        if not statefile:
            return
        try:
            state = json.loads(Path(statefile).read_text(encoding="utf-8"))
            if "helper" in state["inputs"]:
                raise ValueError("This session used the old RPCS3 importer. Start a new session with the built-in importer; the old files are preserved.")
            values = Inputs(**state["inputs"])
            for key, variable in self.fields.items():
                variable.set(getattr(values, key))
            self.jobs.set(str(values.jobs))
            self.no_update.set(not bool(values.update))
        except (OSError, ValueError, KeyError, TypeError) as exc:
            messagebox.showerror("Invalid session", str(exc), parent=self.root)
            return
        self.start(resume=Path(statefile).parent)

    def load_ready(self):
        from .active_build import ready_workspace
        self.ready = ready_workspace(self.project)
        self.play_button.configure(state="normal" if self.ready else "disabled")

    def play(self):
        if self.busy or not self.ready:
            return
        name = "BuzzLauncher.exe" if os.name == "nt" else "BuzzLauncher"
        native = self.ready / "build-launcher" / name
        if not native.is_file():
            native = self.project / "build-launcher" / name
        if not native.is_file():
            self.status.set("Build the native launcher first (launcher/native or scripts/build-launcher.ps1).")
            return
        subprocess.Popen([str(native), "--project", str(self.ready)], cwd=self.project,
                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

    def poll(self):
        try:
            for _ in range(100):
                kind, data = self.events.get_nowait()
                if kind == "log":
                    self.append(data)
                    if data.startswith("Session: "):
                        self.session = Path(data[len("Session: "):])
                else:
                    self.set_busy(False)
                    if kind == "error":
                        self.status.set(data)
                        self.append("STOP: " + data)
                    else:
                        self.session, state = data
                        self.status.set("Ready: " + ("game build" if state.get("built_exe") else "import / analysis (no playable build)"))
                        self.load_ready()
                        self.set_busy(False)
        except queue.Empty:
            pass
        self.poll_id = self.root.after(100, self.poll)

    def close(self):
        if self.busy:
            messagebox.showinfo("Preparation in progress", "Choose Cancel first and wait for the build processes to stop.", parent=self.root)
            return
        self.root.after_cancel(self.poll_id)
        self.root.destroy()


def run(smoke=False, project=model.ROOT):
    root = tk.Tk()
    if smoke:
        root.withdraw()
    wizard = Wizard(root, project)
    if smoke:
        root.update_idletasks()
        root.after_cancel(wizard.poll_id)
        assert root.state() == "withdrawn" and not wizard.busy
        root.destroy()
        print("PASS: preparation wizard constructed withdrawn; no processes/input started")
    else:
        root.mainloop()
