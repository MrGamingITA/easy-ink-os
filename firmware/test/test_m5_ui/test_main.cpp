// M5 con la rete vera (sys.live): cal::Ui con le richieste verificate dal task di rete (core/link).
// Test di accettazione di docs/M5-PERMESSI.md lato display: consenti, nega, nessuna scadenza, annulla con la
// lunga (la richiesta resta raggiungibile), risposto altrove, display fuori rete, niente set senza auth.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/menu.h"
#include "core/pad.h"
#include "core/strbuf.h"
#include "core/ui.h"
#include "gfx/canvas.h"
#include "ui/screens.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const int64_t kEpoch = 1791707400000LL;  // state.time di protocol/examples/state.json
const char *const kReq = "8f1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e";
const char *const kReq2 = "0011223344556677889900aabbccddee";

std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

JsonDocument doc;
StateMsg msg;

struct Bench {
  Ui ui;
  Ms now;
  std::vector<PermEvent> perms;
  std::vector<std::string> permIds;
  std::vector<NetRequest> reqs;
  Bench() : ui(kHeltec213), now(1000) {
    ui.setPermListener(&Bench::onPerm, this);
    ui.setNetListener(&Bench::onNet, this);
    ui.begin(now, wc());
    ui.goLive();
    now += 2601;
    ui.tick(now, wc());
    const std::string js = readFile("../protocol/examples/state.json");
    doc.clear();
    TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
    TEST_ASSERT_TRUE(decodeState(doc.as<JsonVariantConst>(), msg));
    ui.netState(msg, now);
  }
  WallClock wc() const { return WallClock{kEpoch + 2 * 3600000LL + now}; }
  int64_t epoch() const { return kEpoch + now; }
  static void onPerm(const PermEvent &e, void *ctx) {
    Bench *b = static_cast<Bench *>(ctx);
    b->perms.push_back(e);
    b->permIds.push_back(e.requestId);
  }
  static void onNet(const NetRequest &r, void *ctx) { static_cast<Bench *>(ctx)->reqs.push_back(r); }
  RefreshKind act(Action a) { now += 500; return ui.act(a, now, wc()); }
  ScreenId topId() const { return top(ui.model()).id; }
  const Model &m() const { return ui.model(); }
  PermReq req(const char *id, const char *tool = "Bash", const char *arg = "npm run test") const {
    PermReq r;
    memset(&r, 0, sizeof r);
    scopy(r.id, id);
    scopy(r.session, "b2");
    scopy(r.name, "web-app");
    scopy(r.tool, tool);
    scopy(r.arg, arg);
    r.at = epoch() - 90000;  // arrivata al servizio un minuto e mezzo fa
    return r;
  }
  void openWebAppDetail() {
    while (topId() != ScreenId::Home) act(Action::Back);
    act(Action::Ok);  // Home → Dettaglio della più urgente (web-app, permesso)
    TEST_ASSERT_EQUAL(ScreenId::Detail, topId());
    TEST_ASSERT_EQUAL_STRING("b2", m().sessions[m().ui.sel].id);
  }
  std::vector<std::string> texts() {
    std::vector<std::string> out;
    static Canvas<250, 122> cv;
    ui::setTextHook([](const char *s, int, int, int, int, bool, Align, void *ctx) {
      static_cast<std::vector<std::string> *>(ctx)->push_back(s);
    }, &out);
    ui::compose(cv, kHeltec213, m(), 0, now, wc());
    ui::setTextHook(nullptr, nullptr);
    return out;
  }
  bool shows(const char *s) {
    for (const std::string &t : texts())
      if (t == s) return true;
    return false;
  }
};

}  // namespace

static void test_no_answers_before_auth_and_request() {
  static Bench b;
  b.openWebAppDetail();
  TEST_ASSERT_TRUE(b.shows("RISPONDI NEL TERMINALE"));  // non autenticato
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Rispondi nel terminale", b.m().toast.msg);
  b.ui.netAuthed(b.now);
  TEST_ASSERT_TRUE(b.m().sys.authed);
  TEST_ASSERT_EQUAL(PairState::Paired, b.m().sys.pair);
  b.act(Action::Ok);  // autenticato ma nessuna richiesta verificata: lo stato da solo non basta
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Nessuna richiesta verificata dal PC: rispondi nel terminale", b.ui.lastAction());
  TEST_ASSERT_TRUE(b.perms.empty());
}

static void test_allow_and_deny_send_request_id() {
  static Bench b;
  b.ui.netAuthed(b.now);
  b.ui.netPermRequest(b.req(kReq), b.now, b.epoch());
  b.ui.netPermRequest(b.req(kReq), b.now, b.epoch());  // doppione: ignorato
  TEST_ASSERT_EQUAL(1, b.m().nPerms);
  b.openWebAppDetail();
  TEST_ASSERT_TRUE(b.shows("2X  RISPONDI AL PERMESSO"));
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING(kReq, top(b.m()).rid);
  TEST_ASSERT_EQUAL(1, top(b.m()).choice);  // NEGA preselezionato
  TEST_ASSERT_TRUE(b.shows("IN ATTESA DA 1 MIN"));  // da `at`, con l'ora del muro valida
  TEST_ASSERT_TRUE(b.shows("npm run test"));
  b.act(Action::Next);  // CONSENTI
  TEST_ASSERT_TRUE(b.perms.empty());
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(1, b.perms.size());
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.perms[0].outcome);
  TEST_ASSERT_EQUAL_STRING(kReq, b.permIds[0].c_str());
  TEST_ASSERT_EQUAL(0, b.m().nPerms);
  TEST_ASSERT_EQUAL_STRING("Consentito · inviato a Claude Code", b.m().toast.msg);
  // Nega, con due richieste della stessa sessione: dopo la prima resta «PERMESSO».
  b.ui.netPermRequest(b.req(kReq2, "Edit", "src/città/perché.ts"), b.now, b.epoch());
  static StateMsg again;
  again = msg;
  again.sessions[1].since += 1;  // uno state nuovo (lo stesso di prima si scarta per impronta)
  b.ui.netState(again, b.now);  // lo stato dal PC riporta web-app in permesso
  b.ui.netPermRequest(b.req("ffffffffffffffffffffffffffffffff"), b.now, b.epoch());
  b.openWebAppDetail();
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL_STRING(kReq2, top(b.m()).rid);  // la più vecchia per prima
  TEST_ASSERT_TRUE(b.shows("src/città/perché.ts"));
  b.act(Action::Ok);  // NEGA
  TEST_ASSERT_EQUAL(PermOutcome::Deny, b.perms.back().outcome);
  TEST_ASSERT_EQUAL_STRING(kReq2, b.permIds.back().c_str());
  TEST_ASSERT_EQUAL(SessionState::Permission, b.m().sessions[b.m().ui.sel].state);
}

static void test_long_press_keeps_request_and_never_expires() {
  static Bench b;
  b.ui.netAuthed(b.now);
  b.ui.netPermRequest(b.req(kReq), b.now, b.epoch());
  b.openWebAppDetail();
  b.act(Action::Ok);
  b.act(Action::Back);  // lunga: chiude senza rispondere
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL(PermOutcome::Cancelled, b.perms.back().outcome);
  TEST_ASSERT_EQUAL(1, b.m().nPerms);  // resta in coda
  for (int k = 0; k < 120; k++) { b.now += 60000; b.ui.tick(b.now, b.wc()); }  // due ore
  TEST_ASSERT_EQUAL(1, b.m().nPerms);
  b.act(Action::Ok);  // di nuovo dal Dettaglio
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_TRUE(b.shows("IN ATTESA DA 2 H 01"));
}

static void test_answered_elsewhere_closes_screen() {
  static Bench b;
  b.ui.netAuthed(b.now);
  b.ui.netPermRequest(b.req(kReq), b.now, b.epoch());
  b.openWebAppDetail();
  b.act(Action::Ok);
  // Uno state calcolato prima della richiesta (web-app «working») non chiude la schermata: la chiude perm-close.
  static StateMsg working;
  working = msg;
  working.sessions[1].state = SessionState::Working;
  working.time += 1;
  b.ui.netState(working, b.now);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.netPermClose("00000000000000000000000000000000", b.now));  // id sconosciuto
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.ui.netPermClose(kReq, b.now));
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Risposto altrove", b.m().toast.msg);
  TEST_ASSERT_EQUAL(0, b.m().nPerms);
}

static void test_pc_lost_and_redo() {
  static Bench b;
  b.ui.netAuthed(b.now);
  b.ui.netPermRequest(b.req(kReq), b.now, b.epoch());
  b.openWebAppDetail();
  b.act(Action::Ok);
  b.ui.netPcLost(b.now);  // display fuori rete: la domanda resta nel terminale; al ritorno il servizio la rimanda
  TEST_ASSERT_FALSE(b.m().sys.authed);
  TEST_ASSERT_EQUAL(0, b.m().nPerms);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::Perm);
  b.ui.netState(msg, b.now);
  b.ui.netRedo(b.now);
  TEST_ASSERT_EQUAL(PairState::Redo, b.m().sys.pair);
  char v[48];
  pairLabel(b.m(), v, sizeof v);
  TEST_ASSERT_EQUAL_STRING("da rifare", v);
  b.ui.netAuthed(b.now);  // nuovo abbinamento riuscito
  pairLabel(b.m(), v, sizeof v);
  TEST_ASSERT_EQUAL_STRING("abbinato a calamaio.local", v);  // host da mDNS; qui manca, quindi l'alias
}

static void test_pairing_and_unpair_send_commands() {
  static Bench b;
  b.ui.netPairCode("175397", b.now);
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  TEST_ASSERT_TRUE(b.shows("175 397"));
  TEST_ASSERT_TRUE(b.shows("Scrivi questo codice sul PC"));
  b.act(Action::Ok);
  TEST_ASSERT_TRUE(b.reqs.empty());  // prima di pair-confirm niente pair-done
  b.ui.netPairReady(b.now);
  TEST_ASSERT_TRUE(b.shows("Il PC ha il codice giusto"));
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(NetCmd::PairConfirm, b.reqs.back().cmd);
  TEST_ASSERT_EQUAL(PairState::Paired, b.m().sys.pair);
  b.ui.netPairCode("000042", b.now);
  b.act(Action::Back);
  TEST_ASSERT_EQUAL(NetCmd::PairCancel, b.reqs.back().cmd);
  // Durante un abbinamento la rete cade: la schermata si chiude.
  b.ui.netPairCode("000042", b.now);
  b.ui.netPcLost(b.now);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::Pair);
  // Dimentica abbinamento: unpair al servizio dopo la seconda conferma.
  b.ui.netState(msg, b.now);
  b.act(Action::Menu);
  for (int k = 0; k < 6; k++) b.act(Action::Next);
  b.act(Action::Ok);
  b.act(Action::Next);
  const size_t n = b.reqs.size();
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(n, b.reqs.size());
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(NetCmd::Unpair, b.reqs.back().cmd);
  TEST_ASSERT_EQUAL(PairState::None, b.m().sys.pair);
}

static void test_protect_and_factory_go_to_the_loop() {
  static Bench b;
  b.act(Action::Menu);
  for (int k = 0; k < 6; k++) b.act(Action::Next);
  b.act(Action::Ok);
  b.act(Action::Next);
  b.act(Action::Next);
  b.act(Action::Ok);  // avviso
  b.act(Action::Ok);  // 2×
  b.now += 100;
  b.ui.keyDown(b.now);  // il tenuto inizia dopo l'armatura (R1, regola 2)
  b.now += 3000;
  b.ui.act(Action::Sleep, b.now, b.wc());  // tenuto 3 s
  TEST_ASSERT_EQUAL(NetCmd::ProtectKeys, b.reqs.back().cmd);
  TEST_ASSERT_FALSE(b.m().sys.keysProtected);  // lo dice il loop, dopo la eFuse
  b.ui.netKeysProtected(true, b.now);
  TEST_ASSERT_TRUE(b.m().sys.keysProtected);
  TEST_ASSERT_EQUAL_STRING("Chiavi protette", b.m().toast.msg);
  b.act(Action::Next);
  b.act(Action::Ok);
  b.act(Action::Ok);
  b.now += 100;
  b.ui.keyDown(b.now);
  b.now += 3000;
  b.ui.act(Action::Sleep, b.now, b.wc());
  TEST_ASSERT_EQUAL(NetCmd::FactoryReset, b.reqs.back().cmd);
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
}

// R1 e F11 con la rete accesa e il PC autenticato: dal telefono Proteggi, Ripristino, conferma dell'abbinamento,
// «Dimentica abbinamento», «Dimentica tutte le reti» e «Configura da telefono» non mandano niente; col PRG sì.
static void phoneEv(Bench &b, pad::MenuEvent e) {
  b.now += 300;
  b.ui.padEvent(e, b.now, b.wc(), pad::Kind::Phone);
}

static void listItem(Bench &b, MenuId menu, ItemKind k, ScreenId open, ActId act) {
  while (b.topId() != ScreenId::Home) b.act(Action::Back);
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = menu;
  pushScreen(b.ui.model(), e);
  const MenuList &L = b.ui.items(b.now, b.wc());
  int sel = -1;
  for (int i = 0; i < L.n && sel < 0; i++)
    if (L.items[i].kind == k && (k == ItemKind::Open ? L.items[i].open == open : L.items[i].act == act)) sel = i;
  TEST_ASSERT_TRUE(sel >= 0);
  top(b.ui.model()).sel = sel;
}

static void test_phone_never_sends_protect_factory_pairconfirm() {
  static Bench b;
  b.ui.netAuthed(b.now);
  const size_t n0 = b.reqs.size();
  for (ActId a : {ActId::Protect, ActId::Factory}) {
    listItem(b, MenuId::Security, ItemKind::Act, ScreenId::Home, a);
    phoneEv(b, pad::MenuEvent::Select);  // regola 1: nemmeno l'avviso
    TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
    phoneEv(b, pad::MenuEvent::Select);
    phoneEv(b, pad::MenuEvent::HoldDown);
    b.now += 3000;
    phoneEv(b, pad::MenuEvent::HoldFire);
    phoneEv(b, pad::MenuEvent::HoldUp);
    TEST_ASSERT_TRUE(b.topId() != ScreenId::Confirm);  // il tenuto del telefono sulla lista manda a Riposo, e basta
  }
  for (ActId a : {ActId::Unpair}) {
    listItem(b, MenuId::Security, ItemKind::Act, ScreenId::Home, a);
    phoneEv(b, pad::MenuEvent::Select);
    phoneEv(b, pad::MenuEvent::Select);
  }
  listItem(b, MenuId::Wifi, ItemKind::Act, ScreenId::Home, ActId::Forget);
  phoneEv(b, pad::MenuEvent::Select);
  phoneEv(b, pad::MenuEvent::Select);
  listItem(b, MenuId::Wifi, ItemKind::Open, ScreenId::Qr, ActId::Style);
  phoneEv(b, pad::MenuEvent::Select);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  while (b.topId() != ScreenId::Home) b.act(Action::Back);
  b.ui.netPairCode("175397", b.now);
  b.ui.netPairReady(b.now);
  phoneEv(b, pad::MenuEvent::Select);
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  for (size_t i = n0; i < b.reqs.size(); i++) TEST_ASSERT_FALSE_MESSAGE(phoneForbidden(b.reqs[i].cmd), netCmdKey(b.reqs[i].cmd));
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.m().toast.msg);
  for (int k = 0; k < 2; k++) {  // 2× del PRG, coi fronti come nel loop: pair-done parte, con la sorgente PRG
    b.now += 150;
    b.ui.keyDown(b.now);
    b.now += 80;
    b.ui.keyUp(b.now);
  }
  b.now += 381;
  b.ui.gesture(Gesture::Press2, b.now, b.wc());
  TEST_ASSERT_FALSE(b.reqs.empty());
  TEST_ASSERT_EQUAL(NetCmd::PairConfirm, b.reqs.back().cmd);
  TEST_ASSERT_EQUAL(InputSrc::Prg, b.reqs.back().src);
}

static void test_power_save_redraws_only_real_changes() {
  static Bench b;
  b.ui.model().set.save = true;
  static StateMsg s2;
  s2 = msg;
  scopy(s2.sessions[0].detail, "Read altro.ts");  // solo il dettaglio: niente ridisegno
  s2.time += 1;
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.netState(s2, b.now));
  TEST_ASSERT_EQUAL_STRING("Read altro.ts", b.m().sessions[0].detail);  // il modello però è aggiornato
  s2.sessions[0].state = SessionState::Done;  // cambio di stato: sì
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.netState(s2, b.now));
  b.ui.model().set.save = false;
  scopy(s2.sessions[0].detail, "Read altro ancora.ts");
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.netState(s2, b.now));  // senza risparmio: come prima
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_answers_before_auth_and_request);
  RUN_TEST(test_allow_and_deny_send_request_id);
  RUN_TEST(test_long_press_keeps_request_and_never_expires);
  RUN_TEST(test_answered_elsewhere_closes_screen);
  RUN_TEST(test_pc_lost_and_redo);
  RUN_TEST(test_pairing_and_unpair_send_commands);
  RUN_TEST(test_protect_and_factory_go_to_the_loop);
  RUN_TEST(test_power_save_redraws_only_real_changes);
  RUN_TEST(test_phone_never_sends_protect_factory_pairconfirm);
  return UNITY_END();
}
