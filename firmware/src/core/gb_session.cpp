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
