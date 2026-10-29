"""Double-click entry point when Python is associated with .pyw files."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
if __name__ == '__main__':
    try:
        # Keep the same error log for normal GUI, preparation and managed builds.
        import runpy
        runpy.run_module('launcher', run_name='__main__')
    except Exception:
        import traceback
        from pathlib import Path
        from tkinter import messagebox
        log = Path(__file__).resolve().parents[1] / 'runtime/launcher/launcher-error.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(traceback.format_exc(), encoding='utf-8')
        messagebox.showerror('Buzz PC Launcher', f'Could not start the launcher. Details:\n{log}')
