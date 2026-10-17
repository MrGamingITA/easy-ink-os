#include "wifi_proto.h"

#include <string.h>

#include "apply_state.h"
#include "strbuf.h"

namespace cal {
namespace wifi {

const char *const kErrFull = "lista piena";
const char *const kErrPass = "password non valida";
const char *const kErrDecrypt = "decifratura fallita";
const char *const kErrSsid = "ssid non valido";
const char *const kErrMac = "mac non valido";
const char *const kErrUnknown = "rete sconosciuta";
const char *const kErrAuth = "display non abbinato: cambia le impostazioni dal menu del display";

namespace {

const char *str(JsonVariantConst v) { return v.is<const char *>() ? v.as<const char *>() : nullptr; }

// id: 1–32 byte; ssid: 1–32 byte. Copiati in out (false = non validi).
bool readText(JsonVariantConst v, char *out, size_t cap, size_t maxLen) {
  const char *s = str(v);
  if (!s) return false;
  const size_t n = strlen(s);
  if (n < 1 || n > maxLen || n + 1 > cap) return false;
  memcpy(out, s, n + 1);
  return true;
}

}  // namespace

size_t encodeList(const ListEntry *e, int n, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "wifi-list";
  d["v"] = kProtoVersion;
  JsonArray a = d["networks"].to<JsonArray>();
  for (int k = 0; k < n; k++) {
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = e[k].ssid;
    o["current"] = e[k].current;
    if (e[k].hasRssi) o["rssi"] = e[k].rssi;
    else o["rssi"] = nullptr;
    if (e[k].lastUsed > 0) o["lastUsed"] = e[k].lastUsed;
    else o["lastUsed"] = nullptr;
  }
  if (measureJson(d) + 1 > cap) { if (cap) out[0] = 0; return 0; }
  return serializeJson(d, out, cap);
}

void addKey(const uint8_t SK[32], const char *id, uint8_t key[32]) {
  static const char kInfo[] = "calamaio-wifi";
  cry::hkdf(reinterpret_cast<const uint8_t *>(id), strlen(id), SK, 32, reinterpret_cast<const uint8_t *>(kInfo), sizeof kInfo - 1,
            key, 32);
}

bool addAad(const char *id, const char *ssid, cry::Msg &m) {
  m.str("wifi-add").sep().str(id).sep().str(ssid);
  return m.ok();
}

bool forgetMac(const uint8_t SK[32], const char *id, const char *ssid, uint8_t out[16]) {
  cry::Msg m;
  m.str("wifi-forget").sep().str(id).sep().str(ssid);
  return m.ok() && cry::mac16(SK, m, out);
}

size_t sealAdd(const uint8_t SK[32], const char *id, const char *ssid, const char *pass, const uint8_t iv[12], uint8_t *ct, size_t cap) {
  const size_t n = strlen(pass);
  if (n + cry::kGcmTag > cap) return 0;
  uint8_t key[32];
  addKey(SK, id, key);
  cry::Msg aad;
  bool ok = addAad(id, ssid, aad);
  ok = ok && cry::gcmEncrypt(key, iv, aad.data(), aad.size(), reinterpret_cast<const uint8_t *>(pass), n, ct, ct + n);
  cry::wipe(key, sizeof key);
  return ok ? n + cry::kGcmTag : 0;
}

void checkAdd(JsonVariantConst root, bool authed, const uint8_t SK[32], AddCheck &out) {
  memset(&out, 0, sizeof out);
  out.hasId = readText(root["id"], out.id, sizeof out.id, kIdLen - 1);
  if (!out.hasId) { out.error = kErrDecrypt; return; }
  if (!authed) { out.error = kErrAuth; return; }
  if (!readText(root["ssid"], out.ssid, sizeof out.ssid, kSsidMax)) { out.error = kErrSsid; return; }
  // iv: esattamente 12 byte; ct: cifrato + tag, al massimo 64 byte di testo (oltre 63 la password non vale comunque).
  const char *ivHex = str(root["iv"]), *ctHex = str(root["ct"]);
  uint8_t iv[cry::kGcmIv];
  const size_t ctLen = ctHex ? strlen(ctHex) : 0;
  const size_t kCtMax = 128 + cry::kGcmTag;
  if (!ivHex || strlen(ivHex) != 2 * cry::kGcmIv || !cry::fromHex(ivHex, iv, sizeof iv) || ctLen % 2 ||
      ctLen / 2 < cry::kGcmTag) {
    out.error = kErrDecrypt;
    return;
  }
  if (ctLen / 2 > kCtMax) { out.error = kErrPass; return; }  // troppo lunga per essere una password valida
  uint8_t ct[kCtMax], plain[kCtMax];
  const size_t cn = ctLen / 2, pn = cn - cry::kGcmTag;
  if (!cry::fromHex(ctHex, ct, cn)) { out.error = kErrDecrypt; return; }
  uint8_t key[32];
  addKey(SK, out.id, key);
  cry::Msg aad;
  const bool ok = addAad(out.id, out.ssid, aad) &&
                  cry::gcmDecrypt(key, iv, aad.data(), aad.size(), ct, pn, plain, ct + pn);
  cry::wipe(key, sizeof key);
  if (!ok) { cry::wipe(plain, sizeof plain); out.error = kErrDecrypt; return; }
  bool zero = false;
  for (size_t k = 0; k < pn; k++) zero = zero || plain[k] == 0;
  if (pn < size_t(kPassMin) || pn > size_t(kPassMax) || zero) {
    cry::wipe(plain, sizeof plain);
    out.error = kErrPass;
    return;
  }
  memcpy(out.pass, plain, pn);
  out.pass[pn] = 0;
  cry::wipe(plain, sizeof plain);
}

void checkForget(JsonVariantConst root, bool authed, const uint8_t SK[32], ForgetCheck &out) {
  memset(&out, 0, sizeof out);
  out.hasId = readText(root["id"], out.id, sizeof out.id, kIdLen - 1);
  if (!out.hasId) { out.error = kErrMac; return; }
  if (!authed) { out.error = kErrAuth; return; }
  if (!readText(root["ssid"], out.ssid, sizeof out.ssid, kSsidMax)) { out.error = kErrSsid; return; }
  uint8_t mac[16], want[16];
  const char *macHex = str(root["mac"]);
  if (!macHex || strlen(macHex) != 32 || !cry::fromHex(macHex, mac, sizeof mac) || !forgetMac(SK, out.id, out.ssid, want) ||
      !cry::ctEqual(mac, want, sizeof mac)) {
    out.error = kErrMac;
  }
}

}  // namespace wifi
}  // namespace cal
