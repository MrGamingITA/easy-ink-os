// Messaggi dei temi (PROTOCOLLO.md «Temi», v 1): theme-offer e theme-use dal servizio (solo da connessione
// autenticata), themes e theme-ack dalla scheda. Rete non fidata: id, token e dimensioni si controllano qui.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "core/theme_runtime.h"

namespace cal {
namespace theme {

struct Offer {
  char id[kIdLen];
  uint32_t crc;
  uint32_t size;
  char token[33];  // 16 byte in 32 cifre esadecimali minuscole
};
bool decodeOffer(JsonVariantConst root, Offer &out);
// id null = tema di serie (id ''). Il campo id c'è sempre.
bool decodeUse(JsonVariantConst root, char *id, size_t cap);

// themes {have: al massimo 8 {id, crc} con id valido, cur: id o null}: com'è richiesto da parseThemes nel servizio.
// 0 se non ci sta in cap (mai un JSON troncato).
size_t encodeThemes(const CatalogEntry *e, int n, const char *cur, char *out, size_t cap);
// id non valido → 0 (non si manda); error tagliato a kAckErrorMax byte su un confine UTF-8.
constexpr size_t kAckErrorMax = 127;
size_t encodeAck(const char *id, bool ok, const char *error, char *out, size_t cap);
// http://<host>:<port>/themes/<token>; false se non ci sta.
bool downloadUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap);
// 0.14.0: in comune coi download degli avatar (core/avatars). http://<host>:<port>/<dir>/<token>; false se non ci sta.
bool blobUrl(const char *host, uint16_t port, const char *dir, const char *token, char *out, size_t cap);
// s ha esattamente n cifre esadecimali minuscole (i token).
bool lowerHex(const char *s, size_t n);
// Copia al massimo max byte senza spezzare un carattere UTF-8 (dst: max + 1 byte).
void cutUtf8(char *dst, const char *src, size_t max);

// Decisioni della scheda (§7): prima l'autenticazione, poi «un altro tema in arrivo».
enum class OfferVerdict : uint8_t { Accept, NotAuthed, Busy };
OfferVerdict acceptOffer(bool authed, bool busy);
bool acceptUse(bool authed);

}  // namespace theme
}  // namespace cal
