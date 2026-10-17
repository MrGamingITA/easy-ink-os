// SPIKE usa e getta · fastink: costruzione della LUT del comando 0x32 (formato SSD1680).
//
// Formato verificato su SSD1680 datasheet Rev 0.14 §6.6–6.7 (153 byte):
//   0..59    VS[nX-LUTm]: 5 LUT × 12 gruppi, 1 byte per gruppo, fasi A..D nei bit 7:6, 5:4, 3:2, 1:0
//            (00 VSS, 01 VSH1, 10 VSL, 11 VSH2)
//   60..143  12 gruppi × 7 byte: TP[nA] TP[nB] SR[nAB] TP[nC] TP[nD] SR[nCD] RP[n]
//            (ordine come le LUT parziali di GxEPD2_213_BN e Waveshare 2in13_V3)
//   144..149 FR[n], due gruppi per byte (tutti uguali qui: l'ordine dei nibble non conta)
//   150..152 XON
// I 6 byte 153..158 (EOPT, VGH, VSH1, VSH2, VSL, VCOM) NON fanno parte di 0x32: si mandano
// con 0x3F, 0x03, 0x04, 0x2C. La variante "159" li accoda a 0x32 (stile codice dei fornitori).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace fi {

constexpr size_t kLut153 = 153;
constexpr size_t kLut159 = 159;

struct LutParams {
  uint8_t tp = 1;        // fotogrammi per fase di spinta (TP[0A])
  uint8_t phases = 2;    // fasi di spinta per tick (RP[0] = phases − 1)
  uint8_t rest = 0;      // fotogrammi di pausa a VSS dopo la spinta (TP[0B]), 0 = nessuna
  uint8_t fr = 2;        // FR[n] 0..7 (Waveshare/GxEPD2 usano 2)
  bool swap = false;     // inverte VSH1/VSL se il pannello risulta al contrario
};

// Coda dei 6 byte di Waveshare 2in13_V3 (WF_PARTIAL_2IN13_V3[153..158]): EOPT, VGH, VSH1, VSH2, VSL, VCOM
extern const uint8_t kWaveshareV3Tail[6];

// Scrive in out (≥ 159 byte) la LUT del tick; ritorna 153 o 159.
size_t buildTickLut(const LutParams &p, uint8_t *out, bool with159Tail);

// Durata stimata di un tick in fotogrammi
inline uint32_t tickFrames(const LutParams &p) { return (uint32_t)(p.tp + p.rest) * p.phases; }

}  // namespace fi
