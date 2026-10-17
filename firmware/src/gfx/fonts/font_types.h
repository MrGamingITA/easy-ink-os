// Formato dei font bitmap generati da tools/fonts/gen.py (vedi tools/fonts/README.md).
// Avanzamenti e kerning in millesimi di em (UPM 1000), scalati in 16.16 come HarfBuzz in Chrome;
// poi si sceglie la fase a 1/4 px come il subpixel di Skia (vedi text.cpp).
#pragma once
#include <stdint.h>

#if defined(ARDUINO) && !defined(ESP32)
#include <avr/pgmspace.h>
#define FONT_DATA PROGMEM
#else
#define FONT_DATA  // su ESP32 i const finiscono già in flash (rodata mappata)
#endif

namespace fonts {

constexpr int kPhases = 4;  // fasi orizzontali: 0, 1/4, 1/2, 3/4 di pixel

// Una bitmap: w×h bit, riga per riga, MSB first, senza padding tra le righe.
struct Glyph {
  uint16_t off;  // offset in byte nel bitmap del font
  uint8_t w, h;
  int8_t x;      // bordo sinistro rispetto alla x (intera) dell'origine del glifo
  int8_t y;      // bordo alto rispetto alla y 'top' del testo
};

// Un codepoint. Le sue bitmap sono glyphs[first .. first+3] (fasi), poi, se il font ha le
// varianti chiaro su scuro, glyphs[first+4 .. first+7].
struct GlyphEntry {
  uint16_t cp;   // U+E000 = legatura «fi», U+E001 = «fl» (attive di default in Chrome)
  uint16_t adv;  // avanzamento in millesimi di em
  uint16_t first;
};

struct KernTable {
  const uint32_t *keys;  // (a << 16) | b, ordinate
  const int16_t *vals;   // millesimi di em
  uint16_t n;
};

struct Font {
  uint8_t size;
  uint16_t weight;
  bool pix;  // Silkscreen
  bool inv;  // ha le varianti chiaro su scuro
  uint16_t n;
  const GlyphEntry *entries;  // ordinate per cp (ricerca binaria)
  const Glyph *glyphs;
  const uint8_t *bitmap;
  const KernTable *kern;  // nullptr per Silkscreen (niente GPOS kern)
};

}  // namespace fonts
