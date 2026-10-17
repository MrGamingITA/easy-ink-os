// Testo come T(), fit(), wrapLines() e wrapT() del simulatore, su font bitmap rasterizzati da Chrome.
// Posizioni come Chrome: avanzamenti e kerning (millesimi di em) scalati in 16.16 come fa HarfBuzz,
// poi la fase a 1/4 di pixel sceglie quale delle 4 bitmap disegnare (subpixel di Skia).
#include "text.h"

#include <string.h>

#include "fonts/f_registry.h"
#include "fonts/text_internal.h"

namespace {

using fonts::Font;
using fonts::Glyph;
using fonts::GlyphEntry;

constexpr int kMaxCps = 256;   // codepoint per stringa; il resto si ignora
constexpr int kWrapMax = 8;    // righe al massimo per wrapText
constexpr uint16_t kLigFi = 0xE000, kLigFl = 0xE001;

// Matching CSS dei pesi: Chakra Petch ha 500/600/700, Silkscreen 400/700.
int normWeight(int w, bool pix) {
  if (pix) return w <= 500 ? 400 : 700;
  if (w <= 500) return 500;
  if (w <= 600) return 600;
  return 700;
}

const Font *findFont(int size, int weight, bool pix) {
  const int w = normWeight(weight, pix);
  const Font *best = nullptr;
  int bestScore = 1 << 30;
  for (int i = 0; i < fonts::kFontCount; i++) {
    const Font &f = fonts::kFonts[i];
    if (f.pix != pix) continue;
    if (f.size == size && f.weight == w) return &f;
    // ripiego: dimensione più vicina, a parità il peso giusto
    int d = f.size > size ? f.size - size : size - f.size;
    int score = d * 4 + (f.weight == w ? 0 : 1);
    if (score < bestScore) { bestScore = score; best = &f; }
  }
  return best;
}

const GlyphEntry *findGlyph(const Font *f, uint16_t cp) {
  int lo = 0, hi = f->n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint16_t c = f->entries[mid].cp;
    if (c == cp) return &f->entries[mid];
    if (c < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

int kernOf(const Font *f, uint16_t a, uint16_t b) {
  if (!f->kern) return 0;
  const uint32_t key = (uint32_t(a) << 16) | b;
  int lo = 0, hi = f->kern->n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t k = f->kern->keys[mid];
    if (k == key) return f->kern->vals[mid];
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}

// Spazi di \s in JavaScript (split(/\s+/) e trimEnd()).
bool isJsSpace(uint16_t c) {
  return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
         c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

// UTF-8 -> codepoint (BMP). Sequenze non valide o fuori BMP diventano U+FFFD.
int decode(const char *s, uint16_t *out, int cap) {
  int n = 0;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(s ? s : "");
  while (*p && n < cap) {
    uint32_t c = *p++;
    int extra = 0;
    if (c < 0x80) extra = 0;
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
    else { out[n++] = 0xFFFD; continue; }
    bool ok = true;
    for (int k = 0; k < extra; k++) {
      if ((*p & 0xC0) != 0x80) { ok = false; break; }
      c = (c << 6) | (*p++ & 0x3F);
    }
    out[n++] = (!ok || c > 0xFFFF) ? 0xFFFD : uint16_t(c);
  }
  return n;
}

// codepoint -> UTF-8, senza spezzare un carattere se manca spazio. Restituisce i byte scritti.
int encode(const uint16_t *cps, int n, char *out, int cap) {
  if (cap <= 0) return 0;
  int len = 0;
  for (int i = 0; i < n; i++) {
    uint16_t c = cps[i];
    char b[3];
    int k;
    if (c < 0x80) { b[0] = char(c); k = 1; }
    else if (c < 0x800) { b[0] = char(0xC0 | (c >> 6)); b[1] = char(0x80 | (c & 0x3F)); k = 2; }
    else { b[0] = char(0xE0 | (c >> 12)); b[1] = char(0x80 | ((c >> 6) & 0x3F)); b[2] = char(0x80 | (c & 0x3F)); k = 3; }
    if (len + k > cap - 1) break;
    memcpy(out + len, b, k);
    len += k;
  }
  out[len] = 0;
  return len;
}

inline int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// Avanzamento in 16.16: Chrome passa a HarfBuzz la larghezza di Skia troncata (SkiaScalarToHarfBuzzPosition).
inline int64_t advFx(uint16_t adv, int size) { return int64_t(adv) * size * 65536 / 1000; }

// Kerning GPOS scalato da HarfBuzz (em_mult): (v * mult + 32768) >> 16, mult = (scala << 16) / upem.
inline int64_t kernFx(int k, int size) {
  const int64_t mult = (int64_t(size) * 65536 * 65536) / 1000;
  return floorDiv(int64_t(k) * mult + 32768, 65536);
}

// Percorre la sequenza a + b come la impagina Chrome: legature fi/fl (solo Chakra Petch),
// kerning a coppie, spazi ASCII come U+0020 (fillText fa così). fn(entry, posizione in 16.16 px).
// Restituisce la larghezza totale in 16.16 px (measureText(s).width * 65536).
template <class Fn>
int64_t walk(const Font *f, const uint16_t *a, int na, const uint16_t *b, int nb, Fn fn) {
  const GlyphEntry *qmark = findGlyph(f, '?');
  const int size = f->size;
  int64_t u = 0;
  uint16_t prev = 0;
  bool havePrev = false;
  const int n = na + nb;
  for (int i = 0; i < n; i++) {
    uint16_t c = i < na ? a[i] : b[i - na];
    if (c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D) c = 0x20;
    if (!f->pix && c == 'f' && i + 1 < n) {
      uint16_t nx = i + 1 < na ? a[i + 1] : b[i + 1 - na];
      if (nx == 'i' || nx == 'l') { c = nx == 'i' ? kLigFi : kLigFl; i++; }
    }
    const GlyphEntry *g = findGlyph(f, c);
    if (!g) { g = qmark; c = '?'; }  // glifo assente: punto interrogativo
    if (!g) continue;
    if (havePrev) u += kernFx(kernOf(f, prev, c), size);
    fn(g, u);
    u += advFx(g->adv, size);
    prev = c;
    havePrev = true;
  }
  return u;
}

int64_t widthFx(const Font *f, const uint16_t *a, int na, const uint16_t *b = nullptr, int nb = 0) {
  return walk(f, a, na, b, nb, [](const GlyphEntry *, int64_t) {});
}

// measureText(s).width <= max, in aritmetica intera.
bool fits(const Font *f, const uint16_t *a, int na, const uint16_t *b, int nb, int max) {
  return widthFx(f, a, na, b, nb) <= int64_t(max) * 65536;
}

// fit(): se non sta, toglie caratteri finché s + ell sta, poi trimEnd() + ell.
int fitCps(const Font *f, bool pix, uint16_t *cps, int n, int cap, int max) {
  if (fits(f, cps, n, nullptr, 0, max)) return n;
  static const uint16_t kEllUi[] = {0x2026}, kEllPix[] = {'.', '.'};
  const uint16_t *ell = pix ? kEllPix : kEllUi;
  const int ne = pix ? 2 : 1;
  while (n > 1 && !fits(f, cps, n, ell, ne, max)) n--;
  while (n > 0 && isJsSpace(cps[n - 1])) n--;
  for (int k = 0; k < ne && n < cap; k++) cps[n++] = ell[k];
  return n;
}

void blit(Surface &c, const Font *f, const Glyph &g, int x0, int y0, bool ink) {
  const uint8_t *bm = f->bitmap + g.off;
  int bit = 0;
  for (int yy = 0; yy < g.h; yy++) {
    int run = -1;
    for (int xx = 0; xx < g.w; xx++, bit++) {
      bool on = bm[bit >> 3] & (0x80 >> (bit & 7));
      if (on && run < 0) run = xx;
      if (!on && run >= 0) { c.fillRect(x0 + run, y0 + yy, xx - run, 1, ink); run = -1; }
    }
    if (run >= 0) c.fillRect(x0 + run, y0 + yy, g.w - run, 1, ink);
  }
}

void drawCps(Surface &c, const Font *f, const uint16_t *cps, int n, int x, int y, Align align, bool ink) {
  const int64_t total = widthFx(f, cps, n);
  const int a = align == Align::Center ? 1 : align == Align::Right ? 2 : 0;
  // Variante chiaro su scuro se il testo è color carta e il font la possiede.
  const int variant = (!ink && f->inv) ? fonts::kPhases : 0;
  walk(f, cps, n, nullptr, 0, [&](const GlyphEntry *g, int64_t u) {
    // origine del glifo in 1/131072 px (x - a * larghezza / 2 + u), poi quarti di pixel
    // arrotondati come Skia: floor(4 * gx + 1/2)
    const int64_t p2 = int64_t(x) * 131072 - a * total + 2 * u;
    const int64_t q = floorDiv(4 * p2 + 65536, 131072);
    const int64_t ix = floorDiv(q, 4);
    int ph = int(q - ix * 4);
    const Glyph &gl = f->glyphs[g->first + variant + ph];
    blit(c, f, gl, int(ix) + gl.x, y + gl.y, ink);
  });
}

}  // namespace

bool hasFont(int size, int weight, bool pix) {
  const Font *f = findFont(size, weight, pix);
  return f && f->size == size && f->weight == normWeight(weight, pix);
}

bool hasGlyph(int size, int weight, bool pix, uint16_t cp) {
  const Font *f = findFont(size, weight, pix);
  if (!f || f->size != size || f->weight != normWeight(weight, pix)) return false;
  return findGlyph(f, cp) != nullptr;
}

int32_t textWidthMilli(const char *s, int size, int weight, bool pix) {
  const Font *f = findFont(size, weight, pix);
  if (!f) return 0;
  uint16_t cps[kMaxCps];
  int n = decode(s, cps, kMaxCps);
  return int32_t(floorDiv(widthFx(f, cps, n) * 1000 + 32768, 65536));
}

int textWidth(const char *s, int size, int weight, bool pix) {
  const Font *f = findFont(size, weight, pix);
  if (!f) return 0;
  uint16_t cps[kMaxCps];
  int n = decode(s, cps, kMaxCps);
  return int(floorDiv(widthFx(f, cps, n) + 32768, 65536));  // Math.round
}

void drawText(Surface &c, const char *s, int x, int y, int size, const TextOpts &o) {
  const Font *f = findFont(size, o.weight, o.pix);
  if (!f) return;
  uint16_t cps[kMaxCps + 2];
  int n = decode(s, cps, kMaxCps);
  if (o.max) n = fitCps(f, o.pix, cps, n, kMaxCps + 2, o.max);
  drawCps(c, f, cps, n, x, y, o.align, o.ink);
}

void fitText(char *out, int cap, const char *s, int size, int weight, bool pix, int max) {
  if (!out || cap <= 0) return;
  const Font *f = findFont(size, weight, pix);
  uint16_t cps[kMaxCps + 2];
  int n = decode(s, cps, kMaxCps);
  if (f) n = fitCps(f, pix, cps, n, kMaxCps + 2, max);
  encode(cps, n, out, cap);
}

int wrapLines(char lines[][96], int maxLines, const char *s, int size, int maxW, int weight, bool pix) {
  const Font *f = findFont(size, weight, pix);
  if (!f || maxLines <= 0) return 0;
  uint16_t src[kMaxCps], line[kMaxCps], t[kMaxCps];
  const int n = decode(s, src, kMaxCps);
  int ln = 0, count = 0;
  // split(/\s+/): token tra corse di spazi, con i token vuoti a inizio e fine se ci sono spazi lì
  int i = 0;
  while (true) {
    int j = i;
    while (j < n && !isJsSpace(src[j])) j++;
    const uint16_t *wd = src + i;
    const int nw = j - i;
    // t = line ? line + ' ' + wd : wd
    int nt = 0;
    if (ln) {
      memcpy(t, line, ln * sizeof(uint16_t));
      nt = ln;
      if (nt < kMaxCps) t[nt++] = ' ';
    }
    for (int k = 0; k < nw && nt < kMaxCps; k++) t[nt++] = wd[k];
    if (fits(f, t, nt, nullptr, 0, maxW)) {
      memcpy(line, t, nt * sizeof(uint16_t));
      ln = nt;
    } else {
      if (ln) {
        encode(line, ln, lines[count], 96);
        if (++count >= maxLines) return count;
      }
      memcpy(line, wd, nw * sizeof(uint16_t));
      ln = nw;
    }
    if (j >= n) break;
    while (j < n && isJsSpace(src[j])) j++;
    i = j;
  }
  if (ln) {
    encode(line, ln, lines[count], 96);
    count++;
  }
  return count;
}

void wrapText(Surface &c, const char *s, int x, int y, int size, int maxW, int maxLines, int lh, const TextOpts &o) {
  // Il simulatore misura con o.w || 500 e disegna con o.w: tutte le sue chiamate passano w, quindi
  // qui misura e disegno usano lo stesso o.weight.
  char lines[kWrapMax][96];
  const int n = wrapLines(lines, maxLines < kWrapMax ? maxLines : kWrapMax, s, size, maxW, o.weight, o.pix);
  TextOpts oo = o;
  oo.max = maxW;
  for (int i = 0; i < n; i++) drawText(c, lines[i], x, y + i * lh, size, oo);
}
