# Multipiattaforma (Windows e Linux) — piano

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** servizio, plugin e strumenti di Calamaio (EIO) funzionano su Windows 10/11 e Linux come su macOS, provati con una CI GitHub Actions; Android entra nella roadmap.

**Architecture:** il servizio Go è già quasi portabile: si aggiunge l'avvio automatico di Windows (Utilità di pianificazione), un filtro degli indirizzi per il QR, e si fa diventare verde la suite su Windows/Linux. Gli script bash diventano un lanciatore Python unico (`tools/rom-tool/avvia.py`) sopra lo strumento ROM già portabile; gli `.sh`/`.command` restano come scorciatoie, si aggiungono `.bat`. Una CI su `ubuntu-latest` e `windows-latest` esegue i test a ogni push sul repository privato.

**Tech Stack:** Go 1.24+, Python 3.10+ (esptool 5.5–5.x, pyserial), GitHub Actions, PlatformIO (solo Linux in CI).

**Spec:** decisioni dell'utente del 2026-10-18 in chat + audit `<scratchpad>/multipiattaforma-audit.md` (riassunto sotto, in «Decisioni»).

## Decisioni (vincolanti)
- Prove: **solo CI GitHub Actions** sul repository privato (`origin`). Matrice `ubuntu-latest` + `windows-latest`; macOS resta provato sul Mac (i runner macOS costano 10× i minuti).
- Script di backup/ripristino e avvio dello strumento ROM: **riscritti in Python** riusando `romcore`; gli `.sh` e `.command` restano come scorciatoie che chiamano il Python.
- Windows: **niente ACL esplicite** sui file segreti; ci si affida alla cartella dell'utente (`%AppData%`, `%LocalAppData%`) e lo si scrive nella documentazione (sezione sicurezza).
- Windows, spegnimento: Go traduce già CTRL_C/CTRL_BREAK/CTRL_CLOSE in segnali (`bye` parte chiudendo la finestra o con Ctrl+C). Alla disconnessione/spegnimento di Windows un programma non-servizio viene chiuso senza preavviso: la scheda se ne accorge col timeout. Si documenta, niente finestra nascosta per WM_ENDSESSION (YAGNI).
- Android: solo voce di roadmap (README inglese e italiano, `docs/ISTRUZIONI-SVILUPPO.md`), con ciò che si riusa e ciò che va riscritto (dall'audit).
- Fuori piano: installer/binari di release, `os.Link` (NTFS lo supporta), `tools/cattura`, iOS.

## Global Constraints
- Commenti brevi in italiano, identificatori in inglese; testi della CLI in italiano come il resto.
- Nessun commit con riga `Co-Authored-By`.
- Mai dati personali (nome, MAC vero, percorsi `/Users/...`) nei file; MAC di esempio `aa:bb:cc:dd:48:e8`.
- Regole §7 dei permessi intoccabili: nessun task cambia `internal/api` dei permessi, `phoneRoutes` o il firmware.
- Il comportamento su macOS non cambia: i test esistenti restano verdi (`cd service && go test ./...`, `tools/rom-tool/.venv/bin/python -m unittest discover -s tools/rom-tool/tests`, `cd firmware && pio test -e native`).
- Nessun push: lo fa il coordinatore, sul solo `origin` (privato).

## Review Focus
- Percorsi Windows con spazi e lettere di unità (`C:\Program Files\calamaio\calamaio.exe`, `C:\Users\Mario Rossi\...`) nella XML dell'attività e negli argomenti: devono arrivare intatti (test in Task 1).
- Fine riga CRLF: con `core.autocrlf=true` (default dei runner Windows) i file golden/fixture cambiano e i confronti byte a byte falliscono (Task 4, `.gitattributes`).
- Linux senza permesso sulla seriale (`/dev/ttyUSB0` → PermissionError): messaggio che spiega il gruppo `dialout` (test in Task 2).
- Più porte o nessuna porta su Windows (`COM3`, `COM7`): stessa scelta di oggi, senza `lsof` (test in Task 2).
- PC con WSL/Hyper-V/Docker/VPN: il QR non deve proporre come primo indirizzo quello di un adattatore virtuale (test in Task 3).

---

### Task 1: Avvio automatico su Windows (Utilità di pianificazione)

**Files:**
- Modify: `service/internal/autostart/autostart.go` (Kind, File, pathEnv, Plan, Stat/loaded, installSteps, uninstallSteps, unsupportedMsg, commento del pacchetto)
- Modify: `service/internal/autostart/files.go` (nuova `taskXML`)
- Modify: `service/cmd/calamaio/main.go` (opzioni di `serve`)
- Test: `service/internal/autostart/autostart_test.go`, `service/cmd/calamaio/*_test.go` dove si provano le opzioni di serve

**Interfaces:**
- Produces: `Kind()` restituisce `"schtasks"` per `GOOS=="windows"`; costante `TaskName = "Calamaio"`; `File()` su Windows = `filepath.Join(BackupDir, "calamaio-task.xml")` (copia leggibile di ciò che è registrato); `func (e Env) taskXML(args []string) []byte` (UTF-16LE con BOM, come vuole `schtasks /XML`).
- Produces: `calamaio serve --log-file PATH` (scrive log, stdout e stderr in append su PATH) e `calamaio serve --autostart KIND` (imposta `CALAMAIO_AUTOSTART=KIND` nel processo: serve perché l'attività di Windows non può passare variabili d'ambiente).

Contenuto dell'attività (valori fissi):
- trigger `LogonTrigger` dell'utente corrente (`<UserId>` = `getenv("USERDOMAIN")+"\\"+getenv("USERNAME")`, solo `USERNAME` se il dominio è vuoto);
- `Principal` con `LogonType` `InteractiveToken`, `RunLevel` `LeastPrivilege`;
- `Settings`: `MultipleInstancesPolicy` `IgnoreNew`, `DisallowStartIfOnBatteries` false, `StopIfGoingOnBatteries` false, `ExecutionTimeLimit` `PT0S`, `RestartOnFailure` con `Interval` `PT1M` (minimo ammesso da Windows; RestartSec=30 vale solo per launchd/systemd) e `Count` 999, `Hidden` true;
- azione `Exec`: `Command` `conhost.exe`, `Arguments` `--headless "<exe>" serve <opzioni> --log-file "<LogFile>" --autostart schtasks` (finestra invisibile; ogni argomento fra virgolette se contiene spazi, virgolette interne raddoppiate come vuole CommandLineToArgvW), `WorkingDirectory` = Home.

Passi di installazione: se registrata `schtasks /End /TN Calamaio` (ferma senza `bye`: lo dice il testo del passo); poi `schtasks /Create /TN Calamaio /XML <File> /F`, poi `schtasks /Run /TN Calamaio`. Disinstallazione: `/End`, poi `schtasks /Delete /TN Calamaio /F`, poi si toglie il file (con copia in BackupDir come per gli altri). `loaded` = `schtasks /Query /TN Calamaio` esce con 0. `pathEnv` su Windows: cartella del binario + `getenv("PATH")` uniti con `;` (non serve all'attività, che eredita il PATH dell'utente: usarlo solo dove oggi serve, nessun percorso Unix su Windows).

- [ ] **Step 1: test che falliscono** in `autostart_test.go`, con `Env{GOOS:"windows", Home:`C:\Users\Mario Rossi`, Exe:`C:\Program Files\calamaio\calamaio.exe`, BackupDir:…, LogDir:…, Getenv: map USERDOMAIN=PC, USERNAME="Mario Rossi"}` e il Runner finto già usato dai test:
  - `TestWindowsKindAndFile`: `Kind()=="schtasks"`, `File()` finisce con `calamaio-task.xml`;
  - `TestWindowsTaskXML`: decodifica UTF-16LE, inizia con BOM `FF FE`; contiene `<UserId>PC\Mario Rossi</UserId>`, `<Command>conhost.exe</Command>`, argomenti esatti `--headless "C:\Program Files\calamaio\calamaio.exe" serve --firmware "C:\fw dir" --log-file "<LogDir>\calamaio.log" --autostart schtasks` (con extra `--firmware`, `C:\fw dir`), `<Interval>PT1M</Interval>`, `<Count>999</Count>`, `<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>`; caratteri XML (`&`, `<`) nei percorsi escapati;
  - `TestWindowsInstallSteps`: senza attività registrata i comandi sono esattamente `schtasks /Create /TN Calamaio /XML <File> /F` e `schtasks /Run /TN Calamaio`; con attività registrata il primo è `schtasks /End /TN Calamaio`;
  - `TestWindowsUninstallSteps`: `/End` poi `/Delete /TN Calamaio /F`;
  - aggiornare il test che oggi si aspetta `ErrUnsupported` su windows: ora lo si aspetta su `GOOS:"freebsd"`.
- [ ] **Step 2:** `cd service && go test ./internal/autostart/` → FAIL.
- [ ] **Step 3:** implementare come sopra; `Status.Kind` documenta `launchd · systemd · schtasks`.
- [ ] **Step 4: opzioni di serve.** Test in `cmd/calamaio`: `serve --log-file F` crea F e ci scrive la riga d'avvio; `serve --autostart schtasks` fa risultare `Status.Managed` vero (usare il modo in cui i test esistenti avviano serve senza rete, es. `--fake` con contesto annullato; se non c'è, provare la funzione che interpreta le opzioni). Implementare in `main.go`: aprire F in `O_APPEND|O_CREATE|O_WRONLY` 0600, `log.SetOutput(f)`, `os.Stdout = f`, `os.Stderr = f`; `--autostart` → `os.Setenv(autostart.EnvMarker, v)` prima di tutto il resto.
- [ ] **Step 5:** `go vet ./... && go test ./...` (sul Mac) e `GOOS=windows go vet ./...`, `GOOS=linux go vet ./...` → PASS.
- [ ] **Step 6: commit** `Avvio automatico su Windows con l'Utilità di pianificazione; serve --log-file e --autostart`.

### Task 2: Strumento ROM e backup multipiattaforma in Python

**Files:**
- Create: `tools/rom-tool/avvia.py`, `tools/rom-tool/avvia.bat`, `tools/rom-tool/avvia.sh`, `tools/backup/backup-flash.bat`, `tools/backup/restore-flash.bat`
- Modify: `tools/rom-tool/avvia.command`, `tools/backup/backup-flash.sh`, `tools/backup/restore-flash.sh`, `tools/backup/comune.sh` (eliminare se non serve più), `tools/rom-tool/romcore.py`, `tools/rom-tool/rom_tool.py`, `tools/gb/fetch-test-roms.sh`, `tools/rom-tool/README.md`
- Test: `tools/rom-tool/tests/test_romcore.py`, nuovo `tools/rom-tool/tests/test_avvia.py`

**Interfaces:**
- Produces: `python tools/rom-tool/avvia.py [ARGOMENTI DI rom_tool.py]`: crea/aggiorna `.venv` accanto (stampo SHA-256 di requirements.txt in `.venv/.requisiti.sha256`, come oggi), poi esegue `rom_tool.py` con gli argomenti (nessuno = `gui`). Python del venv: `.venv/Scripts/python.exe` su Windows, `.venv/bin/python` altrove. Usa `uv` se c'è, altrimenti `python -m venv` + `pip`. Funzioni pure testabili: `venv_python(venv: Path, os_name: str) -> Path`, `needs_install(venv: Path, req: Path) -> bool`.
- Produces: `rom_tool.py write FILE` prima di scrivere: (a) se esiste `FILE.sha256` deve corrispondere, altrimenti `RomToolError("Il file non corrisponde alla sua impronta: non scrivo.")`; se manca stampa un avviso; (b) se il nome del file non contiene `_<mac senza due punti>_` della scheda collegata chiede di digitare `FORZA` (costante `FORCE_WORD = "FORZA"` in romcore, funzione `backup_matches_mac(name: str, mac: str) -> bool`), poi la solita `SCRIVI`.
- Produces: `rom_tool.py read --cartella-backup` = scrive in `<radice repo>/backup-fabbrica/` (crea la cartella).

Scorciatoie (una riga di lavoro ciascuna, nessuna logica): `avvia.command` (macOS) e `avvia.sh` (Linux) → `python3 "$(dirname "$0")/avvia.py" "$@"` con la pausa finale solo nel `.command`; `avvia.bat` → `py -3 "%~dp0avvia.py" %*` con ripiego su `python`, e `pause` se lanciato con doppio clic. `backup-flash.{sh,bat}` → `avvia.py read --cartella-backup [--porta P]`; `restore-flash.{sh,bat}` → `avvia.py write FILE [--porta P]`. Il percorso fisso di mise in `avvia.command` sparisce.

`romcore`:
- `find_ports`: nessun cambiamento di logica; aggiungere test con porte `COM3`/`COM7` (VID 0x10C4) e `/dev/ttyUSB0`, `/dev/ttyACM0` che risultano candidate.
- `port_in_use`: su Windows (`os.name=="nt"`) restituisce `[]` senza cercare `lsof` (Windows rifiuta da solo una porta già aperta: l'errore di apertura va tradotto, vedi sotto); su Linux se manca `lsof` prova `fuser` (`fuser DEVICE` → PID su stdout).
- nuova `explain_open_error(exc: Exception, device: str, platform: str) -> str | None`: `PermissionError`/`errno 13` su linux → «Non hai il permesso di usare DEVICE. Aggiungi il tuo utente al gruppo dialout: sudo usermod -aG dialout $USER, poi esci e rientra.»; su Windows «Accesso negato» / `PermissionError` → «La porta DEVICE è già aperta da un altro programma (monitor seriale, Arduino IDE, la pagina Firmware nel browser): chiudilo e riprova.». Usata da `rom_tool.main` e dalla GUI dove oggi si mostra l'errore.

- [ ] **Step 1: test che falliscono**: `test_avvia.py` (`venv_python` per `nt`/`posix`; `needs_install` vero senza stampo, falso con stampo uguale, vero con requirements cambiato); in `test_romcore.py`: porte Windows/Linux candidate; `port_in_use` con `os.name` finto `nt` → `[]` e runner mai chiamato; `fuser` usato se `lsof` manca; `explain_open_error` per i due sistemi; `backup_matches_mac("esp32s3_aabbccdd48e8_20261018-101500.bin","aa:bb:cc:dd:48:e8")` vero, con un altro MAC falso; `cmd_write` con `.sha256` sbagliato → errore e nessuna scrittura; senza MAC nel nome chiede `FORZA`, risposta diversa → codice 1 e nessuna scrittura.
- [ ] **Step 2:** `tools/rom-tool/.venv/bin/python -m unittest discover -s tools/rom-tool/tests` → FAIL.
- [ ] **Step 3:** implementare.
- [ ] **Step 4:** test → PASS; `bash -n` sugli `.sh`; `tools/gb/fetch-test-roms.sh` usa `sha256sum -c` se c'è, altrimenti `shasum -a 256 -c`.
- [ ] **Step 5:** `tools/rom-tool/README.md`: avvio su macOS/Windows/Linux (python3-tk su Linux: `sudo apt install python3-tk`), driver CP210x su Windows, gruppo dialout su Linux.
- [ ] **Step 6: commit** `Strumento ROM e backup multipiattaforma: lanciatore Python, controlli FORZA e impronta nel write, porte Windows e Linux`.

### Task 3: Indirizzi del QR del telefono senza adattatori virtuali

**Files:**
- Modify: `service/internal/phones/link.go` (la funzione che raccoglie gli IP con `net.Interfaces()`, riga ~21)
- Test: `service/internal/phones/link_test.go`

**Interfaces:**
- Produces: `func rankAddrs(ifs []ifaceAddrs) []string` (o nome equivalente già presente: la parte pura, separata da `net.Interfaces()`), con `type ifaceAddrs struct{ Name string; Flags net.Flags; Addrs []net.IP }`. Ordine: prima le interfacce fisiche, poi quelle virtuali; mai loopback, link-local o interfacce giù. Virtuali = nome che contiene, senza distinzione di maiuscole, uno fra `vEthernet`, `WSL`, `Hyper-V`, `docker`, `br-`, `veth`, `virbr`, `VirtualBox`, `vboxnet`, `VMware`, `vmnet`, `utun`, `tun`, `tap`, `wg`, `tailscale`, `ZeroTier`. Le virtuali restano in fondo (possono servire), non si scartano. Dentro lo stesso gruppo resta l'ordine di oggi.

- [ ] **Step 1: test** `TestRankAddrsVirtualLast`: input `vEthernet (WSL)` 172.20.0.1, `Wi-Fi` 192.168.1.20, `docker0` 172.17.0.1, `Loopback` 127.0.0.1, `en0` con `fe80::1` → `["192.168.1.20","172.20.0.1","172.17.0.1"]`.
- [ ] **Step 2:** `go test ./internal/phones/` → FAIL. **Step 3:** implementare. **Step 4:** PASS (tutti i test di phones). 
- [ ] **Step 5: commit** `QR del telefono: adattatori virtuali (WSL, Docker, VPN) in fondo`.

### Task 4: CI su Linux e Windows, e suite verde

**Files:**
- Create: `.github/workflows/ci.yml`, `.gitattributes`
- Modify: i test o il codice che falliscono in CI (solo per ragioni di piattaforma), `plugin/skills/setup/SKILL.md`

**Interfaces:**
- Consumes: Task 1–3.

`.gitattributes`: `* text=auto eol=lf`; `*.bat text eol=crlf`; binari espliciti `*.bin *.gb *.png *.jpg *.woff *.woff2 *.ttf *.otf *.zip *.pdf binary`. Dopo averlo aggiunto: `git add --renormalize .` e controllare con `git status` che nessun file cambi contenuto (se cambia, guardare il diff: deve essere solo fine riga).

`ci.yml` (nome `CI`, su `push` e `pull_request` e `workflow_dispatch`; `concurrency` per ramo con `cancel-in-progress`):
- job `service`, matrice `os: [ubuntu-latest, windows-latest]`: `actions/checkout@v4`, `actions/setup-go@v5` con `go-version-file: service/go.mod` e cache, `actions/setup-node@v4` node 22 (alcuni test Go lanciano node), poi in `service/`: `go vet ./...`, `go test ./...`, `go build ./cmd/calamaio`.
- job `rom-tool`, stessa matrice: `actions/setup-python@v5` 3.12, `pip install -r tools/rom-tool/requirements.txt`, `python -m unittest discover -s tools/rom-tool/tests` (i test della GUI si saltano da soli senza display: se non lo fanno, aggiungere lo skip con `DISPLAY`/`tkinter.TclError`).
- job `firmware`, solo `ubuntu-latest`: python 3.12, `pip install platformio==6.1.*` (stessa versione maggiore del Mac), cache `~/.platformio`, `cd firmware && pio test -e native`.
- Nessun segreto, nessun deploy, `permissions: contents: read`.

- [ ] **Step 1:** scrivere `.gitattributes` e rinormalizzare (verifica sopra); `ci.yml`; `plugin/skills/setup/SKILL.md` senza `command -v` (usare `calamaio version`) e senza `curl` (usare `calamaio status`).
- [ ] **Step 2:** commit `CI su Linux e Windows (servizio, strumento ROM, firmware native); fine riga normalizzati`. Il coordinatore fa il push su `origin` e legge l'esito con `gh run watch` / `gh run view --log-failed`.
- [ ] **Step 3: ciclo** per ogni fallimento: capire la causa (percorsi `/tmp` fissi → `t.TempDir()`; separatori → `filepath`; permessi POSIX → skip su windows come già fatto in backup_test.go:84; file aperti durante rename → chiudere prima; fine riga), correggere il minimo, commit per gruppo di cause (`CI Windows: <causa>`), push, ripetere finché tutti i job sono verdi. Mai disattivare un test per farlo passare senza una ragione di piattaforma scritta in un commento; mai toccare la logica §7.
- [ ] **Step 4:** sul Mac tutte le suite dei Global Constraints restano verdi.

### Task 5: Documentazione Windows/Linux e roadmap Android

**Files:**
- Modify: `README.md` (inglese), `README.it.md`, `docs/ISTRUZIONI-SVILUPPO.md` (roadmap), `docs/DECISIONI.md` (nuova sezione «Multipiattaforma, 2026-10-18»), `docs/PROTOCOLLO.md` (riga ~681 sull'avvio automatico Windows)

Contenuto:
- «Setting up the Mac» diventa «Prerequisites» con tre sottosezioni: **macOS** (com'è), **Windows 10/11** (`winget install Git.Git GoLang.Go Python.Python.3.12 astral-sh.uv OpenJS.NodeJS.LTS`, PlatformIO con `uv tool install platformio`, driver CP210x di Silicon Labs, porta `COM3` in Gestione dispositivi, PowerShell; Claude Code su Windows richiede Git for Windows), **Linux** (Debian/Ubuntu: `sudo apt install git golang python3 python3-venv python3-tk nodejs`, uv, PlatformIO, `sudo usermod -aG dialout $USER`, porta `/dev/ttyUSB0`).
- Build del servizio per sistema (Windows: `go build -o $env:USERPROFILE\bin\calamaio.exe .\cmd\calamaio` e aggiunta al PATH utente), avvio automatico per sistema (`calamaio install` = launchd / systemd utente / Utilità di pianificazione; su Windows lo spegnimento del PC non manda `bye`: la scheda passa a «PC spento» dopo il timeout).
- Strumento ROM e backup: `avvia.command` / `avvia.bat` / `avvia.sh`; `backup-flash` e `restore-flash` in `.sh` e `.bat`; esempi di porta per i tre sistemi.
- Firewall: Windows chiede al primo `calamaio serve` di consentire le reti private (va consentito, porte 8765 solo locale, 8767 telefoni, 5353 mDNS); Linux con ufw: `sudo ufw allow 8767/tcp` e `5353/udp`.
- Sicurezza: tabella delle cartelle dati per sistema (macOS `~/Library/Application Support/calamaio`; Windows configurazione `%AppData%\calamaio`, dati `%LocalAppData%\calamaio`; Linux `~/.config/calamaio` e `~/.local/share/calamaio`) e la nota: su Windows i file con le chiavi sono protetti dai permessi della cartella utente (non da permessi propri del file).
- Stato delle funzioni: «Avvio automatico» → macOS provato dal vivo, Linux e Windows provati in CI; nuova riga «Android app: planned».
- Roadmap Android (README e ISTRUZIONI): riusabili protocollo telefono-PC (porta 8767, TLS con impronta fissata, token, QR `calamaio://pair`), webapp, UUID BLE e report v1 con i vettori; da riscrivere WebView con intercettazione, pinning, Keystore, NsdManager, scanner QR, BLE peripheral (punto più rischioso: annuncio e associazione cifrata variano tra marche). Lato PC non cambia nulla.
- Testi a schermo e nomi delle voci del menu identici a oggi.

- [ ] **Step 1:** scrivere; controllare con uno script che tutti i link relativi dei due README esistano e che non compaiano `/Users/` né le parole di `tools/screenshots/vietati.local.txt` (file locale, escluso da git).
- [ ] **Step 2: commit** `Documentazione per Windows e Linux, roadmap Android`.
