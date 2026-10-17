// Più reti Wi-Fi (firmware 0.8.0): messaggi con il servizio. Logica pura, provata in native (test_wifi) con i
// vettori di protocol/vectors/wifi-vectors.json (generati con Node, li usa anche il servizio Go).
//
//   display → servizio   wifi-list   {networks: [{ssid, current, rssi|null, lastUsed|null}]}   mai password
//   servizio → display   wifi-add    {id, ssid, iv, ct}   ct = AES-256-GCM(key, iv, aad, password) ‖ tag
//                        key = HKDF(SK, salt = id, info = "calamaio-wifi"), aad = "wifi-add" ␟ id ␟ ssid
//   servizio → display   wifi-forget {id, ssid, mac}      mac = HMAC(SK, "wifi-forget" ␟ id ␟ ssid), 16 byte
//   display → servizio   ack {id, ok, error?}
// wifi-add e wifi-forget valgono solo su una connessione autenticata (auth-ok, SK della connessione).
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "crypto.h"
#include "model.h"

namespace cal {
namespace wifi {

const int kIdLen = 33;     // id del comando: 1–32 byte (schema cmdId), ripetuto nell'ack
const int kPassMin = 8;    // password: da 8 a 63 byte (WPA2, frase), senza byte zero
const int kPassMax = 63;
const int kSsidMax = 32;
const size_t kListMax = 1024;  // wifi-list con 5 reti da 32 byte: ~ 450 byte

// Errori dell'ack (testi fissi: il servizio li mostra così come sono).
extern const char *const kErrFull;     // «lista piena»
extern const char *const kErrPass;     // «password non valida»
extern const char *const kErrDecrypt;  // «decifratura fallita»
extern const char *const kErrSsid;     // «ssid non valido»
extern const char *const kErrMac;      // «mac non valido»
extern const char *const kErrUnknown;  // «rete sconosciuta»
extern const char *const kErrAuth;     // senza auth-ok (lo stesso testo dei set remoti)

// Una voce di wifi-list. hasRssi falso → null; lastUsed 0 → null.
struct ListEntry {
  char ssid[kSsidLen];
  bool current;
  bool hasRssi;
  int rssi;
  int64_t lastUsed;
};
size_t encodeList(const ListEntry *e, int n, char *out, size_t cap);  // 0 se non sta in cap

void addKey(const uint8_t SK[32], const char *id, uint8_t key[32]);
bool addAad(const char *id, const char *ssid, cry::Msg &m);
bool forgetMac(const uint8_t SK[32], const char *id, const char *ssid, uint8_t out[16]);
// Cifra come il servizio (per i test e per la prova a mano): ct = cifrato ‖ tag. 0 se non sta in cap.
size_t sealAdd(const uint8_t SK[32], const char *id, const char *ssid, const char *pass, const uint8_t iv[12], uint8_t *ct,
               size_t cap);

// wifi-add letto e controllato. hasId falso: niente id valido, quindi niente ack (si scarta e si registra).
// error nullo: pass contiene la password in chiaro (da cancellare con cry::wipe appena salvata).
// La lista piena la controlla chi chiama (conosce le reti salvate): qui solo autenticazione, campi, decifratura, misura.
struct AddCheck {
  bool hasId;
  char id[kIdLen];
  char ssid[kSsidLen];
  char pass[kPassMax + 1];
  const char *error;
};
void checkAdd(JsonVariantConst root, bool authed, const uint8_t SK[32], AddCheck &out);

// wifi-forget letto e controllato (mac). Se la rete esiste lo dice chi chiama (errore kErrUnknown).
struct ForgetCheck {
  bool hasId;
  char id[kIdLen];
  char ssid[kSsidLen];
  const char *error;
};
void checkForget(JsonVariantConst root, bool authed, const uint8_t SK[32], ForgetCheck &out);

}  // namespace wifi
}  // namespace cal
