// Tempo nel core: due grandezze separate, entrambe passate dall'esterno.
// - Ms: tempo monotono in millisecondi (il firmware passa esp_timer o millis esteso a 64 bit).
//   Scadenze, attese e durate del modello sono tutte in questa scala.
// - WallClock: ora locale dell'orologio (fuso e ora legale già applicati) all'istante `now`.
//   Serve solo per scrivere ore e date («10:30», «domenica 11 ottobre», «fino alle 8»).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {

typedef int64_t Ms;

struct WallClock {
  int64_t localMs;  // ms dal 1970-01-01 00:00 in ora locale, all'istante `now` della chiamata
};

struct Civil {
  int year, month, day;  // month 1..12
  int hour, minute, second;
  int weekday;           // 0 = domenica, come Date.getDay()
};

WallClock wallFromCivil(int year, int month, int day, int hour, int minute, int second);
Civil civilFrom(int64_t localMs);

// Ora locale dell'istante monotono t, sapendo che all'istante now era wc.localMs.
inline int64_t localAt(const WallClock &wc, Ms now, Ms t) { return wc.localMs + (t - now); }

// Divisione e resto con arrotondamento verso −∞ (come Math.floor in JS).
int64_t floorDiv(int64_t a, int64_t b);
int64_t floorMod(int64_t a, int64_t b);
// Math.round di JS su un rapporto di interi: floor(a / b + 0,5), b > 0.
int64_t roundDiv(int64_t a, int64_t b);

// «HH:MM» dell'ora locale (hm() del simulatore). out almeno 6 byte.
void hmLocal(int64_t localMs, char *out, size_t cap);
// hm(new Date(t)) per un istante monotono t.
void hmAt(const WallClock &wc, Ms now, Ms t, char *out, size_t cap);
// dateStr(): «domenica 11 ottobre» (toLocaleDateString it-IT, weekday/day/month lunghi).
void dateStr(const WallClock &wc, char *out, size_t cap);
// resetLabel(ts) delle pagine Limiti: «si azzera alle 12:40» oggi, altrimenti «si azzera lunedì alle 14:30».
void resetLabel(const WallClock &wc, Ms now, Ms ts, char *out, size_t cap);

extern const char *const kWeekdays[7];
extern const char *const kMonths[12];

}  // namespace cal
