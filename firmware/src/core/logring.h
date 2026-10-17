// Registro da remoto (docs/OTA.md §5): anello in RAM con le ultime ~16 KB di righe di registro, comprese quelle
// di quando la seriale è spenta (senza cavo la TX resta staccata, core/usb.h), e la trasmissione al servizio:
//   log-start {id, follow, minutes ≤ 60}   servizio → display, solo da connessione autenticata
//   log-stop  {id}
//   log       {id, lines: [...], end?}     display → servizio, ogni messaggio ≤ 8 KB e ≤ 400 righe
// Prima il contenuto dell'anello, con end: true nell'ultimo messaggio (anello vuoto: lines [] ed end); con follow
// poi le righe nuove raggruppate ogni ~1 s, fino a log-stop o alla scadenza (lì si smette e basta).
// Forme di docs/PROTOCOLLO.md («OTA») e dello schema (log, logStart, logStop).
// Logica pura (test_logring). Il lucchetto fra i task e l'aggancio a Serial stanno in hw/logtee.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "model.h"

namespace cal {

class LogRing {
 public:
  static const size_t kCap = 16384;   // OTA.md §5: ~16 KB
  static const size_t kLineMax = 200; // una riga più lunga si tronca (al confine di un carattere UTF-8)
  typedef void (*LineHook)(const char *line, size_t n, void *ctx);

  LogRing() { clear(); }
  void clear();
  // 0.12.2-gb, modalità gioco (rete spenta, il registro da remoto non può uscire): il buffer si presta alla partita
  // come RAM della cartuccia. lend: anello svuotato (i numeri delle righe vanno avanti: uno stream vede «righe
  // perse») e puntatore all'area interna se n ≤ kCap, altrimenti (o se già prestato) nullptr. Mentre è prestato le
  // righe vanno solo al gancio (coda RTC dei crash), mai nel buffer. giveBack: anello di nuovo vuoto e in uso.
  uint8_t *lend(size_t n);
  void giveBack();
  bool lent() const { return lent_; }
  void setHook(LineHook fn, void *ctx) { hook_ = fn; hookCtx_ = ctx; }
  // Testo come arriva da Serial.print*: anche pezzi di riga. Ogni riga completa (fino a '\n'; '\r' si scarta)
  // entra con il prefisso «<secondi>.<millesimi> » dell'istante del suo primo byte (uptime).
  void write(const char *d, size_t n, uint32_t uptimeMs);
  // Riga già completa (senza '\n'), senza prefisso.
  void pushLine(const char *line, size_t n);

  uint32_t firstSeq() const { return firstSeq_; }  // numero della riga più vecchia ancora nell'anello
  uint32_t nextSeq() const { return nextSeq_; }    // numero della prossima riga
  size_t used() const { return used_; }
  // Visita le righe con numero ≥ from, dalla più vecchia; fn restituisce false per fermarsi.
  template <typename F>
  void each(uint32_t from, F fn) const;

 private:
  void dropOldest();
  void endLine();

  alignas(4) char buf_[kCap];
  bool lent_ = false;
  size_t start_, used_;
  uint32_t firstSeq_, nextSeq_;
  char part_[kLineMax + 16];  // riga in costruzione (con il prefisso)
  size_t partLen_;
  bool partTrunc_;
  LineHook hook_ = nullptr;
  void *hookCtx_ = nullptr;
};

template <typename F>
void LogRing::each(uint32_t from, F fn) const {
  if (lent_) return;  // il buffer è della partita
  char line[kLineMax + 16];
  size_t n = 0;
  uint32_t seq = firstSeq_;
  for (size_t i = 0; i < used_; i++) {
    const char c = buf_[(start_ + i) % kCap];
    if (c != '\n') { if (n < sizeof line - 1) line[n++] = c; continue; }
    line[n] = 0;
    if (seq >= from && !fn(seq, line, n)) return;
    seq++;
    n = 0;
  }
}

class LogStream {
 public:
  static const size_t kMsgMax = 8192;   // OTA.md §5: ≤ 8 KB per messaggio
  static const int kLinesMax = 400;     // schema: log.lines maxItems
  static const Ms kFollowEveryMs = 1000;
  static const int kMinutesDefault = 10, kMinutesMax = 60;

  bool active() const { return st_ != St::Idle; }
  const char *id() const { return id_; }
  // log-start: false (e why) se non vale. Uno nuovo prende il posto di quello in corso (senza fine).
  bool start(JsonVariantConst root, bool authed, Ms now, const LogRing &ring, char *why, size_t cap);
  // log-stop con lo stesso id: si smette (niente altro da mandare). false se l'id non è quello in corso.
  bool stop(JsonVariantConst root, bool authed);
  // Connessione chiusa: si smette senza mandare niente.
  void drop() { st_ = St::Idle; }
  // Prossimo messaggio da mandare adesso (0 = niente). Uno per chiamata.
  size_t poll(Ms now, const LogRing &ring, char *out, size_t cap);

 private:
  enum class St : uint8_t { Idle, Dump, Follow };
  size_t chunk(const LogRing &ring, uint32_t until, bool endOfDump, char *out, size_t cap);
  St st_ = St::Idle;
  char id_[33] = "";
  bool follow_ = false;
  uint32_t cursor_ = 0, dumpUntil_ = 0;
  Ms until_ = 0, lastSend_ = 0;
};

// JSON di `log` costruito a mano (per stare sotto kMsgMax senza serializzare due volte). Esposto per i test.
size_t jsonEscape(const char *s, size_t n, char *out, size_t cap);

}  // namespace cal
