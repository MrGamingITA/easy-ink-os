// Giochi Game Boy: dal controller (cal::pad, piano A) ai tasti del Game Boy. Provata in native (test_gb_input).
//   A → A, B → B, Menu (≡) → Start, View (⧉) → Select, croce o levetta sinistra (oltre metà corsa) → direzioni.
//   Start+Select insieme, o il tasto Xbox, = pausa: non arrivano al gioco.
#pragma once
#include <stdint.h>

#include "pad.h"

namespace cal {
namespace gb {

const int kStickDead = 50;  // levetta: −100..100, direzione oltre ±50

struct PadKeys {
  uint8_t keys;  // gb::Key premuti
  bool pause;    // combinazione di pausa tenuta adesso
};
PadKeys mapPad(const pad::State &s);

// Pausa sul fronte: vera solo nel giro in cui la combinazione comincia (tenerla non riapre la pausa dopo Riprendi).
class PauseEdge {
 public:
  bool update(bool held) {
    const bool fire = held && !held_;
    held_ = held;
    return fire;
  }
  void arm(bool held) { held_ = held; }  // dopo Riprendi: una combinazione ancora tenuta non conta

 private:
  bool held_ = false;
};

}  // namespace gb
}  // namespace cal
