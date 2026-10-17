// Temi (docs/TEMI.md §3): archivio astratto, tema attivo e uscita di emergenza. Nessuna allocazione (test_theme_runtime).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/clock.h"
#include "core/theme_format.h"

namespace cal {
namespace theme {

// Archivio dei temi: sulla scheda hw/theme_fs (piano 1b), nei test MemStorage.
const int32_t kSizeIo = -2;  // Storage::size: il file c'è ma non si apre (memoria, flash): errore di I/O

class Storage {
 public:
  virtual ~Storage() {}
  virtual int list(char (*ids)[kIdLen], int cap) = 0;
  virtual int32_t size(const char *id) = 0;  // -1 se manca, kSizeIo se c'è ma non si apre
  virtual bool read(const char *id, uint32_t off, uint8_t *dst, uint32_t n) = 0;
  virtual void remove(const char *id) = 0;
  virtual bool tmpBegin() = 0;
  virtual bool tmpAppend(const uint8_t *p, uint32_t n) = 0;
  virtual int32_t tmpSize() = 0;
  virtual bool tmpRead(uint32_t off, uint8_t *dst, uint32_t n) = 0;
  virtual bool tmpCommit(const char *id) = 0;  // rinomina sopra <id>
  virtual void tmpDrop() = 0;
  virtual int readOrder(char *dst, int cap) = 0;  // -1 se manca
  virtual bool writeOrder(const char *src, int n) = 0;
  virtual void eraseAll() = 0;
};

// In RAM: per i test native (sulla scheda c'è hw/theme_fs, piano 1b). Posti: 8 temi + il file temporaneo.
class MemStorage : public Storage {
 public:
  MemStorage() { eraseAll(); }
  int list(char (*ids)[kIdLen], int cap) override;
  int32_t size(const char *id) override;
  bool read(const char *id, uint32_t off, uint8_t *dst, uint32_t n) override;
  void remove(const char *id) override;
  bool tmpBegin() override;
  bool tmpAppend(const uint8_t *p, uint32_t n) override;
  int32_t tmpSize() override { return tmpUsed_ ? int32_t(tmpLen_) : -1; }
  bool tmpRead(uint32_t off, uint8_t *dst, uint32_t n) override;
  bool tmpCommit(const char *id) override;
  void tmpDrop() override { tmpUsed_ = false; tmpLen_ = 0; }
  int readOrder(char *dst, int cap) override;
  bool writeOrder(const char *src, int n) override;
  void eraseAll() override;
  uint8_t *raw(const char *id);
  bool failWrites;
  int failAfterReads;  // ≥ 0: read() fallisce dopo tante letture riuscite (I/O, memoria sulla scheda); -1 = mai
  int commits;
  int reads;  // letture riuscite (i test controllano che il disegno non ricarichi a vuoto)

 private:
  static const int kSlots = kMaxThemes + 1;  // uno in più: si rinomina prima di togliere il più vecchio
  char id_[kSlots][kIdLen];
  uint32_t len_[kSlots];
  uint8_t data_[kSlots][kMaxFile];
  uint8_t tmp_[kMaxFile];
  uint32_t tmpLen_;
  bool tmpUsed_;
  char order_[kMaxThemes * kIdLen + 16];
  int orderLen_;
};

struct CatalogEntry {
  char id[kIdLen];
  char name[kNameLen];
  uint32_t crc;
  uint32_t size;
};
// Solo i file con l'intestazione valida, nell'ordine di list.
int catalog(Storage &st, CatalogEntry *out, int cap);

// File intero: intestazione, lunghezza, CRC a pezzi da kMaxBlock nello scratch, indice, ogni blocco. scratch = kMaxBlock byte.
// id nullptr = il file temporaneo (installatore del piano 1b).
// io (se c'è): true se il controllo non è arrivato in fondo per un open/read fallito (il file può essere buono),
// false se il contenuto è rovinato o il file manca. Il codice d'errore resta quello di sempre.
Error verifyAt(Storage &st, const char *id, int32_t size, uint8_t *scratch, Header *h, IndexEntry *idx, bool *io = nullptr);
Error verifyStored(Storage &st, const char *id, uint8_t *scratch, Header *h, bool *io = nullptr);

// Arena del Runtime (0.13.0): il blocco della schermata (kMaxBlock) e subito dopo i buffer del disegno dei temi
// (ui/theme_draw: testo espanso, righe, pezzi di expand, QR). Si presta tutta alla partita: tre pezzi da 2 KB.
const int kDrawScratch = 2048;                    // ne usa 1 716 (static_assert in ui/theme_draw.cpp)
const int kLendBytes = kMaxBlock + kDrawScratch;  // 6 144

// Tema attivo. Il blocco all'inizio dell'arena resta quello letto finché non cambia schermata: chi sostituisce o
// cancella il file del tema attivo (installatore, piano 1b) deve richiamare activate() subito dopo.
class Runtime {
 public:
  Runtime();
  void begin(Storage *st);
  bool activate(const char *id);  // "" = di serie; false = mancante o rovinato: resta di serie e il tema risulta rotto
  // Risveglio dell'ora (0.13.0): il tema era già stato controllato tutto quando si è disegnato il Riposo. Si leggono
  // solo intestazione e indice e si confronta il CRC dell'intestazione con quello salvato in RestKeep; ogni blocco
  // passa comunque checkBlock in block() e nel disegno. false (diverso, mancante, illeggibile, in prestito): niente
  // tema rotto, il chiamante fa il risveglio pieno e lì activate() controlla tutto.
  bool activateKnown(const char *id, uint32_t crc);
  const char *active() const { return id_; }
  uint32_t activeCrc() const { return id_[0] ? h_.crc : 0; }
  bool has(Screen s) const;
  bool block(Screen s, const uint8_t *&b, uint16_t &n);
  void suspend(bool on) { suspended_ = on; loaded_ = Screen::None; }
  bool suspended() const { return suspended_; }
  bool takeBroken(char *id, size_t cap);
  // Buffer del disegno dei temi (kDrawScratch byte) per ui::drawThemeBlock; nullptr in prestito.
  uint8_t *drawScratch() { return lent_ ? nullptr : arena_ + kMaxBlock; }
  // Modalità gioco (net::suspended(), 0.13.0): l'arena si presta alla partita come RAM della cartuccia, come l'anello
  // del registro (LogRing::lend). lend: l'arena se n ≤ kLendBytes e non già prestata, altrimenti nullptr; il blocco
  // caricato si dimentica. In prestito has(), block() e activate() rispondono false senza segnare il tema rotto (id,
  // intestazione e indice restano) e drawScratch() è nullptr: niente disegno a tema. giveBack: l'arena torna al
  // Runtime, il blocco si rilegge al prossimo disegno.
  uint8_t *lend(size_t n);
  void giveBack();
  bool lent() const { return lent_; }

 private:
  void broke();
  Storage *st_;
  char id_[kIdLen];
  Header h_;
  IndexEntry idx_[kMaxScreens];
  // [0, kMaxBlock): il blocco della schermata corrente (spec §3: buffer statico da 4 096 byte); poi il disegno
  alignas(8) uint8_t arena_[kLendBytes];
  Screen loaded_;
  bool suspended_;
  bool broken_;
  bool lent_;
  char brokenId_[kIdLen];
};

// Avvio (0.13.0): il tema della NVS si usa solo se il file c'è ed è integro; altrimenti di serie e broken (il loop
// mostra «Tema rovinato: uso quello di serie», toglie il file e svuota la NVS). scratch = kMaxBlock byte.
struct BootPlan { bool use; bool broken; char id[kIdLen]; };
BootPlan themeBootPlan(Storage &st, const char *nvsCur, uint8_t *scratch);

// Uscita di emergenza (spec §3): PRG tenuto 1,5 s durante la schermata di avvio → tema di serie per questo avvio.
// Non all'accensione col tasto già giù: GPIO0 è il pin di boot e la ROM entrerebbe nel caricatore seriale.
// Il tempo si conta dai fronti dell'interrupt (istante vero), non dal loop: il setup e il primo completo (1,5 s)
// bloccano il loop per buona parte dei 2,6 s dell'avvio. Finché il tasto premuto durante l'avvio non ha deciso
// (keepBoot), la schermata di avvio resta (al massimo kKeepBootMs dalla pressione).
class SafeBoot {
 public:
  static const Ms kHoldMs;      // 1 500
  static const Ms kKeepBootMs;  // 3 000: tetto per tenere l'avvio a schermo
  SafeBoot() : armed_(false), fired_(false), due_(false), down_(false), swallow_(false), release_(false), since_(0) {}
  // Fronte del tasto con il suo istante (ms dell'interrupt); boot = la cima è la schermata di avvio quando si conta.
  void edge(bool down, Ms t, bool boot);
  bool step(Ms now);              // true una volta sola: tenuto kHoldMs dalla pressione (anche rilasciato dopo)
  bool keepBoot(Ms now) const;    // pressione dell'avvio ancora da decidere: l'avvio resta
  // Gesto da scartare: quelli col tasto ancora giù dopo l'uscita (VeryLong a 3 s) e quello del rilascio. Si azzera al
  // fronte dopo: un rilascio senza gesto non mangia la pressione successiva.
  bool swallow();

 private:
  bool armed_, fired_, due_, down_, swallow_, release_;
  Ms since_;
};

}  // namespace theme
}  // namespace cal
