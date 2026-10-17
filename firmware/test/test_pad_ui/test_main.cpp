// Il controller nell'interfaccia (core/ui_pad): menu come il tasto PRG, permessi solo col tenuto di A (§7),
// Menu › Gioco. Gli eventi passano dal pad::MenuMapper vero, come sulla scheda.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <memory>

#include "core/gesture.h"
#include "core/menu.h"
#include "core/pad.h"
#include "core/ui.h"
#include "gfx/canvas.h"
#include "ui/screens.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static const int64_t kWall0 = wallFromCivil(2026, 10, 11, 10, 29, 0).localMs;

// R1 e F11: i comandi che dal telefono non devono mai uscire, scritti qui e non presi da phoneForbidden (una prova
// che usasse la stessa lista del codice non vedrebbe un comando tolto per sbaglio).
static const NetCmd kNeverFromPhone[] = {NetCmd::ProtectKeys, NetCmd::FactoryReset, NetCmd::PairConfirm,
                                         NetCmd::Unpair,      NetCmd::Forget,       NetCmd::ForgetNet,
                                         NetCmd::Provision,   NetCmd::Join};

// Seconde barriere da sole (Ui::emitNet, Ui::emitPerm): dall'esterno le prime le coprono sempre e togliendole
// nessuna prova cambierebbe. Qui si chiamano direttamente, con la sorgente scelta.
namespace cal {
struct UiProbe {
  static void emitNet(Ui &u, NetCmd c, InputSrc s) { u.src_ = s; u.emitNet(c); }
  static void emitPerm(Ui &u, PermOutcome o, InputSrc s) { u.src_ = s; u.emitPerm(o, 0, "", 0); }
  static void holdPhone(Ui &u, bool on) { u.m_.ui.holdPad = u.m_.ui.holdPhone = on; }
  static InputSrc src(const Ui &u) { return u.src_; }
  static bool confirmAction(Ui &u, InputSrc s) { u.src_ = s; return u.confirmAction(top(u.m_), 0); }
};
}  // namespace cal

struct Bench {
  Ui ui;
  pad::MenuMapper map;
  GestureRecognizer rec;
  Ms now;
  PermEvent events[8];
  int nEvents;
  pad::Kind src = pad::Kind::Xbox;
  NetCmd cmds[16];  // R1, F11: comandi usciti verso la rete (solo con live)
  InputSrc cmdSrc[16];
  int nCmds = 0;
  explicit Bench(bool live = false) : ui(kHeltec213), now(0), nEvents(0) {
    ui.setPermListener(&Bench::onPerm, this);
    ui.setNetListener(&Bench::onNet, this);
    ui.begin(now, wc());
    if (live) ui.goLive();
  }
  static void onNet(const NetRequest &r, void *ctx) {
    Bench *b = static_cast<Bench *>(ctx);
    if (b->nCmds < 16) {
      b->cmdSrc[b->nCmds] = r.src;
      b->cmds[b->nCmds++] = r.cmd;
    }
  }
  int forbidden() const {
    int n = 0;
    for (int i = 0; i < nCmds; i++)
      for (NetCmd c : kNeverFromPhone) n += cmds[i] == c;
    return n;
  }
  WallClock wc() const { WallClock w; w.localMs = kWall0 + now; return w; }
  static void onPerm(const PermEvent &e, void *ctx) {
    Bench *b = static_cast<Bench *>(ctx);
    if (b->nEvents < 8) b->events[b->nEvents++] = e;
  }
  RefreshKind tick() { return ui.tick(now, wc()); }
  void boot() { now += 2601; tick(); TEST_ASSERT_EQUAL(ScreenId::Home, topId()); }
  void feed(const pad::MenuEvent *ev, int n) { for (int i = 0; i < n; i++) ui.padEvent(ev[i], now, wc(), src); }
  void state(uint16_t buttons) {
    pad::State s;
    memset(&s, 0, sizeof s);
    s.buttons = buttons;
    s.src = src;  // M6: ogni stato fatto a mano dice da dove viene
    pad::MenuEvent ev[8];
    feed(ev, map.update(s, static_cast<uint32_t>(now), ev, 8));
  }
  // Avanza di ms con il tick del mappatore e dell'interfaccia ogni 100 ms (come il loop).
  void run(Ms ms) {
    for (Ms t = 0; t < ms;) {
      const Ms step = ms - t < 100 ? ms - t : 100;
      now += step;
      t += step;
      pad::MenuEvent ev[4];
      feed(ev, map.tick(static_cast<uint32_t>(now), ev, 4));
      tick();
    }
  }
  // Tasto premuto per ms e rilasciato, poi il dito si stacca.
  void tap(uint16_t b, Ms ms = 80) {
    state(b);
    run(ms);
    state(0);
    now += 150;
  }
  // Tasto PRG con il riconoscitore vero (per i confronti).
  RefreshKind press(int n) {
    for (int k = 0; k < n; k++) {
      rec.down(static_cast<uint32_t>(now));
      ui.keyDown(now);  // come il loop (main.cpp): l'inizio del gesto conta per le regole 2 e 3
      now += 80;
      rec.up(static_cast<uint32_t>(now));
      ui.keyUp(now);
      now += k + 1 < n ? 150 : 0;
    }
    now += GestureRecognizer::kMultiMs + 1;
    return ui.gesture(rec.poll(static_cast<uint32_t>(now)), now, wc());
  }
  ScreenId topId() const { return top(ui.model()).id; }
};

// Home → Dettaglio di web-app (chiede un permesso) → Permesso, tutto col controller.
static void openPerm(Bench &b) {
  b.boot();
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).choice);  // NEGA preselezionato
}

static void test_single_a_never_answers() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);  // CONSENTI scelto
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING("Tieni premuto A per 3 s", b.ui.model().toast.msg);
}

static void test_hold_a_allows_chosen() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Consentito · inviato a Claude Code", b.ui.model().toast.msg);
}

static void test_hold_a_denies_preselected() {
  Bench b;
  openPerm(b);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Deny, b.events[0].outcome);
}

static void test_hold_released_early_keeps_request() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.tap(pad::A, 2500);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING("Troppo presto: tieni fino in fondo", b.ui.model().toast.msg);
}

static void test_b_cancels_like_long_press() {
  Bench b;
  openPerm(b);
  b.tap(pad::B);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Cancelled, b.events[0].outcome);
}

static void test_hold_label_on_perm() {
  Bench b;
  openPerm(b);
  b.state(pad::A);
  b.run(800);
  TEST_ASSERT_EQUAL_STRING("NEGA", holdLabel(b.ui.model(), b.now));
  b.state(0);
  b.now += 150;
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(800);
  TEST_ASSERT_EQUAL_STRING("CONSENTI", holdLabel(b.ui.model(), b.now));
  b.state(0);
  b.now += 150;
  b.ui.keyDown(b.now);  // tasto PRG: il tenuto lì resta «Riposo», come oggi
  TEST_ASSERT_EQUAL_STRING("RIPOSO", holdLabel(b.ui.model(), b.now));
  b.ui.keyUp(b.now + 100);
}

// Review Focus: la richiesta si chiude (risposta dal terminale) durante la tenuta.
static void test_perm_closed_mid_hold() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(1000);
  popScreen(b.ui.model());  // come netPermClose: torna il Dettaglio
  b.run(2200);              // scatta a 3 s
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());  // e nemmeno Riposo
  b.state(0);
}

// Review Focus: il tenuto inizia altrove e a 3 s c'è una richiesta in cima.
static void test_hold_started_elsewhere_never_answers() {
  Bench b;
  b.boot();
  b.tap(pad::A);  // Dettaglio
  b.state(pad::A);
  b.run(1000);
  ScreenEntry e = makeScreen(ScreenId::Perm);
  e.i = 1;
  e.choice = 0;  // CONSENTI già scelto
  pushScreen(b.ui.model(), e);
  b.run(2200);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  b.state(0);
}

// Review Focus: controller scollegato a metà tenuta.
static void test_disconnect_mid_hold() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(2000);
  pad::MenuEvent ev[4];
  b.feed(ev, b.map.reset(ev, 4));
  b.run(3000);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOn);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
}

static void test_prg_down_wins_over_pad_hold() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.state(pad::A);
  b.run(500);
  b.ui.keyDown(b.now);  // PRG giù durante la tenuta di A
  b.run(2700);          // il mappatore dà HoldFire: non deve rispondere
  TEST_ASSERT_EQUAL(0, b.nEvents);
  b.ui.keyUp(b.now);
  b.state(0);
}

static void test_menu_navigation() {
  Bench b;
  b.boot();
  b.tap(pad::Menu);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL(MenuId::Main, top(b.ui.model()).menu);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).sel);
  b.tap(pad::Down);
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).sel);
  b.tap(pad::Up);
  b.tap(pad::Up);  // da 0 si va all'ultima voce, come Prev della rotella
  TEST_ASSERT_EQUAL(b.ui.items(b.now, b.wc()).n - 1, top(b.ui.model()).sel);
  b.tap(pad::Down);  // di nuovo 0
  b.tap(pad::Down);  // Strumenti
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(MenuId::Tools, top(b.ui.model()).menu);
  b.tap(pad::B);
  TEST_ASSERT_EQUAL(MenuId::Main, top(b.ui.model()).menu);
  b.tap(pad::B);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
}

// Indice della voce che apre/esegue qualcosa nella lista in cima.
static int findItem(Bench &b, ItemKind k, ScreenId open, ActId act) {
  const MenuList &L = b.ui.items(b.now, b.wc());
  for (int i = 0; i < L.n; i++)
    if (L.items[i].kind == k && (k == ItemKind::Open ? L.items[i].open == open : L.items[i].act == act)) return i;
  return -1;
}

static void pushList(Bench &b, MenuId id) {
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = id;
  pushScreen(b.ui.model(), e);
}

// Ripristino di fabbrica: col controller come col PRG servono due gesti (A, poi A tenuto 3 s).
static void test_confirm_same_as_prg() {
  Bench p, c;
  p.boot();
  c.boot();
  pushList(p, MenuId::Security);
  pushList(c, MenuId::Security);
  const int k = findItem(p, ItemKind::Act, ScreenId::Home, ActId::Factory);
  TEST_ASSERT_TRUE(k >= 0);
  top(p.ui.model()).sel = top(c.ui.model()).sel = k;
  p.tap(pad::A);
  c.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Confirm, p.topId());
  p.tap(pad::A, 3200);  // tenuto senza armare: come il PRG, non conferma
  c.ui.keyDown(c.now);
  c.now += 3000;
  c.ui.gesture(Gesture::VeryLong, c.now, c.wc());
  c.ui.keyUp(c.now + 100);
  c.now += 250;
  TEST_ASSERT_EQUAL_STRING(c.ui.model().toast.msg, p.ui.model().toast.msg);
  p.tap(pad::A);        // arma (come 2×)
  c.press(2);
  p.tap(pad::A, 3200);  // conferma
  c.ui.keyDown(c.now);
  c.now += 3000;
  c.ui.gesture(Gesture::VeryLong, c.now, c.wc());
  TEST_ASSERT_EQUAL_STRING(c.ui.lastAction(), p.ui.lastAction());
  TEST_ASSERT_EQUAL(c.topId(), p.topId());
}

// «Alto o basso»: su = alta (1×), giù = bassa (2×).
static void test_game_up_down_like_prg() {
  Bench p, c;
  p.boot();
  c.boot();
  pushList(p, MenuId::Gb);
  pushList(c, MenuId::Gb);
  const int k = findItem(p, ItemKind::Open, ScreenId::Game, ActId::None);
  TEST_ASSERT_TRUE(k >= 0);
  top(p.ui.model()).sel = top(c.ui.model()).sel = k;
  p.tap(pad::A);
  c.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Game, p.topId());
  p.tap(pad::Up);
  c.press(1);
  TEST_ASSERT_EQUAL(c.ui.model().game.streak, p.ui.model().game.streak);
  TEST_ASSERT_EQUAL(c.ui.model().game.card, p.ui.model().game.card);
  TEST_ASSERT_EQUAL_STRING(c.ui.lastAction(), p.ui.lastAction());
  p.tap(pad::Down);
  c.press(2);
  TEST_ASSERT_EQUAL(c.ui.model().game.card, p.ui.model().game.card);
  TEST_ASSERT_EQUAL_STRING(c.ui.lastAction(), p.ui.lastAction());
}

// Ruling F16: nel gioco A non sceglie (niente scelta implicita): solo su = alta, giù = bassa.
static void test_game_a_does_not_choose() {
  Bench b;
  b.boot();
  pushList(b, MenuId::Gb);
  const int k = findItem(b, ItemKind::Open, ScreenId::Game, ActId::None);
  TEST_ASSERT_TRUE(k >= 0);
  top(b.ui.model()).sel = k;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Game, b.topId());
  const Game before = b.ui.model().game;
  b.tap(pad::A);
  const Game &g = b.ui.model().game;
  TEST_ASSERT_EQUAL(ScreenId::Game, b.topId());
  TEST_ASSERT_EQUAL(before.card, g.card);
  TEST_ASSERT_EQUAL(before.prev, g.prev);
  TEST_ASSERT_EQUAL(before.streak, g.streak);
  TEST_ASSERT_EQUAL(static_cast<int>(before.last), static_cast<int>(g.last));
  TEST_ASSERT_EQUAL(before.over, g.over);
}

// Fix 1 (§7): la scelta non può cambiare durante il tenuto di A. Su/giù a metà tenuta non spostano la scelta e
// annullano il tenuto; una scelta cambiata per altre vie fa scattare il tenuto a vuoto. Mai una risposta.
static void test_choice_change_mid_hold_never_answers() {
  {  // giù col controller a metà tenuta, su NEGA
    Bench b;
    openPerm(b);
    b.state(pad::A);
    b.run(1000);
    b.state(pad::A | pad::Down);
    b.run(80);
    b.state(pad::A);
    TEST_ASSERT_EQUAL(1, top(b.ui.model()).choice);  // NEGA resta
    TEST_ASSERT_NULL(holdLabel(b.ui.model(), b.now));  // via l'indicatore
    b.run(2100);  // oltre i 3 s
    TEST_ASSERT_EQUAL(0, b.nEvents);
    TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
    b.state(0);
    b.run(200);
    TEST_ASSERT_EQUAL(0, b.nEvents);
    TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  }
  {  // su col controller a 2,9 s, partendo da CONSENTI
    Bench b;
    openPerm(b);
    b.tap(pad::Down);
    b.state(pad::A);
    b.run(2900);
    b.state(pad::A | pad::Up);
    b.run(300);
    TEST_ASSERT_EQUAL(0, b.nEvents);
    TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);
    b.state(0);
    TEST_ASSERT_EQUAL(0, b.nEvents);
  }
  {  // scelta cambiata per un'altra via (non dal controller) a metà tenuta
    Bench b;
    openPerm(b);
    b.state(pad::A);
    b.run(1000);
    top(b.ui.model()).choice = 0;
    TEST_ASSERT_NULL(holdLabel(b.ui.model(), b.now));
    b.run(2200);
    TEST_ASSERT_EQUAL(0, b.nEvents);
    TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
    b.state(0);
    TEST_ASSERT_EQUAL(0, b.nEvents);
  }
}

// Fix 1: il tasto PRG nel Permesso resta com'era: 1× cambia la scelta, 2× risponde, tenuto = Riposo.
static void test_prg_path_unaffected() {
  Bench b;
  openPerm(b);
  b.press(1);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);
  b.ui.keyDown(b.now);
  b.now += 800;
  TEST_ASSERT_EQUAL_STRING("RIPOSO", holdLabel(b.ui.model(), b.now));
  b.ui.keyUp(b.now);
  b.press(2);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
}

static int indexOf(const MenuList &L, const char *label) {
  for (int i = 0; i < L.n; i++)
    if (!strcmp(L.items[i].label, label)) return i;
  return -1;
}

static void test_main_menu_gioco_always() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  const int g = indexOf(L, "Gioco");  // anche senza Bluetooth (simulatore, golden)
  TEST_ASSERT_EQUAL(indexOf(L, "Strumenti") + 1, g);
  TEST_ASSERT_EQUAL(g + 1, indexOf(L, "Wi-Fi"));
  TEST_ASSERT_EQUAL(ItemKind::Go, L.items[g].kind);
  TEST_ASSERT_EQUAL(MenuId::Gb, L.items[g].go);
  TEST_ASSERT_EQUAL_STRING("", L.items[g].val);
  TEST_ASSERT_EQUAL_STRING("Alto o basso e, col controller Bluetooth, i giochi del Game Boy.", L.items[g].help);
  TEST_ASSERT_EQUAL_STRING("Concentrazione, agenda, servizi e PR, Wi-Fi ospiti, ambiente, giornata di lavoro.",
                           L.items[indexOf(L, "Strumenti")].help);
  b.ui.model().sys.padHw = true;
  b.ui.model().sys.padLink = pad::Link::Ready;
  b.ui.model().set.padOn = false;  // «collegato» solo col Bluetooth acceso, come lo stato del Controller
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("", L.items[g].val);
  b.ui.model().set.padOn = true;
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("collegato", L.items[g].val);
  b.ui.model().sys.padHw = false;
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("", L.items[g].val);
  TEST_ASSERT_FALSE(L.truncated);
}

// «Alto o basso» non sta più in Menu › Strumenti né al primo livello: è la voce fissa di Menu › Gioco.
static void test_alto_o_basso_moved_into_gioco() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::Main, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Gioco: alto o basso"));
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Alto o basso"));
  menuList(MenuId::Tools, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Gioco: alto o basso"));
  TEST_ASSERT_EQUAL(-1, indexOf(L, "Alto o basso"));
  menuList(MenuId::Gb, kHeltec213, b.ui.model(), b.now, b.wc(), L);  // padHw falso: come il simulatore
  TEST_ASSERT_EQUAL_STRING("Gioco", L.title);
  TEST_ASSERT_EQUAL(1, L.n);
  TEST_ASSERT_EQUAL_STRING("Alto o basso", L.items[0].label);
  TEST_ASSERT_EQUAL(ItemKind::Open, L.items[0].kind);
  TEST_ASSERT_EQUAL(ScreenId::Game, L.items[0].open);
  TEST_ASSERT_EQUAL_STRING("record 0", L.items[0].val);
  b.ui.model().sys.padHw = true;
  menuList(MenuId::Gb, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(3, L.n);
  TEST_ASSERT_EQUAL_STRING("Controller", L.items[0].label);
  TEST_ASSERT_EQUAL(MenuId::GbPad, L.items[0].go);
  TEST_ASSERT_EQUAL_STRING("nessuno associato", L.items[0].val);
  TEST_ASSERT_EQUAL_STRING("ROM", L.items[1].label);
  TEST_ASSERT_EQUAL(MenuId::GbRoms, L.items[1].go);
  TEST_ASSERT_EQUAL_STRING("Alto o basso", L.items[2].label);
  TEST_ASSERT_FALSE(L.truncated);
}

static void test_gb_pad_items() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::GbPad, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Controller", L.title);
  TEST_ASSERT_EQUAL(5, L.n);
  TEST_ASSERT_EQUAL_STRING("Stato", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("nessuno associato", L.items[0].val);
  TEST_ASSERT_EQUAL(ItemKind::Info, L.items[0].kind);
  TEST_ASSERT_EQUAL_STRING("Associa controller", L.items[1].label);
  TEST_ASSERT_EQUAL(ActId::GbPair, L.items[1].act);
  TEST_ASSERT_EQUAL_STRING("Dimentica controller", L.items[2].label);
  TEST_ASSERT_EQUAL(ActId::GbForget, L.items[2].act);
  TEST_ASSERT_FALSE(L.truncated);
  TEST_ASSERT_EQUAL_STRING("gb", menuKey(MenuId::Gb));
  TEST_ASSERT_EQUAL_STRING("gbpad", menuKey(MenuId::GbPad));
  TEST_ASSERT_EQUAL_STRING("gbroms", menuKey(MenuId::GbRoms));
  TEST_ASSERT_EQUAL_STRING("gbforget", actKey(ActId::GbForget));
}

// Piano B (primo gioco): senza partizione «gbrom» la lista «ROM» lo dice; con la partizione e senza ROM «Nessuna ROM»
// (anche col controller collegato).
static void test_gb_roms_hook_empty() {
  Bench b;
  b.boot();
  static MenuList L;
  menuList(MenuId::GbRoms, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("ROM", L.title);
  TEST_ASSERT_EQUAL(1, L.n);
  TEST_ASSERT_EQUAL_STRING("Giochi non disponibili", L.items[0].label);
  b.ui.gbInstalled(true, false, "", 0);
  b.ui.model().sys.padLink = pad::Link::Ready;
  menuList(MenuId::GbRoms, kHeltec213, b.ui.model(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL(1, L.n);
  TEST_ASSERT_EQUAL_STRING("Nessuna ROM", L.items[0].label);
  TEST_ASSERT_FALSE(L.truncated);
}

static void test_gb_status_values() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  static MenuList L;
  menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("nessuno associato", L.items[0].val);  // Bluetooth mai acceso
  m.set.padOn = true;
  struct { pad::Link l; bool bonded; bool hasBatt; uint8_t batt; const char *want; } cases[] = {
      {pad::Link::Off, false, false, 0, "nessuno associato"},
      {pad::Link::Off, true, false, 0, "lo cerco"},
      {pad::Link::Pairing, false, false, 0, "in associazione"},
      {pad::Link::Connecting, true, false, 0, "lo cerco"},
      {pad::Link::Ready, true, false, 0, "collegato"},
      {pad::Link::Ready, true, true, 80, "collegato · 80%"},
  };
  for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
    m.sys.padLink = cases[k].l;
    m.sys.padBonded = cases[k].bonded;
    m.sys.padHasBatt = cases[k].hasBatt;
    m.sys.padBatt = cases[k].batt;
    menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
    TEST_ASSERT_EQUAL_STRING(cases[k].want, L.items[0].val);
  }
  m.sys.padLink = pad::Link::Pairing;
  menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("in corso", L.items[1].val);
}

static void test_gb_pair_turns_bluetooth_on() {
  Bench b;
  b.boot();
  b.ui.model().sys.padHw = true;
  pushList(b, MenuId::GbPad);
  TEST_ASSERT_FALSE(b.ui.model().set.padOn);
  b.tap(pad::Down);  // Associa controller
  b.tap(pad::A);
  TEST_ASSERT_TRUE(b.ui.model().set.padOn);  // associare accende il Bluetooth
  TEST_ASSERT_EQUAL(PadReq::Pair, b.ui.takePadReq());
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());  // una volta sola
  TEST_ASSERT_EQUAL_STRING("Metti il controller in associazione", b.ui.model().toast.msg);
}

static void test_gb_forget_needs_confirm() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  pushList(b, MenuId::GbPad);
  top(m).sel = 2;  // Dimentica controller
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("Nessun controller associato", m.toast.msg);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
  m.set.padOn = true;
  m.sys.padBonded = true;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("Scegli ancora per confermare", m.toast.msg);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[2].val);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(PadReq::Forget, b.ui.takePadReq());
  TEST_ASSERT_FALSE(m.sys.padBonded);
  TEST_ASSERT_FALSE(m.set.padOn);  // dimenticare spegne il Bluetooth
  TEST_ASSERT_EQUAL_STRING("Controller dimenticato", m.toast.msg);
  m.sys.padBonded = true;  // finestra scaduta: si riarma
  b.tap(pad::A);
  b.now += kForgetMs + 1;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
  TEST_ASSERT_EQUAL_STRING("Scegli ancora per confermare", m.toast.msg);
}

// F4: «conferma?» scade da sola (ridisegno nel tick) e il ripristino di fabbrica la disarma.
static void test_gb_forget_confirm_expires() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  pushList(b, MenuId::GbPad);
  top(m).sel = 2;
  m.set.padOn = true;
  m.sys.padBonded = true;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[2].val);
  const Ms until = m.ui.gbForgetUntil;
  TEST_ASSERT_TRUE(until > b.now);
  b.run(until - b.now);  // ancora armata all'istante esatto
  TEST_ASSERT_EQUAL(until, m.ui.gbForgetUntil);
  b.now += 1;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());  // scaduta: via «conferma?» dallo schermo
  TEST_ASSERT_EQUAL(0, m.ui.gbForgetUntil);
  TEST_ASSERT_EQUAL_STRING("", b.ui.items(b.now, b.wc()).items[2].val);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
  // Ripristino di fabbrica (come test_confirm_same_as_prg): la finestra si chiude.
  pushList(b, MenuId::Security);
  const int k = findItem(b, ItemKind::Act, ScreenId::Home, ActId::Factory);
  TEST_ASSERT_TRUE(k >= 0);
  top(m).sel = k;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  b.tap(pad::A);  // arma
  m.ui.gbForgetUntil = b.now + kForgetMs;
  b.tap(pad::A, 3200);  // conferma
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
  TEST_ASSERT_EQUAL(0, m.ui.gbForgetUntil);
}

static void test_pad_link_toasts() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.padLinkChanged(pad::Link::Ready, true, 80, b.now));
  TEST_ASSERT_EQUAL_STRING("Controller collegato", m.toast.msg);
  TEST_ASSERT_TRUE(m.sys.padBonded);
  TEST_ASSERT_TRUE(m.sys.padHasBatt);
  TEST_ASSERT_EQUAL_UINT8(80, m.sys.padBatt);
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.padLinkChanged(pad::Link::Ready, true, 80, b.now));  // niente di nuovo
  m.set.padOn = true;  // F18: gli avvisi di scollegamento solo col Bluetooth acceso di proposito
  b.ui.padLinkChanged(pad::Link::Connecting, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Controller scollegato", m.toast.msg);
  TEST_ASSERT_FALSE(m.sys.padHasBatt);
  b.ui.padLinkChanged(pad::Link::Pairing, true, -1, b.now);
  b.ui.padLinkChanged(pad::Link::Connecting, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Associazione scaduta", m.toast.msg);
}

// F18: dopo «Dimentica controller» lo scollegamento che ne segue non dà «Controller scollegato».
static void test_forget_then_disconnect_is_quiet() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.sys.padHw = true;
  m.set.padOn = true;
  b.ui.padLinkChanged(pad::Link::Ready, true, 80, b.now);
  pushList(b, MenuId::GbPad);
  top(m).sel = 2;
  b.tap(pad::A);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(PadReq::Forget, b.ui.takePadReq());
  TEST_ASSERT_EQUAL_STRING("Controller dimenticato", m.toast.msg);
  b.ui.padLinkChanged(pad::Link::Connecting, false, -1, b.now);  // la radio chiude il collegamento
  TEST_ASSERT_EQUAL_STRING("Controller dimenticato", m.toast.msg);
  b.ui.padLinkChanged(pad::Link::Off, false, -1, b.now);  // e si spegne
  TEST_ASSERT_EQUAL_STRING("Controller dimenticato", m.toast.msg);
  TEST_ASSERT_EQUAL(pad::Link::Off, m.sys.padLink);
  TEST_ASSERT_EQUAL_STRING("nessuno associato", b.ui.items(b.now, b.wc()).items[0].val);
  // Anche un'associazione interrotta da «Dimentica» resta in silenzio.
  b.tap(pad::Up);  // Associa controller
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(PadReq::Pair, b.ui.takePadReq());
  b.ui.padLinkChanged(pad::Link::Pairing, false, -1, b.now);
  m.sys.padBonded = true;  // la finestra di «Dimentica» chiede un abbinamento
  b.tap(pad::Down);
  b.tap(pad::A);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(PadReq::Forget, b.ui.takePadReq());
  b.ui.padLinkChanged(pad::Link::Off, false, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Controller dimenticato", m.toast.msg);
}

// Croce durante il tenuto di A su un permesso: il tenuto si annulla e lo dice (Task 11, rimandato qui).
static void test_choice_change_mid_hold_toast() {
  Bench b;
  openPerm(b);
  b.state(pad::A);
  b.run(1000);
  b.state(pad::A | pad::Down);
  TEST_ASSERT_EQUAL_STRING("Scelta cambiata: rilascia A e riprova", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL_STRING("Scelta cambiata durante il tenuto: nessuna risposta, rilascia A e riprova",
                           b.ui.lastAction());
  b.run(80);
  b.state(pad::A);
  b.run(2100);
  b.state(0);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(1, top(b.ui.model()).choice);
  TEST_ASSERT_EQUAL_STRING("Scelta cambiata: rilascia A e riprova", b.ui.model().toast.msg);  // niente «Troppo presto»
}

// Piano B: la mappatura dei menu del controller si sospende solo con la partita a schermo (GbPlay), non in pausa.
static void test_gb_playing_stub() {
  Bench b;
  b.boot();
  TEST_ASSERT_FALSE(b.ui.gbPlaying());
  b.ui.gbInstalled(true, true, "TETRIS", 32);
  b.ui.gbLoad(b.now);
  TEST_ASSERT_FALSE(b.ui.gbPlaying());
  b.ui.gbPlay(b.now);
  TEST_ASSERT_TRUE(b.ui.gbPlaying());
  b.ui.gbPaused(gb::PauseWhy::User, b.now);
  TEST_ASSERT_FALSE(b.ui.gbPlaying());
}

// §7 scelta A (spec app iPhone §2.3): dal telefono CONSENTI scelto e tenuto 3 s non consente.
static void test_phone_hold_consenti_refused() {
  Bench b;
  openPerm(b);  // aperto col controller: aprire non è rispondere
  b.src = pad::Kind::Phone;
  b.tap(pad::Down);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);  // CONSENTI scelto: si vede
  b.state(pad::A);
  b.run(3200);
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOk);  // niente «FATTO · RILASCIA», guardato prima del rilascio (keyUp lo azzera)
  b.state(0);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneAllowToast, b.ui.model().toast.msg);
}

static void test_phone_hold_nega_answers() {
  Bench b;
  openPerm(b);
  b.src = pad::Kind::Phone;
  b.tap(pad::A, 3200);  // NEGA preselezionato
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Deny, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Phone, b.events[0].src);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
}

// Dopo il rifiuto il PRG consente come sempre (e la sorgente dice PRG).
static void test_prg_allows_after_phone_refusal() {
  Bench b;
  openPerm(b);
  b.src = pad::Kind::Phone;
  b.tap(pad::Down);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  b.press(2);  // 2× del PRG conferma la scelta a schermo (CONSENTI)
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Prg, b.events[0].src);
}

// L'Xbox resta com'era: CONSENTI tenuto 3 s consente.
static void test_xbox_still_allows() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Xbox, b.events[0].src);
}

// Il tenuto iniziato dal telefono resta del telefono anche se lo scatto arriva da un tick (sorgente persa).
static void test_phone_hold_fire_from_tick_refused() {
  Bench b;
  openPerm(b);
  b.src = pad::Kind::Phone;
  b.tap(pad::Down);
  b.state(pad::A);       // HoldDown dal telefono
  b.src = pad::Kind::Xbox;  // gli eventi del tick arrivano con la sorgente sbagliata
  b.run(3200);
  b.state(0);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
}

// R1 e F11: da dove partono le sequenze vietate al telefono. Bench con live: ogni comando passa da onNet.
enum class From : uint8_t { Protect, Factory, PcPair, Unpair, ForgetAll, ForgetNet, Qr, ScanNet, Count };
static const char *const kFromName[] = {"Proteggi", "Ripristino", "abbinamento col PC", "Dimentica abbinamento",
                                        "Dimentica tutte le reti", "Dimentica rete", "Configura da telefono",
                                        "Cerca reti"};

static void phoneSelect(Bench &b) {
  b.ui.padEvent(pad::MenuEvent::Select, b.now, b.wc(), pad::Kind::Phone);
  b.now += 300;
  b.tick();
}

static void xboxSelect(Bench &b) {
  b.ui.padEvent(pad::MenuEvent::Select, b.now, b.wc(), pad::Kind::Xbox);
  b.now += 300;
  b.tick();
}

static void listAt(Bench &b, MenuId menu, ItemKind k, ScreenId open, ActId act, const char *ssid = "") {
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = menu;
  snprintf(e.ssid, sizeof e.ssid, "%s", ssid);
  pushScreen(b.ui.model(), e);
  const int i = findItem(b, k, open, act);
  TEST_ASSERT_TRUE(i >= 0);
  top(b.ui.model()).sel = i;
}

static void startFrom(Bench &b, From f) {
  b.boot();
  Model &m = b.ui.model();
  b.src = pad::Kind::Phone;
  switch (f) {
    case From::Protect:
    case From::Factory:
      listAt(b, MenuId::Security, ItemKind::Act, ScreenId::Home, f == From::Protect ? ActId::Protect : ActId::Factory);
      xboxSelect(b);  // avviso
      xboxSelect(b);  // armata dall'Xbox (regola 1: il telefono non arma)
      TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
      break;
    case From::PcPair:
      m.sys.pair = PairState::None;
      b.ui.netPairCode("175397", b.now);
      b.ui.netPairReady(b.now);
      TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
      break;
    case From::Unpair:
      m.sys.pair = PairState::Paired;
      listAt(b, MenuId::Security, ItemKind::Act, ScreenId::Home, ActId::Unpair);
      xboxSelect(b);  // «2 pressioni ancora per confermare», dall'Xbox
      break;
    case From::ForgetAll:
      listAt(b, MenuId::Wifi, ItemKind::Act, ScreenId::Home, ActId::Forget);
      xboxSelect(b);
      break;
    case From::ForgetNet: {
      SavedNet s;
      memset(&s, 0, sizeof s);
      snprintf(s.ssid, sizeof s.ssid, "Casa");
      b.ui.netSaved(&s, 1, b.now);
      listAt(b, MenuId::SavedNet, ItemKind::Act, ScreenId::Home, ActId::ForgetNet, "Casa");
      xboxSelect(b);
      break;
    }
    case From::Qr:
      listAt(b, MenuId::Wifi, ItemKind::Open, ScreenId::Qr, ActId::Style);
      break;
    case From::ScanNet: {
      ScreenEntry e = makeScreen(ScreenId::WifiScan);
      e.scanUntil = b.now + 1;
      pushScreen(m, e);
      NetEntry n[2];
      memset(n, 0, sizeof n);
      snprintf(n[0].ssid, sizeof n[0].ssid, "Ufficio");
      n[0].lock = true;
      snprintf(n[1].ssid, sizeof n[1].ssid, "Bar");
      b.ui.netScanDone(n, 2, b.now);
      break;
    }
    default: break;
  }
  TEST_ASSERT_EQUAL(0, b.forbidden());
}

// Review Focus §7: ogni sequenza di 4 eventi del telefono dal Permesso con CONSENTI scelto (B, riapertura dal
// Dettaglio, scelta cambiata, tenuti e rilasci in ogni ordine) non produce mai un «consenti».
static void test_phone_never_allows_any_sequence() {
  const pad::MenuEvent kEv[8] = {pad::MenuEvent::Up,       pad::MenuEvent::Down,   pad::MenuEvent::Select,
                                 pad::MenuEvent::Back,     pad::MenuEvent::Menu,   pad::MenuEvent::HoldDown,
                                 pad::MenuEvent::HoldUp,   pad::MenuEvent::HoldFire};
  int allows = 0, denies = 0;
  for (int code = 0; code < 8 * 8 * 8 * 8; code++) {
    std::unique_ptr<Bench> b(new Bench());
    openPerm(*b);
    b->src = pad::Kind::Phone;
    b->tap(pad::Down);
    int c = code;
    bool allowed = false;
    for (int k = 0; k < 4; k++) {
      b->ui.padEvent(kEv[c % 8], b->now, b->wc(), pad::Kind::Phone);
      // prima barriera da sola: un consenso accettato si vede anche senza PermEvent (lo ferma la seconda)
      allowed = allowed || strcmp(b->ui.model().toast.msg, "Consentito · inviato a Claude Code") == 0;
      c /= 8;
      b->now += 3100;
      b->tick();
    }
    allows += allowed;
    for (int i = 0; i < b->nEvents; i++) {
      allows += b->events[i].outcome == PermOutcome::Allow;
      denies += b->events[i].outcome == PermOutcome::Deny;
    }
  }
  TEST_ASSERT_EQUAL(0, allows);
  TEST_ASSERT_TRUE(denies > 0);  // la prova ha raggiunto davvero delle risposte (NEGA dopo una riapertura)
  // R1 e F11: le stesse 4 096 sequenze da ogni conferma vietata al telefono (armata o pronta), due volte. Con la rete
  // accesa ogni comando si vede: mai un NetRequest vietato. A rete spenta emitNet non c'entra e la prima barriera
  // regge da sola: mai l'effetto locale (chiavi protette, abbinamento cambiato, reti tolte, QR o connessione aperti).
  for (int live = 0; live < 2; live++)
    for (int f = 0; f < static_cast<int>(From::Count); f++) {
      int sent = 0, refused = 0;
      for (int code = 0; code < 8 * 8 * 8 * 8; code++) {
        std::unique_ptr<Bench> b(new Bench(live == 1));
        startFrom(*b, static_cast<From>(f));
        const Model &m = b->ui.model();
        const PairState pair0 = m.sys.pair;
        const int saved0 = m.nSaved;
        int c = code;
        bool seen = false, effect = false;
        for (int k = 0; k < 4; k++) {
          b->ui.padEvent(kEv[c % 8], b->now, b->wc(), pad::Kind::Phone);
          seen = seen || strcmp(m.toast.msg, kPhoneConfirmToast) == 0;
          for (int i = 0; i < m.ui.depth; i++)
            effect = effect || m.ui.stack[i].id == ScreenId::Qr || m.ui.stack[i].id == ScreenId::Connecting;
          effect = effect || m.sys.pair != pair0 || m.nSaved < saved0 || m.sys.keysProtected;
          c /= 8;
          b->now += 3100;
          b->tick();
        }
        sent += b->forbidden();
        sent += effect;
        refused += seen;
      }
      TEST_ASSERT_EQUAL_MESSAGE(0, sent, kFromName[f]);
      TEST_ASSERT_TRUE_MESSAGE(refused > 0, kFromName[f]);  // la barriera è stata raggiunta davvero
    }
}

static void test_perm_event_src() {
  Bench b;
  openPerm(b);
  b.tap(pad::B);  // annulla dal controller
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Cancelled, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Xbox, b.events[0].src);
}

// R1, regola 1: dal telefono Ripristino di fabbrica e Proteggi le chiavi non si aprono né si armano; armati
// dall'Xbox, un evento del telefono li disarma e il tenuto che segue non conferma. L'Xbox, da solo, sì.
static void phoneConfirm(ActId act) {
  Bench b;
  b.boot();
  pushList(b, MenuId::Security);
  const int k = findItem(b, ItemKind::Act, ScreenId::Home, act);
  TEST_ASSERT_TRUE(k >= 0);
  top(b.ui.model()).sel = k;
  b.src = pad::Kind::Phone;
  b.tap(pad::A);  // niente avviso
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.src = pad::Kind::Xbox;
  b.tap(pad::A);  // avviso
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  b.src = pad::Kind::Phone;
  b.tap(pad::A);  // il telefono non arma
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).until);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.src = pad::Kind::Xbox;
  b.tap(pad::A);  // armata dall'Xbox
  TEST_ASSERT_TRUE(top(b.ui.model()).until > b.now);
  b.src = pad::Kind::Phone;
  b.state(pad::A);
  b.run(3200);    // tenuto 3 s dal telefono: disarma e non conferma
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOk);  // niente «FATTO · RILASCIA» (prima del rilascio)
  b.state(0);
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).until);
  b.src = pad::Kind::Xbox;
  b.tap(pad::A, 3200);  // disarmata: il tenuto dell'Xbox non conferma
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  TEST_ASSERT_EQUAL_STRING("Prima 2X, poi tieni 3 s", b.ui.model().toast.msg);
  TEST_ASSERT_FALSE(b.ui.model().sys.keysProtected);
  b.tap(pad::A);        // l'Xbox arma di nuovo
  b.tap(pad::A, 3200);  // e conferma
  TEST_ASSERT_TRUE(act == ActId::Protect ? b.ui.model().sys.keysProtected : b.topId() == ScreenId::Boot);
}
static void test_phone_never_factory_reset() { phoneConfirm(ActId::Factory); }
static void test_phone_never_protects_keys() { phoneConfirm(ActId::Protect); }
static void test_phone_never_confirms_pc_pairing() {
  Bench b;
  b.boot();
  b.ui.model().sys.pair = PairState::None;
  b.ui.netPairCode("175397", b.now);
  b.ui.netPairReady(b.now);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  TEST_ASSERT_TRUE(b.ui.model().sys.pair != PairState::Paired);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.src = pad::Kind::Xbox;
  b.tap(pad::A);  // il controller sì, come oggi
  TEST_ASSERT_TRUE(b.ui.model().sys.pair == PairState::Paired);
}

// F11, regola 1: dal telefono «Dimentica abbinamento», «Dimentica tutte le reti» e Reti salvate › Dimentica non si
// armano; armati dall'Xbox, un evento del telefono li disarma (serve di nuovo la prima doppia scelta).
static void phoneNeverArms(Bench &b, MenuId menu, ActId act, const char *ssid = "") {
  b.boot();
  listAt(b, menu, ItemKind::Act, ScreenId::Home, act, ssid);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  const UiState &u = b.ui.model().ui;
  TEST_ASSERT_TRUE(u.unpairUntil == 0 && u.forgetUntil == 0 && u.forgetNetUntil == 0);
  b.src = pad::Kind::Xbox;
  b.tap(pad::A);  // armata dall'Xbox
  TEST_ASSERT_EQUAL_STRING("2 pressioni ancora per confermare", b.ui.model().toast.msg);
  // un evento qualsiasi del telefono, anche senza effetto (A rilasciato senza tenuto), disarma
  b.ui.padEvent(pad::MenuEvent::HoldUp, b.now, b.wc(), pad::Kind::Phone);
  TEST_ASSERT_TRUE(u.unpairUntil == 0 && u.forgetUntil == 0 && u.forgetNetUntil == 0);
  b.tap(pad::A);  // l'Xbox deve ricominciare: riarma, non esegue
  TEST_ASSERT_EQUAL_STRING("2 pressioni ancora per confermare", b.ui.model().toast.msg);
  b.ui.padEvent(pad::MenuEvent::HoldUp, b.now, b.wc(), pad::Kind::Phone);  // e il telefono disarma di nuovo
}

static void test_phone_never_unpairs() {
  Bench b;
  phoneNeverArms(b, MenuId::Security, ActId::Unpair);
  TEST_ASSERT_EQUAL(PairState::Paired, b.ui.model().sys.pair);  // il banco parte abbinato
}

static void test_phone_never_forgets_all_networks() {
  Bench b;
  phoneNeverArms(b, MenuId::Wifi, ActId::Forget);
  TEST_ASSERT_EQUAL(4, b.ui.model().nSaved);
}

static void test_phone_never_forgets_a_network() {
  Bench b;
  b.boot();
  char ssid[kSsidLen];
  snprintf(ssid, sizeof ssid, "%s", b.ui.model().saved[0].ssid);
  phoneNeverArms(b, MenuId::SavedNet, ActId::ForgetNet, ssid);
  TEST_ASSERT_EQUAL(4, b.ui.model().nSaved);
}

// L'Xbox da solo: arma e conferma come sempre (le due doppie scelte).
static void test_xbox_still_forgets() {
  Bench b;
  b.boot();
  listAt(b, MenuId::Wifi, ItemKind::Act, ScreenId::Home, ActId::Forget);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(4, b.ui.model().nSaved);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(0, b.ui.model().nSaved);
}

// F11: «Configura da telefono» e una rete di Cerca reti dal telefono non partono; Cerca reti si apre.
static void test_phone_never_provisions() {
  Bench b;
  b.boot();
  listAt(b, MenuId::Wifi, ItemKind::Open, ScreenId::Qr, ActId::Style);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  const int k = findItem(b, ItemKind::Open, ScreenId::WifiScan, ActId::Style);
  top(b.ui.model()).sel = k;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::WifiScan, b.topId());  // cercare si può
  b.run(1600);  // fine della ricerca finta
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::WifiScan, b.topId());  // ma non collegarsi né configurare
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.src = pad::Kind::Xbox;
  b.now += kPhoneSettleMs;  // la schermata l'ha aperta il telefono: l'Xbox aspetta il margine (regola 3)
  b.tap(pad::A);
  TEST_ASSERT_TRUE(b.topId() == ScreenId::Qr || b.topId() == ScreenId::Connecting);
}

// Riavvia resta ammesso dal telefono (innocuo).
static void test_phone_reboot_allowed() {
  Bench b(true);
  b.boot();
  listAt(b, MenuId::Info, ItemKind::Act, ScreenId::Home, ActId::Reboot);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
  TEST_ASSERT_EQUAL(1, b.nCmds);
  TEST_ASSERT_EQUAL(NetCmd::Reboot, b.cmds[0]);
}

static void test_second_barriers_alone() {
  Bench b(true);
  b.boot();
  b.nCmds = 0;
  for (NetCmd c : kNeverFromPhone) UiProbe::emitNet(b.ui, c, InputSrc::Phone);
  TEST_ASSERT_EQUAL(0, b.nCmds);
  UiProbe::holdPhone(b.ui, true);  // tenuto del telefono, scatto passato da gesture() (src_ Prg)
  UiProbe::emitNet(b.ui, NetCmd::ProtectKeys, InputSrc::Prg);
  TEST_ASSERT_EQUAL(0, b.nCmds);
  UiProbe::holdPhone(b.ui, false);
  for (NetCmd c : kNeverFromPhone) UiProbe::emitNet(b.ui, c, InputSrc::Xbox);
  TEST_ASSERT_EQUAL(8, b.forbidden());
  TEST_ASSERT_EQUAL(InputSrc::Xbox, b.cmdSrc[0]);
  UiProbe::emitNet(b.ui, NetCmd::Reboot, InputSrc::Phone);  // ammesso, con la sorgente giusta
  TEST_ASSERT_EQUAL(9, b.nCmds);
  TEST_ASSERT_EQUAL(NetCmd::Reboot, b.cmds[8]);
  TEST_ASSERT_EQUAL(InputSrc::Phone, b.cmdSrc[8]);

  UiProbe::emitPerm(b.ui, PermOutcome::Allow, InputSrc::Phone);
  UiProbe::holdPhone(b.ui, true);
  UiProbe::emitPerm(b.ui, PermOutcome::Allow, InputSrc::Prg);
  UiProbe::holdPhone(b.ui, false);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  UiProbe::emitPerm(b.ui, PermOutcome::Deny, InputSrc::Phone);
  UiProbe::emitPerm(b.ui, PermOutcome::Allow, InputSrc::Prg);
  TEST_ASSERT_EQUAL(2, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Deny, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Phone, b.events[0].src);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[1].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Prg, b.events[1].src);

  // Round 1: confirmAction rifiuta il telefono anche se nessuna via lo raggiunge più (regola 1 disarma prima).
  ScreenEntry e = makeScreen(ScreenId::Confirm);
  e.kind = ConfirmKind::Protect;
  pushScreen(b.ui.model(), e);
  b.nCmds = 0;
  TEST_ASSERT_FALSE(UiProbe::confirmAction(b.ui, InputSrc::Phone));
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  TEST_ASSERT_EQUAL(0, b.nCmds);
  TEST_ASSERT_TRUE(UiProbe::confirmAction(b.ui, InputSrc::Xbox));
  TEST_ASSERT_EQUAL(1, b.nCmds);
  TEST_ASSERT_EQUAL(NetCmd::ProtectKeys, b.cmds[0]);
}

static void test_phone_forbidden_list() {
  for (NetCmd c : kNeverFromPhone) TEST_ASSERT_TRUE_MESSAGE(phoneForbidden(c), netCmdKey(c));
  const NetCmd yes[] = {NetCmd::Scan, NetCmd::CancelProvision, NetCmd::Reboot, NetCmd::PermReply, NetCmd::PairCancel,
                        NetCmd::Dismiss, NetCmd::AvatarNeed, NetCmd::AccWear, NetCmd::ThemeCur};
  for (NetCmd c : yes) TEST_ASSERT_FALSE_MESSAGE(phoneForbidden(c), netCmdKey(c));
}

// ---------- Round 1 della revisione (I1): il telefono prepara, il PRG o l'Xbox completano ----------
// Prove d'attacco del revisore (A, B, C), più A′, e una prova per ciascuna delle regole 1–4.
static void phoneEvA(Bench &b, pad::MenuEvent e) { b.now += 50; b.ui.padEvent(e, b.now, b.wc(), pad::Kind::Phone); }
static void phoneSelectSel(Bench &b, int target) {
  while (top(b.ui.model()).sel < target) phoneEvA(b, pad::MenuEvent::Down);
  while (top(b.ui.model()).sel > target) phoneEvA(b, pad::MenuEvent::Up);
}
static int goIdx(Bench &b, MenuId id) {
  const MenuList &L = b.ui.items(b.now, b.wc());
  for (int i = 0; i < L.n; i++)
    if (L.items[i].kind == ItemKind::Go && L.items[i].go == id) return i;
  return -1;
}
static void phoneToSecurity(Bench &b) {
  phoneEvA(b, pad::MenuEvent::Menu);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  const int g = goIdx(b, MenuId::Security);
  TEST_ASSERT_TRUE(g >= 0);
  phoneSelectSel(b, g);
  phoneEvA(b, pad::MenuEvent::Select);
}
static int countCmd(const Bench &b, NetCmd c) {
  int n = 0;
  for (int i = 0; i < b.nCmds; i++) n += b.cmds[i] == c;
  return n;
}

// A: l'utente tiene il PRG sulla Home per il Riposo; intanto il telefono va su Proteggi e prova ad armarla; a 3 s
// lo scatto non brucia la eFuse.
static void test_attack_prg_sleep_hold_becomes_protect() {
  Bench b(true);
  b.boot();
  b.ui.keyDown(b.now);  // PRG giù sulla Home (vuole Riposo)
  const Ms t0 = b.now;
  phoneToSecurity(b);
  const int k = findItem(b, ItemKind::Act, ScreenId::Home, ActId::Protect);
  phoneSelectSel(b, k);
  phoneEvA(b, pad::MenuEvent::Select);  // avviso: rifiutato (regola 1)
  phoneEvA(b, pad::MenuEvent::Select);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::Confirm || top(b.ui.model()).until == 0);
  b.now = t0 + 3000;
  b.ui.gesture(Gesture::VeryLong, b.now, b.wc());  // gestures.poll del loop
  TEST_ASSERT_EQUAL_MESSAGE(0, countCmd(b, NetCmd::ProtectKeys), "ProtectKeys partito da un tenuto PRG cominciato sulla Home");
}

// A′: armata dal PRG, il telefono non la tiene armata (né riarma né allunga): il tenuto del PRG che segue non conferma.
static void test_attack_phone_keeps_confirm_armed() {
  Bench b(true);
  b.boot();
  listAt(b, MenuId::Security, ItemKind::Act, ScreenId::Home, ActId::Protect);
  b.press(2);  // avviso
  b.press(2);  // armata dal PRG
  TEST_ASSERT_TRUE(top(b.ui.model()).until > b.now);
  phoneEvA(b, pad::MenuEvent::Select);  // riarmare: no, e disarma
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).until);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  phoneEvA(b, pad::MenuEvent::HoldDown);  // tenuto 1,5 s: niente «resta armata altri 10 s»
  b.now += 1500;
  phoneEvA(b, pad::MenuEvent::HoldUp);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).until);
  b.ui.keyDown(b.now);  // l'utente tiene il PRG
  b.now += 3000;
  b.ui.gesture(Gesture::VeryLong, b.now, b.wc());
  b.ui.keyUp(b.now + 50);
  TEST_ASSERT_EQUAL(0, countCmd(b, NetCmd::ProtectKeys));
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  TEST_ASSERT_EQUAL_STRING("Prima 2X, poi tieni 3 s", b.ui.model().toast.msg);
}

// B: sul Permesso (NEGA) l'utente preme 2× per negare; fra le pressioni e la decodifica il telefono sceglie CONSENTI.
// Senza keyDown (fronti solo nel riconoscitore): l'inizio del gesto resta il HoldDown di openPerm, quindi la prova
// vale solo in parte; la copertura vera, coi fronti del loop, è test_prg_gesture_void_after_phone_change.
static void test_attack_prg_deny_becomes_allow() {
  Bench b;
  openPerm(b);
  b.rec.down((uint32_t)b.now); b.now += 80; b.rec.up((uint32_t)b.now); b.now += 150;
  b.rec.down((uint32_t)b.now); b.now += 80; b.rec.up((uint32_t)b.now);
  phoneEvA(b, pad::MenuEvent::Down);  // CONSENTI, prima che il 2x sia decodificato
  b.now += GestureRecognizer::kMultiMs + 1;
  b.ui.gesture(b.rec.poll((uint32_t)b.now), b.now, b.wc());
  int allow = 0;
  for (int i = 0; i < b.nEvents; i++) allow += b.events[i].outcome == PermOutcome::Allow;
  TEST_ASSERT_EQUAL_MESSAGE(0, allow, "Allow da un 2x PRG cominciato su NEGA");
}

// B con i fronti come nel loop (keyDown/keyUp): il 2× è nullo, la richiesta resta aperta con CONSENTI a schermo.
static void test_prg_gesture_void_after_phone_change() {
  Bench b;
  openPerm(b);
  b.now += 500;
  b.ui.keyDown(b.now);  // prima pressione su NEGA
  b.rec.down((uint32_t)b.now);
  b.now += 80;
  b.rec.up((uint32_t)b.now);
  b.ui.keyUp(b.now);
  phoneEvA(b, pad::MenuEvent::Down);  // CONSENTI fra le due pressioni
  b.now += 100;
  b.ui.keyDown(b.now);
  b.rec.down((uint32_t)b.now);
  b.now += 80;
  b.rec.up((uint32_t)b.now);
  b.ui.keyUp(b.now);
  b.now += GestureRecognizer::kMultiMs + 1;
  b.ui.gesture(b.rec.poll((uint32_t)b.now), b.now, b.wc());
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneRaceToast, b.ui.model().toast.msg);
  b.now += kPhoneSettleMs + 100;
  b.press(2);  // un 2× iniziato più di 2 s dopo: CONSENTI si vede a schermo e vale (scelta fatta prima del gesto)
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Prg, b.events[0].src);
}

// Regola 3 con l'Xbox: A premuto su una voce, il telefono sposta la selezione, il rilascio (Select) non vale.
static void test_xbox_select_void_after_phone_change() {
  Bench b(true);
  b.boot();
  listAt(b, MenuId::Security, ItemKind::Act, ScreenId::Home, ActId::Protect);
  const int sel0 = top(b.ui.model()).sel;
  b.state(pad::A);  // Xbox: A giù
  b.now += 40;
  phoneEvA(b, pad::MenuEvent::Up);
  TEST_ASSERT_TRUE(top(b.ui.model()).sel != sel0);
  b.now += 40;
  b.state(0);  // Select dell'Xbox
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneRaceToast, b.ui.model().toast.msg);
  b.now += kPhoneSettleMs + 100;
  b.tap(pad::A);  // un A nuovo, più di 2 s dopo: vale
  TEST_ASSERT_TRUE(b.topId() != ScreenId::List || strcmp(b.ui.model().toast.msg, kPhoneRaceToast) != 0);
}

// Regola 3 con l'Xbox sul Permesso: il telefono cambia la scelta e poi la rimette, durante il tenuto: nessuna risposta.
static void test_xbox_hold_void_after_phone_change() {
  Bench b;
  openPerm(b);
  b.state(pad::A);  // tenuto dell'Xbox su NEGA
  b.run(500);
  phoneEvA(b, pad::MenuEvent::Back);  // il telefono chiude il Permesso…
  b.ui.padEvent(pad::MenuEvent::Select, b.now, b.wc(), pad::Kind::Phone);  // …e lo riapre dal Dettaglio
  b.run(2700);
  b.state(0);
  for (int i = 0; i < b.nEvents; i++) TEST_ASSERT_TRUE(b.events[i].outcome != PermOutcome::Allow);
  for (int i = 0; i < b.nEvents; i++) TEST_ASSERT_TRUE(b.events[i].outcome != PermOutcome::Deny);
}

// C: il telefono non arma «Dimentica abbinamento»: un solo 2× del PRG non disabbina.
static void test_attack_phone_arms_unpair_one_prg_press() {
  Bench b(true);
  b.boot();
  b.ui.model().sys.pair = PairState::Paired;  // con la rete il banco parte senza abbinamento
  phoneToSecurity(b);
  phoneSelectSel(b, findItem(b, ItemKind::Act, ScreenId::Home, ActId::Unpair));
  phoneEvA(b, pad::MenuEvent::Select);  // armata dal telefono? no
  b.now += kPhoneSettleMs;  // oltre il margine della regola 3: il 2× del PRG vale, ma arma soltanto
  b.press(2);
  TEST_ASSERT_EQUAL_MESSAGE(PairState::Paired, b.ui.model().sys.pair, "un solo 2x PRG ha disabbinato");
  b.press(2);  // la seconda doppia pressione del PRG sì
  TEST_ASSERT_EQUAL(PairState::None, b.ui.model().sys.pair);
}

// Regola 2 da sola (senza telefono): il PRG è giù da prima che l'Xbox armasse Proteggi: lo scatto non conferma.
static void test_hold_started_before_arm_never_confirms() {
  Bench b(true);
  b.boot();
  listAt(b, MenuId::Security, ItemKind::Act, ScreenId::Home, ActId::Protect);
  b.tap(pad::A);  // avviso, dall'Xbox
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  b.now += 200;
  const Ms t0 = b.now;
  b.ui.keyDown(t0);  // PRG giù
  b.now += 500;
  b.ui.padEvent(pad::MenuEvent::Select, b.now, b.wc(), pad::Kind::Xbox);  // armata durante il tenuto del PRG
  TEST_ASSERT_TRUE(top(b.ui.model()).until > b.now);
  b.now = t0 + 3000;
  b.ui.gesture(Gesture::VeryLong, b.now, b.wc());
  TEST_ASSERT_EQUAL(0, countCmd(b, NetCmd::ProtectKeys));
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOk);
  b.ui.keyUp(b.now + 50);
  b.now += 200;
  b.ui.keyDown(b.now);  // un tenuto nuovo, dopo l'armatura: conferma
  b.now += 3000;
  b.ui.gesture(Gesture::VeryLong, b.now, b.wc());
  TEST_ASSERT_EQUAL(1, countCmd(b, NetCmd::ProtectKeys));
}

// Regola 4: dopo un evento del telefono la sorgente torna PRG; il registro dice da dove viene.
static void test_src_back_to_prg_and_log_kind() {
  Bench b;
  b.boot();
  b.ui.padEvent(pad::MenuEvent::Menu, b.now, b.wc(), pad::Kind::Phone);
  TEST_ASSERT_EQUAL(InputSrc::Prg, UiProbe::src(b.ui));
  TEST_ASSERT_EQUAL_STRING("Telefono: Menu", b.ui.lastInput());
  b.ui.padEvent(pad::MenuEvent::Down, b.now, b.wc(), pad::Kind::Xbox);
  TEST_ASSERT_EQUAL(InputSrc::Prg, UiProbe::src(b.ui));
  TEST_ASSERT_EQUAL_STRING("Controller: giù", b.ui.lastInput());
}

// Prova a tappeto con il PRG (2×, lunga, 3 s) e l'Xbox (A, A tenuto 3 s) intercalati: uno o due eventi del telefono
// dentro il gesto, a tempi diversi, da ogni punto di partenza. Mai un consenso; mai un effetto vietato, salvo il gesto
// legittimo del PRG o dell'Xbox su uno stato pronto (Pair, voci Wi-Fi) quando il telefono non ha cambiato niente.
enum class Gx : uint8_t { Prg2, PrgLong, PrgVeryLong, XboxTap, XboxHold, Count };
struct GxPlan {
  Ms edges[4];  // fronti: giù, su, giù, su (−1 = niente)
  Ms end;       // il gesto è risolto entro qui
  Ms slots[4];  // istanti per gli eventi del telefono, dopo la prima pressione e prima della risoluzione
  int nSlots;
};
static const GxPlan kGx[] = {
    {{0, 80, 230, 310}, 800, {10, 150, 260, 600}, 4},  // 2× (decodifica a 691 ms)
    {{0, 1000, -1, -1}, 1100, {10, 500, 990, 0}, 3},    // lunga
    {{0, 3100, -1, -1}, 3200, {10, 1500, 2990, 0}, 3},  // tenuto 3 s (scatta a 3000 col tasto giù)
    {{0, 80, -1, -1}, 200, {10, 70, 0, 0}, 2},          // A dell'Xbox (Select al rilascio)
    {{0, 3100, -1, -1}, 3200, {10, 1500, 2990, 0}, 3},  // A tenuto dell'Xbox (HoldFire a 3000)
};
static const pad::MenuEvent kPhoneEv[8] = {pad::MenuEvent::Up,       pad::MenuEvent::Down,   pad::MenuEvent::Select,
                                           pad::MenuEvent::Back,     pad::MenuEvent::Menu,   pad::MenuEvent::HoldDown,
                                           pad::MenuEvent::HoldUp,   pad::MenuEvent::HoldFire};

struct Look { ScreenId id; int depth, choice, sel; };
static Look look(const Bench &b) {
  const ScreenEntry &t = top(b.ui.model());
  return Look{t.id, b.ui.model().ui.depth, t.choice, t.sel};
}
static bool differs(const Look &a, const Look &c) {
  return a.id != c.id || a.depth != c.depth || a.choice != c.choice || a.sel != c.sel;
}

// Un gesto g con gli eventi del telefono e1 (a s1) ed e2 (a s2, −1 = niente). moved: il telefono ha cambiato qualcosa.
static void runGesture(Bench &b, Gx g, int e1, int s1, int e2, int s2, bool &moved, bool &allowSeen) {
  const GxPlan &P = kGx[static_cast<int>(g)];
  const bool xbox = g == Gx::XboxTap || g == Gx::XboxHold;
  const Ms t0 = b.now;
  int nextEdge = 0;
  for (Ms dt = 0; dt <= P.end; dt += 10) {
    b.now = t0 + dt;
    const uint32_t T = static_cast<uint32_t>(b.now);
    while (nextEdge < 4 && P.edges[nextEdge] >= 0 && P.edges[nextEdge] == dt) {
      const bool down = nextEdge % 2 == 0;
      if (xbox) {
        pad::State s;
        memset(&s, 0, sizeof s);
        s.buttons = down ? pad::A : 0;
        s.src = pad::Kind::Xbox;
        pad::MenuEvent ev[8];
        const int n = b.map.update(s, T, ev, 8);
        for (int i = 0; i < n; i++) b.ui.padEvent(ev[i], b.now, b.wc(), pad::Kind::Xbox);
      } else if (down) {
        b.rec.down(T);
        b.ui.keyDown(b.now);
      } else {
        const Gesture r = b.rec.up(T);
        b.ui.keyUp(b.now);
        if (r != Gesture::None) b.ui.gesture(r, b.now, b.wc());
      }
      nextEdge++;
    }
    const int ev[2] = {e1, e2}, sl[2] = {s1, s2};
    for (int k = 0; k < 2; k++)
      if (ev[k] >= 0 && P.slots[sl[k]] == dt) {
        const Look before = look(b);
        b.ui.padEvent(kPhoneEv[ev[k]], b.now, b.wc(), pad::Kind::Phone);
        moved = moved || differs(before, look(b));
      }
    if (xbox) {
      pad::MenuEvent out[4];
      const int n = b.map.tick(T, out, 4);
      for (int i = 0; i < n; i++) b.ui.padEvent(out[i], b.now, b.wc(), pad::Kind::Xbox);
    } else {
      const Gesture r = b.rec.poll(T);
      if (r != Gesture::None) b.ui.gesture(r, b.now, b.wc());
    }
    if (dt % 100 == 0) b.tick();
    allowSeen = allowSeen || strcmp(b.ui.model().toast.msg, "Consentito · inviato a Claude Code") == 0;
  }
  b.now += 500;
  b.tick();
}

static void test_prg_and_xbox_interleaved_with_phone() {
  const int nStarts = 1 + static_cast<int>(From::Count);  // 0 = Permesso con NEGA, poi i punti di startFrom
  int raced = 0, legit = 0, denies = 0;
  for (int st = 0; st < nStarts; st++)
    for (int g = 0; g < static_cast<int>(Gx::Count); g++) {
      const GxPlan &P = kGx[g];
      for (int e1 = 0; e1 < 8; e1++)
        for (int s1 = 0; s1 < P.nSlots; s1++)
          for (int e2 = -1; e2 < 8; e2++)
            for (int s2 = s1; s2 < (e2 < 0 ? s1 + 1 : P.nSlots); s2++) {
              std::unique_ptr<Bench> b(new Bench(st > 0));
              const From f = static_cast<From>(st - 1);
              if (st == 0) openPerm(*b);
              else startFrom(*b, f);
              b->src = pad::Kind::Xbox;
              b->now += 1000;
              b->tick();
              const Model &m = b->ui.model();
              const PairState pair0 = m.sys.pair;
              const int saved0 = m.nSaved;
              bool moved = false, allowSeen = false;
              runGesture(*b, static_cast<Gx>(g), e1, s1, e2, e2 < 0 ? -1 : s2, moved, allowSeen);
              int allows = allowSeen;
              for (int i = 0; i < b->nEvents; i++) {
                allows += b->events[i].outcome == PermOutcome::Allow;
                denies += b->events[i].outcome == PermOutcome::Deny;
              }
              bool effect = b->forbidden() > 0 || m.sys.keysProtected || m.sys.pair != pair0 || m.nSaved < saved0;
              for (int i = 0; i < m.ui.depth; i++)
                effect = effect || m.ui.stack[i].id == ScreenId::Qr || m.ui.stack[i].id == ScreenId::Connecting;
              // stati armati (Proteggi, Ripristino, Dimentica…): ogni evento del telefono disarma, mai un effetto.
              // Stati pronti non armati (Pair, voci Wi-Fi): il gesto del PRG o dell'Xbox vale se il telefono non ha
              // cambiato niente durante il gesto (regola 3).
              const bool armed = st > 0 && (f == From::Protect || f == From::Factory || f == From::Unpair ||
                                            f == From::ForgetAll || f == From::ForgetNet);
              const bool bad = allows > 0 || (effect && (st == 0 || armed || moved));
              if (bad) {
                char msg[96];
                snprintf(msg, sizeof msg, "partenza %d gesto %d e1 %d s1 %d e2 %d s2 %d", st, g, e1, s1, e2, s2);
                TEST_FAIL_MESSAGE(msg);
              }
              legit += effect;
              raced += strcmp(m.toast.msg, kPhoneRaceToast) == 0;
            }
    }
  TEST_ASSERT_TRUE(raced > 0);   // la regola 3 è stata raggiunta
  TEST_ASSERT_TRUE(legit > 0);   // la prova vede gli effetti (gesti legittimi del PRG sugli stati pronti)
  TEST_ASSERT_TRUE(denies > 0);  // e le risposte (NEGA dal PRG o dall'Xbox)
}

// Round 2 (R2-I1): il telefono sceglie CONSENTI 100 ms PRIMA della prima pressione del PRG (schermo ancora su NEGA:
// il parziale dura ~644 ms). Il 2× dell'utente, pensato per NEGA, non consente (margine kPhoneSettleMs).
static void test_r2_latency_window_allow() {
  Bench b;
  openPerm(b);
  b.now += 2000;
  b.ui.padEvent(pad::MenuEvent::Down, b.now, b.wc(), pad::Kind::Phone);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);
  b.now += 100;
  b.press(2);
  int allow = 0;
  for (int i = 0; i < b.nEvents; i++) allow += b.events[i].outcome == PermOutcome::Allow;
  TEST_ASSERT_EQUAL_MESSAGE(0, allow, "Allow: CONSENTI scelto dal telefono 100 ms prima del 2x, schermo non ancora aggiornato");
}

// Round 2: CONSENTI scelto dal telefono da 0 a 2 000 ms prima della prima pressione: né il 2× del PRG né il tenuto
// dell'Xbox consentono; a 2 100 ms la scelta è a schermo da abbastanza e il consenso è legittimo.
static int allowAfterPhone(Ms d, bool xbox) {
  Bench b;
  openPerm(b);
  b.now += 3000;
  b.ui.padEvent(pad::MenuEvent::Down, b.now, b.wc(), pad::Kind::Phone);
  b.now += d;
  if (xbox) {
    b.src = pad::Kind::Xbox;
    b.tap(pad::A, 3200);
  } else {
    b.press(2);
  }
  int allow = 0;
  for (int i = 0; i < b.nEvents; i++) allow += b.events[i].outcome == PermOutcome::Allow;
  return allow;
}
static void test_r2_settle_window_sweep() {
  for (int x = 0; x < 2; x++) {
    for (Ms d = 0; d <= 2000; d += 20) {  // 2 000 scritto qui, non preso dal codice
      char msg[48];
      snprintf(msg, sizeof msg, "%s a %d ms", x ? "Xbox" : "PRG", static_cast<int>(d));
      TEST_ASSERT_EQUAL_MESSAGE(0, allowAfterPhone(d, x == 1), msg);
    }
    TEST_ASSERT_EQUAL(1, allowAfterPhone(2100, x == 1));
  }
}

// Round 2 (R2-m1): su/giù del telefono sulla Home cambiano la sessione scelta (m.ui.sel): il 2× del PRG non apre
// il Dettaglio di un'altra sessione.
static void test_r2_home_session_change_voids_prg() {
  Bench b;
  b.boot();
  b.now += 3000;
  const int s0 = b.ui.model().ui.sel;
  b.ui.padEvent(pad::MenuEvent::Down, b.now, b.wc(), pad::Kind::Phone);
  TEST_ASSERT_TRUE(b.ui.model().ui.sel != s0);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  b.now += 300;
  b.press(2);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneRaceToast, b.ui.model().toast.msg);
}

// Round 2 (revisore): uso normale, il telefono naviga, 5 s dopo l'Xbox preme A: vale.
static void test_r2_xbox_select_later_ok() {
  Bench b;
  b.boot();
  b.ui.padEvent(pad::MenuEvent::Menu, b.now, b.wc(), pad::Kind::Phone);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  b.now += 5000;
  b.src = pad::Kind::Xbox;
  b.tap(pad::A);
  TEST_ASSERT_TRUE(strcmp(b.ui.model().toast.msg, kPhoneRaceToast) != 0);
  TEST_ASSERT_TRUE(b.ui.model().ui.depth > 2 || b.topId() != ScreenId::List);  // A ha aperto la voce
}


static MenuList padMenu(Bench &b) {
  Model &m = b.ui.model();
  m.sys.padHw = true;
  MenuList L;
  menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_FALSE(L.truncated);
  return L;
}

static int itemIndex(const MenuList &L, const char *label) {
  for (int i = 0; i < L.n; i++)
    if (strcmp(L.items[i].label, label) == 0) return i;
  return -1;
}

// Sceglie la voce sel di Menu › Gioco › Controller col controller (gbAct è privato: si passa dal menu, come sulla scheda).
static void pick(Bench &b, int sel) {
  b.ui.model().sys.padHw = true;
  if (b.topId() != ScreenId::List || top(b.ui.model()).menu != MenuId::GbPad) pushList(b, MenuId::GbPad);
  top(b.ui.model()).sel = sel;
  b.tap(pad::A);
}

static void test_gb_pad_phone_items() {
  Bench b;
  b.boot();
  const MenuList L = padMenu(b);
  TEST_ASSERT_EQUAL(5, L.n);
  TEST_ASSERT_EQUAL(1, itemIndex(L, "Associa controller"));
  TEST_ASSERT_EQUAL(2, itemIndex(L, "Dimentica controller"));
  TEST_ASSERT_EQUAL(3, itemIndex(L, "Associa telefono"));
  TEST_ASSERT_EQUAL(4, itemIndex(L, "Dimentica telefono"));
  TEST_ASSERT_EQUAL(ActId::GbPairPhone, L.items[3].act);
  TEST_ASSERT_EQUAL(ActId::GbForgetPhone, L.items[4].act);
}

static void test_pair_phone_request() {
  Bench b;
  b.boot();
  pick(b, 3);  // Associa telefono
  TEST_ASSERT_TRUE(b.ui.model().set.padOn);
  TEST_ASSERT_EQUAL(PadReq::PairPhone, b.ui.takePadReq());
  TEST_ASSERT_EQUAL_STRING("Metti l'app Calamaio sulla scheda Controller", b.ui.model().toast.msg);
}

// Review Focus «abbinamento sovrascritto»: dimenticare il telefono non tocca il controller, il Bluetooth resta acceso.
static void test_forget_phone_keeps_controller() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.set.padOn = true;
  m.sys.padBonded = true;
  m.sys.padPhoneBonded = true;
  pick(b, 4);  // Dimentica telefono
  TEST_ASSERT_EQUAL_STRING("Scegli ancora per confermare", m.toast.msg);
  pick(b, 4);
  TEST_ASSERT_EQUAL(PadReq::ForgetPhone, b.ui.takePadReq());
  TEST_ASSERT_FALSE(m.sys.padPhoneBonded);
  TEST_ASSERT_TRUE(m.sys.padBonded);
  TEST_ASSERT_TRUE(m.set.padOn);
  TEST_ASSERT_EQUAL_STRING("Telefono dimenticato", m.toast.msg);
}

static void test_forget_controller_keeps_phone_radio() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.set.padOn = true;
  m.sys.padBonded = true;
  m.sys.padPhoneBonded = true;
  pick(b, 2);  // Dimentica controller
  pick(b, 2);
  TEST_ASSERT_EQUAL(PadReq::Forget, b.ui.takePadReq());
  TEST_ASSERT_TRUE(m.set.padOn);  // resta il telefono
  TEST_ASSERT_TRUE(m.sys.padPhoneBonded);
}

static void test_forget_phone_none() {
  Bench b;
  b.boot();
  pick(b, 4);
  TEST_ASSERT_EQUAL_STRING("Nessun telefono associato", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
}

static void test_phone_link_toasts() {
  Bench b;
  b.boot();
  b.ui.model().set.padOn = true;
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, false, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Telefono collegato", b.ui.model().toast.msg);
  const MenuList L = padMenu(b);
  TEST_ASSERT_EQUAL_STRING("telefono collegato", L.items[0].val);
  b.now += 5000;
  b.ui.padLinkChanged(pad::Link::Connecting, pad::Kind::Phone, false, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Telefono scollegato", b.ui.model().toast.msg);
  b.now += 5000;
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Xbox, true, true, 80, b.now);
  TEST_ASSERT_EQUAL_STRING("Controller collegato", b.ui.model().toast.msg);
}

// Dopo «Dimentica telefono» lo scollegamento che segue non si annuncia (come «Dimentica controller»).
static void test_forget_phone_then_disconnect_is_quiet() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.set.padOn = true;
  m.sys.padBonded = true;
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, true, true, -1, b.now);
  pick(b, 4);
  pick(b, 4);
  b.now += 200;
  b.ui.padLinkChanged(pad::Link::Connecting, pad::Kind::Phone, true, false, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Telefono dimenticato", m.toast.msg);
}

static void test_passkey_screen() {
  Bench b;
  b.boot();
  b.ui.model().set.padOn = true;
  b.ui.padLinkChanged(pad::Link::Pairing, pad::Kind::Phone, false, false, -1, b.now);
  b.ui.padPasskey(42, b.now);
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  TEST_ASSERT_EQUAL_STRING("000042", top(b.ui.model()).code);
  b.ui.padPasskey(987654, b.now + 10);  // secondo tentativo: stesso schermo, codice nuovo
  TEST_ASSERT_EQUAL_STRING("987654", top(b.ui.model()).code);
  TEST_ASSERT_EQUAL(1, [&] { int n = 0; for (int i = 0; i < b.ui.model().ui.depth; i++) n += b.ui.model().ui.stack[i].id == ScreenId::PadCode; return n; }());
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, false, true, -1, b.now + 20);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
  TEST_ASSERT_EQUAL_STRING("Telefono collegato", b.ui.model().toast.msg);
}

static void test_passkey_failed() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  b.ui.padPairFailed(b.now + 10);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
  TEST_ASSERT_EQUAL_STRING("Associazione non riuscita", b.ui.model().toast.msg);
}

// LUNGA ANNULLA sul codice: via la schermata, l'associazione si ferma; gli altri gesti non portano via.
static void test_passkey_long_cancels() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  b.press(3);  // Menu: resta
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  b.tap(pad::B);  // B = indietro come la pressione lunga
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
  TEST_ASSERT_EQUAL(PadReq::CancelPair, b.ui.takePadReq());
}

static void test_padcode_key() { TEST_ASSERT_EQUAL_STRING("padcode", screenKey(ScreenId::PadCode)); }

// La schermata si disegna e non è vuota (testi presenti nei caratteri).
static void test_padcode_draws() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  static Canvas<250, 122> cv;
  ui::compose(cv, kHeltec213, b.ui.model(), 0, b.now, b.wc());
  int ink = 0;
  for (int y = 30; y < 80; y++)
    for (int x = 0; x < 250; x++) ink += cv.get(x, y);
  TEST_ASSERT_TRUE(ink > 300);  // le 6 cifre grandi
}

// F8: «conferma?» di «Dimentica telefono» scade a schermo come quello del controller; il Ripristino la chiude.
static void test_gb_forget_phone_confirm_expires() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.sys.padHw = true;
  m.set.padOn = true;
  m.sys.padPhoneBonded = true;
  pushList(b, MenuId::GbPad);
  top(m).sel = 4;  // Dimentica telefono
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[4].val);
  const Ms until = m.ui.gbForgetPhoneUntil;
  TEST_ASSERT_TRUE(until > b.now);
  b.run(until - b.now);
  TEST_ASSERT_EQUAL(until, m.ui.gbForgetPhoneUntil);
  b.now += 1;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());
  TEST_ASSERT_EQUAL(0, m.ui.gbForgetPhoneUntil);
  TEST_ASSERT_EQUAL_STRING("", b.ui.items(b.now, b.wc()).items[4].val);
  pushList(b, MenuId::Security);
  const int k = findItem(b, ItemKind::Act, ScreenId::Home, ActId::Factory);
  top(m).sel = k;
  b.tap(pad::A);
  b.tap(pad::A);  // arma
  m.ui.gbForgetPhoneUntil = b.now + kForgetMs;
  b.tap(pad::A, 3200);  // conferma dal controller
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
  TEST_ASSERT_EQUAL(0, m.ui.gbForgetPhoneUntil);
}

// F9: sul codice del telefono il tenuto di 3 s (PRG o controller) non mette Riposo sopra il codice.
static void test_padcode_hold_does_nothing() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  TEST_ASSERT_NULL(holdLabel(b.ui.model(), b.now));
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  b.now += 100;
  b.ui.gesture(Gesture::VeryLong, b.now, b.wc());
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  b.ui.padPasskey(654321, b.now);  // un secondo codice non ne impila un altro
  int codes = 0;
  for (int k = 0; k < b.ui.model().ui.depth; k++) codes += b.ui.model().ui.stack[k].id == ScreenId::PadCode;
  TEST_ASSERT_EQUAL(1, codes);
  b.tap(pad::B);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
}

// F7: in partita la pausa dice quale controller si è scollegato.
static void test_gb_pause_text_by_kind() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.gb.pause = gb::PauseWhy::Controller;
  MenuList L;
  m.sys.padKind = pad::Kind::Phone;
  menuList(MenuId::GbPause, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Telefono scollegato", L.items[0].label);
  m.sys.padKind = pad::Kind::Xbox;
  menuList(MenuId::GbPause, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Controller scollegato", L.items[0].label);
}

// Task 6 M2: il codice si toglie ovunque sia nella pila, non solo in cima (BattLow sopra lo farebbe riapparire).
static int padCodes(const Model &m) {
  int n = 0;
  for (int k = 0; k < m.ui.depth; k++) n += m.ui.stack[k].id == ScreenId::PadCode;
  return n;
}
static void test_padcode_removed_under_battlow() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.set.padOn = true;
  b.ui.padLinkChanged(pad::Link::Pairing, pad::Kind::Phone, false, false, -1, b.now);
  b.ui.padPasskey(123456, b.now);
  pushScreen(m, makeScreen(ScreenId::BattLow));
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, false, true, -1, b.now + 10);
  TEST_ASSERT_EQUAL(ScreenId::BattLow, b.topId());
  TEST_ASSERT_EQUAL(0, padCodes(m));
  // stesso caso con l'associazione non riuscita
  b.ui.padLinkChanged(pad::Link::Pairing, pad::Kind::Phone, false, false, -1, b.now + 20);
  b.ui.padPasskey(654321, b.now + 30);
  pushScreen(m, makeScreen(ScreenId::BattLow));
  b.ui.padPairFailed(b.now + 40);
  TEST_ASSERT_EQUAL(ScreenId::BattLow, b.topId());
  TEST_ASSERT_EQUAL(0, padCodes(m));
  // un codice nuovo con il vecchio sepolto: uno solo, in cima
  b.ui.padPasskey(111111, b.now + 50);
  pushScreen(m, makeScreen(ScreenId::BattLow));
  b.ui.padPasskey(222222, b.now + 60);
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  TEST_ASSERT_EQUAL_STRING("222222", top(m).code);
  TEST_ASSERT_EQUAL(1, padCodes(m));
}

// Carry-forward 4: abbinamento perso (blepad::takeBondLost), toast per tipo.
static void test_bond_lost_toast() {
  Bench b;
  b.boot();
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.padBondLost(pad::Kind::Xbox, b.now));
  TEST_ASSERT_EQUAL_STRING("Abbinamento perso: scegli Associa controller", b.ui.model().toast.msg);
  b.ui.padBondLost(pad::Kind::Phone, b.now + 10);
  TEST_ASSERT_EQUAL_STRING("Abbinamento perso: scegli Associa telefono", b.ui.model().toast.msg);
}

// Terza barriera (§7 scelta A, R1, F11) senza passare dall'Ui: eventi costruiti a mano, come se le prime due
// barriere fossero state aggirate. È la funzione che chiamano onPerm e onNetRequest in main.cpp.
static void test_third_barrier_without_ui() {
  PermEvent e;
  memset(&e, 0, sizeof e);
  e.outcome = PermOutcome::Allow;
  e.src = InputSrc::Phone;
  TEST_ASSERT_TRUE(dropFromPhone(e));
  e.outcome = PermOutcome::Deny;
  TEST_ASSERT_FALSE(dropFromPhone(e));
  e.outcome = PermOutcome::Cancelled;
  TEST_ASSERT_FALSE(dropFromPhone(e));
  e.outcome = PermOutcome::Allow;
  e.src = InputSrc::Xbox;
  TEST_ASSERT_FALSE(dropFromPhone(e));
  e.src = InputSrc::Prg;
  TEST_ASSERT_FALSE(dropFromPhone(e));

  NetRequest r;
  memset(&r, 0, sizeof r);
  for (NetCmd c : kNeverFromPhone) {
    r.cmd = c;
    r.src = InputSrc::Phone;
    TEST_ASSERT_TRUE_MESSAGE(dropFromPhone(r), netCmdKey(c));
    r.src = InputSrc::Xbox;
    TEST_ASSERT_FALSE_MESSAGE(dropFromPhone(r), netCmdKey(c));
    r.src = InputSrc::Prg;
    TEST_ASSERT_FALSE_MESSAGE(dropFromPhone(r), netCmdKey(c));
  }
  r.src = InputSrc::Phone;
  r.cmd = NetCmd::PermReply;  // un perm-reply «consenti» dal telefono, da qualunque via
  r.ok = true;
  TEST_ASSERT_TRUE(dropFromPhone(r));
  r.ok = false;
  TEST_ASSERT_FALSE(dropFromPhone(r));
  const NetCmd yes[] = {NetCmd::Reboot, NetCmd::Scan, NetCmd::CancelProvision, NetCmd::PairCancel, NetCmd::Dismiss};
  for (NetCmd c : yes) {
    r.cmd = c;
    TEST_ASSERT_FALSE_MESSAGE(dropFromPhone(r), netCmdKey(c));
  }
}

// Round 1, I1: l'Xbox tiene A nel Permesso, cade e il telefono si collega dentro un solo giro del loop (Ready → Ready,
// contatore di collegamento cambiato); il telefono non manda ancora niente. Il tick a +3 s non deve dare HoldFire.
static void test_link_change_ready_to_ready_resets_hold() {
  Bench b;
  openPerm(b);
  top(b.ui.model()).choice = 0;  // CONSENTI scelto
  b.src = pad::Kind::Xbox;
  b.state(pad::A);  // A giù, tenuto in corso
  b.run(1000);
  const pad::LinkEdge e = pad::linkEdge(pad::Link::Ready, 1, pad::Link::Ready, 2);
  TEST_ASSERT_TRUE(e.reset);
  TEST_ASSERT_TRUE(e.lost);
  TEST_ASSERT_TRUE(e.gained);
  if (e.reset) {  // come padLoop: reset prima di mappare qualunque stato nuovo
    pad::MenuEvent ev[4];
    b.feed(ev, b.map.reset(ev, 4));
  }
  b.run(3000);  // nessun report: solo tick
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
}

// m2: Ready → Ready con il tipo cambiato (Xbox → telefono in un solo giro) annuncia il collegamento nuovo.
static void test_ready_to_ready_kind_change_toast() {
  Bench b;
  b.boot();
  b.ui.model().set.padOn = true;
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Xbox, true, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Controller collegato", b.ui.model().toast.msg);
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, true, true, -1, b.now + 10);
  TEST_ASSERT_EQUAL_STRING("Telefono collegato", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(pad::Kind::Phone, b.ui.model().sys.padKind);
}

static void test_link_edge_cases() {
  using pad::Link;
  pad::LinkEdge e = pad::linkEdge(Link::Ready, 3, Link::Ready, 3);  // stesso collegamento
  TEST_ASSERT_FALSE(e.reset || e.lost || e.gained);
  e = pad::linkEdge(Link::Ready, 3, Link::Connecting, 4);  // caduto
  TEST_ASSERT_TRUE(e.reset && e.lost && !e.gained);
  e = pad::linkEdge(Link::Ready, 3, Link::Connecting, 3);  // non pronto, contatore non ancora cresciuto
  TEST_ASSERT_TRUE(e.reset && e.lost && !e.gained);
  e = pad::linkEdge(Link::Connecting, 4, Link::Ready, 4);  // collegato
  TEST_ASSERT_TRUE(!e.reset && !e.lost && e.gained);
  e = pad::linkEdge(Link::Connecting, 4, Link::Connecting, 5);  // tentativo fallito: reset innocuo
  TEST_ASSERT_TRUE(e.reset && !e.lost && !e.gained);
  e = pad::linkEdge(Link::Ready, 255, Link::Ready, 0);  // giro del contatore
  TEST_ASSERT_TRUE(e.reset && e.lost && e.gained);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_single_a_never_answers);
  RUN_TEST(test_hold_a_allows_chosen);
  RUN_TEST(test_hold_a_denies_preselected);
  RUN_TEST(test_hold_released_early_keeps_request);
  RUN_TEST(test_b_cancels_like_long_press);
  RUN_TEST(test_hold_label_on_perm);
  RUN_TEST(test_perm_closed_mid_hold);
  RUN_TEST(test_hold_started_elsewhere_never_answers);
  RUN_TEST(test_disconnect_mid_hold);
  RUN_TEST(test_prg_down_wins_over_pad_hold);
  RUN_TEST(test_menu_navigation);
  RUN_TEST(test_confirm_same_as_prg);
  RUN_TEST(test_game_up_down_like_prg);
  RUN_TEST(test_game_a_does_not_choose);
  RUN_TEST(test_choice_change_mid_hold_never_answers);
  RUN_TEST(test_prg_path_unaffected);
  RUN_TEST(test_main_menu_gioco_always);
  RUN_TEST(test_alto_o_basso_moved_into_gioco);
  RUN_TEST(test_gb_pad_items);
  RUN_TEST(test_gb_pad_phone_items);
  RUN_TEST(test_pair_phone_request);
  RUN_TEST(test_forget_phone_keeps_controller);
  RUN_TEST(test_forget_controller_keeps_phone_radio);
  RUN_TEST(test_forget_phone_none);
  RUN_TEST(test_phone_link_toasts);
  RUN_TEST(test_forget_phone_then_disconnect_is_quiet);
  RUN_TEST(test_passkey_screen);
  RUN_TEST(test_passkey_failed);
  RUN_TEST(test_passkey_long_cancels);
  RUN_TEST(test_padcode_key);
  RUN_TEST(test_padcode_draws);
  RUN_TEST(test_gb_forget_phone_confirm_expires);
  RUN_TEST(test_padcode_hold_does_nothing);
  RUN_TEST(test_gb_pause_text_by_kind);
  RUN_TEST(test_gb_roms_hook_empty);
  RUN_TEST(test_gb_status_values);
  RUN_TEST(test_gb_pair_turns_bluetooth_on);
  RUN_TEST(test_gb_forget_needs_confirm);
  RUN_TEST(test_gb_forget_confirm_expires);
  RUN_TEST(test_pad_link_toasts);
  RUN_TEST(test_forget_then_disconnect_is_quiet);
  RUN_TEST(test_choice_change_mid_hold_toast);
  RUN_TEST(test_gb_playing_stub);
  RUN_TEST(test_phone_hold_consenti_refused);
  RUN_TEST(test_phone_hold_nega_answers);
  RUN_TEST(test_prg_allows_after_phone_refusal);
  RUN_TEST(test_xbox_still_allows);
  RUN_TEST(test_phone_hold_fire_from_tick_refused);
  RUN_TEST(test_phone_never_allows_any_sequence);
  RUN_TEST(test_perm_event_src);
  RUN_TEST(test_phone_never_factory_reset);
  RUN_TEST(test_phone_never_protects_keys);
  RUN_TEST(test_phone_never_confirms_pc_pairing);
  RUN_TEST(test_phone_never_unpairs);
  RUN_TEST(test_phone_never_forgets_all_networks);
  RUN_TEST(test_phone_never_forgets_a_network);
  RUN_TEST(test_phone_never_provisions);
  RUN_TEST(test_phone_reboot_allowed);
  RUN_TEST(test_phone_forbidden_list);
  RUN_TEST(test_second_barriers_alone);
  RUN_TEST(test_xbox_still_forgets);
  RUN_TEST(test_attack_prg_sleep_hold_becomes_protect);
  RUN_TEST(test_attack_phone_keeps_confirm_armed);
  RUN_TEST(test_attack_prg_deny_becomes_allow);
  RUN_TEST(test_prg_gesture_void_after_phone_change);
  RUN_TEST(test_xbox_select_void_after_phone_change);
  RUN_TEST(test_xbox_hold_void_after_phone_change);
  RUN_TEST(test_attack_phone_arms_unpair_one_prg_press);
  RUN_TEST(test_hold_started_before_arm_never_confirms);
  RUN_TEST(test_src_back_to_prg_and_log_kind);
  RUN_TEST(test_prg_and_xbox_interleaved_with_phone);
  RUN_TEST(test_r2_latency_window_allow);
  RUN_TEST(test_r2_settle_window_sweep);
  RUN_TEST(test_r2_home_session_change_voids_prg);
  RUN_TEST(test_r2_xbox_select_later_ok);
  RUN_TEST(test_padcode_removed_under_battlow);
  RUN_TEST(test_bond_lost_toast);
  RUN_TEST(test_third_barrier_without_ui);
  RUN_TEST(test_link_change_ready_to_ready_resets_hold);
  RUN_TEST(test_link_edge_cases);
  RUN_TEST(test_ready_to_ready_kind_change_toast);
  return UNITY_END();
}
