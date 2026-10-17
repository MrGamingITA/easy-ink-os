// Temi: valori, condizioni, sessione di riferimento (docs/TEMI.md §3–§4). Puro.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "model.h"
#include "theme_format.h"

namespace cal {
namespace theme {

struct Ctx {
  const Model *m;
  Ms now;
  WallClock wc;
  int frame;
  Screen screen;
  int ref;  // indice della sessione di riferimento della variante, -1 = nessuna
};

bool whenTrue(const Model &m, When w);
// Per Urgent e Selected (nel Dettaglio: la sessione aperta); -1 senza sessioni.
int refIndex(const Model &m, Ms now, Screen s, Ref r);
// Row → la riga; Variant → la riga dentro una lista, altrimenti quella della variante; Nobody → nullptr.
const Session *refSession(const Ctx &cx, Ref r, const Session *row);
int permWaiting(const Model &m);  // sessioni in Permission

void valueText(uint16_t id, const Ctx &cx, const Session *row, char *out, size_t cap);
bool condTrue(uint8_t op, uint8_t arg, const Ctx &cx, const Session *subject);
bool condsTrue(const uint8_t *conds, int n, const Ctx &cx, const Session *subject);
// Buffer di lavoro di expand: fuori dallo stack, nell'arena del Runtime (ui/theme_draw, 0.13.0).
struct ExpandScratch {
  char piece[256];
  char mapped[256];
};
// Testo finale (al massimo cap - 1 byte, troncato su un confine UTF-8).
void expand(const uint8_t *b, size_t n, const uint8_t *segs, int nSeg, const Ctx &cx, const Session *row, bool upperAll,
            char *out, size_t cap, ExpandScratch &sc);

// ASCII e À–þ come upper() di ui/screens.cpp, senza × e ÷.
void upperUtf8(char *out, size_t cap, const char *s);
void lowerUtf8(char *out, size_t cap, const char *s);

}  // namespace theme
}  // namespace cal
