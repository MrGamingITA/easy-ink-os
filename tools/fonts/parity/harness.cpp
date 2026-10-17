// Banco di parità: legge casi da stdin (una riga: op size weight pix a1..a5 stringa-in-hex) e stampa i risultati
// di firmware/src/gfx/text.cpp, una riga per caso. Lo usa gen-fixture.mjs.
// W larghezza (px e millesimi) · H hasFont · F fitText (a1 max) · L wrapLines (a1 righe, a2 maxW)
// D drawText (a1 x, a2 y, a3 allineamento, a4 ink, a5 max): numero di fillRect e FNV-1a di (x, y, w, h, ink)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "gfx/text.h"
#include "gfx/fonts/text_internal.h"

struct Rec : Surface {
  uint32_t h = 2166136261u; int n = 0;
  void mix(int v) { for (int k = 0; k < 4; k++) { h ^= uint8_t(v >> (8 * k)); h *= 16777619u; } }
  void fillRect(int x, int y, int w, int hh, bool ink) override { n++; mix(x); mix(y); mix(w); mix(hh); mix(ink); }
};
static std::string unhex(const char *s) { std::string o; for (; s[0] && s[1]; s += 2) { char b[3] = {s[0], s[1], 0}; o += char(strtol(b, 0, 16)); } return o; }
static void hexout(const char *s) { for (; *s; s++) printf("%02x", uint8_t(*s)); }
int main() {
  char line[8192];
  while (fgets(line, sizeof line, stdin)) {
    char op; int size, weight, pix, a1, a2, a3, a4, a5; char hx[4096];
    if (sscanf(line, "%c %d %d %d %d %d %d %d %d %4095s", &op, &size, &weight, &pix, &a1, &a2, &a3, &a4, &a5, hx) != 10) { hx[0] = 0;
      sscanf(line, "%c %d %d %d %d %d %d %d %d", &op, &size, &weight, &pix, &a1, &a2, &a3, &a4, &a5); }
    std::string s = unhex(hx);
    if (op == 'W') printf("%d %d\n", textWidth(s.c_str(), size, weight, pix), textWidthMilli(s.c_str(), size, weight, pix));
    else if (op == 'H') printf("%d\n", hasFont(size, weight, pix) ? 1 : 0);
    else if (op == 'F') { char out[256]; fitText(out, sizeof out, s.c_str(), size, weight, pix, a1); hexout(out); printf("\n"); }
    else if (op == 'L') { char ls[8][96]; int n = wrapLines(ls, a1, s.c_str(), size, a2, weight, pix); printf("%d", n);
      for (int i = 0; i < n; i++) { printf(" "); hexout(ls[i]); printf("."); } printf("\n"); }
    else if (op == 'D') { Rec r; TextOpts o; o.weight = weight; o.pix = pix; o.align = Align(a3); o.ink = a4; o.max = a5;
      drawText(r, s.c_str(), a1, a2, size, o); printf("%d %u\n", r.n, r.h); }
  }
}
