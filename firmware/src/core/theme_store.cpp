// Temi sulla scheda: installatore tutto o niente, ordine d'installazione, nono tema (core/theme_store.h).
#include "core/theme_store.h"

#include <string.h>

#include "core/strbuf.h"

namespace cal {
namespace theme {

namespace {

const int kOrderCap = kMaxThemes * kIdLen + 16;

void say(char *why, size_t cap, const char *s) { scopy(why, cap, s); }

// Ordine salvato («id\n» ripetuto, dal più vecchio), senza doppioni.
int readOrder(Storage &st, char (*ids)[kIdLen], int cap) {
  char buf[kOrderCap + 1];
  const int n = st.readOrder(buf, kOrderCap);
  if (n <= 0) return 0;
  buf[n] = 0;
  int k = 0;
  for (char *p = buf; *p && k < cap;) {
    char *e = strchr(p, '\n');
    if (e) *e = 0;
    if (validThemeId(p)) {
      bool dup = false;
      for (int i = 0; i < k; i++) dup = dup || !strcmp(ids[i], p);
      if (!dup) scopy(ids[k++], kIdLen, p);
    }
    if (!e) break;
    p = e + 1;
  }
  return k;
}

void writeOrder(Storage &st, char (*ids)[kIdLen], int n) {
  char buf[kOrderCap];
  Sb b(buf, sizeof buf);
  for (int i = 0; i < n; i++) b.s(ids[i]).ch('\n');
  st.writeOrder(buf, int(b.len()));
}

}  // namespace

bool validThemeId(const char *id) {
  const size_t n = id ? strlen(id) : 0;
  if (n < 1 || n > 24) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = id[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && i > 0 && i + 1 < n && id[i - 1] != '-');
    if (!ok) return false;
  }
  return true;
}

int orderList(Storage &st, char (*ids)[kIdLen], int cap) {
  char present[kMaxThemes + 1][kIdLen];
  const int np = st.list(present, kMaxThemes + 1);
  char saved[kMaxThemes + 1][kIdLen];
  const int ns = readOrder(st, saved, kMaxThemes + 1);
  int k = 0;
  // prima quelli presenti che l'ordine non conosce (i più vecchi per sicurezza), poi l'ordine salvato
  for (int i = 0; i < np && k < cap; i++) {
    bool known = false;
    for (int j = 0; j < ns; j++) known = known || !strcmp(saved[j], present[i]);
    if (!known) scopy(ids[k++], kIdLen, present[i]);
  }
  for (int j = 0; j < ns && k < cap; j++)
    if (st.size(saved[j]) >= 0) scopy(ids[k++], kIdLen, saved[j]);
  return k;
}

void orderDrop(Storage &st, const char *id) {
  char ids[kMaxThemes + 1][kIdLen];
  int n = orderList(st, ids, kMaxThemes + 1), k = 0;
  for (int i = 0; i < n; i++)
    if (strcmp(ids[i], id)) memcpy(ids[k++], ids[i], kIdLen);
  writeOrder(st, ids, k);
}

int orderTrim(Storage &st, const char *activeId) {
  char ids[kMaxThemes + 1][kIdLen];
  int removed = 0;
  for (int round = 0; round < 4; round++) {  // list() ne dà al massimo kMaxThemes + 1: a giri, finché si toglie
    const int n = orderList(st, ids, kMaxThemes + 1);
    if (n <= kMaxThemes) break;
    int extra = n - kMaxThemes, k = 0;
    for (int i = 0; i < n; i++) {
      if (extra > 0 && !(activeId && !strcmp(ids[i], activeId))) {
        st.remove(ids[i]);
        extra--;
        removed++;
        continue;
      }
      memcpy(ids[k++], ids[i], kIdLen);
    }
    writeOrder(st, ids, k);
    if (extra == n - kMaxThemes) break;  // niente tolto
  }
  return removed;
}

bool canUse(Storage &st, const char *id) { return !id || !id[0] || st.size(id) >= 0; }

bool Installer::begin(Storage &st, const char *id, uint32_t crc, uint32_t size, char *why, size_t cap) {
  abort();
  if (!validThemeId(id)) { say(why, cap, "id del tema non valido"); return false; }
  if (size < uint32_t(kHeaderLen) || size > kMaxFile) { say(why, cap, "dimensione del tema non valida"); return false; }
  if (!st.tmpBegin()) { say(why, cap, "spazio per i temi non disponibile"); return false; }
  st_ = &st;
  active_ = true;
  verified_ = false;
  got_ = 0;
  size_ = size;
  crc_ = crc;
  scopy(id_, kIdLen, id);
  return true;
}

bool Installer::write(const uint8_t *p, size_t n, char *why, size_t cap) {
  if (!active_) { say(why, cap, "nessuna installazione in corso"); return false; }
  if (n > size_ - got_) { say(why, cap, "più byte di quelli offerti"); abort(); return false; }
  if (!st_->tmpAppend(p, uint32_t(n))) { say(why, cap, "scrittura non riuscita"); abort(); return false; }
  got_ += uint32_t(n);
  return true;
}

bool Installer::verify(uint8_t *scratch, char *why, size_t cap) {
  if (!active_) { say(why, cap, "nessuna installazione in corso"); return false; }
  if (got_ != size_) { say(why, cap, "download incompleto"); abort(); return false; }
  Header h;
  IndexEntry idx[kMaxScreens];
  const Error e = verifyAt(*st_, nullptr, int32_t(size_), scratch, &h, idx);
  if (e != Error::Ok) { say(why, cap, errorText(e)); abort(); return false; }
  if (strcmp(h.id, id_)) { say(why, cap, "il tema non è quello offerto"); abort(); return false; }
  if (h.crc != crc_) { say(why, cap, "CRC diverso da quello offerto"); abort(); return false; }
  verified_ = true;
  return true;
}

bool Installer::commit(const char *activeId, char *why, size_t cap, const char *keepId) {
  if (!active_) { say(why, cap, "nessuna installazione in corso"); return false; }
  if (!verified_) { say(why, cap, "tema non controllato"); abort(); return false; }
  // Tutto o niente: prima si salva, poi si fa posto. L'ordine si legge prima della rinomina (il nuovo non c'è ancora,
  // o c'è la sua versione vecchia). Se la rinomina fallisce i temi sulla scheda restano quelli. Il nono file ci sta:
  // MemStorage ha 9 posti apposta, in LittleFS 9 × 64 KB stanno nella partizione.
  char ids[kMaxThemes + 1][kIdLen];
  const int n = orderList(*st_, ids, kMaxThemes + 1);
  if (!st_->tmpCommit(id_)) { say(why, cap, "salvataggio non riuscito"); abort(); return false; }
  active_ = verified_ = false;
  int others = 0;
  for (int i = 0; i < n; i++) others += strcmp(ids[i], id_) != 0;
  // con 9 temi esce il più vecchio che non è attivo (e non è questo); l'ordine: senza questo e senza i tolti, poi questo
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!strcmp(ids[i], id_)) continue;
    const bool kept = (activeId && !strcmp(ids[i], activeId)) || (keepId && !strcmp(ids[i], keepId));
    if (others >= kMaxThemes && !kept) {
      st_->remove(ids[i]);
      others--;
      continue;
    }
    memcpy(ids[k++], ids[i], kIdLen);
  }
  scopy(ids[k++], kIdLen, id_);
  writeOrder(*st_, ids, k);
  return true;
}

bool Installer::finish(const char *activeId, uint8_t *scratch, char *why, size_t cap) {
  return verify(scratch, why, cap) && commit(activeId, why, cap);
}

void Installer::abort() {
  if (active_ && st_) st_->tmpDrop();
  active_ = false;
  verified_ = false;
}

void dropAllThemes(Storage &st) {
  char ids[kMaxThemes + 1][kIdLen];
  for (;;) {  // a giri: list() ne dà al massimo kMaxThemes + 1; si ferma se un giro non toglie niente
    const int n = st.list(ids, kMaxThemes + 1);
    int gone = 0;
    for (int i = 0; i < n; i++) {
      st.remove(ids[i]);
      gone += st.size(ids[i]) < 0;
    }
    if (!n || !gone) break;
  }
  st.tmpDrop();
}

}  // namespace theme
}  // namespace cal
