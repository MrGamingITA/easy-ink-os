#include "core/pc_fallback.h"

#include <string.h>

#include "core/netfmt.h"
#include "core/strbuf.h"

namespace cal {

namespace {

// Porta in base 10, 1..5 cifre, 1..65535. Si ferma a stop (o alla fine): *end dopo l'ultima cifra.
bool parsePort(const char *p, char stop, uint16_t &port, const char **end) {
  long v = 0;
  int digits = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p++ - '0');
    if (++digits > 5) return false;
  }
  if (!digits || v < 1 || v > 65535) return false;
  if (*p && *p != stop) return false;
  port = uint16_t(v);
  *end = p;
  return true;
}

// «ip:porta» o «ip» in ip (16 byte) e porta (predefinita se manca). rest: dopo la porta (stop) o nullptr.
bool parseIpPort(const char *s, char stop, char ip[16], uint16_t &port, const char **rest) {
  if (!s) return false;
  const char *c = s;
  while (*c && *c != ':' && *c != stop) c++;
  const size_t n = size_t(c - s);
  if (!n || n > 15) return false;
  char tmp[16];
  memcpy(tmp, s, n);
  tmp[n] = 0;
  if (!mdnsAnswerValid(tmp, 1)) return false;  // IPv4 per intero, non 0.0.0.0 né 255.255.255.255
  uint16_t p = kPcDefaultPort;
  const char *end = c;
  if (*c == ':' && !parsePort(c + 1, stop, p, &end)) return false;
  memcpy(ip, tmp, n + 1);
  port = p;
  *rest = end;
  return true;
}

// Forma della NVS: «ip:porta:percorso».
size_t fullText(const PcAddr &a, char *out, size_t cap) {
  if (!a.ip[0]) { if (cap) out[0] = 0; return 0; }
  Sb b(out, cap);
  b.s(a.ip).ch(':').i(a.port).ch(':').s(a.path);
  return b.truncated() ? 0 : b.len();
}

bool parseFull(const char *s, PcAddr &out) {
  PcAddr a;
  memset(&a, 0, sizeof a);
  const char *rest = nullptr;
  if (!parseIpPort(s, ':', a.ip, a.port, &rest) || *rest != ':') return false;
  if (!scopy(a.path, rest + 1) || a.path[0] != '/') return false;
  out = a;
  return true;
}

}  // namespace

bool pcAddrValid(const PcAddr &a) { return mdnsAnswerValid(a.ip, a.port) && a.path[0] == '/'; }

bool pcAddrSame(const PcAddr &a, const PcAddr &b) {
  return !strcmp(a.ip, b.ip) && a.port == b.port && !strcmp(a.path, b.path);
}

bool parsePcHost(const char *s, const char *defPath, PcAddr &out) {
  PcAddr a;
  memset(&a, 0, sizeof a);
  const char *rest = nullptr;
  if (!parseIpPort(s, 0, a.ip, a.port, &rest) || *rest) return false;
  if (!defPath || defPath[0] != '/' || !scopy(a.path, defPath)) return false;
  out = a;
  return true;
}

size_t pcHostText(const PcAddr &a, char *out, size_t cap) {
  if (!a.ip[0]) { if (cap) out[0] = 0; return 0; }
  Sb b(out, cap);
  b.s(a.ip);
  if (a.port != kPcDefaultPort) b.ch(':').i(a.port);
  return b.truncated() ? 0 : b.len();
}

size_t pcAddrText(const PcAddr &a, char *out, size_t cap) {
  if (!a.ip[0]) { if (cap) out[0] = 0; return 0; }
  Sb b(out, cap);
  b.s(a.ip).ch(':').i(a.port);
  return b.truncated() ? 0 : b.len();
}

PcSrc pickFallback(bool hasManual, bool hasLast, PcSrc prev) {
  if (hasManual && hasLast) return prev == PcSrc::Manual ? PcSrc::Last : PcSrc::Manual;
  if (hasManual) return PcSrc::Manual;
  if (hasLast) return PcSrc::Last;
  return PcSrc::None;
}

// ---------- tabella ----------
int PcMemory::index(const char *ssid) const {
  if (!ssid || !ssid[0]) return -1;
  for (int i = 0; i < n_; i++)
    if (!strcmp(e_[i].ssid, ssid)) return i;
  return -1;
}

const PcMemEntry *PcMemory::find(const char *ssid) const {
  const int i = index(ssid);
  return i < 0 ? nullptr : &e_[i];
}

void PcMemory::removeAt(int i) {
  for (int k = i; k + 1 < n_; k++) e_[k] = e_[k + 1];
  n_--;
}

PcMemEntry &PcMemory::touch(const char *ssid) {
  const int i = index(ssid);
  PcMemEntry e;
  if (i >= 0) {
    e = e_[i];
    removeAt(i);
  } else {
    memset(&e, 0, sizeof e);
    scopy(e.ssid, ssid);
    if (n_ >= kCap) removeAt(0);  // piena: esce la più vecchia
  }
  e_[n_] = e;
  return e_[n_++];
}

bool PcMemory::setManual(const char *ssid, const PcAddr *a) {
  if (!ssid || !ssid[0] || strlen(ssid) > 32) return false;
  const PcMemEntry *cur = find(ssid);
  const bool clear = !a || !a->ip[0];
  if (clear) {
    if (!cur || !cur->manual.ip[0]) return false;
    PcMemEntry &e = touch(ssid);
    memset(&e.manual, 0, sizeof e.manual);
    if (!e.last.ip[0]) removeAt(n_ - 1);  // voce vuota: via
    return true;
  }
  if (!pcAddrValid(*a)) return false;
  if (cur && pcAddrSame(cur->manual, *a)) return false;
  touch(ssid).manual = *a;
  return true;
}

bool PcMemory::setLast(const char *ssid, const PcAddr &a) {
  if (!ssid || !ssid[0] || strlen(ssid) > 32 || !pcAddrValid(a)) return false;
  const PcMemEntry *cur = find(ssid);
  if (cur && pcAddrSame(cur->last, a)) return false;
  touch(ssid).last = a;
  return true;
}

bool PcMemory::forget(const char *ssid) {
  const int i = index(ssid);
  if (i < 0) return false;
  removeAt(i);
  return true;
}

bool PcMemory::prune(const char (*keep)[33], int nKeep) {
  bool changed = false;
  for (int i = n_ - 1; i >= 0; i--) {
    bool kept = false;
    for (int k = 0; k < nKeep && !kept; k++) kept = !strcmp(keep[k], e_[i].ssid);
    if (!kept) { removeAt(i); changed = true; }
  }
  return changed;
}

size_t PcMemory::encode(uint8_t *out, size_t cap) const {
  size_t n = 0;
  auto put = [&](const char *s, size_t len) {
    if (n + 1 + len > cap) return false;
    out[n++] = uint8_t(len);
    memcpy(out + n, s, len);
    n += len;
    return true;
  };
  if (cap < 3) return 0;
  out[n++] = 'P';
  out[n++] = 1;
  out[n++] = uint8_t(n_);
  for (int i = 0; i < n_; i++) {
    char m[64], l[64];
    fullText(e_[i].manual, m, sizeof m);
    fullText(e_[i].last, l, sizeof l);
    if (!put(e_[i].ssid, strlen(e_[i].ssid)) || !put(m, strlen(m)) || !put(l, strlen(l))) return 0;
  }
  return n;
}

bool PcMemory::decode(const uint8_t *in, size_t n) {
  n_ = 0;
  if (!in || n < 3 || in[0] != 'P' || in[1] != 1 || in[2] > kCap) return false;
  const int count = in[2];
  size_t p = 3;
  auto get = [&](char *dst, size_t cap) {
    if (p >= n) return false;
    const size_t len = in[p++];
    if (len >= cap || p + len > n) return false;
    memcpy(dst, in + p, len);
    dst[len] = 0;
    p += len;
    return strlen(dst) == len;  // niente zeri in mezzo
  };
  PcMemEntry tmp[kCap];
  for (int i = 0; i < count; i++) {
    PcMemEntry &e = tmp[i];
    memset(&e, 0, sizeof e);
    char m[64], l[64];
    if (!get(e.ssid, sizeof e.ssid) || !e.ssid[0] || !get(m, sizeof m) || !get(l, sizeof l)) return false;
    if (m[0] && !parseFull(m, e.manual)) return false;
    if (l[0] && !parseFull(l, e.last)) return false;
    if (!e.manual.ip[0] && !e.last.ip[0]) return false;
    for (int k = 0; k < i; k++)
      if (!strcmp(tmp[k].ssid, e.ssid)) return false;
  }
  if (p != n) return false;
  for (int i = 0; i < count; i++) e_[i] = tmp[i];
  n_ = count;
  return true;
}

}  // namespace cal
