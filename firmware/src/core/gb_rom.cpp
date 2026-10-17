#include "gb_rom.h"

#include <string.h>

namespace cal {
namespace gb {

bool parseHeader(const uint8_t *r, size_t n, RomInfo &out) {
  memset(&out, 0, sizeof out);
  if (!r || n < kHeaderLen) return false;
  uint8_t x = 0;
  for (int i = 0x134; i <= 0x14C; i++) x = uint8_t(x - r[i] - 1);
  if (x != r[0x14D] || r[0x148] > 8) return false;  // flash cancellata (0xFF) non passa: 0x148 = 0xFF
  int k = 0;
  for (int i = 0x134; i < 0x144 && k < int(kTitleLen) - 1; i++) {
    const uint8_t c = r[i];
    if (c == 0 || c >= 0x80) break;  // 0x143 può essere il flag CGB (0x80/0xC0)
    out.title[k++] = (c >= 0x20 && c < 0x7F) ? char(c) : '?';
  }
  while (k > 0 && out.title[k - 1] == ' ') k--;
  out.title[k] = 0;
  out.size = uint32_t(0x8000) << r[0x148];
  out.cartType = r[0x147];
  static const uint32_t kRam[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
  out.ramBytes = r[0x149] < 6 ? kRam[r[0x149]] : 0;
  if (out.cartType == 0x05 || out.cartType == 0x06) out.ramBytes = 0x200;  // MBC2: 512 mezzi byte
  out.global = uint16_t(r[0x14E] << 8 | r[0x14F]);
  return true;
}

bool fitsPartition(const RomInfo &r, uint32_t capacity) { return r.size && r.size <= capacity; }

uint16_t globalChecksum(const uint8_t *rom, uint32_t n) {
  uint32_t sum = 0;
  for (uint32_t i = 0; i < n; i++)
    if (i != 0x14E && i != 0x14F) sum += rom[i];
  return uint16_t(sum);
}

bool globalOk(const uint8_t *rom, const RomInfo &info) { return globalChecksum(rom, info.size) == info.global; }

void gameId(const uint8_t sha[32], char out[kIdLen]) {
  static const char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++) {
    out[2 * i] = kHex[sha[i] >> 4];
    out[2 * i + 1] = kHex[sha[i] & 15];
  }
  out[16] = 0;
}

}  // namespace gb
}  // namespace cal
