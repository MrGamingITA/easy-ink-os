// Giochi Game Boy: salvataggi della cartuccia (/gb/<id>.sav in LittleFS, hw/gb_hw). Il file è la RAM della
// cartuccia seguita da 4 byte di CRC-32 IEEE (little endian): un file troncato o rovinato si riconosce. Salvataggio
// automatico (AutoSave): 3 s dopo l'ultima scrittura vera del gioco, il primo subito, poi al più uno al minuto.
// Logica pura, provata in native (test_gb_save).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace gb {

const uint32_t kCrcInit = 0xFFFFFFFFu;
uint32_t crc32Update(uint32_t crc, const uint8_t *d, size_t n);  // CRC-32 IEEE a pezzi (come hash/crc32 del servizio)
inline uint32_t crc32Finish(uint32_t crc) { return ~crc; }

const size_t kTrailerLen = 4;
void packTrailer(uint32_t crc, uint8_t out[kTrailerLen]);
uint32_t unpackTrailer(const uint8_t in[kTrailerLen]);
inline size_t fileLen(size_t ramSize) { return ramSize + kTrailerLen; }

const uint32_t kAutoQuietMs = 3000;   // calma dopo l'ultima scrittura
const uint32_t kAutoEveryMs = 60000;  // fra due salvataggi automatici (e dopo uno non riuscito)

class AutoSave {
 public:
  AutoSave() : writes_(0), changeAt_(0), savedAt_(0), retryAt_(0), saved_(false) {}
  void start(uint32_t nowMs, uint32_t writes);
  // writes: Emu::ramWrites(); dirty: Emu::ramDirty(). true = salvare adesso.
  bool due(uint32_t nowMs, uint32_t writes, bool dirty);
  void saved(uint32_t nowMs) { saved_ = true; savedAt_ = nowMs; retryAt_ = 0; }
  void failed(uint32_t nowMs) { retryAt_ = nowMs + kAutoEveryMs; }

 private:
  uint32_t writes_, changeAt_, savedAt_, retryAt_;
  bool saved_;
};

}  // namespace gb
}  // namespace cal
