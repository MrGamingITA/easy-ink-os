#include "wake.h"

#include <stddef.h>
#include <string.h>

#include "clock.h"

namespace cal {

const char *wakeKindName(WakeKind k) {
  static const char *const n[] = {"nessuno", "ora", "Wi-Fi"};
  return n[static_cast<int>(k)];
}

WakePlan planWake(const WakeIn &in) {
  WakePlan p;
  memset(&p, 0, sizeof p);
  if (in.reason == SleepReason::Battery) return p;  // solo il tasto
  p.kind = WakeKind::Full;
  const bool clock = in.reason == SleepReason::Rest && in.wallOk;
  if (!clock) {
    // Come prima: Wi-Fi e ricerca del PC ogni 15 minuti; nel Riposo la fine della giornata, se viene prima.
    p.sleepMs = kTimerWakeMs;
    if (in.reason == SleepReason::Rest && in.wallOk && in.workOn) {
      const int64_t d = in.workEndMs - in.epochMs;
      if (d < p.sleepMs) { p.sleepMs = d < kMinSleepMs ? kMinSleepMs : d; p.workEnd = true; }
    }
    p.atMs = in.wallOk ? in.epochMs + p.sleepMs : 0;
    p.wifiDueMs = p.atMs ? p.atMs + kTimerWakeMs : 0;
    return p;
  }
  // Riposo con l'ora vera: al prossimo cambio di minuto (o di 5 minuti), più il margine. I confini dei minuti in epoch
  // e in ora locale coincidono: i fusi hanno scarti di minuti interi (e multipli di 5).
  const Ms period = in.saver ? kClockWakeSaveMs : kClockWakeMs;
  int64_t at = (floorDiv(in.epochMs, period) + 1) * period + kClockMarginMs;
  if (at - in.epochMs < kMinSleepMs) at = in.epochMs + kMinSleepMs;
  const int64_t due = in.wifiDueMs > 0 ? in.wifiDueMs : in.epochMs + kTimerWakeMs;
  p.kind = due <= at + period / 2 ? WakeKind::Full : WakeKind::Clock;  // il primo cambio di minuto dopo i 15 minuti
  // Fine della giornata prima del prossimo risveglio dell'ora: sveglia esatta (più il margine), pieno.
  if (in.workOn && in.workEndMs > in.epochMs && in.workEndMs + kClockMarginMs <= at) {
    at = in.workEndMs + kClockMarginMs;
    if (at - in.epochMs < kMinSleepMs) at = in.epochMs + kMinSleepMs;
    p.kind = WakeKind::Full;
    p.workEnd = true;
  }
  p.atMs = at;
  p.sleepMs = at - in.epochMs;
  p.wifiDueMs = p.kind == WakeKind::Full ? at + kTimerWakeMs : due;
  return p;
}

// ---------- somme ----------

namespace {
uint32_t fnv32(const void *p, size_t n) {
  const uint8_t *d = static_cast<const uint8_t *>(p);
  uint32_t h = 2166136261u;
  for (size_t k = 0; k < n; k++) { h ^= d[k]; h *= 16777619u; }
  return h;
}
}  // namespace

void wallKeepSave(WallKeep &k, int64_t epochMs, int64_t rtcUs) {
  memset(&k, 0, sizeof k);
  k.magic = kWallKeepMagic;
  k.epochMs = epochMs;
  k.rtcUs = rtcUs;
  k.sum = fnv32(&k, offsetof(WallKeep, sum));
}

bool wallKeepRestore(const WallKeep &k, int64_t rtcNowUs, int64_t &epochMs) {
  if (k.magic != kWallKeepMagic || k.sum != fnv32(&k, offsetof(WallKeep, sum))) return false;
  if (k.epochMs <= 0 || rtcNowUs < k.rtcUs) return false;  // RTC ripartito: spegnimento, non sonno
  epochMs = k.epochMs + (rtcNowUs - k.rtcUs) / 1000;
  return true;
}

void restSeal(RestKeep &k) {
  k.magic = kRestKeepMagic;
  k.sum = fnv32(&k, offsetof(RestKeep, sum));
}

bool restValid(const RestKeep &k) {
  return k.magic == kRestKeepMagic && k.sum == fnv32(&k, offsetof(RestKeep, sum)) &&
         k.nextKind <= static_cast<uint8_t>(WakeKind::Full);
}

bool clockWakeOk(const RestKeep &k, bool snapOk, SleepReason slept) {
  return snapOk && slept == SleepReason::Rest && restValid(k) && k.imgOk && k.rest &&
         k.nextKind == static_cast<uint8_t>(WakeKind::Clock);
}

bool wakePartialOk(const RestKeep &k, bool fromSleep) { return fromSleep && restValid(k) && k.imgOk; }

void restStateFrom(RestKeep &k, const Model &m) {
  k.invert = m.set.invert;
  k.rot = m.set.rot;
  k.saver = powerSave(m);
  const StoredWork w = storedWork(m);
  k.workStart = w.start;
  k.workEnd = w.end;
  k.workOn = w.on;
  k.workEndLocal = w.endLocal;
  k.workFromLocal = w.fromLocal;
  k.workDoneUntilLocal = w.doneUntilLocal;
}

void restModel(Model &m, const RestKeep &k, const SleepSnap &s, int64_t epochMs, Ms now) {
  m.sys.wallOk = true;
  m.sys.usb = false;
  m.set.invert = k.invert != 0;
  m.set.rot = k.rot != 0;
  m.set.save = k.saver != 0;
  snapToModel(s, m, epochMs, now);
  StoredWork w;
  w.start = k.workStart;
  w.end = k.workEnd;
  w.on = k.workOn != 0;
  w.endLocal = k.workEndLocal;
  w.fromLocal = k.workFromLocal;
  w.doneUntilLocal = k.workDoneUntilLocal;
  restoreWork(m, w);
  resetStack(m, makeScreen(ScreenId::Home));
  pushScreen(m, makeScreen(ScreenId::Sleep));
}

}  // namespace cal
