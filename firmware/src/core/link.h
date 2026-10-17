// Sicurezza della connessione con il servizio (docs/M5-PERMESSI.md §1–3): abbinamento con X25519 e codice
// a 6 cifre, sfida e risposta a ogni connessione, richieste di permesso verificate e risposte firmate.
// Logica pura, provata in native con i vettori di protocol/vectors/crypto-vectors.json.
//
// Chi la usa: il task di rete. Le entrate sono i messaggi già letti (parseMessage) e i comandi
// dell'interfaccia; le uscite passano da LinkOut: frame da mandare al servizio ed eventi per il loop
// (codice da mostrare, abbinato, autenticato, richiesta verificata…). La chiave K la salva chi riceve
// PairDone (e la cancella con Unpaired): Link la tiene solo in memoria.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "model.h"

namespace cal {

const int kPairIdLen = 33;              // id di pair-begin: 1–32 caratteri (scelto dal servizio)
const Ms kPairLinkMs = kPairMs + 5000;  // finestra lato display: il servizio chiude prima, a 3 minuti
const int kSeenIds = 16;                // id di permesso già visti su questa connessione (repliche)

typedef void (*RandFn)(uint8_t *out, size_t n, void *ctx);

enum class LinkEvType : uint8_t {
  PairCode,     // text = 6 cifre da mostrare
  PairReady,    // pair-confirm valido: il display aspetta la conferma fisica (2×)
  PairDone,     // abbinato: key() è la nuova K, da salvare
  PairAbort,    // text = motivo («codice», «tempo», «display», «connessione», «impegno», «mac», …)
  Authed,       // auth-ok valido: permessi e set remoti ammessi su questa connessione
  Redo,         // abbinamento da rifare: text = perché
  PermRequest,  // req verificata
  PermClose,    // id, text = reason
  Unpaired,     // K cancellata (Dimentica abbinamento): da togliere dal vault
  Rejected,     // messaggio scartato: text = perché (solo per il log)
  AuthRetry,    // mac di auth non calcolato (memoria): niente auth né «da rifare», chi usa Link chiude e riprova
};

struct LinkEvent {
  LinkEvType type;
  char text[64];
  char id[kReqIdLen];
  PermReq req;  // solo PermRequest (since = 0: lo decide l'interfaccia)
};

class LinkOut {
 public:
  virtual ~LinkOut() {}
  virtual void send(const char *json, size_t n) = 0;
  virtual void event(const LinkEvent &e) = 0;
};

class Link {
 public:
  Link();
  // board = nome mandato in hello (calamaio-3f2a): entra nel transcript e nei mac di auth.
  void begin(const char *board, RandFn rnd, void *rctx);
  void setKey(const uint8_t *K);  // nullptr = nessun abbinamento
  bool hasKey() const { return hasKey_; }
  const uint8_t *key() const { return K_; }
  bool authed() const { return authed_; }
  const uint8_t *sessionKey() const { return SK_; }  // SK della connessione (valida solo con authed())
  bool pairing() const { return pairActive_; }
  bool pairReady() const { return pairActive_ && pairReady_; }

  // Connessione nuova (dopo hello): auth e abbinamento della connessione precedente non valgono più.
  void onConnected();
  // Connessione chiusa: un abbinamento in corso finisce (PairAbort «connessione»).
  void onDisconnected(LinkOut &o);
  // welcome.paired (se c'è) diverso da hasKey(): Redo.
  void onWelcome(bool hasPaired, bool paired, LinkOut &o);
  // Messaggi M5 del servizio. false se root non è un messaggio M5 (lo gestisce qualcun altro).
  bool onMessage(JsonVariantConst root, Ms now, LinkOut &o);

  // Dall'interfaccia.
  bool confirmPair(LinkOut &o);                     // 2× dopo PairReady: pair-done e PairDone
  void cancelPair(const char *reason, LinkOut &o);  // pair-abort al servizio (lunga sul display, tempo)
  bool reply(const char *id, bool allow, LinkOut &o);  // perm-reply con nonce casuale e mac
  void unpair(LinkOut &o);                          // unpair (se autenticato) e K cancellata
  void tick(Ms now, LinkOut &o);                    // finestra dell'abbinamento

 private:
  void sendJson(JsonDocument &d, LinkOut &o);
  void reject(const char *why, LinkOut &o);
  void ev(LinkEvType t, const char *text, LinkOut &o);
  void endPair();
  void sendAbort(const char *reason, LinkOut &o);
  bool seen(const char *id) const;
  void remember(const char *id);
  void onPairBegin(JsonVariantConst r, Ms now, LinkOut &o);
  void onPairReveal(JsonVariantConst r, LinkOut &o);
  void onPairConfirm(JsonVariantConst r, LinkOut &o);
  void onAuthChallenge(JsonVariantConst r, LinkOut &o);
  void onAuthOk(JsonVariantConst r, LinkOut &o);
  void onPermRequest(JsonVariantConst r, LinkOut &o);

  char board_[32];
  RandFn rnd_;
  void *rctx_;
  bool hasKey_;
  uint8_t K_[32];
  // abbinamento in corso
  bool pairActive_, pairRevealed_, pairReady_;
  char pairId_[kPairIdLen];
  Ms pairUntil_;
  uint8_t skD_[32], pkD_[32], commit_[32], T_[32], Kc_[32];
  // autenticazione
  bool challenged_, authed_;
  uint8_t ns_[16], nd_[16], SK_[32];
  char seen_[kSeenIds][kReqIdLen];
  int seenNext_;
};

}  // namespace cal
