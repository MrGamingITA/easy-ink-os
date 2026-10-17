#include "gb_save.h"

namespace cal {
namespace gb {

uint32_t crc32Update(uint32_t crc, const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int k = 0; k < 8; k++) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
  }
  return crc;
}

void packTrailer(uint32_t crc, uint8_t out[kTrailerLen]) {
  for (size_t i = 0; i < kTrailerLen; i++) out[i] = uint8_t(crc >> (8 * i));
}

uint32_t unpackTrailer(const uint8_t in[kTrailerLen]) {
  return uint32_t(in[0]) | uint32_t(in[1]) << 8 | uint32_t(in[2]) << 16 | uint32_t(in[3]) << 24;
}

void AutoSave::start(uint32_t nowMs, uint32_t writes) {
  writes_ = writes;
  changeAt_ = nowMs;
  savedAt_ = retryAt_ = 0;
  saved_ = false;
}

bool AutoSave::due(uint32_t nowMs, uint32_t writes, bool dirty) {
  if (writes != writes_) {  // il gioco sta ancora scrivendo: si aspetta di nuovo la calma
    writes_ = writes;
    changeAt_ = nowMs;
  }
  if (!dirty || nowMs - changeAt_ < kAutoQuietMs) return false;
  if (retryAt_ && int32_t(nowMs - retryAt_) < 0) return false;
  return !saved_ || nowMs - savedAt_ >= kAutoEveryMs;
}

}  // namespace gb
}  // namespace cal
