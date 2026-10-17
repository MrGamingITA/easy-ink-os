# Giochi, piano A: fondamenta sulla scheda — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dare al firmware della Heltec Wireless Paper un driver e-ink veloce (`cal::fastink`) e il controller Xbox in Bluetooth LE (`cal::pad`, `cal::blepad`), usabile in tutti i menu di Calamaio e nei permessi (solo col tenuto di A), con Menu › Gioco (Controller, ROM vuota per il piano B, «Alto o basso» spostato da Strumenti) e Bluetooth acceso solo con un controller associato. Il driver veloce serve solo alla partita (piano B).

**Architecture:** Come nel resto del firmware, la logica pura sta in `firmware/src/core/` (provata in `env:native`) e l'hardware in `firmware/src/hw/`. Il driver veloce è il porting dello spike `spikes/fastink` (budget per pixel, LUT da 153 byte, Y da 1 byte) più le spinte extra ai bordi e la pulizia a immagine ferma; prende il bus SPI di heltec-eink-modules e lo restituisce. Il controller è il porting dello spike `spikes/blepad` (NimBLE-Arduino 2.5.1 come centrale) in un task FreeRTOS suo; i tasti diventano eventi di menu (`pad::MenuMapper`) che `cal::Ui` tratta come i gesti del tasto PRG.

**Tech Stack:** C++ (scheda: GCC 8.4 con `-std=gnu++11`; test: C++17), PlatformIO `espressif32@6.9.0` (core Arduino 2.0.17, IDF 4.4), heltec-eink-modules 4.6.0, NimBLE-Arduino 2.5.1, Unity.

**Spec:** `docs/superpowers/specs/2026-10-12-giochi-gameboy-design.md` · **Contratto vincolante:** `docs/superpowers/plans/2026-10-12-giochi-contratti.md` · Prove: `spikes/fastink/README.md`, `spikes/blepad/README.md`.

**Prerequisito:** tutti i task che toccano `firmware/src/main.cpp`, `firmware/src/core/ui.{h,cpp}` o `firmware/src/core/model.{h,cpp}` (Task 4, 11, 12, 13) partono **dopo che il lavoro in corso sulla 0.10.2 (refresh) è stato unito**. Prima di iniziare: `git log --oneline -3` deve mostrare il commit finale della 0.10.2 e `cd firmware && pio test -e native` deve passare. I numeri di riga citati qui sono quelli di oggi: cercare i simboli, non le righe.

## Global Constraints

- Scheda: `platform = espressif32@6.9.0`, `board = heltec_wifi_lora_32_V3`, core Arduino 2.0.17. Il core compila con **`-std=gnu++11`**: nei file compilati per la scheda niente namespace annidati `a::b {` (si scrive `namespace cal { namespace pad {`), niente inizializzatori nei membri di struct inizializzate con le graffe, funzioni `constexpr` solo a una riga. Ogni `static const` intero di classe usato in un'espressione va anche definito nel `.cpp` (es. `const uint32_t MenuMapper::kHoldMs;`).
- I nomi del contratto non cambiano: `cal::fastink::{Profile, begin, submit, tick, settled, cleanFull, end}` più le aggiunte `attachBus, active, setParams, params, setTickCtl, tickCtl, lastTickUs, measureHoldTick`; `cal::pad::{Button, State, parseXboxReport, Link}` più `kReportLen, hatToButtons, stickPercent, sameState, linkKey, MenuEvent, MenuMapper, LinkMachine, radioPlan`; `cal::blepad::{begin, startPairing, forget, link, poll}` più `end, active, bonded, battery, setScan, polledAt` (`poll` dà un report cambiato alla volta); file `firmware/src/hw/fastink.{h,cpp}`, `firmware/src/core/fastink_engine.{h,cpp}`, `firmware/src/hw/blepad.{h,cpp}`, `firmware/src/core/pad.{h,cpp}`. Le funzioni in più di questo piano sono **aggiunte** (elencate nel rapporto al coordinatore), mai sostituzioni.
- Prefisso `gb` per tutto ciò che è Game Boy (`MenuId::Gb`, `ActId::Gb*`, `core/gb_menu.*`, NVS `calamaio-gb`).
- Pannello E0213A367: Y da **1 byte** in 0x45/0x4F; LUT da **153 byte** in 0x32 solo dopo un caricamento dall'OTP (completo 0xF7); **mai** scrivere tensioni (0x03, 0x04, 0x2C, 0x3F); VSH2 mai usato; LUT4 (VCOM) = DCVCOM; XON = 0; SPI ≤ 20 MHz. Pin del pannello solo quelli di `docs/HARDWARE.md` (CS 4, DC 5, RST 6, BUSY 7); nessun pin nuovo.
- Profili: **Clean = F8 P1 N10 FR7**, **Smooth = F4 P1 N6 FR7**; budget ammesso 1..31.
- Formato di `submit()`: 250×122, 1 bit per pixel, righe di 32 byte, bit 7 = x più a sinistra, **1 = nero** (è lo stesso formato di `Canvas<250,122>::data()`).
- Solo bianco e nero: niente grigi in questo piano (la retinatura è del piano B).
- NimBLE-Arduino **2.5.1** esatta, solo centrale, una connessione (`-D CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED -D CONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED -D CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`). Abbinamento nella NVS `nimble_bond` (di NimBLE); impostazione nella NVS `calamaio-gb`, chiave `pad` (u8 0/1). La tabella partizioni e la NVS non cambiano in questo piano.
- Con il Bluetooth acceso il Wi-Fi **non** va mai in «nessun risparmio» (`WIFI_PS_NONE`): l'IDF lo vieta in coesistenza.
- Bluetooth acceso solo con `Settings::padOn` (vero da Menu › Gioco › Controller › «Associa controller» a «Dimentica controller»: niente interruttore a sé, decisione dell'utente); spento nel sonno profondo e nei risvegli brevi (timer, orologio). Regole di risveglio invariate.
- **Driver veloce solo durante la partita** (contratto, «Regole comuni»): tutte le altre schermate di Calamaio restano su heltec-eink-modules con la regola dei refresh della 0.10.2; in questo piano il driver gira solo nell'env di prova `fastink-demo` (Task 5–6) e nel firmware si limita al passaggio del pannello (`attachBus`, `panelBack`, guardia in `present()`).
- Menu › Gioco (contratto): sempre presente, anche nel simulatore; «Controller» e «ROM» solo con `sys.padHw`; «Alto o basso» spostato qui da Menu › Strumenti, con l'allineamento del simulatore, dei golden e delle catture (Task 12).
- Permessi (ISTRUZIONI-SVILUPPO §7): NEGA preselezionato; dal controller si risponde **solo** tenendo premuto A per 3 s (con lo stesso indicatore del tasto PRG); una pressione di A non risponde mai; nessuna approvazione automatica.
- Testi a schermo in italiano, esattamente questi (nuovi): «Gioco», «Alto o basso e, col controller Bluetooth, i giochi del Game Boy.», «Controller», «ROM», «Alto o basso», «Nessuna ROM», «Stato», «Associa controller», «Dimentica controller», valori «nessuno associato», «in associazione», «lo cerco», «collegato», «collegato · 80%», «in corso», «conferma?»; toast «Metti il controller in associazione», «Nessun controller associato», «Scegli ancora per confermare», «Controller dimenticato», «Controller collegato», «Controller scollegato», «Associazione scaduta», «Tieni premuto A per 3 s»; etichette del tenuto «CONSENTI», «NEGA». Riusati: «Troppo presto: tieni fino in fondo», «FATTO · RILASCIA».
- Commenti brevi in italiano, identificatori in inglese. Messaggi di commit in italiano nello stile del repository («Area: cosa»), **senza** riga `Co-Authored-By` (preferenza dell'utente).
- Flash solo dal coordinatore, **sempre dopo un sì esplicito dell'utente**, sempre dopo il backup di NVS e otadata, a **115200 baud**, scrivendo solo otadata (`boot_app0.bin`) e app0 (mai `pio run -t upload`, mai cancellare la NVS). I backup contengono segreti: vanno in `backup-fabbrica/` (ignorata da git), mai nel repository.

## Review Focus

- **A premuto e rilasciato durante un refresh bloccante** (i due report arrivano in coda e si leggono insieme): deve restare «scegli», non perdersi né diventare un tenuto — test `test_tap_during_refresh_is_select` nel Task 8 (e `poll()` legge un report alla volta, Task 10).
- **Tenuto di A mentre il permesso cambia** (chiuso dal terminale, o compare un'altra richiesta durante la tenuta): non deve rispondere a niente né mandare a Riposo — test `test_perm_closed_mid_hold` e `test_hold_started_elsewhere_never_answers` nel Task 11.
- **Controller scollegato o spento a metà tenuta**: nessuna risposta, l'indicatore sparisce — test `test_reset_mid_hold` nel Task 8 e `test_disconnect_mid_hold` nel Task 11.
- **Fotogrammi inviati più in fretta dei tick** (il gioco a 60 Hz, il pannello a ~11 Hz): le spinte extra non si sommano e restano ≤ 7 per pixel — test `test_fast_submits_do_not_stack` nel Task 2.
- **«Nessun risparmio» chiesto col Bluetooth acceso** (fase 1 della calibrazione, o regola futura col cavo): diventa «minimo», mai `WIFI_PS_NONE` — test `test_wifi_ps_never_none_with_ble` nel Task 9.

---

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `firmware/src/core/fastink_engine.{h,cpp}` | geometria RAM, profili, LUT 153, conversione del fotogramma, motore a budget con spinte extra e pulizia | 1, 2, 3 |
| `firmware/src/hw/fastink.{h,cpp}` | SPI del pannello, OTP, LUT, 0x22, passaggio da/a heltec-eink-modules | 4, 6 |
| `firmware/src/demo/fastink_demo.cpp` | app di prova dal vivo (env `fastink-demo`), non è Calamaio | 4 |
| `firmware/src/core/pad.{h,cpp}` | report Xbox, croce → bit, eventi di menu col tenuto di A, macchina del collegamento, quando accendere la radio | 7, 8, 9 |
| `firmware/src/core/power.{h,cpp}` | `wifiPowerSave()` con il vincolo del Bluetooth | 9 |
| `firmware/src/hw/blepad.{h,cpp}` | NimBLE centrale, task del controller, abbinamento, ricollegamento | 10 |
| `firmware/src/core/ui_pad.cpp` | `Ui::padEvent`, tenuto di A, `padLinkChanged`, azioni di Menu › Gioco › Controller | 11, 12 |
| `firmware/src/core/gb_menu.{h,cpp}` | aggancio per Menu › Gioco › ROM (vuoto nel piano A) | 12 |
| `firmware/src/core/{model,menu,ui}.{h,cpp}` | campi nuovi, Menu › Gioco (Controller, ROM, Alto o basso), etichetta del tenuto | 11, 12 |
| `reference/calamaio-os-browser/assets/calamaio-test.js`, `LEGGIMI.txt` | allineamento: «Alto o basso» in Menu › Gioco (annotato in DECISIONI) | 12 |
| `tools/golden/core-golden.js`, `firmware/test/test_core_golden/*`, `tools/sim-capture/{scenarios.mjs,README.md,out/heltec213}`, `firmware/test/test_game` | golden e catture rigenerati col menu nuovo | 12 |
| `firmware/src/main.cpp`, `firmware/src/net/net.{h,cpp}` | collegamento al loop, energia, NVS, sonno | 4, 13 |
| `firmware/platformio.ini` | env `fastink-demo`, NimBLE | 4, 10 |
| `firmware/test/test_fastink`, `test_pad`, `test_pad_ui`, `test_power`, `test_net` | test | 1–3, 7–9, 11–13 |

---

### Task 1: Motore veloce — geometria, profili, LUT, conversione del fotogramma

**Files:**
- Create: `firmware/src/core/fastink_engine.h`, `firmware/src/core/fastink_engine.cpp`
- Test: `firmware/test/test_fastink/test_main.cpp`

**Interfaces:**
- Consumes: niente (porting di `spikes/fastink/src/lut.*`, `geom.h`, `canvas.h::ramIndex`).
- Produces (namespace `cal::fastink`): costanti `kScrW=250, kScrH=122, kFrameStride=32, kFrameBytes=3904, kRamW=128, kRamH=250, kRowBytes=16, kRamBytes=4000, kPixels=32000, kVisW=122, kLutLen=153`; `enum class Profile : uint8_t { Clean, Smooth }`; `struct Params { uint8_t budget, tp, phases, fr, edge, polish; }`; `Params paramsFor(Profile)`; `const char *profileName(Profile)`; `void frameToRam(const uint8_t *frame, uint8_t *ram)`; `size_t buildTickLut(const Params &p, uint8_t *out)`.

- [ ] **Step 1: Scrivere il test che fallisce**

`firmware/test/test_fastink/test_main.cpp`:

```cpp
// Driver veloce, parte pura (core/fastink_engine): geometria, profili, LUT, motore a budget.
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "core/fastink_engine.h"
#include "gfx/canvas.h"

using namespace cal::fastink;

void setUp() {}
void tearDown() {}

static Canvas<kScrW, kScrH> cv;
static uint8_t ram[kRamBytes];

// Indice di pixel nella RAM di un punto logico (rotazione 3: RAM x = y, RAM y = 249 − x).
static int pix(int x, int y) { return (kRamH - 1 - x) * kRamW + y; }
static bool bit(const uint8_t *p, int idx) { return p[idx >> 3] & (0x80 >> (idx & 7)); }

static void test_frame_to_ram() {
  TEST_ASSERT_EQUAL(32, Canvas<kScrW, kScrH>::kStride);  // stesso formato di submit()
  cv.clear();
  frameToRam(cv.data(), ram);
  for (int i = 0; i < kRamBytes; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, ram[i]);  // tutto bianco
  cv.set(0, 0, true);
  cv.set(249, 121, true);
  cv.set(10, 20, true);
  frameToRam(cv.data(), ram);
  TEST_ASSERT_EQUAL_HEX8(0x7F, ram[249 * kRowBytes + 0]);  // (0,0) → RAM x 0, y 249
  TEST_ASSERT_EQUAL_HEX8(0xBF, ram[0 * kRowBytes + 15]);   // (249,121) → RAM x 121, y 0
  TEST_ASSERT_FALSE(bit(ram, pix(10, 20)));
  TEST_ASSERT_TRUE(bit(ram, pix(11, 20)));
  TEST_ASSERT_TRUE(bit(ram, pix(10, 21)));
  cv.clear(true);  // tutto nero (anche i bit di riempimento oltre x 249)
  frameToRam(cv.data(), ram);
  for (int y = 0; y < kRamH; y++) {
    TEST_ASSERT_EQUAL_HEX8(0x00, ram[y * kRowBytes + 0]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, ram[y * kRowBytes + 15]);  // colonne 122..127 invisibili: sempre bianche
  }
}

static void test_profiles() {
  const Params c = paramsFor(Profile::Clean);
  TEST_ASSERT_EQUAL_UINT8(10, c.budget);
  TEST_ASSERT_EQUAL_UINT8(8, c.tp);
  TEST_ASSERT_EQUAL_UINT8(1, c.phases);
  TEST_ASSERT_EQUAL_UINT8(7, c.fr);
  TEST_ASSERT_EQUAL_UINT8(2, c.edge);
  TEST_ASSERT_EQUAL_UINT8(2, c.polish);
  const Params s = paramsFor(Profile::Smooth);
  TEST_ASSERT_EQUAL_UINT8(6, s.budget);
  TEST_ASSERT_EQUAL_UINT8(4, s.tp);
  TEST_ASSERT_EQUAL_UINT8(1, s.phases);
  TEST_ASSERT_EQUAL_UINT8(7, s.fr);
  TEST_ASSERT_EQUAL_UINT8(1, s.edge);
  TEST_ASSERT_EQUAL_UINT8(2, s.polish);
  TEST_ASSERT_EQUAL_STRING("pulito", profileName(Profile::Clean));
  TEST_ASSERT_EQUAL_STRING("fluido", profileName(Profile::Smooth));
}

static void test_lut_layout() {
  uint8_t l[kLutLen];
  Params p = paramsFor(Profile::Clean);
  TEST_ASSERT_EQUAL(153, buildTickLut(p, l));
  TEST_ASSERT_EQUAL_HEX8(0x40, l[0]);   // LUT0 gruppo 0 fase A = VSH1 (verso il nero)
  TEST_ASSERT_EQUAL_HEX8(0x80, l[12]);  // LUT1 gruppo 0 fase A = VSL (verso il bianco)
  for (int i = 1; i < 60; i++)
    if (i != 12) TEST_ASSERT_EQUAL_HEX8(0, l[i]);  // LUT2/3 ferme, LUT4 = DCVCOM
  TEST_ASSERT_EQUAL_UINT8(8, l[60]);    // TP[0A] = F
  for (int i = 61; i < 144; i++) TEST_ASSERT_EQUAL_HEX8(0, l[i]);  // RP[0] = P − 1 = 0, altri gruppi vuoti
  for (int i = 144; i < 150; i++) TEST_ASSERT_EQUAL_HEX8(0x77, l[i]);  // FR 7 in tutti i nibble
  for (int i = 150; i < 153; i++) TEST_ASSERT_EQUAL_HEX8(0, l[i]);     // XON
  p.tp = 0;
  p.phases = 3;
  p.fr = 9;
  buildTickLut(p, l);
  TEST_ASSERT_EQUAL_UINT8(1, l[60]);    // F almeno 1
  TEST_ASSERT_EQUAL_UINT8(2, l[66]);    // RP[0] = 3 − 1
  TEST_ASSERT_EQUAL_HEX8(0x11, l[144]); // FR limitato a 3 bit (9 & 7)
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_to_ram);
  RUN_TEST(test_profiles);
  RUN_TEST(test_lut_layout);
  return UNITY_END();
}
```

- [ ] **Step 2: Eseguire il test e vederlo fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_fastink`
Expected: FAIL di compilazione, «core/fastink_engine.h: No such file or directory».

- [ ] **Step 3: Implementazione minima**

`firmware/src/core/fastink_engine.h`:

```cpp
// Driver veloce del pannello, parte pura: geometria della RAM, profili, LUT del tick, motore a budget per pixel.
// Porting di spikes/fastink (risultati dal vivo nel suo README). Provato con `pio test -e native -f test_fastink`.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace fastink {

// Schermo logico (landscape di heltec-eink-modules, antenna in alto) = formato di submit():
// 1 bit per pixel, righe di 32 byte, bit 7 = x più a sinistra, 1 = nero (come Canvas<250,122>::data()).
const int kScrW = 250, kScrH = 122;
const int kFrameStride = 32;
const int kFrameBytes = kFrameStride * kScrH;  // 3904
// RAM del controller (E0213A367, 128 × 250): bit 1 = bianco, 16 byte per riga, colonne 122..127 invisibili.
const int kRamW = 128, kRamH = 250;
const int kRowBytes = kRamW / 8;               // 16
const int kRamBytes = kRowBytes * kRamH;       // 4000 per piano
const int kPixels = kRamW * kRamH;             // 32000
const int kVisW = 122;
const size_t kLutLen = 153;                    // registro 0x32, SSD1680 Rev 0.14 §6.6–6.7

enum class Profile : uint8_t { Clean, Smooth };  // contratto: Clean = F8 P1 N10 FR7, Smooth = F4 P1 N6 FR7

// budget = N impulsi per una transizione completa (1..31); tp = fotogrammi della fase di spinta (F); phases = P;
// fr = FR[n] 0..7; edge = spinte extra ai pixel cambiati e ai loro 4 vicini; polish = spinte extra a tutti i pixel
// quando l'immagine si ferma. edge e polish: valori iniziali, regolati dal vivo nel Task 5.
struct Params { uint8_t budget, tp, phases, fr, edge, polish; };
Params paramsFor(Profile p);
const char *profileName(Profile p);  // 'pulito', 'fluido' (registro)

// Fotogramma di submit() → piano RAM (rotazione 3: RAM x = y logico, RAM y = 249 − x logico).
void frameToRam(const uint8_t *frame, uint8_t *ram);

// LUT del tick: un solo gruppo; LUT0 = VSH1 (nero), LUT1 = VSL (bianco), LUT2/3 ferme, LUT4 = DCVCOM, XON 0.
// Le tensioni non sono nella LUT da 153 byte: restano quelle caricate dall'OTP. Ritorna kLutLen.
size_t buildTickLut(const Params &p, uint8_t *out);

}  // namespace fastink
}  // namespace cal
```

`firmware/src/core/fastink_engine.cpp`:

```cpp
#include "fastink_engine.h"

#include <string.h>

namespace cal {
namespace fastink {

Params paramsFor(Profile p) {
  // Scelta dell'utente nello spike: F8 P1 N10 FR7 «la migliore»; F4 P1 N6 FR7 più fluido, scia evidente.
  Params r;
  if (p == Profile::Smooth) {
    r.budget = 6; r.tp = 4; r.phases = 1; r.fr = 7; r.edge = 1; r.polish = 2;
  } else {
    r.budget = 10; r.tp = 8; r.phases = 1; r.fr = 7; r.edge = 2; r.polish = 2;
  }
  return r;
}

const char *profileName(Profile p) { return p == Profile::Smooth ? "fluido" : "pulito"; }

void frameToRam(const uint8_t *frame, uint8_t *ram) {
  memset(ram, 0xFF, kRamBytes);
  for (int y = 0; y < kScrH; y++) {
    const uint8_t *row = frame + y * kFrameStride;
    const uint8_t m = static_cast<uint8_t>(0x80 >> (y & 7));
    const int xb = y >> 3;
    for (int x = 0; x < kScrW; x++)
      if (row[x >> 3] & (0x80 >> (x & 7))) ram[(kRamH - 1 - x) * kRowBytes + xb] &= static_cast<uint8_t>(~m);
  }
}

size_t buildTickLut(const Params &p, uint8_t *out) {
  memset(out, 0, kLutLen);
  out[0 * 12] = 0x1 << 6;  // LUT0, gruppo 0, fase A: VSH1
  out[1 * 12] = 0x2 << 6;  // LUT1, gruppo 0, fase A: VSL
  uint8_t *g0 = out + 60;  // TP A, TP B, SR AB, TP C, TP D, SR CD, RP
  g0[0] = p.tp < 1 ? 1 : p.tp;
  g0[6] = p.phases < 1 ? 0 : static_cast<uint8_t>(p.phases - 1);
  const uint8_t fr = p.fr & 0x7;
  for (int i = 0; i < 6; i++) out[144 + i] = static_cast<uint8_t>((fr << 4) | fr);
  return kLutLen;
}

}  // namespace fastink
}  // namespace cal
```

- [ ] **Step 4: Eseguire il test e vederlo passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_fastink`
Expected: PASS (3 test).

- [ ] **Step 5: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/fastink_engine.h firmware/src/core/fastink_engine.cpp firmware/test/test_fastink/test_main.cpp
git commit -m "Fastink: geometria, profili e LUT del driver veloce (parte pura)"
```

---

### Task 2: Motore veloce — budget per pixel e spinte extra ai bordi

**Files:**
- Modify: `firmware/src/core/fastink_engine.h`, `firmware/src/core/fastink_engine.cpp`
- Test: `firmware/test/test_fastink/test_main.cpp`

**Interfaces:**
- Consumes: Task 1 (`frameToRam`, costanti).
- Produces: `struct TickStats { uint32_t toBlack, toWhite, extra; uint32_t driven() const; }`; `class Engine` con `static const uint8_t kMaxBudget = 31, kMaxExtra = 7;`, `bool init(uint8_t budget)`, `void release()`, `bool ready() const`, `uint8_t budget() const`, `void setBudget(uint8_t)`, `void syncSaturated(const uint8_t *ram)`, `void setTarget(const uint8_t *ram, uint8_t edge)`, `TickStats tick(uint8_t *bw, uint8_t *red)`, `uint32_t pending() const`, `uint8_t level(int idx) const`, `uint8_t extra(int idx) const`, `const uint8_t *target() const`.

Regola delle spinte extra (risolve le frange viste dal vivo): quando un pixel cambia bersaglio, lui e i suoi 4 vicini visibili ricevono `edge` spinte extra verso il **proprio** bersaglio (massimo, non somma; tetto 7). Una spinta extra parte solo quando il pixel **e** i suoi 4 vicini sono già arrivati (valutato con i livelli prima del tick): così la pulizia avviene dopo che la zona si è fermata, non mentre il vicino viene ancora pilotato.

- [ ] **Step 1: Scrivere i test che falliscono**

Aggiungere in `firmware/test/test_fastink/test_main.cpp` (sopra `main`):

```cpp
static Engine eng;
static uint8_t bw[kRamBytes], red[kRamBytes];

static void target(uint8_t edge = 0) {
  frameToRam(cv.data(), ram);
  eng.setTarget(ram, edge);
}
// Pannello bianco e motore pronto con budget n.
static void start(uint8_t n) {
  TEST_ASSERT_TRUE(eng.init(n));
  cv.clear();
  frameToRam(cv.data(), ram);
  eng.syncSaturated(ram);
}
static bool driven(int idx) { return !bit(red, idx); }

static void test_square_needs_exactly_budget() {
  start(6);
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  cv.fillRect(100, 50, 24, 24, true);
  target();
  TEST_ASSERT_EQUAL_UINT32(24 * 24, eng.pending());
  for (int t = 0; t < 6; t++) {
    const TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(24 * 24, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(0, s.toWhite);
    TEST_ASSERT_FALSE(bit(bw, pix(110, 60)));  // LUT0: spinto al nero
    TEST_ASSERT_FALSE(bit(red, pix(110, 60)));
    TEST_ASSERT_TRUE(bit(red, pix(10, 10)));   // fermo, bianco (LUT3)
    TEST_ASSERT_TRUE(bit(bw, pix(10, 10)));
  }
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  TEST_ASSERT_EQUAL_UINT32(0, eng.tick(bw, red).driven());  // arrivato: niente di più
  TEST_ASSERT_TRUE(bit(red, pix(110, 60)));   // fermo nero (LUT2)
  TEST_ASSERT_FALSE(bit(bw, pix(110, 60)));
}

static void test_budget_over_15() {
  start(20);
  TEST_ASSERT_EQUAL_UINT8(20, eng.budget());
  cv.set(30, 30, true);
  target();
  int ticks = 0;
  while (eng.pending()) {
    TEST_ASSERT_EQUAL_UINT32(1, eng.tick(bw, red).toBlack);
    ticks++;
  }
  TEST_ASSERT_EQUAL(20, ticks);
  TEST_ASSERT_EQUAL_UINT8(20, eng.level(pix(30, 30)));
}

static void test_reversal_is_symmetric() {
  start(8);
  cv.set(5, 5, true);
  target();
  for (int t = 0; t < 3; t++) eng.tick(bw, red);
  TEST_ASSERT_EQUAL_UINT8(3, eng.level(pix(5, 5)));
  cv.set(5, 5, false);  // cambia idea a metà
  target();
  int ticks = 0;
  while (eng.pending()) {
    TEST_ASSERT_EQUAL_UINT32(1, eng.tick(bw, red).toWhite);
    ticks++;
  }
  TEST_ASSERT_EQUAL(3, ticks);  // solo gli impulsi dati prima: carica netta di nuovo 0
  TEST_ASSERT_EQUAL_UINT8(0, eng.level(pix(5, 5)));
}

// Bersagli casuali per 2000 tick senza spinte extra: la carica netta di ogni pixel resta in [−N, N].
static void test_dc_bounded() {
  static int32_t net[kPixels];
  memset(net, 0, sizeof net);
  start(7);
  srand(42);
  for (int t = 0; t < 2000; t++) {
    if (t % 3 == 0) {
      for (int k = 0; k < 40; k++) cv.fillRect(rand() % 250, rand() % 122, 1 + rand() % 30, 1 + rand() % 30, rand() & 1);
      target();
    }
    eng.tick(bw, red);
    for (int i = 0; i < kPixels; i += 7) {
      if (!driven(i)) continue;
      net[i] += bit(bw, i) ? -1 : +1;
    }
  }
  for (int i = 0; i < kPixels; i += 7) {
    TEST_ASSERT_TRUE(net[i] >= -7 && net[i] <= 7);
    TEST_ASSERT_EQUAL_INT32(net[i], static_cast<int32_t>(eng.level(i)));  // partenza bianca: livello = carica netta
  }
}

static void test_invisible_columns_never_driven() {
  start(6);
  cv.clear(true);
  target(3);
  const TickStats s = eng.tick(bw, red);
  TEST_ASSERT_EQUAL_UINT32(250 * 122, s.toBlack);
  for (int y = 0; y < kRamH; y++) TEST_ASSERT_EQUAL_HEX8(0x3F, red[y * kRowBytes + 15] & 0x3F);
  for (int y = 0; y < kRamH; y++) TEST_ASSERT_EQUAL_UINT8(0, eng.extra(y * kRamW + 122));  // niente spinte extra
}

static void test_budget_rescale() {
  start(6);
  cv.set(0, 0, true);
  target();
  eng.tick(bw, red); eng.tick(bw, red); eng.tick(bw, red);  // 3/6
  eng.setBudget(12);
  TEST_ASSERT_EQUAL_UINT8(6, eng.level(pix(0, 0)));
  eng.setBudget(1);
  TEST_ASSERT_EQUAL_UINT8(1, eng.level(pix(0, 0)));
  eng.setBudget(0);
  TEST_ASSERT_EQUAL_UINT8(1, eng.budget());
  eng.setBudget(99);
  TEST_ASSERT_EQUAL_UINT8(Engine::kMaxBudget, eng.budget());
}

static void test_init_release() {
  TEST_ASSERT_TRUE(eng.init(6));
  TEST_ASSERT_TRUE(eng.ready());
  eng.release();
  TEST_ASSERT_FALSE(eng.ready());
  TEST_ASSERT_TRUE(eng.init(6));  // di nuovo, senza perdite
}

// Un pixel diventa nero: lui e i 4 vicini ricevono le spinte extra solo dopo che lui è arrivato.
static void test_edge_pushes_after_arrival() {
  start(6);
  cv.set(100, 60, true);
  target(2);
  const int p = pix(100, 60);
  const int nb[4] = {pix(99, 60), pix(101, 60), pix(100, 59), pix(100, 61)};
  TEST_ASSERT_EQUAL_UINT8(2, eng.extra(p));
  for (int k = 0; k < 4; k++) TEST_ASSERT_EQUAL_UINT8(2, eng.extra(nb[k]));
  TEST_ASSERT_EQUAL_UINT8(0, eng.extra(pix(101, 61)));  // diagonale: no
  TEST_ASSERT_EQUAL_UINT8(0, eng.extra(pix(10, 10)));
  for (int t = 0; t < 6; t++) {  // transizione: solo p
    const TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(1, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(0, s.toWhite);
    TEST_ASSERT_EQUAL_UINT32(0, s.extra);
  }
  for (int t = 0; t < 2; t++) {  // zona ferma: p una spinta verso il nero, i vicini verso il bianco
    const TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(1, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(4, s.toWhite);
    TEST_ASSERT_EQUAL_UINT32(5, s.extra);
    TEST_ASSERT_FALSE(bit(bw, p));
    for (int k = 0; k < 4; k++) TEST_ASSERT_TRUE(driven(nb[k]) && bit(bw, nb[k]));
  }
  TEST_ASSERT_EQUAL_UINT32(0, eng.tick(bw, red).driven());
  TEST_ASSERT_EQUAL_UINT8(6, eng.level(p));  // le spinte extra non cambiano il livello
}

// Review Focus: fotogrammi più veloci dei tick. Le spinte extra non si sommano e restano ≤ 7.
static void test_fast_submits_do_not_stack() {
  start(6);
  for (int i = 0; i < 10; i++) {
    cv.set(5, 5, i % 2 == 0);  // nero, bianco, nero… finisce bianco
    target(3);
  }
  target(99);  // stesso bersaglio: nessun cambio, nessuna spinta in più
  TEST_ASSERT_EQUAL_UINT8(3, eng.extra(pix(5, 5)));
  TEST_ASSERT_EQUAL_UINT8(3, eng.extra(pix(6, 5)));
  cv.set(50, 50, true);
  target(99);
  TEST_ASSERT_EQUAL_UINT8(Engine::kMaxExtra, eng.extra(pix(50, 50)));
  int pushes = 0;
  for (int t = 0; t < 40; t++) {
    eng.tick(bw, red);
    if (driven(pix(5, 5))) pushes++;
  }
  TEST_ASSERT_EQUAL(3, pushes);  // è già al bersaglio (bianco): solo le 3 spinte extra
  for (int i = 0; i < kPixels; i++) TEST_ASSERT_TRUE(eng.extra(i) <= Engine::kMaxExtra);
}
```

e aggiungere in `main()`:

```cpp
  RUN_TEST(test_square_needs_exactly_budget);
  RUN_TEST(test_budget_over_15);
  RUN_TEST(test_reversal_is_symmetric);
  RUN_TEST(test_dc_bounded);
  RUN_TEST(test_invisible_columns_never_driven);
  RUN_TEST(test_budget_rescale);
  RUN_TEST(test_init_release);
  RUN_TEST(test_edge_pushes_after_arrival);
  RUN_TEST(test_fast_submits_do_not_stack);
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_fastink`
Expected: FAIL di compilazione, «'Engine' does not name a type».

- [ ] **Step 3: Implementazione**

In `firmware/src/core/fastink_engine.h`, prima di `}  // namespace fastink`:

```cpp
struct TickStats {
  uint32_t toBlack, toWhite;  // impulsi verso il nero / il bianco in questo tick (spinte extra comprese)
  uint32_t extra;             // di cui spinte extra (bordi e pulizia)
  uint32_t driven() const { return toBlack + toWhite; }
};

// Motore a budget (stile Paperboy): livello stimato per pixel 0..N (0 = bianco pieno, N = nero pieno). A ogni tick un
// pixel lontano dal bersaglio riceve un impulso verso il bersaglio; arrivato, riceve solo le spinte extra in sospeso
// (quando anche i 4 vicini sono arrivati), poi niente. Un byte per pixel: bit 0-4 livello, bit 5-7 spinte extra.
// Memoria: 32 000 + 4 000 (bersaglio) + 4 000 (idoneità) byte, allocati da init().
class Engine {
 public:
  static const uint8_t kMaxBudget = 31;
  static const uint8_t kMaxExtra = 7;

  Engine() : cell_(nullptr), target_(nullptr), elig_(nullptr), budget_(6), polished_(true) {}
  ~Engine() { release(); }
  bool init(uint8_t budget);  // false senza memoria (niente resta allocato)
  void release();
  bool ready() const { return cell_ != nullptr; }
  uint8_t budget() const { return budget_; }
  void setBudget(uint8_t n);  // 1..31, riscala i livelli (arrotondati)

  // Dopo un completo di serie: pannello = ram, livelli saturi, nessuna spinta in sospeso.
  void syncSaturated(const uint8_t *ram);
  // Nuovo bersaglio (formato RAM). edge: spinte extra ai pixel cambiati e ai loro 4 vicini visibili (massimo, tetto 7).
  void setTarget(const uint8_t *ram, uint8_t edge);
  // Un tick: riempie i piani 0x24 (bw) e 0x26 (red) e aggiorna lo stato. LUT = (red << 1) | bw.
  TickStats tick(uint8_t *bw, uint8_t *red);

  uint32_t pending() const;  // pixel non ancora al bersaglio (spinte extra escluse)
  uint8_t level(int idx) const { return cell_[idx] & 0x1F; }
  uint8_t extra(int idx) const { return cell_[idx] >> 5; }
  const uint8_t *target() const { return target_; }

 private:
  Engine(const Engine &);
  Engine &operator=(const Engine &);
  void bump(int idx, uint8_t n);
  bool atTarget(int idx) const;

  uint8_t *cell_;
  uint8_t *target_;
  uint8_t *elig_;    // bit per pixel: spinta extra ammessa in questo tick
  uint8_t budget_;
  bool polished_;    // pulizia a immagine ferma già fatta (Task 3)
};
```

In `firmware/src/core/fastink_engine.cpp` aggiungere `#include <stdlib.h>` in cima e, prima di `}  // namespace fastink`:

```cpp
const uint8_t Engine::kMaxBudget;
const uint8_t Engine::kMaxExtra;

namespace {
uint8_t clampBudget(uint8_t n) { return n < 1 ? 1 : (n > 31 ? 31 : n); }
bool whiteAt(const uint8_t *ram, int idx) { return ram[idx >> 3] & (0x80 >> (idx & 7)); }
bool visible(int idx) { return idx % kRamW < kVisW; }
}  // namespace

bool Engine::init(uint8_t budget) {
  release();
  cell_ = static_cast<uint8_t *>(malloc(kPixels));
  target_ = static_cast<uint8_t *>(malloc(kRamBytes));
  elig_ = static_cast<uint8_t *>(malloc(kRamBytes));
  if (!cell_ || !target_ || !elig_) {
    release();
    return false;
  }
  budget_ = clampBudget(budget);
  memset(cell_, 0, kPixels);
  memset(target_, 0xFF, kRamBytes);
  polished_ = true;
  return true;
}

void Engine::release() {
  free(cell_);
  free(target_);
  free(elig_);
  cell_ = target_ = elig_ = nullptr;
}

void Engine::setBudget(uint8_t n) {
  n = clampBudget(n);
  if (n == budget_) return;
  for (int i = 0; i < kPixels; i++) {
    const uint8_t lv = static_cast<uint8_t>(((cell_[i] & 0x1F) * n + budget_ / 2) / budget_);
    cell_[i] = static_cast<uint8_t>((cell_[i] & 0xE0) | lv);
  }
  budget_ = n;
}

void Engine::syncSaturated(const uint8_t *ram) {
  memcpy(target_, ram, kRamBytes);
  for (int i = 0; i < kPixels; i++) cell_[i] = whiteAt(ram, i) ? 0 : budget_;
  polished_ = true;
}

void Engine::bump(int idx, uint8_t n) {
  if (!visible(idx)) return;
  if (n > kMaxExtra) n = kMaxExtra;
  if ((cell_[idx] >> 5) < n) cell_[idx] = static_cast<uint8_t>((cell_[idx] & 0x1F) | (n << 5));
}

bool Engine::atTarget(int idx) const {
  return (cell_[idx] & 0x1F) == (whiteAt(target_, idx) ? 0 : budget_);
}

void Engine::setTarget(const uint8_t *ram, uint8_t edge) {
  if (memcmp(target_, ram, kRamBytes) == 0) return;  // stesso fotogramma: niente da fare
  polished_ = false;
  if (edge) {
    for (int i = 0; i < kRamBytes; i++) {
      const uint8_t diff = target_[i] ^ ram[i];
      if (!diff) continue;
      for (int k = 0; k < 8; k++) {
        if (!(diff & (0x80 >> k))) continue;
        const int idx = i * 8 + k, x = idx % kRamW, y = idx / kRamW;
        bump(idx, edge);
        if (x > 0) bump(idx - 1, edge);
        if (x + 1 < kRamW) bump(idx + 1, edge);
        if (y > 0) bump(idx - kRamW, edge);
        if (y + 1 < kRamH) bump(idx + kRamW, edge);
      }
    }
  }
  memcpy(target_, ram, kRamBytes);
}

TickStats Engine::tick(uint8_t *bw, uint8_t *red) {
  TickStats s = TickStats();
  // 1) Idoneità delle spinte extra con i livelli di prima del tick: il pixel e i 4 vicini sono arrivati.
  memset(elig_, 0, kRamBytes);
  for (int idx = 0; idx < kPixels; idx++) {
    if (!(cell_[idx] >> 5)) continue;
    const int x = idx % kRamW, y = idx / kRamW;
    const bool ok = atTarget(idx) && (x == 0 || atTarget(idx - 1)) && (x + 1 >= kRamW || atTarget(idx + 1)) &&
                    (y == 0 || atTarget(idx - kRamW)) && (y + 1 >= kRamH || atTarget(idx + kRamW));
    if (ok) elig_[idx >> 3] |= static_cast<uint8_t>(0x80 >> (idx & 7));
  }
  // 2) Un impulso per pixel: verso il bersaglio, oppure una spinta extra, oppure fermo (LUT2/LUT3).
  const uint8_t n = budget_;
  for (int i = 0; i < kRamBytes; i++) {
    const uint8_t t = target_[i], e = elig_[i];
    uint8_t *c = cell_ + i * 8;
    uint8_t b = 0, r = 0;
    for (int k = 0; k < 8; k++) {
      const uint8_t m = static_cast<uint8_t>(0x80 >> k);
      const bool white = t & m;
      const uint8_t want = white ? 0 : n;
      uint8_t lv = c[k] & 0x1F, ex = c[k] >> 5;
      bool toWhite;
      if (lv < want) { lv++; toWhite = false; }
      else if (lv > want) { lv--; toWhite = true; }
      else if (ex && (e & m)) { ex--; toWhite = white; s.extra++; }
      else { r |= m; b |= (t & m); continue; }
      if (toWhite) { b |= m; s.toWhite++; }  // LUT1
      else s.toBlack++;                       // LUT0
      c[k] = static_cast<uint8_t>(lv | (ex << 5));
    }
    bw[i] = b;
    red[i] = r;
  }
  return s;
}

uint32_t Engine::pending() const {
  uint32_t p = 0;
  for (int idx = 0; idx < kPixels; idx++)
    if (!atTarget(idx)) p++;
  return p;
}
```

- [ ] **Step 4: Eseguire e vedere passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_fastink`
Expected: PASS (12 test).

- [ ] **Step 5: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/fastink_engine.h firmware/src/core/fastink_engine.cpp firmware/test/test_fastink/test_main.cpp
git commit -m "Fastink: motore a budget per pixel con spinte extra ai bordi (budget fino a 31)"
```

---

### Task 3: Motore veloce — pulizia a immagine ferma

**Files:**
- Modify: `firmware/src/core/fastink_engine.h`, `firmware/src/core/fastink_engine.cpp`
- Test: `firmware/test/test_fastink/test_main.cpp`

**Interfaces:**
- Consumes: Task 2 (`Engine`, `bump`, `polished_`).
- Produces: `void Engine::polish(uint8_t pushes)`; `bool Engine::settled() const` (al bersaglio e nessuna spinta in sospeso); `bool Engine::next(uint8_t *bw, uint8_t *red, uint8_t polishPushes, TickStats &out)` (un tick se serve; quando l'immagine si ferma fa **una** pulizia con `polishPushes` spinte a tutti i pixel visibili; false = niente da mandare); `bool Engine::idle() const` (fermo e pulito). Il driver (Task 4) usa `next()` e `idle()`.

- [ ] **Step 1: Scrivere i test che falliscono**

Aggiungere in `firmware/test/test_fastink/test_main.cpp`:

```cpp
static void test_polish_once_when_settled() {
  start(6);
  TickStats s;
  TEST_ASSERT_TRUE(eng.idle());
  TEST_ASSERT_FALSE(eng.next(bw, red, 2, s));  // dopo il completo: niente
  cv.fillRect(100, 50, 24, 24, true);
  target();
  TEST_ASSERT_FALSE(eng.idle());
  for (int t = 0; t < 6; t++) {
    TEST_ASSERT_TRUE(eng.next(bw, red, 2, s));
    TEST_ASSERT_EQUAL_UINT32(24 * 24, s.toBlack);
  }
  TEST_ASSERT_TRUE(eng.settled());
  for (int t = 0; t < 2; t++) {  // pulizia: tutti i pixel visibili, ognuno verso il suo colore
    TEST_ASSERT_TRUE(eng.next(bw, red, 2, s));
    TEST_ASSERT_EQUAL_UINT32(250 * 122, s.driven());
    TEST_ASSERT_EQUAL_UINT32(24 * 24, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(250 * 122, s.extra);
    TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
    for (int y = 0; y < kRamH; y++) TEST_ASSERT_EQUAL_HEX8(0x3F, red[y * kRowBytes + 15] & 0x3F);  // invisibili ferme
  }
  TEST_ASSERT_FALSE(eng.next(bw, red, 2, s));
  TEST_ASSERT_TRUE(eng.idle());
  target();  // stesso fotogramma di nuovo (il gioco lo rimanda): nessuna nuova pulizia
  TEST_ASSERT_FALSE(eng.next(bw, red, 2, s));
}

static void test_no_polish_when_zero() {
  start(6);
  TickStats s;
  cv.set(10, 10, true);
  target();
  for (int t = 0; t < 6; t++) TEST_ASSERT_TRUE(eng.next(bw, red, 0, s));
  TEST_ASSERT_FALSE(eng.next(bw, red, 0, s));
  TEST_ASSERT_TRUE(eng.idle());
}

static void test_settled_counts_extras() {
  start(6);
  cv.set(10, 10, true);
  target(1);
  for (int t = 0; t < 6; t++) eng.tick(bw, red);
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  TEST_ASSERT_FALSE(eng.settled());  // spinte extra ancora da dare
  eng.tick(bw, red);
  TEST_ASSERT_TRUE(eng.settled());
}
```

e in `main()`:

```cpp
  RUN_TEST(test_polish_once_when_settled);
  RUN_TEST(test_no_polish_when_zero);
  RUN_TEST(test_settled_counts_extras);
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_fastink`
Expected: FAIL di compilazione, «'class cal::fastink::Engine' has no member named 'idle'».

- [ ] **Step 3: Implementazione**

In `fastink_engine.h`, nella parte pubblica di `Engine` dopo `tick()`:

```cpp
  // Pulizia a immagine ferma: ogni pixel visibile riceve «pushes» spinte verso il suo colore (tetto 7).
  void polish(uint8_t pushes);
  bool settled() const;  // tutti al bersaglio e nessuna spinta extra in sospeso
  // Il passo del driver: un tick se c'è qualcosa da fare. Quando l'immagine si ferma fa una sola pulizia
  // (polishPushes spinte, 0 = nessuna) finché non arriva un fotogramma diverso. false = niente da mandare.
  bool next(uint8_t *bw, uint8_t *red, uint8_t polishPushes, TickStats &out);
  bool idle() const { return polished_ && settled(); }  // fermo e già pulito
```

In `fastink_engine.cpp`:

```cpp
void Engine::polish(uint8_t pushes) {
  for (int idx = 0; idx < kPixels; idx++) bump(idx, pushes);
}

bool Engine::settled() const {
  for (int idx = 0; idx < kPixels; idx++)
    if ((cell_[idx] >> 5) || !atTarget(idx)) return false;
  return true;
}

bool Engine::next(uint8_t *bw, uint8_t *red, uint8_t polishPushes, TickStats &out) {
  if (settled()) {
    if (polished_) return false;
    polished_ = true;
    if (!polishPushes) return false;
    polish(polishPushes);
  }
  out = tick(bw, red);
  return true;
}
```

- [ ] **Step 4: Eseguire e vedere passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_fastink`
Expected: PASS (15 test). Poi `pio test -e native` intero: PASS.

- [ ] **Step 5: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/fastink_engine.h firmware/src/core/fastink_engine.cpp firmware/test/test_fastink/test_main.cpp
git commit -m "Fastink: pulizia una volta quando l'immagine si ferma"
```

---

### Task 4: Driver veloce sulla scheda, passaggio da e verso heltec-eink-modules, app di prova

Dopo che la 0.10.2 è stata unita (tocca `main.cpp`).

**Files:**
- Create: `firmware/src/hw/fastink.h`, `firmware/src/hw/fastink.cpp`, `firmware/src/demo/fastink_demo.cpp`
- Modify: `firmware/platformio.ini` (env `heltec213`: esclude `demo/`; nuovo env `fastink-demo`)
- Modify: `firmware/src/main.cpp` (classe `Panel`, `panelBack()`, `fastink::attachBus` in `setup()`, guardia in `present()`)

**Interfaces:**
- Consumes: Task 1–3 (`Params`, `paramsFor`, `profileName`, `frameToRam`, `buildTickLut`, `Engine::{init, release, syncSaturated, setTarget, next, idle, setBudget, target}`).
- Produces (contratto): `bool cal::fastink::begin(Profile)`, `void submit(const uint8_t *frame)`, `bool tick()`, `bool settled()`, `void cleanFull()`, `void end()`. Aggiunte del piano A: `void attachBus(SPIClass *bus, void (*onReturn)(), Print *log)`, `bool active()`, `void setParams(const Params &)`, `const Params &params()`, `void setTickCtl(uint8_t)`, `uint8_t tickCtl()`, `uint32_t lastTickUs()`, `uint32_t measureHoldTick()`. In `main.cpp`: `SPIClass *Panel::bus()`, `static void panelBack()`.

Uso (contratto): il driver serve solo alla partita del piano B; nel firmware Calamaio questo piano aggiunge solo il passaggio del pannello. `begin()` si può richiamare dopo `end()`; `submit()` non applica Inverti né Ruota; `tick()` cede il core con `vTaskDelay(1)` dopo ogni passo.

Passaggio del pannello: `begin()` fa un reset del controller, un completo di serie (0xF7) col bianco, scrive la nostra LUT e accende l'analogico (0xC0); `end()` spegne l'analogico e chiama `onReturn`, che deve riconfigurare la libreria (`fastmodeOff()`: reset + configurazione completa) e far seguire un completo: quel completo è la pulizia finale. Il bus SPI è quello della libreria (`display_spi`, creato una volta sola da `BaseDisplay::begin()`): il driver non chiama mai `SPI.end()`.

Nessun test nativo nuovo (codice hardware); la verifica è la compilazione dei due env e i test nativi esistenti verdi. La prova vera è il Task 5.

- [ ] **Step 1: `firmware/src/hw/fastink.h`**

```cpp
// Driver veloce del pannello (contratto dei giochi, piano A): forma d'onda corta nel registro 0x32 e budget per pixel
// (core/fastink_engine). Prende il pannello da heltec-eink-modules e lo restituisce. Mai insieme al disegno di serie.
// Solo durante la partita del Game Boy (piano B) e nell'app di prova: l'interfaccia di Calamaio resta sulla libreria.
// Uso: begin(profilo) → submit(fotogramma) e tick() finché serve → end().
#pragma once
#include <Arduino.h>
#include <SPI.h>

#include "core/fastink_engine.h"

namespace cal {
namespace fastink {

// ---- contratto (docs/superpowers/plans/2026-10-12-giochi-contratti.md) ----
bool begin(Profile p);              // completo di serie col bianco, poi LUT veloce e analogico acceso; false senza memoria.
                                    // Si può richiamare dopo end() (ogni partita, ogni ripresa dalla pausa).
void submit(const uint8_t *frame);  // bersaglio 250×122, righe di 32 byte, MSB = x più a sinistra, 1 = nero;
                                    // ignora Inverti e Ruota: li applica chi chiama (piano B) al fotogramma
bool tick();                        // un passo di spinta (blocca su BUSY), poi vTaskDelay(1): cede il core 0 (watchdog
                                    // del task idle); false se non c'è niente da spingere
bool settled();                     // tutti i pixel al bersaglio e pulizia a immagine ferma già fatta
void cleanFull();                   // completo di serie con l'immagine corrente, stato dei pixel riallineato
void end();                         // analogico spento, pannello restituito (onReturn fa la pulizia completa)

// ---- piano A: collegamento al firmware e messa a punto ----
// bus = SPI della libreria; onReturn = chiamata alla fine di end(): deve riconfigurare la libreria e far seguire un
// completo; log = registro (LogSerial nel firmware, Serial nella demo).
void attachBus(SPIClass *bus, void (*onReturn)(), Print *log);
bool active();                      // tra un begin() riuscito ed end()
void setParams(const Params &p);    // budget, F, P, FR, spinte extra (la LUT si riscrive subito)
const Params &params();
void setTickCtl(uint8_t ctl);       // valore di 0x22 del tick (Task 6)
uint8_t tickCtl();
uint32_t lastTickUs();              // ultimo tick: da 0x20 a BUSY basso, µs
uint32_t measureHoldTick();         // tick senza spingere nessun pixel (lo schermo non cambia): costo fisso, µs; 0 = errore

}  // namespace fastink
}  // namespace cal
```

- [ ] **Step 2: `firmware/src/hw/fastink.cpp`**

```cpp
// Driver veloce: comandi SSD1680 (Rev 0.14) sul controller della E0213A367, che usa Y da 1 byte (dal vivo, spike).
#include "hw/fastink.h"

#include <stdlib.h>
#include <string.h>

namespace cal {
namespace fastink {
namespace {

// Pin del pannello (docs/HARDWARE.md, come heltec-eink-modules): CS 4, DC 5, RST 6, BUSY 7.
const int kCs = 4, kDc = 5, kRst = 6, kBusy = 7;
// 0x22 (SSD1680 pag. 26)
const uint8_t kUpdFullOtp = 0xF7;      // completo di serie: forma d'onda e tensioni dall'OTP, poi spegne l'analogico
const uint8_t kUpdAnalogOn = 0xC0;     // accende clock e analogico e li lascia accesi
const uint8_t kUpdAnalogOff = 0x03;    // spegne analogico e oscillatore
const uint8_t kTickCtlDefault = 0x04;  // solo display (modo 1), analogico già acceso: ~40 ms fissi dal vivo

SPIClass *gBus = nullptr;
void (*gOnReturn)() = nullptr;
Print *gLog = &Serial;
SPISettings gSpi(20000000, MSBFIRST, SPI_MODE0);  // 20 MHz: massimo del datasheet, provato nello spike
Engine gEng;
Params gP = paramsFor(Profile::Clean);
uint8_t *gPlanes = nullptr;  // bersaglio + piani 0x24 e 0x26 del tick: 3 × 4000 byte
uint8_t *gRam = nullptr, *gBw = nullptr, *gRed = nullptr;
bool gOn = false, gAnalog = false;
uint8_t gCtl = kTickCtlDefault;
uint32_t gLastUs = 0;

void cmd(uint8_t c) {
  gBus->beginTransaction(gSpi);
  digitalWrite(kDc, LOW);
  digitalWrite(kCs, LOW);
  gBus->transfer(c);
  digitalWrite(kCs, HIGH);
  gBus->endTransaction();
}

void data(const uint8_t *d, size_t n) {
  gBus->beginTransaction(gSpi);
  digitalWrite(kDc, HIGH);
  digitalWrite(kCs, LOW);
  gBus->writeBytes(d, n);
  digitalWrite(kCs, HIGH);
  gBus->endTransaction();
}

void cmdData(uint8_t c, const uint8_t *d, size_t n) {
  cmd(c);
  data(d, n);
}

void cmd1(uint8_t c, uint8_t v) { cmdData(c, &v, 1); }

bool waitIdle(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (digitalRead(kBusy) == HIGH)
    if (millis() - t0 > timeoutMs) return false;
  return true;
}

bool update(uint8_t ctl, uint32_t timeoutMs) {
  cmd1(0x22, ctl);
  cmd(0x20);
  return waitIdle(timeoutMs);
}

void hwReset() {
  digitalWrite(kRst, LOW);
  delay(10);
  digitalWrite(kRst, HIGH);
  delay(10);
  waitIdle(2000);
  cmd(0x12);  // reset software
  delay(10);
  waitIdle(2000);
}

void setWindow() {
  cmd1(0x11, 0x03);  // X+, Y+
  const uint8_t x[2] = {0x00, static_cast<uint8_t>(kRowBytes - 1)};
  cmdData(0x44, x, 2);
  const uint8_t y[2] = {0x00, static_cast<uint8_t>(kRamH - 1)};  // Y da 1 byte, non 2 come l'SSD1680
  cmdData(0x45, y, 2);
}

void writePlane(uint8_t ramCmd, const uint8_t *buf) {
  cmd1(0x4E, 0x00);
  cmd1(0x4F, 0x00);
  cmdData(ramCmd, buf, kRamBytes);
}

void analogOff() {
  if (!gAnalog) return;
  update(kUpdAnalogOff, 2000);
  gAnalog = false;
}

// Completo di serie con l'immagine ram: carica dall'OTP forma d'onda e tensioni (VCOM compreso) e bilancia la carica.
bool fullOtp(const uint8_t *ram) {
  analogOff();
  const uint8_t opt[4] = {0x00, 0x80, 0x03, 0x0E};  // 0x37 come heltec-eink-modules (E0213A367/mode.cpp)
  cmdData(0x37, opt, 4);
  cmd1(0x3C, 0x01);  // bordo come configFull()
  writePlane(0x24, ram);
  writePlane(0x26, ram);
  const bool ok = update(kUpdFullOtp, 10000);
  if (!ok) gLog->println("[fastink] BUSY bloccato nel completo");
  return ok;
}

// Dopo un completo: la nostra LUT al posto di quella di serie (tensioni intatte), bordo fermo, analogico acceso.
bool prepareFast() {
  uint8_t lut[kLutLen];
  buildTickLut(gP, lut);
  cmdData(0x32, lut, kLutLen);
  waitIdle(1000);
  cmd1(0x3C, 0x80);  // bordo a VCOM: fermo
  gAnalog = update(kUpdAnalogOn, 2000);
  return gAnalog;
}

void freeAll() {
  gEng.release();
  free(gPlanes);
  gPlanes = gRam = gBw = gRed = nullptr;
}

}  // namespace

void attachBus(SPIClass *bus, void (*onReturn)(), Print *log) {
  gBus = bus;
  gOnReturn = onReturn;
  if (log) gLog = log;
  pinMode(kRst, OUTPUT);
  digitalWrite(kRst, HIGH);
}

bool active() { return gOn; }

bool begin(Profile p) {
  if (gOn) return true;
  if (!gBus) {
    gLog->println("[fastink] manca attachBus()");
    return false;
  }
  gP = paramsFor(p);
  gPlanes = static_cast<uint8_t *>(malloc(3 * kRamBytes));
  if (!gPlanes || !gEng.init(gP.budget)) {
    freeAll();
    gLog->printf("[fastink] memoria insufficiente (heap libero %u)\n", static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }
  gRam = gPlanes;
  gBw = gPlanes + kRamBytes;
  gRed = gPlanes + 2 * kRamBytes;
  memset(gRam, 0xFF, kRamBytes);
  hwReset();
  setWindow();
  if (!fullOtp(gRam) || !prepareFast()) {
    freeAll();
    return false;
  }
  gEng.syncSaturated(gRam);
  gOn = true;
  gLog->printf("[fastink] acceso: profilo %s, N%u F%u P%u FR%u, bordi %u, pulizia %u, 0x22=%02X, heap libero %u\n",
               profileName(p), gP.budget, gP.tp, gP.phases, gP.fr, gP.edge, gP.polish, gCtl,
               static_cast<unsigned>(ESP.getFreeHeap()));
  return true;
}

void submit(const uint8_t *frame) {
  if (!gOn) return;
  frameToRam(frame, gRam);
  gEng.setTarget(gRam, gP.edge);
}

bool tick() {
  if (!gOn) return false;
  TickStats st;
  if (!gEng.next(gBw, gRed, gP.polish, st)) return false;
  writePlane(0x24, gBw);
  writePlane(0x26, gRed);
  const uint32_t t0 = micros();
  if (!update(gCtl, 3000)) {
    gLog->println("[fastink] BUSY bloccato nel tick: completo di sicurezza");
    cleanFull();
    return false;
  }
  gLastUs = micros() - t0;
  vTaskDelay(1);  // cede il core 0 dopo ogni passo: il task idle (watchdog) deve girare anche durante la partita
  return true;
}

bool settled() { return gOn ? gEng.idle() : true; }

void cleanFull() {
  if (!gOn) return;
  memcpy(gRam, gEng.target(), kRamBytes);
  fullOtp(gRam);
  gEng.syncSaturated(gRam);
  prepareFast();
}

void end() {
  if (!gOn) return;
  analogOff();
  freeAll();
  gOn = false;
  gLog->println("[fastink] spento: pannello restituito alla libreria");
  if (gOnReturn) gOnReturn();  // riconfigura la libreria; il suo completo è la pulizia finale
}

void setParams(const Params &p) {
  gP = p;
  if (!gOn) return;
  gEng.setBudget(p.budget);
  analogOff();
  prepareFast();
}

const Params &params() { return gP; }
void setTickCtl(uint8_t ctl) { gCtl = ctl; }
uint8_t tickCtl() { return gCtl; }
uint32_t lastTickUs() { return gLastUs; }

uint32_t measureHoldTick() {
  if (!gOn) return 0;
  memcpy(gBw, gEng.target(), kRamBytes);
  memset(gRed, 0xFF, kRamBytes);  // LUT2/LUT3 ovunque: nessun pixel spinto
  writePlane(0x24, gBw);
  writePlane(0x26, gRed);
  const uint32_t t0 = micros();
  if (!update(gCtl, 3000)) return 0;
  return micros() - t0;
}

}  // namespace fastink
}  // namespace cal
```

- [ ] **Step 3: App di prova `firmware/src/demo/fastink_demo.cpp`**

```cpp
// Prova dal vivo del driver veloce (env fastink-demo): NON è il firmware Calamaio. La libreria disegna come in Calamaio,
// poi cal::fastink anima quadrato e testo coi due profili, poi il pannello torna alla libreria.
// PRG = demo completa (~3 min); seriale 115200, «h» = aiuto.
#include <Arduino.h>
#include <heltec-eink-modules.h>
#include <stdlib.h>

#include "gfx/canvas.h"
#include "gfx/text.h"
#include "hw/fastink.h"

using cal::fastink::Params;
using cal::fastink::Profile;
namespace fi = cal::fastink;

namespace {

class Panel : public EInkDisplay_WirelessPaperV1_2 {
 public:
  SPIClass *bus() {
    begin();
    return display_spi;
  }
};

Panel display;
Canvas<fi::kScrW, fi::kScrH> cv;
CanvasSurface<fi::kScrW, fi::kScrH> surf(cv);
Profile prof = Profile::Clean;
Params tuned = fi::paramsFor(Profile::Clean);
bool custom = false;
int frames = 150;
String line;

// Disegno di serie, come Calamaio: canvas → libreria, completo.
void showLibrary(const char *big, const char *small) {
  cv.clear();
  TextOpts b;
  b.weight = 700;
  drawText(surf, big, 8, 28, 16, b);
  TextOpts s;
  s.weight = 500;
  drawText(surf, small, 8, 70, 11, s);
  display.clearMemory();
  for (int y = 0; y < fi::kScrH; y++)
    for (int x = 0; x < fi::kScrW; x++)
      if (cv.get(x, y)) display.drawPixel(x, y, BLACK);
  display.update();
}

void onReturn() { display.fastmodeOff(); }  // reset + configurazione completa; il completo lo fa showLibrary()

int bounce(int v, int range) {
  if (range <= 0) return 0;
  const int p = v % (2 * range);
  return p < range ? p : 2 * range - p;
}

void drawSquare(int f) {
  cv.clear();
  cv.fillRect(bounce(f * 6, 250 - 24), bounce(f * 4, 122 - 24), 24, 24, true);
}

void drawScroll(int f) {
  cv.clear();
  TextOpts o;
  o.weight = 700;
  const int w = textWidth("Calamaio", 26, 700);
  drawText(surf, "Calamaio", 250 - (f * 5) % (250 + w), 44, 26, o);
}

void drawBoth(int f) {
  drawScroll(f);
  cv.fillRect(bounce(f * 6, 226), bounce(f * 4, 98), 24, 24, true);
}

bool prg() { return digitalRead(0) == LOW; }

bool startFast() {
  if (!fi::begin(prof)) {
    Serial.println("[demo] begin() non riuscito");
    return false;
  }
  if (custom) fi::setParams(tuned);
  const Params &p = fi::params();
  Serial.printf("[demo] profilo %s: N%u F%u P%u FR%u, bordi %u, pulizia %u, 0x22=%02X, heap libero %u\n",
                fi::profileName(prof), p.budget, p.tp, p.phases, p.fr, p.edge, p.polish, fi::tickCtl(), ESP.getFreeHeap());
  return true;
}

// Un tick per fotogramma (come lo spike), poi fino a fermo (pulizia compresa), poi immagine ferma da guardare.
void animate(const char *name, void (*draw)(int), int holdS) {
  uint32_t maxUs = 0, sumUs = 0;
  const uint32_t t0 = millis();
  int f = 0;
  for (; f < frames; f++) {
    draw(f);
    fi::submit(cv.data());
    fi::tick();
    const uint32_t us = fi::lastTickUs();
    sumUs += us;
    if (us > maxUs) maxUs = us;
    if (prg() || Serial.available()) break;
  }
  const uint32_t ms = millis() - t0;
  int after = 0;
  while (fi::tick() && after < 100) after++;
  Serial.printf("[demo] %s, %s: %d fotogrammi in %lu ms = %.1f al secondo; tick medio %.1f ms, massimo %.1f ms; "
                "%d tick per fermarsi\n", name, fi::profileName(prof), f, static_cast<unsigned long>(ms),
                f && ms ? f * 1000.0f / ms : 0.0f, f ? sumUs / 1000.0f / f : 0.0f, maxUs / 1000.0f, after);
  Serial.printf("[demo] immagine ferma per %d s: guarda scie e frange\n", holdS);
  delay(holdS * 1000UL);
}

void demoProfile(Profile p, const char *label) {
  prof = p;
  custom = false;
  showLibrary(label, "quadrato, testo, insieme");
  delay(2000);
  if (!startFast()) return;
  animate("quadrato", drawSquare, 15);
  animate("testo", drawScroll, 10);
  animate("insieme", drawBoth, 10);
  fi::end();
}

void demo() {
  demoProfile(Profile::Clean, "PROFILO PULITO");
  demoProfile(Profile::Smooth, "PROFILO FLUIDO");
  showLibrary("FINE", "la libreria disegna di nuovo");
}

void help() {
  Serial.println("[demo] a demo completa · c/s profilo pulito/fluido · q quadrato · x testo · y insieme");
  Serial.println("[demo] e<n> spinte ai bordi · p<n> pulizia · n<n> budget · f<n> F · r<n> FR · g<n> fotogrammi");
  Serial.println("[demo] u<hex> valore di 0x22 del tick · t costo fisso (10 tick senza spinte) · z restituisci il pannello");
}

void tune(char k, int v) {
  tuned = fi::active() ? fi::params() : (custom ? tuned : fi::paramsFor(prof));
  if (k == 'e') tuned.edge = uint8_t(v);
  if (k == 'p') tuned.polish = uint8_t(v);
  if (k == 'n') tuned.budget = uint8_t(v);
  if (k == 'f') tuned.tp = uint8_t(v);
  if (k == 'r') tuned.fr = uint8_t(v);
  custom = true;
  if (fi::active()) fi::setParams(tuned);
  Serial.printf("[demo] N%u F%u FR%u bordi %u pulizia %u\n", tuned.budget, tuned.tp, tuned.fr, tuned.edge, tuned.polish);
}

void command(const String &c) {
  if (!c.length()) return;
  const char k = c[0];
  const int v = atoi(c.c_str() + 1);
  if (c == "h") help();
  else if (c == "a") demo();
  else if (c == "c" || c == "s") {
    prof = c == "c" ? Profile::Clean : Profile::Smooth;
    custom = false;
    Serial.printf("[demo] profilo %s\n", fi::profileName(prof));
  } else if (c == "q" || c == "x" || c == "y") {
    if (!fi::active() && !startFast()) return;
    animate(c == "q" ? "quadrato" : c == "x" ? "testo" : "insieme", c == "q" ? drawSquare : c == "x" ? drawScroll : drawBoth, 10);
  } else if (k == 'e' || k == 'p' || k == 'n' || k == 'f' || k == 'r') tune(k, v);
  else if (k == 'g') frames = v > 0 ? v : frames;
  else if (k == 'u') {
    fi::setTickCtl(uint8_t(strtol(c.c_str() + 1, nullptr, 16)));
    Serial.printf("[demo] 0x22 del tick = %02X\n", fi::tickCtl());
  } else if (c == "t") {
    if (!fi::active() && !startFast()) return;
    uint32_t sum = 0;
    int ok = 0;
    for (int i = 0; i < 10; i++) {
      const uint32_t us = fi::measureHoldTick();
      if (us) { sum += us; ok++; }
    }
    Serial.printf("[demo] costo fisso con 0x22=%02X: %.1f ms (media di %d); lo schermo NON deve cambiare\n", fi::tickCtl(),
                  ok ? sum / 1000.0f / ok : 0.0f, ok);
  } else if (c == "z") {
    fi::end();
    showLibrary("PANNELLO RESTITUITO", "completo di serie dalla libreria");
  } else Serial.println("[demo] comando sconosciuto (h = aiuto)");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(0, INPUT_PULLUP);
  display.landscape();
  fi::attachBus(display.bus(), onReturn, &Serial);
  Serial.println("\n[demo] FASTINK: prova del driver veloce (non è Calamaio). PRG = demo completa, h = aiuto");
  showLibrary("PROVA FASTINK", "PRG: demo completa (circa 3 minuti)");
}

void loop() {
  if (prg()) {
    delay(30);
    while (prg()) delay(10);
    demo();
  }
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      command(line);
      line = "";
    } else {
      line += ch;
    }
  }
  delay(5);
}
```

- [ ] **Step 4: `firmware/platformio.ini`**

Nell'env `[env:heltec213]` aggiungere la riga (la demo ha il suo `setup()`/`loop()`):

```ini
build_src_filter = +<*> -<demo/>
```

e in fondo al file:

```ini
; Prova dal vivo del driver veloce (piano A, Task 5 e 6): NON è il firmware Calamaio. Si scrive solo app0 (e otadata),
; NVS e LittleFS restano intatte. Vedi src/demo/fastink_demo.cpp.
[env:fastink-demo]
extends = env:heltec213
build_src_filter = -<*> +<core/fastink_engine.cpp> +<hw/fastink.cpp> +<gfx/text.cpp> +<demo/>
```

- [ ] **Step 5: `firmware/src/main.cpp` (dopo la 0.10.2)**

1. Include, accanto agli altri `hw/`: `#include "hw/fastink.h"`.
2. Nella classe `Panel` aggiungere:

```cpp
  // 0.11.0: bus SPI della libreria per il driver veloce (hw/fastink); begin() della libreria gira una volta sola.
  SPIClass *bus() {
    begin();
    return display_spi;
  }
```

3. Spostare la definizione `static RefreshKind pending = RefreshKind::None;` (oggi sotto «// ---------- comandi ----------») subito **prima** di `static void present(`, e subito dopo di essa aggiungere:

```cpp
// 0.11.0: fine del driver veloce (fastink::end). La libreria si riconfigura (reset + completo) e il prossimo disegno
// è un completo: è la pulizia del pannello dopo il gioco.
static void panelBack() {
  display.fastmodeOff();
  fastmode = false;
  pending = mergeRefresh(pending, RefreshKind::Full);
}
```

(se `fastmode` è dichiarata dopo, spostare `panelBack()` subito sotto `static bool fastmode = false;` e `pending` con lei.)

4. In cima a `present()`:

```cpp
  if (fastink::active()) {  // 0.11.0: il pannello è del driver veloce; si disegna dopo end()
    pending = mergeRefresh(pending, req);
    return;
  }
```

5. In `setup()`, subito dopo `display.landscape();`:

```cpp
  fastink::attachBus(display.bus(), panelBack, &LogSerial);  // 0.11.0: il driver veloce usa lo stesso bus
```

- [ ] **Step 6: Compilare e provare**

Run:
```bash
cd ~/CalamaiOS/firmware
pio run -e heltec213
pio run -e fastink-demo
pio test -e native
```
Expected: le due build finiscono con `SUCCESS`; tutti i test nativi PASS. Annotare le dimensioni stampate da `pio run -e heltec213` (Flash, RAM) per il confronto del Task 13.

- [ ] **Step 7: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/hw/fastink.h firmware/src/hw/fastink.cpp firmware/src/demo/fastink_demo.cpp firmware/platformio.ini firmware/src/main.cpp
git commit -m "Fastink: driver veloce sulla scheda, passaggio del pannello con la libreria e app di prova"
```

---

### Task 5: Prova dal vivo del driver veloce con l'utente (profili, scie, frange)

**Files:**
- Modify (secondo l'esito): `firmware/src/core/fastink_engine.cpp` (`paramsFor`), `firmware/test/test_fastink/test_main.cpp` (`test_profiles`)
- Modify: `docs/HARDWARE.md` (sezione nuova «Driver veloce»), `docs/DECISIONI.md`

**Interfaces:**
- Consumes: Task 4 (env `fastink-demo`, comandi seriali).
- Produces: valori definitivi di `edge` e `polish` nei due profili (F, P, N, FR restano quelli del contratto).

- [ ] **Step 1 (coordinatore): build e richiesta**

```bash
cd ~/CalamaiOS/firmware && pio run -e fastink-demo && pio run -e heltec213
```
Chiedere all'utente, in parole semplici: «Carico sulla scheda una prova del driver veloce (non è Calamaio, dura qualche minuto), poi rimetto Calamaio. Le reti, l'abbinamento e gli avatar non si toccano. Prima salvo una copia della memoria delle impostazioni. Posso?». Senza un sì esplicito ci si ferma.

- [ ] **Step 2 (coordinatore): backup e flash**

```bash
cd ~/CalamaiOS
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  read_flash 0x9000 0x7000 backup-fabbrica/nvs-otadata-$(date +%Y%m%d-%H%M)-pre-fastink.bin
chmod 600 backup-fabbrica/nvs-otadata-*-pre-fastink.bin
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 ~/CalamaiOS/firmware/.pio/build/fastink-demo/firmware.bin
pio device monitor -b 115200 -p /dev/cu.usbserial-0001
```
(Il backup contiene chiavi e password: resta in `backup-fabbrica/`, ignorata da git.)

- [ ] **Step 3 (l'utente osserva): demo completa**

L'utente preme **PRG** una volta. Per ciascun profilo (prima «PROFILO PULITO», poi «PROFILO FLUIDO»): cartello disegnato dalla libreria → quadrato che rimbalza (poi 15 s fermo) → scritta «Calamaio» che scorre (10 s ferma) → tutti e due (10 s ferma) → cartello della libreria. Fine: «FINE».
L'utente guarda e dice per ogni profilo, con un voto 1–5: fluidità; **scia** dietro al quadrato; **frange** (bordi grigi dove il quadrato è passato) nell'immagine ferma; leggibilità del testo che scorre; se i cartelli della libreria dopo il veloce sono puliti (nessun fantasma). Meglio un video col telefono e una foto dell'immagine ferma dopo il quadrato.
Il coordinatore copia dalla seriale le righe `[demo]` (fotogrammi al secondo, tick medio e massimo, tick per fermarsi, heap libero).

- [ ] **Step 4 (coordinatore e l'utente): regolazione, al massimo tre giri**

Se col profilo pulito restano frange: `c`, `e3`, `q` (poi `e4`, `q`); se la pulizia finale lampeggia troppo: `p1`, `q`. Per il fluido: `s`, `e2`, `q`. L'utente sceglie il giro migliore per ciascun profilo. Fine prova: `z`.

- [ ] **Step 5 (coordinatore): rimettere Calamaio**

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 ~/CalamaiOS/firmware/.pio/build/heltec213/firmware.bin
```
Verifica con l'utente: Calamaio si avvia, si collega al PC, abbinamento intatto.

- [ ] **Step 6: Valori scelti nel codice e nei test**

Se l'utente ha scelto valori diversi da quelli del Task 1, cambiare **solo** `edge`/`polish` in `paramsFor()` e le asserzioni corrispondenti in `test_profiles` (es. Clean `r.edge = 3;` e `TEST_ASSERT_EQUAL_UINT8(3, c.edge);`).
Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_fastink` → PASS.

- [ ] **Step 7: Documenti**

`docs/HARDWARE.md`, sezione nuova «Driver veloce (firmware)» con la data della prova: tabella profilo | N F P FR | bordi | pulizia | immagini/s | tick medio/massimo | voti dell'utente; heap libera dopo `begin()`; passaggio alla libreria riuscito sì/no.
`docs/DECISIONI.md`, voce nuova sotto «Giochi»: valori scelti e perché, spinte extra solo a zona ferma (scelta del piano), pulizia una volta a immagine ferma.

- [ ] **Step 8: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/fastink_engine.cpp firmware/test/test_fastink/test_main.cpp docs/HARDWARE.md docs/DECISIONI.md
git commit -m "Fastink: profili regolati dal vivo con l'utente"
```

---

### Task 6: Prova dal vivo — ridurre il costo fisso di ~40 ms (varianti di 0x22)

**Files:**
- Modify (secondo l'esito): `firmware/src/hw/fastink.cpp` (`kTickCtlDefault`)
- Modify: `docs/HARDWARE.md`, `docs/DECISIONI.md`

**Interfaces:**
- Consumes: Task 4 (`setTickCtl`, `measureHoldTick`, comandi `u`, `t`, `q`).
- Produces: `kTickCtlDefault` definitivo.

Si può fare nella stessa sessione di flash del Task 5 (tra lo Step 4 e lo Step 5), con lo stesso permesso dell'utente; altrimenti stessi Step 1, 2 e 5 del Task 5.

- [ ] **Step 1 (coordinatore, seriale): misure**

Per ciascun valore, nell'ordine: `u04`, `t`, `q` · `u0C`, `t`, `q` · `uC4`, `t`, `q` · `uCC`, `t`, `q` · `u84`, `t`, `q`.
(Bit di 0x22: 7 clock, 6 analogico, 3 modo 2, 2 display; nessuna variante carica LUT dall'OTP né tocca le tensioni.)
Per ogni valore annotare: costo fisso `t` in ms; durante `t` lo schermo **non** deve cambiare (se cambia: valore scartato, annotare); con `q` immagini al secondo e se il quadrato si disegna giusto (l'utente guarda: niente rumore, niente inversioni). Se un valore blocca BUSY (riga «BUSY bloccato»), scartarlo e tornare a `u04`.

- [ ] **Step 2: Scelta**

Si tiene il valore col costo fisso più basso che disegna giusto **e** che l'utente giudica non peggiore di `04` nella scia. Se nessuno batte `04` di almeno 5 ms, resta `04` e in DECISIONI si scrive «costo fisso ~40 ms non riducibile con 0x22». Se cambia: in `hw/fastink.cpp` `const uint8_t kTickCtlDefault = 0x0C;` (col valore scelto) e il commento con la misura.

- [ ] **Step 3: Build e test**

Run: `cd ~/CalamaiOS/firmware && pio run -e heltec213 && pio run -e fastink-demo && pio test -e native` → SUCCESS e PASS.

- [ ] **Step 4: Documenti e commit**

Tabella delle misure in `docs/HARDWARE.md` («Driver veloce» › «Costo fisso per 0x22»), decisione in `docs/DECISIONI.md`.

```bash
cd ~/CalamaiOS
git add firmware/src/hw/fastink.cpp docs/HARDWARE.md docs/DECISIONI.md
git commit -m "Fastink: valore di 0x22 del tick scelto dalle misure dal vivo"
```

---

### Task 7: Controller — lettura del report Xbox e croce in bit

**Files:**
- Create: `firmware/src/core/pad.h`, `firmware/src/core/pad.cpp`
- Test: `firmware/test/test_pad/test_main.cpp`

**Interfaces:**
- Consumes: niente (porting di `spikes/blepad/src/xbox_report.*`).
- Produces (namespace `cal::pad`, contratto): `enum Button : uint16_t { A=1<<0, B=1<<1, X=1<<2, Y=1<<3, LB=1<<4, RB=1<<5, View=1<<6, Menu=1<<7, Guide=1<<8, LS=1<<9, RS=1<<10, Share=1<<11, Up=1<<12, Down=1<<13, Left=1<<14, Right=1<<15 }`; `struct State { uint16_t buttons; int8_t lx, ly, rx, ry; uint16_t lt, rt; }`; `bool parseXboxReport(const uint8_t *r, size_t n, State &out)`; `enum class Link : uint8_t { Off, Pairing, Connecting, Ready }`. Aggiunte: `const size_t kReportLen = 16`; `uint16_t hatToButtons(uint8_t hat)`; `int8_t stickPercent(uint16_t raw)`; `bool sameState(const State &, const State &)`; `const char *linkKey(Link)` (`off|pairing|connecting|ready`, come `gb-pad` del contratto).

- [ ] **Step 1: Test che fallisce** — `firmware/test/test_pad/test_main.cpp`:

```cpp
// Controller (core/pad): report HID dell'Xbox 1914, mappatura dei tasti per i menu, collegamento, radio.
#include <string.h>
#include <unity.h>

#include "core/model.h"
#include "core/pad.h"

// Solo cal::pad: in cal c'è anche la classe Link (core/link.h), qui serve pad::Link.
using namespace cal::pad;

void setUp() {}
void tearDown() {}

// Report a riposo: stick al centro, grilletti a 0, niente premuto.
static void idle(uint8_t r[16]) {
  memset(r, 0, 16);
  r[1] = 0x80; r[3] = 0x80; r[5] = 0x80; r[7] = 0x80;
}

static void test_parse_idle() {
  uint8_t r[16];
  idle(r);
  State s;
  TEST_ASSERT_TRUE(parseXboxReport(r, 16, s));
  TEST_ASSERT_EQUAL_UINT16(0, s.buttons);
  TEST_ASSERT_EQUAL_INT8(0, s.lx);
  TEST_ASSERT_EQUAL_INT8(0, s.ry);
  TEST_ASSERT_EQUAL_UINT16(0, s.lt);
}

static void test_parse_short_report_rejected() {
  uint8_t r[16];
  idle(r);
  State s;
  memset(&s, 0, sizeof s);
  s.buttons = B;
  TEST_ASSERT_FALSE(parseXboxReport(r, 15, s));
  TEST_ASSERT_EQUAL_UINT16(B, s.buttons);  // invariato
  TEST_ASSERT_FALSE(parseXboxReport(nullptr, 16, s));
  uint8_t longer[20];
  memset(longer, 0, sizeof longer);
  idle(longer);
  longer[13] = 0x01;
  TEST_ASSERT_TRUE(parseXboxReport(longer, 20, s));  // byte in più: si ignorano
  TEST_ASSERT_EQUAL_UINT16(A, s.buttons);
}

static void test_parse_buttons() {
  uint8_t r[16];
  State s;
  idle(r);
  r[13] = 0x01 | 0x02 | 0x08 | 0x10 | 0x40 | 0x80;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(A | B | X | Y | LB | RB, s.buttons);
  idle(r);
  r[14] = 0x04 | 0x08 | 0x10 | 0x20 | 0x40;
  r[15] = 0x01;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(View | Menu | Guide | LS | RS | Share, s.buttons);
  idle(r);
  r[13] = 0x04 | 0x20;  // bit non usati
  r[14] = 0x03 | 0x80;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(0, s.buttons);
}

static void test_hat_to_bits() {
  TEST_ASSERT_EQUAL_UINT16(0, hatToButtons(0));
  TEST_ASSERT_EQUAL_UINT16(Up, hatToButtons(1));
  TEST_ASSERT_EQUAL_UINT16(Up | Right, hatToButtons(2));
  TEST_ASSERT_EQUAL_UINT16(Right, hatToButtons(3));
  TEST_ASSERT_EQUAL_UINT16(Down | Right, hatToButtons(4));
  TEST_ASSERT_EQUAL_UINT16(Down, hatToButtons(5));
  TEST_ASSERT_EQUAL_UINT16(Down | Left, hatToButtons(6));
  TEST_ASSERT_EQUAL_UINT16(Left, hatToButtons(7));
  TEST_ASSERT_EQUAL_UINT16(Up | Left, hatToButtons(8));
  TEST_ASSERT_EQUAL_UINT16(0, hatToButtons(9));  // fuori scala: niente
  uint8_t r[16];
  idle(r);
  r[12] = 5;
  r[13] = 0x01;
  State s;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(Down | A, s.buttons);
}

static void test_parse_axes() {
  uint8_t r[16];
  idle(r);
  r[0] = 0x00; r[1] = 0x00;    // LX tutto a sinistra
  r[2] = 0xff; r[3] = 0xff;    // LY tutto giù
  r[8] = 0xff; r[9] = 0x03;    // LT 1023
  r[10] = 0x00; r[11] = 0x02;  // RT 512
  State s;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_INT8(-100, s.lx);
  TEST_ASSERT_EQUAL_INT8(100, s.ly);
  TEST_ASSERT_EQUAL_INT8(0, s.rx);
  TEST_ASSERT_EQUAL_UINT16(1023, s.lt);
  TEST_ASSERT_EQUAL_UINT16(512, s.rt);
  TEST_ASSERT_EQUAL_INT8(0, stickPercent(32768));
  TEST_ASSERT_EQUAL_INT8(-100, stickPercent(0));
  TEST_ASSERT_EQUAL_INT8(100, stickPercent(65535));
}

static void test_same_state_and_link_keys() {
  State a, b;
  memset(&a, 0, sizeof a);
  b = a;
  TEST_ASSERT_TRUE(sameState(a, b));
  b.lx = 1;
  TEST_ASSERT_FALSE(sameState(a, b));
  TEST_ASSERT_EQUAL_STRING("off", linkKey(Link::Off));
  TEST_ASSERT_EQUAL_STRING("pairing", linkKey(Link::Pairing));
  TEST_ASSERT_EQUAL_STRING("connecting", linkKey(Link::Connecting));
  TEST_ASSERT_EQUAL_STRING("ready", linkKey(Link::Ready));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_idle);
  RUN_TEST(test_parse_short_report_rejected);
  RUN_TEST(test_parse_buttons);
  RUN_TEST(test_hat_to_bits);
  RUN_TEST(test_parse_axes);
  RUN_TEST(test_same_state_and_link_keys);
  return UNITY_END();
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad`
Expected: FAIL, «core/pad.h: No such file or directory».

- [ ] **Step 3: Implementazione**

`firmware/src/core/pad.h`:

```cpp
// Controller Xbox Series X|S (modello 1914) via Bluetooth LE: parte pura, provata in env:native (test_pad).
// Report d'ingresso (firmware 5.x, HID-over-GATT, report id 1, 16 byte senza l'id), dallo spike blepad (verificato dal vivo):
//   0-1 LX, 2-3 LY, 4-5 RX, 6-7 RY uint16 LE (centro 32768, Y: 0 = su) · 8-9 LT, 10-11 RT 0..1023 · 12 croce (hat 0..8)
//   13 bit0 A, bit1 B, bit3 X, bit4 Y, bit6 LB, bit7 RB · 14 bit2 View, bit3 Menu, bit4 Xbox, bit5 LS, bit6 RS · 15 bit0 Share
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace pad {

enum Button : uint16_t {
  A = 1u << 0, B = 1u << 1, X = 1u << 2, Y = 1u << 3, LB = 1u << 4, RB = 1u << 5, View = 1u << 6, Menu = 1u << 7,
  Guide = 1u << 8, LS = 1u << 9, RS = 1u << 10, Share = 1u << 11, Up = 1u << 12, Down = 1u << 13, Left = 1u << 14,
  Right = 1u << 15
};

struct State {
  uint16_t buttons;        // Button, OR (la croce accende Up/Down/Left/Right; le diagonali due bit)
  int8_t lx, ly, rx, ry;   // −100..100
  uint16_t lt, rt;         // 0..1023
};

const size_t kReportLen = 16;

uint16_t hatToButtons(uint8_t hat);   // 0 = niente, 1 = su, 2 = su-destra … 8 = su-sinistra; fuori scala = niente
int8_t stickPercent(uint16_t raw);    // 0..65535 → −100..100
// false (out intatto) se il report è più corto di 16 byte o manca.
bool parseXboxReport(const uint8_t *r, size_t n, State &out);
bool sameState(const State &a, const State &b);

enum class Link : uint8_t { Off, Pairing, Connecting, Ready };
const char *linkKey(Link l);  // 'off', 'pairing', 'connecting', 'ready' (come gb-pad del contratto)

}  // namespace pad
}  // namespace cal
```

`firmware/src/core/pad.cpp`:

```cpp
#include "pad.h"

#include <string.h>

namespace cal {
namespace pad {

namespace {
uint16_t u16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

uint16_t hatToButtons(uint8_t hat) {
  static const uint16_t kMap[9] = {0, Up, Up | Right, Right, Down | Right, Down, Down | Left, Left, Up | Left};
  return hat <= 8 ? kMap[hat] : 0;
}

int8_t stickPercent(uint16_t raw) {
  const long c = static_cast<long>(raw) - 32768;
  return static_cast<int8_t>(c * 100 / (c < 0 ? 32768 : 32767));
}

bool parseXboxReport(const uint8_t *r, size_t n, State &out) {
  if (!r || n < kReportLen) return false;
  State s;
  s.lx = stickPercent(u16(r + 0));
  s.ly = stickPercent(u16(r + 2));
  s.rx = stickPercent(u16(r + 4));
  s.ry = stickPercent(u16(r + 6));
  s.lt = u16(r + 8);
  s.rt = u16(r + 10);
  uint16_t b = hatToButtons(r[12]);
  const uint8_t m = r[13], c = r[14], e = r[15];
  if (m & 0x01) b |= A;
  if (m & 0x02) b |= B;
  if (m & 0x08) b |= X;
  if (m & 0x10) b |= Y;
  if (m & 0x40) b |= LB;
  if (m & 0x80) b |= RB;
  if (c & 0x04) b |= View;
  if (c & 0x08) b |= Menu;
  if (c & 0x10) b |= Guide;
  if (c & 0x20) b |= LS;
  if (c & 0x40) b |= RS;
  if (e & 0x01) b |= Share;
  s.buttons = b;
  out = s;
  return true;
}

bool sameState(const State &a, const State &b) {
  return a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry && a.lt == b.lt &&
         a.rt == b.rt;
}

const char *linkKey(Link l) {
  switch (l) {
    case Link::Pairing: return "pairing";
    case Link::Connecting: return "connecting";
    case Link::Ready: return "ready";
    default: return "off";
  }
}

}  // namespace pad
}  // namespace cal
```

- [ ] **Step 4: Eseguire e vedere passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad` → PASS (6 test).

- [ ] **Step 5: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/pad.h firmware/src/core/pad.cpp firmware/test/test_pad/test_main.cpp
git commit -m "Controller: lettura del report Xbox e croce in bit (parte pura)"
```

---

### Task 8: Controller — tasti in eventi di menu, col tenuto di A

**Files:**
- Modify: `firmware/src/core/pad.h`, `firmware/src/core/pad.cpp`
- Test: `firmware/test/test_pad/test_main.cpp`

**Interfaces:**
- Consumes: Task 7 (`State`, `Button`).
- Produces: `enum class MenuEvent : uint8_t { None, Up, Down, Select, Back, Menu, HoldDown, HoldUp, HoldFire }`; `const char *menuEventName(MenuEvent)`; `class MenuMapper { static const uint32_t kHoldMs = 3000, kTapMs = 700; int update(const State &s, uint32_t nowMs, MenuEvent *out, int cap); int tick(uint32_t nowMs, MenuEvent *out, int cap); int reset(MenuEvent *out, int cap); bool holding() const; }`.

Regole (diverse dallo spike, che dava «scegli» alla pressione): croce su/giù, B e Menu danno l'evento al fronte di discesa; **A giù** = `HoldDown`; **A su** = `HoldUp`, seguito da `Select` solo se A è stato tenuto meno di 0,7 s e il tenuto non è scattato; `HoldFire` una volta a 3 s con A ancora giù (da `tick()` o dal report successivo, prima del rilascio). Così un tenuto non è mai anche un «scegli» (nella schermata Conferma un tenuto da solo non arma e conferma insieme).

- [ ] **Step 1: Test che falliscono** — aggiungere in `test_pad/test_main.cpp`:

```cpp
static State st(uint16_t buttons) {
  State s;
  memset(&s, 0, sizeof s);
  s.buttons = buttons;
  return s;
}

struct Ev {
  MenuEvent e[8];
  int n;
};
static Ev up(MenuMapper &m, uint16_t b, uint32_t t) { Ev v; v.n = m.update(st(b), t, v.e, 8); return v; }
static Ev tk(MenuMapper &m, uint32_t t) { Ev v; v.n = m.tick(t, v.e, 8); return v; }

static void test_same_hold_as_prg() {
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(cal::kHoldMs), MenuMapper::kHoldMs);      // core/model: 3000
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(cal::kHoldShowMs), MenuMapper::kTapMs);   // 700
}

static void test_dpad_b_menu_on_press() {
  MenuMapper m;
  Ev v = up(m, Up | Right, 100);  // diagonale: conta la componente verticale
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::Up, v.e[0]);
  TEST_ASSERT_EQUAL(0, up(m, Up | Right, 200).n);  // tenuta: niente ripetizioni
  TEST_ASSERT_EQUAL(0, up(m, 0, 300).n);           // rilascio: niente
  v = up(m, Down, 400);
  TEST_ASSERT_EQUAL(MenuEvent::Down, v.e[0]);
  v = up(m, B, 500);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::Back, v.e[0]);
  v = up(m, Menu, 600);
  TEST_ASSERT_EQUAL(MenuEvent::Menu, v.e[0]);
  TEST_ASSERT_EQUAL(0, up(m, View | X | Y | LB, 700).n);  // tasti senza ruolo nei menu
}

static void test_a_tap_selects_on_release() {
  MenuMapper m;
  Ev v = up(m, A, 1000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldDown, v.e[0]);
  TEST_ASSERT_TRUE(m.holding());
  v = up(m, 0, 1120);
  TEST_ASSERT_EQUAL(2, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);
  TEST_ASSERT_EQUAL(MenuEvent::Select, v.e[1]);
  TEST_ASSERT_FALSE(m.holding());
}

// Review Focus: i due report di una pressione breve arrivano in coda durante un refresh e si leggono insieme.
static void test_tap_during_refresh_is_select() {
  MenuMapper m;
  Ev v = up(m, A, 5000);   // istante d'arrivo del report, non di lettura
  v = up(m, 0, 5090);
  TEST_ASSERT_EQUAL(MenuEvent::Select, v.e[v.n - 1]);
  TEST_ASSERT_EQUAL(0, tk(m, 9000).n);  // niente tenuto fantasma dopo
}

static void test_a_released_between_07_and_3s() {
  MenuMapper m;
  up(m, A, 0);
  TEST_ASSERT_EQUAL(0, tk(m, 2999).n);
  Ev v = up(m, 0, 1500 + 1000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);  // né scegli né conferma
}

static void test_a_hold_fires_once_at_3s() {
  MenuMapper m;
  up(m, A, 10000);
  TEST_ASSERT_EQUAL(0, tk(m, 12999).n);
  Ev v = tk(m, 13000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldFire, v.e[0]);
  TEST_ASSERT_EQUAL(0, tk(m, 14000).n);  // una volta sola
  v = up(m, 0, 15000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);  // rilascio dopo lo scatto: niente scegli
}

static void test_late_report_fires_before_release() {
  MenuMapper m;
  up(m, A, 0);
  Ev v = up(m, 0, 3200);  // nessun tick nel mezzo (refresh lungo): prima il tempo, poi il rilascio
  TEST_ASSERT_EQUAL(2, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldFire, v.e[0]);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[1]);
}

// Review Focus: controller scollegato a metà tenuta.
static void test_reset_mid_hold() {
  MenuMapper m;
  up(m, A, 0);
  Ev v;
  v.n = m.reset(v.e, 8);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);
  TEST_ASSERT_EQUAL(0, tk(m, 5000).n);  // niente scatto dopo
  v.n = m.reset(v.e, 8);
  TEST_ASSERT_EQUAL(0, v.n);            // a riposo: niente
  v = up(m, A, 6000);                   // dopo il ricollegamento A riparte da capo
  TEST_ASSERT_EQUAL(MenuEvent::HoldDown, v.e[0]);
}

static void test_cap_respected() {
  MenuMapper m;
  MenuEvent one[1];
  TEST_ASSERT_EQUAL(1, m.update(st(Up | B | A), 0, one, 1));
  TEST_ASSERT_EQUAL(0, m.update(st(0), 100, one, 0));
  TEST_ASSERT_EQUAL_STRING("A tenuto 3 s", menuEventName(MenuEvent::HoldFire));
  TEST_ASSERT_EQUAL_STRING("giù", menuEventName(MenuEvent::Down));
}
```

e in `main()`:

```cpp
  RUN_TEST(test_same_hold_as_prg);
  RUN_TEST(test_dpad_b_menu_on_press);
  RUN_TEST(test_a_tap_selects_on_release);
  RUN_TEST(test_tap_during_refresh_is_select);
  RUN_TEST(test_a_released_between_07_and_3s);
  RUN_TEST(test_a_hold_fires_once_at_3s);
  RUN_TEST(test_late_report_fires_before_release);
  RUN_TEST(test_reset_mid_hold);
  RUN_TEST(test_cap_respected);
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad`
Expected: FAIL, «'MenuMapper' was not declared in this scope».

- [ ] **Step 3: Implementazione**

In `pad.h`, prima di `}  // namespace pad`:

```cpp
// Eventi per i menu di Calamaio. Croce su/giù = sposta, A = scegli, B = indietro, Menu = apre il Menu;
// HoldDown/HoldUp/HoldFire = il tenuto 3 s di A, lo stesso del tasto PRG (indicatore, conferme, permessi).
enum class MenuEvent : uint8_t { None, Up, Down, Select, Back, Menu, HoldDown, HoldUp, HoldFire };
const char *menuEventName(MenuEvent e);  // per il registro: 'su', 'giù', 'A', 'B', 'menu', 'A giù', 'A su', 'A tenuto 3 s'

class MenuMapper {
 public:
  static const uint32_t kHoldMs = 3000;  // = cal::kHoldMs
  static const uint32_t kTapMs = 700;    // = cal::kHoldShowMs: A rilasciato prima = «scegli»
  MenuMapper() : buttons_(0), holding_(false), fired_(false), holdFrom_(0) {}
  // Nuovo stato dal controller con l'istante d'arrivo del report (ms). Al massimo cap eventi in out; ritorna quanti.
  int update(const State &s, uint32_t nowMs, MenuEvent *out, int cap);
  // Da chiamare spesso: il controller manda report solo ai cambiamenti. HoldFire a 3 s con A giù.
  int tick(uint32_t nowMs, MenuEvent *out, int cap);
  // Controller scollegato: tutto rilasciato; una tenuta in corso finisce con HoldUp (mai Select né HoldFire).
  int reset(MenuEvent *out, int cap);
  bool holding() const { return holding_; }

 private:
  uint16_t buttons_;
  bool holding_, fired_;
  uint32_t holdFrom_;
};
```

In `pad.cpp`, dopo `linkKey`:

```cpp
const uint32_t MenuMapper::kHoldMs;
const uint32_t MenuMapper::kTapMs;

namespace {
void push(MenuEvent e, MenuEvent *out, int cap, int &n) {
  if (n < cap) out[n++] = e;
}
}  // namespace

const char *menuEventName(MenuEvent e) {
  switch (e) {
    case MenuEvent::Up: return "su";
    case MenuEvent::Down: return "giù";
    case MenuEvent::Select: return "A";
    case MenuEvent::Back: return "B";
    case MenuEvent::Menu: return "menu";
    case MenuEvent::HoldDown: return "A giù";
    case MenuEvent::HoldUp: return "A su";
    case MenuEvent::HoldFire: return "A tenuto 3 s";
    default: return "-";
  }
}

int MenuMapper::update(const State &s, uint32_t now, MenuEvent *out, int cap) {
  int n = 0;
  const uint16_t pressed = s.buttons & ~buttons_, released = buttons_ & ~s.buttons;
  buttons_ = s.buttons;
  n += tick(now, out, cap);  // un report in ritardo può arrivare dopo i 3 s: prima il tempo, poi i tasti
  if (pressed & Up) push(MenuEvent::Up, out, cap, n);
  if (pressed & Down) push(MenuEvent::Down, out, cap, n);
  if (pressed & B) push(MenuEvent::Back, out, cap, n);
  if (pressed & Menu) push(MenuEvent::Menu, out, cap, n);
  if (pressed & A) {
    holding_ = true;
    fired_ = false;
    holdFrom_ = now;
    push(MenuEvent::HoldDown, out, cap, n);
  }
  if ((released & A) && holding_) {
    const bool tap = !fired_ && now - holdFrom_ < kTapMs;
    holding_ = fired_ = false;
    push(MenuEvent::HoldUp, out, cap, n);
    if (tap) push(MenuEvent::Select, out, cap, n);
  }
  return n;
}

int MenuMapper::tick(uint32_t now, MenuEvent *out, int cap) {
  if (!holding_ || fired_ || cap <= 0 || now - holdFrom_ < kHoldMs) return 0;
  fired_ = true;
  out[0] = MenuEvent::HoldFire;
  return 1;
}

int MenuMapper::reset(MenuEvent *out, int cap) {
  int n = 0;
  if (holding_) push(MenuEvent::HoldUp, out, cap, n);
  buttons_ = 0;
  holding_ = fired_ = false;
  return n;
}
```

- [ ] **Step 4: Eseguire e vedere passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad` → PASS (15 test).

- [ ] **Step 5: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/pad.h firmware/src/core/pad.cpp firmware/test/test_pad/test_main.cpp
git commit -m "Controller: tasti in eventi di menu, A tenuto 3 s come il tasto PRG"
```

---

### Task 9: Controller — macchina del collegamento, quando accendere la radio, risparmio del Wi-Fi

**Files:**
- Modify: `firmware/src/core/pad.h`, `firmware/src/core/pad.cpp`, `firmware/src/core/power.h`, `firmware/src/core/power.cpp`
- Test: `firmware/test/test_pad/test_main.cpp`, `firmware/test/test_power/test_main.cpp`

**Interfaces:**
- Consumes: Task 7 (`Link`).
- Produces: `class pad::LinkMachine { static const uint32_t kPairMs = 120000; void enable(bool on, bool bonded); void pair(uint32_t now); void forget(); void connected(bool bonded); void lost(uint32_t now, bool bondInvalid); void tick(uint32_t now); Link link() const; bool bonded() const; bool acceptNew() const; }`; `struct pad::RadioIn { bool setting, asleep, wifiPsOk, usb, idle; Link link; uint32_t now, searchFrom; }`; `struct pad::RadioOut { bool radio, scan; }`; `pad::RadioOut pad::radioPlan(const RadioIn &)`; costanti `pad::kSearchMs = 120000, kIdleScanEveryMs = 30000, kIdleScanMs = 3000`; `uint8_t cal::wifiPowerSave(bool calibMax, bool idle, bool ble)` (0 nessuno, 1 minimo, 2 massimo).

Regole: associazione aperta 2 minuti; dopo un collegamento perso si cerca subito (anche il caso dal vivo del report map troncato: si scollega e si ricollega); se la cifratura con l'abbinamento salvato fallisce (controller associato altrove) l'abbinamento si considera perso e si riapre l'associazione. Ricerca: sempre in associazione; col controller associato e non collegato, sempre col cavo o in Attivo, sempre nei primi 2 minuti dall'accensione/scollegamento/ultimo tasto, poi in Attesa 3 s ogni 30 s.

- [ ] **Step 1: Test che falliscono**

In `test_pad/test_main.cpp`:

```cpp
static void test_link_enable_and_pair() {
  LinkMachine m;
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.enable(true, false);
  TEST_ASSERT_EQUAL(Link::Off, m.link());         // niente da cercare
  m.enable(true, true);
  TEST_ASSERT_EQUAL(Link::Connecting, m.link());  // si cerca il controller associato
  m.enable(false, true);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.pair(0);
  TEST_ASSERT_EQUAL(Link::Off, m.link());         // radio spenta: niente associazione
  m.enable(true, false);
  m.pair(1000);
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());
  TEST_ASSERT_TRUE(m.acceptNew());
  m.tick(1000 + LinkMachine::kPairMs - 1);
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());
  m.tick(1000 + LinkMachine::kPairMs);
  TEST_ASSERT_EQUAL(Link::Off, m.link());         // scaduta senza controller associato
  TEST_ASSERT_FALSE(m.acceptNew());
}

static void test_link_short_report_map_reconnects() {
  LinkMachine m;
  m.enable(true, false);
  m.pair(0);
  m.lost(500, false);  // dal vivo: primo collegamento con il report map troncato, scollegato apposta
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());  // si continua a cercare (ora è abbinato: si ricollega)
  m.connected(true);
  TEST_ASSERT_EQUAL(Link::Ready, m.link());
  TEST_ASSERT_TRUE(m.bonded());
  m.lost(9000, false);  // controller spento
  TEST_ASSERT_EQUAL(Link::Connecting, m.link());
  m.connected(true);
  TEST_ASSERT_EQUAL(Link::Ready, m.link());
}

static void test_link_bond_invalid_reopens_pairing() {
  LinkMachine m;
  m.enable(true, true);
  m.lost(100, true);  // cifratura rifiutata con l'abbinamento salvato
  TEST_ASSERT_FALSE(m.bonded());
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());
  m.tick(100 + LinkMachine::kPairMs);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
}

static void test_link_forget_and_disable() {
  LinkMachine m;
  m.enable(true, true);
  m.connected(true);
  m.forget();
  TEST_ASSERT_FALSE(m.bonded());
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.enable(true, true);
  m.connected(true);
  m.enable(false, true);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.lost(10, false);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.connected(true);
  TEST_ASSERT_EQUAL(Link::Off, m.link());  // radio spenta: un collegamento tardivo non conta
}

static RadioIn rin(Link l) {
  RadioIn in;
  memset(&in, 0, sizeof in);
  in.setting = true;
  in.wifiPsOk = true;
  in.link = l;
  return in;
}

static void test_radio_plan() {
  RadioIn in = rin(Link::Connecting);
  in.setting = false;
  TEST_ASSERT_FALSE(radioPlan(in).radio);
  in = rin(Link::Connecting);
  in.asleep = true;
  TEST_ASSERT_FALSE(radioPlan(in).radio);
  in = rin(Link::Connecting);
  in.wifiPsOk = false;  // il Wi-Fi è ancora in «nessun risparmio»: prima il risparmio, poi la radio
  TEST_ASSERT_FALSE(radioPlan(in).radio);
  in = rin(Link::Ready);
  TEST_ASSERT_TRUE(radioPlan(in).radio);
  TEST_ASSERT_FALSE(radioPlan(in).scan);
  in = rin(Link::Off);  // acceso ma senza controller associato
  TEST_ASSERT_TRUE(radioPlan(in).radio);
  TEST_ASSERT_FALSE(radioPlan(in).scan);
  in = rin(Link::Pairing);
  in.idle = true;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
  in = rin(Link::Connecting);  // Attivo
  in.now = 999999;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
  in.idle = true;
  in.usb = true;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
  in.usb = false;
  in.searchFrom = 0;
  in.now = kSearchMs - 1;
  TEST_ASSERT_TRUE(radioPlan(in).scan);    // primi 2 minuti
  in.now = kSearchMs;
  TEST_ASSERT_TRUE(radioPlan(in).scan);    // 120000 % 30000 = 0: finestra di 3 s
  in.now = kSearchMs + kIdleScanMs;
  TEST_ASSERT_FALSE(radioPlan(in).scan);
  in.now = kSearchMs + kIdleScanEveryMs + 500;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
}
```

e in `main()`:

```cpp
  RUN_TEST(test_link_enable_and_pair);
  RUN_TEST(test_link_short_report_map_reconnects);
  RUN_TEST(test_link_bond_invalid_reopens_pairing);
  RUN_TEST(test_link_forget_and_disable);
  RUN_TEST(test_radio_plan);
```

In `firmware/test/test_power/test_main.cpp`, dentro il `namespace { … }` dei test (vicino agli altri `static void test_…`):

```cpp
// Review Focus: con il Bluetooth acceso l'IDF vuole il modem sleep. 0 nessuno, 1 minimo, 2 massimo.
static void test_wifi_ps_never_none_with_ble() {
  TEST_ASSERT_EQUAL_UINT8(1, wifiPowerSave(false, false, false));  // Attivo
  TEST_ASSERT_EQUAL_UINT8(2, wifiPowerSave(false, true, false));   // Attesa
  TEST_ASSERT_EQUAL_UINT8(0, wifiPowerSave(true, false, false));   // calibrazione, fase 1
  TEST_ASSERT_EQUAL_UINT8(1, wifiPowerSave(true, false, true));    // …ma col Bluetooth: minimo
  TEST_ASSERT_EQUAL_UINT8(1, wifiPowerSave(false, false, true));
  TEST_ASSERT_EQUAL_UINT8(2, wifiPowerSave(false, true, true));
  for (int c = 0; c < 2; c++)
    for (int i = 0; i < 2; i++) TEST_ASSERT_NOT_EQUAL(0, wifiPowerSave(c, i, true));
}
```

e `RUN_TEST(test_wifi_ps_never_none_with_ble);` in fondo all'elenco di `main()`.

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad -f test_power`
Expected: FAIL, «'LinkMachine' was not declared», «'wifiPowerSave' was not declared».

- [ ] **Step 3: Implementazione**

In `pad.h`, prima di `}  // namespace pad`:

```cpp
// Stato del collegamento (gira nel task del controller, hw/blepad). Un solo controller associato.
class LinkMachine {
 public:
  static const uint32_t kPairMs = 120000;  // associazione: 2 minuti per il tasto del controller
  LinkMachine() : on_(false), bonded_(false), link_(Link::Off), pairUntil_(0) {}
  void enable(bool on, bool bonded);         // radio accesa o spenta; bonded = abbinamento nella NVS
  void pair(uint32_t now);                   // «Associa controller»
  void forget();                             // «Dimentica controller»
  void connected(bool bonded);               // collegato, cifrato e iscritto ai report
  void lost(uint32_t now, bool bondInvalid); // scollegato o fallito; bondInvalid = cifratura rifiutata con l'abbinamento
  void tick(uint32_t now);                   // scadenza dell'associazione
  Link link() const { return link_; }
  bool bonded() const { return bonded_; }
  bool acceptNew() const { return link_ == Link::Pairing; }  // la ricerca accetta un controller nuovo

 private:
  bool on_, bonded_;
  Link link_;
  uint32_t pairUntil_;
};

// Quando tenere accesa la radio e quando cercare il controller (energia).
const uint32_t kSearchMs = 120000;        // dopo accensione, scollegamento o tasto: ricerca continua
const uint32_t kIdleScanEveryMs = 30000;  // poi, in Attesa a batteria, 3 s ogni 30 s
const uint32_t kIdleScanMs = 3000;
struct RadioIn {
  bool setting;   // Settings::padOn: da «Associa controller» a «Dimentica controller»
  bool asleep;    // sonno profondo in arrivo, o risveglio breve (timer)
  bool wifiPsOk;  // il Wi-Fi non è in «nessun risparmio» (vietato dall'IDF col Bluetooth)
  bool usb, idle; // cavo; stato Attesa (core/power)
  Link link;
  uint32_t now, searchFrom;  // ms
};
struct RadioOut { bool radio, scan; };
RadioOut radioPlan(const RadioIn &in);
```

In `pad.cpp`, in fondo al namespace:

```cpp
const uint32_t LinkMachine::kPairMs;

void LinkMachine::enable(bool on, bool bonded) {
  on_ = on;
  bonded_ = bonded;
  link_ = on && bonded ? Link::Connecting : Link::Off;
}

void LinkMachine::pair(uint32_t now) {
  if (!on_) return;
  link_ = Link::Pairing;
  pairUntil_ = now + kPairMs;
}

void LinkMachine::forget() {
  bonded_ = false;
  link_ = Link::Off;
}

void LinkMachine::connected(bool bonded) {
  if (!on_) return;
  link_ = Link::Ready;
  bonded_ = bonded;
}

void LinkMachine::lost(uint32_t now, bool bondInvalid) {
  if (!on_) {
    link_ = Link::Off;
    return;
  }
  if (bondInvalid) {
    bonded_ = false;
    pair(now);
    return;
  }
  if (link_ == Link::Pairing) return;  // si continua a cercare (anche il ricollegamento dopo il report map corto)
  link_ = bonded_ ? Link::Connecting : Link::Off;
}

void LinkMachine::tick(uint32_t now) {
  if (link_ == Link::Pairing && static_cast<int32_t>(now - pairUntil_) >= 0) link_ = bonded_ ? Link::Connecting : Link::Off;
}

RadioOut radioPlan(const RadioIn &in) {
  RadioOut o;
  o.radio = in.setting && !in.asleep && in.wifiPsOk;
  o.scan = false;
  if (!o.radio) return o;
  if (in.link == Link::Pairing) {
    o.scan = true;
    return o;
  }
  if (in.link != Link::Connecting) return o;
  const uint32_t since = in.now - in.searchFrom;
  o.scan = in.usb || !in.idle || since < kSearchMs || since % kIdleScanEveryMs < kIdleScanMs;
  return o;
}
```

In `firmware/src/core/power.h`, in fondo al namespace:

```cpp
// Risparmio del Wi-Fi (valori di wifi_ps_type_t: 0 nessuno, 1 minimo, 2 massimo). Con il Bluetooth acceso l'IDF vuole
// il modem sleep (coesistenza): «nessuno» diventa «minimo». Unica regola: la usa applyPower() in main.cpp.
uint8_t wifiPowerSave(bool calibMax, bool idle, bool ble);
```

In `firmware/src/core/power.cpp`, in fondo al namespace:

```cpp
uint8_t wifiPowerSave(bool calibMax, bool idle, bool ble) {
  if (calibMax) return ble ? 1 : 0;  // fase 1 della calibrazione: massimo consumo
  return idle ? 2 : 1;
}
```

Nota: se dopo la 0.10.2 la regola in `applyPower()` è diversa da «calibrazione → nessuno, Attesa → massimo, altrimenti minimo», `wifiPowerSave()` deve riprodurre la regola nuova **identica** (aggiungendo gli ingressi che servono) e il test va adattato, mantenendo l'ultima riga: col Bluetooth mai 0.

- [ ] **Step 4: Eseguire e vedere passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad -f test_power` → PASS.

- [ ] **Step 5: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/pad.h firmware/src/core/pad.cpp firmware/src/core/power.h firmware/src/core/power.cpp firmware/test/test_pad/test_main.cpp firmware/test/test_power/test_main.cpp
git commit -m "Controller: collegamento, ricerca a risparmio e Wi-Fi mai senza modem sleep col Bluetooth"
```

---

### Task 10: Controller sulla scheda — NimBLE, abbinamento, ricollegamento

**Files:**
- Create: `firmware/src/hw/blepad.h`, `firmware/src/hw/blepad.cpp`
- Modify: `firmware/platformio.ini` (env `heltec213`: NimBLE e flag)

**Interfaces:**
- Consumes: Task 7–9 (`parseXboxReport`, `sameState`, `LinkMachine`, `Link`).
- Produces (contratto): `void cal::blepad::begin()`, `void startPairing()`, `void forget()`, `pad::Link link()`, `bool poll(pad::State &s)`. Aggiunte: `void end()`, `bool active()`, `bool bonded()`, `int battery()` (0–100, −1 = sconosciuta), `void setScan(bool on)`, `uint32_t polledAt()` (istante d'arrivo, in `millis()`, dello stato appena restituito da `poll()`).

Comportamento: `begin()` inizializza NimBLE e avvia un task suo (ricerca e collegamento sono bloccanti, ~2 s: fuori dal loop). `poll()` restituisce **un report alla volta** (il primo diverso dall'ultimo restituito), così una pressione breve arrivata durante un refresh non si perde. Report map più corto di 64 byte (dal vivo: 22 byte al primo collegamento, 283 al secondo): si scollega e si ricollega subito, al massimo 2 volte di fila, poi si accetta. Un nuovo controller associato cancella gli altri abbinamenti. Niente test nativi (hardware): verifica con la build; la prova è il Task 14.

- [ ] **Step 1: `firmware/platformio.ini`, env `heltec213`**

In `build_flags` aggiungere:

```ini
  ; Controller (piano A giochi): NimBLE solo centrale, una connessione (come spikes/blepad)
  -D CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED
  -D CONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED
  -D CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
```

e in `lib_deps`:

```ini
  h2zero/NimBLE-Arduino@2.5.1
```

- [ ] **Step 2: `firmware/src/hw/blepad.h`**

```cpp
// Controller Xbox (1914) via Bluetooth LE: NimBLE-Arduino 2.5.1 come centrale, HID-over-GATT scritto qui
// (porting di spikes/blepad). Abbinamento nella NVS «nimble_bond» (di NimBLE). Un task proprio fa ricerca e collegamento.
#pragma once
#include <stdint.h>

#include "core/pad.h"

namespace cal {
namespace blepad {

// ---- contratto ----
void begin();          // NimBLE e task del controller; si ricollega da solo al controller associato
void startPairing();   // 2 minuti per un controller nuovo in associazione (sostituisce il vecchio)
void forget();         // cancella l'abbinamento (anche a radio spenta)
pad::Link link();
bool poll(pad::State &s);  // true con il prossimo stato diverso dall'ultimo (un report alla volta)

// ---- piano A ----
void end();            // scollega, ferma il task, NimBLEDevice::deinit(true): radio spenta
bool active();
bool bonded();         // c'è un controller associato
int battery();         // 0–100, −1 = sconosciuta
void setScan(bool on); // cercare adesso (pad::radioPlan)
uint32_t polledAt();   // millis() d'arrivo dello stato restituito da poll()

}  // namespace blepad
}  // namespace cal
```

- [ ] **Step 3: `firmware/src/hw/blepad.cpp`**

```cpp
#include "hw/blepad.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <string.h>

#include "hw/logtee.h"

namespace cal {
namespace blepad {
namespace {

const uint16_t kAppearanceGamepad = 0x03C4;
const uint32_t kScanMs = 5000;
const size_t kMinReportMap = 64;  // dal vivo: 22 byte (troncato) al primo collegamento, 283 quello vero
const int kShortMapRetries = 2;
const uint16_t kItvMin = 6, kItvMax = 12;  // 7,5–15 ms; il 1914 chiede 7,5 ms e lo si accetta
const char *const kBondNs = "nimble_bond";

struct Rx {
  uint32_t t;
  uint8_t len;
  uint8_t d[24];
};

QueueHandle_t gRx = nullptr;
TaskHandle_t gTask = nullptr;
volatile bool gActive = false, gStop = false, gTaskDone = false;
volatile bool gScanAllowed = false, gWantPair = false, gWantForget = false;
volatile bool gFound = false, gLost = false, gBondInvalid = false;
volatile pad::Link gLinkPub = pad::Link::Off;
volatile bool gBondedPub = false;
volatile int gBatt = -1;
volatile uint16_t gBatHandle = 0;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
NimBLEAddress gFoundAddr;  // protetto da gMux
NimBLEClient *gClient = nullptr;
pad::LinkMachine gMachine;  // solo nel task del controller
int gShortMaps = 0;
pad::State gLast;  // ultimo stato restituito da poll() (loop)
uint32_t gLastAt = 0;

bool bondInNvs() {
  Preferences p;
  if (!p.begin(kBondNs, true)) return false;
  const bool has = p.isKey("peer_sec_1");  // chiave di NimBLE vista nello spike
  p.end();
  return has;
}

void publish() {
  gLinkPub = gMachine.link();
  gBondedPub = gMachine.bonded();
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    const NimBLEAddress &a = dev->getAddress();
    const bool bonded = NimBLEDevice::isBonded(a);
    const bool gamepad = dev->haveAppearance() && dev->getAppearance() == kAppearanceGamepad;
    const bool hid = dev->isAdvertisingService(NimBLEUUID("1812"));
    const bool take = bonded || (gLinkPub == pad::Link::Pairing && gamepad && hid && dev->isConnectable());
    if (!take || gFound) return;
    portENTER_CRITICAL(&gMux);
    gFoundAddr = a;
    portEXIT_CRITICAL(&gMux);
    gFound = true;
    NimBLEDevice::getScan()->stop();
  }
} gScanCb;

class ClientCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    LogSerial.printf("[pad] scollegato, motivo 0x%x\n", reason);
    gLost = true;
  }
  bool onConnParamsUpdateRequest(NimBLEClient *, const ble_gap_upd_params *) override { return true; }
  void onConfirmPasskey(NimBLEConnInfo &ci, uint32_t) override { NimBLEDevice::injectConfirmPasskey(ci, true); }
  void onPassKeyEntry(NimBLEConnInfo &ci) override { NimBLEDevice::injectPassKey(ci, 0); }
} gClientCb;

void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool) {
  if (c->getHandle() == gBatHandle) {
    if (n) gBatt = d[0];
    return;
  }
  Rx r;
  r.t = millis();
  r.len = n > sizeof r.d ? sizeof r.d : static_cast<uint8_t>(n);
  memcpy(r.d, d, r.len);
  xQueueSend(gRx, &r, 0);  // coda piena: il report si perde (64 report = ~1,5 s di stick)
}

enum class Gatt : uint8_t { Ok, NoHid, NoInput, ShortMap };

Gatt setupGatt() {
  NimBLERemoteService *hid = gClient->getService(NimBLEUUID("1812"));
  if (!hid) return Gatt::NoHid;
  size_t mapLen = 0;
  int subscribed = 0;
  for (NimBLERemoteCharacteristic *c : hid->getCharacteristics(true)) {
    if (c->getUUID() == NimBLEUUID("2a4b")) {
      mapLen = c->readValue().size();
      continue;
    }
    if (c->getUUID() != NimBLEUUID("2a4d")) continue;
    uint8_t id = 0, type = 0;
    if (NimBLERemoteDescriptor *rr = c->getDescriptor(NimBLEUUID("2908"))) {
      const NimBLEAttValue v = rr->readValue();
      if (v.size() >= 2) {
        id = v[0];
        type = v[1];
      }
    }
    if (type == 1 && id == 1 && c->canNotify() && c->subscribe(true, onNotify, true)) subscribed++;
  }
  if (NimBLERemoteService *bat = gClient->getService(NimBLEUUID("180f"))) {
    if (NimBLERemoteCharacteristic *c = bat->getCharacteristic(NimBLEUUID("2a19"))) {
      gBatHandle = c->getHandle();
      const NimBLEAttValue v = c->readValue();
      if (v.size()) gBatt = v[0];
      if (c->canNotify()) c->subscribe(true, onNotify, true);
    }
  }
  LogSerial.printf("[pad] report map %u byte, report d'ingresso id 1 iscritti: %d\n", static_cast<unsigned>(mapLen), subscribed);
  if (!subscribed) return Gatt::NoInput;
  if (mapLen < kMinReportMap) return Gatt::ShortMap;
  return Gatt::Ok;
}

void connectFound() {
  NimBLEAddress a;
  portENTER_CRITICAL(&gMux);
  a = gFoundAddr;
  portEXIT_CRITICAL(&gMux);
  const bool wasBonded = NimBLEDevice::isBonded(a);
  if (!gClient) {
    gClient = NimBLEDevice::createClient();
    gClient->setClientCallbacks(&gClientCb, false);
    gClient->setConnectTimeout(5000);
  }
  gClient->setConnectionParams(kItvMin, kItvMax, 0, 300);
  const uint32_t t0 = millis();
  if (!gClient->connect(a, true)) {
    LogSerial.println("[pad] collegamento non riuscito");
    return;
  }
  if (!gClient->secureConnection()) {
    LogSerial.println("[pad] cifratura non riuscita");
    if (wasBonded) {  // associato altrove: l'abbinamento non vale più
      NimBLEDevice::deleteBond(a);
      gBondInvalid = true;
      LogSerial.println("[pad] abbinamento non più valido: cancellato, serve «Associa controller»");
    }
    gClient->disconnect();
    return;
  }
  const Gatt g = setupGatt();
  if (g == Gatt::NoHid || g == Gatt::NoInput) {
    gClient->disconnect();
    return;
  }
  if (g == Gatt::ShortMap && gShortMaps < kShortMapRetries) {
    gShortMaps++;
    LogSerial.println("[pad] report map corto: ricollego subito");
    gClient->disconnect();
    return;
  }
  gShortMaps = 0;
  for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; i--) {  // un solo controller associato
    const NimBLEAddress b = NimBLEDevice::getBondedAddress(i);
    if (!(b == a)) NimBLEDevice::deleteBond(b);
  }
  gClient->updateConnParams(kItvMin, kItvMax, 0, 300);
  gMachine.connected(NimBLEDevice::isBonded(a));
  publish();
  LogSerial.printf("[pad] PRONTO in %lu ms (%s)\n", static_cast<unsigned long>(millis() - t0),
                   wasBonded ? "ricollegamento" : "nuovo");
}

void padTask(void *) {
  while (!gStop) {
    const uint32_t now = millis();
    NimBLEScan *s = NimBLEDevice::getScan();
    if (gWantForget) {
      gWantForget = false;
      if (s->isScanning()) s->stop();
      if (gClient && gClient->isConnected()) gClient->disconnect();
      NimBLEDevice::deleteAllBonds();
      gMachine.forget();
    }
    if (gWantPair) {
      gWantPair = false;
      if (gClient && gClient->isConnected()) gClient->disconnect();
      gMachine.pair(now);
    }
    if (gLost) {
      gLost = false;
      const bool invalid = gBondInvalid;
      gBondInvalid = false;
      gBatHandle = 0;
      gBatt = -1;
      gMachine.lost(now, invalid);
    }
    gMachine.tick(now);
    publish();
    const pad::Link l = gMachine.link();
    if (l == pad::Link::Ready) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (gFound) {
      if (s->isScanning()) s->stop();
      connectFound();
      gFound = false;
      continue;
    }
    const bool want = gScanAllowed && (l == pad::Link::Pairing || l == pad::Link::Connecting);
    if (want && !s->isScanning()) s->start(kScanMs, false, true);
    else if (!want && s->isScanning()) s->stop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  NimBLEScan *s = NimBLEDevice::getScan();
  if (s->isScanning()) s->stop();
  if (gClient && gClient->isConnected()) gClient->disconnect();
  gTaskDone = true;
  vTaskDelete(nullptr);
}

}  // namespace

void begin() {
  if (gActive) return;
  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  NimBLEDevice::setSecurityAuth(true, false, false);  // abbinamento sì, MITM no, Secure Connections no (come lo spike)
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEScan *sc = NimBLEDevice::getScan();
  sc->setScanCallbacks(&gScanCb, true);
  sc->setActiveScan(true);
  sc->setInterval(100);
  sc->setWindow(60);  // non il 100%: lascia tempo al Wi-Fi
  sc->setMaxResults(0);
  if (!gRx) gRx = xQueueCreate(64, sizeof(Rx));
  gMachine.enable(true, NimBLEDevice::getNumBonds() > 0);
  publish();
  gStop = gTaskDone = gFound = gLost = gBondInvalid = gWantPair = gWantForget = false;
  gBatt = -1;
  gShortMaps = 0;
  memset(&gLast, 0, sizeof gLast);
  gActive = true;
  xTaskCreatePinnedToCore(padTask, "pad", 6144, nullptr, 1, &gTask, 0);
}

void end() {
  if (!gActive) return;
  gStop = true;
  const uint32_t t0 = millis();
  while (!gTaskDone && millis() - t0 < 3000) delay(10);
  if (!gTaskDone) LogSerial.println("[pad] il task del controller non si è fermato in 3 s");
  gTask = nullptr;
  NimBLEDevice::deinit(true);
  gClient = nullptr;
  gMachine.enable(false, false);
  gActive = false;
  gLinkPub = pad::Link::Off;
  gBatt = -1;
  if (gRx) xQueueReset(gRx);
}

bool active() { return gActive; }

void startPairing() {
  if (gActive) gWantPair = true;
}

void forget() {
  gBondedPub = false;
  if (gActive) {
    gWantForget = true;
    return;
  }
  Preferences p;
  if (p.begin(kBondNs, false)) {
    p.clear();
    p.end();
  }
}

pad::Link link() { return gActive ? gLinkPub : pad::Link::Off; }
bool bonded() { return gActive ? gBondedPub : bondInNvs(); }
int battery() { return gActive ? gBatt : -1; }
void setScan(bool on) { gScanAllowed = on; }
uint32_t polledAt() { return gLastAt; }

bool poll(pad::State &s) {
  if (!gRx) return false;
  Rx r;
  while (xQueueReceive(gRx, &r, 0) == pdTRUE) {
    pad::State n;
    if (!pad::parseXboxReport(r.d, r.len, n)) continue;
    if (pad::sameState(n, gLast)) continue;
    gLast = n;
    gLastAt = r.t;
    s = n;
    return true;
  }
  return false;
}

}  // namespace blepad
}  // namespace cal
```

- [ ] **Step 3b: Compilare**

Run: `cd ~/CalamaiOS/firmware && pio run -e heltec213 && pio test -e native`
Expected: SUCCESS (blepad non è ancora chiamato da main: il linker lo scarta, va bene) e test PASS. Se la build segnala un nome dell'API NimBLE diverso da quello usato qui, confrontare con `spikes/blepad/src/main.cpp` (compilato e provato con la stessa versione) e usare quello.

- [ ] **Step 4: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/hw/blepad.h firmware/src/hw/blepad.cpp firmware/platformio.ini
git commit -m "Controller: NimBLE sulla scheda con task proprio, abbinamento e ricollegamento"
```

---

### Task 11: Il controller nei menu e nei permessi (`Ui::padEvent`)

Dopo che la 0.10.2 è stata unita (tocca `ui.h`, `ui.cpp`, `model.h`, `model.cpp`).

**Files:**
- Create: `firmware/src/core/ui_pad.cpp`
- Modify: `firmware/src/core/model.h` (campi di `UiState`), `firmware/src/core/model.cpp` (`holdLabel`), `firmware/src/core/ui.h`, `firmware/src/core/ui.cpp` (`Ui::keyDown`)
- Test: `firmware/test/test_pad_ui/test_main.cpp`

**Interfaces:**
- Consumes: Task 8 (`pad::MenuEvent`, `pad::MenuMapper`); da `cal::Ui` i privati `act`, `gesture`, `keyDown`, `keyUp`, `confirmPerm`, `finish`, `redraw`, `setAction`, `req_`, `lastInput_`, `lastInputAt_`.
- Produces: `RefreshKind Ui::padEvent(pad::MenuEvent e, Ms now, const WallClock &wc)`; campi `UiState::{holdPad, holdPerm, holdI, holdRid[kReqIdLen]}`; `holdLabel()` nel Permesso col tenuto di A = «CONSENTI»/«NEGA».

Mappatura (uguale ai gesti del tasto PRG): `Up` → `Action::Prev`, `Down` → `Action::Next` (nel gioco «Alto o basso»: su = alta come 1×, giù = bassa come 2×), `Select` → `Action::Ok`, `Back` → `Action::Back` (senza il silenzio della pressione lunga), `Menu` → `Action::Menu`, `HoldDown/HoldUp` → l'indicatore del tenuto (`keyDown/keyUp`), `HoldFire` → come il tenuto 3 s del PRG (Riposo, o conferma armata). **Permesso (§7):** su/giù scelgono (NEGA preselezionato), `Select` non risponde mai (toast «Tieni premuto A per 3 s»), `HoldFire` conferma la scelta solo se il tenuto è iniziato su quella stessa richiesta; rilasciato tra 0,7 e 3 s → «Troppo presto: tieni fino in fondo» e la richiesta resta aperta; B = «Annullato · resta in attesa» come la pressione lunga. Se durante una tenuta di A si preme PRG, vince PRG e il tenuto di A non fa più niente.

- [ ] **Step 1: Test che falliscono** — `firmware/test/test_pad_ui/test_main.cpp`:

```cpp
// Il controller nell'interfaccia (core/ui_pad): menu come il tasto PRG, permessi solo col tenuto di A (§7),
// Menu › Gioco. Gli eventi passano dal pad::MenuMapper vero, come sulla scheda.
#include <string.h>
#include <unity.h>

#include "core/gesture.h"
#include "core/menu.h"
#include "core/pad.h"
#include "core/ui.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static const int64_t kWall0 = wallFromCivil(2026, 10, 11, 10, 29, 0).localMs;

struct Bench {
  Ui ui;
  pad::MenuMapper map;
  GestureRecognizer rec;
  Ms now;
  PermEvent events[8];
  int nEvents;
  Bench() : ui(kHeltec213), now(0), nEvents(0) {
    ui.setPermListener(&Bench::onPerm, this);
    ui.begin(now, wc());
  }
  WallClock wc() const { WallClock w; w.localMs = kWall0 + now; return w; }
  static void onPerm(const PermEvent &e, void *ctx) {
    Bench *b = static_cast<Bench *>(ctx);
    if (b->nEvents < 8) b->events[b->nEvents++] = e;
  }
  RefreshKind tick() { return ui.tick(now, wc()); }
  void boot() { now += 2601; tick(); TEST_ASSERT_EQUAL(ScreenId::Home, topId()); }
  void feed(const pad::MenuEvent *ev, int n) { for (int i = 0; i < n; i++) ui.padEvent(ev[i], now, wc()); }
  void state(uint16_t buttons) {
    pad::State s;
    memset(&s, 0, sizeof s);
    s.buttons = buttons;
    pad::MenuEvent ev[8];
    feed(ev, map.update(s, static_cast<uint32_t>(now), ev, 8));
  }
  // Avanza di ms con il tick del mappatore e dell'interfaccia ogni 100 ms (come il loop).
  void run(Ms ms) {
    for (Ms t = 0; t < ms;) {
      const Ms step = ms - t < 100 ? ms - t : 100;
      now += step;
      t += step;
      pad::MenuEvent ev[4];
      feed(ev, map.tick(static_cast<uint32_t>(now), ev, 4));
      tick();
    }
  }
  // Tasto premuto per ms e rilasciato, poi il dito si stacca.
  void tap(uint16_t b, Ms ms = 80) {
    state(b);
    run(ms);
    state(0);
    now += 150;
  }
  // Tasto PRG con il riconoscitore vero (per i confronti).
  RefreshKind press(int n) {
    for (int k = 0; k < n; k++) {
      rec.down(static_cast<uint32_t>(now));
      now += 80;
      rec.up(static_cast<uint32_t>(now));
      now += k + 1 < n ? 150 : 0;
    }
    now += GestureRecognizer::kMultiMs + 1;
    return ui.gesture(rec.poll(static_cast<uint32_t>(now)), now, wc());
  }
  ScreenId topId() const { return top(ui.model()).id; }
};

// Home → Dettaglio di web-app (chiede un permesso) → Permesso, tutto col controller.
static void openPerm(Bench &b) {
  b.boot();
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).choice);  // NEGA preselezionato
}

static void test_single_a_never_answers() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);  // CONSENTI scelto
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING("Tieni premuto A per 3 s", b.ui.model().toast.msg);
}

static void test_hold_a_allows_chosen() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Consentito · inviato a Claude Code", b.ui.model().toast.msg);
}

static void test_hold_a_denies_preselected() {
  Bench b;
  openPerm(b);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Deny, b.events[0].outcome);
}

static void test_hold_released_early_keeps_request() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.tap(pad::A, 2500);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING("Troppo presto: tieni fino in fondo", b.ui.model().toast.msg);
}

static void test_b_cancels_like_long_press() {
  Bench b;
  openPerm(b);
  b.tap(pad::B);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Cancelled, b.events[0].outcome);
}

static void test_hold_label_on_perm() {
  Bench b;
  openPerm(b);
  b.state(pad::A);
  b.run(800);
  TEST_ASSERT_EQUAL_STRING("NEGA", holdLabel(b.ui.model(), b.now));
  b.state(0);
  b.now += 150;
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(800);
  TEST_ASSERT_EQUAL_STRING("CONSENTI", holdLabel(b.ui.model(), b.now));
  b.state(0);
  b.now += 150;
  b.ui.keyDown(b.now);  // tasto PRG: il tenuto lì resta «Riposo», come oggi
  TEST_ASSERT_EQUAL_STRING("RIPOSO", holdLabel(b.ui.model(), b.now));
  b.ui.keyUp(b.now + 100);
}

// Review Focus: la richiesta si chiude (risposta dal terminale) durante la tenuta.
static void test_perm_closed_mid_hold() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(1000);
  popScreen(b.ui.model());  // come netPermClose: torna il Dettaglio
  b.run(2200);              // scatta a 3 s
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());  // e nemmeno Riposo
  b.state(0);
}

// Review Focus: il tenuto inizia altrove e a 3 s c'è una richiesta in cima.
static void test_hold_started_elsewhere_never_answers() {
  Bench b;
  b.boot();
  b.tap(pad::A);  // Dettaglio
  b.state(pad::A);
  b.run(1000);
  ScreenEntry e = makeScreen(ScreenId::Perm);
  e.i = 1;
  e.choice = 0;  // CONSENTI già scelto
  pushScreen(b.ui.model(), e);
  b.run(2200);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  b.state(0);
}

// Review Focus: controller scollegato a metà tenuta.
static void test_disconnect_mid_hold() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(2000);
  pad::MenuEvent ev[4];
  b.feed(ev, b.map.reset(ev, 4));
  b.run(3000);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOn);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
}

static void test_prg_down_wins_over_pad_hold() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(500);
  b.ui.keyDown(b.now);  // PRG giù durante la tenuta di A
  b.run(2700);          // il mappatore dà HoldFire: non deve rispondere
  TEST_ASSERT_EQUAL(0, b.nEvents);
  b.ui.keyUp(b.now);
  b.state(0);
}

static void test_menu_navigation() {
  Bench b;
  b.boot();
  b.tap(pad::Menu);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL(MenuId::Main, top(b.ui.model()).menu);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).sel);
  b.tap(pad::Down);
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).sel);
  b.tap(pad::Up);
  b.tap(pad::Up);  // da 0 si va all'ultima voce, come Prev della rotella
  TEST_ASSERT_EQUAL(b.ui.items(b.now, b.wc()).n - 1, top(b.ui.model()).sel);
  b.tap(pad::Down);  // di nuovo 0
  b.tap(pad::Down);  // Strumenti
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(MenuId::Tools, top(b.ui.model()).menu);
  b.tap(pad::B);
  TEST_ASSERT_EQUAL(MenuId::Main, top(b.ui.model()).menu);
  b.tap(pad::B);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
}

// Indice della voce che apre/esegue qualcosa nella lista in cima.
static int findItem(Bench &b, ItemKind k, ScreenId open, ActId act) {
  const MenuList &L = b.ui.items(b.now, b.wc());
  for (int i = 0; i < L.n; i++)
    if (L.items[i].kind == k && (k == ItemKind::Open ? L.items[i].open == open : L.items[i].act == act)) return i;
  return -1;
}

static void pushList(Bench &b, MenuId id) {
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = id;
  pushScreen(b.ui.model(), e);
}

// Ripristino di fabbrica: col controller come col PRG servono due gesti (A, poi A tenuto 3 s).
static void test_confirm_same_as_prg() {
  Bench p, c;
  p.boot();
  c.boot();
  pushList(p, MenuId::Security);
  pushList(c, MenuId::Security);
  const int k = findItem(p, ItemKind::Act, ScreenId::Home, ActId::Factory);
  TEST_ASSERT_TRUE(k >= 0);
  top(p.ui.model()).sel = top(c.ui.model()).sel = k;
  p.tap(pad::A);
  c.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Confirm, p.topId());
  p.tap(pad::A, 3200);  // tenuto senza armare: come il PRG, non conferma
  c.ui.keyDown(c.now);
  c.now += 3000;
  c.ui.gesture(Gesture::VeryLong, c.now, c.wc());
  c.ui.keyUp(c.now + 100);
  c.now += 250;
  TEST_ASSERT_EQUAL_STRING(c.ui.model().toast.msg, p.ui.model().toast.msg);
  p.tap(pad::A);        // arma (come 2×)
  c.press(2);
  p.tap(pad::A, 3200);  // conferma
  c.ui.keyDown(c.now);
  c.now += 3000;
  c.ui.gesture(Gesture::VeryLong, c.now, c.wc());
  TEST_ASSERT_EQUAL_STRING(c.ui.lastAction(), p.ui.lastAction());
  TEST_ASSERT_EQUAL(c.topId(), p.topId());
}

// «Alto o basso»: su = alta (1×), giù = bassa (2×).
static void test_game_up_down_like_prg() {
  Bench p, c;
  p.boot();
  c.boot();
  pushList(p, MenuId::Tools);
  pushList(c, MenuId::Tools);
  const int k = findItem(p, ItemKind::Open, ScreenId::Game, ActId::None);
  TEST_ASSERT_TRUE(k >= 0);
  top(p.ui.model()).sel = top(c.ui.model()).sel = k;
  p.tap(pad::A);
  c.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Game, p.topId());
  p.tap(pad::Up);
  c.press(1);
  TEST_ASSERT_EQUAL(c.ui.model().game.streak, p.ui.model().game.streak);
  TEST_ASSERT_EQUAL(c.ui.model().game.card, p.ui.model().game.card);
  TEST_ASSERT_EQUAL_STRING(c.ui.lastAction(), p.ui.lastAction());
  p.tap(pad::Down);
  c.press(2);
  TEST_ASSERT_EQUAL(c.ui.model().game.card, p.ui.model().game.card);
  TEST_ASSERT_EQUAL_STRING(c.ui.lastAction(), p.ui.lastAction());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_single_a_never_answers);
  RUN_TEST(test_hold_a_allows_chosen);
  RUN_TEST(test_hold_a_denies_preselected);
  RUN_TEST(test_hold_released_early_keeps_request);
  RUN_TEST(test_b_cancels_like_long_press);
  RUN_TEST(test_hold_label_on_perm);
  RUN_TEST(test_perm_closed_mid_hold);
  RUN_TEST(test_hold_started_elsewhere_never_answers);
  RUN_TEST(test_disconnect_mid_hold);
  RUN_TEST(test_prg_down_wins_over_pad_hold);
  RUN_TEST(test_menu_navigation);
  RUN_TEST(test_confirm_same_as_prg);
  RUN_TEST(test_game_up_down_like_prg);
  return UNITY_END();
}
```

(Se `ActId::None` o il campo `game.card`/`game.streak` hanno un nome diverso dopo la 0.10.2, usare quelli di `core/menu.h` e `core/game.h`; il senso del test non cambia.)

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad_ui`
Expected: FAIL, «'class cal::Ui' has no member named 'padEvent'».

- [ ] **Step 3: Implementazione**

`firmware/src/core/model.h`, in `struct UiState` dopo `bool holdOk;`:

```cpp
  // 0.11.0: tenuto di A sul controller (core/pad). Nel Permesso conferma la scelta solo se è iniziato lì, sulla
  // stessa richiesta (holdI, holdRid); un tasto PRG premuto nel frattempo lo annulla.
  bool holdPad;
  bool holdPerm;
  int holdI;
  char holdRid[kReqIdLen];
```

`firmware/src/core/model.cpp`, in `holdLabel()` subito prima di `return "RIPOSO";`:

```cpp
  // 0.11.0: A tenuto sul controller nel Permesso: conferma la scelta, solo per la richiesta su cui è iniziato.
  if (tp.id == ScreenId::Perm && m.ui.holdPad)
    return m.ui.holdPerm && m.ui.holdI == tp.i && streq(m.ui.holdRid, tp.rid) ? (tp.choice == 0 ? "CONSENTI" : "NEGA")
                                                                             : nullptr;
```

`firmware/src/core/ui.h`: aggiungere `#include "pad.h"` agli include; nella parte pubblica di `Ui`, dopo il blocco «0.10.0: gioco della mascotte»:

```cpp
  // ---------- 0.11.0: controller Bluetooth (core/pad, hw/blepad) ----------
  // Evento del controller (pad::MenuMapper), come i gesti del tasto PRG: su/giù = Prev/Next, A = Ok, B = Back,
  // Menu = Menu; HoldDown/HoldUp/HoldFire = il tenuto 3 s, con lo stesso indicatore. Nel Permesso è l'unico modo di
  // rispondere (§7: CONSENTI o NEGA scelto con su/giù, tenuto iniziato su quella richiesta); A premuto lì non risponde.
  RefreshKind padEvent(pad::MenuEvent e, Ms now, const WallClock &wc);
```

e nella parte privata:

```cpp
  RefreshKind padHoldDown(Ms now);
  RefreshKind padHoldUp(Ms now, const WallClock &wc);
  RefreshKind padHoldFire(Ms now, const WallClock &wc);
```

`firmware/src/core/ui.cpp`, in `Ui::keyDown` dopo `u.holdDone = u.holdOk = false;`:

```cpp
  u.holdPad = u.holdPerm = false;  // 0.11.0: il tasto PRG vince su un tenuto di A in corso
```

`firmware/src/core/ui_pad.cpp`:

```cpp
// Il controller nell'interfaccia: eventi di pad::MenuMapper tradotti nelle azioni del tasto PRG (vedi ui.h).
#include <string.h>

#include "strbuf.h"
#include "ui.h"

namespace cal {

RefreshKind Ui::padEvent(pad::MenuEvent e, Ms now, const WallClock &wc) {
  const ScreenEntry &tp = top(m_);
  switch (e) {
    case pad::MenuEvent::Up:
      scopy(lastInput_, "Controller: su");
      return act(tp.id == ScreenId::Game ? Action::Next : Action::Prev, now, wc);  // nel gioco: alta (1×)
    case pad::MenuEvent::Down:
      scopy(lastInput_, "Controller: giù");
      return act(tp.id == ScreenId::Game ? Action::Ok : Action::Next, now, wc);    // nel gioco: bassa (2×)
    case pad::MenuEvent::Select:
      scopy(lastInput_, "Controller: A");
      if (tp.id == ScreenId::Perm) {  // §7: una pressione non risponde mai
        req_ = RefreshKind::None;
        lastInputAt_ = now;
        toast(m_, "Tieni premuto A per 3 s", now);
        redraw(RefreshKind::Partial);
        setAction("Permesso: tieni premuto A per 3 secondi per confermare la scelta");
        return finish(now);
      }
      return act(Action::Ok, now, wc);
    case pad::MenuEvent::Back:
      scopy(lastInput_, "Controller: B");
      return act(Action::Back, now, wc);
    case pad::MenuEvent::Menu:
      scopy(lastInput_, "Controller: Menu");
      return act(Action::Menu, now, wc);
    case pad::MenuEvent::HoldDown: return padHoldDown(now);
    case pad::MenuEvent::HoldUp: return padHoldUp(now, wc);
    case pad::MenuEvent::HoldFire: return padHoldFire(now, wc);
    default: return RefreshKind::None;
  }
}

RefreshKind Ui::padHoldDown(Ms now) {
  const RefreshKind r = keyDown(now);  // indicatore come il tasto PRG
  UiState &u = m_.ui;
  const ScreenEntry &tp = top(m_);
  u.holdPad = true;
  u.holdPerm = tp.id == ScreenId::Perm;
  u.holdI = tp.i;
  scopy(u.holdRid, u.holdPerm ? tp.rid : "");
  scopy(lastInput_, "Controller: A tenuto…");
  return r;
}

RefreshKind Ui::padHoldUp(Ms now, const WallClock &wc) {
  UiState &u = m_.ui;
  if (!u.holdPad) return RefreshKind::None;  // il tenuto in corso è del tasto PRG, o non c'è
  const bool done = u.holdDone, onPerm = u.holdPerm;
  const Ms el = now - u.holdSince;
  RefreshKind r = keyUp(now);  // keyUp guarda holdLabel: holdPad si spegne dopo
  u.holdPad = u.holdPerm = false;
  u.holdRid[0] = 0;
  scopy(lastInput_, "Controller: A rilasciato");
  if (done || el < kHoldShowMs) return r;
  ScreenEntry &tp = top(m_);
  // Conferma armata (Proteggi le chiavi, Ripristino): come la pressione lunga del PRG, resta armata.
  if (tp.id == ScreenId::Confirm && tp.until > now) return mergeRefresh(r, act(Action::Back, now, wc, true));
  if (onPerm && tp.id == ScreenId::Perm) {
    req_ = RefreshKind::None;
    lastInputAt_ = now;
    toast(m_, "Troppo presto: tieni fino in fondo", now);
    redraw(RefreshKind::Partial);
    setAction("Rilasciato prima dei 3 s: nessuna risposta, la richiesta resta aperta");
    return mergeRefresh(r, finish(now));
  }
  return r;
}

RefreshKind Ui::padHoldFire(Ms now, const WallClock &wc) {
  UiState &u = m_.ui;
  if (!u.holdOn || !u.holdPad || u.holdDone) return RefreshKind::None;  // PRG premuto nel frattempo, o già scattato
  ScreenEntry &tp = top(m_);
  if (u.holdPerm || tp.id == ScreenId::Perm) {
    // §7: dal controller si risponde solo così, e solo alla richiesta su cui il tenuto è iniziato.
    const bool same = tp.id == ScreenId::Perm && holdLabel(m_, now) != nullptr;
    req_ = RefreshKind::None;
    lastInputAt_ = now;
    scopy(lastInput_, "Controller: A tenuto 3 s");
    u.holdDone = true;
    u.holdOk = same;
    if (!same) {
      setAction("Tenuto 3 s: la richiesta è cambiata, nessuna risposta");
      redraw(RefreshKind::Partial);  // via l'indicatore
      return finish(now);
    }
    confirmPerm(tp, now);
    redraw(RefreshKind::Partial);  // «FATTO · RILASCIA»
    return finish(now);
  }
  const RefreshKind r = gesture(Gesture::VeryLong, now, wc);  // come il PRG: Riposo, o conferma armata
  scopy(lastInput_, "Controller: A tenuto 3 s");
  return r;
}

}  // namespace cal
```

- [ ] **Step 4: Eseguire e vedere passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad_ui` → PASS (13 test). Poi `pio test -e native` intero (test_core, test_core_golden, test_ui invariati) → PASS.

- [ ] **Step 5: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/ui_pad.cpp firmware/src/core/ui.h firmware/src/core/ui.cpp firmware/src/core/model.h firmware/src/core/model.cpp firmware/test/test_pad_ui/test_main.cpp
git commit -m "Controller nei menu: come il tasto PRG; permessi solo con A tenuto 3 s"
```

---

### Task 12: Menu › Gioco (Controller, ROM, Alto o basso) e stato del controller

Dopo che la 0.10.2 è stata unita. Contratto: «Menu › Gioco» e «A ↔ B: agganci nell'interfaccia».

**Files:**
- Create: `firmware/src/core/gb_menu.h`, `firmware/src/core/gb_menu.cpp`
- Modify: `firmware/src/core/model.h` (`Settings::padOn`, campi `Sys::pad*`, `UiState::gbForgetUntil`, `MenuId::Gb/GbPad/GbRoms`, commento di `ScreenId::Game`), `firmware/src/core/model.cpp` (`defaultSettings`, `menuKey`), `firmware/src/core/menu.h` (`ActId::GbPair/GbForget`), `firmware/src/core/menu.cpp` (voce «Gioco», liste Gioco, Controller e ROM, «Alto o basso» tolto da Strumenti, `actKey`), `firmware/src/core/ui.h` (`PadReq`, `padLinkChanged`, `takePadReq`, `gbPlaying`), `firmware/src/core/ui.cpp` (`doAct`, `activate`), `firmware/src/core/ui_pad.cpp`
- Modify (allineamento del simulatore, annotato in DECISIONI al Task 13): `reference/calamaio-os-browser/assets/calamaio-test.js`, `reference/calamaio-os-browser/LEGGIMI.txt`
- Modify: `tools/golden/core-golden.js` (scenario e `menus()`), `firmware/test/test_core_golden/golden_core.h` (rigenerato), `firmware/test/test_core_golden/test_main.cpp` (`menus()`), `tools/sim-capture/scenarios.mjs`, `tools/sim-capture/README.md`, `tools/sim-capture/out/heltec213/` (ricatturato), `firmware/test/test_game/test_main.cpp`, e gli indici del Menu in `firmware/test/{test_core,test_net,test_m5_ui,test_power,test_remote,test_pet}/test_main.cpp`
- Test: `firmware/test/test_pad_ui/test_main.cpp`

**Interfaces:**
- Consumes: Task 7 (`pad::Link`), Task 11 (`Ui::padEvent`, `ui_pad.cpp`).
- Produces: `MenuId::Gb, GbPad, GbRoms`; `ActId::GbPair, GbForget`; `Settings::padOn`; `Sys::{padHw, padLink, padBonded, padHasBatt, padBatt}`; `UiState::gbForgetUntil`; `enum class PadReq : uint8_t { None, Pair, Forget }`; `RefreshKind Ui::padLinkChanged(pad::Link l, bool bonded, int batt, Ms now)`; `PadReq Ui::takePadReq()`; `bool Ui::gbPlaying() const` (qui `return false;`, il piano B ne sostituisce il corpo); **aggancio per il piano B**: `int cal::gbGameItems(const Model &m, Ms now, MenuItem *out, int cap)` in `core/gb_menu.h` (il piano A ritorna 0 e la lista «ROM» mostra «Nessuna ROM»; il piano B scrive le voci dei giochi, già azzerate e complete, e ritorna quante; si chiama sempre, anche col controller scollegato).

Scelte (decisioni dell'utente del 2026-10-12, da annotare in DECISIONI, Task 13): nel firmware non esiste un menu «Impostazioni»: la spec «Impostazioni › Giochi» diventa **Menu › Gioco**, dopo «Strumenti» e «La mascotte» e prima di «Wi-Fi» (vicino all'altra voce di gioco, a 2–3 pressioni dal Menu). La voce c'è **sempre**, anche nel simulatore e nei golden: dentro ci sono «Controller» e «ROM» solo sulla scheda vera (`sys.padHw`) e sempre «Alto o basso», il minigioco spostato da Menu › Strumenti (etichetta di 12 caratteri contro i 19 di «Gioco: alto o basso», che già stava nella riga: entra). L'elenco «ROM» è sempre visibile; scegliere un gioco senza controller collegato lo rifiuta il piano B col toast «Collega il controller per giocare». Niente interruttore del Bluetooth: `Settings::padOn` diventa vero con «Associa controller» e falso con «Dimentica controller».

- [ ] **Step 1: Test che falliscono** — aggiungere in `test_pad_ui/test_main.cpp`:

```cpp
static int indexOf(const MenuList &L, const char *label) {
  for (int i = 0; i < L.n; i++)
    if (!strcmp(L.items[i].label, label)) return i;
  return -1;
}

static void test_main_menu_gioco_always() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  const int g = indexOf(L, "Gioco");  // anche senza Bluetooth (simulatore, golden)
  TEST_ASSERT_EQUAL(indexOf(L, "Strumenti") + 1, g);
  TEST_ASSERT_EQUAL(g + 1, indexOf(L, "Wi-Fi"));
  TEST_ASSERT_EQUAL(ItemKind::Go, L.items[g].kind);
  TEST_ASSERT_EQUAL(MenuId::Gb, L.items[g].go);
  TEST_ASSERT_EQUAL_STRING("", L.items[g].val);
  TEST_ASSERT_EQUAL_STRING("Alto o basso e, col controller Bluetooth, i giochi del Game Boy.", L.items[g].help);
  TEST_ASSERT_EQUAL_STRING("Concentrazione, agenda, servizi e PR, Wi-Fi ospiti, ambiente, giornata di lavoro.",
                           L.items[indexOf(L, "Strumenti")].help);
  b.ui.model().sys.padHw = true;
  b.ui.model().sys.padLink = pad::Link::Ready;
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("collegato", L.items[g].val);
  TEST_ASSERT_FALSE(L.truncated);
}

// «Alto o basso» non sta più in Menu › Strumenti né al primo livello: è la voce fissa di Menu › Gioco.
static void test_alto_o_basso_moved_into_gioco() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Gioco: alto o basso"));
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Alto o basso"));
  menuList(MenuId::Tools, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Gioco: alto o basso"));
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Alto o basso"));
  menuList(MenuId::Gb, kHeltec213, b.ui.model(), b.now, b.wc(), L);  // padHw falso: come il simulatore
  TEST_ASSERT_EQUAL_STRING("Gioco", L.title);
  TEST_ASSERT_EQUAL(1, L.n);
  TEST_ASSERT_EQUAL_STRING("Alto o basso", L.items[0].label);
  TEST_ASSERT_EQUAL(ItemKind::Open, L.items[0].kind);
  TEST_ASSERT_EQUAL(ScreenId::Game, L.items[0].open);
  TEST_ASSERT_EQUAL_STRING("record 0", L.items[0].val);
  b.ui.model().sys.padHw = true;
  menuList(MenuId::Gb, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(3, L.n);
  TEST_ASSERT_EQUAL_STRING("Controller", L.items[0].label);
  TEST_ASSERT_EQUAL(MenuId::GbPad, L.items[0].go);
  TEST_ASSERT_EQUAL_STRING("nessuno associato", L.items[0].val);
  TEST_ASSERT_EQUAL_STRING("ROM", L.items[1].label);
  TEST_ASSERT_EQUAL(MenuId::GbRoms, L.items[1].go);
  TEST_ASSERT_EQUAL_STRING("Alto o basso", L.items[2].label);
  TEST_ASSERT_FALSE(L.truncated);
}

static void test_gb_pad_items() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::GbPad, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Controller", L.title);
  TEST_ASSERT_EQUAL(3, L.n);
  TEST_ASSERT_EQUAL_STRING("Stato", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("nessuno associato", L.items[0].val);
  TEST_ASSERT_EQUAL(ItemKind::Info, L.items[0].kind);
  TEST_ASSERT_EQUAL_STRING("Associa controller", L.items[1].label);
  TEST_ASSERT_EQUAL(ActId::GbPair, L.items[1].act);
  TEST_ASSERT_EQUAL_STRING("Dimentica controller", L.items[2].label);
  TEST_ASSERT_EQUAL(ActId::GbForget, L.items[2].act);
  TEST_ASSERT_FALSE(L.truncated);
  TEST_ASSERT_EQUAL_STRING("gb", menuKey(MenuId::Gb));
  TEST_ASSERT_EQUAL_STRING("gbpad", menuKey(MenuId::GbPad));
  TEST_ASSERT_EQUAL_STRING("gbroms", menuKey(MenuId::GbRoms));
  TEST_ASSERT_EQUAL_STRING("gbforget", actKey(ActId::GbForget));
}

// Piano A: l'aggancio non dà voci, la lista «ROM» dice che non ce ne sono (anche col controller collegato).
static void test_gb_roms_hook_empty() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::GbRoms, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("ROM", L.title);
  TEST_ASSERT_EQUAL(1, L.n);
  TEST_ASSERT_EQUAL_STRING("Nessuna ROM", L.items[0].label);
  b.ui.model().sys.padLink = pad::Link::Ready;
  menuList(MenuId::GbRoms, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(1, L.n);
  TEST_ASSERT_FALSE(L.truncated);
}

static void test_gb_status_values() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  static MenuList L;
  menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("nessuno associato", L.items[0].val);  // Bluetooth mai acceso
  m.set.padOn = true;
  struct { pad::Link l; bool bonded; bool hasBatt; uint8_t batt; const char *want; } cases[] = {
      {pad::Link::Off, false, false, 0, "nessuno associato"},
      {pad::Link::Off, true, false, 0, "lo cerco"},
      {pad::Link::Pairing, false, false, 0, "in associazione"},
      {pad::Link::Connecting, true, false, 0, "lo cerco"},
      {pad::Link::Ready, true, false, 0, "collegato"},
      {pad::Link::Ready, true, true, 80, "collegato · 80%"},
  };
  for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
    m.sys.padLink = cases[k].l;
    m.sys.padBonded = cases[k].bonded;
    m.sys.padHasBatt = cases[k].hasBatt;
    m.sys.padBatt = cases[k].batt;
    menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
    TEST_ASSERT_EQUAL_STRING(cases[k].want, L.items[0].val);
  }
  m.sys.padLink = pad::Link::Pairing;
  menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("in corso", L.items[1].val);
}

static void test_gb_pair_turns_bluetooth_on() {
  Bench b;
  b.boot();
  b.ui.model().sys.padHw = true;
  pushList(b, MenuId::GbPad);
  TEST_ASSERT_FALSE(b.ui.model().set.padOn);
  b.tap(pad::Down);  // Associa controller
  b.tap(pad::A);
  TEST_ASSERT_TRUE(b.ui.model().set.padOn);  // associare accende il Bluetooth
  TEST_ASSERT_EQUAL(PadReq::Pair, b.ui.takePadReq());
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());  // una volta sola
  TEST_ASSERT_EQUAL_STRING("Metti il controller in associazione", b.ui.model().toast.msg);
}

static void test_gb_forget_needs_confirm() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  pushList(b, MenuId::GbPad);
  top(m).sel = 2;  // Dimentica controller
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("Nessun controller associato", m.toast.msg);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
  m.set.padOn = true;
  m.sys.padBonded = true;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("Scegli ancora per confermare", m.toast.msg);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[2].val);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(PadReq::Forget, b.ui.takePadReq());
  TEST_ASSERT_FALSE(m.sys.padBonded);
  TEST_ASSERT_FALSE(m.set.padOn);  // dimenticare spegne il Bluetooth
  TEST_ASSERT_EQUAL_STRING("Controller dimenticato", m.toast.msg);
  m.sys.padBonded = true;  // finestra scaduta: si riarma
  b.tap(pad::A);
  b.now += kForgetMs + 1;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
  TEST_ASSERT_EQUAL_STRING("Scegli ancora per confermare", m.toast.msg);
}

static void test_pad_link_toasts() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.padLinkChanged(pad::Link::Ready, true, 80, b.now));
  TEST_ASSERT_EQUAL_STRING("Controller collegato", m.toast.msg);
  TEST_ASSERT_TRUE(m.sys.padBonded);
  TEST_ASSERT_TRUE(m.sys.padHasBatt);
  TEST_ASSERT_EQUAL_UINT8(80, m.sys.padBatt);
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.padLinkChanged(pad::Link::Ready, true, 80, b.now));  // niente di nuovo
  b.ui.padLinkChanged(pad::Link::Connecting, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Controller scollegato", m.toast.msg);
  TEST_ASSERT_FALSE(m.sys.padHasBatt);
  b.ui.padLinkChanged(pad::Link::Pairing, true, -1, b.now);
  b.ui.padLinkChanged(pad::Link::Connecting, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Associazione scaduta", m.toast.msg);
}

// Piano A: nessuna partita, la mappatura dei menu non si sospende mai.
static void test_gb_playing_stub() {
  Bench b;
  b.boot();
  TEST_ASSERT_FALSE(b.ui.gbPlaying());
}
```

e in `main()`:

```cpp
  RUN_TEST(test_main_menu_gioco_always);
  RUN_TEST(test_alto_o_basso_moved_into_gioco);
  RUN_TEST(test_gb_pad_items);
  RUN_TEST(test_gb_roms_hook_empty);
  RUN_TEST(test_gb_status_values);
  RUN_TEST(test_gb_pair_turns_bluetooth_on);
  RUN_TEST(test_gb_forget_needs_confirm);
  RUN_TEST(test_pad_link_toasts);
  RUN_TEST(test_gb_playing_stub);
```

Nello stesso file, in `test_game_up_down_like_prg` (Task 11), le due righe `pushList(p, MenuId::Tools);` e `pushList(c, MenuId::Tools);` diventano `pushList(p, MenuId::Gb);` e `pushList(c, MenuId::Gb);` (la voce del gioco si cerca con `findItem`, non per posizione).

`firmware/test/test_game/test_main.cpp`: in `Bench::openGame()` il percorso diventa Menu › Gioco › Alto o basso:

```cpp
  void openGame() {
    act(Action::Menu);
    act(Action::Next);
    act(Action::Next);
    act(Action::Ok);  // Gioco
    act(Action::Ok);  // Alto o basso
  }
```

e alla fine di `test_seed_matches_simulator` (dopo l'uscita si torna a Menu › Gioco, voce 0):

```cpp
  TEST_ASSERT_EQUAL_STRING("Alto o basso", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("record 3", L.items[0].val);
```

Test esistenti che contano le voci del Menu (con «Gioco» in posizione 2 tutto quello che sta dopo «Strumenti» scende di uno; elenco verificato il 2026-10-12 con `grep -n -A3 "Action::Menu" firmware/test/*/test_main.cpp`):
- `test_core/test_main.cpp`, `test_menu_and_back`: `TEST_ASSERT_EQUAL(13, L.n)`, «Sicurezza» in `L.items[6]`, «Stato» in `L.items[10]` (commento: «0.11.0: Gioco dopo Strumenti»);
- `test_net/test_main.cpp`, `openWifiMenu()`: `next(3)`;
- `test_m5_ui/test_main.cpp`: i due `for (int k = 0; k < 5; k++) b.act(Action::Next);` verso Sicurezza diventano `k < 6`;
- `test_power/test_main.cpp`, `openBattery()`: `k < 9`;
- `test_remote/test_main.cpp`: il primo ciclo verso Display diventa `k < 4`;
- `test_pet/test_main.cpp` (mascotte accesa): `TEST_ASSERT_EQUAL(14, L.n)` e «Info» in `L.items[13]` («La mascotte» resta la voce 2).
`test_workday` (Strumenti › Giornata di lavoro, voce 5) non cambia: «Alto o basso» era l'ultima voce di Strumenti.

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad_ui`
Expected: FAIL, «'Gb' is not a member of 'cal::MenuId'».

- [ ] **Step 3: Modello**

`firmware/src/core/model.h`:
- agli include: `#include "pad.h"`;
- `enum class ScreenId`: il commento di `Game` diventa `// 0.9.0: Menu › Gioco › Alto o basso (fino alla 0.10.x in Menu › Strumenti)`;
- `enum class MenuId`: aggiungere `Gb, GbPad, GbRoms` subito prima di `Count`, e al commento sopra: «Gb, GbPad, GbRoms (0.11.0): Menu › Gioco, › Controller, › ROM.»;
- in `struct Settings` dopo `bool game;`:

```cpp
  // 0.11.0: Bluetooth del controller acceso: vero da «Associa controller» a «Dimentica controller», falso di default.
  // NVS «calamaio-gb/pad», non remota.
  bool padOn;
```

- in `struct Sys` dopo `bool wallOk;`:

```cpp
  // 0.11.0: controller Bluetooth (hw/blepad). padHw = la scheda ha il Bluetooth (solo sulla scheda vera: nel
  // simulatore e nei test golden Menu › Gioco ha solo «Alto o basso»).
  bool padHw;
  pad::Link padLink;
  bool padBonded;
  bool padHasBatt;
  uint8_t padBatt;  // 0–100 con padHasBatt
```

- in `struct UiState` dopo `holdRid`: `Ms gbForgetUntil;  // Menu › Gioco › Controller › «Dimentica controller» armata fino a`.

`firmware/src/core/model.cpp`: in `defaultSettings()` aggiungere `st.padOn = false;`; in `menuKey()` aggiungere `"gb", "gbpad", "gbroms"` in fondo all'array (stesso ordine dell'enum).

`firmware/src/core/menu.h`: in `enum class ActId` aggiungere in fondo `GbPair, GbForget,  // 0.11.0: Menu › Gioco › Controller`; in `menu.cpp` `actKey()` aggiungere `"gbpair", "gbforget"` in fondo all'array.

- [ ] **Step 4: Aggancio per il piano B**

`firmware/src/core/gb_menu.h`:

```cpp
// Aggancio di Menu › Gioco › ROM per il piano B (giochi Game Boy): le voci dei giochi del PC e del gioco già sulla
// scheda, sempre (anche col controller scollegato: la scelta senza controller la rifiuta il piano B).
// Scrive al massimo cap voci complete (MenuItem azzerata e riempita) e ritorna quante.
#pragma once
#include "menu.h"

namespace cal {

int gbGameItems(const Model &m, Ms now, MenuItem *out, int cap);

}  // namespace cal
```

`firmware/src/core/gb_menu.cpp`:

```cpp
#include "gb_menu.h"

namespace cal {

// Piano A: nessun gioco. Il piano B sostituisce il corpo.
int gbGameItems(const Model &, Ms, MenuItem *, int) { return 0; }

}  // namespace cal
```

- [ ] **Step 5: Menu**

`firmware/src/core/menu.cpp`: agli include `#include "gb_menu.h"`; nel namespace anonimo, dopo `upper()`:

```cpp
// Stato del controller per Menu › Gioco › Controller.
const char *padStatus(const Model &m, char *v, size_t cap) {
  const Sys &w = m.sys;
  if (!m.set.padOn) return "nessuno associato";  // Bluetooth spento: si accende con «Associa controller»
  switch (w.padLink) {
    case pad::Link::Pairing: return "in associazione";
    case pad::Link::Connecting: return "lo cerco";
    case pad::Link::Ready:
      if (!w.padHasBatt) return "collegato";
      Sb(v, cap).s("collegato · ").i(w.padBatt).s("%");
      return v;
    default: return w.padBonded ? "lo cerco" : "nessuno associato";
  }
}
```

Nel blocco `if (id == MenuId::Tools) { … }` togliere le due righe finali

```cpp
    Sb(v, sizeof v).s("record ").i(m.game.best);
    B.open("Gioco: alto o basso", v, ScreenId::Game, "Indovina se la prossima carta sarà più alta o più bassa: 1× più alta, 2× più bassa.");
```

(il `return;` resta). In `menuList()`, subito prima di `out.title = "Menu";`:

```cpp
  if (id == MenuId::Gb) {  // 0.11.0: Menu › Gioco (nel simulatore solo «Alto o basso»)
    out.title = "Gioco";
    if (w.padHw) {  // solo sulla scheda vera
      B.go("Controller", padStatus(m, v, sizeof v), MenuId::GbPad,
           "Associa il controller Xbox, guardane lo stato o dimenticalo. Un solo controller alla volta.");
      B.go("ROM", "", MenuId::GbRoms, "I giochi del Game Boy caricati sul PC (pagina Giochi della webapp) e quello già sulla scheda.");
    }
    Sb(v, sizeof v).s("record ").i(m.game.best);
    B.open("Alto o basso", v, ScreenId::Game, "Indovina se la prossima carta sarà più alta o più bassa: 1× più alta, 2× più bassa.");
    return;
  }
  if (id == MenuId::GbPad) {  // 0.11.0: Menu › Gioco › Controller
    out.title = "Controller";
    B.info("Stato", padStatus(m, v, sizeof v),
           "Croce su e giù: sposta. A: scegli. B: indietro. Tasto Menu: apre il Menu. Ai permessi rispondi tenendo premuto A per 3 secondi.");
    B.act("Associa controller", w.padLink == pad::Link::Pairing ? "in corso" : "", ActId::GbPair,
          "Tieni premuto il tasto di associazione del controller finché il logo Xbox lampeggia veloce. Un solo controller: il nuovo sostituisce il vecchio.");
    B.act("Dimentica controller", m.ui.gbForgetUntil > now ? "conferma?" : "", ActId::GbForget,
          "Cancella l'associazione e spegne il Bluetooth. Per usarlo di nuovo serve «Associa controller».");
    return;
  }
  if (id == MenuId::GbRoms) {  // 0.11.0: Menu › Gioco › ROM; le voci le scrive il piano B (core/gb_menu)
    out.title = "ROM";
    const int k = gbGameItems(m, now, out.items, kMaxItems);
    out.n = k < 0 ? 0 : (k > kMaxItems ? kMaxItems : k);
    if (out.n == 0)
      B.info("Nessuna ROM", "", "Le ROM si caricano sul PC dalla webapp (pagina Giochi) e arrivano qui col PC collegato.");
    return;
  }
```

Nella lista principale la voce «Strumenti» cambia aiuto (il gioco non c'è più) e, subito dopo il blocco `if (m.set.game) { … }` della mascotte, arriva «Gioco»:

```cpp
  B.go("Strumenti", "", MenuId::Tools, "Concentrazione, agenda, servizi e PR, Wi-Fi ospiti, ambiente, giornata di lavoro.");
```

```cpp
  B.go("Gioco", w.padHw && w.padLink == pad::Link::Ready ? "collegato" : "", MenuId::Gb,
       "Alto o basso e, col controller Bluetooth, i giochi del Game Boy.");  // 0.11.0: sempre, anche nel simulatore
```

- [ ] **Step 6: Interfaccia**

`firmware/src/core/ui.h`, prima di `class Ui`:

```cpp
// 0.11.0: richieste di Menu › Gioco › Controller al Bluetooth (hw/blepad), lette una volta dal loop.
enum class PadReq : uint8_t { None, Pair, Forget };
```

nella parte pubblica, sotto `padEvent`:

```cpp
  // Stato del collegamento (hw/blepad): toast «Controller collegato», «Controller scollegato», «Associazione scaduta»;
  // ridisegno se cambia qualcosa che si vede. batt −1 = sconosciuta.
  RefreshKind padLinkChanged(pad::Link l, bool bonded, int batt, Ms now);
  // Associa o Dimentica scelti in Menu › Gioco › Controller: il loop li passa a hw/blepad. Si legge una volta.
  PadReq takePadReq() { const PadReq r = padReq_; padReq_ = PadReq::None; return r; }
  // Partita del Game Boy in corso col pannello al driver veloce: il loop sospende la mappatura dei menu del
  // controller (contratto). Piano A: mai; il piano B sostituisce il corpo.
  bool gbPlaying() const { return false; }
```

nella parte privata: `void gbAct(ActId id, Ms now, char *msg, size_t cap);` e il campo `PadReq padReq_ = PadReq::None;  // 0.11.0`.

`firmware/src/core/ui.cpp`, in `Ui::doAct` prima di `default:`:

```cpp
    case ActId::GbPair:
    case ActId::GbForget:
      gbAct(id, now, msg, sizeof msg);  // 0.11.0: core/ui_pad.cpp
      break;
```

e in `Ui::activate`, insieme alle altre righe che disarmano le conferme:

```cpp
  if (!(it.kind == ItemKind::Act && it.act == ActId::GbForget)) m_.ui.gbForgetUntil = 0;
```

`firmware/src/core/ui_pad.cpp`, in fondo al namespace:

```cpp
RefreshKind Ui::padLinkChanged(pad::Link l, bool bonded, int batt, Ms now) {
  req_ = RefreshKind::None;
  Sys &w = m_.sys;
  const pad::Link was = w.padLink;
  const bool hasBatt = batt >= 0 && batt <= 100;
  const uint8_t b = hasBatt ? static_cast<uint8_t>(batt) : 0;
  if (l == was && bonded == w.padBonded && hasBatt == w.padHasBatt && b == w.padBatt) return finish(now);
  w.padLink = l;
  w.padBonded = bonded;
  w.padHasBatt = hasBatt;
  w.padBatt = b;
  if (l != was) {
    if (l == pad::Link::Ready) toast(m_, "Controller collegato", now);
    else if (was == pad::Link::Ready) toast(m_, "Controller scollegato", now);
    else if (was == pad::Link::Pairing) toast(m_, "Associazione scaduta", now);
  }
  redraw(RefreshKind::Partial);
  return finish(now);
}

void Ui::gbAct(ActId id, Ms now, char *msg, size_t cap) {
  Settings &st = m_.set;
  Sb b(msg, cap);
  if (id == ActId::GbPair) {
    st.padOn = true;  // associare accende il Bluetooth (resta acceso fino a «Dimentica controller»)
    padReq_ = PadReq::Pair;
    toast(m_, "Metti il controller in associazione", now);
    b.s("Associa controller: tieni premuto il tasto di associazione finché il logo lampeggia veloce");
    return;
  }
  // GbForget: come «Dimentica rete», seconda scelta entro kForgetMs.
  if (!m_.sys.padBonded) {
    toast(m_, "Nessun controller associato", now);
    b.s("Dimentica controller: nessun controller associato");
    return;
  }
  if (!(m_.ui.gbForgetUntil > now)) {
    m_.ui.gbForgetUntil = now + kForgetMs;
    toast(m_, "Scegli ancora per confermare", now);
    b.s("Dimentica controller: scegli ancora per confermare");
    return;
  }
  m_.ui.gbForgetUntil = 0;
  padReq_ = PadReq::Forget;
  m_.sys.padBonded = false;
  st.padOn = false;  // dimenticare spegne il Bluetooth
  toast(m_, "Controller dimenticato", now);
  b.s("Controller dimenticato: per usarlo di nuovo, Associa controller");
}
```

- [ ] **Step 7: Allineamento del simulatore, golden e catture**

Il simulatore (`reference/`) è in sola lettura salvo allineamenti annotati in DECISIONI: questo lo è (Task 13, Step 5). In `reference/calamaio-os-browser/assets/calamaio-test.js`, in `menuList()`:
1. nel blocco `if (id === 'tools') { … }` togliere la voce `{ label: 'Gioco: alto o basso', … }` (e la virgola dopo «Giornata di lavoro»);
2. subito dopo quel blocco aggiungere

```js
      // Menu › Gioco (firmware 0.11.0): «Alto o basso», spostato da Strumenti. Controller e ROM solo sulla scheda vera.
      if (id === 'gb') {
        return { title: 'Gioco', items: [
          { label: 'Alto o basso', val: 'record ' + m.game.best, open: 'game', help: 'Indovina se la prossima carta sarà più alta o più bassa: 1× più alta, 2× più bassa.' }
        ] };
      }
```

3. nella lista principale l'aiuto di «Strumenti» diventa `'Concentrazione, agenda, servizi e PR, Wi-Fi ospiti, ambiente, giornata di lavoro.'` e, tra l'array della mascotte e quello che comincia con «Wi-Fi», si aggiunge `[{ label: 'Gioco', go: 'gb', help: 'Alto o basso e, col controller Bluetooth, i giochi del Game Boy.' }],` (la voce del simulatore non ha valore: il firmware scrive «collegato» solo con `padHw`).

`reference/calamaio-os-browser/LEGGIMI.txt`: «Menu > Strumenti > Gioco: alto o basso» diventa «Menu > Gioco > Alto o basso».

Golden (`tools/golden/core-golden.js` e `firmware/test/test_core_golden/test_main.cpp`):
1. in `menus()` di tutti e due si aggiunge la lista nuova in fondo all'elenco: `'gb'` dopo `'work'` nel JS, `MenuId::Gb` dopo `MenuId::Work` nel C++;
2. nello scenario di `core-golden.js` ogni passo che dal Menu scende a una voce dopo «Strumenti» (cioè `G('3x'); G('1x', k)` con k ≥ 2, k ≥ 3 quando la mascotte è accesa) prende un `'1x'` in più, e il percorso verso il gioco passa da Menu › Gioco (voce 2, poi `G('2x')` su «Alto o basso»);
3. rigenerare: `node tools/golden/core-golden.js`. Verifica di copertura: prima di rigenerare copiare il vecchio `golden_core.h` in `/tmp/golden_old.h`; `diff <(grep -o 'stack=[^"]*' /tmp/golden_old.h) <(grep -o 'stack=[^"]*' firmware/test/test_core_golden/golden_core.h)` deve mostrare solo righe con `menu=gb` al posto di `menu=tools` sul percorso del gioco: ogni altro passo arriva alla stessa schermata di prima.

Catture (`tools/sim-capture`):
1. `scenarios.mjs`: `TO_DISPLAY` diventa `['3x', '1x', '1x', '1x', '1x', '2x']` (Display è la voce 4), `TO_STATO` prende un `'1x'` in più (Stato è la voce 10), ogni `menuItem(k)` con k ≥ 2 diventa `menuItem(k + 1)` (k ≥ 3 → k + 1 negli scenari con `sim:game`, dove «La mascotte» è la voce 2); gli scenari `gioco-*` di «Alto o basso» passano da `...menuItem(1), ...times(6, '1x'), '2x'` a `...menuItem(2), '2x'`; scenario nuovo `{ name: 'menu-gioco', steps: [BOOT, ...menuItem(2)], note: 'Menu › Gioco: solo «Alto o basso» (il simulatore non ha il controller Bluetooth).' }`; le note che contano le voci di Strumenti («7 voci») si correggono (6);
2. `README.md`: «Menu › Strumenti › Gioco (voce 7)» diventa «Menu › Gioco › Alto o basso»;
3. prima di ricatturare copiare `out/heltec213/manifest.json` in `/tmp/manifest_old.json`; poi `cd tools/sim-capture && node capture.mjs` (circa 25 minuti, misurato il 2026-10-11) e `node verify-determinism.mjs`;
4. verifica: per ogni cattura con lo stesso nome nei due manifest, l'ultima voce di `stack` (id e menu) è la stessa, tranne la lista dei passi verso «Alto o basso» che ora attraversa `gb`; `menu-gioco` è nuova. Una differenza diversa vuol dire un indice sbagliato in `scenarios.mjs`.

- [ ] **Step 8: Eseguire e vedere passare**

Run: `cd ~/CalamaiOS/firmware && pio test -e native -f test_pad_ui` → PASS (22 test). Poi `pio test -e native` intero → PASS, in particolare `test_core_golden` (golden rigenerato), `test_ui` (catture nuove), `test_game` (percorso nuovo).

- [ ] **Step 9: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/core/gb_menu.h firmware/src/core/gb_menu.cpp firmware/src/core/model.h firmware/src/core/model.cpp firmware/src/core/menu.h firmware/src/core/menu.cpp firmware/src/core/ui.h firmware/src/core/ui.cpp firmware/src/core/ui_pad.cpp firmware/test/test_pad_ui/test_main.cpp firmware/test/test_game/test_main.cpp firmware/test/test_core/test_main.cpp firmware/test/test_net/test_main.cpp firmware/test/test_m5_ui/test_main.cpp firmware/test/test_power/test_main.cpp firmware/test/test_remote/test_main.cpp firmware/test/test_pet/test_main.cpp firmware/test/test_core_golden reference/calamaio-os-browser/assets/calamaio-test.js reference/calamaio-os-browser/LEGGIMI.txt tools/golden/core-golden.js tools/sim-capture/scenarios.mjs tools/sim-capture/README.md tools/sim-capture/out/heltec213
git commit -m "Menu › Gioco: Controller, ROM e Alto o basso (spostato da Strumenti), simulatore e catture allineati"
```

---

### Task 13: Collegamento al loop — NVS, radio, energia, sonno, ripristino (firmware 0.11.0)

Dopo che la 0.10.2 è stata unita.

**Files:**
- Modify: `firmware/src/main.cpp`, `firmware/src/net/net.h`, `firmware/src/net/net.cpp`, `firmware/src/core/model.h` (versione), `firmware/test/test_net/test_main.cpp` (versione), `docs/DECISIONI.md`

**Interfaces:**
- Consumes: Task 9 (`pad::radioPlan`, `wifiPowerSave`), Task 10 (`blepad::*`), Task 11–12 (`Ui::padEvent`, `padLinkChanged`, `takePadReq`, `gbPlaying`, `Settings::padOn`, `Sys::padHw`, `Sys::padBonded`).
- Produces: `int8_t net::appliedPowerSave()` (−1 = mai impostato, cioè il predefinito dell'IDF: minimo); firmware 0.11.0 col controller.

Nessun test nativo nuovo oltre alla versione (codice di collegamento); la verifica è build + test nativi, poi il Task 14.

- [ ] **Step 1: `net::appliedPowerSave()`**

`firmware/src/net/net.h`, sotto `void setPowerSave(PowerSave ps);`:

```cpp
// Risparmio applicato davvero dal task di rete (wifi_ps_type_t), −1 = mai impostato (predefinito dell'IDF: minimo).
int8_t appliedPowerSave();
```

`firmware/src/net/net.cpp`, accanto a `setPowerSave`:

```cpp
int8_t appliedPowerSave() { return gPs; }
```

- [ ] **Step 2: `firmware/src/main.cpp`**

1. Include, dopo il blocco `#ifndef CALAMAIO_NO_NET … #endif`:

```cpp
#ifndef CALAMAIO_NO_PAD
#include <esp_heap_caps.h>
#include "core/pad.h"
#include "hw/blepad.h"
#define CALAMAIO_PAD 1
#else
#define CALAMAIO_PAD 0
#endif
```

2. In `applyPower()`, sostituire la riga `net::setPowerSave(...)` con:

```cpp
  // 0.11.0: col Bluetooth voluto mai «nessun risparmio» (coesistenza, core/power).
  net::setPowerSave(static_cast<net::PowerSave>(wifiPowerSave(calibMax(m), idle, CALAMAIO_PAD && m.set.padOn)));
```

3. Lo stato del controller serve anche a `runSecurityOps()` e ai fronti del PRG, definiti prima di `powerLoop()`: queste variabili vanno **subito prima di `static void runSecurityOps(`**:

```cpp
// ---------- controller Bluetooth (0.11.0: core/pad, hw/blepad), stato ----------
#if CALAMAIO_PAD
static Preferences gbPrefs;
static bool padSaved = false;   // impostazione nella NVS
static bool padPsWant = false;  // impostazione già passata ad applyPower()
static pad::MenuMapper padMenu;
static pad::Link padSeen = pad::Link::Off;
static int padBattSeen = -1;
static bool padPairWanted = false;
static uint32_t padSearchFrom = 0;
static uint32_t padHeapAt = 0;
#endif
```

e subito dopo `powerLoop()` le funzioni:

```cpp
// ---------- controller Bluetooth (0.11.0: core/pad, hw/blepad) ----------
// Bluetooth acceso solo con Settings::padOn («calamaio-gb/pad»: da «Associa controller» a «Dimentica controller»); mai nel sonno profondo né nei risvegli
// dal timer. Prima il risparmio del Wi-Fi, poi la radio (coesistenza).
#if CALAMAIO_PAD
static void heapLine(const char *when) {
  LogSerial.printf("[memoria] %s: heap libero %u, minimo %u, blocco max %u\n", when, (unsigned)ESP.getFreeHeap(),
                   (unsigned)ESP.getMinFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void padLoad() {
  gbPrefs.begin("calamaio-gb", false);
  padSaved = gbPrefs.getUChar("pad", 0) == 1;
  Model &m = gUi.model();
  m.set.padOn = padSaved;
  m.sys.padHw = true;
  m.sys.padBonded = blepad::bonded();
  LogSerial.printf("[pad] controller %s, %s\n", padSaved ? "acceso" : "spento",
                   m.sys.padBonded ? "associato" : "nessuno associato");
}

static void padHandle(const pad::MenuEvent *ev, int n, Ms now) {
  for (int i = 0; i < n; i++) {
    if (ev[i] == pad::MenuEvent::HoldDown && gestures.pressed()) continue;  // tasto PRG giù: il tenuto è suo
    power.key(now);
    counter.input(now);  // 0.10.2: come un tasto
    keySinceBoot = true;
    padSearchFrom = millis();
    const RefreshKind r = gUi.padEvent(ev[i], now, wallAt(now));
    LogSerial.printf("[pad] %s -> %s\n", pad::menuEventName(ev[i]), gUi.lastAction());
    pending = mergeRefresh(pending, r);
  }
}

static void padLoop(Ms now) {
  Model &m = gUi.model();
  const uint32_t ms = millis();
  switch (gUi.takePadReq()) {
    case PadReq::Pair: padPairWanted = true; padSearchFrom = ms; break;
    case PadReq::Forget: blepad::forget(); LogSerial.println("[pad] controller dimenticato"); break;
    default: break;
  }
  if (m.set.padOn != padSaved) {
    padSaved = m.set.padOn;
    gbPrefs.putUChar("pad", padSaved ? 1 : 0);
    LogSerial.printf("[nvs] controller %s (calamaio-gb/pad)\n", padSaved ? "acceso" : "spento");
    padSearchFrom = ms;
  }
  if (m.set.padOn != padPsWant) {
    padPsWant = m.set.padOn;
    applyPower(power.state(), now);
  }
  pad::RadioIn in;
  in.setting = m.set.padOn;
  in.asleep = timerWake || power.state() == PowerState::Sleep;
#if CALAMAIO_NET
  in.wifiPsOk = net::appliedPowerSave() != 0;
#else
  in.wifiPsOk = true;
#endif
  in.usb = usbOn();
  in.idle = power.state() == PowerState::Idle;
  in.link = blepad::link();
  in.now = ms;
  in.searchFrom = padSearchFrom;
  const pad::RadioOut out = pad::radioPlan(in);
  if (out.radio && !blepad::active()) {
    heapLine("prima del Bluetooth");
    blepad::begin();
    heapLine("Bluetooth acceso");
    padSearchFrom = padHeapAt = ms;
  } else if (!out.radio && blepad::active()) {
    blepad::end();
    heapLine("Bluetooth spento");
  }
  if (blepad::active()) {
    blepad::setScan(out.scan);
    if (padPairWanted) {
      padPairWanted = false;
      blepad::startPairing();
      LogSerial.println("[pad] associazione: 2 minuti per il tasto di associazione del controller");
    }
    if (ms - padHeapAt >= 600000) {
      padHeapAt = ms;
      heapLine("Bluetooth acceso (ogni 10 min)");
    }
  }
  const pad::Link l = blepad::link();
  const int batt = blepad::battery();
  if (l != padSeen || batt != padBattSeen) {
    if (padSeen == pad::Link::Ready && l != pad::Link::Ready) {  // scollegato: una tenuta di A si annulla
      pad::MenuEvent ev[4];
      padHandle(ev, padMenu.reset(ev, 4), now);
      padSearchFrom = ms;
    }
    if (l != padSeen) LogSerial.printf("[pad] %s -> %s\n", pad::linkKey(padSeen), pad::linkKey(l));
    padSeen = l;
    padBattSeen = batt;
    pending = mergeRefresh(pending, gUi.padLinkChanged(l, blepad::bonded(), batt, now));
  }
  pad::MenuEvent ev[8];
  if (gUi.gbPlaying()) {  // partita del Game Boy (piano B): blepad::poll lo legge solo il gioco, menu sospesi
    padMenu.reset(ev, 8);  // un'eventuale tenuta di A si annulla; gli eventi si scartano
    return;
  }
  pad::State s;
  while (blepad::poll(s)) padHandle(ev, padMenu.update(s, blepad::polledAt(), ev, 8), now);
  padHandle(ev, padMenu.tick(millis(), ev, 8), now);
}
#endif
```

4. In `loop()`, nel ramo `if (e.down) {` dei fronti del PRG, dopo `keySinceBoot = true;`:

```cpp
#if CALAMAIO_PAD
      padSearchFrom = millis();  // un tasto: il controller si cerca di nuovo per 2 minuti
#endif
```

5. In `loop()`, subito dopo `workSync();` (prima del blocco `if (now >= nextTick)`):

```cpp
#if CALAMAIO_PAD
  padLoop(now);
#endif
```

6. In `setup()`, subito dopo il blocco `#if CALAMAIO_NET … petBegin(); … #endif` (dopo `goLive()`, che azzera `sys`, e dopo la pulizia profonda della NVS):

```cpp
#if CALAMAIO_PAD
  padLoad();  // 0.11.0: impostazione e abbinamento; la radio la accende padLoop()
#endif
```

7. All'inizio di `deepSleep()`:

```cpp
#if CALAMAIO_PAD
  if (blepad::active()) {
    blepad::end();
    LogSerial.println("[pad] Bluetooth spento per il sonno profondo");
  }
#endif
```

8. In `runSecurityOps()`, nel ramo `wantFactory`, dopo `gamePrefs.clear();`:

```cpp
#if CALAMAIO_PAD
    blepad::forget();  // 0.11.0: abbinamento del controller (NVS nimble_bond)
    if (blepad::active()) blepad::end();
    gbPrefs.clear();
    gUi.model().set.padOn = false;
    padSaved = padPsWant = false;
#endif
```

- [ ] **Step 3: Versione**

`firmware/src/core/model.h`: `#define CALAMAIO_FW_VERSION "0.11.0"`.
`firmware/test/test_net/test_main.cpp`, la riga del controllo della versione:

```cpp
  TEST_ASSERT_EQUAL_STRING("0.11.0", kFwVersion);  // controller Bluetooth nei menu, driver veloce del pannello
```

- [ ] **Step 4: Build e test**

Run:
```bash
cd ~/CalamaiOS/firmware
pio test -e native
pio run -e heltec213
pio run -e fastink-demo
```
Expected: test PASS; build SUCCESS. Annotare Flash e RAM statica di `heltec213` e confrontarli con quelli del Task 4 (atteso: circa +200 KB di flash e +7 KB di RAM statica per NimBLE, come nello spike). L'immagine deve restare sotto i 2,5 MB (spazio app del piano B): se la supera, fermarsi e dirlo al coordinatore.

- [ ] **Step 5: DECISIONI**

In `docs/DECISIONI.md`, voce nuova «Giochi, piano A (firmware 0.11.0)»:
- Menu › Gioco al posto di «Impostazioni › Giochi» (nel firmware non c'è un menu Impostazioni; decisioni dell'utente del 2026-10-12): voce sempre presente, dopo «Strumenti» e «La mascotte», prima di «Wi-Fi»; dentro «Controller» (stato, Associa, Dimentica) e «ROM» (elenco dei giochi del piano B, sempre visibile) solo sulla scheda vera (`sys.padHw`), e «Alto o basso», spostato da Menu › Strumenti. **Allineamento del simulatore** (`reference/calamaio-os-browser/assets/calamaio-test.js`, `LEGGIMI.txt`): lista `gb` «Gioco» con «Alto o basso», voce tolta da Strumenti (aiuto di Strumenti senza «gioco»), voce «Gioco» nel Menu; golden e catture rigenerati, scenari con gli indici del Menu spostati di uno dopo «Strumenti».
- Niente interruttore del Bluetooth: si accende con «Associa controller» e si spegne con «Dimentica controller».
- Driver veloce solo durante la partita (piano B): l'interfaccia di Calamaio resta su heltec-eink-modules con la regola dei refresh della 0.10.2.
- Mappatura del controller: croce su/giù, A, B, tasto Menu (apre il Menu, come 3×); nel gioco «Alto o basso» su = alta, giù = bassa. A dà «scegli» al rilascio (prima dei 0,7 s), non alla pressione come nello spike: così un tenuto non è mai anche una scelta.
- Permessi dal controller solo con A tenuto 3 s (indicatore «TIENI PREMUTO · CONSENTI/NEGA»), valido solo per la richiesta su cui il tenuto è iniziato; A premuto: toast «Tieni premuto A per 3 s»; rilasciato prima: «Troppo presto: tieni fino in fondo» e la richiesta resta aperta.
- Bluetooth solo con un controller associato (o in associazione); ricerca continua per 2 minuti, poi in Attesa a batteria 3 s ogni 30 s; spento nel sonno profondo e nei risvegli dal timer; il Wi-Fi non va mai in «nessun risparmio» col Bluetooth (anche nella fase 1 della calibrazione: minimo).
- Impostazione in `calamaio-gb/pad` (non nel blocco da 16 byte di `calamaio-set`: un ritorno alla 0.10.x lo rifiuterebbe); abbinamento in `nimble_bond`; il Ripristino di fabbrica li cancella entrambi.
- Firmware 0.11.0.

- [ ] **Step 6: Commit**

```bash
cd ~/CalamaiOS
git add firmware/src/main.cpp firmware/src/net/net.h firmware/src/net/net.cpp firmware/src/core/model.h firmware/test/test_net/test_main.cpp docs/DECISIONI.md
git commit -m "Firmware 0.11.0: controller Bluetooth nei menu e nei permessi, Bluetooth solo quando serve"
```

---

### Task 14: Prova dal vivo del controller con l'utente (associazione, menu, permessi, Wi-Fi, memoria, sonno)

**Files:**
- Modify: `docs/HARDWARE.md` (sezione «Controller Bluetooth (firmware 0.11.0)»), `docs/DECISIONI.md`

**Interfaces:**
- Consumes: Task 13 (firmware 0.11.0), righe di registro `[pad]`, `[memoria]`, `[wifi] risparmio`, `[energia]`.
- Produces: misure di heap, tempi di ricollegamento, esito delle prove §7 col controller.

- [ ] **Step 1 (coordinatore): build, richiesta, backup, flash**

```bash
cd ~/CalamaiOS/firmware && pio test -e native && pio run -e heltec213
```
Chiedere all'utente: «Carico Calamaio 0.11.0 (controller Xbox nei menu) col cavo. Reti, abbinamento col PC e avatar restano. Prima salvo una copia della memoria delle impostazioni. Posso?». Solo con un sì:

```bash
cd ~/CalamaiOS
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  read_flash 0x9000 0x7000 backup-fabbrica/nvs-otadata-$(date +%Y%m%d-%H%M)-pre-0.11.0.bin
chmod 600 backup-fabbrica/nvs-otadata-*-pre-0.11.0.bin
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 ~/CalamaiOS/firmware/.pio/build/heltec213/firmware.bin
pio device monitor -b 115200 -p /dev/cu.usbserial-0001
```

- [ ] **Step 2: Avvio e accensione del controller**

Atteso in seriale: `Calamaio OS 0.11.0`, `[pad] controller spento, nessuno associato`, riga `[memoria]` dell'avvio. L'utente apre Menu (3× PRG) e vede «Gioco» tra «Strumenti» e «Wi-Fi»; entra: «Controller · nessuno associato», «ROM», «Alto o basso»; in «ROM»: «Nessuna ROM».
L'utente apre «Controller»: Stato «nessuno associato», Bluetooth ancora spento (nessuna riga `[memoria] … Bluetooth`): si accende solo con «Associa controller».

- [ ] **Step 3: Associazione**

L'utente accende il 1914 col tasto Xbox e tiene premuto il tasto di associazione finché il logo lampeggia veloce, poi sceglie «Associa controller». Atteso: `[nvs] controller acceso`, eventuale `[wifi] risparmio: modem sleep minimo`, `[memoria] prima del Bluetooth` e `[memoria] Bluetooth acceso` (nessun `ERRORE esp_wifi_set_ps`, nessun riavvio), toast «Metti il controller in associazione», Stato «in associazione», poi entro ~10 s `[pad] PRONTO`, toast «Controller collegato», Stato «collegato · NN%». Se compare `[pad] report map corto: ricollego subito` va bene (caso visto nello spike): deve seguire `PRONTO`. Annotare il tempo.

- [ ] **Step 4: Menu col controller**

Dalla Home: tasto Menu → Menu; croce giù/su → la selezione si sposta; A → apre; B → indietro; B dalla Home → «Sei già nella Home». Confrontare con PRG (1×, 2×, lunga): stesso comportamento. Nel gioco «Alto o basso» (Strumenti): su = alta, giù = bassa.

- [ ] **Step 5: Permessi (§7) col controller**

L'utente avvia in una sessione di prova di Claude Code un comando che chiede il permesso (con Calamaio collegato). Sul display: Dettaglio → A → schermata Permesso (NEGA evidenziato).
1. A premuto una volta: toast «Tieni premuto A per 3 s»; nel terminale la domanda resta.
2. Croce giù → CONSENTI; A tenuto ~2 s e rilasciato: riquadro «TIENI PREMUTO · CONSENTI», poi «Troppo presto: tieni fino in fondo»; nel terminale ancora in attesa.
3. A tenuto 3 s: «FATTO · RILASCIA», toast «Consentito · inviato a Claude Code», il comando parte.
4. Nuova richiesta: A tenuto 3 s senza toccare la croce → negato.
5. Nuova richiesta: B → «Annullato · resta in attesa»; si riapre dal Dettaglio.
6. Nuova richiesta: A tenuto e, prima dei 3 s, l'utente risponde nel terminale → la schermata si chiude, il tenuto non risponde a niente e non manda a Riposo.

- [ ] **Step 6: Ricollegamento**

Controller spento (tasto Xbox 6 s): toast «Controller scollegato». Riacceso: «Controller collegato» (annotare i secondi). RST della scheda: dopo l'avvio il controller si ricollega da solo (annotare i secondi).

- [ ] **Step 7: Wi-Fi insieme al controller**

Due minuti di uso continuo del controller (croce e stick) col PC collegato: il display non deve mostrare «PC non raggiungibile» e una richiesta di permesso deve arrivare normalmente. Poi cavo USB staccato, un minuto fermo (Attesa: `[energia] … -> Attesa`, `[wifi] risparmio: modem sleep massimo`): il controller resta collegato. Spegnere il controller, aspettare 3 minuti, riaccenderlo: si ricollega entro ~30 s (ricerca a intervalli). Ricollegare il cavo.

- [ ] **Step 8: Memoria**

Annotare le righe `[memoria]`: avvio; prima e dopo il Bluetooth; `ogni 10 min` dopo almeno 10 minuti con PC e controller collegati (heap minima). **Soglia:** heap minima ≥ 100 KB (spazio per i ~52 KB del driver veloce e l'emulatore del piano B). Sotto la soglia: annotarlo e segnalarlo al coordinatore prima di iniziare il piano B (possibile rimedio da valutare allora: ridurre i buffer di NimBLE con le opzioni di `nimconfig.h` della 2.5.1).
Spegnere e riaccendere il Bluetooth 5 volte di fila («Dimentica controller», poi «Associa controller» con l'associazione del 1914): la heap libera dopo ogni «Bluetooth spento» non deve calare di più di 4 KB rispetto alla prima (nessuna perdita da `NimBLEDevice::deinit`). Se cala, annotare: rimedio da decidere (spegnimento applicato al riavvio successivo).

- [ ] **Step 9: Sonno profondo**

A batteria: Menu › Riposo. Atteso `[pad] Bluetooth spento per il sonno profondo` prima di `[energia] sonno profondo`. Risveglio col tasto PRG: Bluetooth di nuovo acceso, controller ricollegato. I risvegli dell'orologio (ogni minuto) non accendono il Bluetooth (nessuna riga `[memoria] Bluetooth acceso` nei risvegli brevi, `calamaio logs`).

- [ ] **Step 10: Dimentica**

Menu › Gioco › Controller › «Dimentica controller» due volte: «Scegli ancora per confermare», poi «Controller dimenticato»; Stato «nessuno associato»; spento e riacceso, il controller non si ricollega.

- [ ] **Step 11: Documenti e commit**

`docs/HARDWARE.md`, sezione «Controller Bluetooth (firmware 0.11.0)»: heap (avvio, Bluetooth acceso, minima dopo 10 min), flash dell'immagine, tempi di associazione e ricollegamento, report map corto visto sì/no, Wi-Fi in Attesa col controller, prova di accensione/spegnimento ×5. `docs/DECISIONI.md`: «Prove del controller superate dal vivo (data, firmware 0.11.0)» con l'elenco degli step superati e quelli eventualmente falliti.

```bash
cd ~/CalamaiOS
git add docs/HARDWARE.md docs/DECISIONI.md
git commit -m "Controller: prove dal vivo del firmware 0.11.0"
```

---

## Verifica del piano rispetto alla spec e al contratto

- Driver veloce (spec §1, §4bis): porting dello spike (Task 1–4), Y da 1 byte e LUT 153 (Task 1, 4), spinte ai vicini per le frange (Task 2), pulizia a immagine ferma (Task 3), budget oltre 15 (Task 2), profili Clean/Smooth del contratto (Task 1), prova dal vivo con giudizio dell'utente sulle scie (Task 5), costo fisso (Task 6), passaggio da/a heltec-eink-modules (Task 4). Grigi: fuori (Global Constraints). Pulizia periodica in gioco: `cleanFull()` disponibile, la usa il piano B ogni 10 minuti (contratto). Driver solo per la partita (contratto).
- Controller (spec §1, §4bis): report e croce (Task 7), mappatura menu con tenuto di A 3 s (Task 8), NimBLE 2.5.1, `nimble_bond`, ricollegamento, report map troncato (Task 9–10), associazione solo da Menu › Gioco › Controller, un solo controller (Task 10, 12), Wi-Fi e coesistenza (Task 9, 13, 14), RAM (Task 13, 14).
- Menu col controller e permessi §7 (spec §1): Task 11 (test di ogni caso), prova dal vivo Task 14 Step 5.
- Impostazioni › Giochi (spec §1): Task 12, diventato Menu › Gioco (Controller, ROM, Alto o basso) con l'aggancio `gbGameItems` e `Ui::gbPlaying` per il piano B.
- Energia (richiesta del coordinatore): Task 9 (`radioPlan`, `wifiPowerSave`), Task 13 (accensione, sonno, ripristino), Task 14 Step 7–9.
- Contratto (versione del 2026-10-12): nomi e firme usati identici; le aggiunte di questo piano (Interfaces dei Task 4, 7, 9, 10, 12, 13) sono ora nel contratto, con `Ui::gbPlaying` e la sospensione della mappatura dei menu durante la partita (Task 12, 13).
