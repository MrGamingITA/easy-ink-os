// 0.10.0, gioco della mascotte (core/pet, core/accessories, Ui): messaggi game/accessory/anchors e le risposte,
// regole della festa (mai con permesso aperto, silenzio, notturno, risparmio: solo toast), menu e scelta
// dell'accessorio (accessory-wear), posa stanca, cache di accessori e ancore con richieste ogni 30 s, salvataggio.
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "../test_accessory/golden.h"
#include "core/accessories.h"
#include "core/apply_state.h"
#include "core/avatars.h"
#include "core/menu.h"
#include "core/pet.h"
#include "core/remote_settings.h"
#include "core/strbuf.h"
#include "core/ui.h"
#include "gfx/accessory_builtin.h"
#include "gfx/avatar_builtin.h"
#include "gfx/canvas.h"
#include "ui/screens.h"

using namespace cal;

void setUp() { setAccessoryStore(nullptr); setAvatarCache(nullptr); }
void tearDown() { setAccessoryStore(nullptr); setAvatarCache(nullptr); }

namespace {

std::vector<NetRequest> gNet;
void onNet(const NetRequest &r, void *) { gNet.push_back(r); }

struct Bench {
  Ui ui;
  Ms now;
  Bench() : ui(kHeltec213), now(1000000) {
    gNet.clear();
    ui.setNetListener(onNet, nullptr);
    ui.begin(now, wc());
    now += 2700;
    ui.tick(now, wc());
  }
  WallClock wc() const { WallClock w; w.localMs = wallFromCivil(2026, 10, 11, 10, 30, 0).localMs + (now - 1000000); return w; }
  RefreshKind act(Action a) { return ui.act(a, now, wc()); }
  Model &m() { return ui.model(); }
  void gameOn() { m().set.game = true; m().pet.on = true; }
  // Risponde un permesso: la sessione web-app non chiede più niente (come Simula «Risposto nel terminale»).
  void noPerm() {
    for (int k = 0; k < m().nSessions; k++)
      if (m().sessions[k].state == SessionState::Permission) m().sessions[k].state = SessionState::Working;
  }
  RefreshKind goal(const char *id, const char *title) {
    GameMsg g;
    petMsg(m().pet, g);
    g.hasCelebrate = true;
    scopy(g.celebrateId, id);
    scopy(g.celebrateTitle, title);
    return ui.netGame(g, now);
  }
  void openMenu() { act(Action::Menu); }
  void openPet() {
    openMenu();
    act(Action::Next);
    act(Action::Next);
    act(Action::Ok);  // La mascotte (voce 2 con il gioco acceso)
  }
};

bool parse(const std::string &js, JsonDocument &doc) { return parseMessage(js.data(), js.size(), doc); }

const char *kGame =
    "{\"t\":\"game\",\"v\":1,\"on\":true,\"level\":4,\"xp\":800,\"xpLevelStart\":763,\"xpNext\":1369,\"energy\":55,\"tired\":false,"
    "\"wear\":{\"id\":\"occhiali\",\"crc\":61548584},\"celebrate\":{\"id\":\"primo-task\",\"title\":\"Primo task\"},"
    "\"unlocked\":[{\"id\":\"cappellino\",\"name\":\"Cappellino\"},{\"id\":\"occhiali\",\"name\":\"Occhiali\"}]}";

}  // namespace

static void test_decode_game() {
  JsonDocument doc;
  TEST_ASSERT_TRUE(parse(kGame, doc));
  TEST_ASSERT_EQUAL(int(MsgType::Game), int(msgType(doc.as<JsonVariantConst>())));
  GameMsg g;
  TEST_ASSERT_TRUE(decodeGame(doc.as<JsonVariantConst>(), g));
  TEST_ASSERT_TRUE(g.on);
  TEST_ASSERT_EQUAL(4, g.level);
  TEST_ASSERT_EQUAL(800, g.xp);
  TEST_ASSERT_EQUAL(763, g.xpLevelStart);
  TEST_ASSERT_EQUAL(1369, g.xpNext);
  TEST_ASSERT_EQUAL(55, g.energy);
  TEST_ASSERT_TRUE(g.hasWear);
  TEST_ASSERT_EQUAL_STRING("occhiali", g.wear.id);
  TEST_ASSERT_EQUAL_HEX32(0x03AB2828u, g.wear.crc);  // 61548584: il CRC degli occhiali del catalogo
  TEST_ASSERT_TRUE(g.hasCelebrate);
  TEST_ASSERT_EQUAL_STRING("Primo task", g.celebrateTitle);
  TEST_ASSERT_EQUAL(2, g.nUnlocked);
  TEST_ASSERT_EQUAL_STRING("Occhiali", g.unlocked[1].name);
  // wear e celebrate null, campi mancanti o sbagliati: neutri; id non validi scartati; doppioni tolti; titoli accorciati.
  const std::string js =
      "{\"t\":\"game\",\"v\":1,\"on\":\"si\",\"level\":-3,\"energy\":180,\"wear\":null,\"celebrate\":null,"
      "\"unlocked\":[{\"id\":\"Cappellino\"},{\"id\":\"corona\"},{\"id\":\"corona\",\"name\":\"Due\"},{\"id\":\"mago\",\"name\":\"\"}]}";
  TEST_ASSERT_TRUE(parse(js, doc));
  TEST_ASSERT_TRUE(decodeGame(doc.as<JsonVariantConst>(), g));
  TEST_ASSERT_FALSE(g.on);
  TEST_ASSERT_EQUAL(0, g.level);
  TEST_ASSERT_EQUAL(100, g.energy);
  TEST_ASSERT_FALSE(g.hasWear);
  TEST_ASSERT_FALSE(g.hasCelebrate);
  TEST_ASSERT_EQUAL(2, g.nUnlocked);
  TEST_ASSERT_EQUAL_STRING("corona", g.unlocked[0].name);  // senza nome: l'id
  TEST_ASSERT_EQUAL_STRING("mago", g.unlocked[1].id);
  std::string longTitle(80, 'x');
  TEST_ASSERT_TRUE(parse("{\"t\":\"game\",\"v\":1,\"celebrate\":{\"id\":\"a\",\"title\":\"" + longTitle + "\"},\"wear\":{\"id\":\"no!\"}}", doc));
  TEST_ASSERT_TRUE(decodeGame(doc.as<JsonVariantConst>(), g));
  TEST_ASSERT_EQUAL(kPetTitleLen - 1, int(strlen(g.celebrateTitle)));
  TEST_ASSERT_FALSE(g.hasWear);
  // Non è un game.
  TEST_ASSERT_TRUE(parse("{\"t\":\"state\",\"v\":1,\"time\":1}", doc));
  TEST_ASSERT_FALSE(decodeGame(doc.as<JsonVariantConst>(), g));
}

static void test_party_rules() {
  // Gioco acceso, nessun permesso aperto: festa di 3 s, toast, Home in posa «finito».
  {
    static Bench b;
    b.gameOn();
    b.noPerm();
    TEST_ASSERT_EQUAL(int(RefreshKind::Partial), int(b.goal("primo-task", "Primo task")));
    TEST_ASSERT_TRUE(petParty(b.m(), b.now));
    TEST_ASSERT_EQUAL_STRING("Traguardo: Primo task", b.m().toast.msg);
    TEST_ASSERT_EQUAL(int(kPetPartyMs), int(b.m().toast.until - b.now));
    TEST_ASSERT_TRUE(b.m().burstUntil >= b.m().pet.party);  // si anima
    TEST_ASSERT_EQUAL_STRING("Traguardo: Primo task (festa di 3 s)", b.ui.lastAction());
    // Lo stesso traguardo di nuovo (game ripetuto): niente seconda festa.
    b.now += 3100;
    TEST_ASSERT_EQUAL(int(RefreshKind::Partial), int(b.ui.tick(b.now, b.wc())));  // fine della festa: ridisegno
    TEST_ASSERT_FALSE(petParty(b.m(), b.now));
    b.goal("primo-task", "Primo task");
    TEST_ASSERT_FALSE(petParty(b.m(), b.now));
    TEST_ASSERT_EQUAL(1, b.m().pet.nGoals);
    // Gli ultimi 3, il più recente per primo.
    b.goal("primo-verde", "Primo verde");
    b.goal("task-10", "10 task");
    b.goal("serie-7", "7 giorni di serie");
    TEST_ASSERT_EQUAL(3, b.m().pet.nGoals);
    TEST_ASSERT_EQUAL_STRING("7 giorni di serie", b.m().pet.goals[0]);
    TEST_ASSERT_EQUAL_STRING("Primo verde", b.m().pet.goals[2]);
  }
  // Permesso aperto, silenzio, concentrazione, notturno, risparmio (scelto o batteria sotto il 15%): solo il toast.
  for (int c = 0; c < 7; c++) {
    static Bench *pb = nullptr;
    delete pb;
    pb = new Bench();  // Ui ~20 KB: sullo heap
    Bench &b = *pb;
    b.gameOn();
    if (c != 0) b.noPerm();
    if (c == 1) b.m().set.mute = b.now + kMuteMs;
    if (c == 2) b.m().focus.until = b.now + 60000;
    if (c == 3) b.m().set.night = true;
    if (c == 4) b.m().set.save = true;
    if (c == 5) { b.m().sys.batt = 10; b.m().sys.usb = false; }
    if (c == 6) b.m().nPerms = 1;  // richiesta del servizio in coda
    b.goal("primo-task", "Primo task");
    TEST_ASSERT_FALSE_MESSAGE(petParty(b.m(), b.now), std::to_string(c).c_str());
    TEST_ASSERT_EQUAL_STRING("Traguardo: Primo task", b.m().toast.msg);
    TEST_ASSERT_EQUAL_STRING("Traguardo: Primo task (solo il toast)", b.ui.lastAction());
    TEST_ASSERT_EQUAL_STRING("Primo task", b.m().pet.goals[0]);
  }
  // Gioco spento (dalla scheda o dal PC): niente toast, niente festa, niente traguardi.
  for (int c = 0; c < 2; c++) {
    static Bench *pb = nullptr;
    delete pb;
    pb = new Bench();
    Bench &b = *pb;
    b.noPerm();
    b.gameOn();
    GameMsg g;
    petMsg(b.m().pet, g);
    g.on = false;  // il PC dice spento: la scheda si allinea (c = 1: anche l'interruttore era già spento)
    if (c == 1) b.m().set.game = false;
    g.hasCelebrate = true;
    scopy(g.celebrateId, "primo-task");
    scopy(g.celebrateTitle, "Primo task");
    b.ui.netGame(g, b.now);
    TEST_ASSERT_FALSE(petParty(b.m(), b.now));
    TEST_ASSERT_EQUAL_STRING("", b.m().toast.msg);
    TEST_ASSERT_EQUAL(0, b.m().pet.nGoals);
    TEST_ASSERT_EQUAL_STRING("Traguardo: Primo task (gioco spento: niente)", b.ui.lastAction());
  }
}

static void test_tired_pose() {
  static Bench b;
  Session &s = b.m().sessions[0];  // api-server, lavora
  b.m().pet.tired = true;
  TEST_ASSERT_EQUAL(int(MascotPose::Working), int(pose(b.m(), s)));  // gioco spento: niente posa stanca dal gioco
  b.gameOn();
  TEST_ASSERT_EQUAL(int(MascotPose::Tired), int(pose(b.m(), s)));
  TEST_ASSERT_EQUAL(int(MascotPose::Permission), int(pose(b.m(), b.m().sessions[1])));  // chi aspetta resta com'è
  b.m().pet.on = false;  // spento sul PC
  TEST_ASSERT_EQUAL(int(MascotPose::Working), int(pose(b.m(), s)));
  // STANCA dei limiti resta com'era, col gioco acceso o spento.
  b.m().pet.on = true;
  b.m().pet.tired = false;
  b.m().limits.hasH5 = true;
  b.m().limits.h5 = 92;
  TEST_ASSERT_EQUAL(int(MascotPose::Tired), int(pose(b.m(), s)));
}

static void test_menus_and_wear() {
  static Bench b;
  // Menu › Display › Gioco della mascotte (penultima voce, prima di Tema): spento → acceso.
  b.openMenu();
  for (int k = 0; k < 4; k++) b.act(Action::Next);
  b.act(Action::Ok);  // Display
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(10, L.n);  // 0.13.0: «Tema» in fondo
  TEST_ASSERT_FALSE(L.truncated);
  TEST_ASSERT_EQUAL_STRING("Gioco della mascotte", L.items[8].label);
  TEST_ASSERT_EQUAL_STRING("no", L.items[8].val);
  for (int k = 0; k < 8; k++) b.act(Action::Next);
  b.act(Action::Ok);
  TEST_ASSERT_TRUE(b.m().set.game);
  TEST_ASSERT_EQUAL_STRING("Gioco della mascotte: sì", b.ui.lastAction());
  TEST_ASSERT_TRUE(remoteValues(b.m().set, b.now).game);  // va al PC con settings e nella NVS
  // «La mascotte» compare nel Menu, voce 2.
  b.act(Action::Menu);
  b.openPet();
  TEST_ASSERT_EQUAL(int(ScreenId::Pet), int(top(b.m()).id));
  // 2× apre Accessorio (Nessuno indossato, Cappellino), 1× Cappellino, 2× indossa → accessory-wear e torna indietro.
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(MenuId::PetAcc), int(top(b.m()).menu));
  TEST_ASSERT_EQUAL(0, top(b.m()).sel);
  const MenuList &A = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(2, A.n);
  TEST_ASSERT_EQUAL_STRING("indossato", A.items[0].val);
  gNet.clear();
  b.act(Action::Next);
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(ScreenId::Pet), int(top(b.m()).id));
  TEST_ASSERT_EQUAL_STRING("cappellino", b.m().pet.wear.id);
  TEST_ASSERT_TRUE(b.m().pet.wear.hasCrc);
  TEST_ASSERT_EQUAL_HEX32(kBuiltinAccessories[0].crc, b.m().pet.wear.crc);
  TEST_ASSERT_EQUAL_STRING("Indossa: Cappellino", b.m().toast.msg);
  TEST_ASSERT_EQUAL(1, int(gNet.size()));
  TEST_ASSERT_EQUAL(int(NetCmd::AccWear), int(gNet[0].cmd));
  TEST_ASSERT_EQUAL_STRING("cappellino", gNet[0].avatarIds[0]);
  TEST_ASSERT_TRUE(b.ui.takePetChanged());
  TEST_ASSERT_NOT_NULL(wornAccessory(b.m()));
  // Di nuovo: la selezione parte dall'indossato; Nessuno → accessory-wear con id null.
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(1, top(b.m()).sel);
  b.act(Action::Next);  // giro: torna su Nessuno
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL_STRING("", b.m().pet.wear.id);
  TEST_ASSERT_EQUAL_STRING("Nessun accessorio", b.m().toast.msg);
  TEST_ASSERT_EQUAL_STRING("", gNet.back().avatarIds[0]);
  char js[96];
  encodeAccessoryWear(gNet.back().avatarIds[0], js, sizeof js);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"accessory-wear\",\"v\":1,\"id\":null}", js);
  // Il PC spegne il gioco (set) mentre si sceglie: via le schermate del gioco, via la voce del Menu.
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(MenuId::PetAcc), int(top(b.m()).menu));
  SettingsPatch p;
  memset(&p, 0, sizeof p);
  p.has = 1u << int(RemoteKey::Game);
  p.v.game = false;
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(b.ui.applyRemote(p, b.now, b.wc())));
  TEST_ASSERT_EQUAL(int(MenuId::Main), int(top(b.m()).menu));
  const MenuList &M = b.ui.items(b.now, b.wc());
  for (int k = 0; k < M.n; k++) TEST_ASSERT_TRUE(strcmp(M.items[k].label, "La mascotte") != 0);
  TEST_ASSERT_NULL(wornAccessory(b.m()));
}

// 0.14.1: le griglie dell'avatar sono condivise da accOverlay e partyFx: i fotogrammi restano quelli della 0.14.0.
static uint32_t fnvCanvas(const Canvas<250, 122> &cv) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 250 * 122 / 8; i++) h = (h ^ cv.data()[i]) * 16777619u;
  return h;
}
static void test_party_and_accessory_frames_match_0140() {
  static Canvas<250, 122> cv;
  uint32_t got[8];
  int n = 0;
  for (int sc = 0; sc < 4; sc++) {
    static Bench *pb = nullptr;
    delete pb;
    pb = new Bench();
    Bench &b = *pb;
    b.gameOn();
    b.noPerm();
    b.m().sys.pc = true;
    if (sc >= 1) {  // accessorio indossato
      b.openPet();
      b.act(Action::Ok);
      b.act(Action::Next);
      b.act(Action::Ok);
      TEST_ASSERT_NOT_NULL(wornAccessory(b.m()));
      b.act(Action::Menu);  // torna alla Home
    }
    while (sc < 3 && top(b.m()).id != ScreenId::Home && b.m().ui.depth > 1) b.act(Action::Back);
    if (sc < 3) b.goal("primo-task", "Primo task");
    TEST_ASSERT_TRUE(sc == 3 || petParty(b.m(), b.now));
    if (sc == 2) b.act(Action::Ok);  // Home → Dettaglio
    if (sc == 3) {
      cal::pushScreen(b.m(), cal::makeScreen(cal::ScreenId::Ota));
      b.m().ota.phase = cal::OtaPhase::Receive;
      b.m().ota.total = 100;
      b.m().ota.done = 40;
    }
    TEST_ASSERT_EQUAL(int(sc < 2 ? ScreenId::Home : sc == 2 ? ScreenId::Detail : ScreenId::Ota), int(top(b.m()).id));
    for (int f = 0; f < 2; f++) {
      ui::compose(cv, kHeltec213, b.m(), f, b.now, b.wc());
      got[n++] = fnvCanvas(cv);
    }
  }
  // Impronte FNV-1a prese con il codice della 0.14.0 (griglie statiche separate).
  static const uint32_t kPartyFrames[8] = {0x0469BF1Fu, 0x0EDEC0CFu, 0xCCAF80BFu, 0x240B0CCFu,
                                           0x6D364790u, 0xB2451C40u, 0xB87E15A8u, 0xB87E15A8u};
  for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_HEX32(kPartyFrames[i], got[i]);
}

static void test_menu_capacity() {
  // Menu con il gioco (13 voci) e Accessorio con 16 sbloccati (17 voci) ci stanno senza tagli.
  static Bench b;
  b.gameOn();
  MenuList L;
  WallClock wc = b.wc();
  menuList(MenuId::Main, kHeltec213, b.m(), b.now, wc, L);
  TEST_ASSERT_EQUAL(14, L.n);
  TEST_ASSERT_EQUAL_STRING("La mascotte", L.items[2].label);
  TEST_ASSERT_EQUAL_STRING("livello 3", L.items[2].val);
  TEST_ASSERT_EQUAL_STRING("Info", L.items[13].label);
  for (int k = 0; k < kPetUnlockedCap; k++) {
    snprintf(b.m().pet.unlocked[k].id, kAvatarRefLen, "acc-%d", k);
    snprintf(b.m().pet.unlocked[k].name, kAvatarRefLen, "Accessorio speciale %d", k);
  }
  b.m().pet.nUnlocked = kPetUnlockedCap;
  menuList(MenuId::PetAcc, kHeltec213, b.m(), b.now, wc, L);
  TEST_ASSERT_EQUAL(21, L.n);
  TEST_ASSERT_FALSE(L.truncated);
  TEST_ASSERT_EQUAL(19, L.items[20].net);
}

static void test_encoders_and_blob_msgs() {
  char ids[2][kAvatarRefLen] = {"corona", "mago"};
  char out[200];
  encodeAccessoryNeed(ids, 2, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"accessory-need\",\"v\":1,\"ids\":[\"corona\",\"mago\"]}", out);
  const uint32_t crcs[2] = {0xB2432AD7u, 7};
  encodeAnchorsNeed(ids, crcs, 2, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"anchors-need\",\"v\":1,\"avatars\":[{\"id\":\"corona\",\"crc\":2990746327},{\"id\":\"mago\",\"crc\":7}]}", out);
  encodeAccessoryWear("mago", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"accessory-wear\",\"v\":1,\"id\":\"mago\"}", out);
  char small[20];
  TEST_ASSERT_EQUAL(0, int(encodeAccessoryWear("mago", small, sizeof small)));
  // accessory {id, crc, data}: base64 → byte (al massimo 1 024).
  JsonDocument doc;
  static AccessoryMsg am;
  TEST_ASSERT_TRUE(parse("{\"t\":\"accessory\",\"v\":1,\"id\":\"prova\",\"crc\":5,\"data\":\"Q0FDQw==\"}", doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Ok), int(decodeAccessoryMsg(doc.as<JsonVariantConst>(), am)));
  TEST_ASSERT_EQUAL(4, am.len);
  TEST_ASSERT_TRUE(parse("{\"t\":\"accessory\",\"v\":1,\"id\":\"Prova\",\"crc\":5,\"data\":\"Q0FDQw==\"}", doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Id), int(decodeAccessoryMsg(doc.as<JsonVariantConst>(), am)));
  TEST_ASSERT_TRUE(parse("{\"t\":\"accessory\",\"v\":1,\"id\":\"prova\",\"crc\":\"5\",\"data\":\"Q0FDQw==\"}", doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Crc), int(decodeAccessoryMsg(doc.as<JsonVariantConst>(), am)));
  TEST_ASSERT_TRUE(parse("{\"t\":\"accessory\",\"v\":1,\"id\":\"prova\",\"crc\":5,\"data\":\"Q0FDQ!==\"}", doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Data), int(decodeAccessoryMsg(doc.as<JsonVariantConst>(), am)));
  std::string big(1400, 'A');  // 1 050 byte: oltre il massimo
  TEST_ASSERT_TRUE(parse("{\"t\":\"accessory\",\"v\":1,\"id\":\"prova\",\"crc\":5,\"data\":\"" + big + "\"}", doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Data), int(decodeAccessoryMsg(doc.as<JsonVariantConst>(), am)));
  static AnchorsMsg an;
  TEST_ASSERT_TRUE(parse("{\"t\":\"anchors\",\"v\":1,\"avatar\":\"mini\",\"avatarCrc\":9,\"data\":\"Q0FOQw==\"}", doc));
  TEST_ASSERT_EQUAL(int(MsgType::Anchors), int(msgType(doc.as<JsonVariantConst>())));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Ok), int(decodeAnchorsMsg(doc.as<JsonVariantConst>(), an)));
  TEST_ASSERT_EQUAL_STRING("mini", an.avatar);
  TEST_ASSERT_EQUAL(9, int(an.avatarCrc));
  TEST_ASSERT_TRUE(parse("{\"t\":\"anchors\",\"v\":1,\"avatar\":\"mini\",\"data\":\"Q0FOQw==\"}", doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Crc), int(decodeAnchorsMsg(doc.as<JsonVariantConst>(), an)));
}

static void test_store_and_needs() {
  // Un avatar dell'utente (mini, dalla cache) e un accessorio non del catalogo (base dei casi costruiti).
  static MemAvatarStorage avMem;
  static AvatarCache avc;
  avMem.eraseAll();
  avc.begin(&avMem);
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(avc.store("mini", avatarCrc32(kMiniCavt + 12, sizeof kMiniCavt - 12), kMiniCavt, sizeof kMiniCavt)));
  setAvatarCache(&avc);
  static MemAccStorage mem;
  mem.eraseAll();
  static AccessoryStore store;
  TEST_ASSERT_EQUAL(0, store.begin(&mem));
  setAccessoryStore(&store);
  static Bench b;
  b.gameOn();
  setAvatarRef(b.m().sessions[0].avatar, "mini");
  const Avatar &mini = sessionAvatar(b.m(), b.m().sessions[0]);
  TEST_ASSERT_EQUAL_STRING("mini", mini.id);
  Accessory prova;
  TEST_ASSERT_EQUAL(int(AccError::Ok), int(parseAccessory(kTestAcc[0], kTestAccLen[0], prova)));
  // Indossa «prova» (arrivato dal PC con il game): manca → accessory-need subito, poi non prima di 30 s.
  setAvatarRef(b.m().pet.wear, "prova");
  b.m().pet.wear.crc = prova.crc;
  b.m().pet.wear.hasCrc = true;
  AccNeeds needs;
  char ids[8][kAvatarRefLen];
  uint32_t crcs[8];
  TEST_ASSERT_EQUAL(1, needs.dueAccessories(b.m(), b.now, ids, 8));
  TEST_ASSERT_EQUAL_STRING("prova", ids[0]);
  TEST_ASSERT_EQUAL(0, needs.dueAccessories(b.m(), b.now + 29999, ids, 8));
  TEST_ASSERT_EQUAL(1, needs.dueAccessories(b.m(), b.now + 30000, ids, 8));
  // Ancore: le chiede solo per l'avatar senza (mini), con il suo CRC; i predefiniti hanno le loro.
  TEST_ASSERT_EQUAL(1, needs.dueAnchors(b.m(), b.now, ids, crcs, 8));
  TEST_ASSERT_EQUAL_STRING("mini", ids[0]);
  TEST_ASSERT_EQUAL_HEX32(mini.crc, crcs[0]);
  // Arrivano: CRC o id diversi dal messaggio → rifiutati; giusti → salvati e disegnati.
  TEST_ASSERT_EQUAL(int(AccError::Crc), int(store.store("prova", prova.crc ^ 1, kTestAcc[0], kTestAccLen[0])));
  TEST_ASSERT_EQUAL(int(AccError::Id), int(store.store("altro", prova.crc, kTestAcc[0], kTestAccLen[0])));
  TEST_ASSERT_EQUAL(int(AccError::Ok), int(store.store("prova", prova.crc, kTestAcc[0], kTestAccLen[0])));
  TEST_ASSERT_NOT_NULL(wornAccessory(b.m()));
  TEST_ASSERT_EQUAL(0, needs.dueAccessories(b.m(), b.now + 60000, ids, 8));
  uint8_t g[kAvatarCells];
  bool drawn = true;
  TEST_ASSERT_TRUE(figureGrid(b.m(), mini, Pose::Working, 0, g, &drawn));
  TEST_ASSERT_FALSE(drawn);  // mini senza ancore: niente accessorio, mai posizioni inventate
  TEST_ASSERT_EQUAL(int(AncError::Range), int(store.storeAnchors("mini", mini.crc ^ 1, kMiniAnc[0], kMiniAncLen[0])));  // per un altro disegno
  TEST_ASSERT_EQUAL(int(AncError::Ok), int(store.storeAnchors("mini", mini.crc, kMiniAnc[0], kMiniAncLen[0])));
  TEST_ASSERT_TRUE(figureGrid(b.m(), mini, Pose::Working, 0, g, &drawn));
  TEST_ASSERT_TRUE(drawn);
  TEST_ASSERT_EQUAL(0, needs.dueAnchors(b.m(), b.now + 60000, ids, crcs, 8));
  // Dopo un riavvio: tutto dalla memoria (ancore rilette, accessorio letto quando serve).
  static AccessoryStore again;
  TEST_ASSERT_EQUAL(1, again.begin(&mem));
  setAccessoryStore(&again);
  TEST_ASSERT_NOT_NULL(wornAccessory(b.m()));
  TEST_ASSERT_TRUE(figureGrid(b.m(), mini, Pose::Working, 0, g, &drawn));
  TEST_ASSERT_TRUE(drawn);
  // Ancore rovinate in memoria: si cancellano al riavvio e si richiedono.
  int n = 0;
  uint8_t *raw = mem.raw(AccStorage::Anc, "mini", &n);
  TEST_ASSERT_NOT_NULL(raw);
  raw[30] ^= 0xFF;
  static AccessoryStore third;
  TEST_ASSERT_EQUAL(0, third.begin(&mem));
  setAccessoryStore(&third);
  TEST_ASSERT_NULL(mem.raw(AccStorage::Anc, "mini"));
  AccNeeds fresh;
  TEST_ASSERT_EQUAL(1, fresh.dueAnchors(b.m(), b.now, ids, crcs, 8));
  // Accessorio rovinato in memoria: non si disegna, si cancella e si richiede.
  int na = 0;
  uint8_t *ra = mem.raw(AccStorage::Acc, "prova", &na);
  TEST_ASSERT_NOT_NULL(ra);
  ra[na - 1] ^= 0x01;  // l'ultimo byte dei pixel
  TEST_ASSERT_NULL(wornAccessory(b.m()));
  TEST_ASSERT_NULL(mem.raw(AccStorage::Acc, "prova"));
  TEST_ASSERT_EQUAL(1, fresh.dueAccessories(b.m(), b.now, ids, 8));
  // Rifiutato dal display (blocco rovinato dal PC): non si chiede più fino alla prossima connessione.
  fresh.rejected(false, "prova", prova.crc);
  TEST_ASSERT_EQUAL(0, fresh.dueAccessories(b.m(), b.now + 90000, ids, 8));
  fresh.reset();
  TEST_ASSERT_EQUAL(1, fresh.dueAccessories(b.m(), b.now + 90000, ids, 8));
  // Gioco spento o niente indossato: nessuna richiesta.
  b.m().set.game = false;
  AccNeeds off;
  TEST_ASSERT_EQUAL(0, off.dueAccessories(b.m(), b.now, ids, 8));
  TEST_ASSERT_EQUAL(0, off.dueAnchors(b.m(), b.now, ids, crcs, 8));
  b.m().set.game = true;
  memset(&b.m().pet.wear, 0, sizeof b.m().pet.wear);
  TEST_ASSERT_EQUAL(0, off.dueAnchors(b.m(), b.now, ids, crcs, 8));
  // Catalogo compilato: niente richieste, anche senza rete.
  setAvatarRef(b.m().pet.wear, "corona");
  TEST_ASSERT_EQUAL(0, off.dueAccessories(b.m(), b.now, ids, 8));
  TEST_ASSERT_NOT_NULL(wornAccessory(b.m()));
}

static void test_pet_blob() {
  static Bench b;
  Pet &p = b.m().pet;
  p.on = true;
  p.special = true;
  setAvatarRef(p.wear, "occhiali");
  p.wear.crc = 0x03AB2828u;
  p.wear.hasCrc = true;
  p.tired = true;
  scopy(p.goals[0], "Primo task");
  p.nGoals = 1;
  uint8_t blob[kPetBlobMax];
  const size_t n = packPet(p, blob, sizeof blob);
  TEST_ASSERT_EQUAL(46 + 48 + 4, int(n));
  Pet q;
  memset(&q, 0, sizeof q);
  TEST_ASSERT_TRUE(unpackPet(blob, n, q));
  TEST_ASSERT_TRUE(q.known && q.on && q.tired && q.special);
  TEST_ASSERT_EQUAL(3, q.level);
  TEST_ASSERT_EQUAL(412, q.xp);
  TEST_ASSERT_EQUAL(763, q.xpNext);
  TEST_ASSERT_EQUAL(80, q.energy);
  TEST_ASSERT_EQUAL_STRING("occhiali", q.wear.id);
  TEST_ASSERT_TRUE(q.wear == p.wear);
  TEST_ASSERT_EQUAL(1, q.nUnlocked);
  TEST_ASSERT_EQUAL_STRING("Cappellino", q.unlocked[0].name);
  TEST_ASSERT_EQUAL(0, q.nGoals);  // i traguardi restano in RAM
  // Ogni bit cambiato, ogni troncamento: rifiutato, p intatto.
  for (size_t k = 0; k < n; k++)
    for (int bit = 0; bit < 8; bit++) {
      blob[k] ^= uint8_t(1u << bit);
      Pet r;
      memset(&r, 0, sizeof r);
      TEST_ASSERT_FALSE(unpackPet(blob, n, r));
      TEST_ASSERT_FALSE(r.known);
      blob[k] ^= uint8_t(1u << bit);
    }
  for (size_t k = 0; k < n; k++) TEST_ASSERT_FALSE(unpackPet(blob, k, q));
  // Nessun indossato: id vuoto.
  memset(&p.wear, 0, sizeof p.wear);
  TEST_ASSERT_TRUE(unpackPet(blob, packPet(p, blob, sizeof blob), q));
  TEST_ASSERT_EQUAL_STRING("", q.wear.id);
  TEST_ASSERT_EQUAL(0, int(packPet(p, blob, 20)));
}

static void test_settings_game() {
  // set {game: true}: bool soltanto; nella NVS nel byte 14 (0 = spento, come i blocchi della 0.9.0).
  JsonDocument doc;
  RemoteCmd cmd;
  TEST_ASSERT_TRUE(parse("{\"t\":\"set\",\"v\":1,\"id\":\"c1\",\"settings\":{\"game\":true}}", doc));
  TEST_ASSERT_TRUE(decodeSet(doc.as<JsonVariantConst>(), cmd));
  TEST_ASSERT_TRUE(cmd.ok);
  TEST_ASSERT_TRUE(cmd.patch.on(RemoteKey::Game));
  TEST_ASSERT_TRUE(cmd.patch.v.game);
  TEST_ASSERT_TRUE(parse("{\"t\":\"set\",\"v\":1,\"id\":\"c1\",\"settings\":{\"game\":1}}", doc));
  TEST_ASSERT_TRUE(decodeSet(doc.as<JsonVariantConst>(), cmd));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_EQUAL_STRING("game: valore non valido", cmd.error);
  Settings s;
  memset(&s, 0, sizeof s);
  defaultSettings(s);
  TEST_ASSERT_FALSE(s.game);  // spento di default
  s.game = true;
  uint8_t blob[kStoredLen];
  StoredMute sm = {MuteKind::None, 0};
  packSettings(s, sm, blob, sizeof blob);
  TEST_ASSERT_EQUAL(1, blob[14]);
  Settings t;
  defaultSettings(t);
  TEST_ASSERT_TRUE(unpackSettings(blob, sizeof blob, t, sm));
  TEST_ASSERT_TRUE(t.game);
  s.game = false;
  packSettings(s, sm, blob, sizeof blob);
  TEST_ASSERT_TRUE(unpackSettings(blob, sizeof blob, t, sm));
  TEST_ASSERT_FALSE(t.game);
}

static void test_switch_follows_service() {
  // La fonte è il servizio (game.on): ogni game riallinea l'interruttore; spento → via le schermate del gioco.
  static Bench b;
  GameMsg g;
  petMsg(b.m().pet, g);
  g.on = true;
  b.ui.netGame(g, b.now);
  TEST_ASSERT_TRUE(b.m().set.game);
  TEST_ASSERT_TRUE(petOn(b.m()));
  b.openPet();
  b.act(Action::Ok);  // Accessorio
  g.on = false;
  b.ui.netGame(g, b.now);
  TEST_ASSERT_FALSE(b.m().set.game);
  TEST_ASSERT_EQUAL(int(MenuId::Main), int(top(b.m()).menu));
  // Dal menu: lo si dà per fatto (pet.on) e parte settings con game.
  static Bench c;
  c.openMenu();
  for (int k = 0; k < 4; k++) c.act(Action::Next);
  c.act(Action::Ok);
  for (int k = 0; k < 8; k++) c.act(Action::Next);
  c.act(Action::Ok);
  TEST_ASSERT_TRUE(petOn(c.m()));
}

static void test_special_toast_once() {
  static Bench b;
  b.gameOn();
  b.noPerm();
  GameMsg g;
  petMsg(b.m().pet, g);
  g.special = true;
  b.ui.netGame(g, b.now);
  TEST_ASSERT_EQUAL_STRING(kPetSpecialToast, b.m().toast.msg);
  TEST_ASSERT_TRUE(b.m().pet.special);
  // Lo stesso special di nuovo (un game ogni pochi minuti): niente secondo toast da questa accensione.
  b.now += 6000;
  b.ui.tick(b.now, b.wc());
  b.ui.netGame(g, b.now);
  TEST_ASSERT_EQUAL_STRING("", b.m().toast.msg);
  // Con un traguardo nello stesso messaggio: prima «Traguardo: …», poi alla sua fine il toast dello speciale.
  static Bench c;
  c.gameOn();
  c.noPerm();
  petMsg(c.m().pet, g);
  g.special = true;
  g.hasCelebrate = true;
  scopy(g.celebrateId, "livello-4");
  scopy(g.celebrateTitle, "Livello 4");
  c.ui.netGame(g, c.now);
  TEST_ASSERT_EQUAL_STRING("Traguardo: Livello 4", c.m().toast.msg);
  for (int k = 0; k < 40; k++) { c.now += 100; c.ui.tick(c.now, c.wc()); }
  TEST_ASSERT_EQUAL_STRING(kPetSpecialToast, c.m().toast.msg);
  // Gioco spento: niente toast.
  static Bench d;
  petMsg(d.m().pet, g);
  g.on = false;
  g.special = true;
  g.hasCelebrate = false;
  d.ui.netGame(g, d.now);
  TEST_ASSERT_EQUAL_STRING("", d.m().toast.msg);
}

std::string readFile(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return "";
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();  // a capo finale degli esempi
  return s;
}

static void test_protocol_examples() {
  // Gli esempi veri di protocol/examples (li scrive il servizio): decodifica, risposte e salvataggio.
  JsonDocument doc;
  GameMsg g;
  const std::string game = readFile("../protocol/examples/game.json");
  TEST_ASSERT_TRUE(game.size() > 0);
  TEST_ASSERT_TRUE(parse(game, doc));
  TEST_ASSERT_TRUE(decodeGame(doc.as<JsonVariantConst>(), g));
  TEST_ASSERT_TRUE(g.on && g.hasWear && !g.hasCelebrate);
  TEST_ASSERT_TRUE(builtinAccessoryById(g.wear.id, g.wear.crc, true) != nullptr);  // il cappellino compilato, stesso CRC
  TEST_ASSERT_TRUE(parse(readFile("../protocol/examples/game-festa.json"), doc));
  TEST_ASSERT_TRUE(decodeGame(doc.as<JsonVariantConst>(), g));
  TEST_ASSERT_TRUE(g.hasCelebrate);
  TEST_ASSERT_EQUAL_STRING("10 task", g.celebrateTitle);
  TEST_ASSERT_TRUE(builtinAccessoryById(g.wear.id, g.wear.crc, true) != nullptr);  // occhiali
  TEST_ASSERT_TRUE(parse(readFile("../protocol/examples/game-spento.json"), doc));
  TEST_ASSERT_TRUE(decodeGame(doc.as<JsonVariantConst>(), g));
  TEST_ASSERT_FALSE(g.on || g.hasWear || g.nUnlocked);
  // accessory: il cappellino vero, uguale a quello compilato; anchors: il Gatto, uguale alle ancore compilate.
  static AccessoryMsg am;
  TEST_ASSERT_TRUE(parse(readFile("../protocol/examples/accessory.json"), doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Ok), int(decodeAccessoryMsg(doc.as<JsonVariantConst>(), am)));
  TEST_ASSERT_EQUAL(int(kBuiltinAccessories[0].len), int(am.len));
  TEST_ASSERT_EQUAL_MEMORY(kBuiltinAccessories[0].data, am.data, am.len);
  static MemAccStorage mem;
  mem.eraseAll();
  static AccessoryStore st;
  st.begin(&mem);
  TEST_ASSERT_EQUAL(int(AccError::Ok), int(st.store(am.id, am.crc, am.data, am.len)));
  static AnchorsMsg an;
  TEST_ASSERT_TRUE(parse(readFile("../protocol/examples/anchors.json"), doc));
  TEST_ASSERT_EQUAL(int(BlobMsgError::Ok), int(decodeAnchorsMsg(doc.as<JsonVariantConst>(), an)));
  TEST_ASSERT_EQUAL_STRING("gatto", an.avatar);
  TEST_ASSERT_EQUAL(int(AncError::Ok), int(st.storeAnchors(an.avatar, an.avatarCrc, an.data, an.len)));
  TEST_ASSERT_EQUAL_MEMORY(kBuiltinAnchors[1].data, an.data, an.len);
  // Le risposte del display, byte per byte come gli esempi.
  char out[200];
  char one[1][kAvatarRefLen] = {"cappellino"};
  encodeAccessoryNeed(one, 1, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(readFile("../protocol/examples/accessory-need.json").c_str(), out);
  char two[2][kAvatarRefLen] = {"gatto", "goccia"};
  const uint32_t crcs[2] = {0xB2432AD7u, 0x33072CEEu};
  encodeAnchorsNeed(two, crcs, 2, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(readFile("../protocol/examples/anchors-need.json").c_str(), out);
  encodeAccessoryWear("occhiali", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(readFile("../protocol/examples/accessory-wear.json").c_str(), out);
  encodeAccessoryWear("", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(readFile("../protocol/examples/accessory-wear-nessuno.json").c_str(), out);
  // settings con il gioco acceso = settings-gioco.json; set-gioco.json lo spegne.
  static Bench b;
  b.m().set.game = true;
  char big[400];
  encodeSettings(remoteValues(b.m().set, b.now), big, sizeof big);
  std::string want = readFile("../protocol/examples/settings-gioco.json");
  while (!want.empty() && (want.back() == '\n' || want.back() == '\r')) want.pop_back();
  TEST_ASSERT_EQUAL_STRING(want.c_str(), big);
  RemoteCmd cmd;
  TEST_ASSERT_TRUE(parse(readFile("../protocol/examples/set-gioco.json"), doc));
  TEST_ASSERT_TRUE(decodeSet(doc.as<JsonVariantConst>(), cmd));
  TEST_ASSERT_TRUE(cmd.ok && cmd.patch.on(RemoteKey::Game) && !cmd.patch.v.game);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decode_game);
  RUN_TEST(test_party_rules);
  RUN_TEST(test_tired_pose);
  RUN_TEST(test_menus_and_wear);
  RUN_TEST(test_menu_capacity);
  RUN_TEST(test_party_and_accessory_frames_match_0140);
  RUN_TEST(test_encoders_and_blob_msgs);
  RUN_TEST(test_store_and_needs);
  RUN_TEST(test_pet_blob);
  RUN_TEST(test_settings_game);
  RUN_TEST(test_switch_follows_service);
  RUN_TEST(test_special_toast_once);
  RUN_TEST(test_protocol_examples);
  return UNITY_END();
}
