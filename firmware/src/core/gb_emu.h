// Giochi Game Boy: emulatore (Peanut-GB, lib/peanut_gb, MIT) dietro un'interfaccia piccola, provata in native
// (test_gb_emu). La ROM resta di chi chiama: sulla scheda è la partizione «gbrom» mappata in memoria.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <time.h>

struct gb_s;

namespace cal {
namespace gb {

const int kLcdW = 160, kLcdH = 144;
const int kFrameStride = kLcdW / 4;                 // 2 bit per pixel, MSB = x più a sinistra
const size_t kFrameBytes = size_t(kFrameStride) * kLcdH;  // 5760
const size_t kCartRamMax = 0x8000;                  // 32 KB: oltre, «Gioco non supportato»
const uint32_t kRomMin = 0x150;                     // l'intestazione finisce a 0x14F
const size_t kRamBank = 0x800;  // pezzo della RAM della cartuccia (2 KB, 0.12.3: lo heap in gioco è frammentato)
const int kMaxRamBanks = int(kCartRamMax / kRamBank);  // 16

// Tasti del Game Boy: bit a 1 = premuto (Peanut-GB li vuole al contrario, ci pensa Emu).
enum Key : uint8_t {
  KeyA = 0x01, KeyB = 0x02, KeySelect = 0x04, KeyStart = 0x08,
  KeyRight = 0x10, KeyLeft = 0x20, KeyUp = 0x40, KeyDown = 0x80,
};

enum class Load : uint8_t { Ok, TooSmall, BadChecksum, Unsupported, RamTooBig };
const char *loadKey(Load l);  // per il registro: ok, piccola, checksum, mbc, ram

// Tonalità del pixel (0 = bianco … 3 = nero) in un fotogramma da kFrameBytes.
inline uint8_t shadeAt(const uint8_t *f, int x, int y) {
  return (f[y * kFrameStride + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;
}

class Emu {
 public:
  Emu() {}
  ~Emu();
  Emu(const Emu &) = delete;
  Emu &operator=(const Emu &) = delete;

  // Controlla l'intestazione e prepara la CPU. Il contesto (~17 KB) va sullo heap: false se manca memoria.
  Load load(const uint8_t *rom, uint32_t romSize);
  bool loaded() const { return gb_ != nullptr; }
  size_t ramSize() const { return ramSize_; }          // byte di RAM della cartuccia (0 = nessuna)
  int ramBanks() const { return int((ramSize_ + kRamBank - 1) / kRamBank); }  // pezzi da 2 KB (l'ultimo può essere più corto)
  // Dopo load(): buffer di ramSize() byte, già riempito (salvataggio o 0xFF). Resta di chi chiama.
  void attachRam(uint8_t *ram);
  // Oppure un banco alla volta (i < ramBanks()): sulla scheda niente blocco da 32 KB contiguo nello heap.
  void attachBank(int i, uint8_t *bank) { if (i >= 0 && i < kMaxRamBanks) bank_[i] = bank; }
  static size_t contextBytes();                          // contesto di Peanut-GB (sullo heap da load())
  void setRtc(const struct tm &t);                       // MBC3: orologio della cartuccia
  void frameSkip(bool on);                               // disegna un fotogramma sì e uno no
  // Un fotogramma (70224 cicli). keys: tasti premuti (Key). false = la CPU si è fermata (codice non valido).
  bool runFrame(uint8_t keys);
  const uint8_t *frame() const { return frame_; }
  uint32_t frames() const { return frames_; }            // fotogrammi disegnati (cresce; con frameSkip uno su due)
  bool ramDirty() const { return dirty_; }
  uint32_t ramWrites() const { return writes_; }         // scritture che hanno cambiato un byte (cresce e basta)
  void clearDirty() { dirty_ = false; }
  void title(char out[17]) const;                        // titolo dell'intestazione (0x134, ≤ 16), ripulito
  // Byte mandati sulla porta seriale (solo test: le ROM di Blargg scrivono lì l'esito). nullptr = nessun cavo.
  void onSerial(void (*fn)(uint8_t c));
  void unload();

  // Richiamate da Peanut-GB (pubbliche solo per i trampolini in gb_emu.cpp).
  uint8_t romRead(uint32_t a) const { return a < romSize_ ? rom_[a] : 0xFF; }
  uint8_t ramRead(uint32_t a) const {
    const uint8_t *b = a < ramSize_ ? bank_[a / kRamBank] : nullptr;
    return b ? b[a % kRamBank] : 0xFF;
  }
  void ramWrite(uint32_t a, uint8_t v);
  void drawLine(const uint8_t *px, unsigned line);
  [[noreturn]] void fault();  // da gb_error: Peanut-GB non prevede il ritorno (PGB_UNREACHABLE)
  void serialOut(uint8_t c) { if (serial_) serial_(c); }

 private:
  struct gb_s *gb_ = nullptr;
  const uint8_t *rom_ = nullptr;
  uint32_t romSize_ = 0;
  uint8_t *bank_[kMaxRamBanks] = {};
  size_t ramSize_ = 0;
  uint32_t frames_ = 0;
  uint32_t writes_ = 0;
  bool dirty_ = false, fault_ = false;
  void (*serial_)(uint8_t) = nullptr;
  jmp_buf jb_;
  uint8_t frame_[kFrameBytes] = {};
};

}  // namespace gb
}  // namespace cal
