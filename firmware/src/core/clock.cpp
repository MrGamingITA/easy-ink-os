#include "clock.h"

#include "strbuf.h"

namespace cal {

const char *const kWeekdays[7] = {"domenica", "lunedì", "martedì", "mercoledì", "giovedì", "venerdì", "sabato"};
const char *const kMonths[12] = {"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno",
                                 "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre"};

static const int64_t kDayMs = 86400000;

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}
int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }
int64_t roundDiv(int64_t a, int64_t b) { return floorDiv(2 * a + b, 2 * b); }

// Giorni dal 1970-01-01 per una data civile (algoritmo di H. Hinnant).
static int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = floorDiv(y, 400);
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

WallClock wallFromCivil(int year, int month, int day, int hour, int minute, int second) {
  WallClock wc;
  wc.localMs = daysFromCivil(year, month, day) * kDayMs + (hour * 3600LL + minute * 60LL + second) * 1000LL;
  return wc;
}

Civil civilFrom(int64_t localMs) {
  Civil c;
  int64_t days = floorDiv(localMs, kDayMs), ms = floorMod(localMs, kDayMs);
  c.hour = static_cast<int>(ms / 3600000);
  c.minute = static_cast<int>(ms / 60000 % 60);
  c.second = static_cast<int>(ms / 1000 % 60);
  c.weekday = static_cast<int>(floorMod(days + 4, 7));  // 1970-01-01 era giovedì
  int64_t z = days + 719468;
  const int64_t era = floorDiv(z, 146097);
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  c.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  c.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  c.year = static_cast<int>(yoe + era * 400 + (c.month <= 2));
  return c;
}

void hmLocal(int64_t localMs, char *out, size_t cap) {
  Civil c = civilFrom(localMs);
  Sb(out, cap).pad2(c.hour).ch(':').pad2(c.minute);
}

void hmAt(const WallClock &wc, Ms now, Ms t, char *out, size_t cap) { hmLocal(localAt(wc, now, t), out, cap); }

void dateStr(const WallClock &wc, char *out, size_t cap) {
  Civil c = civilFrom(wc.localMs);
  Sb(out, cap).s(kWeekdays[c.weekday]).ch(' ').i(c.day).ch(' ').s(kMonths[c.month - 1]);
}

void resetLabel(const WallClock &wc, Ms now, Ms ts, char *out, size_t cap) {
  int64_t l = localAt(wc, now, ts);
  char h[8];
  hmLocal(l, h, sizeof h);
  Sb b(out, cap);
  if (floorDiv(l, kDayMs) == floorDiv(wc.localMs, kDayMs)) b.s("si azzera alle ").s(h);
  else b.s("si azzera ").s(kWeekdays[civilFrom(l).weekday]).s(" alle ").s(h);
}

}  // namespace cal
