"""Finti per i test: porte seriali e backend esptool. Nessuna porta vera viene aperta."""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass

import romcore as rc


@dataclass
class FakePortInfo:
    device: str
    vid: int | None = None
    pid: int | None = None
    description: str = "n/a"
    serial_number: str | None = None


# copia dell'elenco reale di questa macchina (letto con list_ports, che non apre)
MACHINE_PORTS = [
    FakePortInfo("/dev/cu.debug-console"),
    FakePortInfo("/dev/cu.Bluetooth-Incoming-Port"),
    FakePortInfo("/dev/cu.JBLLIVEPRO2TWS"),
    FakePortInfo("/dev/cu.usbserial-0001", 0x10C4, 0xEA60, "CP2102 USB to UART Bridge Controller", "0001"),
]


class FakeSerialError(Exception):
    """Simula 'Invalid head of packet' e simili."""


class FakeBackend:
    """Flash finta in memoria. `faults` = {(indirizzo_blocco, tentativo): eccezione}."""

    recoverable_errors = (FakeSerialError,)

    def __init__(self, flash: bytes | None = None, faults=None, always_fail_at=None,
                 short_read_at=None, log=None, step=4096, verify_ok=True,
                 info_overrides=None):
        self.flash = bytearray(flash if flash is not None else os.urandom(1024 * 1024))
        self.faults = dict(faults or {})
        self.always_fail_at = always_fail_at
        self.short_read_at = set(short_read_at or ())
        self.attempts: dict[int, int] = {}
        self.recover_calls = 0
        self.read_calls = []
        self.write_calls = []
        self.closed = False
        self.connected = None
        self.step = step
        self.verify_ok = verify_ok
        self.log = log or (lambda m: None)
        self.info_overrides = info_overrides or {}

    # interfaccia del backend
    def connect(self, port, baud=rc.DEFAULT_BAUD, flash_size_override=None):
        self.connected = (port, baud)
        info = rc.DeviceInfo(port=port, chip="ESP32-S3", description="ESP32-S3 (QFN56) (revision v0.2)",
                             features=["Wi-Fi", "BT 5 (LE)", "Dual Core + LP Core", "240MHz"],
                             mac="aa:bb:cc:dd:48:e8", crystal_mhz=40,
                             flash_vendor_id=0xC8, flash_device_id=0x4017,
                             flash_size_str="8MB" if len(self.flash) == 8 * 1024 * 1024 else None,
                             flash_size=flash_size_override or len(self.flash))
        for k, v in self.info_overrides.items():
            setattr(info, k, v)
        return info

    def read_range(self, address, size, progress):
        n = self.attempts.get(address, 0) + 1
        self.attempts[address] = n
        self.read_calls.append((address, size, n))
        if self.always_fail_at is not None and address == self.always_fail_at:
            progress(size // 2)
            raise FakeSerialError("Invalid head of packet (0x65): Possible serial noise or corruption.")
        err = self.faults.get((address, n))
        done = 0
        while done < size:
            done = min(done + self.step, size)
            progress(done)
            if err is not None and done >= size // 2:
                raise err
        if (address, n) in self.short_read_at:
            return bytes(self.flash[address:address + size - 1])
        return bytes(self.flash[address:address + size])

    def write_range(self, address, data, progress):
        self.write_calls.append((address, len(data)))
        total = max(1, len(data) // 3)  # come esptool: byte compressi
        for cur in range(0, total + 1, max(1, total // 10)):
            progress(cur, total)
        progress(total, total)
        self.flash[address:address + len(data)] = data

    def verify(self, address, data):
        if not self.verify_ok:
            return False
        return hashlib.md5(bytes(self.flash[address:address + len(data)])).hexdigest() == hashlib.md5(data).hexdigest()

    def recover(self):
        self.recover_calls += 1

    def close(self, reset=True):
        self.closed = True
