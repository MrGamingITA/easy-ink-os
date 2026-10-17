// Schermate della classe S: traduzione riga per riga del ramo S di draw() del simulatore.
// Regole seguite ovunque (come nel JS):
//   - T() arrotonda x e y con Math.round (::jsRound) e accorcia con fit() se c'è max;
//   - le larghezze che il JS prende con Math.ceil(measureText().width) passano da ceilW();
//   - PAPER/INK: true = inchiostro, false = carta (il colore del testo è TextOpts::ink).
#include "ui/screens.h"

#include "ui/kit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/accessories.h"
#include "core/avatars.h"
#include "core/menu.h"
#include "core/netfmt.h"
#include "core/strbuf.h"
#include "core/game.h"
#include "core/theme_runtime.h"
#include "core/gb_frame.h"
#include "core/pet.h"
#include "core/workday.h"
#include "gfx/qr.h"
#include "gfx/accessory.h"
#include "gfx/avatar.h"
#include "gfx/prims.h"
#include "ui/theme_draw.h"

namespace ui {

using cal::Ms;
using cal::Model;
using cal::Session;
using cal::DeviceProfile;
using cal::WallClock;
using cal::ScreenEntry;
using cal::ScreenId;
using cal::SessionState;

namespace {

TextHook gHook = nullptr;
void *gHookCtx = nullptr;

const bool INK = true, PAPER = false;

int R(double v) { return ::jsRound(v); }  // Math.round

// Opzioni di T(): o.w, o.f (pix), o.a, o.c, o.max.
struct O {
  int w;
  bool pix;
  Align a;
  bool ink;
  int max;
  O right() const { O o = *this; o.a = Align::Right; return o; }
  O center() const { O o = *this; o.a = Align::Center; return o; }
  O col(bool c) const { O o = *this; o.ink = c; return o; }
  O mx(int m) const { O o = *this; o.max = m; return o; }
};
O Wt(int w) { return O{w, false, Align::Left, INK, 0}; }  // F_UI (Chakra Petch)
O Px(int w) { return O{w, true, Align::Left, INK, 0}; }   // F_PIX (Silkscreen)

void T(Surface &c, const char *s, double x, double y, int size, const O &o) {
  char buf[256];
  const char *str = s ? s : "";
  if (o.max) {
    fitText(buf, sizeof buf, str, size, o.w, o.pix, o.max);
    str = buf;
  }
  const int ix = R(x), iy = R(y);
  TextOpts to;
  to.weight = o.w;
  to.pix = o.pix;
  to.align = o.a;
  to.ink = o.ink;
  to.max = 0;
  drawText(c, str, ix, iy, size, to);
  if (gHook) gHook(str, ix, iy, size, o.w, o.pix, o.a, gHookCtx);
}

// Math.ceil(c.measureText(s).width): textWidth() arrotonda, quindi si controlla con fit() se la
// larghezza vera sta entro il valore arrotondato (fit() confronta in aritmetica esatta).
int ceilW(const char *s, int size, int weight, bool pix) {
  if (!s || !*s) return 0;
  const int r = textWidth(s, size, weight, pix);
  char buf[256];
  fitText(buf, sizeof buf, s, size, weight, pix, r);
  return strcmp(buf, s) == 0 ? r : r + 1;
}

// wrapT(): wrapLines() + T() riga per riga (qui per registrare i testi come gli altri).
void wrapT(Surface &c, const char *s, int x, int y, int size, int maxW, int maxLines, int lh, const O &o) {
  char lines[8][96];
  const int n = wrapLines(lines, maxLines < 8 ? maxLines : 8, s, size, maxW, o.w, o.pix);
  for (int i = 0; i < n; i++) T(c, lines[i], x, y + i * lh, size, o.mx(maxW));
}

// toUpperCase() per l'italiano: ASCII e lettere latine accentate (U+00E0..U+00FE, tranne ÷).
void upper(char *out, size_t cap, const char *s) {
  size_t n = 0;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
  while (*p && n + 1 < cap) {
    unsigned char ch = *p++;
    if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
    else if (ch == 0xC3 && *p >= 0xA0 && *p <= 0xBE && *p != 0xB7 && n + 2 < cap) {
      out[n++] = char(ch);
      ch = *p++ - 0x20;
    }
    out[n++] = char(ch);
  }
  out[n] = 0;
}

// toLowerCase() per i testi di STATES[...].long (ASCII).
void lower(char *out, size_t cap, const char *s) {
  size_t n = 0;
  for (; *s && n + 1 < cap; s++) out[n++] = (*s >= 'A' && *s <= 'Z') ? char(*s - 'A' + 'a') : *s;
  out[n] = 0;
}

Pose P(cal::MascotPose p) { return static_cast<Pose>(static_cast<int>(p)); }

// L'avatar della sessione (cal::sessionAvatar: il suo, o il predefinito, o la Goccia) o, nelle schermate
// senza sessione, l'avatar predefinito (cal::defaultAvatar). AV_W = 28: le centrature usano w (AVATAR.md §2.4).
const int AV_W = kAvatarW;
// Con il gioco acceso la mascotte porta l'accessorio indossato (accOverlay del simulatore): si ridisegnano solo i
// punti che l'accessorio e il suo contorno cambiano.
// 0.14.1: griglie 28×25 dell'avatar condivise da accOverlay (due) e partyFx (una): solo il loop disegna, mai annidate.
uint8_t gGridBase[kAvatarCells], gGridFig[kAvatarCells];
void accOverlay(Surface &c, const Model &m, const Avatar &a, int ox, int oy, int sc, Pose p, int f, bool ink) {
  if (!cal::wornAccessory(m)) return;
  if (!avatarGrid(a, p, f, gGridBase) || !cal::figureGrid(m, a, p, f, gGridFig)) return;
  for (int k = 0; k < kAvatarCells; k++)
    if (gGridFig[k] != gGridBase[k]) c.fillRect(ox + (k % kAvatarW) * sc, oy + (k / kAvatarW) * sc, sc, sc, (gGridFig[k] == 1) == ink);
}
void avatarS(Surface &c, const Model &m, const Session *s, int ox, int oy, int sc, cal::MascotPose st, int f) {
  const Avatar &a = s ? cal::sessionAvatar(m, *s) : cal::defaultAvatar(m);
  drawAvatar(c, a, ox, oy, sc, P(st), f, INK);
  accOverlay(c, m, a, ox, oy, sc, P(st), f, INK);
}

// Festa di un traguardo (gioco): coriandoli e scintille attorno alla mascotte, solo nei punti della griglia 28×25
// rimasti trasparenti (mascotte e accessorio compresi). Due serie che si alternano coi fotogrammi pari e dispari.
const int8_t kConfetti[2][12][2] = {
    {{2, 1}, {6, 4}, {10, 0}, {17, 2}, {22, 0}, {26, 4}, {1, 8}, {26, 10}, {0, 15}, {27, 17}, {3, 21}, {24, 22}},
    {{4, 0}, {8, 2}, {14, 1}, {19, 4}, {24, 1}, {27, 6}, {0, 11}, {25, 13}, {2, 18}, {26, 20}, {1, 23}, {23, 24}}};
const int8_t kSparks[2][2][2] = {{{5, 9}, {22, 7}}, {{3, 5}, {24, 16}}};
void partyFx(Surface &c, const Model &m, const Session *s, int ox, int oy, int sc, int f) {
  const Avatar &a = s ? cal::sessionAvatar(m, *s) : cal::defaultAvatar(m);
  uint8_t *const g = gGridFig;
  if (!cal::figureGrid(m, a, Pose::Done, f, g)) return;
  const int k = ((f % 2) + 2) % 2;
  auto dot = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= kAvatarW || y >= kAvatarH || g[y * kAvatarW + x]) return;
    c.fillRect(ox + x * sc, oy + y * sc, sc, sc, INK);
  };
  for (const auto &p : kConfetti[k]) dot(p[0], p[1]);
  for (const auto &p : kSparks[k]) {
    dot(p[0], p[1]);
    dot(p[0] - 1, p[1]);
    dot(p[0] + 1, p[1]);
    dot(p[0], p[1] - 1);
    dot(p[0], p[1] + 1);
  }
}

BarState barOf(const Session &s) { return BarState{s.done, s.total, s.state == SessionState::Working}; }

// 0.10.1: ora mai sincronizzata da questa accensione a freddo (né welcome né SNTP): «--:--», mai un'ora sbagliata.
void clockStr(const Model &m, const WallClock &wc, char *out, size_t cap) {
  if (m.sys.wallOk) cal::hmLocal(wc.localMs, out, cap);
  else cal::scopy(out, cap, "--:--");
}

// fillRect con coordinate frazionarie come il canvas: copertura per pixel, poi soglia 0,5 di
// Screen.compose (luminanza < 0,5 → inchiostro, cioè copertura > 0,5).
void fillRectF(Surface &c, double x, double y, double w, double h, bool col) {
  const int x0 = int(floor(x)), x1 = int(ceil(x + w)), y0 = int(floor(y)), y1 = int(ceil(y + h));
  for (int py = y0; py < y1; py++) {
    const double cy = fmin(py + 1.0, y + h) - fmax(double(py), y);
    if (cy <= 0) continue;
    for (int px = x0; px < x1; px++) {
      const double cx = fmin(px + 1.0, x + w) - fmax(double(px), x);
      if (cx > 0 && cx * cy > 0.5) c.fillRect(px, py, 1, 1, col);
    }
  }
}

// dotsV con estremi frazionari (oggi(): y + gh − 6 con gh = 106 / 3).
void dotsVf(Surface &c, int x, double y0, double y1) {
  for (double y = y0; y < y1; y += 2) fillRectF(c, x, y, 1, 1, INK);
}

int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

// ---------- elementi comuni ----------

// battShown: la batteria è già in alto accanto all'orologio (battS), qui non si ripete.
int alertsS(Surface &c, const Model &m, Ms now, int xr, int y, bool battShown = false) {
  int x = xr;
  auto put = [&](Sym g, bool struck) {
    x -= 7;
    sym(c, g, x, y, 1, INK);
    if (struck) strike(c, x - 1, y - 1, 9, 1, INK, PAPER);
    x -= 4;
  };
  if (!m.sys.pc) put(Sym::Plug, true);
  if (!m.sys.wifi) { x -= 11; wifiIcon(c, x, y - 1, 1, -1, INK); x -= 4; }
  if (m.sys.batt < 15 && !m.sys.usb && !battShown) { x -= 16; battIcon(c, x, y, 1, m.sys.batt, INK); x -= 4; }
  if (cal::muted(m, now)) put(Sym::Bell, true);
  if (m.set.night) put(Sym::Moon, false);
  if (cal::focusOn(m, now)) put(Sym::Glass, false);
  if (m.limits.hasH5 && m.limits.h5 >= 80) put(Sym::Gauge, false);
  return x;
}

// battS(): batteria accanto all'orologio. Da destra «78%» (Silkscreen 8), 3 px, icona 14×8 con la
// punta; in carica il fulmine a sinistra dell'icona. Restituisce il bordo sinistro.
int battS(Surface &c, const Model &m, int xr, int y, bool col) {
  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", R(m.sys.batt));
  T(c, pct, xr, y, 8, Px(400).right().col(col));
  int x = xr - ceilW(pct, 8, 400, true) - 3 - 16;
  battIcon(c, x, y, 1, m.sys.batt, col);
  if (m.sys.usb) { x -= 7; sym(c, Sym::Bolt, x, y, 1, col); }
  return x;
}

// Giornata di lavoro (0.9.0): «6 h 25» in Silkscreen 8 a sinistra degli avvisi, sulla riga dell'orologio (Goccia,
// Contatori, Home vuota). xr = bordo sinistro degli avvisi (alertsS), xmin = dove può arrivare al massimo: se non ci
// sta (molti avvisi) non si disegna. Restituisce il nuovo bordo sinistro della riga.
int workS(Surface &c, const Model &m, const WallClock &wc, int xr, int y, int xmin) {
  char t[24];
  cal::workLine(m, wc, t, sizeof t);
  if (!t[0]) return xr;
  const int x = xr - 2 - ceilW(t, 8, 400, true);
  if (x < xmin) return xr;
  T(c, t, xr - 2, y, 8, Px(400).right());
  return x - 4;
}

// Home S: durante la festa di fine giornata (o di un traguardo del gioco) la mascotte esulta, se la sessione mostrata
// non aspetta qualcuno.
cal::MascotPose homePose(const Model &m, const Session &s, Ms now) {
  return (cal::workParty(m, now) || cal::petParty(m, now)) && !cal::isAttn(s) ? cal::MascotPose::Done : cal::pose(m, s);
}

void drawToast(Surface &c, const DeviceProfile &dev, const Model &m, Ms now) {
  if (!m.toast.msg[0] || now > m.toast.until) return;
  const int fs = 10, px = 8;
  int tw = ceilW(m.toast.msg, fs, 700, false) + px * 2;
  if (tw > dev.W - 20) tw = dev.W - 20;
  const int x = R((dev.W - tw) / 2.0), h = fs + 8, y = dev.H - 0 - h - 4;
  c.fillRect(x - 2, y - 2, tw + 4, h + 4, PAPER);
  c.fillRect(x, y, tw, h, INK);
  T(c, m.toast.msg, dev.W / 2.0, y + R((h - fs) / 2.0), fs, Wt(700).center().col(PAPER).mx(tw - 8));
}

void stripS(Surface &c, const DeviceProfile &dev, const Model &m, const char *title, const WallClock &wc) {
  c.fillRect(0, 0, dev.W, 14, INK);
  chevL(c, 4, 2, PAPER, 1);
  char up[64], clk[8];
  clockStr(m, wc, clk, sizeof clk);
  const int bx = battS(c, m, dev.W - 4 - ceilW(clk, 8, 400, true) - 6, 3, PAPER);
  upper(up, sizeof up, title);
  T(c, up, 13, 3, 8, Px(700).col(PAPER).mx(bx - 13 - 6 > 40 ? bx - 13 - 6 : 40));
  T(c, clk, dev.W - 4, 3, 8, Px(400).right().col(PAPER));
}

int viewHeader(Surface &c, const DeviceProfile &dev, const Model &m, const char *title, const WallClock &wc) {
  stripS(c, dev, m, title, wc);
  return 16;
}

void drawVal(Surface &c, const cal::MenuItem &it, int rx, int y, int fs, bool col, int s) {
  int x = rx;
  if (it.chevron()) {
    const int w = ceilW("›", fs + 2, 700, false);
    T(c, "›", x, y - 2, fs + 2, Wt(700).col(col).right());
    x -= w + 6;
  }
  if (it.hasBars) {
    x -= 11 * s;
    wifiIcon(c, x, y + R((fs - 9 * s) / 2.0), s, it.bars, col);
    x -= 5 * s;
    if (it.lock) {
      x -= 7 * s;
      sym(c, Sym::Lock, x, y + R((fs - 7 * s) / 2.0), s, col);
      x -= 5 * s;
    }
  }
  if (it.val[0]) {
    int mx = R(rx * 0.45);
    if (mx < 40) mx = 40;
    T(c, it.val, x, y, fs, Wt(600).col(col).right().mx(mx));
  }
}

// drawQR(): QR finto (stessi moduli del simulatore), seme FNV-1a e xorshift a 32 bit. N moduli per lato:
// 25 (versione 2, Wi-Fi ospiti) o 29 (versione 3, come il QR vero della configurazione).
void drawQR(Surface &c, int x, int y, int px, const char *seed, int N) {
  uint32_t h = 2166136261u;
  for (const char *p = seed; *p; p++) { h ^= uint8_t(*p); h *= 16777619u; }
  if (!h) h = 1;
  auto rnd = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return double(h % 1000) / 1000.0; };
  c.fillRect(x - px, y - px, (N + 2) * px, (N + 2) * px, PAPER);
  auto inFinder = [&](int r, int q) { return (r < 8 && q < 8) || (r < 8 && q >= N - 8) || (r >= N - 8 && q < 8); };
  for (int r = 0; r < N; r++)
    for (int q = 0; q < N; q++) {
      if (inFinder(r, q)) continue;
      bool on;
      if (r == 6) on = q % 2 == 0;
      else if (q == 6) on = r % 2 == 0;
      else on = rnd() < 0.48;
      if (on) c.fillRect(x + q * px, y + r * px, px, px, INK);
    }
  auto finder = [&](int fx, int fy) {
    for (int r = 0; r < 7; r++)
      for (int q = 0; q < 7; q++) {
        const bool on = r == 0 || r == 6 || q == 0 || q == 6 || (r >= 2 && r <= 4 && q >= 2 && q <= 4);
        c.fillRect(x + (fx + q) * px, y + (fy + r) * px, px, px, on);
      }
  };
  finder(0, 0);
  finder(N - 7, 0);
  finder(0, N - 7);
  for (int r = 0; r < 5; r++)
    for (int q = 0; q < 5; q++) {
      const bool on = r == 0 || r == 4 || q == 0 || q == 4 || (r == 2 && q == 2);
      c.fillRect(x + (N - 9 + q) * px, y + (N - 9 + r) * px, px, px, on);  // allineamento: 16 per N = 25
    }
}

// «3/7 task» o «senza task»
void taskCount(const Session &s, char *out, size_t cap) {
  cal::Sb b(out, cap);
  if (s.total) b.i(s.done).s("/").i(s.total).s(" task");
  else b.s("senza task");
}

// ---------- Home ----------

void homeGoccia(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  const int W = dev.W, i = cal::sIdx(m, now);
  const Session &s = m.sessions[i];
  avatarS(c, m, &s, 4, 10, 4, homePose(m, s, now), f);
  if (cal::petParty(m, now) && !cal::isAttn(s)) partyFx(c, m, &s, 4, 10, 4, f);
  const int x = 120, w = W - x - 6;
  char clk[8], tmp[32];
  clockStr(m, wc, clk, sizeof clk);
  T(c, clk, W - 6, 4, 8, Px(400).right());
  workS(c, m, wc, alertsS(c, m, now, battS(c, m, W - 6 - ceilW(clk, 8, 400, true) - 6, 4, INK) - 6, 4, true), 4, x);
  T(c, cal::stateInfo(s.state).label, x, 18, 20, Wt(700).mx(w));
  T(c, s.name, x, 44, 13, Wt(600).mx(w));
  T(c, cal::stateText(s), x, 61, 10, Wt(500).mx(w));
  bar(c, x, 78, w, 8, barOf(s), f);
  for (int k = 0; k < m.nSessions; k++) {
    const int ix = x + k * 14;
    iconForState(c, cal::stateInfo(m.sessions[k].state).key, ix, 101, 1, f, INK);
    if (k == i) c.fillRect(ix, 111, 7, 2, INK);
  }
  cal::ago(now, s.since, tmp, sizeof tmp);
  T(c, tmp, W - 6, 101, 8, Px(400).right());
}

void homeRighe(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now) {
  const int W = dev.W, H = dev.H, pin = m.ui.selUntil > now ? m.ui.sel : -1;
  for (int i = 0; i < m.nSessions; i++) {
    const Session &s = m.sessions[i];
    const int y = i * 30 + 1;
    const bool a = cal::isAttn(s), col = a ? PAPER : INK;
    if (a) c.fillRect(0, y, W, 17, INK);
    iconForState(c, cal::stateInfo(s.state).key, 6, y + 2, 2, f, col);
    T(c, s.name, 26, y + 1, 13, Wt(700).col(col).mx(150));
    char r[24];
    cal::Sb b(r, sizeof r);
    if (a) b.s(cal::stateInfo(s.state).label);
    else if (s.total) b.i(s.done).s("/").i(s.total);
    T(c, r, W - 6, y + 3, 10, Wt(700).right().col(col));
    bar(c, 6, y + 19, W - 12, 7, barOf(s), f);
    if (i == pin) {
      c.fillRect(0, y + 19, 3, 7, INK);
      c.fillRect(W - 3, y + 19, 3, 7, INK);
    }
  }
  if (cal::hasAlerts(m, now)) {
    c.fillRect(W - 64, H - 12, 64, 12, PAPER);
    alertsS(c, m, now, W - 3, H - 10);
  }
}

void homeNumeri(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  const int W = dev.W;
  int lav = 0, att = 0, fin = 0;
  for (int k = 0; k < m.nSessions; k++) {
    const Session &s = m.sessions[k];
    if (s.state == SessionState::Working) lav++;
    if (cal::isAttn(s)) att++;
    if (s.state == SessionState::Done || s.state == SessionState::Idle) fin++;
  }
  const char *labels[3] = {"LAVORANO", "PER TE", "FINITE"};
  const int vals[3] = {lav, att, fin};
  const bool invs[3] = {false, att > 0, false};
  const int cw = W / 3;
  for (int i = 0; i < 3; i++) {
    const int x = i * cw;
    const double cx = x + cw / 2.0;
    const bool inv = invs[i], col = inv ? PAPER : INK;
    if (inv) c.fillRect(x + 3, 3, cw - 6, 74, INK);
    char num[12];
    snprintf(num, sizeof num, "%d", vals[i]);
    T(c, num, cx, 6, 46, Wt(700).center().col(col));
    T(c, labels[i], cx, 62, 8, Px(700).center().col(col).mx(cw - 8));
    if (i < 2 && !inv && !invs[i + 1]) dotsV(c, x + cw, 8, 74);
  }
  dotsH(c, 0, W, 83);
  const int i = cal::sIdx(m, now);
  const Session &s = m.sessions[i];
  avatarS(c, m, &s, 3, 90, 1, homePose(m, s, now), f);
  if (cal::petParty(m, now) && !cal::isAttn(s)) partyFx(c, m, &s, 3, 90, 1, f);
  char clk[8];
  clockStr(m, wc, clk, sizeof clk);
  T(c, clk, W - 6, 90, 8, Px(400).right());
  const int ax = workS(c, m, wc, alertsS(c, m, now, battS(c, m, W - 6 - ceilW(clk, 8, 400, true) - 6, 90, INK) - 6, 90, true), 90, 36 + 60 + 4);
  T(c, s.name, 36, 88, 12, Wt(700).mx(ax - 36 - 4 > 40 ? ax - 36 - 4 : 40));
  char line[160];
  if (cal::isAttn(s)) {
    cal::Sb(line, sizeof line).s(cal::stateInfo(s.state).label).s(" · ").s(s.detail);
  } else {
    cal::scopy(line, cal::stateText(s));
  }
  T(c, line, 36, 105, 10, Wt(500).mx(W - 44));
}

void homeUna(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now) {
  const int W = dev.W, i = cal::unaIdx(m, now);
  const Session &s = m.sessions[i];
  const bool a = cal::isAttn(s);
  T(c, s.name, 8, 5, 15, Wt(700).mx(150));
  if (a) c.fillRect(4, 25, 152, 27, INK);
  T(c, cal::stateInfo(s.state).label, 8, 26, 22, Wt(700).col(a ? PAPER : INK).mx(146));
  wrapT(c, cal::lineFor(s), 8, 57, 11, 150, 2, 13, Wt(500));
  avatarS(c, m, &s, 163, 4, 3, homePose(m, s, now), f);
  if (cal::petParty(m, now) && !a) partyFx(c, m, &s, 163, 4, 3, f);
  bar(c, 8, 88, W - 16, 10, barOf(s), f);
  char tc[32], ag[24], line[64];
  taskCount(s, tc, sizeof tc);
  cal::ago(now, s.since, ag, sizeof ag);
  cal::Sb(line, sizeof line).s(tc).s(" · ").s(ag);
  T(c, line, 8, 104, 10, Wt(600).mx(140));
  const int n = m.nSessions;
  for (int k = 0; k < n; k++) {
    const int x = W - 8 - (n - k) * 10 + 4;
    if (k == i) c.fillRect(x, 106, 6, 6, INK);
    else {
      box(c, x, 106, 6, 6, INK, 1);
      if (cal::isAttn(m.sessions[k])) c.fillRect(x + 2, 108, 2, 2, INK);
    }
  }
  alertsS(c, m, now, W - 8 - n * 10 - 4, 105);
}

// Nessuna sessione dal PC (state con sessions vuoto): il simulatore non ha questo caso.
// Stessa impaginazione di homeGoccia, con l'avatar predefinito in pausa.
void homeEmpty(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  const int W = dev.W;
  avatarS(c, m, nullptr, 4, 10, 4, cal::MascotPose::Idle, f);
  const int x = 120, w = W - x - 6;
  char clk[8];
  clockStr(m, wc, clk, sizeof clk);
  T(c, clk, W - 6, 4, 8, Px(400).right());
  workS(c, m, wc, alertsS(c, m, now, battS(c, m, W - 6 - ceilW(clk, 8, 400, true) - 6, 4, INK) - 6, 4, true), 4, x);
  T(c, "NESSUNA", x, 18, 20, Wt(700).mx(w));
  T(c, "sessione aperta", x, 44, 13, Wt(600).mx(w));
  wrapT(c, "Apri Claude Code sul PC: le sessioni compaiono qui.", x, 61, 10, w, 3, 12, Wt(500));
}

void homeS(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  if (m.nSessions == 0) { homeEmpty(c, dev, m, f, now, wc); return; }
  switch (m.set.homeStyle) {
    case cal::HomeStyle::Righe: homeRighe(c, dev, m, f, now); break;
    case cal::HomeStyle::Contatori: homeNumeri(c, dev, m, f, now, wc); break;
    case cal::HomeStyle::Una: homeUna(c, dev, m, f, now); break;
    default: homeGoccia(c, dev, m, f, now, wc); break;
  }
}

// ---------- Dettaglio e Permesso ----------

void detailS(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now) {
  const int W = dev.W, i = m.ui.sel;
  const Session &s = m.sessions[i];
  const bool a = cal::isAttn(s);
  c.fillRect(0, 0, W, 15, INK);
  chevL(c, 4, 3, PAPER, 1);
  char pos[12];
  snprintf(pos, sizeof pos, "%d/%d", i + 1, m.nSessions);
  const int bx = battS(c, m, W - 4 - ceilW(pos, 8, 400, true) - 6, 4, PAPER);
  T(c, s.name, 13, 1, 12, Wt(700).col(PAPER).mx(bx - 13 - 6 < 170 ? bx - 13 - 6 : 170));
  T(c, pos, W - 4, 4, 8, Px(400).right().col(PAPER));
  avatarS(c, m, &s, 2, 20, 3, cal::pose(m, s), f);
  const int x = 88, w = W - x - 4;
  if (a) c.fillRect(x - 2, 18, w + 4, 20, INK);
  T(c, cal::stateInfo(s.state).label, x, 19, 16, Wt(700).col(a ? PAPER : INK).mx(w));
  wrapT(c, s.detail, x, 41, 10, w, 2, 12, Wt(500));
  bar(c, x, 68, w, 8, barOf(s), f);
  char tc[32], ag[24], meta[96];
  taskCount(s, tc, sizeof tc);
  cal::ago(now, s.since, ag, sizeof ag);
  cal::Sb(meta, sizeof meta).s(tc).s(" · ").s(ag).s(" · ctx ").i(s.ctx).s("%");
  T(c, meta, x, 79, 10, Wt(600).mx(w));
  const cal::Task *cur = nullptr;
  for (int k = 0; k < s.nTasks && !cur; k++)
    if (s.tasks[k].status == cal::TaskStatus::Doing) cur = &s.tasks[k];
  for (int k = 0; k < s.nTasks && !cur; k++)
    if (s.tasks[k].status == cal::TaskStatus::Todo) cur = &s.tasks[k];
  if (s.state == SessionState::Done && s.summary[0]) T(c, s.summary, x, 92, 10, Wt(600).mx(w));
  else if (cur) {
    char t[64];
    cal::Sb(t, sizeof t).s("› ").s(cur->name);
    T(c, t, x, 92, 10, Wt(600).mx(w));
  }
  dotsH(c, 0, W, 106);
  const char *line;
  if (s.state == SessionState::Permission)
    line = cal::permFromDisplay(m) && (!m.sys.live || cal::permReqFor(m, s.id) >= 0) ? "2X  RISPONDI AL PERMESSO" : "RISPONDI NEL TERMINALE";
  else if (s.state == SessionState::Input) line = "RISPONDI NEL TERMINALE";
  else if (cal::canArchive(s)) line = "1X SESSIONE   2X ARCHIVIA   LUNGA INDIETRO";
  else line = "1X SESSIONE SUCCESSIVA   LUNGA INDIETRO";
  T(c, line, 4, 110, 8, Px(s.state == SessionState::Permission ? 700 : 400).mx(W - 8));
}

void permS(Surface &c, const DeviceProfile &dev, const Model &m, Ms now) {
  const int W = dev.W;
  const ScreenEntry &tp = cal::top(m);
  const Session &s = m.sessions[tp.i];
  char tool[cal::kToolLen], arg[cal::kArgLen];
  const char *name = s.name;
  const int q = cal::permReqById(m, tp.rid);
  if (q >= 0) {  // con la rete: strumento e argomento della richiesta verificata (M5 §3)
    cal::scopy(tool, m.perms[q].tool);
    cal::scopy(arg, m.perms[q].arg);
    if (m.perms[q].name[0]) name = m.perms[q].name;
  } else {
    cal::splitCmd(s.detail, tool, sizeof tool, arg, sizeof arg);
  }
  c.fillRect(0, 0, W, 15, INK);
  T(c, "PERMESSO", 4, 1, 12, Wt(700).col(PAPER));
  T(c, name, W - 4, 2, 11, Wt(600).col(PAPER).right().mx(140));
  box(c, 4, 19, W - 8, 34, INK, 1);
  T(c, tool, 9, 22, 12, Wt(700).mx(W - 18));
  T(c, arg[0] ? arg : "—", 9, 37, 11, Wt(600).mx(W - 18));
  const int bw = (W - 14) / 2;
  const char *labs[2] = {"CONSENTI", "NEGA"};
  for (int k = 0; k < 2; k++) {
    const int x = 4 + k * (bw + 6);
    const bool on = tp.choice == k;
    if (on) c.fillRect(x, 58, bw, 26, INK);
    else box(c, x, 58, bw, 26, INK, 2);
    T(c, labs[k], x + bw / 2.0, 63, 14, Wt(700).center().col(on ? PAPER : INK));
  }
  // Nessuna scadenza: al posto del conto alla rovescia, da quanto aspetta (minuti interi).
  char wait[32];
  cal::permWaitText(now, cal::permSince(m, tp), wait, sizeof wait);
  T(c, wait, 4, 89, 8, Px(700).mx(W - 8));
  dotsH(c, 0, W, 104);
  T(c, "1X CAMBIA   2X CONFERMA   LUNGA ANNULLA", 4, 109, 8, Px(400).mx(W - 8));
}

// ---------- Abbinamento e conferme (M5) ----------

// Codice «175397» come «175 397»: si legge e si ricopia meglio.
void codeGroups(const char *code, char *out, size_t cap) {
  cal::Sb b(out, cap);
  for (int k = 0; code[k]; k++) {
    if (k == 3) b.ch(' ');
    b.ch(code[k]);
  }
}

void pairS(Surface &c, const DeviceProfile &dev, const Model &m) {
  const int W = dev.W;
  const ScreenEntry &tp = cal::top(m);
  const bool ready = tp.sel == 1;
  c.fillRect(0, 0, W, 15, INK);
  T(c, "ABBINAMENTO", 4, 1, 12, Wt(700).col(PAPER));
  T(c, cal::pcLabel(m), W - 4, 2, 11, Wt(600).col(PAPER).right().mx(130));
  T(c, ready ? "Il PC ha il codice giusto" : "Scrivi questo codice sul PC", W / 2.0, 21, 10, Wt(600).center().mx(W - 8));
  char code[16];
  codeGroups(tp.code, code, sizeof code);
  T(c, code, W / 2.0, 33, 44, Wt(700).center());
  if (ready) {  // codice confermato dal PC: incorniciato, ora tocca al display
    const int tw = ceilW(code, 44, 700, false);
    box(c, R(W / 2.0 - tw / 2.0) - 10, 35, tw + 20, 40, INK, 2);
  }
  T(c, ready ? "Conferma qui con 2 pressioni" : "Poi conferma qui con 2 pressioni", W / 2.0, 86, 10, Wt(ready ? 700 : 500).center().mx(W - 8));
  dotsH(c, 0, W, 104);
  T(c, "2X CONFERMA   LUNGA ANNULLA", 4, 109, 8, Px(400).mx(W - 8));
}

// 0.14.0: codice di associazione del telefono (passkey di LE Secure Connections). Bloccata per i temi.
void padCodeS(Surface &c, const DeviceProfile &dev, const Model &m) {
  const int W = dev.W;
  const ScreenEntry &tp = cal::top(m);
  c.fillRect(0, 0, W, 15, INK);
  T(c, "TELEFONO", 4, 1, 12, Wt(700).col(PAPER));
  T(c, "Codice per il telefono", W / 2.0, 21, 10, Wt(600).center().mx(W - 8));
  char code[16];
  codeGroups(tp.code, code, sizeof code);
  T(c, code, W / 2.0, 33, 44, Wt(700).center());
  T(c, "Scrivilo sull'iPhone quando lo chiede", W / 2.0, 86, 10, Wt(500).center().mx(W - 8));
  dotsH(c, 0, W, 104);
  T(c, "LUNGA ANNULLA", 4, 109, 8, Px(400).mx(W - 8));
}

void confirmS(Surface &c, const DeviceProfile &dev, const Model &m, Ms now, const WallClock &wc) {
  const int W = dev.W;
  const ScreenEntry &tp = cal::top(m);
  const bool protect = tp.kind == cal::ConfirmKind::Protect, armed = tp.until > now;
  const int t0 = viewHeader(c, dev, m, protect ? "Proteggi le chiavi" : "Ripristino di fabbrica", wc);
  const char *body = protect
      ? "Scrive nel chip una chiave segreta che non si può più cancellare né leggere. Da lì in poi password del Wi-Fi e abbinamento restano cifrati e valgono solo su questa scheda."
      : "Cancella rete Wi-Fi, abbinamento, impostazioni e sale delle chiavi, poi riavvia. Il display andrà configurato di nuovo dal telefono.";
  wrapT(c, body, 6, t0 + 3, 10, W - 12, 5, 12, Wt(500));
  if (armed) {
    c.fillRect(4, 84, W - 8, 16, INK);
    T(c, "TIENI PREMUTO 3 SECONDI", W / 2.0, 87, 10, Wt(700).center().col(PAPER));
  } else {
    box(c, 4, 84, W - 8, 16, INK, 1);
    T(c, "2X PER CONTINUARE", W / 2.0, 87, 10, Wt(700).center());
  }
  dotsH(c, 0, W, 104);
  T(c, armed ? "TIENI 3 S CONFERMA   3X ANNULLA" : "2X CONTINUA   LUNGA ANNULLA", 4, 109, 8, Px(400).mx(W - 8));
}

// ---------- Liste ----------

void listScreen(Surface &c, const DeviceProfile &dev, const Model &m, const char *title, const cal::MenuList &L, int sel,
                const WallClock &wc) {
  const int W = dev.W, H = dev.H;
  stripS(c, dev, m, title, wc);
  const int rh = 18, y0 = 16, vis = (H - y0) / rh;
  const int n = L.n;
  const int off = clampi(sel - vis / 2, 0, n - vis > 0 ? n - vis : 0);
  for (int q = 0; q < vis && off + q < n; q++) {
    const int i = off + q, y = y0 + q * rh;
    const bool on = i == sel, col = on ? PAPER : INK;
    const cal::MenuItem &it = L.items[i];
    if (on) c.fillRect(0, y, W - 5, rh - 1, INK);
    T(c, it.label, 6, y + 2, 12, Wt(on ? 700 : 600).col(col).mx(R(W * 0.52)));
    drawVal(c, it, W - 10, y + 4, 10, col, 1);
  }
  scrollbar(c, W - 3, y0, H - y0 - 2, off, vis, n);
}

// ---------- Altre schermate ----------

void scanning(Surface &c, const DeviceProfile &dev, const Model &m, int f, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Reti vicine", wc), W = dev.W;
  const int ms = 2, cx = R(W / 2.0), my = t0 + 8;
  avatarS(c, m, nullptr, cx - R(AV_W * ms / 2.0), my, ms, cal::MascotPose::Working, f);
  T(c, "Ricerca reti…", cx, my + 25 * ms + 6, 12, Wt(700).center());
}

// QR vero (M3): stessi angolo e passo del QR finto (x = 4 + px, y = t0 + px, bordo di carta di un
// modulo), ma con i moduli veri: 29 per lato (versione 3) invece di 25, quindi 12 px in più e il testo
// che parte 12 px più a destra.
void drawRealQR(Surface &c, int x, int y, int px, const qr::Bits &q) {
  c.fillRect(x - px, y - px, (q.size + 2) * px, (q.size + 2) * px, PAPER);
  for (int r = 0; r < q.size; r++)
    for (int k = 0; k < q.size; k++)
      if (q.get(k, r)) c.fillRect(x + k * px, y + r * px, px, px, INK);
}

void qrScreen(Surface &c, const DeviceProfile &dev, const Model &m, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Configura Wi-Fi", wc), W = dev.W, H = dev.H;
  const ScreenEntry &tp = cal::top(m);
  const int avail = H - t0 - 4;
  int px = avail / 27;
  if (px > 3) px = 3;
  if (px < 2) px = 2;
  const int x = 4 + px, y = t0 + px;
  char ap[24];
  cal::apSsid(m.sys.boardId, ap, sizeof ap);
  int n = 29;  // versione 3: il QR vero della rete Calamaio-XXXX; nel finto, 29 moduli come lui
  if (m.sys.live) {
    static qr::Bits q;  // ~180 byte: fuori dallo stack
    char payload[96];
    cal::wifiQrText(ap, m.sys.apPass, payload, sizeof payload);
    if (m.sys.apPass[0] && qr::encode(payload, q)) {
      n = q.size;
      drawRealQR(c, x, y, px, q);
    }  // AP non ancora pronto: posto vuoto della stessa misura
  } else {
    drawQR(c, x, y, px, "calamaio-3f2a", n);
  }
  const int tx = x + (n + 1) * px + 8, tw = W - tx - 4;
  T(c, "INQUADRA IL QR", tx, t0 + 2, 8, Px(700).mx(tw));
  char l3[64];
  if (tp.ssid[0]) {
    char up[40];
    upper(up, sizeof up, tp.ssid);
    cal::Sb(l3, sizeof l3).s("3. SCEGLI ").s(up);
  } else {
    cal::scopy(l3, "3. SCEGLI LA RETE");
  }
  char l1[48], apUp[24];
  upper(apUp, sizeof apUp, ap);
  cal::Sb(l1, sizeof l1).s("1. RETE ").s(apUp);
  const char *lines[4] = {l1, "2. APRI 192.168.4.1", l3, "4. SCRIVI LA PASSWORD"};
  for (int q = 0; q < 4; q++) T(c, lines[q], tx, t0 + 18 + q * 13, 8, Px(400).mx(tw));
  // Password della rete Calamaio-XXXX, per chi non riesce a inquadrare il QR (nel finto: K7MZ3QRT, come il simulatore).
  char pw[32];
  if (m.sys.apPass[0]) cal::Sb(pw, sizeof pw).s("PASSWORD ").s(m.sys.apPass);
  else cal::scopy(pw, "AVVIO DELLA RETE…");
  T(c, pw, tx, H - 12, 8, Px(700).mx(tw));
}

void connecting(Surface &c, const DeviceProfile &dev, const Model &m, int f) {
  const ScreenEntry &tp = cal::top(m);
  const int W = dev.W, ms = 3, cx = R(W / 2.0), my = 0 + 6;
  avatarS(c, m, nullptr, cx - R(AV_W * ms / 2.0), my, ms, cal::MascotPose::Working, f);
  char t[96];
  cal::Sb b(t, sizeof t);
  b.s("Connessione a ").s(tp.ssid);
  const int dots = 1 + (f < 0 ? -f : f) % 3;
  for (int k = 0; k < dots; k++) b.ch('.');
  T(c, t, cx, my + 25 * ms + 4, 12, Wt(700).center().mx(W - 20));
}

void oggi(Surface &c, const DeviceProfile &dev, const Model &m, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Oggi", wc), W = dev.W, H = dev.H;
  const cal::Stats &st = m.stats;
  const long long avg = st.waitN ? (long long)floor(double(st.waitSum) / st.waitN / 60000.0 + 0.5) : 0;
  char v[6][32];
  snprintf(v[0], 32, "%d", st.tasks);
  snprintf(v[1], 32, "%d", st.permDisplay);
  snprintf(v[2], 32, "%d", st.permTerminal);
  snprintf(v[3], 32, "%lld min", avg);
  snprintf(v[4], 32, "+%ld −%ld", st.linesAdd, st.linesDel);
  snprintf(v[5], 32, "%.2f $", st.cost);  // toFixed(2), poi la virgola
  for (char *p = v[5]; *p; p++)
    if (*p == '.') *p = ',';
  const char *labels[6] = {"TASK FATTI", "OK DAL DISPLAY", "DAL TERMINALE", "ATTESA MEDIA", "RIGHE", "COSTO STIMATO"};
  const int cols = 2, rows = 3, bottom = H;
  const double gw = double(W) / cols, gh = double(bottom - t0) / rows;
  for (int k = 0; k < 6; k++) {
    const double cx = (k % cols) * gw + gw / 2, y = t0 + (k / cols) * gh;
    const int vs = 16, ls = 8;
    T(c, v[k], cx, y + gh * 0.5 - vs * 0.75, vs, Wt(700).center().mx(int(gw - 10)));
    T(c, labels[k], cx, y + gh * 0.5 + vs * 0.4, ls, Px(700).center().mx(int(gw - 10)));
    if (k % cols < cols - 1) dotsVf(c, R((k % cols + 1) * gw), y + 6, y + gh - 6);
  }
  for (int r = 1; r < rows; r++) dotsH(c, 4, W - 4, R(t0 + gh * r));
}

void storico(Surface &c, const DeviceProfile &dev, const Model &m, Ms now, const WallClock &wc) {
  const ScreenEntry &tp = cal::top(m);
  const int t0 = viewHeader(c, dev, m, dev.sd ? "Storico · SD" : "Storico", wc), W = dev.W, H = dev.H;
  const int fs = 10, lh = fs + 4, bottom = H - 2;
  int vis = (bottom - t0) / lh;
  if (vis < 1) vis = 1;
  const int maxOff = m.log.n - vis > 0 ? m.log.n - vis : 0;
  const int off = tp.off < maxOff ? tp.off : maxOff;
  const int tx = 4, nx = tx + 30, ex = nx + 72;
  for (int q = 0; q < vis && off + q < m.log.n; q++) {
    const cal::LogEntry &e = m.log.at(off + q);
    const int y = t0 + q * lh;
    char hm[8];
    cal::hmAt(wc, now, e.t, hm, sizeof hm);
    T(c, hm, tx, y, fs, Wt(700));
    T(c, e.name, nx, y, fs, Wt(700).mx(ex - nx - 6));
    T(c, e.text, ex, y, fs, Wt(500).mx(W - ex - 10));
  }
  scrollbar(c, W - 4, t0, bottom - t0, off, vis, m.log.n);
}

void batteria(Surface &c, const DeviceProfile &dev, const Model &m, const WallClock &wc) {
  const int W = dev.W, t0 = viewHeader(c, dev, m, "Batteria", wc);
  const int pct = R(m.sys.batt);
  const bool save = cal::powerSave(m);
  const int s = 3, x = 8, y = t0 + 6;
  battIcon(c, x, y, s, pct, INK);
  if (m.sys.usb) sym(c, Sym::Bolt, x + 18 * s, y, s, INK);
  const int fsBig = 22;
  char t[64];
  snprintf(t, sizeof t, "%d%%", pct);
  T(c, t, x + (m.sys.usb ? 25 : 19) * s, y + R(4 * s - fsBig / 2.0), fsBig, Wt(700));
  const int fs = 10, lh = fs + 4, yy = y + 8 * s + 8;
  char l1[64], l2[64];
  cal::autonomyText(m, l1, sizeof l1);
  cal::Sb(l2, sizeof l2).s("Risparmio energetico: ").s(cal::yes(save)).s(m.sys.batt < 15 && !m.set.save && !m.sys.usb ? " (automatico)" : "");
  const char *lines[3] = {m.sys.usb ? "In carica dalla USB-C" : "A batteria", l1, l2};
  for (int k = 0; k < 3; k++) T(c, lines[k], x, yy + k * lh, fs, Wt(k == 2 ? 700 : 600).mx(W - x * 2));
  T(c, "2X RISPARMIO   LUNGA INDIETRO", 4, 110, 8, Px(400).mx(W - 8));
}

void stato(Surface &c, const DeviceProfile &dev, const Model &m, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Stato", wc), W = dev.W, tx = 48;
  const cal::Sys &s = m.sys;
  wifiIcon(c, 10, t0 + 4, 2, s.wifi ? cal::rssiBars(s.rssi) : -1, INK);
  T(c, s.wifi ? s.ssid : "Nessuna rete", tx, t0 + 1, 13, Wt(700).mx(W - tx - 4));
  char t[96];
  if (s.wifi) cal::Sb(t, sizeof t).i(s.rssi).s(" dBm · ").s(s.ip);
  else cal::scopy(t, "Menu > Wi-Fi per configurare");
  T(c, t, tx, t0 + 16, 10, Wt(500).mx(W - tx - 4));
  battIcon(c, 8, t0 + 38, 2, s.batt, INK);
  cal::Sb(t, sizeof t).i(R(s.batt)).s("%").s(s.usb ? " · in carica" : "");
  T(c, t, tx, t0 + 34, 13, Wt(700).mx(W - tx - 4));
  T(c, cal::powerSave(m) ? "risparmio energetico attivo" : "circa 2 giorni con Wi-Fi acceso", tx, t0 + 49, 10, Wt(500).mx(W - tx - 4));
  sym(c, Sym::Plug, 12, t0 + 70, 2, INK);
  if (!s.pc) strike(c, 10, t0 + 68, 9, 2, INK, PAPER);
  T(c, s.pc ? "PC collegato" : "PC non risponde", tx, t0 + 68, 13, Wt(700).mx(W - tx - 4));
  // M5: chiavi diverse tra display e servizio («da rifare») si vedono anche qui.
  T(c, s.pair == cal::PairState::Redo ? "Abbinamento: da rifare" : cal::pcLabel(m), tx, t0 + 83, 10, Wt(500).mx(W - tx - 4));
}

}  // namespace

// 0.10.1: riquadro della mascotte del Riposo (stesse formule di sleepScreen qui sotto).
SleepRect sleepMascotRect(const DeviceProfile &dev, const Model &m, const WallClock &wc) {
  // Riposo disegnato dal tema: il riquadro viene dallo stesso disegno (stessi now e f), senza ricaricare il blocco
  const ThemeDrawInfo &ld = lastThemeDraw();
  if (cal::top(m).id == ScreenId::Sleep && ld.drawn && ld.screen == cal::theme::Screen::Sleep)
    return ld.hasMascot ? SleepRect{ld.mx, ld.my, ld.mw, ld.mh} : SleepRect{0, 0, 0, 0};
  const int W = dev.W, H = dev.H, pad = 8;
  int ms = int(floor(H * 0.55 / 25));
  if (ms < 2) ms = 2;
  const bool wk = m.work.on || cal::workDone(m, wc);
  SleepRect r;
  r.x = W - AV_W * ms - pad;
  r.y = wk ? H - pad - 25 * ms - 4 : R((H - 25 * ms) / 2.0);
  r.w = AV_W * ms;
  r.h = 25 * ms;
  return r;
}

namespace {

void sleepScreen(Surface &c, const DeviceProfile &dev, const Model &m, int f, const WallClock &wc) {
  const int W = dev.W, H = dev.H;
  const int fs = R(H * 0.36), pad = 8;
  char clk[8], date[48], t[64];
  clockStr(m, wc, clk, sizeof clk);
  T(c, clk, pad, pad, fs, Wt(700));
  int ds = R(fs * 0.24);
  if (ds < 10) ds = 10;
  if (m.sys.wallOk) cal::dateStr(wc, date, sizeof date);
  else date[0] = 0;  // 0.10.1: senza l'ora vera niente data
  T(c, date, pad, pad + fs + R(fs * 0.12), ds, Wt(600).mx(R(W * 0.6)));
  int ms = int(floor(H * 0.55 / 25));
  if (ms < 2) ms = 2;
  // Giornata di lavoro (0.9.0): attiva, o finita da meno di un'ora. In alto a destra «MANCANO» e il conto (come un
  // secondo orologio), sotto «fino alle 18:00», la mascotte più in basso; sotto la data la barra della giornata
  // (dall'orario d'inizio alla fine). Alla fine «GIORNATA» / «FINITA», barra piena, mascotte contenta.
  const bool wk = m.work.on || cal::workDone(m, wc), done = !m.work.on;
  const cal::MascotPose wpose = done && wk ? cal::MascotPose::Done : cal::MascotPose::Idle;
  if (wk) {
    const int xr = W - pad;
    char hh[8], v[24];
    cal::hmLocal(m.work.endLocal, hh, sizeof hh);
    T(c, done ? "GIORNATA" : "MANCANO", xr, pad + 2, 8, Px(700).right());
    if (done) cal::scopy(v, "FINITA");
    else cal::workLeftText(m, wc, true, v, sizeof v);
    T(c, v, xr, pad + 12, 18, Wt(700).right());
    cal::Sb(t, sizeof t).s(done ? "alle " : "fino alle ").s(hh);
    T(c, t, xr, pad + 33, 10, Wt(600).right());
    avatarS(c, m, nullptr, W - AV_W * ms - pad, H - pad - 25 * ms - 4, ms, wpose, f);
    bigBar(c, pad, 74, W - AV_W * ms - pad * 3, 8, done ? 100 : cal::workFrac(m, wc) * 100);
  } else {
    avatarS(c, m, nullptr, W - AV_W * ms - pad, R((H - 25 * ms) / 2.0), ms, wpose, f);
  }
  int att = 0;
  for (int k = 0; k < m.nSessions; k++) att += cal::isAttn(m.sessions[k]);
  cal::Sb b(t, sizeof t);
  b.i(m.nSessions).s(" sessioni · ");
  if (att) b.i(att).s(" ti aspettano");
  else b.s("nessuna attesa");
  T(c, t, pad, H - pad - ds, ds, Wt(600).mx(R(W * 0.6)));
  // M6: senza cavo il Riposo è sonno profondo; col cavo resta acceso. 0.10.1: in sonno l'ora si aggiorna con un risveglio
  // breve a ogni minuto (ogni 5 con il risparmio energetico), con un parziale e senza Wi-Fi; il Wi-Fi ogni 15 minuti.
  const int ps = 8;
  T(c, m.sys.usb ? "COL CAVO RESTO ACCESO" : "UN TASTO MI SVEGLIA", pad, H - pad - ds - ps - 6, ps, Px(700).mx(R(W * 0.6)));
}

// M6: batteria sotto la soglia critica senza cavo: ultima schermata prima del sonno profondo senza timer.
void battLowScreen(Surface &c, const DeviceProfile &dev, const Model &m) {
  const int W = dev.W;
  avatarS(c, m, nullptr, 4, 22, 3, cal::MascotPose::Tired, 0);
  const int x = 92, w = W - x - 4, big = 18, fs = 10;
  int y = 14;
  T(c, "BATTERIA", x, y, big, Wt(700).mx(w));
  y += big + 2;
  T(c, "SCARICA", x, y, big, Wt(700).mx(w));
  y += big + 6;
  T(c, "Collega la USB-C", x, y, fs, Wt(600).mx(w));
  y += fs + 4;
  T(c, "poi premi il tasto", x, y, fs, Wt(600).mx(w));
  T(c, "SPENTO PER PROTEGGERLA", x, 100, 8, Px(700).mx(w));
}

// M6: calibrazione della batteria (Menu › Batteria › Calibra la batteria).
void calibScreen(Surface &c, const DeviceProfile &dev, const Model &m, int f) {
  const int W = dev.W;
  const cal::Calib &k = m.calib;
  const bool done = k.phase == cal::CalibPhase::Done, wait = k.phase == cal::CalibPhase::Wait;
  avatarS(c, m, nullptr, 4, 22, 3, done ? cal::MascotPose::Done : cal::MascotPose::Working, f);
  const int x = 92, w = W - x - 4, big = 14, fs = 10, px = 8;
  int y = 6;
  char t[64], a[24], b2[24];
  if (done) cal::scopy(t, "CALIBRAZIONE FATTA");
  else if (wait) cal::scopy(t, "CALIBRAZIONE");
  else cal::Sb(t, sizeof t).s("CALIBRAZIONE · FASE ").i(k.phase == cal::CalibPhase::Max ? 1 : 2).s("/2");
  T(c, t, x, y, px, Px(700).mx(w));
  y += px + 6;
  T(c, done ? "Fatto" : wait ? "Stacca il cavo" : k.phase == cal::CalibPhase::Max ? "Massimo consumo" : "Attesa", x, y, big, Wt(700).mx(w));
  y += big + 6;
  char lines[2][64];
  if (done) {
    cal::rateText(m.bat.cal.active, a, sizeof a);
    cal::Sb(lines[0], sizeof lines[0]).s("Attivo ").s(a);
    cal::rateText(m.bat.cal.idle, a, sizeof a);
    cal::Sb(lines[1], sizeof lines[1]).s("Attesa ").s(a);
  } else if (wait) {
    cal::scopy(lines[0], "La prova parte da sola");
    cal::scopy(lines[1], "quando stacchi la USB-C");
  } else {
    const int el = int(k.ms / 60000);
    if (k.paused) cal::scopy(lines[0], "In pausa: stacca il cavo");
    else cal::Sb(lines[0], sizeof lines[0]).i(el).s(" min · ne mancano ").i(60 - el > 0 ? 60 - el : 0);
    double r = 0;
    if (k.mv) cal::voltText(k.mv, a, sizeof a);
    else cal::scopy(a, "— V");
    if (cal::calRate(k.h, k.p, k.n, r)) cal::rateText(r, b2, sizeof b2);
    else cal::scopy(b2, "%/h in corso");
    cal::Sb(lines[1], sizeof lines[1]).s(a).s(" · ").s(b2);
  }
  for (int q = 0; q < 2; q++) {
    T(c, lines[q], x, y, fs, Wt(600).mx(w));
    y += fs + 4;
  }
  T(c, done ? "LUNGA INDIETRO" : "3X ANNULLA", x, 100, 8, Px(700).mx(w));
}

// ---------- aggiornamento remoto (docs/OTA.md §1.6, AVATAR.md §12): otaScreen() del simulatore ----------
// Barra a tutta larghezza; la mascotte predefinita a scala 2 (56×50) corre sopra: riga 24 sul bordo superiore
// della barra, colonna 27 sul fronte della parte riempita, ferma al bordo sinistro sotto i 56 px di avanzamento.
const int kOtaBarX = 6, kOtaBarY = 72, kOtaBarW = 238, kOtaBarH = 14, kOtaRunS = 2;

void kbText(uint32_t n, char *out, size_t cap) {  // KB arrotondati, migliaia separate da uno spazio
  char d[16];
  const unsigned long v = (n + 512) / 1024;
  snprintf(d, sizeof d, "%lu", v);
  const int len = int(strlen(d));
  size_t k = 0;
  for (int i = 0; i < len && k + 2 < cap; i++) {
    if (i && (len - i) % 3 == 0) out[k++] = ' ';
    out[k++] = d[i];
  }
  out[k] = 0;
}

const char *otaLabel(cal::OtaPhase p) {
  switch (p) {
    case cal::OtaPhase::Receive: return "RICEZIONE";
    case cal::OtaPhase::Verify: return "VERIFICA";
    case cal::OtaPhase::Write: return "SCRITTURA";
    case cal::OtaPhase::Reboot: return "RIAVVIO";
    case cal::OtaPhase::Done: return "FATTO";
    default: return "";
  }
}

void otaResult(Surface &c, const DeviceProfile &dev, const Model &m) {
  const cal::OtaView &o = m.ota;
  const bool err = o.phase == cal::OtaPhase::Error;
  const int x = 92, w = dev.W - x - 4;
  avatarS(c, m, nullptr, 4, 22, 3, err ? cal::MascotPose::Error : cal::MascotPose::Idle, 0);
  T(c, "AGGIORNAMENTO", x, 6, 8, Px(700).mx(w));
  T(c, err ? "NON RIUSCITO" : "RIFIUTATO", x, 18, 16, Wt(700).mx(w));
  int y = 40;
  if (err) {
    T(c, "Resta la versione di prima", x, y, 10, Wt(600).mx(w));
    y += 14;
  }
  wrapT(c, o.why, x, y, 10, w, err ? 3 : 4, 13, Wt(600));
  T(c, "LUNGA INDIETRO", x, 100, 8, Px(700).mx(w));
}

void otaScreen(Surface &c, const DeviceProfile &dev, const Model &m) {
  const cal::OtaView &o = m.ota;
  const int W = dev.W;
  if (o.phase == cal::OtaPhase::Error || o.phase == cal::OtaPhase::Rejected) { otaResult(c, dev, m); return; }
  char t[64];
  T(c, "AGGIORNAMENTO", 6, 4, 14, Wt(700));
  cal::Sb(t, sizeof t).s(o.from).s(" → ").s(o.to);
  T(c, t, W - 6, 7, 10, Wt(600).right());
  const int pct = cal::otaPct(o);
  const bool end = o.phase == cal::OtaPhase::Reboot || o.phase == cal::OtaPhase::Done;
  bigBar(c, kOtaBarX, kOtaBarY, kOtaBarW, kOtaBarH, pct);
  const int front = kOtaBarX + 2 + R((kOtaBarW - 4) * double(clampi(pct, 0, 100)) / 100);
  const int ox = clampi(front - AV_W * kOtaRunS, 0, W - AV_W * kOtaRunS), oy = kOtaBarY - kAvatarH * kOtaRunS;
  // Passo k → fotogramma k mod 4 sul profilo fluido, k mod 2 sul ridotto (poi mod frames in drawAvatar).
  const int fr = o.step % (dev.fps >= 3 ? 4 : 2);
  drawAvatar(c, cal::defaultAvatar(m), ox, oy, kOtaRunS, end ? Pose::Done : Pose::Run, fr, INK);
  accOverlay(c, m, cal::defaultAvatar(m), ox, oy, kOtaRunS, end ? Pose::Done : Pose::Run, fr, INK);
  if (end) cal::scopy(t, "Riavvio…");
  else cal::Sb(t, sizeof t).i(pct).s("%");
  T(c, t, 6, 92, 16, Wt(700));
  char a[16], b[16];
  kbText(o.done, a, sizeof a);
  kbText(o.total, b, sizeof b);
  cal::Sb(t, sizeof t).s(a).s(" / ").s(b).s(" KB");
  T(c, t, W - 6, 96, 10, Wt(600).right());
  T(c, "NON SPEGNERE", 6, 111, 8, Px(700));
  T(c, otaLabel(o.phase), W - 6, 111, 8, Px(700).right());
}

// ---------- 0.12.0: giochi Game Boy (schermate nuove, non nel simulatore: testi in DECISIONI) ----------
// Avvio della partita: Wi-Fi spento, ROM, salvataggio, pannello veloce (qualche secondo).
void gbLoadScreen(Surface &c, const DeviceProfile &dev, const Model &m) {
  const int W = dev.W;
  T(c, "GIOCO", 6, 4, 14, Wt(700));
  T(c, m.gb.title[0] ? m.gb.title : "Game Boy", 6, 34, 13, Wt(700).mx(W - 12));
  T(c, "Avvio…", 6, 56, 10, Wt(600));
  wrapT(c, "Durante la partita Wi-Fi e avvisi sono in pausa: le richieste di permesso aspettano nel terminale.", 6, 76,
        10, W - 12, 3, 13, Wt(600));
}

// Titolo del gioco nella banda destra (ASCII, ≤ 16 caratteri): fino a 3 righe, a capo sugli spazi; una parola più
// larga della banda si spezza fra due lettere (con 41 px «POKEMON» non ci sta intero). L'ultima riga si tronca.
void gbTitle(Surface &c, const char *s, int x, int y, int maxW) {
  char line[24], fit[32];
  for (int k = 0; k < 3 && *s; k++, y += 12) {
    while (*s == ' ') s++;
    int n = 0, sp = 0;  // caratteri che stanno; ultimo spazio dentro
    while (s[n] && n < int(sizeof line) - 1) {
      memcpy(line, s, n + 1);
      line[n + 1] = 0;
      fitText(fit, sizeof fit, line, 10, 700, false, maxW);  // stesso criterio di T
      if (strcmp(fit, line) != 0) break;
      if (s[n] == ' ') sp = n;
      n++;
    }
    if (s[n] && sp > 0 && k < 2) n = sp;  // a capo sullo spazio
    if (n == 0) n = 1;
    if (k == 2) n = int(strlen(s));        // ultima riga: tutto, troncato da T
    if (n > int(sizeof line) - 1) n = sizeof line - 1;
    memcpy(line, s, n);
    line[n] = 0;
    T(c, line, x, y, 10, Wt(700).mx(maxW));
    s += n;
  }
}

// Partita: solo le due bande ai lati (45 px, gb_frame); la finestra al centro (x 45–204) resta bianca e la riempie
// core/gb_frame con il fotogramma del gioco. Niente toast: coprirebbero il gioco. Ogni testo sta nella sua banda
// (0.12.5: bande più strette, testi spezzati o senza spazi; test_gb_ui lo controlla).
void gbBands(Surface &c, const DeviceProfile &dev, const Model &m) {
  const cal::GbView &g = m.gb;
  const int W = dev.W, H = dev.H;
  const int L = cal::gb::kWinX, R0 = cal::gb::kWinX + cal::gb::kWinW;  // finestra del gioco: x 45–204
  const int rx = R0 + 3;                                                // testi a destra: x 208–248
  dotsV(c, L - 2, 0, H - 1);
  dotsV(c, R0 + 1, 0, H - 1);
  battIcon(c, 4, 4, 1, m.sys.batt, INK);
  char t[16];
  cal::Sb(t, sizeof t).i(R(m.sys.batt)).s("%");
  T(c, t, 4, 15, 10, Wt(600));
  if (g.lowBand) {
    c.fillRect(0, 30, L - 3, 26, INK);
    T(c, "Batteria", 2, 32, 10, Wt(700).col(PAPER));
    T(c, "bassa", 2, 43, 10, Wt(700).col(PAPER));
  }
  T(c, "AVVISI", 3, 86, 8, Px(700));
  T(c, "IN", 3, 97, 8, Px(700));
  T(c, "PAUSA", 3, 108, 8, Px(700));
  gbTitle(c, g.title[0] ? g.title : "Game Boy", rx, 4, W - rx - 1);
  T(c, "START+", rx, 86, 8, Px(700));
  T(c, "SELECT", rx, 97, 8, Px(700));
  T(c, "=PAUSA", rx, 108, 8, Px(700));
}

void boot(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now) {
  const int W = dev.W, H = dev.H;
  const ScreenEntry &tp = cal::top(m);
  const int ms = 3;
  double el = 1.0 - double(tp.until - now) / 2600.0;
  el = el < 0 ? 0 : el > 1 ? 1 : el;
  const int bw = 27 * ms, x = 8, y = R((H - 25 * ms) / 2.0);
  avatarS(c, m, nullptr, x, y, ms, el > 0.85 ? cal::MascotPose::Done : cal::MascotPose::Working, f);
  const int tx = x + bw + 10, fs = 18, ss = R(fs * 0.55);
  T(c, "CALAMAIO OS", tx, y + 4, fs, Wt(700).mx(W - tx - 6));
  char ver[24];
  cal::Sb(ver, sizeof ver).s("versione ").s(cal::uiVersion(m));
  T(c, ver, tx, y + fs + 8, ss, Wt(600));
  char wifi[64];
  // Con la rete l'avvio mostra la rete salvata (non ancora collegata); nel finto ssid è vuoto solo se wifi è falso.
  cal::Sb(wifi, sizeof wifi).s("Wi-Fi ").s(m.sys.ssid[0] ? m.sys.ssid : "da configurare");
  char pc[64];
  cal::Sb(pc, sizeof pc).s("PC ").s(cal::pcLabel(m));
  const char *names[3] = {"Schermo", wifi, pc};
  const double th[3] = {0.15, 0.45, 0.75};
  for (int k = 0; k < 3; k++) {
    const int yy = y + fs * 2 + 12 + k * R(fs * 0.75);
    char t[96];
    cal::Sb(t, sizeof t).s(names[k]).s(el >= th[k] ? "  ok" : "  …");
    T(c, t, tx, yy, ss, Wt(el >= th[k] ? 700 : 500).mx(W - tx - 6));
  }
}

void offline(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  const int W = dev.W;
  char last[8], t[48];
  if (m.sys.wallOk) cal::hmAt(wc, now, m.sys.lastPc ? m.sys.lastPc : now, last, sizeof last);
  else cal::scopy(last, "--:--");  // 0.10.1: senza l'ora vera nessun orario inventato
  avatarS(c, m, nullptr, 4, 22, 3, cal::MascotPose::Idle, f);
  const int x = 92;
  T(c, "PC NON", x, 14, 18, Wt(700));
  T(c, "RAGGIUNGIBILE", x, 34, 14, Wt(700).mx(W - x - 4));
  cal::Sb(t, sizeof t).s("Ultimo dato alle ").s(last);
  T(c, t, x, 56, 10, Wt(600).mx(W - x - 4));
  T(c, "Riprovo ogni 30 secondi", x, 70, 10, Wt(600).mx(W - x - 4));
  T(c, "3X MENU", x, 100, 8, Px(700));
  alertsS(c, m, now, W - 4, 4);
}

void limiti(Surface &c, const DeviceProfile &dev, const Model &m, Ms now, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Limiti", wc), W = dev.W, H = dev.H;
  const cal::Limits &L = m.limits;
  const char *names[2] = {"Ultime 5 ore", "Ultimi 7 giorni"};
  const double vals[2] = {L.h5, L.d7};
  const Ms resets[2] = {L.h5Reset, L.d7Reset};
  const bool has[2] = {L.hasH5, L.hasD7};  // dal PC una finestra può mancare
  for (int k = 0; k < 2; k++) {
    const int y = t0 + 2 + k * 38;
    char up[32], t[64];
    upper(up, sizeof up, names[k]);
    T(c, up, 6, y, 8, Px(700));
    if (has[k]) snprintf(t, sizeof t, "%d%%", R(vals[k]));
    else cal::scopy(t, "—");
    T(c, t, W - 6, y - 2, 13, Wt(700).right());
    bigBar(c, 6, y + 12, W - 12, 9, has[k] ? vals[k] : 0);
    if (has[k]) cal::resetLabel(wc, now, resets[k], t, sizeof t);
    else cal::scopy(t, "nessun dato dal PC");
    T(c, t, 6, y + 25, 10, Wt(600).mx(W - 12));
  }
  if (m.nSessions == 0) return;
  const Session *hi = &m.sessions[0];
  for (int k = 0; k < m.nSessions; k++)
    if (m.sessions[k].ctx > hi->ctx) hi = &m.sessions[k];
  char up[40], t[80];
  upper(up, sizeof up, hi->name);
  cal::Sb(t, sizeof t).s("CONTESTO: ").s(up).s(" ").i(hi->ctx).s("%");
  T(c, t, 6, H - 11, 8, Px(400).mx(W - 12));
}

void focusScreen(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Concentrazione", wc), W = dev.W;
  const bool on = cal::focusOn(m, now);
  long long left = m.focus.len;
  if (on) {
    const Ms d = m.focus.until - now;  // > 0 quando on
    left = (d + 59999) / 60000;        // Math.ceil(d / 60000)
    if (left < 0) left = 0;
  }
  const int ms = 3, mx = 4, my = t0 + 4;
  avatarS(c, m, nullptr, mx, my, ms, on ? cal::MascotPose::Working : cal::MascotPose::Idle, f);
  const int tx = mx + 27 * ms + 10, tw = W - tx - 4;
  const int big = 40, fs = 10;
  char t[64];
  snprintf(t, sizeof t, "%lld min", left);
  T(c, t, tx, my, big, Wt(700).mx(tw));
  int y = my + big + 6;
  char l0[64], l1[64];
  int nl;
  if (on) {
    char hm[8];
    cal::hmAt(wc, now, m.focus.until, hm, sizeof hm);
    cal::scopy(l0, "Avvisi in silenzio");
    cal::Sb(l1, sizeof l1).s("fino alle ").s(hm);
    nl = 2;
  } else {
    cal::Sb(l0, sizeof l0).s("Pronto per ").i(m.focus.len).s(" minuti");
    nl = 1;
  }
  const char *ls[2] = {l0, l1};
  for (int k = 0; k < nl; k++) {
    char lines[2][96];
    const int n = wrapLines(lines, 2, ls[k], fs, tw, 600, false);
    for (int q = 0; q < n; q++) {
      T(c, lines[q], tx, y, fs, Wt(600).mx(tw));
      y += fs + 4;
    }
  }
  T(c, on ? "2X FERMA   LUNGA INDIETRO" : "2X INIZIA   LUNGA INDIETRO", 4, 110, 8, Px(400).mx(W - 8));
}

void guest(Surface &c, const DeviceProfile &dev, const Model &m, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Wi-Fi ospiti", wc), W = dev.W, H = dev.H;
  const int avail = H - t0 - 4;
  int px = avail / 27;
  if (px > 3) px = 3;
  if (px < 2) px = 2;
  const int x = 4 + px, y = t0 + px;
  drawQR(c, x, y, px, "ospiti-wifi", 25);
  const int tx = x + 26 * px + 8, tw = W - tx - 4, f1 = 16, f2 = 11;
  int yy = y - px;
  T(c, "Rete ospiti", tx, yy, f1, Wt(700).mx(tw));
  yy += f1 + 6;
  T(c, "Ospiti", tx, yy, f2 + 2, Wt(700).mx(tw));
  yy += f2 + 6;
  char lines[3][96];
  const int n = wrapLines(lines, 3, "Inquadra il codice con la fotocamera per collegarti senza scrivere la password.", f2, tw, 500, false);
  for (int q = 0; q < n; q++) {
    T(c, lines[q], tx, yy, f2, Wt(500).mx(tw));
    yy += f2 + 3;
  }
  T(c, "QR DI ESEMPIO", tx, H - 12, 8, Px(600).mx(tw));
}

void servizi(Surface &c, const DeviceProfile &dev, const Model &m, const WallClock &wc) {
  const ScreenEntry &tp = cal::top(m);
  const int t0 = viewHeader(c, dev, m, "Servizi e PR", wc), W = dev.W, H = dev.H;
  // Righe: SERVIZI, i servizi, PULL REQUEST, le PR.
  struct Row { const char *head; Icon ok; char a[48]; char b[64]; };
  Row rows[2 + cal::kServiceCap + cal::kPrCap];
  int n = 0;
  rows[n++].head = "SERVIZI";
  for (int k = 0; k < m.nServices; k++) {
    Row &r = rows[n++];
    const cal::Service &s = m.services[k];
    r.head = nullptr;
    r.ok = s.ok ? Icon::Done : Icon::Error;
    cal::scopy(r.a, s.name);
    if (s.ok) cal::Sb(r.b, sizeof r.b).i(s.ms).s(" ms");
    else cal::scopy(r.b, "non risponde");
  }
  rows[n++].head = "PULL REQUEST";
  for (int k = 0; k < m.nPrs; k++) {
    Row &r = rows[n++];
    const cal::Pr &p = m.prs[k];
    r.head = nullptr;
    r.ok = p.checks == cal::PrChecks::Ok ? Icon::Done : p.checks == cal::PrChecks::Ko ? Icon::Error : Icon::Working0;
    cal::Sb(r.a, sizeof r.a).s(p.repo).s(" #").i(p.n);
    cal::scopy(r.b, p.checks == cal::PrChecks::Run ? "controlli in corso" : p.title);
  }
  const int fs = 10, lh = fs + 5, bottom = H - 2;
  int vis = (bottom - t0) / lh;
  if (vis < 1) vis = 1;
  const int off = clampi(tp.off, 0, n - vis > 0 ? n - vis : 0);
  const int x = 4, s = 1;
  for (int q = 0; q < vis && off + q < n; q++) {
    const Row &r = rows[off + q];
    const int y = t0 + q * lh;
    if (r.head) {
      T(c, r.head, x, y + 1, 8, Px(700));
      dotsH(c, x + ceilW(r.head, 8, 700, true) + 8, W - x - 8, y + R(fs / 2.0));
      continue;
    }
    icon(c, r.ok, x, y + R((fs - 7 * s) / 2.0), s, INK);
    T(c, r.a, x + 7 * s + 6, y, fs, Wt(700).mx(R(W * 0.4)));
    T(c, r.b, W - x - 8, y, fs, Wt(500).right().mx(R(W * 0.45)));
  }
  scrollbar(c, W - 4, t0, bottom - t0, off, vis, n);
}

bool sameDay(int64_t a, int64_t b) {
  const cal::Civil x = cal::civilFrom(a), y = cal::civilFrom(b);
  return x.year == y.year && x.month == y.month && x.day == y.day;
}

void agenda(Surface &c, const DeviceProfile &dev, const Model &m, Ms now, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Agenda", wc), W = dev.W, H = dev.H;
  int idx[3], n = 0;
  for (int k = 0; k < m.nAgenda; k++)
    if (m.agenda[k].t > now - 30 * 60000) idx[n++] = k;
  if (!n) {
    T(c, "Nessun appuntamento", W / 2.0, t0 + 20, 12, Wt(700).center());
    return;
  }
  const cal::AgendaItem &nx = m.agenda[idx[0]];
  long long mins = (long long)floor(double(nx.t - now) / 60000.0 + 0.5);
  if (mins < 0) mins = 0;
  const int x = 6, f1 = 22, f2 = 12, col2 = 70;
  int y = t0 + 2;
  char when[8], t[64];
  cal::hmAt(wc, now, nx.t, when, sizeof when);
  T(c, when, x, y, f1, Wt(700));
  if (mins < 60) cal::Sb(t, sizeof t).s("tra ").i(mins).s(" min");
  else cal::Sb(t, sizeof t).s("tra ").i(mins / 60).s(" h ").pad2(mins % 60);
  T(c, t, x + ceilW(when, f1, 700, false) + 10, y + f1 - f2 - 2, f2, Wt(600));
  y += f1 + 4;
  T(c, nx.title, x, y, f2 + 2, Wt(700).mx(W - x * 2));
  y += f2 + 8;
  dotsH(c, x, W - x, y);
  y += 5;
  for (int q = 1; q < n && q < 3; q++) {
    const cal::AgendaItem &a = m.agenda[idx[q]];
    const bool same = sameDay(cal::localAt(wc, now, a.t), wc.localMs);
    char hm[8];
    cal::hmAt(wc, now, a.t, hm, sizeof hm);
    cal::Sb(t, sizeof t).s(same ? "" : "domani ").s(hm);
    T(c, t, x, y, f2, Wt(700));
    T(c, a.title, x + col2, y, f2, Wt(500).mx(W - x * 2 - col2));
    y += f2 + 4;
  }
  T(c, "DA UN CALENDARIO ICS", x, H - 11, 8, Px(400).mx(W - x * 2));
}

void ambiente(Surface &c, const DeviceProfile &dev, const Model &m, int f, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Ambiente", wc), W = dev.W;
  const bool hasCo2 = m.acc, hasTh = dev.sensor || m.acc;
  if (!hasTh) {
    const int ms = 2, mx = 6;
    avatarS(c, m, nullptr, mx, t0 + 6, ms, cal::MascotPose::Idle, f);
    const int tx = mx + 27 * ms + 8, tw = W - tx - 6, fs = 11;
    int y = t0 + 4;
    T(c, "Nessun sensore", tx, y, 14, Wt(700).mx(tw));
    y += 14 + 8;
    char lines[5][96];
    const int n = wrapLines(lines, 5,
                            "Collega un sensore di CO2, temperatura e umidità ai pin I2C, per esempio un SCD40, e attiva Accessori.",
                            fs, tw, 500, false);
    for (int q = 0; q < n; q++) {
      T(c, lines[q], tx, y, fs, Wt(500).mx(tw));
      y += fs + 3;
    }
    return;
  }
  const int co2 = R(m.air.co2);
  const bool bad = hasCo2 && co2 > 1000;
  const int ms = 3, mx = 4;
  avatarS(c, m, nullptr, mx, t0 + 4, ms, bad ? cal::MascotPose::Stuffy : cal::MascotPose::Idle, f);
  const int tx = mx + 27 * ms + 10, tw = W - tx - 6, big = 26, fs = 11;
  int y = t0 + 2;
  char t[80], tf[16];
  if (hasCo2) {
    snprintf(t, sizeof t, "%d ppm", co2);
    T(c, t, tx, y, big, Wt(700).mx(tw));
    y += big + 4;
    T(c, bad ? "Apri la finestra" : co2 > 800 ? "Aria discreta" : "Aria buona", tx, y, fs + 2, Wt(700).mx(tw));
    y += fs + 2 + 8;
  } else {
    T(c, "CO2 non misurata", tx, y, fs + 2, Wt(700).mx(tw));
    y += fs + 2 + 8;
  }
  cal::fmt1(m.sys.temp, tf, sizeof tf);
  cal::Sb(t, sizeof t).s(tf).s(" °C  ·  ").i(R(m.sys.rh)).s("% umidità");
  T(c, t, tx, y, fs, Wt(600).mx(tw));
  y += fs + 6;
  T(c, dev.sensor ? (hasCo2 ? "Sensore interno SHT40 e CO2 esterno" : "Sensore interno SHT40") : "Sensore esterno sui pin I2C", tx, y,
    10, Wt(500).mx(tw));
}

void riepilogo(Surface &c, const DeviceProfile &dev, const Model &m, Ms now, const WallClock &wc) {
  const int W = dev.W;
  int fin = 0, nWait = 0, firstWait = -1, last = -1;
  for (int k = 0; k < m.nSessions; k++) {
    const Session &s = m.sessions[k];
    if (s.state == SessionState::Done) fin++;
    if (cal::isAttn(s)) { if (firstWait < 0) firstWait = k; nWait++; }
    if (s.summary[0]) last = k;
  }
  char off[8], t[160];
  cal::hmAt(wc, now, m.sys.offAt ? m.sys.offAt : now, off, sizeof off);
  char waitTxt[40];
  if (nWait == 0) cal::scopy(waitTxt, "nessuna ti aspettava");
  else if (nWait == 1) cal::scopy(waitTxt, "1 ti aspettava");
  else cal::Sb(waitTxt, sizeof waitTxt).i(nWait).s(" ti aspettavano");
  c.fillRect(0, 0, W, 15, INK);
  cal::Sb(t, sizeof t).s("PC SPENTO ALLE ").s(off);
  T(c, t, 4, 4, 8, Px(700).col(PAPER).mx(W - 8));
  avatarS(c, m, nullptr, 2, 22, 3, cal::MascotPose::Idle, 0);
  const int x = 90, w = W - x - 4;
  cal::Sb(t, sizeof t).i(fin).s(fin == 1 ? " finita" : " finite");
  T(c, t, x, 20, 16, Wt(700).mx(w));
  T(c, waitTxt, x, 40, 12, Wt(600).mx(w));
  if (nWait) {
    const Session &s = m.sessions[firstWait];
    char lo[48];
    lower(lo, sizeof lo, cal::stateInfo(s.state).longText);
    cal::Sb(t, sizeof t).s(s.name).s(" · ").s(lo);
    T(c, t, x, 56, 10, Wt(500).mx(w));
  }
  if (last >= 0) {
    const Session &s = m.sessions[last];
    cal::Sb(t, sizeof t).s(s.name).s(": ").s(s.summary);
    wrapT(c, t, x, 72, 10, w, 3, 12, Wt(500));
  }
}

// ---------- 0.9.0: gioco «Alto o basso» (gameScreen del simulatore) ----------
// Carta a sinistra: riquadro con angoli arrotondati in pixel (bordo 2 px), valore grande con cifre e lettere 5×7 a
// scala 5, indice piccolo in due angoli. Al centro serie, record ed esito; a destra la mascotte predefinita.
struct CardGlyph { char ch; const char *rows[7]; };
const CardGlyph kCardGlyphs[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {'1', {".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
};
const CardGlyph *cardGlyph(char ch) {
  for (const CardGlyph &g : kCardGlyphs)
    if (g.ch == ch) return &g;
  return nullptr;
}
const int kCardX = 8, kCardW = 58, kCardH = 86, kCardS = 5;

void cardRank(Surface &c, const char *txt, int cx, int cy, int s) {
  const CardGlyph *gs[2] = {nullptr, nullptr};
  int n = 0, cols = 0;
  for (const char *p = txt; *p && n < 2; p++)
    if ((gs[n] = cardGlyph(*p))) { cols += int(strlen(gs[n]->rows[0])); n++; }
  if (!n) return;
  cols += n - 1;
  int x = cx - int(floor(cols * s / 2.0));
  const int y = cy - int(floor(7 * s / 2.0));
  for (int k = 0; k < n; k++) {
    const int w = int(strlen(gs[k]->rows[0]));
    for (int r = 0; r < 7; r++)
      for (int q = 0; q < w; q++)
        if (gs[k]->rows[r][q] == '#') c.fillRect(x + q * s, y + r * s, s, s, INK);
    x += (w + 1) * s;
  }
}

void cardBox(Surface &c, int x, int y, int w, int h) {
  c.fillRect(x + 3, y, w - 6, 2, INK);
  c.fillRect(x + 3, y + h - 2, w - 6, 2, INK);
  c.fillRect(x, y + 3, 2, h - 6, INK);
  c.fillRect(x + w - 2, y + 3, 2, h - 6, INK);
  c.fillRect(x + 1, y + 1, 2, 2, INK);
  c.fillRect(x + w - 3, y + 1, 2, 2, INK);
  c.fillRect(x + 1, y + h - 3, 2, 2, INK);
  c.fillRect(x + w - 3, y + h - 3, 2, 2, INK);
}

void gameScreen(Surface &c, const DeviceProfile &dev, const Model &m, int f, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "Alto o basso", wc), W = dev.W;
  const cal::Game &g = m.game;
  const char *r = cal::kRanks[g.card >= 0 && g.card <= 13 ? g.card : 0];
  const int y = t0 + 3;
  cardBox(c, kCardX, y, kCardW, kCardH);
  T(c, r, kCardX + 5, y + 5, 8, Px(700));
  T(c, r, kCardX + kCardW - 5, y + kCardH - 13, 8, Px(700).right());
  cardRank(c, r, kCardX + kCardW / 2, y + kCardH / 2, kCardS);
  avatarS(c, m, nullptr, W - AV_W * 2 - 6, t0 + 4, 2, cal::gamePose(g), f);
  const int tx = kCardX + kCardW + 10, tw = W - AV_W * 2 - 10 - tx;
  T(c, g.over ? "FINE · SERIE" : "SERIE", tx, t0 + 4, 8, Px(700).mx(tw));
  char t[64];
  snprintf(t, sizeof t, "%d", g.streak);
  T(c, t, tx, t0 + 14, 40, Wt(700).mx(tw));
  if (g.over && g.streak > g.startBest) T(c, "NUOVO RECORD!", tx, t0 + 58, 8, Px(700).mx(tw));
  else T(c, cal::Sb(t, sizeof t).s("record ").i(g.best).str(), tx, t0 + 56, 10, Wt(600).mx(tw));
  const char *pr = cal::kRanks[g.prev >= 0 && g.prev <= 13 ? g.prev : 0];
  cal::Sb b(t, sizeof t);
  if (g.last == cal::GameResult::Win) b.s("GIUSTO: ").s(pr).s(" → ").s(r);
  else if (g.last == cal::GameResult::Tie) b.s("PARI: ").s(pr).s(" e ").s(r);
  else if (g.last == cal::GameResult::Lose) b.s("SBAGLIATO: ").s(pr).s(" → ").s(r);
  if (t[0]) T(c, t, tx, t0 + 72, 13, Wt(700).mx(W - tx - 6));
  else T(c, "Più alta o più bassa?", tx, t0 + 73, 12, Wt(600).mx(W - tx - 6));
  T(c, g.over ? "1X O 2X ANCORA   LUNGA ESCI" : "1X ALTA   2X BASSA   LUNGA ESCI", 4, 110, 8, Px(400).mx(W - 8));
}

// «La mascotte» (gioco, 0.10.0): petScreen() del simulatore. A sinistra la mascotte predefinita con l'accessorio
// (stanca se il gioco lo dice, altrimenti contenta); a destra livello, barra degli XP del livello, XP ed energia,
// ultimi 3 traguardi. Senza dati dal PC (pet.known falso) livello «—».
void petScreen(Surface &c, const DeviceProfile &dev, const Model &m, int f, const WallClock &wc) {
  const int t0 = viewHeader(c, dev, m, "La mascotte", wc), W = dev.W, ms = 3;
  const cal::Pet &p = m.pet;
  const bool tired = p.tired && cal::petOn(m);
  avatarS(c, m, nullptr, 2, t0 + 2, ms, tired ? cal::MascotPose::Tired : cal::MascotPose::Done, f);
  const int tx = 2 + AV_W * ms + 6, tw = W - tx - 6;
  char t[64];
  if (p.known) cal::Sb(t, sizeof t).s("LIVELLO ").i(p.level);
  else cal::scopy(t, "LIVELLO —");
  T(c, t, tx, t0 + 1, 14, Wt(700).mx(tw));
  box(c, tx, t0 + 19, tw, 7, INK, 1);
  if (p.known) {
    double fr = p.xpNext > p.xpLevelStart ? double(p.xp - p.xpLevelStart) / double(p.xpNext - p.xpLevelStart) : 0;
    fr = fr < 0 ? 0 : fr > 1 ? 1 : fr;
    c.fillRect(tx + 2, t0 + 21, R((tw - 4) * fr), 3, INK);
    T(c, cal::Sb(t, sizeof t).i(p.xp - p.xpLevelStart).s("/").i(p.xpNext - p.xpLevelStart).s(" XP").str(), tx, t0 + 29, 10,
      Wt(600).mx(R(tw / 2.0)));
    T(c, cal::Sb(t, sizeof t).s(tired ? "stanca " : "energia ").i(p.energy).s("%").str(), tx + tw, t0 + 29, 10,
      Wt(600).right().mx(R(tw / 2.0)));
  } else {
    T(c, "Dati dal PC non ancora arrivati", tx, t0 + 29, 10, Wt(600).mx(tw));
  }
  T(c, "ULTIMI TRAGUARDI", tx, t0 + 43, 8, Px(700).mx(tw));
  // Accessorio speciale da chiedere a Claude: una riga in neretto al posto del terzo traguardo.
  const bool sp = p.special && cal::petOn(m);
  const int ng = sp ? (p.nGoals < 2 ? p.nGoals : 2) : p.nGoals;
  if (!p.nGoals) T(c, "Ancora nessuno", tx, t0 + 54, 10, Wt(500).mx(tw));
  for (int k = 0; k < ng && k < cal::kPetGoals; k++) T(c, p.goals[k], tx, t0 + 54 + k * 11, 10, Wt(500).mx(tw));
  if (sp) T(c, "Speciale: chiedilo a Claude", tx, t0 + 54 + (ng > 1 ? ng : 1) * 11, 10, Wt(700).mx(tw));
  T(c, "2X ACCESSORIO   LUNGA INDIETRO", 4, 110, 8, Px(400).mx(W - 8));
}

// drawHold(): indicatore del tenuto 3 s sopra la schermata (classe S). Barra = tempo tenuto / 3 s
// all'istante del disegno; dopo lo scatto «FATTO · RILASCIA» fino al rilascio.
void drawHold(Surface &c, const DeviceProfile &dev, const Model &m, Ms now) {
  const cal::UiState &h = m.ui;
  if (!h.holdOn) return;
  const Ms el = now - h.holdSince;
  const char *label = h.holdDone ? nullptr : cal::holdLabel(m, now);
  if (h.holdDone ? !h.holdOk : (el < cal::kHoldShowMs || !label)) return;
  const int W = dev.W, H = dev.H;
  const int bw = W - 40 < 210 ? W - 40 : 210, bh = 46, bx = R((W - bw) / 2.0), by = R((H - bh) / 2.0) - 4;
  c.fillRect(bx - 2, by - 2, bw + 4, bh + 4, PAPER);
  c.fillRect(bx, by, bw, bh, INK);
  if (h.holdDone) {
    T(c, "FATTO · RILASCIA", W / 2.0, by + R((bh - 14) / 2.0), 14, Wt(700).center().col(PAPER));
    return;
  }
  T(c, "TIENI PREMUTO", W / 2.0, by + 5, 8, Px(700).center().col(PAPER));
  T(c, label, W / 2.0, by + 15, 12, Wt(700).center().col(PAPER).mx(bw - 16));
  box(c, bx + 8, by + 32, bw - 16, 9, PAPER, 1);
  double f = double(el) / double(cal::kHoldMs);
  f = f < 0 ? 0 : f > 1 ? 1 : f;
  c.fillRect(bx + 10, by + 34, R((bw - 20) * f), 5, PAPER);
}

}  // namespace

void setTextHook(TextHook fn, void *ctx) {
  gHook = fn;
  gHookCtx = ctx;
}

static void drawScreen(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc);

void draw(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  memset(&lastThemeDrawMut(), 0, sizeof(ThemeDrawInfo));
  drawScreen(c, dev, m, f, now, wc);
  drawHold(c, dev, m, now);
}

// Schermata personalizzata dal tema attivo (docs/TEMI.md §1.1). false = di serie.
// Regola §7: Permessi, Abbinamento, Conferma e schermate di sistema non passano mai di qui.
static bool themed(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc,
                   cal::theme::Screen s) {
  cal::theme::Runtime *rt = themeRuntime();
  const uint8_t *b;
  uint16_t n;
  if (!rt || !rt->block(s, b, n)) return false;
  return drawThemeBlock(c, dev, m, f, now, wc, s, b, n, &lastThemeDrawMut(), rt->drawScratch());
}

static void drawScreen(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc) {
  using TS = cal::theme::Screen;
  // In M2 c'è solo la classe S (Heltec 250×122): le altre classi arrivano in M7.
  const ScreenEntry &tp = cal::top(m);
  if (tp.id == ScreenId::Boot) { boot(c, dev, m, f, now); return; }
  if (tp.id == ScreenId::Sleep) { if (!themed(c, dev, m, f, now, wc, TS::Sleep)) sleepScreen(c, dev, m, f, wc); return; }
  if (tp.id == ScreenId::BattLow) { battLowScreen(c, dev, m); return; }
  if (tp.id == ScreenId::Calib) { calibScreen(c, dev, m, f); drawToast(c, dev, m, now); return; }
  if (tp.id == ScreenId::Ota && m.ota.phase != cal::OtaPhase::None) { otaScreen(c, dev, m); drawToast(c, dev, m, now); return; }
  if (tp.id == ScreenId::GbLoad) { gbLoadScreen(c, dev, m); return; }
  if (tp.id == ScreenId::GbPlay) { gbBands(c, dev, m); return; }  // niente toast: coprirebbe il gioco
  if (!m.sys.pc && (tp.id == ScreenId::Home || tp.id == ScreenId::Detail)) {
    if (m.sys.pcOff) { if (!themed(c, dev, m, f, now, wc, TS::Riepilogo)) riepilogo(c, dev, m, now, wc); }
    else if (!themed(c, dev, m, f, now, wc, TS::Offline)) offline(c, dev, m, f, now, wc);
    drawToast(c, dev, m, now);
    return;
  }
  cal::MenuList &list = cal::sharedMenuList();  // 0.14.1: la stessa di cal::Ui, riempita qui
  switch (tp.id) {
    case ScreenId::Home: if (!themed(c, dev, m, f, now, wc, TS::Home)) homeS(c, dev, m, f, now, wc); break;
    case ScreenId::Detail: if (!themed(c, dev, m, f, now, wc, TS::Detail)) detailS(c, dev, m, f, now); break;
    case ScreenId::Perm: permS(c, dev, m, now); break;
    case ScreenId::List: {
      cal::menuList(tp.menu, dev, m, now, wc, list, tp.ssid);
      listScreen(c, dev, m, list.title, list, clampi(tp.sel, 0, list.n - 1), wc);
      break;
    }
    case ScreenId::WifiScan:
      if (tp.scanUntil > now) scanning(c, dev, m, f, wc);
      else {
        cal::netItems(m, list);
        listScreen(c, dev, m, "Reti vicine", list, clampi(tp.sel, 0, list.n - 1), wc);
      }
      break;
    case ScreenId::Qr: qrScreen(c, dev, m, wc); break;
    case ScreenId::Connecting: connecting(c, dev, m, f); break;
    case ScreenId::Oggi: if (!themed(c, dev, m, f, now, wc, TS::Oggi)) oggi(c, dev, m, wc); break;
    case ScreenId::Storico: storico(c, dev, m, now, wc); break;
    case ScreenId::Batteria: if (!themed(c, dev, m, f, now, wc, TS::Batteria)) batteria(c, dev, m, wc); break;
    case ScreenId::Stato: if (!themed(c, dev, m, f, now, wc, TS::Stato)) stato(c, dev, m, wc); break;
    case ScreenId::Limiti: if (!themed(c, dev, m, f, now, wc, TS::Limiti)) limiti(c, dev, m, now, wc); break;
    case ScreenId::Focus: if (!themed(c, dev, m, f, now, wc, TS::Focus)) focusScreen(c, dev, m, f, now, wc); break;
    case ScreenId::Game: gameScreen(c, dev, m, f, wc); break;
    case ScreenId::Pet: if (!themed(c, dev, m, f, now, wc, TS::Pet)) petScreen(c, dev, m, f, wc); break;
    case ScreenId::Guest: guest(c, dev, m, wc); break;
    case ScreenId::Servizi: servizi(c, dev, m, wc); break;
    case ScreenId::Agenda: agenda(c, dev, m, now, wc); break;
    case ScreenId::Ambiente: if (!themed(c, dev, m, f, now, wc, TS::Ambiente)) ambiente(c, dev, m, f, wc); break;
    case ScreenId::Pair: pairS(c, dev, m); break;
    case ScreenId::PadCode: padCodeS(c, dev, m); break;
    case ScreenId::Confirm: confirmS(c, dev, m, now, wc); break;
    default: break;
  }
  drawToast(c, dev, m, now);
}

// Funzioni per l'interprete dei temi (ui/kit.h): fuori dal namespace anonimo, vedono T, O, P e accOverlay.
namespace kit {

void text(Surface &c, const char *s, double x, double y, int size, int weight, bool pix, Align a, bool ink, int max) {
  O o = {weight, pix, a, ink, max};
  T(c, s, x, y, size, o);
}

void mascot(Surface &c, const Model &m, const Session *s, int ox, int oy, int sc, cal::MascotPose st, int f, bool accessory,
            bool ink) {
  const Avatar &a = s ? cal::sessionAvatar(m, *s) : cal::defaultAvatar(m);
  drawAvatar(c, a, ox, oy, sc, P(st), f, ink);
  if (accessory) accOverlay(c, m, a, ox, oy, sc, P(st), f, ink);  // stessi buffer delle schermate di serie
}

}  // namespace kit

}  // namespace ui
