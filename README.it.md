# EIO — Easy Ink OS

🇬🇧 [English](README.md) · 🇮🇹 Italiano

*Nome in codice: **Calamaio**. Nel codice, nei comandi e sulla scheda si chiama ancora così.*

EIO è un piccolo schermo e-ink a batteria da tenere accanto al computer. Mostra cosa stanno facendo le sessioni di Claude Code: chi lavora, chi aspetta un permesso, chi ha finito. Il progetto ha quattro parti: il **firmware** che gira sulla scheda (ESP32-S3), un **servizio** che gira sul Mac e parla con Claude Code, un **plugin** di Claude Code che manda gli eventi al servizio, e un'**app per iPhone** facoltativa.

> **Progetto personale, in sviluppo.** È stato provato su una sola scheda (sotto, «Progettato e collaudato su»). Su altre schede può non funzionare. Fai sempre il backup prima di scrivere sulla scheda.

## Come si vede

<table>
<tr><td align="center" width="50%"><img src="docs/img/display-home.png" alt="Home (stile Goccia): in primo piano la sessione che chiede attenzione" width="400"><br><sub>Home (stile Goccia): in primo piano la sessione che chiede attenzione</sub></td><td align="center" width="50%"><img src="docs/img/display-home-righe.png" alt="Home stile Righe: tutte le sessioni con l'avanzamento dei task" width="400"><br><sub>Home stile Righe: tutte le sessioni con l'avanzamento dei task</sub></td></tr>
<tr><td align="center" width="50%"><img src="docs/img/display-dettaglio.png" alt="Dettaglio di una sessione al lavoro" width="400"><br><sub>Dettaglio di una sessione al lavoro</sub></td><td align="center" width="50%"><img src="docs/img/display-permesso.png" alt="Richiesta di permesso: CONSENTI o NEGA dal display" width="400"><br><sub>Richiesta di permesso: CONSENTI o NEGA dal display</sub></td></tr>
<tr><td align="center" width="50%"><img src="docs/img/display-menu.png" alt="Menu principale" width="400"><br><sub>Menu principale</sub></td><td align="center" width="50%"><img src="docs/img/display-oggi.png" alt="Oggi: task, permessi, righe e costo stimato" width="400"><br><sub>Oggi: task, permessi, righe e costo stimato</sub></td></tr>
<tr><td align="center" width="50%"><img src="docs/img/display-mascotte.png" alt="La mascotte: livello, XP ed energia" width="400"><br><sub>La mascotte: livello, XP ed energia</sub></td><td align="center" width="50%"><img src="docs/img/display-riposo.png" alt="Riposo: ora, data e sessioni in attesa" width="400"><br><sub>Riposo: ora, data e sessioni in attesa</sub></td></tr>
<tr><td align="center" width="50%"><img src="docs/img/display-tema-mascotte-grande.png" alt="Tema installato «Mascotte grande», con più sessioni" width="400"><br><sub>Tema installato «Mascotte grande», con più sessioni</sub></td><td align="center" width="50%"><img src="docs/img/display-tema-mascotte-grande-una.png" alt="Tema installato «Mascotte grande», con una sola sessione" width="400"><br><sub>Tema installato «Mascotte grande», con una sola sessione</sub></td></tr>
<tr><td align="center" width="50%"><img src="docs/img/display-gioco-dmg-acid2.png" alt="Emulatore Game Boy sul display e-ink (ROM di prova libera dmg-acid2, MIT; le ROM non sono incluse)" width="400"><br><sub>Emulatore Game Boy sul display e-ink (ROM di prova libera dmg-acid2, MIT; le ROM non sono incluse)</sub></td></tr>
</table>

<p align="center"><img src="docs/img/web-panoramica.png" alt="Webapp: panoramica di display e sessioni" width="100%"><br><sub>Webapp: panoramica di display e sessioni</sub></p>

<p align="center"><img src="docs/img/web-impostazioni.png" alt="Webapp: impostazioni Display e Avvisi da remoto" width="100%"><br><sub>Webapp: impostazioni Display e Avvisi da remoto</sub></p>

<p align="center"><img src="docs/img/web-editor.png" alt="Webapp: editor delle schermate (Beta)" width="100%"><br><sub>Webapp: editor delle schermate (Beta)</sub></p>

Immagini generate dal codice di disegno del firmware con dati di esempio (`node tools/screenshots/make.mjs`).

---

## Cosa fa

- **Sessioni**: per ogni sessione di Claude Code mostra lo stato (lavora, aspetta un permesso, aspetta una tua risposta, finito, in errore), il progetto, il consumo del contesto e i limiti di utilizzo.
- **Permessi dal display**: quando Claude chiede il permesso di usare uno strumento, puoi rispondere CONSENTI o NEGA dalla scheda. Le regole di sicurezza, che non si possono aggirare:
  - **NEGA è sempre preselezionato**; per consentire servono due gesti: scegliere CONSENTI e poi confermare;
  - la scheda deve essere **abbinata** al PC con un codice di 6 cifre; ogni risposta è firmata (HMAC) e vale una volta sola (nonce): le risposte false da altri dispositivi in rete vengono scartate;
  - **nessuna approvazione automatica**, mai, per nessuno strumento;
  - le **domande** di Claude (scelte, piani da approvare) si rispondono solo nel terminale;
  - la richiesta resta aperta finché qualcuno risponde, sul display o nel terminale: vale la prima risposta;
  - si spegne da Menu › Avvisi › Permessi dal display;
  - il **telefono usato come controller non può mai dare CONSENTI** (né le altre conferme delicate: Proteggi le chiavi, Ripristino, abbinamento, cancellazione delle reti).
- **Avatar**: ogni sessione può avere un personaggio diverso (Goccia, Gatto, Demone, Calamaro, o creati da te), così capisci a colpo d'occhio chi ti chiama. Formato in [`docs/AVATAR.md`](docs/AVATAR.md).
- **Mascotte e gioco**: la mascotte guadagna punti e traguardi mentre lavori e sblocca avatar e accessori.
- **Temi**: le schermate principali si possono ridisegnare con l'**editor delle schermate** della webapp (**Beta**) e mandare alla scheda. Formato in [`docs/TEMI.md`](docs/TEMI.md).
- **Giochi Game Boy**: un emulatore (Peanut-GB) gira sulla scheda. Si gioca con un controller Xbox via Bluetooth o con l'iPhone come controller. La ROM la metti tu (vedi «Giochi Game Boy»).
- **App iPhone**: apre la webapp dal telefono (abbinata con un QR) e trasforma il telefono in un controller Bluetooth per la scheda.
- **Aggiornamento senza cavo (OTA)**: il servizio manda il firmware nuovo via Wi-Fi. L'immagine è firmata, la scheda la controlla prima di avviarla e, se la versione nuova non parte, torna da sola a quella di prima. Dettagli in [`docs/OTA.md`](docs/OTA.md).
- **Webapp** su <http://127.0.0.1:8765>: panoramica, avatar, gioco, dispositivo (abbinamento, Wi-Fi, registro), firmware (scrittura e backup via cavo dal browser), editor delle schermate.

---

## Progettato e collaudato su:

Tutti i valori vengono da [`docs/HARDWARE.md`](docs/HARDWARE.md), [`docs/DECISIONI.md`](docs/DECISIONI.md), `firmware/platformio.ini`, `firmware/partitions_gb.csv` e dalla lettura della scheda con esptool ed espefuse dalla porta USB (17 ottobre 2026). Dove né la documentazione né la scheda dicono niente c'è scritto «non documentato».

### Scheda

| Voce | Valore |
|---|---|
| Scheda | **Heltec Wireless Paper V1.2** |
| Microcontrollore | **ESP32-S3FN8** (package QFN56 con la flash dentro), revisione v0.2, doppio core Xtensa LX7 a 240 MHz, quarzo a 40 MHz |
| Memoria flash | 8 MB dentro il chip (GigaDevice, id c8/4017, per temperature fino a 85 °C), modalità quad |
| PSRAM | **nessuna** (verificato: gli eFuse della scheda non dichiarano PSRAM) |
| RAM | 512 KB di SRAM nel chip più 16 KB di memoria RTC (datasheet dell'ESP32-S3); circa 320 KB utilizzabili dal programma (stima in DECISIONI, voce «Game Boy Advance») |
| Schermo | e-ink **E0213A367**, **2,13″**, **250×122** pixel (memoria 128×250), bianco e nero |
| Controller dello schermo | comandi della famiglia SSD168x; modello esatto del chip: non documentato (lo schermo non ha un comando che dica il modello, quindi dalla porta USB non si legge) |
| Pin dello schermo | MOSI 2, CLK 3, CS 4, DC 5, RST 6, BUSY 7; alimentazione delle periferiche Vext GPIO45 (attiva bassa) |
| Aggiornamenti dello schermo | completo 1540 ms, parziale 644 ms (al massimo circa 1,5 al secondo), pulizia iniziale 1626 ms; nei giochi un driver veloce a circa 9,8 immagini al secondo (profilo «Pulito») |
| Tasti | uno solo: **PRG** su GPIO0, attivo basso (sveglia la scheda dal sonno). Tenuto premuto mentre colleghi il cavo, fa entrare la scheda nel caricatore seriale |
| LED | GPIO18; in più un LED arancione di carica, collegato solo al caricatore |
| Batteria | lettura su GPIO20 (ADC2 canale 9) con partitore 1:2 acceso da GPIO19 (attivo basso); capacità della batteria: non documentata (dipende da quella collegata) |
| Carica | TP4054, circa 500 mA (calcolato dallo schema, non verificato sul datasheet); si ferma da sola a 4,2 V; lo stato di carica non arriva al microcontrollore |
| Ponte USB-seriale | Silicon Labs **CP2102** (10c4:ea60), su macOS `/dev/cu.usbserial-0001` |
| Velocità della seriale | **115200** affidabile; 460800 e 921600 danno errori di pacchetto con questo CP2102 |
| Wi-Fi | 802.11 b/g/n, **solo 2,4 GHz** (le reti a 5 GHz non si vedono); antenna: non documentata |
| Bluetooth | Bluetooth 5 Low Energy (datasheet del chip); il firmware usa NimBLE, solo ruolo centrale, per il controller |
| eFuse | 6 spazi chiave; con «Proteggi le chiavi» uno diventa la chiave segreta della scheda (vedi sotto) |

### Tabella delle partizioni (`firmware/partitions_gb.csv`)

| Nome | Indirizzo | Dimensione | A cosa serve |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | impostazioni, reti Wi-Fi, chiavi di abbinamento (cifrate) |
| otadata | 0xE000 | 8 KB | quale delle due copie del firmware avviare |
| app0 | 0x10000 | 2,25 MB | firmware |
| app1 | 0x250000 | 2,25 MB | firmware (la seconda copia, per l'aggiornamento senza cavo) |
| gbrom | 0x490000 | 2 MB | la ROM del Game Boy |
| spiffs (LittleFS) | 0x690000 | 1,375 MB | cache di avatar, accessori e temi; salvataggi dei giochi |
| coredump | 0x7F0000 | 64 KB | rapporto dell'ultimo crash |

### Attenzione alle eFuse (irreversibile)

Le eFuse sono piccole memorie dentro il chip che **si scrivono una volta sola, per sempre**. Calamaio ne usa una sola, e solo se lo chiedi tu: Menu › Sicurezza › **Proteggi le chiavi**, con avviso, due pressioni e poi «tieni 3 s». Scrive una chiave casuale che nessuno può più leggere, nemmeno tu; da quel momento password del Wi-Fi e chiavi di abbinamento sono cifrate con quella chiave. Conseguenze:

- **non si può annullare**, nemmeno riscrivendo la flash o rimettendo un backup;
- un backup della flash fatto su questa scheda **non funziona su un'altra** (i segreti sono legati a questo chip);
- nessuno script di questo repository scrive eFuse, e non va mai fatto a mano.

### Software usato per le prove

| Cosa | Versione |
|---|---|
| Firmware Calamaio OS | **0.14.1** |
| Servizio | 0.9.0-game |
| Plugin | 0.5.0 |
| App iPhone | 0.1.0 |
| Piattaforma PlatformIO | `espressif32@6.9.0` (core Arduino 2.0.17, ESP-IDF 4.4.7) |
| Librerie del firmware | heltec-eink-modules 4.6.0, ArduinoJson 7.4.3, links2004/WebSockets 2.7.3, NimBLE-Arduino 2.5.1, Peanut-GB (commit `d0bcca7`), QRCode 0.0.1 (copiata), TweetNaCl, mbedTLS 2.28.7 (dell'IDF) |
| PlatformIO Core | 6.2.0 |
| Go | 1.27.2 (il codice chiede almeno 1.24) |
| Librerie Go | coder/websocket 1.8.15, brutella/dnssd 1.2.14, santhosh-tekuri/jsonschema 6.0.3, golang.org/x/term 0.21.0, rsc.io/qr 0.2.0 |
| esptool | 5.5 e 5.6 |
| Node (solo strumenti di sviluppo) | 22 |
| Xcode e XcodeGen | Xcode 27.1, XcodeGen 2.45.4 |
| iPhone | iPhone 17 Pro con iOS 27; simulatore iOS 27.0 |
| macOS | 27.0.1 |
| Claude Code | 2.1.296 e successive |

### Altre schede

Il **CrowPanel ESP32-S3 4,2″** (Elecrow) è previsto (milestone M7) ma **non è ancora supportato**: non c'è un firmware per lui. Lo stesso vale per le altre schede elencate in `docs/ISTRUZIONI-SVILUPPO.md`.

---

## Stato delle funzioni

| Funzione | Stato |
|---|---|
| Sessioni di Claude Code sul display | stabile |
| Permessi dal display (abbinamento, firma, regole di sicurezza) | stabile |
| Avatar, mascotte e accessori | stabile |
| Aggiornamento senza cavo (OTA) con ritorno automatico | stabile |
| Più reti Wi-Fi, abbinamento, registro da remoto | stabile |
| Avvio automatico del servizio al login | provato su macOS; Linux presente nel codice; **Windows non ancora** |
| **Editor delle schermate e temi** | **BETA**: «Parti dalla schermata di serie» non c'è ancora; la cronologia delle versioni non ha un'interfaccia; le modifiche non valide vivono solo nella pagina aperta |
| Giochi Game Boy | funziona, con limiti: niente audio, immagine in bianco e nero, cartucce MBC 0/1/2/3/5, ROM al massimo 2 MB |
| Controller Xbox e iPhone come controller | funziona |
| App iPhone | in sviluppo: alcune sezioni (Firmware, registro, backup) dal telefono sono spente apposta; con l'Apple ID gratuito va reinstallata ogni 7 giorni |
| Percentuale della batteria | implementata; il confronto col multimetro sulla scheda vera è ancora da fare |
| Firmware 0.14.1 (risparmio di memoria) | le prove dal vivo sulla scheda sono ancora da annotare |
| Rapporti di crash | presenti; si basano sul coredump del core Arduino |

---

## Preparare il Mac

I comandi vanno scritti nell'app **Terminale**. Dopo ogni installazione c'è il comando per controllare che sia andata bene.

1. **Homebrew** (installa gli altri programmi):
   ```sh
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   brew --version
   ```
2. **git** (scarica il codice):
   ```sh
   brew install git
   git --version
   ```
3. **Go** (compila il servizio):
   ```sh
   brew install go
   go version          # deve dire go1.24 o più nuovo
   ```
4. **Python 3 e uv** (servono a PlatformIO, a esptool e allo strumento ROM):
   ```sh
   brew install python uv
   python3 --version
   uv --version
   ```
   Al posto di uv va bene anche pipx: `brew install pipx && pipx ensurepath`.
5. **PlatformIO Core** (compila e scrive il firmware):
   ```sh
   uv tool install platformio      # oppure: pipx install platformio
   pio --version
   ```
   Se `pio` non si trova, esegui `uv tool update-shell` e riapri il Terminale.
6. **esptool** (legge e scrive la flash della scheda). Non serve installarlo: `uvx` lo scarica la prima volta che lo usi.
   ```sh
   uvx --from "esptool>=5.5,<6" esptool version
   ```
7. **Driver USB-seriale**: di solito **non serve**, macOS riconosce già il CP2102. Collega la scheda con un cavo **dati** (alcuni cavi servono solo a caricare) e controlla:
   ```sh
   ls /dev/cu.usbserial-*
   ```
   Se con un cavo buono non compare niente, installa il driver CP210x dal sito di Silicon Labs.
8. **Xcode e XcodeGen** (solo per l'app iPhone): installa Xcode dall'App Store, aprilo una volta per accettare la licenza, poi:
   ```sh
   brew install xcodegen
   xcodebuild -version
   xcodegen --version
   ```
9. **Node** (solo per le prove della webapp e gli strumenti in `tools/`; per usare Calamaio non serve):
   ```sh
   brew install node
   node --version      # 22 o più nuovo
   ```
10. **Claude Code**: segui le istruzioni ufficiali su <https://docs.claude.com/claude-code>, poi:
    ```sh
    claude --version
    ```

Scarica il codice:

```sh
git clone https://github.com/MrGamingITA/easy-ink-os.git
cd easy-ink-os
```

---

## ⚠️ Prima di tutto: il backup della scheda

**Prima di scrivere qualsiasi cosa sulla scheda, salva una copia completa della sua memoria flash.** Se qualcosa va storto, con il backup la riporti esattamente com'era (anche al firmware di fabbrica). La lettura non cambia niente sulla scheda. A 115200 baud ci vogliono circa **12 minuti** per 8 MB.

I backup contengono la **password del Wi-Fi** e le **chiavi** salvate sulla scheda: tienili privati. La cartella `backup-fabbrica/` è esclusa da git apposta: non mettere mai un backup nel repository.

### Modo 1: strumento ROM (con finestra, il più semplice)

Doppio clic su **`tools/rom-tool/avvia.command`** (la prima volta prepara da solo quello che serve; se macOS lo blocca: tasto destro › Apri). Oppure da riga di comando, dopo il primo avvio:

```sh
cd tools/rom-tool
.venv/bin/python rom_tool.py info     # chip, MAC, flash
.venv/bin/python rom_tool.py read     # backup completo in <chip>_<mac>_<data-ora>.bin, con il file .sha256
```

Se la lettura si interrompe, riprende con `--riprendi`. Istruzioni complete in [`tools/rom-tool/README.md`](tools/rom-tool/README.md).

### Modo 2: script nel Terminale

```sh
tools/backup/backup-flash.sh                          # trova da solo la porta, se c'è una sola scheda
tools/backup/backup-flash.sh /dev/cu.usbserial-0001   # oppure indicala tu
```

Il file finisce in `backup-fabbrica/<chip>_<mac>_<data>.bin`, con accanto `….bin.sha256`, l'impronta che serve a controllare che il file sia integro (`shasum -a 256 -c file.bin.sha256`).

Per rimettere un backup sulla scheda (**cancella tutto quello che c'è**: prima di partire chiede di scrivere `SCRIVI`, e `FORZA` se il file sembra di un'altra scheda):

```sh
tools/backup/restore-flash.sh backup-fabbrica/esp32s3_<mac>_<data>.bin
```

### Modo 3: esptool a mano

```sh
PORTA=/dev/cu.usbserial-0001
# backup completo (8 MB = 0x800000)
uvx --from "esptool>=5.5,<6" esptool --port $PORTA --baud 115200 read-flash 0 0x800000 backup-completo.bin
shasum -a 256 backup-completo.bin > backup-completo.bin.sha256
# ripristino (distruttivo: sostituisce tutta la flash)
uvx --from "esptool>=5.5,<6" esptool --port $PORTA --baud 115200 write-flash 0x0 backup-completo.bin
```

C'è anche un quarto modo: la pagina **Firmware** della webapp fa backup e ripristino dal browser (solo Chrome o Edge, che hanno Web Serial).

Se la scheda non risponde: tieni premuto **PRG** mentre colleghi il cavo, poi riprova.

---

## Compilare e installare

### 1. Il servizio (sul Mac)

```sh
cd service
go test ./...                                       # le prove: devono dire tutte «ok»
go build -o ~/.local/bin/calamaio ./cmd/calamaio
calamaio version
```

Se `calamaio` non si trova, aggiungi `~/.local/bin` al PATH (`echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc`, poi riapri il Terminale).

Avvio, dalla radice del repository:

```sh
calamaio serve --firmware firmware/.pio/build/heltec213
```

`--firmware` è facoltativo: serve perché la webapp e `calamaio ota push` conoscano il firmware compilato. Poi apri <http://127.0.0.1:8765>. Per provare lo schermo senza Claude Code: `calamaio serve --fake`.

Avvio automatico al login (macOS): `calamaio install`; per toglierlo `calamaio uninstall`; per controllare `calamaio install --status`. Lo stato di tutto: `calamaio status`.

### 2. Il plugin di Claude Code

Il plugin manda al servizio gli eventi delle sessioni. Il repository contiene un piccolo «negozio» locale di plugin (`.claude-plugin/marketplace.json`, nome `calamaio-local`). Dalla radice del repository:

```sh
claude plugin validate plugin                                  # controlla il plugin
claude plugin marketplace add "$PWD"                           # registra il negozio locale
```

Poi, nella cartella del progetto in cui vuoi vedere le sessioni sul display:

```sh
claude plugin install calamaio@calamaio-local --scope local    # solo per questo progetto
```

Con `--scope user` vale per tutti i progetti. Per una prova di una sola sessione, senza installare niente: `claude --plugin-dir <repository>/plugin`.

La **status line** (consumo del contesto e limiti) va impostata nelle impostazioni di Claude Code. Lo fa `calamaio setup claude`: chiede conferma e salva prima una copia di `~/.claude/settings.json`. Si annulla con `calamaio setup claude --undo`.

### 3. Il firmware (sulla scheda)

Compilare:

```sh
cd firmware
pio run -e heltec213
pio test -e native          # le prove della logica, sul Mac
```

**Prima scrittura, con il cavo** (dopo il backup!). Scrive bootloader, tabella delle partizioni e firmware:

```sh
pio run -e heltec213 -t upload --upload-port /dev/cu.usbserial-0001
pio device monitor -b 115200 -p /dev/cu.usbserial-0001      # i messaggi della scheda (Ctrl+C per uscire)
```

La tabella delle partizioni si cambia **solo con il cavo**: l'aggiornamento senza cavo scrive solo il firmware. Se la tua scheda aveva già una versione di Calamaio con un'altra tabella, segui la procedura che lascia intatte le impostazioni, nel «Task 2: Backup e flash via cavo della tabella nuova» di `docs/superpowers/plans/2026-10-12-giochi-B-gioco.md`.

**Primo avvio**: senza reti salvate la scheda apre una sua rete Wi-Fi e mostra un QR con la password; collegati col telefono e scegli la tua rete (solo 2,4 GHz). Poi abbina la scheda al PC:

```sh
calamaio pair          # scrivi il codice di 6 cifre che mostra la scheda, poi conferma sulla scheda
```

Infine sulla scheda accendi Menu › Avvisi › **Permessi dal display** se vuoi rispondere ai permessi da lì.

**Aggiornamenti successivi, senza cavo** (la scheda deve essere abbinata e collegata):

```sh
calamaio ota keygen                                      # una volta sola: crea la chiave di firma
cd firmware && pio run -e heltec213 && cd ..
calamaio ota push --build firmware/.pio/build/heltec213
```

Senza cavo l'aggiornamento parte solo con la batteria almeno al 50%, e mai durante una richiesta di permesso. La chiave di firma resta in `~/Library/Application Support/calamaio/ota-key`: non condividerla.

### 4. L'app iPhone (facoltativa)

Serve un Apple ID gratuito. Crea il file `ios/Config/Local.xcconfig` con la **tua** squadra e un identificativo tuo:

```
DEVELOPMENT_TEAM = ABCDE12345
CALAMAIO_BUNDLE_ID = com.tuonome.calamaio
```

Il codice della squadra lo trovi in Xcode › Settings › Accounts. **Non mettere mai questo file nel repository** (è già escluso da git).

```sh
cd ios
xcodegen generate
open Calamaio.xcodeproj
```

In Xcode scegli il tuo iPhone e premi ▶︎. Con l'Apple ID gratuito l'app smette di aprirsi dopo 7 giorni: basta reinstallarla da Xcode. Per abbinarla: webapp › Dispositivo, sezione dei telefoni, poi inquadra il QR con l'app.

---

## Giochi Game Boy

**Le ROM non sono nel repository e non lo saranno mai.** Usa la ROM di un gioco che possiedi legalmente (o un gioco homebrew con licenza libera). La ROM si scrive con il cavo nella partizione `gbrom` (al massimo 2 MB):

```sh
uvx --from "esptool>=5.5,<6" esptool --port /dev/cu.usbserial-0001 --baud 115200 write-flash 0x490000 il-mio-gioco.gb
```

Poi sulla scheda: Menu › Gioco › ROM. Serve un controller collegato (Xbox via Bluetooth, oppure l'iPhone dalla scheda «Controller» dell'app). I salvataggi del gioco restano sulla scheda.

---

## Sicurezza: cosa resta dove

- **Tutto resta in casa**: scheda, servizio e app parlano fra loro solo nella rete locale.
- **Dati del servizio** in `~/Library/Application Support/calamaio`: configurazione, abbinamenti, avatar, temi, gioco, telefoni abbinati, registri, rapporti di crash, chiave OTA. Si possono spostare con le variabili `CALAMAIO_CONFIG_DIR` e `CALAMAIO_DATA_DIR`.
- **Webapp** solo su `127.0.0.1` (il tuo Mac); i telefoni abbinati passano da una porta a parte (8767, HTTPS) con un elenco chiuso di funzioni.
- **Permessi**: le regole scritte sopra («Cosa fa») sono fisse nel codice; dettagli in `docs/ISTRUZIONI-SVILUPPO.md` §7 e `docs/M5-PERMESSI.md`.
- **Chiavi sulla scheda**: cifrate; con «Proteggi le chiavi» la chiave madre vive in una eFuse illeggibile (irreversibile, vedi sopra).
- **Backup della flash**: contengono password del Wi-Fi e chiavi. Privati, mai nel repository.
- **`~/.claude/settings.json`** si cambia solo con `calamaio setup claude`, che chiede conferma e fa prima una copia.

---

## Mappa del repository

| Cartella | Contenuto |
|---|---|
| `service/` | servizio per il Mac (Go): `calamaio serve`, webapp, hook, status line, OTA |
| `plugin/` | plugin di Claude Code (hook e skill) |
| `firmware/` | firmware per la scheda (PlatformIO) |
| `ios/` | app iPhone (SwiftUI, progetto generato con XcodeGen) |
| `protocol/` | schemi dei messaggi, esempi, vettori di prova, temi di esempio |
| `avatars/` | avatar e accessori predefiniti |
| `tools/` | strumento ROM, backup, avatar, caratteri, prove della webapp e altre utilità |
| `reference/` | simulatore nel browser e progetto di design (sola lettura) |
| `spikes/` | piccole prove tecniche |
| `docs/` | documentazione |

## Documentazione

- [`docs/ISTRUZIONI-SVILUPPO.md`](docs/ISTRUZIONI-SVILUPPO.md): il progetto completo, le milestone, le regole dei permessi (§7);
- [`docs/DECISIONI.md`](docs/DECISIONI.md): tutte le scelte prese, con data e motivo;
- [`docs/HARDWARE.md`](docs/HARDWARE.md): dati verificati della scheda;
- [`docs/PROTOCOLLO.md`](docs/PROTOCOLLO.md): come parlano scheda, servizio e app;
- [`docs/OTA.md`](docs/OTA.md): aggiornamento senza cavo, registro da remoto, crash;
- [`docs/M5-PERMESSI.md`](docs/M5-PERMESSI.md): abbinamento e permessi;
- [`docs/AVATAR.md`](docs/AVATAR.md) e [`docs/TEMI.md`](docs/TEMI.md): formati di avatar e temi;
- [`tools/rom-tool/README.md`](tools/rom-tool/README.md): strumento ROM.

## Licenza

© 2026 [MrGamingITA](https://github.com/MrGamingITA). Licenza **[CC BY-NC-SA 4.0](LICENSE)** (Creative Commons Attribuzione, Non commerciale, Condividi allo stesso modo):

- puoi usare, copiare e modificare il progetto, e ripubblicarlo, **gratis**;
- nelle versioni ripubblicate, intere o modificate, va citato **MrGamingITA** come autore originale, con il link al profilo, e va detto cosa è cambiato;
- le versioni modificate vanno pubblicate **con la stessa licenza**, quindi restano aperte e gratuite;
- **niente uso commerciale** (vendere il progetto o prodotti che lo contengono) senza l'autorizzazione scritta dell'autore: per chiederla, contattalo su [GitHub](https://github.com/MrGamingITA).

I componenti di terzi inclusi (Peanut-GB, QRCode, TweetNaCl, esptool-js, caratteri) restano con le loro licenze: l'elenco è in [`NOTICE`](NOTICE).
