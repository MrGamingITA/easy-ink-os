# Giochi Game Boy — piano B «Il gioco sulla scheda» Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** far girare i giochi del Game Boy classico sulla Heltec Wireless Paper V1.2: nuova tabella delle partizioni (una volta via cavo), emulatore Peanut-GB con la ROM letta dalla flash, immagine ridotta e retinata sul pannello veloce, menu Giochi e pausa, regole della partita, e il lato scheda dei messaggi `gb-*` (ROM con ripresa e SHA-256, salvataggi verso il PC).

**Architecture:** tutta la logica nuova è pura in `firmware/src/core/gb_*` (provata in native con Unity): emulatore (`gb_emu`, Peanut-GB incluso in un solo file), riduzione e retinatura (`gb_frame`), triplo buffer e ritmo (`gb_handoff`), tasti e regole della partita (`gb_input`, `gb_session`), protocollo (`gb_proto`), installazione della ROM (`gb_rom`), salvataggi (`gb_save`), interfaccia (`gb_ui.cpp` dentro `cal::Ui`, `ui/gb_screens`). La parte che tocca la scheda sta in `firmware/src/hw/gb_hw` (partizione, NVS, LittleFS, HTTP), `hw/gb_run` (emulatore sul core 1 nel task del loop, pannello veloce in un task sul core 0) e `hw/gb_main` (il pezzo di `loop()`); `main.cpp` e `net/net.cpp` ricevono solo agganci corti. Il driver veloce e il controller arrivano dal piano A con i nomi del contratto.

**Tech Stack:** PlatformIO, espressif32@6.9.0 (core Arduino 2.0.17, IDF 4.4), C++ (gnu++11 sulla scheda, c++17 in native), Unity, ArduinoJson 7.4.3, Peanut-GB (MIT, commit `d0bcca771c83a2638c93a9ae61f3d51f226dc905`), LittleFS, `esp_partition_*`, HTTPClient.

**Spec:** `docs/superpowers/specs/2026-10-12-giochi-gameboy-design.md`. **Contratto vincolante:** `docs/superpowers/plans/2026-10-12-giochi-contratti.md` (nomi verso i piani A e C; versione del 2026-10-12, che accoglie le proposte di questo piano su mac obbligatori, token del salvataggio scelto dalla scheda e OTA rimandato). Prove dal vivo: `spikes/fastink/README.md`, `spikes/blepad/README.md`.

## Prerequisiti e ordine

- **Piano A** (fondamenta sulla scheda) va eseguito prima dei Task 6, 7 e 11–16: fornisce `firmware/src/core/pad.h` (`cal::pad::State`, `Button`, `Link`), `firmware/src/hw/blepad.{h,cpp}` (`cal::blepad::link`, `poll`), `firmware/src/hw/fastink.{h,cpp}` (`cal::fastink::begin/submit/tick/settled/cleanFull/end`, `Profile::Clean/Smooth`), NimBLE nel `platformio.ini`, Menu › Gioco (`MenuId::Gb`, sempre presente; dentro `MenuId::GbPad` «Controller» e `MenuId::GbRoms` «ROM» sulla scheda vera, e «Alto o basso»), l'aggancio `cal::gbGameItems` (`core/gb_menu.{h,cpp}`, vuoto) e `Ui::gbPlaying()` (corpo `return false;`), la mappatura del controller sulle azioni dei menu (`Up/Down/A/B` → `Action`) sospesa mentre `gbPlaying()` è vero, `pad::linkKey`. I Task 1–5 e 8–10 non dipendono dal piano A.
- **Piano C** (servizio) serve per i Task 15–16 dal vivo: `gb-list`, `gb-start`, `GET /gb/rom/<token>` con `Range`, `PUT`/`GET /gb/save/<token>`, `gb-save-ok`, webapp «Giochi».
- Il codice dei Task 1–14 è stato provato il 2026-10-12 in una copia di lavoro del firmware 0.10.2 (fuori dal repository): 658 test native verdi (i 9 file `test_gb_*` più tutti quelli esistenti) e `pio run -e heltec213` riuscito, con segnaposto vuoti al posto di `hw/fastink.*`, `hw/blepad.*`, `core/pad.h` e del blocco `MenuId::Gb` del piano A. Le impronte dorate (dmg-acid2, riduzione) e il vettore del mac sono stati calcolati con implementazioni indipendenti (Python).
- Un altro agente sta chiudendo la 0.10.2 su `main.cpp` e `core/ui.*`: gli agganci qui sotto citano il codice per contenuto (righe vicine), non per numero di riga. Prima di ogni Task che tocca quei file, `git pull`/`git log -3` e rileggere il punto citato.

## Global Constraints

- Testi a schermo in italiano; identificatori in inglese; commenti brevi in italiano (CLAUDE.md).
- Prefisso **`gb`** ovunque: file `gb_*`, schermate `ScreenId::Gb*`, messaggi `gb-*`, NVS `calamaio-gb`, cartella LittleFS `/gb`; a schermo «Giochi» (contratto, «Nomi»).
- Nomi del contratto usati tali e quali: `cal::fastink::{Profile::Clean, Profile::Smooth, begin, submit, tick, settled, cleanFull, end}`, `cal::pad::{Button, State, parseXboxReport, linkKey, Link::{Off, Pairing, Connecting, Ready}}`, `cal::blepad::{begin, startPairing, forget, link, poll}`, `MenuId::{Gb, GbPad, GbRoms}`, `cal::gbGameItems`, `Ui::gbPlaying`. `blepad::poll` dà un report cambiato alla volta e durante la partita lo legge solo questo piano. `fastink::begin` si richiama a ogni ripresa; `submit` non applica Inverti né Ruota (li applica `gbrun` al fotogramma); `tick` cede il core 0 da sé.
- C++ della scheda `-std=gnu++11` (contratto, «Regole comuni»): niente C++14/17 nemmeno nei test.
- **Driver veloce solo durante la partita** (contratto): caricamento, pausa, Permesso e ogni altra schermata restano su heltec-eink-modules con la regola dei refresh della 0.10.2. Fotogramma per `submit`: 250×122, 1 bit per pixel, righe di 32 byte, MSB = x più a sinistra, 1 = nero.
- Tabella `firmware/partitions_gb.csv` esattamente: nvs 0x9000/0x5000, otadata 0xe000/0x2000, app0 0x10000/0x240000, app1 0x250000/0x240000, gbrom data 0x40 0x490000/0x200000, spiffs 0x690000/0x160000, coredump 0x7f0000/0x10000. NVS invariata; LittleFS si riformatta (cache).
- Solo Game Boy classico, niente audio. ROM ≤ 2 MiB (partizione `gbrom`); RAM della cartuccia ≤ 32 KB, oltre «Gioco non supportato». MBC come Peanut-GB (0, 1, 2, 3, 5).
- Mai avviare una ROM incompleta o non verificata: SHA-256 della partizione uguale a `gb-start.sha256` a ogni avvio.
- Messaggi `gb-*` accettati solo da una connessione autenticata e, quelli del servizio, solo col `mac` giusto (obbligatorio: contratto, «B ↔ C»). HTTP sulla porta dei display 8766 (indirizzo e porta della connessione WebSocket), mai 8765.
- Regole della partita (spec §4): controller scollegato → pausa subito; 5 min senza tasti → pausa; 10 min in pausa → salva, esce, Riposo; batteria < 15 % → avviso nella banda; < 5 % → salva ed esce; OTA rifiutato durante la partita (il servizio riprova dopo `gb-state {playing: null}`); ridisegno completo all'apertura della pausa e all'uscita (il completo di `fastink::end`, un lampo solo) e `fastink::cleanFull()` periodico in gioco ogni 10 minuti (NVS `calamaio-gb/clean`, valore da tarare nel Task 15).
- Permessi (ISTRUZIONI-SVILUPPO §7, non negoziabili): NEGA preselezionato, mai una pressione singola che approva; una richiesta in gioco mette in pausa e apre la schermata Permesso normale.
- Diritti: nessuna ROM commerciale nel repository né scaricata; `dmg-acid2.gb` (MIT) entra nel repository con la licenza; `cpu_instrs.gb` di Blargg (senza licenza esplicita) si scarica fuori dal repository con impronta fissa.
- Flash, cancellazioni di regioni e scritture via cavo: backup e conferma dell'utente prima (CLAUDE.md); 115200 baud (CP2102); porta `/dev/cu.usbserial-0001`.
- Test: `cd firmware && pio test -e native` (tutti verdi a ogni commit) e `pio run -e heltec213`. Commit in italiano, **senza** riga `Co-Authored-By`.

## Review Focus

1. **Emulatore troppo lento sulla scheda** (CPU a 240 MHz, ROM letta dalla flash tramite la cache dati da 32 KB): il gioco deve restare a 59,7 Hz o rallentare in modo uniforme, mai correre a scatti per recuperare. Coperto da `Pacer` (Task 5: `test_pacer_long_stall_resyncs`) e misurato dal vivo nel Task 15 (riga `[gb] pannello: … fotogrammi emulati persi`, soglia < 5 %).
2. **Heap esaurito** con NimBLE + Wi-Fi + driver veloce + emulatore + cartuccia da 32 KB (Pokémon Oro): ci si aspetta «Memoria insufficiente» e il menu, non un riavvio. Controllo in `gbrun::begin` (Task 12) prima di ogni allocazione grande; misura dal vivo con la ROM più grande nel Task 15.
3. **Corrente o reset a metà download o a metà salvataggio**: mai un gioco «pronto» sbagliato, mai un `.sav` a metà. Coperto da `test_power_loss_resumes_from_saved_progress`, `test_wrong_bytes_never_ready`, `test_installed_but_corrupted_is_never_ready` (Task 8) e dalla scrittura con file temporaneo e rinomina (Task 12); passo dal vivo nel Task 16 (reset durante il download).
4. **ROM strane** (intestazione che promette più banchi del file, codice 0x148 vecchio, MBC sconosciuto, codice non valido): «Gioco non supportato» o «Il gioco si è fermato», mai letture fuori dal buffer. Coperto da `test_truncated_rom_reads_ff`, `test_too_small_and_old_rom_size_code`, `test_unsupported_mbc`, `test_invalid_opcode_stops` (Task 3).
5. **`gb-start` di un altro gioco a partita aperta** (dalla webapp): la partita si salva e si chiude prima che la rete scriva nella partizione mappata. Coperto dal flusso `GbStart` → `exitGame(Switch)` → `NetCmd::GbGo` (Task 13–14), dal rifiuto di `PartFlash::eraseWrite` con la ROM mappata (Task 12) e dal passo dal vivo «cambio gioco» (Task 16).

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `firmware/partitions_gb.csv`, `firmware/platformio.ini` | tabella delle partizioni e build | 1 |
| `firmware/test/test_gb_part/test_main.cpp` | tabella contro il contratto | 1 |
| `firmware/lib/peanut_gb/peanut_gb.h` | Peanut-GB (MIT), copia fissa | 3 |
| `firmware/src/core/gb_emu.{h,cpp}` | emulatore dietro `cal::gb::Emu` | 3 |
| `firmware/test/roms/dmg-acid2.gb`, `LICENSE-dmg-acid2.txt`; `tools/gb/fetch-test-roms.sh` | ROM di prova | 3 |
| `firmware/src/core/gb_frame.{h,cpp}` | 160×144 a 4 tonalità → 136×122 bianco e nero centrato | 4 |
| `firmware/src/core/gb_handoff.{h,cpp}` | triplo buffer fra i core e ritmo a 59,7 Hz | 5 |
| `firmware/src/core/gb_model.h` | parte del modello per l'interfaccia (`GbView`, enum) | 6 |
| `firmware/src/core/gb_input.{h,cpp}`, `gb_session.{h,cpp}` | tasti dal controller, regole della partita | 6 |
| `firmware/src/core/gb_proto.{h,cpp}`, `core/apply_state.*` | messaggi `gb-*` | 7 |
| `firmware/src/core/gb_rom.{h,cpp}` | installazione della ROM (Range, SHA-256, registro) | 8 |
| `firmware/src/core/gb_save.{h,cpp}` | salvataggi verso il PC | 9 |
| `firmware/src/core/ota.{h,cpp}` | OTA rimandato durante la partita | 10 |
| `firmware/src/core/{model,menu,ui}.*`, `core/gb_menu.cpp`, `core/gb_ui.cpp`, `ui/gb_screens.*`, `ui/screens.*` | Menu › Gioco › ROM, caricamento, partita, pausa | 11 |
| `firmware/src/hw/gb_hw.{h,cpp}`, `hw/gb_run.{h,cpp}` | partizione, NVS, LittleFS, HTTP; emulatore e pannello sui due core | 12 |
| `firmware/src/net/net.{h,cpp}` | `gb-*` sul WebSocket, installazione e salvataggi nel task di rete | 13 |
| `firmware/src/hw/gb_main.{h,cpp}`, `firmware/src/main.cpp` | il pezzo di `loop()` e gli agganci | 14 |
| `docs/DECISIONI.md` | scelte e deviazioni | 1, 3, 4, 11, 14, 15 |

---

### Task 1: Tabella delle partizioni e build

**Files:**
- Create: `firmware/partitions_gb.csv`
- Modify: `firmware/platformio.ini` (sezione `[env:heltec213]`)
- Modify: `firmware/src/hw/ota_hw.h` (commento di `maxImageSize`)
- Test: `firmware/test/test_gb_part/test_main.cpp`
- Modify: `docs/DECISIONI.md`

**Interfaces:**
- Consumes: niente.
- Produces: partizione dati `gbrom` (sottotipo 0x40, 0x490000, 2 MiB) che i Task 12–13 cercano con `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "gbrom")`; app da 0x240000 (`otahw::maxImageSize()` la legge dalla partizione: nessun codice da cambiare).

Effetti da sapere (non c'è codice da scrivere per questi):
- **NVS** resta a 0x9000 da 0x5000 byte: chiavi, abbinamento, reti, impostazioni restano dove sono (il test lo blocca).
- **LittleFS**: la partizione `spiffs` passa da 0x670000/0x180000 a 0x690000/0x160000. `avfs::begin()` monta con `LittleFS.begin(true, "/littlefs", 4, "spiffs")` (`formatOnFail = true`): sulla posizione nuova non trova un superblocco e formatta. Avatar, accessori, ancore e riassunto del gioco della mascotte sono una cache: tornano dal servizio con `avatar-need`/`accessory-need`/`anchors-need` e col messaggio `game`. Nel Task 2 la regione si cancella comunque via cavo, così il montaggio non vede mai resti della tabella vecchia.
- **OTA**: l'immagine massima scende da 3 342 336 a 2 359 296 byte (`otahw::maxImageSize()` = dimensione della partizione libera). Oggi il firmware è 1,68 MB; con emulatore (+50 KB, misurato) e NimBLE (+~200 KB, prova blepad) resta sotto 2,0 MB. `pio run` controlla da solo la dimensione contro la tabella.
- **Rollback**: dopo il flash via cavo app1 conterrebbe resti della tabella vecchia; il Task 2 ci scrive la stessa immagine di app0, così un rollback ha sempre un'app valida.
- Una scheda che riceve un firmware con questa tabella **via OTA** senza il flash via cavo continua a girare con la tabella vecchia (la tabella sta a 0x8000, l'OTA scrive solo l'app): non trova `gbrom` e Menu › Gioco › ROM dice «Giochi non disponibili» (Task 11–12).

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_part/test_main.cpp`:

```cpp
// Tabella delle partizioni dei giochi (firmware/partitions_gb.csv) contro il contratto dei piani giochi e contro la
// tabella di oggi (default_8MB.csv del core 2.0.17): NVS e coredump identici, niente sovrapposizioni, allineamenti.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

void setUp() {}
void tearDown() {}

namespace {

struct Row {
  std::string name, type, subtype;
  uint32_t offset, size;
};

std::string trim(const std::string &s) {
  const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<Row> readCsv(const char *path) {
  std::vector<Row> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> c;
    size_t at = 0;
    for (;;) {
      const size_t k = line.find(',', at);
      c.push_back(trim(line.substr(at, k == std::string::npos ? std::string::npos : k - at)));
      if (k == std::string::npos) break;
      at = k + 1;
    }
    TEST_ASSERT_TRUE_MESSAGE(c.size() >= 5, line.c_str());
    out.push_back(Row{c[0], c[1], c[2], uint32_t(strtoul(c[3].c_str(), nullptr, 0)), uint32_t(strtoul(c[4].c_str(), nullptr, 0))});
  }
  return out;
}

const Row *find(const std::vector<Row> &t, const char *name) {
  for (const Row &r : t)
    if (r.name == name) return &r;
  return nullptr;
}

}  // namespace

void test_matches_contract() {
  const std::vector<Row> t = readCsv("partitions_gb.csv");
  const Row want[] = {
      {"nvs", "data", "nvs", 0x9000, 0x5000},          {"otadata", "data", "ota", 0xe000, 0x2000},
      {"app0", "app", "ota_0", 0x10000, 0x240000},     {"app1", "app", "ota_1", 0x250000, 0x240000},
      {"gbrom", "data", "0x40", 0x490000, 0x200000},   {"spiffs", "data", "spiffs", 0x690000, 0x160000},
      {"coredump", "data", "coredump", 0x7f0000, 0x10000},
  };
  TEST_ASSERT_EQUAL(7, int(t.size()));
  for (size_t i = 0; i < 7; i++) {
    TEST_ASSERT_EQUAL_STRING(want[i].name.c_str(), t[i].name.c_str());
    TEST_ASSERT_EQUAL_STRING(want[i].type.c_str(), t[i].type.c_str());
    TEST_ASSERT_EQUAL_STRING(want[i].subtype.c_str(), t[i].subtype.c_str());
    TEST_ASSERT_EQUAL_HEX32(want[i].offset, t[i].offset);
    TEST_ASSERT_EQUAL_HEX32(want[i].size, t[i].size);
  }
}

void test_layout_is_sound() {
  const std::vector<Row> t = readCsv("partitions_gb.csv");
  for (size_t i = 0; i + 1 < t.size(); i++)
    TEST_ASSERT_TRUE_MESSAGE(t[i].offset + t[i].size <= t[i + 1].offset, t[i].name.c_str());  // niente sovrapposizioni
  const Row &last = t.back();
  TEST_ASSERT_TRUE(last.offset + last.size <= 0x800000);  // 8 MB
  for (const Row &r : t) {
    TEST_ASSERT_EQUAL_MESSAGE(0, r.offset % 0x1000, r.name.c_str());  // settori da 4 KB
    if (r.type == "app") TEST_ASSERT_EQUAL_MESSAGE(0, r.offset % 0x10000, r.name.c_str());  // app a 64 KB
  }
  TEST_ASSERT_EQUAL(0, int(find(t, "gbrom")->offset % 0x10000));  // mappata a pagine MMU da 64 KB
}

// NVS e coredump come in default_8MB.csv: chiavi, abbinamento, reti e rapporti di crash restano dove sono.
void test_nvs_and_coredump_unchanged() {
  const std::vector<Row> t = readCsv("partitions_gb.csv");
  TEST_ASSERT_EQUAL_HEX32(0x9000, find(t, "nvs")->offset);
  TEST_ASSERT_EQUAL_HEX32(0x5000, find(t, "nvs")->size);
  TEST_ASSERT_EQUAL_HEX32(0xe000, find(t, "otadata")->offset);
  TEST_ASSERT_EQUAL_HEX32(0x7f0000, find(t, "coredump")->offset);
  TEST_ASSERT_EQUAL_HEX32(0x10000, find(t, "coredump")->size);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_matches_contract);
  RUN_TEST(test_layout_is_sound);
  RUN_TEST(test_nvs_and_coredump_unchanged);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_part`
Expected: FAIL, `test_matches_contract: Expected 7 Was 0` (il file `partitions_gb.csv` non esiste ancora).

- [ ] **Step 3: Write the table and the build option**

Create `firmware/partitions_gb.csv`:

```csv
# Calamaio OS · tabella con i giochi Game Boy (docs/superpowers/plans/2026-10-12-giochi-contratti.md).
# NVS identica a default_8MB.csv (chiavi, abbinamento, Wi-Fi intatti). spiffs = LittleFS (cache: si riformatta).
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  ota_0,    0x10000,  0x240000,
app1,     app,  ota_1,    0x250000, 0x240000,
gbrom,    data, 0x40,     0x490000, 0x200000,
spiffs,   data, spiffs,   0x690000, 0x160000,
coredump, data, coredump, 0x7f0000, 0x10000,
```

In `firmware/platformio.ini`, nella sezione `[env:heltec213]`, subito dopo `framework = arduino`:

```ini
; Giochi Game Boy (piano B): app 2,25 MB ciascuna, «gbrom» 2 MB. NVS invariata. Si installa una volta via cavo.
board_build.partitions = partitions_gb.csv
```

In `firmware/src/hw/ota_hw.h` sostituire il commento sopra `uint32_t maxImageSize();`:

```cpp
// Partizione libera (app0/app1 da 2 359 296 byte con partitions_gb.csv, 3 342 336 con la tabella vecchia
// default_8MB.csv): dimensione massima dell'immagine, letta dalla partizione.
uint32_t maxImageSize();
```

- [ ] **Step 4: Run tests and build**

Run: `cd firmware && pio test -e native -f test_gb_part && pio run -e heltec213`
Expected: 3 test PASSED; build SUCCESS con `Flash: … from 2359296 bytes` (non più 3342336). Annotare la riga `Flash:`.

Run: `python3 ~/.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py firmware/.pio/build/heltec213/partitions.bin`
Expected: le sette righe della tabella, con `gbrom,data,64,0x490000,2M`.

- [ ] **Step 5: DECISIONI**

Aggiungere in fondo a `docs/DECISIONI.md`:

```markdown
- **Giochi, tabella delle partizioni** (piano B): `firmware/partitions_gb.csv` come da contratto (app 2 × 0x240000, `gbrom` 0x490000 da 2 MiB, sottotipo dati 0x40, spiffs 0x690000 da 0x160000). NVS e coredump invariati. LittleFS si riformatta al primo avvio (cache: avatar, accessori e ancore tornano dal servizio). Immagine OTA massima 2 359 296 byte. Si installa una volta via cavo (piano B, Task 2), poi gli aggiornamenti tornano via OTA. Una scheda aggiornata via OTA senza il flash via cavo resta con la tabella vecchia e i giochi spenti.
```

- [ ] **Step 6: Commit**

```bash
git add firmware/partitions_gb.csv firmware/platformio.ini firmware/src/hw/ota_hw.h firmware/test/test_gb_part docs/DECISIONI.md
git commit -m "Giochi: tabella delle partizioni con gbrom"
```

---

### Task 2: Backup e flash via cavo della tabella nuova (dal vivo, con l'utente)

**Files:** nessun file del repository. Backup in `backup-fabbrica/` (ignorata da git).

**Interfaces:**
- Consumes: build del Task 1 (`firmware/.pio/build/heltec213/{bootloader,partitions,firmware}.bin`).
- Produces: scheda con la tabella nuova, NVS intatta, abbinamento funzionante, app1 valida per il rollback.

> Il coordinatore **chiede prima all'utente**: «Faccio un backup completo della flash (circa 12 minuti a 115200 baud) e poi scrivo via cavo la tabella nuova delle partizioni, il firmware in app0 e app1 e cancello la cache LittleFS. NVS (chiavi, abbinamento, Wi-Fi) non viene toccata. Procedo?». Senza un sì esplicito ci si ferma qui.

- [ ] **Step 1: Preparazione**

Scheda collegata col cavo USB-C, nessun `pio device monitor` aperto. Variabili per i comandi qui sotto:

```bash
cd ~/CalamaiOS
B=firmware/.pio/build/heltec213
BK=backup-fabbrica/pre-giochi-2026-10-12.bin
ESP="pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200"
APP0=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
```

- [ ] **Step 2: Backup completo e impronta**

```bash
$ESP read_flash 0 0x800000 "$BK"
shasum -a 256 "$BK" | tee -a backup-fabbrica/SHA256.txt
dd if="$BK" of=/tmp/nvs-prima.bin bs=4096 skip=9 count=5   # NVS: 0x9000, 0x5000 byte
```
Expected: file di 8 388 608 byte; riga aggiunta a `SHA256.txt`.

- [ ] **Step 3: Bootloader invariato?**

```bash
cmp -n "$(stat -f%z $B/bootloader.bin)" "$B/bootloader.bin" "$BK" && echo "bootloader uguale: non si scrive"
```
Expected: «bootloader uguale: non si scrive» (stessa piattaforma 6.9.0, tabella sempre a 0x8000). Se è diverso: fermarsi e chiedere al coordinatore (il bootloader non si riscrive senza una ragione).

- [ ] **Step 4: Scrittura (senza riavvio) e cancellazione della cache LittleFS**

```bash
$ESP --before default_reset --after no_reset write_flash \
  0x8000 $B/partitions.bin \
  0xe000 "$APP0" \
  0x10000 $B/firmware.bin \
  0x250000 $B/firmware.bin
$ESP --before default_reset --after no_reset erase_region 0x690000 0x160000
```
Expected: `Hash of data verified.` quattro volte; `Erase completed successfully`.
- `0x8000`: tabella nuova. `0xe000`: `boot_app0.bin`, così parte app0 anche se l'ultimo OTA aveva scelto app1. `0x10000` e `0x250000`: la stessa immagine in app0 e app1 (app1 nella tabella nuova copre resti della vecchia app0/app1: senza questa scrittura un rollback troverebbe un'app rotta).
- NVS (0x9000–0xDFFF) e coredump (0x7F0000) non si toccano. `gbrom` resta con i resti della vecchia app1: nessun registro nella NVS dice che c'è un gioco, quindi non si avvia niente.

- [ ] **Step 5: NVS identica al backup, poi riavvio**

```bash
$ESP --before default_reset --after no_reset read_flash 0x9000 0x5000 /tmp/nvs-dopo.bin
cmp /tmp/nvs-prima.bin /tmp/nvs-dopo.bin && echo "NVS intatta"
$ESP --before default_reset --after hard_reset read_mac
```
Expected: «NVS intatta» (letta prima del primo avvio, che la cambierebbe). Se `cmp` trova differenze: **non riavviare**, riscrivere il backup con `$ESP write_flash 0 "$BK"` e fermarsi.

- [ ] **Step 6: Verifica dal vivo**

```bash
pio device monitor -b 115200 -p /dev/cu.usbserial-0001
```
Expected nel registro, in quest'ordine o quasi:
- `[avatar] LittleFS: …/1441792 byte usati` (1 441 792 = 0x160000: la partizione nuova, formattata);
- `[wifi] …` collegato alla rete salvata, senza portale (il vault nella NVS c'è);
- `[m5] servizio autenticato (auth-ok)` **senza** un nuovo abbinamento;
- `avatar-need` e poi gli avatar che tornano; Home con le sessioni.
Poi `calamaio status` sul PC: la scheda risulta abbinata e autenticata. Se l'abbinamento non regge: riscrivere il backup (`$ESP write_flash 0 "$BK"`) e fermarsi.

- [ ] **Step 7: Annotare**

Nel `docs/DECISIONI.md`, sotto la voce del Task 1, aggiungere data e ora del flash, l'impronta del backup e l'esito («NVS intatta, auth-ok senza riabbinare, LittleFS riformattata»). Commit:

```bash
git add docs/DECISIONI.md
git commit -m "Giochi: tabella nuova installata via cavo"
```

---
### Task 3: Emulatore (Peanut-GB) e ROM di prova

**Files:**
- Create: `firmware/lib/peanut_gb/peanut_gb.h` (copia fissa, non modificata)
- Create: `firmware/src/core/gb_emu.h`, `firmware/src/core/gb_emu.cpp`
- Create: `firmware/test/roms/dmg-acid2.gb`, `firmware/test/roms/LICENSE-dmg-acid2.txt`
- Create: `tools/gb/fetch-test-roms.sh`
- Modify: `.gitignore`
- Test: `firmware/test/test_gb_emu/test_main.cpp`
- Modify: `docs/DECISIONI.md`

**Interfaces:**
- Consumes: niente.
- Produces (`namespace cal::gb`, `core/gb_emu.h`):
  - costanti `kLcdW = 160`, `kLcdH = 144`, `kFrameStride = 40`, `kFrameBytes = 5760`, `kCartRamMax = 0x8000`, `kRomMin = 0x150`;
  - `enum Key : uint8_t { KeyA = 0x01, KeyB = 0x02, KeySelect = 0x04, KeyStart = 0x08, KeyRight = 0x10, KeyLeft = 0x20, KeyUp = 0x40, KeyDown = 0x80 }` (bit a 1 = premuto);
  - `enum class Load : uint8_t { Ok, TooSmall, BadChecksum, Unsupported, RamTooBig }`, `const char *loadKey(Load)`;
  - `inline uint8_t shadeAt(const uint8_t *frame, int x, int y)` (0 bianco … 3 nero);
  - `class Emu { Load load(const uint8_t *rom, uint32_t romSize); bool loaded() const; size_t ramSize() const; void attachRam(uint8_t *ram); void setRtc(const struct tm &); void frameSkip(bool); bool runFrame(uint8_t keys); const uint8_t *frame() const; bool ramDirty() const; void clearDirty(); void title(char out[17]) const; void onSerial(void (*fn)(uint8_t)); void unload(); uint8_t romRead(uint32_t) const; … }`.

Scelte (da annotare in DECISIONI allo Step 7):
- **Peanut-GB** di Mahyar Koshkouei, licenza MIT (in testa al file), un solo header C che compila anche come C++ (provato con clang e con xtensa-esp32s3-elf-gcc 8.4). Commit fisso `d0bcca771c83a2638c93a9ae61f3d51f226dc905` (2026-09-21), SHA-256 del file `cf7777068492474d084f7dca0b00724d4dcdeab7f9a6c91a5d3a82e60d1cf728`. Configurazione in `gb_emu.cpp`: `ENABLE_SOUND 0`, `PEANUT_GB_12_COLOUR 0`. `sizeof(struct gb_s)` = 16 952 byte (sullo heap).
- `gb_error` di Peanut-GB **non deve tornare** (dopo c'è `__builtin_unreachable()`): `Emu::fault()` salta fuori da `gb_run_frame` con `longjmp`; `runFrame` restituisce false e l'emulatore resta fermo.
- Peanut-GB legge `num_rom_banks_mask[rom[0x148]]` da una tabella di 9 voci: i codici vecchi 0x52–0x54 la sforerebbero, `load` li rifiuta prima (`Unsupported`).
- ROM di prova: **dmg-acid2** (Matt Currie, MIT) nel repository con la sua licenza: al fotogramma 60 lo schermo di Peanut-GB è identico pixel per pixel a `reference-dmg.png` (verificato il 2026-10-12), impronta FNV-1a 32 delle tonalità `0xb312e6f6`. **cpu_instrs** di Blargg (nessuna licenza dichiarata nel repository `retrio/gb-test-roms`): non entra nel repository; la scarica `tools/gb/fetch-test-roms.sh` in `firmware/test/roms/.fetched/` (ignorata da git) con impronta fissa, e il test la salta se manca. Con Peanut-GB `d0bcca7` scrive «Passed all tests» sulla seriale al fotogramma 3173.

- [ ] **Step 1: Vendor Peanut-GB, ROM di prova e script**

```bash
cd ~/CalamaiOS
mkdir -p firmware/lib/peanut_gb firmware/test/roms tools/gb
curl -fsSL -o firmware/lib/peanut_gb/peanut_gb.h \
  https://raw.githubusercontent.com/deltabeard/Peanut-GB/d0bcca771c83a2638c93a9ae61f3d51f226dc905/peanut_gb.h
echo "cf7777068492474d084f7dca0b00724d4dcdeab7f9a6c91a5d3a82e60d1cf728  firmware/lib/peanut_gb/peanut_gb.h" | shasum -a 256 -c -
curl -fsSL -o firmware/test/roms/dmg-acid2.gb https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb
echo "464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1  firmware/test/roms/dmg-acid2.gb" | shasum -a 256 -c -
curl -fsSL -o firmware/test/roms/LICENSE-dmg-acid2.txt \
  https://raw.githubusercontent.com/mattcurrie/dmg-acid2/8a98ce731f96dde032ffb22ec36dc985d78fdb18/LICENSE
head -3 firmware/test/roms/LICENSE-dmg-acid2.txt   # «MIT License … Copyright (c) 2020 Matt Currie»
```
Expected: due righe `OK` da `shasum`.

Create `tools/gb/fetch-test-roms.sh` (poi `chmod +x`):

```sh
#!/bin/sh
# ROM di prova senza licenza esplicita: non vanno nel repository. Le scarica in firmware/test/roms/.fetched/
# con impronta fissa; test_gb_emu le usa se ci sono e le salta se mancano.
set -eu
DIR="$(cd "$(dirname "$0")/../.." && pwd)/firmware/test/roms/.fetched"
mkdir -p "$DIR"
URL=https://raw.githubusercontent.com/retrio/gb-test-roms/c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15/cpu_instrs/cpu_instrs.gb
SHA=8c5e12f41e0ba5bbca796944f92ffe6de28809198682c4332e38d1b3cf56fcf2
curl -fsSL -o "$DIR/cpu_instrs.gb.part" "$URL"
echo "$SHA  $DIR/cpu_instrs.gb.part" | shasum -a 256 -c -
mv "$DIR/cpu_instrs.gb.part" "$DIR/cpu_instrs.gb"
echo "cpu_instrs.gb in $DIR"
```

Aggiungere a `.gitignore`, sotto `# Build`:

```gitignore
firmware/test/roms/.fetched/
```

Run: `sh tools/gb/fetch-test-roms.sh`
Expected: `cpu_instrs.gb.part: OK` e il percorso.

- [ ] **Step 2: Write the failing test**

Create `firmware/test/test_gb_emu/test_main.cpp`:

```cpp
// Emulatore (core/gb_emu): ROM costruite qui (intestazione, MBC, RAM della cartuccia, schermo noto), dmg-acid2
// (test/roms, MIT) contro l'immagine di riferimento, cpu_instrs di Blargg se scaricata (tools/gb/fetch-test-roms.sh).
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/gb_emu.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {

std::vector<uint8_t> readRom(const char *path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string s = ss.str();
  return std::vector<uint8_t>(s.begin(), s.end());
}

void fixChecksum(std::vector<uint8_t> &r) {
  uint8_t x = 0;
  for (int i = 0x134; i <= 0x14C; i++) x = uint8_t(x - r[i] - 1);
  r[0x14D] = x;
}

// ROM da 32 KB: tipo di cartuccia, codice della RAM e programma da 0x150.
std::vector<uint8_t> makeRom(uint8_t cartType, uint8_t ramCode, const std::vector<uint8_t> &prog) {
  std::vector<uint8_t> r(0x8000, 0x00);
  const uint8_t entry[] = {0x00, 0xC3, 0x50, 0x01};  // NOP; JP 0x0150
  memcpy(&r[0x100], entry, sizeof entry);
  memcpy(&r[0x134], "CALAMAIO TEST", 13);
  r[0x147] = cartType;
  r[0x148] = 0x00;  // 32 KB
  r[0x149] = ramCode;
  memcpy(&r[0x150], prog.data(), prog.size());
  fixChecksum(r);
  return r;
}

// Abilita la RAM, scrive 0x42 in 0xA000, poi gira in tondo.
const std::vector<uint8_t> kRamProg = {0x3E, 0x0A, 0xEA, 0x00, 0x00, 0x3E, 0x42, 0xEA, 0x00, 0xA0, 0x18, 0xFE};

// Tessera 0 = righe FF/00 (colore 1), mappa già a zero, BGP = E4, LCD acceso: tutto lo schermo in tonalità 1.
const std::vector<uint8_t> kScreenProg = {
    0xF3,                    // DI
    0xAF, 0xE0, 0x40,        // XOR A; LDH (40),A   LCD spento
    0x21, 0x00, 0x80,        // LD HL,8000
    0x06, 0x08,              // LD B,8
    0x3E, 0xFF, 0x22,        // loop: LD A,FF; LD (HL+),A
    0x3E, 0x00, 0x22,        //       LD A,00; LD (HL+),A
    0x05, 0x20, 0xF7,        //       DEC B; JR NZ,loop
    0x3E, 0xE4, 0xE0, 0x47,  // LD A,E4; LDH (47),A  BGP
    0x3E, 0x91, 0xE0, 0x40,  // LD A,91; LDH (40),A  LCD acceso, sfondo da 8000/9800
    0x18, 0xFE,              // JR -2
};

uint32_t fnv(const uint8_t *frame) {
  uint32_t h = 2166136261u;
  for (int y = 0; y < kLcdH; y++)
    for (int x = 0; x < kLcdW; x++) {
      h ^= shadeAt(frame, x, y);
      h *= 16777619u;
    }
  return h;
}

}  // namespace

void test_bad_checksum() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, kRamProg);
  r[0x14D] ^= 0xFF;
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::BadChecksum), int(e.load(r.data(), r.size())));
  TEST_ASSERT_FALSE(e.loaded());
}

void test_too_small_and_old_rom_size_code() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, kRamProg);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::TooSmall), int(e.load(r.data(), 0x100)));
  TEST_ASSERT_EQUAL(int(Load::TooSmall), int(e.load(nullptr, 0x8000)));
  r[0x148] = 0x52;  // 72 banchi: codice vecchio, fuori dalla tabella di Peanut-GB
  fixChecksum(r);
  TEST_ASSERT_EQUAL(int(Load::Unsupported), int(e.load(r.data(), r.size())));
}

// Intestazione che promette 128 KB (MBC1, 8 banchi) ma file da 32 KB: il programma passa al banco 7, che non c'è.
// Le letture oltre la fine danno 0xFF (RST 38 all'infinito): niente letture fuori dal buffer, nessun crash.
void test_truncated_rom_reads_ff() {
  const std::vector<uint8_t> prog = {0x3E, 0x07, 0xEA, 0x00, 0x20, 0xC3, 0x00, 0x40};  // LD A,7; LD (2000),A; JP 4000
  std::vector<uint8_t> r = makeRom(0x01, 0x00, prog);
  r[0x148] = 0x02;  // 128 KB
  fixChecksum(r);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL_HEX8(0xFF, e.romRead(7 * 0x4000));
  for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(e.runFrame(0));
}

void test_unsupported_mbc() {
  Emu e;
  std::vector<uint8_t> r = makeRom(0xFC, 0x00, kRamProg);  // Pocket Camera
  TEST_ASSERT_EQUAL(int(Load::Unsupported), int(e.load(r.data(), r.size())));
  r = makeRom(0x22, 0x00, kRamProg);  // MBC7
  TEST_ASSERT_EQUAL(int(Load::Unsupported), int(e.load(r.data(), r.size())));
}

void test_ram_limits() {
  Emu e;
  std::vector<uint8_t> r = makeRom(0x1B, 0x03, kRamProg);  // MBC5+RAM+BATTERY, 32 KB
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0x8000, int(e.ramSize()));
  r = makeRom(0x1B, 0x04, kRamProg);  // 128 KB
  TEST_ASSERT_EQUAL(int(Load::RamTooBig), int(e.load(r.data(), r.size())));
  r = makeRom(0x1B, 0x05, kRamProg);  // 64 KB
  TEST_ASSERT_EQUAL(int(Load::RamTooBig), int(e.load(r.data(), r.size())));
  r = makeRom(0x06, 0x00, kRamProg);  // MBC2: 512 mezzi byte
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0x200, int(e.ramSize()));
  r = makeRom(0x00, 0x00, kRamProg);  // solo ROM
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0, int(e.ramSize()));
}

void test_cart_ram_write_marks_dirty() {
  std::vector<uint8_t> r = makeRom(0x03, 0x02, kRamProg);  // MBC1+RAM+BATTERY, 8 KB
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0x2000, int(e.ramSize()));
  std::vector<uint8_t> ram(e.ramSize(), 0xFF);
  e.attachRam(ram.data());
  TEST_ASSERT_FALSE(e.ramDirty());
  for (int i = 0; i < 3; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_EQUAL_HEX8(0x42, ram[0]);
  TEST_ASSERT_TRUE(e.ramDirty());
  e.clearDirty();
  TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_FALSE(e.ramDirty());  // stesso valore riscritto: niente da salvare
}

void test_known_screen() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, kScreenProg);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  for (int y = 0; y < kLcdH; y++)
    for (int x = 0; x < kLcdW; x++)
      if (shadeAt(e.frame(), x, y) != 1) TEST_FAIL_MESSAGE("pixel diverso dalla tonalità 1");
  char t[17];
  e.title(t);
  TEST_ASSERT_EQUAL_STRING("CALAMAIO TEST", t);
}

void test_invalid_opcode_stops() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, {0xD3});  // codice inesistente sul Game Boy
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_FALSE(e.runFrame(0));
  TEST_ASSERT_FALSE(e.runFrame(0));  // resta fermo
}

// dmg-acid2 (Matt Currie, MIT): al fotogramma 60 lo schermo è identico a reference-dmg.png (verificato
// pixel per pixel il 2026-10-12 con Peanut-GB d0bcca7). FNV-1a 32 delle tonalità 0–3, riga per riga.
void test_dmg_acid2_frame_hash() {
  const std::vector<uint8_t> r = readRom("test/roms/dmg-acid2.gb");
  TEST_ASSERT_EQUAL_MESSAGE(32768, int(r.size()), "manca test/roms/dmg-acid2.gb");
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  for (int i = 0; i < 60; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_EQUAL_HEX32(0xb312e6f6u, fnv(e.frame()));
  for (int i = 0; i < 60; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_EQUAL_HEX32(0xb312e6f6u, fnv(e.frame()));  // immagine ferma: deterministico
}

// cpu_instrs di Blargg (senza licenza esplicita: non nel repository). La scarica tools/gb/fetch-test-roms.sh.
std::string gSerial;
void test_blargg_cpu_instrs() {
  const std::vector<uint8_t> r = readRom("test/roms/.fetched/cpu_instrs.gb");
  if (r.empty()) TEST_IGNORE_MESSAGE("cpu_instrs.gb assente: tools/gb/fetch-test-roms.sh");
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  gSerial.clear();
  e.onSerial([](uint8_t c) { gSerial.push_back(char(c)); });
  for (int i = 0; i < 6000 && gSerial.find("Passed all tests") == std::string::npos && gSerial.find("Failed") == std::string::npos; i++)
    TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_TRUE_MESSAGE(gSerial.find("Passed all tests") != std::string::npos, gSerial.c_str());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_bad_checksum);
  RUN_TEST(test_too_small_and_old_rom_size_code);
  RUN_TEST(test_truncated_rom_reads_ff);
  RUN_TEST(test_unsupported_mbc);
  RUN_TEST(test_ram_limits);
  RUN_TEST(test_cart_ram_write_marks_dirty);
  RUN_TEST(test_known_screen);
  RUN_TEST(test_invalid_opcode_stops);
  RUN_TEST(test_dmg_acid2_frame_hash);
  RUN_TEST(test_blargg_cpu_instrs);
  return UNITY_END();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_emu`
Expected: FAIL in compilazione, `fatal error: core/gb_emu.h: No such file or directory`.

- [ ] **Step 4: Write the implementation**

Create `firmware/src/core/gb_emu.h`:

```cpp
// Giochi Game Boy: emulatore (Peanut-GB, lib/peanut_gb, MIT) dietro un'interfaccia piccola, provata in native
// (test_gb_emu). La ROM resta di chi chiama: sulla scheda è la partizione «gbrom» mappata in memoria.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>

struct gb_s;

namespace cal {
namespace gb {

const int kLcdW = 160, kLcdH = 144;
const int kFrameStride = kLcdW / 4;                 // 2 bit per pixel, MSB = x più a sinistra
const size_t kFrameBytes = size_t(kFrameStride) * kLcdH;  // 5760
const size_t kCartRamMax = 0x8000;                  // 32 KB: oltre, «Gioco non supportato»
const uint32_t kRomMin = 0x150;                     // l'intestazione finisce a 0x14F

// Tasti del Game Boy: bit a 1 = premuto (Peanut-GB li vuole al contrario, ci pensa Emu).
enum Key : uint8_t {
  KeyA = 0x01, KeyB = 0x02, KeySelect = 0x04, KeyStart = 0x08,
  KeyRight = 0x10, KeyLeft = 0x20, KeyUp = 0x40, KeyDown = 0x80,
};

enum class Load : uint8_t { Ok, TooSmall, BadChecksum, Unsupported, RamTooBig };
const char *loadKey(Load l);  // per il registro: ok, piccola, checksum, mbc, ram

// Tonalità del pixel (0 = bianco … 3 = nero) in un fotogramma da kFrameBytes.
inline uint8_t shadeAt(const uint8_t *f, int x, int y) {
  return (f[y * kFrameStride + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;
}

class Emu {
 public:
  Emu() {}
  ~Emu();
  Emu(const Emu &) = delete;
  Emu &operator=(const Emu &) = delete;

  // Controlla l'intestazione e prepara la CPU. Il contesto (~17 KB) va sullo heap: false se manca memoria.
  Load load(const uint8_t *rom, uint32_t romSize);
  bool loaded() const { return gb_ != nullptr; }
  size_t ramSize() const { return ramSize_; }          // byte di RAM della cartuccia (0 = nessuna)
  // Dopo load(): buffer di ramSize() byte, già riempito (salvataggio o 0xFF). Resta di chi chiama.
  void attachRam(uint8_t *ram) { ram_ = ram; }
  void setRtc(const struct tm &t);                       // MBC3: orologio della cartuccia
  void frameSkip(bool on);                               // disegna un fotogramma sì e uno no
  // Un fotogramma (70224 cicli). keys: tasti premuti (Key). false = la CPU si è fermata (codice non valido).
  bool runFrame(uint8_t keys);
  const uint8_t *frame() const { return frame_; }
  bool ramDirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  void title(char out[17]) const;                        // titolo dell'intestazione (0x134, ≤ 16), ripulito
  // Byte mandati sulla porta seriale (solo test: le ROM di Blargg scrivono lì l'esito). nullptr = nessun cavo.
  void onSerial(void (*fn)(uint8_t c));
  void unload();

  // Richiamate da Peanut-GB (pubbliche solo per i trampolini in gb_emu.cpp).
  uint8_t romRead(uint32_t a) const { return a < romSize_ ? rom_[a] : 0xFF; }
  uint8_t ramRead(uint32_t a) const { return ram_ && a < ramSize_ ? ram_[a] : 0xFF; }
  void ramWrite(uint32_t a, uint8_t v);
  void drawLine(const uint8_t *px, unsigned line);
  [[noreturn]] void fault();  // da gb_error: Peanut-GB non prevede il ritorno (PGB_UNREACHABLE)
  void serialOut(uint8_t c) { if (serial_) serial_(c); }

 private:
  struct gb_s *gb_ = nullptr;
  const uint8_t *rom_ = nullptr;
  uint32_t romSize_ = 0;
  uint8_t *ram_ = nullptr;
  size_t ramSize_ = 0;
  bool dirty_ = false, fault_ = false;
  void (*serial_)(uint8_t) = nullptr;
  jmp_buf jb_;
  uint8_t frame_[kFrameBytes] = {};
};

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_emu.cpp`:

```cpp
#include "gb_emu.h"

#include <stdlib.h>
#include <string.h>

// Peanut-GB: niente audio, 4 tonalità senza tavolozze a colori, tutto in questo file (unica unità con il codice).
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_12_COLOUR 0
#include <peanut_gb.h>

namespace cal {
namespace gb {

namespace {
Emu *self(struct gb_s *g) { return static_cast<Emu *>(g->direct.priv); }
uint8_t romRd(struct gb_s *g, const uint_fast32_t a) { return self(g)->romRead(uint32_t(a)); }
uint8_t ramRd(struct gb_s *g, const uint_fast32_t a) { return self(g)->ramRead(uint32_t(a)); }
void ramWr(struct gb_s *g, const uint_fast32_t a, const uint8_t v) { self(g)->ramWrite(uint32_t(a), v); }
void onError(struct gb_s *g, const enum gb_error_e, const uint16_t) { self(g)->fault(); }
void serTx(struct gb_s *g, const uint8_t c) { self(g)->serialOut(c); }
enum gb_serial_rx_ret_e serRx(struct gb_s *, uint8_t *) { return GB_SERIAL_RX_NO_CONNECTION; }
void onLine(struct gb_s *g, const uint8_t *px, const uint_fast8_t line) { self(g)->drawLine(px, line); }
}  // namespace

const char *loadKey(Load l) {
  switch (l) {
    case Load::Ok: return "ok";
    case Load::TooSmall: return "piccola";
    case Load::BadChecksum: return "checksum";
    case Load::Unsupported: return "mbc";
    case Load::RamTooBig: return "ram";
  }
  return "?";
}

Emu::~Emu() { unload(); }

void Emu::unload() {
  free(gb_);
  gb_ = nullptr;
  rom_ = nullptr;
  ram_ = nullptr;
  serial_ = nullptr;
  romSize_ = 0;
  ramSize_ = 0;
  dirty_ = fault_ = false;
}

Load Emu::load(const uint8_t *rom, uint32_t romSize) {
  unload();
  if (!rom || romSize < kRomMin) return Load::TooSmall;
  // Peanut-GB indicizza una tabella di 9 voci con il byte 0x148: i codici vecchi (0x52–0x54) la sforerebbero.
  if (rom[0x148] > 8) return Load::Unsupported;
  gb_ = static_cast<struct gb_s *>(malloc(sizeof(struct gb_s)));
  if (!gb_) return Load::Unsupported;
  rom_ = rom;
  romSize_ = romSize;
  const enum gb_init_error_e e = gb_init(gb_, romRd, ramRd, ramWr, onError, this);
  if (e != GB_INIT_NO_ERROR) {
    unload();
    return e == GB_INIT_INVALID_CHECKSUM ? Load::BadChecksum : Load::Unsupported;
  }
  size_t n = 0;
  if (gb_get_save_size_s(gb_, &n) != 0 || n > kCartRamMax) {
    unload();
    return Load::RamTooBig;
  }
  ramSize_ = n;
  gb_init_lcd(gb_, onLine);
  memset(frame_, 0, sizeof frame_);
  return Load::Ok;
}

void Emu::setRtc(const struct tm &t) {
  if (gb_) gb_set_rtc(gb_, &t);
}

void Emu::onSerial(void (*fn)(uint8_t c)) {
  serial_ = fn;
  if (gb_) gb_init_serial(gb_, fn ? serTx : nullptr, fn ? serRx : nullptr);
}

void Emu::frameSkip(bool on) {
  if (gb_) gb_->direct.frame_skip = on;
}

bool Emu::runFrame(uint8_t keys) {
  if (!gb_ || fault_) return false;
  gb_->direct.joypad = uint8_t(~keys);  // Peanut-GB: 0 = premuto
  // Codice non valido o lettura impossibile: gb_error salta qui (fault), il fotogramma resta a metà.
  if (setjmp(jb_)) return false;
  gb_run_frame(gb_);
  return true;
}

void Emu::fault() {
  fault_ = true;
  longjmp(jb_, 1);
}

void Emu::ramWrite(uint32_t a, uint8_t v) {
  if (!ram_ || a >= ramSize_ || ram_[a] == v) return;
  ram_[a] = v;
  dirty_ = true;
}

void Emu::drawLine(const uint8_t *px, unsigned line) {
  if (line >= unsigned(kLcdH)) return;
  uint8_t *row = frame_ + line * kFrameStride;
  for (int b = 0; b < kFrameStride; b++) {
    const uint8_t *p = px + b * 4;
    row[b] = uint8_t(((p[0] & 3) << 6) | ((p[1] & 3) << 4) | ((p[2] & 3) << 2) | (p[3] & 3));
  }
}

void Emu::title(char out[17]) const {
  int n = 0;
  for (uint32_t a = 0x134; a < 0x144 && n < 16; a++) {
    const uint8_t c = romRead(a);
    if (c == 0 || c >= 0x80) break;  // 0x143 può essere il flag CGB (0x80/0xC0)
    out[n++] = (c >= 0x20 && c < 0x7F) ? char(c) : '?';
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

}  // namespace gb
}  // namespace cal
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_gb_emu`
Expected: 10 test PASSED (con `cpu_instrs.gb` scaricato; senza, `test_blargg_cpu_instrs` IGNORED e 9 PASSED).

Run: `cd firmware && pio test -e native && pio run -e heltec213`
Expected: tutti PASSED; build SUCCESS (Peanut-GB compila anche con xtensa gcc 8.4).

- [ ] **Step 6: DECISIONI**

```markdown
- **Giochi, emulatore**: Peanut-GB (MIT) copiato in `firmware/lib/peanut_gb/peanut_gb.h` dal commit `d0bcca771c83a2638c93a9ae61f3d51f226dc905` (SHA-256 `cf777706…f728`), senza modifiche; MBC 0/1/2/3/5, niente audio, 4 tonalità. Errori della CPU: `longjmp` fuori da `gb_run_frame` (Peanut-GB non prevede il ritorno da `gb_error`). Codici 0x148 > 8 rifiutati. RAM della cartuccia oltre 32 KB → «Gioco non supportato». Prove: dmg-acid2 (MIT, nel repository) identica al riferimento al fotogramma 60; cpu_instrs di Blargg scaricata da `tools/gb/fetch-test-roms.sh` (fuori dal repository: nessuna licenza dichiarata).
```

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/peanut_gb firmware/src/core/gb_emu.h firmware/src/core/gb_emu.cpp firmware/test/test_gb_emu \
  firmware/test/roms/dmg-acid2.gb firmware/test/roms/LICENSE-dmg-acid2.txt tools/gb/fetch-test-roms.sh .gitignore docs/DECISIONI.md
git commit -m "Giochi: emulatore Peanut-GB con ROM di prova"
```

---

### Task 4: Riduzione e retinatura del fotogramma

**Files:**
- Create: `firmware/src/core/gb_frame.h`, `firmware/src/core/gb_frame.cpp`
- Test: `firmware/test/test_gb_frame/test_main.cpp`
- Modify: `docs/DECISIONI.md`

**Interfaces:**
- Consumes: `cal::gb::shadeAt`, `kLcdW`, `kLcdH`, `Emu` (Task 3, solo nel test dorato).
- Produces (`core/gb_frame.h`): `kScreenW = 250`, `kScreenH = 122`, `kScreenStride = 32`, `kScreenBytes = 3904`, `kWinW = 136`, `kWinH = 122`, `kWinX = 57`; `uint8_t darkness(const uint8_t *gbFrame, int ox, int oy)`; `uint8_t bayer(int x, int y)`; `void scaleDither(const uint8_t *gbFrame, uint8_t *screen)`; `void rotate180(uint8_t *screen)`; `void composeFrame(const uint8_t *bands, const uint8_t *gbFrame, bool rot, uint8_t *out)`; `screenBit`, `screenSet`.

Algoritmo (spec §«Vincoli della scheda»): ogni pixel d'arrivo 136×122 copre un rettangolo dello schermo del Game Boy (122/144 su entrambi gli assi); in unità comuni un pixel d'arrivo è lungo 160 (o 144) e uno di partenza 136 (o 122), quindi copre al massimo 3 pixel di partenza per asse con pesi interi che sommano a 160 (o 144). Nero = media pesata delle tonalità × 85, arrotondata (0–255). Retinatura **ordinata** Bayer 4×4 sulle coordinate dello schermo (soglie 8, 24, …, 248): tonalità 1 → 5 pixel neri su 16, tonalità 2 → 11 su 16. Scelta ordinata invece della diffusione dell'errore: la diffusione sposta i puntini a ogni fotogramma anche dove l'immagine non cambia, e ogni pixel che cambia sul pannello veloce costa impulsi e scie. Le impronte del test dorato vengono da un'implementazione indipendente in Python con frazioni esatte (2026-10-12).

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_frame/test_main.cpp`:

```cpp
// Riduzione e retinatura (core/gb_frame): casi calcolati a mano e il fotogramma 60 di dmg-acid2 contro
// un'implementazione indipendente in Python con frazioni esatte (impronte qui sotto, 2026-10-12).
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/gb_emu.h"
#include "core/gb_frame.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {

void fill(uint8_t *f, uint8_t shade) { memset(f, uint8_t(shade * 0x55), kFrameBytes); }
void setShade(uint8_t *f, int x, int y, uint8_t s) {
  uint8_t &b = f[y * kFrameStride + (x >> 2)];
  const int sh = 6 - 2 * (x & 3);
  b = uint8_t((b & ~(3 << sh)) | (s << sh));
}
uint32_t fnv(const uint8_t *d, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= d[i];
    h *= 16777619u;
  }
  return h;
}
int inkIn(const uint8_t *s, int x0, int y0, int w, int h) {
  int n = 0;
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) n += screenBit(s, x, y);
  return n;
}

}  // namespace

void test_flat_shades() {
  uint8_t f[kFrameBytes], s[kScreenBytes];
  const int perBlock[4] = {0, 5, 11, 16};  // soglie Bayer sotto 0, 85, 170, 255
  for (uint8_t sh = 0; sh < 4; sh++) {
    fill(f, sh);
    memset(s, 0, sizeof s);
    scaleDither(f, s);
    TEST_ASSERT_EQUAL_UINT8(sh * 85, darkness(f, 10, 10));
    // ogni blocco 4×4 allineato e tutto dentro la finestra (x 60–191, y 0–119)
    for (int by = 0; by + 4 <= kWinH; by += 4)
      for (int bx = 60; bx + 4 <= kWinX + kWinW; bx += 4) TEST_ASSERT_EQUAL(perBlock[sh], inkIn(s, bx, by, 4, 4));
  }
}

void test_bands_untouched() {
  uint8_t f[kFrameBytes], s[kScreenBytes];
  fill(f, 3);
  memset(s, 0xA5, sizeof s);
  uint8_t before[kScreenBytes];
  memcpy(before, s, sizeof s);
  scaleDither(f, s);
  for (int y = 0; y < kScreenH; y++)
    for (int x = 0; x < kScreenW; x++)
      if (x < kWinX || x >= kWinX + kWinW) TEST_ASSERT_EQUAL(screenBit(before, x, y), screenBit(s, x, y));
  TEST_ASSERT_EQUAL(kWinW * kWinH, inkIn(s, kWinX, 0, kWinW, kWinH));  // tutto nero dentro
}

// Barra nera larga 8 pixel a sinistra: 8 × 136/160 = 6,8 colonne. Le colonne 0–5 sono piene, la 6 è coperta
// per 128/160 (darkness 204), dalla 7 in poi bianco.
void test_edge_partial_column() {
  uint8_t f[kFrameBytes], s[kScreenBytes];
  fill(f, 0);
  for (int y = 0; y < kLcdH; y++)
    for (int x = 0; x < 8; x++) setShade(f, x, y, 3);
  memset(s, 0, sizeof s);
  scaleDither(f, s);
  for (int ox = 0; ox <= 5; ox++) TEST_ASSERT_EQUAL(kWinH, inkIn(s, kWinX + ox, 0, 1, kWinH));
  TEST_ASSERT_EQUAL_UINT8(204, darkness(f, 6, 50));
  TEST_ASSERT_EQUAL(0, inkIn(s, kWinX + 7, 0, kWinW - 7, kWinH));
}

void test_rotate180_is_involution() {
  uint8_t s[kScreenBytes], r[kScreenBytes];
  memset(s, 0, sizeof s);
  screenSet(s, 0, 0, true);
  screenSet(s, 3, 7, true);
  memcpy(r, s, sizeof s);
  rotate180(r);
  TEST_ASSERT_TRUE(screenBit(r, 249, 121));
  TEST_ASSERT_TRUE(screenBit(r, 246, 114));
  TEST_ASSERT_EQUAL(2, inkIn(r, 0, 0, kScreenW, kScreenH));
  rotate180(r);
  TEST_ASSERT_EQUAL_MEMORY(s, r, sizeof s);
}

void test_dmg_acid2_golden() {
  std::ifstream in("test/roms/dmg-acid2.gb", std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string str = ss.str();
  TEST_ASSERT_EQUAL_MESSAGE(32768, int(str.size()), "manca test/roms/dmg-acid2.gb");
  std::vector<uint8_t> rom(str.begin(), str.end());
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(rom.data(), rom.size())));
  for (int i = 0; i < 60; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  uint8_t bands[kScreenBytes], out[kScreenBytes];
  memset(bands, 0, sizeof bands);
  composeFrame(bands, e.frame(), false, out);
  TEST_ASSERT_EQUAL_HEX32(0x938ba228u, fnv(out, sizeof out));
  composeFrame(bands, e.frame(), true, out);
  TEST_ASSERT_EQUAL_HEX32(0xea0f15fau, fnv(out, sizeof out));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_flat_shades);
  RUN_TEST(test_bands_untouched);
  RUN_TEST(test_edge_partial_column);
  RUN_TEST(test_rotate180_is_involution);
  RUN_TEST(test_dmg_acid2_golden);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_frame`
Expected: FAIL in compilazione, `fatal error: core/gb_frame.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `firmware/src/core/gb_frame.h`:

```cpp
// Giochi Game Boy: dal fotogramma 160×144 a 4 tonalità alla finestra 136×122 in bianco e nero, centrata nello
// schermo 250×122 (bande di 57 px ai lati). Riduzione per aree (ogni pixel d'arrivo media i pixel che copre, con
// il loro peso) e retinatura ordinata Bayer 4×4 sulle coordinate dello schermo: la stessa tonalità dà sempre
// lo stesso disegno, così fra un fotogramma e l'altro cambiano solo i pixel che cambiano davvero (meno scie
// sul pannello veloce). Logica pura, provata in native (test_gb_frame).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace gb {

const int kScreenW = 250, kScreenH = 122;
const int kScreenStride = 32;                                   // byte per riga, MSB = x più a sinistra, 1 = nero
const size_t kScreenBytes = size_t(kScreenStride) * kScreenH;   // 3904, come Canvas<250, 122>
const int kWinW = 136, kWinH = 122;                             // 160×144 ridotto di 122/144
const int kWinX = (kScreenW - kWinW) / 2;                       // 57

// Livello di nero (0–255) del pixel d'arrivo (ox, oy) della finestra.
uint8_t darkness(const uint8_t *gbFrame, int ox, int oy);
// Soglia Bayer (8–248) del pixel di schermo (x, y): nero se darkness > soglia.
uint8_t bayer(int x, int y);
// Scrive solo la finestra (x 57–192): le bande restano come sono.
void scaleDither(const uint8_t *gbFrame, uint8_t *screen);
// Ruota 180° (Display › Ruota): il pixel (x, y) va in (249 − x, 121 − y).
void rotate180(uint8_t *screen);
// Fotogramma completo: bande (già disegnate, kScreenBytes) + finestra, poi rotazione se serve.
void composeFrame(const uint8_t *bands, const uint8_t *gbFrame, bool rot, uint8_t *out);

inline bool screenBit(const uint8_t *s, int x, int y) { return s[y * kScreenStride + (x >> 3)] & (0x80 >> (x & 7)); }
inline void screenSet(uint8_t *s, int x, int y, bool ink) {
  uint8_t &b = s[y * kScreenStride + (x >> 3)];
  const uint8_t m = uint8_t(0x80 >> (x & 7));
  b = ink ? uint8_t(b | m) : uint8_t(b & ~m);
}

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_frame.cpp`:

```cpp
#include "gb_frame.h"

#include <string.h>

#include "gb_emu.h"

namespace cal {
namespace gb {

namespace {

// Pesi della riduzione su un asse. In unità comuni un pixel d'arrivo è lungo `src` (160 o 144) e uno di partenza
// `dst` (136 o 122): ogni pixel d'arrivo copre al massimo 3 pixel di partenza, i pesi sommano a `src`.
struct Tap {
  uint8_t first;  // primo pixel di partenza
  uint8_t n;      // quanti (1–3)
  uint8_t w[3];   // peso di ciascuno
};

template <int Dst, int Src>
struct Axis {
  Tap t[Dst];
  Axis() {
    for (int o = 0; o < Dst; o++) {
      const int a = o * Src, b = (o + 1) * Src;  // copertura del pixel d'arrivo
      Tap &tp = t[o];
      tp.first = uint8_t(a / Dst);
      tp.n = 0;
      for (int s = a / Dst; s * Dst < b; s++) {
        const int lo = s * Dst > a ? s * Dst : a, hi = (s + 1) * Dst < b ? (s + 1) * Dst : b;
        tp.w[tp.n++] = uint8_t(hi - lo);
      }
    }
  }
};

const Axis<kWinW, kLcdW> &cols() {
  static const Axis<kWinW, kLcdW> a;
  return a;
}
const Axis<kWinH, kLcdH> &rows() {
  static const Axis<kWinH, kLcdH> a;
  return a;
}

const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
const int kDen = kLcdW * kLcdH;  // 23040: somma dei pesi di un pixel d'arrivo

}  // namespace

uint8_t bayer(int x, int y) { return uint8_t(kBayer[y & 3][x & 3] * 16 + 8); }

uint8_t darkness(const uint8_t *f, int ox, int oy) {
  const Tap &ty = rows().t[oy], &tx = cols().t[ox];
  int acc = 0;
  for (int j = 0; j < ty.n; j++) {
    int line = 0;
    for (int i = 0; i < tx.n; i++) line += tx.w[i] * shadeAt(f, tx.first + i, ty.first + j);
    acc += ty.w[j] * line;
  }
  return uint8_t((acc * 85 + kDen / 2) / kDen);  // tonalità 3 = 255
}

void scaleDither(const uint8_t *f, uint8_t *screen) {
  for (int oy = 0; oy < kWinH; oy++)
    for (int ox = 0; ox < kWinW; ox++) {
      const int x = kWinX + ox;
      screenSet(screen, x, oy, darkness(f, ox, oy) > bayer(x, oy));
    }
}

void rotate180(uint8_t *s) {
  const int n = kScreenW * kScreenH;
  for (int i = 0; i < n / 2; i++) {
    const int j = n - 1 - i;
    const int xi = i % kScreenW, yi = i / kScreenW, xj = j % kScreenW, yj = j / kScreenW;
    const bool a = screenBit(s, xi, yi), b = screenBit(s, xj, yj);
    screenSet(s, xi, yi, b);
    screenSet(s, xj, yj, a);
  }
}

void composeFrame(const uint8_t *bands, const uint8_t *gbFrame, bool rot, uint8_t *out) {
  memcpy(out, bands, kScreenBytes);
  scaleDither(gbFrame, out);
  if (rot) rotate180(out);
}

}  // namespace gb
}  // namespace cal
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_gb_frame`
Expected: 5 test PASSED (anche `test_dmg_acid2_golden`: `0x938ba228` dritto, `0xea0f15fa` ruotato).

- [ ] **Step 5: DECISIONI e commit**

```markdown
- **Giochi, immagine**: 160×144 → 136×122 centrato (x 57–192), riduzione per aree con pesi interi e retinatura ordinata Bayer 4×4 sulle coordinate dello schermo (stabile fra fotogrammi: meno pixel cambiati, meno scie sul pannello veloce). Ruota 180° vale anche in gioco (in `composeFrame`); Inverti colori anche (`gbrun` inverte il fotogramma composto, bande comprese: `fastink::submit` non applica né l'una né l'altra, contratto). Bande laterali di 57 px disegnate da `ui::draw` (schermata GbPlay).
```

```bash
git add firmware/src/core/gb_frame.h firmware/src/core/gb_frame.cpp firmware/test/test_gb_frame docs/DECISIONI.md
git commit -m "Giochi: riduzione e retinatura del fotogramma"
```

---

### Task 5: Triplo buffer fra i core e ritmo a 59,7 Hz

**Files:**
- Create: `firmware/src/core/gb_handoff.h`, `firmware/src/core/gb_handoff.cpp`
- Test: `firmware/test/test_gb_handoff/test_main.cpp`

**Interfaces:**
- Consumes: `kScreenBytes` (Task 4).
- Produces (`core/gb_handoff.h`): `class FrameMailbox { uint8_t *back(); void publish(); const uint8_t *take(); void reset(); uint32_t published() const; }`; `const int64_t kFrameNs = 16742706`; `kMaxBurst = 2`; `kResyncFrames = 6`; `class Pacer { void start(int64_t nowUs); int due(int64_t nowUs); void ran(int n); int64_t waitUs(int64_t nowUs) const; uint32_t dropped() const; }`.

Passaggio fra i core (spec «dual core»): l'emulatore sul core 1 scrive sempre in `back()` e chiama `publish()`, che scambia atomicamente `back` con il buffer di mezzo marcandolo «fresco»; il task del pannello sul core 0 chiama `take()`, che se il buffer di mezzo è fresco lo scambia con il suo. Nessuno aspetta l'altro, il pannello prende sempre il fotogramma più recente e quelli intermedi si perdono (il Game Boy va a 59,7 Hz, il pannello a ~11–15 immagini/s). Il ritmo: 70 224 cicli a 4 194 304 Hz = 16 742,706 µs; con un piccolo ritardo si emulano al massimo 2 fotogrammi di fila, oltre 6 (~100 ms, per esempio una scrittura in flash) si riparte da adesso: il gioco rallenta, non corre a scatti.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_handoff/test_main.cpp`:

```cpp
// Triplo buffer e ritmo dell'emulazione (core/gb_handoff).
#include <string.h>
#include <unity.h>

#include <thread>

#include "core/gb_handoff.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

static FrameMailbox gMb;  // 12 KB: fuori dallo stack

void test_take_without_publish_is_null() {
  gMb.reset();
  TEST_ASSERT_NULL(gMb.take());
}

void test_latest_wins_and_taken_once() {
  gMb.reset();
  for (uint8_t k = 1; k <= 3; k++) {
    memset(gMb.back(), k, kScreenBytes);
    gMb.publish();
  }
  const uint8_t *f = gMb.take();
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_EQUAL_UINT8(3, f[0]);
  TEST_ASSERT_EQUAL_UINT8(3, f[kScreenBytes - 1]);
  TEST_ASSERT_NULL(gMb.take());
  memset(gMb.back(), 4, kScreenBytes);  // il buffer preso non viene riscritto finché il consumatore lo tiene
  TEST_ASSERT_EQUAL_UINT8(3, f[0]);
  gMb.publish();
  TEST_ASSERT_EQUAL_UINT8(4, gMb.take()[0]);
}

// Due thread veri: ogni fotogramma preso è intero (tutti i byte uguali) e i numeri crescono.
void test_threads_never_tear() {
  gMb.reset();
  int bad = 0, seen = 0, last = 0;
  std::thread cons([&] {
    for (;;) {  // finisce quando vede l'ultimo (250): il più recente non si perde mai
      const uint8_t *f = gMb.take();
      if (f) {
        const uint8_t k = f[0];
        for (size_t i = 1; i < kScreenBytes; i++)
          if (f[i] != k) { bad++; break; }
        if (k < last) bad++;
        last = k;
        seen++;
        if (k == 250) break;
      }
      std::this_thread::yield();
    }
  });
  for (int k = 1; k <= 250; k++) {
    memset(gMb.back(), k, kScreenBytes);
    gMb.publish();
    std::this_thread::yield();
  }
  cons.join();
  TEST_ASSERT_EQUAL(0, bad);
  TEST_ASSERT_TRUE(seen > 0);
  TEST_ASSERT_EQUAL(250, last);
}

void test_pacer_on_time_and_early() {
  Pacer p;
  p.start(1000000);
  TEST_ASSERT_EQUAL(1, p.due(1000000));
  p.ran(1);
  TEST_ASSERT_EQUAL(0, p.due(1000000 + 16000));
  TEST_ASSERT_EQUAL(743, int(p.waitUs(1000000 + 16000)));  // 16 742,706 µs arrotondati in su
  TEST_ASSERT_EQUAL(1, p.due(1000000 + 16743));
}

void test_pacer_small_delay_bursts() {
  Pacer p;
  p.start(0);
  TEST_ASSERT_EQUAL(2, p.due(40000));  // 3 dovuti, al massimo 2 di fila
  p.ran(2);
  TEST_ASSERT_EQUAL(1, p.due(40000));
  p.ran(1);
  TEST_ASSERT_EQUAL(0, p.due(40000));
  TEST_ASSERT_EQUAL(0u, p.dropped());
}

void test_pacer_long_stall_resyncs() {
  Pacer p;
  p.start(0);
  p.ran(1);
  TEST_ASSERT_EQUAL(1, p.due(500000));  // 0,5 s fermo (refresh lento, flash): niente corsa
  TEST_ASSERT_TRUE(p.dropped() >= 25);
  p.ran(1);
  TEST_ASSERT_EQUAL(0, p.due(500000));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_take_without_publish_is_null);
  RUN_TEST(test_latest_wins_and_taken_once);
  RUN_TEST(test_threads_never_tear);
  RUN_TEST(test_pacer_on_time_and_early);
  RUN_TEST(test_pacer_small_delay_bursts);
  RUN_TEST(test_pacer_long_stall_resyncs);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_handoff`
Expected: FAIL in compilazione, `fatal error: core/gb_handoff.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `firmware/src/core/gb_handoff.h`:

```cpp
// Giochi Game Boy: passaggio dei fotogrammi fra i due core (core/gb_frame → pannello veloce). Triplo buffer
// senza lucchetti: l'emulatore (core 1) scrive sempre in back() e pubblica; il task del pannello (core 0) prende
// il più recente non ancora preso. Nessuno aspetta l'altro; i fotogrammi intermedi si perdono (il pannello va a
// ~11–15 immagini/s, il Game Boy a 59,7). Ritmo dell'emulazione: Pacer. Provati in native (test_gb_handoff).
#pragma once
#include <stdint.h>

#include <atomic>

#include "gb_frame.h"

namespace cal {
namespace gb {

class FrameMailbox {
 public:
  uint8_t *back() { return buf_[back_]; }   // solo il produttore
  void publish();                            // solo il produttore: back() diventa il più recente
  const uint8_t *take();                     // solo il consumatore: nullptr se non c'è niente di nuovo
  void reset();                              // con i due lati fermi
  uint32_t published() const { return published_; }

 private:
  static constexpr uint8_t kFresh = 0x80;
  uint8_t buf_[3][kScreenBytes];
  uint8_t back_ = 0, front_ = 2;
  std::atomic<uint8_t> mid_{1};
  uint32_t published_ = 0;
};

// 70224 cicli a 4 194 304 Hz.
const int64_t kFrameNs = 16742706;
const int kMaxBurst = 2;       // fotogrammi di fila per recuperare un piccolo ritardo
const int kResyncFrames = 6;   // oltre (~100 ms) si riparte da adesso: il gioco rallenta, non corre

class Pacer {
 public:
  void start(int64_t nowUs) { next_ = nowUs * 1000; }
  // Quanti fotogrammi emulare adesso (0 = in anticipo).
  int due(int64_t nowUs);
  void ran(int n) { next_ += int64_t(n) * kFrameNs; }
  int64_t waitUs(int64_t nowUs) const;  // al prossimo fotogramma (0 = già dovuto)
  uint32_t dropped() const { return dropped_; }

 private:
  int64_t next_ = 0;  // ns
  uint32_t dropped_ = 0;
};

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_handoff.cpp`:

```cpp
#include "gb_handoff.h"

namespace cal {
namespace gb {

void FrameMailbox::publish() {
  back_ = uint8_t(mid_.exchange(uint8_t(back_ | kFresh)) & 3);
  published_++;
}

const uint8_t *FrameMailbox::take() {
  if (!(mid_.load() & kFresh)) return nullptr;
  front_ = uint8_t(mid_.exchange(front_) & 3);  // il produttore può aver pubblicato nel frattempo: va bene, è più nuovo
  return buf_[front_];
}

void FrameMailbox::reset() {
  back_ = 0;
  front_ = 2;
  mid_.store(1);
  published_ = 0;
}

int Pacer::due(int64_t nowUs) {
  const int64_t now = nowUs * 1000;
  if (now < next_) return 0;
  const int64_t late = (now - next_) / kFrameNs + 1;
  if (late > kResyncFrames) {
    dropped_ += uint32_t(late - 1);
    next_ = now;
    return 1;
  }
  return late > kMaxBurst ? kMaxBurst : int(late);
}

int64_t Pacer::waitUs(int64_t nowUs) const {
  const int64_t d = next_ - nowUs * 1000;
  return d > 0 ? (d + 999) / 1000 : 0;
}

}  // namespace gb
}  // namespace cal
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_gb_handoff`
Expected: 6 test PASSED (`test_threads_never_tear` con due thread veri).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core/gb_handoff.h firmware/src/core/gb_handoff.cpp firmware/test/test_gb_handoff
git commit -m "Giochi: triplo buffer fra i core e ritmo a 59,7 Hz"
```

---
### Task 6: Tasti dal controller e regole della partita

**Prerequisito:** piano A, `firmware/src/core/pad.h` (`cal::pad::State`, `Button`, `Link`).

**Files:**
- Create: `firmware/src/core/gb_model.h`
- Create: `firmware/src/core/gb_input.h`, `firmware/src/core/gb_input.cpp`
- Create: `firmware/src/core/gb_session.h`, `firmware/src/core/gb_session.cpp`
- Test: `firmware/test/test_gb_session/test_main.cpp`

**Interfaces:**
- Consumes: `cal::pad::State`, `cal::pad::Button` (piano A); `cal::gb::Key` (Task 3); `cal::Ms` (`core/clock.h`).
- Produces:
  - `core/gb_model.h`: `cal::gb::kIdLen = 17`, `kTitleLen = 33`, `kMaxGames = 16` (voci mostrate in Menu › Gioco › ROM; `gb-list` ne porta fino a 64); `struct GameEntry { char id[17]; char title[33]; uint32_t size; }`; `enum class LoadStep : uint8_t { Download, Verify, Start }`; `enum class PauseWhy : uint8_t { None, User, Controller, Idle, Perm }`; `enum class GbAct : uint8_t { None, Pick, Resume, Exit, ExitRest, Profile, Cancel, Pause }`; `struct cal::GbView { GameEntry games[16]; int nGames; bool listSeen; bool partOk; char installed[17]; char current[17]; char title[33]; LoadStep step; uint32_t done, total; int drawnPct; Ms drawnAt; PauseWhy pause; bool smooth; bool lowBand; char installedTitle[33]; }`.
  - `core/gb_input.h`: `kStickDead = 50`; `struct PadKeys { uint8_t keys; bool pause; }`; `PadKeys mapPad(const pad::State &)`; `class PauseEdge { bool update(bool held); void arm(bool held); }`.
  - `core/gb_session.h`: `kIdlePauseMs = 300000`, `kPausedExitMs = 600000`, `kLowBattPct = 15`, `kExitBattPct = 5`; `enum class Phase { Off, Playing, Paused }`; `enum class ExitWhy : uint8_t { None, User, Rest, PausedTooLong, Battery, Switch, Fault }`; `const char *pauseKey(PauseWhy)`, `const char *exitKey(ExitWhy)`; `struct SessionIn { bool padReady; uint32_t permSeq; int battPct; bool usb; bool battCritical; }`; `struct Verdict { enum Act : uint8_t { None, Pause, Exit } act; PauseWhy pause; ExitWhy exit; bool lowBand; }`; `class Session { void start(Ms now, uint32_t permSeq); void stop(); Phase phase() const; bool active() const; PauseWhy pauseWhy() const; void input(Ms now); void pause(PauseWhy, Ms now); bool resume(const SessionIn &, Ms now); Verdict update(const SessionIn &, Ms now); }`.

Regole (spec §4, decise qui dove la spec tace):
- Mappatura per etichetta: A → A, B → B, Menu (≡) → Start, View (⧉) → Select, croce o levetta sinistra oltre ±50 → direzioni (Y negativo = su, come lo spike). Direzioni opposte insieme si annullano (non esistono su una croce vera; alcuni giochi si bloccano).
- **Pausa**: Start+Select insieme, oppure il tasto Xbox; la combinazione non arriva al gioco. Conta solo il fronte (tenerla premuta dopo Riprendi non riapre la pausa: `PauseEdge::arm`).
- **Permesso**: pausa sul **fronte** di una richiesta nuova (`permSeq` cresce), non finché la richiesta resta aperta: si può riprendere senza rispondere (con «Annulla · resta in attesa» la richiesta resta aperta come sempre, ISTRUZIONI §7). Una richiesta che arriva a partita già in pausa la apre il loop (Task 14).
- **Batteria**: senza cavo, `battCritical` (la soglia del sonno profondo, ~10 %) o < 5 % → salva ed esce; il gioco esce **prima** che `powerLoop` addormenti la scheda. Percentuale sconosciuta (−1): nessuna regola.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_session/test_main.cpp`:

```cpp
// Regole della partita (core/gb_session) e tasti dal controller (core/gb_input).
#include <unity.h>

#include "core/gb_emu.h"
#include "core/gb_input.h"
#include "core/gb_session.h"

using namespace cal;
using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {
pad::State st(uint16_t buttons, int lx = 0, int ly = 0) {
  pad::State s = {};
  s.buttons = buttons;
  s.lx = int8_t(lx);
  s.ly = int8_t(ly);
  return s;
}
SessionIn ok() { return SessionIn{true, 0, 80, false, false}; }
}  // namespace

// ---------- tasti ----------
void test_map_buttons() {
  TEST_ASSERT_EQUAL_HEX8(KeyA | KeyStart, mapPad(st(pad::A | pad::Menu)).keys);
  TEST_ASSERT_EQUAL_HEX8(KeyB | KeySelect, mapPad(st(pad::B | pad::View)).keys);
  TEST_ASSERT_EQUAL_HEX8(KeyUp | KeyLeft, mapPad(st(pad::Up | pad::Left)).keys);
  TEST_ASSERT_EQUAL_HEX8(0, mapPad(st(pad::X | pad::Y | pad::LB | pad::RB)).keys);
}

void test_map_stick_with_deadzone() {
  TEST_ASSERT_EQUAL_HEX8(0, mapPad(st(0, 50, -50)).keys);           // metà corsa: niente
  TEST_ASSERT_EQUAL_HEX8(KeyRight | KeyUp, mapPad(st(0, 51, -51)).keys);  // Y negativo = su
  TEST_ASSERT_EQUAL_HEX8(KeyDown | KeyLeft, mapPad(st(0, -100, 100)).keys);
}

void test_opposite_directions_cancel() {
  TEST_ASSERT_EQUAL_HEX8(0, mapPad(st(pad::Up, 0, 100)).keys);  // croce su + levetta giù
  TEST_ASSERT_EQUAL_HEX8(KeyA, mapPad(st(pad::A | pad::Left | pad::Right)).keys);
}

void test_pause_chord_not_forwarded() {
  PadKeys k = mapPad(st(pad::Menu | pad::View | pad::A));
  TEST_ASSERT_TRUE(k.pause);
  TEST_ASSERT_EQUAL_HEX8(0, k.keys);
  k = mapPad(st(pad::Guide));
  TEST_ASSERT_TRUE(k.pause);
  TEST_ASSERT_FALSE(mapPad(st(pad::Menu)).pause);
}

void test_pause_edge() {
  PauseEdge e;
  TEST_ASSERT_TRUE(e.update(true));
  TEST_ASSERT_FALSE(e.update(true));  // ancora tenuta
  TEST_ASSERT_FALSE(e.update(false));
  e.arm(true);                         // Riprendi con la combinazione ancora giù
  TEST_ASSERT_FALSE(e.update(true));
}

// ---------- regole ----------
void test_controller_lost_pauses_immediately() {
  Session s;
  s.start(0, 0);
  SessionIn in = ok();
  in.padReady = false;
  const Verdict v = s.update(in, 10);
  TEST_ASSERT_EQUAL(int(Verdict::Pause), int(v.act));
  TEST_ASSERT_EQUAL(int(PauseWhy::Controller), int(v.pause));
  TEST_ASSERT_EQUAL(int(Phase::Paused), int(s.phase()));
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 20).act));  // una volta sola
  TEST_ASSERT_FALSE(s.resume(in, 30));                               // senza controller non si riprende
  in.padReady = true;
  TEST_ASSERT_TRUE(s.resume(in, 40));
}

void test_idle_five_minutes() {
  Session s;
  s.start(0, 0);
  s.input(60000);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(ok(), 60000 + kIdlePauseMs - 1).act));
  const Verdict v = s.update(ok(), 60000 + kIdlePauseMs);
  TEST_ASSERT_EQUAL(int(PauseWhy::Idle), int(v.pause));
}

void test_new_perm_pauses_once() {
  Session s;
  s.start(0, 3);  // tre richieste arrivate prima della partita: non contano
  SessionIn in = ok();
  in.permSeq = 3;
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 1).act));
  in.permSeq = 4;
  TEST_ASSERT_EQUAL(int(PauseWhy::Perm), int(s.update(in, 5).pause));
  TEST_ASSERT_TRUE(s.resume(in, 6));  // si riprende anche senza rispondere: la richiesta resta aperta
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 7).act));
  in.permSeq = 5;
  TEST_ASSERT_EQUAL(int(PauseWhy::Perm), int(s.update(in, 8).pause));
}

void test_perm_while_paused_is_not_lost() {
  Session s;
  s.start(0, 0);
  s.pause(PauseWhy::User, 1);
  SessionIn in = ok();
  in.permSeq = 1;  // arriva in pausa: niente da fare, è già in pausa
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 2).act));
  TEST_ASSERT_TRUE(s.resume(in, 3));
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 4).act));
}

void test_paused_ten_minutes_exits() {
  Session s;
  s.start(0, 0);
  s.pause(PauseWhy::User, 1000);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(ok(), 1000 + kPausedExitMs - 1).act));
  const Verdict v = s.update(ok(), 1000 + kPausedExitMs);
  TEST_ASSERT_EQUAL(int(Verdict::Exit), int(v.act));
  TEST_ASSERT_EQUAL(int(ExitWhy::PausedTooLong), int(v.exit));
}

void test_battery_band_and_exit() {
  Session s;
  s.start(0, 0);
  SessionIn in = ok();
  in.battPct = 15;
  TEST_ASSERT_FALSE(s.update(in, 1).lowBand);
  in.battPct = 14;
  Verdict v = s.update(in, 2);
  TEST_ASSERT_TRUE(v.lowBand);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(v.act));
  in.battPct = 4;
  v = s.update(in, 3);
  TEST_ASSERT_EQUAL(int(ExitWhy::Battery), int(v.exit));
}

void test_battery_critical_exits_even_paused_and_usb_wins() {
  Session s;
  s.start(0, 0);
  s.pause(PauseWhy::User, 1);
  SessionIn in = ok();
  in.battCritical = true;  // la scheda andrebbe a dormire: prima si salva
  TEST_ASSERT_EQUAL(int(ExitWhy::Battery), int(s.update(in, 2).exit));
  in.usb = true;
  in.battPct = 2;
  const Verdict v = s.update(in, 3);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(v.act));
  TEST_ASSERT_FALSE(v.lowBand);
}

void test_unknown_battery_never_exits() {
  Session s;
  s.start(0, 0);
  SessionIn in = ok();
  in.battPct = -1;
  const Verdict v = s.update(in, 1);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(v.act));
  TEST_ASSERT_FALSE(v.lowBand);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_map_buttons);
  RUN_TEST(test_map_stick_with_deadzone);
  RUN_TEST(test_opposite_directions_cancel);
  RUN_TEST(test_pause_chord_not_forwarded);
  RUN_TEST(test_pause_edge);
  RUN_TEST(test_controller_lost_pauses_immediately);
  RUN_TEST(test_idle_five_minutes);
  RUN_TEST(test_new_perm_pauses_once);
  RUN_TEST(test_perm_while_paused_is_not_lost);
  RUN_TEST(test_paused_ten_minutes_exits);
  RUN_TEST(test_battery_band_and_exit);
  RUN_TEST(test_battery_critical_exits_even_paused_and_usb_wins);
  RUN_TEST(test_unknown_battery_never_exits);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_session`
Expected: FAIL in compilazione, `fatal error: core/gb_input.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `firmware/src/core/gb_model.h`:

```cpp
// Giochi Game Boy: la parte del modello che l'interfaccia mostra (Menu › Gioco › ROM, pausa, caricamento, bande).
// Senza JSON né hardware: la includono model.h e i test.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "clock.h"

namespace cal {
namespace gb {

const size_t kIdLen = 17;     // 16 cifre esadecimali minuscole (primi 8 byte dello SHA-256 della ROM)
const size_t kTitleLen = 33;  // titoli del PC: 1–16 ASCII stampabili (contratto); 32 per quelli dell'intestazione
const int kMaxGames = 16;     // Menu › Gioco › ROM (kMaxItems 22): gb-list ne porta fino a 64, le altre si contano (registro)

struct GameEntry {
  char id[kIdLen];
  char title[kTitleLen];
  uint32_t size;
};

enum class LoadStep : uint8_t { Download, Verify, Start };
enum class PauseWhy : uint8_t { None, User, Controller, Idle, Perm };

// Richieste del menu al loop (Ui::takeGbAct): il loop le passa al runtime del gioco o alla rete.
enum class GbAct : uint8_t { None, Pick, Resume, Exit, ExitRest, Profile, Cancel, Pause };

}  // namespace gb

struct GbView {
  gb::GameEntry games[gb::kMaxGames];  // gb-list: giochi sul PC
  int nGames;
  bool listSeen;                       // almeno un gb-list da questo avvio
  bool partOk;                         // partizione «gbrom» presente (tabella nuova, flash via cavo fatto)
  char installed[gb::kIdLen];          // gioco verificato nella partizione ('' = nessuno)
  char current[gb::kIdLen];            // in caricamento, in gioco o in pausa ('' = nessuno)
  char title[gb::kTitleLen];           // del gioco corrente
  gb::LoadStep step;
  uint32_t done, total;                // download
  int drawnPct;
  Ms drawnAt;
  gb::PauseWhy pause;
  bool smooth;                         // Profilo: false = Pulito (F8 N10), true = Fluido (F4 N6)
  bool lowBand;                        // batteria < 15 % senza cavo: avviso nella banda
  char installedTitle[gb::kTitleLen];  // titolo del gioco nella partizione (intestazione), per giocare senza PC
};

}  // namespace cal
```

Create `firmware/src/core/gb_input.h`:

```cpp
// Giochi Game Boy: dal controller (cal::pad, piano A) ai tasti del Game Boy. Provata in native (test_gb_input).
//   A → A, B → B, Menu (≡) → Start, View (⧉) → Select, croce o levetta sinistra (oltre metà corsa) → direzioni.
//   Start+Select insieme, o il tasto Xbox, = pausa: non arrivano al gioco.
#pragma once
#include <stdint.h>

#include "pad.h"

namespace cal {
namespace gb {

const int kStickDead = 50;  // levetta: −100..100, direzione oltre ±50

struct PadKeys {
  uint8_t keys;  // gb::Key premuti
  bool pause;    // combinazione di pausa tenuta adesso
};
PadKeys mapPad(const pad::State &s);

// Pausa sul fronte: vera solo nel giro in cui la combinazione comincia (tenerla non riapre la pausa dopo Riprendi).
class PauseEdge {
 public:
  bool update(bool held) {
    const bool fire = held && !held_;
    held_ = held;
    return fire;
  }
  void arm(bool held) { held_ = held; }  // dopo Riprendi: una combinazione ancora tenuta non conta

 private:
  bool held_ = false;
};

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_input.cpp`:

```cpp
#include "gb_input.h"

#include "gb_emu.h"

namespace cal {
namespace gb {

PadKeys mapPad(const pad::State &s) {
  const uint16_t b = s.buttons;
  const bool chord = (b & pad::Menu) && (b & pad::View);
  PadKeys k = {0, chord || (b & pad::Guide) != 0};
  if (k.pause) return k;  // in pausa al gioco non arriva niente
  if (b & pad::A) k.keys |= KeyA;
  if (b & pad::B) k.keys |= KeyB;
  if (b & pad::Menu) k.keys |= KeyStart;
  if (b & pad::View) k.keys |= KeySelect;
  if ((b & pad::Up) || s.ly < -kStickDead) k.keys |= KeyUp;
  if ((b & pad::Down) || s.ly > kStickDead) k.keys |= KeyDown;
  if ((b & pad::Left) || s.lx < -kStickDead) k.keys |= KeyLeft;
  if ((b & pad::Right) || s.lx > kStickDead) k.keys |= KeyRight;
  // Su e giù (o sinistra e destra) insieme non esistono su una croce vera: alcuni giochi si bloccano.
  if ((k.keys & KeyUp) && (k.keys & KeyDown)) k.keys &= uint8_t(~(KeyUp | KeyDown));
  if ((k.keys & KeyLeft) && (k.keys & KeyRight)) k.keys &= uint8_t(~(KeyLeft | KeyRight));
  return k;
}

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_session.h`:

```cpp
// Giochi Game Boy: regole della partita (spec §4), logica pura provata in native (test_gb_session).
//   controller scollegato → pausa subito; 5 min senza tasti → pausa; richiesta di permesso nuova → pausa (una volta:
//   si può riprendere anche senza rispondere, la richiesta resta aperta come sempre);
//   10 min in pausa → salva, esce e torna al Riposo; batteria < 15 % senza cavo → avviso nella banda;
//   < 5 % o batteria critica senza cavo → salva ed esce.
#pragma once
#include <stdint.h>

#include "clock.h"
#include "gb_model.h"

namespace cal {
namespace gb {

const Ms kIdlePauseMs = 5 * 60000;
const Ms kPausedExitMs = 10 * 60000;
const int kLowBattPct = 15;
const int kExitBattPct = 5;

enum class Phase : uint8_t { Off, Playing, Paused };
enum class ExitWhy : uint8_t { None, User, Rest, PausedTooLong, Battery, Switch, Fault };  // Rest: Riposo dalla pausa
const char *pauseKey(PauseWhy w);  // per il registro
const char *exitKey(ExitWhy w);

struct SessionIn {
  bool padReady;      // pad::Link::Ready
  uint32_t permSeq;   // richieste di permesso arrivate da quando la scheda è accesa (cresce e basta)
  int battPct;        // −1 = non si sa
  bool usb;           // cavo collegato
  bool battCritical;  // CriticalBattery: la scheda andrebbe in sonno profondo
};

struct Verdict {
  enum Act : uint8_t { None, Pause, Exit } act;
  PauseWhy pause;
  ExitWhy exit;
  bool lowBand;  // avviso «batteria bassa» nella banda
};

class Session {
 public:
  void start(Ms now, uint32_t permSeq);
  void stop() { phase_ = Phase::Off; why_ = PauseWhy::None; }
  Phase phase() const { return phase_; }
  bool active() const { return phase_ != Phase::Off; }
  PauseWhy pauseWhy() const { return why_; }
  void input(Ms now) { lastInput_ = now; }  // un tasto del gioco cambiato
  void pause(PauseWhy w, Ms now);           // da Playing; in pausa non cambia il motivo
  // Riprendi: solo con il controller pronto. false = resta in pausa.
  bool resume(const SessionIn &in, Ms now);
  // Da chiamare a ogni giro: decide pausa o uscita (una volta: la pausa porta Phase a Paused).
  Verdict update(const SessionIn &in, Ms now);

 private:
  Phase phase_ = Phase::Off;
  PauseWhy why_ = PauseWhy::None;
  Ms lastInput_ = 0, pausedAt_ = 0;
  uint32_t permSeq_ = 0;
};

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_session.cpp`:

```cpp
#include "gb_session.h"

namespace cal {
namespace gb {

const char *pauseKey(PauseWhy w) {
  switch (w) {
    case PauseWhy::None: return "nessuna";
    case PauseWhy::User: return "utente";
    case PauseWhy::Controller: return "controller scollegato";
    case PauseWhy::Idle: return "5 min senza tasti";
    case PauseWhy::Perm: return "permesso";
  }
  return "?";
}

const char *exitKey(ExitWhy w) {
  switch (w) {
    case ExitWhy::None: return "nessuna";
    case ExitWhy::User: return "salva ed esci";
    case ExitWhy::Rest: return "salva ed esci, poi Riposo";
    case ExitWhy::PausedTooLong: return "10 min in pausa";
    case ExitWhy::Battery: return "batteria";
    case ExitWhy::Switch: return "altro gioco";
    case ExitWhy::Fault: return "errore dell'emulatore";
  }
  return "?";
}

void Session::start(Ms now, uint32_t permSeq) {
  phase_ = Phase::Playing;
  permSeq_ = permSeq;
  why_ = PauseWhy::None;
  lastInput_ = now;
  pausedAt_ = 0;
}

void Session::pause(PauseWhy w, Ms now) {
  if (phase_ != Phase::Playing) return;
  phase_ = Phase::Paused;
  why_ = w;
  pausedAt_ = now;
}

bool Session::resume(const SessionIn &in, Ms now) {
  if (phase_ != Phase::Paused || !in.padReady) return false;
  phase_ = Phase::Playing;
  why_ = PauseWhy::None;
  lastInput_ = now;
  return true;
}

Verdict Session::update(const SessionIn &in, Ms now) {
  Verdict v = {Verdict::None, PauseWhy::None, ExitWhy::None, false};
  if (phase_ == Phase::Off) return v;
  const bool onBattery = !in.usb;
  v.lowBand = onBattery && in.battPct >= 0 && in.battPct < kLowBattPct;
  if (onBattery && (in.battCritical || (in.battPct >= 0 && in.battPct < kExitBattPct))) {
    v.act = Verdict::Exit;
    v.exit = ExitWhy::Battery;
    return v;
  }
  const bool newPerm = in.permSeq != permSeq_;
  permSeq_ = in.permSeq;
  if (phase_ == Phase::Paused) {
    if (now - pausedAt_ >= kPausedExitMs) {
      v.act = Verdict::Exit;
      v.exit = ExitWhy::PausedTooLong;
    }
    return v;
  }
  PauseWhy w = PauseWhy::None;
  if (!in.padReady) w = PauseWhy::Controller;
  else if (newPerm) w = PauseWhy::Perm;
  else if (now - lastInput_ >= kIdlePauseMs) w = PauseWhy::Idle;
  if (w != PauseWhy::None) {
    pause(w, now);
    v.act = Verdict::Pause;
    v.pause = w;
  }
  return v;
}

}  // namespace gb
}  // namespace cal
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_gb_session`
Expected: 13 test PASSED.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core/gb_model.h firmware/src/core/gb_input.h firmware/src/core/gb_input.cpp \
  firmware/src/core/gb_session.h firmware/src/core/gb_session.cpp firmware/test/test_gb_session
git commit -m "Giochi: tasti dal controller e regole della partita"
```

---

### Task 7: Messaggi `gb-*`

**Prerequisito:** piano A (`core/pad.h`, per `pad::Link`).

**Files:**
- Create: `firmware/src/core/gb_proto.h`, `firmware/src/core/gb_proto.cpp`
- Modify: `firmware/src/core/apply_state.h` (enum `MsgType`), `firmware/src/core/apply_state.cpp` (`msgType`, `filter`)
- Test: `firmware/test/test_gb_proto/test_main.cpp`

**Interfaces:**
- Consumes: `gb_model.h` (Task 6), `cry::fromHex`, `cry::Msg`, `cry::mac16`, `cry::ctEqual`, `cry::Sha256Stream`, `cry::wipe` (`core/crypto.h`), `kProtoVersion`, `scopy`; dal piano A `pad::Link` e `pad::linkKey` (`core/pad.h`).
- Produces (`core/gb_proto.h`): `kTokenLen = 65`, `kRomMinBytes = 0x150`, `kRomMaxBytes = 0x200000`, `kListMax = 64`; `bool validGameId(const char *)`, `bool validToken(const char *)`, `bool validTitle(const char *)`; `struct ListMsg { GameEntry games[kMaxGames]; int n; int dropped; }`, `bool listMac(const uint8_t SK[32], JsonVariantConst root, uint8_t out[16])`, `bool checkList(JsonVariantConst root, bool authed, const uint8_t SK[32], const char **why)`, `bool decodeList(JsonVariantConst, ListMsg &)`; `struct StartMsg { char id[17]; uint32_t size; char sha256[65]; uint8_t sha[32]; char token[65]; char saveToken[65]; bool hasMac; uint8_t mac[16]; }`, `bool decodeStart(JsonVariantConst, StartMsg &, const char **why)`, `bool startMac(const uint8_t SK[32], const StartMsg &, uint8_t out[16])`, `bool checkStart(const StartMsg &, bool authed, const uint8_t SK[32], const char **why)`; `struct SaveOk { char id[17]; uint32_t crc32; bool hasMac; uint8_t mac[16]; }`, `bool decodeSaveOk(JsonVariantConst, SaveOk &)`, `bool saveOkMac(const uint8_t SK[32], const SaveOk &, uint8_t out[16])`, `bool checkSaveOk(const SaveOk &, bool authed, const uint8_t SK[32], const char **why)`; `size_t encodePick(id, out, cap)`, `encodeSave(id, size, crc32, token, out, cap)`, `encodeState(playingId, out, cap)`, `encodePad(pad::Link, out, cap)` (testo dello stato con `pad::linkKey` del piano A: niente doppione qui). In `MsgType`: `GbList`, `GbStart`, `GbSaveOk`.

Formati (contratto, «B ↔ C»), con le precisazioni del lato scheda:
- `id` = esattamente 16 cifre esadecimali **minuscole**; in `gb-start` deve essere uguale ai primi 16 caratteri di `sha256` (controllo di coerenza).
- `size` della ROM 0x150–0x200000. `token`, `saveToken`: 1–64 caratteri `[A-Za-z0-9_-]` (finiscono in un URL).
- **mac obbligatori** sui tre messaggi del servizio (HMAC-SHA256 con la SK troncato a 16 byte, `␟` = 0x1F, interi in decimale): `gb-list` = `"gb-list" ␟ id₁ ␟ size₁ ␟ title₁ ␟ …` (elenco vuoto: `"gb-list"`), `gb-start` = `"gb-start" ␟ id ␟ size ␟ sha256 ␟ token ␟ saveToken` (saveToken assente = campo vuoto), `gb-save-ok` = `"gb-save-ok" ␟ id ␟ crc32`. Senza mac o con mac sbagliato il messaggio si scarta con una riga di registro. Vettori: quelli di `gb-list` e `gb-save-ok` sono gli stessi del servizio (contratto; `protocol/examples/gb-*.json` del piano C), più l'esempio `gb-start-ripristino.json`; il vettore proprio di `gb-start` (SK = 32 × 0x02, id `0123456789abcdef`, size 32768, sha `kSha`, token `kTok`, nessun saveToken) è stato **ricalcolato con Python `hmac`** sulla stringa nuova: `1fc0ebabc1688237acadfd50d8c512fe` (il precedente `e1998d39…` era della forma senza size e saveToken).
- Il mac di `gb-list` si calcola a pezzi (`MacStream` sopra `cry::Sha256Stream`): 64 giochi fanno fino a ~2,7 KB, oltre i 512 byte di `cry::Msg`.
- `gb-list`: più di 64 voci → messaggio scartato; ogni voce con id, titolo e dimensione validi (titolo ASCII stampabile 1–16 caratteri) entra, le altre si contano in `dropped`; oltre 16 (`kMaxGames`, la capienza di Menu › Gioco › ROM) si contano in `dropped` (registro).
- `crc32` è un intero u32 (come `avatarCrc` in PROTOCOLLO.md), CRC-32 IEEE.
- **Filtro**: `parseMessage()` deserializza con un filtro che tiene solo i campi noti; senza le chiavi nuove (`games`, `size`, `sha256`, `token`, `saveToken`, `crc32`) i messaggi arriverebbero vuoti. Per questo i test passano da `parseMessage`, come la scheda.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_proto/test_main.cpp`:

```cpp
// Messaggi dei giochi (core/gb_proto): gb-list, gb-start e gb-save-ok con i mac obbligatori (vettori calcolati con
// Python hmac, gli stessi del servizio dove indicato) e i quattro messaggi della scheda.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "core/apply_state.h"
#include "core/crypto.h"
#include "core/gb_proto.h"

using namespace cal;
using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {
// Come sulla scheda: parseMessage con il filtro dei campi (un campo che il filtro non conosce sparisce).
JsonDocument doc(const char *js) {
  JsonDocument d;
  TEST_ASSERT_TRUE_MESSAGE(parseMessage(js, strlen(js), d), js);
  return d;
}
const char *kSha = "0123456789abcdef000000000000000000000000000000000000000000000000";
const char *kTok = "00112233445566778899aabbccddeeff";
}  // namespace

void test_msg_types() {
  TEST_ASSERT_EQUAL(int(MsgType::GbList), int(msgType(doc("{\"t\":\"gb-list\",\"v\":1,\"games\":[]}").as<JsonVariantConst>())));
  TEST_ASSERT_EQUAL(int(MsgType::GbStart), int(msgType(doc("{\"t\":\"gb-start\",\"v\":1}").as<JsonVariantConst>())));
  TEST_ASSERT_EQUAL(int(MsgType::GbSaveOk), int(msgType(doc("{\"t\":\"gb-save-ok\",\"v\":1}").as<JsonVariantConst>())));
}

void test_ids_and_tokens() {
  TEST_ASSERT_TRUE(validGameId("0123456789abcdef"));
  TEST_ASSERT_FALSE(validGameId("0123456789ABCDEF"));
  TEST_ASSERT_FALSE(validGameId("0123456789abcde"));
  TEST_ASSERT_FALSE(validGameId(nullptr));
  TEST_ASSERT_TRUE(validToken("a-b_C9"));
  TEST_ASSERT_FALSE(validToken(""));
  TEST_ASSERT_FALSE(validToken("../x"));
  char big[80];
  memset(big, 'a', 65);
  big[65] = 0;
  TEST_ASSERT_FALSE(validToken(big));
  TEST_ASSERT_TRUE(validTitle("POKEMON GOLD"));
  TEST_ASSERT_TRUE(validTitle("ABCDEFGHIJKLMNOP"));    // 16
  TEST_ASSERT_FALSE(validTitle("ABCDEFGHIJKLMNOPQ"));  // 17
  TEST_ASSERT_FALSE(validTitle(""));
  TEST_ASSERT_FALSE(validTitle("riga\nnuova"));
  TEST_ASSERT_FALSE(validTitle("citt\xc3\xa0"));  // solo ASCII stampabile
  TEST_ASSERT_FALSE(validTitle(nullptr));
}

void test_list_keeps_only_valid_entries() {
  JsonDocument d = doc(
      "{\"t\":\"gb-list\",\"v\":1,\"games\":["
      "{\"id\":\"0123456789abcdef\",\"title\":\"TETRIS\",\"size\":32768},"
      "{\"id\":\"BAD\",\"title\":\"x\",\"size\":32768},"
      "{\"id\":\"fedcba9876543210\",\"title\":\"riga\\nnuova\",\"size\":1048576},"
      "{\"id\":\"00000000000000aa\",\"title\":\"ABCDEFGHIJKLMNOPQ\",\"size\":65536},"
      "{\"id\":\"00000000000000bb\",\"title\":\"PICCOLA\",\"size\":335},"
      "{\"id\":\"00000000000000cc\",\"title\":\"Puzzle 2\",\"size\":2097152}]}");
  ListMsg m;
  TEST_ASSERT_TRUE(decodeList(d.as<JsonVariantConst>(), m));
  TEST_ASSERT_EQUAL(2, m.n);
  TEST_ASSERT_EQUAL(4, m.dropped);
  TEST_ASSERT_EQUAL_STRING("TETRIS", m.games[0].title);
  TEST_ASSERT_EQUAL_STRING("Puzzle 2", m.games[1].title);
  TEST_ASSERT_EQUAL_UINT32(2097152, m.games[1].size);
}

void test_list_caps_at_max() {
  JsonDocument d;
  d["t"] = "gb-list";
  d["v"] = 1;
  JsonArray a = d["games"].to<JsonArray>();
  for (int i = 0; i < kMaxGames + 3; i++) {
    char id[17];
    snprintf(id, sizeof id, "%016x", i + 1);
    JsonObject g = a.add<JsonObject>();
    g["id"] = id;
    g["title"] = "G";
    g["size"] = 32768;
  }
  ListMsg m;
  TEST_ASSERT_TRUE(decodeList(d.as<JsonVariantConst>(), m));
  TEST_ASSERT_EQUAL(kMaxGames, m.n);
  TEST_ASSERT_EQUAL(3, m.dropped);
}

// Vettori del servizio (protocol/examples/gb-list.json, piano C): stessa stringa, stesso mac.
void test_list_mac_vectors() {
  uint8_t SK[32];
  memset(SK, 0x02, sizeof SK);
  const char *why = "";
  JsonDocument d = doc(
      "{\"t\":\"gb-list\",\"v\":1,\"games\":["
      "{\"id\":\"3b5f0c9a1d2e4f60\",\"title\":\"HOMEBREW DEMO\",\"size\":32768},"
      "{\"id\":\"a1b2c3d4e5f60718\",\"title\":\"Puzzle\",\"size\":65536}],"
      "\"mac\":\"f6d53e39660c529c836fafb597a847e8\"}");
  TEST_ASSERT_TRUE_MESSAGE(checkList(d.as<JsonVariantConst>(), true, SK, &why), why);
  TEST_ASSERT_FALSE(checkList(d.as<JsonVariantConst>(), false, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-list senza autenticazione", why);
  JsonDocument e = doc("{\"t\":\"gb-list\",\"v\":1,\"games\":[],\"mac\":\"c41604f236b545be2f894c2bb645b6e1\"}");
  TEST_ASSERT_TRUE_MESSAGE(checkList(e.as<JsonVariantConst>(), true, SK, &why), why);
  JsonDocument f = doc("{\"t\":\"gb-list\",\"v\":1,\"games\":[]}");
  TEST_ASSERT_FALSE(checkList(f.as<JsonVariantConst>(), true, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-list senza mac", why);
  JsonDocument g = doc("{\"t\":\"gb-list\",\"v\":1,\"games\":[],\"mac\":\"c41604f236b545be2f894c2bb645b6e0\"}");
  TEST_ASSERT_FALSE(checkList(g.as<JsonVariantConst>(), true, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-list con mac sbagliato", why);
}

void test_list_over_64_rejected() {
  JsonDocument d;
  d["t"] = "gb-list";
  d["v"] = 1;
  JsonArray a = d["games"].to<JsonArray>();
  for (int i = 0; i < kListMax + 1; i++) {
    char id[17];
    snprintf(id, sizeof id, "%016x", i + 1);
    JsonObject g = a.add<JsonObject>();
    g["id"] = id;
    g["title"] = "G";
    g["size"] = 32768;
  }
  d["mac"] = "00000000000000000000000000000000";
  uint8_t SK[32] = {0};
  const char *why = "";
  TEST_ASSERT_FALSE(checkList(d.as<JsonVariantConst>(), true, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-list oltre 64 giochi", why);
}

void test_start_valid_with_mac_vector() {
  char js[400];
  snprintf(js, sizeof js,
           "{\"t\":\"gb-start\",\"v\":1,\"id\":\"0123456789abcdef\",\"size\":32768,\"sha256\":\"%s\",\"token\":\"%s\","
           "\"mac\":\"1fc0ebabc1688237acadfd50d8c512fe\"}",  // ricalcolato con Python hmac (forma con size e saveToken)
           kSha, kTok);
  JsonDocument d = doc(js);
  StartMsg m;
  const char *why = "";
  TEST_ASSERT_TRUE_MESSAGE(decodeStart(d.as<JsonVariantConst>(), m, &why), why);
  TEST_ASSERT_TRUE(m.hasMac);
  TEST_ASSERT_EQUAL_STRING("", m.saveToken);
  uint8_t SK[32];
  memset(SK, 0x02, sizeof SK);
  TEST_ASSERT_TRUE_MESSAGE(checkStart(m, true, SK, &why), why);
  TEST_ASSERT_FALSE(checkStart(m, false, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-start senza autenticazione", why);
  m.mac[0] ^= 1;
  TEST_ASSERT_FALSE(checkStart(m, true, SK, &why));
}

// Il mac è obbligatorio e copre anche saveToken (e size).
void test_start_mac_required_and_covers_save_token() {
  char js[400];
  snprintf(js, sizeof js,
           "{\"t\":\"gb-start\",\"v\":1,\"id\":\"0123456789abcdef\",\"size\":32768,\"sha256\":\"%s\",\"token\":\"%s\","
           "\"saveToken\":\"s1\"}",
           kSha, kTok);
  JsonDocument d = doc(js);
  StartMsg m;
  const char *why = "";
  TEST_ASSERT_TRUE(decodeStart(d.as<JsonVariantConst>(), m, &why));
  TEST_ASSERT_EQUAL_STRING("s1", m.saveToken);
  uint8_t SK[32];
  memset(SK, 0x02, sizeof SK);
  TEST_ASSERT_FALSE(checkStart(m, true, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-start senza mac", why);
  TEST_ASSERT_TRUE(cry::fromHex("1fc0ebabc1688237acadfd50d8c512fe", m.mac, 16));  // mac dello stesso avvio senza saveToken
  m.hasMac = true;
  TEST_ASSERT_FALSE(checkStart(m, true, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-start con mac sbagliato", why);
  m.saveToken[0] = 0;
  TEST_ASSERT_TRUE_MESSAGE(checkStart(m, true, SK, &why), why);
  m.size = 65536;
  TEST_ASSERT_FALSE(checkStart(m, true, SK, &why));
}

// Stesso vettore del servizio (protocol/examples/gb-start-ripristino.json, piano C).
void test_start_service_vector() {
  JsonDocument d = doc(
      "{\"t\":\"gb-start\",\"v\":1,\"id\":\"3b5f0c9a1d2e4f60\",\"size\":32768,"
      "\"sha256\":\"3b5f0c9a1d2e4f607c1e9d22a8b04f3165e0d7a9c2b18f4e5d6a7b8c9d0e1f20\","
      "\"token\":\"5d2c8e1f0a9b4c3d6e7f8091a2b3c4d5\",\"saveToken\":\"9e8d7c6b5a4f3e2d1c0b0a9988776655\","
      "\"mac\":\"d57133ff50a083ee56d6005253591fff\"}");
  StartMsg m;
  const char *why = "";
  TEST_ASSERT_TRUE_MESSAGE(decodeStart(d.as<JsonVariantConst>(), m, &why), why);
  uint8_t SK[32];
  memset(SK, 0x02, sizeof SK);
  TEST_ASSERT_TRUE_MESSAGE(checkStart(m, true, SK, &why), why);
}

void test_start_rejections() {
  struct Case { const char *js; const char *why; } cases[] = {
      {"{\"id\":\"0123456789abcdef\",\"size\":32768,\"sha256\":\"ff23456789abcdef000000000000000000000000000000000000000000000000\",\"token\":\"t\"}",
       "id diverso dallo sha256"},
      {"{\"id\":\"0123456789abcdef\",\"size\":2097153,\"sha256\":\"0123456789abcdef000000000000000000000000000000000000000000000000\",\"token\":\"t\"}",
       "dimensione fuori dai limiti"},
      {"{\"id\":\"0123456789abcdef\",\"size\":32768,\"sha256\":\"0123456789abcdef000000000000000000000000000000000000000000000000\",\"token\":\"a/b\"}",
       "token non valido"},
      {"{\"id\":\"0123456789abcdef\",\"sha256\":\"0123456789abcdef000000000000000000000000000000000000000000000000\",\"token\":\"t\"}",
       "size mancante"},
      {"{\"id\":\"0123456789abcdef\",\"size\":32768,\"sha256\":\"0123\",\"token\":\"t\"}", "sha256 non valido"},
  };
  for (const Case &c : cases) {
    JsonDocument d = doc(c.js);
    StartMsg m;
    const char *why = "";
    TEST_ASSERT_FALSE(decodeStart(d.as<JsonVariantConst>(), m, &why));
    TEST_ASSERT_EQUAL_STRING(c.why, why);
  }
}

void test_save_ok() {
  JsonDocument d = doc("{\"t\":\"gb-save-ok\",\"v\":1,\"id\":\"0123456789abcdef\",\"crc32\":4294967295}");
  SaveOk s;
  TEST_ASSERT_TRUE(decodeSaveOk(d.as<JsonVariantConst>(), s));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, s.crc32);
  TEST_ASSERT_FALSE(s.hasMac);
}

// Vettore del servizio (protocol/examples/gb-save-ok.json, piano C).
void test_save_ok_mac() {
  uint8_t SK[32];
  memset(SK, 0x02, sizeof SK);
  const char *why = "";
  JsonDocument d = doc(
      "{\"t\":\"gb-save-ok\",\"v\":1,\"id\":\"3b5f0c9a1d2e4f60\",\"crc32\":305419896,"
      "\"mac\":\"7f690a4c01c28ed6e0f773b54866f6ef\"}");
  SaveOk s;
  TEST_ASSERT_TRUE(decodeSaveOk(d.as<JsonVariantConst>(), s));
  TEST_ASSERT_TRUE_MESSAGE(checkSaveOk(s, true, SK, &why), why);
  TEST_ASSERT_FALSE(checkSaveOk(s, false, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-save-ok senza autenticazione", why);
  s.crc32++;
  TEST_ASSERT_FALSE(checkSaveOk(s, true, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-save-ok con mac sbagliato", why);
  JsonDocument e = doc("{\"t\":\"gb-save-ok\",\"v\":1,\"id\":\"3b5f0c9a1d2e4f60\",\"crc32\":305419896}");
  TEST_ASSERT_TRUE(decodeSaveOk(e.as<JsonVariantConst>(), s));
  TEST_ASSERT_FALSE(checkSaveOk(s, true, SK, &why));
  TEST_ASSERT_EQUAL_STRING("gb-save-ok senza mac", why);
}

void test_encoders() {
  char b[200];
  TEST_ASSERT_TRUE(encodePick("0123456789abcdef", b, sizeof b) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"gb-pick\",\"v\":1,\"id\":\"0123456789abcdef\"}", b);
  encodeSave("0123456789abcdef", 8192, 305419896, "tok", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"gb-save\",\"v\":1,\"id\":\"0123456789abcdef\",\"size\":8192,\"crc32\":305419896,\"token\":\"tok\"}", b);
  encodeState(nullptr, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"gb-state\",\"v\":1,\"playing\":null}", b);
  encodeState("0123456789abcdef", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"gb-state\",\"v\":1,\"playing\":\"0123456789abcdef\"}", b);
  encodePad(pad::Link::Connecting, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"gb-pad\",\"v\":1,\"link\":\"connecting\"}", b);
  TEST_ASSERT_EQUAL(0, int(encodePad(pad::Link::Ready, b, 10)));  // non sta: 0 e stringa vuota
  TEST_ASSERT_EQUAL_STRING("", b);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_msg_types);
  RUN_TEST(test_ids_and_tokens);
  RUN_TEST(test_list_keeps_only_valid_entries);
  RUN_TEST(test_list_caps_at_max);
  RUN_TEST(test_list_mac_vectors);
  RUN_TEST(test_list_over_64_rejected);
  RUN_TEST(test_start_valid_with_mac_vector);
  RUN_TEST(test_start_mac_required_and_covers_save_token);
  RUN_TEST(test_start_service_vector);
  RUN_TEST(test_start_rejections);
  RUN_TEST(test_save_ok);
  RUN_TEST(test_save_ok_mac);
  RUN_TEST(test_encoders);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_proto`
Expected: FAIL in compilazione, `core/gb_proto.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

In `firmware/src/core/apply_state.h`, nell'enum `MsgType`, dopo la riga `Game, Accessory, Anchors,`:

```cpp
  // Giochi Game Boy (core/gb_proto): servizio → display, solo da connessione autenticata
  GbList, GbStart, GbSaveOk,
```

In `firmware/src/core/apply_state.cpp`, in `msgType()`, dopo `if (streq(t, "anchors")) return MsgType::Anchors;`:

```cpp
  if (streq(t, "gb-list")) return MsgType::GbList;
  if (streq(t, "gb-start")) return MsgType::GbStart;
  if (streq(t, "gb-save-ok")) return MsgType::GbSaveOk;
```

e in `filter()`, subito prima del commento `// M6 (PROTOCOLLO.md, bye): …`:

```cpp
  // Giochi Game Boy (core/gb_proto): gb-start {id, size, sha256, token, saveToken, mac}, gb-save-ok {id, crc32},
  // gb-list {games: [{id, title, size}]}.
  for (const char *k : {"size", "sha256", "token", "saveToken", "crc32"}) f[k] = true;
  JsonObject g = f["games"].add<JsonObject>();
  for (const char *k : {"id", "title", "size"}) g[k] = true;
```

Create `firmware/src/core/gb_proto.h`:

```cpp
// Giochi Game Boy: messaggi del protocollo (PROTOCOLLO.md «Giochi», contratto dei piani giochi). Solo da e verso una
// connessione autenticata; decodifica e codifica pure, provate in native (test_gb_proto). Mac obbligatori, troncati a
// 16 byte, ␟ = 0x1F, interi in decimale; senza mac o con mac sbagliato il messaggio si scarta:
//   gb-list    servizio → display  {games:[{id, title, size}], mac}   mac = HMAC(SK, "gb-list" ␟ id ␟ size ␟ title ␟ …)
//   gb-start   servizio → display  {id, size, sha256, token, saveToken?, mac}
//              mac = HMAC(SK, "gb-start" ␟ id ␟ size ␟ sha256 ␟ token ␟ saveToken), saveToken vuoto se assente
//   gb-save-ok servizio → display  {id, crc32, mac}                    mac = HMAC(SK, "gb-save-ok" ␟ id ␟ crc32)
//   gb-pick, gb-save, gb-state, gb-pad   display → servizio, senza mac
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "gb_model.h"
#include "pad.h"

namespace cal {
namespace gb {

const size_t kTokenLen = 65;     // 1–64 caratteri [A-Za-z0-9_-]
const uint32_t kRomMinBytes = 0x150;     // fine dell'intestazione della cartuccia
const uint32_t kRomMaxBytes = 0x200000;  // partizione gbrom
const int kListMax = 64;                 // voci al massimo in un gb-list (contratto)

bool validGameId(const char *s);  // esattamente 16 cifre [0-9a-f]
bool validToken(const char *s);
bool validTitle(const char *s);   // 1–16 caratteri ASCII stampabili (0x20–0x7E)

struct ListMsg {
  GameEntry games[kMaxGames];
  int n;
  int dropped;  // voci scartate (non valide o oltre kMaxGames)
};
// mac di gb-list sulle voci così come arrivano (stringhe tali e quali, size in decimale). false = più di kListMax voci
// o una voce senza id, title o size intero (il mac non si può rifare).
bool listMac(const uint8_t SK[32], JsonVariantConst root, uint8_t out[16]);
// Autenticazione, al massimo kListMax voci, mac giusto. false → why. Da chiamare prima di decodeList.
bool checkList(JsonVariantConst root, bool authed, const uint8_t SK[32], const char **why);
bool decodeList(JsonVariantConst root, ListMsg &out);  // false = messaggio senza games

struct StartMsg {
  char id[kIdLen];
  uint32_t size;
  char sha256[65];
  uint8_t sha[32];
  char token[kTokenLen];
  char saveToken[kTokenLen];  // '' = nessun salvataggio da scaricare
  bool hasMac;
  uint8_t mac[16];
};
// false → why: motivo per il registro. Controlla anche che id = primi 16 caratteri di sha256.
bool decodeStart(JsonVariantConst root, StartMsg &out, const char **why);
bool startMac(const uint8_t SK[32], const StartMsg &m, uint8_t out[16]);
// Autenticazione + mac obbligatorio. false → why.
bool checkStart(const StartMsg &m, bool authed, const uint8_t SK[32], const char **why);

struct SaveOk {
  char id[kIdLen];
  uint32_t crc32;
  bool hasMac;
  uint8_t mac[16];
};
bool decodeSaveOk(JsonVariantConst root, SaveOk &out);
bool saveOkMac(const uint8_t SK[32], const SaveOk &m, uint8_t out[16]);
// Autenticazione + mac obbligatorio. false → why.
bool checkSaveOk(const SaveOk &m, bool authed, const uint8_t SK[32], const char **why);

size_t encodePick(const char *id, char *out, size_t cap);
size_t encodeSave(const char *id, uint32_t size, uint32_t crc32, const char *token, char *out, size_t cap);
size_t encodeState(const char *playingId, char *out, size_t cap);  // '' o nullptr = playing: null
size_t encodePad(pad::Link l, char *out, size_t cap);               // link = pad::linkKey(l) (piano A)

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_proto.cpp`:

```cpp
#include "gb_proto.h"

#include <stdio.h>
#include <string.h>

#include "apply_state.h"
#include "crypto.h"
#include "strbuf.h"

namespace cal {
namespace gb {

namespace {

size_t emit(JsonDocument &d, char *out, size_t cap) {
  if (measureJson(d) + 1 > cap) {
    if (cap) out[0] = 0;
    return 0;
  }
  return serializeJson(d, out, cap);
}

bool hexLower(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

// HMAC-SHA256 a pezzi sopra cry::Sha256Stream: un gb-list da 64 giochi supera i 512 byte di cry::Msg. La chiave
// (SK, 32 byte) sta in un blocco da 64: si completa con zeri, senza hash (RFC 2104).
class MacStream {
 public:
  explicit MacStream(const uint8_t key[32]) {
    memset(k_, 0, sizeof k_);
    memcpy(k_, key, 32);
    uint8_t pad[64];
    for (int i = 0; i < 64; i++) pad[i] = k_[i] ^ 0x36;
    in_.update(pad, sizeof pad);
  }
  ~MacStream() { cry::wipe(k_, sizeof k_); }
  MacStream &str(const char *s) {
    in_.update(reinterpret_cast<const uint8_t *>(s), strlen(s));
    return *this;
  }
  MacStream &sep() {
    const uint8_t u = 0x1F;
    in_.update(&u, 1);
    return *this;
  }
  MacStream &num(uint32_t v) {
    char b[11];
    snprintf(b, sizeof b, "%lu", static_cast<unsigned long>(v));
    return str(b);
  }
  void finish(uint8_t out[16]) {  // una volta sola
    uint8_t inner[32], full[32], pad[64];
    in_.finish(inner);
    for (int i = 0; i < 64; i++) pad[i] = k_[i] ^ 0x5C;
    cry::Sha256Stream o;
    o.update(pad, sizeof pad);
    o.update(inner, sizeof inner);
    o.finish(full);
    memcpy(out, full, 16);
  }

 private:
  uint8_t k_[64];
  cry::Sha256Stream in_;
};

}  // namespace

bool validGameId(const char *s) {
  if (!s || strlen(s) != kIdLen - 1) return false;
  for (const char *p = s; *p; p++)
    if (!hexLower(*p)) return false;
  return true;
}

bool validToken(const char *s) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (n < 1 || n > kTokenLen - 1) return false;
  for (const char *p = s; *p; p++) {
    const char c = *p;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

bool validTitle(const char *s) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (n < 1 || n > 16) return false;
  for (const char *p = s; *p; p++) {
    const uint8_t c = static_cast<uint8_t>(*p);  // char con o senza segno: stesso risultato
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

bool listMac(const uint8_t SK[32], JsonVariantConst root, uint8_t out[16]) {
  JsonArrayConst a = root["games"].as<JsonArrayConst>();
  if (a.isNull() || a.size() > size_t(kListMax)) return false;
  MacStream h(SK);
  h.str("gb-list");
  for (JsonVariantConst g : a) {
    const char *id = g["id"].as<const char *>();
    const char *title = g["title"].as<const char *>();
    if (!id || !title || !g["size"].is<uint32_t>()) return false;
    h.sep().str(id).sep().num(g["size"].as<uint32_t>()).sep().str(title);
  }
  h.finish(out);
  return true;
}

bool checkList(JsonVariantConst root, bool authed, const uint8_t SK[32], const char **why) {
  if (!authed) { *why = "gb-list senza autenticazione"; return false; }
  uint8_t got[16], want[16];
  const char *mac = root["mac"].as<const char *>();
  if (!mac || !cry::fromHex(mac, got, 16)) { *why = "gb-list senza mac"; return false; }
  JsonArrayConst a = root["games"].as<JsonArrayConst>();
  if (a.isNull()) { *why = "gb-list senza games"; return false; }
  if (a.size() > size_t(kListMax)) { *why = "gb-list oltre 64 giochi"; return false; }
  if (!listMac(SK, root, want)) { *why = "gb-list non valido"; return false; }
  if (!cry::ctEqual(want, got, 16)) { *why = "gb-list con mac sbagliato"; return false; }
  return true;
}

bool decodeList(JsonVariantConst root, ListMsg &out) {
  memset(&out, 0, sizeof out);
  JsonArrayConst a = root["games"].as<JsonArrayConst>();
  if (a.isNull()) return false;
  for (JsonVariantConst g : a) {
    const char *id = g["id"].as<const char *>();
    const char *title = g["title"].as<const char *>();
    const uint32_t size = g["size"].as<uint32_t>();
    if (!validGameId(id) || !validTitle(title) || !g["size"].is<uint32_t>() || size < kRomMinBytes ||
        size > kRomMaxBytes || out.n >= kMaxGames) {
      out.dropped++;
      continue;
    }
    GameEntry &e = out.games[out.n++];
    scopy(e.id, id);
    scopy(e.title, title);
    e.size = size;
  }
  return true;
}

bool decodeStart(JsonVariantConst root, StartMsg &out, const char **why) {
  memset(&out, 0, sizeof out);
  const char *id = root["id"].as<const char *>();
  const char *sha = root["sha256"].as<const char *>();
  const char *tok = root["token"].as<const char *>();
  const char *saveTok = root["saveToken"].as<const char *>();
  if (!validGameId(id)) { *why = "id non valido"; return false; }
  if (!sha || strlen(sha) != 64 || !cry::fromHex(sha, out.sha, 32)) { *why = "sha256 non valido"; return false; }
  if (strncmp(sha, id, 16) != 0) { *why = "id diverso dallo sha256"; return false; }
  if (!root["size"].is<uint32_t>()) { *why = "size mancante"; return false; }
  out.size = root["size"].as<uint32_t>();
  if (out.size < kRomMinBytes || out.size > kRomMaxBytes) { *why = "dimensione fuori dai limiti"; return false; }
  if (!validToken(tok)) { *why = "token non valido"; return false; }
  if (!root["saveToken"].isNull() && !validToken(saveTok)) { *why = "saveToken non valido"; return false; }
  const char *mac = root["mac"].as<const char *>();
  if (!root["mac"].isNull()) {
    if (!mac || !cry::fromHex(mac, out.mac, 16)) { *why = "mac non valido"; return false; }
    out.hasMac = true;
  }
  scopy(out.id, id);
  scopy(out.sha256, sha);
  scopy(out.token, tok);
  if (saveTok) scopy(out.saveToken, saveTok);
  return true;
}

bool startMac(const uint8_t SK[32], const StartMsg &m, uint8_t out[16]) {
  cry::Msg b;
  b.str("gb-start").sep().str(m.id).sep().num(m.size).sep().str(m.sha256).sep().str(m.token).sep().str(m.saveToken);
  if (!b.ok()) return false;
  cry::mac16(SK, b, out);
  return true;
}

bool checkStart(const StartMsg &m, bool authed, const uint8_t SK[32], const char **why) {
  if (!authed) { *why = "gb-start senza autenticazione"; return false; }
  if (!m.hasMac) { *why = "gb-start senza mac"; return false; }
  uint8_t want[16];
  if (!startMac(SK, m, want) || !cry::ctEqual(want, m.mac, 16)) { *why = "gb-start con mac sbagliato"; return false; }
  return true;
}

bool decodeSaveOk(JsonVariantConst root, SaveOk &out) {
  memset(&out, 0, sizeof out);
  const char *id = root["id"].as<const char *>();
  if (!validGameId(id) || !root["crc32"].is<uint32_t>()) return false;
  if (!root["mac"].isNull()) {
    const char *mac = root["mac"].as<const char *>();
    if (!mac || !cry::fromHex(mac, out.mac, 16)) return false;
    out.hasMac = true;
  }
  scopy(out.id, id);
  out.crc32 = root["crc32"].as<uint32_t>();
  return true;
}

bool saveOkMac(const uint8_t SK[32], const SaveOk &m, uint8_t out[16]) {
  cry::Msg b;
  b.str("gb-save-ok").sep().str(m.id).sep().num(m.crc32);
  if (!b.ok()) return false;
  cry::mac16(SK, b, out);
  return true;
}

bool checkSaveOk(const SaveOk &m, bool authed, const uint8_t SK[32], const char **why) {
  if (!authed) { *why = "gb-save-ok senza autenticazione"; return false; }
  if (!m.hasMac) { *why = "gb-save-ok senza mac"; return false; }
  uint8_t want[16];
  if (!saveOkMac(SK, m, want) || !cry::ctEqual(want, m.mac, 16)) { *why = "gb-save-ok con mac sbagliato"; return false; }
  return true;
}

size_t encodePick(const char *id, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "gb-pick";
  d["v"] = kProtoVersion;
  d["id"] = id;
  return emit(d, out, cap);
}

size_t encodeSave(const char *id, uint32_t size, uint32_t crc32, const char *token, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "gb-save";
  d["v"] = kProtoVersion;
  d["id"] = id;
  d["size"] = size;
  d["crc32"] = crc32;
  if (token && token[0]) d["token"] = token;
  return emit(d, out, cap);
}

size_t encodeState(const char *playingId, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "gb-state";
  d["v"] = kProtoVersion;
  if (playingId && playingId[0]) d["playing"] = playingId;
  else d["playing"] = nullptr;
  return emit(d, out, cap);
}

size_t encodePad(pad::Link l, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "gb-pad";
  d["v"] = kProtoVersion;
  d["link"] = pad::linkKey(l);  // piano A (core/pad): stesse chiavi del contratto
  return emit(d, out, cap);
}

}  // namespace gb
}  // namespace cal
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native`
Expected: tutti PASSED, `test_gb_proto` con 13 test; `test_net` e `test_core` invariati (i campi nuovi del filtro non toccano gli altri messaggi).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core/gb_proto.h firmware/src/core/gb_proto.cpp firmware/src/core/apply_state.h \
  firmware/src/core/apply_state.cpp firmware/test/test_gb_proto
git commit -m "Giochi: messaggi gb-* lato scheda"
```

---

### Task 8: Installazione della ROM (ripresa, SHA-256, registro)

**Files:**
- Create: `firmware/src/core/gb_rom.h`, `firmware/src/core/gb_rom.cpp`
- Test: `firmware/test/test_gb_rom/test_main.cpp`

**Interfaces:**
- Consumes: `validGameId`, `kIdLen` (Task 6–7); `cry::sha256`, `cry::ctEqual`, `cry::toHex`; `avatarCrc32` (`gfx/avatar.h`, CRC-32 IEEE).
- Produces (`core/gb_rom.h`): `kSector = 4096`, `kSaveEvery = 65536`, `kStallMs = 15000`, `kRetries = 3`, `kRetryMs = 2000`, `kUrlLen = 160`, `kWhyLen = 64`; `struct RomRecord { char id[17]; uint8_t sha[32]; uint32_t size; uint32_t written; bool complete; }`, `kRecordLen = 62`, `packRecord`, `unpackRecord`; interfacce `RomFlash { capacity(); eraseWrite(off, d, n); sha256(n, out); }`, `RomStore { load(r); save(r); }`, `RangeSource { open(url, from) → codice HTTP; read(buf, cap); close(); }`; `enum class RomPhase : uint8_t { Idle, Download, Verify, Ready, Failed }`; `class RomInstaller { RomPhase start(id, sha, size, url, RomFlash &, RomStore &, RangeSource &, Ms now); RomPhase step(Ms now, size_t maxBytes = 16384); void cancel(); RomPhase phase() const; uint32_t done() const; uint32_t total() const; const char *why() const; bool newToken() const; const char *id() const; }`.

Regole (contratto: «mai avvio di una ROM incompleta»; spec §4 «ROM interrotta: ripresa»):
1. Registro `{id, sha, size, written = 0, complete = false}` nella NVS **prima** di toccare la flash; un gioco diverso da quello del registro riparte da 0.
2. Settori da 4 KB cancellati e scritti in ordine; `written` salvato ogni 64 KB (solo settori interi). Dopo uno spegnimento si riprende da `written` con `Range: bytes=written-`.
3. Connessione chiusa o 15 s senza byte: si riapre dall'inizio del settore in costruzione, fino a 3 volte di fila (si azzera quando arrivano byte). 200 invece di 206 (il servizio ha ignorato Range) → si ricomincia da 0. 403/404/410 → `newToken()`: la rete chiede un `gb-start` nuovo con `gb-pick` (il progresso resta nel registro). Altri codici → errore con il codice.
4. Più byte del previsto → errore. Alla fine SHA-256 **riletto dalla flash**: uguale → `complete = true`; diverso → `written = 0` e «gioco danneggiato: riprova».
5. Un gioco già `complete` si riverifica comunque (SHA-256 della flash) prima di ogni avvio: nessun download.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_rom/test_main.cpp`:

```cpp
// Installazione della ROM (core/gb_rom) con flash, registro e HTTP finti: download intero, ripresa dopo
// un'interruzione e dopo uno spegnimento, servizio che ignora Range, link scaduto, ROM danneggiata, gioco già
// presente (si riverifica senza scaricare), troppo grande, più lunga del previsto, nessun byte per 15 s.
#include <string.h>
#include <unity.h>

#include <vector>

#include "core/crypto.h"
#include "core/gb_rom.h"

using namespace cal;
using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {

struct FakeFlash : RomFlash {
  std::vector<uint8_t> mem = std::vector<uint8_t>(0x200000, 0xFF);
  int writes = 0;
  uint32_t capacity() const override { return uint32_t(mem.size()); }
  bool eraseWrite(uint32_t off, const uint8_t *d, size_t n) override {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, off % kSector, "settore non allineato");
    TEST_ASSERT_TRUE(n <= kSector);
    memset(&mem[off], 0xFF, kSector);
    memcpy(&mem[off], d, n);
    writes++;
    return true;
  }
  bool sha256(uint32_t n, uint8_t out[32]) override {
    cry::sha256(mem.data(), n, out);
    return true;
  }
};

struct FakeStore : RomStore {
  bool has = false;
  uint8_t blob[kRecordLen];
  int saves = 0;
  bool load(RomRecord &r) override { return has && unpackRecord(blob, sizeof blob, r); }
  bool save(const RomRecord &r) override {
    has = packRecord(r, blob, sizeof blob) == kRecordLen;
    saves++;
    return has;
  }
  RomRecord rec() {
    RomRecord r;
    TEST_ASSERT_TRUE(load(r));
    return r;
  }
};

// Serve i byte di `rom` da `from`, 1000 alla volta. cutAt: chiude la connessione dopo quel byte assoluto (una volta).
struct FakeSource : RangeSource {
  const std::vector<uint8_t> *rom = nullptr;
  std::vector<uint32_t> opens;  // from di ogni apertura
  int code = 0;                 // 0 = 206/200 normali; altrimenti il codice da dare
  bool ignoreRange = false;
  uint32_t cutAt = 0, pos = 0, extra = 0;
  bool silent = false;  // aperta ma non manda niente
  int open(const char *, uint32_t from) override {
    opens.push_back(from);
    if (code) return code;
    pos = ignoreRange ? 0 : from;
    return ignoreRange || from == 0 ? 200 : 206;
  }
  int read(uint8_t *buf, size_t cap) override {
    if (silent) return 0;
    const uint32_t end = uint32_t(rom->size()) + extra;
    if (cutAt && pos >= cutAt) { cutAt = 0; return -1; }
    if (pos >= end) return 0;
    uint32_t n = cap < 1000 ? uint32_t(cap) : 1000;
    if (cutAt && pos + n > cutAt) n = cutAt - pos;
    if (pos + n > end) n = end - pos;
    for (uint32_t i = 0; i < n; i++) buf[i] = pos + i < rom->size() ? (*rom)[pos + i] : 0xEE;
    pos += n;
    return int(n);
  }
  void close() override {}
};

std::vector<uint8_t> makeRom(uint32_t n) {
  std::vector<uint8_t> r(n);
  for (uint32_t i = 0; i < n; i++) r[i] = uint8_t(i * 7 + (i >> 8));
  return r;
}

struct Rig {
  std::vector<uint8_t> rom;
  uint8_t sha[32];
  char id[17];
  FakeFlash flash;
  FakeStore store;
  FakeSource src;
  RomInstaller inst;
  explicit Rig(uint32_t n) : rom(makeRom(n)) {
    cry::sha256(rom.data(), rom.size(), sha);
    cry::toHex(sha, 8, id);
    src.rom = &rom;
  }
  RomPhase start(Ms now = 0) { return inst.start(id, sha, uint32_t(rom.size()), "http://pc/gb/rom/t", flash, store, src, now); }
  RomPhase run(Ms &now, int maxSteps = 100000) {
    RomPhase p = inst.phase();
    for (int i = 0; i < maxSteps && (p == RomPhase::Download || p == RomPhase::Verify); i++) {
      p = inst.step(now);
      now += 10;
    }
    return p;
  }
  bool flashMatches() { return memcmp(flash.mem.data(), rom.data(), rom.size()) == 0; }
};

}  // namespace

void test_record_roundtrip_and_crc() {
  RomRecord r = {};
  strcpy(r.id, "0123456789abcdef");
  r.sha[0] = 9;
  r.size = 70000;
  r.written = 65536;
  uint8_t b[kRecordLen];
  TEST_ASSERT_EQUAL(int(kRecordLen), int(packRecord(r, b, sizeof b)));
  RomRecord back;
  TEST_ASSERT_TRUE(unpackRecord(b, sizeof b, back));
  TEST_ASSERT_EQUAL_STRING(r.id, back.id);
  TEST_ASSERT_EQUAL_UINT32(65536, back.written);
  b[20] ^= 1;
  TEST_ASSERT_FALSE(unpackRecord(b, sizeof b, back));
}

void test_fresh_download_verifies_and_marks_complete() {
  Rig g(70000);  // non multiplo di 4 KB
  Ms now = 0;
  TEST_ASSERT_EQUAL(int(RomPhase::Download), int(g.start(now)));
  TEST_ASSERT_FALSE(g.store.rec().complete);  // registro scritto prima della flash
  TEST_ASSERT_EQUAL(int(RomPhase::Ready), int(g.run(now)));
  TEST_ASSERT_TRUE(g.flashMatches());
  const RomRecord r = g.store.rec();
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_EQUAL_UINT32(70000, r.written);
  TEST_ASSERT_EQUAL(1, int(g.src.opens.size()));
  TEST_ASSERT_EQUAL_UINT32(0, g.src.opens[0]);
}

void test_already_installed_verifies_without_download() {
  Rig g(40000);
  Ms now = 0;
  g.start(now);
  g.run(now);
  RomInstaller again;
  g.src.opens.clear();
  TEST_ASSERT_EQUAL(int(RomPhase::Verify), int(again.start(g.id, g.sha, 40000, "u", g.flash, g.store, g.src, now)));
  TEST_ASSERT_EQUAL(int(RomPhase::Ready), int(again.step(now)));
  TEST_ASSERT_EQUAL(0, int(g.src.opens.size()));
}

void test_installed_but_corrupted_is_never_ready() {
  Rig g(40000);
  Ms now = 0;
  g.start(now);
  g.run(now);
  g.flash.mem[12345] ^= 0xFF;  // un bit cambiato nella flash
  RomInstaller again;
  again.start(g.id, g.sha, 40000, "u", g.flash, g.store, g.src, now);
  TEST_ASSERT_EQUAL(int(RomPhase::Failed), int(again.step(now)));
  TEST_ASSERT_EQUAL_STRING("gioco danneggiato: riprova", again.why());
  TEST_ASSERT_FALSE(g.store.rec().complete);
  TEST_ASSERT_EQUAL_UINT32(0, g.store.rec().written);
}

void test_cut_connection_resumes_from_sector() {
  Rig g(200000);
  g.src.cutAt = 50000;
  Ms now = 0;
  g.start(now);
  TEST_ASSERT_EQUAL(int(RomPhase::Ready), int(g.run(now)));
  TEST_ASSERT_TRUE(g.flashMatches());
  TEST_ASSERT_EQUAL(2, int(g.src.opens.size()));
  TEST_ASSERT_EQUAL_UINT32(49152, g.src.opens[1]);  // inizio del settore in costruzione (12 × 4 KB)
}

void test_power_loss_resumes_from_saved_progress() {
  Rig g(300000);
  Ms now = 0;
  g.start(now);
  while (g.inst.done() < 150000) g.inst.step(now += 10, 4000);
  // spegnimento: nessun cancel, si ricomincia con un installatore nuovo sugli stessi registro e flash
  TEST_ASSERT_EQUAL_UINT32(131072, g.store.rec().written);  // ultimo multiplo di 64 KB
  RomInstaller after;
  g.src.opens.clear();
  TEST_ASSERT_EQUAL(int(RomPhase::Download), int(after.start(g.id, g.sha, 300000, "u", g.flash, g.store, g.src, now)));
  TEST_ASSERT_EQUAL_UINT32(131072, g.src.opens[0]);
  RomPhase p = RomPhase::Download;
  while (p == RomPhase::Download || p == RomPhase::Verify) p = after.step(now += 10);
  TEST_ASSERT_EQUAL(int(RomPhase::Ready), int(p));
  TEST_ASSERT_TRUE(g.flashMatches());
}

void test_server_ignoring_range_restarts_from_zero() {
  Rig g(100000);
  g.src.cutAt = 70000;
  g.src.ignoreRange = true;
  Ms now = 0;
  g.start(now);
  TEST_ASSERT_EQUAL(int(RomPhase::Ready), int(g.run(now)));
  TEST_ASSERT_TRUE(g.flashMatches());
}

void test_expired_link_asks_new_token_and_keeps_progress() {
  Rig g(100000);
  Ms now = 0;
  g.start(now);
  while (g.inst.done() < 70000) g.inst.step(now += 10, 4000);
  g.inst.cancel();
  g.src.code = 410;
  RomInstaller again;
  TEST_ASSERT_EQUAL(int(RomPhase::Failed), int(again.start(g.id, g.sha, 100000, "u", g.flash, g.store, g.src, now)));
  TEST_ASSERT_TRUE(again.newToken());
  TEST_ASSERT_EQUAL_STRING("link del gioco scaduto", again.why());
  TEST_ASSERT_FALSE(g.store.rec().complete);
  TEST_ASSERT_EQUAL_UINT32(65536, g.store.rec().written);  // il gb-start nuovo riprende da qui
}

void test_other_http_error_fails() {
  Rig g(50000);
  g.src.code = 500;
  TEST_ASSERT_EQUAL(int(RomPhase::Failed), int(g.start()));
  TEST_ASSERT_EQUAL_STRING("download non riuscito (HTTP 500)", g.inst.why());
  TEST_ASSERT_FALSE(g.inst.newToken());
}

void test_network_errors_give_up_after_retries() {
  Rig g(50000);
  g.src.code = -1;
  Ms now = 0;
  g.start(now);
  TEST_ASSERT_EQUAL(int(RomPhase::Failed), int(g.run(now)));
  TEST_ASSERT_EQUAL_STRING("download interrotto", g.inst.why());
  TEST_ASSERT_EQUAL(1 + kRetries, int(g.src.opens.size()));
}

void test_stall_reopens() {
  Rig g(50000);
  g.src.silent = true;
  Ms now = 0;
  g.start(now);
  g.inst.step(now += kStallMs + 1);
  g.src.silent = false;
  TEST_ASSERT_EQUAL(int(RomPhase::Ready), int(g.run(now)));
  TEST_ASSERT_EQUAL(2, int(g.src.opens.size()));
}

void test_too_big_and_too_long() {
  Rig big(1000);
  TEST_ASSERT_EQUAL(int(RomPhase::Failed),
                    int(big.inst.start(big.id, big.sha, 0x200001, "u", big.flash, big.store, big.src, 0)));
  TEST_ASSERT_EQUAL_STRING("gioco troppo grande per la scheda", big.inst.why());
  Rig g(50000);
  g.src.extra = 10;
  Ms now = 0;
  g.start(now);
  TEST_ASSERT_EQUAL(int(RomPhase::Failed), int(g.run(now)));
  TEST_ASSERT_EQUAL_STRING("download più lungo del previsto", g.inst.why());
  TEST_ASSERT_FALSE(g.store.rec().complete);
}

void test_wrong_bytes_never_ready() {
  Rig g(50000);
  std::vector<uint8_t> other = g.rom;
  other[49999] ^= 1;
  g.src.rom = &other;  // il servizio manda un file diverso dallo sha annunciato
  Ms now = 0;
  g.start(now);
  TEST_ASSERT_EQUAL(int(RomPhase::Failed), int(g.run(now)));
  TEST_ASSERT_EQUAL_STRING("gioco danneggiato: riprova", g.inst.why());
  TEST_ASSERT_FALSE(g.store.rec().complete);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_record_roundtrip_and_crc);
  RUN_TEST(test_fresh_download_verifies_and_marks_complete);
  RUN_TEST(test_already_installed_verifies_without_download);
  RUN_TEST(test_installed_but_corrupted_is_never_ready);
  RUN_TEST(test_cut_connection_resumes_from_sector);
  RUN_TEST(test_power_loss_resumes_from_saved_progress);
  RUN_TEST(test_server_ignoring_range_restarts_from_zero);
  RUN_TEST(test_expired_link_asks_new_token_and_keeps_progress);
  RUN_TEST(test_other_http_error_fails);
  RUN_TEST(test_network_errors_give_up_after_retries);
  RUN_TEST(test_stall_reopens);
  RUN_TEST(test_too_big_and_too_long);
  RUN_TEST(test_wrong_bytes_never_ready);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_rom`
Expected: FAIL in compilazione, `core/gb_rom.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `firmware/src/core/gb_rom.h`:

```cpp
// Giochi Game Boy: installazione della ROM nella partizione «gbrom» (contratto: GET /gb/rom/<token> con Range per la
// ripresa, SHA-256 verificato, mai avviare una ROM incompleta). Logica pura dietro tre interfacce (flash, registro
// nella NVS, HTTP), provata in native con finti (test_gb_rom); quelle vere stanno in hw/gb_hw.cpp.
//
// Ordine delle scritture, perché un'interruzione (rete, corrente) non lasci mai un gioco «pronto» sbagliato:
//   1. registro {id, sha, size, written = 0, complete = false} nella NVS, PRIMA di toccare la flash;
//   2. settori da 4 KB cancellati e scritti in ordine; written aggiornato ogni 64 KB (solo settori interi);
//   3. alla fine SHA-256 riletto dalla flash: uguale → complete = true; diverso → written = 0 (si riscarica).
// Un gioco con complete = true si riverifica comunque (SHA-256 della flash) a ogni avvio.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "gb_proto.h"

namespace cal {
namespace gb {

const uint32_t kSector = 4096;
const uint32_t kSaveEvery = 64 * 1024;
const Ms kStallMs = 15000;   // nessun byte da 15 s: connessione persa
const int kRetries = 3;      // riaperture dello stesso link prima di arrendersi
const Ms kRetryMs = 2000;
const size_t kUrlLen = 160;
const size_t kWhyLen = 64;

struct RomRecord {
  char id[kIdLen];
  uint8_t sha[32];
  uint32_t size;
  uint32_t written;  // byte già nella flash (multiplo di kSector, o size)
  bool complete;     // SHA-256 verificato
};
const size_t kRecordLen = 1 + 16 + 32 + 4 + 4 + 1 + 4;  // versione, …, CRC-32
size_t packRecord(const RomRecord &r, uint8_t *out, size_t cap);
bool unpackRecord(const uint8_t *in, size_t n, RomRecord &r);

class RomFlash {
 public:
  virtual ~RomFlash() {}
  virtual uint32_t capacity() const = 0;
  // Cancella il settore che comincia a off (multiplo di kSector) e ci scrive n ≤ kSector byte.
  virtual bool eraseWrite(uint32_t off, const uint8_t *d, size_t n) = 0;
  virtual bool sha256(uint32_t n, uint8_t out[32]) = 0;  // dei primi n byte
};
class RomStore {
 public:
  virtual ~RomStore() {}
  virtual bool load(RomRecord &r) = 0;
  virtual bool save(const RomRecord &r) = 0;
};
class RangeSource {
 public:
  virtual ~RangeSource() {}
  // GET con «Range: bytes=from-» (niente Range con from = 0). Codice HTTP; < 0 = errore di rete.
  virtual int open(const char *url, uint32_t from) = 0;
  virtual int read(uint8_t *buf, size_t cap) = 0;  // > 0 letti, 0 niente per ora, < 0 chiusa
  virtual void close() = 0;
};

enum class RomPhase : uint8_t { Idle, Download, Verify, Ready, Failed };

class RomInstaller {
 public:
  // Ready già qui non succede mai: anche un gioco presente passa da Verify (step()).
  RomPhase start(const char *id, const uint8_t sha[32], uint32_t size, const char *url, RomFlash &f, RomStore &st,
                 RangeSource &src, Ms now);
  RomPhase step(Ms now, size_t maxBytes = 16384);
  void cancel();  // l'utente o il servizio: il registro resta (si riprende da written)
  RomPhase phase() const { return phase_; }
  uint32_t done() const { return base_ + fill_; }
  uint32_t total() const { return rec_.size; }
  const char *why() const { return why_; }
  bool newToken() const { return newToken_; }  // 404/410: serve un gb-start nuovo (gb-pick)
  const char *id() const { return rec_.id; }

 private:
  RomPhase fail(const char *why);
  bool openAt(uint32_t from, Ms now);
  bool flush();
  RomPhase verify();

  RomPhase phase_ = RomPhase::Idle;
  RomRecord rec_ = {};
  RomFlash *flash_ = nullptr;
  RomStore *store_ = nullptr;
  RangeSource *src_ = nullptr;
  char url_[kUrlLen] = "";
  char why_[kWhyLen] = "";
  bool newToken_ = false, open_ = false;
  int retries_ = 0;
  Ms lastData_ = 0, retryAt_ = 0;
  uint32_t base_ = 0, fill_ = 0;  // settore in costruzione: inizio e byte già dentro
  uint8_t sec_[kSector];
};

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_rom.cpp`:

```cpp
#include "gb_rom.h"

#include <stdio.h>
#include <string.h>

#include "crypto.h"
#include "strbuf.h"
#include "gfx/avatar.h"  // avatarCrc32: CRC-32 IEEE

namespace cal {
namespace gb {

namespace {
void put32(uint8_t *p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}
uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
}  // namespace

size_t packRecord(const RomRecord &r, uint8_t *out, size_t cap) {
  if (cap < kRecordLen) return 0;
  out[0] = 1;
  memcpy(out + 1, r.id, 16);
  memcpy(out + 17, r.sha, 32);
  put32(out + 49, r.size);
  put32(out + 53, r.written);
  out[57] = r.complete ? 1 : 0;
  put32(out + 58, avatarCrc32(out, 58));
  return kRecordLen;
}

bool unpackRecord(const uint8_t *in, size_t n, RomRecord &r) {
  memset(&r, 0, sizeof r);
  if (n != kRecordLen || in[0] != 1 || get32(in + 58) != avatarCrc32(in, 58)) return false;
  memcpy(r.id, in + 1, 16);
  r.id[16] = 0;
  memcpy(r.sha, in + 17, 32);
  r.size = get32(in + 49);
  r.written = get32(in + 53);
  r.complete = in[57] == 1;
  if (!validGameId(r.id) || r.written > r.size) {
    memset(&r, 0, sizeof r);
    return false;
  }
  return true;
}

RomPhase RomInstaller::fail(const char *why) {
  if (open_) src_->close();
  open_ = false;
  scopy(why_, why);
  phase_ = RomPhase::Failed;
  return phase_;
}

RomPhase RomInstaller::start(const char *id, const uint8_t sha[32], uint32_t size, const char *url, RomFlash &f,
                             RomStore &st, RangeSource &src, Ms now) {
  cancel();
  flash_ = &f;
  store_ = &st;
  src_ = &src;
  why_[0] = 0;
  newToken_ = false;
  retries_ = 0;
  scopy(url_, url);
  if (size > f.capacity()) return fail("gioco troppo grande per la scheda");
  RomRecord old;
  const bool have = st.load(old);
  const bool same = have && !strcmp(old.id, id) && !memcmp(old.sha, sha, 32) && old.size == size;
  if (same) {
    rec_ = old;
  } else {
    memset(&rec_, 0, sizeof rec_);
    scopy(rec_.id, id);
    memcpy(rec_.sha, sha, 32);
    rec_.size = size;
    if (!st.save(rec_)) return fail("memoria della scheda non scrivibile");  // prima della flash
  }
  if (rec_.complete) {
    base_ = rec_.size;
    fill_ = 0;
    phase_ = RomPhase::Verify;
    return phase_;
  }
  base_ = rec_.written - rec_.written % kSector;
  fill_ = 0;
  phase_ = RomPhase::Download;
  openAt(base_, now);
  return phase_;
}

bool RomInstaller::openAt(uint32_t from, Ms now) {
  if (open_) src_->close();
  open_ = false;
  const int code = src_->open(url_, from);
  lastData_ = now;
  if (code == 206 || (code == 200 && from == 0)) {
    open_ = true;
    return true;
  }
  if (code == 200) {  // il servizio ha ignorato Range: si ricomincia da capo
    base_ = 0;
    fill_ = 0;
    open_ = true;
    return true;
  }
  if (code == 404 || code == 410 || code == 403) {
    newToken_ = true;
    fail("link del gioco scaduto");
    return false;
  }
  if (code > 0) {
    char t[kWhyLen];
    snprintf(t, sizeof t, "download non riuscito (HTTP %d)", code);
    fail(t);
    return false;
  }
  retryAt_ = now + kRetryMs;  // errore di rete: si riprova
  return false;
}

bool RomInstaller::flush() {
  if (!fill_) return true;
  if (!flash_->eraseWrite(base_, sec_, fill_)) return false;
  const uint32_t end = base_ + fill_;
  if (fill_ == kSector) {
    base_ = end;
    fill_ = 0;
  }
  if (end % kSaveEvery == 0 || end == rec_.size) {
    rec_.written = end == rec_.size ? end : end - end % kSector;
    store_->save(rec_);  // se non riesce si riprende un po' più indietro: va bene
  }
  return true;
}

RomPhase RomInstaller::verify() {
  uint8_t got[32];
  if (!flash_->sha256(rec_.size, got) || !cry::ctEqual(got, rec_.sha, 32)) {
    rec_.written = 0;
    rec_.complete = false;
    store_->save(rec_);
    base_ = 0;
    fill_ = 0;
    return fail("gioco danneggiato: riprova");
  }
  if (!rec_.complete) {
    rec_.complete = true;
    rec_.written = rec_.size;
    if (!store_->save(rec_)) return fail("memoria della scheda non scrivibile");
  }
  phase_ = RomPhase::Ready;
  return phase_;
}

RomPhase RomInstaller::step(Ms now, size_t maxBytes) {
  if (phase_ == RomPhase::Verify) return verify();
  if (phase_ != RomPhase::Download) return phase_;
  if (!open_) {
    if (now < retryAt_) return phase_;
    if (++retries_ > kRetries) return fail("download interrotto");
    openAt(base_, now);
    return phase_;
  }
  size_t got = 0;
  while (got < maxBytes && done() < rec_.size) {
    const size_t room = kSector - fill_;
    const int n = src_->read(sec_ + fill_, room);
    if (n == 0) break;
    if (n < 0) {  // chiusa a metà: si riapre dall'inizio del settore in costruzione
      src_->close();
      open_ = false;
      fill_ = 0;
      retryAt_ = now + kRetryMs;
      return phase_;
    }
    lastData_ = now;
    retries_ = 0;  // si va avanti: i tentativi ricominciano
    got += size_t(n);
    fill_ += uint32_t(n);
    if (done() > rec_.size) return fail("download più lungo del previsto");
    if (fill_ == kSector || done() == rec_.size)
      if (!flush()) return fail("scrittura nella flash non riuscita");
  }
  if (done() == rec_.size) {
    // Un byte in più dopo la fine vuol dire un file diverso da quello annunciato.
    uint8_t extra;
    if (src_->read(&extra, 1) > 0) return fail("download più lungo del previsto");
    src_->close();
    open_ = false;
    phase_ = RomPhase::Verify;
    return phase_;
  }
  if (got == 0 && now - lastData_ > kStallMs) {
    src_->close();
    open_ = false;
    fill_ = 0;
    retryAt_ = now;
  }
  return phase_;
}

void RomInstaller::cancel() {
  if (open_ && src_) src_->close();
  open_ = false;
  if (phase_ == RomPhase::Download || phase_ == RomPhase::Verify) phase_ = RomPhase::Idle;
}

}  // namespace gb
}  // namespace cal
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_gb_rom`
Expected: 13 test PASSED.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core/gb_rom.h firmware/src/core/gb_rom.cpp firmware/test/test_gb_rom
git commit -m "Giochi: installazione della ROM con ripresa e verifica"
```

---

### Task 9: Salvataggi verso il PC

**Files:**
- Create: `firmware/src/core/gb_save.h`, `firmware/src/core/gb_save.cpp`
- Test: `firmware/test/test_gb_save/test_main.cpp`

**Interfaces:**
- Consumes: `SaveOk`, `encodeSave`, `validGameId`, `kMaxGames` (Task 6–7); `cry::toHex`.
- Produces (`core/gb_save.h`): `kPutDelayMs = 300`, `kPutRetryMs = 700`, `kPutTries = 3`, `kSaveOkMs = 10000`; `void savePath(id, out, cap)` (`/gb/<id>.sav`), `void ackPath(id, out, cap)` (`/gb/<id>.ack`); `struct SaveInfo { char id[17]; uint32_t size; uint32_t crc; }`; interfacce `SaveFiles { info; list; acked; setAcked; }`, `SaveTransport { send(json, n); put(token, id, size) → codice; }`; `class SaveSync { explicit SaveSync(RandFn); void onConnected(SaveFiles &); void enqueue(const char *id); void onDisconnected(); void onSaveOk(const SaveOk &, SaveFiles &); void tick(Ms now, bool connected, SaveFiles &, SaveTransport &); bool idle() const; int queued() const; }`.

Contratto: `gb-save {id, size, crc32, token?}` seguito da `PUT http://<host>:8766/gb/save/<token>` e `gb-save-ok {id, crc32, mac}` (mac verificato dalla rete, Task 13). Il token **lo crea la scheda** (16 byte casuali, 32 cifre minuscole, mai riusato) e lo manda nel `gb-save` autenticato; il servizio lo registra per l'IP della scheda e accetta PUT fino al primo caricamento completo riuscito, per 10 minuti. Questo piano manda sempre il token (la forma senza token, l'annuncio, il servizio la capisce ma la scheda non la usa). Il PUT parte 300 ms dopo il `gb-save`; un 404 (il servizio non l'ha ancora registrato) si ritenta 2 volte dopo 700 ms. Un `gb-save-ok` con lo stesso CRC, anche prima del PUT (il servizio aveva già quella copia), segna il salvataggio come «al PC» (`/gb/<id>.ack`). Senza conferma entro 10 s si riprova alla prossima connessione autenticata (`onConnected` rimette in coda ogni `.sav` con CRC diverso dal suo `.ack`).

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_save/test_main.cpp`:

```cpp
// Salvataggi verso il PC (core/gb_save): solo quelli cambiati, gb-save poi PUT, conferma con gb-save-ok,
// 404 ritentato, servizio che ha già quel CRC, connessione persa.
#include <string.h>
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "core/gb_save.h"

using namespace cal;
using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {

struct FakeFiles : SaveFiles {
  std::map<std::string, SaveInfo> saves;
  std::map<std::string, uint32_t> acks;
  void add(const char *id, uint32_t crc, uint32_t size = 8192) {
    SaveInfo s = {};
    strcpy(s.id, id);
    s.size = size;
    s.crc = crc;
    saves[id] = s;
  }
  bool info(const char *id, SaveInfo &out) override {
    auto it = saves.find(id);
    if (it == saves.end()) return false;
    out = it->second;
    return true;
  }
  int list(SaveInfo *out, int cap) override {
    int n = 0;
    for (auto &kv : saves)
      if (n < cap) out[n++] = kv.second;
    return n;
  }
  bool acked(const char *id, uint32_t &crc) override {
    auto it = acks.find(id);
    if (it == acks.end()) return false;
    crc = it->second;
    return true;
  }
  bool setAcked(const char *id, uint32_t crc) override {
    acks[id] = crc;
    return true;
  }
};

struct FakeNet : SaveTransport {
  std::vector<std::string> sent;
  std::vector<std::string> puts;
  std::vector<int> codes;  // risposte dei PUT in ordine (vuoto = 200)
  void send(const char *json, size_t n) override { sent.push_back(std::string(json, n)); }
  int put(const char *token, const char *id, uint32_t) override {
    puts.push_back(std::string(id) + "/" + token);
    if (codes.empty()) return 200;
    const int c = codes.front();
    codes.erase(codes.begin());
    return c;
  }
};

void fixedRand(uint8_t *out, size_t n) { memset(out, 0xAB, n); }
const char *A = "0123456789abcdef";
const char *B = "fedcba9876543210";

SaveOk ok(const char *id, uint32_t crc) {
  SaveOk s = {};
  strcpy(s.id, id);
  s.crc32 = crc;
  return s;
}

}  // namespace

void test_connect_sends_only_changed() {
  FakeFiles f;
  FakeNet t;
  f.add(A, 111);
  f.add(B, 222);
  f.acks[B] = 222;  // già al PC
  SaveSync s(fixedRand);
  s.onConnected(f);
  TEST_ASSERT_EQUAL(1, s.queued());
  s.tick(0, true, f, t);
  TEST_ASSERT_EQUAL(1, int(t.sent.size()));
  TEST_ASSERT_EQUAL_STRING(
      "{\"t\":\"gb-save\",\"v\":1,\"id\":\"0123456789abcdef\",\"size\":8192,\"crc32\":111,\"token\":\"abababababababababababababababab\"}",
      t.sent[0].c_str());
  s.tick(kPutDelayMs - 1, true, f, t);
  TEST_ASSERT_EQUAL(0, int(t.puts.size()));  // prima il servizio registra il token
  s.tick(kPutDelayMs, true, f, t);
  TEST_ASSERT_EQUAL(1, int(t.puts.size()));
  s.onSaveOk(ok(A, 111), f);
  TEST_ASSERT_EQUAL_UINT32(111, f.acks[A]);
  TEST_ASSERT_TRUE(s.idle());
}

void test_wrong_crc_ack_ignored() {
  FakeFiles f;
  FakeNet t;
  f.add(A, 111);
  SaveSync s(fixedRand);
  s.enqueue(A);
  s.tick(0, true, f, t);
  s.tick(kPutDelayMs, true, f, t);
  s.onSaveOk(ok(A, 999), f);
  TEST_ASSERT_TRUE(f.acks.find(A) == f.acks.end());
  TEST_ASSERT_FALSE(s.idle());
  s.tick(kPutDelayMs + kSaveOkMs, true, f, t);  // niente conferma: si lascia per la prossima connessione
  TEST_ASSERT_TRUE(s.idle());
}

void test_service_already_has_it() {
  FakeFiles f;
  FakeNet t;
  f.add(A, 5);
  SaveSync s(fixedRand);
  s.enqueue(A);
  s.tick(0, true, f, t);
  s.onSaveOk(ok(A, 5), f);  // risposta prima del PUT
  TEST_ASSERT_TRUE(s.idle());
  s.tick(kPutDelayMs, true, f, t);
  TEST_ASSERT_EQUAL(0, int(t.puts.size()));
}

void test_put_404_retried_then_dropped() {
  FakeFiles f;
  FakeNet t;
  f.add(A, 7);
  t.codes = {404, 404, 404};
  SaveSync s(fixedRand);
  s.enqueue(A);
  Ms now = 0;
  for (int i = 0; i < 20; i++) s.tick(now += 100, true, f, t);
  TEST_ASSERT_EQUAL(kPutTries, int(t.puts.size()));
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_TRUE(f.acks.find(A) == f.acks.end());
}

void test_offline_keeps_queue() {
  FakeFiles f;
  FakeNet t;
  f.add(A, 7);
  SaveSync s(fixedRand);
  s.enqueue(A);
  s.tick(0, false, f, t);
  TEST_ASSERT_EQUAL(0, int(t.sent.size()));
  TEST_ASSERT_EQUAL(1, s.queued());
  s.enqueue(A);  // niente doppioni
  TEST_ASSERT_EQUAL(1, s.queued());
}

void test_save_changed_during_upload_is_resent() {
  FakeFiles f;
  FakeNet t;
  f.add(A, 1);
  SaveSync s(fixedRand);
  s.enqueue(A);
  s.tick(0, true, f, t);
  f.add(A, 2);  // nuova uscita dal gioco mentre si mandava il vecchio
  s.enqueue(A);
  s.tick(1, true, f, t);
  TEST_ASSERT_EQUAL(2, int(t.sent.size()));
  TEST_ASSERT_TRUE(t.sent[1].find("\"crc32\":2") != std::string::npos);
}

void test_paths() {
  char p[40];
  savePath(A, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("/gb/0123456789abcdef.sav", p);
  ackPath(A, p, sizeof p);
  TEST_ASSERT_EQUAL_STRING("/gb/0123456789abcdef.ack", p);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_connect_sends_only_changed);
  RUN_TEST(test_wrong_crc_ack_ignored);
  RUN_TEST(test_service_already_has_it);
  RUN_TEST(test_put_404_retried_then_dropped);
  RUN_TEST(test_offline_keeps_queue);
  RUN_TEST(test_save_changed_during_upload_is_resent);
  RUN_TEST(test_paths);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_save`
Expected: FAIL in compilazione, `core/gb_save.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `firmware/src/core/gb_save.h`:

```cpp
// Giochi Game Boy: salvataggi verso il PC (contratto: gb-save {id, size, crc32, token} seguito da
// PUT /gb/save/<token>, il servizio risponde gb-save-ok {id, crc32}). Si manda ogni salvataggio il cui CRC è
// diverso dall'ultimo confermato dal servizio: a ogni connessione autenticata e all'uscita da un gioco.
// File in LittleFS (hw/gb_hw): /gb/<id>.sav (RAM della cartuccia) e /gb/<id>.ack (CRC confermato).
// Logica pura dietro due interfacce, provata in native (test_gb_save).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "gb_proto.h"

namespace cal {
namespace gb {

const Ms kPutDelayMs = 300;     // il servizio registra il token del gb-save prima del PUT
const Ms kPutRetryMs = 700;     // 404: il gb-save non è ancora arrivato, si riprova
const int kPutTries = 3;
const Ms kSaveOkMs = 10000;     // senza gb-save-ok: si riprova alla prossima connessione

void savePath(const char *id, char *out, size_t cap);  // /gb/<id>.sav
void ackPath(const char *id, char *out, size_t cap);   // /gb/<id>.ack

struct SaveInfo {
  char id[kIdLen];
  uint32_t size;
  uint32_t crc;
};

class SaveFiles {
 public:
  virtual ~SaveFiles() {}
  virtual bool info(const char *id, SaveInfo &out) = 0;   // false = nessun salvataggio
  virtual int list(SaveInfo *out, int cap) = 0;           // tutti i salvataggi presenti
  virtual bool acked(const char *id, uint32_t &crc) = 0;  // ultimo CRC confermato dal servizio
  virtual bool setAcked(const char *id, uint32_t crc) = 0;
};
class SaveTransport {
 public:
  virtual ~SaveTransport() {}
  virtual void send(const char *json, size_t n) = 0;                     // WebSocket
  virtual int put(const char *token, const char *id, uint32_t size) = 0;  // PUT del file: codice HTTP, < 0 rete
};

class SaveSync {
 public:
  typedef void (*RandFn)(uint8_t *out, size_t n);
  explicit SaveSync(RandFn rnd) : rnd_(rnd) {}
  void onConnected(SaveFiles &f);   // dopo auth-ok: in coda tutto quello che il servizio non ha
  void enqueue(const char *id);     // uscita dal gioco con la RAM cambiata
  void onDisconnected() { state_ = State::Idle; }  // la coda resta: si riprende alla prossima
  void onSaveOk(const SaveOk &ok, SaveFiles &f);
  void tick(Ms now, bool connected, SaveFiles &f, SaveTransport &t);
  bool idle() const { return state_ == State::Idle && n_ == 0; }
  int queued() const { return n_; }

 private:
  enum class State : uint8_t { Idle, Announced, WaitOk };
  void finish() { state_ = State::Idle; }
  RandFn rnd_;
  State state_ = State::Idle;
  char queue_[kMaxGames][kIdLen];
  int n_ = 0;
  SaveInfo cur_ = {};
  char token_[33] = "";
  Ms at_ = 0;
  int tries_ = 0;
};

}  // namespace gb
}  // namespace cal
```

Create `firmware/src/core/gb_save.cpp`:

```cpp
#include "gb_save.h"

#include <stdio.h>
#include <string.h>

#include "crypto.h"
#include "strbuf.h"

namespace cal {
namespace gb {

void savePath(const char *id, char *out, size_t cap) { snprintf(out, cap, "/gb/%s.sav", id); }
void ackPath(const char *id, char *out, size_t cap) { snprintf(out, cap, "/gb/%s.ack", id); }

void SaveSync::enqueue(const char *id) {
  if (!validGameId(id)) return;
  for (int i = 0; i < n_; i++)
    if (!strcmp(queue_[i], id)) return;
  if (state_ != State::Idle && !strcmp(cur_.id, id)) {
    state_ = State::Idle;  // quello in corso è già vecchio: si rimanda il nuovo
  }
  if (n_ >= kMaxGames) return;
  scopy(queue_[n_++], id);
}

void SaveSync::onConnected(SaveFiles &f) {
  SaveInfo all[kMaxGames];
  const int n = f.list(all, kMaxGames);
  for (int i = 0; i < n; i++) {
    uint32_t crc;
    if (!f.acked(all[i].id, crc) || crc != all[i].crc) enqueue(all[i].id);
  }
}

void SaveSync::onSaveOk(const SaveOk &ok, SaveFiles &f) {
  if (state_ == State::Idle || strcmp(ok.id, cur_.id) != 0 || ok.crc32 != cur_.crc) return;
  f.setAcked(cur_.id, cur_.crc);
  finish();
}

void SaveSync::tick(Ms now, bool connected, SaveFiles &f, SaveTransport &t) {
  if (!connected) return;
  if (state_ == State::Idle) {
    while (n_ > 0) {
      SaveInfo s;
      char id[kIdLen];
      scopy(id, queue_[0]);
      memmove(queue_[0], queue_[1], sizeof(queue_[0]) * size_t(n_ - 1));
      n_--;
      uint32_t ack;
      if (!f.info(id, s) || (f.acked(id, ack) && ack == s.crc)) continue;  // sparito o già al PC
      cur_ = s;
      uint8_t r[16];
      rnd_(r, sizeof r);
      cry::toHex(r, sizeof r, token_);
      char buf[200];
      const size_t len = encodeSave(cur_.id, cur_.size, cur_.crc, token_, buf, sizeof buf);
      if (!len) continue;
      t.send(buf, len);
      state_ = State::Announced;
      at_ = now + kPutDelayMs;
      tries_ = 0;
      return;
    }
    return;
  }
  if (state_ == State::Announced) {
    if (now < at_) return;
    const int code = t.put(token_, cur_.id, cur_.size);
    if (code >= 200 && code < 300) {
      state_ = State::WaitOk;
      at_ = now + kSaveOkMs;
    } else if (code == 404 && ++tries_ < kPutTries) {
      at_ = now + kPutRetryMs;
    } else {
      finish();  // si riprova alla prossima connessione
    }
    return;
  }
  if (state_ == State::WaitOk && now >= at_) finish();
}

}  // namespace gb
}  // namespace cal
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_gb_save`
Expected: 7 test PASSED.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core/gb_save.h firmware/src/core/gb_save.cpp firmware/test/test_gb_save
git commit -m "Giochi: salvataggi verso il PC"
```

---

### Task 10: Aggiornamento remoto rimandato durante la partita

**Files:**
- Modify: `firmware/src/core/ota.h` (`struct Conditions`, costanti), `firmware/src/core/ota.cpp` (`conditionsOk`)
- Test: `firmware/test/test_ota/test_main.cpp`

**Interfaces:**
- Consumes: niente di nuovo.
- Produces: `ota::Conditions::playing` (bool, ultimo campo: le inizializzazioni a graffe esistenti restano valide e lo lasciano a false); `ota::kPlaying = "partita in corso: riprova a fine partita"`. Il Task 14 lo riempie con `gUi.gbOnStack()`.

La spec dice «OTA rimandato a fine partita». La scheda risponde `ota-status {phase: "rifiutato", error: "partita in corso: riprova a fine partita"}` senza coprire la partita (`shown = false`); il servizio (piano C) rifà l'offerta una volta dopo `gb-state {playing: null}` (contratto, «OTA e partita»).

- [ ] **Step 1: Write the failing test**

In `firmware/test/test_ota/test_main.cpp`, subito prima di `static void test_conditions() {`:

```cpp
// Giochi: con una partita aperta (anche in pausa) l'aggiornamento aspetta, anche col cavo; non copre la partita.
static void test_conditions_playing() {
  char why[96];
  bool shown = true;
  ota::Conditions c = {false, false, false, false, UsbPresent::Yes, 100};
  c.playing = true;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why, &shown));
  TEST_ASSERT_EQUAL_STRING("partita in corso: riprova a fine partita", why);
  TEST_ASSERT_FALSE(shown);
  c.playing = false;
  TEST_ASSERT_TRUE(ota::conditionsOk(c, why, sizeof why));
}
```

e in `main()`, dopo `RUN_TEST(test_conditions);`:

```cpp
  RUN_TEST(test_conditions_playing);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_ota`
Expected: FAIL in compilazione, `'struct cal::ota::Conditions' has no member named 'playing'`.

- [ ] **Step 3: Write the implementation**

In `firmware/src/core/ota.h`, in `struct Conditions`, dopo `int battPct; …`:

```cpp
  bool playing;       // giochi: partita in corso o in pausa (l'aggiornamento aspetta la fine)
```

e dopo `const char *const kCancelled = "annullato dal PC";`:

```cpp
const char *const kPlaying = "partita in corso: riprova a fine partita";
```

In `firmware/src/core/ota.cpp`, in `conditionsOk()`, dopo la riga di `c.calibrating`:

```cpp
  if (c.playing) { say(why, cap, kPlaying); return false; }  // giochi: la partita non si interrompe (spec §4)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd firmware && pio test -e native -f test_ota`
Expected: 21 test PASSED.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core/ota.h firmware/src/core/ota.cpp firmware/test/test_ota/test_main.cpp
git commit -m "Giochi: aggiornamento remoto rimandato durante la partita"
```

---
### Task 11: Interfaccia — Menu › Gioco › ROM, caricamento, partita, pausa

**Prerequisito:** piano A, Menu › Gioco (`MenuId::Gb`, `GbPad`, `GbRoms` con i loro blocchi in `menuList()`), l'aggancio `gbGameItems` in `core/gb_menu.{h,cpp}`, lo stub `Ui::gbPlaying()` e la mappatura del controller sulle `Action`.

**Files:**
- Modify: `firmware/src/core/model.h` (include, `ScreenId`, `MenuId`, `Model`), `firmware/src/core/model.cpp` (`screenKey`, `menuKey`)
- Modify: `firmware/src/core/menu.h` (`ActId`), `firmware/src/core/menu.cpp` (`actKey`, blocco `MenuId::GbPause`), `firmware/src/core/gb_menu.cpp` (corpo di `gbGameItems`, aggancio del piano A)
- Modify: `firmware/src/core/ui.h` (metodi e campi `gb*`, corpo di `gbPlaying`, `NetCmd::Gb*`, `NetRequest::gbLink`), `firmware/src/core/ui.cpp` (`netCmdKey`, due agganci)
- Create: `firmware/src/core/gb_ui.cpp` (metodi `gb*` di `cal::Ui`)
- Create: `firmware/src/ui/gb_screens.h`, `firmware/src/ui/gb_screens.cpp`
- Modify: `firmware/src/ui/screens.h` (`ui::text`), `firmware/src/ui/screens.cpp` (`ui::text`, due righe in `drawScreen`)
- Test: `firmware/test/test_gb_ui/test_main.cpp`
- Modify: `docs/DECISIONI.md`

**Interfaces:**
- Consumes: `GbView`, `GameEntry`, `LoadStep`, `PauseWhy`, `GbAct` (Task 6); `kWinX`, `kWinW` (Task 4, solo nel test); dal piano A `MenuId::Gb/GbRoms`, `int cal::gbGameItems(const Model &, Ms, MenuItem *, int)`, `Ui::gbPlaying()`, `Sys::padLink`.
- Produces:
  - `ScreenId::GbLoad`, `ScreenId::GbPlay` (prima di `Count`); `MenuId::GbPause` (subito prima di `Count`, dopo `GbRoms` del piano A); il corpo di `gbGameItems`; `ActId::GbPick`, `GbResume`, `GbExit`, `GbProfile`; `Model::gb` (`GbView`).
  - In `cal::Ui`: `RefreshKind gbList(const gb::GameEntry *, int n, Ms)`; `void gbInstalled(bool partOk, const char *id, const char *title)`; `RefreshKind gbLoad(const char *id, Ms)`; `RefreshKind gbProgress(gb::LoadStep, uint32_t done, uint32_t total, Ms)`; `RefreshKind gbPlay(Ms)`; `RefreshKind gbPaused(gb::PauseWhy, Ms)`; `RefreshKind gbShowPerm(Ms)`; `RefreshKind gbResumed(Ms)`; `RefreshKind gbEnd(const char *msg, bool rest, Ms)`; `gb::GbAct takeGbAct(char out[17])`; il corpo di `bool gbPlaying() const` (stub del piano A); `bool gbOnStack() const`.
  - `NetCmd::GbPick, GbGo, GbLocal, GbCancel, GbState, GbPad, GbSaveNow` e `NetRequest::gbLink` (li usano i Task 13–14).
  - `ui::gbLoadScreen`, `ui::gbBands`, `ui::text`.

Schermate (nuove, non nel simulatore: testi decisi qui e annotati in DECISIONI):
- **Menu › Gioco › ROM** (`MenuId::GbRoms` del piano A, tutta di questo piano tramite `gbGameItems`): un gioco per riga, valore «sulla scheda» o la dimensione «32 KB»; il gioco già sulla scheda che il PC non elenca (PC lontano) resta giocabile («Il gioco già sulla scheda: si gioca anche senza PC»); senza partizione «Giochi non disponibili»; senza elenco «Giochi sul PC · PC non raggiungibile»; elenco vuoto «Nessun gioco sul PC». L'elenco si vede **sempre**, anche col controller scollegato; scegliere un gioco senza controller collegato (`sys.padLink != Ready`) mostra il toast «Collega il controller per giocare» e non parte (decisione dell'utente).
- **GbLoad**: «GIOCO», titolo, «Scarico il gioco» / «Verifico il gioco» / «Avvio…», barra, percentuale, «205 / 2048 KB», «B = ANNULLA». Ridisegno ogni ~5 % e non prima di 2 s, come la barra dell'OTA. B (o pressione lunga del tasto) annulla; gli altri gesti no.
- **GbPlay**: solo le bande (la finestra x 57–192 resta bianca, la riempie `composeFrame`). Sinistra: batteria e percentuale, «Batteria / bassa» in negativo sotto il 15 %, «PC ✓/✗». Destra: titolo su 3 righe, «START + / SELECT / = PAUSA». Niente toast (coprirebbero il gioco). Un gesto del tasto PRG durante la partita chiede la pausa.
- **Pausa** (`MenuId::GbPause`, una lista come le altre: su, giù, scegli): riga d'avviso «Controller scollegato» o «Pausa automatica» quando serve, «Riprendi», «Salva ed esci», «Profilo · Pulito/Fluido». B = Riprendi; tenuto 3 s (Riposo) = salva, esce e Riposo; Menu e Home non portano via dalla partita. La pausa sta subito sopra GbPlay: una schermata Permesso aperta resta in cima; con `PauseWhy::Perm` la pausa apre subito il Permesso più recente (NEGA preselezionato) se si può rispondere dal display.
- **Capienza**: `MenuList` ha `kMaxItems = 22` voci e la lista «ROM» è tutta di `gbGameItems`: 16 giochi (`kMaxGames`) + il gioco sulla scheda = 17. `gb-list` ne porta fino a 64: oltre 16 si contano come scartati (Task 7, registro). `test_games_menu_full_list_fits` lo controlla.
- **Fine**: `gbEnd` toglie GbLoad, GbPlay e la pausa ovunque siano nella pila (anche sotto un Permesso, che resta), mette il toast e, con `rest`, il Riposo.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_gb_ui/test_main.cpp`:

```cpp
// Giochi Game Boy nell'interfaccia (core/gb_ui.cpp, core/gb_menu.cpp, ui/gb_screens): Menu › Gioco › ROM, caricamento,
// partita, pausa (anche sotto una schermata Permesso), uscita verso il Riposo, bande con la finestra vuota.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/gb_frame.h"
#include "core/menu.h"
#include "core/strbuf.h"
#include "core/ui.h"
#include "gfx/canvas.h"
#include "ui/screens.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const int64_t kEpoch = 1791707400000LL;  // state.time di protocol/examples/state.json
const char *const kA = "0123456789abcdef";
const char *const kB = "fedcba9876543210";

// Menu › Gioco › ROM: i test cercano per etichetta, non per posizione.
int indexOf(const MenuList &l, const char *label) {
  for (int i = 0; i < l.n; i++)
    if (strcmp(l.items[i].label, label) == 0) return i;
  return -1;
}

std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

JsonDocument doc;
StateMsg msg;

struct Bench {
  Ui ui;
  Ms now;
  Bench() : ui(kHeltec213), now(1000) {
    ui.begin(now, wc());
    ui.goLive();
    now += 2601;
    ui.tick(now, wc());
    const std::string js = readFile("../protocol/examples/state.json");
    doc.clear();
    TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
    TEST_ASSERT_TRUE(decodeState(doc.as<JsonVariantConst>(), msg));
    ui.netState(msg, now);
    ui.netAuthed(now);
  }
  WallClock wc() const { return WallClock{kEpoch + 2 * 3600000LL + now}; }
  Model &m() { return ui.model(); }
  ScreenId topId() { return top(m()).id; }
  RefreshKind act(Action a) { return ui.act(a, now += 100, wc()); }
  void openGames() {
    ScreenEntry e = makeScreen(ScreenId::List);
    e.menu = MenuId::GbRoms;  // Menu › Gioco › ROM (piano A)
    pushScreen(m(), e);
  }
  void list(int n) {
    gb::GameEntry g[2] = {};
    strcpy(g[0].id, kA);
    strcpy(g[0].title, "TETRIS");
    g[0].size = 32768;
    strcpy(g[1].id, kB);
    strcpy(g[1].title, "POKEMON GOLD");
    g[1].size = 2097152;
    ui.gbList(g, n, now);
  }
  MenuList items(MenuId id) {
    MenuList l;
    menuList(id, kHeltec213, m(), now, wc(), l);
    return l;
  }
  std::vector<std::string> texts(Canvas<250, 122> &cv) {
    std::vector<std::string> out;
    ui::setTextHook([](const char *s, int, int, int, int, bool, Align, void *ctx) {
      static_cast<std::vector<std::string> *>(ctx)->push_back(s);
    }, &out);
    ui::compose(cv, kHeltec213, m(), 0, now, wc());
    ui::setTextHook(nullptr, nullptr);
    return out;
  }
  bool shows(const char *s) {
    static Canvas<250, 122> cv;
    for (const std::string &t : texts(cv))
      if (t == s) return true;
    return false;
  }
  PermReq req(const char *id) {
    PermReq r;
    memset(&r, 0, sizeof r);
    scopy(r.id, id);
    scopy(r.session, "b2");
    scopy(r.name, "web-app");
    scopy(r.tool, "Bash");
    scopy(r.arg, "npm run test");
    r.at = kEpoch + now - 1000;
    return r;
  }
  void play() {
    ui.gbInstalled(true, "", "");
    list(2);
    ui.gbLoad(kA, now);
    ui.gbPlay(now);
  }
};

}  // namespace

// L'elenco si vede anche col controller scollegato (padLink Off nel banco).
static void test_games_menu_lists_and_marks_installed() {
  static Bench b;
  b.ui.gbInstalled(true, kB, "POKEMON GOLD");
  b.list(2);
  const MenuList l = b.items(MenuId::GbRoms);
  TEST_ASSERT_FALSE(l.truncated);
  const int t = indexOf(l, "TETRIS"), p = indexOf(l, "POKEMON GOLD");
  TEST_ASSERT_TRUE(t >= 0 && p == t + 1);
  TEST_ASSERT_EQUAL(p + 1, l.n);  // i giochi in fondo, l'installato non si ripete
  TEST_ASSERT_EQUAL_STRING("32 KB", l.items[t].val);
  TEST_ASSERT_EQUAL_STRING("sulla scheda", l.items[p].val);
  TEST_ASSERT_EQUAL(int(ActId::GbPick), int(l.items[p].act));
  TEST_ASSERT_EQUAL(1, l.items[p].net);
}

// 16 giochi (il massimo) più un installato che non è nell'elenco: tutto entra nei kMaxItems della lista.
static void test_games_menu_full_list_fits() {
  static Bench b;
  b.ui.gbInstalled(true, "ffffffffffffffff", "VECCHIO");
  static gb::GameEntry g[gb::kMaxGames];
  for (int i = 0; i < gb::kMaxGames; i++) {
    snprintf(g[i].id, sizeof g[i].id, "%016x", i + 1);
    snprintf(g[i].title, sizeof g[i].title, "GIOCO %d", i + 1);
    g[i].size = 65536;
  }
  b.ui.gbList(g, gb::kMaxGames, b.now);
  const MenuList l = b.items(MenuId::GbRoms);
  TEST_ASSERT_FALSE(l.truncated);
  TEST_ASSERT_TRUE(indexOf(l, "GIOCO 16") >= 0);
  TEST_ASSERT_TRUE(indexOf(l, "VECCHIO") >= 0);
  TEST_ASSERT_TRUE(l.n <= kMaxItems);
}

static void test_games_menu_offline_offers_installed_game() {
  static Bench b;
  b.ui.gbInstalled(true, kB, "POKEMON GOLD");  // nessun gb-list: PC lontano
  const MenuList l = b.items(MenuId::GbRoms);
  const int k = indexOf(l, "POKEMON GOLD");
  TEST_ASSERT_TRUE(k >= 0);
  TEST_ASSERT_EQUAL(-1, l.items[k].net);
  b.openGames();
  b.m().sys.padLink = pad::Link::Ready;  // si gioca solo col controller collegato
  top(b.m()).sel = k;
  b.act(Action::Ok);
  char id[gb::kIdLen];
  TEST_ASSERT_EQUAL(int(gb::GbAct::Pick), int(b.ui.takeGbAct(id)));
  TEST_ASSERT_EQUAL_STRING("", id);  // gioco locale
  TEST_ASSERT_EQUAL(int(gb::GbAct::None), int(b.ui.takeGbAct(id)));  // una volta sola
}

static void test_games_menu_without_partition_or_games() {
  static Bench b;
  b.ui.gbInstalled(false, "", "");
  MenuList l = b.items(MenuId::GbRoms);
  TEST_ASSERT_TRUE(indexOf(l, "Giochi non disponibili") >= 0);
  b.ui.gbInstalled(true, "", "");
  l = b.items(MenuId::GbRoms);
  const int k = indexOf(l, "Giochi sul PC");
  TEST_ASSERT_TRUE(k >= 0);
  TEST_ASSERT_EQUAL_STRING("PC non raggiungibile", l.items[k].val);
  b.list(0);
  l = b.items(MenuId::GbRoms);
  TEST_ASSERT_TRUE(indexOf(l, "Nessun gioco sul PC") >= 0);
}

static void test_pick_from_list_requests_id() {
  static Bench b;
  b.ui.gbInstalled(true, "", "");
  b.list(2);
  b.openGames();
  b.m().sys.padLink = pad::Link::Ready;
  top(b.m()).sel = indexOf(b.items(MenuId::GbRoms), "POKEMON GOLD");
  b.act(Action::Ok);
  char id[gb::kIdLen];
  TEST_ASSERT_EQUAL(int(gb::GbAct::Pick), int(b.ui.takeGbAct(id)));
  TEST_ASSERT_EQUAL_STRING(kB, id);
}

// Senza controller collegato l'elenco c'è, ma scegliere un gioco mostra il toast e non parte.
static void test_pick_without_controller_shows_toast() {
  static Bench b;
  b.ui.gbInstalled(true, "", "");
  b.list(2);
  b.openGames();
  top(b.m()).sel = indexOf(b.items(MenuId::GbRoms), "TETRIS");
  b.act(Action::Ok);
  char id[gb::kIdLen];
  TEST_ASSERT_EQUAL(int(gb::GbAct::None), int(b.ui.takeGbAct(id)));
  TEST_ASSERT_EQUAL_STRING("Collega il controller per giocare", b.m().toast.msg);
  TEST_ASSERT_EQUAL(int(MenuId::GbRoms), int(top(b.m()).menu));
}

static void test_load_progress_and_cancel() {
  static Bench b;
  b.ui.gbInstalled(true, "", "");
  b.list(2);
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(b.ui.gbLoad(kB, b.now)));
  TEST_ASSERT_EQUAL(ScreenId::GbLoad, b.topId());
  TEST_ASSERT_TRUE(b.shows("POKEMON GOLD"));
  TEST_ASSERT_TRUE(b.shows("Scarico il gioco"));
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(b.ui.gbProgress(gb::LoadStep::Download, 1000, 2097152, b.now + 500)));
  TEST_ASSERT_EQUAL(int(RefreshKind::Partial), int(b.ui.gbProgress(gb::LoadStep::Download, 210000, 2097152, b.now + 2500)));
  TEST_ASSERT_TRUE(b.shows("10%"));
  TEST_ASSERT_TRUE(b.shows("205 / 2048 KB"));
  TEST_ASSERT_EQUAL(int(RefreshKind::Partial), int(b.ui.gbProgress(gb::LoadStep::Verify, 2097152, 2097152, b.now + 2600)));
  TEST_ASSERT_TRUE(b.shows("Verifico il gioco"));
  b.act(Action::Next);  // un gesto qualunque non annulla
  char id[gb::kIdLen];
  TEST_ASSERT_EQUAL(int(gb::GbAct::None), int(b.ui.takeGbAct(id)));
  b.act(Action::Back);
  TEST_ASSERT_EQUAL(int(gb::GbAct::Cancel), int(b.ui.takeGbAct(id)));
  b.ui.gbEnd("Caricamento annullato", false, b.now);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL_STRING("Caricamento annullato", b.m().toast.msg);
}

static void test_play_bands_leave_window_blank() {
  static Bench b;
  b.play();
  TEST_ASSERT_TRUE(b.ui.gbPlaying());
  b.m().sys.batt = 12;
  b.m().gb.lowBand = true;
  static Canvas<250, 122> cv;
  const std::vector<std::string> t = b.texts(cv);
  bool title = false, low = false, pause = false;
  for (const std::string &s : t) {
    title = title || s == "TETRIS";
    low = low || s == "Batteria";
    pause = pause || s == "= PAUSA";
  }
  TEST_ASSERT_TRUE(title);
  TEST_ASSERT_TRUE(low);
  TEST_ASSERT_TRUE(pause);
  for (int y = 0; y < 122; y++)
    for (int x = gb::kWinX; x < gb::kWinX + gb::kWinW; x++)
      if (cv.get(x, y)) TEST_FAIL_MESSAGE("le bande sconfinano nella finestra del gioco");
}

static void test_prg_or_any_gesture_in_play_asks_pause() {
  static Bench b;
  b.play();
  b.act(Action::Ok);
  char id[gb::kIdLen];
  TEST_ASSERT_EQUAL(int(gb::GbAct::Pause), int(b.ui.takeGbAct(id)));
  TEST_ASSERT_EQUAL(ScreenId::GbPlay, b.topId());  // la pausa la apre il runtime con gbPaused
}

static void test_pause_menu_rows_and_actions() {
  static Bench b;
  b.play();
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(b.ui.gbPaused(gb::PauseWhy::Controller, b.now)));
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL(int(MenuId::GbPause), int(top(b.m()).menu));
  MenuList l = b.items(MenuId::GbPause);
  TEST_ASSERT_EQUAL_STRING("Controller scollegato", l.items[0].label);
  TEST_ASSERT_EQUAL_STRING("Riprendi", l.items[1].label);
  TEST_ASSERT_EQUAL(1, top(b.m()).sel);  // su Riprendi
  TEST_ASSERT_EQUAL_STRING("Pulito", l.items[3].val);
  char id[gb::kIdLen];
  top(b.m()).sel = 3;
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(gb::GbAct::Profile), int(b.ui.takeGbAct(id)));
  TEST_ASSERT_TRUE(b.m().gb.smooth);
  TEST_ASSERT_EQUAL_STRING("Fluido", b.items(MenuId::GbPause).items[3].val);
  b.act(Action::Menu);  // niente Home con la partita aperta
  TEST_ASSERT_EQUAL(int(MenuId::GbPause), int(top(b.m()).menu));
  b.act(Action::Back);  // B / pressione lunga = Riprendi
  TEST_ASSERT_EQUAL(int(gb::GbAct::Resume), int(b.ui.takeGbAct(id)));
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(b.ui.gbResumed(b.now)));
  TEST_ASSERT_EQUAL(ScreenId::GbPlay, b.topId());
  b.ui.gbPaused(gb::PauseWhy::User, b.now);
  top(b.m()).sel = 1;
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(gb::GbAct::Exit), int(b.ui.takeGbAct(id)));
  b.act(Action::Sleep);
  TEST_ASSERT_EQUAL(int(gb::GbAct::ExitRest), int(b.ui.takeGbAct(id)));
}

static void test_perm_pause_opens_perm_above_pause() {
  static Bench b;
  b.play();
  b.ui.netPermRequest(b.req("8f1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e"), b.now, kEpoch + b.now);
  TEST_ASSERT_EQUAL(ScreenId::GbPlay, b.topId());  // la richiesta da sola non tocca il pannello del gioco
  b.ui.gbPaused(gb::PauseWhy::Perm, b.now);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL(1, top(b.m()).choice);  // NEGA preselezionato
  b.act(Action::Back);  // annulla: la richiesta resta aperta, si torna alla pausa
  TEST_ASSERT_EQUAL(int(MenuId::GbPause), int(top(b.m()).menu));
  // Una seconda richiesta mentre è già in pausa: la mostra gbShowPerm.
  b.ui.netPermRequest(b.req("0011223344556677889900aabbccddee"), b.now, kEpoch + b.now);
  b.ui.gbShowPerm(b.now);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING("0011223344556677889900aabbccddee", top(b.m()).rid);
}

static void test_end_removes_game_screens_even_under_perm_and_rests() {
  static Bench b;
  b.play();
  b.ui.netPermRequest(b.req("8f1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e"), b.now, kEpoch + b.now);
  b.ui.gbPaused(gb::PauseWhy::Perm, b.now);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  b.ui.gbEnd("Partita salvata", true, b.now);  // 10 min in pausa
  TEST_ASSERT_FALSE(b.ui.gbOnStack());
  TEST_ASSERT_EQUAL(ScreenId::Sleep, b.topId());
  bool permKept = false;
  for (int k = 0; k < b.m().ui.depth; k++) permKept = permKept || b.m().ui.stack[k].id == ScreenId::Perm;
  TEST_ASSERT_TRUE(permKept);  // la richiesta resta raggiungibile dopo il Riposo
  TEST_ASSERT_EQUAL_STRING("", b.m().gb.current);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_games_menu_lists_and_marks_installed);
  RUN_TEST(test_games_menu_full_list_fits);
  RUN_TEST(test_games_menu_offline_offers_installed_game);
  RUN_TEST(test_games_menu_without_partition_or_games);
  RUN_TEST(test_pick_from_list_requests_id);
  RUN_TEST(test_pick_without_controller_shows_toast);
  RUN_TEST(test_load_progress_and_cancel);
  RUN_TEST(test_play_bands_leave_window_blank);
  RUN_TEST(test_prg_or_any_gesture_in_play_asks_pause);
  RUN_TEST(test_pause_menu_rows_and_actions);
  RUN_TEST(test_perm_pause_opens_perm_above_pause);
  RUN_TEST(test_end_removes_game_screens_even_under_perm_and_rests);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && pio test -e native -f test_gb_ui`
Expected: FAIL in compilazione (`'GbPause' is not a member of 'cal::MenuId'`, `'class cal::Ui' has no member named 'gbList'`).

- [ ] **Step 3: Modello, menu e chiavi**

`firmware/src/core/model.h`: dopo `#include "clock.h"`:

```cpp
#include "gb_model.h"
```

nell'enum `ScreenId`, subito prima di `Count`:

```cpp
  GbLoad,   // giochi Game Boy: scarico e verifica della ROM (sopra tutto, B annulla)
  GbPlay,   // giochi Game Boy: partita in corso (pannello del driver veloce; ui::draw disegna solo le bande)
```

nell'enum `MenuId`, subito prima di `Count` (dopo `Gb, GbPad, GbRoms` del piano A):

```cpp
GbPause,  // giochi: menu di pausa della partita
```

in `struct Model`, dopo l'ultimo campo (`Pet pet;` o quelli aggiunti dal piano A):

```cpp
  GbView gb;    // giochi Game Boy (core/gb_model)
```

`firmware/src/core/model.cpp`: in `screenKey()` aggiungere in fondo all'array `"gbload", "gbplay"`; in `menuKey()` aggiungere in fondo all'array `"gbpause"` (dopo `"gb", "gbpad", "gbroms"` del piano A). Gli array sono indicizzati con l'enum: l'ordine deve essere lo stesso.

`firmware/src/core/menu.h`, nell'enum `ActId` dopo l'ultima voce:

```cpp
  GbPick, GbResume, GbExit, GbProfile,  // giochi: Menu › Gioco › ROM › un gioco (net = indice); pausa
```

`firmware/src/core/menu.cpp`: in `actKey()` aggiungere in fondo all'array `"gbpick", "gbresume", "gbexit", "gbprofile"`.

`firmware/src/core/gb_menu.cpp` (aggancio del piano A: si sostituisce il corpo vuoto, la firma resta quella del contratto):

```cpp
// Giochi Game Boy (piano B): le voci di Menu › Gioco › ROM (MenuId::GbRoms del piano A), sempre, anche col
// controller scollegato (la scelta senza controller la rifiuta Ui::gbActivate). net = indice in m.gb.games;
// −1 = il gioco già sulla scheda, avviabile anche senza PC.
#include "gb_menu.h"

#include <string.h>

#include "strbuf.h"

namespace cal {

namespace {

MenuItem *item(MenuItem *out, int cap, int &n, const char *label, const char *val, const char *help) {
  if (n >= cap) return nullptr;
  MenuItem &it = out[n++];
  memset(&it, 0, sizeof it);
  it.kind = ItemKind::Info;
  it.bars = -1;
  scopy(it.label, label);
  scopy(it.val, val);
  scopy(it.help, help);
  return &it;
}

void pick(MenuItem *out, int cap, int &n, const char *label, const char *val, const char *help, int k) {
  MenuItem *it = item(out, cap, n, label, val, help);
  if (!it) return;
  it->kind = ItemKind::Act;
  it->act = ActId::GbPick;
  it->net = k;
}

}  // namespace

int gbGameItems(const Model &m, Ms now, MenuItem *out, int cap) {
  (void)now;
  const GbView &g = m.gb;
  int n = 0;
  if (!g.partOk) {
    item(out, cap, n, "Giochi non disponibili", "",
         "Manca lo spazio per i giochi: serve un aggiornamento via cavo della memoria della scheda.");
    return n;
  }
  bool installedListed = false;
  char v[kValLen];
  for (int k = 0; k < g.nGames && k < gb::kMaxGames; k++) {
    const gb::GameEntry &e = g.games[k];
    const bool here = g.installed[0] && streq(g.installed, e.id);
    installedListed = installedListed || here;
    if (here) scopy(v, "sulla scheda");
    else Sb(v, sizeof v).i(int((e.size + 1023) / 1024)).s(" KB");
    pick(out, cap, n, e.title, v,
         here ? "Premi per giocare." : "Premi per giocare: il gioco arriva dal PC e resta sulla scheda.", k);
  }
  if (g.installed[0] && !installedListed)
    pick(out, cap, n, g.installedTitle[0] ? g.installedTitle : g.installed, "sulla scheda",
         "Il gioco già sulla scheda: si gioca anche senza PC. Il salvataggio arriva al PC al ritorno.", -1);
  if (!g.nGames && !g.installed[0])
    item(out, cap, n, g.listSeen ? "Nessun gioco sul PC" : "Giochi sul PC", g.listSeen ? "" : "PC non raggiungibile",
         "Carica le ROM dalla webapp (pagina Giochi) o con calamaio gb add.");
  return n;
}

}  // namespace cal
```

In `menuList()`, subito prima di `if (id == MenuId::PetAcc) {`:

```cpp
  if (id == MenuId::GbPause) {
    out.title = "Pausa";
    if (m.gb.pause == gb::PauseWhy::Controller)
      B.info("Controller scollegato", "", "Riaccendi il controller con il tasto Xbox: si ricollega da solo. Poi Riprendi.");
    else if (m.gb.pause == gb::PauseWhy::Idle)
      B.info("Pausa automatica", "", "5 minuti senza tasti. Dopo 10 minuti in pausa la partita si salva e la scheda va a riposo.");
    B.act("Riprendi", "", ActId::GbResume, "Torna alla partita.");
    B.act("Salva ed esci", "", ActId::GbExit, "Salva sulla scheda e manda il salvataggio al PC.");
    B.act("Profilo", m.gb.smooth ? "Fluido" : "Pulito", ActId::GbProfile,
          "Pulito: circa 11 immagini al secondo, meno scie. Fluido: circa 15, più scie.");
    return;
  }
```

- [ ] **Step 4: `cal::Ui`**

`firmware/src/core/ui.h`, nell'enum `NetCmd` dopo `AnchorsNeed, …`:

```cpp
  // Giochi Game Boy (core/gb_*, hw/gb_main): id = id del gioco
  GbPick,           // gb-pick {id}: scelta dalla scheda, il servizio risponde con gb-start
  GbGo,             // il loop ha chiuso l'eventuale partita: la rete installa il gb-start in attesa (id)
  GbLocal,          // gioco già sulla scheda, senza PC: solo verifica (id)
  GbCancel,         // caricamento annullato
  GbState,          // gb-state {playing}: id ('' = null)
  GbPad,            // gb-pad {link}: gbLink
  GbSaveNow,        // salvataggio cambiato all'uscita: in coda per il PC (id)
```

in `struct NetRequest`, dopo `int64_t workEnd; …`:

```cpp
  uint8_t gbLink;        // GbPad: cal::pad::Link
```

nella classe `Ui`, parte pubblica, subito prima di `Model &model() { return m_; }`:

```cpp
  // ---------- Giochi Game Boy (piano B; core/gb_ui.cpp) ----------
  // gb-list arrivato: ridisegna se Menu › Gioco › ROM è a schermo.
  RefreshKind gbList(const gb::GameEntry *games, int n, Ms now);
  // Partizione presente e gioco verificato al suo interno (id '' = nessuno): dal registro all'avvio e dopo ogni verifica.
  void gbInstalled(bool partOk, const char *id, const char *title);
  // gb-start accettato (o gioco locale scelto): schermata GbLoad sopra tutto.
  RefreshKind gbLoad(const char *id, Ms now);
  // Avanzamento: ridisegno (Partial) ogni ~5 % e non prima di 2 s, come otaProgress; cambio di fase subito.
  RefreshKind gbProgress(gb::LoadStep s, uint32_t done, uint32_t total, Ms now);
  // ROM pronta e verificata: GbLoad diventa GbPlay. Da qui il pannello è del driver veloce (None).
  RefreshKind gbPlay(Ms now);
  // Pausa: menu di pausa sopra GbPlay (sotto un'eventuale schermata Permesso). Quick: il pannello torna all'interfaccia.
  RefreshKind gbPaused(gb::PauseWhy w, Ms now);
  // Richiesta di permesso arrivata in pausa: la schermata Permesso subito (NEGA preselezionato), se si può rispondere
  // dal display. Con PauseWhy::Perm la apre già gbPaused.
  RefreshKind gbShowPerm(Ms now);
  // Ripresa: via il menu di pausa. None: il pannello torna al driver veloce.
  RefreshKind gbResumed(Ms now);
  // Fine (uscita, errore, caricamento fallito): via tutte le schermate del gioco; toast msg (nullptr = niente);
  // rest = Riposo (10 min in pausa).
  RefreshKind gbEnd(const char *msg, bool rest, Ms now);
  // Richiesta dal menu o dai gesti (una volta). Pick: id in out ('' = gioco locale già sulla scheda).
  gb::GbAct takeGbAct(char out[gb::kIdLen]);
  // Sostituisce il corpo dello stub del piano A (contratto): vero con la partita a schermo e il pannello al driver
  // veloce; intanto il loop del piano A non mappa più il controller sui menu.
  bool gbPlaying() const { return top(m_).id == ScreenId::GbPlay; }
  bool gbOnStack() const;  // GbLoad, GbPlay o la pausa nella pila
```

nella parte privata, subito dopo `void redraw(RefreshKind k) { … }`:

```cpp
  bool gbInput(Action a, Ms now);        // gesti sulle schermate del gioco: true = gestito (core/gb_ui.cpp)
  void gbActivate(ActId a, int k, Ms now);
  void gbRequest(gb::GbAct a, const char *id = "");
  void gbOpenPerm(Ms now);
```

e in fondo ai campi privati (dopo `bool petChanged_ = false; …`):

```cpp
  gb::GbAct gbAct_ = gb::GbAct::None;  // giochi: richiesta in attesa del loop
  char gbActId_[gb::kIdLen] = "";
```

`firmware/src/core/ui.cpp`: in `netCmdKey()` aggiungere in fondo all'array `"gb-pick", "gb-go", "gb-local", "gb-cancel", "gb-state", "gb-pad", "gb-save-now"`. In `Ui::act()`, subito prima del commento `// M6: calibrazione in corso: solo 3× la interrompe.` (dopo il blocco `if (tp.id == ScreenId::Ota) { … }`):

```cpp
  if (gbInput(a, t)) return finish(t);  // giochi: caricamento, partita, pausa
```

In `Ui::activate()`, subito dopo la riga `if (it.kind == ItemKind::Act && it.act == ActId::Wear) { petWear(it.net, now); return; }`:

```cpp
  if (it.kind == ItemKind::Act && it.act >= ActId::GbPick && it.act <= ActId::GbProfile) { gbActivate(it.act, it.net, now); return; }
```

Create `firmware/src/core/gb_ui.cpp`:

```cpp
// Giochi Game Boy: la parte di cal::Ui che riguarda caricamento, partita e pausa (dichiarata in ui.h). Le regole
// della partita stanno in core/gb_session; qui solo pila delle schermate, testi e richieste per il loop.
#include <string.h>

#include "strbuf.h"
#include "ui.h"

namespace cal {

namespace {

const int kGbRedrawPct = 5;
const Ms kGbRedrawMs = 2000;

bool isGbScreen(const ScreenEntry &e) {
  return e.id == ScreenId::GbLoad || e.id == ScreenId::GbPlay || (e.id == ScreenId::List && e.menu == MenuId::GbPause);
}
bool isPause(const ScreenEntry &e) { return e.id == ScreenId::List && e.menu == MenuId::GbPause; }

int pct(uint32_t done, uint32_t total) { return total ? int(uint64_t(done) * 100 / total) : 0; }

}  // namespace

bool Ui::gbOnStack() const {
  for (int k = 0; k < m_.ui.depth; k++)
    if (isGbScreen(m_.ui.stack[k])) return true;
  return false;
}

void Ui::gbRequest(gb::GbAct a, const char *id) {
  gbAct_ = a;
  scopy(gbActId_, id ? id : "");
}

gb::GbAct Ui::takeGbAct(char out[gb::kIdLen]) {
  const gb::GbAct a = gbAct_;
  scopy(out, gb::kIdLen, gbActId_);
  gbAct_ = gb::GbAct::None;
  gbActId_[0] = 0;
  return a;
}

RefreshKind Ui::gbList(const gb::GameEntry *games, int n, Ms now) {
  req_ = RefreshKind::None;
  GbView &g = m_.gb;
  g.nGames = n < gb::kMaxGames ? n : gb::kMaxGames;
  for (int k = 0; k < g.nGames; k++) g.games[k] = games[k];
  g.listSeen = true;
  const ScreenEntry &tp = top(m_);
  if (tp.id == ScreenId::List && tp.menu == MenuId::GbRoms) redraw(RefreshKind::Partial);
  return finish(now);
}

void Ui::gbInstalled(bool partOk, const char *id, const char *title) {
  m_.gb.partOk = partOk;
  scopy(m_.gb.installed, id ? id : "");
  scopy(m_.gb.installedTitle, title ? title : "");
}

RefreshKind Ui::gbLoad(const char *id, Ms now) {
  req_ = RefreshKind::None;
  GbView &g = m_.gb;
  scopy(g.current, id);
  g.title[0] = 0;
  for (int k = 0; k < g.nGames; k++)
    if (streq(g.games[k].id, id)) scopy(g.title, g.games[k].title);
  if (!g.title[0]) scopy(g.title, streq(g.installed, id) && g.installedTitle[0] ? g.installedTitle : id);
  g.step = gb::LoadStep::Download;
  g.done = g.total = 0;
  g.drawnPct = 0;
  g.drawnAt = now;
  g.pause = gb::PauseWhy::None;
  // Sopra tutto, come l'aggiornamento: una richiesta di permesso arriva comunque sopra (netPermRequest).
  while (m_.ui.depth > 1 && isGbScreen(top(m_))) popScreen(m_);
  if (!pushScreen(m_, makeScreen(ScreenId::GbLoad))) {
    resetStack(m_, makeScreen(ScreenId::Home));
    pushScreen(m_, makeScreen(ScreenId::GbLoad));
  }
  setAction("Gioco: caricamento");
  redraw(RefreshKind::Quick);
  return finish(now);
}

RefreshKind Ui::gbProgress(gb::LoadStep s, uint32_t done, uint32_t total, Ms now) {
  req_ = RefreshKind::None;
  GbView &g = m_.gb;
  const bool stepChanged = s != g.step;
  g.step = s;
  g.done = done > total ? total : done;
  g.total = total;
  if (top(m_).id != ScreenId::GbLoad) return RefreshKind::None;
  const int p = pct(g.done, g.total);
  if (!stepChanged && (p - g.drawnPct < kGbRedrawPct || now - g.drawnAt < kGbRedrawMs)) return RefreshKind::None;
  g.drawnPct = p;
  g.drawnAt = now;
  redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::gbPlay(Ms now) {
  req_ = RefreshKind::None;
  // GbLoad (in cima o sotto un permesso) diventa GbPlay al suo posto.
  for (int k = 0; k < m_.ui.depth; k++)
    if (m_.ui.stack[k].id == ScreenId::GbLoad) m_.ui.stack[k] = makeScreen(ScreenId::GbPlay);
  if (!inStack(ScreenId::GbPlay)) pushScreen(m_, makeScreen(ScreenId::GbPlay));
  m_.gb.pause = gb::PauseWhy::None;
  setAction("Gioco: partita");
  return RefreshKind::None;
}

RefreshKind Ui::gbPaused(gb::PauseWhy w, Ms now) {
  req_ = RefreshKind::None;
  m_.gb.pause = w;
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = MenuId::GbPause;
  e.sel = (w == gb::PauseWhy::Controller || w == gb::PauseWhy::Idle) ? 1 : 0;  // su «Riprendi» (dopo la riga d'avviso)
  // Subito sopra GbPlay: una schermata Permesso già aperta resta in cima.
  int at = -1;
  for (int k = 0; k < m_.ui.depth; k++)
    if (m_.ui.stack[k].id == ScreenId::GbPlay) at = k;
  if (at < 0 || m_.ui.depth >= kStackCap) return RefreshKind::None;
  if (at + 1 < m_.ui.depth && isPause(m_.ui.stack[at + 1])) {
    m_.ui.stack[at + 1].sel = e.sel;  // già in pausa: aggiorna solo la riga d'avviso
  } else {
    memmove(&m_.ui.stack[at + 2], &m_.ui.stack[at + 1], sizeof(ScreenEntry) * size_t(m_.ui.depth - at - 1));
    m_.ui.stack[at + 1] = e;
    m_.ui.depth++;
  }
  setAction("Gioco: pausa");
  redraw(RefreshKind::Quick);
  if (w == gb::PauseWhy::Perm) gbOpenPerm(now);
  return finish(now);
}

void Ui::gbOpenPerm(Ms now) {
  if (!m_.nPerms || !m_.set.permOn || top(m_).id == ScreenId::Perm) return;
  const PermReq &q = m_.perms[m_.nPerms - 1];  // la più recente
  int i = -1;
  for (int k = 0; k < m_.nSessions; k++)
    if (streq(m_.sessions[k].id, q.session)) i = k;
  if (i < 0 || !canAnswer(m_.sessions[i])) return;  // si risponde nel terminale (o dalla Home dopo la partita)
  ScreenEntry e = makeScreen(ScreenId::Perm);
  e.i = i;
  e.choice = 1;  // §7: NEGA preselezionato
  scopy(e.rid, q.id);
  if (!pushScreen(m_, e)) return;
  permWaitText(now, permSince(m_, e), permText_, sizeof permText_);
  redraw(RefreshKind::Quick);
  setAction("Permesso durante la partita: NEGA preselezionato");
}

RefreshKind Ui::gbShowPerm(Ms now) {
  req_ = RefreshKind::None;
  gbOpenPerm(now);
  return finish(now);
}

RefreshKind Ui::gbResumed(Ms now) {
  req_ = RefreshKind::None;
  if (isPause(top(m_))) popScreen(m_);
  m_.gb.pause = gb::PauseWhy::None;
  setAction("Gioco: si riprende");
  (void)now;
  return RefreshKind::None;
}

RefreshKind Ui::gbEnd(const char *msg, bool rest, Ms now) {
  req_ = RefreshKind::None;
  int d = 0;
  for (int k = 0; k < m_.ui.depth; k++)
    if (!isGbScreen(m_.ui.stack[k])) m_.ui.stack[d++] = m_.ui.stack[k];
  if (d == 0) {
    m_.ui.stack[0] = makeScreen(ScreenId::Home);
    d = 1;
  }
  m_.ui.depth = d;
  GbView &g = m_.gb;
  g.current[0] = 0;
  g.title[0] = 0;
  g.pause = gb::PauseWhy::None;
  g.lowBand = false;
  if (rest && top(m_).id != ScreenId::Sleep) pushScreen(m_, makeScreen(ScreenId::Sleep));
  if (msg && msg[0]) toast(m_, msg, now);
  setAction(msg && msg[0] ? msg : "Gioco: fine");
  redraw(RefreshKind::Quick);
  return finish(now);
}

bool Ui::gbInput(Action a, Ms now) {
  const ScreenEntry &tp = top(m_);
  if (tp.id == ScreenId::GbLoad) {
    if (a == Action::Back) {
      gbRequest(gb::GbAct::Cancel);
      setAction("Caricamento annullato");
    } else {
      setAction("Caricamento del gioco: B o pressione lunga per annullare");
    }
    return true;
  }
  if (tp.id == ScreenId::GbPlay) {  // il tasto PRG durante la partita: pausa
    gbRequest(gb::GbAct::Pause);
    setAction("Pausa");
    return true;
  }
  if (!isPause(tp)) return false;
  switch (a) {
    case Action::Back: gbRequest(gb::GbAct::Resume); setAction("Riprendi"); return true;
    case Action::Sleep: gbRequest(gb::GbAct::ExitRest); setAction("Salva ed esci, poi Riposo"); return true;
    case Action::Menu:
    case Action::Home: setAction("Prima Riprendi o Salva ed esci"); return true;
    default: return false;  // su, giù, scegli: come ogni lista
  }
  (void)now;
}

void Ui::gbActivate(ActId a, int k, Ms now) {
  GbView &g = m_.gb;
  switch (a) {
    case ActId::GbPick:
      if (m_.sys.padLink != pad::Link::Ready) {  // l'elenco si vede sempre, ma si gioca solo col controller
        toast(m_, "Collega il controller per giocare", now);
        setAction("Collega il controller per giocare");
        redraw(RefreshKind::Partial);
        break;
      }
      if (k >= 0 && k < g.nGames) gbRequest(gb::GbAct::Pick, g.games[k].id);
      else gbRequest(gb::GbAct::Pick, "");  // il gioco già sulla scheda, senza PC
      setAction("Gioco scelto");
      break;
    case ActId::GbResume: gbRequest(gb::GbAct::Resume); setAction("Riprendi"); break;
    case ActId::GbExit: gbRequest(gb::GbAct::Exit); setAction("Salva ed esci"); break;
    case ActId::GbProfile:
      g.smooth = !g.smooth;
      gbRequest(gb::GbAct::Profile);
      setAction(g.smooth ? "Profilo: Fluido" : "Profilo: Pulito");
      redraw(RefreshKind::Partial);
      break;
    default: break;
  }
  (void)now;
}

}  // namespace cal
```

- [ ] **Step 5: Schermate**

`firmware/src/ui/screens.h`, dopo `void setTextHook(TextHook fn, void *ctx);`:

```cpp

// T() per le schermate scritte in altri file (ui/gb_screens): stesso disegno, stesso registro dei testi.
void text(Surface &c, const char *s, int x, int y, int size, const TextOpts &o);
```

`firmware/src/ui/screens.cpp`: `#include "ui/gb_screens.h"` dopo `#include "ui/screens.h"`; subito prima di `void setTextHook(TextHook fn, void *ctx) {`:

```cpp
void text(Surface &c, const char *s, int x, int y, int size, const TextOpts &o) {
  T(c, s, x, y, size, O{o.weight, o.pix, o.align, o.ink, o.max});
}
```

e in `drawScreen()`, subito dopo la riga `if (tp.id == ScreenId::Ota && m.ota.phase != cal::OtaPhase::None) { … }`:

```cpp
  if (tp.id == ScreenId::GbLoad) { gbLoadScreen(c, dev, m); drawToast(c, dev, m, now); return; }
  if (tp.id == ScreenId::GbPlay) { gbBands(c, dev, m); return; }  // niente toast: coprirebbe la finestra del gioco
```

Create `firmware/src/ui/gb_screens.h`:

```cpp
// Giochi Game Boy: schermate nuove (non nel simulatore, annotate in DECISIONI). GbLoad: scarico e verifica della
// ROM. GbPlay: solo le due bande ai lati (57 px): la finestra al centro (x 57–192) resta bianca e la riempie
// core/gb_frame con il fotogramma del gioco.
#pragma once
#include "core/model.h"
#include "gfx/canvas.h"

namespace ui {

void gbLoadScreen(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m);
void gbBands(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m);

}  // namespace ui
```

Create `firmware/src/ui/gb_screens.cpp`:

```cpp
#include "ui/gb_screens.h"

#include <stdio.h>

#include "core/strbuf.h"
#include "gfx/prims.h"
#include "gfx/text.h"
#include "ui/screens.h"

namespace ui {

namespace {

TextOpts opts(int weight, bool pix = false, Align a = Align::Left, bool ink = true, int max = 0) {
  TextOpts o;
  o.weight = weight;
  o.pix = pix;
  o.align = a;
  o.ink = ink;
  o.max = max;
  return o;
}

const char *stepText(cal::gb::LoadStep s) {
  switch (s) {
    case cal::gb::LoadStep::Download: return "Scarico il gioco";
    case cal::gb::LoadStep::Verify: return "Verifico il gioco";
    case cal::gb::LoadStep::Start: return "Avvio…";
  }
  return "";
}

}  // namespace

void gbLoadScreen(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m) {
  const cal::GbView &g = m.gb;
  const int W = dev.W;
  text(c, "GIOCO", 6, 4, 14, opts(700));
  text(c, g.title, W - 6, 7, 10, opts(600, false, Align::Right, true, 150));
  text(c, stepText(g.step), 6, 38, 13, opts(700));
  const bool dl = g.step == cal::gb::LoadStep::Download;
  const int p = dl ? (g.total ? int(uint64_t(g.done) * 100 / g.total) : 0) : 100;
  bigBar(c, 6, 60, W - 12, 16, p);
  char t[40];
  snprintf(t, sizeof t, "%d%%", p);
  text(c, t, 6, 90, 16, opts(700));
  if (dl && g.total) {
    snprintf(t, sizeof t, "%lu / %lu KB", (unsigned long)(g.done / 1024), (unsigned long)((g.total + 1023) / 1024));
    text(c, t, W - 6, 94, 10, opts(600, false, Align::Right));
  }
  text(c, "B = ANNULLA", 6, 111, 8, opts(700, true));
}

void gbBands(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m) {
  const cal::GbView &g = m.gb;
  const int W = dev.W;
  const int L = 57, R = W - 57;  // bordi della finestra del gioco: x 57–192
  dotsV(c, L - 2, 0, dev.H - 1);
  dotsV(c, R + 1, 0, dev.H - 1);
  // Sinistra: batteria, avviso, PC.
  battIcon(c, 4, 5, 1, m.sys.batt, true);
  char t[16];
  snprintf(t, sizeof t, "%d%%", int(m.sys.batt + 0.5));
  text(c, t, 23, 3, 10, opts(600));
  if (g.lowBand) {
    c.fillRect(0, 22, L - 3, 26, true);
    text(c, "Batteria", 3, 24, 10, opts(700, false, Align::Left, false));
    text(c, "bassa", 3, 35, 10, opts(700, false, Align::Left, false));
  }
  text(c, m.sys.pc ? "PC ✓" : "PC ✗", 4, 108, 10, opts(600));
  // Destra: titolo e come si mette in pausa.
  char lines[3][96];
  const int n = wrapLines(lines, 3, g.title, 10, W - R - 6, 700);
  for (int i = 0; i < n && i < 3; i++) text(c, lines[i], R + 4, 4 + 12 * i, 10, opts(700, false, Align::Left, true, W - R - 6));
  text(c, "START +", R + 4, 86, 8, opts(700, true));
  text(c, "SELECT", R + 4, 97, 8, opts(700, true));
  text(c, "= PAUSA", R + 4, 108, 8, opts(700, true));
}

}  // namespace ui
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd firmware && pio test -e native`
Expected: tutti PASSED, `test_gb_ui` con 12 test; `test_ui`, `test_m5_ui`, `test_core_golden` invariati (le schermate esistenti non cambiano: nel simulatore Menu › Gioco ha solo «Alto o basso», `gbGameItems` si chiama solo per `MenuId::GbRoms`).

- [ ] **Step 7: DECISIONI e commit**

```markdown
- **Giochi, interfaccia** (schermate nuove, non nel simulatore): Menu › Gioco › ROM elenca i giochi del PC («sulla scheda» o la dimensione), sempre, anche senza controller (scegliendo un gioco senza controller: «Collega il controller per giocare»); il gioco già sulla scheda resta giocabile senza PC. Caricamento a tutto schermo (GIOCO, Scarico/Verifico/Avvio, barra, «B = ANNULLA»). In partita solo bande di 57 px: batteria, «Batteria bassa» sotto il 15 %, PC ✓/✗, titolo, «START + SELECT = PAUSA»; niente toast. Pausa = lista «Riprendi», «Salva ed esci», «Profilo Pulito/Fluido», con «Controller scollegato» o «Pausa automatica» quando serve; B riprende, tenuto 3 s salva ed esce nel Riposo. Un permesso nuovo mette in pausa e apre subito la schermata Permesso (NEGA preselezionato); si può riprendere senza rispondere (la richiesta resta aperta).
```

```bash
git add firmware/src/core/model.h firmware/src/core/model.cpp firmware/src/core/menu.h firmware/src/core/menu.cpp \
  firmware/src/core/ui.h firmware/src/core/ui.cpp firmware/src/core/gb_ui.cpp firmware/src/ui/gb_screens.h \
  firmware/src/ui/gb_screens.cpp firmware/src/ui/screens.h firmware/src/ui/screens.cpp firmware/test/test_gb_ui docs/DECISIONI.md
git commit -m "Giochi: menu, caricamento, partita e pausa nell'interfaccia"
```

---
### Task 12: Partizione, NVS, LittleFS, HTTP; emulatore e pannello sui due core

**Prerequisito:** piano A, `firmware/src/hw/fastink.{h,cpp}`.

**Files:**
- Create: `firmware/src/hw/gb_hw.h`, `firmware/src/hw/gb_hw.cpp`
- Create: `firmware/src/hw/gb_run.h`, `firmware/src/hw/gb_run.cpp`

**Interfaces:**
- Consumes: `RomFlash`, `RomStore`, `RangeSource`, `RomRecord`, `packRecord/unpackRecord`, `kSector` (Task 8); `SaveFiles`, `SaveInfo` (Task 9); `Emu`, `kCartRamMax` (Task 3); `composeFrame`, `kScreenBytes` (Task 4); `FrameMailbox`, `Pacer` (Task 5); `cal::fastink::*` (piano A); `avatarCrc32`; `cry::Sha256Stream`.
- Produces:
  - `namespace gbhw`: `bool begin()`, `bool partitionOk()`, `RomFlash &flash()`, `RomStore &store()`, `RangeSource &source()`, `SaveFiles &saves()`, `void romTitle(char out[17])`, `const uint8_t *mapRom(uint32_t size)`, `void unmapRom()`, `bool loadSmooth()`, `void saveSmooth(bool)`, `uint8_t loadCleanMin()`, `bool readSave(id, dst, n)`, `bool writeSave(id, src, n)`, `int putSave(host, port, token, id, size)`, `int getSave(host, port, token, id)`.
  - `namespace gbrun`: `kHeapMargin = 24576`, `kCleanMinDefault = 10`, `void setCleanEvery(uint8_t minutes)`, `bool begin(const char *id, uint32_t romSize, const char **why)`, `void resume(bool smooth, bool rot, bool inv, const uint8_t *bands, int64_t nowUs)`, `void pause()`, `bool running()`, `bool step(uint8_t keys, int64_t nowUs)`, `void setBands(const uint8_t *)`, `bool end()`, `bool active()`.

Dettagli che contano:
- **Partizione**: `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "gbrom")`; assente = tabella vecchia, giochi spenti con il loro messaggio. Scrittura per settori (`esp_partition_erase_range` + `esp_partition_write`), **rifiutata** mentre la ROM è mappata (`gMapped`): la rete non può mai scrivere sotto un gioco in corso. SHA-256 rileggendo a blocchi da 1 KB sullo stack del task di rete (12 KB).
- **Mappatura**: `esp_partition_mmap(…, SPI_FLASH_MMAP_DATA, …)` a pagine MMU da 64 KB; la ROM non si copia mai in RAM (non c'è PSRAM). Letture via cache dati (32 KB): vedi Review Focus 1.
- **NVS** «calamaio-gb»: `rom` (registro di 62 byte), `prof` (0 = Pulito, 1 = Fluido), `clean` (minuti fra due pulizie complete in gioco, 0 = mai; assente = 10, valore da tarare nel Task 15). Nessuna chiave nuova in `calamaio-*` esistenti; `pad` è del piano A.
- **LittleFS** `/gb/<id>.sav` e `/gb/<id>.ack`, scritti con file temporaneo `/gb/tmp` e rinomina (tutto o niente); `/gb/tmp` rimasto da un riavvio si cancella in `begin()`. CRC-32 IEEE a pezzi (stesso valore di `avatarCrc32` e di `hash/crc32` del servizio).
- **HTTP** come `hw/ota_hw`: HTTP/1.0, niente chunked, timeout 5 s; `Range: bytes=N-` solo con N > 0; PUT del salvataggio in streaming dal file. Il link (con il token) va solo sulla seriale con «link monouso», mai nel registro remoto con il token: le righe `[gb] GET rom (link monouso) …` non stampano l'URL.
- **Runtime**: tutto sullo heap solo durante la partita (`Ctx` ~21,5 KB con fotogramma, triplo buffer e bande; contesto Peanut-GB 17 KB; RAM della cartuccia ≤ 32 KB). Prima di allocare: heap libero ≥ `sizeof(Ctx)` + 17 KB + 32 KB + 24 KB di margine, altrimenti «Memoria insufficiente». ROM rifiutata dall'emulatore → «Gioco non supportato» (registro con `loadKey`). `frameSkip(true)`: Peanut-GB disegna un fotogramma su due (il pannello ne mostra ~11–15 al secondo).
- **Task «gb-ink»** sul core 0, priorità 1 (sotto il task di rete, priorità 2), stack 4 KB: prende il fotogramma più recente, `fastink::submit`, poi `fastink::tick()` finché non è `settled()`; dopo ogni passo `vTaskDelay(1)` per il task idle del core 0 (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 = 1` nel core 2.0.17). Ogni 10 s una riga `[gb] pannello: N immagini in 10 s, fotogrammi emulati persi M`. Solo questo task chiama `fastink::*` mentre gira; `begin`, `end` e il primo `cleanFull` li chiama il loop a task fermo. **Pulizia periodica** (contratto): ogni `clean` minuti di partita il task stesso chiama `fastink::cleanFull()` (un completo di serie, ~2 s in cui il pannello resta fermo; l'emulazione sul core 1 continua). Alla pausa e all'uscita la pulizia è il completo di `fastink::end()`, senza un `cleanFull()` in più.
- **Inverti e Ruota** (contratto: `fastink::submit` non li applica): la rotazione la fa `composeFrame`, l'inversione `gbrun` su tutto il fotogramma composto (bande comprese) prima di pubblicarlo.
- **Ripresa**: `fastink::begin(profilo)`, primo fotogramma, `fastink::cleanFull()` (stato dei pixel noto), poi il task. **Pausa**: task fermo, `fastink::end()` (pulizia completa, pannello al driver di serie) e salvataggio in LittleFS se la RAM è cambiata (una pausa non manda niente al PC: lo fa l'uscita).

- [ ] **Step 1: Write the code**

Create `firmware/src/hw/gb_hw.h`:

```cpp
// Giochi Game Boy sulla scheda: partizione «gbrom» (partitions_gb.csv), registro della ROM e profilo nella NVS
// («calamaio-gb»), salvataggi in LittleFS (/gb, montato da avfs::begin), HTTP verso il servizio (ROM con Range,
// PUT e GET dei salvataggi). Le interfacce sono quelle di core/gb_rom e core/gb_save.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/gb_rom.h"
#include "core/gb_save.h"

namespace gbhw {

// Cerca la partizione e crea /gb. false = tabella delle partizioni vecchia: niente giochi (Menu › Gioco › ROM lo dice).
bool begin();
bool partitionOk();

cal::gb::RomFlash &flash();
cal::gb::RomStore &store();
cal::gb::RangeSource &source();
cal::gb::SaveFiles &saves();
// Titolo dall'intestazione della ROM nella partizione (0x134, ≤ 16), '' se non si legge.
void romTitle(char out[17]);
// ROM mappata in memoria (MMU della cache, sola lettura). nullptr = mappatura non riuscita.
const uint8_t *mapRom(uint32_t size);
void unmapRom();

bool loadSmooth();             // profilo del pannello veloce: false = Pulito (predefinito)
void saveSmooth(bool smooth);
uint8_t loadCleanMin();        // minuti fra due pulizie complete in gioco («clean»; 0 = mai; assente = 10)

// Salvataggio della cartuccia: lettura (false = assente o di un'altra dimensione) e scrittura tutto o niente.
bool readSave(const char *id, uint8_t *dst, size_t n);
bool writeSave(const char *id, const uint8_t *src, size_t n);
// HTTP verso il servizio: codice, < 0 errore di rete.
int putSave(const char *host, uint16_t port, const char *token, const char *id, uint32_t size);
// GET /gb/save/<token> → /gb/<id>.sav (tutto o niente) e CRC segnato come già al PC.
int getSave(const char *host, uint16_t port, const char *token, const char *id);

}  // namespace gbhw
```

Create `firmware/src/hw/gb_hw.cpp`:

```cpp
#include "hw/gb_hw.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <string.h>

#include "core/crypto.h"
#include "core/gb_emu.h"
#include "core/gb_model.h"
#include "core/strbuf.h"
#include "gfx/avatar.h"  // avatarCrc32
#include "hw/gb_run.h"   // kCleanMinDefault
#include "hw/logtee.h"

namespace gbhw {

namespace {

const char *const kNs = "calamaio-gb";
const char *const kDir = "/gb";
const char *const kTmp = "/gb/tmp";
const esp_partition_t *gPart = nullptr;
spi_flash_mmap_handle_t gMap = 0;
bool gMapped = false;

class PartFlash : public cal::gb::RomFlash {
 public:
  uint32_t capacity() const override { return gPart ? gPart->size : 0; }
  bool eraseWrite(uint32_t off, const uint8_t *d, size_t n) override {
    if (!gPart || gMapped) return false;  // mai scrivere sotto un gioco in corso
    if (esp_partition_erase_range(gPart, off, cal::gb::kSector) != ESP_OK) return false;
    return esp_partition_write(gPart, off, d, n) == ESP_OK;
  }
  bool sha256(uint32_t n, uint8_t out[32]) override {
    if (!gPart || n > gPart->size) return false;
    cal::cry::Sha256Stream s;
    uint8_t buf[1024];  // stack del task di rete (12 KB)
    for (uint32_t off = 0; off < n; off += sizeof buf) {
      const uint32_t k = n - off < sizeof buf ? n - off : sizeof buf;
      if (esp_partition_read(gPart, off, buf, k) != ESP_OK) return false;
      s.update(buf, k);
    }
    s.finish(out);
    return true;
  }
};

class NvsStore : public cal::gb::RomStore {
 public:
  bool load(cal::gb::RomRecord &r) override {
    Preferences p;
    if (!p.begin(kNs, true)) return false;
    uint8_t b[cal::gb::kRecordLen];
    const size_t n = p.isKey("rom") ? p.getBytes("rom", b, sizeof b) : 0;
    p.end();
    return n && cal::gb::unpackRecord(b, n, r);
  }
  bool save(const cal::gb::RomRecord &r) override {
    uint8_t b[cal::gb::kRecordLen];
    const size_t n = cal::gb::packRecord(r, b, sizeof b);
    Preferences p;
    if (!n || !p.begin(kNs, false)) return false;
    const bool ok = p.putBytes("rom", b, n) == n;
    p.end();
    return ok;
  }
};

// GET con Range: HTTP/1.0, niente chunked (come hw/ota_hw).
class HttpRange : public cal::gb::RangeSource {
 public:
  int open(const char *url, uint32_t from) override {
    close();
    http_.useHTTP10(true);
    http_.setReuse(false);
    http_.setConnectTimeout(5000);
    http_.setTimeout(5000);
    if (!http_.begin(client_, url)) return -1;
    if (from) {
      char h[32];
      snprintf(h, sizeof h, "bytes=%lu-", (unsigned long)from);
      http_.addHeader("Range", h);
    }
    const int code = http_.GET();
    if (code == 200 || code == 206) stream_ = http_.getStreamPtr();
    else http_.end();
    LogSerial.printf("[gb] GET rom (link monouso) da %lu: HTTP %d\n", (unsigned long)from, code);
    return code;
  }
  int read(uint8_t *buf, size_t cap) override {
    if (!stream_) return -1;
    const int av = stream_->available();
    if (av > 0) return stream_->read(buf, size_t(av) < cap ? size_t(av) : cap);
    return stream_->connected() ? 0 : -1;
  }
  void close() override {
    if (stream_ || http_.connected()) http_.end();
    stream_ = nullptr;
  }

 private:
  WiFiClient client_;
  HTTPClient http_;
  WiFiClient *stream_ = nullptr;
};

void path(const char *id, const char *ext, char *out, size_t cap) { snprintf(out, cap, "/gb/%s.%s", id, ext); }

uint32_t fileCrc(File &f, uint32_t &size) {
  // CRC-32 IEEE (come avatarCrc32 e hash/crc32 del servizio), a pezzi: il file può essere di 32 KB.
  uint8_t buf[256];
  uint32_t crc = 0xFFFFFFFFu;
  size = 0;
  for (;;) {
    const int n = f.read(buf, sizeof buf);
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      crc ^= buf[i];
      for (int k = 0; k < 8; k++) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    size += uint32_t(n);
  }
  return ~crc;
}

class FsSaves : public cal::gb::SaveFiles {
 public:
  bool info(const char *id, cal::gb::SaveInfo &out) override {
    char p[32];
    path(id, "sav", p, sizeof p);
    File f = LittleFS.open(p, "r");
    if (!f) return false;
    memset(&out, 0, sizeof out);
    cal::scopy(out.id, id);
    out.crc = fileCrc(f, out.size);
    f.close();
    return true;
  }
  int list(cal::gb::SaveInfo *out, int cap) override {
    File dir = LittleFS.open(kDir);
    int n = 0;
    for (File f = dir.openNextFile(); f && n < cap; f = dir.openNextFile()) {
      const char *name = strrchr(f.name(), '/');
      name = name ? name + 1 : f.name();
      char id[cal::gb::kIdLen];
      const char *dot = strrchr(name, '.');
      f.close();
      if (!dot || strcmp(dot, ".sav") != 0 || dot - name != 16) continue;
      memcpy(id, name, 16);
      id[16] = 0;
      if (info(id, out[n])) n++;
    }
    return n;
  }
  bool acked(const char *id, uint32_t &crc) override {
    char p[32];
    path(id, "ack", p, sizeof p);
    File f = LittleFS.open(p, "r");
    if (!f) return false;
    uint8_t b[4];
    const bool ok = f.read(b, 4) == 4;
    f.close();
    crc = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    return ok;
  }
  bool setAcked(const char *id, uint32_t crc) override {
    char p[32];
    path(id, "ack", p, sizeof p);
    const uint8_t b[4] = {uint8_t(crc), uint8_t(crc >> 8), uint8_t(crc >> 16), uint8_t(crc >> 24)};
    return writeAtomic(p, b, 4);
  }
  static bool writeAtomic(const char *p, const uint8_t *d, size_t n) {
    File f = LittleFS.open(kTmp, "w");
    if (!f) return false;
    const bool ok = f.write(d, n) == n;
    f.close();
    if (!ok) { LittleFS.remove(kTmp); return false; }
    if (LittleFS.exists(p)) LittleFS.remove(p);
    return LittleFS.rename(kTmp, p);
  }
};

PartFlash gFlash;
NvsStore gStore;
HttpRange gSource;
FsSaves gSaves;

}  // namespace

bool begin() {
  gPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "gbrom");
  if (!gPart) {
    LogSerial.println("[gb] partizione gbrom assente: tabella vecchia, giochi spenti (serve il flash via cavo)");
    return false;
  }
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);
  LogSerial.printf("[gb] partizione gbrom 0x%06lx, %lu KB\n", (unsigned long)gPart->address, (unsigned long)(gPart->size / 1024));
  return true;
}

bool partitionOk() { return gPart != nullptr; }
cal::gb::RomFlash &flash() { return gFlash; }
cal::gb::RomStore &store() { return gStore; }
cal::gb::RangeSource &source() { return gSource; }
cal::gb::SaveFiles &saves() { return gSaves; }

void romTitle(char out[17]) {
  out[0] = 0;
  uint8_t b[16];
  if (!gPart || esp_partition_read(gPart, 0x134, b, sizeof b) != ESP_OK) return;
  int n = 0;
  for (int i = 0; i < 16; i++) {
    if (b[i] == 0 || b[i] >= 0x80) break;
    out[n++] = (b[i] >= 0x20 && b[i] < 0x7F) ? char(b[i]) : '?';
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = 0;
}

const uint8_t *mapRom(uint32_t size) {
  if (!gPart || gMapped || size > gPart->size) return nullptr;
  const uint32_t len = (size + SPI_FLASH_MMU_PAGE_SIZE - 1) / SPI_FLASH_MMU_PAGE_SIZE * SPI_FLASH_MMU_PAGE_SIZE;
  const void *p = nullptr;
  const esp_err_t e = esp_partition_mmap(gPart, 0, len, SPI_FLASH_MMAP_DATA, &p, &gMap);
  if (e != ESP_OK) {
    LogSerial.printf("[gb] mappatura della ROM non riuscita: %s\n", esp_err_to_name(e));
    return nullptr;
  }
  gMapped = true;
  return static_cast<const uint8_t *>(p);
}

void unmapRom() {
  if (gMapped) spi_flash_munmap(gMap);
  gMapped = false;
}

bool loadSmooth() {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  const bool s = p.getUChar("prof", 0) == 1;
  p.end();
  return s;
}

void saveSmooth(bool smooth) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putUChar("prof", smooth ? 1 : 0);
  p.end();
}

uint8_t loadCleanMin() {
  Preferences p;
  if (!p.begin(kNs, true)) return gbrun::kCleanMinDefault;
  const uint8_t m = p.getUChar("clean", gbrun::kCleanMinDefault);
  p.end();
  return m;
}

bool readSave(const char *id, uint8_t *dst, size_t n) {
  char p[32];
  path(id, "sav", p, sizeof p);
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  const bool ok = f.size() == n && f.read(dst, n) == n;
  f.close();
  return ok;
}

bool writeSave(const char *id, const uint8_t *src, size_t n) {
  char p[32];
  path(id, "sav", p, sizeof p);
  return FsSaves::writeAtomic(p, src, n);
}

int putSave(const char *host, uint16_t port, const char *token, const char *id, uint32_t size) {
  char p[32];
  path(id, "sav", p, sizeof p);
  File f = LittleFS.open(p, "r");
  if (!f || f.size() != size) return -1;
  char url[128];
  snprintf(url, sizeof url, "http://%s:%u/gb/save/%s", host, unsigned(port), token);
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  if (!http.begin(client, url)) { f.close(); return -1; }
  http.addHeader("Content-Type", "application/octet-stream");
  const int code = http.sendRequest("PUT", &f, size);
  http.end();
  f.close();
  LogSerial.printf("[gb] PUT salvataggio %s (%lu byte, link monouso): HTTP %d\n", id, (unsigned long)size, code);
  return code;
}

int getSave(const char *host, uint16_t port, const char *token, const char *id) {
  char url[128];
  snprintf(url, sizeof url, "http://%s:%u/gb/save/%s", host, unsigned(port), token);
  WiFiClient client;
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  if (!http.begin(client, url)) return -1;
  const int code = http.GET();
  const int len = http.getSize();
  if (code != 200 || len <= 0 || len > int(cal::gb::kCartRamMax)) {
    http.end();
    return code == 200 ? -2 : code;
  }
  uint8_t *buf = static_cast<uint8_t *>(malloc(size_t(len)));
  if (!buf) { http.end(); return -3; }
  WiFiClient *s = http.getStreamPtr();
  int got = 0;
  const uint32_t t0 = millis();
  while (got < len && millis() - t0 < 10000) {
    const int n = s->read(buf + got, size_t(len - got));
    if (n > 0) got += n;
    else delay(2);
  }
  http.end();
  bool ok = got == len;
  if (ok) ok = writeSave(id, buf, size_t(len)) && gSaves.setAcked(id, avatarCrc32(buf, size_t(len)));
  free(buf);
  LogSerial.printf("[gb] GET salvataggio %s dal PC (link monouso): %s\n", id, ok ? "sostituito" : "non riuscito");
  return ok ? 200 : -4;
}

}  // namespace gbhw
```

Create `firmware/src/hw/gb_run.h`:

```cpp
// Giochi Game Boy: la partita sulla scheda. Emulatore nel task del loop (core 1, Pacer a 59,7 Hz), pannello veloce
// (cal::fastink del piano A) in un task «gb-ink» sul core 0; i fotogrammi passano dal triplo buffer
// (core/gb_handoff). Memoria sullo heap solo durante la partita (contesto ~17 KB, fotogrammi ~22 KB, RAM della
// cartuccia fino a 32 KB). La ROM è la partizione «gbrom» mappata in memoria, mai copiata in RAM.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/gb_emu.h"

namespace gbrun {

const size_t kHeapMargin = 24 * 1024;  // heap che resta libero durante la partita (Wi-Fi, NimBLE, WebSocket)
// Pulizia completa periodica durante la partita (contratto). Valore da tarare nel Task 15: minuti di gioco dopo cui
// le scie residue del driver veloce si vedono a occhio. Si cambia con la chiave NVS «calamaio-gb/clean».
const uint8_t kCleanMinDefault = 10;

void setCleanEvery(uint8_t minutes);  // 0 = mai; vale dalla prossima ripresa

// Prepara la partita (ROM mappata, intestazione, RAM della cartuccia dal salvataggio). Non tocca il pannello.
// false → why: testo per lo schermo («Gioco non supportato», «Memoria insufficiente», …).
bool begin(const char *id, uint32_t romSize, const char **why);
// Pannello al driver veloce e task del pannello acceso. bands: schermo intero con le bande (kScreenBytes).
// rot, inv: Ruota e Inverti colori, applicati al fotogramma (fastink::submit non li applica).
void resume(bool smooth, bool rot, bool inv, const uint8_t *bands, int64_t nowUs);
// Task del pannello fermo e pannello restituito pulito (fastink::end). Il salvataggio va in LittleFS se cambiato.
void pause();
bool running();  // task del pannello acceso
// Fotogrammi dovuti con i tasti dati; false = la CPU del Game Boy si è fermata (codice non valido).
bool step(uint8_t keys, int64_t nowUs);
void setBands(const uint8_t *bands);  // bande nuove (batteria, avviso): valgono dal prossimo fotogramma
// Fine: pausa se serve, salvataggio, memoria liberata. true = la RAM della cartuccia è cambiata da begin().
bool end();
bool active();

}  // namespace gbrun
```

Create `firmware/src/hw/gb_run.cpp`:

```cpp
#include "hw/gb_run.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <new>

#include "core/gb_frame.h"
#include "core/gb_handoff.h"
#include "core/strbuf.h"
#include "hw/fastink.h"
#include "hw/gb_hw.h"
#include "hw/logtee.h"

namespace gbrun {

namespace {

struct Ctx {
  cal::gb::Emu emu;                 // ~5,8 KB (fotogramma) + contesto Peanut-GB sullo heap (~17 KB)
  cal::gb::FrameMailbox mb;         // 3 × 3904 byte
  cal::gb::Pacer pacer;
  uint8_t bands[cal::gb::kScreenBytes];
  uint8_t *ram = nullptr;
  size_t ramSize = 0;
  char id[cal::gb::kIdLen] = "";
  bool rot = false;
  bool inv = false;                 // Inverti colori
  bool changed = false;             // RAM cambiata da begin()
};

Ctx *g = nullptr;
TaskHandle_t gInk = nullptr;
std::atomic<bool> gStop{false};
std::atomic<bool> gInkDone{true};
std::atomic<uint32_t> gCleanMs{uint32_t(kCleanMinDefault) * 60000u};

// Inverti colori su tutto il fotogramma composto, bande comprese (fastink::submit non lo fa).
void invertFrame(uint8_t *f) {
  for (size_t i = 0; i < cal::gb::kScreenBytes; i++) f[i] = static_cast<uint8_t>(~f[i]);
}

// Core 0, priorità 1 (sotto il task di rete): prende il fotogramma più recente e spinge il pannello.
// fastink::tick() aspetta BUSY (~50–90 ms): dopo ogni passo vTaskDelay(1) lascia girare il task idle del core 0
// (watchdog dei task, CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 = 1).
void inkLoop(void *) {
  uint32_t frames = 0, t0 = millis(), cleanAt = millis();
  while (!gStop.load()) {
    const uint8_t *f = g->mb.take();
    if (f) {
      cal::fastink::submit(f);
      frames++;
    }
    const uint32_t every = gCleanMs.load();
    if (every && millis() - cleanAt >= every) {  // pulizia periodica (contratto): un completo, poi di nuovo veloce
      cal::fastink::cleanFull();
      cleanAt = millis();
      LogSerial.printf("[gb] pannello: pulizia completa periodica (ogni %lu min)\n", (unsigned long)(every / 60000));
    }
    if (!cal::fastink::settled()) cal::fastink::tick();  // tick() cede già il core (vTaskDelay(1), contratto)
    else vTaskDelay(pdMS_TO_TICKS(4));
    vTaskDelay(1);
    if (millis() - t0 >= 10000) {
      LogSerial.printf("[gb] pannello: %lu immagini in 10 s, fotogrammi emulati persi %lu\n", (unsigned long)frames,
                       (unsigned long)g->pacer.dropped());
      frames = 0;
      t0 = millis();
    }
  }
  gInkDone = true;
  vTaskDelete(nullptr);
}

void stopInk() {
  if (!gInk) return;
  gStop = true;
  while (!gInkDone.load()) delay(2);
  gInk = nullptr;
}

}  // namespace

bool active() { return g != nullptr; }
bool running() { return gInk != nullptr; }
void setCleanEvery(uint8_t minutes) { gCleanMs = uint32_t(minutes) * 60000u; }

bool begin(const char *id, uint32_t romSize, const char **why) {
  if (g) end();
  // Prima i pezzi grandi: se la memoria non basta si dice subito, prima di toccare la ROM.
  const size_t need = sizeof(Ctx) + 17 * 1024 + cal::gb::kCartRamMax + kHeapMargin;
  if (ESP.getFreeHeap() < need) {
    LogSerial.printf("[gb] memoria insufficiente: libera %u, servono %u\n", unsigned(ESP.getFreeHeap()), unsigned(need));
    *why = "Memoria insufficiente";
    return false;
  }
  g = new (std::nothrow) Ctx;
  if (!g) { *why = "Memoria insufficiente"; return false; }
  const uint8_t *rom = gbhw::mapRom(romSize);
  if (!rom) { delete g; g = nullptr; *why = "Gioco non leggibile"; return false; }
  const cal::gb::Load l = g->emu.load(rom, romSize);
  if (l != cal::gb::Load::Ok) {
    LogSerial.printf("[gb] ROM rifiutata dall'emulatore: %s\n", cal::gb::loadKey(l));
    gbhw::unmapRom();
    delete g;
    g = nullptr;
    *why = "Gioco non supportato";
    return false;
  }
  g->ramSize = g->emu.ramSize();
  if (g->ramSize) {
    g->ram = static_cast<uint8_t *>(malloc(g->ramSize));
    if (!g->ram) { end(); *why = "Memoria insufficiente"; return false; }
    if (!gbhw::readSave(id, g->ram, g->ramSize)) memset(g->ram, 0xFF, g->ramSize);
    g->emu.attachRam(g->ram);
  }
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  g->emu.setRtc(lt);       // MBC3: l'orologio della cartuccia parte dall'ora della scheda
  g->emu.frameSkip(true);  // il pannello va a ~11–15 immagini/s: si disegna un fotogramma su due
  cal::scopy(g->id, id);
  LogSerial.printf("[gb] %s pronto: ROM %lu byte, RAM %u byte, heap libero %u\n", id, (unsigned long)romSize,
                   unsigned(g->ramSize), unsigned(ESP.getFreeHeap()));
  return true;
}

void resume(bool smooth, bool rot, bool inv, const uint8_t *bands, int64_t nowUs) {
  if (!g || gInk) return;
  g->rot = rot;
  g->inv = inv;
  memcpy(g->bands, bands, sizeof g->bands);
  g->mb.reset();
  cal::fastink::begin(smooth ? cal::fastink::Profile::Smooth : cal::fastink::Profile::Clean);
  // Primo fotogramma con un refresh completo: si parte da uno stato dei pixel noto.
  cal::gb::composeFrame(g->bands, g->emu.frame(), g->rot, g->mb.back());
  if (g->inv) invertFrame(g->mb.back());
  g->mb.publish();
  cal::fastink::submit(g->mb.take());
  cal::fastink::cleanFull();
  g->pacer.start(nowUs);
  gStop = false;
  gInkDone = false;
  xTaskCreatePinnedToCore(inkLoop, "gb-ink", 4096, nullptr, 1, &gInk, 0);
}

void pause() {
  if (!g) return;
  stopInk();
  cal::fastink::end();  // pulizia completa, pannello di nuovo al driver di serie
  if (g->emu.ramDirty() && g->ram) {
    if (gbhw::writeSave(g->id, g->ram, g->ramSize)) {
      g->emu.clearDirty();
      g->changed = true;
    }
  }
}

bool step(uint8_t keys, int64_t nowUs) {
  if (!g || !gInk) return true;
  const int n = g->pacer.due(nowUs);
  for (int i = 0; i < n; i++)
    if (!g->emu.runFrame(keys)) return false;
  if (n) {
    g->pacer.ran(n);
    cal::gb::composeFrame(g->bands, g->emu.frame(), g->rot, g->mb.back());
    if (g->inv) invertFrame(g->mb.back());
    g->mb.publish();
  }
  return true;
}

void setBands(const uint8_t *bands) {
  if (g) memcpy(g->bands, bands, sizeof g->bands);  // solo il core 1 le legge (composeFrame in step)
}

bool end() {
  if (!g) return false;
  if (gInk) pause();
  else if (g->emu.ramDirty() && g->ram && gbhw::writeSave(g->id, g->ram, g->ramSize)) g->changed = true;
  const bool changed = g->changed;
  g->emu.unload();
  gbhw::unmapRom();
  free(g->ram);
  delete g;
  g = nullptr;
  LogSerial.printf("[gb] partita chiusa%s, heap libero %u\n", changed ? " (salvataggio nuovo)" : "", unsigned(ESP.getFreeHeap()));
  return changed;
}

}  // namespace gbrun
```

- [ ] **Step 2: Build and native tests**

Questi file toccano solo la scheda (`esp_partition_*`, LittleFS, HTTPClient, FreeRTOS): non hanno test in native; la loro logica sta nei Task 3–9 e si prova dal vivo nei Task 15–16.

Run: `cd firmware && pio run -e heltec213 && pio test -e native`
Expected: build SUCCESS (nessun warning nuovo in `gb_hw.cpp`/`gb_run.cpp`); test tutti PASSED.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/hw/gb_hw.h firmware/src/hw/gb_hw.cpp firmware/src/hw/gb_run.h firmware/src/hw/gb_run.cpp
git commit -m "Giochi: partizione, salvataggi, HTTP ed emulatore sui due core"
```

---

### Task 13: Messaggi `gb-*` nel task di rete

**Files:**
- Modify: `firmware/src/net/net.h` (`EvType::Gb*`, `takeGbList`)
- Modify: `firmware/src/net/net.cpp`

**Interfaces:**
- Consumes: `gb_proto` (Task 7), `RomInstaller` (Task 8), `SaveSync` (Task 9), `gbhw::*` (Task 12), `NetCmd::Gb*` e `NetRequest::gbLink` (Task 11).
- Produces: eventi `net::EvType::GbList` (lista con `net::takeGbList(cal::gb::ListMsg *&)`, da liberare con `free`), `GbStart` (`id`), `GbProgress` (`rssi` = `gb::LoadStep`, `time` = `(total << 32) | done`), `GbReady` (`id`, `text` = titolo dell'intestazione), `GbFailed` (`id`, `text` = motivo per lo schermo). Comandi gestiti: `GbPick`, `GbGo`, `GbLocal`, `GbCancel`, `GbState`, `GbPad`, `GbSaveNow`.

Flusso (come `ota-offer` → `OtaGo`): la rete verifica `gb-start` e lo tiene in attesa; il loop chiude l'eventuale partita (salvataggio, ROM non più mappata) e risponde `GbGo`; solo allora la rete scrive nella partizione. L'installatore vive sullo heap solo mentre lavora (16 KB per giro del task). Link scaduto (403/404/410): un solo `gb-pick` automatico per gioco, il servizio risponde con un `gb-start` nuovo e si riprende da `written`. Dopo `auth-ok`: salvataggi da mandare, `gb-state`, `gb-pad`. URL: `http://<pcIp>:<pcPort>/gb/rom/<token>` con indirizzo e porta della connessione WebSocket (porta dei display 8766, mai la 8765 della API locale), come `ota-offer` (contratto). `gb-list` e `gb-save-ok` si accettano solo col mac giusto (`checkList`, `checkSaveOk`), come `gb-start` (`checkStart`).

Questo Task tocca solo il filo: la logica è provata nei Task 7–9; qui la prova è la build e, dal vivo, il Task 15.

- [ ] **Step 1: `net.h`**

In `firmware/src/net/net.h`: `#include "core/gb_proto.h"` dopo `#include "core/avatars.h"`; nell'enum `EvType`, dopo `Workday, …`:

```cpp
  // Giochi Game Boy (core/gb_*, hw/gb_main): id = id del gioco
  GbList,         // gb-list: elenco con takeGbList()
  GbStart,        // gb-start verificato: id. Risposta: NetCmd::GbGo (dopo aver chiuso l'eventuale partita)
  GbProgress,     // rssi = gb::LoadStep, time = (total << 32) | done
  GbReady,        // ROM nella partizione e verificata: id, text = titolo dell'intestazione
  GbFailed,       // id, text = motivo per lo schermo
```

e prima di `bool hasSavedNetwork();`:

```cpp
// Giochi: ultimo gb-list (sullo heap, lo libera chi lo prende).
bool takeGbList(cal::gb::ListMsg *&out);
```

- [ ] **Step 2: `net.cpp`**

Include, dopo `#include "core/crypto.h"`:

```cpp
#include "core/gb_proto.h"
#include "core/gb_rom.h"
#include "core/gb_save.h"
```

dopo `#include "hw/secrets.h"` o accanto agli altri `hw/`: `#include "hw/gb_hw.h"`; dopo `#include <atomic>`: `#include <new>`.

Stato, subito dopo `char gLogBuf[cal::LogStream::kMsgMax + 1]; …`:

```cpp

// Giochi Game Boy (core/gb_rom, core/gb_save, hw/gb_hw): installazione della ROM e salvataggi verso il PC.
QueueHandle_t gGbList = nullptr;
cal::gb::RomInstaller *gRom = nullptr;  // ~4,3 KB sullo heap solo durante l'installazione
cal::gb::StartMsg gGbStart;          // gb-start verificato, in attesa di NetCmd::GbGo
bool gGbStartWaiting = false;
bool gGbAskedToken = false;          // link scaduto: un solo gb-pick automatico per gioco
char gGbSaveTok[cal::gb::kTokenLen] = "";
uint32_t gGbShown = 0;               // byte dell'ultimo GbProgress mandato al loop
char gGbState[cal::gb::kIdLen] = "";
bool gGbStateSet = false;
int gGbPad = -1;
void gbRand(uint8_t *out, size_t n) { esp_fill_random(out, n); }
cal::gb::SaveSync gSaves(gbRand);
```

Dichiarazione in avanti, dopo `void otaAfterAuth();`:

```cpp
void gbAfterAuth();
```

In `NetOut::event`, caso `LinkEvType::Authed`, dopo `otaAfterAuth();`:

```cpp
      gbAfterAuth();
```

Nel `switch` dei messaggi, subito prima di `default:` (quello con «messaggio sconosciuto ignorato»):

```cpp
    case cal::MsgType::GbList: {  // giochi: solo autenticato e col mac giusto (contratto dei piani giochi)
      const char *why = "";
      if (!cal::gb::checkList(root, gLink.authed(), gLink.sessionKey(), &why)) {
        LogSerial.printf("[gb] gb-list scartato: %s\n", why);
        break;
      }
      cal::gb::ListMsg *p = static_cast<cal::gb::ListMsg *>(malloc(sizeof(cal::gb::ListMsg)));
      if (!p || !cal::gb::decodeList(root, *p)) { free(p); LogSerial.println("[gb] gb-list non valido: ignorato"); break; }
      LogSerial.printf("[gb] gb-list: %d giochi%s\n", p->n, p->dropped ? " (alcuni scartati: non validi o oltre 16)" : "");
      if (xQueueSend(gGbList, &p, 0) != pdTRUE) { free(p); break; }
      post(EvType::GbList);
      break;
    }
    case cal::MsgType::GbStart: {
      static cal::gb::StartMsg m;
      const char *why = "";
      if (!cal::gb::decodeStart(root, m, &why) || !cal::gb::checkStart(m, gLink.authed(), gLink.sessionKey(), &why)) {
        LogSerial.printf("[gb] gb-start scartato: %s\n", why);
        break;
      }
      if (gUpd.active() || gOfferWaiting) { LogSerial.println("[gb] gb-start ignorato: aggiornamento in corso"); break; }
      if (!gbhw::partitionOk()) { post(EvType::GbFailed, "", "Manca lo spazio per i giochi", 0, 0, m.id); break; }
      if (strcmp(m.id, gGbStart.id) != 0) gGbAskedToken = false;
      gGbStart = m;
      gGbStartWaiting = true;
      LogSerial.printf("[gb] gb-start %s: %lu byte%s, il loop chiude l'eventuale partita\n", m.id, (unsigned long)m.size,
                       m.saveToken[0] ? ", con salvataggio dal PC" : "");
      post(EvType::GbStart, "", "", 0, 0, m.id);
      break;
    }
    case cal::MsgType::GbSaveOk: {
      cal::gb::SaveOk ok;
      const char *why = "gb-save-ok non valido";
      if (!cal::gb::decodeSaveOk(root, ok) || !cal::gb::checkSaveOk(ok, gLink.authed(), gLink.sessionKey(), &why)) {
        LogSerial.printf("[gb] gb-save-ok scartato: %s\n", why);
        break;
      }
      LogSerial.printf("[gb] gb-save-ok %s crc %08lx\n", ok.id, (unsigned long)ok.crc32);
      gSaves.onSaveOk(ok, gbhw::saves());
      break;
    }
```

Sostituire le due righe

```cpp
// ---------- comandi dall'interfaccia ----------
void handle(const cal::NetRequest &r) {
```

con il blocco qui sotto (il resto di `handle()` resta com'è, dopo lo `switch`):

```cpp
// ---------- giochi Game Boy ----------
void sendGb(const char *buf, size_t n) {
  if (wsConn && n) { ws.sendTXT(buf, n); LogSerial.printf("[ws] %s\n", buf); }
}
void sendGbState() {
  if (!gGbStateSet || !gLink.authed()) return;
  char b[96];
  sendGb(b, cal::gb::encodeState(gGbState, b, sizeof b));
}
void sendGbPad() {
  if (gGbPad < 0 || !gLink.authed()) return;
  char b[96];
  sendGb(b, cal::gb::encodePad(static_cast<cal::pad::Link>(gGbPad), b, sizeof b));
}
void gbAfterAuth() {
  gSaves.onConnected(gbhw::saves());
  sendGbState();
  sendGbPad();
}

// L'installatore vive sullo heap solo mentre serve (settore da 4 KB): nessun costo fuori dai giochi.
bool gbRomNew() {
  if (!gRom) gRom = new (std::nothrow) cal::gb::RomInstaller;
  return gRom != nullptr;
}
void gbRomFree() {
  delete gRom;
  gRom = nullptr;
}

void gbProgress(cal::gb::LoadStep s) {
  gGbShown = gRom->done();
  post(EvType::GbProgress, "", "", int(s), (int64_t(gRom->total()) << 32) | gRom->done(), gRom->id());
}

void gbInstall() {
  char url[cal::gb::kUrlLen];
  snprintf(url, sizeof url, "http://%s:%u/gb/rom/%s", pcIp, unsigned(pcPort), gGbStart.token);
  cal::scopy(gGbSaveTok, gGbStart.saveToken);
  gGbShown = 0;
  if (!gbRomNew()) { post(EvType::GbFailed, "", "Memoria insufficiente", 0, 0, gGbStart.id); return; }
  const cal::gb::RomPhase p = gRom->start(gGbStart.id, gGbStart.sha, gGbStart.size, url, gbhw::flash(), gbhw::store(),
                                         gbhw::source(), cal::Ms(millis()));
  LogSerial.printf("[gb] %s: %s (link monouso, non registrato)\n", gGbStart.id,
                   p == cal::gb::RomPhase::Verify ? "già sulla scheda, verifico" : "scarico");
  if (p == cal::gb::RomPhase::Verify) gbProgress(cal::gb::LoadStep::Verify);
}

struct WsSaves : cal::gb::SaveTransport {
  void send(const char *json, size_t n) override { sendGb(json, n); }
  int put(const char *token, const char *id, uint32_t size) override { return gbhw::putSave(pcIp, pcPort, token, id, size); }
} gSaveNet;

// A ogni giro del task: installazione (16 KB per giro, WebSocket e HTTP restano serviti) e salvataggi verso il PC.
void gbLoop() {
  using cal::gb::RomPhase;
  const RomPhase before = gRom ? gRom->phase() : RomPhase::Idle;
  if (before == RomPhase::Download || before == RomPhase::Verify) {
    const RomPhase p = gRom->step(cal::Ms(millis()));
    if (p == RomPhase::Download && gRom->done() - gGbShown >= 32768) gbProgress(cal::gb::LoadStep::Download);
    if (p == RomPhase::Verify && before == RomPhase::Download) gbProgress(cal::gb::LoadStep::Verify);
    if (p == RomPhase::Ready) {
      char title[17];
      gbhw::romTitle(title);
      if (gGbSaveTok[0]) gbhw::getSave(pcIp, pcPort, gGbSaveTok, gRom->id());  // ripristino dalla webapp
      gGbSaveTok[0] = 0;
      LogSerial.printf("[gb] %s verificato: «%s»\n", gRom->id(), title);
      post(EvType::GbReady, "", title, 0, 0, gRom->id());
      gbRomFree();
    } else if (p == RomPhase::Failed) {
      if (gRom->newToken() && !gGbAskedToken && wsConn && gLink.authed()) {
        gGbAskedToken = true;  // il servizio risponde con un gb-start nuovo: si riprende dal punto salvato
        char b[96];
        sendGb(b, cal::gb::encodePick(gRom->id(), b, sizeof b));
      } else {
        post(EvType::GbFailed, "", gRom->why(), 0, 0, gRom->id());
      }
      gbRomFree();
    }
  }
  gSaves.tick(cal::Ms(millis()), wsConn && gLink.authed(), gbhw::saves(), gSaveNet);
}

// ---------- comandi dall'interfaccia ----------
void handle(const cal::NetRequest &r) {
  switch (r.cmd) {  // giochi
    case cal::NetCmd::GbPick: {
      char b[96];
      sendGb(b, cal::gb::encodePick(r.id, b, sizeof b));
      return;
    }
    case cal::NetCmd::GbGo:
      if (!gGbStartWaiting || strcmp(r.id, gGbStart.id) != 0) return;
      gGbStartWaiting = false;
      gbInstall();
      return;
    case cal::NetCmd::GbLocal: {
      cal::gb::RomRecord rec;
      if (!gbhw::store().load(rec) || !rec.complete || strcmp(rec.id, r.id) != 0) {
        post(EvType::GbFailed, "", "Gioco non sulla scheda", 0, 0, r.id);
        return;
      }
      gGbSaveTok[0] = 0;
      if (!gbRomNew()) { post(EvType::GbFailed, "", "Memoria insufficiente", 0, 0, r.id); return; }
      gRom->start(rec.id, rec.sha, rec.size, "", gbhw::flash(), gbhw::store(), gbhw::source(), cal::Ms(millis()));
      gbProgress(cal::gb::LoadStep::Verify);
      return;
    }
    case cal::NetCmd::GbCancel:
      gbRomFree();  // il registro resta: si riprende da written
      gGbStartWaiting = false;
      return;
    case cal::NetCmd::GbState:
      cal::scopy(gGbState, r.id);
      gGbStateSet = true;
      sendGbState();
      return;
    case cal::NetCmd::GbPad:
      gGbPad = r.gbLink;
      sendGbPad();
      return;
    case cal::NetCmd::GbSaveNow: gSaves.enqueue(r.id); return;
    default: break;
  }
```

Nel ciclo di `task()`, subito dopo `otaLoop();`:

```cpp
    gbLoop();
```

In `begin()`, dopo `gAnc = xQueueCreate(…);`:

```cpp
  gGbList = xQueueCreate(2, sizeof(cal::gb::ListMsg *));
```

In fondo, accanto a `bool takeGame(…)`:

```cpp
bool takeGbList(cal::gb::ListMsg *&out) { return gGbList && xQueueReceive(gGbList, &out, 0) == pdTRUE; }
```

- [ ] **Step 3: Build and tests**

Run: `cd firmware && pio run -e heltec213 && pio test -e native`
Expected: SUCCESS; tutti PASSED. Senza il Task 14 gli eventi `Gb*` arrivano al loop e non fanno niente; il compilatore può avvisare `enumeration value 'GbList' not handled in switch` in `pumpNet()`: l'avviso sparisce con il Task 14.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/net/net.h firmware/src/net/net.cpp
git commit -m "Giochi: gb-list, gb-start, ROM e salvataggi nel task di rete"
```

---

### Task 14: La partita nel loop e gli agganci in `main.cpp`

**Files:**
- Create: `firmware/src/hw/gb_main.h`, `firmware/src/hw/gb_main.cpp`
- Modify: `firmware/src/main.cpp` (dieci agganci corti)
- Modify: `docs/DECISIONI.md`

**Interfaces:**
- Consumes: tutto quello sopra; da `main.cpp`: `gUi`, `canvas`, `power`, `usbOn()`, `critBatt`, `wallAt()`, `fastmode`, `pending`.
- Produces (`namespace gbmain`): `struct Env { bool usb; bool battCritical; cal::WallClock wc; Canvas<250, 122> *scratch; }`; `void begin(cal::Ui &)`; `RefreshKind onEvent(cal::Ui &, const net::Event &, Ms, const Env &)`; `RefreshKind permArrived(cal::Ui &, Ms)`; `RefreshKind loop(cal::Ui &, Ms, const Env &)`; `bool ownsPanel(const cal::Ui &)`; `void bandsDirty()`; `bool panelReturned()`.

Regole del loop (oltre a quelle di `gb_session`):
- **Scelta dalla scheda**: con il PC autenticato `gb-pick` e schermata di caricamento; 15 s senza `gb-start` → «Il PC non risponde». Senza PC, solo il gioco già sulla scheda: `GbLocal` (verifica SHA-256 e via). Altrimenti «PC non raggiungibile».
- **`gb-start` a partita aperta**: stesso gioco → niente; altro gioco → salva ed esce (`ExitWhy::Switch`) e solo dopo `GbGo`.
- **Bande**: ridisegnate (`ui::draw` della schermata GbPlay sul canvas di `main.cpp`, che durante la partita non serve) solo quando l'interfaccia chiede un ridisegno o cambia l'avviso della batteria.
- **Energia**: `in.playing` vero per tutta la partita (pausa compresa); `power.key()` a ogni giro con la partita aperta (il controller non passa dal tasto PRG: senza, il sonno «PC spento» scatterebbe dopo 60 s); `powerLoop` non addormenta mai con la partita aperta; la batteria critica la chiude prima (salvataggio) e solo dopo il sonno.
- **Pannello**: mentre il driver veloce lo tiene, `present()` non si chiama (un ridisegno rifà solo le bande); quando lo restituisce, `fastmode = false` così il prossimo parziale reinizializza il driver di serie (heltec-eink-modules).
- **OTA**: `Conditions.playing = gUi.gbOnStack()`.

- [ ] **Step 1: Write the code**

Create `firmware/src/hw/gb_main.h`:

```cpp
// Giochi Game Boy: il pezzo di loop() che fa girare le partite (regole di core/gb_session, schermate di cal::Ui,
// emulatore e pannello di hw/gb_run, controller di cal::blepad, rete con NetCmd::Gb*). main.cpp lo chiama in
// pochi punti: begin in setup, onEvent e permArrived in pumpNet, loop prima di powerLoop, ownsPanel prima di present.
#pragma once
#include <stdint.h>

#include "core/ui.h"
#include "gfx/canvas.h"
#include "net/net.h"

namespace gbmain {

struct Env {
  bool usb;                  // cavo collegato (UsbMonitor)
  bool battCritical;         // CriticalBattery: senza il gioco la scheda andrebbe in sonno profondo
  cal::WallClock wc;
  Canvas<250, 122> *scratch;  // canvas di main.cpp: libero mentre si gioca (present non disegna)
};

void begin(cal::Ui &ui);                                                  // setup(): dopo avfs::begin e goLive
cal::RefreshKind onEvent(cal::Ui &ui, const net::Event &e, cal::Ms now, const Env &env);  // EvType::Gb*
cal::RefreshKind permArrived(cal::Ui &ui, cal::Ms now);                   // dopo ogni Ui::netPermRequest
cal::RefreshKind loop(cal::Ui &ui, cal::Ms now, const Env &env);          // a ogni giro, prima di powerLoop
// Partita in corso con il pannello al driver veloce: present() non deve toccare il pannello. Un ridisegno chiesto
// intanto rifà solo le bande (bandsDirty).
bool ownsPanel(const cal::Ui &ui);
void bandsDirty();
// Vero una volta dopo che il driver veloce ha restituito il pannello (pausa, fine): main.cpp rimette il driver di
// serie in modo parziale da capo (fastmode = false) prima del prossimo disegno.
bool panelReturned();

}  // namespace gbmain
```

Create `firmware/src/hw/gb_main.cpp`:

```cpp
#include "hw/gb_main.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <string.h>

#include "core/gb_input.h"
#include "core/gb_rom.h"
#include "core/gb_session.h"
#include "core/strbuf.h"
#include "hw/blepad.h"
#include "hw/gb_hw.h"
#include "hw/gb_run.h"
#include "hw/logtee.h"
#include "ui/screens.h"

namespace gbmain {

using cal::Ms;
using cal::RefreshKind;
namespace gb = cal::gb;

namespace {

const Ms kPickWaitMs = 15000;  // gb-pick senza risposta del servizio: «Il PC non risponde»

gb::Session gSession;
gb::PauseEdge gEdge;
uint32_t gPermSeq = 0;
uint8_t gKeys = 0;
bool gBandsDirty = true;
int gLink = -1;   // ultimo pad::Link mandato
Ms gPickAt = 0;   // gb-pick mandato, in attesa di gb-start
bool gReturned = false;

void request(cal::NetCmd c, const char *id = "", uint8_t link = 0) {
  cal::NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = c;
  cal::scopy(r.id, id ? id : "");
  r.gbLink = link;
  net::request(r);
}

RefreshKind merge(RefreshKind a, RefreshKind b) { return cal::mergeRefresh(a, b); }

// Bande della schermata GbPlay, disegnate senza Inverti né Ruota (la rotazione la fa composeFrame su tutto).
const uint8_t *bands(cal::Ui &ui, Ms now, const Env &env) {
  env.scratch->clear(false);
  CanvasSurface<250, 122> s(*env.scratch);
  ui::draw(s, ui.device(), ui.model(), 0, now, env.wc);
  return env.scratch->data();
}

gb::SessionIn inputs(const cal::Ui &ui, const Env &env) {
  const cal::Model &m = ui.model();
  return gb::SessionIn{cal::blepad::link() == cal::pad::Link::Ready, gPermSeq, int(m.sys.batt + 0.5), env.usb,
                       env.battCritical};
}

const char *exitText(gb::ExitWhy w) {
  switch (w) {
    case gb::ExitWhy::Battery: return "Batteria scarica: partita salvata";
    case gb::ExitWhy::Fault: return "Il gioco si è fermato";
    case gb::ExitWhy::Switch: return nullptr;
    default: return "Partita salvata";
  }
}

RefreshKind exitGame(cal::Ui &ui, gb::ExitWhy why, Ms now) {
  char id[gb::kIdLen];
  cal::scopy(id, ui.model().gb.current);
  gReturned = gReturned || gbrun::running();
  const bool changed = gbrun::end();
  gSession.stop();
  gKeys = 0;
  LogSerial.printf("[gb] fine partita %s: %s%s\n", id, gb::exitKey(why), changed ? ", salvataggio al PC" : "");
  if (changed) request(cal::NetCmd::GbSaveNow, id);
  request(cal::NetCmd::GbState, "");
  return ui.gbEnd(exitText(why), why == gb::ExitWhy::PausedTooLong || why == gb::ExitWhy::Rest, now);
}

RefreshKind startGame(cal::Ui &ui, const char *id, Ms now, const Env &env) {
  gb::RomRecord rec;
  const char *why = "Gioco non sulla scheda";
  if (!gbhw::store().load(rec) || !rec.complete || strcmp(rec.id, id) != 0 || !gbrun::begin(id, rec.size, &why))
    return ui.gbEnd(why, false, now);
  ui.gbPlay(now);
  gSession.start(now, gPermSeq);
  gEdge.arm(true);
  gKeys = 0;
  const cal::Model &m = ui.model();
  gbrun::resume(m.gb.smooth, m.set.rot, m.set.invert, bands(ui, now, env), esp_timer_get_time());
  gBandsDirty = false;
  request(cal::NetCmd::GbState, id);
  LogSerial.printf("[gb] partita %s «%s», profilo %s\n", id, m.gb.title, m.gb.smooth ? "Fluido" : "Pulito");
  return RefreshKind::None;
}

RefreshKind pauseGame(cal::Ui &ui, gb::PauseWhy why, Ms now) {
  gSession.pause(why, now);  // da Session::update è già in pausa: non cambia niente
  gReturned = gReturned || gbrun::running();
  gbrun::pause();
  LogSerial.printf("[gb] pausa: %s\n", gb::pauseKey(why));
  return ui.gbPaused(why, now);
}

RefreshKind resumeGame(cal::Ui &ui, Ms now, const Env &env) {
  if (!gSession.resume(inputs(ui, env), now)) {
    cal::toast(ui.model(), "Ricollega il controller", now);
    return RefreshKind::Partial;
  }
  ui.gbResumed(now);
  gEdge.arm(true);
  gKeys = 0;
  const cal::Model &m = ui.model();
  gbrun::resume(m.gb.smooth, m.set.rot, m.set.invert, bands(ui, now, env), esp_timer_get_time());
  gBandsDirty = false;
  return RefreshKind::None;
}

}  // namespace

void begin(cal::Ui &ui) {
  const bool part = gbhw::begin();
  gb::RomRecord rec;
  char title[17] = "";
  const bool have = part && gbhw::store().load(rec) && rec.complete;
  if (have) gbhw::romTitle(title);
  ui.gbInstalled(part, have ? rec.id : "", title);
  ui.model().gb.smooth = gbhw::loadSmooth();
  gbrun::setCleanEvery(gbhw::loadCleanMin());  // pulizia completa periodica in gioco (contratto)
  if (have) LogSerial.printf("[gb] sulla scheda: %s «%s», %lu byte\n", rec.id, title, (unsigned long)rec.size);
}

void bandsDirty() { gBandsDirty = true; }

bool panelReturned() {
  const bool r = gReturned;
  gReturned = false;
  return r;
}

bool ownsPanel(const cal::Ui &ui) { return ui.gbPlaying() && gbrun::running(); }

RefreshKind permArrived(cal::Ui &ui, Ms now) {
  gPermSeq++;
  // In pausa la regola della sessione non scatta più: la schermata Permesso si apre qui.
  if (gSession.phase() == gb::Phase::Paused) return ui.gbShowPerm(now);
  return RefreshKind::None;  // in gioco: la pausa (e il Permesso) al prossimo loop
}

RefreshKind onEvent(cal::Ui &ui, const net::Event &e, Ms now, const Env &env) {
  switch (e.type) {
    case net::EvType::GbList: {
      gb::ListMsg *p = nullptr;
      RefreshKind r = RefreshKind::None;
      while (net::takeGbList(p)) {
        r = merge(r, ui.gbList(p->games, p->n, now));
        free(p);
      }
      return r;
    }
    case net::EvType::GbStart: {
      gPickAt = 0;
      RefreshKind r = RefreshKind::None;
      if (gbrun::active()) {
        if (!strcmp(ui.model().gb.current, e.id)) {  // stesso gioco già aperto: niente da rifare
          request(cal::NetCmd::GbCancel);
          return RefreshKind::None;
        }
        r = exitGame(ui, gb::ExitWhy::Switch, now);  // salva, libera la ROM: poi la rete può scrivere
      }
      r = merge(r, ui.gbLoad(e.id, now));
      request(cal::NetCmd::GbGo, e.id);
      return r;
    }
    case net::EvType::GbProgress: {
      gPickAt = 0;
      const uint32_t done = uint32_t(e.time & 0xFFFFFFFF), total = uint32_t(uint64_t(e.time) >> 32);
      return ui.gbProgress(static_cast<gb::LoadStep>(e.rssi), done, total, now);
    }
    case net::EvType::GbReady:
      ui.gbInstalled(true, e.id, e.text);
      if (!ui.gbOnStack()) return RefreshKind::None;  // annullato nel frattempo
      return merge(ui.gbProgress(gb::LoadStep::Start, 1, 1, now), startGame(ui, e.id, now, env));
    case net::EvType::GbFailed:
      gPickAt = 0;
      LogSerial.printf("[gb] caricamento di %s non riuscito: %s\n", e.id, e.text);
      if (gbrun::active() || !ui.gbOnStack()) return RefreshKind::None;
      request(cal::NetCmd::GbState, "");  // fine del caricamento: playing null, il servizio può rifare l'OTA
      return ui.gbEnd(e.text, false, now);
    default: return RefreshKind::None;
  }
}

RefreshKind loop(cal::Ui &ui, Ms now, const Env &env) {
  RefreshKind r = RefreshKind::None;
  cal::Model &m = ui.model();
  char id[gb::kIdLen];
  switch (ui.takeGbAct(id)) {
    case gb::GbAct::Pick:
      if (id[0] && m.sys.pc && m.sys.authed) {
        request(cal::NetCmd::GbPick, id);
        gPickAt = now ? now : 1;
        r = ui.gbLoad(id, now);
      } else if ((id[0] ? cal::streq(id, m.gb.installed) : m.gb.installed[0] != 0)) {
        r = ui.gbLoad(m.gb.installed, now);
        request(cal::NetCmd::GbLocal, m.gb.installed);
      } else {
        cal::toast(m, "PC non raggiungibile", now);
        r = RefreshKind::Partial;
      }
      break;
    case gb::GbAct::Cancel:
      gPickAt = 0;
      request(cal::NetCmd::GbCancel);
      request(cal::NetCmd::GbState, "");  // un OTA rifiutato durante il caricamento si rifà (contratto «OTA e partita»)
      r = ui.gbEnd("Caricamento annullato", false, now);
      break;
    case gb::GbAct::Pause:
      if (gSession.phase() == gb::Phase::Playing) r = pauseGame(ui, gb::PauseWhy::User, now);
      break;
    case gb::GbAct::Resume: r = resumeGame(ui, now, env); break;
    case gb::GbAct::Exit: r = exitGame(ui, gb::ExitWhy::User, now); break;
    case gb::GbAct::ExitRest: r = exitGame(ui, gb::ExitWhy::Rest, now); break;
    case gb::GbAct::Profile: gbhw::saveSmooth(m.gb.smooth); break;
    case gb::GbAct::None: break;
  }
  if (gPickAt && now - gPickAt > kPickWaitMs) {
    gPickAt = 0;
    if (cal::top(m).id == cal::ScreenId::GbLoad) {
      request(cal::NetCmd::GbState, "");  // fine del caricamento: playing null (contratto «OTA e partita»)
      r = merge(r, ui.gbEnd("Il PC non risponde", false, now));
    }
  }
  const int link = int(cal::blepad::link());
  if (link != gLink) {
    gLink = link;
    request(cal::NetCmd::GbPad, "", uint8_t(link));
  }
  if (!gSession.active()) return r;
  const gb::Verdict v = gSession.update(inputs(ui, env), now);
  if (v.lowBand != m.gb.lowBand) {
    m.gb.lowBand = v.lowBand;
    gBandsDirty = true;
  }
  if (v.act == gb::Verdict::Exit) return merge(r, exitGame(ui, v.exit, now));
  if (v.act == gb::Verdict::Pause) return merge(r, pauseGame(ui, v.pause, now));
  if (gSession.phase() != gb::Phase::Playing || !gbrun::running()) return r;
  cal::pad::State s;
  if (cal::blepad::poll(s)) {
    const gb::PadKeys k = gb::mapPad(s);
    if (gEdge.update(k.pause)) return merge(r, pauseGame(ui, gb::PauseWhy::User, now));
    if (k.keys != gKeys) {
      gKeys = k.keys;
      gSession.input(now);
    }
  }
  if (gBandsDirty) {
    gbrun::setBands(bands(ui, now, env));
    gBandsDirty = false;
  }
  if (!gbrun::step(gKeys, esp_timer_get_time())) r = merge(r, exitGame(ui, gb::ExitWhy::Fault, now));
  return r;
}

}  // namespace gbmain
```

- [ ] **Step 2: Agganci in `main.cpp`**

1. Dopo `#include "hw/avatar_fs.h"` (dentro `#ifndef CALAMAIO_NO_NET`):

```cpp
#include "hw/gb_main.h"
```

2. In `otaOffer()`, dopo `c.calibrating = calRunning(m);`:

```cpp
  c.playing = gUi.gbOnStack();  // giochi: l'aggiornamento aspetta la fine della partita
```

3. Subito prima del commento `// Eventi del task di rete. Durante l'avvio (2,6 s) si aspettano: …` (cioè prima di `pumpNet`):

```cpp
// Giochi Game Boy (hw/gb_main): quello che il loop sa e la partita no.
static gbmain::Env gbEnv(Ms now) { return gbmain::Env{usbOn(), critBatt.critical(), wallAt(now), &canvas}; }

```

4. In `pumpNet()`, nello `switch`, subito prima di `case net::EvType::FullRefresh:`:

```cpp
      case net::EvType::GbList:
      case net::EvType::GbStart:
      case net::EvType::GbProgress:
      case net::EvType::GbReady:
      case net::EvType::GbFailed: r = gbmain::onEvent(gUi, e, now, gbEnv(now)); break;
```

5. In `pumpNet()`, sostituire la riga `while (net::takePerm(req)) pending = mergeRefresh(pending, gUi.netPermRequest(req, now, wallValid() ? epochNowMs() : 0));` con:

```cpp
  while (net::takePerm(req)) {
    pending = mergeRefresh(pending, gUi.netPermRequest(req, now, wallValid() ? epochNowMs() : 0));
    pending = mergeRefresh(pending, gbmain::permArrived(gUi, now));  // giochi: pausa e schermata Permesso
  }
```

6. In `powerLoop()`, dopo `in.battCritical = critBatt.critical();`:

```cpp
  in.playing = in.playing || gUi.gbOnStack();  // giochi: Attivo per tutta la partita, pausa compresa
```

7. In `powerLoop()`, subito dopo la riga `if (gestures.pressed() || digitalRead(PIN_BUTTON_PRG) == LOW || edgeTail != edgeHead) return;`:

```cpp
  if (gUi.gbOnStack()) return;  // giochi: prima si salva e si esce (gbmain::loop, batteria critica)
```

8. In `setup()`, dopo `petBegin();` (dentro `#if CALAMAIO_NET`):

```cpp
  gbmain::begin(gUi);  // giochi: partizione, gioco sulla scheda, profilo (LittleFS già montato da avatarsBegin)
```

9. In `loop()`, tra `if (pending == RefreshKind::None && counter.idleCleanup(now)) pending = RefreshKind::Full;` e `powerLoop(now);`:

```cpp
#if CALAMAIO_NET
  pending = mergeRefresh(pending, gbmain::loop(gUi, now, gbEnv(now)));  // giochi: prima del sonno e del disegno
  if (gUi.gbOnStack()) power.key(now);  // il controller non passa dal tasto PRG: niente sonno «PC spento» a metà partita
  if (gbmain::ownsPanel(gUi)) {  // il pannello è del driver veloce: un ridisegno rifà solo le bande
    if (pending != RefreshKind::None) gbmain::bandsDirty();
    pending = RefreshKind::None;
  }
  if (gbmain::panelReturned()) fastmode = false;  // il prossimo parziale reinizializza il driver di serie
#endif
```

10. In fondo a `loop()`, sostituire `delay(5);` (l'ultima riga) con:

```cpp
  delay(gUi.gbPlaying() ? 1 : 5);  // giochi: il ritmo lo tiene il Pacer, il giro deve essere corto
```

- [ ] **Step 3: Build, tests and sizes**

Run: `cd firmware && pio run -e heltec213 && pio test -e native`
Expected: SUCCESS, tutti PASSED. Annotare `RAM:` e `Flash:`. Riferimento misurato in prova (senza NimBLE del piano A, 2026-10-12): statica +3 744 byte rispetto alla 0.10.2, immagine +49 776 byte (1 725 808 byte, 73 % di app0 da 2 359 296). Con NimBLE (+~198 KB flash, +~7 KB statica dalla prova blepad) si resta intorno all'82 %: sopra il 90 % fermarsi e avvisare il coordinatore.

- [ ] **Step 4: DECISIONI e commit**

```markdown
- **Giochi, sulla scheda**: emulatore nel task del loop (core 1) a 59,7 Hz con al massimo 2 fotogrammi di recupero, pannello veloce nel task «gb-ink» (core 0, priorità 1) che prende sempre il fotogramma più recente (triplo buffer). Memoria della partita sullo heap solo durante la partita, con controllo prima («Memoria insufficiente»). La rete scrive nella partizione solo dopo che il loop ha chiuso la partita (`gb-start` → `GbGo`); con la ROM mappata la scrittura è rifiutata. Partita aperta: stato Attivo, niente sonno profondo, OTA rifiutato con «partita in corso: riprova a fine partita». Il token del PUT dei salvataggi lo crea la scheda (contratto). Pulizia completa del pannello ogni 10 minuti di gioco (`calamaio-gb/clean`). Durante la partita i tasti del controller li legge solo il gioco: la mappatura dei menu del piano A è sospesa (`Ui::gbPlaying`). Salvataggio in LittleFS a ogni pausa e all'uscita; al PC all'uscita e a ogni connessione autenticata.
```

```bash
git add firmware/src/hw/gb_main.h firmware/src/hw/gb_main.cpp firmware/src/main.cpp docs/DECISIONI.md
git commit -m "Giochi: la partita nel loop"
```

---

### Task 15: Primo gioco sulla scheda (dal vivo, con l'utente)

**Prerequisiti:** Task 1–14; piano A completo e provato dal vivo (controller 1914 associato da Menu › Gioco › Controller); piano C con `gb-list`, `gb-start`, `GET /gb/rom/<token>` con Range, `PUT`/`GET /gb/save/<token>`, `gb-save-ok` e la pagina «Giochi» della webapp. Scheda con la tabella nuova (Task 2).

**Files:** nessuno, salvo `docs/DECISIONI.md` (misure).

**Interfaces:** tutte, dal vivo.

ROM da usare: solo file dell'utente (cartucce proprie, homebrew) e `firmware/test/roms/dmg-acid2.gb` (MIT: utile per vedere la riduzione e la retinatura). Nessun download di giochi.

- [ ] **Step 1: Aggiornamento via OTA nella tabella nuova**

Run: `cd firmware && pio run -e heltec213`, poi `calamaio ota push --build firmware/.pio/build/heltec213` (il coordinatore chiede prima all'utente, come ogni aggiornamento).
Expected nel registro (`calamaio logs --board <scheda> --follow`): `[ota] scrivo in app1 (0x250000, 2359296 byte)`, riavvio, conferma all'`auth-ok`; poi `[gb] partizione gbrom 0x490000, 2048 KB` e `[memoria] … heap libero` all'avvio.

- [ ] **Step 2: Elenco e caricamento**

L'utente carica `dmg-acid2.gb` dalla webapp (pagina Giochi). Expected: `[gb] gb-list: 1 giochi`; Menu › Gioco › ROM mostra «DMG-ACID2 · 32 KB» (anche col controller scollegato: scegliendolo così compare «Collega il controller per giocare» e non parte). Col controller collegato, scegliere con A: schermata GIOCO, «Scarico il gioco», barra, poi «Verifico il gioco», «Avvio…». Registro: `[gb] … scarico`, `GET rom (link monouso) da 0: HTTP 200`, `[gb] … verificato`, `[gb] … pronto: ROM 32768 byte, RAM 0 byte, heap libero N`.

- [ ] **Step 3: Immagine e ritmo**

Expected sul pannello: la faccia di dmg-acid2 al centro (136×122, bande ai lati con batteria, PC ✓, titolo, «START + SELECT = PAUSA»), ferma e leggibile. Foto per il registro. Nel registro ogni 10 s: `[gb] pannello: N immagini in 10 s, fotogrammi emulati persi M`. Criteri: N ≈ 110 (Pulito) o ≈ 150 (Fluido); **M < 30 in 10 s** (< 5 % di 597). Se M è più alto: annotare e provare, nell'ordine, `delay(0)` nel loop, `PEANUT_GB_HIGH_LCD_ACCURACY 0` in `gb_emu.cpp`, le funzioni calde di Peanut-GB in IRAM; fermarsi e riferire prima di cambiare la piattaforma.

- [ ] **Step 4: Gioco vero e memoria**

Con un gioco dell'utente con salvataggio (MBC1/3/5 + batteria), il più grande che ha (idealmente 1–2 MB e 32 KB di RAM): stessi passi. Annotare `heap libero` dopo `pronto` e la minima (`[memoria] … minimo`) dopo 5 minuti di gioco con Wi-Fi e controller. **Sotto 20 KB di minimo**: riferire (Review Focus 2). Giocare 2 minuti: tasti, Start, Select, direzioni dalla croce e dalla levetta.

Taratura della pulizia periodica (contratto: `calamaio-gb/clean`, predefinito 10): giocare 15 minuti col profilo Pulito e annotare dopo quanti minuti le scie residue si vedono a occhio e se la riga `[gb] pannello: pulizia completa periodica` a 10 minuti le toglie. Se si vedono prima, il valore da proporre è quel numero di minuti (si cambia nella NVS, chiave `clean`).

- [ ] **Step 5: DECISIONI**

Annotare in `docs/DECISIONI.md`: data, gioco (titolo, MBC, dimensioni ROM e RAM), immagini/s, fotogrammi persi, heap libero e minimo, giudizio dell'utente su scie e leggibilità nei due profili, minuti della pulizia periodica scelti. Commit `Giochi: primo gioco sulla scheda, misure`.

---

### Task 16: Prove dal vivo dei flussi (con l'utente)

**Prerequisiti:** Task 15 riuscito.

**Files:** nessuno, salvo `docs/DECISIONI.md`.

**Interfaces:** tutte, dal vivo. Per ogni passo: cosa fa l'utente, cosa deve succedere, la riga di registro che lo conferma. Un passo che non torna si annota, si apre un `superpowers:systematic-debugging` e non si va avanti.

- [ ] **Step 1: Pausa e profilo.** Start+Select → refresh completo, menu «Pausa» con «Riprendi» selezionato (`[gb] pausa: utente`). «Profilo» → «Fluido»; Riprendi → refresh completo e gioco più fluido (`profilo Fluido` al prossimo avvio: `prof` in NVS). B in pausa = Riprendi. Start+Select tenuti durante Riprendi non riaprono la pausa.
- [ ] **Step 2: Permesso durante la partita.** Da Claude Code una richiesta di permesso (`calamaio hook --wait` come in M5). Expected: pausa (`[gb] pausa: permesso`), refresh completo, schermata Permesso con **NEGA** preselezionato; una pressione singola di A non approva; A tenuto 3 s conferma (piano A). Dopo la risposta, la pausa; Riprendi. Seconda prova: lunga «Annulla» → la richiesta resta aperta nel terminale, si riprende a giocare.
- [ ] **Step 3: Controller scollegato.** Spegnere il controller (Xbox 6 s). Expected: pausa subito con «Controller scollegato»; Riprendi rifiutato («Ricollega il controller»); riaccendere → `PRONTO` (piano A) → Riprendi funziona. `gb-pad` al servizio: la webapp mostra lo stato.
- [ ] **Step 4: Pausa automatica e Riposo.** Non toccare niente per 5 minuti → «Pausa automatica». Altri 10 minuti → `[gb] fine partita …: 10 min in pausa`, toast «Partita salvata», Riposo e sonno profondo (senza cavo). Al risveglio col tasto: Home normale.
- [ ] **Step 5: Salvataggi.** In un gioco con salvataggio: salvare nel gioco, «Salva ed esci». Expected: `gb-save {…}` nel registro, `PUT salvataggio … HTTP 2xx`, `gb-save-ok`; la webapp mostra la copia. Spegnere il Wi-Fi del PC o fermare il servizio, giocare e salvare di nuovo, uscire, riaccendere il servizio: alla connessione autenticata il salvataggio parte da solo.
- [ ] **Step 6: Cambio gioco dalla webapp.** A partita aperta, avviare dalla webapp un altro gioco. Expected: `fine partita …: altro gioco`, salvataggio, poi download del nuovo (mai `esp_partition_*` con la ROM mappata). Tornare al primo gioco: il salvataggio c'è ancora (`/gb/<id>.sav`).
- [ ] **Step 7: Ripristino di un salvataggio.** Dalla webapp ripristinare una copia vecchia (piano C): `gb-start` con `saveToken`, `GET salvataggio … sostituito`, il gioco riparte da lì.
- [ ] **Step 8: Download interrotto.** Avviare un gioco grande e, a metà, fermare il servizio 30 s e riavviarlo. Expected: «download interrotto» oppure ripresa con `GET rom … da N: HTTP 206` (N multiplo di 65 536 se il token nuovo arriva con `gb-pick`), mai una partita con una ROM incompleta. Poi RST della scheda a metà download: al riavvio la scelta dello stesso gioco riprende da `written`.
- [ ] **Step 9: Senza PC.** Servizio spento: Menu › Gioco › ROM mostra il gioco «sulla scheda» e lo avvia (`GbLocal`, solo verifica); il salvataggio arriva al PC quando torna.
- [ ] **Step 10: OTA durante la partita.** `calamaio ota push` a partita aperta: `ota-status rifiutato «partita in corso: riprova a fine partita»`, la partita non si interrompe; dopo «Salva ed esci» l'aggiornamento riparte (piano C) o si rilancia.
- [ ] **Step 11: Batteria.** A batteria, una partita fino al 15 %: «Batteria bassa» nella banda; sotto la soglia critica: «Batteria scarica: partita salvata», poi la schermata della batteria scarica e il sonno profondo. Il salvataggio c'è al riavvio col cavo.
- [ ] **Step 12: Consumo in un'ora.** Con la batteria carica, un'ora di gioco senza cavo: annotare la percentuale prima e dopo (spec: stima 100–130 mA, 7–9 h con 1000 mAh).
- [ ] **Step 13: DECISIONI.** Esiti di tutti i passi, tempi, eventuali deviazioni. Commit `Giochi: prove dal vivo dei flussi`.

---

## Copertura della specifica

| Spec / contratto | Task |
|---|---|
| Tabella delle partizioni, NVS intatta, flash via cavo con backup | 1, 2 |
| Peanut-GB, ROM dalla flash mappata, RAM della cartuccia sullo heap ≤ 32 KB, MBC, «Gioco non supportato» | 3, 12 |
| Immagine intera ridotta a ~136×122 centrata, bande con batteria e indicatori | 4, 11 |
| Emulazione a 59,7 Hz su un core, pannello veloce sull'altro, passaggio dei fotogrammi | 5, 12 |
| Controller in gioco, Start+Select = pausa | 6, 14 |
| Menu › Gioco › ROM (elenco dal PC, gioco sulla scheda, sempre visibile; senza controller «Collega il controller per giocare»), pausa con Riprendi, Salva ed esci, profilo | 11 |
| Permesso in gioco: pausa e schermata Permesso, §7 invariata | 6, 11, 14, 16 |
| Pause automatiche, 10 min → Riposo, batteria < 15 % e < 5 % | 6, 14, 16 |
| OTA rimandato | 10, 14, 16 |
| Pulizia completa a pausa e uscita; `cleanFull` ogni 10 minuti di gioco | 12, 14 |
| `gb-list`, `gb-pick`, `gb-start` (+ `saveToken`) con mac obbligatori, ROM con Range e SHA-256 sulla porta 8766, registro nella NVS, mai ROM incompleta | 7, 8, 12, 13 |
| `gb-save` + PUT + `gb-save-ok`, `gb-state`, `gb-pad` | 9, 13, 14 |
| PC irraggiungibile: si gioca il gioco presente | 11, 14, 16 |
| Prove automatiche (CPU, riduzione, invio con interruzioni) e dal vivo | 3–9, 15, 16 |
| Salvataggi «ultime 3 copie», webapp, cartella giochi | piano C |
