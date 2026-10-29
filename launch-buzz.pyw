"""Double-click entry point when Python is associated with .pyw files."""
if __name__ == '__main__':
    try:
        # Keep the same error log for normal GUI, preparation and managed builds.
        import runpy
        runpy.run_module('launcher', run_name='__main__')
    except Exception:
        import traceback
        from pathlib import Path
        from tkinter import messagebox
        log = Path(__file__).resolve().parent / 'runtime/launcher/launcher-error.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(traceback.format_exc(), encoding='utf-8')
        messagebox.showerror('Buzz PC Launcher', f'Could not start the launcher. Details:\n{log}')
