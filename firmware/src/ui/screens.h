// Schermate della classe S (250×122): porting di draw() del simulatore e delle funzioni che smista
// (homeS, detailS, permS, listScreen, stato, sleepScreen, boot, offline, riepilogo, limiti, oggi,
// storico, batteria, scanning, qrScreen, connecting, focusScreen, guest, servizi, agenda, ambiente,
// drawToast). Fonte: reference/calamaio-os-browser/assets/calamaio-test.js (ramo S).
#pragma once
#include <stdint.h>

#include "core/clock.h"
#include "core/model.h"
#include "gfx/canvas.h"
#include "gfx/text.h"

namespace ui {

// draw(c, dev, m, f) del simulatore: disegna su un canvas già pulito (carta). Il modello non viene
// toccato: i limiti che il simulatore applica a tp.sel/tp.off durante il disegno li applica già
// cal::Ui (normalize), qui si usano copie locali.
void draw(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m, int f, cal::Ms now, const cal::WallClock &wc);

// Screen.compose(): carta, draw(), poi Inverti colori e Ruota 180° (set.invert, set.rot).
template <int W, int H>
void compose(Canvas<W, H> &cv, const cal::DeviceProfile &dev, const cal::Model &m, int f, cal::Ms now,
             const cal::WallClock &wc) {
  cv.clear(false);
  CanvasSurface<W, H> s(cv);
  draw(s, dev, m, f, now, wc);
  if (m.set.invert)
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) cv.set(x, y, !cv.get(x, y));
  if (m.set.rot) {  // t.reverse(): il pixel i va in n-1-i, cioè (W-1-x, H-1-y)
    const int n = W * H;
    for (int i = 0; i < n / 2; i++) {
      const int j = n - 1 - i;
      const bool a = cv.get(i % W, i / W), b = cv.get(j % W, j / W);
      cv.set(i % W, i / W, b);
      cv.set(j % W, j / W, a);
    }
  }
}

// 0.10.1: riquadro della mascotte nel Riposo, in coordinate logiche (prima di Inverti e Ruota): la griglia 28×25 a
// scala 2; accessori e coriandoli non ne escono (gfx/accessory, partyFx). Il risveglio dell'ora in sonno profondo
// lo riprende tale e quale dall'immagine salvata prima di dormire: niente avatar né accessori da caricare.
struct SleepRect {
  int x, y, w, h;
  bool operator==(const SleepRect &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};
// Con un tema attivo vale per l'ultimo ui::draw (o compose) dello stesso modello: chiamarla subito dopo il disegno.
SleepRect sleepMascotRect(const cal::DeviceProfile &dev, const cal::Model &m, const cal::WallClock &wc);

// Copia in cv il riquadro r (coordinate logiche) dall'immagine old, nello stesso formato di cv (già invertita e
// ruotata da compose): con Ruota 180° il riquadro sul canvas è (W-x-w, H-y-h).
template <int W, int H>
void keepRect(Canvas<W, H> &cv, const uint8_t *old, SleepRect r, bool rot) {
  const int x0 = rot ? W - r.x - r.w : r.x, y0 = rot ? H - r.y - r.h : r.y;
  for (int y = y0; y < y0 + r.h; y++)
    for (int x = x0; x < x0 + r.w; x++)
      if (x >= 0 && y >= 0 && x < W && y < H) cv.set(x, y, Canvas<W, H>::bitOf(old, x, y));
}

// Registro dei testi disegnati (solo per i test): stringa già accorciata, posizione arrotondata
// come T(), corpo, peso, famiglia e allineamento. Stesso contenuto di manifest.json › texts.
typedef void (*TextHook)(const char *s, int x, int y, int size, int weight, bool pix, Align a, void *ctx);
void setTextHook(TextHook fn, void *ctx);

}  // namespace ui
