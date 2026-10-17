// SPIKE usa e getta · blepad: tasti del controller → eventi astratti dei menu di Calamaio.
// Croce su/giù = sposta, A = scegli, B = indietro; A tenuto 3 s = conferma «tieni premuto» (permesso).
// Logica pura (niente Arduino): provata in env:native.
#pragma once
#include <stdint.h>

#include "xbox_report.h"

namespace pad {

enum class MenuEvent : uint8_t { None, Up, Down, Select, Back, HoldStart, HoldProgress, HoldDone, HoldCancel };

const char *menuEventName(MenuEvent e);  // testo italiano per il log

class MenuMapper {
 public:
  static const uint32_t kHoldMs = 3000;

  // Nuovo stato dal controller. Scrive al massimo cap eventi in out; restituisce quanti.
  int update(const State &s, uint32_t nowMs, MenuEvent *out, int cap);
  // Da chiamare anche senza report nuovi (il controller manda i report solo ai cambiamenti):
  // fa scattare HoldProgress (una volta al secondo) e HoldDone a 3 s.
  int tick(uint32_t nowMs, MenuEvent *out, int cap);
  // Controller scollegato: tutto rilasciato, un'eventuale tenuta si annulla.
  int reset(MenuEvent *out, int cap);

  uint32_t holdElapsed(uint32_t nowMs) const { return holding_ ? nowMs - holdFrom_ : 0; }
  int holdSeconds() const { return holdSec_; }

 private:
  uint16_t buttons_ = 0;
  uint8_t dpad_ = 0;
  bool holding_ = false, holdDone_ = false;
  uint32_t holdFrom_ = 0;
  int holdSec_ = 0;
};

}  // namespace pad
