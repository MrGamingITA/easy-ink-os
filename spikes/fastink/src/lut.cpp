// SPIKE usa e getta · fastink: LUT del tick (un solo gruppo).
#include "lut.h"
#include <string.h>

namespace fi {

const uint8_t kWaveshareV3Tail[6] = {0x22, 0x17, 0x41, 0x00, 0x32, 0x36};

size_t buildTickLut(const LutParams &p, uint8_t *out, bool with159Tail) {
  memset(out, 0, kLut159);
  // VSH1 = nero, VSL = bianco: come LUT1/LUT2 delle parziali GxEPD2_213_BN e Waveshare V3
  const uint8_t vsBlack = p.swap ? 0x2 : 0x1;
  const uint8_t vsWhite = p.swap ? 0x1 : 0x2;
  // gruppo 0, fase A (bit 7:6); fase B resta 00 = pausa a VSS
  out[0 * 12 + 0] = vsBlack << 6;  // LUT0: pilota verso il nero
  out[1 * 12 + 0] = vsWhite << 6;  // LUT1: pilota verso il bianco
  // LUT2, LUT3 (fermo) e LUT4 (VCOM = DCVCOM) a zero
  uint8_t *g0 = out + 60;
  uint8_t tp = p.tp < 1 ? 1 : p.tp;
  uint8_t ph = p.phases < 1 ? 1 : p.phases;
  g0[0] = tp;          // TP[0A]
  g0[1] = p.rest;      // TP[0B] (0 = saltata)
  g0[2] = 0;           // SR[0AB]
  g0[3] = 0;           // TP[0C]
  g0[4] = 0;           // TP[0D]
  g0[5] = 0;           // SR[0CD]
  g0[6] = ph - 1;      // RP[0]: 0 = una volta
  const uint8_t fr = p.fr & 0x7;
  for (int i = 0; i < 6; i++) out[144 + i] = (uint8_t)((fr << 4) | fr);
  // XON 150..152 = 0 (scansione normale dei gate)
  if (!with159Tail) return kLut153;
  memcpy(out + 153, kWaveshareV3Tail, 6);
  return kLut159;
}

}  // namespace fi
