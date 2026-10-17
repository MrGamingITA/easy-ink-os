#include "gb_handoff.h"

namespace cal {
namespace gb {

uint8_t *FrameMailbox::beginWrite() {
  mx_.lock();
  return buf_;
}

void FrameMailbox::endWrite() {
  fresh_ = true;
  published_++;
  mx_.unlock();
}

bool FrameMailbox::take(void (*fn)(const uint8_t *frame, void *ctx), void *ctx) {
  if (!fresh_) return false;  // lettura senza lucchetto: al più si guarda al giro dopo
  std::lock_guard<std::mutex> g(mx_);
  if (!fresh_) return false;
  fresh_ = false;
  fn(buf_, ctx);
  return true;
}

void FrameMailbox::reset() {
  std::lock_guard<std::mutex> g(mx_);
  fresh_ = false;
  published_ = 0;
}

int Pacer::due(int64_t nowUs) {
  const int64_t now = nowUs * 1000;
  if (now < next_) return 0;
  const int64_t late = (now - next_) / kFrameNs + 1;
  if (late > kResyncFrames) {
    dropped_ += uint32_t(late - 1);
    next_ = now;
    return 1;
  }
  return late > kMaxBurst ? kMaxBurst : int(late);
}

int64_t Pacer::waitUs(int64_t nowUs) const {
  const int64_t d = next_ - nowUs * 1000;
  return d > 0 ? (d + 999) / 1000 : 0;
}

}  // namespace gb
}  // namespace cal
