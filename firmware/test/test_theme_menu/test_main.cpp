// Menu › Display › Tema (0.13.0): voce in fondo a Display, lista dei temi con ✓ sull'attivo, 2× sceglie.
#include <string.h>
#include <unity.h>

#include "core/gesture.h"
#include "core/menu.h"
#include "core/strbuf.h"
#include "core/ui.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static const Ms kNow = 1791707400000LL;
static const WallClock kWc = {kNow};

static void test_display_has_theme_last() {
  static Ui ui(kHeltec213);
  ui.begin(kNow, kWc);
  static MenuList L;
  menuList(MenuId::Display, kHeltec213, ui.model(), kNow, kWc, L);
  TEST_ASSERT_FALSE(L.truncated);
  const MenuItem &it = L.items[L.n - 1];
  TEST_ASSERT_EQUAL_STRING("Tema", it.label);
  TEST_ASSERT_EQUAL_STRING("Calamaio", it.val);
  TEST_ASSERT_EQUAL(int(ItemKind::Go), int(it.kind));
  TEST_ASSERT_EQUAL(int(MenuId::Theme), int(it.go));
  TEST_ASSERT_EQUAL_STRING("tema", menuKey(MenuId::Theme));
}

static void test_theme_list_and_pick() {
  static Ui ui(kHeltec213);
  ui.begin(kNow, kWc);
  Model &m = ui.model();
  m.themes.n = 2;
  scopy(m.themes.items[0].id, "prova");
  scopy(m.themes.items[0].name, "Mascotte grande");
  scopy(m.themes.items[1].id, "lista");
  scopy(m.themes.items[1].name, "Righe a pagine");
  m.themes.cur = 1;
  static MenuList L;
  menuList(MenuId::Theme, kHeltec213, m, kNow, kWc, L);
  TEST_ASSERT_EQUAL_STRING("Tema", L.title);
  TEST_ASSERT_EQUAL(3, L.n);
  TEST_ASSERT_EQUAL_STRING("Calamaio", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("", L.items[0].val);
  TEST_ASSERT_EQUAL_STRING("\xe2\x9c\x93", L.items[2].val);  // ✓ sull'attivo
  menuList(MenuId::Display, kHeltec213, m, kNow, kWc, L);
  TEST_ASSERT_EQUAL_STRING("Righe a pagine", L.items[L.n - 1].val);
  // dalla lista: seconda voce («Mascotte grande»), 2× sceglie
  resetStack(m, makeScreen(ScreenId::Home));
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = MenuId::Theme;
  e.sel = 1;
  pushScreen(m, e);
  ui.gesture(Gesture::Press2, kNow, kWc);
  TEST_ASSERT_EQUAL(0, m.themes.cur);
  TEST_ASSERT_EQUAL_STRING("Tema: Mascotte grande", ui.lastAction());
  top(m).sel = 0;  // «Calamaio»
  ui.gesture(Gesture::Press2, kNow, kWc);
  TEST_ASSERT_EQUAL(-1, m.themes.cur);
}

// Catalogo cambiato sotto la lista (tema tolto dal task di rete o dal loop, che poi ridisegna la lista): la voce sotto
// il cursore che non c'è più non sceglie un altro tema. La lista si rifà dal modello a ogni tasto: «Righe a pagine»
// (indice 1) sparita con il cursore lì sopra.
static void test_theme_pick_stale_index() {
  static Ui ui(kHeltec213);
  ui.begin(kNow, kWc);
  Model &m = ui.model();
  m.themes.n = 2;
  scopy(m.themes.items[0].id, "prova");
  scopy(m.themes.items[0].name, "Mascotte grande");
  scopy(m.themes.items[1].id, "lista");
  scopy(m.themes.items[1].name, "Righe a pagine");
  m.themes.cur = -1;
  resetStack(m, makeScreen(ScreenId::Home));
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = MenuId::Theme;
  e.sel = 2;
  pushScreen(m, e);
  m.themes.n = 1;
  ui.gesture(Gesture::Press2, kNow, kWc);
  TEST_ASSERT_NOT_EQUAL(0, m.themes.cur);  // mai «Mascotte grande», che a schermo non era sotto il cursore
  TEST_ASSERT_EQUAL(-1, m.themes.cur);
  // themePick con l'indice vecchio (k ≥ n): di serie
  m.themes.cur = 0;
  m.themes.picked = false;
  ui.themePick(1, kNow);
  TEST_ASSERT_EQUAL(-1, m.themes.cur);
  TEST_ASSERT_TRUE(m.themes.picked);  // anche senza cambio di cur: il loop salva la scelta
  TEST_ASSERT_EQUAL_STRING("Tema: Calamaio", ui.lastAction());
  ui.themePick(0, kNow);
  TEST_ASSERT_EQUAL(0, m.themes.cur);
  ui.themePick(-7, kNow);
  TEST_ASSERT_EQUAL(-1, m.themes.cur);
}

// themeSync legge la scelta prima di tutto (tema rotto, ritorno dalla partita: themesToModel riscrive cur): takeThemePick
// la copia e la consuma; quello che cambia dopo in m.themes non la tocca.
static void test_take_theme_pick() {
  static Ui ui(kHeltec213);
  ui.begin(kNow, kWc);
  Model &m = ui.model();
  m.themes.n = 2;
  scopy(m.themes.items[0].id, "prova");
  scopy(m.themes.items[1].id, "lista");
  m.themes.cur = 0;
  char id[kAvatarRefLen] = "x";
  TEST_ASSERT_FALSE(takeThemePick(m.themes, id, sizeof id));  // nessuna scelta
  ui.themePick(1, kNow);
  TEST_ASSERT_TRUE(takeThemePick(m.themes, id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("lista", id);
  TEST_ASSERT_FALSE(m.themes.picked);
  m.themes.cur = 0;  // themesToModel dal tema attivo
  TEST_ASSERT_EQUAL_STRING("lista", id);
  TEST_ASSERT_FALSE(takeThemePick(m.themes, id, sizeof id));
  ui.themePick(-1, kNow);  // Calamaio
  TEST_ASSERT_TRUE(takeThemePick(m.themes, id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("", id);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_display_has_theme_last);
  RUN_TEST(test_theme_list_and_pick);
  RUN_TEST(test_theme_pick_stale_index);
  RUN_TEST(test_take_theme_pick);
  return UNITY_END();
}
