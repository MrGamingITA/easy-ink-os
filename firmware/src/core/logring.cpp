#include "logring.h"

#include <stdio.h>
#include <string.h>

#include "strbuf.h"

namespace cal {

// ---------- LogRing ----------
void LogRing::clear() {
  start_ = used_ = 0;
  firstSeq_ = nextSeq_ = 0;
  partLen_ = 0;
  partTrunc_ = false;
}

uint8_t *LogRing::lend(size_t n) {
  if (lent_ || n > kCap) return nullptr;
  start_ = used_ = 0;
  firstSeq_ = nextSeq_;
  lent_ = true;
  return reinterpret_cast<uint8_t *>(buf_);
}

void LogRing::giveBack() {
  if (!lent_) return;
  start_ = used_ = 0;  // il contenuto è quello della partita: niente da leggere
  firstSeq_ = nextSeq_;
  lent_ = false;
}

void LogRing::dropOldest() {
  while (used_) {
    const char c = buf_[start_];
    start_ = (start_ + 1) % kCap;
    used_--;
    if (c == '\n') break;
  }
  firstSeq_++;
}

void LogRing::pushLine(const char *line, size_t n) {
  if (n > sizeof part_ - 1) n = sizeof part_ - 1;
  if (lent_) {  // anello prestato: solo il gancio (coda RTC)
    if (hook_) hook_(line, n, hookCtx_);
    return;
  }
  while (kCap - used_ < n + 1 && used_) dropOldest();
  for (size_t i = 0; i < n; i++) {
    const char c = line[i] == '\n' ? ' ' : line[i];
    buf_[(start_ + used_++) % kCap] = c;
  }
  buf_[(start_ + used_++) % kCap] = '\n';
  nextSeq_++;
  if (hook_) hook_(line, n, hookCtx_);
}

// Una riga tagliata non deve finire a metà di un carattere UTF-8: si toglie la sequenza incompleta in coda.
static size_t utf8Cut(const char *s, size_t n) {
  size_t k = n;
  int back = 0;
  while (k > 0 && back < 4 && (static_cast<unsigned char>(s[k - 1]) & 0xC0) == 0x80) { k--; back++; }
  if (k == 0) return n;
  const unsigned char lead = static_cast<unsigned char>(s[k - 1]);
  const int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  return need == back + 1 ? n : k - 1 + (need == 1 ? 1 : 0);
}

void LogRing::endLine() {
  size_t n = partLen_;
  if (partTrunc_) n = utf8Cut(part_, n);
  pushLine(part_, n);
  partLen_ = 0;
  partTrunc_ = false;
}

void LogRing::write(const char *d, size_t n, uint32_t uptimeMs) {
  for (size_t i = 0; i < n; i++) {
    const char c = d[i];
    if (c == '\r') continue;
    if (c == '\n') {
      if (!partLen_) {  // riga vuota: prefisso e basta
        partLen_ = size_t(snprintf(part_, sizeof part_, "%lu.%03lu", (unsigned long)(uptimeMs / 1000), (unsigned long)(uptimeMs % 1000)));
      }
      endLine();
      continue;
    }
    if (!partLen_)
      partLen_ = size_t(snprintf(part_, sizeof part_, "%lu.%03lu ", (unsigned long)(uptimeMs / 1000), (unsigned long)(uptimeMs % 1000)));
    if (partLen_ < kLineMax) part_[partLen_++] = c;
    else partTrunc_ = true;
  }
}

// ---------- JSON ----------
size_t jsonEscape(const char *s, size_t n, char *out, size_t cap) {
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    char e[8];
    size_t m = 0;
    if (c == '"' || c == '\\') { e[0] = '\\'; e[1] = char(c); m = 2; }
    else if (c < 0x20 || c == 0x7F) m = size_t(snprintf(e, sizeof e, "\\u%04x", c));
    else { e[0] = char(c); m = 1; }
    if (k + m > cap) return size_t(-1);
    memcpy(out + k, e, m);
    k += m;
  }
  return k;
}

// ---------- LogStream ----------
bool LogStream::start(JsonVariantConst r, bool authed, Ms now, const LogRing &ring, char *why, size_t cap) {
  if (!authed) { scopy(why, cap, "log-start senza autenticazione"); return false; }
  if (ring.lent()) { scopy(why, cap, "registro prestato alla partita"); return false; }
  const char *id = r["id"].is<const char *>() ? r["id"].as<const char *>() : nullptr;
  const size_t n = id ? strlen(id) : 0;
  bool ok = n >= 1 && n <= 32;  // otaId: [A-Za-z0-9_-]{1,32}
  for (size_t k = 0; ok && k < n; k++) {
    const char c = id[k];
    ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  }
  if (!ok) { scopy(why, cap, "log-start con id non valido"); return false; }
  int minutes = kMinutesDefault;
  if (!r["minutes"].isNull()) {
    if (!r["minutes"].is<int>() || r["minutes"].as<int>() < 1 || r["minutes"].as<int>() > kMinutesMax) {
      scopy(why, cap, "log-start: minutes fra 1 e 60");
      return false;
    }
    minutes = r["minutes"].as<int>();
  }
  if (!r["follow"].isNull() && !r["follow"].is<bool>()) { scopy(why, cap, "log-start: follow non booleano"); return false; }
  scopy(id_, id);
  follow_ = r["follow"].is<bool>() && r["follow"].as<bool>();
  cursor_ = ring.firstSeq();
  dumpUntil_ = ring.nextSeq();
  until_ = now + Ms(minutes) * 60000;
  lastSend_ = now;
  st_ = St::Dump;
  return true;
}

bool LogStream::stop(JsonVariantConst r, bool authed) {
  const char *id = r["id"].is<const char *>() ? r["id"].as<const char *>() : nullptr;
  if (!authed || st_ == St::Idle || !id || strcmp(id, id_) != 0) return false;
  st_ = St::Idle;
  return true;
}

size_t LogStream::chunk(const LogRing &ring, uint32_t until, bool endOfDump, char *out, size_t cap) {
  if (cap > kMsgMax + 1) cap = kMsgMax + 1;  // kMsgMax byte di JSON più il terminatore
  Sb head(out, cap);
  head.s("{\"t\":\"log\",\"v\":1,\"id\":\"").s(id_).s("\",\"lines\":[");
  if (head.truncated()) return 0;
  size_t k = head.len();
  const size_t tail = endOfDump ? 13 : 2;  // ],"end":true} oppure ]}
  bool first = true;
  int count = 0;
  auto add = [&](const char *line, size_t n) {
    const size_t at = k + (first ? 0 : 1);
    if (count >= kLinesMax || at + 3 + tail > cap) return false;
    const size_t m = jsonEscape(line, n, out + at + 1, cap - at - 3 - tail);  // due virgolette, la coda e il terminatore
    if (m == size_t(-1)) return false;
    if (!first) out[k] = ',';
    out[at] = '"';
    out[at + 1 + m] = '"';
    k = at + m + 2;
    first = false;
    count++;
    return true;
  };
  if (cursor_ < ring.firstSeq()) {  // l'anello ha fatto il giro prima che le righe partissero
    char t[64];
    const int n = snprintf(t, sizeof t, "… %lu righe perse (anello pieno)", (unsigned long)(ring.firstSeq() - cursor_));
    add(t, size_t(n));
    cursor_ = ring.firstSeq();
  }
  ring.each(cursor_, [&](uint32_t seq, const char *line, size_t n) {
    if (seq >= until || !add(line, n)) return false;
    cursor_ = seq + 1;
    return true;
  });
  // end solo se in questo messaggio è entrato tutto quello che restava del dump
  const bool end = endOfDump && cursor_ >= until;
  memcpy(out + k, end ? "],\"end\":true}" : "]}", end ? 13 : 2);
  k += end ? 13 : 2;
  out[k] = 0;
  return k;
}

size_t LogStream::poll(Ms now, const LogRing &ring, char *out, size_t cap) {
  if (ring.lent()) return 0;  // anello della partita: niente righe (alla restituzione, «righe perse»)
  if (st_ == St::Dump) {
    lastSend_ = now;
    const size_t n = chunk(ring, dumpUntil_, true, out, cap);
    if (cursor_ >= dumpUntil_) st_ = follow_ ? St::Follow : St::Idle;  // l'ultimo del dump porta end
    return n;
  }
  if (st_ == St::Follow) {
    if (now >= until_) { st_ = St::Idle; return 0; }  // scadenza: si smette
    if (now - lastSend_ >= kFollowEveryMs && ring.nextSeq() > cursor_) {
      lastSend_ = now;
      return chunk(ring, ring.nextSeq(), false, out, cap);
    }
  }
  return 0;
}

}  // namespace cal
