// Buffer 1 bit per pixel di tutto lo schermo. true = inchiostro (nero), false = carta.
#pragma once
#include <stdint.h>
#include <string.h>

template <int W, int H>
class Canvas {
 public:
  static constexpr int kW = W, kH = H, kStride = (W + 7) / 8;

  void clear(bool ink = false) { memset(buf_, ink ? 0xFF : 0x00, sizeof(buf_)); }

  void set(int x, int y, bool ink) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t &b = buf_[y * kStride + (x >> 3)];
    uint8_t bit = 0x80 >> (x & 7);
    b = ink ? (b | bit) : (b & ~bit);
  }

  bool get(int x, int y) const {
    if (x < 0 || y < 0 || x >= W || y >= H) return false;
    return buf_[y * kStride + (x >> 3)] & (0x80 >> (x & 7));
  }

  void fillRect(int x, int y, int w, int h, bool ink) {
    for (int yy = y; yy < y + h; yy++)
      for (int xx = x; xx < x + w; xx++) set(xx, yy, ink);
  }

  int countInk() const {
    int n = 0;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) n += get(x, y);
    return n;
  }

  const uint8_t *data() const { return buf_; }
  // 0.10.1: immagine salvata nella memoria RTC prima del sonno profondo (stesso formato di data()).
  static constexpr int kBytes = kStride * H;
  void load(const uint8_t *src) { memcpy(buf_, src, sizeof(buf_)); }
  static bool bitOf(const uint8_t *img, int x, int y) { return img[y * kStride + (x >> 3)] & (0x80 >> (x & 7)); }

 private:
  uint8_t buf_[kStride * H];
};

// Superficie su cui disegna la mascotte: astrae il canvas concreto.
struct Surface {
  virtual void fillRect(int x, int y, int w, int h, bool ink) = 0;
  virtual ~Surface() = default;
};

template <int W, int H>
struct CanvasSurface : Surface {
  explicit CanvasSurface(Canvas<W, H> &c) : c(c) {}
  void fillRect(int x, int y, int w, int h, bool ink) override { c.fillRect(x, y, w, h, ink); }
  Canvas<W, H> &c;
};
