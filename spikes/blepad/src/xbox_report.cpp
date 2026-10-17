// SPIKE usa e getta · blepad: vedi xbox_report.h.
#include "xbox_report.h"

#include <stdio.h>
#include <stdlib.h>

namespace pad {

namespace {
uint16_t u16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
unsigned absDiff(uint16_t a, uint16_t b) { return a > b ? a - b : b - a; }
}  // namespace

uint8_t hatToDpad(uint8_t hat) {
  static const uint8_t kMap[9] = {0, kUp, kUp | kRight, kRight, kDown | kRight, kDown, kDown | kLeft, kLeft, kUp | kLeft};
  return hat <= 8 ? kMap[hat] : 0;
}

bool parse(const uint8_t *d, size_t n, State *out) {
  if (!d || n < kReportLen) return false;
  State s;
  s.lx = u16(d + 0);
  s.ly = u16(d + 2);
  s.rx = u16(d + 4);
  s.ry = u16(d + 6);
  s.lt = u16(d + 8);
  s.rt = u16(d + 10);
  s.dpad = hatToDpad(d[12]);
  const uint8_t m = d[13], c = d[14], e = d[15];
  uint16_t b = 0;
  if (m & 0x01) b |= kA;
  if (m & 0x02) b |= kB;
  if (m & 0x08) b |= kX;
  if (m & 0x10) b |= kY;
  if (m & 0x40) b |= kLB;
  if (m & 0x80) b |= kRB;
  if (c & 0x04) b |= kView;
  if (c & 0x08) b |= kMenu;
  if (c & 0x10) b |= kXbox;
  if (c & 0x20) b |= kLS;
  if (c & 0x40) b |= kRS;
  if (e & 0x01) b |= kShare;
  s.buttons = b;
  *out = s;
  return true;
}

int stickPercent(uint16_t v) {
  const long c = long(v) - 32768;
  return int(c * 100 / (c < 0 ? 32768 : 32767));
}

bool buttonsDiffer(const State &a, const State &b) { return a.buttons != b.buttons || a.dpad != b.dpad; }

bool differs(const State &a, const State &b, uint16_t st, uint16_t tt) {
  if (buttonsDiffer(a, b)) return true;
  return absDiff(a.lx, b.lx) > st || absDiff(a.ly, b.ly) > st || absDiff(a.rx, b.rx) > st ||
         absDiff(a.ry, b.ry) > st || absDiff(a.lt, b.lt) > tt || absDiff(a.rt, b.rt) > tt;
}

size_t format(const State &s, char *buf, size_t cap) {
  if (!buf || !cap) return 0;
  // Un segno per tasto, sempre nella stessa colonna: A B X Y LB RB View Menu Xbox LS RS Share
  static const char *const kNames[12] = {"A", "B", "X", "Y", "LB", "RB", "V", "M", "G", "LS", "RS", "S"};
  char btn[32];
  size_t k = 0;
  for (int i = 0; i < 12; i++) {
    const bool on = s.buttons & (1u << i);
    for (const char *p = kNames[i]; *p && k < sizeof btn - 1; p++) btn[k++] = on ? *p : '.';
  }
  btn[k] = 0;
  char dp[5] = {s.dpad & kUp ? 'U' : '-', s.dpad & kDown ? 'D' : '-', s.dpad & kLeft ? 'L' : '-',
                s.dpad & kRight ? 'R' : '-', 0};
  const int n = snprintf(buf, cap, "%s croce=%s L=%+d,%+d R=%+d,%+d LT=%u RT=%u", btn, dp, stickPercent(s.lx),
                         stickPercent(s.ly), stickPercent(s.rx), stickPercent(s.ry), unsigned(s.lt), unsigned(s.rt));
  if (n < 0) return 0;
  return size_t(n) < cap ? size_t(n) : cap - 1;
}

}  // namespace pad
