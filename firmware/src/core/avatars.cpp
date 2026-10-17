#include "avatars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apply_state.h"
#include "gfx/avatar_builtin.h"
#include "strbuf.h"
#include "theme_proto.h"

namespace cal {

namespace {

AvatarCache *gCache = nullptr;

void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Indice: "CAVI", versione 1, orologio d'uso, poi per ogni posto crc e ultimo uso, infine CRC-32 del resto.
const int kIndexLen = 4 + 1 + 4 + AvatarCache::kSlots * 8 + 4;

}  // namespace

// ---------- MemAvatarStorage ----------
MemAvatarStorage::MemAvatarStorage() { eraseAll(); }
int MemAvatarStorage::slotSize(int slot) { return slot >= 0 && slot < kSlots ? size_[slot] : -1; }
bool MemAvatarStorage::readSlot(int slot, uint32_t off, uint8_t *dst, uint16_t n) {
  if (slot < 0 || slot >= kSlots || size_[slot] < 0 || off + n > uint32_t(size_[slot])) return false;
  memcpy(dst, data_[slot] + off, n);
  return true;
}
bool MemAvatarStorage::writeSlot(int slot, const uint8_t *data, uint16_t n) {
  if (failWrites || slot < 0 || slot >= kSlots || n > kCavtMaxBytes) return false;
  memcpy(data_[slot], data, n);
  size_[slot] = n;
  writes++;
  return true;
}
void MemAvatarStorage::eraseSlot(int slot) {
  if (slot >= 0 && slot < kSlots) size_[slot] = -1;
}
int MemAvatarStorage::readIndex(uint8_t *dst, int cap) {
  if (indexLen_ < 0) return -1;
  const int n = indexLen_ < cap ? indexLen_ : cap;
  memcpy(dst, index_, n);
  return n;
}
bool MemAvatarStorage::writeIndex(const uint8_t *data, int n) {
  if (failWrites || n > int(sizeof index_)) return false;
  memcpy(index_, data, n);
  indexLen_ = n;
  writes++;
  return true;
}
void MemAvatarStorage::eraseAll() {
  for (int i = 0; i < kSlots; i++) size_[i] = -1;
  indexLen_ = -1;
  tempAbort();
}

bool MemAvatarStorage::tempBegin() {
  if (failWrites) return false;
  tempLen_ = 0;
  tempOpen_ = true;
  return true;
}
bool MemAvatarStorage::tempAppend(const uint8_t *p, uint16_t n) {
  if (failWrites || !tempOpen_ || tempLen_ + n > kCavtMaxBytes) return false;
  memcpy(temp_ + tempLen_, p, n);
  tempLen_ += n;
  return true;
}
bool MemAvatarStorage::tempEnd() {
  if (!tempOpen_) return false;
  tempOpen_ = false;
  return true;
}
void MemAvatarStorage::tempAbort() {
  tempOpen_ = false;
  tempLen_ = -1;
}
bool MemAvatarStorage::adoptTemp(int slot, uint32_t len) {
  if (failWrites || tempOpen_ || tempLen_ < 0 || uint32_t(tempLen_) != len || slot < 0 || slot >= kSlots) return false;
  memcpy(data_[slot], temp_, tempLen_);
  size_[slot] = tempLen_;
  tempLen_ = -1;
  writes++;
  return true;
}

// ---------- AvatarCache ----------
AvatarCache::AvatarCache() : st_(nullptr), clock_(0) {
  memset(used_, 0, sizeof used_);
  memset(stamp_, 0, sizeof stamp_);
  memset(av_, 0, sizeof av_);
  memset(ctx_, 0, sizeof ctx_);
}

bool AvatarCache::readCb(const void *ctx, uint32_t off, uint8_t *dst, uint16_t n) {
  const Ctx *c = static_cast<const Ctx *>(ctx);
  return c->st && c->st->readSlot(c->slot, off, dst, n);
}

int AvatarCache::begin(AvatarStorage *st) {
  st_ = st;
  clock_ = 0;
  memset(used_, 0, sizeof used_);
  memset(stamp_, 0, sizeof stamp_);
  if (!st_) return 0;
  uint8_t idx[kIndexLen];
  const bool haveIdx = st_->readIndex(idx, sizeof idx) == kIndexLen && memcmp(idx, "CAVI", 4) == 0 && idx[4] == 1 &&
                       get32(idx + kIndexLen - 4) == avatarCrc32(idx, kIndexLen - 4);
  if (haveIdx) clock_ = get32(idx + 5);
  // Un CAVT alla volta da rivalidare: sullo heap e solo qui (6 372 byte in meno di RAM statica).
  uint8_t *tmp = static_cast<uint8_t *>(malloc(kCavtMaxBytes));
  if (!tmp) return 0;
  int n = 0;
  for (int i = 0; i < kSlots; i++) {
    const int sz = st_->slotSize(i);
    if (sz < 0) continue;
    Avatar a;
    if (sz > kCavtMaxBytes || !st_->readSlot(i, 0, tmp, uint16_t(sz)) || parseAvatar(tmp, sz, a) != AvatarError::Ok) {
      st_->eraseSlot(i);  // rovinato (scrittura interrotta, flash): si richiederà
      continue;
    }
    a.data = nullptr;  // da qui in poi si legge a pezzi dalla memoria
    ctx_[i] = Ctx{st_, i};
    a.read = readCb;
    a.ctx = &ctx_[i];
    av_[i] = a;
    used_[i] = true;
    stamp_[i] = haveIdx && get32(idx + 9 + i * 8) == a.crc ? get32(idx + 13 + i * 8) : 0;
    n++;
  }
  free(tmp);
  return n;
}

void AvatarCache::saveIndex() {
  if (!st_) return;
  uint8_t idx[kIndexLen];
  memcpy(idx, "CAVI", 4);
  idx[4] = 1;
  put32(idx + 5, clock_);
  for (int i = 0; i < kSlots; i++) {
    put32(idx + 9 + i * 8, used_[i] ? av_[i].crc : 0);
    put32(idx + 13 + i * 8, used_[i] ? stamp_[i] : 0);
  }
  put32(idx + kIndexLen - 4, avatarCrc32(idx, kIndexLen - 4));
  st_->writeIndex(idx, kIndexLen);
}

int AvatarCache::lookup(const char *id, uint32_t crc, bool hasCrc) const {
  int best = -1;
  for (int i = 0; i < kSlots; i++) {
    if (!used_[i] || strcmp(av_[i].id, id) != 0) continue;
    if (hasCrc && av_[i].crc != crc) continue;
    if (best < 0 || stamp_[i] > stamp_[best]) best = i;
  }
  return best;
}

int AvatarCache::pickSlot(const char *id) const {
  int slot = lookup(id, 0, false);  // versione vecchia dello stesso avatar
  for (int i = 0; i < kSlots && slot < 0; i++)
    if (!used_[i]) slot = i;
  if (slot < 0) {  // pieno: il meno usato
    slot = 0;
    for (int i = 1; i < kSlots; i++)
      if (stamp_[i] < stamp_[slot]) slot = i;
  }
  return slot;
}

AvatarError AvatarCache::adopt(const Avatar &hdr, int *slotOut) {
  if (slotOut) *slotOut = -1;
  if (!st_) return AvatarError::Short;  // niente memoria: non salvato
  if (!validAvatarId(hdr.id) || !hdr.frames || hdr.len < kCavtMinBytes) {  // non uscita da una finish riuscita
    st_->tempAbort();
    return AvatarError::Short;
  }
  int slot = lookup(hdr.id, hdr.crc, true);
  if (slot >= 0) {  // già presente (arrivato nel frattempo con `avatar`)
    st_->tempAbort();
    stamp_[slot] = ++clock_;
    if (slotOut) *slotOut = slot;
    return AvatarError::Ok;
  }
  slot = pickSlot(hdr.id);
  if (!st_->adoptTemp(slot, hdr.len)) {  // rinomina fallita o temporaneo non di hdr: il posto resta com'era
    st_->tempAbort();
    return AvatarError::Short;  // per chi chiama è «non salvato»
  }
  Avatar a = hdr;
  a.data = nullptr;
  ctx_[slot] = Ctx{st_, slot};
  a.read = readCb;
  a.ctx = &ctx_[slot];
  av_[slot] = a;
  used_[slot] = true;
  stamp_[slot] = ++clock_;
  saveIndex();
  if (slotOut) *slotOut = slot;
  return AvatarError::Ok;
}

AvatarError AvatarCache::store(const char *id, uint32_t crc, const uint8_t *data, size_t len, int *slotOut) {
  if (slotOut) *slotOut = -1;
  Avatar a;
  const AvatarError e = parseAvatar(data, len, a);
  if (e != AvatarError::Ok) return e;
  if (!id || strcmp(a.id, id) != 0) return AvatarError::Id;
  if (a.crc != crc) return AvatarError::Crc;
  if (!st_) return AvatarError::Ok;  // niente memoria: valido ma non salvato
  int slot = lookup(id, crc, true);
  if (slot >= 0) {  // già presente
    stamp_[slot] = ++clock_;
    if (slotOut) *slotOut = slot;
    return AvatarError::Ok;
  }
  slot = pickSlot(id);
  if (!st_->writeSlot(slot, data, uint16_t(len))) {  // tutto o niente: il posto resta com'era
    return AvatarError::Short;  // scrittura fallita: per chi chiama è «non salvato»
  }
  a.data = nullptr;
  ctx_[slot] = Ctx{st_, slot};
  a.read = readCb;
  a.ctx = &ctx_[slot];
  av_[slot] = a;
  used_[slot] = true;
  stamp_[slot] = ++clock_;
  saveIndex();
  if (slotOut) *slotOut = slot;
  return AvatarError::Ok;
}

const Avatar *AvatarCache::find(const char *id, uint32_t crc, bool hasCrc) {
  const int i = lookup(id, crc, hasCrc);
  if (i < 0) return nullptr;
  // Solo in RAM: l'indice si riscrive quando cambia il contenuto, non a ogni disegno (usura della flash).
  stamp_[i] = ++clock_;
  return &av_[i];
}

bool AvatarCache::has(const char *id, uint32_t crc, bool hasCrc) const { return lookup(id, crc, hasCrc) >= 0; }

int AvatarCache::count() const {
  int n = 0;
  for (bool u : used_) n += u;
  return n;
}

void AvatarCache::clear() {
  memset(used_, 0, sizeof used_);
  memset(stamp_, 0, sizeof stamp_);
  clock_ = 0;
  if (st_) st_->eraseAll();
}

// ---------- quale avatar disegnare ----------
void setAvatarCache(AvatarCache *c) { gCache = c; }
AvatarCache *avatarCache() { return gCache; }

bool validAvatarId(const char *id) {
  if (!id) return false;
  const size_t n = strlen(id);
  if (n == 0 || n > 24) return false;
  for (size_t k = 0; k < n; k++) {
    const char ch = id[k];
    if (ch == '-') {
      if (k == 0 || k == n - 1 || id[k - 1] == '-') return false;
    } else if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
      return false;
    }
  }
  return true;
}

const Avatar *lookupAvatar(const AvatarRef &r) {
  if (!validAvatarId(r.id)) return nullptr;
  for (int i = 0; i < kBuiltinAvatarCount; i++) {
    const BuiltinAvatar &b = kBuiltinAvatars[i];
    if (strcmp(b.id, r.id) == 0 && (!r.hasCrc || r.crc == b.crc)) return builtinAvatar(i);
  }
  return gCache ? gCache->find(r.id, r.crc, r.hasCrc) : nullptr;
}

const Avatar &defaultAvatar(const Model &m) {
  const Avatar *a = lookupAvatar(m.set.defAvatar);
  return a ? *a : gocciaAvatar();
}

const Avatar &sessionAvatar(const Model &m, const Session &s) {
  if (!s.avatar.id[0]) return defaultAvatar(m);
  const Avatar *a = lookupAvatar(s.avatar);
  return a ? *a : gocciaAvatar();
}

bool avatarWanted(const Model &m, const char *id) {
  if (!id || !id[0]) return false;
  if (strcmp(m.set.defAvatar.id, id) == 0) return true;
  for (int k = 0; k < m.nSessions; k++)
    if (strcmp(m.sessions[k].avatar.id, id) == 0) return true;
  return false;
}

// ---------- richieste ----------
void AvatarNeeds::reset() {
  memset(e_, 0, sizeof e_);
  n_ = 0;
  memset(rej_, 0, sizeof rej_);
  nRej_ = 0;
}

void AvatarNeeds::rejected(const char *id, uint32_t crc) {
  AvatarRef r;
  setAvatarRef(r, id);
  r.crc = crc;
  r.hasCrc = true;
  for (int i = 0; i < nRej_; i++)
    if (rej_[i] == r) return;
  if (nRej_ < 4) rej_[nRej_++] = r;
  else { memmove(rej_, rej_ + 1, sizeof rej_[0] * 3); rej_[3] = r; }
}

int AvatarNeeds::due(const Model &m, Ms now, char (*out)[kAvatarRefLen], int cap) {
  // Che cosa manca adesso: avatar delle sessioni (gli archiviati non si mostrano) e predefinito.
  AvatarRef want[kCap];
  int nw = 0;
  auto consider = [&](const AvatarRef &r) {
    if (!validAvatarId(r.id) || lookupAvatar(r)) return;
    for (int i = 0; i < nRej_; i++)
      if (strcmp(rej_[i].id, r.id) == 0 && (!r.hasCrc || rej_[i].crc == r.crc)) return;
    for (int i = 0; i < nw; i++)
      if (want[i] == r) return;
    if (nw < kCap) want[nw++] = r;
  };
  for (int k = 0; k < m.nSessions; k++) consider(m.sessions[k].avatar);
  consider(m.set.defAvatar);
  // Si tengono le richieste già fatte che servono ancora (con il loro orologio), le altre si buttano.
  E next[kCap];
  int nn = 0;
  for (int i = 0; i < nw; i++) {
    E e = {want[i], 0, false};
    for (int j = 0; j < n_; j++)
      if (e_[j].ref == want[i]) e = e_[j];
    next[nn++] = e;
  }
  memcpy(e_, next, sizeof next[0] * nn);
  n_ = nn;
  // Da chiedere: mai chiesti o chiesti da almeno 30 s. Id distinti (crc diversi dello stesso id: uno solo).
  int no = 0;
  for (int i = 0; i < n_; i++) {
    E &e = e_[i];
    if (e.asked && now - e.askedAt < kRetryMs) continue;
    bool dup = false;
    for (int k = 0; k < no; k++)
      if (strcmp(out[k], e.ref.id) == 0) dup = true;
    if (!dup) {
      if (no >= cap) break;
      scopy(out[no], kAvatarRefLen, e.ref.id);
      no++;
    }
    e.asked = true;
    e.askedAt = now;
  }
  return no;
}

// ---------- messaggi ----------
size_t base64Decode(const char *s, size_t n, uint8_t *out, size_t cap) {
  if (!s || n % 4) return 0;
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  size_t o = 0;
  for (size_t i = 0; i < n; i += 4) {
    const bool last = i + 4 == n;
    const int pad = last ? (s[i + 3] == '=') + (s[i + 2] == '=' && s[i + 3] == '=') : 0;
    int v[4];
    for (int k = 0; k < 4 - pad; k++)
      if ((v[k] = val(s[i + k])) < 0) return 0;
    for (int k = 4 - pad; k < 4; k++) v[k] = 0;
    if (!last && (s[i + 2] == '=' || s[i + 3] == '=')) return 0;
    const uint32_t w = uint32_t(v[0]) << 18 | uint32_t(v[1]) << 12 | uint32_t(v[2]) << 6 | uint32_t(v[3]);
    const int nb = 3 - pad;
    if (o + nb > cap) return 0;
    if ((pad == 1 && (w & 0xFF)) || (pad == 2 && (w & 0xFFFF))) return 0;  // bit di troppo: non canonico
    out[o++] = uint8_t(w >> 16);
    if (nb > 1) out[o++] = uint8_t(w >> 8);
    if (nb > 2) out[o++] = uint8_t(w);
  }
  return o;
}

bool decodeAvatarRef(JsonVariantConst id, JsonVariantConst crc, AvatarRef &out) {
  memset(&out, 0, sizeof out);
  const char *s = id.as<const char *>();
  if (!id.is<const char *>() || !s) return false;
  if (validAvatarId(s)) scopy(out.id, s);
  else if (s[0]) scopy(out.id, "-");  // non valido: si disegna la Goccia, non si chiede
  if (crc.is<uint32_t>()) {
    out.crc = crc.as<uint32_t>();
    out.hasCrc = true;
  }
  return true;
}

AvatarMsgError decodeAvatarMsg(JsonVariantConst root, AvatarMsg &out) {
  out.id[0] = 0;
  out.crc = 0;
  out.len = 0;
  if (msgType(root) != MsgType::Avatar) return AvatarMsgError::Type;
  const char *id = root["id"].as<const char *>();
  if (!root["id"].is<const char *>() || !validAvatarId(id)) return AvatarMsgError::Id;
  scopy(out.id, id);
  if (!root["crc"].is<uint32_t>()) return AvatarMsgError::Crc;
  out.crc = root["crc"].as<uint32_t>();
  const char *d = root["data"].as<const char *>();
  if (!root["data"].is<const char *>() || !d) return AvatarMsgError::Data;
  const size_t n = base64Decode(d, strlen(d), out.data, sizeof out.data);
  if (!n) return AvatarMsgError::Data;
  out.len = uint16_t(n);
  return AvatarMsgError::Ok;
}

size_t encodeAvatarNeed(const char (*ids)[kAvatarRefLen], int n, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "avatar-need";
  d["v"] = kProtoVersion;
  JsonArray a = d["ids"].to<JsonArray>();
  for (int i = 0; i < n; i++) a.add(ids[i]);
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

// ---------- 0.14.0 (R4): avatar a pezzi ----------
bool decodeAvatarOffer(JsonVariantConst root, AvatarOffer &out) {
  if (msgType(root) != MsgType::AvatarOffer) return false;
  const char *id = root["id"].as<const char *>(), *tok = root["token"].as<const char *>();
  if (!validAvatarId(id) || !theme::lowerHex(tok, 32) || !root["crc"].is<uint32_t>() || !root["size"].is<uint32_t>())
    return false;
  const uint32_t size = root["size"].as<uint32_t>();
  if (size < kCavtMinBytes || size > uint32_t(kCavtMaxBytes)) return false;
  AvatarOffer o;
  memset(&o, 0, sizeof o);
  scopy(o.id, id);
  o.crc = root["crc"].as<uint32_t>();
  o.size = size;
  scopy(o.token, tok);
  out = o;
  return true;
}

size_t encodeAvatarAck(const char *id, uint32_t crc, bool ok, const char *error, char *out, size_t cap) {
  if (!validAvatarId(id)) return 0;  // il servizio lo scarterebbe
  JsonDocument d;
  d["t"] = "avatar-ack";
  d["v"] = 1;
  d["id"] = id;
  d["crc"] = crc;
  d["ok"] = ok;
  if (!ok && error && error[0]) {
    char e[theme::kAckErrorMax + 1];
    theme::cutUtf8(e, error, theme::kAckErrorMax);
    d["error"] = e;  // copiato nel documento (char* non costante)
  }
  const size_t len = measureJson(d);
  if (len + 1 > cap) return 0;
  return serializeJson(d, out, cap);
}

bool avatarUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap) {
  return theme::blobUrl(host, port, "avatars", token, out, cap);
}

bool AvatarDownload::begin(AvatarStorage &st, const AvatarOffer &o, char *why, size_t cap) {
  st_ = &st;
  bad_ = false;
  scopy(id_, o.id);
  crc_ = o.crc;
  size_ = o.size;
  got_ = 0;
  cs_.begin();
  if (st.tempBegin()) return true;
  st_ = nullptr;
  snprintf(why, cap, "spazio per gli avatar non disponibile");
  return false;
}

bool AvatarDownload::write(const uint8_t *p, size_t n, char *why, size_t cap) {
  if (!st_) return false;
  if (n > size_ - got_) { snprintf(why, cap, "più byte di quelli offerti"); abort(); return false; }
  for (size_t i = 0; i < n;) {  // la memoria vuole pezzi da al più 65 535 byte
    const uint16_t k = n - i > 0xFFFF ? 0xFFFF : uint16_t(n - i);
    if (!st_->tempAppend(p + i, k)) { snprintf(why, cap, "scrittura non riuscita"); abort(); return false; }
    i += k;
  }
  cs_.feed(p, n);
  got_ += uint32_t(n);
  return true;
}

// Ogni errore chiude il download da sé (temporaneo buttato): dopo un false non resta niente da adottare.
bool AvatarDownload::finish(Avatar &hdr, char *why, size_t cap) {
  if (!st_) return false;
  if (got_ != size_) { snprintf(why, cap, "download incompleto"); abort(); return false; }
  if (!st_->tempEnd()) { snprintf(why, cap, "scrittura non riuscita"); abort(); return false; }
  const AvatarError e = cs_.finish(hdr);
  if (e != AvatarError::Ok) { snprintf(why, cap, "%s", avatarErrorText(e)); bad_ = true; abort(); return false; }
  if (strcmp(hdr.id, id_) != 0) { snprintf(why, cap, "l'avatar non è quello offerto"); bad_ = true; abort(); return false; }
  if (hdr.crc != crc_) { snprintf(why, cap, "CRC diverso da quello offerto"); bad_ = true; abort(); return false; }
  return true;
}

void AvatarDownload::abort() {
  if (st_) st_->tempAbort();
  st_ = nullptr;
}

}  // namespace cal
