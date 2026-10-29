"""Withdrawn widget tests using fake input, never real desktop events."""
from pathlib import Path
import sys
import tempfile
import tkinter as tk
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from launcher.gui import App, COLORS


class UiTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.root=tk.Tk();self.root.withdraw()
        self.app=App(self.root,Path(self.temporary.name))
        self.root.update_idletasks()
        self.root.after_cancel(self.app.poll_id)

    def tearDown(self):
        try:self.root.after_cancel(self.app.poll_id)
        except tk.TclError:pass
        self.root.destroy();self.temporary.cleanup()

    def test_layout_and_controls(self):
        self.assertEqual(len(self.app.notebook.tabs()),3)
        self.assertEqual(len(self.app.tab_canvases),3)
        self.assertEqual(self.root.state(),'withdrawn')
        self.assertLess(self.root.winfo_reqheight(),600)
        self.app.places.set('8');self.app._update_places()
        self.app.key_vars[7][0].set('F1')
        self.assertEqual(self.app.collect().players[7].keys[0],'F1')

    def test_inactive_preview(self):
        self.app.gamepads.set(True);self.app.keys_down.add(32)
        class FakePads:
            connected=[False]*4
            def poll(self,active):
                if active:raise AssertionError('Inactive UI polled devices')
                return [0]*4
        self.app.pad_reader=FakePads()
        with patch.object(self.app,'active',return_value=False):self.app.poll()
        self.assertFalse(self.app.keys_down)
        self.assertEqual(self.app.last_masks,[0]*8)

    def test_pad_assignment_with_injected_input(self):
        class FakePads:
            connected=[True,False,False,False]
            def poll(self,active):return [0x10,0,0,0] if active else [0]*4
        self.app.pad_reader=FakePads();self.app.assign_player=1
        with patch.object(self.app,'active',return_value=True),patch('launcher.gui.messagebox.askyesno',return_value=True):
            self.app.poll()
        self.assertEqual(self.app.pad_players[0].get(),'Gracz 2')
        self.assertEqual(self.app.pad_players[1].get(),'Nieprzypisany')
        self.assertTrue(self.app.gamepads.get())
        self.assertEqual(self.app.last_masks[1],1)
        self.assertEqual(self.app.preview_canvas.itemcget(self.app.preview_items[1][0],'fill'),COLORS[0])

    def test_bad_partial_mapping_does_not_break_preview(self):
        self.app.pulse.set('not yet a number')
        with patch.object(self.app,'active',return_value=False):self.app.poll()
        self.assertEqual(self.app.last_masks,[0]*8)


if __name__=='__main__':unittest.main()
