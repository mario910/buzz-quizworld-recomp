"""Tk desktop frontend. Input is local to this window; no global hooks."""
from __future__ import annotations

from dataclasses import asdict
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import uuid

from . import model
from .devices import Pads, foreground_owned

COLORS = ('#ec4658', '#f6cb45', '#4bd7a4', '#ffa34b', '#56a6ff')
BG, CARD, TEXT, MUTED = '#101625', '#1b2538', '#edf2fc', '#a8b6ce'


class App:
    def __init__(self, root: tk.Tk, project: Path = model.ROOT):
        self.root, self.project = root, project
        self.events = queue.Queue()
        self.busy = False
        self.keys_down: set[int] = set()
        self.last_masks = None
        self.pad_reader = Pads()
        self.previous_pad_states = [0] * 4
        self.assign_player = None
        self.load_error = None
        try:
            initial = model.load(project)
        except (ValueError, OSError, TypeError) as exc:
            initial, self.load_error = model.Settings(), str(exc)
        self.initial = initial
        root.title('Buzz! Quiz World • PC Launcher')
        root.geometry(f'{min(1120, root.winfo_screenwidth() - 60)}x{min(810, root.winfo_screenheight() - 100)}')
        root.minsize(960, 600)
        root.configure(bg=BG)
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('.', font=('Segoe UI', 10), background=BG, foreground=TEXT)
        style.configure('TFrame', background=BG)
        style.configure('Card.TFrame', background=CARD)
        style.configure('TLabel', background=BG, foreground=TEXT)
        style.configure('Muted.TLabel', foreground=MUTED)
        style.configure('Title.TLabel', font=('Segoe UI', 24, 'bold'))
        style.configure('TEntry', fieldbackground='#26334b', foreground=TEXT, insertcolor=TEXT, padding=6)
        style.configure('TCombobox', fieldbackground='#26334b', foreground=TEXT, padding=5)
        style.map('TCombobox', fieldbackground=[('readonly', '#26334b')], foreground=[('readonly', TEXT)])
        style.configure('TButton', padding=(12, 7), background='#2b3b59', foreground=TEXT)
        style.map('TButton', background=[('active', '#405879')])
        style.configure('Primary.TButton', font=('Segoe UI', 11, 'bold'), background='#7149db', padding=(18, 10))
        style.map('Primary.TButton', background=[('active', '#9067f5')])
        style.configure('TCheckbutton', background=BG, foreground=TEXT)
        style.map('TCheckbutton', background=[('active', BG)])
        style.configure('TNotebook', background=BG, borderwidth=0)
        style.configure('TNotebook.Tab', padding=(18, 9), background=CARD)
        style.map('TNotebook.Tab', background=[('selected', '#39466a')])
        root.option_add('*TCombobox*Listbox.background', '#26334b')
        root.option_add('*TCombobox*Listbox.foreground', TEXT)

        outer = ttk.Frame(root, padding=22)
        outer.pack(fill='both', expand=True)
        header = ttk.Frame(outer)
        header.pack(fill='x', pady=(0, 14))
        ttk.Label(header, text='BUZZ!  /  PC', style='Title.TLabel').pack(side='left')
        ttk.Label(header, text='Quiz World 1.02 • eksperymentalny port', style='Muted.TLabel').pack(side='right', anchor='s')
        self.notebook = ttk.Notebook(outer)
        self.notebook.pack(fill='both', expand=True)
        self.tab_canvases = []
        self.start_tab = self._tab('Start i pliki gry')
        self.players_tab = self._tab('Gracze i mapowanie')
        self.test_tab = self._tab('Test przycisków')
        self.paths = {key: tk.StringVar(value=value) for key, value in initial.paths.items()}
        self.places = tk.StringVar(value=str(initial.places))
        self.network = tk.StringVar(value='Offline — bez logowania PSN' if initial.offline else 'Oryginalna ścieżka sieciowa')
        self.hide_store = tk.BooleanVar(value=initial.hide_store)
        self.background = tk.BooleanVar(value=initial.background)
        self.gamepads = tk.BooleanVar(value=initial.gamepads)
        self.pulse = tk.StringVar(value=str(initial.pulse_ms))
        self.names = [tk.StringVar(value=p.name) for p in initial.players]
        self.key_vars = [[tk.StringVar(value=k) for k in p.keys] for p in initial.players]
        self.pad_players = [tk.StringVar(value='Nieprzypisany' if p.player is None else f'Gracz {p.player + 1}') for p in initial.pads]
        self.pad_buttons = [[tk.StringVar(value=b) for b in p.buttons] for p in initial.pads]
        self.pad_labels = []
        self._start_ui()
        self._players_ui()
        self._test_ui()

        footer = ttk.Frame(outer, padding=(0, 14, 0, 0))
        footer.pack(side='bottom', fill='x')
        # Allocate the footer before the expandable notebook on short displays.
        self.notebook.pack_forget()
        self.notebook.pack(fill='both', expand=True)
        self.status = tk.StringVar(value='Gotowy. Ustawienia nie zmieniają plików gry ani zapisanych profili.')
        status_label = ttk.Label(footer, textvariable=self.status, style='Muted.TLabel', wraplength=950)
        status_label.pack(side='top', fill='x', pady=(0, 8))
        footer.bind('<Configure>', lambda event: status_label.configure(wraplength=max(200, event.width)))
        controls = ttk.Frame(footer)
        controls.pack(side='bottom', anchor='e')
        self.check_button = ttk.Button(controls, text='Sprawdź pliki', command=self.check_files)
        self.check_button.pack(side='left', padx=5)
        self.save_button = ttk.Button(controls, text='Zapisz', command=self.save)
        self.save_button.pack(side='left', padx=5)
        self.launch_button = ttk.Button(controls, text='Uruchom grę', style='Primary.TButton', command=self.launch)
        self.launch_button.pack(side='left', padx=(5, 0))
        if self.load_error:
            self.status.set('Nie udało się wczytać ustawień: ' + self.load_error + ' Zapis nowego zestawu wymaga potwierdzenia.')
        root.bind('<KeyPress>', self._key_down)
        root.bind('<KeyRelease>', self._key_up)
        root.bind('<FocusOut>', lambda event: root.after_idle(self._clear_if_inactive))
        root.bind('<MouseWheel>', self._wheel)
        self.notebook.bind('<<NotebookTabChanged>>', self._tab_changed)
        self._update_places()
        self.poll_id = root.after(40, self.poll)

    def _tab(self, title):
        host = ttk.Frame(self.notebook)
        canvas = tk.Canvas(host, bg=BG, highlightthickness=0)
        scrollbar = ttk.Scrollbar(host, orient='vertical', command=canvas.yview)
        horizontal = ttk.Scrollbar(host, orient='horizontal', command=canvas.xview)
        canvas.configure(yscrollcommand=scrollbar.set, xscrollcommand=horizontal.set)
        horizontal.pack(side='bottom', fill='x')
        scrollbar.pack(side='right', fill='y')
        canvas.pack(side='left', fill='both', expand=True)
        inner = ttk.Frame(canvas, padding=18)
        item = canvas.create_window(0, 0, window=inner, anchor='nw')
        inner.bind('<Configure>', lambda event: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>', lambda event: canvas.itemconfigure(item, width=max(event.width, inner.winfo_reqwidth())))
        self.notebook.add(host, text=title)
        self.tab_canvases.append(canvas)
        return inner

    def _wheel(self, event):
        if not isinstance(event.widget, ttk.Combobox):
            self.tab_canvases[self.notebook.index('current')].yview_scroll(-int(event.delta / 120), 'units')

    def _tab_changed(self, event):
        self._cancel_assignment()
        self.keys_down.clear()
        # Only a user's active-window tab change can move focus internally.
        # Otherwise typing in the preview could edit a previously focused entry.
        if self.notebook.index('current') == 2 and self.active():
            self.preview_canvas.focus_set()

    def _start_ui(self):
        tab = self.start_tab
        ttk.Label(tab, text='Twoja kopia gry', font=('Segoe UI', 14, 'bold')).pack(anchor='w')
        ttk.Label(tab, text='Wskaż własny dump BCES00647, firmware i odszyfrowany EBOOT aktualizacji 1.02.', style='Muted.TLabel').pack(anchor='w', pady=(4, 14))
        for key, label in (('game_root', 'Folder gry (z PS3_GAME)'), ('dev_flash', 'Folder dev_flash'), ('eboot_elf', 'Plik EBOOT.elf • update 1.02')):
            row = ttk.Frame(tab)
            row.pack(fill='x', pady=5)
            ttk.Label(row, text=label, width=28).pack(side='left')
            ttk.Entry(row, textvariable=self.paths[key]).pack(side='left', fill='x', expand=True, padx=(0, 10))
            ttk.Button(row, text='Wybierz…', command=lambda k=key: self.browse(k)).pack(side='right')
        ttk.Separator(tab).pack(fill='x', pady=22)
        ttk.Label(tab, text='Uruchomienie', font=('Segoe UI', 14, 'bold')).pack(anchor='w')
        row = ttk.Frame(tab)
        row.pack(fill='x', pady=12)
        ttk.Label(row, text='Tryb usług sieciowych', width=28).pack(side='left')
        ttk.Combobox(row, textvariable=self.network, state='readonly', width=38,
                     values=['Offline — bez logowania PSN', 'Oryginalna ścieżka sieciowa']).pack(side='left')
        ttk.Label(tab, text='Offline wyłącza logowanie, MyBuzz i grę sieciową. Drugi tryb korzysta z istniejącej konfiguracji usług;\nlauncher nie uruchamia serwera i nie emuluje PSN.', style='Muted.TLabel').pack(anchor='w', pady=(0, 14))
        ttk.Checkbutton(tab, text='Ukryj PlayStation Store w menu', variable=self.hide_store).pack(anchor='w', pady=5)
        ttk.Checkbutton(tab, text='Uruchom okno gry w tle, bez przejmowania fokusu', variable=self.background).pack(anchor='w', pady=5)
        ttk.Label(tab, text='Launcher zachowuje aktualne ustawienia wydajności. Nie przebudowuje EXE ani nie zmienia schedulerów.', style='Muted.TLabel', wraplength=900).pack(anchor='w', pady=(18, 0))

    def _players_ui(self):
        tab = self.players_tab
        options = ttk.Frame(tab)
        options.pack(fill='x', pady=(0, 10))
        ttk.Label(options, text='Dostępne miejsca:').pack(side='left')
        places = ttk.Combobox(options, textvariable=self.places, values=['4', '8'], state='readonly', width=4)
        places.pack(side='left', padx=(8, 20))
        places.bind('<<ComboboxSelected>>', lambda event: self._update_places())
        ttk.Checkbutton(options, text='Włącz pady XInput', variable=self.gamepads).pack(side='left')
        ttk.Label(options, text='Impuls (ms):').pack(side='left', padx=(20, 5))
        ttk.Entry(options, textvariable=self.pulse, width=6).pack(side='left')
        ttk.Label(tab, text='4/8 to dostępne buzzery — liczbę uczestników i tryb solo wybierzesz w grze. Nazwy poniżej są tylko etykietami launchera.', style='Muted.TLabel', wraplength=970).pack(anchor='w', pady=(0, 10))
        grid = ttk.Frame(tab)
        grid.pack(fill='x')
        ttk.Label(grid, text='Miejsce / nazwa', width=22).grid(row=0, column=0, sticky='w')
        for b, text in enumerate(model.BUTTONS):
            tk.Label(grid, text=text, bg=BG, fg=COLORS[b], font=('Segoe UI', 10, 'bold')).grid(row=0, column=b + 1, padx=5, pady=(0, 5))
        self.player_widgets = []
        for p in range(8):
            widgets = []
            entry = ttk.Entry(grid, textvariable=self.names[p], width=19)
            entry.grid(row=p + 1, column=0, padx=(0, 8), pady=3, sticky='ew')
            widgets.append(entry)
            for b in range(5):
                entry = ttk.Entry(grid, textvariable=self.key_vars[p][b], width=11)
                entry.grid(row=p + 1, column=b + 1, padx=5, pady=3, sticky='ew')
                widgets.append(entry)
            button = ttk.Button(grid, text='Nagraj 5 klawiszy', command=lambda n=p: self.capture(n))
            button.grid(row=p + 1, column=6, padx=(10, 0))
            widgets.append(button)
            self.player_widgets.append(widgets)
        ttk.Label(tab, text='Możesz wpisać np. Space, A, F1, 0x60 lub NONE. Kolejność: czerwony → żółty → zielony → pomarańczowy → niebieski.', style='Muted.TLabel', wraplength=970).pack(anchor='w', pady=10)
        ttk.Separator(tab).pack(fill='x', pady=(0, 8))
        pads = ttk.Frame(tab)
        pads.pack(fill='x')
        ttk.Label(pads, text='Pad / przypisanie').grid(row=0, column=0, sticky='w')
        for b, text in enumerate(model.BUTTONS):
            ttk.Label(pads, text=text).grid(row=0, column=b + 2, padx=6)
        choices = ['Nieprzypisany', *[f'Gracz {p}' for p in range(1, 9)]]
        for slot in range(4):
            label = ttk.Label(pads, text=f'Pad {slot + 1} • nie wykryto', width=25)
            label.grid(row=slot + 1, column=0, sticky='w', pady=3)
            self.pad_labels.append(label)
            ttk.Combobox(pads, textvariable=self.pad_players[slot], values=choices, state='readonly', width=15).grid(row=slot + 1, column=1, padx=5)
            for b in range(5):
                ttk.Combobox(pads, textvariable=self.pad_buttons[slot][b], values=list(model.PAD_BITS), state='readonly', width=8).grid(row=slot + 1, column=b + 2, padx=5)

    def _test_ui(self):
        ttk.Label(self.test_tab, text='Sprawdź swój buzzer', font=('Segoe UI', 16, 'bold')).pack(anchor='w')
        ttk.Label(self.test_tab, text='Kliknij tę zakładkę i naciskaj przyciski. Podgląd jest surowym wejściem, bez wydłużania impulsu USB.\nGdy launcher traci fokus, podgląd zostaje wyzerowany. Klawisze nie są wysyłane do żadnej aplikacji.', style='Muted.TLabel', wraplength=970).pack(anchor='w', pady=(6, 15))
        self.preview_canvas = tk.Canvas(self.test_tab, bg=BG, highlightthickness=0, height=400)
        self.preview_canvas.pack(fill='x')
        self.preview_items, self.preview_names = [], []
        for p in range(8):
            y = 25 + p * 47
            name = self.preview_canvas.create_text(8, y, text=f'Gracz {p + 1}', anchor='w', fill=TEXT, font=('Segoe UI', 11))
            self.preview_names.append(name)
            row = []
            for b in range(5):
                x = 200 + b * 135
                item = self.preview_canvas.create_rectangle(x, y - 15, x + 116, y + 15, fill=CARD, outline=COLORS[b], width=2)
                self.preview_canvas.create_text(x + 58, y, text=model.BUTTONS[b], fill=TEXT, font=('Segoe UI', 9))
                row.append(item)
            self.preview_items.append(row)
        row = ttk.Frame(self.test_tab)
        row.pack(fill='x', pady=12)
        ttk.Label(row, text='Szybkie przypisanie:').pack(side='left')
        self.assign_choice = tk.StringVar(value='Gracz 1')
        ttk.Combobox(row, textvariable=self.assign_choice, values=[f'Gracz {p}' for p in range(1, 9)], state='readonly', width=14).pack(side='left', padx=8)
        ttk.Button(row, text='Naciśnij START na padzie', command=self.arm_pad).pack(side='left')
        ttk.Label(self.test_tab, text='XInput obsługuje do 4 fizycznych padów. Na osiem miejsc można połączyć pady i klawiaturę.\nPad nie musi być podłączony, żeby zapisać jego mapowanie.', style='Muted.TLabel').pack(anchor='w')

    def _update_places(self):
        places = int(self.places.get())
        for p, widgets in enumerate(self.player_widgets):
            for widget in widgets:
                widget.configure(state='normal' if p < places else 'disabled')
        self.last_masks = None

    def collect(self) -> model.Settings:
        settings = model.Settings(paths={k: v.get().strip() for k, v in self.paths.items()},
            places=int(self.places.get()), offline=self.network.get().startswith('Offline'),
            hide_store=self.hide_store.get(), background=self.background.get(),
            gamepads=self.gamepads.get(), pulse_ms=int(self.pulse.get()),
            players=[model.Player(self.names[p].get().strip(), [v.get().strip() for v in self.key_vars[p]]) for p in range(8)],
            pads=[model.Pad(None if v.get() == 'Nieprzypisany' else int(v.get().split()[-1]) - 1,
                           [b.get() for b in self.pad_buttons[n]]) for n, v in enumerate(self.pad_players)])
        settings.validate()
        return settings

    def browse(self, key):
        if key == 'eboot_elf':
            chosen = filedialog.askopenfilename(parent=self.root, title='Odszyfrowany EBOOT.elf 1.02', filetypes=[('ELF', '*.elf'), ('Wszystkie pliki', '*')])
        else:
            chosen = filedialog.askdirectory(parent=self.root, title='Folder gry z PS3_GAME' if key == 'game_root' else 'Folder dev_flash')
        if chosen:
            self.paths[key].set(chosen)

    def capture(self, player):
        dialog = tk.Toplevel(self.root)
        dialog.title(f'Klawisze • {self.names[player].get()}')
        dialog.configure(bg=BG)
        dialog.transient(self.root)
        label = tk.Label(dialog, text='', bg=BG, fg=TEXT, font=('Segoe UI', 15), padx=35, pady=30)
        label.pack()
        ttk.Button(dialog, text='Anuluj (Esc)', command=dialog.destroy).pack(pady=(0, 20))
        recorded = []
        def update():
            label.configure(text=f'Naciśnij: {model.BUTTONS[len(recorded)]}\n{len(recorded) + 1} / 5', fg=COLORS[len(recorded)])
        def key(event):
            if event.keysym == 'Escape':
                dialog.destroy()
            elif 1 <= event.keycode <= 255 and event.keycode not in recorded:
                recorded.append(event.keycode)
                if len(recorded) == 5:
                    for b, code in enumerate(recorded):
                        self.key_vars[player][b].set(model.key_label(code))
                    dialog.destroy()
                    self.status.set(f'Zapisano pięć klawiszy dla: {self.names[player].get()} (jeszcze niezapisane na dysku).')
                else:
                    update()
            return 'break'
        dialog.bind('<KeyPress>', key)
        dialog.grab_set()  # Only this user-opened dialog, never an OS keyboard hook.
        dialog.focus_set()
        update()

    def _key_down(self, event):
        self.keys_down.add(event.keycode)
        if event.keysym == 'Escape' and self.assign_player is not None:
            self._cancel_assignment()
            self.status.set('Przypisywanie pada anulowane.')

    def _key_up(self, event):
        self.keys_down.discard(event.keycode)

    def active(self):
        return self.root.focus_displayof() is not None and foreground_owned()

    def _clear_if_inactive(self):
        if not self.active():
            self.keys_down.clear()
            self._cancel_assignment()

    def _cancel_assignment(self):
        self.assign_player = None

    def arm_pad(self):
        target = int(self.assign_choice.get().split()[-1]) - 1
        if target >= int(self.places.get()):
            messagebox.showwarning('Miejsce nieaktywne', 'Najpierw włącz 8 miejsc w zakładce graczy.', parent=self.root)
            return
        self.assign_player = target
        self.preview_canvas.focus_set()
        self.status.set(f'Naciśnij START na padzie dla {self.names[target].get()}. Esc lub zmiana zakładki anuluje.')

    def poll(self):
        focused = self.active()
        if not focused:
            self.keys_down.clear()
        states = self.pad_reader.poll(focused and (self.gamepads.get() or self.assign_player is not None))
        if self.assign_player is not None and focused:
            for slot, state in enumerate(states):
                if state & 0x10 and not self.previous_pad_states[slot] & 0x10:
                    player = self.assign_player
                    assigned = f'Gracz {player + 1}'
                    owner = self.pad_players[slot].get()
                    if owner not in ('Nieprzypisany', assigned) and not messagebox.askyesno('Przepisać pad?', f'Pad {slot + 1} jest przypisany do {owner}. Przepisać go do {assigned}?', parent=self.root):
                        self._cancel_assignment()
                        break
                    for other in self.pad_players:
                        if other.get() == assigned:
                            other.set('Nieprzypisany')
                    self.pad_players[slot].set(assigned)
                    self.gamepads.set(True)
                    self._cancel_assignment()
                    self.status.set(f'Pad {slot + 1} → {self.names[player].get()}. Zapisz ustawienia przed grą.')
                    break
        self.previous_pad_states = states
        for n, label in enumerate(self.pad_labels):
            state = ('test wstrzymany' if not focused else 'wyłączony' if not self.gamepads.get() and self.assign_player is None
                     else 'połączony' if self.pad_reader.connected[n] else 'nie wykryto')
            label.configure(text=f'Pad {n + 1} • {state}')
        try:
            settings = self.collect()
            masks = model.preview(settings, self.keys_down if focused else set(), states)
        except ValueError:
            masks = [0] * 8
        if masks != self.last_masks:
            for p, row in enumerate(self.preview_items):
                self.preview_canvas.itemconfigure(self.preview_names[p], text=self.names[p].get(), fill=TEXT if p < int(self.places.get()) else '#536079')
                for b, item in enumerate(row):
                    self.preview_canvas.itemconfigure(item, fill=COLORS[b] if masks[p] & (1 << b) else CARD)
            self.last_masks = masks
        try:
            while True:
                callback, arguments = self.events.get_nowait()
                callback(*arguments)
        except queue.Empty:
            pass
        self.poll_id = self.root.after(40, self.poll)

    def save(self, settings=None):
        try:
            settings = self.collect() if settings is None else settings
            if self.load_error:
                if not messagebox.askyesno('Nowy zestaw ustawień', 'Nie udało się wczytać poprzednich ustawień. Zapisać obecny zestaw? Oryginał zostanie zachowany jako kopia.', parent=self.root):
                    return False
                old = self.project / 'runtime/launcher/settings.json'
                if old.exists():
                    shutil.copy2(old, old.with_name('settings-backup-' + uuid.uuid4().hex[:8] + '.json'))
            model.save(settings, self.project)
            self.load_error = None
            self.status.set('Ustawienia zapisane. Pliki gry i profile pozostały bez zmian.')
            return True
        except (OSError, ValueError) as exc:
            messagebox.showerror('Nie udało się zapisać', str(exc), parent=self.root)
            return False

    def set_busy(self, busy):
        self.busy = busy
        for widget in (self.check_button, self.save_button, self.launch_button):
            widget.configure(state='disabled' if busy else 'normal')

    def check_files(self, launch=False):
        if self.busy:
            return
        try:
            settings = self.collect()
            if launch:
                warnings = settings.warnings()
                if warnings and not messagebox.askyesno('Sprawdź przypisania', '\n'.join(warnings) + '\n\nUruchomić mimo to?', parent=self.root):
                    return
        except ValueError as exc:
            messagebox.showerror('Sprawdź ustawienia', str(exc), parent=self.root)
            return
        self.set_busy(True)
        self.status.set('Sprawdzam pliki i SHA-256 aktualizacji 1.02…')
        def work():
            try:
                model.validate_files(settings, self.project)
                self.events.put((self.checked, (settings, launch, None)))
            except (OSError, ValueError) as exc:
                self.events.put((self.checked, (settings, launch, str(exc))))
        threading.Thread(target=work, daemon=True).start()

    def checked(self, settings, launch, error):
        self.set_busy(False)
        if error:
            self.status.set(error)
            messagebox.showerror('Nie można uruchomić gry', error, parent=self.root)
            return
        self.status.set('Pliki prawidłowe: BCES00647, aktualizacja 1.02, firmware i zbudowany EXE.')
        if launch:
            try:
                if asdict(self.collect()) != asdict(settings):
                    self.status.set('Ustawienia zmieniły się podczas sprawdzania. Kliknij Uruchom ponownie, aby użyć nowego zestawu.')
                    return
            except ValueError as exc:
                self.status.set(str(exc))
                return
        if not launch or not self.save(settings):
            return
        try:
            command, env = model.plan_launch(settings, self.project)
        except (OSError, ValueError) as exc:
            messagebox.showerror('Błąd konfiguracji startowej', str(exc), parent=self.root)
            return
        self.set_busy(True)
        self.status.set('Uruchamiam zapisany zestaw. Zamknięcie launchera nie zamknie gry.')
        def work():
            try:
                result = subprocess.run(command, cwd=self.project, env=env, capture_output=True,
                                        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), timeout=45)
                output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                model.atomic_text(self.project / 'runtime/launcher/last-start.log', output)
                self.events.put((self.started, (result.returncode, output)))
            except (OSError, subprocess.SubprocessError) as exc:
                self.events.put((self.started, (-1, str(exc))))
        threading.Thread(target=work, daemon=True).start()

    def started(self, code, output):
        self.set_busy(False)
        if code:
            self.status.set('Nie udało się uruchomić gry. Szczegóły: runtime/launcher/last-start.log')
            messagebox.showerror('Start nie powiódł się', output[-1800:], parent=self.root)
        else:
            self.status.set('Gra uruchomiona. Przy starcie w tle kliknij jej okno, aby sterować klawiaturą.')

    def launch(self):
        self.check_files(launch=True)


def run(smoke=False, project=model.ROOT):
    root = tk.Tk()
    if smoke:
        root.withdraw()  # No window activation/display in automated layout tests.
    app = App(root, project)
    if smoke:
        root.update_idletasks()
        settings = app.collect()
        assert settings.places in (4, 8) and len(app.player_widgets) == 8
        assert app.launch_button.winfo_reqwidth() > 0
        print('PASS: launcher UI constructed withdrawn; 8 player rows, 4 pad rows, 3 tabs; no game launched')
        root.after_cancel(app.poll_id)
        root.destroy()
    else:
        root.mainloop()
