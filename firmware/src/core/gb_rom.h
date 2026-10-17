// Giochi Game Boy: la ROM nella partizione «gbrom» (scritta col cavo), riconosciuta dall'intestazione della cartuccia
// (0x100–0x14F): titolo, dimensione dichiarata, tipo di cartuccia, RAM, checksum dell'intestazione e globale. L'id
// del gioco (nome del salvataggio /gb/<id>.sav) = primi 16 caratteri dello SHA-256 della ROM (contratto dei piani).
// Logica pura, provata in native (test_gb_rom).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "gb_model.h"

namespace cal {
namespace gb {

const size_t kHeaderLen = 0x150;

struct RomInfo {
  char title[kTitleLen];  // 0x134, ≤ 16 ASCII, ripulito ('' = senza titolo)
  uint32_t size;          // dichiarata: 32 KB << 0x148
  uint8_t cartType;       // 0x147 (MBC)
  uint32_t ramBytes;      // dichiarata da 0x149 (MBC2: 512)
  uint16_t global;        // 0x14E–0x14F
};

// Intestazione valida: almeno kHeaderLen byte, checksum 0x14D giusto, codice 0x148 ≤ 8. false su flash cancellata.
bool parseHeader(const uint8_t *rom, size_t n, RomInfo &out);
bool fitsPartition(const RomInfo &r, uint32_t capacity);
// Somma di tutti i byte tranne 0x14E–0x14F (modulo 65536), su n byte.
uint16_t globalChecksum(const uint8_t *rom, uint32_t n);
// Checksum globale uguale a quello dell'intestazione su info.size byte (ROM scritta tutta e intera).
bool globalOk(const uint8_t *rom, const RomInfo &info);
// 16 cifre esadecimali minuscole dai primi 8 byte dello SHA-256.
void gameId(const uint8_t sha[32], char out[kIdLen]);

}  // namespace gb
}  // namespace cal
