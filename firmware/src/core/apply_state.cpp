#include "apply_state.h"

#include <math.h>
#include <algorithm>
#include <string.h>

#include "avatars.h"
#include "strbuf.h"

namespace cal {

namespace {

// Filtro di deserializzazione: solo i campi del protocollo che il display usa.
const JsonDocument &filter() {
  static JsonDocument f;
  static bool ready = false;
  if (ready) return f;
  for (const char *k : {"t", "v", "time", "tz", "host", "service", "limits", "today"}) f[k] = true;
  // set e full-refresh (core/remote_settings): l'oggetto settings passa intero, così le chiavi
  // sconosciute arrivano al controllo e si rifiutano invece di sparire nel filtro.
  f["id"] = true;
  f["settings"] = true;
  f["ok"] = true;     // ack del servizio a un dismiss
  f["error"] = true;
  // M5 (core/link): abbinamento, autenticazione, permessi. welcome.paired: il servizio ha una K per noi.
  for (const char *k : {"commit", "pkS", "mac", "ns", "session", "name", "tool", "arg", "at", "reason", "paired"}) f[k] = true;
  // Avatar (docs/AVATAR.md §10): predefinito in welcome e state, messaggio avatar (id, crc, data).
  for (const char *k : {"defaultAvatar", "defaultAvatarCrc", "crc", "data"}) f[k] = true;
  // OTA (docs/OTA.md): ota-key {pub, mac}, ota-offer {id, manifest, sig, url, mac}, log-start {id, follow, minutes}.
  // manifest passa intero: i campi sconosciuti si vedono (e si rifiutano) nel controllo, non spariscono qui.
  for (const char *k : {"pub", "manifest", "sig", "url", "follow", "minutes"}) f[k] = true;
  // Più reti (0.8.0, core/wifi_proto): wifi-add {id, ssid, iv, ct}, wifi-forget {id, ssid, mac}.
  for (const char *k : {"ssid", "iv", "ct"}) f[k] = true;
  f["action"] = true;  // 0.9.0: workday {id, action}
  // 0.10.0, gioco della mascotte (core/pet): game {on, level, xp, xpLevelStart, xpNext, energy, tired, wear, celebrate,
  // unlocked}; accessory {id, crc, data}; anchors {avatar, avatarCrc, data}. wear, celebrate e unlocked passano interi.
  for (const char *k : {"on", "level", "xp", "xpLevelStart", "xpNext", "energy", "tired", "wear", "celebrate", "unlocked", "avatar", "avatarCrc"})
    f[k] = true;
  // 0.13.0: theme-offer {id, crc, size, token}, theme-use {id}; 0.14.0: avatar-offer {id, crc, size, token}
  f["size"] = true;
  f["token"] = true;
  // M6 (PROTOCOLLO.md, bye): del riepilogo serve solo summary.off (ms epoch dello spegnimento).
  f["summary"]["off"] = true;
  JsonObject s = f["sessions"].add<JsonObject>();
  for (const char *k : {"id", "name", "state", "detail", "since", "attnAt", "ctx", "subs", "test", "summary", "tasks", "error", "avatar",
                        "avatarCrc"})
    s[k] = true;
  JsonObject l = f["log"].add<JsonObject>();
  for (const char *k : {"t", "name", "text"}) l[k] = true;
  JsonObject sv = f["services"].add<JsonObject>();
  for (const char *k : {"name", "ok", "ms"}) sv[k] = true;
  JsonObject p = f["prs"].add<JsonObject>();
  for (const char *k : {"repo", "n", "title", "checks"}) p[k] = true;
  JsonObject a = f["agenda"].add<JsonObject>();
  for (const char *k : {"t", "title"}) a[k] = true;
  ready = true;
  return f;
}

// Copia un testo JSON: i caratteri di controllo (a capo, tab…) diventano spazi, poi troncamento UTF-8.
void text(char *dst, size_t cap, JsonVariantConst v) {
  const char *s = v.is<const char *>() ? v.as<const char *>() : nullptr;
  if (!s) { if (cap) dst[0] = 0; return; }
  char tmp[256];
  size_t n = 0;
  for (; s[n] && n + 1 < sizeof tmp; n++) tmp[n] = static_cast<unsigned char>(s[n]) < 0x20 ? ' ' : s[n];
  tmp[n] = 0;
  scopy(dst, cap, tmp);  // se tmp è stato tagliato a metà carattere, scopy non lo può sapere:
  // tmp è più lungo di ogni campo del modello (max 128), quindi il taglio vero lo fa sempre scopy.
}
template <size_t N>
void text(char (&dst)[N], JsonVariantConst v) { text(dst, N, v); }

bool isNum(JsonVariantConst v) { return v.is<double>() || v.is<int64_t>(); }
int64_t i64(JsonVariantConst v, int64_t def = 0) {
  if (v.is<int64_t>()) return v.as<int64_t>();
  if (v.is<double>()) return static_cast<int64_t>(llround(v.as<double>()));
  return def;
}
double num(JsonVariantConst v, double def = 0) { return isNum(v) ? v.as<double>() : def; }
int clampInt(int64_t v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : static_cast<int>(v); }

SessionState stateFrom(const char *s) {
  if (!s) return SessionState::Idle;
  for (int k = 0; k < static_cast<int>(SessionState::Count); k++)
    if (streq(kStates[k].key, s)) return static_cast<SessionState>(k);
  return SessionState::Idle;  // stato sconosciuto: si mostra come «in pausa» invece di perdere la sessione
}

void decodeSession(JsonObjectConst o, Session &s) {
  memset(&s, 0, sizeof s);
  text(s.id, o["id"]);
  text(s.name, o["name"]);
  s.state = stateFrom(o["state"].as<const char *>());
  text(s.detail, o["detail"]);
  s.since = i64(o["since"]);
  s.hasAttnAt = isNum(o["attnAt"]);
  s.attnAt = s.hasAttnAt ? i64(o["attnAt"]) : 0;
  s.ctx = clampInt(i64(o["ctx"]), 0, 100);  // null (all'inizio) → 0
  s.subs = clampInt(i64(o["subs"]), 0, 99);
  const char *t = o["test"].as<const char *>();
  // 'run' (test in corso) non ha un simbolo nel simulatore: per ora non si mostra.
  s.test = t && streq(t, "ok") ? TestResult::Ok : t && streq(t, "ko") ? TestResult::Ko : TestResult::None;
  text(s.summary, o["summary"]);
  decodeAvatarRef(o["avatar"], o["avatarCrc"], s.avatar);  // assente → '' (l'avatar predefinito)
  if (s.state == SessionState::Error && !s.detail[0] && o["error"].is<const char *>()) {
    // come raise() del simulatore: «StopFailure: rate_limit»
    Sb(s.detail, sizeof s.detail).s("StopFailure: ").s(o["error"].as<const char *>());
  }
  // Task: totale e fatti su tutto l'elenco; si tengono kMaxTasks titoli attorno al primo non fatto.
  JsonArrayConst tasks = o["tasks"].as<JsonArrayConst>();
  int total = 0, done = 0, firstOpen = -1;
  for (JsonVariantConst t : tasks) {
    JsonArrayConst pair = t.as<JsonArrayConst>();
    const int st = clampInt(i64(pair[1]), 0, 2);
    if (st == 2) done++;
    else if (firstOpen < 0) firstOpen = total;
    total++;
  }
  int start = 0;
  if (total > kMaxTasks) {
    start = firstOpen < 0 ? total - kMaxTasks : firstOpen - 1;
    if (start < 0) start = 0;
    if (start > total - kMaxTasks) start = total - kMaxTasks;
  }
  int k = 0;
  for (JsonVariantConst t : tasks) {
    if (k - start >= kMaxTasks) break;
    if (k >= start) {
      JsonArrayConst pair = t.as<JsonArrayConst>();
      Task &dst = s.tasks[s.nTasks++];
      text(dst.name, pair[0]);
      dst.status = static_cast<TaskStatus>(clampInt(i64(pair[1]), 0, 2));
    }
    k++;
  }
  s.total = total;
  s.done = done;
}

}  // namespace

bool parseMessage(const char *data, size_t len, JsonDocument &doc) {
  if (!data || !len || len > kMaxFrame) return false;
  const DeserializationError e =
      deserializeJson(doc, data, len, DeserializationOption::Filter(filter()), DeserializationOption::NestingLimit(6));
  return !e && doc.is<JsonObject>();
}

MsgType msgType(JsonVariantConst root) {
  if (i64(root["v"], -1) != kProtoVersion) return MsgType::Unknown;
  const char *t = root["t"].as<const char *>();
  if (!t) return MsgType::Unknown;
  if (streq(t, "welcome")) return MsgType::Welcome;
  if (streq(t, "state")) return MsgType::State;
  if (streq(t, "bye")) return MsgType::Bye;
  if (streq(t, "set")) return MsgType::Set;
  if (streq(t, "full-refresh")) return MsgType::FullRefresh;
  if (streq(t, "ack")) return MsgType::Ack;
  if (streq(t, "pair-begin")) return MsgType::PairBegin;
  if (streq(t, "pair-reveal")) return MsgType::PairReveal;
  if (streq(t, "pair-confirm")) return MsgType::PairConfirm;
  if (streq(t, "pair-abort")) return MsgType::PairAbort;
  if (streq(t, "auth-challenge")) return MsgType::AuthChallenge;
  if (streq(t, "auth-ok")) return MsgType::AuthOk;
  if (streq(t, "auth-fail")) return MsgType::AuthFail;
  if (streq(t, "perm-request")) return MsgType::PermRequest;
  if (streq(t, "perm-close")) return MsgType::PermClose;
  if (streq(t, "avatar")) return MsgType::Avatar;
  if (streq(t, "ota-key")) return MsgType::OtaKey;
  if (streq(t, "ota-offer")) return MsgType::OtaOffer;
  if (streq(t, "ota-cancel")) return MsgType::OtaCancel;
  if (streq(t, "log-start")) return MsgType::LogStart;
  if (streq(t, "log-stop")) return MsgType::LogStop;
  if (streq(t, "wifi-add")) return MsgType::WifiAdd;
  if (streq(t, "wifi-forget")) return MsgType::WifiForget;
  if (streq(t, "workday")) return MsgType::Workday;
  if (streq(t, "game")) return MsgType::Game;
  if (streq(t, "accessory")) return MsgType::Accessory;
  if (streq(t, "anchors")) return MsgType::Anchors;
  if (streq(t, "theme-offer")) return MsgType::ThemeOffer;
  if (streq(t, "theme-use")) return MsgType::ThemeUse;
  if (streq(t, "avatar-offer")) return MsgType::AvatarOffer;
  return MsgType::Unknown;
}

bool decodeWelcome(JsonVariantConst root, WelcomeMsg &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::Welcome) return false;
  out.time = i64(root["time"]);
  text(out.tz, root["tz"]);
  text(out.host, root["host"]);
  text(out.service, root["service"]);
  out.hasPaired = root["paired"].is<bool>();
  out.paired = out.hasPaired && root["paired"].as<bool>();
  out.hasDefAvatar = decodeAvatarRef(root["defaultAvatar"], root["defaultAvatarCrc"], out.defAvatar);
  return true;
}

bool decodeState(JsonVariantConst root, StateMsg &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::State) return false;
  if (!isNum(root["time"])) return false;  // senza l'istantanea non si possono convertire i tempi
  out.time = i64(root["time"]);

  for (JsonVariantConst v : root["sessions"].as<JsonArrayConst>()) {
    if (!v.is<JsonObjectConst>()) continue;
    if (out.nSessions >= kMaxSessions) { out.sessionsDropped++; continue; }
    decodeSession(v.as<JsonObjectConst>(), out.sessions[out.nSessions++]);
  }

  JsonObjectConst L = root["limits"].as<JsonObjectConst>();  // null o assente → oggetto vuoto
  out.limits.hasH5 = isNum(L["h5"]);
  out.limits.h5 = out.limits.hasH5 ? num(L["h5"]) : 0;
  out.limits.h5Reset = out.limits.hasH5 ? i64(L["h5Reset"]) : 0;
  out.limits.hasD7 = isNum(L["d7"]);
  out.limits.d7 = out.limits.hasD7 ? num(L["d7"]) : 0;
  out.limits.d7Reset = out.limits.hasD7 ? i64(L["d7Reset"]) : 0;

  JsonObjectConst T = root["today"].as<JsonObjectConst>();
  out.today.tasks = clampInt(i64(T["tasks"]), 0, 1000000);
  out.today.permDisplay = clampInt(i64(T["permDisplay"]), 0, 1000000);
  out.today.permTerminal = clampInt(i64(T["permTerminal"]), 0, 1000000);
  const int64_t wait = i64(T["waitAvgMs"]);
  // il modello tiene somma e numero (come il simulatore): la media del PC diventa una sola attesa
  out.today.waitSum = wait > 0 ? wait : 0;
  out.today.waitN = wait > 0 ? 1 : 0;
  out.today.linesAdd = static_cast<long>(i64(T["linesAdd"]));
  out.today.linesDel = static_cast<long>(i64(T["linesDel"]));
  out.today.cost = num(T["cost"]);

  for (JsonVariantConst v : root["log"].as<JsonArrayConst>()) {
    if (out.nLog >= kStateLogCap) break;
    if (!v.is<JsonObjectConst>()) continue;
    LogEntry &e = out.log[out.nLog++];
    e.t = i64(v["t"]);
    text(e.name, v["name"]);
    text(e.text, v["text"]);
    e.local = false;
  }
  for (JsonVariantConst v : root["services"].as<JsonArrayConst>()) {
    if (out.nServices >= kServiceCap) break;
    if (!v.is<JsonObjectConst>()) continue;
    Service &s = out.services[out.nServices++];
    text(s.name, v["name"]);
    s.ok = v["ok"].as<bool>();
    s.ms = clampInt(i64(v["ms"]), 0, 1000000);
  }
  for (JsonVariantConst v : root["prs"].as<JsonArrayConst>()) {
    if (out.nPrs >= kPrCap) break;
    if (!v.is<JsonObjectConst>()) continue;
    Pr &p = out.prs[out.nPrs++];
    text(p.repo, v["repo"]);
    p.n = clampInt(i64(v["n"]), 0, 1000000000);
    text(p.title, v["title"]);
    const char *c = v["checks"].as<const char *>();
    p.checks = c && streq(c, "ok") ? PrChecks::Ok : c && streq(c, "ko") ? PrChecks::Ko : PrChecks::Run;
  }
  for (JsonVariantConst v : root["agenda"].as<JsonArrayConst>()) {
    if (out.nAgenda >= kAgendaCap) break;
    if (!v.is<JsonObjectConst>() || !isNum(v["t"])) continue;
    AgendaItem &a = out.agenda[out.nAgenda++];
    a.t = i64(v["t"]);
    text(a.title, v["title"]);
  }
  out.hasDefAvatar = decodeAvatarRef(root["defaultAvatar"], root["defaultAvatarCrc"], out.defAvatar);
  return true;
}

// Log: eventi locali della scheda + quelli del PC, il più recente in testa, al massimo kLogCap.
// 0.14.1, sul posto (prima un buffer statico da 8 960 B): anello raddrizzato, locali compattati in testa, ordine
// deciso con la regola di sempre, poi scrittura dal fondo (non tocca mai un locale ancora da spostare).
static void mergeLog(Log &L, const StateMsg &msg, Ms now) {
  auto mono = [&](int64_t t) -> Ms { return t ? now + (t - msg.time) : 0; };
  std::rotate(L.e, L.e + L.head, L.e + kLogCap);
  L.head = 0;
  int nl = 0;
  for (int k = 0; k < L.n; k++)
    if (L.e[k].local) {
      if (nl != k) L.e[nl] = L.e[k];
      nl++;
    }
  bool fromLocal[kLogCap];
  int n = 0, li = 0, pi = 0;
  while (n < kLogCap && (li < nl || pi < msg.nLog)) {
    const bool loc = li < nl && (pi >= msg.nLog || L.e[li].t >= mono(msg.log[pi].t));
    fromLocal[n++] = loc;
    if (loc) li++;
    else pi++;
  }
  for (int w = n - 1; w >= 0; w--) {
    if (fromLocal[w]) {
      if (--li != w) L.e[w] = L.e[li];
    } else {
      --pi;
      L.e[w] = msg.log[pi];
      L.e[w].t = mono(msg.log[pi].t);
    }
  }
  L.n = n;
}

void applyState(Model &m, const StateMsg &msg, Ms now) {
  auto mono = [&](int64_t t) -> Ms { return t ? now + (t - msg.time) : 0; };

  m.nSessions = msg.nSessions;
  for (int k = 0; k < kMaxSessions; k++) {
    Session &s = m.sessions[k];
    if (k >= msg.nSessions) { memset(&s, 0, sizeof s); continue; }
    s = msg.sessions[k];
    s.since = mono(s.since);
    if (s.hasAttnAt) s.attnAt = mono(s.attnAt);
    s.ticks = 0;
  }

  m.limits = msg.limits;
  if (m.limits.hasH5) m.limits.h5Reset = mono(m.limits.h5Reset);
  if (m.limits.hasD7) m.limits.d7Reset = mono(m.limits.d7Reset);

  m.stats = msg.today;

  mergeLog(m.log, msg, now);

  m.nServices = msg.nServices;
  for (int k = 0; k < msg.nServices; k++) m.services[k] = msg.services[k];
  m.nPrs = msg.nPrs;
  for (int k = 0; k < msg.nPrs; k++) m.prs[k] = msg.prs[k];
  m.nAgenda = msg.nAgenda;
  for (int k = 0; k < msg.nAgenda; k++) {
    m.agenda[k] = msg.agenda[k];
    m.agenda[k].t = mono(m.agenda[k].t);
  }

  if (msg.hasDefAvatar) m.set.defAvatar = msg.defAvatar;

  m.sys.pc = true;
  m.sys.pcOff = false;
  m.sys.lastPc = now;
}

void clearForLive(Model &m) {
  m.nSessions = 0;
  memset(m.sessions, 0, sizeof m.sessions);
  m.nArchived = 0;  // con la rete l'archivio lo tiene il servizio
  memset(&m.log, 0, sizeof m.log);
  memset(&m.stats, 0, sizeof m.stats);
  memset(&m.limits, 0, sizeof m.limits);  // hasH5 = hasD7 = false
  m.nServices = m.nPrs = m.nAgenda = m.nNets = 0;
  m.nSaved = 0;  // reti salvate: dal vault, con netSaved()
  m.ui.sel = 0;
  m.ui.selUntil = 0;
  m.ui.page = 0;
  Sys &y = m.sys;
  y.live = true;
  y.pair = PairState::None;  // dal vault della scheda, dopo goLive
  y.authed = false;
  y.pairHost[0] = 0;
  m.nPerms = 0;
  y.wifi = false;
  y.ssid[0] = 0;
  y.rssi = 0;
  y.ip[0] = 0;
  y.pc = false;
  y.lastPc = 0;
  y.apPass[0] = 0;
  y.pcHost[0] = 0;
  // Gioco della mascotte: niente riassunto finto; arriva dal servizio (game) o dall'ultimo salvato (core/pet).
  memset(&m.pet, 0, sizeof m.pet);
  // Batteria: la scrive Ui::battery() dall'ADC (M6); USB e sensori restano quelli finti di newModel.
}

}  // namespace cal
