#include "crash.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "strbuf.h"

namespace cal {
namespace crash {

bool tailValid(const Tail &t) {
  if (t.magic != kTailMagic || t.head >= kTailCap || t.used > kTailCap) return false;
  return memchr(t.fw, 0, sizeof t.fw) != nullptr;
}

void tailReset(Tail &t, const char *fw) {
  memset(&t, 0, sizeof t);
  t.magic = kTailMagic;
  scopy(t.fw, fw ? fw : "");
}

void tailAppend(Tail &t, const char *line, size_t n) {
  if (!tailValid(t)) return;
  for (size_t i = 0; i <= n; i++) {
    t.buf[t.head] = i < n ? (line[i] == '\n' ? ' ' : line[i]) : '\n';
    t.head = uint16_t((t.head + 1) % kTailCap);
    if (t.used < kTailCap) t.used++;
  }
}

int tailLines(const Tail &t, char (*lines)[160], int maxLines) {
  if (!tailValid(t) || maxLines <= 0) return 0;
  if (maxLines > kLastMax) maxLines = kLastMax;
  // Coda in ordine dalla più vecchia; se ha fatto il giro, il primo pezzo (prima del primo '\n') è tagliato.
  const size_t start = (t.head + kTailCap - t.used) % kTailCap;
  size_t from[kLastMax], to[kLastMax];
  int n = 0, next = 0;
  size_t seg = 0;
  bool skip = t.used == kTailCap;
  for (size_t i = 0; i < t.used; i++) {
    if (t.buf[(start + i) % kTailCap] != '\n') continue;
    if (!skip) {  // anello delle ultime maxLines righe
      from[next] = seg;
      to[next] = i;
      next = (next + 1) % maxLines;
      if (n < maxLines) n++;
    }
    skip = false;
    seg = i + 1;
  }
  for (int c = 0; c < n; c++) {
    const int k = (next - n + c + maxLines) % maxLines;
    size_t m = 0;
    for (size_t i = from[k]; i < to[k] && m < 159; i++) lines[c][m++] = t.buf[(start + i) % kTailCap];
    lines[c][m] = 0;
  }
  return n;
}

const char *resetKey(Reset r) {
  switch (r) {
    case Reset::Panic: return "ESP_RST_PANIC";
    case Reset::IntWdt: return "ESP_RST_INT_WDT";
    case Reset::TaskWdt: return "ESP_RST_TASK_WDT";
    case Reset::Wdt: return "ESP_RST_WDT";
    case Reset::Brownout: return "ESP_RST_BROWNOUT";
    default: return nullptr;
  }
}

const char *excName(uint32_t c) {
  switch (c) {
    case 0: return "IllegalInstruction";
    case 2: return "InstructionFetchError";
    case 3: return "LoadStoreError";
    case 6: return "IntegerDivideByZero";
    case 9: return "LoadStoreAlignment";
    case 20: return "InstFetchProhibited";
    case 28: return "LoadProhibited";
    case 29: return "StoreProhibited";
    default: return nullptr;
  }
}

namespace {
// Righe più recenti unite da '\n' entro kLastBytes; una sola riga troppo lunga si taglia (confine UTF-8).
void joinLast(char (*lines)[160], int n, char *out) {
  out[0] = 0;
  if (n <= 0) return;
  int from = n - 1;
  size_t total = strlen(lines[n - 1]);
  while (from > 0 && total + 1 + strlen(lines[from - 1]) <= kLastBytes) total += 1 + strlen(lines[--from]);
  Sb b(out, kLastBytes + 1);
  for (int i = from; i < n; i++) {
    if (i > from) b.ch('\n');
    b.s(lines[i]);
  }
}
}  // namespace

bool build(Reset r, const Summary &core, const Tail *tail, const char *fwNow, const char *elfNow, Report &out) {
  memset(&out, 0, sizeof out);
  const char *key = resetKey(r);
  if (!key && !core.has) return false;  // riavvio normale e nessun coredump
  const bool tailOk = tail && tailValid(*tail);
  // La versione che è caduta: quella che ha scritto la coda (può essere la nuova, tornata indietro col rollback).
  scopy(out.fw, tailOk && key && tail->fw[0] ? tail->fw : fwNow);
  if (elfNow && strcmp(out.fw, fwNow) == 0) scopy(out.elf, elfNow);
  if (core.has) {
    const char *e = excName(core.cause);
    if (core.cause != 0xFFFFFFFFu) snprintf(out.exc, sizeof out.exc, "EXCCAUSE %lu", (unsigned long)core.cause);
    scopy(out.reason, e ? e : out.exc[0] ? out.exc : key ? key : "panic");
    scopy(out.task, core.task);
    out.hasPc = true;
    out.pc = core.pc;
    out.nBt = core.nBt < kBtMax ? core.nBt : kBtMax;
    for (int i = 0; i < out.nBt; i++) out.bt[i] = core.bt[i];
  } else {
    scopy(out.reason, key);
  }
  // Le righe RTC valgono solo se il riavvio di adesso è il crash (altrimenti sono di un avvio normale).
  if (tailOk && key) {
    char lines[kLastMax][160];
    joinLast(lines, tailLines(*tail, lines, kLastMax), out.last);
  }
  return true;
}

size_t encode(const Report &r, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "crash";
  d["v"] = 1;
  d["fw"] = r.fw;
  d["reason"] = r.reason;
  if (r.exc[0]) d["exc"] = r.exc;
  if (r.task[0]) d["task"] = r.task;
  char hex[12];
  if (r.hasPc) {
    snprintf(hex, sizeof hex, "0x%08lx", (unsigned long)r.pc);
    d["pc"] = hex;
  }
  if (r.nBt) {
    JsonArray a = d["backtrace"].to<JsonArray>();
    for (int i = 0; i < r.nBt; i++) {
      snprintf(hex, sizeof hex, "0x%08lx", (unsigned long)r.bt[i]);
      a.add(hex);
    }
  }
  if (r.last[0]) d["last"] = r.last;
  if (r.elf[0]) d["elf"] = r.elf;
  const size_t n = serializeJson(d, out, cap);
  return n && n < cap ? n : 0;
}

}  // namespace crash
}  // namespace cal
