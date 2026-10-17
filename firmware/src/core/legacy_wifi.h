// Migrazione della rete salvata da M3 («calamaio-net», stringhe in chiaro) nel vault (M5). Logica pura,
// provata in native (test_vault). Il namespace si apre prima in SOLA LETTURA: aperto in scrittura, la NVS lo
// creava vuoto a ogni avvio (residuo della prova 9, DECISIONI M5). In scrittura solo per togliere le voci
// dopo che la copia nel vault è stata riletta uguale.
#pragma once
#include <stddef.h>

#include "vault.h"

namespace cal {

class LegacyStore {
 public:
  virtual ~LegacyStore() {}
  virtual bool openRead() = 0;   // false = namespace assente (mai creato o già tolto dalla pulizia): niente da fare
  virtual bool get(const char *key, char *out, size_t cap) = 0;  // false = chiave assente
  virtual bool openWrite() = 0;
  virtual void remove(const char *key) = 0;
  virtual void close() = 0;
};

enum class LegacyResult { NoNamespace, Empty, Migrated, Failed };
const char *legacyResultText(LegacyResult r);

LegacyResult migrateLegacyWifi(LegacyStore &st, Vault &v);

}  // namespace cal
