// Peanut-GB è il ciclo caldo della partita: sulla scheda (GCC, il core compila con -Os) questo file va a -O2.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "gb_emu.h"

#include <stdlib.h>
#include <string.h>

// Peanut-GB: niente audio, 4 tonalità senza tavolozze a colori, tutto in questo file (unica unità con il codice).
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_12_COLOUR 0
#include <peanut_gb.h>

namespace cal {
namespace gb {

namespace {
Emu *self(struct gb_s *g) { return static_cast<Emu *>(g->direct.priv); }
uint8_t romRd(struct gb_s *g, const uint_fast32_t a) { return self(g)->romRead(uint32_t(a)); }
uint8_t ramRd(struct gb_s *g, const uint_fast32_t a) { return self(g)->ramRead(uint32_t(a)); }
void ramWr(struct gb_s *g, const uint_fast32_t a, const uint8_t v) { self(g)->ramWrite(uint32_t(a), v); }
void onError(struct gb_s *g, const enum gb_error_e, const uint16_t) { self(g)->fault(); }
void serTx(struct gb_s *g, const uint8_t c) { self(g)->serialOut(c); }
enum gb_serial_rx_ret_e serRx(struct gb_s *, uint8_t *) { return GB_SERIAL_RX_NO_CONNECTION; }
void onLine(struct gb_s *g, const uint8_t *px, const uint_fast8_t line) { self(g)->drawLine(px, line); }
}  // namespace

const char *loadKey(Load l) {
  switch (l) {
    case Load::Ok: return "ok";
    case Load::TooSmall: return "piccola";
    case Load::BadChecksum: return "checksum";
    case Load::Unsupported: return "mbc";
    case Load::RamTooBig: return "ram";
  }
  return "?";
}

Emu::~Emu() { unload(); }

void Emu::unload() {
  free(gb_);
  gb_ = nullptr;
  rom_ = nullptr;
  memset(bank_, 0, sizeof bank_);
  serial_ = nullptr;
  romSize_ = 0;
  ramSize_ = 0;
  frames_ = writes_ = 0;
  dirty_ = fault_ = false;
}

size_t Emu::contextBytes() { return sizeof(struct gb_s); }

void Emu::attachRam(uint8_t *ram) {
  memset(bank_, 0, sizeof bank_);
  for (int i = 0; i < ramBanks() && ram; i++) bank_[i] = ram + size_t(i) * kRamBank;
}

Load Emu::load(const uint8_t *rom, uint32_t romSize) {
  unload();
  if (!rom || romSize < kRomMin) return Load::TooSmall;
  // Peanut-GB indicizza una tabella di 9 voci con il byte 0x148: i codici vecchi (0x52–0x54) la sforerebbero.
  if (rom[0x148] > 8) return Load::Unsupported;
  gb_ = static_cast<struct gb_s *>(malloc(sizeof(struct gb_s)));
  if (!gb_) return Load::Unsupported;
  rom_ = rom;
  romSize_ = romSize;
  const enum gb_init_error_e e = gb_init(gb_, romRd, ramRd, ramWr, onError, this);
  if (e != GB_INIT_NO_ERROR) {
    unload();
    return e == GB_INIT_INVALID_CHECKSUM ? Load::BadChecksum : Load::Unsupported;
  }
  size_t n = 0;
  if (gb_get_save_size_s(gb_, &n) != 0 || n > kCartRamMax) {
    unload();
    return Load::RamTooBig;
  }
  ramSize_ = n;
  gb_init_lcd(gb_, onLine);
  memset(frame_, 0, sizeof frame_);
  return Load::Ok;
}

void Emu::setRtc(const struct tm &t) {
  if (gb_) gb_set_rtc(gb_, &t);
}

void Emu::onSerial(void (*fn)(uint8_t c)) {
  serial_ = fn;
  if (gb_) gb_init_serial(gb_, fn ? serTx : nullptr, fn ? serRx : nullptr);
}

void Emu::frameSkip(bool on) {
  if (gb_) gb_->direct.frame_skip = on;
}

bool Emu::runFrame(uint8_t keys) {
  if (!gb_ || fault_) return false;
  gb_->direct.joypad = uint8_t(~keys);  // Peanut-GB: 0 = premuto
  // Codice non valido o lettura impossibile: gb_error salta qui (fault), il fotogramma resta a metà.
  if (setjmp(jb_)) return false;
  gb_run_frame(gb_);
  return true;
}

void Emu::fault() {
  fault_ = true;
  longjmp(jb_, 1);
}

void Emu::ramWrite(uint32_t a, uint8_t v) {
  uint8_t *b = a < ramSize_ ? bank_[a / kRamBank] : nullptr;
  if (!b || b[a % kRamBank] == v) return;
  b[a % kRamBank] = v;
  dirty_ = true;
  writes_++;
}

void Emu::drawLine(const uint8_t *px, unsigned line) {
  if (line >= unsigned(kLcdH)) return;
  if (line == unsigned(kLcdH - 1)) frames_++;
  uint8_t *row = frame_ + line * kFrameStride;
  for (int b = 0; b < kFrameStride; b++) {
    const uint8_t *p = px + b * 4;
    row[b] = uint8_t(((p[0] & 3) << 6) | ((p[1] & 3) << 4) | ((p[2] & 3) << 2) | (p[3] & 3));
  }
}

void Emu::title(char out[17]) const {
  int n = 0;
  for (uint32_t a = 0x134; a < 0x144 && n < 16; a++) {
    const uint8_t c = romRead(a);
    if (c == 0 || c >= 0x80) break;  // 0x143 può essere il flag CGB (0x80/0xC0)
    out[n++] = (c >= 0x20 && c < 0x7F) ? char(c) : '?';
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

}  // namespace gb
}  // namespace cal
