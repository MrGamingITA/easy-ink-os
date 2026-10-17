#!/usr/bin/env python3
"""Strumento ROM di Calamaio OS: legge e scrive la flash delle schede ESP32/ESP32-S3.

Uso:
  rom_tool.py                 apre la GUI
  rom_tool.py gui             apre la GUI
  rom_tool.py porte           elenca le porte (senza aprirle)
  rom_tool.py info            mostra chip, MAC e flash
  rom_tool.py read [-o FILE]  backup completo della flash
  rom_tool.py write FILE      scrive un .bin (chiede di digitare SCRIVI)
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import romcore as rc  # noqa: E402


# --- barra nel terminale ----------------------------------------------------

class TerminalBar:
    def __init__(self, stream=None, width: int = 30, min_interval: float = 0.1):
        self.stream = stream or sys.stderr
        self.width = width
        self.min_interval = min_interval
        self._last = 0.0
        self._shown = False

    def __call__(self, st: rc.ProgressState):
        now = time.monotonic()
        if now - self._last < self.min_interval and st.done < st.total:
            return
        self._last = now
        filled = int(self.width * st.percent / 100)
        bar = "#" * filled + "-" * (self.width - filled)
        self.stream.write(f"\r[{bar}] {st.text()}   ")
        self.stream.flush()
        self._shown = True

    def finish(self):
        if self._shown:
            self.stream.write("\n")
            self.stream.flush()
            self._shown = False


def _log(msg: str):
    print(msg, flush=True)


def _cli_chooser(cands):
    print("Ho trovato più schede:")
    for i, c in enumerate(cands, 1):
        print(f"  {i}) {c.label()}")
    ans = input("Quale uso? (numero, invio per annullare) ").strip()
    if not ans:
        return None
    try:
        return cands[int(ans) - 1].device
    except (ValueError, IndexError):
        raise rc.RomToolError(f"Scelta non valida: {ans!r}")


def resolve_port(args) -> str:
    if args.porta:
        return args.porta
    cands, _ = rc.find_ports()
    return rc.choose_port(cands, _cli_chooser)


def connect(args, backend_factory=rc.EsptoolBackend):
    port = resolve_port(args)
    rc.ensure_port_free(port)
    print(f"Connessione a {port} a {args.baud} baud...")
    backend = backend_factory(log=_log)
    info = backend.connect(port, args.baud, rc.parse_size(getattr(args, "dimensione", None)))
    return backend, info


def print_info(info: rc.DeviceInfo):
    print()
    for k, v in info.rows():
        print(f"  {k:<22} {v}")
    print()


# --- comandi ----------------------------------------------------------------

def cmd_ports(args) -> int:
    cands, ignored = rc.find_ports()
    if cands:
        print("Schede ESP trovate:")
        for c in cands:
            print(f"  {c.label()}  [{c.description}]")
    else:
        print("Nessuna scheda ESP trovata.")
    if ignored:
        print("Porte ignorate:")
        for dev, why in ignored:
            print(f"  {dev}  ({why})")
    return 0


def cmd_info(args, backend_factory=rc.EsptoolBackend) -> int:
    backend, info = connect(args, backend_factory)
    try:
        print_info(info)
    finally:
        backend.close()
    return 0


def cmd_read(args, backend_factory=rc.EsptoolBackend) -> int:
    backend, info = connect(args, backend_factory)
    bar = TerminalBar()
    try:
        print_info(info)
        out = Path(args.output) if args.output else Path.cwd() / rc.default_backup_name(info)
        if out.is_dir():
            out = out / rc.default_backup_name(info)
        size = rc.parse_size(args.dimensione) or info.flash_size
        print(f"Backup di {rc.format_bytes(size or 0)} in {out}")
        res = rc.backup_flash(backend, info, out, size=size, resume=args.riprendi,
                              chunk_size=args.blocco * 1024, retries=args.tentativi,
                              on_progress=bar, log=lambda m: (bar.finish(), _log(m)))
        bar.finish()
        print(f"Fatto in {rc.format_duration(res['seconds'])}, ritentativi: {res['retries']}.")
    except BaseException:
        bar.finish()
        raise
    finally:
        backend.close()
    return 0


def cmd_write(args, backend_factory=rc.EsptoolBackend, ask=input) -> int:
    src = Path(args.file)
    if not src.is_file():
        raise rc.RomToolError(f"File non trovato: {src}")
    data = src.read_bytes()
    address = rc.parse_address(args.indirizzo)
    backend, info = connect(args, backend_factory)
    bar = TerminalBar()
    try:
        print_info(info)
        rc.validate_write(len(data), address, info.flash_size)
        print("ATTENZIONE: operazione DISTRUTTIVA. Il contenuto attuale della flash")
        print("in quell'intervallo verrà cancellato e sostituito.")
        print(f"  File:       {src}")
        print(f"  Dimensione: {len(data)} byte ({rc.format_bytes(len(data))})")
        print(f"  Indirizzo:  0x{address:08X}")
        print(f"  Flash:      {info.flash_size} byte, chip {info.description or info.chip}, MAC {info.mac}")
        answer = ask(f"Digita {rc.CONFIRM_WORD} per confermare (altro = annulla): ")
        if not rc.check_confirmation(answer):
            print("Annullato: nessuna scrittura eseguita.")
            return 1
        res = rc.write_image(backend, info, data, address, confirmation=answer,
                             on_progress=bar, log=lambda m: (bar.finish(), _log(m)))
        bar.finish()
        print(f"Scrittura verificata in {rc.format_duration(res['seconds'])}.")
    except BaseException:
        bar.finish()
        raise
    finally:
        backend.close()
    return 0


def cmd_gui(args) -> int:
    import rom_gui
    return rom_gui.main(autoclose_ms=int(args.autochiudi * 1000) if args.autochiudi else None)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="rom_tool.py",
                                description="Legge e scrive la flash delle schede ESP32/ESP32-S3.")
    sub = p.add_subparsers(dest="cmd")

    def common(sp, size=True):
        sp.add_argument("--porta", help="porta seriale (default: rilevata da sola)")
        sp.add_argument("--baud", type=int, default=rc.DEFAULT_BAUD,
                        help=f"velocità seriale (default {rc.DEFAULT_BAUD})")
        if size:
            sp.add_argument("--dimensione", help="forza la dimensione della flash, es. 8MB")

    sub.add_parser("porte", help="elenca le porte seriali (senza aprirle)")
    common(sub.add_parser("info", help="mostra chip, MAC e flash"))
    r = sub.add_parser("read", help="backup dell'intera flash in un .bin")
    common(r)
    r.add_argument("-o", "--output", help="file o cartella di destinazione")
    r.add_argument("--blocco", type=int, default=rc.DEFAULT_CHUNK // 1024,
                   help="dimensione del blocco in KB (default 256)")
    r.add_argument("--tentativi", type=int, default=rc.DEFAULT_RETRIES,
                   help="tentativi per blocco (default 5)")
    r.add_argument("--riprendi", action="store_true",
                   help="riprende da un file .part lasciato da un backup interrotto")
    w = sub.add_parser("write", help="scrive un .bin nella flash (distruttivo)")
    common(w, size=False)
    w.add_argument("file")
    w.add_argument("--indirizzo", default="0x0", help="indirizzo di partenza (default 0x0)")
    g = sub.add_parser("gui", help="apre la finestra")
    g.add_argument("--autochiudi", type=float, default=0, help=argparse.SUPPRESS)
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    handlers = {"porte": cmd_ports, "info": cmd_info, "read": cmd_read,
                "write": cmd_write, "gui": cmd_gui, None: None}
    if args.cmd is None:
        args.autochiudi = 0
        return cmd_gui(args)
    try:
        return handlers[args.cmd](args)
    except KeyboardInterrupt:
        print("\nInterrotto.", file=sys.stderr)
        return 130
    except rc.RomToolError as e:
        print(f"Errore: {e}", file=sys.stderr)
        return 2
    except Exception as e:  # errori di esptool/pyserial
        print(f"Errore: {type(e).__name__}: {e}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
