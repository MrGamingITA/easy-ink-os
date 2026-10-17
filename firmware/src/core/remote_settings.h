// Impostazioni remote (docs/PROTOCOLLO.md, «Impostazioni remote»): set, full-refresh, settings, ack,
// più il salvataggio delle impostazioni in NVS (brief §8.7). Logica pura, provata in native.
//
// Chi fa cosa sulla scheda:
//   task di rete  parseMessage + decodeSet/decodeFullRefresh; un set non valido riceve subito l'ack
//                 con ok = false (non c'è niente da applicare); gli altri vanno al loop come eventi;
//   loop          Ui::applyRemote (le stesse conseguenze delle voci del menu), poi chiede alla rete di
//                 mandare settings e ack (encodeSettings, encodeAck): il modello si tocca solo lì.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "model.h"

namespace cal {

// id del comando: 1–32 caratteri nello schema. Qui si contano i byte (il servizio usa «c<n>», ASCII):
// un id più lungo di 32 byte non si può ripetere in un ack valido e il comando si ignora.
const int kCmdIdLen = 33;
const int kAckErrLen = 96;  // error: max 95 byte UTF-8
extern const char *const kPermOnRemoteError;  // «permOn: si accende solo dal dispositivo» (brief §7)

struct RemoteCmd {
  bool fullRefresh;      // false = set
  char id[kCmdIdLen];
  bool ok;               // set: false = rifiutato (error spiega perché), niente da applicare
  char error[kAckErrLen];
  SettingsPatch patch;   // set valido: le chiavi da cambiare
};

// set: false se l'id manca, è vuoto o supera 32 byte (non si può rispondere: niente ack).
// Altrimenti true, e out.ok dice se il comando si può applicare. Regole, in quest'ordine:
//   1. permOn: true → ok = false con kPermOnRemoteError (da remoto si può solo spegnere);
//   2. settings assente, non oggetto o vuoto → ok = false;
//   3. chiave sconosciuta o valore del tipo sbagliato/fuori elenco → ok = false, niente applicato.
bool decodeSet(JsonVariantConst root, RemoteCmd &out);
// full-refresh: false se l'id non va (come sopra).
bool decodeFullRefresh(JsonVariantConst root, RemoteCmd &out);
// 0.9.0: un set valido nella forma va ancora controllato con le impostazioni attuali (lo fa il loop prima di
// applicarlo): workStart e workEnd, anche se ne arriva uno solo, devono dare inizio prima della fine. false = da
// rifiutare con ack negativo, motivo in err.
bool checkPatch(const SettingsPatch &p, const Settings &cur, char *err, size_t cap);

// ---------- 0.9.0: workday (servizio → display, solo autenticato) ----------
// {"t":"workday","v":1,"id":<1–32>,"action":"start"|"stop"}. false se l'id non va (niente ack); ok = false se action
// manca o non è valida (ack negativo).
struct WorkdayCmd { char id[kCmdIdLen]; bool ok; bool start; char error[kAckErrLen]; };
bool decodeWorkday(JsonVariantConst root, WorkdayCmd &out);

// Testo dei valori come nel protocollo.
const char *speedKey(SpeedMode s);  // 'profilo' / 'massima'
const char *muteKey(MuteKind k);    // 'off' / '1h' / 'night'

// JSON compatto, stesso ordine delle chiavi di protocol/examples. Restituiscono la lunghezza scritta
// (senza terminatore), 0 se non ci sta in cap.
// remote: true = il display accetta set e full-refresh (M5: solo dopo auth-ok).
size_t encodeSettings(const RemoteValues &v, char *out, size_t cap, bool remote = true);
size_t encodeAck(const char *id, bool ok, const char *error, char *out, size_t cap);

// ---------- Archivia (dismiss → ack) ----------
// dismiss (display → servizio): {"t":"dismiss","v":1,"id":<richiesta, 1–32 byte>,"session":<session_id>}.
size_t encodeDismiss(const char *id, const char *session, char *out, size_t cap);
// ack (servizio → display): false se manca l'id o ok non è un booleano. error ('' se assente) max 95 byte.
struct AckMsg { char id[kCmdIdLen]; bool ok; char error[kAckErrLen]; };
bool decodeAck(JsonVariantConst root, AckMsg &out);

// ---------- NVS (Preferences, namespace «calamaio-set», chiave «v1») ----------
// Le impostazioni remote (risparmio energetico compreso, bit 6 dei flag) più gli hook diretti, in 16 byte con versione e somma
// di controllo. Il silenzio si salva come tipo + fine in ms epoch UTC (0 = sconosciuta: senza l'ora vera
// non si sa quando finisce, e dopo un riavvio non si ripristina). 0.10.0: il gioco della mascotte nel byte 14 (0/1),
// libero fino alla 0.9.0: i blocchi vecchi danno il gioco spento.
const size_t kStoredLen = 16;
const uint8_t kStoredVersion = 1;
struct StoredMute { MuteKind kind; int64_t endEpochMs; };
size_t packSettings(const Settings &s, const StoredMute &mute, uint8_t *out, size_t cap);
// false (e s intatto) se il blocco è di un'altra versione, corto, corrotto o con valori fuori elenco.
// Con true copia le impostazioni in s; il silenzio resta spento e torna in mute, da ripristinare con
// restoreMute quando l'ora è valida.
bool unpackSettings(const uint8_t *in, size_t len, Settings &s, StoredMute &mute);
// Silenzio salvato prima del riavvio: si riaccende solo se non è scaduto (e se mancano al massimo 24 h:
// oltre, l'ora salvata o quella attuale è sbagliata). true se l'ha riacceso.
const int64_t kMaxRestoreMs = 24LL * 3600000;
bool restoreMute(Settings &s, const StoredMute &mute, int64_t nowEpochMs, Ms now);

}  // namespace cal
