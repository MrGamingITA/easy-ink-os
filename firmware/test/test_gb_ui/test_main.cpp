// Giochi Game Boy nell'interfaccia (core/gb_ui.cpp, core/gb_menu.cpp, ui/screens): Menu › Gioco › ROM col gioco
// scritto via cavo, avvio, partita, pausa, uscita (anche verso il Riposo), bande con la finestra vuota.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "core/gb_frame.h"
#include "core/menu.h"
#include "core/ui.h"
#include "core/strbuf.h"
#include "gfx/canvas.h"
#include "gfx/text.h"
#include "ui/screens.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const int64_t kWall0 = wallFromCivil(2026, 10, 15, 10, 0, 0).localMs;

int indexOf(const MenuList &l, const char *label) {
  for (int i = 0; i < l.n; i++)
    if (strcmp(l.items[i].label, label) == 0) return i;
  return -1;
}

struct Bench {
  Ui ui;
  Ms now;
  Bench() : ui(kHeltec213), now(0) {
    ui.begin(now, wc());
    now += 2601;
    ui.tick(now, wc());
    ui.gbInstalled(true, true, "POKEMON RED", 1024);
  }
  WallClock wc() const { WallClock w; w.localMs = kWall0 + now; return w; }
  Model &m() { return ui.model(); }
  ScreenId topId() { return top(m()).id; }
  RefreshKind act(Action a) { return ui.act(a, now += 100, wc()); }
  void openRoms() {
    ScreenEntry e = makeScreen(ScreenId::List);
    e.menu = MenuId::GbRoms;
    pushScreen(m(), e);
  }
  MenuList items(MenuId id) {
    MenuList l;
    menuList(id, kHeltec213, m(), now, wc(), l);
    return l;
  }
  std::vector<std::string> texts(Canvas<250, 122> &cv) {
    std::vector<std::string> out;
    ui::setTextHook([](const char *s, int, int, int, int, bool, Align, void *ctx) {
      static_cast<std::vector<std::string> *>(ctx)->push_back(s);
    }, &out);
    ui::compose(cv, kHeltec213, m(), 0, now, wc());
    ui::setTextHook(nullptr, nullptr);
    return out;
  }
  bool shows(const char *s) {
    static Canvas<250, 122> cv;
    for (const std::string &t : texts(cv))
      if (t == s) return true;
    return false;
  }
  void play() {
    openRoms();
    ui.gbLoad(now);
    ui.gbPlay(now);
  }
};

}  // namespace

static void test_roms_menu_shows_the_rom_on_the_board() {
  static Bench b;
  const MenuList l = b.items(MenuId::GbRoms);
  TEST_ASSERT_EQUAL(1, l.n);
  TEST_ASSERT_EQUAL_STRING("POKEMON RED", l.items[0].label);
  TEST_ASSERT_EQUAL_STRING("sulla scheda", l.items[0].val);
  TEST_ASSERT_EQUAL(int(ActId::GbPick), int(l.items[0].act));
  b.ui.gbInstalled(true, true, "", 32);  // intestazione senza titolo
  TEST_ASSERT_EQUAL_STRING("Gioco senza titolo", b.items(MenuId::GbRoms).items[0].label);
  b.ui.gbInstalled(false, true, "POKEMON RED", 1024);  // tabella vecchia: la ROM non conta
  TEST_ASSERT_EQUAL_STRING("Giochi non disponibili", b.items(MenuId::GbRoms).items[0].label);
}

static void test_pick_needs_the_controller() {
  static Bench b;
  b.openRoms();
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(gb::GbAct::None), int(b.ui.takeGbAct()));
  TEST_ASSERT_EQUAL_STRING("Collega il controller per giocare", b.m().toast.msg);
  TEST_ASSERT_EQUAL(int(MenuId::GbRoms), int(top(b.m()).menu));
  b.m().sys.padLink = pad::Link::Ready;
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(gb::GbAct::Pick), int(b.ui.takeGbAct()));
  TEST_ASSERT_EQUAL(int(gb::GbAct::None), int(b.ui.takeGbAct()));  // una volta sola
}

static void test_load_screen_then_play() {
  static Bench b;
  b.openRoms();
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(b.ui.gbLoad(b.now)));
  TEST_ASSERT_EQUAL(ScreenId::GbLoad, b.topId());
  TEST_ASSERT_TRUE(b.ui.gbOnStack());
  TEST_ASSERT_TRUE(b.shows("GIOCO"));
  TEST_ASSERT_TRUE(b.shows("POKEMON RED"));
  TEST_ASSERT_TRUE(b.shows("Avvio…"));
  b.act(Action::Back);  // l'avvio non si annulla: dura pochi secondi
  TEST_ASSERT_EQUAL(int(gb::GbAct::None), int(b.ui.takeGbAct()));
  TEST_ASSERT_EQUAL(ScreenId::GbLoad, b.topId());
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(b.ui.gbPlay(b.now)));
  TEST_ASSERT_EQUAL(ScreenId::GbPlay, b.topId());
  TEST_ASSERT_TRUE(b.ui.gbPlaying());
}

static void test_play_bands_leave_window_blank() {
  static Bench b;
  b.play();
  b.m().sys.batt = 12;
  b.m().gb.lowBand = true;
  static Canvas<250, 122> cv;
  const std::vector<std::string> t = b.texts(cv);
  bool title = false, low = false, pause = false;
  for (const std::string &s : t) {
    title = title || s == "POKEM";
    low = low || s == "Batteria";
    pause = pause || s == "=PAUSA";
  }
  TEST_ASSERT_TRUE(title);
  TEST_ASSERT_TRUE(low);
  TEST_ASSERT_TRUE(pause);
  for (int y = 0; y < 122; y++)
    for (int x = gb::kWinX; x < gb::kWinX + gb::kWinW; x++)
      if (cv.get(x, y)) TEST_FAIL_MESSAGE("le bande sconfinano nella finestra del gioco");
}

// 0.12.5: bande di 45 px. Ogni testo (misurato col motore dei testi) sta nella sua banda, senza toccare i puntini
// del bordo (x 43 e 206) né la finestra; anche con «Batteria bassa», il 100 % e un titolo lungo.
static void test_play_texts_fit_their_band() {
  static Bench b;
  b.play();
  b.m().sys.batt = 100;
  b.m().gb.lowBand = true;
  scopy(b.m().gb.title, "POKEMON RED");
  struct Box {
    int n, bad;
  } box = {0, 0};
  static Canvas<250, 122> cv;
  ui::setTextHook([](const char *s, int x, int, int size, int weight, bool pix, Align, void *ctx) {
    Box &bx = *static_cast<Box *>(ctx);
    const int w = textWidth(s, size, weight, pix);
    const bool left = x < gb::kWinX;
    const bool ok = left ? x >= 0 && x + w <= gb::kWinX - 3 : x >= gb::kWinX + gb::kWinW + 3 && x + w <= 250;
    if (!ok) printf("fuori banda: «%s» x %d largo %d\n", s, x, w);
    bx.n++;
    bx.bad += !ok;
  }, &box);
  ui::compose(cv, kHeltec213, b.m(), 0, b.now, b.wc());
  ui::setTextHook(nullptr, nullptr);
  TEST_ASSERT_TRUE(box.n >= 10);
  TEST_ASSERT_EQUAL(0, box.bad);
}

static void test_prg_gesture_in_play_asks_pause() {
  static Bench b;
  b.play();
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(gb::GbAct::Pause), int(b.ui.takeGbAct()));
  TEST_ASSERT_EQUAL(ScreenId::GbPlay, b.topId());  // la pausa la apre il runtime con gbPaused
}

static void test_pause_menu_rows_and_actions() {
  static Bench b;
  b.play();
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(b.ui.gbPaused(gb::PauseWhy::Controller, b.now)));
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL(int(MenuId::GbPause), int(top(b.m()).menu));
  MenuList l = b.items(MenuId::GbPause);
  TEST_ASSERT_EQUAL(3, l.n);
  TEST_ASSERT_EQUAL_STRING("Controller scollegato", l.items[0].label);
  TEST_ASSERT_EQUAL_STRING("Riprendi", l.items[1].label);
  TEST_ASSERT_EQUAL_STRING("Salva ed esci", l.items[2].label);
  TEST_ASSERT_EQUAL(1, top(b.m()).sel);  // su Riprendi
  b.act(Action::Menu);  // niente Home con la partita aperta
  TEST_ASSERT_EQUAL(int(MenuId::GbPause), int(top(b.m()).menu));
  TEST_ASSERT_EQUAL(int(gb::GbAct::None), int(b.ui.takeGbAct()));
  b.act(Action::Back);  // B / pressione lunga = Riprendi
  TEST_ASSERT_EQUAL(int(gb::GbAct::Resume), int(b.ui.takeGbAct()));
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(b.ui.gbResumed(b.now)));
  TEST_ASSERT_EQUAL(ScreenId::GbPlay, b.topId());
  b.ui.gbPaused(gb::PauseWhy::User, b.now);
  l = b.items(MenuId::GbPause);
  TEST_ASSERT_EQUAL(2, l.n);  // niente riga d'avviso
  TEST_ASSERT_EQUAL(0, top(b.m()).sel);
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(gb::GbAct::Resume), int(b.ui.takeGbAct()));
  top(b.m()).sel = 1;
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(gb::GbAct::Exit), int(b.ui.takeGbAct()));
  b.act(Action::Sleep);
  TEST_ASSERT_EQUAL(int(gb::GbAct::ExitRest), int(b.ui.takeGbAct()));
}

static void test_pause_twice_updates_the_row() {
  static Bench b;
  b.play();
  b.ui.gbPaused(gb::PauseWhy::User, b.now);
  const int depth = b.m().ui.depth;
  b.ui.gbPaused(gb::PauseWhy::Idle, b.now);  // già in pausa: nessuna seconda pausa nella pila
  TEST_ASSERT_EQUAL(depth, b.m().ui.depth);
  TEST_ASSERT_EQUAL_STRING("Pausa automatica", b.items(MenuId::GbPause).items[0].label);
}

static void test_end_back_to_the_roms_menu_or_rest() {
  static Bench b;
  b.play();
  b.ui.gbPaused(gb::PauseWhy::User, b.now);
  b.m().nPerms = 1;  // Ui::gbEnd non tocca i permessi: li azzera il runtime solo dopo una modalità gioco vera
  b.ui.gbEnd("Partita salvata", false, b.now);
  TEST_ASSERT_EQUAL(1, b.m().nPerms);
  TEST_ASSERT_FALSE(b.ui.gbOnStack());
  TEST_ASSERT_EQUAL(int(MenuId::GbRoms), int(top(b.m()).menu));
  TEST_ASSERT_EQUAL_STRING("Partita salvata", b.m().toast.msg);
  TEST_ASSERT_EQUAL_STRING("", b.m().gb.title);
  b.play();
  b.ui.gbEnd("Partita salvata", true, b.now);  // 10 min in pausa: Riposo
  TEST_ASSERT_FALSE(b.ui.gbOnStack());
  TEST_ASSERT_EQUAL(ScreenId::Sleep, b.topId());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_roms_menu_shows_the_rom_on_the_board);
  RUN_TEST(test_pick_needs_the_controller);
  RUN_TEST(test_load_screen_then_play);
  RUN_TEST(test_play_bands_leave_window_blank);
  RUN_TEST(test_play_texts_fit_their_band);
  RUN_TEST(test_prg_gesture_in_play_asks_pause);
  RUN_TEST(test_pause_menu_rows_and_actions);
  RUN_TEST(test_pause_twice_updates_the_row);
  RUN_TEST(test_end_back_to_the_roms_menu_or_rest);
  return UNITY_END();
}
