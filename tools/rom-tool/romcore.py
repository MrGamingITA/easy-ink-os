"""Nucleo dello strumento ROM di Calamaio OS.

Logica pura (porte, avanzamento, lettura a blocchi, validazioni) separata
dal backend esptool, così i test possono sostituire esptool con un finto.
API esptool verificata sul sorgente di esptool 5.5.0 (esptool.cmds).
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Callable, Iterable, Sequence

# --- costanti ---------------------------------------------------------------

DEFAULT_BAUD = 115200  # a 460800/921600 il CP2102 di questa macchina perde pacchetti
BAUD_CHOICES = (115200, 230400, 460800, 921600)
DEFAULT_CHUNK = 256 * 1024
DEFAULT_RETRIES = 5
SECTOR_SIZE = 0x1000  # le scritture devono partire su un settore da 4 KB
CONFIRM_WORD = "SCRIVI"

# VID USB dei bridge tipici delle schede ESP
KNOWN_BRIDGES = {
    0x10C4: "Silicon Labs CP210x",
    0x1A86: "WCH CH340/CH9102",
    0x0403: "FTDI",
    0x303A: "Espressif USB nativo",
}

# porte di sistema da scartare sempre (per nome)
IGNORED_NAME_HINTS = ("bluetooth", "debug-console", "wlan-debug", "airpods")

# produttori JEDEC più comuni sulle schede ESP
FLASH_VENDORS = {
    0xEF: "Winbond",
    0xC8: "GigaDevice",
    0xC2: "Macronix",
    0x20: "XMC / Micron",
    0x68: "Boya Micro",
    0x0B: "XTX",
    0x85: "Puya",
    0x9D: "ISSI",
    0x1C: "EON",
    0x5E: "Zbit",
}


# --- errori -----------------------------------------------------------------

class RomToolError(Exception):
    """Errore mostrato all'utente così com'è (testo in italiano)."""


class NoDeviceError(RomToolError):
    pass


class PortBusyError(RomToolError):
    pass


class WriteRejected(RomToolError):
    pass


class ChunkReadError(RomToolError):
    pass


class Cancelled(RomToolError):
    pass


# --- porte seriali ----------------------------------------------------------

@dataclass
class PortCandidate:
    device: str
    vid: int | None
    pid: int | None
    description: str
    bridge: str
    serial_number: str | None = None

    def label(self) -> str:
        ids = f"{self.vid:04X}:{self.pid:04X}" if self.vid is not None and self.pid is not None else "?"
        return f"{self.device}  -  {self.bridge} ({ids})"


def _default_comports():
    from serial.tools import list_ports  # non apre le porte, legge solo l'elenco
    return list_ports.comports()


def find_ports(comports: Callable[[], Iterable] | None = None):
    """Restituisce (candidate, ignorate). ignorate = [(device, motivo)]."""
    ports = list((comports or _default_comports)())
    candidates: list[PortCandidate] = []
    ignored: list[tuple[str, str]] = []
    devices = {p.device for p in ports}
    for p in ports:
        dev = p.device or ""
        low = dev.lower()
        if any(h in low for h in IGNORED_NAME_HINTS):
            ignored.append((dev, "porta di sistema"))
            continue
        # su macOS ogni USB compare come /dev/cu.X e /dev/tty.X: teniamo cu
        if dev.startswith("/dev/tty.") and dev.replace("/dev/tty.", "/dev/cu.", 1) in devices:
            ignored.append((dev, "doppione tty di una porta cu"))
            continue
        if p.vid is None:
            ignored.append((dev, "non USB (Bluetooth o virtuale)"))
            continue
        bridge = KNOWN_BRIDGES.get(p.vid)
        if bridge is None:
            ignored.append((dev, f"bridge USB non riconosciuto (VID {p.vid:04X})"))
            continue
        candidates.append(PortCandidate(dev, p.vid, p.pid, p.description or "", bridge,
                                        getattr(p, "serial_number", None)))
    return candidates, ignored


def choose_port(candidates: Sequence[PortCandidate],
                chooser: Callable[[Sequence[PortCandidate]], str | None]) -> str:
    """Una sola porta: scelta automatica. Piu porte: decide `chooser`."""
    if not candidates:
        raise NoDeviceError("Nessuna scheda ESP trovata. Collega la scheda via USB "
                            "e controlla il cavo (deve essere un cavo dati).")
    if len(candidates) == 1:
        return candidates[0].device
    picked = chooser(candidates)
    if not picked:
        raise Cancelled("Nessuna porta scelta.")
    return picked


def port_in_use(device: str, runner=subprocess.run) -> list[str]:
    """Processi che tengono aperta la porta (via lsof, che non la apre)."""
    if runner is subprocess.run and shutil.which("lsof") is None:
        return []
    try:
        res = runner(["lsof", "-F", "pc", device], capture_output=True, text=True, timeout=10)
    except Exception:
        return []
    procs, pid = [], None
    for line in (res.stdout or "").splitlines():
        if line.startswith("p"):
            pid = line[1:]
        elif line.startswith("c") and pid:
            procs.append(f"{line[1:]} (PID {pid})")
            pid = None
    if pid:
        procs.append(f"PID {pid}")
    return procs


def ensure_port_free(device: str, runner=subprocess.run) -> None:
    procs = port_in_use(device, runner)
    if procs:
        raise PortBusyError(f"La porta {device} è già in uso da: {', '.join(procs)}. "
                            "Aspetta che finisca o chiudi quel programma: aprirla ora "
                            "interromperebbe il suo lavoro.")


# --- formattazione e avanzamento --------------------------------------------

def format_bytes(n: float) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if abs(n) < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} GB"


def format_duration(seconds: float | None) -> str:
    if seconds is None or seconds != seconds or seconds == float("inf"):
        return "--:--"
    s = int(round(seconds))
    h, rem = divmod(s, 3600)
    m, s = divmod(rem, 60)
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"


@dataclass
class ProgressState:
    done: int
    total: int
    percent: float
    speed: float  # byte/s
    eta: float | None  # secondi
    elapsed: float

    def text(self) -> str:
        return (f"{format_bytes(self.done)} / {format_bytes(self.total)}  "
                f"({self.percent:5.1f}%)  {format_bytes(self.speed)}/s  "
                f"mancano {format_duration(self.eta)}")


class ProgressTracker:
    """Calcola percentuale, velocità (finestra mobile) e tempo rimanente."""

    def __init__(self, total: int, start_done: int = 0, window: float = 8.0,
                 clock: Callable[[], float] = time.monotonic):
        self.total = max(int(total), 0)
        self.clock = clock
        self.window = window
        self.t0 = clock()
        self.done = start_done
        self._samples: list[tuple[float, int]] = [(self.t0, start_done)]

    def update(self, done: int) -> ProgressState:
        now = self.clock()
        self.done = max(0, min(int(done), self.total))
        self._samples.append((now, self.done))
        # tiene solo i campioni nella finestra (ma almeno due)
        while len(self._samples) > 2 and now - self._samples[0][0] > self.window:
            self._samples.pop(0)
        t_old, d_old = self._samples[0]
        dt = now - t_old
        speed = (self.done - d_old) / dt if dt > 0 else 0.0
        speed = max(speed, 0.0)
        remaining = self.total - self.done
        eta = remaining / speed if speed > 0 else (0.0 if remaining == 0 else None)
        pct = 100.0 * self.done / self.total if self.total else 100.0
        return ProgressState(self.done, self.total, pct, speed, eta, now - self.t0)


# --- informazioni sul dispositivo -------------------------------------------

@dataclass
class DeviceInfo:
    port: str
    chip: str  # es. "ESP32-S3"
    description: str  # es. "ESP32-S3 (QFN56) (revision v0.2)"
    features: list[str] = field(default_factory=list)
    mac: str = ""
    crystal_mhz: int | None = None
    flash_vendor_id: int | None = None
    flash_device_id: int | None = None
    flash_size_str: str | None = None
    flash_size: int | None = None
    secure_download_mode: bool = False

    @property
    def flash_vendor_name(self) -> str:
        if self.flash_vendor_id is None:
            return "sconosciuto"
        return FLASH_VENDORS.get(self.flash_vendor_id, "sconosciuto")

    def rows(self) -> list[tuple[str, str]]:
        vend = (f"{self.flash_vendor_name} (0x{self.flash_vendor_id:02X})"
                if self.flash_vendor_id is not None else "non letto")
        dev = f"0x{self.flash_device_id:04X}" if self.flash_device_id is not None else "non letto"
        size = (f"{self.flash_size_str} ({self.flash_size} byte)"
                if self.flash_size else "non rilevata")
        return [
            ("Porta", self.port),
            ("Chip", self.description or self.chip),
            ("Funzionalità", ", ".join(self.features) or "-"),
            ("MAC", self.mac or "-"),
            ("Cristallo", f"{self.crystal_mhz} MHz" if self.crystal_mhz else "-"),
            ("Flash: produttore", vend),
            ("Flash: id dispositivo", dev),
            ("Flash: dimensione", size),
        ]


def parse_size(text: str | None) -> int | None:
    """'8MB' / '512KB' / '0x800000' / '8388608' -> byte."""
    if not text:
        return None
    t = text.strip().upper().replace(" ", "")
    if t in ("AUTO", ""):
        return None
    m = re.fullmatch(r"(\d+)(MB|KB|M|K)", t)
    if m:
        return int(m.group(1)) * (1024 * 1024 if m.group(2).startswith("M") else 1024)
    return int(t, 0)


def parse_address(text: str) -> int:
    try:
        value = int(str(text).strip(), 0)
    except ValueError:
        raise WriteRejected(f"Indirizzo non valido: {text!r} (usa ad esempio 0x0 o 0x10000).")
    return value


def mac_to_text(mac) -> str:
    if mac is None:
        return ""
    if isinstance(mac, str):
        return mac.lower()
    return ":".join(f"{b:02x}" for b in mac)


def default_backup_name(info: DeviceInfo, now: datetime | None = None) -> str:
    now = now or datetime.now()
    chip = re.sub(r"[^a-z0-9]", "", (info.chip or "esp").lower()) or "esp"
    mac = re.sub(r"[^0-9a-f]", "", (info.mac or "").lower()) or "nomac"
    return f"{chip}_{mac}_{now:%Y%m%d-%H%M%S}.bin"


# --- hash -------------------------------------------------------------------

def sha256_file(path: str | os.PathLike) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_sha256_sidecar(path: str | os.PathLike) -> tuple[str, Path]:
    """Scrive <file>.sha256 nel formato di `shasum -a 256 -c`."""
    p = Path(path)
    digest = sha256_file(p)
    side = p.with_name(p.name + ".sha256")
    side.write_text(f"{digest}  {p.name}\n", encoding="utf-8")
    return digest, side


# --- lettura a blocchi con ritentativi --------------------------------------

LogFn = Callable[[str], None]
ProgressFn = Callable[[int], None]  # byte totali fatti finora


def _noop(*_a, **_k):
    return None


def read_flash_chunked(backend, address: int, size: int, *,
                       sink: Callable[[bytes], None] | None = None,
                       start_offset: int = 0,
                       chunk_size: int = DEFAULT_CHUNK,
                       retries: int = DEFAULT_RETRIES,
                       on_progress: ProgressFn | None = None,
                       log: LogFn | None = None,
                       cancel: threading.Event | None = None,
                       sleep: Callable[[float], None] = time.sleep) -> dict:
    """Legge [address+start_offset, address+size) a blocchi.

    Ogni blocco ha `retries` tentativi; tra un tentativo e l'altro il backend
    si riconnette. I blocchi buoni vanno subito in `sink`, quindi un errore
    definitivo non butta via il lavoro gia fatto.
    """
    if chunk_size <= 0 or chunk_size % SECTOR_SIZE:
        raise ValueError("chunk_size deve essere un multiplo positivo di 4096")
    log = log or _noop
    on_progress = on_progress or _noop
    recoverable = tuple(getattr(backend, "recoverable_errors", (Exception,)))
    pos = start_offset
    retry_count = 0
    on_progress(pos)
    while pos < size:
        if cancel is not None and cancel.is_set():
            raise Cancelled("Operazione annullata.")
        n = min(chunk_size, size - pos)
        base = pos

        def chunk_progress(done_in_chunk: int, _base=base, _n=n):
            if cancel is not None and cancel.is_set():
                raise Cancelled("Operazione annullata.")
            on_progress(_base + max(0, min(done_in_chunk, _n)))

        data = None
        last_err: BaseException | None = None
        for attempt in range(1, retries + 1):
            try:
                data = backend.read_range(address + base, n, chunk_progress)
                if len(data) != n:
                    raise ChunkReadError(f"blocco corto: {len(data)} byte invece di {n}")
                break
            except Cancelled:
                raise
            except (ChunkReadError, *recoverable) as e:  # type: ignore[misc]
                last_err = e
                data = None
                retry_count += 1
                on_progress(base)  # la barra torna all'inizio del blocco
                log(f"Errore al blocco 0x{address + base:08X} (tentativo {attempt}/{retries}): {e}")
                if attempt < retries:
                    sleep(min(0.5 * attempt, 3.0))
                    try:
                        log("Riconnessione alla scheda...")
                        backend.recover()
                    except Exception as re_err:  # il prossimo tentativo riprovera
                        log(f"Riconnessione fallita: {re_err}")
        if data is None:
            raise ChunkReadError(
                f"Il blocco 0x{address + base:08X} non si legge dopo {retries} tentativi "
                f"(ultimo errore: {last_err}). Salvati {base} byte su {size}.")
        if sink is not None:
            sink(data)
        pos = base + n
        on_progress(pos)
    return {"bytes": size - start_offset, "retries": retry_count}


def backup_flash(backend, info: DeviceInfo, out_path: str | os.PathLike, *,
                 size: int | None = None, resume: bool = False,
                 chunk_size: int = DEFAULT_CHUNK, retries: int = DEFAULT_RETRIES,
                 on_progress: Callable[[ProgressState], None] | None = None,
                 log: LogFn | None = None, cancel: threading.Event | None = None,
                 sleep: Callable[[float], None] = time.sleep,
                 clock: Callable[[], float] = time.monotonic) -> dict:
    """Legge tutta la flash in out_path (via out_path.part) e scrive lo SHA-256."""
    log = log or _noop
    total = size or info.flash_size
    if not total:
        raise RomToolError("Dimensione della flash non rilevata: indicala a mano (es. 8MB).")
    out = Path(out_path)
    part = out.with_name(out.name + ".part")
    start = 0
    if resume and part.exists():
        start = (part.stat().st_size // chunk_size) * chunk_size
        start = min(start, total)
        log(f"Riprendo da {format_bytes(start)} (file parziale {part.name}).")
    mode = "r+b" if start else "wb"
    tracker = ProgressTracker(total, start_done=start, clock=clock)
    cb = (lambda d: on_progress(tracker.update(d))) if on_progress else None
    with open(part, mode) as f:
        f.seek(start)
        f.truncate()
        result = read_flash_chunked(backend, 0, total, sink=f.write, start_offset=start,
                                    chunk_size=chunk_size, retries=retries,
                                    on_progress=cb, log=log, cancel=cancel, sleep=sleep)
        f.flush()
        os.fsync(f.fileno())
    if part.stat().st_size != total:
        raise RomToolError(f"Il file parziale ha {part.stat().st_size} byte invece di {total}.")
    os.replace(part, out)
    digest, side = write_sha256_sidecar(out)
    log(f"Backup completato: {out} ({format_bytes(total)}).")
    log(f"SHA-256: {digest}  (salvato in {side.name})")
    result.update(path=str(out), sha256=digest, sidecar=str(side), size=total,
                  seconds=tracker.clock() - tracker.t0)
    return result


# --- scrittura --------------------------------------------------------------

def check_confirmation(text: str | None) -> bool:
    """Conferma valida solo se è esattamente SCRIVI (maiuscolo, senza altro)."""
    return (text or "").strip() == CONFIRM_WORD


def validate_write(file_size: int, address: int, flash_size: int | None) -> None:
    if file_size <= 0:
        raise WriteRejected("Il file è vuoto.")
    if address < 0:
        raise WriteRejected("L'indirizzo non può essere negativo.")
    if address % SECTOR_SIZE:
        raise WriteRejected(f"L'indirizzo 0x{address:X} non è allineato a un settore da 4 KB.")
    if not flash_size:
        raise WriteRejected("Dimensione della flash sconosciuta: non scrivo alla cieca.")
    if file_size > flash_size:
        raise WriteRejected(f"Il file ({file_size} byte) è più grande della flash "
                            f"({flash_size} byte). Scrittura rifiutata.")
    if address + file_size > flash_size:
        raise WriteRejected(f"Il file non entra: 0x{address:X} + {file_size} byte supera la "
                            f"flash di {flash_size} byte.")


def write_image(backend, info: DeviceInfo, data: bytes, address: int, *,
                confirmation: str | None,
                on_progress: Callable[[ProgressState], None] | None = None,
                log: LogFn | None = None,
                clock: Callable[[], float] = time.monotonic) -> dict:
    """Scrive `data` e verifica l'MD5 sulla scheda. Richiede la parola SCRIVI."""
    log = log or _noop
    if not check_confirmation(confirmation):
        raise WriteRejected(f"Conferma mancante: per scrivere serve la parola {CONFIRM_WORD}.")
    validate_write(len(data), address, info.flash_size)
    tracker = ProgressTracker(len(data), clock=clock)

    def prog(cur: int, total: int):
        # esptool conta i byte compressi: li riporto alla dimensione del file
        if on_progress and total:
            on_progress(tracker.update(int(len(data) * cur / total)))

    log(f"Scrittura di {format_bytes(len(data))} a 0x{address:08X}...")
    backend.write_range(address, data, prog)
    if on_progress:
        on_progress(tracker.update(len(data)))
    log("Verifica in corso (MD5 calcolato dalla scheda)...")
    ok = backend.verify(address, data)
    if not ok:
        raise RomToolError("VERIFICA FALLITA: il contenuto della flash non corrisponde al file.")
    log("Verifica riuscita: la flash corrisponde al file.")
    return {"bytes": len(data), "address": address, "verified": True,
            "sha256": hashlib.sha256(data).hexdigest(),
            "seconds": tracker.clock() - tracker.t0}


# --- backend esptool --------------------------------------------------------

_RICH_TAG = re.compile(r"\[/?(?:bold|dim|italic|red|green|yellow|blue|cyan|magenta|b|i)\b[^\]]*\]")


def _make_callback_logger(on_line: LogFn, holder: dict):
    """Logger per esptool che inoltra testo e barra di avanzamento a callback."""
    from esp_pylib.logger import EspLogBase

    class CallbackLogger(EspLogBase):
        def __init__(self):
            self._buf = ""

        def _emit(self, text: str):
            self._buf += _RICH_TAG.sub("", text)
            while "\n" in self._buf:
                line, self._buf = self._buf.split("\n", 1)
                if line.strip():
                    on_line(line.rstrip())

        def print(self, *args, **kwargs):
            sep = kwargs.get("sep", " ")
            end = kwargs.get("end", "\n")
            self._emit(sep.join(str(a) for a in args) + end)

        def err(self, *args, suggestion=None):
            self._emit("ERRORE: " + " ".join(str(a) for a in args) + "\n")

        def warn(self, *args, suggestion=None):
            self._emit("Attenzione: " + " ".join(str(a) for a in args) + "\n")

        warning = warn
        error = err

        def note(self, *args):
            self._emit("Nota: " + " ".join(str(a) for a in args) + "\n")

        def hint(self, *args):
            self._emit(" ".join(str(a) for a in args) + "\n")

        def debug(self, *args):
            pass

        def set_verbosity(self, mode):
            pass

        def progress_bar(self, cur_iter, total_iters, prefix="", suffix="", bar_length=30):
            fn = holder.get("progress")
            if fn:
                fn(int(cur_iter), int(total_iters))

    return CallbackLogger()


class EsptoolBackend:
    """Usa esptool come libreria. Una connessione per operazione."""

    def __init__(self, log: LogFn | None = None, connect_attempts: int = 7):
        import serial
        from esptool.util import FatalError
        self.log = log or _noop
        self.connect_attempts = connect_attempts
        self.recoverable_errors = (FatalError, serial.SerialException, OSError, TimeoutError)
        self.esp = None
        self.port = None
        self.baud = DEFAULT_BAUD
        self._holder: dict = {}
        self._prev_logger = None

    # logger di esptool -> nostro log
    def _install_logger(self):
        from esp_pylib.logger import EspLog
        if self._prev_logger is None:
            self._prev_logger = (EspLog.instance,)  # tupla: distingue "None salvato" da "niente"
            EspLog.set_logger(_make_callback_logger(self.log, self._holder))

    def _restore_logger(self):
        from esp_pylib.logger import EspLog
        if self._prev_logger is not None:
            EspLog.instance = self._prev_logger[0]
            self._prev_logger = None

    def _open(self):
        from esptool import cmds
        esp = cmds.detect_chip(self.port, DEFAULT_BAUD, connect_mode="default-reset",
                               connect_attempts=self.connect_attempts)
        esp = cmds.run_stub(esp)
        if self.baud != DEFAULT_BAUD:
            esp.change_baud(self.baud)
        cmds.attach_flash(esp)
        self.esp = esp

    def connect(self, port: str, baud: int = DEFAULT_BAUD, flash_size_override: int | None = None) -> DeviceInfo:
        from esptool import cmds
        from esptool.util import flash_size_bytes
        self.port, self.baud = port, int(baud)
        self._install_logger()
        self._open()
        esp = self.esp
        info = DeviceInfo(port=port, chip=esp.CHIP_NAME, description="")
        info.secure_download_mode = bool(getattr(esp, "secure_download_mode", False))
        for attr, fn in (("description", esp.get_chip_description),
                         ("features", esp.get_chip_features),
                         ("crystal_mhz", esp.get_crystal_freq)):
            try:
                setattr(info, attr, fn())
            except Exception as e:
                self.log(f"Non riesco a leggere {attr}: {e}")
        try:
            try:
                mac = esp.read_mac("BASE_MAC")
            except TypeError:
                mac = esp.read_mac()
            info.mac = mac_to_text(mac)
        except Exception as e:
            self.log(f"Non riesco a leggere il MAC: {e}")
        try:
            # stessa decodifica di esptool.cmds._get_flash_info
            fid = esp.flash_id()
            info.flash_vendor_id = fid & 0xFF
            info.flash_device_id = ((fid >> 16) & 0xFF) | (((fid >> 8) & 0xFF) << 8)
        except Exception as e:
            self.log(f"Non riesco a leggere l'id della flash: {e}")
        try:
            info.flash_size_str = cmds.detect_flash_size(esp)
            info.flash_size = flash_size_bytes(info.flash_size_str)
        except Exception as e:
            self.log(f"Dimensione flash non rilevata: {e}")
        if flash_size_override:
            info.flash_size = flash_size_override
            info.flash_size_str = format_bytes(flash_size_override).replace(".0 ", "")
        return info

    def read_range(self, address: int, size: int, progress: Callable[[int], None]) -> bytes:
        from esptool import cmds
        self._holder["progress"] = lambda cur, total: progress(cur)
        try:
            return cmds.read_flash(self.esp, address, size, output=None, flash_size="keep")
        finally:
            self._holder.pop("progress", None)

    def write_range(self, address: int, data: bytes, progress: Callable[[int, int], None]) -> None:
        from esptool import cmds
        self._holder["progress"] = progress
        try:
            cmds.write_flash(self.esp, [(address, data)], flash_size="keep")
        finally:
            self._holder.pop("progress", None)

    def verify(self, address: int, data: bytes) -> bool:
        if getattr(self.esp, "secure_download_mode", False):
            self.log("Secure Download Mode: la verifica MD5 non è disponibile.")
            return False
        return self.esp.flash_md5sum(address, len(data)) == hashlib.md5(data).hexdigest()

    def recover(self) -> None:
        """Chiude e riapre da zero: reset in bootloader, stub, baud."""
        try:
            if self.esp is not None:
                self.esp._port.close()
        except Exception:
            pass
        self.esp = None
        time.sleep(0.5)
        self._open()

    def close(self, reset: bool = True) -> None:
        from esptool import cmds
        try:
            if self.esp is not None and reset:
                cmds.reset_chip(self.esp, "hard-reset")
        except Exception as e:
            self.log(f"Reset finale non riuscito: {e}")
        try:
            if self.esp is not None:
                self.esp._port.close()
        except Exception:
            pass
        self.esp = None
        self._restore_logger()
