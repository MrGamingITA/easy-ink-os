#include "power.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "battery.h"
#include "strbuf.h"

namespace cal {

const char *powerStateName(PowerState s) {
  static const char *const k[] = {"Attivo", "Attesa", "Sonno profondo"};
  return k[static_cast<int>(s)];
}

const char *sleepReasonName(SleepReason r) {
  static const char *const k[] = {"nessuno", "bye", "riposo", "batteria scarica"};
  return k[static_cast<int>(r)];
}

PowerInputs powerInputs(const Model &m) {
  PowerInputs in;
  memset(&in, 0, sizeof in);
  in.pc = m.sys.pc;
  in.pcOff = m.sys.pcOff;
  // Le sessioni contano solo con il PC collegato: dopo un `bye` l'ultimo state non dice più niente.
  if (m.sys.pc)
    for (int k = 0; k < m.nSessions; k++) {
      const SessionState s = m.sessions[k].state;
      if (s == SessionState::Working || s == SessionState::Permission || s == SessionState::Input ||
          s == SessionState::Compact)
        in.busy = true;
    }
  in.permOpen = (m.sys.pc && m.nPerms > 0) || top(m).id == ScreenId::Perm;
  in.calibrating = calRunning(m);
  in.calibIdle = m.calib.phase == CalibPhase::Idle;
  const OtaPhase op = m.ota.phase;
  in.ota = op == OtaPhase::Receive || op == OtaPhase::Verify || op == OtaPhase::Write || op == OtaPhase::Reboot;
  // F6 (0.14.0): anche il codice del telefono a schermo tiene Attivo (il sonno chiuderebbe l'associazione)
  in.playing = top(m).id == ScreenId::Game || top(m).id == ScreenId::PadCode;
  return in;
}

// ---------- PowerManager ----------

void PowerManager::begin(Ms now) {
  state_ = PowerState::Active;
  reason_ = SleepReason::None;
  why_ = "avvio";
  lastKey_ = now;
  byeAt_ = 0;
  rest_ = false;
}

void PowerManager::key(Ms now) { lastKey_ = now; }
void PowerManager::bye(Ms now) { byeAt_ = now ? now : 1; }
void PowerManager::rest(Ms now) { (void)now; rest_ = true; }

bool PowerManager::update(const PowerInputs &in, Ms now) {
  PowerState s;
  SleepReason r = SleepReason::None;
  const char *why;
  if (in.pc) byeAt_ = 0;  // il servizio è tornato (riavvio): niente sonno
  const Ms quietFor = now - lastKey_;
  const Ms sinceBye = byeAt_ ? now - byeAt_ : 0;
  if (in.ota) {
    rest_ = false;  // il sonno profondo con la nuova versione «in prova» la scarterebbe (rollback del bootloader)
    s = PowerState::Active, why = "aggiornamento remoto in corso o da confermare";
  } else if (in.battCritical && !in.usb) {
    s = PowerState::Sleep, r = SleepReason::Battery, why = "batteria sotto la soglia critica senza cavo";
  } else if (in.calibrating) {
    rest_ = false;  // durante la calibrazione niente sonno profondo
    if (in.calibIdle) s = PowerState::Idle, why = "calibrazione, fase 2 (attesa)";
    else s = PowerState::Active, why = "calibrazione, fase 1 (massimo consumo)";
  } else if (rest_ && !in.usb) {
    s = PowerState::Sleep, r = SleepReason::Rest, why = "Riposo senza cavo";
  } else if (in.pcOff && !in.pc && !in.usb && (byeAt_ ? sinceBye >= kByeGraceMs : true) && quietFor >= kIdleAfterMs) {
    s = PowerState::Sleep, r = SleepReason::Bye,
    why = byeAt_ ? "PC spento: nessuna risposta per 60 s dopo il bye" : "PC spento e nessun tasto da 60 s";
  } else if (in.usb) {
    s = PowerState::Active, why = "cavo USB collegato";
  } else if (in.permOpen) {
    s = PowerState::Active, why = "richiesta di permesso aperta";
  } else if (in.busy) {
    s = PowerState::Active, why = "una sessione lavora o aspetta";
  } else if (in.playing) {
    s = PowerState::Active, why = "gioco in corso";
  } else if (quietFor < kIdleAfterMs) {
    s = PowerState::Active, why = "tasto premuto da meno di 60 s";
  } else {
    s = PowerState::Idle, why = "sessioni ferme e nessun tasto da 60 s";
  }
  if (in.usb) rest_ = false;  // col cavo il Riposo è solo la schermata
  const bool changed = s != state_ || (s == PowerState::Sleep && r != reason_);
  state_ = s;
  reason_ = r;
  why_ = why;
  return changed;
}

// Dopo un `bye` il PC si sta spegnendo (o il servizio riavvia): con il cavo si resta svegli, senza si
// aspetta la grazia di 60 s. Un `bye` mentre l'utente usa i tasti non lo addormenta sotto le dita (60 s
// dall'ultimo tasto anche qui).

TimerWakeOutcome timerWakeDecision(SleepReason r, bool pcFound, bool busy, bool usb, bool key) {
  if (usb) return {true, false, "cavo USB collegato"};
  if (key) return {true, false, "tasto premuto durante il risveglio"};
  if (r == SleepReason::Battery) return {false, false, "batteria scarica"};
  if (!pcFound) return {false, r == SleepReason::Rest, "PC non trovato"};
  if (r == SleepReason::Bye) return {true, false, "il PC è di nuovo acceso"};
  if (busy) return {true, false, "una sessione lavora o aspetta"};
  return {false, true, "PC acceso ma niente da fare: resta il Riposo"};
}

// ---------- CriticalBattery ----------

bool CriticalBattery::update(int mv, bool usb) {
  if (usb) { n_ = 0; return false; }
  if (mv < 0) return critical();
  if (mv < kBattCriticalMv) n_++;
  else n_ = 0;
  return critical();
}

// ---------- BatteryTrend ----------

void BatteryTrend::add(Ms now, int mv, bool usb) {
  if (usb) { n_ = 0; last_ = 0; return; }
  if (mv < 0) return;
  if (n_ && now - last_ < kEveryMs) return;
  if (n_ == kCap) {
    memmove(t_, t_ + 1, sizeof(Ms) * (kCap - 1));
    memmove(mv_, mv_ + 1, sizeof(int16_t) * (kCap - 1));
    n_--;
  }
  t_[n_] = now;
  mv_[n_] = static_cast<int16_t>(mv);
  n_++;
  last_ = now;
}

bool BatteryTrend::rate(double &pctPerH) const {
  if (n_ < kCalMinSamples) return false;
  if ((t_[n_ - 1] - t_[0]) / 3600000.0 < kMinSpanH) return false;
  double h[kCap], p[kCap];
  for (int k = 0; k < n_; k++) {
    h[k] = (t_[k] - t_[0]) / 3600000.0;
    p[k] = ocvPercent(mv_[k]);
  }
  double r = 0;
  if (!calRate(h, p, n_, r) || r <= 0) return false;
  pctPerH = r;
  return true;
}

// ---------- EnergyLedger ----------

void EnergyLedger::clear() { memset(b, 0, sizeof b); }

void EnergyLedger::add(PowerState s, int64_t endEpochS, int64_t secs) {
  if (secs <= 0 || endEpochS <= 0) return;
  if (secs > 24 * 3600) secs = 24 * 3600;  // più vecchio di un giorno non serve
  const int st = static_cast<int>(s);
  int64_t t = endEpochS - secs;
  while (t < endEpochS) {
    const int32_t hour = static_cast<int32_t>(t / 3600);
    const int64_t hourEnd = int64_t(hour + 1) * 3600;
    const int64_t chunk = (hourEnd < endEpochS ? hourEnd : endEpochS) - t;
    LedgerBucket &k = b[hour % 24];
    if (k.hour != hour) { k.hour = hour; k.sec[0] = k.sec[1] = k.sec[2] = 0; }
    const uint32_t v = k.sec[st] + static_cast<uint32_t>(chunk);
    k.sec[st] = static_cast<uint16_t>(v > 3600 ? 3600 : v);
    t += chunk;
  }
}

bool EnergyLedger::totals(int64_t nowEpochS, uint32_t out[3]) const {
  out[0] = out[1] = out[2] = 0;
  const int32_t nowHour = static_cast<int32_t>(nowEpochS / 3600);
  for (const LedgerBucket &k : b)
    if (k.hour > 0 && k.hour <= nowHour && nowHour - k.hour < 24)
      for (int q = 0; q < 3; q++) out[q] += k.sec[q];
  return out[0] + out[1] + out[2] >= 3600;
}

bool EnergyLedger::mix(int64_t nowEpochS, int out[3]) const {
  uint32_t t[3];
  if (!totals(nowEpochS, t)) return false;
  const double tot = double(t[0]) + t[1] + t[2];
  out[0] = jsRound(100.0 * t[0] / tot);
  out[1] = jsRound(100.0 * t[1] / tot);
  out[2] = 100 - out[0] - out[1];
  if (out[2] < 0) { out[1] += out[2]; out[2] = 0; }
  return true;
}

// ---------- sonno dalle notti ----------

bool sleepRateSample(int mvStart, int mvEnd, int64_t secs, double &rate) {
  if (secs < kSleepSampleMinS || mvStart < kBattMinMv || mvEnd < kBattMinMv) return false;
  const double r = (ocvPercent(mvStart) - ocvPercent(mvEnd)) / (secs / 3600.0);
  if (r < 0 || r > 5) return false;  // la tensione è salita (cavo?) o scesa in modo assurdo
  rate = r;
  return true;
}

double mergeSleepRate(bool has, double old, double sample) { return has ? (old + sample) / 2 : sample; }

// ---------- NVS ----------

namespace {
uint16_t to100(double v) {
  const int x = jsRound(v * 100);
  return static_cast<uint16_t>(x < 0 ? 0 : x > 65535 ? 65535 : x);
}
uint8_t sum8(const uint8_t *d, size_t n) {
  uint8_t s = 0x5A;
  for (size_t k = 0; k < n; k++) s = static_cast<uint8_t>((s << 1 | s >> 7) ^ d[k]);
  return s;
}
}  // namespace

// [0] versione 1 · [1] bandiere (1 calibrata, 2 sonno misurato) · [2..7] attivo, attesa, sonno × 100 (u16 LE)
// · [8..15] ora locale della calibrazione (i64 LE) · [16..18] riservati · [19] somma
size_t packBat(const BatInfo &b, uint8_t *out, size_t cap) {
  if (cap < kBatBlobLen) return 0;
  memset(out, 0, kBatBlobLen);
  out[0] = 1;
  out[1] = (b.cal.valid ? 1 : 0) | (b.hasSleep ? 2 : 0);
  const uint16_t v[3] = {to100(b.cal.active), to100(b.cal.idle), to100(b.sleep)};
  for (int k = 0; k < 3; k++) { out[2 + 2 * k] = v[k] & 0xFF; out[3 + 2 * k] = v[k] >> 8; }
  const uint64_t at = static_cast<uint64_t>(b.cal.atLocal);
  for (int k = 0; k < 8; k++) out[8 + k] = static_cast<uint8_t>(at >> (8 * k));
  out[19] = sum8(out, 19);
  return kBatBlobLen;
}

bool unpackBat(const uint8_t *in, size_t n, BatInfo &b) {
  if (n != kBatBlobLen || in[0] != 1 || in[19] != sum8(in, 19)) return false;
  b.cal.valid = in[1] & 1;
  b.hasSleep = (in[1] & 2) != 0;
  const double v0 = (in[2] | in[3] << 8) / 100.0, v1 = (in[4] | in[5] << 8) / 100.0, v2 = (in[6] | in[7] << 8) / 100.0;
  b.cal.active = b.cal.valid ? v0 : 0;
  b.cal.idle = b.cal.valid ? v1 : 0;
  b.sleep = b.hasSleep ? v2 : 0;
  uint64_t at = 0;
  for (int k = 0; k < 8; k++) at |= uint64_t(in[8 + k]) << (8 * k);
  b.cal.atLocal = b.cal.valid ? static_cast<int64_t>(at) : 0;
  return true;
}

// ---------- istantanea del sonno ----------

namespace {
uint32_t fnv32(const void *p, size_t n) {
  const uint8_t *d = static_cast<const uint8_t *>(p);
  uint32_t h = 2166136261u;
  for (size_t k = 0; k < n; k++) { h ^= d[k]; h *= 16777619u; }
  return h;
}
}  // namespace

void ledgerSeal(LedgerStore &s) {
  s.magic = kSnapMagic ^ 0x1EDu;
  s.sum = fnv32(&s, offsetof(LedgerStore, sum));
}

bool ledgerValid(const LedgerStore &s) {
  return s.magic == (kSnapMagic ^ 0x1EDu) && s.sum == fnv32(&s, offsetof(LedgerStore, sum));
}

void snapSeal(SleepSnap &s) {
  s.magic = kSnapMagic;
  s.sum = fnv32(&s, offsetof(SleepSnap, sum));
}

bool snapValid(const SleepSnap &s) {
  return s.magic == kSnapMagic && s.sum == fnv32(&s, offsetof(SleepSnap, sum)) && s.n <= kMaxSessions &&
         s.reason <= static_cast<uint8_t>(SleepReason::Battery);
}

void snapFromModel(SleepSnap &s, const Model &m, int64_t epochNowMs, Ms now) {
  s.pcOff = m.sys.pcOff;
  s.offEpochMs = m.sys.pcOff && m.sys.offAt ? epochNowMs - (now - m.sys.offAt) : 0;
  s.n = static_cast<uint8_t>(m.nSessions);
  for (int k = 0; k < m.nSessions; k++) {
    const Session &x = m.sessions[k];
    SnapSession &y = s.s[k];
    scopy(y.name, x.name);
    y.state = static_cast<uint8_t>(x.state);
    scopy(y.summary, x.summary);
    y.attnEpochMs = x.hasAttnAt ? epochNowMs - (now - x.attnAt) : 0;
  }
}

void snapToModel(const SleepSnap &s, Model &m, int64_t epochNowMs, Ms now) {
  m.nSessions = s.n <= kMaxSessions ? s.n : kMaxSessions;
  for (int k = 0; k < m.nSessions; k++) {
    const SnapSession &y = s.s[k];
    Session &x = m.sessions[k];
    memset(&x, 0, sizeof x);
    scopy(x.name, y.name);
    x.state = y.state < static_cast<uint8_t>(SessionState::Count) ? static_cast<SessionState>(y.state) : SessionState::Idle;
    scopy(x.summary, y.summary);
    x.hasAttnAt = y.attnEpochMs != 0;
    x.attnAt = x.hasAttnAt ? now - (epochNowMs - y.attnEpochMs) : 0;
    x.since = now;
  }
  m.sys.pcOff = s.pcOff;
  if (s.pcOff) {
    m.sys.pc = false;
    m.sys.offAt = s.offEpochMs ? now - (epochNowMs - s.offEpochMs) : now;
    m.sys.lastPc = m.sys.offAt;
  }
}

uint8_t wifiPowerSave(bool calibMax, bool idle, bool ble) {
  if (calibMax) return ble ? 1 : 0;  // fase 1 della calibrazione: massimo consumo
  return idle ? 2 : 1;
}


}  // namespace cal
