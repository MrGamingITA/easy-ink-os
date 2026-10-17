// Schermate del display per il README, disegnate dal codice del firmware (ui::compose, theme_draw, gb_emu +
// gb_frame), non a mano. Dati finti: tools/screenshots/demo-state.json entra dal percorso vero del protocollo
// (parseMessage → decodeState → Ui::netState). Uscita: un PBM P4 250×122 per schermata (1 = inchiostro).
//   uso: device <cartella-uscita> <radice-repo>
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/gb_emu.h"
#include "core/gb_frame.h"
#include "core/gb_rom.h"
#include "core/model.h"
#include "core/pet.h"
#include "core/theme_runtime.h"
#include "core/ui.h"
#include "gfx/canvas.h"
#include "ui/screens.h"
#include "ui/theme_draw.h"

using namespace cal;

namespace {

const Ms kT0 = 1791707400000LL;       // 2026-10-11 10:30:00 Europe/Rome, come demo-state.json › time
const int64_t kTzMs = 2 * 3600000LL;  // CEST
std::string gOut, gRepo;

std::string readFile(const std::string &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) { fprintf(stderr, "manca %s\n", p.c_str()); exit(2); }
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void writePbm(const std::string &name, const uint8_t *bits) {
  std::ofstream out(gOut + "/" + name + ".pbm", std::ios::binary);
  out << "P4\n250 122\n";
  out.write(reinterpret_cast<const char *>(bits), 32 * 122);
  printf("  %s\n", name.c_str());
}

struct Bench {
  Ui ui;
  Ms now;
  // keep > 0: solo le prime keep sessioni dello state
  explicit Bench(int keep = 0) : ui(kHeltec213), now(kT0) {
    ui.begin(now, wc());
    advance(2700);  // fine del boot
    std::string js = readFile(gRepo + "/tools/screenshots/demo-state.json");
    JsonDocument doc;
    StateMsg msg;
    if (!parseMessage(js.data(), js.size(), doc) || msgType(doc.as<JsonVariantConst>()) != MsgType::State ||
        !decodeState(doc.as<JsonVariantConst>(), msg)) {
      fprintf(stderr, "demo-state.json non valido\n");
      exit(2);
    }
    if (keep > 0 && keep < msg.nSessions) msg.nSessions = keep;
    ui.netState(msg, now);
    advance(200);
  }
  WallClock wc() const { WallClock w; w.localMs = now + kTzMs; return w; }
  Model &m() { return ui.model(); }
  void advance(Ms ms) {
    for (Ms end = now + ms; now < end;) { now += 100; ui.tick(now, wc()); }
  }
  void act(Action a) { ui.act(a, now, wc()); advance(300); }
  void shot(const std::string &name) {
    Canvas<250, 122> cv;
    ui::compose(cv, kHeltec213, m(), ui.frame(now), now, wc());
    writePbm(name, cv.data());
  }
  void game(bool on) {
    GameMsg g;
    petMsg(m().pet, g);
    g.on = on;
    g.hasWear = true;
    setAvatarRef(g.wear, "cappellino");
    ui.netGame(g, now);
    advance(4000);  // passano i toast
  }
};

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "uso: device <cartella-uscita> <radice-repo>\n"); return 2; }
  gOut = argv[1];
  gRepo = argv[2];

  {  // Home (stile Goccia, di serie): mostra la sessione più urgente
    static Bench b;
    b.shot("home");
  }
  {  // Home stile Righe
    static Bench b;
    b.m().set.homeStyle = HomeStyle::Righe;
    b.shot("home-righe");
  }
  {  // Dettaglio di una sessione al lavoro, poi la richiesta di permesso
    static Bench b;
    b.act(Action::Prev);  // dalla sessione in permesso (selezionata) ad app-meteo
    b.act(Action::Ok);
    b.shot("dettaglio");
  }
  {
    static Bench b;
    b.act(Action::Ok);  // dettaglio di sito-ricette (PERMESSO)
    b.act(Action::Ok);  // schermata Permessi
    b.shot("permesso");
  }
  {
    static Bench b;
    b.act(Action::Menu);
    b.shot("menu");
  }
  {  // Oggi: costi e numeri della giornata
    static Bench b;
    pushScreen(b.m(), makeScreen(ScreenId::Oggi));
    b.advance(200);
    b.shot("oggi");
  }
  {  // La mascotte (gioco acceso)
    static Bench b;
    b.game(true);
    pushScreen(b.m(), makeScreen(ScreenId::Pet));
    b.advance(200);
    b.shot("mascotte");
  }
  {  // Riposo
    static Bench b;
    b.act(Action::Sleep);
    b.advance(1000);
    b.shot("riposo");
  }

  // Tema installato: «Mascotte grande» (protocol/themes/fixtures/prova.cthm, compilato dal servizio)
  {
    static theme::MemStorage st;
    static theme::Runtime rt;
    const std::string f = readFile(gRepo + "/protocol/themes/fixtures/prova.cthm");
    st.tmpBegin();
    st.tmpAppend(reinterpret_cast<const uint8_t *>(f.data()), uint32_t(f.size()));
    st.tmpCommit("prova");
    rt.begin(&st);
    if (!rt.activate("prova")) { fprintf(stderr, "tema prova non attivabile\n"); return 1; }
    ui::setThemeRuntime(&rt);
    static Bench b;
    b.shot("tema-mascotte-grande");
    static Bench one(1);  // con una sola sessione il tema mostra la mascotte grande
    one.shot("tema-mascotte-grande-una");
    ui::setThemeRuntime(nullptr);
  }

  // Game Boy: ROM di prova dmg-acid2 (MIT) nell'emulatore del firmware, bande disegnate da ui::draw (GbPlay)
  {
    const std::string s = readFile(gRepo + "/firmware/test/roms/dmg-acid2.gb");
    std::vector<uint8_t> rom(s.begin(), s.end());
    gb::RomInfo info;
    if (!gb::parseHeader(rom.data(), rom.size(), info)) { fprintf(stderr, "intestazione ROM\n"); return 1; }
    static Bench b;
    b.ui.gbInstalled(true, true, info.title, uint32_t(rom.size() / 1024));
    b.m().sys.padLink = pad::Link::Ready;
    ScreenEntry e = makeScreen(ScreenId::List);
    e.menu = MenuId::GbRoms;
    pushScreen(b.m(), e);
    b.ui.gbLoad(b.now);
    b.ui.gbPlay(b.now);
    gb::Emu emu;
    if (emu.load(rom.data(), uint32_t(rom.size())) != gb::Load::Ok) { fprintf(stderr, "ROM non caricata\n"); return 1; }
    for (int i = 0; i < 60; i++) emu.runFrame(0);
    Canvas<250, 122> bands;
    bands.clear(false);
    CanvasSurface<250, 122> surf(bands);
    ui::draw(surf, kHeltec213, b.m(), 0, b.now, b.wc());
    uint8_t out[gb::kScreenBytes];
    gb::composeFrame(bands.data(), emu.frame(), false, out);
    writePbm("gioco-dmg-acid2", out);
  }
  return 0;
}
