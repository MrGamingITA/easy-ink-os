// Pulizia profonda della NVS (docs/M5-PERMESSI.md §4, prova 9).
//
// La NVS non cancella davvero: una voce sovrascritta o tolta viene solo marcata come cancellata nella
// bitmap della pagina, e i suoi byte restano nella flash finché la pagina non viene riciclata. Dal vivo
// (prova 9) la vecchia `pair-key` in chiaro era ancora leggibile nel dump dopo «Proteggi le chiavi».
//
// La pulizia copia in RAM tutte le voci valide di tutti i namespace (anche quelli che non conosce),
// cancella fisicamente la partizione (tutti i settori a 0xFF), riscrive le voci e le rilegge.
// Niente si cancella se una lettura non riesce o se le voci superano il limite di sicurezza.
// I namespace da eliminare (copia di sistema del Wi-Fi, vecchio spazio in chiaro di M3) non si riscrivono.
//
// Logica pura: la NVS sta dietro NvsPort (sulla scheda hw/nvs_idf.cpp, nei test una NVS finta).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {

// Stessi valori di nvs_type_t dell'IDF 4.4.
enum class NvsType : uint8_t {
  U8 = 0x01, I8 = 0x11, U16 = 0x02, I16 = 0x12, U32 = 0x04, I32 = 0x14, U64 = 0x08, I64 = 0x18,
  Str = 0x21, Blob = 0x42,
};
// Byte del valore per i tipi interi (1, 2, 4, 8), 0 per stringhe e blob.
size_t nvsIntSize(NvsType t);
const char *nvsTypeKey(NvsType t);

// Una voce: namespace e chiave (al massimo 15 caratteri nella NVS), tipo e, dopo la copia, il valore
// nell'arena (interi little endian, stringhe con il terminatore, blob così come sono).
struct NvsItem {
  char ns[16];
  char key[16];
  NvsType type;
  uint32_t len;
  uint32_t off;
};

class NvsPort {
 public:
  virtual ~NvsPort() {}
  // Voci valide di tutta la partizione, in tutti i namespace. Ne scrive al massimo cap in out e
  // restituisce quante ne ha trovate (anche più di cap); -1 se l'elenco non riesce.
  virtual int list(NvsItem *out, int cap) = 0;
  virtual bool size(const NvsItem &it, size_t *n) = 0;                      // byte del valore
  virtual bool read(const NvsItem &it, uint8_t *out, size_t n) = 0;          // esattamente n byte
  virtual bool erasePartition() = 0;  // chiude tutto, cancella fisicamente la partizione, la riapre vuota
  virtual bool write(const NvsItem &it, const uint8_t *d) = 0;               // it.len byte
  virtual bool commit() = 0;          // conferma le scritture e chiude gli handle
};

enum class ScrubOutcome : uint8_t {
  Done,         // partizione cancellata e voci riscritte, rilette uguali
  ListFailed,   // elenco delle voci non riuscito: niente cancellato
  TooMany,      // più voci della tabella: niente cancellato
  TooBig,       // valori oltre il limite di sicurezza: niente cancellato
  ReadFailed,   // una lettura non è riuscita: niente cancellato
  EraseFailed,  // la cancellazione fisica non è riuscita (le voci sono state comunque riscritte)
  WriteFailed,  // riscrittura o rilettura non riuscite anche al secondo tentativo: VOCI PERSE (vedi log)
};
const char *scrubOutcomeText(ScrubOutcome o);

struct ScrubReport {
  ScrubOutcome outcome;
  int found;      // voci valide trovate
  int kept;       // voci riscritte
  int dropped;    // voci dei namespace esclusi (non riscritte)
  size_t bytes;   // byte dei valori riscritti
  int attempts;   // cancellazioni + riscritture eseguite (1, o 2 se la prima rilettura non tornava)
};

struct ScrubConfig {
  const char *const *dropNs;  // namespace da non riscrivere
  int nDropNs;
  size_t maxBytes;            // limite di sicurezza sui valori copiati in RAM
};

// Lavoro: items (itemCap voci) e arena (arenaCap byte) li dà chi chiama. L'arena contiene i segreti in
// chiaro: alla fine si azzera, qualunque sia l'esito. log: righe leggibili per la seriale (mai i valori).
ScrubReport scrubNvs(NvsPort &port, const ScrubConfig &cfg, NvsItem *items, int itemCap, uint8_t *arena,
                     size_t arenaCap, char *log, size_t logCap);

// Quante volte needle compare in hay (anche sovrapposte). Per cercare un segreto noto nella flash grezza.
int countOccurrences(const uint8_t *hay, size_t n, const uint8_t *needle, size_t m);

}  // namespace cal
