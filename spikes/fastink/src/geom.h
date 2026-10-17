// SPIKE usa e getta · fastink: geometria della RAM del controller (E0213A367, 128×250).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace fi {

constexpr int kRamW = 128;                        // bit per riga di RAM (16 byte)
constexpr int kRamH = 250;                        // righe (gate)
constexpr int kRowBytes = kRamW / 8;              // 16
constexpr int kRamBytes = kRowBytes * kRamH;      // 4000 per piano
constexpr int kPixels = kRamW * kRamH;            // 32000 (6 colonne invisibili)
constexpr int kVisW = 122;                        // colonne di RAM visibili

// Schermo logico orizzontale, come landscape() di heltec-eink-modules (rotazione 3)
constexpr int kScrW = 250;
constexpr int kScrH = 122;

}  // namespace fi
