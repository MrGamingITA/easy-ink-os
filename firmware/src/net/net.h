// Rete della scheda (M3, brief §8.5, docs/PROTOCOLLO.md): Wi-Fi, configurazione dal telefono (AP +
// portale captive), mDNS, client WebSocket verso il servizio, server HTTP sulla porta 80.
//
// Tutto gira in un task FreeRTOS sul core 0: il loop (core 1) resta bloccato 0,6–1,5 s a ogni refresh
// del pannello e non deve fermare la rete. Scambio con il loop, unico proprietario di cal::Ui:
//   loop → rete   request(): coda di NetRequest (dai comandi dell'interfaccia);
//   rete → loop   pollEvent(): coda di eventi piccoli; withState(): ultimo state già decodificato
//                 (casella con mutex, vince il più recente); takeScan(): ultima scansione.
// Il task non tocca mai il modello. M5: abbinamento, autenticazione e mac dei permessi (core/link) stanno
// qui; la chiave K e le password Wi-Fi passano dal vault (hw/secrets). Più reti (0.8.0, core/wifi_roam): fino a 5
// reti salvate, giro di collegamento dalla più forte, cambio automatico sotto −80 dBm, wifi-list/add/forget. Impostazioni remote: `set` e `full-refresh` arrivano al loop come
// eventi; il loop risponde con le richieste Settings (la rete tiene l'ultima copia e la manda anche dopo
// ogni hello) e Ack.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/apply_state.h"
#include "core/avatars.h"
#include "core/pet.h"
#include "core/remote_settings.h"
#include "core/ui.h"

namespace net {

enum class EvType : uint8_t {
  ApReady,        // text = password dell'AP
  Connecting,     // ssid
  Connected,      // ssid, text = ip, rssi
  ConnectFailed,  // ssid
  WifiLost,
  Rssi,           // rssi
  ScanDone,       // risultati con takeScan()
  PcHost,         // text = host del PC trovato via mDNS
  PcLost,         // WebSocket chiuso o 45 s senza niente
  Welcome,        // time (ms epoch UTC), text = tz POSIX, ssid = host del PC (solo log), avatar predefinito
  Bye,            // spegnimento pulito del PC (o riavvio del servizio): time = summary.off (ms epoch, 0 = assente)
  RemoteSet,      // `set` valido: id, patch (quelli non validi la rete li rifiuta da sola con l'ack)
  FullRefresh,    // `full-refresh`: id
  Ack,            // `ack` del servizio a un dismiss: id, rssi = ok (1/0), text = error
  // M5 (core/link): la sicurezza la fa il task di rete, al loop arrivano solo gli esiti
  PairCode,       // text = 6 cifre
  PairReady,      // pair-confirm valido
  PairDone,       // K salvata nel vault
  PairAbort,      // text = motivo
  Authed,         // auth-ok valido
  Redo,           // abbinamento da rifare: text = perché
  PermClose,      // id, text = reason
  // Aggiornamento remoto (docs/OTA.md, core/ota): la rete verifica, il loop decide le condizioni e disegna
  OtaOffer,       // ota-offer verificato (mac, firma, scheda): id, text = versione nuova, time = byte. Risposta: NetCmd::OtaGo
  OtaPhase,       // rssi = cal::OtaPhase (verifico, scrivo, riavvio, errore), text = motivo dell'errore
  OtaUpdated,     // la nuova versione è confermata (auth-ok entro 120 s dal riavvio)
  // Più reti (0.8.0)
  Saved,          // reti salvate o loro visibilità cambiate: lista con takeSaved()
  WifiForgotten,  // wifi-forget dal PC accettato: ssid (la scheda riavvia fra 3 s per la pulizia profonda)
  // Le richieste di permesso verificate arrivano con takePerm().
  Workday,        // 0.9.0: `workday` valido da una connessione autenticata: id, rssi = 1 start / 0 stop
  ThemeStored,    // 0.13.0: tema installato (id)
  ThemeUse,       // 0.13.0: theme-use da una connessione autenticata (id, '' = di serie)
  AvatarRejected, // 0.14.0 (R4): avatar a pezzi con il CAVT rotto sul PC (avatar: id e crc dell'offerta)
};

struct Event {
  EvType type;
  char ssid[cal::kSsidLen];
  char text[96];  // 96: i motivi dell'aggiornamento arrivano a 95 byte
  int rssi;
  int64_t time;
  char id[cal::kCmdIdLen];   // RemoteSet, FullRefresh
  cal::SettingsPatch patch;  // RemoteSet
  bool hasAvatar;            // Welcome: defaultAvatar presente
  cal::AvatarRef avatar;     // Welcome: defaultAvatar, defaultAvatarCrc; AvatarRejected: id e crc dell'offerta
};

// Avvia il task. boardId = ultime cifre del MAC (cal::boardIdFromMac).
void begin(const char *boardId);
void request(const cal::NetRequest &r);
bool pollEvent(Event &e);
// Chiama fn sull'ultimo state arrivato (se c'è) tenendo il lucchetto: fn deve essere breve.
bool withState(void (*fn)(const cal::StateMsg &msg, void *ctx), void *ctx);
int takeScan(cal::NetEntry *out, int cap);
// Prossima richiesta di permesso già verificata (mac con SK), nell'ordine di arrivo.
bool takePerm(cal::PermReq &out);
// Prossimo messaggio `avatar` arrivato (base64 già decodificato, CAVT ancora da validare): la memoria è
// sullo heap e la libera chi lo prende (free). Al massimo 4 in coda: gli altri si richiedono dopo 30 s.
bool takeAvatar(cal::AvatarMsg *&out);
// 0.14.0 (R4): avatar scaricato a pezzi e controllato (avatar-offer → GET /avatars/<token>), ancora nel temporaneo di
// avfs: il loop lo adotta (AvatarCache::adopt) e risponde con NetCmd::AvatarAck (id, avatarCrcs[0], ok, error). Fino
// alla risposta il task di rete non scarica altri avatar. Uno alla volta.
bool takeAvatarFile(::Avatar &out);
// 0.10.0, gioco della mascotte: messaggi `game` (già decodificati), `accessory` e `anchors` (base64 già decodificato,
// CACC e CANC ancora da validare). Come takeAvatar: sullo heap, li libera chi li prende (free).
bool takeGame(cal::GameMsg *&out);
bool takeAccessory(cal::AccessoryMsg *&out);
bool takeAnchors(cal::AnchorsMsg *&out);
bool hasSavedNetwork();
// Reti salvate (0.8.0) nell'ordine di inserimento, con la visibilità dell'ultima ricerca. ATTENZIONE: usedLocal qui è
// l'ultimo uso in ms epoch UTC (0 = mai): il loop lo converte in ora locale prima di passarlo a Ui::netSaved.
int takeSaved(cal::SavedNet *out, int cap);
// Cambio automatico di rete (core/wifi_roam): il loop dice se c'è un permesso aperto o una calibrazione in corso
// (l'aggiornamento remoto lo sa già il task di rete). Si può chiamare a ogni giro: costa due scritture atomiche.
void setRoamGuard(bool permOpen, bool calibrating);
// M6: risparmio del Wi-Fi (valori di wifi_ps_type_t): Attivo = Min, Attesa = Max, calibrazione fase 1 = None.
enum class PowerSave : uint8_t { None = 0, Min = 1, Max = 2 };
void setPowerSave(PowerSave ps);
// Risparmio applicato davvero dal task di rete (wifi_ps_type_t), −1 = mai impostato (predefinito dell'IDF: minimo).
int8_t appliedPowerSave();
// M6: prima del sonno profondo. WebSocket chiuso, mDNS fermo, Wi-Fi spento; il task resta fermo.
// false = il task non ha risposto entro timeoutMs (si dorme lo stesso).
bool shutdown(uint32_t timeoutMs);
const char *savedNetwork();  // prima rete salvata, '' se non ce ne sono (valido dopo begin)
// Byte dell'aggiornamento ricevuti finora (il loop li passa a Ui::otaProgress a ogni giro).
uint32_t otaDone();
// 0.12.0, modalità gioco (partita del Game Boy): WebSocket chiuso, mDNS, HTTP e Wi-Fi spenti (driver deinizializzato),
// buffer dello state e del registro liberati, task di rete chiuso: tutto torna allo heap. Le richieste di permesso
// restano nel terminale (nessuna risposta automatica); request() tiene solo l'ultima copia di settings e status.
// false = rifiutata (aggiornamento in corso) o task senza risposta entro timeoutMs: la rete resta com'era.
bool suspend(uint32_t timeoutMs);
// Fine della modalità gioco: buffer e task di nuovo, Wi-Fi dal primo tentativo, poi PC e WebSocket come all'avvio
// (il servizio rimanda le richieste ancora aperte). false = memoria insufficiente: riprovare.
bool resume();
bool suspended();


}  // namespace net
