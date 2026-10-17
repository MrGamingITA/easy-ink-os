#include "core/close_storm.h"

namespace cal {

void CloseStorm::opened(uint32_t now) {
  openedAt_ = now;
  open_ = true;
}

uint32_t CloseStorm::closed(uint32_t now, bool allocFailed) {
  if (!open_) return 0;  // nessuna connessione aperta (tentativo fallito, chiusura doppia): niente da contare
  const uint32_t lived = now - openedAt_;
  open_ = false;
  if (!allocFailed && lived >= kStableMs) {  // connessione stabile: raffica finita
    n_ = 0;
    hold_ = 0;
    return 0;
  }
  if (!allocFailed && lived >= kShortMs) return 0;  // né breve né stabile: niente di nuovo
  if (n_ && now - last_ > kWindowMs + hold_) {  // la breve di prima è lontana: si ricomincia
    n_ = 0;
    hold_ = 0;
  }
  last_ = now;
  if (n_ < 255) n_++;
  if (!allocFailed && n_ <= kFreeShort) return 0;
  hold_ = hold_ ? (hold_ * 2 > kMaxMs ? kMaxMs : hold_ * 2) : kFirstMs;
  return hold_;
}

}  // namespace cal
