// SPIKE usa e getta · fastink: motore a budget per pixel (stile Paperboy), parte pura.
//
// Ogni pixel ha un livello stimato 0..N (0 = bianco pieno, N = nero pieno, N = budget).
// A ogni tick un pixel lontano dal bersaglio riceve UN impulso verso il bersaglio e il livello
// si sposta di 1; un pixel già arrivato non riceve nulla (VSS). Quindi:
//  - una transizione completa costa esattamente N impulsi;
//  - la carica netta di un pixel (impulsi neri − impulsi bianchi) = livello − livello iniziale,
//    sempre fra −N e +N: niente deriva in continua senza limite (bilanciamento DC).
#pragma once
#include "geom.h"

namespace fi {

enum Act : uint8_t { kHold = 0, kToBlack = 1, kToWhite = 2 };

// Codifica di un pixel nella coppia di RAM. Indice di LUT = (bit RED 0x26 << 1) | bit BW 0x24
// (SSD1680 Rev 0.14, tabella 6-5). LUT0 = nero, LUT1 = bianco, LUT2/LUT3 = fermo (VSS).
inline void encodePixel(Act a, bool targetWhite, bool &bw, bool &red) {
  switch (a) {
    case kToBlack: bw = false; red = false; break;   // LUT0
    case kToWhite: bw = true;  red = false; break;   // LUT1
    default:       bw = targetWhite; red = true;     // LUT2/LUT3
  }
}

struct TickStats {
  uint32_t toBlack = 0;
  uint32_t toWhite = 0;
  uint32_t driven() const { return toBlack + toWhite; }
};

class Engine {
 public:
  static constexpr uint8_t kMaxBudget = 15;

  explicit Engine(uint8_t budget = 6);

  uint8_t budget() const { return budget_; }
  // Cambia N riscalando i livelli (arrotondati): la stima resta coerente.
  void setBudget(uint8_t n);

  // Dopo un refresh completo OTP: pannello uguale a img, livelli saturi.
  void syncSaturated(const uint8_t *img);
  // Nuovo bersaglio (bit 1 = bianco, formato RAM)
  void setTarget(const uint8_t *img);
  const uint8_t *target() const { return target_; }

  // Un tick: riempie i due piani (0x24 = bw, 0x26 = red) e aggiorna i livelli.
  TickStats tick(uint8_t *bw, uint8_t *red);
  // Piani "tutto fermo" (nessun pixel pilotato): per misurare i tempi senza toccare il pannello.
  static void holdPlanes(const uint8_t *img, uint8_t *bw, uint8_t *red);

  // Pixel non ancora al bersaglio
  uint32_t pending() const;
  uint8_t level(int idx) const { return level_[idx]; }

 private:
  uint8_t budget_;
  uint8_t target_[kRamBytes];
  uint8_t level_[kPixels];
};

}  // namespace fi
