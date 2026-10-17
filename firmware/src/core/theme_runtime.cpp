#include "core/theme_runtime.h"

#include <string.h>

namespace cal {
namespace theme {

const Ms SafeBoot::kHoldMs = 1500;

const Ms SafeBoot::kKeepBootMs = 3000;

void SafeBoot::edge(bool down, Ms t, bool boot) {
  release_ = false;  // il gesto del rilascio di prima, se c'era, è già passato
  if (down) {
    down_ = true;
    if (!fired_ && boot) { armed_ = true; since_ = t; }
    return;
  }
  down_ = false;
  if (armed_ && !fired_ && t - since_ >= kHoldMs) { fired_ = due_ = true; swallow_ = true; }  // rilascio arrivato tardi
  armed_ = false;
  if (swallow_) { swallow_ = false; release_ = true; }  // si scarta solo il gesto di questo rilascio
}

bool SafeBoot::step(Ms now) {
  if (armed_ && down_ && !fired_ && now - since_ >= kHoldMs) { fired_ = due_ = swallow_ = true; armed_ = false; }
  const bool d = due_;
  due_ = false;
  return d;
}

bool SafeBoot::keepBoot(Ms now) const { return armed_ && down_ && !fired_ && now - since_ < kKeepBootMs; }

bool SafeBoot::swallow() {
  if (swallow_) return true;
  if (release_) { release_ = false; return true; }
  return false;
}

// ---------- MemStorage ----------
namespace {
int findSlot(const char (*ids)[kIdLen], int n, const char *id) {
  for (int i = 0; i < n; i++)
    if (ids[i][0] && !strcmp(ids[i], id)) return i;
  return -1;
}
}  // namespace

void MemStorage::eraseAll() {
  memset(id_, 0, sizeof id_);
  memset(len_, 0, sizeof len_);
  tmpLen_ = 0;
  tmpUsed_ = false;
  orderLen_ = -1;
  failWrites = false;
  failAfterReads = -1;
  commits = 0;
  reads = 0;
}
int MemStorage::list(char (*ids)[kIdLen], int cap) {
  int n = 0;
  for (int i = 0; i < kSlots && n < cap; i++)
    if (id_[i][0]) memcpy(ids[n++], id_[i], kIdLen);
  return n;
}
int32_t MemStorage::size(const char *id) {
  const int k = findSlot(id_, kSlots, id);
  return k < 0 ? -1 : int32_t(len_[k]);
}
bool MemStorage::read(const char *id, uint32_t off, uint8_t *dst, uint32_t n) {
  if (failAfterReads >= 0 && reads >= failAfterReads) return false;
  const int k = findSlot(id_, kSlots, id);
  if (k < 0 || off > len_[k] || n > len_[k] - off) return false; // senza giro di uint32
  memcpy(dst, data_[k] + off, n);
  reads++;
  return true;
}
void MemStorage::remove(const char *id) {
  const int k = findSlot(id_, kSlots, id);
  if (k >= 0) id_[k][0] = 0;
}
bool MemStorage::tmpBegin() { tmpUsed_ = !failWrites; tmpLen_ = 0; return tmpUsed_; }
bool MemStorage::tmpAppend(const uint8_t *p, uint32_t n) {
  if (!tmpUsed_ || failWrites || n > kMaxFile - tmpLen_) return false;
  memcpy(tmp_ + tmpLen_, p, n);
  tmpLen_ += n;
  return true;
}
bool MemStorage::tmpRead(uint32_t off, uint8_t *dst, uint32_t n) {
  if (!tmpUsed_ || off > tmpLen_ || n > tmpLen_ - off) return false;
  memcpy(dst, tmp_ + off, n);
  return true;
}
bool MemStorage::tmpCommit(const char *id) {
  if (!tmpUsed_ || failWrites) return false;
  int k = findSlot(id_, kSlots, id);
  for (int i = 0; k < 0 && i < kSlots; i++)
    if (!id_[i][0]) k = i;
  if (k < 0) return false;
  strncpy(id_[k], id, kIdLen - 1);
  id_[k][kIdLen - 1] = 0;
  memcpy(data_[k], tmp_, tmpLen_);
  len_[k] = tmpLen_;
  tmpDrop();
  commits++;
  return true;
}
int MemStorage::readOrder(char *dst, int cap) {
  if (orderLen_ < 0 || orderLen_ > cap) return -1;
  memcpy(dst, order_, orderLen_);
  return orderLen_;
}
bool MemStorage::writeOrder(const char *src, int n) {
  if (failWrites || n > int(sizeof order_)) return false;
  memcpy(order_, src, n);
  orderLen_ = n;
  return true;
}
uint8_t *MemStorage::raw(const char *id) {
  const int k = findSlot(id_, kSlots, id);
  return k < 0 ? nullptr : data_[k];
}

// ---------- controlli su un file salvato ----------
int catalog(Storage &st, CatalogEntry *out, int cap) {
  char ids[kMaxThemes + 1][kIdLen];
  const int n = st.list(ids, kMaxThemes + 1);
  int k = 0;
  for (int i = 0; i < n && k < cap; i++) {
    uint8_t hb[kHeaderLen];
    Header h;
    if (!st.read(ids[i], 0, hb, kHeaderLen) || parseHeader(hb, kHeaderLen, h) != Error::Ok || strcmp(h.id, ids[i])) continue;
    memcpy(out[k].id, h.id, kIdLen);
    memcpy(out[k].name, h.name, kNameLen);
    out[k].crc = h.crc;
    out[k].size = h.length;
    k++;
  }
  return k;
}

// Lettore comune per i file salvati e per il temporaneo (piano 1b): id nullptr = temporaneo.
static bool rd(Storage &st, const char *id, uint32_t off, uint8_t *dst, uint32_t n) {
  return id ? st.read(id, off, dst, n) : st.tmpRead(off, dst, n);
}

Error verifyAt(Storage &st, const char *id, int32_t size, uint8_t *scratch, Header *hOut, IndexEntry *idx, bool *io) {
  Header h;
  if (io) *io = size == kSizeIo;
  if (size < kHeaderLen) return Error::Short;
  // Da qui la lunghezza è nota: una lettura fallita è un errore di I/O (memoria, flash), non un file corto.
  auto ioFail = [io](Error e) {
    if (io) *io = true;
    return e;
  };
  uint8_t hb[kHeaderLen];
  if (!rd(st, id, 0, hb, kHeaderLen)) return ioFail(Error::Short);
  Error e = parseHeader(hb, kHeaderLen, h);
  if (hOut) *hOut = h;
  if (e != Error::Ok) return e;
  if (h.length != uint32_t(size)) return Error::Length;
  uint32_t crc = 0;
  for (uint32_t off = 12; off < h.length;) {  // CRC a pezzi da kMaxBlock nello scratch: con LittleFS ogni read apre il file
    const uint32_t n = h.length - off < uint32_t(kMaxBlock) ? h.length - off : uint32_t(kMaxBlock);
    if (!rd(st, id, off, scratch, n)) return ioFail(Error::Short);
    crc = crc32Update(crc, scratch, n);
    off += n;
  }
  if (crc != h.crc) return Error::Crc;
  if (!rd(st, id, kHeaderLen, scratch, 8u * h.nScreens)) return ioFail(Error::Index);
  e = parseIndex(scratch, 8u * h.nScreens, h, idx, kMaxScreens);
  if (e != Error::Ok) return e;
  for (int i = 0; i < h.nScreens; i++) {
    if (!rd(st, id, idx[i].off, scratch, idx[i].len)) return ioFail(Error::Block);
    e = checkBlock(scratch, idx[i].len);
    if (e != Error::Ok) return e;
  }
  return Error::Ok;
}

Error verifyStored(Storage &st, const char *id, uint8_t *scratch, Header *h, bool *io) {
  IndexEntry idx[kMaxScreens];
  return verifyAt(st, id, st.size(id), scratch, h, idx, io);
}

// ---------- Runtime ----------
Runtime::Runtime() : st_(nullptr), loaded_(Screen::None), suspended_(false), broken_(false), lent_(false) {
  id_[0] = 0;
  brokenId_[0] = 0;
  memset(&h_, 0, sizeof h_);
}

void Runtime::begin(Storage *st) {
  st_ = st;
  id_[0] = 0;
  loaded_ = Screen::None;
}

void Runtime::broke() {
  if (id_[0]) {
    memcpy(brokenId_, id_, kIdLen);
    broken_ = true;
  }
  id_[0] = 0;
  loaded_ = Screen::None;
}

bool Runtime::activate(const char *id) {
  if (lent_) return false;  // l'arena è della partita: si riattiva dopo giveBack
  loaded_ = Screen::None;
  if (!id || !id[0]) { id_[0] = 0; return true; }
  if (!st_) return false;
  strncpy(id_, id, kIdLen - 1);
  id_[kIdLen - 1] = 0;
  if (verifyAt(*st_, id_, st_->size(id_), arena_, &h_, idx_) != Error::Ok) { broke(); return false; }
  return true;
}

bool Runtime::activateKnown(const char *id, uint32_t crc) {
  if (lent_ || !st_ || !id || !id[0] || strlen(id) >= size_t(kIdLen)) return false;
  loaded_ = Screen::None;
  id_[0] = 0;
  const int32_t size = st_->size(id);
  uint8_t hb[kHeaderLen];
  Header h;
  if (size < kHeaderLen || !st_->read(id, 0, hb, kHeaderLen) || parseHeader(hb, kHeaderLen, h) != Error::Ok) return false;
  if (h.length != uint32_t(size) || h.crc != crc || strcmp(h.id, id)) return false;
  // indice nell'arena (il blocco si carica dopo, in block())
  if (!st_->read(id, kHeaderLen, arena_, 8u * h.nScreens) || parseIndex(arena_, 8u * h.nScreens, h, idx_, kMaxScreens) != Error::Ok)
    return false;
  h_ = h;
  memcpy(id_, id, strlen(id) + 1);
  return true;
}

bool Runtime::has(Screen s) const {
  if (lent_) return false;  // come block(): in prestito niente schermate del tema
  if (!id_[0] || suspended_) return false;
  for (int i = 0; i < h_.nScreens; i++)
    if (idx_[i].screen == s) return true;
  return false;
}

bool Runtime::block(Screen s, const uint8_t *&b, uint16_t &n) {
  if (lent_) return false;
  if (!id_[0] || suspended_ || !st_) return false;
  for (int i = 0; i < h_.nScreens; i++) {
    if (idx_[i].screen != s) continue;
    if (loaded_ != s) {
      loaded_ = Screen::None;
      if (!st_->read(id_, idx_[i].off, arena_, idx_[i].len) || checkBlock(arena_, idx_[i].len) != Error::Ok) { broke(); return false; }
      loaded_ = s;
    }
    b = arena_;
    n = idx_[i].len;
    return true;
  }
  return false;
}

uint8_t *Runtime::lend(size_t n) {
  if (lent_ || n > sizeof arena_) return nullptr;
  lent_ = true;
  loaded_ = Screen::None;
  return arena_;
}

void Runtime::giveBack() {
  lent_ = false;
  loaded_ = Screen::None;
}

bool Runtime::takeBroken(char *id, size_t cap) {
  if (!broken_) return false;
  broken_ = false;
  if (cap) { strncpy(id, brokenId_, cap - 1); id[cap - 1] = 0; }
  return true;
}

BootPlan themeBootPlan(Storage &st, const char *nvsCur, uint8_t *scratch) {
  BootPlan p;
  memset(&p, 0, sizeof p);
  if (!nvsCur || !nvsCur[0]) return p;
  strncpy(p.id, nvsCur, kIdLen - 1);
  Header h;
  if (verifyStored(st, p.id, scratch, &h) == Error::Ok && !strcmp(h.id, p.id)) p.use = true;
  else p.broken = true;
  return p;
}

}  // namespace theme
}  // namespace cal
