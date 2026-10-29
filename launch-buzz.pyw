"""Double-click entry point when Python is associated with .pyw files."""
if __name__ == '__main__':
    try:
        from launcher.gui import run
        run()
    except Exception:
        import traceback
        from pathlib import Path
        from tkinter import messagebox
        log = Path(__file__).resolve().parent / 'runtime/launcher/launcher-error.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(traceback.format_exc(), encoding='utf-8')
        messagebox.showerror('Buzz PC Launcher', f'Nie udało się uruchomić launchera. Szczegóły:\n{log}')
