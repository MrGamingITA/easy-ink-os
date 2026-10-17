// Interprete dei temi (docs/TEMI.md §6): disegna il blocco di una schermata con le funzioni di oggi. Nessuna allocazione.
#pragma once
#include <stdint.h>

#include "core/model.h"
#include "core/theme_format.h"
#include "gfx/canvas.h"

namespace cal {
namespace theme {
class Runtime;
}  // namespace theme
}  // namespace cal

namespace ui {

struct ThemeDrawInfo {
  bool drawn;
  cal::Ms nextChange;  // 0 = nessun ridisegno a tempo (orologio coi secondi, pagine della lista)
  bool hasMascot;
  int mx, my, mw, mh;  // unione dei riquadri delle mascotte disegnate
  cal::theme::Screen screen;  // schermata del blocco disegnato
};

// Scambia inchiostro e carta (elementi con invert, tranne il testo e la mascotte che lo fanno da sé).
struct SwapSurface : Surface {
  explicit SwapSurface(Surface &s) : s(s) {}
  void fillRect(int x, int y, int w, int h, bool ink) override { s.fillRect(x, y, w, h, !ink); }
  Surface &s;
};
// Non disegna (per chi vuole solo ThemeDrawInfo).
struct NullSurface : Surface {
  void fillRect(int, int, int, int, bool) override {}
};

// false = blocco non valido, nessuna variante vera o scratch nullptr: si disegna la schermata di serie (e su c non è
// stato scritto niente). info può essere nullptr (allora il risultato si scarta). scratch: cal::theme::kDrawScratch
// byte allineati a 8, quelli del Runtime (Runtime::drawScratch(), nullptr in prestito alla partita).
bool drawThemeBlock(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m, int f, cal::Ms now, const cal::WallClock &wc,
                    cal::theme::Screen s, const uint8_t *block, uint16_t len, ThemeDrawInfo *info, uint8_t *scratch);

// Tema attivo per ui::draw (nullptr = sempre di serie, come nei test delle schermate).
void setThemeRuntime(cal::theme::Runtime *rt);
cal::theme::Runtime *themeRuntime();
// Ultimo disegno fatto da ui::draw: drawn falso se la schermata era di serie.
const ThemeDrawInfo &lastThemeDraw();
ThemeDrawInfo &lastThemeDrawMut();  // solo per screens.cpp

}  // namespace ui
