// La NVS vera (IDF 4.4, partizione «nvs») per la pulizia profonda di core/nvs_scrub, più due misure
// che servono alla seriale: voci valide e ricerca di un segreto noto nei byte grezzi della partizione.
// Solo all'avvio, quando nessun altro ha handle NVS aperti (vedi secrets::bootScrub).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/nvs_scrub.h"

namespace nvsidf {

class Port : public cal::NvsPort {
 public:
  ~Port() override { close(); }
  int list(cal::NvsItem *out, int cap) override;
  bool size(const cal::NvsItem &it, size_t *n) override;
  bool read(const cal::NvsItem &it, uint8_t *out, size_t n) override;
  bool erasePartition() override;
  bool write(const cal::NvsItem &it, const uint8_t *d) override;
  bool commit() override;

 private:
  bool open(const char *ns, bool rw);
  bool close();  // con commit se l'handle era in scrittura
  uint32_t h_ = 0;
  bool open_ = false, rw_ = false;
  char ns_[16] = "";
};

// Voci valide in tutti i namespace (-1 se la NVS non risponde).
int liveEntries();
// Volte in cui needle compare nei byte grezzi della partizione «nvs», voci cancellate comprese (-1 errore).
// partBytes: misura della partizione.
int countInPartition(const uint8_t *needle, size_t n, size_t *partBytes);

}  // namespace nvsidf
