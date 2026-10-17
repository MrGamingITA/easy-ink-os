#include "gfx/qr.h"

#include <string.h>

#include "gfx/qr/qrcode_ricmoo.h"

namespace qr {

bool Bits::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= size || y >= size) return false;
  const int o = y * size + x;
  return (buf[o >> 3] >> (7 - (o & 7))) & 1;
}

int byteCapacityM(int version) {
  // ISO/IEC 18004, tabella 7: modo byte, livello M
  static const int kCap[kMaxVersion] = {14, 26, 42, 62, 84};
  return version >= 1 && version <= kMaxVersion ? kCap[version - 1] : 0;
}

bool encode(const char *text, Bits &out) {
  memset(&out, 0, sizeof out);
  const size_t len = strlen(text);
  for (int v = 1; v <= kMaxVersion; v++) {
    // La libreria non controlla la capienza: si sceglie qui la versione (il modo byte è il caso peggiore).
    if (len > static_cast<size_t>(byteCapacityM(v))) continue;
    if (qrcode_getBufferSize(v) > sizeof out.buf) return false;
    QRCode q;
    if (qrcode_initText(&q, out.buf, v, ECC_MEDIUM, text) < 0) return false;
    out.size = q.size;
    out.version = v;
    return true;
  }
  return false;
}

}  // namespace qr
