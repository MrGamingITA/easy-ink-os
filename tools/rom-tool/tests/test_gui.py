"""Avvio rapido della GUI con backend finto: nessuna porta vera viene aperta."""

from __future__ import annotations

import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))

# Tk 9 su macOS va in segmentation fault se si crea un secondo Tk() dopo aver
# distrutto il primo: un solo root per tutto il modulo, una Toplevel per test.
try:
    import tkinter as tk
    ROOT = tk.Tk()
    ROOT.withdraw()
    TK_OK = True
except Exception:  # niente display o niente Tk
    ROOT = None
    TK_OK = False

import romcore as rc  # noqa: E402
from fakes import FakeBackend, FakePortInfo  # noqa: E402

KB = 1024
ONE_PORT = [FakePortInfo("/dev/cu.usbserial-FINTA", 0x10C4, 0xEA60, "CP2102 finto")]


@unittest.skipUnless(TK_OK, "Tkinter non disponibile")
class TestGui(unittest.TestCase):
    def setUp(self):
        import rom_gui
        self.rom_gui = rom_gui
        self.mb = mock.patch.object(rom_gui, "messagebox")
        self.messagebox = self.mb.start()
        self.messagebox.askyesno.return_value = False
        self.addCleanup(self.mb.stop)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = tk.Toplevel(ROOT)
        self.addCleanup(self.root.destroy)

    def make_app(self, backend, ports=ONE_PORT, confirm=lambda s: None, save_name="out.bin", open_path=None):
        self.created = []

        def factory(log):
            self.created.append(backend)
            backend.log = log
            return backend
        return self.rom_gui.RomApp(
            self.root, backend_factory=factory,
            find_ports=lambda: rc.find_ports(lambda: ports),
            port_checker=lambda dev: None,
            ask_save=lambda folder, name: str(Path(self.tmp.name) / (save_name or name)),
            ask_open=lambda folder: open_path, confirm=confirm)

    def pump(self, app, timeout=10.0):
        end = time.monotonic() + timeout
        time.sleep(0.05)
        while time.monotonic() < end:
            self.root.update()
            if not app.busy and app.q.empty():
                for _ in range(5):
                    self.root.update()
                    time.sleep(0.02)
                if not app.busy and app.q.empty():
                    return
            time.sleep(0.02)
        self.fail("la GUI non è tornata libera")

    def test_single_port_autoselected_and_detect(self):
        be = FakeBackend(bytes(8 * 1024 * KB))
        app = self.make_app(be)
        self.assertEqual(app.selected_port(), "/dev/cu.usbserial-FINTA")
        app.on_detect()
        self.pump(app)
        self.assertEqual(app.info_vars["MAC"].get(), "aa:bb:cc:dd:48:e8")
        self.assertIn("8MB", app.info_vars["Flash: dimensione"].get())
        self.assertIn("GigaDevice", app.info_vars["Flash: produttore"].get())
        self.assertTrue(be.closed)

    def test_multiple_ports_require_choice(self):
        ports = ONE_PORT + [FakePortInfo("/dev/cu.wchusbserial2", 0x1A86, 0x55D4)]
        app = self.make_app(FakeBackend(bytes(64 * KB)), ports=ports)
        self.assertIsNone(app.selected_port())
        app.on_detect()
        self.messagebox.showwarning.assert_called_once()
        app.port_var.set(app.candidates[1].label())
        self.assertEqual(app.selected_port(), "/dev/cu.wchusbserial2")

    def test_read_backup_full_flow(self):
        flash = os.urandom(1024 * KB)
        be = FakeBackend(flash, faults={(256 * KB, 1): __import__("fakes").FakeSerialError("Invalid head of packet")})
        app = self.make_app(be)
        values = []
        orig = app._show_progress
        app._show_progress = lambda st: (values.append(st.percent), orig(st))
        app.on_read()  # senza rilevamento: prima rileva, poi legge
        self.pump(app)
        self.pump(app)
        out = Path(self.tmp.name) / "out.bin"
        self.assertTrue(out.exists(), "il backup non è stato scritto")
        self.assertEqual(out.read_bytes(), flash)
        self.assertTrue(Path(str(out) + ".sha256").exists())
        self.assertEqual(float(app.progress["value"]), 1000.0)
        self.assertGreater(len(values), 10)
        self.assertIn("Backup completato", app.progress_text.get())

    def test_write_cancelled_dialog_writes_nothing(self):
        be = FakeBackend(bytes(1024 * KB))
        fw = Path(self.tmp.name) / "fw.bin"
        fw.write_bytes(b"\x55" * 8192)
        app = self.make_app(be, confirm=lambda s: None, open_path=str(fw))
        app.on_detect()
        self.pump(app)
        app.on_write()
        self.pump(app)
        self.assertEqual(be.write_calls, [])

    def test_write_too_big_rejected_before_dialog(self):
        be = FakeBackend(bytes(64 * KB))
        fw = Path(self.tmp.name) / "big.bin"
        fw.write_bytes(b"\x00" * (128 * KB))
        confirm = mock.Mock(return_value="SCRIVI")
        app = self.make_app(be, confirm=confirm, open_path=str(fw))
        app.on_detect()
        self.pump(app)
        app.on_write()
        self.pump(app)
        confirm.assert_not_called()
        self.messagebox.showerror.assert_called_once()
        self.assertEqual(be.write_calls, [])

    def test_write_confirmed(self):
        be = FakeBackend(bytes(1024 * KB))
        fw = Path(self.tmp.name) / "fw.bin"
        payload = os.urandom(64 * KB)
        fw.write_bytes(payload)
        seen = []
        app = self.make_app(be, confirm=lambda s: seen.append(s) or "SCRIVI", open_path=str(fw))
        app.addr_var.set("0x10000")
        app.on_detect()
        self.pump(app)
        app.on_write()
        self.pump(app)
        self.assertEqual(be.write_calls, [(0x10000, 64 * KB)])
        self.assertEqual(bytes(be.flash[0x10000:0x10000 + 64 * KB]), payload)
        summary = dict(seen[0])
        self.assertEqual(summary["Indirizzo"], "0x00010000")
        self.assertIn("65536 byte", summary["Dimensione"])
        self.assertIn("verificata", app.progress_text.get())

    def test_confirm_dialog_button_needs_scrivi(self):
        dlg = self.rom_gui.ConfirmWriteDialog(self.root, [("File", "fw.bin")])
        self.root.update()
        self.assertEqual(str(dlg.ok_btn["state"]), "disabled")
        dlg.var.set("scrivi")
        self.assertEqual(str(dlg.ok_btn["state"]), "disabled")
        dlg.var.set("SCRIVI")
        self.assertEqual(str(dlg.ok_btn["state"]), "normal")
        dlg.ok_btn.invoke()
        self.assertEqual(dlg.result, "SCRIVI")


@unittest.skipUnless(TK_OK, "Tkinter non disponibile")
class TestGuiProcess(unittest.TestCase):
    def test_gui_starts_and_closes_by_itself(self):
        """Avvio vero della finestra come processo, chiusura automatica dopo 1,5 s.

        Le porte vengono solo elencate (list_ports non apre i dispositivi)."""
        import subprocess
        tool = HERE.parent / "rom_tool.py"
        t0 = time.monotonic()
        res = subprocess.run([sys.executable, str(tool), "gui", "--autochiudi", "1.5"],
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertGreaterEqual(time.monotonic() - t0, 1.4)


if __name__ == "__main__":
    unittest.main(verbosity=2)
