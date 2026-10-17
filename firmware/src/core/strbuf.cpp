#include "strbuf.h"

#include <string.h>

namespace cal {

// Lunghezza massima <= room che non spezza un carattere UTF-8.
static size_t utf8Fit(const char *t, size_t len, size_t room) {
  if (len <= room) return len;
  size_t k = room;
  while (k > 0 && (static_cast<uint8_t>(t[k]) & 0xC0) == 0x80) k--;
  return k;
}

Sb &Sb::s(const char *t) {
  if (!t || !cap_) return *this;
  size_t len = strlen(t), room = cap_ - 1 - n_;
  size_t k = utf8Fit(t, len, room);
  if (k < len) trunc_ = true;
  memcpy(b_ + n_, t, k);
  n_ += k;
  b_[n_] = 0;
  return *this;
}

Sb &Sb::i(long long v) {
  char tmp[24];
  int p = 0;
  bool neg = v < 0;
  unsigned long long u = neg ? 0ULL - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
  do { tmp[p++] = static_cast<char>('0' + u % 10); u /= 10; } while (u);
  char out[24];
  int q = 0;
  if (neg) out[q++] = '-';
  while (p) out[q++] = tmp[--p];
  out[q] = 0;
  return s(out);
}

Sb &Sb::pad2(long long v) {
  if (v >= 0 && v < 10) ch('0');
  return i(v);
}

Sb &Sb::ch(char c) {
  char t[2] = {c, 0};
  return s(t);
}

bool scopy(char *dst, size_t cap, const char *src) {
  Sb b(dst, cap);
  b.s(src);
  return !b.truncated();
}

bool startsWith(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

}  // namespace cal
