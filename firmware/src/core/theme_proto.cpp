// Messaggi dei temi (PROTOCOLLO.md «Temi»): theme-offer, theme-use in arrivo; themes, theme-ack in uscita.
#include "core/theme_proto.h"

#include <stdio.h>
#include <string.h>

#include "core/apply_state.h"
#include "core/strbuf.h"
#include "core/theme_store.h"

namespace cal {
namespace theme {

bool lowerHex(const char *s, size_t n) {
  if (!s || strlen(s) != n) return false;
  for (size_t i = 0; i < n; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}

// Copia al massimo max byte senza spezzare un carattere UTF-8 (dst: max + 1 byte).
void cutUtf8(char *dst, const char *src, size_t max) {
  size_t n = strlen(src);
  if (n > max) {
    n = max;
    while (n > 0 && (uint8_t(src[n]) & 0xC0) == 0x80) n--;  // src[n] continua un carattere: si torna al suo inizio
  }
  memcpy(dst, src, n);
  dst[n] = 0;
}

// Un'offerta scartata lascia out com'era.
bool decodeOffer(JsonVariantConst root, Offer &out) {
  if (msgType(root) != MsgType::ThemeOffer) return false;
  const char *id = root["id"].as<const char *>(), *tok = root["token"].as<const char *>();
  if (!validThemeId(id) || !lowerHex(tok, 32) || !root["crc"].is<uint32_t>() || !root["size"].is<uint32_t>()) return false;
  Offer o;
  memset(&o, 0, sizeof o);
  o.size = root["size"].as<uint32_t>();
  if (o.size < uint32_t(kHeaderLen) || o.size > uint32_t(kMaxFile)) return false;
  o.crc = root["crc"].as<uint32_t>();
  scopy(o.id, kIdLen, id);
  scopy(o.token, sizeof o.token, tok);
  out = o;
  return true;
}

bool decodeUse(JsonVariantConst root, char *id, size_t cap) {
  if (msgType(root) != MsgType::ThemeUse) return false;
  if (!cap || root["id"].isUnbound()) return false;
  if (root["id"].isNull()) { id[0] = 0; return true; }
  const char *s = root["id"].as<const char *>();
  if (!validThemeId(s) || strlen(s) >= cap) return false;  // mai un id troncato
  scopy(id, cap, s);
  return true;
}

size_t encodeThemes(const CatalogEntry *e, int n, const char *cur, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "themes";
  d["v"] = 1;
  JsonArray have = d["have"].to<JsonArray>();
  // il servizio scarta tutto il messaggio se una voce non va: si saltano gli id non validi, al massimo 8 voci
  for (int i = 0, k = 0; i < n && k < kMaxThemes; i++) {
    if (!validThemeId(e[i].id)) continue;
    k++;
    JsonObject o = have.add<JsonObject>();
    o["id"] = e[i].id;
    o["crc"] = e[i].crc;
  }
  if (validThemeId(cur)) d["cur"] = cur;
  else d["cur"] = nullptr;
  const size_t len = measureJson(d);
  if (len + 1 > cap) return 0;
  return serializeJson(d, out, cap);
}

// theme-ack (schema themeAck): id obbligatorio e valido (niente null), error al massimo 127 byte.
size_t encodeAck(const char *id, bool ok, const char *error, char *out, size_t cap) {
  if (!validThemeId(id)) return 0;  // il servizio lo scarterebbe: non si manda
  JsonDocument d;
  d["t"] = "theme-ack";
  d["v"] = 1;
  d["id"] = id;
  d["ok"] = ok;
  if (!ok && error && error[0]) {
    char e[kAckErrorMax + 1];
    cutUtf8(e, error, kAckErrorMax);
    d["error"] = e;  // copiato nel documento (char* non costante)
  }
  const size_t len = measureJson(d);
  if (len + 1 > cap) return 0;
  return serializeJson(d, out, cap);
}

bool blobUrl(const char *host, uint16_t port, const char *dir, const char *token, char *out, size_t cap) {
  const int n = snprintf(out, cap, "http://%s:%u/%s/%s", host, unsigned(port), dir, token);
  return n > 0 && size_t(n) < cap;
}

bool downloadUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap) {
  return blobUrl(host, port, "themes", token, out, cap);
}

// §7: solo da una connessione autenticata (prima di tutto), un tema alla volta.
OfferVerdict acceptOffer(bool authed, bool busy) {
  if (!authed) return OfferVerdict::NotAuthed;
  return busy ? OfferVerdict::Busy : OfferVerdict::Accept;
}

bool acceptUse(bool authed) { return authed; }

}  // namespace theme
}  // namespace cal
