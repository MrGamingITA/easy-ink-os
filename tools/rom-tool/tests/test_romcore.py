"""Test del nucleo: esptool sostituito da finti, nessuna porta seriale aperta."""

from __future__ import annotations

import hashlib
import inspect
import io
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from datetime import datetime
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))

import romcore as rc  # noqa: E402
import rom_tool  # noqa: E402
from fakes import MACHINE_PORTS, FakeBackend, FakePortInfo, FakeSerialError  # noqa: E402

KB = 1024


class FakeClock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


class TestPorts(unittest.TestCase):
    def test_machine_ports(self):
        cands, ignored = rc.find_ports(lambda: MACHINE_PORTS)
        self.assertEqual([c.device for c in cands], ["/dev/cu.usbserial-0001"])
        self.assertEqual(cands[0].bridge, "Silicon Labs CP210x")
        self.assertEqual({d for d, _ in ignored},
                         {"/dev/cu.debug-console", "/dev/cu.Bluetooth-Incoming-Port", "/dev/cu.JBLLIVEPRO2TWS"})

    def test_all_bridges_and_filters(self):
        ports = [
            FakePortInfo("/dev/cu.usbserial-A", 0x10C4, 0xEA60),
            FakePortInfo("/dev/tty.usbserial-A", 0x10C4, 0xEA60),  # doppione tty
            FakePortInfo("/dev/cu.wchusbserial1", 0x1A86, 0x55D4),
            FakePortInfo("/dev/cu.usbserial-FT1", 0x0403, 0x6001),
            FakePortInfo("/dev/cu.usbmodem101", 0x303A, 0x1001),
            FakePortInfo("/dev/cu.usbmodemXYZ", 0x2341, 0x0043),  # Arduino: non ESP
            FakePortInfo("/dev/cu.Bluetooth-Incoming-Port"),
        ]
        cands, ignored = rc.find_ports(lambda: ports)
        self.assertEqual([c.vid for c in cands], [0x10C4, 0x1A86, 0x0403, 0x303A])
        reasons = dict(ignored)
        self.assertIn("doppione", reasons["/dev/tty.usbserial-A"])
        self.assertIn("non riconosciuto", reasons["/dev/cu.usbmodemXYZ"])

    def test_choose_port(self):
        one = rc.find_ports(lambda: MACHINE_PORTS)[0]
        chooser = mock.Mock(return_value="X")
        self.assertEqual(rc.choose_port(one, chooser), "/dev/cu.usbserial-0001")
        chooser.assert_not_called()
        two = rc.find_ports(lambda: [FakePortInfo("/dev/cu.a", 0x10C4, 1), FakePortInfo("/dev/cu.b", 0x1A86, 2)])[0]
        chooser = mock.Mock(return_value="/dev/cu.b")
        self.assertEqual(rc.choose_port(two, chooser), "/dev/cu.b")
        chooser.assert_called_once()
        with self.assertRaises(rc.Cancelled):
            rc.choose_port(two, lambda c: None)
        with self.assertRaises(rc.NoDeviceError):
            rc.choose_port([], chooser)

    def test_port_in_use_parser(self):
        runner = mock.Mock(return_value=SimpleNamespace(stdout="p4242\ncpython3.13\n", returncode=0))
        self.assertEqual(rc.port_in_use("/dev/cu.x", runner), ["python3.13 (PID 4242)"])
        with self.assertRaises(rc.PortBusyError):
            rc.ensure_port_free("/dev/cu.x", runner)
        free = mock.Mock(return_value=SimpleNamespace(stdout="", returncode=1))
        rc.ensure_port_free("/dev/cu.x", free)  # non solleva

    def test_port_in_use_real_lsof(self):
        """Prova di presenza: lsof vero trova un processo che tiene aperto un file."""
        with tempfile.NamedTemporaryFile(delete=False) as tf:
            path = tf.name
        child = subprocess.Popen([sys.executable, "-c",
                                  f"f=open({path!r},'rb'); import time; time.sleep(30)"])
        try:
            found = []
            for _ in range(50):
                found = rc.port_in_use(path)
                if found:
                    break
                time.sleep(0.1)
            self.assertTrue(any(f"PID {child.pid}" in f for f in found), found)
        finally:
            child.kill()
            child.wait()
            os.unlink(path)


class TestProgress(unittest.TestCase):
    def test_tracker(self):
        clk = FakeClock()
        tr = rc.ProgressTracker(1000, clock=clk)
        clk.t = 2.0
        st = tr.update(250)
        self.assertAlmostEqual(st.percent, 25.0)
        self.assertAlmostEqual(st.speed, 125.0)
        self.assertAlmostEqual(st.eta, 6.0)
        clk.t = 10.0
        st = tr.update(1000)
        self.assertEqual(st.percent, 100.0)
        self.assertEqual(st.eta, 0.0)
        self.assertIn("100.0%", st.text())

    def test_tracker_no_speed_yet(self):
        tr = rc.ProgressTracker(100, clock=FakeClock())
        st = tr.update(0)
        self.assertIsNone(st.eta)
        self.assertIn("--:--", st.text())

    def test_format(self):
        self.assertEqual(rc.format_bytes(8 * 1024 * 1024), "8.0 MB")
        self.assertEqual(rc.format_duration(3725), "1:02:05")
        self.assertEqual(rc.format_duration(65), "01:05")
        self.assertEqual(rc.parse_size("8MB"), 8 * 1024 * 1024)
        self.assertEqual(rc.parse_size("0x400000"), 4 * 1024 * 1024)
        self.assertIsNone(rc.parse_size("auto"))


class TestChunkedRead(unittest.TestCase):
    def setUp(self):
        self.flash = os.urandom(1024 * KB)

    def run_read(self, backend, **kw):
        out = bytearray()
        prog = []
        res = rc.read_flash_chunked(backend, 0, len(self.flash), sink=out.extend,
                                    chunk_size=256 * KB, on_progress=prog.append,
                                    sleep=lambda s: None, **kw)
        return bytes(out), prog, res

    def test_clean_read(self):
        be = FakeBackend(self.flash)
        data, prog, res = self.run_read(be)
        self.assertEqual(data, self.flash)
        self.assertEqual(res["retries"], 0)
        self.assertEqual(prog[-1], len(self.flash))
        self.assertEqual(prog, sorted(prog))  # monotona senza errori
        self.assertGreater(len(prog), 4 * 10)  # avanza dentro i blocchi, non solo a fine blocco
        self.assertEqual([c[0] for c in be.read_calls], [0, 256 * KB, 512 * KB, 768 * KB])

    def test_retry_and_reassemble(self):
        err = FakeSerialError("Invalid head of packet (0x65)")
        be = FakeBackend(self.flash, faults={(256 * KB, 1): err, (256 * KB, 2): err, (768 * KB, 1): err})
        logs = []
        data, prog, res = self.run_read(be, log=logs.append)
        self.assertEqual(data, self.flash)
        self.assertEqual(hashlib.sha256(data).hexdigest(), hashlib.sha256(self.flash).hexdigest())
        self.assertEqual(res["retries"], 3)
        self.assertEqual(be.recover_calls, 3)
        self.assertEqual(be.attempts[256 * KB], 3)
        self.assertTrue(any("tentativo 1/5" in m for m in logs))
        # dopo un errore la barra torna esattamente all'inizio del blocco, mai sotto
        drops = [(a, b) for a, b in zip(prog, prog[1:]) if b < a]
        self.assertEqual(len(drops), 3)
        self.assertEqual([b for _, b in drops], [256 * KB, 256 * KB, 768 * KB])
        self.assertEqual(prog[-1], len(self.flash))

    def test_short_read_is_retried(self):
        be = FakeBackend(self.flash, short_read_at={(512 * KB, 1)})
        data, _, res = self.run_read(be)
        self.assertEqual(data, self.flash)
        self.assertEqual(res["retries"], 1)

    def test_persistent_failure_keeps_previous_chunks(self):
        be = FakeBackend(self.flash, always_fail_at=512 * KB)
        out = bytearray()
        with self.assertRaises(rc.ChunkReadError) as cm:
            rc.read_flash_chunked(be, 0, len(self.flash), sink=out.extend, chunk_size=256 * KB,
                                  retries=3, sleep=lambda s: None)
        self.assertEqual(bytes(out), self.flash[:512 * KB])
        self.assertEqual(be.attempts[512 * KB], 3)
        self.assertIn("0x00080000", str(cm.exception))

    def test_unexpected_error_not_retried(self):
        be = FakeBackend(self.flash, faults={(0, 1): ZeroDivisionError("bug")})
        with self.assertRaises(ZeroDivisionError):
            self.run_read(be)
        self.assertEqual(be.recover_calls, 0)

    def test_cancel(self):
        ev = threading.Event()
        be = FakeBackend(self.flash)
        calls = []

        def prog(d):
            calls.append(d)
            if d > 300 * KB:
                ev.set()
        with self.assertRaises(rc.Cancelled):
            rc.read_flash_chunked(be, 0, len(self.flash), chunk_size=256 * KB,
                                  on_progress=prog, cancel=ev, sleep=lambda s: None)
        self.assertLess(max(calls), len(self.flash))


class TestBackupFile(unittest.TestCase):
    def test_backup_writes_bin_and_sha(self):
        flash = os.urandom(512 * KB)
        be = FakeBackend(flash, faults={(256 * KB, 1): FakeSerialError("Invalid head of packet")})
        info = be.connect("/dev/cu.finta")
        states = []
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / rc.default_backup_name(info, datetime(2026, 10, 11, 13, 5, 0))
            self.assertEqual(out.name, "esp32s3_aabbccdd48e8_20261011-130500.bin")
            res = rc.backup_flash(be, info, out, on_progress=states.append, sleep=lambda s: None)
            self.assertEqual(out.read_bytes(), flash)
            self.assertFalse(Path(str(out) + ".part").exists())
            side = Path(str(out) + ".sha256")
            self.assertEqual(side.read_text(), f"{hashlib.sha256(flash).hexdigest()}  {out.name}\n")
            self.assertEqual(res["sha256"], hashlib.sha256(flash).hexdigest())
            # il file .sha256 e verificabile con shasum
            chk = subprocess.run(["shasum", "-a", "256", "-c", side.name], cwd=d,
                                 capture_output=True, text=True)
            self.assertEqual(chk.returncode, 0, chk.stdout + chk.stderr)
        self.assertEqual(states[-1].percent, 100.0)
        self.assertEqual(res["retries"], 1)

    def test_resume_from_part(self):
        flash = os.urandom(1024 * KB)
        be = FakeBackend(flash)
        info = be.connect("/dev/cu.finta")
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "x.bin"
            Path(str(out) + ".part").write_bytes(flash[:600 * KB])  # 600 KB -> riparte da 512 KB
            rc.backup_flash(be, info, out, resume=True, sleep=lambda s: None)
            self.assertEqual(out.read_bytes(), flash)
        self.assertEqual([c[0] for c in be.read_calls], [512 * KB, 768 * KB])


class TestWrite(unittest.TestCase):
    def setUp(self):
        self.be = FakeBackend(bytes(1024 * KB))
        self.info = self.be.connect("/dev/cu.finta")

    def test_confirmation_word(self):
        self.assertTrue(rc.check_confirmation("SCRIVI"))
        self.assertTrue(rc.check_confirmation("  SCRIVI\n"))
        for bad in (None, "", "scrivi", "Scrivi", "SCRIVI!", "si", "S C R I V I"):
            self.assertFalse(rc.check_confirmation(bad), bad)

    def test_write_requires_confirmation(self):
        for bad in (None, "", "scrivi", "ok"):
            with self.assertRaises(rc.WriteRejected):
                rc.write_image(self.be, self.info, b"\x01" * 4096, 0, confirmation=bad)
        self.assertEqual(self.be.write_calls, [])

    def test_too_big_rejected(self):
        with self.assertRaises(rc.WriteRejected) as cm:
            rc.write_image(self.be, self.info, b"\x00" * (1024 * KB + 1), 0, confirmation="SCRIVI")
        self.assertIn("più grande della flash", str(cm.exception))
        with self.assertRaises(rc.WriteRejected):
            rc.validate_write(512 * KB, 0x80000 + 0x1000, 1024 * KB)  # non entra
        with self.assertRaises(rc.WriteRejected):
            rc.validate_write(4096, 0x100, 1024 * KB)  # non allineato
        with self.assertRaises(rc.WriteRejected):
            rc.validate_write(0, 0, 1024 * KB)
        with self.assertRaises(rc.WriteRejected):
            rc.validate_write(4096, 0, None)
        self.assertEqual(self.be.write_calls, [])
        rc.validate_write(1024 * KB, 0, 1024 * KB)  # esattamente la flash: ok

    def test_write_and_verify(self):
        data = os.urandom(300 * KB)
        states = []
        res = rc.write_image(self.be, self.info, data, 0x10000, confirmation="SCRIVI",
                             on_progress=states.append)
        self.assertTrue(res["verified"])
        self.assertEqual(bytes(self.be.flash[0x10000:0x10000 + len(data)]), data)
        self.assertEqual(states[-1].percent, 100.0)
        self.assertEqual([s.done for s in states], sorted(s.done for s in states))

    def test_verify_failure(self):
        be = FakeBackend(bytes(1024 * KB), verify_ok=False)
        with self.assertRaises(rc.RomToolError) as cm:
            rc.write_image(be, be.connect("p"), b"\x01" * 4096, 0, confirmation="SCRIVI")
        self.assertIn("VERIFICA FALLITA", str(cm.exception))


class TestCli(unittest.TestCase):
    def setUp(self):
        self.p = mock.patch.object(rc, "ensure_port_free", lambda dev: None)
        self.p.start()
        self.addCleanup(self.p.stop)

    def args(self, *argv):
        return rom_tool.build_parser().parse_args(list(argv))

    def test_cli_write_refuses_without_scrivi(self):
        be = FakeBackend(bytes(1024 * KB))
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "fw.bin"
            f.write_bytes(b"\xAA" * 8192)
            with mock.patch("sys.stdout", new=io.StringIO()):
                rc_ = rom_tool.cmd_write(self.args("write", str(f), "--porta", "/dev/cu.finta"),
                                         backend_factory=lambda log: be, ask=lambda prompt: "si")
        self.assertEqual(rc_, 1)
        self.assertEqual(be.write_calls, [])
        self.assertTrue(be.closed)

    def test_cli_write_with_scrivi(self):
        be = FakeBackend(bytes(1024 * KB))
        prompts = []
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "fw.bin"
            f.write_bytes(b"\xAA" * 8192)
            out = io.StringIO()
            with mock.patch("sys.stdout", new=out), mock.patch("sys.stderr", new=io.StringIO()):
                rc_ = rom_tool.cmd_write(self.args("write", str(f), "--porta", "/dev/cu.finta", "--indirizzo", "0x1000"),
                                         backend_factory=lambda log: be,
                                         ask=lambda prompt: prompts.append(prompt) or "SCRIVI")
        self.assertEqual(rc_, 0)
        self.assertEqual(be.write_calls, [(0x1000, 8192)])
        self.assertIn("SCRIVI", prompts[0])
        self.assertIn("8192 byte", out.getvalue())
        self.assertIn("0x00001000", out.getvalue())

    def test_cli_write_too_big_never_asks(self):
        be = FakeBackend(bytes(64 * KB))
        ask = mock.Mock(return_value="SCRIVI")
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "big.bin"
            f.write_bytes(b"\x00" * (64 * KB + 4096))
            with mock.patch("sys.stdout", new=io.StringIO()), mock.patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(rc.WriteRejected):
                    rom_tool.cmd_write(self.args("write", str(f), "--porta", "/dev/cu.finta"),
                                       backend_factory=lambda log: be, ask=ask)
        ask.assert_not_called()
        self.assertEqual(be.write_calls, [])

    def test_cli_read(self):
        flash = os.urandom(512 * KB)
        be = FakeBackend(flash)
        err = io.StringIO()
        with tempfile.TemporaryDirectory() as d:
            with mock.patch("sys.stdout", new=io.StringIO()), mock.patch("sys.stderr", new=err):
                code = rom_tool.cmd_read(self.args("read", "--porta", "/dev/cu.finta", "-o", d),
                                         backend_factory=lambda log: be)
            bins = list(Path(d).glob("esp32s3_aabbccdd48e8_*.bin"))
            self.assertEqual(len(bins), 1)
            self.assertEqual(bins[0].read_bytes(), flash)
            self.assertTrue(Path(str(bins[0]) + ".sha256").exists())
        self.assertEqual(code, 0)
        self.assertIn("100.0%", err.getvalue())
        self.assertIn("[####", err.getvalue())


class TestEsptoolIntegration(unittest.TestCase):
    """Controlla l'API reale di esptool installata, senza hardware."""

    def test_api_exists(self):
        import esptool
        from esptool import cmds
        for name in ("detect_chip", "run_stub", "attach_flash", "detect_flash_size",
                     "read_flash", "write_flash", "reset_chip"):
            self.assertTrue(callable(getattr(cmds, name)), name)
        self.assertGreaterEqual(int(esptool.__version__.split(".")[0]), 5)
        rf = inspect.signature(cmds.read_flash).parameters
        for p in ("esp", "address", "size", "output", "flash_size", "no_progress"):
            self.assertIn(p, rf)
        self.assertIn("connect_attempts", inspect.signature(cmds.detect_chip).parameters)

    def test_real_read_flash_progress_reaches_our_callback(self):
        """cmds.read_flash vero -> log.progress di esp_pylib -> nostro logger -> callback."""
        from esptool import cmds
        flash = os.urandom(64 * KB)

        class FakeEsp:
            IS_STUB = True

            def read_flash(self, offset, length, progress_fn=None):
                for done in range(4096, length + 1, 4096):
                    progress_fn(done, length, offset)
                return flash[offset:offset + length]

        logs = []
        be = rc.EsptoolBackend(log=logs.append)
        be.esp = FakeEsp()
        be._install_logger()
        seen = []
        try:
            with mock.patch.object(cmds, "_set_flash_parameters", lambda esp, fs="keep": None):
                data = be.read_range(0x1000, 32 * KB, seen.append)
        finally:
            be._restore_logger()
        self.assertEqual(data, flash[0x1000:0x1000 + 32 * KB])
        self.assertEqual(seen[-1], 32 * KB)
        self.assertGreaterEqual(len(seen), 8)
        self.assertTrue(any("Read 32768 bytes" in m for m in logs), logs)

    def test_backend_glue_with_fake_esp(self):
        """connect/recover/close di EsptoolBackend con cmds veri dove possibile e un chip finto."""
        from esptool import cmds

        class FakePort:
            closed = 0

            def close(self):
                FakePort.closed += 1

        class FakeEsp:
            CHIP_NAME = "ESP32-S3"
            IS_STUB = True
            secure_download_mode = False

            def __init__(self):
                self._port = FakePort()
                self.bauds = []
                self.resets = 0

            def get_chip_description(self):
                return "ESP32-S3 (QFN56) (revision v0.2)"

            def get_chip_features(self):
                return ["Wi-Fi", "BT 5 (LE)"]

            def get_crystal_freq(self):
                return 40

            def read_mac(self, mac_type="BASE_MAC"):
                return (0xAA, 0xBB, 0xCC, 0xDD, 0x48, 0xE8)

            def flash_id(self, cache=True):
                return 0x1740C8  # GigaDevice, 8 MB

            def change_baud(self, baud):
                self.bauds.append(baud)

            def hard_reset(self):
                self.resets += 1

        made = []

        def fake_detect(port, baud, connect_mode="default-reset", connect_attempts=7):
            made.append((port, baud))
            esp = FakeEsp()
            made_esps.append(esp)
            return esp
        made_esps = []
        logs = []
        with mock.patch.object(cmds, "detect_chip", fake_detect), \
                mock.patch.object(cmds, "run_stub", lambda esp: esp), \
                mock.patch.object(cmds, "attach_flash", lambda esp: None):
            be = rc.EsptoolBackend(log=logs.append)
            info = be.connect("/dev/cu.finta", 230400)
            self.assertEqual(made, [("/dev/cu.finta", 115200)])  # connessione sempre a 115200
            self.assertEqual(made_esps[0].bauds, [230400])  # poi cambio baud
            self.assertEqual(info.mac, "aa:bb:cc:dd:48:e8")
            self.assertEqual(info.flash_vendor_id, 0xC8)
            self.assertEqual(info.flash_device_id, 0x4017)
            self.assertEqual(info.flash_size_str, "8MB")  # detect_flash_size vero
            self.assertEqual(info.flash_size, 8 * 1024 * 1024)
            self.assertEqual(info.flash_vendor_name, "GigaDevice")
            be.recover()
            self.assertEqual(len(made), 2)
            self.assertEqual(FakePort.closed, 1)
            be.close()
            self.assertEqual(made_esps[1].resets, 1)  # reset_chip vero -> hard_reset
            self.assertEqual(FakePort.closed, 2)

    def test_logger_restored(self):
        from esp_pylib.logger import EspLog
        before = EspLog.instance
        be = rc.EsptoolBackend()
        be._install_logger()
        self.assertIsNot(EspLog.instance, before)
        be._restore_logger()
        self.assertIs(EspLog.instance, before)


if __name__ == "__main__":
    unittest.main(verbosity=2)
