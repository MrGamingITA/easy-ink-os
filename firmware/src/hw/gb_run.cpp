#include "hw/gb_run.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <new>

#include "core/crypto.h"
#include "core/fastink_engine.h"
#include "core/gb_emu.h"
#include "core/gb_frame.h"
#include "core/gb_handoff.h"
#include "core/gb_save.h"
#include "core/strbuf.h"
#include "core/theme_runtime.h"
#include "hw/fastink.h"
#include "hw/gb_hw.h"
#include "hw/logtee.h"
#include "net/net.h"
#include "ui/theme_draw.h"

namespace gbrun {

namespace {

const uint32_t kInkStack = 4096;

// 0.12.2-gb: in modalità gioco l'anello del registro da remoto (rete spenta, non può uscire) fa da RAM per i primi
// banchi della cartuccia: lo heap ne deve fornire 16 KB in meno.
const int kLentBanks = 8;  // 8 pezzi da 2 KB = 16 KB
static_assert(cal::LogRing::kCap >= size_t(kLentBanks) * cal::gb::kRamBank, "anello del registro troppo piccolo");
// 0.13.0: l'arena del tema dà 3 pezzi interi; l'anello + l'arena (11 pezzi) stanno nella maschera dei prestati
const int kThemeBanks = cal::theme::kLendBytes / int(cal::gb::kRamBank);
static_assert(kThemeBanks == 3 && cal::theme::kLendBytes % int(cal::gb::kRamBank) == 0, "arena del tema: 3 pezzi da 2 KB");
static_assert(kLentBanks + kThemeBanks <= 16, "lentMask è a 16 bit");

const size_t kFastInkBytes = cal::fastink::Engine::kStateBytes + 2 * size_t(cal::fastink::kRamBytes);  // 36 000

struct Ctx {
  cal::gb::Emu emu;           // fotogramma 160×144 (5 760 byte) + contesto di Peanut-GB sullo heap
  cal::gb::FrameMailbox mb;   // 3 904 byte
  cal::gb::Pacer pacer;
  const uint8_t *bands;       // canvas di main.cpp
  uint8_t *bank[cal::gb::kMaxRamBanks];
  uint16_t lentMask;          // bit i: bank[i] è nell'anello prestato (niente free)
  size_t ramSize;
  char id[cal::gb::kIdLen];
  bool rot, inv;
  bool changed;               // salvataggio nuovo da begin()
  uint32_t drawn;             // ultimo fotogramma composto (Emu::frames)
  bool recompose;
  uint32_t emulated, statAt;  // fotogrammi emulati, per il registro ogni 10 s
  cal::gb::AutoSave autoSave;  // il «SALVA» del gioco in LittleFS dopo 3 s di calma (core/gb_save)
  bool saveFailed;             // ultimo salvataggio non riuscito (la RAM cambiata non è nella flash)
};

Ctx *g = nullptr;
uint8_t *gLent = nullptr;  // anello del registro in prestito (nullptr = no)
size_t gLentBytes = 0;
// 0.13.0: l'arena del tema (Runtime::lend, 6 144 byte) subito dopo l'anello, per i banchi da gLentBytes in poi
uint8_t *gThemeLent = nullptr;
size_t gThemeLentBytes = 0;
TaskHandle_t gInk = nullptr;
std::atomic<bool> gStop{false};
std::atomic<bool> gInkDone{true};

// Prestito solo con la rete già spenta: con la rete accesa il registro da remoto può servire.
void lendRing(size_t ramBytes) {
  if (gLent || !ramBytes || !net::suspended()) return;
  const size_t max = size_t(kLentBanks) * cal::gb::kRamBank;
  const size_t n = ramBytes < max ? ramBytes : max;
  gLent = logtee::lendRing(n);
  gLentBytes = gLent ? n : 0;
  // 0.13.0: dopo i 16 KB dell'anello, i 6 KB dell'arena del tema (Runtime::lend: blocco + buffer del disegno): tre
  // pezzi da 2 KB in meno dallo heap. Anche se l'anello non c'è (gLentBytes = 0: il tema copre i primi pezzi).
  cal::theme::Runtime *rt = ui::themeRuntime();
  if (rt && !gThemeLent && ramBytes > gLentBytes) {
    const size_t rest = ramBytes - gLentBytes;
    const size_t want = rest < size_t(cal::theme::kLendBytes) ? rest : size_t(cal::theme::kLendBytes);
    const size_t m = want / cal::gb::kRamBank * cal::gb::kRamBank;  // solo banchi interi
    gThemeLent = m ? rt->lend(m) : nullptr;
    gThemeLentBytes = gThemeLent ? m : 0;
  }
}

// Su ogni uscita, quando emulatore e task del pannello non toccano più i banchi.
void returnRing() {
  if (gThemeLent) {  // 0.13.0: prima il tema, anche se l'anello non è stato prestato
    gThemeLent = nullptr;
    gThemeLentBytes = 0;
    if (cal::theme::Runtime *rt = ui::themeRuntime()) rt->giveBack();
  }
  if (!gLent) return;
  gLent = nullptr;
  gLentBytes = 0;
  logtee::returnRing();
}

// Banco dall'anello prestato, poi dall'arena del tema (se ci sta tutto), altrimenti nullptr.
uint8_t *lentBank(size_t from, size_t len) {
  if (gLent && from + len <= gLentBytes) return gLent + from;
  if (gThemeLent && from >= gLentBytes && from + len <= gLentBytes + gThemeLentBytes) return gThemeLent + (from - gLentBytes);
  return nullptr;
}

void submitFrame(const uint8_t *f, void *) { cal::fastink::submit(f); }

size_t bankLen(size_t size, int i) {
  const size_t from = size_t(i) * cal::gb::kRamBank;
  return size - from < cal::gb::kRamBank ? size - from : cal::gb::kRamBank;
}

// Core 0, priorità 1 (sotto il task di NimBLE): prende il fotogramma più recente e spinge il pannello. tick()
// aspetta BUSY (~50–90 ms) e poi cede il core (vTaskDelay(1): il task idle del core 0 ha il watchdog).
void inkLoop(void *) {
  uint32_t frames = 0, ticks = 0, t0 = millis(), cleanAt = millis();
  while (!gStop.load()) {
    if (g->mb.take(submitFrame, nullptr)) frames++;
    if (kCleanMin && millis() - cleanAt >= uint32_t(kCleanMin) * 60000u) {  // contratto: pulizia periodica
      cal::fastink::cleanFull();
      cleanAt = millis();
      LogSerial.printf("[gb] pannello: pulizia completa periodica (ogni %u min)\n", unsigned(kCleanMin));
    }
    if (!cal::fastink::settled()) {
      if (cal::fastink::tick()) ticks++;
    } else {
      vTaskDelay(pdMS_TO_TICKS(4));
    }
    if (millis() - t0 >= 10000) {
      LogSerial.printf("[gb] pannello: %lu immagini nuove e %lu passi in 10 s, ultimo passo %lu ms, stack libero %u\n",
                       (unsigned long)frames, (unsigned long)ticks, (unsigned long)(cal::fastink::lastTickUs() / 1000),
                       unsigned(uxTaskGetStackHighWaterMark(nullptr)));
      frames = ticks = 0;
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

void compose() {
  uint8_t *out = g->mb.beginWrite();
  cal::gb::composeFrame(g->bands, g->emu.frame(), g->rot, out);
  if (g->inv)  // Inverti colori su tutto, bande comprese (fastink::submit non lo fa: contratto)
    for (size_t i = 0; i < cal::gb::kScreenBytes; i++) out[i] = static_cast<uint8_t>(~out[i]);
  g->mb.endWrite();
  g->drawn = g->emu.frames();
  g->recompose = false;
}

bool saveNow() {
  if (!g || !g->ramSize || !g->emu.ramDirty()) return true;
  const uint32_t t0 = millis();
  const bool ok = gbhw::writeSave(g->id, g->bank, g->ramSize);
  g->saveFailed = !ok;
  if (ok) {
    g->emu.clearDirty();
    g->changed = true;
  }
  LogSerial.printf("[gb] salvataggio /gb/%s.sav (%u byte): %s in %lu ms\n", g->id, unsigned(g->ramSize),
                   ok ? "scritto" : "NON riuscito", (unsigned long)(millis() - t0));
  return ok;
}

void freeCtx() {
  if (!g) return;
  g->emu.unload();
  for (int i = 0; i < cal::gb::kMaxRamBanks; i++)
    if (!(g->lentMask & (1u << i))) free(g->bank[i]);  // i prestati no
  delete g;
  g = nullptr;
  cal::fastink::unreserve();
  returnRing();
  gbhw::unmapRom();
}

}  // namespace

bool active() { return g != nullptr; }
bool running() { return gInk != nullptr; }
bool saveFailed() { return g && g->saveFailed; }
const char *id() { return g ? g->id : ""; }

size_t needBytes(const cal::gb::RomInfo &info, size_t lentBytes) {
  const size_t ram = info.ramBytes > lentBytes ? info.ramBytes - lentBytes : 0;  // solo quella dallo heap
  return sizeof(Ctx) + cal::gb::Emu::contextBytes() + ram + kFastInkBytes + kInkStack + 512;
}

bool begin(const cal::gb::RomInfo &info, const char **why) {
  if (g) end();
  if (info.ramBytes > cal::gb::kCartRamMax) {
    *why = "Gioco non supportato";
    return false;
  }
  if (info.ramBytes && !gbhw::savesOk()) {  // gioco con la batteria: senza LittleFS la partita andrebbe persa
    LogSerial.println("[gb] partita non avviata: LittleFS non montato, i salvataggi non si scriverebbero");
    *why = "Salvataggi non disponibili";
    return false;
  }
  lendRing(info.ramBytes);  // da qui ogni uscita con errore restituisce l'anello (returnRing o freeCtx)
  const size_t need = needBytes(info, gLentBytes + gThemeLentBytes), freeNow = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
  LogSerial.printf("[gb] memoria per la partita: servono %u byte (+%u di margine), liberi %u, blocco massimo %u\n",
                   unsigned(need), unsigned(kHeapFloor), unsigned(freeNow), unsigned(block));
  if (freeNow < need + kHeapFloor || block < cal::gb::Emu::contextBytes() + 64) {
    returnRing();
    *why = "Memoria insufficiente";
    return false;
  }
  if (freeNow < need + kHeapTarget)
    LogSerial.printf("[gb] attenzione: durante la partita resteranno circa %u byte (obiettivo %u)\n",
                     unsigned(freeNow - need), unsigned(kHeapTarget));
  if (!cal::fastink::reserve()) {  // prima dell'emulatore: i blocchi da 16 KB vanno presi a heap ancora intero
    returnRing();
    *why = "Memoria insufficiente";
    return false;
  }
  const uint8_t *rom = gbhw::mapRom(info.size);
  if (!rom) {
    cal::fastink::unreserve();
    returnRing();
    *why = "Gioco non leggibile";
    return false;
  }
  // Id del gioco (SHA-256, contratto) e checksum globale in un solo passaggio sulla flash mappata.
  const uint32_t t0 = millis();
  cal::cry::Sha256Stream sha;
  for (uint32_t off = 0; off < info.size; off += 4096) sha.update(rom + off, info.size - off < 4096 ? info.size - off : 4096);
  uint8_t h[cal::cry::kHash];
  sha.finish(h);
  const bool global = cal::gb::globalOk(rom, info);
  g = new (std::nothrow) Ctx;
  if (!g) {
    cal::fastink::unreserve();
    returnRing();
    gbhw::unmapRom();
    *why = "Memoria insufficiente";
    return false;
  }
  memset(g->bank, 0, sizeof g->bank);
  g->lentMask = 0;
  g->bands = nullptr;
  g->ramSize = 0;
  g->rot = g->inv = g->changed = g->recompose = false;
  g->drawn = g->emulated = 0;
  g->statAt = millis();
  g->saveFailed = false;
  cal::gb::gameId(h, g->id);
  LogSerial.printf("[gb] ROM %s «%s»: %lu byte, checksum globale %s, letta in %lu ms\n", g->id, info.title,
                   (unsigned long)info.size, global ? "giusto" : "SBAGLIATO (ROM scritta male o rovinata?)",
                   (unsigned long)(millis() - t0));
  const cal::gb::Load l = g->emu.load(rom, info.size);
  if (l != cal::gb::Load::Ok) {
    LogSerial.printf("[gb] ROM rifiutata dall'emulatore: %s\n", cal::gb::loadKey(l));
    *why = l == cal::gb::Load::RamTooBig || l == cal::gb::Load::Unsupported ? "Gioco non supportato" : "Gioco non leggibile";
    freeCtx();
    return false;
  }
  g->ramSize = g->emu.ramSize();
  size_t fromRing = 0, fromTheme = 0;  // fromRing: tutto il prestato (anello + tema)
  for (int i = 0; i < g->emu.ramBanks(); i++) {  // pezzi da 2 KB: lo heap in gioco non ha blocchi da 8 KB
    const size_t len = bankLen(g->ramSize, i);
    g->bank[i] = lentBank(size_t(i) * cal::gb::kRamBank, len);
    if (g->bank[i]) {
      fromRing += len;
      if (size_t(i) * cal::gb::kRamBank >= gLentBytes) fromTheme += len;
      g->lentMask |= uint16_t(1u << i);
    } else {
      g->bank[i] = static_cast<uint8_t *>(malloc(len));
    }
    if (!g->bank[i]) {
      LogSerial.printf("[gb] pezzo %d della RAM (%u byte) non allocato: liberi %u, blocco massimo %u\n", i, unsigned(len),
                       unsigned(ESP.getFreeHeap()), unsigned(ESP.getMaxAllocHeap()));
      freeCtx();
      *why = "Memoria insufficiente";
      return false;
    }
    g->emu.attachBank(i, g->bank[i]);
  }
  if (g->ramSize)
    LogSerial.printf("[gb] RAM cartuccia: %u byte dall'anello del registro, %u dal tema, %u dallo heap\n",
                     unsigned(fromRing - fromTheme), unsigned(fromTheme), unsigned(g->ramSize - fromRing));
  if (!fromRing) returnRing();  // prestato ma non usato (RAM più piccola del previsto): torna subito
  const gbhw::SaveRead sr = g->ramSize ? gbhw::readSave(g->id, g->bank, g->ramSize) : gbhw::SaveRead::Missing;
  if (sr == gbhw::SaveRead::Bad || sr == gbhw::SaveRead::Stuck) {  // mai sovrascrivere un salvataggio che non si legge
    freeCtx();
    *why = sr == gbhw::SaveRead::Bad ? "Salvataggio rovinato: messo da parte" : "Salvataggio rovinato: copie in /gb piene";
    return false;
  }
  const bool had = sr == gbhw::SaveRead::Ok;
  g->autoSave.start(millis(), g->emu.ramWrites());
  if (!had)  // cartuccia nuova (o salvataggio illeggibile): RAM come una batteria appena montata
    for (int i = 0; i < g->emu.ramBanks(); i++) memset(g->bank[i], 0xFF, bankLen(g->ramSize, i));
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  g->emu.setRtc(lt);       // MBC3: l'orologio della cartuccia parte dall'ora della scheda
  g->emu.frameSkip(true);  // il pannello va a ~11 immagini/s: si disegna un fotogramma su due
  LogSerial.printf("[gb] %s pronto: RAM della cartuccia %u byte (%s), heap libero %u\n", g->id, unsigned(g->ramSize),
                   !g->ramSize ? "nessuna" : had ? "dal salvataggio" : "nuova", unsigned(ESP.getFreeHeap()));
  return true;
}

bool resume(bool rot, bool inv, const uint8_t *bands, int64_t nowUs) {
  if (!g || gInk) return g != nullptr;
  g->rot = rot;
  g->inv = inv;
  g->bands = bands;
  g->mb.reset();
  if (!cal::fastink::begin(cal::fastink::Profile::Clean)) return false;  // completo di serie col bianco
  compose();  // primo fotogramma: lo disegnano i passi veloci, dal bianco noto
  g->pacer.start(nowUs);
  gStop = false;
  gInkDone = false;
  if (xTaskCreatePinnedToCore(inkLoop, "gb-ink", kInkStack, nullptr, 1, &gInk, 0) != pdPASS) {
    gInk = nullptr;
    gInkDone = true;
    cal::fastink::end();
    return false;
  }
  return true;
}

void pause() {
  if (!g) return;
  stopInk();
  if (cal::fastink::active()) cal::fastink::end();  // pulizia completa, pannello di nuovo al driver di serie
  saveNow();
}

bool step(uint8_t keys, int64_t nowUs) {
  if (!g || !gInk) return true;
  const int n = g->pacer.due(nowUs);
  for (int i = 0; i < n; i++)
    if (!g->emu.runFrame(keys)) return false;
  if (n) {
    g->pacer.ran(n);
    g->emulated += uint32_t(n);
  }
  if (g->emu.frames() != g->drawn || g->recompose) compose();
  if (g->autoSave.due(millis(), g->emu.ramWrites(), g->emu.ramDirty())) {
    // ~0,1–0,3 s di flash: il Pacer recupera o riparte da adesso
    if (saveNow()) g->autoSave.saved(millis());
    else g->autoSave.failed(millis());
  }
  if (millis() - g->statAt >= 10000) {
    LogSerial.printf("[gb] emulatore: %lu fotogrammi in 10 s (597 attesi), persi in tutto %lu, heap libero %u (minimo %u)\n",
                     (unsigned long)g->emulated, (unsigned long)g->pacer.dropped(), unsigned(ESP.getFreeHeap()),
                     unsigned(ESP.getMinFreeHeap()));
    g->emulated = 0;
    g->statAt = millis();
  }
  return true;
}

void bandsChanged() {
  if (g) g->recompose = true;
}

bool end(bool save, bool *saveFailed) {
  if (!g) return false;
  if (gInk) {
    stopInk();
    if (cal::fastink::active()) cal::fastink::end();
  }
  if (save) saveNow();
  else if (g->emu.ramDirty()) LogSerial.printf("[gb] uscita senza salvare: resta il salvataggio di prima di %s\n", g->id);
  if (saveFailed) *saveFailed = save && g->saveFailed;
  const bool changed = g->changed;
  freeCtx();
  LogSerial.printf("[gb] partita chiusa%s, heap libero %u\n", changed ? " (salvataggio nuovo)" : "", unsigned(ESP.getFreeHeap()));
  return changed;
}

}  // namespace gbrun
