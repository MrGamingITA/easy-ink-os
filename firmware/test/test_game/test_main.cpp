// 0.9.0: gioco «Alto o basso» (core/game, Ui, power): sequenza col seme del simulatore, giusto, pari, sbagliato,
// serie e record, nuova partita, permesso che interrompe, stato Attivo mentre si gioca, riga d'aiuto che ci sta.
#include <string.h>
#include <unity.h>

#include <string>

#include "core/game.h"
#include "core/menu.h"
#include "core/power.h"
#include "core/ui.h"
#include "gfx/text.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

// Carte scelte dal test: rank k = 1 + v % 13 con v = k − 1.
const int *gSeq = nullptr;
int gPos = 0;
uint32_t seqRand() { return uint32_t(gSeq[gPos++] - 1); }
void useSeq(const int *s) { gSeq = s; gPos = 0; }

struct Bench {
  Ui ui;
  Ms now;
  Bench() : ui(kHeltec213), now(1000000) {
    ui.begin(now, wc());
    now += 2700;
    ui.tick(now, wc());
  }
  WallClock wc() const { WallClock w; w.localMs = wallFromCivil(2026, 10, 11, 10, 30, 0).localMs + (now - 1000000); return w; }
  RefreshKind act(Action a) { return ui.act(a, now, wc()); }
  void openGame() {
    act(Action::Menu);
    act(Action::Next);
    act(Action::Next);
    act(Action::Ok);  // Gioco
    act(Action::Ok);  // Alto o basso
  }
  Game &g() { return ui.model().game; }
};

}  // namespace

// Stesso seme del simulatore (GAME_SEED): stesse carte delle catture (2, 10, J, 10, 7, K, 3, 2, 8, J).
static void test_seed_matches_simulator() {
  Game g;
  memset(&g, 0, sizeof g);
  g.seed = kGameSeed;
  const int want[] = {2, 10, 11, 10, 7, 13, 3, 2, 8, 11};
  for (int w : want) TEST_ASSERT_EQUAL(w, gameDraw(g, nullptr));
  TEST_ASSERT_EQUAL_STRING("A", kRanks[1]);
  TEST_ASSERT_EQUAL_STRING("10", kRanks[10]);
  TEST_ASSERT_EQUAL_STRING("K", kRanks[13]);
  // Con una sorgente esterna (esp_random sulla scheda) il seme non si usa.
  const int s[] = {5};
  useSeq(s);
  TEST_ASSERT_EQUAL(5, gameDraw(g, seqRand));
}

// Giusto: serie +1 e record; pari: serie invariata, partita avanti; sbagliato: fine, record tenuto.
static void test_win_tie_lose_record() {
  Game g;
  memset(&g, 0, sizeof g);
  g.best = 2;
  const int s[] = {7, 9, 9, 4, 4, 12, 1, 6, 3};
  useSeq(s);
  gameNew(g, seqRand);
  TEST_ASSERT_EQUAL(7, g.card);
  TEST_ASSERT_EQUAL(2, g.startBest);
  TEST_ASSERT_EQUAL(GameResult::Win, gameGuess(g, true, seqRand));   // 7 → 9 più alta
  TEST_ASSERT_EQUAL(1, g.streak);
  TEST_ASSERT_EQUAL(GameResult::Tie, gameGuess(g, true, seqRand));   // 9 → 9 pari
  TEST_ASSERT_EQUAL(1, g.streak);
  TEST_ASSERT_FALSE(g.over);
  TEST_ASSERT_EQUAL(GameResult::Win, gameGuess(g, false, seqRand));  // 9 → 4 più bassa
  TEST_ASSERT_EQUAL(GameResult::Tie, gameGuess(g, false, seqRand));  // 4 → 4: pari anche indovinando «più bassa»
  TEST_ASSERT_EQUAL(2, g.streak);
  TEST_ASSERT_EQUAL(2, g.best);
  TEST_ASSERT_EQUAL(GameResult::Win, gameGuess(g, true, seqRand));   // 4 → Q
  TEST_ASSERT_EQUAL(3, g.streak);
  TEST_ASSERT_EQUAL(3, g.best);  // record superato: sale subito
  TEST_ASSERT_EQUAL(MascotPose::Done, gamePose(g));
  TEST_ASSERT_EQUAL(GameResult::Lose, gameGuess(g, true, seqRand));  // Q → A: sbagliato
  TEST_ASSERT_TRUE(g.over);
  TEST_ASSERT_EQUAL(3, g.streak);
  TEST_ASSERT_EQUAL(3, g.best);
  TEST_ASSERT_EQUAL(MascotPose::Error, gamePose(g));
  TEST_ASSERT_TRUE(g.streak > g.startBest);  // «NUOVO RECORD!»
  gameNew(g, seqRand);
  TEST_ASSERT_EQUAL(0, g.streak);
  TEST_ASSERT_EQUAL(3, g.startBest);
  TEST_ASSERT_EQUAL(MascotPose::Input, gamePose(g));
  TEST_ASSERT_EQUAL(GameResult::Lose, gameGuess(g, true, seqRand));  // 6 → 3
  TEST_ASSERT_EQUAL(3, g.best);  // una serie peggiore non tocca il record
}

// Dal menu: carta nuova a ogni gesto (parziale), 1× alta, 2× bassa, lunga esce; a partita finita 1× o 2× ricomincia.
static void test_ui_flow() {
  static Bench b;
  b.openGame();
  TEST_ASSERT_EQUAL(ScreenId::Game, top(b.ui.model()).id);
  TEST_ASSERT_EQUAL(2, b.g().card);  // seme fisso: 2
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.act(Action::Next));  // 2 → 10
  TEST_ASSERT_EQUAL_STRING("Più alta: 2 → 10, giusto · serie 1", b.ui.lastAction());
  b.act(Action::Next);  // 10 → J
  b.act(Action::Ok);    // J → 10
  TEST_ASSERT_EQUAL(3, b.g().streak);
  b.act(Action::Next);  // 10 → 7: sbagliato
  TEST_ASSERT_TRUE(b.g().over);
  TEST_ASSERT_EQUAL_STRING("Più alta: 10 → 7, sbagliato · serie 3, record 3", b.ui.lastAction());
  b.act(Action::Ok);    // ancora
  TEST_ASSERT_FALSE(b.g().over);
  TEST_ASSERT_EQUAL(13, b.g().card);
  TEST_ASSERT_EQUAL_STRING("Nuova partita: K", b.ui.lastAction());
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.ui.act(Action::Back, b.now, b.wc(), true));  // esce
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("Alto o basso", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("record 3", L.items[0].val);
}

// Mentre si gioca lo stato resta Attivo anche dopo 60 s senza tasti; un permesso in arrivo interrompe la partita.
static void test_power_and_permission() {
  static Bench b;
  b.openGame();
  PowerInputs in = powerInputs(b.ui.model());
  TEST_ASSERT_TRUE(in.playing);
  in.busy = false;
  PowerManager pm;
  pm.begin(0);
  pm.update(in, 10 * 60000);
  TEST_ASSERT_EQUAL(PowerState::Active, pm.state());
  TEST_ASSERT_EQUAL_STRING("gioco in corso", pm.why());
  // Nel gioco il tenuto 3 s resta il Riposo.
  TEST_ASSERT_EQUAL_STRING("RIPOSO", holdLabel(b.ui.model(), b.now));
  // Permesso (con la rete: perm-request verificata).
  b.ui.goLive();
  b.ui.model().sys.pc = true;
  b.act(Action::Home);
  b.openGame();
  TEST_ASSERT_EQUAL(ScreenId::Game, top(b.ui.model()).id);
  PermReq r;
  memset(&r, 0, sizeof r);
  strcpy(r.id, "abc");
  strcpy(r.session, "s1");
  strcpy(r.name, "web-app");
  strcpy(r.tool, "Bash");
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.ui.netPermRequest(r, b.now, 0));
  TEST_ASSERT_EQUAL(ScreenId::Home, top(b.ui.model()).id);
  TEST_ASSERT_EQUAL(1, b.ui.model().ui.depth);
  TEST_ASSERT_EQUAL_STRING("Partita interrotta: un permesso", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(1, b.ui.model().nPerms);  // la richiesta resta: si risponde dal Dettaglio (2×), NEGA preselezionato
  // Senza rete (simulatore): raise() fa lo stesso.
  static Bench c;
  c.openGame();
  Rng rng(1);
  c.ui.raise(0, SessionState::Permission, c.now, rng);
  TEST_ASSERT_EQUAL(ScreenId::Home, top(c.ui.model()).id);
  // Una domanda (input) non interrompe.
  c.openGame();
  TEST_ASSERT_EQUAL(ScreenId::Game, top(c.ui.model()).id);
  c.ui.raise(2, SessionState::Input, c.now, rng);
  TEST_ASSERT_EQUAL(ScreenId::Game, top(c.ui.model()).id);
}

// Riga d'aiuto in Silkscreen 8: ci sta intera nei 242 px (niente «..»).
static void test_help_line_fits() {
  const char *lines[] = {"1X ALTA   2X BASSA   LUNGA ESCI", "1X O 2X ANCORA   LUNGA ESCI"};
  for (const char *l : lines) {
    char out[64];
    fitText(out, sizeof out, l, 8, 400, true, 250 - 8);
    TEST_ASSERT_EQUAL_STRING(l, out);
    TEST_ASSERT_TRUE(textWidth(l, 8, 400, true) <= 242);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_seed_matches_simulator);
  RUN_TEST(test_win_tie_lose_record);
  RUN_TEST(test_ui_flow);
  RUN_TEST(test_power_and_permission);
  RUN_TEST(test_help_line_fits);
  return UNITY_END();
}
