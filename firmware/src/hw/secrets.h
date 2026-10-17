// Segreti della scheda (docs/M5-PERMESSI.md §4) sull'hardware vero: core/vault con la eFuse e la periferica
// HMAC dell'ESP32-S3 (esp_efuse_*, esp_hmac_calculate dell'IDF 4.4) e la NVS (Preferences «calamaio-sec»),
// più la pulizia profonda della NVS (core/nvs_scrub, hw/nvs_idf).
// Le funzioni si chiamano dal loop e dal task di rete: un mutex le mette in fila.
//
// La eFuse si scrive SOLO da protect(), che il loop chiama solo dopo Menu › Sicurezza › Proteggi le chiavi,
// avviso, 2× e tieni 3 s. Mai in automatico.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/vault.h"

namespace secrets {

// Pulizia profonda della NVS (core/nvs_scrub): si chiama in setup() PRIMA di ogni altro uso della NVS
// (impostazioni, rete, avatar), perché cancella e reinizializza la partizione e invaliderebbe gli handle aperti.
// Fa prima il lavoro di begin() che toglie voci (migrazione di «calamaio-net», copia di «nvs.net80211»),
// poi, se la bandiera «calamaio-scrub/scrub_v1» manca, copia in RAM tutte le voci valide, cancella la
// partizione, le riscrive e le rilegge. La bandiera si toglie dopo «Proteggi le chiavi», ogni rete dimenticata (menu,
// wifi-forget, rete tolta dal portale), «Dimentica abbinamento» e il ripristino di fabbrica, che poi riavviano la
// scheda (vedi requestScrub); dopo una password cambiata la pulizia aspetta il prossimo avvio (scrubAtNextBoot).
// A ogni avvio scrive nella seriale quante volte K e la password Wi-Fi compaiono in chiaro nella partizione.
void bootScrub();

// Avvio (prima della rete): vault, migrazione della rete salvata dalle versioni prima di M5 («calamaio-net»),
// cancellazione della copia in chiaro che il driver Wi-Fi dell'IDF tiene in «nvs.net80211».
// Se bootScrub() l'ha già fatto, qui restano solo l'autoverifica e il riepilogo.
void begin();
bool keysProtected();

// Più reti (firmware 0.8.0, core/vault): fino a 5 reti nell'ordine di inserimento, password nel vault.
int loadNets(cal::Vault::NetMeta *out, int cap);
bool loadNetPass(const char *ssid, char *pass, size_t cap);
// Rete nuova in fondo alla lista o password nuova per una rete già salvata (Updated): in quel caso la password vecchia
// resta fra le voci cancellate della NVS, quindi la pulizia profonda si fa al prossimo avvio (senza riavviare adesso).
cal::Vault::AddNet addNet(const char *ssid, const char *pass, int64_t lastUsed);
// Dimentica una rete (menu, wifi-forget, rete tolta dal portale con la lista piena): pulizia profonda e riavvio dopo
// kScrubRestartMs, come «Dimentica rete» delle versioni precedenti. why: motivo per il registro.
bool forgetNet(const char *ssid, const char *why);
// Toglie una rete senza riavviare: la pulizia la chiede chi chiama dopo (portale: quando la configurazione si chiude).
bool dropNet(const char *ssid);
// Dimentica tutte le reti (Menu › Wi-Fi › Dimentica tutte le reti): pulizia profonda e riavvio.
void forgetWifi();
bool touchNet(const char *ssid, int64_t lastUsed);

bool loadPair(uint8_t K[32], char *host, size_t hostCap);
bool savePair(const uint8_t K[32], const char *host);
void forgetPair();

// Chiave pubblica Ed25519 degli aggiornamenti (docs/OTA.md §2); savePair() e forgetPair() la cancellano.
bool loadOtaKey(uint8_t pub[32]);
bool saveOtaKey(const uint8_t pub[32]);

// «Proteggi le chiavi». Scrive nella seriale cosa fa e perché.
cal::ProtectResult protect();
// Ripristino di fabbrica: Wi-Fi, abbinamento, sale (le impostazioni le cancella il loop, che poi riavvia).
void factoryReset();

// Dopo protect() riuscito, forgetNet(), forgetWifi() e forgetPair() con qualcosa da togliere, factoryReset(): toglie la
// bandiera della pulizia e riavvia la scheda dopo kScrubRestartMs, così la pulizia la fa bootScrub() quando
// nessuno ha handle NVS aperti. Le voci cancellate restano leggibili nella flash fino a quel riavvio.
const uint32_t kScrubRestartMs = 3000;  // toast di 2,6 s (cal::kToastMs) + 0,4 s per il refresh
void requestScrub(const char *why);
// Toglie solo la bandiera: la pulizia profonda si fa al prossimo avvio, quando capita (nessun riavvio adesso).
void scrubAtNextBoot(const char *why);

}  // namespace secrets
