#include "accessories.h"

#include <string.h>

#include "avatars.h"
#include "pet.h"
#include "strbuf.h"

namespace cal {

namespace {
AccessoryStore *gStore = nullptr;
}

// ---------- MemAccStorage ----------
MemAccStorage::E *MemAccStorage::find(Kind k, const char *id) {
  for (E &e : e_)
    if (e.used && e.k == k && streq(e.id, id)) return &e;
  return nullptr;
}
int MemAccStorage::read(Kind k, const char *id, uint8_t *dst, int cap) {
  reads++;
  E *e = find(k, id);
  if (!e || e->n > cap) return -1;
  memcpy(dst, e->data, e->n);
  return e->n;
}
bool MemAccStorage::write(Kind k, const char *id, const uint8_t *data, int n) {
  if (failWrites || n > kAccMaxBytes) return false;
  E *e = find(k, id);
  for (int i = 0; i < kCap && !e; i++)
    if (!e_[i].used) e = &e_[i];
  if (!e) return false;
  e->used = true;
  e->k = k;
  scopy(e->id, id);
  memcpy(e->data, data, n);
  e->n = n;
  writes++;
  return true;
}
void MemAccStorage::remove(Kind k, const char *id) {
  if (E *e = find(k, id)) e->used = false;
}
int MemAccStorage::list(Kind k, char (*ids)[kAvatarRefLen], int cap) {
  int n = 0;
  for (E &e : e_)
    if (e.used && e.k == k && n < cap) scopy(ids[n++], e.id);
  return n;
}
void MemAccStorage::eraseAll() { memset(e_, 0, sizeof e_); }
uint8_t *MemAccStorage::raw(Kind k, const char *id, int *len) {
  E *e = find(k, id);
  if (len) *len = e ? e->n : -1;
  return e ? e->data : nullptr;
}

// ---------- AccessoryStore ----------
AccessoryStore::AccessoryStore() : st_(nullptr), curOk_(false), missSet_(false), clock_(0) {
  memset(cur_, 0, sizeof cur_);
  memset(&curView_, 0, sizeof curView_);
  memset(&miss_, 0, sizeof miss_);
  memset(anc_, 0, sizeof anc_);
}

int AccessoryStore::begin(AccStorage *st) {
  st_ = st;
  curOk_ = false;
  missSet_ = false;
  clock_ = 0;
  memset(anc_, 0, sizeof anc_);
  if (!st_) return 0;
  char ids[kAncSlots * 2][kAvatarRefLen];
  const int n = st_->list(AccStorage::Anc, ids, kAncSlots * 2);
  int ok = 0;
  for (int i = 0; i < n; i++) {
    AncSlot *s = nullptr;
    for (AncSlot &q : anc_)
      if (!q.used && !s) s = &q;
    const int len = s ? st_->read(AccStorage::Anc, ids[i], s->data, kAncMaxBytes) : -1;
    if (!s || len < 0 || !validAvatarId(ids[i]) || parseAnchors(s->data, size_t(len), s->view) != AncError::Ok) {
      st_->remove(AccStorage::Anc, ids[i]);  // rovinate o di troppo: si richiederanno
      continue;
    }
    s->used = true;
    scopy(s->id, ids[i]);
    s->len = uint16_t(len);
    s->stamp = ++clock_;
    ok++;
  }
  return ok;
}

bool AccessoryStore::loadAcc(const char *id) {
  curOk_ = false;
  if (!st_) return false;
  const int len = st_->read(AccStorage::Acc, id, cur_, sizeof cur_);
  if (len < 0) return false;
  if (parseAccessory(cur_, size_t(len), curView_) != AccError::Ok || !streq(curView_.id, id)) {
    st_->remove(AccStorage::Acc, id);  // rovinato (flash, scrittura interrotta): si richiederà
    return false;
  }
  curOk_ = true;
  return true;
}

const Accessory *AccessoryStore::find(const AvatarRef &r) {
  if (!validAvatarId(r.id)) return nullptr;
  if (const Accessory *b = builtinAccessoryById(r.id, r.crc, r.hasCrc)) return b;
  auto match = [&]() { return curOk_ && streq(curView_.id, r.id) && (!r.hasCrc || curView_.crc == r.crc); };
  if (match()) return &curView_;
  if (missSet_ && miss_ == r) return nullptr;
  if (loadAcc(r.id) && match()) return &curView_;
  miss_ = r;
  missSet_ = true;
  return nullptr;
}

AccError AccessoryStore::store(const char *id, uint32_t crc, const uint8_t *data, size_t len, const char *keep) {
  Accessory a;
  const AccError e = parseAccessory(data, len, a);
  if (e != AccError::Ok) return e;
  if (!id || !streq(a.id, id)) return AccError::Id;
  if (a.crc != crc) return AccError::Crc;
  missSet_ = false;
  // In RAM subito (anche senza memoria o con la scrittura fallita): è quello che serve adesso.
  memcpy(cur_, data, len);
  parseAccessory(cur_, len, curView_);
  curOk_ = true;
  if (!st_) return AccError::Ok;
  char ids[kAccFiles + 4][kAvatarRefLen];
  const int n = st_->list(AccStorage::Acc, ids, kAccFiles + 4);
  bool there = false;
  for (int i = 0; i < n; i++)
    if (streq(ids[i], id)) there = true;
  if (!there && n >= kAccFiles)  // piena: via il primo che non è quello da tenere
    for (int i = 0; i < n; i++)
      if (!streq(ids[i], keep ? keep : "")) { st_->remove(AccStorage::Acc, ids[i]); break; }
  st_->write(AccStorage::Acc, id, data, int(len));  // fallita: resta in RAM, dopo un riavvio si richiede
  return AccError::Ok;
}

const Anchors *AccessoryStore::anchorsFor(const Avatar &av) {
  if (const Anchors *b = builtinAnchorsFor(av)) return b;
  for (AncSlot &s : anc_)
    if (s.used && streq(s.id, av.id) && s.view.avatarCrc == av.crc) {
      s.stamp = ++clock_;
      return &s.view;
    }
  return nullptr;
}

AncError AccessoryStore::storeAnchors(const char *avatar, uint32_t avatarCrc, const uint8_t *data, size_t len) {
  Anchors v;
  const AncError e = parseAnchors(data, len, v);
  if (e != AncError::Ok) return e;
  if (!validAvatarId(avatar) || v.avatarCrc != avatarCrc) return AncError::Range;
  AncSlot *s = nullptr;
  for (AncSlot &q : anc_)
    if (q.used && streq(q.id, avatar)) s = &q;  // stesso avatar: la versione nuova sostituisce la vecchia
  for (AncSlot &q : anc_)
    if (!s && !q.used) s = &q;
  if (!s) {  // piene: la meno usata
    s = &anc_[0];
    for (AncSlot &q : anc_)
      if (q.stamp < s->stamp) s = &q;
    if (st_) st_->remove(AccStorage::Anc, s->id);
  }
  memcpy(s->data, data, len);
  s->len = uint16_t(len);
  parseAnchors(s->data, len, s->view);
  s->used = true;
  scopy(s->id, avatar);
  s->stamp = ++clock_;
  if (st_) st_->write(AccStorage::Anc, avatar, data, int(len));
  return AncError::Ok;
}

void AccessoryStore::clear() {
  curOk_ = false;
  missSet_ = false;
  memset(anc_, 0, sizeof anc_);
  if (st_) st_->eraseAll();
}

int AccessoryStore::anchorsCount() const {
  int n = 0;
  for (const AncSlot &s : anc_) n += s.used;
  return n;
}

// ---------- quale accessorio disegnare ----------
void setAccessoryStore(AccessoryStore *s) { gStore = s; }
AccessoryStore *accessoryStore() { return gStore; }

const Accessory *wornAccessory(const Model &m) {
  if (!petOn(m) || !m.pet.wear.id[0]) return nullptr;
  if (gStore) return gStore->find(m.pet.wear);
  return builtinAccessoryById(m.pet.wear.id, m.pet.wear.crc, m.pet.wear.hasCrc);
}

const Anchors *anchorsFor(const Avatar &av) { return gStore ? gStore->anchorsFor(av) : builtinAnchorsFor(av); }

bool figureGrid(const Model &m, const Avatar &av, Pose p, int f, uint8_t *grid, bool *drawn) {
  if (drawn) *drawn = false;
  if (!avatarGrid(av, p, f, grid)) return false;
  const Accessory *acc = wornAccessory(m);
  if (acc) {
    const bool d = drawAccessory(grid, av, anchorsFor(av), *acc, p, f);
    if (drawn) *drawn = d;
  }
  return true;
}

// ---------- richieste ----------
void AccNeeds::reset() {
  memset(&acc_, 0, sizeof acc_);
  memset(&anc_, 0, sizeof anc_);
}

void AccNeeds::rejected(bool anchors, const char *id, uint32_t crc) {
  List &l = anchors ? anc_ : acc_;
  AvatarRef r;
  setAvatarRef(r, id);
  r.crc = crc;
  r.hasCrc = true;
  for (int i = 0; i < l.nRej; i++)
    if (l.rej[i] == r) return;
  if (l.nRej < 4) l.rej[l.nRej++] = r;
  else { memmove(l.rej, l.rej + 1, sizeof l.rej[0] * 3); l.rej[3] = r; }
}

int AccNeeds::due(List &l, const AvatarRef *want, int nw, Ms now, char (*ids)[kAvatarRefLen], uint32_t *crcs, int cap) {
  // Si tengono le richieste già fatte che servono ancora (con il loro orologio), le altre si buttano.
  E next[kCap];
  int nn = 0;
  for (int i = 0; i < nw && nn < kCap; i++) {
    bool rej = false;
    for (int k = 0; k < l.nRej; k++)
      if (streq(l.rej[k].id, want[i].id) && (!want[i].hasCrc || l.rej[k].crc == want[i].crc)) rej = true;
    if (rej) continue;
    E e = {want[i], 0, false};
    for (int j = 0; j < l.n; j++)
      if (l.e[j].ref == want[i]) e = l.e[j];
    next[nn++] = e;
  }
  memcpy(l.e, next, sizeof next[0] * nn);
  l.n = nn;
  int no = 0;
  for (int i = 0; i < l.n; i++) {
    E &e = l.e[i];
    if (e.asked && now - e.askedAt < kRetryMs) continue;
    if (no >= cap) break;
    scopy(ids[no], kAvatarRefLen, e.ref.id);
    if (crcs) crcs[no] = e.ref.crc;
    no++;
    e.asked = true;
    e.askedAt = now;
  }
  return no;
}

int AccNeeds::dueAccessories(const Model &m, Ms now, char (*out)[kAvatarRefLen], int cap) {
  AvatarRef want[1];
  int nw = 0;
  if (petOn(m) && validAvatarId(m.pet.wear.id) && !wornAccessory(m)) want[nw++] = m.pet.wear;
  return due(acc_, want, nw, now, out, nullptr, cap);
}

int AccNeeds::dueAnchors(const Model &m, Ms now, char (*ids)[kAvatarRefLen], uint32_t *crcs, int cap) {
  AvatarRef want[kCap];
  int nw = 0;
  if (petOn(m) && m.pet.wear.id[0]) {
    auto consider = [&](const Avatar &av) {
      if (!av.id[0] || anchorsFor(av)) return;
      AvatarRef r;
      setAvatarRef(r, av.id);
      r.crc = av.crc;
      r.hasCrc = true;
      for (int i = 0; i < nw; i++)
        if (want[i] == r) return;
      if (nw < kCap) want[nw++] = r;
    };
    for (int k = 0; k < m.nSessions; k++) consider(sessionAvatar(m, m.sessions[k]));
    consider(defaultAvatar(m));
  }
  return due(anc_, want, nw, now, ids, crcs, cap);
}

}  // namespace cal
