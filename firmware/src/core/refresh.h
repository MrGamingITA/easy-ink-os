// 0.10.2: quando il pannello vero fa un refresh completo (il lampo nero/bianco). RefreshCounter (ui.h) resta la copia
// fedele di class Screen del simulatore per i test golden; questa è la regola della scheda.
// - Partial e Quick si eseguono come parziali; Quick conta 2.
// - Raggiunti fullEvery parziali la pulizia è «dovuta» ma rimandata: si fa entrando nel Riposo, o dopo 30 s senza
//   disegni né tasti (completo da solo). Tetto: a 2×fullEvery il completo si fa su quel disegno comunque.
// - Prima del sonno profondo un completo solo con almeno kSleepFullMin parziali dall'ultimo.
// La pulizia dovuta non ha un campo suo: è partials() >= fullEvery, quindi passa il sonno con i parziali (RestKeep).
#pragma once
#include <stdint.h>

#include "clock.h"
#include "ui.h"

namespace cal {

// Perché l'ultimo apply() ha dato un completo.
enum class FullWhy : uint8_t { None, Asked, Deferred, Cap, BeforeSleep };

class PanelRefresh {
 public:
  static constexpr int kSleepFullMin = 10;   // parziali minimi per un completo prima del sonno
  static constexpr Ms kIdleMs = 30000;       // senza disegni né tasti: pulizia dovuta da sola

  explicit PanelRefresh(int fullEvery) : fullEvery_(fullEvery) {}
  // Richiesta → refresh da eseguire sul pannello: None, Partial o Full (mai Quick). restEntry: questo disegno porta
  // nel Riposo (momento buono per la pulizia dovuta). now: per l'inattività.
  RefreshKind apply(RefreshKind req, bool restEntry, Ms now);
  FullWhy why() const { return why_; }
  int partials() const { return partials_; }
  bool due() const { return partials_ >= fullEvery_; }
  // Un tasto: la pulizia d'inattività aspetta altri 30 s.
  void input(Ms now) { last_ = now; }
  // Pulizia dovuta e 30 s fermi: true = chiedere un completo (il prossimo apply(Full) dice Deferred).
  bool idleCleanup(Ms now);
  // Prima del sonno profondo, con il disegno in attesa: che cosa disegnare prima di dormire (None: si dorme subito).
  // Full se pending è Full o se, contato pending, i parziali dall'ultimo completo sono almeno kSleepFullMin.
  RefreshKind beforeSleep(RefreshKind pending);
  // Dopo il sonno profondo: parziali salvati nella memoria RTC (RestKeep::partials).
  void restore(int partials, Ms now);

 private:
  int fullEvery_;
  int partials_ = 0;
  Ms last_ = 0;
  FullWhy why_ = FullWhy::None;
  FullWhy forced_ = FullWhy::None;  // motivo del prossimo apply(Full) chiesto da idleCleanup/beforeSleep
};

}  // namespace cal
