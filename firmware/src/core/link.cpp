#include "link.h"

#include <string.h>

#include "apply_state.h"
#include "crypto.h"
#include "strbuf.h"

namespace cal {

namespace {

const char *str(JsonVariantConst v) { return v.is<const char *>() ? v.as<const char *>() : nullptr; }

// Esadecimale minuscolo di esattamente n byte.
bool hexField(JsonVariantConst v, uint8_t *out, size_t n) { return cry::fromHex(str(v), out, n); }

// Testo di 1..max byte (max = 0: anche vuoto ammesso fino a maxEmpty).
bool textField(JsonVariantConst v, size_t minLen, size_t maxLen) {
  const char *s = str(v);
  if (!s) return false;
  const size_t n = strlen(s);
  return n >= minLen && n <= maxLen;
}

bool isLowerHex(const char *s, size_t n) {
  if (!s || strlen(s) != n) return false;
  for (size_t k = 0; k < n; k++)
    if (!((s[k] >= '0' && s[k] <= '9') || (s[k] >= 'a' && s[k] <= 'f'))) return false;
  return true;
}

}  // namespace

Link::Link() : rnd_(nullptr), rctx_(nullptr), hasKey_(false), pairActive_(false), pairRevealed_(false), pairReady_(false),
               pairUntil_(0), challenged_(false), authed_(false), seenNext_(0) {
  board_[0] = 0;
  pairId_[0] = 0;
  memset(K_, 0, sizeof K_);
  memset(seen_, 0, sizeof seen_);
}

void Link::begin(const char *board, RandFn rnd, void *rctx) {
  scopy(board_, board);
  rnd_ = rnd;
  rctx_ = rctx;
}

void Link::setKey(const uint8_t *K) {
  hasKey_ = K != nullptr;
  if (K) memcpy(K_, K, 32);
  else cry::wipe(K_, sizeof K_);
  authed_ = false;
  challenged_ = false;
}

void Link::ev(LinkEvType t, const char *text, LinkOut &o) {
  LinkEvent e;
  memset(&e, 0, sizeof e);
  e.type = t;
  scopy(e.text, text ? text : "");
  o.event(e);
}

void Link::reject(const char *why, LinkOut &o) { ev(LinkEvType::Rejected, why, o); }

void Link::sendJson(JsonDocument &d, LinkOut &o) {
  char buf[400];
  const size_t n = serializeJson(d, buf, sizeof buf);
  if (n && n < sizeof buf) o.send(buf, n);
}

void Link::endPair() {
  pairActive_ = pairRevealed_ = pairReady_ = false;
  pairId_[0] = 0;
  pairUntil_ = 0;
  cry::wipe(skD_, sizeof skD_);
  cry::wipe(Kc_, sizeof Kc_);
  cry::wipe(T_, sizeof T_);
}

void Link::sendAbort(const char *reason, LinkOut &o) {
  JsonDocument d;
  d["t"] = "pair-abort";
  d["v"] = kProtoVersion;
  d["id"] = static_cast<const char *>(pairId_);
  d["reason"] = reason;
  sendJson(d, o);
}

bool Link::seen(const char *id) const {
  for (int k = 0; k < kSeenIds; k++)
    if (seen_[k][0] && streq(seen_[k], id)) return true;
  return false;
}

void Link::remember(const char *id) {
  if (seen(id)) return;
  scopy(seen_[seenNext_], id);
  seenNext_ = (seenNext_ + 1) % kSeenIds;
}

void Link::onConnected() {
  authed_ = challenged_ = false;
  cry::wipe(SK_, sizeof SK_);
  memset(seen_, 0, sizeof seen_);  // con SK nuova il servizio rimanda le richieste ancora aperte
  seenNext_ = 0;
  if (pairActive_) endPair();
}

void Link::onDisconnected(LinkOut &o) {
  const bool was = pairActive_;
  authed_ = challenged_ = false;
  cry::wipe(SK_, sizeof SK_);
  if (was) {
    endPair();
    ev(LinkEvType::PairAbort, "connessione", o);
  }
}

void Link::onWelcome(bool hasPaired, bool paired, LinkOut &o) {
  if (!hasPaired) return;  // servizio senza M5: niente da confrontare
  if (paired && !hasKey_) ev(LinkEvType::Redo, "il PC ha una chiave, il display no", o);
  else if (!paired && hasKey_) ev(LinkEvType::Redo, "il display ha una chiave, il PC no", o);
}

bool Link::onMessage(JsonVariantConst r, Ms now, LinkOut &o) {
  switch (msgType(r)) {
    case MsgType::PairBegin: onPairBegin(r, now, o); return true;
    case MsgType::PairReveal: onPairReveal(r, o); return true;
    case MsgType::PairConfirm: onPairConfirm(r, o); return true;
    case MsgType::PairAbort: {
      const char *id = str(r["id"]);
      if (!pairActive_ || !id || !streq(id, pairId_)) { reject("pair-abort per un abbinamento che non c'è", o); return true; }
      char why[32];
      scopy(why, str(r["reason"]) ? str(r["reason"]) : "servizio");
      endPair();
      ev(LinkEvType::PairAbort, why, o);
      return true;
    }
    case MsgType::AuthChallenge: onAuthChallenge(r, o); return true;
    case MsgType::AuthOk: onAuthOk(r, o); return true;
    case MsgType::AuthFail:
      authed_ = challenged_ = false;
      cry::wipe(SK_, sizeof SK_);
      ev(LinkEvType::Redo, str(r["reason"]) ? str(r["reason"]) : "il PC non riconosce il display", o);
      return true;
    case MsgType::PermRequest: onPermRequest(r, o); return true;
    case MsgType::PermClose: {
      // Con mac (M5 §3): chi sta in rete non può far sparire una richiesta dal display.
      const char *id = str(r["id"]), *reason = str(r["reason"]);
      uint8_t mac[16], want[16];
      if (!authed_) { reject("perm-close senza autenticazione", o); return true; }
      if (!isLowerHex(id, m5::kReqIdHex) || !textField(r["reason"], 1, 31)) { reject("perm-close con campi non validi", o); return true; }
      if (!hexField(r["mac"], mac, 16)) { reject("perm-close senza mac", o); return true; }
      if (!m5::closeMac(SK_, id, reason, want) || !cry::ctEqual(mac, want, 16)) { reject("perm-close con mac sbagliato", o); return true; }
      remember(id);
      LinkEvent e;
      memset(&e, 0, sizeof e);
      e.type = LinkEvType::PermClose;
      scopy(e.id, id);
      scopy(e.text, reason);
      o.event(e);
      return true;
    }
    default:
      return false;
  }
}

void Link::onPairBegin(JsonVariantConst r, Ms now, LinkOut &o) {
  uint8_t c[32];
  if (!textField(r["id"], 1, kPairIdLen - 1) || !hexField(r["commit"], c, 32)) { reject("pair-begin non valido", o); return; }
  // Un solo abbinamento per display: uno nuovo prende il posto di quello in corso.
  endPair();
  scopy(pairId_, str(r["id"]));
  memcpy(commit_, c, 32);
  rnd_(skD_, 32, rctx_);
  cry::x25519Base(pkD_, skD_);
  pairActive_ = true;
  pairUntil_ = now + kPairLinkMs;
  char pk[65];
  cry::toHex(pkD_, 32, pk);
  JsonDocument d;
  d["t"] = "pair-key";
  d["v"] = kProtoVersion;
  d["id"] = static_cast<const char *>(pairId_);
  d["pkD"] = static_cast<const char *>(pk);
  sendJson(d, o);
}

void Link::onPairReveal(JsonVariantConst r, LinkOut &o) {
  const char *id = str(r["id"]);
  uint8_t pkS[32], c[32], Z[32];
  if (!pairActive_ || pairRevealed_ || !id || !streq(id, pairId_)) { reject("pair-reveal fuori sequenza", o); return; }
  if (!hexField(r["pkS"], pkS, 32)) { reject("pair-reveal non valido", o); return; }
  // Impegno: chi sta in mezzo non può scegliere pkS dopo aver visto pkD.
  m5::commit(pkS, c);
  if (!cry::ctEqual(c, commit_, 32)) {
    sendAbort("impegno", o);
    endPair();
    ev(LinkEvType::PairAbort, "impegno", o);
    return;
  }
  if (!cry::x25519(Z, skD_, pkS)) {
    sendAbort("chiave", o);
    endPair();
    ev(LinkEvType::PairAbort, "chiave", o);
    return;
  }
  cry::wipe(skD_, sizeof skD_);
  m5::transcript(board_, commit_, pkD_, pkS, T_);
  m5::pairKey(Z, T_, Kc_);
  char code[7];
  m5::sasText(m5::sas(Z, T_), code);
  cry::wipe(Z, sizeof Z);
  pairRevealed_ = true;
  ev(LinkEvType::PairCode, code, o);
}

void Link::onPairConfirm(JsonVariantConst r, LinkOut &o) {
  const char *id = str(r["id"]);
  uint8_t mac[16], want[16];
  if (!pairActive_ || !pairRevealed_ || !id || !streq(id, pairId_)) { reject("pair-confirm fuori sequenza", o); return; }
  if (!hexField(r["mac"], mac, 16)) { reject("pair-confirm non valido", o); return; }
  m5::pairConfirmMac(Kc_, T_, want);
  if (!cry::ctEqual(mac, want, 16)) {
    sendAbort("mac", o);
    endPair();
    ev(LinkEvType::PairAbort, "mac", o);
    return;
  }
  pairReady_ = true;
  ev(LinkEvType::PairReady, "", o);
}

bool Link::confirmPair(LinkOut &o) {
  if (!pairReady()) return false;
  uint8_t mac[16];
  char hex[33];
  m5::pairDoneMac(Kc_, T_, mac);
  cry::toHex(mac, 16, hex);
  JsonDocument d;
  d["t"] = "pair-done";
  d["v"] = kProtoVersion;
  d["id"] = static_cast<const char *>(pairId_);
  d["mac"] = static_cast<const char *>(hex);
  sendJson(d, o);
  // Un nuovo abbinamento sostituisce il vecchio; l'autenticazione riparte con la sfida del servizio.
  memcpy(K_, Kc_, 32);
  hasKey_ = true;
  authed_ = challenged_ = false;
  cry::wipe(SK_, sizeof SK_);
  endPair();
  ev(LinkEvType::PairDone, "", o);
  return true;
}

void Link::cancelPair(const char *reason, LinkOut &o) {
  if (!pairActive_) return;
  sendAbort(reason, o);
  endPair();
  ev(LinkEvType::PairAbort, reason, o);
}

void Link::tick(Ms now, LinkOut &o) {
  if (pairActive_ && now > pairUntil_) cancelPair("tempo", o);
}

void Link::onAuthChallenge(JsonVariantConst r, LinkOut &o) {
  uint8_t ns[16];
  if (!hexField(r["ns"], ns, 16)) { reject("auth-challenge non valido", o); return; }
  if (!hasKey_) {
    ev(LinkEvType::Redo, "il PC chiede l'autenticazione, il display non ha la chiave", o);
    return;
  }
  memcpy(ns_, ns, 16);
  rnd_(nd_, 16, rctx_);
  uint8_t mac[16];
  authed_ = false;
  if (!m5::authMac(K_, board_, ns_, nd_, mac) || !m5::sessionKey(K_, ns_, nd_, SK_)) {
    // Un mac sbagliato darebbe auth-fail e un falso «da rifare»: nessuna risposta, la connessione si rifà.
    challenged_ = false;
    cry::wipe(SK_, sizeof SK_);
    ev(LinkEvType::AuthRetry, "auth-challenge: mac non calcolato", o);
    return;
  }
  challenged_ = true;
  char ndh[33], mh[33];
  cry::toHex(nd_, 16, ndh);
  cry::toHex(mac, 16, mh);
  JsonDocument d;
  d["t"] = "auth";
  d["v"] = kProtoVersion;
  d["nd"] = static_cast<const char *>(ndh);
  d["mac"] = static_cast<const char *>(mh);
  sendJson(d, o);
}

void Link::onAuthOk(JsonVariantConst r, LinkOut &o) {
  uint8_t mac[16], want[16];
  if (!challenged_ || !hasKey_) { reject("auth-ok senza sfida", o); return; }
  if (!hexField(r["mac"], mac, 16)) { reject("auth-ok non valido", o); return; }
  const bool computed = m5::authOkMac(K_, board_, ns_, nd_, want);
  challenged_ = false;
  if (!computed) {  // non si sa se il mac è giusto: non è un abbinamento da rifare
    cry::wipe(SK_, sizeof SK_);
    ev(LinkEvType::AuthRetry, "auth-ok: mac non calcolato", o);
    return;
  }
  if (!cry::ctEqual(mac, want, 16)) {
    authed_ = false;
    cry::wipe(SK_, sizeof SK_);
    ev(LinkEvType::Redo, "auth-ok con un mac sbagliato", o);
    return;
  }
  authed_ = true;
  ev(LinkEvType::Authed, "", o);
}

void Link::onPermRequest(JsonVariantConst r, LinkOut &o) {
  // Prima di mostrarla: connessione autenticata, campi nei limiti, mac con SK, id mai visto.
  if (!authed_) { reject("perm-request senza autenticazione", o); return; }
  const char *id = str(r["id"]), *session = str(r["session"]), *name = str(r["name"]), *tool = str(r["tool"]),
             *arg = str(r["arg"]);
  uint8_t mac[16], want[16];
  if (!isLowerHex(id, m5::kReqIdHex) || !textField(r["session"], 1, kIdLen - 1) || !textField(r["name"], 0, m5::kNameMax) ||
      !textField(r["tool"], 1, m5::kToolMax) || !textField(r["arg"], 0, m5::kArgMax) || !hexField(r["mac"], mac, 16)) {
    reject("perm-request con campi non validi", o);
    return;
  }
  int64_t at = 0;
  if (r["at"].is<int64_t>()) at = r["at"].as<int64_t>();
  if (at <= 0) { reject("perm-request senza at", o); return; }
  if (!m5::reqMac(SK_, id, session, name, tool, arg, at, want) || !cry::ctEqual(mac, want, 16)) {
    reject("perm-request con mac sbagliato", o);
    return;
  }
  if (seen(id)) { reject("perm-request ripetuta", o); return; }
  remember(id);
  LinkEvent e;
  memset(&e, 0, sizeof e);
  e.type = LinkEvType::PermRequest;
  scopy(e.id, id);
  PermReq &q = e.req;
  scopy(q.id, id);
  scopy(q.session, session);
  scopy(q.name, name);  // nel mac (§3): il nome mostrato è quello firmato dal servizio
  scopy(q.tool, tool);
  scopy(q.arg, arg);
  q.at = at;
  o.event(e);
}

bool Link::reply(const char *id, bool allow, LinkOut &o) {
  if (!authed_ || !isLowerHex(id, m5::kReqIdHex)) return false;
  uint8_t nonce[16], mac[16];
  char nh[33], mh[33];
  rnd_(nonce, 16, rctx_);
  cry::toHex(nonce, 16, nh);
  const char *decision = allow ? "allow" : "deny";
  if (!m5::repMac(SK_, id, decision, nh, mac)) return false;
  cry::toHex(mac, 16, mh);
  JsonDocument d;
  d["t"] = "perm-reply";
  d["v"] = kProtoVersion;
  d["id"] = id;
  d["decision"] = decision;
  d["nonce"] = static_cast<const char *>(nh);
  d["mac"] = static_cast<const char *>(mh);
  sendJson(d, o);
  remember(id);
  return true;
}

void Link::unpair(LinkOut &o) {
  if (authed_) {
    uint8_t mac[16];
    char mh[33];
    if (m5::unpairMac(SK_, board_, mac)) {
      cry::toHex(mac, 16, mh);
      JsonDocument d;
      d["t"] = "unpair";
      d["v"] = kProtoVersion;
      d["mac"] = static_cast<const char *>(mh);
      sendJson(d, o);
    }
  }
  setKey(nullptr);
  cry::wipe(SK_, sizeof SK_);
  ev(LinkEvType::Unpaired, "", o);
}

}  // namespace cal
