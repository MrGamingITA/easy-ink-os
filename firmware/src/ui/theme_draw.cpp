// Interprete dei temi: regole in docs/TEMI.md §6 (le stesse del renderer JS dell'editor, pixel per pixel).
#include "ui/theme_draw.h"

#include <string.h>

#include <new>

#include "core/clock.h"
#include "core/strbuf.h"
#include "core/theme_runtime.h"
#include "core/theme_values.h"
#include "core/workday.h"
#include "gfx/avatar.h"
#include "gfx/prims.h"
#include "gfx/qr.h"
#include "gfx/text.h"
#include "ui/kit.h"

namespace ui {

using namespace cal::theme;
using cal::Model;
using cal::Ms;
using cal::Session;

namespace {

cal::theme::Runtime *gRt = nullptr;  // registro del tema attivo (setThemeRuntime)
ThemeDrawInfo gLast;                 // ultimo ui::draw

constexpr int kW = 250, kH = 122;  // il formato v1 è solo 250×122

int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
int mini(int a, int b) { return a < b ? a : b; }

// Dentro il riquadro con gli angoli tagliati di raggio r: dx, dy = distanza dal bordo più vicino in x e in y.
bool inRound(int px, int py, int x, int y, int w, int h, int r) {
  if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h) return false;
  const int dx = mini(px - x, x + w - 1 - px), dy = mini(py - y, y + h - 1 - py);
  return !(dx < r && dy < r && dx + dy < r);
}

// Riquadro: solo i pixel del canvas (mai una chiamata fuori). Bordo = dentro e non dentro il riquadro rientrato di t.
void rect(Surface &c, int x, int y, int w, int h, int t, bool fill, int r, bool ink) {
  const int x0 = clampi(x, 0, kW), x1 = clampi(x + w, 0, kW), y0 = clampi(y, 0, kH), y1 = clampi(y + h, 0, kH);
  const int ri = r - t > 0 ? r - t : 0;
  for (int py = y0; py < y1; py++)
    for (int px = x0; px < x1; px++) {
      if (!inRound(px, py, x, y, w, h, r)) continue;
      const bool border = t > 0 && !inRound(px, py, x + t, y + t, w - 2 * t, h - 2 * t, ri);
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}

// Cerchio di diametro d con centro (cx2 / 2, cy2 / 2): (2px + 1 − cx2)² + (2py + 1 − cy2)² ≤ d².
bool inCircle(int px, int py, int cx2, int cy2, int d) {
  const int32_t dx = 2 * px + 1 - cx2, dy = 2 * py + 1 - cy2;
  return d > 0 && dx * dx + dy * dy <= int32_t(d) * d;
}

void circle(Surface &c, int x, int y, int w, int h, int t, bool fill, bool ink) {
  const int d = mini(w, h), cx2 = 2 * x + d, cy2 = 2 * y + d;
  const int x0 = clampi(x, 0, kW), x1 = clampi(x + d, 0, kW), y0 = clampi(y, 0, kH), y1 = clampi(y + d, 0, kH);
  for (int py = y0; py < y1; py++)
    for (int px = x0; px < x1; px++) {
      if (!inCircle(px, py, cx2, cy2, d)) continue;
      const bool border = t > 0 && !inCircle(px, py, cx2, cy2, d - 2 * t);
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}

double barPct(const Ctx &cx, uint8_t src, const Session *s) {
  const Model &m = *cx.m;
  switch (src) {
    case 0: return s ? s->ctx : 0;
    case 1: return s && s->total ? 100.0 * s->done / s->total : 0;
    case 2: return m.work.on ? cal::workFrac(m, cx.wc) * 100 : 0;
    case 3: return m.sys.batt;
    case 4: return m.limits.hasH5 ? m.limits.h5 : 0;
    default: return m.limits.hasD7 ? m.limits.d7 : 0;
  }
}

// Buffer del disegno, nell'arena del Runtime (Runtime::drawScratch): fuori dallo stack e prestati alla partita con
// il blocco. Si usano solo dentro drawThemeBlock, che in prestito non si chiama (block() e drawScratch() falsi).
struct Scratch {
  char t[256];      // testo espanso (≤ 255 byte, §3)
  char ls[8][96];   // righe di un testo a capo
  ExpandScratch ex;
  qr::Bits q;
};
static_assert(sizeof(Scratch) <= size_t(kDrawScratch), "buffer del disegno oltre l'arena del Runtime");
static_assert(alignof(Scratch) <= 8, "arena allineata a 8");

struct Run {
  Surface *c;
  const Ctx *cx;
  const uint8_t *b;
  uint16_t n;
  int f;
  ThemeDrawInfo *info;
  Scratch *sc;
};

void wake(ThemeDrawInfo *info, Ms t) {
  if (t > 0 && (!info->nextChange || t < info->nextChange)) info->nextChange = t;
}

void mascotBox(ThemeDrawInfo *info, int x, int y, int w, int h) {
  if (!info->hasMascot) {
    info->hasMascot = true;
    info->mx = x; info->my = y; info->mw = w; info->mh = h;
    return;
  }
  const int x1 = x + w > info->mx + info->mw ? x + w : info->mx + info->mw;
  const int y1 = y + h > info->my + info->mh ? y + h : info->my + info->mh;
  if (x < info->mx) info->mx = x;
  if (y < info->my) info->my = y;
  info->mw = x1 - info->mx;
  info->mh = y1 - info->my;
}

// Un elemento con origine (ox, oy) (la riga di una lista, altrimenti 0, 0). row: sessione della riga o nullptr.
void drawElem(Run &R, const Elem &e, int ox, int oy, const Session *row) {
  const Ctx &cx = *R.cx;
  const Model &m = *cx.m;
  const Session *subject = row ? row : (cx.ref >= 0 ? &m.sessions[cx.ref] : nullptr);
  if (!condsTrue(e.conds, e.nConds, cx, subject)) return;
  const int x = e.x + ox, y = e.y + oy;
  const uint8_t *p = e.payload;
  const bool ink = !e.invert;
  SwapSurface swap(*R.c);
  Surface &c = e.invert ? static_cast<Surface &>(swap) : *R.c;
  char *const t = R.sc->t;
  const size_t tCap = sizeof R.sc->t;
  switch (e.type) {
    case Type::Mascot: {
      const Session *s = refSession(cx, Ref(p[2]), row);
      const cal::MascotPose pose = p[1] == 0xFF ? (s ? cal::pose(m, *s) : cal::MascotPose::Idle) : cal::MascotPose(p[1]);
      kit::mascot(*R.c, m, s, x, y, p[0], pose, (p[3] & 2) ? R.f : 0, p[3] & 1, ink);
      mascotBox(R.info, x, y, kAvatarW * p[0], kAvatarH * p[0]);
      break;
    }
    case Type::Text: {
      // un valore «ora coi secondi» chiede il ridisegno al secondo dopo
      for (int i = 0; i < p[6]; i++)
        if ((p[7 + 3 * i] & 3) == 1 && (p[8 + 3 * i] | (p[9 + 3 * i] << 8)) == kValClockSec)
          wake(R.info, cx.now + (1000 - cal::floorMod(cx.wc.localMs, 1000)));
      expand(R.b, R.n, p + 7, p[6], cx, row, p[5] & 1, t, tCap, R.sc->ex);
      if (!t[0]) break;
      const int size = p[0], weight = (p[1] & 0x7F) * 100, lines = p[3], lh = p[4];
      const bool pix = p[1] & 0x80;
      const Align a = Align(p[2]);
      const double ax = a == Align::Center ? x + e.w / 2.0 : a == Align::Right ? double(x + e.w) : double(x);
      if (lines <= 1) {
        kit::text(*R.c, t, ax, y, size, weight, pix, a, ink, e.w);
        break;
      }
      char (*const ls)[96] = R.sc->ls;
      int k = wrapLines(ls, lines, t, size, e.w, weight, pix);
      if (k > lines) k = lines;
      for (int i = 0; i < k; i++) kit::text(*R.c, ls[i], ax, y + i * lh, size, weight, pix, a, ink, e.w);
      break;
    }
    case Type::Battery:
      battIcon(c, x, y, p[0], m.sys.batt, true);
      if (p[1] & 1) {
        cal::Sb(t, tCap).i(jsRound(m.sys.batt)).s("%");
        kit::text(*R.c, t, x + 16 * p[0] + 3, y + (8 * p[0] - 8) / 2, 8, 400, true, Align::Left, ink, 0);
      }
      break;
    case Type::Wifi: wifiIcon(c, x, y, p[0], m.sys.wifi ? cal::rssiBars(m.sys.rssi) : -1, true); break;
    case Type::StateIcon: {
      const Session *s = refSession(cx, Ref(p[1]), row);
      if (s) iconForState(c, cal::stateInfo(s->state).key, x, y, p[0], R.f, true);
      break;
    }
    case Type::Bar: {
      const double pct = barPct(cx, p[0], subject);
      if (!p[1]) {
        bigBar(c, x, y, e.w, e.h, pct);
        break;
      }
      BarState st;
      if (p[0] == 1 && subject) {
        st.done = int(subject->done);
        st.total = int(subject->total);
        st.working = subject->state == cal::SessionState::Working;
      } else {
        st.done = jsRound(pct);
        st.total = 100;
        st.working = false;
      }
      bar(c, x, y, e.w, e.h, st, R.f);
      break;
    }
    case Type::Qr: {
      qr::Bits &q = R.sc->q;
      expand(R.b, R.n, p + 1, p[0], cx, row, false, t, tCap, R.sc->ex);
      if (!t[0] || !qr::encode(t, q)) break;
      const int side = mini(e.w, e.h), px = side / (q.size + 2) > 1 ? side / (q.size + 2) : 1;
      c.fillRect(x, y, (q.size + 2) * px, (q.size + 2) * px, false);
      for (int r = 0; r < q.size; r++)
        for (int k = 0; k < q.size; k++)
          if (q.get(k, r)) c.fillRect(x + px + k * px, y + px + r * px, px, px, true);
      break;
    }
    case Type::Sym: sym(c, ::Sym(p[0]), x, y, p[1], true); break;
    case Type::Line:
      if (!(p[1] & 1)) c.fillRect(x, y, e.w, e.h, true);
      else if (!p[0])
        for (int r = 0; r < e.h; r++) dotsH(c, x, x + e.w, y + r);
      else
        for (int k = 0; k < e.w; k++) dotsV(c, x + k, y, y + e.h);
      break;
    case Type::Rect: rect(*R.c, x, y, e.w, e.h, p[0], p[1], p[2], ink); break;
    case Type::Circle: circle(*R.c, x, y, e.w, e.h, p[0], p[1], ink); break;
    default: break;
  }
}

// Lista: le nr righe seguono subito l'elemento lista; una riga per sessione, a pagine.
void drawList(Run &R, const Elem &e) {
  const Ctx &cx = *R.cx;
  const Model &m = *cx.m;
  if (!condsTrue(e.conds, e.nConds, cx, cx.ref >= 0 ? &m.sessions[cx.ref] : nullptr)) return;
  const uint8_t *p = e.payload;
  const int rowH = p[0], gap = p[1], nRow = p[4];
  const Ms period = Ms(p[2]) * 1000;
  int per = (e.h + gap) / (rowH + gap);
  if (per < 1) per = 1;
  const int n = m.nSessions, pages = (n + per - 1) / per;
  const int64_t slot = cal::floorDiv(cx.now, period);
  const int page = pages > 1 ? int(cal::floorMod(slot, pages)) : 0;
  if (pages > 1) wake(R.info, (slot + 1) * period);
  for (int i = 0; i < per; i++) {
    const int k = page * per + i;
    if (k >= n) break;
    const uint8_t *q = e.next;
    for (int j = 0; j < nRow; j++) {
      Elem r;
      if (!readElem(R.b, R.n, q, r)) return;
      drawElem(R, r, e.x, e.y + i * (rowH + gap), &m.sessions[k]);
      q = r.next;
    }
  }
  if ((p[3] & 1) && pages > 1) {
    SwapSurface swap(*R.c);
    Surface &c = e.invert ? static_cast<Surface &>(swap) : *R.c;
    for (int pg = 0; pg < pages; pg++) {
      const int sx = e.x + e.w - 4 * (pages - pg), sy = e.y + e.h - 3;
      if (pg == page) c.fillRect(sx, sy, 3, 3, true);
      else c.fillRect(sx + 1, sy + 1, 1, 1, true);
    }
  }
}

// L'elemento dopo una lista con le sue righe (nullptr se il blocco finisce prima: non succede dopo checkBlock).
const uint8_t *skip(const uint8_t *b, uint16_t n, const Elem &e) {
  if (e.type != Type::List) return e.next;
  const uint8_t *q = e.next;
  for (int j = 0; j < e.payload[4]; j++) {
    Elem r;
    if (!readElem(b, n, q, r)) return nullptr;
    q = r.next;
  }
  return q;
}

}  // namespace

bool drawThemeBlock(Surface &c, const cal::DeviceProfile &dev, const Model &m, int f, Ms now, const cal::WallClock &wc, Screen s,
                    const uint8_t *block, uint16_t len, ThemeDrawInfo *info, uint8_t *scratch) {
  (void)dev;
  ThemeDrawInfo local;
  if (!info) info = &local;
  memset(info, 0, sizeof *info);
  if (!scratch) return false;  // arena prestata alla partita (o nessun Runtime): di serie
  if (!block || checkBlock(block, len) != Error::Ok) return false;  // mai a metà: il disegno non si fida dei byte
  Variant v;
  const int nv = variantCount(block);
  int k = 0;
  for (; k < nv; k++)
    if (variantAt(block, len, k, v) && whenTrue(m, v.when)) break;
  if (k >= nv) return false;
  Ctx cx;
  cx.m = &m;
  cx.now = now;
  cx.wc = wc;
  cx.frame = f;
  cx.screen = s;
  cx.ref = refIndex(m, now, s, v.ref);
  Run R = {&c, &cx, block, len, f, info, new (scratch) Scratch};  // tipo banale: niente azzeramento
  const uint8_t *p = block + v.off;
  for (int i = 0; i < v.count && p; i++) {
    Elem e;
    if (!readElem(block, len, p, e)) break;
    if (e.type == Type::List) drawList(R, e);
    else drawElem(R, e, 0, 0, nullptr);
    p = skip(block, len, e);
  }
  info->drawn = true;
  info->screen = s;
  return true;
}

void setThemeRuntime(cal::theme::Runtime *rt) { gRt = rt; }
cal::theme::Runtime *themeRuntime() { return gRt; }
const ThemeDrawInfo &lastThemeDraw() { return gLast; }
ThemeDrawInfo &lastThemeDrawMut() { return gLast; }

}  // namespace ui
