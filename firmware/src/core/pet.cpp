#include "pet.h"

#include <math.h>
#include <string.h>

#include "apply_state.h"
#include "avatars.h"
#include "strbuf.h"

namespace cal {

bool petOn(const Model &m) { return m.set.game && m.pet.on; }

const char *const kPetSpecialToast = "Nuovo accessorio speciale: chiedilo a Claude";

void setGameSwitch(Model &m, bool on) {
  m.set.game = on;
  if (on) return;
  m.pet.party = 0;
  while (m.ui.depth > 1 && (top(m).id == ScreenId::Pet || (top(m).id == ScreenId::List && top(m).menu == MenuId::PetAcc)))
    popScreen(m);
}

bool petParty(const Model &m, Ms now) { return petOn(m) && m.pet.party > now; }

bool partyBlocked(const Model &m, Ms now) {
  for (int k = 0; k < m.nSessions; k++)
    if (m.sessions[k].state == SessionState::Permission) return true;
  return m.nPerms > 0 || top(m).id == ScreenId::Perm || quiet(m, now) || m.set.night || powerSave(m);
}

int levelCost(int n) { return jsRound(100.0 * pow(double(n), 1.3)); }

namespace {

// Testo dal JSON: caratteri di controllo → spazi, poi taglio su un confine UTF-8 (come text() di apply_state).
void textOf(char *dst, size_t cap, JsonVariantConst v) {
  const char *s = v.is<const char *>() ? v.as<const char *>() : nullptr;
  if (!s) { if (cap) dst[0] = 0; return; }
  char tmp[128];
  size_t n = 0;
  for (; s[n] && n + 1 < sizeof tmp; n++) tmp[n] = static_cast<unsigned char>(s[n]) < 0x20 ? ' ' : s[n];
  tmp[n] = 0;
  scopy(dst, cap, tmp);
}

int32_t intOf(JsonVariantConst v, int32_t lo, int32_t hi) {
  int64_t x = 0;
  if (v.is<int64_t>()) x = v.as<int64_t>();
  else if (v.is<double>()) x = int64_t(llround(v.as<double>()));
  return x < lo ? lo : x > hi ? hi : int32_t(x);
}

bool readRef(JsonVariantConst o, const char *idKey, const char *crcKey, AvatarRef &out) {
  memset(&out, 0, sizeof out);
  const char *id = o[idKey].is<const char *>() ? o[idKey].as<const char *>() : nullptr;
  if (!id || !validAvatarId(id)) return false;
  scopy(out.id, id);
  if (o[crcKey].is<uint32_t>()) {
    out.crc = o[crcKey].as<uint32_t>();
    out.hasCrc = true;
  }
  return true;
}

void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { for (int k = 0; k < 4; k++) p[k] = uint8_t(v >> (8 * k)); }
uint16_t get16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Campo di 24 byte completato con zeri: testo (0..24 byte), poi solo zeri.
void putField(uint8_t *p, const char *s) {
  memset(p, 0, 24);
  const size_t n = strlen(s);
  memcpy(p, s, n < 24 ? n : 24);
}
bool getField(const uint8_t *p, char *out, size_t cap) {
  size_t n = 0;
  while (n < 24 && p[n]) n++;
  for (size_t k = n; k < 24; k++)
    if (p[k]) return false;
  if (n + 1 > cap) return false;
  memcpy(out, p, n);
  out[n] = 0;
  return true;
}

}  // namespace

bool decodeGame(JsonVariantConst root, GameMsg &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::Game) return false;
  out.on = root["on"].is<bool>() && root["on"].as<bool>();
  out.level = intOf(root["level"], 0, 9999);
  out.xp = intOf(root["xp"], 0, 2000000000);
  out.xpLevelStart = intOf(root["xpLevelStart"], 0, 2000000000);
  out.xpNext = intOf(root["xpNext"], 0, 2000000000);
  out.energy = intOf(root["energy"], 0, 100);
  out.tired = root["tired"].is<bool>() && root["tired"].as<bool>();
  out.special = root["special"].is<bool>() && root["special"].as<bool>();
  JsonVariantConst w = root["wear"];
  if (w.is<JsonObjectConst>()) out.hasWear = readRef(w, "id", "crc", out.wear);
  JsonVariantConst c = root["celebrate"];
  if (c.is<JsonObjectConst>()) {
    const char *id = c["id"].is<const char *>() ? c["id"].as<const char *>() : nullptr;
    if (id && id[0] && strlen(id) < sizeof out.celebrateId) {
      scopy(out.celebrateId, id);
      textOf(out.celebrateTitle, sizeof out.celebrateTitle, c["title"]);
      if (!out.celebrateTitle[0]) scopy(out.celebrateTitle, id);
      out.hasCelebrate = true;
    }
  }
  for (JsonVariantConst u : root["unlocked"].as<JsonArrayConst>()) {
    if (out.nUnlocked >= kPetUnlockedCap) break;
    PetUnlocked &e = out.unlocked[out.nUnlocked];
    const char *id = u["id"].is<const char *>() ? u["id"].as<const char *>() : nullptr;
    if (!id || !validAvatarId(id)) continue;
    bool dup = false;
    for (int k = 0; k < out.nUnlocked; k++)
      if (streq(out.unlocked[k].id, id)) dup = true;
    if (dup) continue;
    scopy(e.id, id);
    textOf(e.name, sizeof e.name, u["name"]);
    if (!e.name[0]) scopy(e.name, id);
    out.nUnlocked++;
  }
  return true;
}

void petMsg(const Pet &p, GameMsg &out) {
  memset(&out, 0, sizeof out);
  out.on = p.on;
  out.level = p.level;
  out.xp = p.xp;
  out.xpLevelStart = p.xpLevelStart;
  out.xpNext = p.xpNext;
  out.energy = p.energy;
  out.tired = p.tired;
  out.special = p.special;
  out.hasWear = p.wear.id[0] != 0;
  out.wear = p.wear;
  out.nUnlocked = p.nUnlocked;
  memcpy(out.unlocked, p.unlocked, sizeof out.unlocked);
}

Celebrate petCelebrate(Model &m, const char *id, const char *title, Ms now) {
  Pet &p = m.pet;
  if (!petOn(m) || !id || !id[0] || streq(id, p.lastGoal)) return Celebrate::None;
  scopy(p.lastGoal, id);
  for (int k = kPetGoals - 1; k > 0; k--) memcpy(p.goals[k], p.goals[k - 1], kPetTitleLen);
  scopy(p.goals[0], title ? title : id);
  if (p.nGoals < kPetGoals) p.nGoals++;
  char msg[kToastLen], ev[kLogTextLen];
  toast(m, Sb(msg, sizeof msg).s("Traguardo: ").s(p.goals[0]).str(), now, kPetPartyMs);
  logEv(m, nullptr, Sb(ev, sizeof ev).s("traguardo: ").s(p.goals[0]).str(), now);
  if (partyBlocked(m, now)) return Celebrate::Toast;
  p.party = now + kPetPartyMs;
  if (m.burstUntil < p.party) m.burstUntil = p.party;
  return Celebrate::Party;
}

Celebrate applyGame(Model &m, const GameMsg &g, Ms now) {
  Pet &p = m.pet;
  p.known = true;
  p.on = g.on;
  p.level = g.level;
  p.xp = g.xp;
  p.xpLevelStart = g.xpLevelStart;
  p.xpNext = g.xpNext;
  p.energy = g.energy;
  p.tired = g.tired;
  if (g.hasWear) p.wear = g.wear;
  else memset(&p.wear, 0, sizeof p.wear);
  p.nUnlocked = g.nUnlocked < kPetUnlockedCap ? g.nUnlocked : kPetUnlockedCap;
  memcpy(p.unlocked, g.unlocked, sizeof p.unlocked);
  if (m.set.game != p.on) setGameSwitch(m, p.on);
  p.special = g.special;
  if (p.special && petOn(m) && !p.specialSeen) {
    p.specialSeen = true;
    p.specialToast = true;
  }
  const Celebrate c = g.hasCelebrate ? petCelebrate(m, g.celebrateId, g.celebrateTitle, now) : Celebrate::None;
  if (p.specialToast && c == Celebrate::None) {
    p.specialToast = false;
    toast(m, kPetSpecialToast, now, 5000);
  }
  return c;
}

BlobMsgError decodeAccessoryMsg(JsonVariantConst root, AccessoryMsg &out) {
  out.id[0] = 0;
  out.crc = 0;
  out.len = 0;
  if (msgType(root) != MsgType::Accessory) return BlobMsgError::Type;
  const char *id = root["id"].as<const char *>();
  if (!root["id"].is<const char *>() || !validAvatarId(id)) return BlobMsgError::Id;
  scopy(out.id, id);
  if (!root["crc"].is<uint32_t>()) return BlobMsgError::Crc;
  out.crc = root["crc"].as<uint32_t>();
  const char *d = root["data"].as<const char *>();
  if (!root["data"].is<const char *>() || !d) return BlobMsgError::Data;
  const size_t n = base64Decode(d, strlen(d), out.data, sizeof out.data);
  if (!n) return BlobMsgError::Data;
  out.len = uint16_t(n);
  return BlobMsgError::Ok;
}

BlobMsgError decodeAnchorsMsg(JsonVariantConst root, AnchorsMsg &out) {
  out.avatar[0] = 0;
  out.avatarCrc = 0;
  out.len = 0;
  if (msgType(root) != MsgType::Anchors) return BlobMsgError::Type;
  const char *id = root["avatar"].as<const char *>();
  if (!root["avatar"].is<const char *>() || !validAvatarId(id)) return BlobMsgError::Id;
  scopy(out.avatar, id);
  if (!root["avatarCrc"].is<uint32_t>()) return BlobMsgError::Crc;
  out.avatarCrc = root["avatarCrc"].as<uint32_t>();
  const char *d = root["data"].as<const char *>();
  if (!root["data"].is<const char *>() || !d) return BlobMsgError::Data;
  const size_t n = base64Decode(d, strlen(d), out.data, sizeof out.data);
  if (!n) return BlobMsgError::Data;
  out.len = uint16_t(n);
  return BlobMsgError::Ok;
}

size_t encodeAccessoryNeed(const char (*ids)[kAvatarRefLen], int n, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "accessory-need";
  d["v"] = kProtoVersion;
  JsonArray a = d["ids"].to<JsonArray>();
  for (int i = 0; i < n; i++) a.add(ids[i]);
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

size_t encodeAnchorsNeed(const char (*ids)[kAvatarRefLen], const uint32_t *crcs, int n, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "anchors-need";
  d["v"] = kProtoVersion;
  JsonArray a = d["avatars"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = ids[i];
    o["crc"] = crcs[i];
  }
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

size_t encodeAccessoryWear(const char *id, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "accessory-wear";
  d["v"] = kProtoVersion;
  if (id && id[0]) d["id"] = id;
  else d["id"] = nullptr;
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

// Blocco: [0] versione 1, [1] flag (on, tired, indossato, crc dell'indossato), [2..3] livello, [4..15] xp,
// xpLevelStart, xpNext, [16] energia, [17..40] id indossato, [41..44] crc, [45] sbloccati n, n × (id 24, name 24),
// CRC-32 dei byte precedenti.
size_t packPet(const Pet &p, uint8_t *out, size_t cap) {
  const int n = p.nUnlocked < kPetUnlockedCap ? p.nUnlocked : kPetUnlockedCap;
  const size_t len = 46 + size_t(n) * 48 + 4;
  if (cap < len) return 0;
  memset(out, 0, len);
  out[0] = 1;
  out[1] = (p.on ? 1 : 0) | (p.tired ? 2 : 0) | (p.wear.id[0] ? 4 : 0) | (p.wear.hasCrc ? 8 : 0) | (p.special ? 16 : 0);
  put16(out + 2, uint16_t(p.level < 0 ? 0 : p.level > 0xFFFF ? 0xFFFF : p.level));
  put32(out + 4, uint32_t(p.xp));
  put32(out + 8, uint32_t(p.xpLevelStart));
  put32(out + 12, uint32_t(p.xpNext));
  out[16] = uint8_t(p.energy < 0 ? 0 : p.energy > 100 ? 100 : p.energy);
  putField(out + 17, p.wear.id);
  put32(out + 41, p.wear.crc);
  out[45] = uint8_t(n);
  for (int k = 0; k < n; k++) {
    putField(out + 46 + k * 48, p.unlocked[k].id);
    putField(out + 46 + k * 48 + 24, p.unlocked[k].name);
  }
  put32(out + len - 4, avatarCrc32(out, len - 4));
  return len;
}

bool unpackPet(const uint8_t *in, size_t len, Pet &p) {
  if (!in || len < 50 || in[0] != 1 || (in[1] >> 5) || in[16] > 100 || in[45] > kPetUnlockedCap) return false;
  const int n = in[45];
  if (len != 46 + size_t(n) * 48 + 4 || get32(in + len - 4) != avatarCrc32(in, len - 4)) return false;
  Pet q;
  memset(&q, 0, sizeof q);
  if (!getField(in + 17, q.wear.id, sizeof q.wear.id)) return false;
  if ((in[1] & 4) ? !validAvatarId(q.wear.id) : q.wear.id[0] != 0) return false;
  for (int k = 0; k < n; k++) {
    if (!getField(in + 46 + k * 48, q.unlocked[k].id, sizeof q.unlocked[k].id) || !validAvatarId(q.unlocked[k].id)) return false;
    if (!getField(in + 46 + k * 48 + 24, q.unlocked[k].name, sizeof q.unlocked[k].name)) return false;
  }
  q.known = true;
  q.on = in[1] & 1;
  q.tired = in[1] & 2;
  q.special = in[1] & 16;
  q.wear.hasCrc = (in[1] & 8) != 0;
  q.wear.crc = q.wear.hasCrc ? get32(in + 41) : 0;
  q.level = get16(in + 2);
  q.xp = int32_t(get32(in + 4));
  q.xpLevelStart = int32_t(get32(in + 8));
  q.xpNext = int32_t(get32(in + 12));
  q.energy = in[16];
  q.nUnlocked = n;
  // ultimi traguardi e festa: in RAM, non si ripristinano
  p = q;
  return true;
}

}  // namespace cal
