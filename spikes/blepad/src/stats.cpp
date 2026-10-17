// SPIKE usa e getta · blepad: vedi stats.h.
#include "stats.h"

namespace pad {

void Window::add(uint32_t v) {
  if (!count || v < min) min = v;
  if (!count || v > max) max = v;
  sum += v;
  count++;
  if (v > threshold) over++;
}

void Window::clear() {
  count = min = max = over = 0;
  sum = 0;
}

uint32_t PingTracker::send(uint32_t now) {
  Slot &s = slots_[next_ % kSlots];
  if (s.busy) lost++;  // slot ancora occupato: quel ping è perso (più di 64 in volo)
  s = {next_, now, true};
  sent++;
  return next_++;
}

bool PingTracker::reply(uint32_t seq, uint32_t now, uint32_t *rtt) {
  Slot &s = slots_[seq % kSlots];
  if (!s.busy || s.seq != seq) return false;
  s.busy = false;
  received++;
  if (rtt) *rtt = now - s.at;
  return true;
}

int PingTracker::expire(uint32_t now) {
  int n = 0;
  for (Slot &s : slots_)
    if (s.busy && now - s.at > timeout_) {
      s.busy = false;
      lost++;
      n++;
    }
  return n;
}

void PingTracker::dropAll() {
  for (Slot &s : slots_)
    if (s.busy) {
      s.busy = false;
      lost++;
    }
}

}  // namespace pad
