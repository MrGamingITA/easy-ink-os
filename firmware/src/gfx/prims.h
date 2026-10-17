// Primitive di disegno: porting di box, dotsH/V, glyph, strike, tri, chevL, icon, bar, checkbox,
// wifiIcon, battIcon, scrollbar, bigBar del simulatore. Solo bianco/nero (i grigi del T5 4,7″ in M7).
#pragma once
#include "canvas.h"

// Math.round di JavaScript: .5 arrotonda verso +infinito.
int jsRound(double v);

// Righe di '#' e '.' come le costanti G e ICONS del simulatore.
typedef const char *const Rows[7];

enum class Icon : uint8_t { Done, Error, Input, Permission, Idle, Compact, Working0, Working1 };
enum class Sym : uint8_t { Plug, Bell, Moon, Lock, Bolt, Dot, Glass, Gauge };
enum class Dir : uint8_t { Up, Down, Left, Right };

struct BarState {   // quanto serve a bar() della sessione
  int done, total;
  bool working;
};

void box(Surface &c, int x, int y, int w, int h, bool col = true, int t = 1);
void dotsH(Surface &c, int x0, int x1, int y);
void dotsV(Surface &c, int x, int y0, int y1);
void glyph(Surface &c, const char *const *rows, int nrows, int x, int y, int s, bool col);
void strike(Surface &c, int x, int y, int n, int s, bool col, bool bg);
void tri(Surface &c, int x, int y, int s, Dir dir);
void chevL(Surface &c, int x, int y, bool col, int s = 1);
void icon(Surface &c, Icon st, int x, int y, int s, bool col = true);
void iconForState(Surface &c, const char *state, int x, int y, int s, int f, bool col = true);
void sym(Surface &c, Sym g, int x, int y, int s, bool col);
void bar(Surface &c, int x, int y, int w, int h, const BarState &s, int f);
void checkbox(Surface &c, int x, int y, int st, int f, int sz = 10);
void wifiIcon(Surface &c, int x, int y, int s, int level, bool col);
void battIcon(Surface &c, int x, int y, int s, double pct, bool col);
void scrollbar(Surface &c, int x, int y, int h, int off, int vis, int total);
void bigBar(Surface &c, int x, int y, int w, int h, double pct);
