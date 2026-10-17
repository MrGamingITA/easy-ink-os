#include "theme_values.h"

#include <stdio.h>
#include <string.h>

#include "pet.h"
#include "strbuf.h"
#include "workday.h"

namespace cal {
namespace theme {

namespace {

void sessionValue(int k, const Session &s, Ms now, char *out, size_t cap) {
  Sb b(out, cap);
  switch (k) {
    case 0: b.s(s.name); break;
    case 1: b.s(stateText(s)); break;
    case 2: b.s(lineFor(s)); break;
    case 3: b.s(s.summary); break;
    case 4: metaLine(s, now, out, cap); break;
    case 5: b.i(s.ctx).s("%"); break;
    case 6: if (s.total) b.i(s.done).s("/").i(s.total); break;
    case 7: ago(now, s.since, out, cap); break;
    case 8: if (s.subs) b.i(s.subs); break;
    case 9: if (s.test != TestResult::None) b.s(s.test == TestResult::Ok ? "\xe2\x9c\x93" : "\xe2\x9c\x97"); break;
    default: break;
  }
}

// Quanti byte di s (lunghi len) stanno in room senza spezzare un carattere UTF-8.
size_t utf8Fit(const char *s, size_t len, size_t room) {
  if (len <= room) return len;
  size_t n = room;
  while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) n--;
  return n;
}

void append(char *out, size_t cap, size_t &n, const char *s) {
  if (!cap || n + 1 > cap) return;
  const size_t add = utf8Fit(s, strlen(s), cap - 1 - n);
  memcpy(out + n, s, add);
  n += add;
  out[n] = 0;
}

void caseMap(char *out, size_t cap, const char *s, bool up) {
  if (!cap) return;
  size_t n = 0;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
  while (*p && n + 1 < cap) {
    unsigned char ch = *p++;
    if (up && ch >= 'a' && ch <= 'z') {
      ch = ch - 'a' + 'A';
    } else if (!up && ch >= 'A' && ch <= 'Z') {
      ch = ch - 'A' + 'a';
    } else if (ch == 0xC3 && *p) {
      if (n + 2 >= cap) break;
      unsigned char c2 = *p++;
      if (up && c2 >= 0xA0 && c2 <= 0xBE && c2 != 0xB7) c2 -= 0x20;
      else if (!up && c2 >= 0x80 && c2 <= 0x9E && c2 != 0x97) c2 += 0x20;
      out[n++] = char(ch);
      ch = c2;
    }
    out[n++] = char(ch);
  }
  out[n] = 0;
}

}  // namespace

void upperUtf8(char *out, size_t cap, const char *s) { caseMap(out, cap, s, true); }
void lowerUtf8(char *out, size_t cap, const char *s) { caseMap(out, cap, s, false); }

int permWaiting(const Model &m) {
  int n = 0;
  for (int k = 0; k < m.nSessions; k++) n += m.sessions[k].state == SessionState::Permission;
  return n;
}

bool whenTrue(const Model &m, When w) {
  switch (w) {
    case When::None: return m.nSessions == 0;
    case When::One: return m.nSessions == 1;
    case When::Many: return m.nSessions >= 2;
    default: return true;
  }
}

int refIndex(const Model &m, Ms now, Screen s, Ref r) {
  if (m.nSessions <= 0) return -1;
  int k = r == Ref::Urgent ? urgentIdx(m) : s == Screen::Detail ? m.ui.sel : sIdx(m, now);
  if (k < 0) k = 0;
  if (k >= m.nSessions) k = m.nSessions - 1;
  return k;
}

const Session *refSession(const Ctx &cx, Ref r, const Session *row) {
  switch (r) {
    case Ref::Row: return row;
    case Ref::Nobody: return nullptr;
    case Ref::Variant:
      if (row) return row;
      break;
    case Ref::Urgent: {
      const int k = refIndex(*cx.m, cx.now, cx.screen, Ref::Urgent);
      return k < 0 ? nullptr : &cx.m->sessions[k];
    }
    case Ref::Selected: {
      const int k = refIndex(*cx.m, cx.now, cx.screen, Ref::Selected);
      return k < 0 ? nullptr : &cx.m->sessions[k];
    }
  }
  return cx.ref < 0 || cx.ref >= cx.m->nSessions ? nullptr : &cx.m->sessions[cx.ref];
}

void valueText(uint16_t id, const Ctx &cx, const Session *row, char *out, size_t cap) {
  if (!cap) return;
  out[0] = 0;
  const Model &m = *cx.m;
  if (id < kValClock) {
    const Session *s = id >= kValRow ? row : (cx.ref >= 0 && cx.ref < m.nSessions ? &m.sessions[cx.ref] : nullptr);
    if (s) sessionValue(id & 0x3F, *s, cx.now, out, cap);
    return;
  }
  Sb b(out, cap);
  const Civil c = civilFrom(cx.wc.localMs);
  char t[64];
  switch (id) {
    case 0x80:
      if (m.sys.wallOk) hmLocal(cx.wc.localMs, out, cap);
      else b.s("--:--");
      break;
    case 0x81:
      if (m.sys.wallOk) b.pad2(c.hour).ch(':').pad2(c.minute).ch(':').pad2(c.second);
      else b.s("--:--:--");
      break;
    case 0x82: if (m.sys.wallOk) dateStr(cx.wc, out, cap); break;
    case 0x83: if (m.sys.wallOk) b.s(kWeekdays[c.weekday]); break;
    case 0x84: b.i(m.nSessions); break;
    case 0x85: b.i(permWaiting(m)); break;
    case 0x86: b.i(jsRound(m.sys.batt)).s("%"); break;
    case 0x87: autonomyText(m, out, cap); break;
    case 0x88: if (m.sys.wifi) b.s(m.sys.ssid); break;
    case 0x89: b.s(pcLabel(m)); break;
    case 0x8A: workLine(m, cx.wc, out, cap); break;
    case 0x8B:
      if (m.work.on || workDone(m, cx.wc)) hmLocal(m.work.endLocal, out, cap);
      else minHm(m.set.workEnd, out, cap);
      break;
    case 0x8C: b.i(m.stats.tasks); break;
    case 0x8D: b.s("+").i(m.stats.linesAdd).s(" \xe2\x88\x92").i(m.stats.linesDel); break;
    case 0x8E:
      // %.2f di C, come printf2 del JS (non toFixed: i pari merito esatti vanno alla cifra pari); poi la virgola
      snprintf(t, sizeof t, "%.2f $", m.stats.cost);
      for (char *p = t; *p; p++)
        if (*p == '.') *p = ',';
      b.s(t);
      break;
    case 0x8F:
      if (m.limits.hasH5) b.i(jsRound(m.limits.h5)).s("%");
      else b.s("\xe2\x80\x94");
      break;
    case 0x90:
      if (m.limits.hasD7) b.i(jsRound(m.limits.d7)).s("%");
      else b.s("\xe2\x80\x94");
      break;
    case 0x91:
      if (m.pet.known) b.i(m.pet.level);
      else b.s("\xe2\x80\x94");
      break;
    case 0x92:
      if (m.pet.known) b.i(m.pet.xp - m.pet.xpLevelStart).s("/").i(m.pet.xpNext - m.pet.xpLevelStart).s(" XP");
      break;
    case 0x93: if (m.pet.known) b.i(m.pet.energy).s("%"); break;
    case 0x94: if (m.acc) b.i(jsRound(m.air.co2)).s(" ppm"); break;
    case 0x95: b.s(kFwVersion); break;
    default: break;
  }
}

bool condTrue(uint8_t op, uint8_t arg, const Ctx &cx, const Session *subject) {
  const Model &m = *cx.m;
  const bool want = arg != 0;
  switch (CondOp(op)) {
    case CondOp::Perm: return (permWaiting(m) > 0) == want;
    case CondOp::BattBelow: return m.sys.batt < arg;
    case CondOp::Charging: return m.sys.usb == want;
    case CondOp::Night: return m.set.night == want;
    case CondOp::Pc: return m.sys.pc == want;
    case CondOp::Workday: return m.work.on == want;
    case CondOp::Game: return petOn(m) == want;
    case CondOp::SessionsMin: return m.nSessions >= arg;
    case CondOp::SessionsMax: return m.nSessions <= arg;
    case CondOp::State: return subject && uint8_t(subject->state) == arg;
    case CondOp::Blink: return (((cx.frame % 2) + 2) % 2) == arg;
    default: return false;
  }
}

bool condsTrue(const uint8_t *conds, int n, const Ctx &cx, const Session *subject) {
  for (int i = 0; i < n; i++)
    if (!condTrue(conds[2 * i], conds[2 * i + 1], cx, subject)) return false;
  return true;
}

void expand(const uint8_t *b, size_t n, const uint8_t *segs, int nSeg, const Ctx &cx, const Session *row, bool upperAll,
            char *out, size_t cap, ExpandScratch &sc) {
  if (!cap) return;
  char *const piece = sc.piece, *const mapped = sc.mapped;
  out[0] = 0;
  size_t len = 0;
  for (int i = 0; i < nSeg; i++) {
    const uint8_t *p = segs + 3 * i;
    const uint16_t arg = uint16_t(p[1] | (p[2] << 8));
    if ((p[0] & 3) == 0) {
      const char *s;
      uint8_t l;
      if (!stringAt(b, n, arg, s, l)) continue;
      memcpy(piece, s, l);
      piece[l] = 0;
    } else {
      valueText(arg, cx, row, piece, sizeof sc.piece);
    }
    const int filter = (p[0] >> 2) & 3;
    if (filter == 1) {
      upperUtf8(mapped, sizeof sc.mapped, piece);
      append(out, cap, len, mapped);
    } else if (filter == 2) {
      lowerUtf8(mapped, sizeof sc.mapped, piece);
      append(out, cap, len, mapped);
    } else {
      append(out, cap, len, piece);
    }
  }
  if (upperAll) {
    upperUtf8(mapped, sizeof sc.mapped, out);
    scopy(out, cap, mapped);
  }
}

}  // namespace theme
}  // namespace cal
