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
