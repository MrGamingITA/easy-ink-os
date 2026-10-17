// theme_store.h — Temi sulla scheda (PROTOCOLLO.md «Temi»): installazione tutto o niente su Storage (LittleFS in
// hw/theme_fs, MemStorage nei test). Il file arriva a pezzi nel temporaneo, si controlla tutto (CRC, intestazione,
// ogni blocco con l'interprete in sola lettura), poi si rinomina sopra <id>. Al massimo 8 temi: esce il più vecchio
// non attivo (ordine d'installazione nel file «order»).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/theme_runtime.h"

namespace cal {
namespace theme {

// write(), verify(), commit() o finish() che falliscono chiudono l'installazione da soli (temporaneo buttato, busy()
// falso): un chiamante che dimentica abort() non blocca le offerte successive. abort() resta lecito e non fa niente in più.
// verify (CRC e blocchi, lungo) e commit (rinomina, nono tema, ordine) sono separati perché il lucchetto di theme_fs
// serve solo attorno a commit; finish = verify + commit.
class Installer {
 public:
  Installer() : st_(nullptr), active_(false), verified_(false), got_(0), size_(0), crc_(0) { id_[0] = 0; }
  bool begin(Storage &st, const char *id, uint32_t crc, uint32_t size, char *why, size_t cap);
  bool write(const uint8_t *p, size_t n, char *why, size_t cap);
  bool verify(uint8_t *scratch, char *why, size_t cap);
  // solo dopo verify riuscito. keepId (0.13.0): il tema che il loop sta attivando (themefs::protectedId), mai tolto
  // come l'attivo, anche prima che ThemeCur arrivi al task di rete.
  bool commit(const char *activeId, char *why, size_t cap, const char *keepId = nullptr);
  bool finish(const char *activeId, uint8_t *scratch, char *why, size_t cap);
  void abort();
  bool busy() const { return active_; }
  const char *id() const { return id_; }

 private:
  Storage *st_;
  bool active_;
  bool verified_;
  uint32_t got_, size_, crc_;
  char id_[kIdLen];
};

int orderList(Storage &st, char (*ids)[kIdLen], int cap);
void orderDrop(Storage &st, const char *id);
bool canUse(Storage &st, const char *id);
// Avvio (0.13.0): un riavvio fra la rinomina del nono tema e la riscrittura dell'ordine lascia 9 file. Si torna a
// kMaxThemes togliendo i più vecchi non attivi (orderList: prima quelli che l'ordine non conosce) e si riscrive
// l'ordine. Quanti ne ha tolti.
int orderTrim(Storage &st, const char *activeId);
bool validThemeId(const char *id);
// Toglie tutti i temi che list() trova (anche più di kMaxThemes + 1, a giri) e il temporaneo. L'ordine no: è di chi chiama.
void dropAllThemes(Storage &st);

}  // namespace theme
}  // namespace cal
