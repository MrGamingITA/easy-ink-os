// Giochi Game Boy: dal fotogramma 160×144 a 4 tonalità alla finestra 160×122 in bianco e nero, centrata nello
// schermo 250×122 (bande di 45 px ai lati). Larghezza 1:1; in altezza la riga 7 di ogni tessera da 8 righe si fonde
// con la riga 0 della tessera dopo (vince la più scura): 126 righe, poi 2 tagliate in alto e 2 in basso (la 143 si
// scarta). Le tonalità diventano un disegno fisso sulle coordinate dello schermo: la stessa tonalità dà sempre lo
// stesso disegno, così fra un fotogramma e l'altro cambiano solo i pixel che cambiano davvero (meno scie sul
// pannello veloce). Scelta V4 dell'utente (2026-10-15, leggibilità delle scritte). Logica pura, provata in native
// (test_gb_frame).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace gb {

const int kScreenW = 250, kScreenH = 122;
const int kScreenStride = 32;                                   // byte per riga, MSB = x più a sinistra, 1 = nero
const size_t kScreenBytes = size_t(kScreenStride) * kScreenH;   // 3904, come Canvas<250, 122>
const int kWinW = 160, kWinH = 122;                             // 160×144 → 160×122
const int kWinX = (kScreenW - kWinW) / 2;                       // 45: bande x 0–44 e 205–249
const int kRowGroups = 126;                                     // righe dopo la fusione, prima del taglio

// Righe del Game Boy che formano una riga della finestra (b == a: riga singola).
struct RowPair {
  uint8_t a, b;
};
// Tutti i gruppi dopo la fusione (out ha posto per 144): ne restituisce il numero (126).
int rowGroups(RowPair *out);
// Gruppo della riga oy (0–121) della finestra: rowGroups[oy + 2].
RowPair rowMap(int oy);
// Nero o bianco della tonalità v (0–3) nel pixel di schermo (x, y): 3 pieno, 2 a scacchi, 1 un pixel su quattro.
inline bool inkFor(uint8_t v, int x, int y) {
  return v == 3 || (v == 2 && ((x + y) & 1)) || (v == 1 && !(x & 1) && !(y & 1));
}
// Scrive solo la finestra (x 45–204): le bande restano come sono.
void scaleDither(const uint8_t *gbFrame, uint8_t *screen);
// Ruota 180° (Display › Ruota): il pixel (x, y) va in (249 − x, 121 − y).
void rotate180(uint8_t *screen);
// Fotogramma completo: bande (già disegnate, kScreenBytes) + finestra, poi rotazione se serve.
void composeFrame(const uint8_t *bands, const uint8_t *gbFrame, bool rot, uint8_t *out);

inline bool screenBit(const uint8_t *s, int x, int y) { return s[y * kScreenStride + (x >> 3)] & (0x80 >> (x & 7)); }
inline void screenSet(uint8_t *s, int x, int y, bool ink) {
  uint8_t &b = s[y * kScreenStride + (x >> 3)];
  const uint8_t m = uint8_t(0x80 >> (x & 7));
  b = ink ? uint8_t(b | m) : uint8_t(b & ~m);
}

}  // namespace gb
}  // namespace cal
