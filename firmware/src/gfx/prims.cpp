#include "prims.h"
#include <math.h>
#include <string.h>

int jsRound(double v) { return (int)floor(v + 0.5); }

namespace {
double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }

const char *const ICONS[][7] = {
  {".......", "......#", ".....##", "#...##.", "##.##..", ".###...", "..#...."},  // done
  {"#.....#", "##...##", ".##.##.", "..###..", ".##.##.", "##...##", "#.....#"},  // error
  {".#####.", "##...##", ".....##", "...###.", "..##...", ".......", "..##..."},  // input
  {"..##...", "..##...", "..##...", "..##...", "..##...", ".......", "..##..."},  // permission
  {"#######", "....##.", "...##..", "..##...", ".##....", "#######", "......."},  // idle
  {"#######", ".#####.", ".......", "..###..", ".......", "...#...", "...#..."},  // compact
  {"#######", ".#####.", "..###..", "...#...", "..#.#..", ".#...#.", "#######"},  // working0
  {"#######", ".#...#.", "..#.#..", "...#...", "..###..", ".#####.", "#######"},  // working1
};

const char *const SYMS[][7] = {
  {".#...#.", ".#...#.", "#######", "#######", ".#####.", "..###..", "...#..."},  // plug
  {"...#...", "..###..", ".#####.", ".#####.", ".#####.", "#######", "...#..."},  // bell
  {"..###..", ".##....", "##.....", "##.....", "##.....", ".##....", "..###.."},  // moon
  {"..###..", ".#...#.", ".#...#.", "#######", "###.###", "###.###", "#######"},  // lock
  {"...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..."},                 // bolt
  {"..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###.."},  // dot
  {"#######", ".#...#.", "..#.#..", "...#...", "..###..", ".#####.", "#######"},  // glass
  {".#####.", "#.....#", "#.#...#", "#..#..#", "#...#.#", "#.....#", ".#####."},  // gauge
};
}  // namespace

void box(Surface &c, int x, int y, int w, int h, bool col, int t) {
  if (t < 1) t = 1;
  c.fillRect(x, y, w, t, col);
  c.fillRect(x, y + h - t, w, t, col);
  c.fillRect(x, y, t, h, col);
  c.fillRect(x + w - t, y, t, h, col);
}

void dotsH(Surface &c, int x0, int x1, int y) {
  for (int x = x0; x < x1; x += 3) c.fillRect(x, y, 1, 1, true);
}

void dotsV(Surface &c, int x, int y0, int y1) {
  for (int y = y0; y < y1; y += 2) c.fillRect(x, y, 1, 1, true);
}

void glyph(Surface &c, const char *const *rows, int nrows, int x, int y, int s, bool col) {
  for (int r = 0; r < nrows; r++)
    for (int q = 0; rows[r][q]; q++)
      if (rows[r][q] == '#') c.fillRect(x + q * s, y + r * s, s, s, col);
}

void strike(Surface &c, int x, int y, int n, int s, bool col, bool bg) {
  for (int k = 0; k < n; k++) {
    c.fillRect(x + (k + 1) * s, y + k * s, s, s, bg);
    c.fillRect(x + k * s, y + k * s, s, s, col);
  }
}

void tri(Surface &c, int x, int y, int s, Dir dir) {
  for (int k = 0; k < 4; k++) {
    if (dir == Dir::Up) c.fillRect(x + (3 - k) * s, y + k * s, (1 + 2 * k) * s, s, true);
    else if (dir == Dir::Down) c.fillRect(x + k * s, y + k * s, (7 - 2 * k) * s, s, true);
    else if (dir == Dir::Left) c.fillRect(x + k * s, y + (3 - k) * s, s, (1 + 2 * k) * s, true);
    else c.fillRect(x + k * s, y + k * s, s, (7 - 2 * k) * s, true);
  }
}

void chevL(Surface &c, int x, int y, bool col, int s) {
  for (int r = 0; r < 5; r++) c.fillRect(x + (4 - r) * s, y + r * s, 2 * s, s, col);
  for (int r = 0; r < 4; r++) c.fillRect(x + (1 + r) * s, y + (5 + r) * s, 2 * s, s, col);
}

void icon(Surface &c, Icon st, int x, int y, int s, bool col) { glyph(c, ICONS[(int)st], 7, x, y, s, col); }

void iconForState(Surface &c, const char *state, int x, int y, int s, int f, bool col) {
  Icon i = Icon::Idle;
  if (!strcmp(state, "working")) i = (((f % 2) + 2) % 2) ? Icon::Working1 : Icon::Working0;
  else if (!strcmp(state, "done")) i = Icon::Done;
  else if (!strcmp(state, "error")) i = Icon::Error;
  else if (!strcmp(state, "input")) i = Icon::Input;
  else if (!strcmp(state, "permission")) i = Icon::Permission;
  else if (!strcmp(state, "compact")) i = Icon::Compact;
  icon(c, i, x, y, s, col);
}

void sym(Surface &c, Sym g, int x, int y, int s, bool col) { glyph(c, SYMS[(int)g], 7, x, y, s, col); }

void bar(Surface &c, int x, int y, int w, int h, const BarState &s, int f) {
  box(c, x, y, w, h, true, 1);
  const int ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 4;
  if (ih <= 0 || iw <= 0) return;
  if (s.total > 0) {
    const double per = (double)iw / s.total;
    const int fw = jsRound(per * s.done);
    c.fillRect(ix, iy, fw, ih, true);
    if (s.working && s.done < s.total) {
      const int sx = ix + fw, sw = jsRound(per) > 2 ? jsRound(per) : 2;
      for (int py = 0; py < ih; py++)
        for (int px = 0; px < sw; px++)
          if (((px + py + f) % 4) == 0) c.fillRect(sx + px, iy + py, 1, 1, true);
    }
    if (s.total <= 16) {
      for (int k = 1; k < s.total; k++) {
        const int tx = ix + jsRound(per * k);
        if (tx < ix + fw) c.fillRect(tx, iy, 1, ih, false);
        else c.fillRect(tx, iy + ih - 2, 1, 2, true);
      }
    }
  } else {
    const int mv = s.working ? f * 2 : 0;
    for (int px = 0; px < iw; px++)
      if (((px + mv) % 8) < 3) c.fillRect(ix + px, iy, 1, ih, true);
  }
}

void checkbox(Surface &c, int x, int y, int st, int f, int sz) {
  if (st == 2) {
    c.fillRect(x, y, sz, sz, true);
    const double k = sz / 10.0;
    const int P[7][2] = {{2, 5}, {3, 6}, {4, 7}, {5, 6}, {6, 5}, {7, 4}, {8, 3}};
    for (auto &p : P) {
      int w = jsRound(k) > 1 ? jsRound(k) : 1, h = jsRound(2 * k) > 2 ? jsRound(2 * k) : 2;
      c.fillRect(jsRound(x + p[0] * k), jsRound(y + (p[1] - 1) * k), w, h, false);
    }
  } else {
    box(c, x, y, sz, sz, true, 1);
    if (st == 1)
      for (int yy = 2; yy < sz - 2; yy++)
        for (int xx = 2; xx < sz - 2; xx++)
          if ((xx + yy + f) % 2 == 0) c.fillRect(x + xx, y + yy, 1, 1, true);
  }
}

void wifiIcon(Surface &c, int x, int y, int s, int level, bool col) {
  for (int i = 0; i < 4; i++) {
    const int h = (3 + 2 * i) * s, bx = x + i * 3 * s;
    if (level > i) c.fillRect(bx, y + 9 * s - h, 2 * s, h, col);
    else c.fillRect(bx, y + 8 * s, 2 * s, s, col);
  }
  if (level < 0)
    for (int k = 0; k < 5; k++) {
      c.fillRect(x + k * s, y + k * s, s, s, col);
      c.fillRect(x + (4 - k) * s, y + k * s, s, s, col);
    }
}

void battIcon(Surface &c, int x, int y, int s, double pct, bool col) {
  box(c, x, y, 14 * s, 8 * s, col, s);
  c.fillRect(x + 14 * s, y + 2 * s, 2 * s, 4 * s, col);
  c.fillRect(x + 2 * s, y + 2 * s, jsRound(10 * s * clampd(pct, 0, 100) / 100), 4 * s, col);
}

void scrollbar(Surface &c, int x, int y, int h, int off, int vis, int total) {
  if (total <= vis) return;
  dotsV(c, x + 1, y, y + h);
  int th = jsRound((double)h * vis / total);
  if (th < 8) th = 8;
  const int den = total - vis > 1 ? total - vis : 1;
  const int ty = y + jsRound((h - th) * ((double)off / den));
  c.fillRect(x, ty, 3, th, true);
}

void bigBar(Surface &c, int x, int y, int w, int h, double pct) {
  box(c, x, y, w, h, true, 1);
  c.fillRect(x + 2, y + 2, jsRound((w - 4) * clampd(pct, 0, 100) / 100), h - 4 > 1 ? h - 4 : 1, true);
  for (int k = 1; k < 4; k++) c.fillRect(x + jsRound((double)w * k / 4), y + h, 1, 3, true);
}
