// Schermate della classe S contro il simulatore.
// Per ogni scenario di tools/sim-capture/scenarios.mjs (letto da out/heltec213/manifest.json, che
// riporta i passi di ogni cattura) rifà la stessa sequenza con cal::Ui, allo stesso istante e con lo
// stesso orologio della cattura, disegna i fotogrammi 0 e 1 con ui::compose() e confronta:
//   1. il replay: schermata in cima alla pila, tempo trascorso e «Ultimo comando» come nel manifest;
//   2. il layout: ogni testo (stringa già accorciata, x, y, corpo, peso, famiglia, allineamento)
//      uguale, nello stesso ordine, a manifest › texts (0 differenze ammesse);
//   3. i pixel: % di pixel diversi dal PBM di riferimento, sotto kMaxDiffPct.
// I PBM del firmware vanno in tools/sim-capture/fw/heltec213/ per diff.mjs.
//
// I riferimenti si leggono da disco (non da un header generato): sono già nel repository, il test
// deve comunque scrivere i PBM del firmware accanto, e così un nuovo scenario aggiunto a
// scenarios.mjs entra nel test con la sola ricattura (node capture.mjs), senza rigenerare niente.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/avatars.h"
#include "core/gesture.h"
#include "core/pet.h"
#include "core/remote_settings.h"
#include "core/ui.h"
#include "core/workday.h"
#include "gfx/accessory.h"
#include "gfx/accessory_builtin.h"
#include "gfx/avatar_builtin.h"
#include "gfx/canvas.h"
#include "ui/screens.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const double kMaxDiffPct = 0.3;           // obiettivo per schermata
const Ms kT0 = 1791707400000LL;           // 2026-10-11 10:30:00 Europe/Rome (manifest › frozenInstant)
const int64_t kTzMs = 2 * 3600000LL;      // CEST, UTC+2
const int kW = 250, kH = 122;

// ---------------------------------------------------------------- JSON minimo (solo lettura)
struct J {
  enum Kind { Null, Bool, Num, Str, Arr, Obj } k = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<J> a;
  std::vector<std::pair<std::string, J>> o;
  const J *get(const char *key) const {
    for (auto &kv : o)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
};

struct Parser {
  const char *p;
  void ws() { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; }
  static void put(std::string &s, unsigned c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xC0 | (c >> 6)); s += char(0x80 | (c & 0x3F)); }
    else { s += char(0xE0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
  }
  std::string str() {
    std::string s;
    p++;  // "
    while (*p && *p != '"') {
      if (*p == '\\') {
        p++;
        switch (*p) {
          case 'n': s += '\n'; break;
          case 't': s += '\t'; break;
          case 'r': s += '\r'; break;
          case 'b': s += '\b'; break;
          case 'f': s += '\f'; break;
          case 'u': { unsigned c = strtoul(std::string(p + 1, 4).c_str(), nullptr, 16); put(s, c); p += 4; break; }
          default: s += *p; break;
        }
        p++;
      } else s += *p++;
    }
    if (*p) p++;
    return s;
  }
  J val() {
    ws();
    J j;
    if (*p == '{') {
      j.k = J::Obj;
      p++;
      ws();
      if (*p == '}') { p++; return j; }
      while (true) {
        ws();
        std::string key = str();
        ws();
        p++;  // :
        j.o.emplace_back(key, val());
        ws();
        if (*p == ',') { p++; continue; }
        p++;  // }
        break;
      }
    } else if (*p == '[') {
      j.k = J::Arr;
      p++;
      ws();
      if (*p == ']') { p++; return j; }
      while (true) {
        j.a.push_back(val());
        ws();
        if (*p == ',') { p++; continue; }
        p++;  // ]
        break;
      }
    } else if (*p == '"') {
      j.k = J::Str;
      j.s = str();
    } else if (!strncmp(p, "true", 4)) { j.k = J::Bool; j.b = true; p += 4; }
    else if (!strncmp(p, "false", 5)) { j.k = J::Bool; p += 5; }
    else if (!strncmp(p, "null", 4)) { p += 4; }
    else { j.k = J::Num; char *e; j.n = strtod(p, &e); p = e; }
    return j;
  }
};

// ---------------------------------------------------------------- percorsi
std::string repoRoot() {
  if (const char *e = getenv("CALAMAIO_REPO")) return std::string(e) + "/";
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/test_ui/");
  if (k != std::string::npos) return f.substr(0, k);
  const size_t k2 = f.find("test/test_ui/");  // percorso relativo: il test gira da firmware/
  if (k2 != std::string::npos) return f.substr(0, k2) + "../";
  return "../";
}

bool readFile(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

// PBM P4, 1 = inchiostro.
bool readPbm(const std::string &path, std::vector<uint8_t> &bits) {
  std::string d;
  if (!readFile(path, d) || d.size() < 2 || d[0] != 'P' || d[1] != '4') return false;
  size_t pos = 2;
  int nums[2], got = 0;
  while (got < 2) {
    while (pos < d.size() && isspace((unsigned char)d[pos])) pos++;
    if (pos < d.size() && d[pos] == '#') { while (pos < d.size() && d[pos] != '\n') pos++; continue; }
    nums[got++] = atoi(d.c_str() + pos);
    while (pos < d.size() && isdigit((unsigned char)d[pos])) pos++;
  }
  pos++;  // uno spazio dopo l'altezza
  if (nums[0] != kW || nums[1] != kH) return false;
  const int stride = (kW + 7) / 8;
  if (d.size() < pos + size_t(stride * kH)) return false;
  bits.assign(kW * kH, 0);
  for (int y = 0; y < kH; y++)
    for (int x = 0; x < kW; x++) bits[y * kW + x] = (uint8_t(d[pos + y * stride + (x >> 3)]) >> (7 - (x & 7))) & 1;
  return true;
}

void writePbm(const std::string &path, const Canvas<kW, kH> &cv) {
  std::ofstream out(path, std::ios::binary);
  out << "P4\n" << kW << " " << kH << "\n";
  out.write(reinterpret_cast<const char *>(cv.data()), Canvas<kW, kH>::kStride * kH);
}

// ---------------------------------------------------------------- testi disegnati
struct Txt {
  std::string s;
  int x, y, size, weight;
  bool pix;
  std::string align;
};
const char *alignKey(Align a) { return a == Align::Center ? "center" : a == Align::Right ? "right" : "left"; }
void onText(const char *s, int x, int y, int size, int weight, bool pix, Align a, void *ctx) {
  static_cast<std::vector<Txt> *>(ctx)->push_back(Txt{s, x, y, size, weight, pix, alignKey(a)});
}
// '700 20px "Chakra Petch", …' · 'bold 18px …' · '400 8px "Silkscreen", monospace'
Txt fromManifest(const J &t) {
  Txt r;
  r.s = t.get("s")->s;
  r.x = int(t.get("x")->n);
  r.y = int(t.get("y")->n);
  r.align = t.get("align")->s;
  const std::string font = t.get("font")->s;
  // Il canvas omette il peso 400 («8px "Silkscreen"») e scrive 700 come «bold».
  const std::string w = font.substr(0, font.find(' '));
  const bool noWeight = w.size() > 2 && w.compare(w.size() - 2, 2, "px") == 0;
  r.weight = noWeight || w == "normal" ? 400 : w == "bold" ? 700 : atoi(w.c_str());
  r.size = atoi(noWeight ? font.c_str() : font.c_str() + font.find(' ') + 1);
  r.pix = font.find("Silkscreen") != std::string::npos;
  return r;
}
std::string show(const Txt &t) {
  char b[400];
  snprintf(b, sizeof b, "«%s» %d,%d %d/%d%s %s", t.s.c_str(), t.x, t.y, t.size, t.weight, t.pix ? "pix" : "", t.align.c_str());
  return b;
}

// ---------------------------------------------------------------- replay come capture.mjs
// Tempi di capture.mjs: pressione 60 ms, pausa 120, finestra 400, lunga 800, tenuto 3100.
// Orologio: tick() ogni 100 ms dall'istante congelato (setInterval del componente); a parità di
// istante il tick parte prima del timer dei gesti (creato dopo, id più alto).
struct Replay {
  Ui ui;
  Ms now;
  GestureRecognizer gr;
  std::string simAction;
  bool lastWasSim;
  Replay() : ui(kHeltec213), now(kT0), lastWasSim(false) {}
  WallClock wc() const { WallClock w; w.localMs = now + kTzMs; return w; }
  uint32_t rel() const { return uint32_t(now - kT0); }
  void begin() { ui.begin(now, wc()); }
  void advance(Ms ms) {
    const Ms end = now + ms;
    while (now < end) {
      now++;
      if ((now - kT0) % 100 == 0) {
        const std::string before = ui.lastAction();
        ui.tick(now, wc());
        // tick() del simulatore che scrive «Ultimo comando» (fine della giornata di lavoro): vale più del pulsante
        if (before != ui.lastAction()) lastWasSim = false;
      }
      const Gesture g = gr.poll(rel());
      if (g != Gesture::None) { ui.gesture(g, now, wc()); lastWasSim = false; }
    }
  }
  void down() { gr.down(rel()); ui.keyDown(now); }
  void up() {
    const Gesture g = gr.up(rel());
    ui.keyUp(now);  // come keyUp() del simulatore: prima l'indicatore, poi il gesto
    if (g != Gesture::None) { ui.gesture(g, now, wc()); lastWasSim = false; }
  }
  void press(Ms hold) { down(); advance(hold); up(); }
  // Pulsanti del pannello Simula (simClicks del simulatore): agiscono sul modello come lì.
  bool sim(const std::string &id) {
    Model &m = ui.model();
    Sys &s = m.sys;
    char msg[96];
    if (id == "pc") {
      s.pc = !s.pc;
      if (!s.pc) s.lastPc = now; else s.pcOff = false;
      toast(m, s.pc ? "PC di nuovo raggiungibile" : "PC non raggiungibile", now);
      simAction = s.pc ? "Il servizio sul PC risponde di nuovo" : "Servizio sul PC spento";
    } else if (id == "wifi") {
      s.wifi = !s.wifi;
      s.pc = s.wifi;
      if (!s.wifi) s.lastPc = now;
      toast(m, s.wifi ? "Wi-Fi di nuovo connesso" : "Wi-Fi assente", now);
      simAction = s.wifi ? "Wi-Fi tornato" : "Wi-Fi assente: anche il PC non è raggiungibile";
    } else if (id == "usb") {
      s.usb = !s.usb;
      toast(m, s.usb ? "In carica" : "A batteria", now);
      simAction = s.usb ? "Cavo USB-C collegato" : "Cavo USB-C scollegato";
    } else if (id == "batt") {
      s.batt = s.batt - 20 < 3 ? 3 : s.batt - 20;
      if (s.batt < 15 && !s.usb) toast(m, "Batteria bassa · risparmio attivo", now);
      snprintf(msg, sizeof msg, "Batteria al %d%%", cal::jsRound(s.batt));
      simAction = msg;
    } else if (id == "battlvl") {  // livello 100 → 50 → 10 %, a giro (da un valore fuori giro: 100)
      static const double kLvl[3] = {100, 50, 10};
      int k = -1;
      for (int q = 0; q < 3; q++)
        if (s.batt == kLvl[q]) k = q;
      s.batt = kLvl[(k + 1) % 3];
      if (s.batt < 15 && !s.usb) toast(m, "Batteria bassa · risparmio attivo", now);
      snprintf(msg, sizeof msg, "Batteria al %d%%", cal::jsRound(s.batt));
      simAction = msg;
    } else if (id == "pcoff") {
      if (!s.pcOff) {
        s.pcOff = true;
        s.pc = false;
        s.offAt = now;
        s.lastPc = s.offAt;
        resetStack(m, makeScreen(ScreenId::Home));
        logEv(m, nullptr, "PC spento", now);
        simAction = "PC spento: il display mostra il riepilogo e resta fermo";
      } else {
        s.pcOff = false;
        s.pc = true;
        toast(m, "PC acceso", now);
        logEv(m, nullptr, "PC acceso", now);
        simAction = "PC di nuovo acceso";
      }
    } else if (id == "limit") {
      if (!m.limits.hasH5) { m.limits.hasH5 = true; m.limits.h5Reset = now + 130 * 60000LL; }
      m.limits.h5 = 92;
      toast(m, "Limite 5 ore al 92%", now);
      simAction = "Limite delle 5 ore al 92%: Goccia è stanca";
      if (!quiet(m, now)) m.burstUntil = now + 4000;  // changed()
    } else if (id == "acc") {
      m.acc = !m.acc;
      if (m.acc) m.air.co2 = 820;
      toast(m, m.acc ? "Accessori collegati" : "Accessori scollegati", now);
      simAction = m.acc ? "Sensore CO2 e buzzer collegati ai pin" : "Accessori scollegati";
    } else if (id == "air") {
      m.acc = true;
      m.air.co2 = 1250;
      toast(m, "CO2 alta: apri la finestra", now);
      simAction = "CO2 a 1250 ppm: Goccia boccheggia";
      if (!quiet(m, now)) m.burstUntil = now + 4000;  // changed()
    } else if (id == "empty") {  // M3: «Nessuna sessione» (state con sessions vuoto)
      if (m.nSessions) {
        nBak = m.nSessions;
        memcpy(bak, m.sessions, sizeof bak);
        m.nSessions = 0;
        while (top(m).id == ScreenId::Detail || top(m).id == ScreenId::Perm) popScreen(m);
        m.ui.sel = 0;
        m.ui.selUntil = 0;
        m.ui.page = 0;
        simAction = "Nessuna sessione aperta sul PC";
      } else {
        m.nSessions = nBak;
        memcpy(m.sessions, bak, sizeof bak);
        simAction = "Sessioni di nuovo aperte";
      }
    } else if (id == "nolimits") {  // M3: limits null dal PC
      if (m.limits.hasH5 || m.limits.hasD7) {
        m.limits.hasH5 = m.limits.hasD7 = false;
        simAction = "Limiti: nessun dato dal PC";
      } else {
        m.limits.hasH5 = m.limits.hasD7 = true;
        simAction = "Limiti di nuovo dal PC";
      }
    } else if (id == "joinfail") {  // M3: la rete non risponde (stessa funzione della rete vera)
      if (top(m).id == ScreenId::Connecting) {
        const std::string ssid = top(m).ssid;
        ui.netConnectFailed(ssid.c_str(), now);
        simAction = "Connessione a " + ssid + " non riuscita";
      } else {
        simAction = "Nessuna connessione in corso";
      }
    } else if (id == "elsewhere") {  // M3: permesso risposto nel terminale mentre il display aspetta
      const bool perm = top(m).id == ScreenId::Perm;
      int i = perm ? top(m).i : -1;
      for (int k = 0; k < m.nSessions && i < 0; k++)
        if (m.sessions[k].state == SessionState::Permission) i = k;
      if (i < 0) {
        simAction = "Nessun permesso in attesa";
      } else {
        Session &s = m.sessions[i];
        s.state = SessionState::Working;
        s.ticks = 0;
        m.stats.permTerminal++;
        addWait(m, s, now);
        logEv(m, &s, "risposto dal terminale", now);
        if (perm) {
          popScreen(m);
          toast(m, "Risposto altrove", now);
        } else if (!quiet(m, now)) {
          m.burstUntil = now + 4000;  // changed()
        }
        simAction = std::string("Permesso di ") + s.name + " risposto nel terminale";
      }
    } else if (id == "pair") {  // M5: abbinamento dal PC (stesso codice dei vettori di prova)
      ui.netPairCode("175397", now);
      simAction = "Abbinamento: scrivi il codice sul PC";
    } else if (id == "pairok") {
      if (top(m).id != ScreenId::Pair) simAction = "Nessun abbinamento in corso";
      else { ui.netPairReady(now); simAction = "Il PC ha il codice giusto: conferma con 2 pressioni"; }
    } else if (id == "pairbad") {
      if (top(m).id != ScreenId::Pair) simAction = "Nessun abbinamento in corso";
      else { ui.netPairAborted("codice", now); simAction = "Abbinamento annullato dal PC (codice)"; }
    } else if (id == "redo") {
      s.pair = s.pair == PairState::Redo ? PairState::Paired : PairState::Redo;
      if (s.pair == PairState::Paired && !s.pairHost[0]) snprintf(s.pairHost, sizeof s.pairHost, "%s", "calamaio.local");
      simAction = s.pair == PairState::Redo ? "Abbinamento da rifare: le chiavi del display e del PC non coincidono" : "Abbinamento di nuovo valido";
    } else if (id == "prompt") {  // Archivia: «Nuovo prompt» rimette al lavoro una sessione archiviata o ferma
      int si = -1;
      if (m.nArchived && m.nSessions < kMaxSessions) {
        si = m.nSessions++;
        m.sessions[si] = m.archived[0].s;
        for (int k = 0; k + 1 < m.nArchived; k++) m.archived[k] = m.archived[k + 1];
        m.nArchived--;
      }
      for (int k = 0; k < m.nSessions && si < 0; k++)
        if (m.sessions[k].state == SessionState::Done || m.sessions[k].state == SessionState::Idle) si = k;
      if (si < 0) {
        simAction = "Nessuna sessione ferma o archiviata";
      } else {
        Session &s = m.sessions[si];
        const char *names[3] = {"Analizza il codice", "Implementa", "Verifica finale"};
        s.nTasks = 3;
        for (int k = 0; k < 3; k++) {
          snprintf(s.tasks[k].name, sizeof s.tasks[k].name, "%s", names[k]);
          s.tasks[k].status = k == 0 ? TaskStatus::Doing : TaskStatus::Todo;
        }
        countTasks(s);
        s.state = SessionState::Working;
        snprintf(s.detail, sizeof s.detail, "%s", "Read package.json");
        s.since = now;
        s.ticks = 0;
        s.summary[0] = 0;
        s.test = TestResult::None;
        logEv(m, &s, "nuovo prompt, 3 task", now);
        simAction = std::string("Nuovo prompt a ") + s.name;
        if (!quiet(m, now)) m.burstUntil = now + 4000;  // changed()
      }
    } else if (id == "avatar") {  // avatar della sessione mostrata: predefinito → libreria tranne la Goccia → predefinito
      if (!m.nSessions) {
        simAction = "Nessuna sessione";
      } else {
        const ScreenEntry &tp = top(m);
        const int i = tp.id == ScreenId::Detail ? m.ui.sel
                      : tp.id == ScreenId::Perm ? tp.i
                      : m.set.homeStyle == HomeStyle::Una ? unaIdx(m, now) : sIdx(m, now);
        Session &se = m.sessions[i];
        std::vector<std::string> L{""};
        for (int k = 0; k < kBuiltinAvatarCount; k++)
          if (strcmp(kBuiltinAvatars[k].id, "goccia") != 0) L.push_back(kBuiltinAvatars[k].id);
        size_t k = 0;
        while (k < L.size() && L[k] != se.avatar.id) k++;
        const std::string next = L[k < L.size() ? (k + 1) % L.size() : 0];
        setAvatarRef(se.avatar, next.c_str());
        simAction = std::string("Avatar di ") + se.name + ": " + (next.empty() ? "predefinito" : builtinAvatarById(next.c_str())->name);
      }
    } else if (id == "defavatar") {  // avatar predefinito: goccia → gatto → demone → calamaro → goccia
      int k = 0;
      while (k < kBuiltinAvatarCount && strcmp(kBuiltinAvatars[k].id, m.set.defAvatar.id) != 0) k++;
      const char *next = kBuiltinAvatars[k < kBuiltinAvatarCount ? (k + 1) % kBuiltinAvatarCount : 0].id;
      setAvatarRef(m.set.defAvatar, next);
      simAction = std::string("Avatar predefinito: ") + builtinAvatarById(next)->name;
    } else if (id == "noclock") {  // 0.10.1: ora non ancora nota (né welcome né SNTP): «--:--», a giro
      s.wallOk = !s.wallOk;
      simAction = s.wallOk ? "Ora vera arrivata (welcome o SNTP)" : "Ora non ancora nota: l'orologio mostra «--:--»";
    } else if (id == "battcrit") {  // M6: batteria scarica, ultima schermata prima del sonno profondo
      s.batt = 3;
      s.usb = false;
      memset(&m.calib, 0, sizeof m.calib);
      resetStack(m, makeScreen(ScreenId::Home));
      pushScreen(m, makeScreen(ScreenId::BattLow));
      logEv(m, nullptr, "batteria scarica: sonno profondo", now);
      simAction = "Batteria scarica: il display si spegne finché non colleghi la USB-C";
    } else if (id == "ota") {  // aggiornamento dal PC a giro, come simClicks.ota del simulatore
      const uint32_t total = 1556033;
      OtaView &o = m.ota;
      const int seq = o.phase == OtaPhase::None ? 0 : otaSeq + 1;
      if (seq > 6) {
        if (top(m).id == ScreenId::Ota) popScreen(m);
        memset(&o, 0, sizeof o);
        otaSeq = 0;
        simAction = "Aggiornamento: schermata chiusa";
      } else {
        if (seq == 0) {
          memset(&o, 0, sizeof o);
          snprintf(o.from, sizeof o.from, "%s", "0.6.0");
          snprintf(o.to, sizeof o.to, "%s", "0.7.0");
          o.total = total;
          pushScreen(m, makeScreen(ScreenId::Ota));
        }
        otaSeq = seq;
        static const OtaPhase kPh[7] = {OtaPhase::Receive, OtaPhase::Receive, OtaPhase::Receive, OtaPhase::Verify,
                                        OtaPhase::Reboot, OtaPhase::Error, OtaPhase::Rejected};
        static const double kF[7] = {0, 0.37, 0.8, 1, 1, 0.62, 0};
        o.phase = kPh[seq];
        o.step = seq;
        o.until = 0;
        o.why[0] = 0;
        o.done = uint32_t(ceil(total * kF[seq]));
        if (o.phase == OtaPhase::Error) { snprintf(o.why, sizeof o.why, "%s", "download interrotto: nessun dato da 15 s"); o.until = now + kOtaResultMs; }
        if (o.phase == OtaPhase::Rejected) { snprintf(o.why, sizeof o.why, "%s", "batteria al 42%: collega il cavo USB o ricarica fino al 50%"); o.until = now + kOtaResultMs; }
        static const char *const kL[7] = {"", "", "", "verifica", "fatto, riavvio", "errore", "rifiutato"};
        char b[96];
        if (seq < 3) snprintf(b, sizeof b, "Aggiornamento 0.6.0 → 0.7.0: ricezione %d%%", int(uint64_t(o.done) * 100 / total));
        else snprintf(b, sizeof b, "Aggiornamento 0.6.0 → 0.7.0: %s", kL[seq]);
        simAction = b;
      }
    } else if (id == "perm") {  // simEvent('permission'): la prima sessione che lavora (o non aspetta, o la prima)
      int i = -1;
      for (int k = 0; k < m.nSessions && i < 0; k++)
        if (m.sessions[k].state == SessionState::Working) i = k;
      for (int k = 0; k < m.nSessions && i < 0; k++)
        if (!isAttn(m.sessions[k])) i = k;
      if (i < 0 && m.nSessions) i = 0;
      if (i < 0) {
        simAction = "Nessuna sessione: apri Claude Code sul PC";
      } else {
        Rng rng(1);
        ui.raise(i, SessionState::Permission, now, rng);
        simAction = std::string("Richiesta di permesso da ") + m.sessions[i].name;
      }
    } else if (id == "work") {  // 0.9.0: come il comando workday del PC (start, poi stop)
      char err[96];
      ui.workdayRemote(!m.work.on, now, wc(), err, sizeof err);
      simAction = ui.lastAction();  // «Dal PC: …» come il pulsante Simula
      if (err[0]) simAction += std::string(" (ack: ") + err + ")";
    } else if (id == "worklast" || id == "workend") {  // solo nel simulatore: la fine fra 25 minuti, o adesso
      if (!m.work.on) { m.work.on = true; m.work.fromLocal = wc().localMs; m.work.doneUntilLocal = 0; }
      m.work.endLocal = wc().localMs + (id == "worklast" ? 25 * 60000LL : 0);
      simAction = id == "worklast" ? "Giornata di lavoro: fine fra 25 minuti" : "Giornata di lavoro: salta alla fine";
    } else if (id == "gameseed") {
      m.game.seed = kGameSeed;
      if (top(m).id == ScreenId::Game) gameNew(m.game, nullptr);
      simAction = "Gioco: seme fisso " + std::to_string(kGameSeed);
    } else if (id == "petlevel" || id == "petgoal" || id == "petwear" || id == "pettired" || id == "petspecial" || id == "game") {
      // 0.10.0: messaggi `game` del servizio, costruiti sul riassunto di adesso come i pulsanti del simulatore.
      static GameMsg g;
      petMsg(m.pet, g);
      auto unlocked = [&](const char *aid) {
        for (int k = 0; k < g.nUnlocked; k++)
          if (strcmp(g.unlocked[k].id, aid) == 0) return true;
        return false;
      };
      auto unlock = [&](const Accessory *a) {
        PetUnlocked &u = g.unlocked[g.nUnlocked++];
        snprintf(u.id, sizeof u.id, "%s", a->id);
        snprintf(u.name, sizeof u.name, "%s", a->name);
      };
      if (id == "petlevel") {
        g.level = m.pet.level + 1;
        g.xpLevelStart = m.pet.xpNext;
        g.xpNext = m.pet.xpNext + levelCost(g.level);
        g.xp = g.xpLevelStart + 25;
        const Accessory *un = nullptr;
        if (g.level % 2 == 0)
          for (int k = 0; k < kBuiltinAccessoryCount && !un; k++)
            if (!unlocked(builtinAccessory(k)->id)) un = builtinAccessory(k);
        if (un) unlock(un);
        ui.netGame(g, now);
        simAction = "Gioco: livello " + std::to_string(g.level) + (un ? std::string(", sbloccato ") + un->name : "");
      } else if (id == "petgoal") {
        static const char *const kGoals[5][2] = {{"primo-task", "Primo task"}, {"primo-verde", "Primo verde"}, {"task-10", "10 task"},
                                                 {"serie-7", "7 giorni di serie"}, {"livello-5", "Livello 5"}};
        const char *const *q = kGoals[goalSeq++ % 5];
        g.hasCelebrate = true;
        snprintf(g.celebrateId, sizeof g.celebrateId, "%s", q[0]);
        snprintf(g.celebrateTitle, sizeof g.celebrateTitle, "%s", q[1]);
        ui.netGame(g, now);
        simAction = ui.lastAction();
      } else if (id == "petwear") {
        int k = -1;  // posizione dell'indossato nel catalogo (-1: nessuno o fuori catalogo)
        for (int q = 0; q < kBuiltinAccessoryCount; q++)
          if (g.hasWear && strcmp(kBuiltinAccessories[q].id, g.wear.id) == 0) k = q;
        // come L = ['', catalogo…]: nessuno → il primo; l'ultimo o uno fuori catalogo → nessuno
        const Accessory *a = !g.hasWear ? builtinAccessory(0) : (k < 0 || k + 1 >= kBuiltinAccessoryCount) ? nullptr : builtinAccessory(k + 1);
        memset(&g.wear, 0, sizeof g.wear);
        g.hasWear = a != nullptr;
        if (a) {
          snprintf(g.wear.id, sizeof g.wear.id, "%s", a->id);
          g.wear.crc = a->crc;
          g.wear.hasCrc = true;
          if (!unlocked(a->id)) unlock(a);
        }
        ui.netGame(g, now);
        simAction = a ? std::string("Dal PC: indossa ") + a->name : "Dal PC: nessun accessorio";
      } else if (id == "game") {  // acceso o spento dal PC: game con on, la scheda si allinea
        g.on = !m.set.game;
        ui.netGame(g, now);
        simAction = std::string("Dal PC: gioco della mascotte ") + (m.set.game ? "acceso" : "spento");
      } else if (id == "petspecial") {
        g.special = !g.special;
        ui.netGame(g, now);
        simAction = std::string("Gioco: accessorio speciale ") + (g.special ? "disponibile" : "chiesto");
      } else {
        g.tired = !g.tired;
        g.energy = g.tired ? 15 : 80;
        ui.netGame(g, now);
        simAction = std::string("Gioco: mascotte ") + (g.tired ? "stanca" : "riposata");
      }
    } else if (id == "calsaved") {  // M6: calibrazione già salvata (8 %/h, 1,5 %/h)
      if (m.bat.cal.valid) memset(&m.bat.cal, 0, sizeof m.bat.cal);
      else { m.bat.cal.valid = true; m.bat.cal.active = 8; m.bat.cal.idle = 1.5; m.bat.cal.atLocal = wc().localMs; }
      simAction = m.bat.cal.valid ? "Batteria calibrata: 8 %/h in Attivo, 1,5 %/h in Attesa" : "Calibrazione cancellata";
    } else {
      return false;
    }
    lastWasSim = true;
    return true;
  }
  Session bak[kMaxSessions];
  int nBak = 0;
  int otaSeq = 0;
  int goalSeq = 0;  // 0.10.0: Simula «Mascotte: traguardo», a giro
  bool run(const std::vector<std::string> &steps, std::string &err) {
    begin();
    for (const std::string &st : steps) {
      if (st.size() == 2 && st[1] == 'x' && st[0] >= '1' && st[0] <= '3') {
        const int n = st[0] - '0';
        for (int k = 0; k < n; k++) {
          if (k) advance(120);
          press(60);
        }
        advance(400);
      } else if (st == "long") { press(800); advance(120); }
      else if (st == "vlong") { press(3100); advance(120); }
      else if (st == "down") down();  // tasto giù e resta giù
      else if (st == "up") up();
      else if (st.rfind("wait:", 0) == 0) advance(atoll(st.c_str() + 5));
      else if (st.rfind("sim:", 0) == 0) {
        if (!sim(st.substr(4))) { err = "pulsante Simula non portato nel test: " + st; return false; }
        advance(120);
      } else { err = "passo sconosciuto: " + st; return false; }
    }
    return true;
  }
  // Testo iniziale del pannello «Ultimo comando» del banco di prova (non è dello schermo).
  std::string lastAction() const {
    if (lastWasSim) return simAction;
    return ui.lastAction()[0] ? ui.lastAction() : "Premi i tasti del dispositivo qui a sinistra.";
  }
};

// ---------------------------------------------------------------- scenari
struct Capture { int frame; const J *c; };
struct Scenario {
  std::string name;
  std::vector<std::string> steps;
  std::vector<Capture> caps;
};

std::string gRoot, gRefDir, gFwDir;
J gManifest;
std::vector<Scenario> gScen;
std::vector<std::string> gNames;
int gCur = 0;
std::string gReport;  // tabella finale

void runScenario() {
  const Scenario &sc = gScen[gCur];
  static Replay *R = nullptr;  // Ui ~17,5 KB: sullo heap
  delete R;
  R = new Replay();
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(R->run(sc.steps, err), err.c_str());

  const J &c0 = *sc.caps[0].c;
  const J *model = c0.get("model");
  std::string msg;
  const std::string scr = screenKey(top(R->ui.model()).id);
  if (scr != model->get("screen")->s) msg += "schermata " + scr + " invece di " + model->get("screen")->s + "; ";
  if (double(R->now - kT0) != c0.get("elapsedMs")->n) msg += "tempo diverso; ";
  if (R->lastAction() != c0.get("lastAction")->s) msg += "ultimo comando «" + R->lastAction() + "» invece di «" + c0.get("lastAction")->s + "»; ";
  TEST_ASSERT_TRUE_MESSAGE(msg.empty(), ("replay: " + msg).c_str());

  std::string fails;
  for (const Capture &cap : sc.caps) {
    Canvas<kW, kH> cv;
    std::vector<Txt> got;
    ui::setTextHook(onText, &got);
    // f del manifest è il contatore: a schermo va frameFor (0 con il risparmio energetico), come in capture.mjs.
    ui::compose(cv, kHeltec213, R->ui.model(), frameFor(kHeltec213, R->ui.model(), cap.frame, R->now), R->now, R->wc());
    ui::setTextHook(nullptr, nullptr);
    const std::string base = sc.name + "-f" + std::to_string(cap.frame);
    writePbm(gFwDir + base + ".pbm", cv);

    // layout dei testi
    const J *texts = cap.c->get("texts");
    int layoutDiff = 0;
    std::string firstDiff;
    const size_t nt = texts->a.size() > got.size() ? texts->a.size() : got.size();
    for (size_t i = 0; i < nt; i++) {
      const bool hasRef = i < texts->a.size(), hasGot = i < got.size();
      Txt ref = hasRef ? fromManifest(texts->a[i]) : Txt{};
      const bool same = hasRef && hasGot && ref.s == got[i].s && ref.x == got[i].x && ref.y == got[i].y && ref.size == got[i].size &&
                        ref.weight == got[i].weight && ref.pix == got[i].pix && ref.align == got[i].align;
      if (!same) {
        layoutDiff++;
        if (firstDiff.empty())
          firstDiff = "testo " + std::to_string(i) + ": atteso " + (hasRef ? show(ref) : "nessuno") + ", disegnato " +
                      (hasGot ? show(got[i]) : "nessuno");
      }
    }

    // pixel
    std::vector<uint8_t> ref;
    TEST_ASSERT_TRUE_MESSAGE(readPbm(gRefDir + base + ".pbm", ref), ("riferimento illeggibile: " + base).c_str());
    int diff = 0, x0 = kW, y0 = kH, x1 = -1, y1 = -1;
    for (int y = 0; y < kH; y++)
      for (int x = 0; x < kW; x++)
        if (ref[y * kW + x] != (cv.get(x, y) ? 1 : 0)) {
          diff++;
          if (x < x0) x0 = x;
          if (x > x1) x1 = x;
          if (y < y0) y0 = y;
          if (y > y1) y1 = y;
        }
    const double pct = diff * 100.0 / (kW * kH);
    char line[256];
    if (diff)
      snprintf(line, sizeof line, "%-34s %6.3f%%  %4d px  [%d,%d..%d,%d]  testi %zu, diversi %d\n", base.c_str(), pct, diff, x0, y0, x1, y1,
               texts->a.size(), layoutDiff);
    else
      snprintf(line, sizeof line, "%-34s %6.3f%%  %4d px  %-17s  testi %zu, diversi %d\n", base.c_str(), pct, diff, "", texts->a.size(), layoutDiff);
    gReport += line;
    if (layoutDiff) fails += base + ": " + std::to_string(layoutDiff) + " testi diversi (" + firstDiff + "); ";
    if (pct >= kMaxDiffPct) {
      snprintf(line, sizeof line, "%s: %.3f%% di pixel diversi; ", base.c_str(), pct);
      fails += line;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(fails.empty(), fails.c_str());
}

bool loadScenarios(std::string &err) {
  std::string text;
  if (!readFile(gRefDir + "manifest.json", text)) { err = "manca " + gRefDir + "manifest.json"; return false; }
  Parser p{text.c_str()};
  gManifest = p.val();
  const J *caps = gManifest.get("captures");
  if (!caps || caps->k != J::Arr) { err = "manifest senza captures"; return false; }
  for (const J &c : caps->a) {
    const std::string name = c.get("name")->s;
    Scenario *sc = nullptr;
    for (auto &s : gScen)
      if (s.name == name) sc = &s;
    if (!sc) {
      gScen.push_back(Scenario{name, {}, {}});
      sc = &gScen.back();
      for (const J &st : c.get("steps")->a) sc->steps.push_back(st.s);
    }
    sc->caps.push_back(Capture{int(c.get("frame")->n), &c});
  }
  return !gScen.empty();
}

void test_sources() {
  // Il manifest deve venire dagli stessi sorgenti del simulatore (altrimenti: node capture.mjs).
  const J *src = gManifest.get("simulatorSha256");
  TEST_ASSERT_NOT_NULL(src);
  TEST_ASSERT_TRUE(gScen.size() >= 50);
}

}  // namespace

int main() {
  gRoot = repoRoot();
  gRefDir = gRoot + "tools/sim-capture/out/heltec213/";
  gFwDir = gRoot + "tools/sim-capture/fw/heltec213/";
  std::filesystem::create_directories(gFwDir);
  std::string err;
  if (!loadScenarios(err)) {
    printf("scenari non caricati: %s\n", err.c_str());
    return 1;
  }
  UNITY_BEGIN();
  RUN_TEST(test_sources);
  // Un test Unity per scenario (nome = scenario), così il resoconto dice quale schermata cede.
  gNames.reserve(gScen.size());
  for (auto &s : gScen) gNames.push_back("scenario_" + s.name);
  for (gCur = 0; gCur < int(gScen.size()); gCur++) UnityDefaultTestRun(runScenario, gNames[gCur].c_str(), __LINE__);
  printf("\n--- differenze firmware / simulatore (soglia %.1f%%) ---\n%s", kMaxDiffPct, gReport.c_str());
  std::ofstream(gFwDir + "report.txt") << gReport;
  return UNITY_END();
}
