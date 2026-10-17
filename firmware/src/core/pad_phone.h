// Telefono come controller (spec app iPhone §2.1): report v1 del servizio «Calamaio pad» e mantenimento.
// Parte pura, provata in env:native (test_pad_phone) contro protocol/vectors/pad-report-v1.json.
//   0 ver = 1 · 1 seq · 2-3 buttons u16 LE (bit di pad::Button) · 4 lx i8 · 5 ly i8 · 6 flags (bit0 primo piano) · 7 rsv
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pad.h"

namespace cal {
namespace pad {

const size_t kPhoneReportLen = 8;
const uint8_t kPhoneReportVer = 1;
const uint8_t kPhoneFlagForeground = 0x01;

enum class PhoneParse : uint8_t { Ok, BadLen, BadVer };
const char *phoneParseName(PhoneParse p);  // 'ok', 'lunghezza', 'versione' (come «reason» dei vettori)

struct PhoneMeta {
  uint8_t seq;  // cresce a ogni report: solo per il registro, mai per scartare (gira a 255)
  bool fg;      // app in primo piano
};

// Lunghezza ≠ 8 o versione ≠ 1: scartato, out e meta intatti. Levette limitate a ±100, rsv e bit sconosciuti di
// flags ignorati; app in secondo piano = tutto rilasciato. rx, ry, lt, rt sempre 0; src = Phone.
PhoneParse parsePhoneReport(const uint8_t *r, size_t n, State &out, PhoneMeta &meta);

// Mantenimento: l'app manda un report ogni 1 s; 3 s senza = tutto rilasciato (spec §2.1).
class Keepalive {
 public:
  static const uint32_t kMs = 3000;
  static const uint32_t kFreshMs = 0;  // l'orologio del tenuto non supera mai l'ultimo report
  void feed(uint32_t at) { at_ = at; on_ = true; }  // ora d'ARRIVO di un report valido
  void stop() { on_ = false; }                      // Xbox, scollegato, già rilasciato
  bool on() const { return on_; }
  bool due(uint32_t now) const { return on_ && now - at_ >= kMs; }
  // Orologio per MenuMapper::tick: mai oltre l'ultimo report (kFreshMs = 0). Un HoldFire richiede quindi un report
  // arrivato a 3 s o più dalla pressione, cioè un telefono vivo in quel momento; sparito, il tenuto si chiude con
  // HoldUp al rilascio. L'app manda un report ogni 250 ms con un tasto giù (ogni 1 s altrimenti): il HoldFire
  // arriva al più 250 ms dopo i 3 s.
  uint32_t cap(uint32_t now) const { return on_ && now - at_ >= kFreshMs ? at_ + kFreshMs : now; }

 private:
  uint32_t at_ = 0;
  bool on_ = false;
};

// Se è ora: spegne k e, se last aveva qualcosa premuto, scrive in out lo stato vuoto (src Phone) con l'istante
// d'arrivo at = ultimo report (cap); true solo in quel caso.
bool keepaliveRelease(Keepalive &k, const State &last, uint32_t now, State &out, uint32_t &at);

// Stato da hw/blepad::poll verso il MenuMapper (main.cpp): release = blepad::polledRelease(), cioè il «tutto
// rilasciato» del mantenimento. Quello si chiude con reset (solo HoldUp, mai Select: un tocco con il rilascio perso
// non sceglie una voce); un report vero va a update con l'ora d'arrivo. Ai menu arriva solo quest'uscita.
inline int mapPolled(MenuMapper &m, const State &s, uint32_t at, bool release, MenuEvent *out, int cap) {
  return release ? m.reset(out, cap) : m.update(s, at, out, cap);
}

// Revisione finale I2: il report di secondo piano (flags bit0 = 0) è un rilascio forzato come il mantenimento:
// blepad::poll lo segna in polledRelease, così va ai menu come reset (mai Select né HoldFire).
inline bool phoneForcedRelease(const PhoneMeta &m) { return !m.fg; }

// F7 (0.14.0): un telefono in secondo piano o sparito non tiene occupata la scheda. iOS non chiude il collegamento
// quando l'app va in secondo piano: la scheda lo scollega dopo kSilentMs senza report validi, o kBackgroundMs dopo
// un report di secondo piano (flags bit0 = 0) senza report di primo piano dopo, e torna a cercare (l'Xbox).
class PhoneIdle {
 public:
  static const uint32_t kSilentMs = 10000, kBackgroundMs = 5000;
  void start(uint32_t now) { last_ = now; bg_ = false; on_ = true; }  // telefono collegato
  void stop() { on_ = false; }
  void report(uint32_t now, bool fg) { last_ = now; bg_ = !fg; }  // report valido
  bool due(uint32_t now) const { return on_ && now - last_ >= (bg_ ? kBackgroundMs : kSilentMs); }

 private:
  uint32_t last_ = 0;
  bool bg_ = false, on_ = false;
};

}  // namespace pad
}  // namespace cal
