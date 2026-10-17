#include "model.h"

#include <math.h>
#include <string.h>

#include "strbuf.h"

namespace cal {

const char *const kFwVersion = CALAMAIO_FW_VERSION;

const StateInfo kStates[static_cast<int>(SessionState::Count)] = {
    {"working", "LAVORA", "Sta lavorando", 3},
    {"permission", "PERMESSO", "Chiede un permesso", 6},
    {"input", "ATTENDE TE", "Aspetta una risposta", 5},
    {"error", "ERRORE", "Errore API", 4},
    {"compact", "RIORDINA", "Riordina la memoria", 3},
    {"done", "FINITO", "Turno completato", 2},
    {"idle", "IN PAUSA", "Sessione aperta", 1},
};

const char *otaPhaseKey(OtaPhase p) {
  static const char *const k[] = {"", "rifiutato", "ricevo", "verifico", "scrivo", "riavvio", "fatto", "errore"};
  return k[static_cast<int>(p)];
}

int otaPct(const OtaView &o) {
  if (o.phase != OtaPhase::Receive) return 100;
  if (!o.total) return 0;
  const uint64_t p = uint64_t(o.done) * 100 / o.total;
  return p > 100 ? 100 : int(p);
}

const char *poseKey(MascotPose p) {
  static const char *const k[] = {"working", "permission", "input", "done", "error", "idle", "compact", "tired", "stuffy", "run"};
  return k[static_cast<int>(p)];
}

const ControlRow kControlsSingle[6] = {
    {"1 pressione", "Avanti: sessione, voce o pagina successiva"},
    {"2 pressioni", "Apri o conferma"},
    {"3 pressioni", "Menu; dal Menu torna alla Home"},
    {"Lunga 0,7 s", "Indietro; nella Home silenzia per 1 ora"},
    {"Tieni 3 s", "Riposo; un altro gesto lo sveglia"},
    {"Nel permesso", "1 cambia scelta, 2 conferma, lunga annulla"},
};

const char *const kHomeLabel[4] = {"Goccia", "Righe", "Contatori", "Una alla volta"};
const char *const kAnimLabel[4] = {"Auto", "A raffiche", "Continue", "Spente"};
const char *const kSpeedLabel[2] = {"Profilo", "Massima"};
const char *homeKey(HomeStyle h) {
  static const char *const k[] = {"goccia", "righe", "contatori", "una"};
  return k[static_cast<int>(h)];
}
const char *animKey(AnimMode a) {
  static const char *const k[] = {"auto", "raffiche", "continue", "spente"};
  return k[static_cast<int>(a)];
}

bool operator==(const RemoteValues &a, const RemoteValues &b) {
  return a.homeStyle == b.homeStyle && a.anim == b.anim && a.speed == b.speed && a.invert == b.invert && a.rot == b.rot &&
         a.night == b.night && a.permOn == b.permOn && a.sound == b.sound && a.led == b.led && a.mute == b.mute &&
         a.save == b.save && a.workStart == b.workStart && a.workEnd == b.workEnd && a.game == b.game;
}

RemoteValues remoteValues(const Settings &s, Ms now) {
  RemoteValues v;
  v.homeStyle = s.homeStyle;
  v.anim = s.anim;
  v.speed = s.speed;
  v.invert = s.invert;
  v.rot = s.rot;
  v.night = s.night;
  v.permOn = s.permOn;
  v.sound = s.sound;
  v.led = s.led;
  v.mute = s.mute > now ? s.muteKind : MuteKind::None;  // come muted(): finito il silenzio è «off»
  v.save = s.save;
  v.workStart = s.workStart;
  v.workEnd = s.workEnd;
  v.game = s.game;
  return v;
}

const char *remoteKeyName(RemoteKey k) {
  static const char *const n[] = {"homeStyle", "anim", "speed", "invert", "rot", "night", "permOn", "sound", "led", "mute", "save",
                                  "workStart", "workEnd", "game"};
  return n[static_cast<int>(k)];
}

const NetInfo kNets[5] = {
    {"CasaWiFi", -58, true}, {"Officina", -66, true}, {"Ospiti", -71, false},
    {"TIM-29384712", -79, true}, {"Vodafone-A1B2", -86, true},
};

const DeviceProfile kHeltec213 = {
    "heltec213", "Heltec Wireless Paper", "2,13″", 250, 122, 'S', false, 2, 1.5f, 40, InputKind::Single,
    false, false, false, false, false,
};

const char *screenKey(ScreenId id) {
  static const char *const k[] = {"boot", "home", "detail", "perm", "list", "wifiScan", "qr", "connecting", "oggi",
                                  "storico", "batteria", "stato", "limiti", "focus", "guest", "servizi", "agenda",
                                  "ambiente", "sleep", "pair", "confirm", "battlow", "calib", "ota", "game", "pet", "gbload", "gbplay", "padcode"};
  return k[static_cast<int>(id)];
}

const char *menuKey(MenuId id) {
  static const char *const k[] = {"main", "wifi", "display", "avvisi", "info", "tools", "security", "battery", "saved", "savednet", "work", "petacc", "gb", "gbpad", "gbroms", "gbpause", "tema"};
  return k[static_cast<int>(id)];
}

// ---------- modello ----------

void countTasks(Session &s) {
  s.total = s.nTasks;
  s.done = 0;
  for (int k = 0; k < s.nTasks; k++)
    if (s.tasks[k].status == TaskStatus::Done) s.done++;
}

void logEv(Model &m, const Session *s, const char *text, Ms t) {
  Log &L = m.log;
  L.head = (L.head + kLogCap - 1) % kLogCap;
  if (L.n < kLogCap) L.n++;
  LogEntry &e = L.e[L.head];
  e.t = t;
  scopy(e.name, s ? s->name : "sistema");
  scopy(e.text, text);
  e.local = true;
}

bool takeThemePick(Themes &t, char *id, size_t cap) {
  if (!t.picked) return false;
  t.picked = false;
  scopy(id, cap, t.cur >= 0 && t.cur < t.n ? t.items[t.cur].id : "");
  return true;
}

void toast(Model &m, const char *msg, Ms now, Ms ms) {
  scopy(m.toast.msg, msg);
  m.toast.until = now + (ms ? ms : kToastMs);
}

bool isAttn(const Session &s) {
  return s.state == SessionState::Permission || s.state == SessionState::Input || s.state == SessionState::Error;
}
bool canArchive(const Session &s) {
  return s.state == SessionState::Done || s.state == SessionState::Idle || s.state == SessionState::Error;
}

bool attention(const Model &m) {
  for (int k = 0; k < m.nSessions; k++)
    if (isAttn(m.sessions[k])) return true;
  return false;
}

int urgentIdx(const Model &m) {
  int b = 0;
  for (int k = 0; k < m.nSessions; k++)
    if (stateInfo(m.sessions[k].state).prio > stateInfo(m.sessions[b].state).prio) b = k;
  return b;
}

int sIdx(const Model &m, Ms now) { return m.ui.selUntil > now ? m.ui.sel : urgentIdx(m); }

int unaIdx(const Model &m, Ms now) {
  if (m.ui.selUntil > now) return m.ui.sel;
  if (attention(m)) return urgentIdx(m);
  if (m.nSessions <= 0) return 0;  // dal PC possono arrivare zero sessioni
  return m.ui.page % m.nSessions;
}

bool muted(const Model &m, Ms now) { return m.set.mute > now; }
bool focusOn(const Model &m, Ms now) { return m.focus.until > now; }
bool quiet(const Model &m, Ms now) { return muted(m, now) || focusOn(m, now); }

bool hasAlerts(const Model &m, Ms now) {
  return !m.sys.pc || !m.sys.wifi || (m.sys.batt < 15 && !m.sys.usb) || muted(m, now) || m.set.night ||
         focusOn(m, now) || (m.limits.hasH5 && m.limits.h5 >= 80);
}

void extras(const Session &s, char *out, size_t cap, const char *sep) {
  Sb b(out, cap);
  bool any = false;
  if (s.subs) { b.s("+").i(s.subs).s(s.subs == 1 ? " agente" : " agenti"); any = true; }
  if (s.test != TestResult::None) { if (any) b.s(sep); b.s(s.test == TestResult::Ok ? "test ✓" : "test ✗"); any = true; }
  if (s.ctx >= 80) { if (any) b.s(sep); b.s("contesto ").i(s.ctx).s("%"); }
}

const char *lineFor(const Session &s) {
  return s.state == SessionState::Done && s.summary[0] ? s.summary : s.detail;
}

const char *stateText(const Session &s) {
  if (s.state == SessionState::Working) return s.detail;
  if (s.state == SessionState::Done && s.summary[0]) return s.summary;
  return stateInfo(s.state).longText;
}

void metaLine(const Session &s, Ms now, char *out, size_t cap) {
  static const char *const kSep = "  ·  ";
  char a[24], ex[64];
  Sb b(out, cap);
  if (s.total) b.i(s.done).s("/").i(s.total).s(" task");
  else b.s("senza task");
  ago(now, s.since, a, sizeof a);
  b.s(kSep).s(a);
  extras(s, ex, sizeof ex, kSep);
  if (ex[0]) b.s(kSep).s(ex);
  b.s(kSep).s(lineFor(s));
}

MascotPose pose(const Model &m, const Session &s) {
  if (m.limits.hasH5 && m.limits.h5 >= 90 &&
      (s.state == SessionState::Working || s.state == SessionState::Done || s.state == SessionState::Idle))
    return MascotPose::Tired;
  // 0.10.0, gioco della mascotte: dopo 3 ore senza pausa saggia è stanca, nelle stesse pose dei limiti.
  if (m.set.game && m.pet.on && m.pet.tired &&
      (s.state == SessionState::Working || s.state == SessionState::Done || s.state == SessionState::Idle))
    return MascotPose::Tired;
  if (m.acc && m.air.co2 > 1000 && !isAttn(s)) return MascotPose::Stuffy;
  switch (s.state) {
    case SessionState::Working: return MascotPose::Working;
    case SessionState::Permission: return MascotPose::Permission;
    case SessionState::Input: return MascotPose::Input;
    case SessionState::Error: return MascotPose::Error;
    case SessionState::Compact: return MascotPose::Compact;
    case SessionState::Done: return MascotPose::Done;
    default: return MascotPose::Idle;
  }
}

bool powerSave(const Model &m) { return m.set.save || (m.sys.batt < 15 && !m.sys.usb); }

ScreenEntry &top(Model &m) { return m.ui.stack[m.ui.depth - 1]; }
const ScreenEntry &top(const Model &m) { return m.ui.stack[m.ui.depth - 1]; }

void addWait(Model &m, Session &s, Ms now) {
  if (s.hasAttnAt) {
    m.stats.waitSum += now - s.attnAt;
    m.stats.waitN++;
    s.hasAttnAt = false;
    s.attnAt = 0;
  }
}

const char *holdLabel(const Model &m, Ms now) {
  const ScreenEntry &tp = top(m);
  // Ota: mai Riposo (né sonno profondo) durante l'aggiornamento.
  // F9 (0.14.0): sul codice del telefono il tenuto non fa niente (Riposo coprirebbe il codice).
  if (tp.id == ScreenId::Boot || tp.id == ScreenId::Sleep || tp.id == ScreenId::BattLow || tp.id == ScreenId::Ota ||
      tp.id == ScreenId::PadCode)
    return nullptr;
  if (tp.id == ScreenId::Calib && calRunning(m)) return nullptr;
  if (tp.id == ScreenId::Confirm)
    return tp.until > now ? (tp.kind == ConfirmKind::Protect ? "PROTEGGI LE CHIAVI" : "RIPRISTINO DI FABBRICA") : nullptr;
  // 0.11.0: A tenuto sul controller nel Permesso: conferma la scelta, solo per la richiesta su cui è iniziato e
  // con la scelta di quel momento.
  if (tp.id == ScreenId::Perm && m.ui.holdPad) {
    const UiState &u = m.ui;
    const bool same = u.holdPerm && u.holdI == tp.i && streq(u.holdRid, tp.rid) && u.holdChoice == tp.choice;
    return same ? (tp.choice == 0 ? "CONSENTI" : "NEGA") : nullptr;
  }
  return "RIPOSO";
}

const char *pcLabel(const Model &m) { return m.sys.pcHost[0] ? m.sys.pcHost : "calamaio.local"; }
const char *uiVersion(const Model &m) { return m.sys.live ? "0.9" : "0.2"; }  // 0.2: il simulatore
bool permFromDisplay(const Model &m) {
  return m.set.permOn && (m.sys.live ? m.sys.authed : m.sys.pair == PairState::Paired);
}

int permReqFor(const Model &m, const char *session) {
  for (int k = 0; k < m.nPerms; k++)
    if (session && session[0] && streq(m.perms[k].session, session)) return k;
  return -1;
}

int permReqById(const Model &m, const char *id) {
  for (int k = 0; k < m.nPerms; k++)
    if (id && id[0] && streq(m.perms[k].id, id)) return k;
  return -1;
}

void dropPermReq(Model &m, int k) {
  if (k < 0 || k >= m.nPerms) return;
  for (int j = k; j + 1 < m.nPerms; j++) m.perms[j] = m.perms[j + 1];
  m.nPerms--;
}

Ms permSince(const Model &m, const ScreenEntry &tp) {
  const int q = permReqById(m, tp.rid);
  if (q >= 0) return m.perms[q].since;
  if (tp.i < 0 || tp.i >= m.nSessions) return 0;
  const Session &s = m.sessions[tp.i];
  return s.hasAttnAt ? s.attnAt : s.since;
}

void permWaitText(Ms now, Ms since, char *out, size_t cap) {
  // Nessuna scadenza (DECISIONI, «Permessi senza scadenza»): al posto dei secondi rimasti, i minuti
  // passati. Minuti interi, non arrotondati: «DA 1 MIN» vuol dire che il minuto è passato davvero.
  const int64_t mins = now > since ? (now - since) / 60000 : 0;
  Sb b(out, cap);
  if (mins < 1) b.s("ARRIVATA ORA");
  else if (mins < 60) b.s("IN ATTESA DA ").i(mins).s(" MIN");
  else b.s("IN ATTESA DA ").i(mins / 60).s(" H ").pad2(mins % 60);
}

void pairLabel(const Model &m, char *out, size_t cap) {
  Sb b(out, cap);
  if (m.sys.pair == PairState::Redo) b.s("da rifare");
  else if (m.sys.pair == PairState::None) b.s("non abbinato");
  else if (m.sys.pairHost[0]) b.s("abbinato a ").s(m.sys.pairHost);
  else b.s("abbinato");
}

const char *pairShort(const Model &m) {
  return m.sys.pair == PairState::Redo ? "da rifare" : m.sys.pair == PairState::None ? "non abbinato" : "abbinato";
}

int rssiBars(int r) { return r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0; }

int jsRound(double v) { return static_cast<int>(floor(v + 0.5)); }

ScreenEntry makeScreen(ScreenId id) {
  ScreenEntry e;
  memset(&e, 0, sizeof e);
  e.id = id;
  e.choice = 1;
  return e;
}

bool pushScreen(Model &m, const ScreenEntry &e) {
  if (m.ui.depth >= kStackCap) return false;
  m.ui.stack[m.ui.depth++] = e;
  return true;
}

void popScreen(Model &m) {
  if (m.ui.depth > 1) m.ui.depth--;
}

void dropScreens(Model &m, ScreenId id) {
  int w = 1;
  for (int r = 1; r < m.ui.depth; r++)
    if (m.ui.stack[r].id != id) m.ui.stack[w++] = m.ui.stack[r];
  m.ui.depth = w;
}

void resetStack(Model &m, const ScreenEntry &e) {
  m.ui.stack[0] = e;
  m.ui.depth = 1;
}

// ---------- formati ----------

void ago(Ms now, Ms t, char *out, size_t cap) {
  int64_t mins = roundDiv(now - t, 60000);
  if (mins < 0) mins = 0;
  Sb b(out, cap);
  if (mins < 60) b.i(mins).s(" min");
  else b.i(mins / 60).s(" h ").pad2(mins % 60);
}

void fmt1(double v, char *out, size_t cap) {
  // toFixed(1) arrotonda il valore decimale ESATTO del double (22,45 è 22,4499…: «22,4»), e sui pari
  // merito sceglie il numero più grande. fma dà il resto esatto di a·10, così si decide senza errori.
  const bool neg = v < 0;
  const double a = neg ? -v : v, t = a * 10.0, err = fma(a, 10.0, -t), fl = floor(t), frac = t - fl;
  const bool up = frac > 0.5 || (frac == 0.5 && err >= 0);
  const long long n = static_cast<long long>(fl) + (up ? 1 : 0);
  Sb b(out, cap);
  if (neg) b.ch('-');
  b.i(n / 10).ch(',').i(n % 10);
}

const char *yes(bool v) { return v ? "sì" : "no"; }

void splitCmd(const char *detail, char *tool, size_t toolCap, char *arg, size_t argCap) {
  const char *p = strstr(detail, ": ");
  Sb t(tool, toolCap), a(arg, argCap);
  if (!p) { t.s(detail); return; }
  char tmp[kDetailLen];
  size_t n = static_cast<size_t>(p - detail);
  if (n >= sizeof tmp) n = sizeof tmp - 1;
  memcpy(tmp, detail, n);
  tmp[n] = 0;
  t.s(tmp);
  a.s(p + 2);
}

// ---------- newModel ----------

static void setTasks(Session &s, const char *const *names, const uint8_t *st, int n) {
  s.nTasks = n;
  for (int k = 0; k < n; k++) {
    scopy(s.tasks[k].name, names[k]);
    s.tasks[k].status = static_cast<TaskStatus>(st[k]);
  }
}

static void initSession(Session &s, const char *name, SessionState st, const char *detail, Ms since, int ctx,
                        int subs, TestResult test, const char *summary) {
  memset(&s, 0, sizeof s);
  scopy(s.name, name);
  s.state = st;
  scopy(s.detail, detail);
  s.since = since;
  s.ctx = ctx;
  s.subs = subs;
  s.test = test;
  scopy(s.summary, summary);
}

void defaultSettings(Settings &st) {
  st.homeStyle = HomeStyle::Goccia;
  st.anim = AnimMode::Auto;
  st.speed = SpeedMode::Profilo;
  st.invert = false;
  st.rot = false;
  st.mute = 0;
  st.muteKind = MuteKind::None;
  st.night = false;
  st.permOn = true;
  st.sound = true;
  st.led = true;
  st.save = false;
  st.hookDirect = true;
  setAvatarRef(st.defAvatar, "goccia");  // defaultSet().defaultAvatar
  st.workStart = 9 * 60;  // m.work.start / end del simulatore
  st.workEnd = 18 * 60;
  st.game = false;  // gioco della mascotte: spento di default
  st.padOn = false;  // 0.11.0: Bluetooth del controller spento finché non si associa
}

bool operator==(const AvatarRef &a, const AvatarRef &b) {
  return strcmp(a.id, b.id) == 0 && a.hasCrc == b.hasCrc && (!a.hasCrc || a.crc == b.crc);
}

void setAvatarRef(AvatarRef &r, const char *id) {
  memset(&r, 0, sizeof r);
  scopy(r.id, id ? id : "");
}

void newModel(Model &m, Ms now, const WallClock &wc) {
  memset(&m, 0, sizeof m);
  memset(&m.themes, 0, sizeof m.themes);
  m.themes.cur = -1;  // 0.13.0: di serie
  const Ms t = now;
#define AGO(mm) (t - (Ms)(mm) * 60000)
  m.nSessions = 4;
  Session *S = m.sessions;
  {
    initSession(S[0], "api-server", SessionState::Working, "Edit src/routes/orders.ts", AGO(12), 46, 2, TestResult::None, "");
    static const char *const n[] = {"Leggi le route esistenti", "Endpoint POST /orders", "Validazione input", "Test di integrazione", "Aggiorna OpenAPI", "Errori 4xx coerenti", "Review finale"};
    static const uint8_t st[] = {2, 2, 2, 1, 0, 0, 0};
    setTasks(S[0], n, st, 7);
  }
  {
    initSession(S[1], "web-app", SessionState::Permission, "Bash: npm run test", AGO(26), 71, 0, TestResult::Ko, "");
    S[1].hasAttnAt = true;
    S[1].attnAt = AGO(1);
    static const char *const n[] = {"Pagina carrello", "Hook useCart", "Stato globale", "Stili mobile", "Fix layout tablet", "Esegui i test", "Correggi i test rotti", "Controllo Lighthouse", "Changelog"};
    static const uint8_t st[] = {2, 2, 2, 2, 2, 1, 0, 0, 0};
    setTasks(S[1], n, st, 9);
  }
  {
    initSession(S[2], "firmware-esp32", SessionState::Working, "Read lib/epd/driver.cpp", AGO(4), 18, 1, TestResult::Ok, "");
    static const char *const n[] = {"Driver SSD1683", "Layout 400×300", "Sprite mascotte", "Client WebSocket"};
    static const uint8_t st[] = {2, 1, 0, 0};
    setTasks(S[2], n, st, 4);
  }
  {
    initSession(S[3], "docs-site", SessionState::Done, "6 task completati", AGO(41), 83, 0, TestResult::Ok, "Guida di installazione aggiornata e 4 link rotti corretti.");
    static const char *const n[] = {"Indice", "Guida installazione", "Esempi API", "Screenshot", "Glossario", "Link rotti"};
    static const uint8_t st[] = {2, 2, 2, 2, 2, 2};
    setTasks(S[3], n, st, 6);
  }
  for (int k = 0; k < m.nSessions; k++) countTasks(S[k]);
  // Avatar delle sessioni finte (s.avatar di newModel): docs-site senza, quindi con il predefinito.
  setAvatarRef(S[0].avatar, "gatto");
  setAvatarRef(S[1].avatar, "demone");
  setAvatarRef(S[2].avatar, "calamaro");

  ScreenEntry boot = makeScreen(ScreenId::Boot);
  boot.until = t + kBootMs;
  resetStack(m, boot);
  m.ui.sel = 1;
  m.ui.selUntil = 0;
  m.ui.page = 0;

  defaultSettings(m.set);
  m.game.seed = kGameSeed;  // il resto di m.work e m.game parte da zero (memset)
  // Gioco della mascotte: riassunto finto come se fosse arrivato dal servizio (m.pet di newModel del simulatore):
  // livello 3, 66/417 XP, energia 80%, il cappellino sbloccato al livello 2, gioco spento come l'interruttore.
  // Con la rete lo azzera clearForLive.
  {
    Pet &p = m.pet;
    p.known = true;
    p.on = false;
    p.level = 3;
    p.xp = 412;
    p.xpLevelStart = 346;
    p.xpNext = 763;
    p.energy = 80;
    scopy(p.unlocked[0].id, "cappellino");
    scopy(p.unlocked[0].name, "Cappellino");
    p.nUnlocked = 1;
  }

  Sys &y = m.sys;
  y.wifi = true;
  scopy(y.ssid, "CasaWiFi");
  y.rssi = -58;
  scopy(y.ip, "192.168.1.42");
  y.pc = true;
  y.lastPc = t;
  y.batt = 78;
  y.usb = false;
  y.temp = 22.4;
  y.rh = 48;
  y.bootAt = t;
  y.pcOff = false;
  y.offAt = 0;
  scopy(y.boardId, "3f2a");  // finto di M2: lo stesso AP Calamaio-3F2A del simulatore
  scopy(y.apPass, "K7MZ3QRT");  // password finta sotto il QR (sys.apPass del simulatore); con la rete: ''
  y.pair = PairState::Paired;   // il banco di prova risponde ai permessi dal display (sys.pair del simulatore)
  y.authed = false;
  scopy(y.pairHost, "calamaio.local");
  y.keysProtected = false;
  y.wallOk = true;  // il simulatore ha sempre l'ora vera; sulla scheda la mette il loop
  // M6: uso tipico finché il registro dei tempi non dice altro (stimato: 30% Attivo, 50% Attesa, 20% Sonno).
  m.bat.mix[0] = 30;
  m.bat.mix[1] = 50;
  m.bat.mix[2] = 20;

  m.stats.tasks = 14;
  m.stats.permDisplay = 2;
  m.stats.permTerminal = 3;
  m.stats.waitSum = 11 * 60000;
  m.stats.waitN = 5;
  m.stats.linesAdd = 1240;
  m.stats.linesDel = 380;
  m.stats.cost = 18.4;

  m.limits.h5 = 62;
  m.limits.h5Reset = t + 130 * 60000LL;
  m.limits.d7 = 41;
  m.limits.d7Reset = t + (3 * 24 + 4) * 3600000LL;
  m.limits.hasH5 = m.limits.hasD7 = true;
  m.focus.until = 0;
  m.focus.len = 25;
  m.acc = false;
  m.air.co2 = 820;

  static const struct { const char *name; bool ok; int ms; } kSvc[3] = {
      {"Sito pubblico", true, 120}, {"API", true, 85}, {"Backup notturno", false, 0}};
  m.nServices = 3;
  for (int k = 0; k < 3; k++) {
    scopy(m.services[k].name, kSvc[k].name);
    m.services[k].ok = kSvc[k].ok;
    m.services[k].ms = kSvc[k].ms;
  }
  static const struct { const char *repo; int n; const char *title; PrChecks checks; } kPr[3] = {
      {"web-app", 42, "Carrello su mobile", PrChecks::Ko},
      {"api-server", 17, "Endpoint ordini", PrChecks::Run},
      {"docs-site", 8, "Guida installazione", PrChecks::Ok}};
  m.nPrs = 3;
  for (int k = 0; k < 3; k++) {
    scopy(m.prs[k].repo, kPr[k].repo);
    m.prs[k].n = kPr[k].n;
    scopy(m.prs[k].title, kPr[k].title);
    m.prs[k].checks = kPr[k].checks;
  }
  m.nNets = kNetCount;
  for (int k = 0; k < kNetCount; k++) {
    scopy(m.nets[k].ssid, kNets[k].ssid);
    m.nets[k].rssi = kNets[k].rssi;
    m.nets[k].lock = kNets[k].lock;
  }
  // Reti salvate finte (m.saved del simulatore): ultimo uso adesso, 3 e 64 giorni fa, mai. Visibili quelle che
  // stanno tra le reti vicine finte (NETS).
  static const struct { const char *ssid; int days; } kSaved[4] = {{"CasaWiFi", 0}, {"Officina", 3}, {"Casa Mare", 64}, {"Laboratorio", -1}};
  m.nSaved = 4;
  for (int k = 0; k < 4; k++) {
    SavedNet &n = m.saved[k];
    scopy(n.ssid, kSaved[k].ssid);
    n.usedLocal = kSaved[k].days < 0 ? 0 : wc.localMs - kSaved[k].days * 86400000LL;
    for (int q = 0; q < kNetCount; q++)
      if (streq(kNets[q].ssid, n.ssid)) { n.visible = true; n.rssi = kNets[q].rssi; }
  }

  // Appuntamenti arrotondati al quarto d'ora dell'ora locale, come d.setMinutes(round(min/15)*15, 0, 0).
  static const int kAgendaMin[3] = {50, 165, 1500};
  static const char *const kAgendaTitle[3] = {"Riunione di progetto", "Chiamata con un fornitore", "Revisione del codice"};
  m.nAgenda = 3;
  for (int k = 0; k < 3; k++) {
    int64_t l = localAt(wc, now, t + kAgendaMin[k] * 60000LL);
    int64_t hourStart = l - floorMod(l, 3600000);
    int minute = civilFrom(l).minute;
    int64_t rounded = hourStart + roundDiv(minute, 15) * 15 * 60000LL;
    m.agenda[k].t = now + (rounded - wc.localMs);
    scopy(m.agenda[k].title, kAgendaTitle[k]);
  }

  m.toast.msg[0] = 0;
  m.toast.until = 0;
  m.burstUntil = t + 6000;
  m.nextBurst = t + 30000;

  static const struct { int ago; int s; const char *text; } L[] = {
      {118, 3, "sessione aperta"}, {96, 0, "sessione aperta"}, {80, 3, "task 1/6 completato"},
      {64, 3, "task 3/6 completato"}, {41, 3, "turno completato"}, {33, 1, "sessione aperta"},
      {26, 1, "nuovo prompt, 9 task"}, {12, 0, "nuovo prompt, 7 task"}, {9, 0, "task 2/7 completato"},
      {7, 1, "task 5/9 completato"}, {4, 2, "sessione aperta"}, {3, 2, "task 1/4 completato"},
      {2, 0, "task 3/7 completato"}, {1, 1, "chiede permesso: npm run test"},
  };
  for (size_t k = 0; k < sizeof L / sizeof L[0]; k++) logEv(m, &S[L[k].s], L[k].text, AGO(L[k].ago));
#undef AGO
}

// ---------- animazioni e refresh ----------

const char *profileName(const DeviceProfile &dev) {
  return dev.fps >= 3 ? "fluide" : dev.fps == 2 ? "ridotte" : "a pose";
}

bool animOff(const Model &m, Ms now) {
  (void)now;  // il notturno del simulatore è un interruttore, non dipende dall'ora
  // M6: in Attesa (core/power) le animazioni sono ferme; il simulatore non ha questo stato (sys.idle sempre falso).
  return m.set.anim == AnimMode::Spente || m.set.night || powerSave(m) || m.sys.idle;
}

bool animScreen(ScreenId id) {
  switch (id) {
    case ScreenId::Home: case ScreenId::Detail: case ScreenId::Perm: case ScreenId::Connecting:
    case ScreenId::Sleep: case ScreenId::Boot: case ScreenId::Focus: case ScreenId::Ambiente: case ScreenId::Calib:
    case ScreenId::Game:
      return true;
    default:
      return false;
  }
}

bool maxSpeed(const Model &m) { return m.set.speed == SpeedMode::Massima; }

float effFps(const DeviceProfile &dev, const Model &m) {
  return maxSpeed(m) ? (dev.fpsMax > 0 ? dev.fpsMax : static_cast<float>(dev.fps)) : static_cast<float>(dev.fps);
}

float animRate(const DeviceProfile &dev, const Model &m, Ms t) {
  const ScreenEntry &tp = top(m);
  const float fps = effFps(dev, m);
  if (!m.sys.pc && (tp.id == ScreenId::Home || tp.id == ScreenId::Detail)) return 0;
  if (!animScreen(tp.id)) return 0;
  if (tp.id == ScreenId::Connecting || tp.id == ScreenId::Boot) return fps > 1 ? fps : 1.0f;
  // Calibrazione, fase 1: deve consumare davvero (animazione continua alla velocità massima); fase 2 ferma.
  if (tp.id == ScreenId::Calib)
    return m.calib.phase == CalibPhase::Max && !m.calib.paused ? (dev.fpsMax > 0 ? dev.fpsMax : static_cast<float>(dev.fps)) : 0.0f;
  if (animOff(m, t)) return 0;
  bool call = attention(m) && !quiet(m, t);
  if (dev.fps <= 1 && !maxSpeed(m)) return call ? 0.5f : 0.0f;
  if (m.set.anim == AnimMode::Continue) return fps;
  if (t < m.burstUntil) return fps;
  if (m.set.anim == AnimMode::Auto && call) return fps;
  return 0;
}

int frameFor(const DeviceProfile &dev, const Model &m, int f, Ms t) {
  // Risparmio energetico: la Goccia resta ferma sul fotogramma 0 della sua posa, anche se l'ultima
  // animazione si era fermata su un altro. Avvio e connessione restano animati (animRate).
  const ScreenId id = top(m).id;
  if (id == ScreenId::Calib) return m.calib.phase == CalibPhase::Max ? f : 0;
  if (powerSave(m) && id != ScreenId::Boot && id != ScreenId::Connecting) return 0;
  if (dev.fps >= 3 || maxSpeed(m)) return f;
  if (dev.fps == 2) return f % 2;
  return (attention(m) && !quiet(m, t) && !animOff(m, t)) ? static_cast<int>(floorMod(floorDiv(t, 2000), 2)) : 0;
}

void profileText(const DeviceProfile &dev, const Model &m, Ms now, char *out, size_t cap) {
  static const char *const kLower[4] = {"auto", "a raffiche", "continue", "spente"};  // ANIM_LABEL[..].toLowerCase()
  Sb(out, cap).i(dev.fps).s(" agg./s (stima) · completo ogni ").i(dev.fullEvery).s(" · animazioni ")
      .s(profileName(dev)).s(" · modalità ").s(kLower[static_cast<int>(m.set.anim)])
      .s(animOff(m, now) ? " (ferme ora)" : "");
}

// ---------- M6: batteria ----------

namespace {
const int kOcvMv[11] = {4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100};  // = core/battery.cpp
}

double ocvPercent(int mv) {
  if (mv >= kOcvMv[0]) return 100;
  if (mv <= kOcvMv[10]) return 0;
  for (int k = 0; k < 10; k++)
    if (mv >= kOcvMv[k + 1]) return 100 - 10 * k - 10.0 * (kOcvMv[k] - mv) / (kOcvMv[k] - kOcvMv[k + 1]);
  return 0;
}

int mvFromPercent(double p) {
  if (p >= 100) return kOcvMv[0];
  if (p <= 0) return kOcvMv[10];
  const int k = static_cast<int>(floor((100 - p) / 10));
  const double f = (100 - 10 * k - p) / 10;
  return jsRound(kOcvMv[k] - f * (kOcvMv[k] - kOcvMv[k + 1]));
}

bool calRate(const double *h, const double *p, int n, double &rate) {
  if (n < kCalMinSamples) return false;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (int k = 0; k < n; k++) { sx += h[k]; sy += p[k]; sxx += h[k] * h[k]; sxy += h[k] * p[k]; }
  const double d = n * sxx - sx * sx;
  if (d <= 0) return false;
  rate = -(n * sxy - sx * sy) / d;
  return true;
}

bool calRunning(const Model &m) { return m.calib.phase != CalibPhase::Off && m.calib.phase != CalibPhase::Done; }

double battRate(const Model &m) {
  const BattCal &c = m.bat.cal;
  if (!c.valid) return 0;
  const int *x = m.bat.mix;
  const double sl = m.bat.hasSleep ? m.bat.sleep : kSleepRateDef;
  const int tot = x[0] + x[1] + x[2];
  return tot > 0 ? (x[0] * c.active + x[1] * c.idle + x[2] * sl) / tot : 0;
}

static void hoursText(Sb &b, double h) {
  if (h >= 48) b.s("circa ").i(jsRound(h / 24)).s(" giorni");
  else b.s("circa ").i(h < 1 ? 1 : jsRound(h)).s(" ore");
}

void autonomyText(const Model &m, char *out, size_t cap) {
  Sb b(out, cap);
  const double r = battRate(m);
  if (m.bat.cal.valid && r > 0.01) { b.s("Autonomia: "); hoursText(b, m.sys.batt / r); b.s(" con il tuo uso"); return; }
  if (!m.sys.usb && m.bat.trend > 0.01) { b.s("Autonomia: "); hoursText(b, m.sys.batt / m.bat.trend); b.s(" (dalla scarica)"); return; }
  b.s(m.sys.usb ? "Autonomia: calibra per una stima" : "Autonomia: stima in corso");
}

void rateText(double r, char *out, size_t cap) {
  char t[24];
  fmt1(r > 0 ? r : 0, t, sizeof t);
  Sb(out, cap).s(t).s(" %/h");
}

void voltText(int mv, char *out, size_t cap) {
  // (Math.round(mv / 10) / 100).toFixed(2) con la virgola
  const int c = jsRound(mv / 10.0);
  Sb(out, cap).i(c / 100).s(",").i((c % 100) / 10).i(c % 10).s(" V");
}

}  // namespace cal
