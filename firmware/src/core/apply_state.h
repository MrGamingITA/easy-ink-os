// Messaggi del servizio (docs/PROTOCOLLO.md, versione 1) → modello dell'interfaccia.
// Due passi, entrambi puri e provati in native:
//   1. parseMessage + decode*: JSON → struttura intermedia a dimensione fissa (StateMsg, WelcomeMsg).
//      Si fa nel task di rete; i testi si troncano qui su un confine UTF-8.
//   2. applyState: StateMsg → cal::Model. Si fa nel loop (unico proprietario del modello).
// I tempi del protocollo sono ms epoch UTC; il modello usa il tempo monotono Ms. La conversione usa
// il campo `time` dello stesso state (istantanea del PC): t_mono = now + (t − time). Così le durate
// («12 min») sono giuste anche se l'orologio della scheda non è ancora sincronizzato.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "model.h"

namespace cal {

const int kProtoVersion = 1;
// Frame più lunghi si scartano senza leggerli. 15 KB = WEBSOCKETS_MAX_DATA_SIZE di arduinoWebSockets su
// ESP32 (la libreria chiude la connessione oltre quel limite). Stima di uno state pieno: ≈ 11 KB.
const size_t kMaxFrame = 15 * 1024;
const int kStateLogCap = 20;     // log: «al massimo 20»

enum class MsgType : uint8_t {
  Unknown, Welcome, State, Bye, Set, FullRefresh, Ack,
  // M5 (core/link): servizio → display
  PairBegin, PairReveal, PairConfirm, PairAbort, AuthChallenge, AuthOk, AuthFail, PermRequest, PermClose,
  Avatar,  // avatar: CAVT in base64 (core/avatars, docs/AVATAR.md §10)
  // docs/OTA.md (core/ota, core/logring): servizio → display, solo da connessione autenticata
  OtaKey, OtaOffer, OtaCancel, LogStart, LogStop,
  // Più reti (0.8.0, core/wifi_proto): servizio → display, solo da connessione autenticata
  WifiAdd, WifiForget,
  Workday,  // 0.9.0: giornata di lavoro (servizio → display, solo da connessione autenticata)
  // 0.10.0, gioco della mascotte (core/pet): riassunto del gioco, disegno di un accessorio (CACC), ancore di un avatar (CANC)
  Game, Accessory, Anchors,
  // 0.13.0, temi delle schermate (core/theme_proto): servizio → display, solo da connessione autenticata
  ThemeOffer, ThemeUse,
  // 0.14.0 (R4): avatar a pezzi (core/avatars): servizio -> display, anche senza autenticazione
  AvatarOffer,
};

// JSON con filtro (solo i campi usati) e profondità massima: frame malformati → false.
bool parseMessage(const char *data, size_t len, JsonDocument &doc);
// t e v (= 1). Tipo sconosciuto o versione diversa → Unknown (si ignora).
MsgType msgType(JsonVariantConst root);

struct WelcomeMsg {
  int64_t time;   // ms epoch UTC (0 = assente)
  char tz[64];    // POSIX TZ ('' = assente)
  char host[kHostLen];
  char service[16];
  bool hasPaired;  // M5: welcome.paired presente (servizi più vecchi non lo mandano)
  bool paired;     // il servizio ha una chiave di abbinamento per questa scheda
  bool hasDefAvatar;    // defaultAvatar presente (facoltativo)
  AvatarRef defAvatar;  // defaultAvatar, defaultAvatarCrc
};
bool decodeWelcome(JsonVariantConst root, WelcomeMsg &out);

// state: stessi tipi del modello, ma tempi ancora in ms epoch UTC.
struct StateMsg {
  int64_t time;
  Session sessions[kMaxSessions];
  int nSessions;
  int sessionsDropped;  // oltre kMaxSessions (il protocollo dice al massimo 6)
  Limits limits;
  Stats today;
  LogEntry log[kStateLogCap];
  int nLog;
  Service services[kServiceCap];
  int nServices;
  Pr prs[kPrCap];
  int nPrs;
  AgendaItem agenda[kAgendaCap];
  int nAgenda;
  bool hasDefAvatar;    // defaultAvatar alla radice (facoltativo: assente = invariato)
  AvatarRef defAvatar;
};
// false se manca qualcosa di essenziale (t/v sbagliati, time assente): il messaggio si ignora.
// I campi mancanti o null diventano vuoti/assenti, i tipi sbagliati si ignorano.
bool decodeState(JsonVariantConst root, StateMsg &out);

// Applica lo state al modello (sessioni con i loro avatar, avatar predefinito se c'è, limiti, oggi, log,
// servizi, pr, agenda) e segna il PC
// collegato (sys.pc, lastPc = now). Gli eventi locali del log (LogEntry::local) restano.
void applyState(Model &m, const StateMsg &msg, Ms now);

// Modello di partenza con la rete: niente dati finti (sessioni, log, oggi, limiti, servizi, reti),
// Wi-Fi e PC scollegati, sys.live = true. La batteria la aggiorna Ui::battery() (M6).
void clearForLive(Model &m);

}  // namespace cal
