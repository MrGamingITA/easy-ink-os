#include "netfmt.h"

#include <string.h>

#include "strbuf.h"

namespace cal {

const char *const kApAlphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";  // 31 simboli

void boardIdFromMac(const uint8_t mac[6], char *out, size_t cap) {
  static const char kHex[] = "0123456789abcdef";
  char b[5] = {kHex[mac[4] >> 4], kHex[mac[4] & 15], kHex[mac[5] >> 4], kHex[mac[5] & 15], 0};
  scopy(out, cap, b);
}

void apSsid(const char *boardId, char *out, size_t cap) {
  Sb b(out, cap);
  b.s("Calamaio-");
  for (const char *p = boardId; *p; p++) b.ch(*p >= 'a' && *p <= 'z' ? char(*p - 32) : *p);
}

void mdnsHost(const char *boardId, char *out, size_t cap) { Sb(out, cap).s("calamaio-").s(boardId); }

static void escaped(Sb &b, const char *s) {
  for (; *s; s++) {
    if (*s == '\\' || *s == ';' || *s == ',' || *s == ':' || *s == '"') b.ch('\\');
    b.ch(*s);
  }
}

void wifiQrText(const char *ssid, const char *pass, char *out, size_t cap) {
  Sb b(out, cap);
  b.s("WIFI:T:WPA;S:");
  escaped(b, ssid);
  b.s(";P:");
  escaped(b, pass);
  b.s(";;");
}

void makeApPassword(char *out, size_t cap, Rand32 rnd, void *ctx) {
  const uint32_t n = static_cast<uint32_t>(strlen(kApAlphabet));
  const uint32_t limit = 0xFFFFFFFFu - (0xFFFFFFFFu % n);  // scarto: ogni simbolo con la stessa probabilità
  size_t k = 0;
  while (k < static_cast<size_t>(kApPassChars) && k + 1 < cap) {
    const uint32_t r = rnd(ctx);
    if (r >= limit) continue;
    out[k++] = kApAlphabet[r % n];
  }
  if (cap) out[k] = 0;
}

bool mdnsAnswerValid(const char *ip, uint16_t port) {
  if (!ip || !port) return false;
  long part[4];
  int n = 0;
  const char *p = ip;
  while (n < 4) {
    if (*p < '0' || *p > '9') return false;
    long v = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      v = v * 10 + (*p++ - '0');
      if (++digits > 3) return false;
    }
    if (v > 255) return false;
    part[n++] = v;
    if (n < 4) {
      if (*p != '.') return false;
      p++;
    }
  }
  if (*p) return false;
  const bool zero = !part[0] && !part[1] && !part[2] && !part[3];
  const bool bcast = part[0] == 255 && part[1] == 255 && part[2] == 255 && part[3] == 255;
  return !zero && !bcast;
}

}  // namespace cal
