"""Finestra Tkinter dello strumento ROM di Calamaio OS."""

from __future__ import annotations

import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import romcore as rc  # noqa: E402

SIZE_CHOICES = ("auto", "2MB", "4MB", "8MB", "16MB", "32MB")


def default_backup_dir() -> Path:
    cand = HERE.parent.parent / "backup-fabbrica"
    return cand if cand.is_dir() else Path.home() / "Downloads"


class ConfirmWriteDialog(tk.Toplevel):
    """Finestra modale: il pulsante si abilita solo digitando SCRIVI."""

    def __init__(self, parent, summary: list[tuple[str, str]]):
        super().__init__(parent)
        self.title("Conferma scrittura della flash")
        self.resizable(False, False)
        self.result: str | None = None
        frm = ttk.Frame(self, padding=14)
        frm.pack(fill="both", expand=True)
        ttk.Label(frm, text="ATTENZIONE: operazione DISTRUTTIVA",
                  foreground="#b00020", font=("TkDefaultFont", 13, "bold")).pack(anchor="w")
        ttk.Label(frm, text="Il contenuto attuale della flash in questo intervallo verrà "
                            "cancellato e sostituito dal file.", wraplength=420).pack(anchor="w", pady=(4, 8))
        grid = ttk.Frame(frm)
        grid.pack(fill="x")
        for i, (k, v) in enumerate(summary):
            ttk.Label(grid, text=k + ":").grid(row=i, column=0, sticky="w", padx=(0, 8))
            ttk.Label(grid, text=v, font=("TkFixedFont", 11)).grid(row=i, column=1, sticky="w")
        ttk.Label(frm, text=f"Per confermare scrivi {rc.CONFIRM_WORD} qui sotto:").pack(anchor="w", pady=(10, 2))
        self.var = tk.StringVar()
        self.entry = ttk.Entry(frm, textvariable=self.var, width=20)
        self.entry.pack(anchor="w")
        btns = ttk.Frame(frm)
        btns.pack(fill="x", pady=(12, 0))
        self.ok_btn = ttk.Button(btns, text="Scrivi la flash", command=self._ok, state="disabled")
        self.ok_btn.pack(side="right")
        ttk.Button(btns, text="Annulla", command=self._cancel).pack(side="right", padx=6)
        self.var.trace_add("write", lambda *_: self._refresh())
        self.protocol("WM_DELETE_WINDOW", self._cancel)
        self.bind("<Escape>", lambda _e: self._cancel())
        self.transient(parent)
        self.entry.focus_set()

    def _refresh(self):
        self.ok_btn.configure(state="normal" if rc.check_confirmation(self.var.get()) else "disabled")

    def _ok(self):
        if rc.check_confirmation(self.var.get()):
            self.result = self.var.get().strip()
            self.destroy()

    def _cancel(self):
        self.result = None
        self.destroy()

    def run(self) -> str | None:
        self.grab_set()
        self.wait_window()
        return self.result


class RomApp:
    def __init__(self, root: tk.Tk, backend_factory=rc.EsptoolBackend,
                 find_ports=rc.find_ports, port_checker=rc.ensure_port_free,
                 ask_save=None, ask_open=None, confirm=None):
        self.root = root
        self.backend_factory = backend_factory
        self.find_ports = find_ports
        self.port_checker = port_checker
        self.ask_save = ask_save or self._ask_save_dialog
        self.ask_open = ask_open or self._ask_open_dialog
        self.confirm = confirm or (lambda summary: ConfirmWriteDialog(self.root, summary).run())
        self.q: queue.Queue = queue.Queue()
        self.cancel = threading.Event()
        self.busy = False
        self.info: rc.DeviceInfo | None = None
        self.candidates: list[rc.PortCandidate] = []
        self.last_result = None
        self._build()
        self.refresh_ports()
        self.root.after(80, self._poll)

    # --- costruzione finestra ---
    def _build(self):
        r = self.root
        r.title("Calamaio OS - Strumento ROM (ESP32 / ESP32-S3)")
        r.minsize(720, 560)
        main = ttk.Frame(r, padding=10)
        main.pack(fill="both", expand=True)

        top = ttk.Frame(main)
        top.pack(fill="x")
        ttk.Label(top, text="Porta:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=46, state="readonly")
        self.port_box.pack(side="left", padx=(4, 4))
        ttk.Button(top, text="Aggiorna", command=self.refresh_ports).pack(side="left")
        ttk.Label(top, text="Baud:").pack(side="left", padx=(12, 0))
        self.baud_var = tk.StringVar(value=str(rc.DEFAULT_BAUD))
        ttk.Combobox(top, textvariable=self.baud_var, width=8, state="readonly",
                     values=[str(b) for b in rc.BAUD_CHOICES]).pack(side="left", padx=4)
        ttk.Label(top, text="Flash:").pack(side="left", padx=(12, 0))
        self.size_var = tk.StringVar(value="auto")
        ttk.Combobox(top, textvariable=self.size_var, width=6, state="readonly",
                     values=SIZE_CHOICES).pack(side="left", padx=4)

        acts = ttk.Frame(main)
        acts.pack(fill="x", pady=(10, 6))
        self.btn_detect = ttk.Button(acts, text="Rileva", command=self.on_detect)
        self.btn_read = ttk.Button(acts, text="Leggi (backup)", command=self.on_read)
        self.btn_write = ttk.Button(acts, text="Scrivi...", command=self.on_write)
        self.btn_cancel = ttk.Button(acts, text="Annulla", command=self.on_cancel, state="disabled")
        for b in (self.btn_detect, self.btn_read, self.btn_write):
            b.pack(side="left", padx=(0, 6))
        ttk.Label(acts, text="Indirizzo scrittura:").pack(side="left", padx=(12, 2))
        self.addr_var = tk.StringVar(value="0x0")
        ttk.Entry(acts, textvariable=self.addr_var, width=10).pack(side="left")
        self.btn_cancel.pack(side="right")

        box = ttk.LabelFrame(main, text="Dispositivo", padding=8)
        box.pack(fill="x")
        self.info_vars: dict[str, tk.StringVar] = {}
        empty = rc.DeviceInfo(port="-", chip="-", description="-").rows()
        for i, (k, _v) in enumerate(empty):
            ttk.Label(box, text=k + ":").grid(row=i, column=0, sticky="w", padx=(0, 10))
            var = tk.StringVar(value="-")
            ttk.Label(box, textvariable=var).grid(row=i, column=1, sticky="w")
            self.info_vars[k] = var

        prog = ttk.Frame(main)
        prog.pack(fill="x", pady=(10, 4))
        self.progress = ttk.Progressbar(prog, orient="horizontal", mode="determinate", maximum=1000)
        self.progress.pack(fill="x")
        self.progress_text = tk.StringVar(value="Pronto.")
        ttk.Label(prog, textvariable=self.progress_text, font=("TkFixedFont", 11)).pack(anchor="w", pady=(2, 0))

        ttk.Label(main, text="Registro:").pack(anchor="w")
        self.log_box = ScrolledText(main, height=12, state="disabled", font=("TkFixedFont", 10))
        self.log_box.pack(fill="both", expand=True)

    # --- utilità ---
    def log(self, msg: str):
        self.log_box.configure(state="normal")
        self.log_box.insert("end", msg + "\n")
        self.log_box.see("end")
        self.log_box.configure(state="disabled")

    def _post(self, kind, payload=None):
        self.q.put((kind, payload))

    def _set_busy(self, busy: bool, cancellable: bool = False):
        self.busy = busy
        state = "disabled" if busy else "normal"
        for b in (self.btn_detect, self.btn_read, self.btn_write):
            b.configure(state=state)
        self.btn_cancel.configure(state="normal" if (busy and cancellable) else "disabled")

    def _show_info(self, info: rc.DeviceInfo | None):
        rows = info.rows() if info else rc.DeviceInfo(port="-", chip="-", description="-").rows()
        for k, v in rows:
            self.info_vars[k].set(v if info else "-")

    def _show_progress(self, st: rc.ProgressState):
        self.progress["value"] = st.percent * 10
        self.progress_text.set(st.text())

    def selected_port(self) -> str | None:
        label = self.port_var.get()
        for c in self.candidates:
            if c.label() == label:
                return c.device
        return None

    def refresh_ports(self):
        try:
            self.candidates, ignored = self.find_ports()
        except Exception as e:
            self.log(f"Errore nell'elenco porte: {e}")
            return
        labels = [c.label() for c in self.candidates]
        self.port_box.configure(values=labels)
        if len(labels) == 1:
            self.port_var.set(labels[0])
        elif self.port_var.get() not in labels:
            self.port_var.set("")
        # scrive nel registro solo se l'elenco è cambiato
        snapshot = (tuple(labels), tuple(ignored))
        if snapshot == getattr(self, "_last_ports", None):
            return
        self._last_ports = snapshot
        if not labels:
            self.log("Nessuna scheda ESP trovata. Collega la scheda e premi Aggiorna.")
        elif len(labels) == 1:
            self.log(f"Scheda trovata: {labels[0]}")
        else:
            self.log(f"Trovate {len(labels)} schede: scegli la porta dal menu.")
        for dev, why in ignored:
            self.log(f"  ignorata {dev} ({why})")

    def _params(self):
        port = self.selected_port()
        if not port:
            if not self.candidates:
                raise rc.NoDeviceError("Nessuna scheda ESP trovata.")
            raise rc.RomToolError("Ci sono più schede: scegli la porta dal menu.")
        return port, int(self.baud_var.get()), rc.parse_size(self.size_var.get())

    def _start(self, target, *args, cancellable=False):
        if self.busy:
            return
        self.cancel.clear()
        self._set_busy(True, cancellable)
        threading.Thread(target=self._guard, args=(target, *args), daemon=True).start()

    def _guard(self, target, *args):
        follow = None
        try:
            follow = target(*args)
        except rc.Cancelled as e:
            self._post("log", str(e))
            self._post("status", "Annullato.")
        except rc.RomToolError as e:
            self._post("error", str(e))
        except Exception as e:
            self._post("error", f"{type(e).__name__}: {e}")
        finally:
            self._post("idle")
            # il passo successivo parte solo dopo che la finestra è tornata libera
            if callable(follow):
                self._post("then", follow)

    def _open_backend(self, port, baud, size_override):
        self.port_checker(port)
        self._post("log", f"Connessione a {port} a {baud} baud...")
        backend = self.backend_factory(log=lambda m: self._post("log", m))
        info = backend.connect(port, baud, size_override)
        self._post("info", info)
        return backend, info

    # --- coda dal thread di lavoro ---
    def _poll(self):
        try:
            while True:
                kind, payload = self.q.get_nowait()
                if kind == "log":
                    self.log(payload)
                elif kind == "progress":
                    self._show_progress(payload)
                elif kind == "info":
                    self.info = payload
                    self._show_info(payload)
                elif kind == "status":
                    self.progress_text.set(payload)
                elif kind == "error":
                    self.log("ERRORE: " + payload)
                    self.progress_text.set("Errore: vedi registro.")
                    messagebox.showerror("Errore", payload, parent=self.root)
                elif kind == "done":
                    self.last_result = payload
                elif kind == "then":
                    payload()
                elif kind == "idle":
                    self._set_busy(False)
        except queue.Empty:
            pass
        self.root.after(80, self._poll)

    # --- azioni ---
    def on_detect(self, then=None):
        self.refresh_ports()
        try:
            params = self._params()
        except rc.RomToolError as e:
            messagebox.showwarning("Porta", str(e), parent=self.root)
            return
        self._start(self._job_detect, params, then)

    def _job_detect(self, params, then):
        backend, info = self._open_backend(*params)
        try:
            self._post("status", "Dispositivo rilevato.")
        finally:
            backend.close()
        return then

    def _need_info(self, then) -> bool:
        """Se manca il rilevamento per la porta scelta, lo fa e poi richiama `then`."""
        port = self.selected_port()
        if self.info is None or self.info.port != port:
            self.on_detect(then=then)
            return True
        return False

    def on_read(self):
        if self.busy or self._need_info(self.on_read):
            return
        info = self.info
        path = self.ask_save(default_backup_dir(), rc.default_backup_name(info))
        if not path:
            return
        resume = False
        part = Path(str(path) + ".part")
        if part.exists() and part.stat().st_size > 0:
            resume = messagebox.askyesno("Backup interrotto",
                                         f"Esiste un backup parziale ({rc.format_bytes(part.stat().st_size)}). "
                                         "Riprendo da li?", parent=self.root)
        self._start(self._job_read, self._params(), Path(path), resume, cancellable=True)

    def _job_read(self, params, path: Path, resume: bool):
        backend, info = self._open_backend(*params)
        try:
            if self.info and info.mac and self.info.mac and info.mac != self.info.mac:
                raise rc.RomToolError("La scheda collegata è cambiata dopo il rilevamento. Premi Rileva.")
            self._post("log", f"Backup di {rc.format_bytes(info.flash_size or 0)} in {path}")
            res = rc.backup_flash(backend, info, path, resume=resume,
                                  on_progress=lambda st: self._post("progress", st),
                                  log=lambda m: self._post("log", m), cancel=self.cancel)
            self._post("status", f"Backup completato in {rc.format_duration(res['seconds'])}. "
                                 f"SHA-256 {res['sha256'][:16]}...")
            self._post("done", res)
        finally:
            backend.close()

    def on_write(self):
        if self.busy or self._need_info(self.on_write):
            return
        info = self.info
        try:
            address = rc.parse_address(self.addr_var.get())
        except rc.RomToolError as e:
            messagebox.showerror("Indirizzo", str(e), parent=self.root)
            return
        path = self.ask_open(default_backup_dir())
        if not path:
            return
        data = Path(path).read_bytes()
        try:
            rc.validate_write(len(data), address, info.flash_size)
        except rc.WriteRejected as e:
            self.log("Scrittura rifiutata: " + str(e))
            messagebox.showerror("Scrittura rifiutata", str(e), parent=self.root)
            return
        summary = [
            ("File", Path(path).name),
            ("Dimensione", f"{len(data)} byte ({rc.format_bytes(len(data))})"),
            ("Indirizzo", f"0x{address:08X}"),
            ("Chip", info.description or info.chip),
            ("MAC", info.mac or "-"),
            ("Flash", f"{info.flash_size} byte"),
        ]
        typed = self.confirm(summary)
        if not rc.check_confirmation(typed):
            self.log("Scrittura annullata: conferma non data.")
            return
        self._start(self._job_write, self._params(), data, address, typed, info.mac)

    def _job_write(self, params, data: bytes, address: int, typed: str, expected_mac: str):
        backend, info = self._open_backend(*params)
        try:
            if expected_mac and info.mac != expected_mac:
                raise rc.RomToolError("La scheda collegata non è quella confermata (MAC diverso). "
                                      "Nessuna scrittura eseguita.")
            self._post("log", "Scrittura in corso: NON scollegare la scheda.")
            res = rc.write_image(backend, info, data, address, confirmation=typed,
                                 on_progress=lambda st: self._post("progress", st),
                                 log=lambda m: self._post("log", m))
            self._post("status", f"Scrittura verificata in {rc.format_duration(res['seconds'])}.")
            self._post("done", res)
        finally:
            backend.close()

    def on_cancel(self):
        self.cancel.set()
        self.log("Annullamento richiesto: mi fermo appena possibile...")

    # --- dialoghi file ---
    def _ask_save_dialog(self, folder: Path, name: str):
        return filedialog.asksaveasfilename(parent=self.root, initialdir=str(folder),
                                            initialfile=name, defaultextension=".bin",
                                            filetypes=[("Immagine flash", "*.bin")])

    def _ask_open_dialog(self, folder: Path):
        return filedialog.askopenfilename(parent=self.root, initialdir=str(folder),
                                          filetypes=[("Immagine flash", "*.bin"), ("Tutti i file", "*")])


def main(autoclose_ms: int | None = None) -> int:
    root = tk.Tk()
    RomApp(root)
    if autoclose_ms:
        root.after(autoclose_ms, root.destroy)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
