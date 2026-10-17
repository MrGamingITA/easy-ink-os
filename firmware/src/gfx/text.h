// Testo: porting di T(), fit(), wrapLines() del simulatore su font bitmap generati.
// I font sono Chakra Petch (pesi 500/600/700) e Silkscreen (400/700), rasterizzati come fa il
// simulatore: glifo antialiasato e soglia di luminanza al 50%. Vedi tools/fonts/.
// Le stringhe sono UTF-8 (lettere accentate italiane e · › ″ − … ✓ ✗ ▲ ▼ ◀ ▶).
#pragma once
#include <stdint.h>
#include "canvas.h"

enum class Align : uint8_t { Left, Center, Right };

struct TextOpts {
  int weight = 600;       // 500, 600, 700 (Chakra Petch) · 400, 700 (Silkscreen)
  bool pix = false;       // true = Silkscreen (F_PIX), false = Chakra Petch (F_UI)
  Align align = Align::Left;
  bool ink = true;        // colore del testo: true = inchiostro, false = carta
  int max = 0;            // larghezza massima in px; 0 = nessun limite. Tronca con «…» (Silkscreen: «..»)
};

// Larghezza in pixel come c.measureText(s).width del simulatore (arrotondata).
int textWidth(const char *s, int size, int weight = 600, bool pix = false);

// Come T(c, s, x, y, size, o): y è il bordo superiore (textBaseline 'top').
void drawText(Surface &c, const char *s, int x, int y, int size, const TextOpts &o = TextOpts());

// Come fit(): scrive in out (capienza cap byte) s troncata per stare in max px.
void fitText(char *out, int cap, const char *s, int size, int weight, bool pix, int max);

// Come wrapLines(): spezza su spazi. Scrive fino a maxLines righe in lines[i] (capienza cap ciascuna).
// Restituisce il numero di righe prodotte (senza limite superiore oltre maxLines).
int wrapLines(char lines[][96], int maxLines, const char *s, int size, int maxW, int weight = 500, bool pix = false);

// Come wrapT(): wrapLines + drawText riga per riga con interlinea lh, ogni riga troncata a maxW.
void wrapText(Surface &c, const char *s, int x, int y, int size, int maxW, int maxLines, int lh, const TextOpts &o = TextOpts());

// Vero se esiste un font generato per questa combinazione (per i test e per gli assert).
bool hasFont(int size, int weight, bool pix);

// Vero se il font di questa combinazione ha il glifo (altrimenti esce «?»); falso se la combinazione non esiste.
bool hasGlyph(int size, int weight, bool pix, uint16_t cp);
