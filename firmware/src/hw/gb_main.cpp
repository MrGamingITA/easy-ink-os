#include "hw/gb_main.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <string.h>

#include "core/gb_input.h"
#include "core/gb_rom.h"
#include "core/gb_session.h"
#include "hw/blepad.h"
#include "hw/gb_hw.h"
#include "hw/gb_run.h"
#include "hw/logtee.h"
#include "hw/ota_hw.h"
#include "net/net.h"
#include "ui/screens.h"

namespace gbmain {

using cal::Ms;
using cal::RefreshKind;
namespace gb = cal::gb;

namespace {

const uint32_t kSuspendMs = 8000;  // il task di rete può essere dentro una ricerca mDNS (~3 s) o un collegamento
const Ms kNetRetryMs = 2000;
const Ms kHeapAfterMs = 15000;     // la seconda riga «dopo l'uscita», con il Wi-Fi di nuovo su

gb::Session gSession;
gb::PauseEdge gEdge;
uint8_t gKeys = 0;
bool gBandsDirty = true;
bool gStartPending = false;  // GbLoad a schermo: la modalità gioco parte al giro dopo il disegno
bool gNetBack = false;       // net::resume() da ritentare
Ms gNetRetryAt = 0, gHeapAfterAt = 0;

void heapLine(const char *when) {
  LogSerial.printf("[memoria] %s: heap libero %u, minimo %u, blocco max %u\n", when, unsigned(ESP.getFreeHeap()),
                   unsigned(ESP.getMinFreeHeap()), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

void netBack(Ms now) {
  gNetBack = !net::resume();
  gNetRetryAt = now + kNetRetryMs;
}

// Bande della schermata GbPlay nel canvas di main.cpp, senza Inverti né Ruota (li applica gbrun a tutto il fotogramma).
const uint8_t *bands(cal::Ui &ui, Ms now, const Env &env) {
  env.scratch->clear(false);
  CanvasSurface<250, 122> s(*env.scratch);
  ui::draw(s, ui.device(), ui.model(), 0, now, env.wc);
  return env.scratch->data();
}

gb::SessionIn inputs(const cal::Ui &ui, const Env &env) {
  const cal::Model &m = ui.model();
  return gb::SessionIn{cal::blepad::link() == cal::pad::Link::Ready, 0, int(m.sys.batt + 0.5), env.usb, env.battCritical};
}

const char *exitText(gb::ExitWhy w, bool saveFailed) {
  if (w == gb::ExitWhy::Fault) return "Il gioco si è fermato: resta il salvataggio di prima";
  if (saveFailed) return "Salvataggio NON riuscito";  // mai «Partita salvata» se la flash non l'ha preso
  return w == gb::ExitWhy::Battery ? "Batteria scarica: partita salvata" : "Partita salvata";
}

RefreshKind exitGame(cal::Ui &ui, gb::ExitWhy why, Ms now, const char *msg = nullptr) {
  bool failed = false;
  // pannello restituito (fastink::end → panelBack) e salvataggio; dopo un errore della CPU la RAM non si salva
  const bool changed = gbrun::end(why != gb::ExitWhy::Fault, &failed);
  gSession.stop();
  gKeys = 0;
  LogSerial.printf("[gb] fine partita: %s%s%s\n", gb::exitKey(why), changed ? ", salvataggio nuovo nella scheda" : "",
                   failed ? ", ULTIMO SALVATAGGIO NON RIUSCITO" : "");
  netBack(now);
  heapLine("dopo l'uscita");
  gHeapAfterAt = now + kHeapAfterMs;
  return ui.gbEnd(msg && !failed ? msg : exitText(why, failed), why == gb::ExitWhy::PausedTooLong || why == gb::ExitWhy::Rest, now);
}

RefreshKind startGame(cal::Ui &ui, Ms now, const Env &env) {
  gb::RomInfo info;
  if (!gbhw::romInfo(info)) return ui.gbEnd("Nessuna ROM", false, now);
  heapLine("prima della partita");
  if (!net::suspend(kSuspendMs)) {
    gNetBack = true;  // se il task finisse di spegnersi più tardi, la rete si riaccende da sola
    gNetRetryAt = now + kNetRetryMs;
    return ui.gbEnd("Rete non spenta: riprova fra poco", false, now);
  }
  gNetBack = false;  // rete spenta adesso: nessun vecchio tentativo di riaccenderla durante la partita
  // Le richieste di permesso di prima non valgono più (anche se la partita non parte): il servizio rimanda le
  // aperte al prossimo collegamento.
  ui.model().nPerms = 0;
  heapLine("modalità gioco");
  const char *why = "Memoria insufficiente";
  if (!gbrun::begin(info, &why)) {
    LogSerial.printf("[gb] partita non avviata: %s\n", why);
    netBack(now);
    return ui.gbEnd(why, false, now);
  }
  ui.gbPlay(now);
  gSession.start(now, 0);
  gEdge.arm(true);
  gKeys = 0;
  const cal::Model &m = ui.model();
  if (!gbrun::resume(m.set.rot, m.set.invert, bands(ui, now, env), esp_timer_get_time())) {
    gbrun::end();
    gSession.stop();
    netBack(now);
    return ui.gbEnd("Memoria insufficiente", false, now);
  }
  gBandsDirty = false;
  LogSerial.printf("[gb] partita %s «%s», profilo pulito\n", gbrun::id(), m.gb.title);
  heapLine("partita avviata");
  return RefreshKind::None;
}

RefreshKind pauseGame(cal::Ui &ui, gb::PauseWhy why, Ms now) {
  gSession.pause(why, now);  // da Session::update è già in pausa: non cambia niente
  gbrun::pause();            // pannello di nuovo alla libreria (panelBack), salvataggio in LittleFS
  LogSerial.printf("[gb] pausa: %s\n", gb::pauseKey(why));
  heapLine("pausa");
  const RefreshKind r = ui.gbPaused(why, now);
  if (gbrun::saveFailed()) cal::toast(ui.model(), "Salvataggio NON riuscito: si riprova all'uscita", now);
  return r;
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
  if (!gbrun::resume(m.set.rot, m.set.invert, bands(ui, now, env), esp_timer_get_time()))
    return exitGame(ui, gb::ExitWhy::Fault, now, "Memoria insufficiente: partita salvata");
  gBandsDirty = false;
  heapLine("partita ripresa");
  return RefreshKind::None;
}

}  // namespace

void begin(cal::Ui &ui) {
  const bool part = gbhw::begin();
  gb::RomInfo info;
  const bool rom = part && gbhw::romInfo(info);
  ui.gbInstalled(part, rom, rom ? info.title : "", rom ? info.size / 1024 : 0);
}

void bandsDirty() { gBandsDirty = true; }
bool playing() { return gSession.active() || gStartPending || gbrun::active(); }
bool ownsPanel(const cal::Ui &ui) { return ui.gbPlaying() && gbrun::running(); }

RefreshKind loop(cal::Ui &ui, Ms now, const Env &env) {
  // Rete da riaccendere dopo una partita: mai a partita aperta o in avvio (si spegnerebbe sotto il gioco).
  if (gNetBack && now >= gNetRetryAt && !playing()) netBack(now);
  if (gHeapAfterAt && now >= gHeapAfterAt) {
    gHeapAfterAt = 0;
    heapLine("dopo l'uscita, rete riaccesa");
  }
  if (gStartPending) {
    gStartPending = false;
    return startGame(ui, now, env);
  }
  RefreshKind r = RefreshKind::None;
  cal::Model &m = ui.model();
  switch (ui.takeGbAct()) {
    case gb::GbAct::Pick:
      if (gSession.active()) break;
      if (otahw::trialActive()) {  // senza Wi-Fi la versione appena arrivata tornerebbe indietro
        cal::toast(m, "Aggiornamento in prova: riprova fra 2 minuti", now);
        r = RefreshKind::Partial;
        break;
      }
      r = ui.gbLoad(now);
      gStartPending = true;
      break;
    case gb::GbAct::Pause:
      if (gSession.phase() == gb::Phase::Playing) r = pauseGame(ui, gb::PauseWhy::User, now);
      break;
    case gb::GbAct::Resume:
      if (gSession.phase() == gb::Phase::Paused) r = resumeGame(ui, now, env);
      break;
    case gb::GbAct::Exit:
      if (gSession.active()) r = exitGame(ui, gb::ExitWhy::User, now);
      break;
    case gb::GbAct::ExitRest:
      if (gSession.active()) r = exitGame(ui, gb::ExitWhy::Rest, now);
      break;
    case gb::GbAct::None: break;
  }
  if (!gSession.active()) return r;
  const gb::Verdict v = gSession.update(inputs(ui, env), now);
  if (v.lowBand != m.gb.lowBand) {
    m.gb.lowBand = v.lowBand;
    gBandsDirty = true;
  }
  if (v.act == gb::Verdict::Exit) return cal::mergeRefresh(r, exitGame(ui, v.exit, now));
  if (v.act == gb::Verdict::Pause) return cal::mergeRefresh(r, pauseGame(ui, v.pause, now));
  if (gSession.phase() != gb::Phase::Playing || !gbrun::running()) return r;
  cal::pad::State s;
  while (cal::blepad::poll(s)) {  // tutti i report arrivati: la combinazione di pausa conta sul fronte
    const gb::PadKeys k = gb::mapPad(s);
    if (gEdge.update(k.pause)) return cal::mergeRefresh(r, pauseGame(ui, gb::PauseWhy::User, now));
    if (k.keys != gKeys) {
      gKeys = k.keys;
      gSession.input(now);
    }
  }
  if (gBandsDirty) {
    bands(ui, now, env);
    gbrun::bandsChanged();
    gBandsDirty = false;
  }
  if (!gbrun::step(gKeys, esp_timer_get_time())) r = cal::mergeRefresh(r, exitGame(ui, gb::ExitWhy::Fault, now));
  return r;
}

}  // namespace gbmain
