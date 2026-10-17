#include "ota.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include "ed25519.h"
#include "strbuf.h"

namespace cal {
namespace ota {

namespace {

const char *str(JsonVariantConst v) { return v.is<const char *>() ? v.as<const char *>() : nullptr; }

bool lowerHex(const char *s, size_t n) {
  if (!s || strlen(s) != n) return false;
  for (size_t k = 0; k < n; k++)
    if (!((s[k] >= '0' && s[k] <= '9') || (s[k] >= 'a' && s[k] <= 'f'))) return false;
  return true;
}

// Testo di 1..max byte senza caratteri di controllo (0x1F compreso: separa i campi della forma canonica).
bool cleanText(const char *s, size_t maxLen) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (!n || n > maxLen) return false;
  for (size_t k = 0; k < n; k++)
    if (static_cast<unsigned char>(s[k]) < 0x20 || s[k] == 0x7F) return false;
  return true;
}

void say(char *why, size_t cap, const char *a, const char *b = nullptr, const char *c = nullptr) {
  if (!cap) return;
  Sb(why, cap).s(a).s(b).s(c);
}

void decimal(Sb &b, int64_t v) { b.i(static_cast<long long>(v)); }

}  // namespace

const char *phaseKey(Phase p) { return otaPhaseKey(p); }

size_t canonical(const Manifest &m, uint8_t *out, size_t cap) {
  char buf[kCanonMax + 1];
  Sb b(buf, sizeof buf);
  b.s(m.board).ch(0x1F).s(m.device).ch(0x1F).s(m.version).ch(0x1F);
  decimal(b, m.size);
  b.ch(0x1F).s(m.sha256).ch(0x1F);
  decimal(b, m.built);
  if (b.truncated() || b.len() > cap) return 0;
  memcpy(out, buf, b.len());
  return b.len();
}

bool manifestSigned(const Manifest &m, const uint8_t sig[64], const uint8_t pub[32]) {
  uint8_t c[kCanonMax];
  const size_t n = canonical(m, c, sizeof c);
  return n && ed::verify(sig, c, n, pub);
}

bool keyMac(const uint8_t SK[32], const char *pubHex, uint8_t out[16]) {
  cry::Msg m;
  m.str("ota-key").sep().str(pubHex);
  return m.ok() && cry::mac16(SK, m, out);
}

bool offerMac(const uint8_t SK[32], const char *id, const char *sha256Hex, uint8_t out[16]) {
  cry::Msg m;
  m.str("ota-offer").sep().str(id).sep().str(sha256Hex);
  return m.ok() && cry::mac16(SK, m, out);
}

KeyCheck checkKey(JsonVariantConst r, bool authed, const uint8_t SK[32], bool hasSaved, const uint8_t saved[32]) {
  KeyCheck k;
  memset(&k, 0, sizeof k);
  k.verdict = KeyVerdict::Rejected;
  if (!authed) { k.why = "ota-key senza autenticazione"; return k; }
  const char *pub = str(r["pub"]);
  uint8_t mac[16], want[16];
  if (!lowerHex(pub, 64) || !cry::fromHex(pub, k.pub, 32) || !cry::fromHex(str(r["mac"]), mac, 16)) {
    k.why = "ota-key con campi non validi";
    return k;
  }
  if (!keyMac(SK, pub, want) || !cry::ctEqual(mac, want, 16)) { k.why = "ota-key con mac sbagliato"; return k; }
  if (hasSaved) {
    if (cry::ctEqual(k.pub, saved, 32)) { k.verdict = KeyVerdict::Same; return k; }
    k.why = "chiave OTA diversa da quella salvata: vale solo dopo un nuovo abbinamento";
    return k;
  }
  k.verdict = KeyVerdict::Save;
  return k;
}

bool validId(const char *id) {
  if (!id) return false;
  const size_t n = strlen(id);
  if (!n || n > kIdLen - 1) return false;
  for (size_t k = 0; k < n; k++) {
    const char c = id[k];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

bool cancelMac(const uint8_t SK[32], const char *id, uint8_t out[16]) {
  cry::Msg m;
  m.str("ota-cancel").sep().str(id);
  return m.ok() && cry::mac16(SK, m, out);
}

bool checkCancel(JsonVariantConst r, bool authed, const uint8_t SK[32], const char *current, const char **why) {
  const char *id = str(r["id"]);
  uint8_t mac[16], want[16];
  const char *w = nullptr;
  if (!authed) w = "ota-cancel senza autenticazione";
  else if (!validId(id) || !cry::fromHex(str(r["mac"]), mac, 16)) w = "ota-cancel con campi non validi";
  else if (!cancelMac(SK, id, want) || !cry::ctEqual(mac, want, 16)) w = "ota-cancel con mac sbagliato";
  else if (!current || strcmp(current, id) != 0) w = "ota-cancel per un aggiornamento che non è in corso (o già in scrittura)";
  if (why) *why = w;
  return !w;
}

bool PowerReporter::due(const PowerStatus &s, Ms now) {
  const bool first = !sent_;
  const bool work = s.hasWork != last_.hasWork || s.workOn != last_.workOn || (s.workOn && s.workEnd != last_.workEnd);
  const bool now_ = first || work || s.usb != last_.usb || s.cal != last_.cal || ((s.batt < 0) != (last_.batt < 0)) ||
                    (s.batt != last_.batt && now - at_ >= kPowerStatusMs);
  if (!now_) return false;
  sent_ = true;
  last_ = s;
  at_ = now;
  return true;
}

size_t encodePowerStatus(const PowerStatus &s, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "status";
  d["v"] = 1;
  if (s.batt >= 0) d["batt"] = s.batt > 100 ? 100 : s.batt;
  else d["batt"] = nullptr;
  if (s.usb >= 0) d["usb"] = s.usb == 1;
  else d["usb"] = nullptr;
  d["cal"] = s.cal;
  if (s.hasWork) {
    JsonObject w = d["workday"].to<JsonObject>();
    w["on"] = s.workOn;
    if (s.workOn) w["end"] = s.workEnd;
    else w["end"] = nullptr;
  }
  const size_t n = serializeJson(d, out, cap);
  return n && n < cap ? n : 0;
}

bool offerId(JsonVariantConst r, char out[kIdLen]) {
  const char *id = str(r["id"]);
  if (!validId(id)) return false;
  scopy(out, kIdLen, id);
  return true;
}

bool checkOffer(JsonVariantConst r, bool authed, const uint8_t SK[32], bool hasPub, const uint8_t pub[32],
                const Identity &me, Offer &o, char *why, size_t cap) {
  memset(&o, 0, sizeof o);
  if (!offerId(r, o.id)) { say(why, cap, "messaggio non valido: id"); return false; }
  // Il manifesto si legge prima di tutto il resto, così anche un rifiuto riporta total.
  JsonVariantConst m = r["manifest"];
  const char *board = str(m["board"]), *device = str(m["device"]), *version = str(m["version"]), *sha = str(m["sha256"]);
  const bool sizeOk = m["size"].is<uint32_t>() && m["size"].as<uint32_t>() > 0;
  if (sizeOk) o.m.size = m["size"].as<uint32_t>();
  if (!authed) { say(why, cap, "display non autenticato"); return false; }
  const bool built = m["built"].is<int64_t>() && m["built"].as<int64_t>() >= 0;
  if (!m.is<JsonObjectConst>() || m.as<JsonObjectConst>().size() != 6 || !cleanText(board, kBoardLen - 1) ||
      !cleanText(device, kDeviceLen - 1) || !cleanText(version, kVersionLen - 1) || !sizeOk || !lowerHex(sha, kShaHex) || !built) {
    say(why, cap, "manifesto non valido");
    return false;
  }
  uint8_t mac[16], want[16];
  if (!cry::fromHex(str(r["mac"]), mac, 16) || !offerMac(SK, o.id, sha, want) || !cry::ctEqual(mac, want, 16)) {
    say(why, cap, "mac sbagliato");
    return false;
  }
  scopy(o.m.board, board);
  scopy(o.m.device, device);
  scopy(o.m.version, version);
  scopy(o.m.sha256, sha);
  o.m.built = m["built"].as<int64_t>();
  if (!hasPub) { say(why, cap, "nessuna chiave per gli aggiornamenti: manca ota-key dal PC"); return false; }
  if (!lowerHex(str(r["sig"]), 128) || !cry::fromHex(str(r["sig"]), o.sig, 64) || !manifestSigned(o.m, o.sig, pub)) {
    say(why, cap, "firma del manifesto non valida");
    return false;
  }
  if (strcmp(o.m.board, me.board) != 0) { say(why, cap, "aggiornamento per un'altra scheda (", o.m.board, ")"); return false; }
  if (strcmp(o.m.device, me.device) != 0) { say(why, cap, "aggiornamento per un altro dispositivo (", o.m.device, ")"); return false; }
  if (o.m.size > me.maxSize) { say(why, cap, "immagine più grande della partizione"); return false; }
  const char *url = str(r["url"]);
  if (!cleanText(url, kUrlLen - 1) || strncmp(url, "http://", 7) != 0 || strlen(url) < 9) {
    say(why, cap, "indirizzo dell'immagine non valido");
    return false;
  }
  scopy(o.url, url);
  return true;
}

bool conditionsOk(const Conditions &c, char *why, size_t cap, bool *shown) {
  if (shown) *shown = false;
  if (c.busy) { say(why, cap, "aggiornamento già in corso"); return false; }
  if (c.trial) { say(why, cap, "la versione attuale è ancora da confermare: riprova fra 2 minuti"); return false; }
  if (c.permOpen) { say(why, cap, "c'è una richiesta di permesso aperta"); return false; }
  if (c.calibrating) { say(why, cap, "calibrazione della batteria in corso"); return false; }
  if (c.playing) {  // contratto «OTA e partita»: a schermo e nella webapp «rifiutato: partita in corso: …»
    if (shown) *shown = true;
    say(why, cap, "partita in corso: riprova a fine partita");
    return false;
  }
  if (c.usb == UsbPresent::Yes) return true;  // col cavo parte sempre (OTA.md §1.5)
  if (shown) *shown = true;
  if (c.battPct < 0) {
    say(why, cap, c.usb == UsbPresent::No ? "batteria non misurabile e cavo USB staccato"
                                           : "batteria non misurabile e cavo USB non rilevato");
    return false;
  }
  if (c.battPct < kMinBatteryPct) {
    char t[16];
    snprintf(t, sizeof t, "%d%%", c.battPct);
    say(why, cap, "batteria al ", t, ": collega il cavo USB o ricarica fino al 50%");
    return false;
  }
  return true;
}

size_t encodeStatus(const char *id, Phase p, uint32_t done, uint32_t total, const char *error, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "ota-status";
  d["v"] = 1;
  d["id"] = id;
  d["phase"] = phaseKey(p);
  d["done"] = done;
  d["total"] = total;
  if (error && error[0] && (p == Phase::Rejected || p == Phase::Error)) d["error"] = error;
  const size_t n = serializeJson(d, out, cap);
  return n && n < cap ? n : 0;
}

// ---------- Record ----------
size_t packRecord(const Record &r, uint8_t *out, size_t cap) {
  if (cap < kRecordLen) return 0;
  memset(out, 0, kRecordLen);
  size_t k = 0;
  out[k++] = 1;  // versione
  memcpy(out + k, r.id, strnlen(r.id, kIdLen - 1)); k += kIdLen;
  memcpy(out + k, r.from, strnlen(r.from, kVersionLen - 1)); k += kVersionLen;
  memcpy(out + k, r.to, strnlen(r.to, kVersionLen - 1)); k += kVersionLen;
  memcpy(out + k, r.label, strnlen(r.label, 16)); k += 17;
  for (int i = 0; i < 4; i++) out[k++] = uint8_t(r.size >> (8 * i));
  uint32_t sum = 0x0A7A0001u;
  for (size_t i = 0; i < k; i++) sum = sum * 31u + out[i];
  for (int i = 0; i < 4; i++) out[k++] = uint8_t(sum >> (8 * i));
  return k;
}

bool unpackRecord(const uint8_t *in, size_t n, Record &r) {
  memset(&r, 0, sizeof r);
  if (!in || n != kRecordLen || in[0] != 1) return false;
  uint32_t sum = 0x0A7A0001u;
  for (size_t i = 0; i < kRecordLen - 4; i++) sum = sum * 31u + in[i];
  uint32_t got = 0;
  for (int i = 0; i < 4; i++) got |= uint32_t(in[kRecordLen - 4 + i]) << (8 * i);
  if (got != sum) return false;
  size_t k = 1;
  memcpy(r.id, in + k, kIdLen - 1); k += kIdLen;
  memcpy(r.from, in + k, kVersionLen - 1); k += kVersionLen;
  memcpy(r.to, in + k, kVersionLen - 1); k += kVersionLen;
  memcpy(r.label, in + k, 16); k += 17;
  for (int i = 0; i < 4; i++) r.size |= uint32_t(in[k++]) << (8 * i);
  return r.id[0] && r.label[0];
}

// ---------- Updater ----------
void Updater::report(Phase p, UpdaterOut &out, const char *error) {
  out.status(p, done_, offer_.m.size, error);
  out.progress(p, done_, offer_.m.size, error);
}

void Updater::fail(const char *why, UpdaterOut &out) {
  scopy(why_, why);
  if (flash_) flash_->abort();  // l'immagine parziale non si avvia mai (OTA.md §3.5)
  if (src_) src_->close();
  if (recSaved_) out.clearRecord();
  recSaved_ = false;
  if (sha_) { sha_->~Sha256Stream(); sha_ = nullptr; }
  flash_ = nullptr;
  src_ = nullptr;
  phase_ = Phase::Error;
  report(Phase::Error, out, why_);
}

bool Updater::start(const Offer &o, const char *fromVersion, const char *label, Flash &f, Source &s, Ms now, UpdaterOut &out) {
  if (active()) return false;
  offer_ = o;
  memset(&rec_, 0, sizeof rec_);
  scopy(rec_.id, o.id);
  scopy(rec_.from, fromVersion);
  scopy(rec_.to, o.m.version);
  scopy(rec_.label, label ? label : "");
  rec_.size = o.m.size;
  done_ = 0;
  sentStep_ = shownPct_ = -1;
  lastData_ = now;
  recSaved_ = false;
  why_[0] = 0;
  flash_ = nullptr;
  src_ = nullptr;
  sha_ = new (shaBuf_) cry::Sha256Stream();
  phase_ = Phase::Receive;
  char why[kWhyLen] = "";
  if (!f.begin(o.m.size, why, sizeof why)) { fail(why[0] ? why : "partizione OTA non disponibile", out); return false; }
  flash_ = &f;
  if (!s.open(o.url, why, sizeof why)) { fail(why[0] ? why : "download non partito", out); return false; }
  src_ = &s;
  sentStep_ = 0;
  shownPct_ = 0;
  report(Phase::Receive, out);
  return true;
}

Step Updater::step(Ms now, UpdaterOut &out, size_t maxBytes) {
  if (phase_ != Phase::Receive) return phase_ == Phase::Error ? Step::Failed : Step::Idle;
  uint8_t buf[1460];  // un segmento TCP
  size_t got = 0;
  while (got < maxBytes && done_ < offer_.m.size) {
    const size_t want = offer_.m.size - done_ < sizeof buf ? offer_.m.size - done_ : sizeof buf;
    const int n = src_->read(buf, want);
    if (n == 0) break;
    if (n < 0) {
      char t[96];
      snprintf(t, sizeof t, "download interrotto a %lu di %lu byte", (unsigned long)done_, (unsigned long)offer_.m.size);
      fail(t, out);
      return Step::Failed;
    }
    sha_->update(buf, size_t(n));
    if (!flash_->write(buf, size_t(n))) { fail("scrittura nella flash non riuscita", out); return Step::Failed; }
    done_ += uint32_t(n);
    got += size_t(n);
    lastData_ = now;
  }
  if (done_ >= offer_.m.size) return finish(out);
  if (now - lastData_ >= Ms(kStallMs)) { fail("download interrotto: nessun dato da 15 s", out); return Step::Failed; }
  const uint64_t pct = uint64_t(done_) * 100 / offer_.m.size;
  if (int(pct / kStatusStepPct) > sentStep_) {
    sentStep_ = int(pct / kStatusStepPct);
    out.status(Phase::Receive, done_, offer_.m.size, nullptr);
  }
  if (int(pct) > shownPct_) {
    shownPct_ = int(pct);
    out.progress(Phase::Receive, done_, offer_.m.size, nullptr);
  }
  return Step::Busy;
}

Step Updater::finish(UpdaterOut &out) {
  // Dimensione esatta: più dati del manifesto non si leggono mai (want sopra), meno dati sono un errore di read.
  phase_ = Phase::Verify;
  report(Phase::Verify, out);
  uint8_t h[32];
  char hex[65];
  sha_->finish(h);
  sha_->~Sha256Stream();
  sha_ = nullptr;
  cry::toHex(h, 32, hex);
  src_->close();
  src_ = nullptr;
  if (strcmp(hex, offer_.m.sha256) != 0) { fail("SHA-256 diverso dal manifesto", out); return Step::Failed; }
  char why[kWhyLen] = "";
  Flash *f = flash_;
  flash_ = nullptr;  // dopo end() l'handle non vale più: niente abort()
  if (!f->end(why, sizeof why)) {
    char t[kWhyLen];
    Sb(t, sizeof t).s("immagine non valida").s(why[0] ? ": " : "").s(why);
    fail(t, out);
    return Step::Failed;
  }
  phase_ = Phase::Write;
  report(Phase::Write, out);
  recSaved_ = out.saveRecord(rec_);
  if (!f->setBoot()) { fail("partizione di avvio non impostata", out); return Step::Failed; }
  phase_ = Phase::Reboot;
  report(Phase::Reboot, out);
  return Step::Reboot;
}

void Updater::cancel(const char *why, UpdaterOut &out) {
  if (phase_ == Phase::Receive) fail(why, out);
}

// ---------- dopo il riavvio ----------
BootCase bootCase(bool hasRecord, const Record &r, const char *runningLabel, ImgState st) {
  if (st == ImgState::PendingVerify) return BootCase::Trial;
  if (!hasRecord) return BootCase::Normal;
  return runningLabel && strcmp(runningLabel, r.label) == 0 ? BootCase::Confirmed : BootCase::RolledBack;
}

Trial::Act Trial::onAuthed(Ms now) {
  if (!active_) return Act::None;
  active_ = false;
  return now - start_ <= kValidateMs ? Act::MarkValid : Act::Rollback;
}

Trial::Act Trial::tick(Ms now) {
  if (!active_ || now - start_ <= kValidateMs) return Act::None;
  active_ = false;
  return Act::Rollback;
}

}  // namespace ota
}  // namespace cal
