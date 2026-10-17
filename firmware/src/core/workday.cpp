#include "workday.h"

#include <string.h>

#include "strbuf.h"

namespace cal {

const char *const kWorkPastError = "orario di fine già passato";
const char *const kWorkNoClock = "ora non ancora nota";

bool parseHm(const char *s, int &mins) {
  if (!s || strlen(s) != 5 || s[2] != ':') return false;
  static const int kDigits[4] = {0, 1, 3, 4};
  for (int k : kDigits)
    if (s[k] < '0' || s[k] > '9') return false;
  const int h = (s[0] - '0') * 10 + (s[1] - '0'), mi = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || mi > 59) return false;
  mins = h * 60 + mi;
  return true;
}

void minHm(int mins, char *out, size_t cap) {
  if (mins < 0) mins = 0;
  const int h = mins / 60, mi = mins % 60;
  Sb b(out, cap);
  if (h < 10) b.s("0");
  b.i(h).s(":");
  if (mi < 10) b.s("0");
  b.i(mi);
}

bool workTimesOk(int start, int end) { return start >= 0 && end < 24 * 60 && start < end; }

int64_t dayAtLocal(int64_t localMs, int mins) { return localMs - floorMod(localMs, kDayMs) + int64_t(mins) * 60000; }
int64_t nextMidnightLocal(int64_t localMs) { return localMs - floorMod(localMs, kDayMs) + kDayMs; }
bool sameDayLocal(int64_t a, int64_t b) { return floorDiv(a, kDayMs) == floorDiv(b, kDayMs); }

int workLeftMin(const Model &m, const WallClock &wc) {
  const int64_t d = m.work.endLocal - wc.localMs;
  if (d <= 0) return 0;
  return static_cast<int>((d + 59999) / 60000);  // Math.ceil(d / 60000)
}

void workLeftText(const Model &m, const WallClock &wc, bool up, char *out, size_t cap) {
  const int n = workLeftMin(m, wc);
  Sb b(out, cap);
  if (n >= 60) {
    b.i(n / 60).s(up ? " H " : " h ");
    if (n % 60 < 10) b.s("0");
    b.i(n % 60);
  } else {
    b.i(n).s(up ? " MIN" : " min");
  }
}

bool workDone(const Model &m, const WallClock &wc) { return m.work.doneUntilLocal > wc.localMs; }

void workLine(const Model &m, const WallClock &wc, char *out, size_t cap) {
  if (m.work.on) workLeftText(m, wc, false, out, cap);
  else if (workDone(m, wc)) scopy(out, cap, "finita");
  else if (cap) out[0] = 0;
}

double workFrac(const Model &m, const WallClock &wc) {
  const Workday &w = m.work;
  int64_t a = dayAtLocal(w.endLocal, m.set.workStart);
  if (a >= w.endLocal) a = w.fromLocal;
  if (w.endLocal <= a) return 1;
  const double f = double(wc.localMs - a) / double(w.endLocal - a);
  return f < 0 ? 0 : f > 1 ? 1 : f;
}

bool workParty(const Model &m, Ms now) { return m.work.party > now; }

WorkStep workStep(const Model &m, const WallClock &wc) {
  const Workday &w = m.work;
  if (w.on && !sameDayLocal(wc.localMs, w.endLocal)) return WorkStep::Midnight;
  if (w.on && wc.localMs >= w.endLocal) return WorkStep::Finish;
  if (w.doneUntilLocal && wc.localMs >= w.doneUntilLocal) return WorkStep::DoneOver;
  return WorkStep::None;
}

bool workFinish(Model &m, const WallClock &wc) {
  Workday &w = m.work;
  w.on = false;
  const int64_t a = w.endLocal + kWorkDoneMs, b = nextMidnightLocal(w.endLocal);
  w.doneUntilLocal = a < b ? a : b;
  return wc.localMs < w.doneUntilLocal;
}

const char *workStart(Model &m, const WallClock &wc, bool &alreadyOn) {
  Workday &w = m.work;
  alreadyOn = w.on;
  if (w.on) return nullptr;
  if (!m.sys.wallOk) return kWorkNoClock;
  const int64_t end = dayAtLocal(wc.localMs, m.set.workEnd);
  if (wc.localMs >= end) return kWorkPastError;
  w.on = true;
  w.endLocal = end;
  w.fromLocal = wc.localMs;
  w.doneUntilLocal = 0;
  w.party = 0;
  return nullptr;
}

bool workStop(Model &m) {
  Workday &w = m.work;
  if (!w.on) return false;
  w.on = false;
  w.doneUntilLocal = 0;
  w.party = 0;
  return true;
}

Ms workSleepMs(const Model &m, const WallClock &wc, Ms def) {
  if (!m.work.on || !m.sys.wallOk) return def;
  const int64_t d = m.work.endLocal - wc.localMs;
  if (d < 1000) return 1000;
  return d < def ? d : def;
}

int64_t workEndEpoch(const Model &m, const WallClock &wc, int64_t epochNow) {
  if (!m.work.on) return -1;
  return epochNow + (m.work.endLocal - wc.localMs);
}

// ---------- NVS ----------

namespace {
uint8_t sum8(const uint8_t *d, size_t n) {
  uint8_t s = 0x3C;
  for (size_t k = 0; k < n; k++) s = static_cast<uint8_t>((s << 1 | s >> 7) ^ d[k]);
  return s;
}
void put16(uint8_t *p, int v) { p[0] = uint8_t(v & 0xFF); p[1] = uint8_t((v >> 8) & 0xFF); }
int get16(const uint8_t *p) { return int16_t(p[0] | p[1] << 8); }
void put64(uint8_t *p, int64_t v) { for (int k = 0; k < 8; k++) p[k] = uint8_t(uint64_t(v) >> (8 * k)); }
int64_t get64(const uint8_t *p) {
  uint64_t v = 0;
  for (int k = 0; k < 8; k++) v |= uint64_t(p[k]) << (8 * k);
  return int64_t(v);
}
}  // namespace

StoredWork storedWork(const Model &m) {
  StoredWork w;
  w.start = m.set.workStart;
  w.end = m.set.workEnd;
  w.on = m.work.on;
  w.endLocal = m.work.on ? m.work.endLocal : 0;
  w.fromLocal = m.work.on ? m.work.fromLocal : 0;
  w.doneUntilLocal = m.work.doneUntilLocal;
  return w;
}

// [0] versione 1 · [1..2] inizio · [3..4] fine (minuti, i16 LE) · [5] bandiere (1 = attiva) · [6..13] fine della
// giornata · [14..21] attivazione · [22..29] «GIORNATA FINITA» fino a (ora locale ms, i64 LE) · [30] riservato · [31] somma
size_t packWork(const StoredWork &w, uint8_t *out, size_t cap) {
  if (cap < kWorkBlobLen) return 0;
  memset(out, 0, kWorkBlobLen);
  out[0] = 1;
  put16(out + 1, w.start);
  put16(out + 3, w.end);
  out[5] = w.on ? 1 : 0;
  put64(out + 6, w.endLocal);
  put64(out + 14, w.fromLocal);
  put64(out + 22, w.doneUntilLocal);
  out[31] = sum8(out, 31);
  return kWorkBlobLen;
}

bool unpackWork(const uint8_t *in, size_t n, StoredWork &w) {
  if (!in || n != kWorkBlobLen || in[0] != 1 || in[31] != sum8(in, 31)) return false;
  StoredWork t;
  t.start = int16_t(get16(in + 1));
  t.end = int16_t(get16(in + 3));
  if (!workTimesOk(t.start, t.end)) return false;
  t.on = in[5] & 1;
  t.endLocal = get64(in + 6);
  t.fromLocal = get64(in + 14);
  t.doneUntilLocal = get64(in + 22);
  w = t;
  return true;
}

void restoreWork(Model &m, const StoredWork &w) {
  m.set.workStart = w.start;
  m.set.workEnd = w.end;
  m.work.on = w.on;
  m.work.endLocal = w.endLocal;
  m.work.fromLocal = w.fromLocal;
  m.work.doneUntilLocal = w.doneUntilLocal;
  m.work.party = 0;
}

}  // namespace cal
