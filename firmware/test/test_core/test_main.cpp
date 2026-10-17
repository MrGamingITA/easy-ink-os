// Logica dell'interfaccia (classe S, un tasto): priorità, pose, navigazione con i gesti veri del
// riconoscitore, permessi (§7), animazioni della Heltec, scadenze. Il confronto riga per riga con il
// simulatore è in test_core_golden; qui i comportamenti con un nome.
#include <unity.h>
#include <stdio.h>
#include <string.h>

#include "core/battery.h"
#include "core/gesture.h"
#include "core/usb.h"
#include "core/menu.h"
#include "core/ui.h"
#include "gfx/avatar.h"

using namespace cal;

void setUp() {}
void tearDown() {}

// 11 ottobre 2026, 10:29:00 ora locale (un minuto pieno prima del cambio di minuto); il tempo monotono parte da 0 come millis() all'accensione.
static const int64_t kWall0 = wallFromCivil(2026, 10, 11, 10, 29, 0).localMs;

struct Bench {
  Ui ui;
  GestureRecognizer rec;
  Ms now;
  PermEvent events[8];
  int nEvents;
  Bench() : ui(kHeltec213), now(0), nEvents(0) {
    ui.setPermListener(&Bench::onPerm, this);
    ui.begin(now, wc());
  }
  WallClock wc() const { WallClock w; w.localMs = kWall0 + now; return w; }
  static void onPerm(const PermEvent &e, void *ctx) {
    Bench *b = static_cast<Bench *>(ctx);
    if (b->nEvents < 8) b->events[b->nEvents++] = e;
  }
  RefreshKind tick() { return ui.tick(now, wc()); }
  RefreshKind wait(Ms ms) { now += ms; return tick(); }
  // Fine dell'avvio: 2,6 s.
  void boot() { wait(2601); TEST_ASSERT_EQUAL(ScreenId::Home, top(ui.model()).id); }
  // Pressioni brevi attraverso il riconoscitore vero, poi la finestra di 380 ms si chiude.
  RefreshKind press(int n) {
    for (int k = 0; k < n; k++) {
      rec.down(static_cast<uint32_t>(now)); now += 80;
      TEST_ASSERT_EQUAL(Gesture::None, rec.up(static_cast<uint32_t>(now)));
      now += k + 1 < n ? 150 : 0;
    }
    now += GestureRecognizer::kMultiMs + 1;
    Gesture g = rec.poll(static_cast<uint32_t>(now));
    TEST_ASSERT_EQUAL(n >= 3 ? Gesture::Press3 : n == 2 ? Gesture::Press2 : Gesture::Press1, g);
    return ui.gesture(g, now, wc());
  }
  RefreshKind hold(Ms ms) {
    rec.down(static_cast<uint32_t>(now));
    ui.keyDown(now);  // come il loop: R1 regola 2 guarda quando è iniziato il tenuto
    now += ms;
    Gesture g = rec.up(static_cast<uint32_t>(now));
    TEST_ASSERT_EQUAL(ms >= 3000 ? Gesture::VeryLong : Gesture::Long, g);
    ui.keyUp(now);
    RefreshKind r = ui.gesture(g, now, wc());
    now += 150;  // il dito si stacca: il prossimo fronte non cade nell'antirimbalzo
    return r;
  }
  ScreenId topId() const { return top(ui.model()).id; }
  int depth() const { return ui.model().ui.depth; }
};

// ---------- priorità ----------

static void test_state_table() {
  TEST_ASSERT_EQUAL_STRING("PERMESSO", stateInfo(SessionState::Permission).label);
  TEST_ASSERT_EQUAL_STRING("Aspetta una risposta", stateInfo(SessionState::Input).longText);
  TEST_ASSERT_EQUAL(6, stateInfo(SessionState::Permission).prio);
  TEST_ASSERT_EQUAL(5, stateInfo(SessionState::Input).prio);
  TEST_ASSERT_EQUAL(4, stateInfo(SessionState::Error).prio);
  TEST_ASSERT_EQUAL(3, stateInfo(SessionState::Working).prio);
  TEST_ASSERT_EQUAL(3, stateInfo(SessionState::Compact).prio);
  TEST_ASSERT_EQUAL(2, stateInfo(SessionState::Done).prio);
  TEST_ASSERT_EQUAL(1, stateInfo(SessionState::Idle).prio);
  TEST_ASSERT_EQUAL_STRING("Tenuto 3 secondi", gestureName(Gesture::VeryLong));
  TEST_ASSERT_EQUAL_STRING("Lunga 0,7 s", kControlsSingle[3].key);
}

static void test_urgent_idx() {
  static Model m;
  newModel(m, 0, WallClock{kWall0});
  TEST_ASSERT_EQUAL(1, urgentIdx(m));  // web-app chiede un permesso
  m.sessions[1].state = SessionState::Working;
  TEST_ASSERT_EQUAL(0, urgentIdx(m));  // tutte pari a 3 tranne docs-site: vince la prima
  m.sessions[3].state = SessionState::Error;
  TEST_ASSERT_EQUAL(3, urgentIdx(m));
  m.sessions[2].state = SessionState::Input;
  TEST_ASSERT_EQUAL(2, urgentIdx(m));
  m.sessions[0].state = SessionState::Permission;
  TEST_ASSERT_EQUAL(0, urgentIdx(m));
  m.sessions[3].state = SessionState::Permission;
  TEST_ASSERT_EQUAL(0, urgentIdx(m));  // pari merito: la prima
  TEST_ASSERT_TRUE(attention(m));
  for (int k = 0; k < m.nSessions; k++) m.sessions[k].state = SessionState::Idle;
  TEST_ASSERT_FALSE(attention(m));
  TEST_ASSERT_EQUAL(0, urgentIdx(m));
}

// ---------- pose ----------

static void test_pose_tired_and_stuffy() {
  static Model m;
  newModel(m, 0, WallClock{kWall0});
  Session &s = m.sessions[0];
  s.state = SessionState::Working;
  m.limits.h5 = 89.99;
  TEST_ASSERT_EQUAL(MascotPose::Working, pose(m, s));
  m.limits.h5 = 90;  // «supera il 90%»: il simulatore usa >= 90
  TEST_ASSERT_EQUAL(MascotPose::Tired, pose(m, s));
  s.state = SessionState::Done;
  TEST_ASSERT_EQUAL(MascotPose::Tired, pose(m, s));
  s.state = SessionState::Idle;
  TEST_ASSERT_EQUAL(MascotPose::Tired, pose(m, s));
  s.state = SessionState::Permission;  // STANCA non copre chi aspetta
  TEST_ASSERT_EQUAL(MascotPose::Permission, pose(m, s));
  s.state = SessionState::Compact;
  TEST_ASSERT_EQUAL(MascotPose::Compact, pose(m, s));

  m.limits.h5 = 50;
  m.acc = true;
  m.air.co2 = 1000;
  s.state = SessionState::Working;
  TEST_ASSERT_EQUAL(MascotPose::Working, pose(m, s));
  m.air.co2 = 1001;
  TEST_ASSERT_EQUAL(MascotPose::Stuffy, pose(m, s));
  s.state = SessionState::Input;  // ARIA VIZIATA solo se la sessione non aspetta
  TEST_ASSERT_EQUAL(MascotPose::Input, pose(m, s));
  m.acc = false;  // senza sensore la CO2 non conta
  s.state = SessionState::Working;
  TEST_ASSERT_EQUAL(MascotPose::Working, pose(m, s));
  m.acc = true;
  m.limits.h5 = 95;  // STANCA ha la precedenza
  TEST_ASSERT_EQUAL(MascotPose::Tired, pose(m, s));
}

static void test_pose_enum_matches_gfx() {
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Count), static_cast<int>(MascotPose::Count));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Working), static_cast<int>(MascotPose::Working));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Permission), static_cast<int>(MascotPose::Permission));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Input), static_cast<int>(MascotPose::Input));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Done), static_cast<int>(MascotPose::Done));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Error), static_cast<int>(MascotPose::Error));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Idle), static_cast<int>(MascotPose::Idle));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Compact), static_cast<int>(MascotPose::Compact));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Tired), static_cast<int>(MascotPose::Tired));
  TEST_ASSERT_EQUAL(static_cast<int>(Pose::Stuffy), static_cast<int>(MascotPose::Stuffy));
}

// ---------- avvio e selezione ----------

static void test_boot_expires() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
  TEST_ASSERT_EQUAL(RefreshKind::None, b.press(1));  // tasti ignorati durante l'avvio
  TEST_ASSERT_EQUAL_STRING("Avvio in corso: tasto ignorato", b.ui.lastAction());
  b.now = 2600;
  b.tick();
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());  // scade solo DOPO 2600 ms
  b.now = 2601;
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.tick());
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL(1, b.depth());
}

static void test_selection_expires() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  const Model &m = b.ui.model();
  TEST_ASSERT_EQUAL(1, sIdx(m, b.now));  // senza selezione: la più urgente
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.press(1));
  TEST_ASSERT_EQUAL(2, m.ui.sel);
  TEST_ASSERT_EQUAL_STRING("Seleziona firmware-esp32", b.ui.lastAction());
  const Ms until = m.ui.selUntil;
  TEST_ASSERT_EQUAL(b.now + kSelMs, until);
  TEST_ASSERT_EQUAL(2, sIdx(m, b.now));
  b.now = until;
  TEST_ASSERT_EQUAL(2, sIdx(m, b.now - 1));
  b.tick();
  TEST_ASSERT_EQUAL(until, m.ui.selUntil);  // scade solo dopo
  b.now = until + 1;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());  // classe S: ridisegna
  TEST_ASSERT_EQUAL(0, m.ui.selUntil);
  TEST_ASSERT_EQUAL(1, sIdx(m, b.now));
}

// ---------- navigazione con i gesti ----------

static void test_home_detail_perm_deny_preselected() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(2));
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Apri web-app", b.ui.lastAction());
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(2));
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).choice);  // NEGA preselezionato
  TEST_ASSERT_EQUAL_STRING("Rispondi al permesso di web-app (NEGA preselezionato)", b.ui.lastAction());
  TEST_ASSERT_EQUAL(RefreshKind::None, b.press(3));  // niente menu nel permesso
  TEST_ASSERT_EQUAL_STRING("Prima rispondi o annulla", b.ui.lastAction());
  TEST_ASSERT_EQUAL(0, b.nEvents);
  // un solo gesto (2×) conferma la scelta preselezionata: NEGA
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(2));
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Deny, b.events[0].outcome);
  TEST_ASSERT_EQUAL(1, b.events[0].session);
  TEST_ASSERT_EQUAL_STRING("web-app", b.events[0].sessionName);
  const Session &s = b.ui.model().sessions[1];
  TEST_ASSERT_EQUAL(SessionState::Working, s.state);
  TEST_ASSERT_EQUAL_STRING("Permesso negato, cerca un’alternativa", s.detail);
  TEST_ASSERT_EQUAL_STRING("Negato · inviato a Claude Code", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(3, b.ui.model().stats.permDisplay);
}

static void test_perm_allow_needs_two_gestures() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  b.press(2);
  b.press(2);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.press(1));  // 1° gesto: scegli CONSENTI
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);
  TEST_ASSERT_EQUAL_STRING("Scelta: CONSENTI", b.ui.lastAction());
  TEST_ASSERT_EQUAL(0, b.nEvents);  // scegliere non risponde
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(2));  // 2° gesto: conferma
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL_STRING("Bash in corso: npm run test", b.ui.model().sessions[1].detail);
  TEST_ASSERT_EQUAL_STRING("Consentito: Claude Code prosegue", b.ui.lastAction());
  TEST_ASSERT_FALSE(b.ui.model().sessions[1].hasAttnAt);  // attesa contata in stats
}

static void test_perm_long_cancels_and_never_expires() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  b.press(2);
  b.press(2);
  b.press(1);  // CONSENTI scelto ma non confermato
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.hold(900));
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL(SessionState::Permission, b.ui.model().sessions[1].state);  // resta aperta
  TEST_ASSERT_EQUAL_STRING("Annullato · resta in attesa", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Cancelled, b.events[0].outcome);
  TEST_ASSERT_EQUAL_STRING("", b.events[0].requestId);  // senza rete non c'è un id del servizio

  // Di nuovo dal Dettaglio (2×), e niente scadenza: dopo un'ora è ancora lì, NEGA preselezionato.
  b.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  for (int k = 0; k < 60; k++) b.wait(60000);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).choice);
  TEST_ASSERT_EQUAL(1, b.nEvents);
}

static void test_perm_wait_text_redraws_per_minute() {
  char t[32];
  permWaitText(1000, 1000, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("ARRIVATA ORA", t);
  permWaitText(60000 + 59999, 60000, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("ARRIVATA ORA", t);  // minuti interi: il primo minuto non è ancora passato
  permWaitText(120000, 60000, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("IN ATTESA DA 1 MIN", t);
  permWaitText(65 * 60000, 0, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("IN ATTESA DA 1 H 05", t);
  permWaitText(0, 5000, t, sizeof t);  // orologio indietro: niente negativi
  TEST_ASSERT_EQUAL_STRING("ARRIVATA ORA", t);

  Bench b;
  b.boot();
  b.ui.model().set.anim = AnimMode::Spente;  // niente fotogrammi: si vedono solo i ridisegni del testo
  b.ui.model().sessions[1].attnAt = b.now - 90000;  // web-app aspetta da un minuto e mezzo
  const Ms two = b.now - 90000 + 120000;            // qui diventa «2 MIN» (prima del cambio di minuto dell'orologio, a 60 s)
  b.press(2);
  b.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  // Niente più ridisegno a ogni secondo: solo quando «IN ATTESA DA 1 MIN» diventa «2 MIN».
  int partials = 0;
  while (b.now < two - 100) {
    if (b.wait(100) == RefreshKind::Partial) partials++;
  }
  TEST_ASSERT_EQUAL(0, partials);
  b.now = two;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());
}

static void test_perm_disabled_from_menu() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  b.ui.model().set.permOn = false;
  b.press(2);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.press(2));
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Risposte dal display spente", b.ui.model().toast.msg);
}

static void test_menu_and_back() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(3));
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL(MenuId::Main, top(b.ui.model()).menu);
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(13, L.n);  // classe S: c'è anche «Stato»; M5: «Sicurezza» dopo Avvisi; 0.11.0: Gioco dopo Strumenti
  TEST_ASSERT_EQUAL_STRING("Sicurezza", L.items[6].label);
  TEST_ASSERT_EQUAL_STRING("Stato", L.items[10].label);
  TEST_ASSERT_FALSE(L.truncated);
  b.press(1);  // Strumenti
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(2));
  TEST_ASSERT_EQUAL(3, b.depth());
  TEST_ASSERT_EQUAL(MenuId::Tools, top(b.ui.model()).menu);
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.hold(700));  // lunga: indietro
  TEST_ASSERT_EQUAL(2, b.depth());
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).sel);
  b.press(2);
  b.press(2);  // Concentrazione
  TEST_ASSERT_EQUAL(ScreenId::Focus, b.topId());
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(3));  // 3 pressioni dal menu: Home
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL(1, b.depth());
  TEST_ASSERT_EQUAL_STRING("Torna alla Home", b.ui.lastAction());
  // dal Dettaglio il menu si apre sopra il Dettaglio
  b.press(2);
  b.press(3);
  TEST_ASSERT_EQUAL(3, b.depth());
  b.press(3);
  TEST_ASSERT_EQUAL(1, b.depth());
}

static void test_long_in_home_mutes_one_hour() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.hold(800));
  const Ms at = b.now - 150;  // istante del gesto (hold() aggiunge lo stacco del dito)
  const Model &m = b.ui.model();
  TEST_ASSERT_TRUE(muted(m, b.now));
  TEST_ASSERT_EQUAL(at + 3600000, m.set.mute);
  TEST_ASSERT_EQUAL(MuteKind::Hour, m.set.muteKind);
  TEST_ASSERT_EQUAL_STRING("Silenziato per 1 ora", m.toast.msg);
  TEST_ASSERT_EQUAL(at + kToastMs, m.toast.until);
  TEST_ASSERT_EQUAL_STRING("Silenzia per 1 ora", b.ui.lastAction());
  TEST_ASSERT_TRUE(quiet(m, b.now));
  TEST_ASSERT_EQUAL(1, b.depth());
  // la Home resta la Home; un'altra lunga riattiva
  b.hold(800);
  TEST_ASSERT_FALSE(muted(m, b.now));
  TEST_ASSERT_EQUAL_STRING("Avvisi attivi", m.toast.msg);
  // scadenza dell'ora
  b.hold(800);
  const Ms until = m.set.mute;
  b.now = until + 1;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());
  TEST_ASSERT_FALSE(muted(m, b.now));
  TEST_ASSERT_EQUAL(MuteKind::None, m.set.muteKind);
}

static void test_hold_three_seconds_sleeps_and_any_gesture_wakes() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  b.press(2);  // dal Dettaglio
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.hold(3000));
  TEST_ASSERT_EQUAL(ScreenId::Sleep, b.topId());
  TEST_ASSERT_EQUAL_STRING("Riposo: orologio e Goccia che dorme", b.ui.lastAction());
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(1));  // qualunque gesto sveglia, senza altri effetti
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Riattivato", b.ui.lastAction());
  TEST_ASSERT_EQUAL(1, b.ui.model().ui.sel);  // il gesto non ha cambiato sessione
  b.hold(3500);
  TEST_ASSERT_EQUAL(ScreenId::Sleep, b.topId());
  b.hold(800);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
}

static void test_menu_full_refresh() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();
  b.press(3);
  for (int k = 0; k < 4; k++) b.press(1);  // Display
  b.press(2);
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("Refresh completo", L.items[6].label);  // dopo Risparmio energetico (M5)
  for (int k = 0; k < 6; k++) b.press(1);
  TEST_ASSERT_EQUAL(RefreshKind::Full, b.press(2));
  TEST_ASSERT_EQUAL_STRING("Refresh completo", b.ui.lastAction());
}

// ---------- animazioni e refresh ----------

static void test_anim_rate_heltec() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  const DeviceProfile &d = kHeltec213;
  Model &m = b.ui.model();
  TEST_ASSERT_EQUAL(2, d.fps);
  TEST_ASSERT_EQUAL(40, d.fullEvery);
  TEST_ASSERT_EQUAL_STRING("ridotte", profileName(d));
  TEST_ASSERT_EQUAL_FLOAT(2.0f, animRate(d, m, b.now));  // avvio: max(1, fps)
  b.boot();
  // raffica iniziale (6 s) e qualcuno aspetta: 2/s
  TEST_ASSERT_EQUAL_FLOAT(2.0f, animRate(d, m, b.now));
  m.sessions[1].state = SessionState::Working;  // nessuno aspetta
  TEST_ASSERT_EQUAL_FLOAT(2.0f, animRate(d, m, m.burstUntil - 1));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, m.burstUntil));  // Auto: ferma fuori dalle raffiche
  m.set.anim = AnimMode::Continue;
  TEST_ASSERT_EQUAL_FLOAT(2.0f, animRate(d, m, m.burstUntil));
  m.set.anim = AnimMode::Spente;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, b.now));
  m.set.anim = AnimMode::Auto;
  m.sessions[1].state = SessionState::Permission;
  TEST_ASSERT_EQUAL_FLOAT(2.0f, animRate(d, m, m.burstUntil));  // Auto: anima finché qualcuno aspetta
  m.set.mute = m.burstUntil + 1000;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, m.burstUntil));  // in silenzio no
  m.set.mute = 0;
  m.set.night = true;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, b.now));
  m.set.night = false;
  m.sys.batt = 14;  // risparmio automatico sotto il 15%
  TEST_ASSERT_TRUE(powerSave(m));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, b.now));
  m.sys.usb = true;
  TEST_ASSERT_FALSE(powerSave(m));
  m.sys.batt = 78;
  m.sys.usb = false;
  // schermate senza animazioni
  b.press(3);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, b.now));
  // ridotte: 2 fotogrammi
  for (int f = 0; f < 8; f++) TEST_ASSERT_EQUAL(f % 2, frameFor(d, m, f, b.now));
  char t[160];
  profileText(d, m, b.now, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("2 agg./s (stima) · completo ogni 40 · animazioni ridotte · modalità auto", t);
}

static void test_anim_frames_advance_at_fps() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  b.boot();  // qualcuno aspetta: 2/s → un fotogramma ogni 500 ms
  b.tick();  // allinea l'ultimo fotogramma
  const int f0 = b.ui.rawFrame();
  b.now += 499;
  TEST_ASSERT_EQUAL(RefreshKind::None, b.tick());
  b.now += 1;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());
  TEST_ASSERT_EQUAL(f0 + 1, b.ui.rawFrame());
  TEST_ASSERT_EQUAL((f0 + 1) % 2, b.ui.frame(b.now));
}

static void test_speed_setting() {
  Bench b;  // il modello occupa qualche kB: va bene sullo stack del PC
  const DeviceProfile &d = kHeltec213;
  Model &m = b.ui.model();
  TEST_ASSERT_EQUAL_FLOAT(1.5f, d.fpsMax);
  TEST_ASSERT_FALSE(maxSpeed(m));
  b.boot();
  b.press(3);
  for (int k = 0; k < 4; k++) b.press(1);
  b.press(2);  // Display
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("Animazioni", L.items[1].label);
  TEST_ASSERT_EQUAL_STRING("Velocità animazioni", L.items[2].label);  // subito dopo Animazioni
  TEST_ASSERT_EQUAL_STRING("Profilo", L.items[2].val);
  TEST_ASSERT_EQUAL(ActId::Speed, L.items[2].act);
  b.press(1);
  b.press(1);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.press(2));
  TEST_ASSERT_EQUAL_STRING("Velocità animazioni: Massima", b.ui.lastAction());
  TEST_ASSERT_TRUE(maxSpeed(m));
  TEST_ASSERT_EQUAL_STRING("Massima", b.ui.items(b.now, b.wc()).items[2].val);
  b.press(3);  // Home: qualcuno aspetta
  TEST_ASSERT_EQUAL_FLOAT(1.5f, effFps(d, m));
  TEST_ASSERT_EQUAL_FLOAT(1.5f, animRate(d, m, b.now));
  for (int f = 0; f < 8; f++) TEST_ASSERT_EQUAL(f, frameFor(d, m, f, b.now));  // tutti e 4 i fotogrammi (f va a mascot, che fa f % 4)
  m.set.anim = AnimMode::Spente;  // Massima non scavalca Spente
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, b.now));
  m.set.anim = AnimMode::Auto;
  // avvio e connessione: max(1, fpsMax)
  b.ui.model().ui.stack[0].id = ScreenId::Boot;
  TEST_ASSERT_EQUAL_FLOAT(1.5f, animRate(d, m, b.now));
  b.ui.model().ui.stack[0].id = ScreenId::Home;
  // un pannello a pose (fps 1) con Massima anima a fpsMax anche senza attese
  DeviceProfile slow = d;
  slow.fps = 1;
  slow.fpsMax = 0;  // senza misura: usa fps
  m.sessions[1].state = SessionState::Working;
  TEST_ASSERT_EQUAL_FLOAT(1.0f, animRate(slow, m, m.burstUntil - 1));
  m.set.speed = SpeedMode::Profilo;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(slow, m, m.burstUntil - 1));  // a pose: 0,5 solo se qualcuno chiama
  m.sessions[1].state = SessionState::Permission;
  TEST_ASSERT_EQUAL_FLOAT(0.5f, animRate(slow, m, m.burstUntil - 1));
  // fotogrammi a 1,5/s: uno ogni 667 ms
  m.set.speed = SpeedMode::Massima;
  b.tick();
  const int f0 = b.ui.rawFrame();
  b.now += 666;
  TEST_ASSERT_EQUAL(RefreshKind::None, b.tick());
  b.now += 1;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());
  TEST_ASSERT_EQUAL(f0 + 1, b.ui.rawFrame());
}

static void test_refresh_counter() {
  RefreshCounter c(kHeltec213.fullEvery);
  for (int k = 1; k < 40; k++) TEST_ASSERT_EQUAL(RefreshKind::Partial, c.apply(RefreshKind::Partial));
  TEST_ASSERT_EQUAL(39, c.partials());
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Partial));  // il 40° diventa completo
  TEST_ASSERT_EQUAL(0, c.partials());
  TEST_ASSERT_EQUAL(RefreshKind::Quick, c.apply(RefreshKind::Quick));
  TEST_ASSERT_EQUAL(1, c.partials());  // quick conta
  for (int k = 0; k < 45; k++) c.apply(RefreshKind::Quick);
  TEST_ASSERT_EQUAL(46, c.partials());  // ma non fa scattare il completo
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Partial));
  TEST_ASSERT_EQUAL(RefreshKind::Full, c.apply(RefreshKind::Full));
  TEST_ASSERT_EQUAL(0, c.partials());
  TEST_ASSERT_EQUAL(RefreshKind::None, c.apply(RefreshKind::None));
  TEST_ASSERT_EQUAL(RefreshKind::Quick, mergeRefresh(RefreshKind::Partial, RefreshKind::Quick));
  TEST_ASSERT_EQUAL(RefreshKind::Quick, mergeRefresh(RefreshKind::Quick, RefreshKind::Partial));
  TEST_ASSERT_EQUAL(RefreshKind::Full, mergeRefresh(RefreshKind::Quick, RefreshKind::Full));
}

// ---------- menu e dati ----------

static void test_hook_url_uses_board_id() {
  static Model m;
  static MenuList L;
  newModel(m, 0, WallClock{kWall0});
  menuList(MenuId::Wifi, kHeltec213, m, 0, WallClock{kWall0}, L);
  TEST_ASSERT_FALSE(L.truncated);
  TEST_ASSERT_EQUAL_STRING("Hook diretti", L.items[7].label);
  TEST_ASSERT_EQUAL_STRING("Claude Code manda gli eventi direttamente a http://calamaio-3f2a.local/hook, anche da un server o da un secondo PC. I permessi passano sempre dal servizio sul PC.", L.items[7].help);
  TEST_ASSERT_NOT_NULL(strstr(L.items[5].help, "Calamaio-3F2A"));
  strcpy(m.sys.boardId, "a1b2");  // dato del modello, non scritto fisso
  menuList(MenuId::Wifi, kHeltec213, m, 0, WallClock{kWall0}, L);
  TEST_ASSERT_NOT_NULL(strstr(L.items[7].help, "http://calamaio-a1b2.local/hook"));
  TEST_ASSERT_NOT_NULL(strstr(L.items[5].help, "Calamaio-A1B2"));
  TEST_ASSERT_EQUAL_STRING("calamaio.local", L.items[6].val);  // il Server PC resta il PC
}

static void test_mute_until_eight() {
  const int hours[] = {23, 6};
  const Ms expect[] = {9 * 3600000LL, 2 * 3600000LL};
  for (int k = 0; k < 2; k++) {
    Bench b;
    b.boot();
    b.press(3);
    for (int i = 0; i < 5; i++) b.press(1);  // Avvisi
    b.press(2);
    b.press(2);  // Silenzia: 1 ora
    WallClock w = wallFromCivil(2026, 10, 11, hours[k], 0, 0);
    b.ui.act(Action::Ok, b.now, w);  // Silenzia: fino alle 8
    TEST_ASSERT_EQUAL_STRING("Silenzia: fino alle 8", b.ui.lastAction());
    TEST_ASSERT_EQUAL(b.now + expect[k], b.ui.model().set.mute);
    TEST_ASSERT_EQUAL_STRING("fino alle 8", b.ui.items(b.now, w).items[0].val);
  }
}

static void test_log_ring_keeps_80() {
  static Model m;
  newModel(m, 0, WallClock{kWall0});
  TEST_ASSERT_EQUAL(14, m.log.n);
  TEST_ASSERT_EQUAL_STRING("chiede permesso: npm run test", m.log.at(0).text);
  char t[16];
  for (int k = 0; k < 100; k++) {
    snprintf(t, sizeof t, "e%d", k);
    logEv(m, 0, t, k);
  }
  TEST_ASSERT_EQUAL(80, m.log.n);
  TEST_ASSERT_EQUAL_STRING("e99", m.log.at(0).text);
  TEST_ASSERT_EQUAL_STRING("e20", m.log.at(79).text);
  TEST_ASSERT_EQUAL_STRING("sistema", m.log.at(0).name);
}

static void test_stack_capacity_is_safe() {
  static Model m;
  newModel(m, 0, WallClock{kWall0});
  int pushed = 0;
  for (int k = 0; k < kStackCap + 5; k++) pushed += pushScreen(m, makeScreen(ScreenId::Sleep)) ? 1 : 0;
  TEST_ASSERT_EQUAL(kStackCap - 1, pushed);
  TEST_ASSERT_EQUAL(kStackCap, m.ui.depth);
  for (int k = 0; k < kStackCap + 5; k++) popScreen(m);
  TEST_ASSERT_EQUAL(1, m.ui.depth);  // la base non si toglie
}

// ---------- Risparmio energetico (richiesta dell'utente) ----------

static void test_power_save_menu_frame_and_clock() {
  Bench b;
  const DeviceProfile &d = kHeltec213;
  Model &m = b.ui.model();
  b.boot();
  b.press(3);
  for (int k = 0; k < 4; k++) b.press(1);
  b.press(2);  // Display
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("Risparmio energetico", L.items[3].label);  // dopo Velocità animazioni
  TEST_ASSERT_EQUAL_STRING("no", L.items[3].val);
  TEST_ASSERT_EQUAL(ActId::Save, L.items[3].act);
  TEST_ASSERT_EQUAL_STRING("Ferma le animazioni e aggiorna lo schermo solo quando cambia qualcosa. Si attiva da solo sotto il 15% di batteria.",
                           L.items[3].help);
  for (int k = 0; k < 3; k++) b.press(1);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.press(2));
  TEST_ASSERT_TRUE(m.set.save);
  TEST_ASSERT_EQUAL_STRING("Risparmio energetico: sì", b.ui.lastAction());
  TEST_ASSERT_EQUAL_STRING("sì", b.ui.items(b.now, b.wc()).items[3].val);
  b.press(3);  // Home, qualcuno aspetta
  // Goccia ferma sul fotogramma 0, qualunque sia il contatore; niente animazioni né raffiche.
  for (int f = 0; f < 8; f++) TEST_ASSERT_EQUAL(0, frameFor(d, m, f, b.now));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, b.now));
  m.burstUntil = b.now + 10000;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, animRate(d, m, b.now));
  // Avvio e connessione restano animati (sono brevi e dicono che cosa succede).
  m.ui.stack[0].id = ScreenId::Boot;
  TEST_ASSERT_EQUAL(1, frameFor(d, m, 1, b.now));
  m.ui.stack[0].id = ScreenId::Home;
  // Anche il risparmio automatico (batteria sotto il 15%) ferma la Goccia sul fotogramma 0.
  m.set.save = false;
  TEST_ASSERT_EQUAL(1, frameFor(d, m, 1, b.now));
  m.sys.batt = 10;
  TEST_ASSERT_EQUAL(0, frameFor(d, m, 1, b.now));
  m.sys.batt = 78;
  m.set.save = true;
  // Orologio: un ridisegno ogni 5 minuti invece che a ogni minuto. kWall0 = 10:29:00, now parte da 0.
  int redraws = 0, at5 = 0;
  const Ms start = b.now;
  for (Ms t = 100; t <= 11 * 60000; t += 100) {
    b.now = start + t;
    if (b.tick() != RefreshKind::None) {
      redraws++;
      at5 = civilFrom(b.wc().localMs).minute;
    }
  }
  TEST_ASSERT_EQUAL(3, redraws);  // 10:30, 10:35 e 10:40 (fra 10:29 e 10:40 passato), non 11 volte
  TEST_ASSERT_EQUAL(0, at5 % 5);
  m.set.save = false;
  m.set.anim = AnimMode::Spente;  // per contare solo l'orologio
  redraws = 0;
  const Ms s2 = b.now;
  for (Ms t = 100; t <= 5 * 60000; t += 100) {
    b.now = s2 + t;
    if (b.tick() != RefreshKind::None) redraws++;
  }
  TEST_ASSERT_EQUAL(5, redraws);  // senza risparmio: ogni minuto
}

static void test_power_save_stops_una_paging() {
  Bench b;
  Model &m = b.ui.model();
  b.boot();
  m.set.homeStyle = HomeStyle::Una;
  m.sessions[1].state = SessionState::Working;  // nessuno aspetta: Una alla volta gira ogni 8 s
  m.set.anim = AnimMode::Spente;
  b.wait(8100);
  const int p0 = m.ui.page;
  b.wait(8100);
  TEST_ASSERT_NOT_EQUAL(p0, m.ui.page);
  m.set.save = true;
  const int p1 = m.ui.page;
  b.wait(8100);
  b.wait(8100);
  TEST_ASSERT_EQUAL(p1, m.ui.page);  // con il risparmio la pagina resta ferma
}

// ---------- Sicurezza (M5), senza rete: come il simulatore ----------

static void openSecurity(Bench &b) {
  b.press(3);
  for (int k = 0; k < 6; k++) b.press(1);
  b.press(2);
  TEST_ASSERT_EQUAL(MenuId::Security, top(b.ui.model()).menu);
}

static void test_security_menu_and_unpair() {
  Bench b;
  b.boot();
  b.press(3);
  const MenuList &M = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("Sicurezza", M.items[6].label);
  TEST_ASSERT_EQUAL_STRING("abbinato", M.items[6].val);
  b.press(3);
  openSecurity(b);
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(4, L.n);
  TEST_ASSERT_EQUAL_STRING("Abbinamento", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("abbinato a calamaio.local", L.items[0].val);
  TEST_ASSERT_EQUAL_STRING("Dimentica abbinamento", L.items[1].label);
  TEST_ASSERT_EQUAL_STRING("Proteggi le chiavi", L.items[2].label);
  TEST_ASSERT_EQUAL_STRING("non protette", L.items[2].val);
  TEST_ASSERT_EQUAL_STRING("Ripristino di fabbrica", L.items[3].label);
  TEST_ASSERT_FALSE(L.truncated);
  b.press(2);  // Abbinamento: si avvia dal PC
  TEST_ASSERT_EQUAL_STRING("Avvia dal PC: calamaio pair", b.ui.model().toast.msg);
  b.press(1);
  b.press(2);  // Dimentica abbinamento: prima pressione arma
  TEST_ASSERT_EQUAL(PairState::Paired, b.ui.model().sys.pair);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[1].val);
  b.wait(kForgetMs + 200);  // scaduta
  TEST_ASSERT_EQUAL_STRING("", b.ui.items(b.now, b.wc()).items[1].val);
  b.press(2);
  b.press(2);
  TEST_ASSERT_EQUAL(PairState::None, b.ui.model().sys.pair);
  TEST_ASSERT_EQUAL_STRING("Abbinamento dimenticato", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL_STRING("non abbinato", b.ui.items(b.now, b.wc()).items[0].val);
  // Senza abbinamento il permesso si risponde nel terminale.
  b.press(3);
  b.press(2);  // Dettaglio di web-app (permesso)
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  b.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Rispondi nel terminale", b.ui.model().toast.msg);
}

static void test_protect_needs_double_press_then_hold() {
  Bench b;
  b.boot();
  openSecurity(b);
  b.press(1);
  b.press(1);
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.press(2));  // Proteggi le chiavi: avviso
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  TEST_ASSERT_EQUAL(ConfirmKind::Protect, top(b.ui.model()).kind);
  b.hold(3100);  // tenuto senza 2× prima: niente, e niente Riposo
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  TEST_ASSERT_FALSE(b.ui.model().sys.keysProtected);
  TEST_ASSERT_EQUAL_STRING("Prima 2X, poi tieni 3 s", b.ui.model().toast.msg);
  b.press(2);  // armata
  TEST_ASSERT_TRUE(top(b.ui.model()).until > b.now);
  b.wait(kArmMs + 200);  // troppo tardi: si disarma
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).until);
  b.hold(3100);
  TEST_ASSERT_FALSE(b.ui.model().sys.keysProtected);
  b.press(2);  // armata
  b.hold(800);  // rilasciato troppo presto: resta armata (non torna indietro), altri 10 s
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  TEST_ASSERT_EQUAL_STRING("Troppo presto: tieni fino in fondo", b.ui.model().toast.msg);
  TEST_ASSERT_TRUE(top(b.ui.model()).until == b.now - 150 + 10000);
  TEST_ASSERT_FALSE(b.ui.model().sys.keysProtected);
  b.press(3);  // 3× esce (Home)
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  openSecurity(b);
  b.press(1);
  b.press(1);
  b.press(2);
  b.press(2);
  b.hold(3100);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_TRUE(b.ui.model().sys.keysProtected);
  TEST_ASSERT_EQUAL_STRING("Chiavi protette", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL_STRING("protette", b.ui.items(b.now, b.wc()).items[2].val);
  b.press(2);  // già protette: niente avviso
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL_STRING("Chiavi già protette", b.ui.model().toast.msg);
}

// Tenuto 3 s con il tasto ancora giù: indicatore a 0,7 · 1,4 · 2,1 s, azione a 3 s, rilascio ignorato.
static void test_hold_indicator_and_fire_while_pressed() {
  Bench b;
  b.boot();
  b.press(3);  // Menu
  const Ms t0 = b.now;
  b.rec.down(uint32_t(b.now));
  b.ui.keyDown(b.now);
  int redraws = 0;
  bool fired = false;
  for (Ms k = 1; k <= 3300; k++) {
    b.now = t0 + k;
    if (k % 100 == 0 && b.tick() != RefreshKind::None && k < 3000) {  // dopo lo scatto ridisegna il Riposo
      redraws++;
      TEST_ASSERT_TRUE(k == 700 || k == 1400 || k == 2100);
    }
    const Gesture g = b.rec.poll(uint32_t(b.now));
    if (g != Gesture::None) {
      TEST_ASSERT_EQUAL(Gesture::VeryLong, g);
      TEST_ASSERT_EQUAL(3000, int(k));
      TEST_ASSERT_FALSE(fired);
      fired = true;
      TEST_ASSERT_TRUE(b.ui.gesture(g, b.now, b.wc()) != RefreshKind::None);
      TEST_ASSERT_EQUAL(ScreenId::Sleep, b.topId());  // Riposo subito, tasto ancora giù
      TEST_ASSERT_TRUE(b.ui.model().ui.holdDone && b.ui.model().ui.holdOk);
    }
  }
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL(3, redraws);
  TEST_ASSERT_EQUAL(Gesture::None, b.rec.up(uint32_t(b.now)));  // il rilascio non è un gesto
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.keyUp(b.now));      // via «FATTO · RILASCIA»
  TEST_ASSERT_EQUAL(ScreenId::Sleep, b.topId());
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOn);
}

static void test_hold_label() {
  Bench b;
  b.boot();
  TEST_ASSERT_EQUAL_STRING("RIPOSO", holdLabel(b.ui.model(), b.now));
  openSecurity(b);
  b.press(1);
  b.press(1);
  b.press(2);
  TEST_ASSERT_NULL(holdLabel(b.ui.model(), b.now));  // conferma non armata: la lunga annulla
  b.press(2);
  TEST_ASSERT_EQUAL_STRING("PROTEGGI LE CHIAVI", holdLabel(b.ui.model(), b.now));
  b.ui.keyDown(b.now);
  b.now += 1000;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.keyUp(b.now));  // l'indicatore era a schermo: va tolto
}

static void test_factory_reset() {
  Bench b;
  b.boot();
  b.ui.model().set.homeStyle = HomeStyle::Righe;
  openSecurity(b);
  for (int k = 0; k < 3; k++) b.press(1);
  b.press(2);
  TEST_ASSERT_EQUAL(ConfirmKind::Factory, top(b.ui.model()).kind);
  b.press(2);
  b.hold(3100);
  const Model &m = b.ui.model();
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
  TEST_ASSERT_EQUAL(HomeStyle::Goccia, m.set.homeStyle);
  TEST_ASSERT_FALSE(m.sys.wifi);
  TEST_ASSERT_EQUAL(PairState::None, m.sys.pair);
}

static void test_pairing_screen_flow_without_network() {
  Bench b;
  b.boot();
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.ui.netPairCode("175397", b.now));
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  TEST_ASSERT_EQUAL_STRING("175397", top(b.ui.model()).code);
  b.press(3);  // il menu non scappa dall'abbinamento
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  b.press(2);  // prima che il PC abbia il codice: niente
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  TEST_ASSERT_EQUAL_STRING("Prima scrivi il codice sul PC", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.netPairReady(b.now));
  b.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL_STRING("Abbinato", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(PairState::Paired, b.ui.model().sys.pair);
  // Pressione lunga: annullato, niente cambia.
  b.ui.model().sys.pair = PairState::None;
  b.ui.netPairCode("000042", b.now);
  b.hold(800);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL_STRING("Abbinamento annullato", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(PairState::None, b.ui.model().sys.pair);
  // Codice sbagliato sul PC.
  b.ui.netPairCode("000042", b.now);
  b.ui.netPairAborted("codice", b.now);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL_STRING("Abbinamento annullato", b.ui.model().toast.msg);
  // Senza rete la finestra di 3 minuti la chiude il display.
  b.ui.netPairCode("000042", b.now);
  b.wait(kPairMs);
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  b.wait(200);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL(PairState::None, b.ui.model().sys.pair);
}

static void test_agenda_rounded_to_quarter() {
  static Model m;
  newModel(m, 0, WallClock{kWall0});  // 10:29
  char h[8];
  hmAt(WallClock{kWall0}, 0, m.agenda[0].t, h, sizeof h);  // +50 min = 11:19 → 11:15
  TEST_ASSERT_EQUAL_STRING("11:15", h);
  hmAt(WallClock{kWall0}, 0, m.agenda[1].t, h, sizeof h);  // +165 min = 13:14 → 13:15
  TEST_ASSERT_EQUAL_STRING("13:15", h);
  char d[32];
  dateStr(WallClock{kWall0}, d, sizeof d);
  TEST_ASSERT_EQUAL_STRING("domenica 11 ottobre", d);
}

// M6: curva LiPo (OCV a passi del 10 %), media dei campioni e passaggio dalla batteria finta a quella vera.
static void test_lipo_curve() {
  TEST_ASSERT_EQUAL_INT(100, lipoPercent(4250));
  TEST_ASSERT_EQUAL_INT(100, lipoPercent(4190));
  TEST_ASSERT_EQUAL_INT(90, lipoPercent(4050));
  TEST_ASSERT_EQUAL_INT(95, lipoPercent(4120));  // a metà fra 4050 e 4190
  TEST_ASSERT_EQUAL_INT(50, lipoPercent(3720));
  TEST_ASSERT_EQUAL_INT(10, lipoPercent(3300));
  TEST_ASSERT_EQUAL_INT(5, lipoPercent(3200));
  TEST_ASSERT_EQUAL_INT(0, lipoPercent(3100));
  TEST_ASSERT_EQUAL_INT(0, lipoPercent(2900));
  int prev = -1;  // monotona
  for (int mv = 3000; mv <= 4300; mv++) {
    const int p = lipoPercent(mv);
    TEST_ASSERT_TRUE(p >= prev);
    prev = p;
  }
}

static void test_battery_mv_average_and_rejects() {
  int mv[4] = {1900, 1910, 0, 1890};
  bool ok[4] = {true, true, false, true};
  TEST_ASSERT_EQUAL_INT(3800, batteryMv(mv, ok, 4));  // media dei validi × 2
  bool half[4] = {true, false, false, false};
  TEST_ASSERT_EQUAL_INT(-1, batteryMv(mv, half, 4));  // meno di metà validi
  int low[2] = {1000, 1000};
  bool all[2] = {true, true};
  TEST_ASSERT_EQUAL_INT(-1, batteryMv(low, all, 2));  // 2000 mV: batteria assente o partitore spento
  int high[2] = {2400, 2400};
  TEST_ASSERT_EQUAL_INT(-1, batteryMv(high, all, 2));
}

static void test_real_battery_replaces_fake() {
  Bench b;
  Rng rng(1);
  b.ui.slowTick(b.now, rng);
  TEST_ASSERT_TRUE(b.ui.model().sys.batt < 78);  // finta: scende di 0,15 ogni 10 s
  TEST_ASSERT_EQUAL_INT(static_cast<int>(RefreshKind::None), static_cast<int>(b.ui.battery(12, b.now)));  // classe S: nessun ridisegno da solo
  TEST_ASSERT_TRUE(b.ui.model().sys.batt == 12);
  TEST_ASSERT_TRUE(powerSave(b.ui.model()));  // sotto il 15 % senza USB: risparmio automatico
  for (int k = 0; k < 6; k++) b.ui.slowTick(b.now + k * 10000, rng);
  TEST_ASSERT_TRUE(b.ui.model().sys.batt == 12);  // niente più scarica finta
  b.ui.battery(140, b.now);
  TEST_ASSERT_TRUE(b.ui.model().sys.batt == 100);
}

// Cavo USB: un campione alto basta per il «sì», il «no» vuole due letture di fila.
static void test_usb_classify_and_filter() {
  TEST_ASSERT_EQUAL(UsbPresent::Yes, usbClassify(1, 8, true));
  TEST_ASSERT_EQUAL(UsbPresent::No, usbClassify(0, 8, true));
  TEST_ASSERT_EQUAL(UsbPresent::Unknown, usbClassify(8, 8, false));  // ingresso del pad spento
  TEST_ASSERT_EQUAL(UsbPresent::Unknown, usbClassify(0, 0, true));
  UsbFilter f;
  TEST_ASSERT_EQUAL(UsbPresent::Unknown, f.state());
  TEST_ASSERT_FALSE(f.update(UsbPresent::No));
  TEST_ASSERT_TRUE(f.update(UsbPresent::No));
  TEST_ASSERT_EQUAL(UsbPresent::No, f.state());
  TEST_ASSERT_TRUE(f.update(UsbPresent::Yes));  // collegato: subito
  TEST_ASSERT_FALSE(f.update(UsbPresent::No));  // un «no» solo (byte di zeri dal PC): resta sì
  TEST_ASSERT_FALSE(f.update(UsbPresent::Yes));
  TEST_ASSERT_FALSE(f.update(UsbPresent::No));
  TEST_ASSERT_FALSE(f.update(UsbPresent::Unknown));  // non conta e non azzera
  TEST_ASSERT_TRUE(f.update(UsbPresent::No));
  TEST_ASSERT_EQUAL(UsbPresent::No, f.state());
}

static void test_usb_toast_and_power_save() {
  Bench b;
  b.boot();
  b.ui.battery(10, b.now);
  TEST_ASSERT_TRUE(powerSave(b.ui.model()));
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.usb(true, b.now));
  TEST_ASSERT_EQUAL_STRING("In carica", b.ui.model().toast.msg);
  TEST_ASSERT_FALSE(powerSave(b.ui.model()));  // con il cavo niente risparmio automatico
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.usb(true, b.now));  // nessun cambiamento
  b.ui.usb(false, b.now);
  TEST_ASSERT_EQUAL_STRING("A batteria", b.ui.model().toast.msg);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_state_table);
  RUN_TEST(test_urgent_idx);
  RUN_TEST(test_pose_tired_and_stuffy);
  RUN_TEST(test_pose_enum_matches_gfx);
  RUN_TEST(test_boot_expires);
  RUN_TEST(test_selection_expires);
  RUN_TEST(test_home_detail_perm_deny_preselected);
  RUN_TEST(test_perm_allow_needs_two_gestures);
  RUN_TEST(test_perm_long_cancels_and_never_expires);
  RUN_TEST(test_perm_wait_text_redraws_per_minute);
  RUN_TEST(test_perm_disabled_from_menu);
  RUN_TEST(test_menu_and_back);
  RUN_TEST(test_long_in_home_mutes_one_hour);
  RUN_TEST(test_hold_three_seconds_sleeps_and_any_gesture_wakes);
  RUN_TEST(test_menu_full_refresh);
  RUN_TEST(test_anim_rate_heltec);
  RUN_TEST(test_anim_frames_advance_at_fps);
  RUN_TEST(test_speed_setting);
  RUN_TEST(test_refresh_counter);
  RUN_TEST(test_hook_url_uses_board_id);
  RUN_TEST(test_mute_until_eight);
  RUN_TEST(test_log_ring_keeps_80);
  RUN_TEST(test_stack_capacity_is_safe);
  RUN_TEST(test_power_save_menu_frame_and_clock);
  RUN_TEST(test_power_save_stops_una_paging);
  RUN_TEST(test_security_menu_and_unpair);
  RUN_TEST(test_protect_needs_double_press_then_hold);
  RUN_TEST(test_hold_indicator_and_fire_while_pressed);
  RUN_TEST(test_hold_label);
  RUN_TEST(test_factory_reset);
  RUN_TEST(test_pairing_screen_flow_without_network);
  RUN_TEST(test_agenda_rounded_to_quarter);
  RUN_TEST(test_lipo_curve);
  RUN_TEST(test_battery_mv_average_and_rejects);
  RUN_TEST(test_real_battery_replaces_fake);
  RUN_TEST(test_usb_classify_and_filter);
  RUN_TEST(test_usb_toast_and_power_save);
  return UNITY_END();
}
