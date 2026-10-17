// Ancore dei predefiniti e catalogo degli accessori compilati nel firmware (accessory_builtin.cpp, generato da
// tools/sim-capture/gen-avatars.mjs da avatars/ancore/ e avatars/accessori/): funzionano anche senza rete.
#pragma once
#include <stdint.h>

// Blocco CANC v1 (docs/AVATAR.md §13.4) di un avatar predefinito: vale solo per il CAVT con CRC avatarCrc.
struct BuiltinAnchors {
  const char *avatar;
  const uint8_t *data;
  uint16_t len;
  uint32_t avatarCrc;
};
extern const BuiltinAnchors kBuiltinAnchors[];
extern const int kBuiltinAnchorsCount;

// Accessorio del catalogo (CACC v1, docs/AVATAR.md §14.3), nell'ordine della specifica del gioco §1.5.
struct BuiltinAccessory {
  const char *id;
  const uint8_t *data;
  uint16_t len;
  uint32_t crc;
};
extern const BuiltinAccessory kBuiltinAccessories[];
extern const int kBuiltinAccessoryCount;
