#include "remote_settings.h"

#include <string.h>

#include "apply_state.h"
#include "strbuf.h"
#include "workday.h"

namespace cal {

const char *const kPermOnRemoteError = "permOn: si accende solo dal dispositivo";

const char *speedKey(SpeedMode s) { return s == SpeedMode::Massima ? "massima" : "profilo"; }
const char *muteKey(MuteKind k) { return k == MuteKind::Hour ? "1h" : k == MuteKind::Night ? "night" : "off"; }

namespace {

bool readId(JsonVariantConst root, char *id) {
  const char *s = root["id"].as<const char *>();
  if (!s || !root["id"].is<const char *>()) return false;
  const size_t n = strlen(s);
  if (n < 1 || n > static_cast<size_t>(kCmdIdLen - 1)) return false;
  memcpy(id, s, n + 1);
  return true;
}

// Indice di s in keys (n voci), -1 se non c'è o s non è una stringa.
int pick(JsonVariantConst v, const char *const *keys, int n) {
  if (!v.is<const char *>()) return -1;
  const char *s = v.as<const char *>();
  for (int k = 0; k < n; k++)
    if (streq(keys[k], s)) return k;
  return -1;
}

void fail(RemoteCmd &out, const char *a, const char *b = "") {
  out.ok = false;
  out.patch.has = 0;
  Sb(out.error, sizeof out.error).s(a).s(b);
}

}  // namespace

bool decodeFullRefresh(JsonVariantConst root, RemoteCmd &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::FullRefresh) return false;
  if (!readId(root, out.id)) return false;
  out.fullRefresh = true;
  out.ok = true;
  return true;
}

bool decodeSet(JsonVariantConst root, RemoteCmd &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::Set) return false;
  if (!readId(root, out.id)) return false;
  out.ok = true;
  JsonObjectConst o = root["settings"].as<JsonObjectConst>();
  // Regola dei permessi prima di tutto il resto: con permOn: true il rifiuto ha sempre questo motivo.
  if (!o.isNull() && o["permOn"].is<bool>() && o["permOn"].as<bool>()) { fail(out, kPermOnRemoteError); return true; }
  if (o.isNull()) { fail(out, "settings: manca o non è un oggetto"); return true; }
  if (o.size() == 0) { fail(out, "settings: nessuna impostazione da cambiare"); return true; }

  static const char *const kHome[] = {"goccia", "righe", "contatori", "una"};
  static const char *const kAnim[] = {"auto", "raffiche", "continue", "spente"};
  static const char *const kSpeed[] = {"profilo", "massima"};
  static const char *const kMute[] = {"off", "1h", "night"};
  SettingsPatch &p = out.patch;
  for (JsonPairConst kv : o) {
    const char *key = kv.key().c_str();
    JsonVariantConst v = kv.value();
    int k = -1;
    for (int q = 0; q < static_cast<int>(RemoteKey::Count); q++)
      if (streq(remoteKeyName(static_cast<RemoteKey>(q)), key)) k = q;
    if (k < 0) { fail(out, "chiave sconosciuta: ", key); return true; }
    const RemoteKey rk = static_cast<RemoteKey>(k);
    int idx = -1;
    bool b = false;
    switch (rk) {
      case RemoteKey::HomeStyle: idx = pick(v, kHome, 4); if (idx >= 0) p.v.homeStyle = static_cast<HomeStyle>(idx); break;
      case RemoteKey::Anim: idx = pick(v, kAnim, 4); if (idx >= 0) p.v.anim = static_cast<AnimMode>(idx); break;
      case RemoteKey::Speed: idx = pick(v, kSpeed, 2); if (idx >= 0) p.v.speed = static_cast<SpeedMode>(idx); break;
      case RemoteKey::Mute: idx = pick(v, kMute, 3); if (idx >= 0) p.v.mute = static_cast<MuteKind>(idx); break;
      case RemoteKey::WorkStart: case RemoteKey::WorkEnd: {  // «HH:MM» a due cifre, 00:00–23:59
        int mins = 0;
        if (v.is<const char *>() && parseHm(v.as<const char *>(), mins)) {
          idx = 0;
          if (rk == RemoteKey::WorkStart) p.v.workStart = int16_t(mins);
          else p.v.workEnd = int16_t(mins);
        }
        break;
      }
      default:  // i booleani: solo true/false, niente 0/1 né stringhe
        if (v.is<bool>()) { idx = 0; b = v.as<bool>(); }
        break;
    }
    if (idx < 0) { fail(out, key, ": valore non valido"); return true; }
    switch (rk) {
      case RemoteKey::Invert: p.v.invert = b; break;
      case RemoteKey::Rot: p.v.rot = b; break;
      case RemoteKey::Night: p.v.night = b; break;
      case RemoteKey::PermOn: p.v.permOn = b; break;  // qui vale sempre false (true rifiutato sopra)
      case RemoteKey::Sound: p.v.sound = b; break;
      case RemoteKey::Led: p.v.led = b; break;
      case RemoteKey::Save: p.v.save = b; break;  // si accende e si spegne anche da remoto
      case RemoteKey::Game: p.v.game = b; break;  // 0.10.0: gioco della mascotte
      default: break;
    }
    p.has |= 1u << k;
  }
  return true;
}

size_t encodeSettings(const RemoteValues &v, char *out, size_t cap, bool remote) {
  JsonDocument d;
  d["t"] = "settings";
  d["v"] = kProtoVersion;
  JsonObject o = d["settings"].to<JsonObject>();
  o["homeStyle"] = homeKey(v.homeStyle);
  o["anim"] = animKey(v.anim);
  o["speed"] = speedKey(v.speed);
  o["invert"] = v.invert;
  o["rot"] = v.rot;
  o["night"] = v.night;
  o["permOn"] = v.permOn;
  o["sound"] = v.sound;
  o["led"] = v.led;
  o["mute"] = muteKey(v.mute);
  o["save"] = v.save;
  char a[8], b[8];  // 0.9.0: sempre tutte e due (PROTOCOLLO.md, «Giornata di lavoro»)
  minHm(v.workStart, a, sizeof a);
  minHm(v.workEnd, b, sizeof b);
  o["workStart"] = static_cast<const char *>(a);
  o["workEnd"] = static_cast<const char *>(b);
  o["game"] = v.game;  // 0.10.0: gioco della mascotte, sempre
  d["remote"] = remote;
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

bool checkPatch(const SettingsPatch &p, const Settings &cur, char *err, size_t cap) {
  if (cap) err[0] = 0;
  const int s = p.on(RemoteKey::WorkStart) ? p.v.workStart : cur.workStart;
  const int e = p.on(RemoteKey::WorkEnd) ? p.v.workEnd : cur.workEnd;
  if ((p.on(RemoteKey::WorkStart) || p.on(RemoteKey::WorkEnd)) && !workTimesOk(s, e)) {
    char a[8], b[8];
    minHm(s, a, sizeof a);
    minHm(e, b, sizeof b);
    Sb(err, cap).s("l'inizio (").s(a).s(") deve venire prima della fine (").s(b).s(")");
    return false;
  }
  return true;
}

bool decodeWorkday(JsonVariantConst root, WorkdayCmd &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::Workday) return false;
  if (!readId(root, out.id)) return false;
  const char *a = root["action"].is<const char *>() ? root["action"].as<const char *>() : nullptr;
  if (a && streq(a, "start")) { out.ok = true; out.start = true; }
  else if (a && streq(a, "stop")) { out.ok = true; out.start = false; }
  else scopy(out.error, "action: valore non valido");
  return true;
}

size_t encodeAck(const char *id, bool ok, const char *error, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "ack";
  d["v"] = kProtoVersion;
  d["id"] = id;
  d["ok"] = ok;
  char err[kAckErrLen];
  if (!ok) {
    scopy(err, error && error[0] ? error : "rifiutato");  // max 95 byte, taglio su un confine UTF-8
    d["error"] = static_cast<const char *>(err);
  }
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

size_t encodeDismiss(const char *id, const char *session, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "dismiss";
  d["v"] = kProtoVersion;
  d["id"] = id;
  d["session"] = session;
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

bool decodeAck(JsonVariantConst root, AckMsg &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::Ack) return false;
  if (!readId(root, out.id) || !root["ok"].is<bool>()) return false;
  out.ok = root["ok"].as<bool>();
  if (root["error"].is<const char *>()) scopy(out.error, root["error"].as<const char *>());
  return true;
}

// ---------- NVS ----------

namespace {
uint8_t sum(const uint8_t *b, size_t n) {
  uint8_t s = 0x5A;
  for (size_t k = 0; k < n; k++) s = static_cast<uint8_t>((s << 1 | s >> 7) ^ b[k]);
  return s;
}
}  // namespace

size_t packSettings(const Settings &s, const StoredMute &mute, uint8_t *out, size_t cap) {
  if (cap < kStoredLen) return 0;
  memset(out, 0, kStoredLen);
  out[0] = kStoredVersion;
  out[1] = static_cast<uint8_t>(s.homeStyle);
  out[2] = static_cast<uint8_t>(s.anim);
  out[3] = static_cast<uint8_t>(s.speed);
  out[4] = (s.invert ? 1 : 0) | (s.rot ? 2 : 0) | (s.night ? 4 : 0) | (s.permOn ? 8 : 0) | (s.sound ? 16 : 0) |
           (s.led ? 32 : 0) | (s.save ? 64 : 0) | (s.hookDirect ? 128 : 0);
  out[5] = static_cast<uint8_t>(mute.kind);
  const uint64_t e = static_cast<uint64_t>(mute.kind == MuteKind::None ? 0 : mute.endEpochMs);
  for (int k = 0; k < 8; k++) out[6 + k] = static_cast<uint8_t>(e >> (8 * k));
  out[14] = s.game ? 1 : 0;  // 0.10.0: byte libero fino alla 0.9.0 (scritto 0: chi aggiorna trova il gioco spento)
  out[15] = sum(out, 15);
  return kStoredLen;
}

bool unpackSettings(const uint8_t *in, size_t len, Settings &s, StoredMute &mute) {
  if (!in || len != kStoredLen || in[0] != kStoredVersion || in[15] != sum(in, 15)) return false;
  if (in[1] >= static_cast<int>(HomeStyle::Count) || in[2] >= static_cast<int>(AnimMode::Count) ||
      in[3] >= static_cast<int>(SpeedMode::Count) || in[5] > static_cast<int>(MuteKind::Night) || in[14] > 1)
    return false;
  s.homeStyle = static_cast<HomeStyle>(in[1]);
  s.anim = static_cast<AnimMode>(in[2]);
  s.speed = static_cast<SpeedMode>(in[3]);
  s.invert = in[4] & 1;
  s.rot = in[4] & 2;
  s.night = in[4] & 4;
  s.permOn = in[4] & 8;
  s.sound = in[4] & 16;
  s.led = in[4] & 32;
  s.save = in[4] & 64;
  s.hookDirect = in[4] & 128;
  s.game = in[14] == 1;
  s.mute = 0;
  s.muteKind = MuteKind::None;
  uint64_t e = 0;
  for (int k = 0; k < 8; k++) e |= static_cast<uint64_t>(in[6 + k]) << (8 * k);
  mute.kind = static_cast<MuteKind>(in[5]);
  mute.endEpochMs = static_cast<int64_t>(e);
  return true;
}

bool restoreMute(Settings &s, const StoredMute &mute, int64_t nowEpochMs, Ms now) {
  if (mute.kind == MuteKind::None || mute.endEpochMs <= 0) return false;
  const int64_t left = mute.endEpochMs - nowEpochMs;
  if (left <= 0 || left > kMaxRestoreMs) return false;
  s.mute = now + left;
  s.muteKind = mute.kind;
  return true;
}

}  // namespace cal
