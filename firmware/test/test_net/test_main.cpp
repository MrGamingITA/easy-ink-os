// M3: protocollo → modello (core/apply_state), nomi di rete e QR (core/netfmt, gfx/qr) e i flussi
// dell'interfaccia con la rete vera (cal::Ui in sys.live). Gli esempi del protocollo si leggono da
// protocol/examples/ (la stessa fonte del servizio): pio test gira con la cartella firmware/ come cwd.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/menu.h"
#include "core/netfmt.h"
#include "core/retry.h"
#include "core/strbuf.h"
#include "core/ui.h"
#include "gfx/canvas.h"
#include "gfx/qr.h"
#include "ui/screens.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const int64_t kT = 1791707400000LL;  // state.time degli esempi
const Ms kNow = 5000000;             // tempo monotono della scheda quando arriva lo state

std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool utf8Valid(const char *s) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
  while (*p) {
    int n = *p < 0x80 ? 0 : (*p >> 5) == 6 ? 1 : (*p >> 4) == 14 ? 2 : (*p >> 3) == 30 ? 3 : -1;
    if (n < 0) return false;
    p++;
    for (int k = 0; k < n; k++, p++)
      if ((*p >> 6) != 2) return false;
  }
  return true;
}

JsonDocument doc;
StateMsg msg;

bool decode(const std::string &json, StateMsg &out) {
  doc.clear();
  if (!parseMessage(json.data(), json.size(), doc)) return false;
  return decodeState(doc.as<JsonVariantConst>(), out);
}

}  // namespace

// ---------------------------------------------------------------- nomi e QR

static void test_board_names() {
  const uint8_t mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0x48, 0xe8};  // MAC di esempio
  char id[8], ssid[24], host[24];
  boardIdFromMac(mac, id, sizeof id);
  TEST_ASSERT_EQUAL_STRING("48e8", id);
  apSsid(id, ssid, sizeof ssid);
  TEST_ASSERT_EQUAL_STRING("Calamaio-48E8", ssid);
  mdnsHost(id, host, sizeof host);
  TEST_ASSERT_EQUAL_STRING("calamaio-48e8", host);
}

static void test_wifi_qr_text() {
  char t[96];
  wifiQrText("Calamaio-48E8", "K7MZ3QRT", t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Calamaio-48E8;P:K7MZ3QRT;;", t);
  wifiQrText("a;b,c:d\\e\"f", "p;w", t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:a\\;b\\,c\\:d\\\\e\\\"f;P:p\\;w;;", t);
}

static uint32_t seqRand(void *ctx) {
  uint32_t *s = static_cast<uint32_t *>(ctx);
  *s = *s * 1664525u + 1013904223u;
  return *s;
}
static uint32_t maxRand(void *ctx) {
  // il primo valore va scartato (oltre il limite), poi 0, 1, 2…
  uint32_t *s = static_cast<uint32_t *>(ctx);
  return (*s)++ == 0 ? 0xFFFFFFFFu : *s - 2;
}

static void test_ap_password() {
  TEST_ASSERT_EQUAL(31, (int)strlen(kApAlphabet));
  for (const char *bad = "01ILO"; *bad; bad++) TEST_ASSERT_NULL(strchr(kApAlphabet, *bad));
  uint32_t seed = 7;
  for (int run = 0; run < 200; run++) {
    char pw[kApPassLen];
    makeApPassword(pw, sizeof pw, seqRand, &seed);
    TEST_ASSERT_EQUAL(kApPassChars, (int)strlen(pw));
    for (const char *p = pw; *p; p++) TEST_ASSERT_NOT_NULL(strchr(kApAlphabet, *p));
  }
  uint32_t c = 0;
  char pw[kApPassLen];
  makeApPassword(pw, sizeof pw, maxRand, &c);
  TEST_ASSERT_EQUAL_STRING("ABCDEFGH", pw);  // 0xFFFFFFFF scartato: niente sbilanciamento
}

// Bug visto sulla scheda: dopo la caduta del servizio ESPmDNS risponde «0.0.0.0», porta 0, host vuoto,
// e il display provava a collegarsi lì. Una risposta così vale «nessun PC trovato».
static void test_mdns_answer_valid() {
  TEST_ASSERT_TRUE(mdnsAnswerValid("192.168.1.20", 8766));
  TEST_ASSERT_TRUE(mdnsAnswerValid("10.0.0.1", 1));
  TEST_ASSERT_FALSE(mdnsAnswerValid("0.0.0.0", 0));      // la risposta vuota vista nel log
  TEST_ASSERT_FALSE(mdnsAnswerValid("0.0.0.0", 8766));   // indirizzo vuoto, porta presente
  TEST_ASSERT_FALSE(mdnsAnswerValid("192.168.1.20", 0)); // porta vuota
  TEST_ASSERT_FALSE(mdnsAnswerValid("", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid(nullptr, 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("255.255.255.255", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("256.1.1.1", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("1.2.3", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("1.2.3.4.5", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("1.2.3.4 ", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("1..3.4", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("1.2.3.0004", 8766));
  TEST_ASSERT_FALSE(mdnsAnswerValid("fe80::1", 8766));    // solo IPv4 (ESPmDNS.IP())
}

static void test_qr_wifi_is_version3() {
  static qr::Bits q;
  TEST_ASSERT_TRUE(qr::encode("WIFI:T:WPA;S:Calamaio-48E8;P:K7MZ3QRT;;", q));
  TEST_ASSERT_EQUAL(3, q.version);
  TEST_ASSERT_EQUAL(29, q.size);
  // Prova di presenza: i tre riquadri di posizione 7×7 negli angoli giusti, il quarto angolo no.
  const int corners[3][2] = {{0, 0}, {q.size - 7, 0}, {0, q.size - 7}};
  for (auto &c : corners)
    for (int y = 0; y < 7; y++)
      for (int x = 0; x < 7; x++) {
        const bool ring = x == 0 || y == 0 || x == 6 || y == 6, core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
        TEST_ASSERT_EQUAL(ring || core, q.get(c[0] + x, c[1] + y));
      }
  TEST_ASSERT_FALSE(q.get(q.size - 1, q.size - 1) && q.get(q.size - 7, q.size - 7) && q.get(q.size - 1, q.size - 7));
  // Moduli su disco (PBM, 4 di margine) per decodificarli con un lettore vero fuori dal test.
  FILE *f = fopen(".pio/qr-wifi-48e8.pbm", "w");
  if (f) {
    const int m = 4, n = q.size + 2 * m, s = 8;
    fprintf(f, "P1\n%d %d\n", n * s, n * s);
    for (int y = 0; y < n * s; y++) {
      for (int x = 0; x < n * s; x++) fputs(q.get(x / s - m, y / s - m) ? "1 " : "0 ", f);
      fputc('\n', f);
    }
    fclose(f);
  }
  std::string big(qr::byteCapacityM(qr::kMaxVersion) + 1, 'x');
  TEST_ASSERT_FALSE(qr::encode(big.c_str(), q));
  TEST_ASSERT_TRUE(qr::encode("ciao", q));
  TEST_ASSERT_EQUAL(1, q.version);
}

// ---------------------------------------------------------------- protocollo → modello

static void test_example_state() {
  const std::string js = readFile("../protocol/examples/state.json");
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 100, "protocol/examples/state.json non trovato");
  TEST_ASSERT_TRUE(decode(js, msg));
  TEST_ASSERT_EQUAL(4, msg.nSessions);
  static Model m;
  newModel(m, kNow, WallClock{kNow});
  clearForLive(m);
  logEv(m, 0, "Wi-Fi connesso a CasaWiFi", kNow - 30000);  // evento locale: deve restare
  applyState(m, msg, kNow);

  TEST_ASSERT_EQUAL(4, m.nSessions);
  const Session &a = m.sessions[0];
  TEST_ASSERT_EQUAL_STRING("a1", a.id);
  TEST_ASSERT_EQUAL_STRING("api-server", a.name);
  TEST_ASSERT_EQUAL(SessionState::Working, a.state);
  TEST_ASSERT_EQUAL_STRING("Edit src/routes/orders.ts", a.detail);
  TEST_ASSERT_EQUAL_INT64(kNow - 12 * 60000, a.since);  // 1791706680000 = 12 min prima di time
  TEST_ASSERT_FALSE(a.hasAttnAt);
  TEST_ASSERT_EQUAL(46, a.ctx);
  TEST_ASSERT_EQUAL(2, a.subs);
  TEST_ASSERT_EQUAL(TestResult::None, a.test);
  TEST_ASSERT_EQUAL(7, a.nTasks);
  TEST_ASSERT_EQUAL(7, a.total);
  TEST_ASSERT_EQUAL(3, a.done);
  TEST_ASSERT_EQUAL(TaskStatus::Doing, a.tasks[3].status);
  const Session &b = m.sessions[1];
  TEST_ASSERT_EQUAL(SessionState::Permission, b.state);
  TEST_ASSERT_TRUE(b.hasAttnAt);
  TEST_ASSERT_EQUAL_INT64(kNow - 60000, b.attnAt);
  TEST_ASSERT_EQUAL(TestResult::Ko, b.test);
  TEST_ASSERT_EQUAL(5, b.done);
  TEST_ASSERT_EQUAL(9, b.total);
  TEST_ASSERT_EQUAL_STRING("Layout 400×300", m.sessions[2].tasks[1].name);
  TEST_ASSERT_EQUAL_STRING("Guida di installazione aggiornata e 4 link rotti corretti.", m.sessions[3].summary);
  TEST_ASSERT_EQUAL(SessionState::Done, m.sessions[3].state);

  TEST_ASSERT_TRUE(m.limits.hasH5);
  TEST_ASSERT_TRUE(m.limits.hasD7);
  TEST_ASSERT_TRUE((m.limits.h5) == (62));
  TEST_ASSERT_EQUAL_INT64(kNow + (1791715200000LL - kT), m.limits.h5Reset);
  TEST_ASSERT_TRUE((m.limits.d7) == (41));

  TEST_ASSERT_EQUAL(14, m.stats.tasks);
  TEST_ASSERT_EQUAL(2, m.stats.permDisplay);
  TEST_ASSERT_EQUAL(3, m.stats.permTerminal);
  TEST_ASSERT_EQUAL_INT64(132000, m.stats.waitSum / m.stats.waitN);
  TEST_ASSERT_EQUAL(1240, m.stats.linesAdd);
  TEST_ASSERT_EQUAL(380, m.stats.linesDel);
  TEST_ASSERT_TRUE(m.stats.cost > 18.4 - 1e-6 && m.stats.cost < 18.4 + 1e-6);  // ArduinoJson non arrotonda al double più vicino

  // Log: 2 del PC + 1 locale, dal più recente.
  TEST_ASSERT_EQUAL(3, m.log.n);
  TEST_ASSERT_EQUAL_STRING("sistema", m.log.at(0).name);  // kNow − 30 s
  TEST_ASSERT_TRUE(m.log.at(0).local);
  TEST_ASSERT_EQUAL_STRING("chiede permesso: npm run test", m.log.at(1).text);
  TEST_ASSERT_EQUAL_INT64(kNow - 60000, m.log.at(1).t);
  TEST_ASSERT_EQUAL_STRING("task 3/7 completato", m.log.at(2).text);

  TEST_ASSERT_EQUAL(3, m.nServices);
  TEST_ASSERT_EQUAL_STRING("Backup notturno", m.services[2].name);
  TEST_ASSERT_FALSE(m.services[2].ok);
  TEST_ASSERT_EQUAL(85, m.services[1].ms);
  TEST_ASSERT_EQUAL(2, m.nPrs);
  TEST_ASSERT_EQUAL(PrChecks::Ko, m.prs[0].checks);
  TEST_ASSERT_EQUAL(PrChecks::Run, m.prs[1].checks);
  TEST_ASSERT_EQUAL(17, m.prs[1].n);
  TEST_ASSERT_EQUAL(1, m.nAgenda);
  TEST_ASSERT_EQUAL_INT64(kNow + 50 * 60000, m.agenda[0].t);
  TEST_ASSERT_EQUAL_STRING("Riunione di progetto", m.agenda[0].title);

  TEST_ASSERT_TRUE(m.sys.pc);
  TEST_ASSERT_EQUAL_INT64(kNow, m.sys.lastPc);

  // Un secondo state sostituisce il log del PC ma tiene l'evento locale.
  applyState(m, msg, kNow + 1000);
  TEST_ASSERT_EQUAL(3, m.log.n);
}

static void test_example_state_empty() {
  const std::string js = readFile("../protocol/examples/state-empty.json");
  TEST_ASSERT_TRUE(js.size() > 20);
  TEST_ASSERT_TRUE(decode(js, msg));
  static Model m;
  newModel(m, kNow, WallClock{kNow});
  applyState(m, msg, kNow);  // anche sopra il modello finto: tutto sostituito
  TEST_ASSERT_EQUAL(0, m.nSessions);
  TEST_ASSERT_FALSE(m.limits.hasH5);
  TEST_ASSERT_FALSE(m.limits.hasD7);
  TEST_ASSERT_EQUAL(0, m.nServices);
  TEST_ASSERT_EQUAL(0, m.nPrs);
  TEST_ASSERT_EQUAL(0, m.nAgenda);
  TEST_ASSERT_EQUAL(0, m.stats.waitN);
  TEST_ASSERT_EQUAL_STRING("", m.sessions[0].name);  // le sessioni vecchie sono azzerate
}

// Fusione del registro della 0.14.0 (buffer da 80 voci), copiata alla lettera: riferimento per la 0.14.1 sul posto.
static void refMergeLog(Log &L0, const StateMsg &msg, Ms now) {
  auto mono = [&](int64_t t) -> Ms { return t ? now + (t - msg.time) : 0; };
  static LogEntry merged[kLogCap];
  int n = 0, li = 0, pi = 0;
  const Log &L = L0;
  auto nextLocal = [&]() -> const LogEntry * {
    while (li < L.n && !L.at(li).local) li++;
    return li < L.n ? &L.at(li) : nullptr;
  };
  while (n < kLogCap) {
    const LogEntry *a = nextLocal();
    const LogEntry *b = pi < msg.nLog ? &msg.log[pi] : nullptr;
    if (!a && !b) break;
    Ms bt = b ? mono(b->t) : 0;
    if (a && (!b || a->t >= bt)) { merged[n++] = *a; li++; }
    else { merged[n] = *b; merged[n].t = bt; n++; pi++; }
  }
  L0.head = 0;
  L0.n = n;
  for (int k = 0; k < n; k++) L0.e[k] = merged[k];
}

static void assertSameLog(const Log &want, const Log &got, int caseNo) {
  char why[48];
  snprintf(why, sizeof why, "caso %d", caseNo);
  TEST_ASSERT_EQUAL_MESSAGE(want.n, got.n, why);
  TEST_ASSERT_EQUAL_MESSAGE(want.head, got.head, why);
  for (int i = 0; i < want.n; i++) {
    TEST_ASSERT_EQUAL_INT64_MESSAGE(want.at(i).t, got.at(i).t, why);
    TEST_ASSERT_EQUAL_MESSAGE(want.at(i).local, got.at(i).local, why);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want.at(i).name, got.at(i).name, why);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want.at(i).text, got.at(i).text, why);
  }
}

static uint32_t gRnd = 0x9E3779B9u;
static int rnd(int n) {  // xorshift32, seme fisso: [0, n)
  gRnd ^= gRnd << 13;
  gRnd ^= gRnd >> 17;
  gRnd ^= gRnd << 5;
  return (int)(gRnd % (uint32_t)n);
}

static void test_log_merge_in_place_matches_old() {
  static Model a;
  static Log want;
  static StateMsg s;
  // Voce i dell'anello (fisica (head + i) % kLogCap) e voce j del PC con tempo monotono dato.
  auto setLocal = [](int i, Ms t, bool local, int caseNo) {
    LogEntry &e = a.log.e[(a.log.head + i) % kLogCap];
    e.t = t;
    e.local = local;
    snprintf(e.name, sizeof e.name, "L%d-%d", caseNo, i);
    snprintf(e.text, sizeof e.text, "testo L%d-%d", caseNo, i);
  };
  auto setPc = [](int j, Ms monoT, int caseNo) {  // monoT = 0: tempo assente
    LogEntry &e = s.log[j];
    e.t = monoT ? monoT - kNow + s.time : 0;
    e.local = false;
    snprintf(e.name, sizeof e.name, "P%d-%d", caseNo, j);
    snprintf(e.text, sizeof e.text, "testo P%d-%d", caseNo, j);
  };
  auto randT = []() -> Ms { return rnd(8) == 0 ? 0 : kNow - 1500 + rnd(501); };
  auto check = [](int caseNo) {
    want = a.log;
    refMergeLog(want, s, kNow);
    applyState(a, s, kNow);
    assertSameLog(want, a.log, caseNo);
  };
  auto reset = [](int head, int n, Ms stime, int nLog) {
    memset(&a.log, 0, sizeof a.log);
    a.log.head = head;
    a.log.n = n;
    memset(&s, 0, sizeof s);
    s.time = stime;
    s.nLog = nLog;
  };

  // Casi casuali: anello a metà o pieno, locali e non, pari merito, tempi assenti, log del PC non ordinato.
  for (int c = 0; c < 5000; c++) {
    reset(rnd(kLogCap), rnd(kLogCap + 1), kNow - rnd(5000), rnd(kStateLogCap + 1));
    for (int i = 0; i < a.log.n; i++) setLocal(i, randT(), rnd(3) != 0, c);
    for (int j = 0; j < s.nLog; j++) setPc(j, randT(), c);
    check(c);
  }

  int c = 10000;
  // Anello vuoto e log del PC vuoto.
  reset(37, 0, kNow, 0);
  check(c++);
  // 80 locali tutti più nuovi del PC (anello che gira).
  reset(50, kLogCap, kNow - 1234, kStateLogCap);
  for (int i = 0; i < kLogCap; i++) setLocal(i, kNow - i, true, c);
  for (int j = 0; j < kStateLogCap; j++) setPc(j, kNow - 1000 - j, c);
  check(c++);
  // 80 locali tutti più vecchi del PC.
  reset(13, kLogCap, kNow - 77, kStateLogCap);
  for (int i = 0; i < kLogCap; i++) setLocal(i, kNow - 1000 - i, true, c);
  for (int j = 0; j < kStateLogCap; j++) setPc(j, kNow - j, c);
  check(c++);
  // 80 locali con head = 79 e 20 del PC più nuovi: si perdono 20 locali in coda.
  reset(79, kLogCap, kNow - 5, kStateLogCap);
  for (int i = 0; i < kLogCap; i++) setLocal(i, kNow - 100 - i, true, c);
  for (int j = 0; j < kStateLogCap; j++) setPc(j, kNow - j, c);
  check(c);
  TEST_ASSERT_EQUAL(kLogCap, a.log.n);
  TEST_ASSERT_EQUAL_STRING("P10003-0", a.log.at(0).name);
  TEST_ASSERT_EQUAL_STRING("L10003-59", a.log.at(kLogCap - 1).name);
  c++;
  // Locali e PC con tempi tutti uguali: vince il locale.
  reset(70, 30, kNow - 3000, kStateLogCap);
  for (int i = 0; i < 30; i++) setLocal(i, kNow - 500, true, c);
  for (int j = 0; j < kStateLogCap; j++) setPc(j, kNow - 500, c);
  check(c++);
  // Solo non locali: spariscono tutti.
  reset(60, 40, kNow, 0);
  for (int i = 0; i < 40; i++) setLocal(i, kNow - i, false, c);
  check(c);
  TEST_ASSERT_EQUAL(0, a.log.n);
  c++;
  // Log del PC non ordinato, locali e non mescolati a metà anello.
  reset(75, 12, kNow - 999, kStateLogCap);
  for (int i = 0; i < 12; i++) setLocal(i, kNow - 100 * i, i % 3 != 1, c);
  for (int j = 0; j < kStateLogCap; j++) setPc(j, kNow - ((j * 7) % 13) * 90, c);
  check(c++);
}

static void test_welcome_and_types() {
  const std::string js = readFile("../protocol/examples/welcome.json");
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_EQUAL(MsgType::Welcome, msgType(doc.as<JsonVariantConst>()));
  WelcomeMsg w;
  TEST_ASSERT_TRUE(decodeWelcome(doc.as<JsonVariantConst>(), w));
  TEST_ASSERT_EQUAL_INT64(kT, w.time);
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", w.tz);
  TEST_ASSERT_EQUAL_STRING("mac-sviluppo", w.host);
  TEST_ASSERT_EQUAL_STRING("0.1.0", w.service);
  // hello di esempio: è un messaggio del display, il display non lo riconosce come in arrivo
  const std::string h = readFile("../protocol/examples/hello.json");
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(h.data(), h.size(), doc));
  TEST_ASSERT_EQUAL(MsgType::Unknown, msgType(doc.as<JsonVariantConst>()));
  // fw non passa dal filtro (il display non lo legge), si cerca nel testo. L'esempio lo aggiorna il servizio
  // (era 0.3.0): qui si controlla solo che ci sia; la versione del firmware è 0.5.0 (M5).
  TEST_ASSERT_NOT_NULL(strstr(h.c_str(), "\"fw\":\""));
  TEST_ASSERT_EQUAL_STRING("0.14.1", kFwVersion);  // 0.14.1: memoria, pacchetto rapido
}

static void test_malformed_ignored() {
  const char *bad[] = {
      "", "{", "[1,2]", "null", "{\"t\":\"state\"}",                       // niente v
      "{\"t\":\"state\",\"v\":2,\"time\":1}",                               // versione diversa
      "{\"t\":\"state\",\"v\":1}",                                          // niente time
      "{\"t\":\"state\",\"v\":1,\"time\":\"ieri\"}",                        // time non numerico
      "{\"t\":\"boh\",\"v\":1,\"time\":1}",                                 // tipo sconosciuto
      "{\"t\":\"state\",\"v\":1,\"time\":1,\"sessions\":[{\"name\":\"x\"}", // troncato
  };
  for (const char *b : bad) {
    StateMsg tmp;
    TEST_ASSERT_FALSE_MESSAGE(decode(b, tmp), b);
  }
  // Troppo lungo: scartato prima di leggerlo.
  std::string huge = "{\"t\":\"state\",\"v\":1,\"time\":1,\"x\":\"" + std::string(kMaxFrame, 'a') + "\"}";
  doc.clear();
  TEST_ASSERT_FALSE(parseMessage(huge.data(), huge.size(), doc));
  // Annidamento oltre il limite.
  std::string deep = "{\"t\":\"state\",\"v\":1,\"time\":1,\"sessions\":[{\"tasks\":[[[[[[[[1]]]]]]]]}]}";
  StateMsg tmp;
  TEST_ASSERT_FALSE(decode(deep, tmp));
}

static void test_missing_and_null_fields() {
  TEST_ASSERT_TRUE(decode("{\"t\":\"state\",\"v\":1,\"time\":1000}", msg));
  TEST_ASSERT_EQUAL(0, msg.nSessions);
  TEST_ASSERT_FALSE(msg.limits.hasH5);
  TEST_ASSERT_EQUAL(0, msg.today.tasks);
  const char *js =
      "{\"t\":\"state\",\"v\":1,\"time\":1000,\"sessions\":[{\"id\":null,\"name\":null,\"state\":\"riflette\","
      "\"detail\":null,\"since\":null,\"attnAt\":null,\"ctx\":null,\"subs\":null,\"test\":\"run\",\"summary\":null,"
      "\"tasks\":null},{\"state\":\"error\",\"error\":\"rate_limit\",\"since\":400}],"
      "\"limits\":{\"h5\":95.5},\"today\":null,\"log\":null,\"services\":[{\"name\":\"API\"}],\"prs\":[{}],"
      "\"agenda\":[{\"title\":\"senza ora\"},{\"t\":61000,\"title\":null}]}";
  TEST_ASSERT_TRUE(decode(js, msg));
  TEST_ASSERT_EQUAL(2, msg.nSessions);
  const Session &s = msg.sessions[0];
  TEST_ASSERT_EQUAL(SessionState::Idle, s.state);  // stato sconosciuto
  TEST_ASSERT_EQUAL_STRING("", s.name);
  TEST_ASSERT_FALSE(s.hasAttnAt);
  TEST_ASSERT_EQUAL(0, s.ctx);
  TEST_ASSERT_EQUAL(TestResult::None, s.test);  // 'run' non si mostra
  TEST_ASSERT_EQUAL(0, s.nTasks);
  TEST_ASSERT_EQUAL_STRING("StopFailure: rate_limit", msg.sessions[1].detail);
  TEST_ASSERT_TRUE(msg.limits.hasH5);
  TEST_ASSERT_FALSE(msg.limits.hasD7);
  TEST_ASSERT_EQUAL(1, msg.nServices);
  TEST_ASSERT_FALSE(msg.services[0].ok);
  TEST_ASSERT_EQUAL(PrChecks::Run, msg.prs[0].checks);
  TEST_ASSERT_EQUAL(1, msg.nAgenda);  // quella senza ora si scarta
  static Model m;
  newModel(m, 0, WallClock{0});
  clearForLive(m);
  applyState(m, msg, 100000);
  TEST_ASSERT_EQUAL_INT64(100000 + (400 - 1000), m.sessions[1].since);
  TEST_ASSERT_EQUAL_INT64(100000 + 60000, m.agenda[0].t);
  TEST_ASSERT_EQUAL_INT64(0, m.sessions[0].since);  // since assente resta 0
  TEST_ASSERT_EQUAL(MascotPose::Tired, pose(m, m.sessions[0]));  // 5h al 95%: Goccia stanca
}

static void test_long_utf8_texts() {
  std::string name, detail, summary, task;
  for (int k = 0; k < 40; k++) name += "è";        // 80 byte
  for (int k = 0; k < 60; k++) detail += "·";      // 120 byte (2 byte ciascuno)
  for (int k = 0; k < 50; k++) summary += "✓";     // 150 byte (3 byte ciascuno)
  for (int k = 0; k < 30; k++) task += "😀";       // 120 byte (4 byte ciascuno)
  std::string js = "{\"t\":\"state\",\"v\":1,\"time\":1,\"sessions\":[{\"name\":\"" + name + "\",\"detail\":\"" + detail +
                   "\",\"summary\":\"riga 1\\nriga 2\\t" + summary + "\",\"tasks\":[[\"" + task + "\",1]]}]}";
  TEST_ASSERT_TRUE(decode(js, msg));
  const Session &s = msg.sessions[0];
  TEST_ASSERT_TRUE(utf8Valid(s.name));
  TEST_ASSERT_EQUAL(30, (int)strlen(s.name));  // 31 byte disponibili: 15 «è» interi
  TEST_ASSERT_TRUE(utf8Valid(s.detail));
  TEST_ASSERT_EQUAL(94, (int)strlen(s.detail));
  TEST_ASSERT_TRUE(utf8Valid(s.summary));
  TEST_ASSERT_TRUE(strlen(s.summary) <= 127);
  TEST_ASSERT_TRUE(startsWith(s.summary, "riga 1 riga 2 "));  // a capo e tab diventano spazi
  TEST_ASSERT_TRUE(utf8Valid(s.tasks[0].name));
  TEST_ASSERT_EQUAL(44, (int)strlen(s.tasks[0].name));  // 47 byte disponibili: 11 emoji
}

static void test_more_than_six_sessions_and_twelve_tasks() {
  std::string js = "{\"t\":\"state\",\"v\":1,\"time\":1,\"sessions\":[";
  for (int k = 0; k < 8; k++) {
    if (k) js += ",";
    js += "{\"id\":\"s" + std::to_string(k) + "\",\"name\":\"n" + std::to_string(k) + "\",\"state\":\"working\",\"tasks\":[";
    const int nt = k == 0 ? 20 : 3;
    for (int t = 0; t < nt; t++) {
      if (t) js += ",";
      // sessione 0: 15 fatti, poi 1 in corso e 4 da fare
      const int st = k == 0 ? (t < 15 ? 2 : t == 15 ? 1 : 0) : 0;
      js += "[\"t" + std::to_string(t) + "\"," + std::to_string(st) + "]";
    }
    js += "]}";
  }
  js += "]}";
  TEST_ASSERT_TRUE(js.size() < kMaxFrame);
  TEST_ASSERT_TRUE(decode(js, msg));
  TEST_ASSERT_EQUAL(kMaxSessions, msg.nSessions);
  TEST_ASSERT_EQUAL(2, msg.sessionsDropped);
  TEST_ASSERT_EQUAL_STRING("n5", msg.sessions[5].name);
  const Session &s = msg.sessions[0];
  TEST_ASSERT_EQUAL(20, s.total);  // conteggi su tutto l'elenco
  TEST_ASSERT_EQUAL(15, s.done);
  TEST_ASSERT_EQUAL(kMaxTasks, s.nTasks);
  TEST_ASSERT_EQUAL_STRING("t8", s.tasks[0].name);  // finestra: dall'ultimo fatto prima del primo aperto
  TEST_ASSERT_EQUAL(TaskStatus::Doing, s.tasks[7].status);
  TEST_ASSERT_EQUAL_STRING("t19", s.tasks[11].name);
  static Model m;
  newModel(m, 0, WallClock{0});
  applyState(m, msg, 0);
  TEST_ASSERT_EQUAL(20, m.sessions[0].total);  // nessuno li ricalcola sui soli 12 tenuti
}

static void test_log_capped() {
  std::string js = "{\"t\":\"state\",\"v\":1,\"time\":100000,\"log\":[";
  for (int k = 0; k < 30; k++) js += std::string(k ? "," : "") + "{\"t\":" + std::to_string(100000 - k * 1000) + ",\"name\":\"x\",\"text\":\"e" + std::to_string(k) + "\"}";
  js += "]}";
  TEST_ASSERT_TRUE(decode(js, msg));
  TEST_ASSERT_EQUAL(kStateLogCap, msg.nLog);
}

// ---------------------------------------------------------------- interfaccia con la rete vera

namespace {

struct LiveBench {
  Ui ui;
  Ms now;
  NetRequest reqs[16];
  int nReqs;
  LiveBench() : ui(kHeltec213), now(1000), nReqs(0) {
    ui.setNetListener(&LiveBench::onNet, this);
    ui.begin(now, wc());
    ui.goLive();
    now += 2601;
    ui.tick(now, wc());
  }
  WallClock wc() const { return WallClock{kNow + now}; }
  static void onNet(const NetRequest &r, void *ctx) {
    LiveBench *b = static_cast<LiveBench *>(ctx);
    if (b->nReqs < 16) b->reqs[b->nReqs++] = r;
  }
  RefreshKind act(Action a) { now += 500; return ui.act(a, now, wc()); }
  RefreshKind next(int n) { RefreshKind r = RefreshKind::None; for (int k = 0; k < n; k++) r = act(Action::Next); return r; }
  ScreenId topId() const { return top(ui.model()).id; }
  const NetRequest *last() const { return nReqs ? &reqs[nReqs - 1] : nullptr; }
  void openWifiMenu() {
    act(Action::Menu);
    next(3);
    act(Action::Ok);
    TEST_ASSERT_EQUAL(MenuId::Wifi, top(ui.model()).menu);
  }
};

}  // namespace

static void test_live_starts_empty_and_offline() {
  static LiveBench b;
  const Model &m = b.ui.model();
  TEST_ASSERT_TRUE(m.sys.live);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL(0, m.nSessions);
  TEST_ASSERT_FALSE(m.sys.pc);
  TEST_ASSERT_FALSE(m.sys.wifi);
  TEST_ASSERT_EQUAL(0, m.log.n);
  // Lo stato finto non parte: step() non tocca niente.
  static Rng rng(1);
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.step(b.now, b.wc(), rng));
  TEST_ASSERT_EQUAL(0, m.nSessions);
  b.act(Action::Next);
  TEST_ASSERT_EQUAL_STRING("PC non raggiungibile: apri il Menu", b.ui.lastAction());
}

static void test_live_state_and_pc_lost() {
  static LiveBench b;
  TEST_ASSERT_TRUE(decode(readFile("../protocol/examples/state.json"), msg));
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.ui.netState(msg, b.now));  // offline → Home con le sessioni
  const Model &m = b.ui.model();
  TEST_ASSERT_TRUE(m.sys.pc);
  TEST_ASSERT_EQUAL(4, m.nSessions);
  // Stesso state: niente ridisegno.
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.netState(msg, b.now + 250));
  // Dettaglio di web-app (indice 1), poi le sessioni cambiano ordine: la selezione segue l'id.
  b.act(Action::Next);  // selezione: dalla più urgente (web-app) alla successiva
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  const int sel = m.ui.sel;
  char id[kIdLen];
  scopy(id, m.sessions[sel].id);
  static StateMsg swapped;
  swapped = msg;
  swapped.sessions[0] = msg.sessions[3];
  swapped.sessions[3] = msg.sessions[0];
  b.ui.netState(swapped, b.now);
  TEST_ASSERT_EQUAL_STRING(id, m.sessions[m.ui.sel].id);
  // Permesso senza abbinamento (M3): niente schermata Permesso, si risponde nel terminale.
  for (int k = 0; k < 4 && m.sessions[m.ui.sel].state != SessionState::Permission; k++) b.act(Action::Next);
  TEST_ASSERT_EQUAL(SessionState::Permission, m.sessions[m.ui.sel].state);
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
  TEST_ASSERT_EQUAL_STRING("Rispondi nel terminale", m.toast.msg);
  // Il PC tace: offline con l'ora dell'ultimo dato.
  const Ms last = m.sys.lastPc;
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.ui.netPcLost(b.now + 45000));
  TEST_ASSERT_FALSE(m.sys.pc);
  TEST_ASSERT_EQUAL_INT64(last, m.sys.lastPc);
  // Le sessioni spariscono mentre si guarda il Dettaglio: si torna alla Home vuota.
  TEST_ASSERT_TRUE(decode(readFile("../protocol/examples/state-empty.json"), msg));
  b.ui.netState(msg, b.now + 50000);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL_STRING("Nessuna sessione", b.ui.lastAction());
  // Senza limiti: niente azzeramento finto allo scadere.
  b.now += 6 * 3600000LL;
  b.ui.tick(b.now, b.wc());
  TEST_ASSERT_FALSE(m.limits.hasH5);
  TEST_ASSERT_TRUE((m.limits.h5) == (0));
}

static void test_live_forget_needs_confirmation() {
  static LiveBench b;
  b.ui.netConnected("CasaWiFi", -61, "192.168.1.50", b.now);
  b.openWifiMenu();
  b.next(8);
  static MenuList L;
  TEST_ASSERT_EQUAL_STRING("Dimentica tutte le reti", b.ui.items(b.now, b.wc()).items[8].label);
  const int before = b.nReqs;
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(before, b.nReqs);  // prima doppia pressione: solo armata
  TEST_ASSERT_TRUE(b.ui.model().sys.wifi);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[8].val);
  TEST_ASSERT_EQUAL_STRING("2 pressioni ancora per confermare", b.ui.model().toast.msg);
  b.now += kForgetMs + 1;  // scade: si riparte da capo
  b.ui.tick(b.now, b.wc());
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(before, b.nReqs);
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(before + 1, b.nReqs);
  TEST_ASSERT_EQUAL(NetCmd::Forget, b.last()->cmd);
  TEST_ASSERT_FALSE(b.ui.model().sys.wifi);
  TEST_ASSERT_EQUAL_STRING("Reti dimenticate", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(0, b.ui.model().nSaved);
  (void)L;
}

// 0.8.0: Menu › Wi-Fi › Reti salvate con i dati del vault (netSaved), Dimentica di una rete con doppia conferma,
// wifi-forget dal PC (netForgotten).
static void test_live_saved_networks() {
  static LiveBench b;
  const Model &m = b.ui.model();
  TEST_ASSERT_EQUAL(0, m.nSaved);  // goLive: niente reti finte
  b.ui.netConnected("Officina", -71, "192.168.1.50", b.now);
  SavedNet list[3] = {};
  strcpy(list[0].ssid, "CasaWiFi");
  list[0].usedLocal = kNow - 5 * 86400000LL;  // 5 giorni fa
  list[0].visible = true;
  list[0].rssi = -83;
  strcpy(list[1].ssid, "Officina");
  list[1].usedLocal = kNow;
  strcpy(list[2].ssid, "Nascosta");
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.netSaved(list, 3, b.now));  // la lista non è a schermo
  TEST_ASSERT_EQUAL(3, m.nSaved);
  b.openWifiMenu();
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.netSaved(list, 3, b.now));  // uguale: niente ridisegno
  const MenuList &W = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("Reti salvate", W.items[3].label);
  TEST_ASSERT_EQUAL_STRING("3 di 5", W.items[3].val);
  b.next(3);
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(MenuId::Saved, top(m).menu);
  const MenuList &S = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(3, S.n);
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", S.items[0].label);
  TEST_ASSERT_EQUAL_STRING("", S.items[0].val);
  TEST_ASSERT_TRUE(S.items[0].hasBars);
  TEST_ASSERT_EQUAL(1, S.items[0].bars);           // −83 dBm
  TEST_ASSERT_EQUAL_STRING("in uso", S.items[1].val);
  TEST_ASSERT_EQUAL(2, S.items[1].bars);           // la rete in uso ha il segnale vero (−71)
  TEST_ASSERT_FALSE(S.items[2].hasBars);           // non si vede
  TEST_ASSERT_TRUE(S.items[2].chevron());
  b.act(Action::Ok);  // CasaWiFi
  TEST_ASSERT_EQUAL(MenuId::SavedNet, top(m).menu);
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", top(m).ssid);
  const MenuList &N = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", N.title);
  TEST_ASSERT_EQUAL_STRING("-83 dBm", N.items[0].val);
  char d[8];
  const Civil c = civilFrom(kNow - 5 * 86400000LL);
  snprintf(d, sizeof d, "%02d/%02d", c.day, c.month);
  TEST_ASSERT_EQUAL_STRING(d, N.items[1].val);
  TEST_ASSERT_EQUAL_STRING("Dimentica", N.items[2].label);
  b.next(2);
  const int before = b.nReqs;
  b.act(Action::Ok);  // prima doppia pressione: solo armata
  TEST_ASSERT_EQUAL(before, b.nReqs);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[2].val);
  TEST_ASSERT_EQUAL_STRING("2 pressioni ancora per confermare", m.toast.msg);
  b.now += kForgetMs + 1;  // scade
  b.ui.tick(b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("", b.ui.items(b.now, b.wc()).items[2].val);
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(before, b.nReqs);
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.act(Action::Ok));
  TEST_ASSERT_EQUAL(before + 1, b.nReqs);
  TEST_ASSERT_EQUAL(NetCmd::ForgetNet, b.last()->cmd);
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", b.last()->ssid);
  TEST_ASSERT_EQUAL(MenuId::Saved, top(m).menu);  // di nuovo nella lista
  TEST_ASSERT_EQUAL(2, m.nSaved);
  TEST_ASSERT_EQUAL_STRING("Officina", m.saved[0].ssid);
  TEST_ASSERT_EQUAL_STRING("Rete dimenticata", m.toast.msg);
  TEST_ASSERT_TRUE(m.sys.wifi);  // non era quella in uso
  // wifi-forget dal PC della rete in uso: via dalla lista, Wi-Fi giù, toast.
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.ui.netForgotten("Officina", b.now));
  TEST_ASSERT_EQUAL(1, m.nSaved);
  TEST_ASSERT_FALSE(m.sys.wifi);
  TEST_ASSERT_EQUAL_STRING("Rete dimenticata", m.toast.msg);
  // Lista vuota: una voce che lo dice.
  b.ui.netSaved(list, 0, b.now);
  const MenuList &E = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(1, E.n);
  TEST_ASSERT_EQUAL_STRING("Nessuna rete", E.items[0].label);
  TEST_ASSERT_EQUAL(ItemKind::Info, E.items[0].kind);
}

static void test_live_provisioning_flow() {
  static LiveBench b;
  b.openWifiMenu();
  b.next(5);
  b.act(Action::Ok);  // Configura da telefono
  TEST_ASSERT_EQUAL(ScreenId::Qr, b.topId());
  TEST_ASSERT_EQUAL(NetCmd::Provision, b.last()->cmd);
  TEST_ASSERT_TRUE(b.ui.provisioning());
  b.act(Action::Ok);  // niente simulazione
  TEST_ASSERT_EQUAL(ScreenId::Qr, b.topId());
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.netApReady("K7MZ3QRT", b.now));
  TEST_ASSERT_EQUAL_STRING("K7MZ3QRT", b.ui.model().sys.apPass);
  b.ui.netConnecting("CasaWiFi", b.now);
  TEST_ASSERT_EQUAL(ScreenId::Connecting, b.topId());
  // password sbagliata: si torna al QR
  b.ui.netConnectFailed("CasaWiFi", b.now);
  TEST_ASSERT_EQUAL(ScreenId::Qr, b.topId());
  TEST_ASSERT_EQUAL_STRING("Connessione non riuscita", b.ui.model().toast.msg);
  b.ui.netConnecting("CasaWiFi", b.now);
  const int before = b.nReqs;
  b.ui.netConnected("CasaWiFi", -58, "192.168.1.42", b.now);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());  // di nuovo nel menu Wi-Fi
  TEST_ASSERT_TRUE(b.ui.model().sys.wifi);
  TEST_ASSERT_EQUAL_STRING("192.168.1.42", b.ui.model().sys.ip);
  TEST_ASSERT_EQUAL(before, b.nReqs);  // connesso: niente annullamento
  TEST_ASSERT_FALSE(b.ui.provisioning());

  // Uscire dal QR con il gesto lungo spegne AP e portale.
  b.act(Action::Ok);
  TEST_ASSERT_EQUAL(ScreenId::Qr, b.topId());
  b.act(Action::Back);
  TEST_ASSERT_EQUAL(NetCmd::CancelProvision, b.last()->cmd);
  // …anche con 3 pressioni (Home).
  b.act(Action::Ok);
  b.act(Action::Menu);
  TEST_ASSERT_EQUAL(ScreenId::Home, b.topId());
  TEST_ASSERT_EQUAL(NetCmd::CancelProvision, b.last()->cmd);
  // Primo avvio senza rete: l'AP pronto apre il QR da solo.
  b.ui.netApReady("ABCD2345", b.now);
  TEST_ASSERT_EQUAL(ScreenId::Qr, b.topId());
}

static void test_live_scan_and_join() {
  static LiveBench b;
  b.openWifiMenu();
  b.next(4);
  b.act(Action::Ok);  // Cerca reti
  TEST_ASSERT_EQUAL(ScreenId::WifiScan, b.topId());
  TEST_ASSERT_EQUAL(NetCmd::Scan, b.last()->cmd);
  TEST_ASSERT_TRUE(top(b.ui.model()).scanUntil > b.now);
  NetEntry nets[2] = {{"Officina", -52, true}, {"Ospiti", -70, false}};
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.ui.netScanDone(nets, 2, b.now));
  const MenuList &L = b.ui.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(2, L.n);
  TEST_ASSERT_EQUAL_STRING("Officina", L.items[0].label);
  TEST_ASSERT_EQUAL(4, L.items[0].bars);
  TEST_ASSERT_TRUE(L.items[0].lock);
  b.act(Action::Ok);  // rete protetta: QR con la rete preselezionata nel portale
  TEST_ASSERT_EQUAL(ScreenId::Qr, b.topId());
  TEST_ASSERT_EQUAL(NetCmd::Provision, b.last()->cmd);
  TEST_ASSERT_EQUAL_STRING("Officina", b.last()->ssid);
  b.act(Action::Back);
  b.act(Action::Next);
  b.act(Action::Ok);  // rete aperta: collegamento diretto
  TEST_ASSERT_EQUAL(ScreenId::Connecting, b.topId());
  TEST_ASSERT_EQUAL(NetCmd::Join, b.last()->cmd);
  TEST_ASSERT_EQUAL_STRING("Ospiti", b.last()->ssid);
  b.now += kJoinMaxMs + 1;  // la rete non risponde mai: fallisce da solo
  b.ui.tick(b.now, b.wc());
  TEST_ASSERT_EQUAL(ScreenId::WifiScan, b.topId());
  TEST_ASSERT_FALSE(b.ui.model().sys.wifi);
}

static void test_live_menu_values() {
  static LiveBench b;
  b.ui.netConnected("CasaWiFi", -61, "192.168.1.50", b.now);
  static MenuList L;
  const Model &m = b.ui.model();
  menuList(MenuId::Wifi, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", L.items[0].val);
  TEST_ASSERT_EQUAL_STRING("-61 dBm", L.items[1].val);
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", L.items[2].val);
  TEST_ASSERT_EQUAL_STRING("non risponde", L.items[6].val);
  TEST_ASSERT_TRUE(decode(readFile("../protocol/examples/state.json"), msg));
  b.ui.netState(msg, b.now);
  menuList(MenuId::Wifi, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("calamaio.local", L.items[6].val);  // PC collegato, host mDNS non ancora noto
  b.ui.netPcHost("mac-sviluppo.local", b.now);
  menuList(MenuId::Wifi, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("mac-sviluppo.local", L.items[6].val);
  menuList(MenuId::Info, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Calamaio OS 0.9", L.items[0].val);
  TEST_ASSERT_TRUE(decode("{\"t\":\"state\",\"v\":1,\"time\":1}", msg));
  b.ui.netState(msg, b.now);
  menuList(MenuId::Main, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("5h —", L.items[0].val);
}

// ---------------------------------------------------------------- schermate con la rete

namespace {
struct TextLog {
  std::vector<std::string> s;
  static void hook(const char *str, int, int, int, int, bool, Align, void *ctx) {
    static_cast<TextLog *>(ctx)->s.push_back(str);
  }
  bool has(const char *t) const {
    for (auto &x : s)
      if (x == t) return true;
    return false;
  }
};
}  // namespace

static void test_live_qr_screen_draws_real_qr() {
  static LiveBench b;
  strcpy(b.ui.model().sys.boardId, "48e8");
  b.ui.netApReady("K7MZ3QRT", b.now);
  static Canvas<250, 122> cv;
  TextLog log;
  ui::setTextHook(&TextLog::hook, &log);
  ui::compose(cv, kHeltec213, b.ui.model(), 0, b.now, b.wc());
  ui::setTextHook(nullptr, nullptr);
  static qr::Bits q;
  TEST_ASSERT_TRUE(qr::encode("WIFI:T:WPA;S:Calamaio-48E8;P:K7MZ3QRT;;", q));
  // stesso angolo del QR finto: x = 4 + 3, y = 16 + 3, moduli da 3 px
  int diff = 0;
  for (int r = 0; r < q.size; r++)
    for (int k = 0; k < q.size; k++)
      for (int dy = 0; dy < 3; dy++)
        for (int dx = 0; dx < 3; dx++) diff += cv.get(7 + k * 3 + dx, 19 + r * 3 + dy) != q.get(k, r);
  TEST_ASSERT_EQUAL(0, diff);
  for (int k = -3; k < q.size * 3 + 3; k++) {  // bordo di carta di un modulo
    TEST_ASSERT_FALSE(cv.get(7 + k, 16));
    TEST_ASSERT_FALSE(cv.get(4, 19 + (k < 0 ? 0 : k)));
  }
  // Schermata intera su disco: per provarla con un lettore QR vero (margine di un solo modulo, striscia nera sopra).
  if (FILE *f = fopen(".pio/qr-screen-48e8.pbm", "w")) {
    fprintf(f, "P1\n250 122\n");
    for (int y = 0; y < 122; y++) {
      for (int x = 0; x < 250; x++) fputs(cv.get(x, y) ? "1 " : "0 ", f);
      fputc('\n', f);
    }
    fclose(f);
  }
  TEST_ASSERT_TRUE(log.has("1. RETE CALAMAIO-48E8"));
  TEST_ASSERT_TRUE(log.has("PASSWORD K7MZ3QRT"));
  TEST_ASSERT_FALSE(log.has("QR DI ESEMPIO · 2X SIMULA"));
}

static void test_live_screens_without_data_dont_crash() {
  static LiveBench b;
  TEST_ASSERT_TRUE(decode("{\"t\":\"state\",\"v\":1,\"time\":1}", msg));
  b.ui.netState(msg, b.now);
  static Canvas<250, 122> cv;
  TextLog log;
  ui::setTextHook(&TextLog::hook, &log);
  const HomeStyle styles[4] = {HomeStyle::Goccia, HomeStyle::Righe, HomeStyle::Contatori, HomeStyle::Una};
  for (HomeStyle st : styles) {
    b.ui.model().set.homeStyle = st;
    ui::compose(cv, kHeltec213, b.ui.model(), 0, b.now, b.wc());
  }
  TEST_ASSERT_TRUE(log.has("NESSUNA"));
  const ScreenId views[] = {ScreenId::Limiti, ScreenId::Oggi, ScreenId::Storico, ScreenId::Servizi, ScreenId::Agenda,
                            ScreenId::Stato, ScreenId::Sleep, ScreenId::WifiScan};
  for (ScreenId id : views) {
    pushScreen(b.ui.model(), makeScreen(id));
    ui::compose(cv, kHeltec213, b.ui.model(), 0, b.now, b.wc());
    popScreen(b.ui.model());
  }
  ui::setTextHook(nullptr, nullptr);
  TEST_ASSERT_TRUE(log.has("nessun dato dal PC"));
  TEST_ASSERT_TRUE(log.has("calamaio.local"));  // Stato: alias finché mDNS non trova il PC
}

// 0.14.1: cal::Ui e ui::draw condividono una sola MenuList: i fotogrammi delle liste restano quelli della 0.14.0.
static uint32_t fnvFrame(const Canvas<250, 122> &cv) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 250 * 122 / 8; i++) h = (h ^ cv.data()[i]) * 16777619u;
  return h;
}
// Banco con dati veri (state d'esempio, rete in uso, reti salvate) e il Menu aperto.
static void listBench(LiveBench &b) {
  TEST_ASSERT_TRUE(decode(readFile("../protocol/examples/state.json"), msg));
  b.ui.netState(msg, b.now);
  b.ui.netConnected("Officina", -71, "192.168.1.50", b.now);
  SavedNet saved[2] = {};
  strcpy(saved[0].ssid, "CasaWiFi");
  saved[0].usedLocal = kNow - 5 * 86400000LL;
  saved[0].visible = true;
  saved[0].rssi = -83;
  strcpy(saved[1].ssid, "Officina");
  saved[1].usedLocal = kNow;
  b.ui.netSaved(saved, 2, b.now);
  b.act(Action::Menu);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
}
static void fakeNets(Model &m) {
  const NetEntry nets[3] = {{"Officina", -50, true}, {"Ospiti", -67, false}, {"Vicino di casa", -88, true}};
  for (int k = 0; k < 3; k++) m.nets[k] = nets[k];
  m.nNets = 3;
}
// Un fotogramma per MenuId (sel = 1) più «Reti vicine».
static void listFrames(LiveBench &b, uint32_t *out) {
  static Canvas<250, 122> cv;
  Model &m = b.ui.model();
  for (int id = 0; id < int(MenuId::Count); id++) {
    ScreenEntry e = makeScreen(ScreenId::List);
    e.menu = MenuId(id);
    e.sel = 1;
    if (e.menu == MenuId::SavedNet) strcpy(e.ssid, "CasaWiFi");
    m.ui.stack[m.ui.depth - 1] = e;
    ui::compose(cv, kHeltec213, m, 0, b.now, b.wc());
    out[id] = fnvFrame(cv);
  }
  fakeNets(m);
  ScreenEntry w = makeScreen(ScreenId::WifiScan);
  w.scanUntil = 0;
  w.sel = 1;
  m.ui.stack[m.ui.depth - 1] = w;
  ui::compose(cv, kHeltec213, m, 0, b.now, b.wc());
  out[int(MenuId::Count)] = fnvFrame(cv);
}

// Impronte prese sulla 0.14.0 (due MenuList), nell'ordine di MenuId; l'ultima è «Reti vicine».
static const uint32_t kListFrames[int(MenuId::Count) + 1] = {
    0x2A8347FCu, 0x82A76503u, 0xBDAF4A05u, 0xB769D5F7u, 0xBBE2F31Au, 0xB436971Au,
    0x0D1E2A77u, 0x6DBF9E37u, 0x4EF7F197u, 0x4F03F5E3u, 0x17968E6Eu, 0xF2666D8Du,
    0x63C76989u, 0xB141CD04u, 0x548A7A9Eu, 0xEEE3FB6Fu, 0xE2713571u, 0x157E23FDu,
};

static void test_list_frames_match_0140() {
  static LiveBench b;
  listBench(b);
  uint32_t got[int(MenuId::Count) + 1];
  listFrames(b, got);
  for (int i = 0; i <= int(MenuId::Count); i++) TEST_ASSERT_EQUAL_HEX32(kListFrames[i], got[i]);
}

// Disegno e cal::Ui si alternano sulla stessa lista: ognuno la riempie prima di leggerla.
static void test_shared_menu_list_draw_then_ui() {
  static LiveBench b, ref;  // ref: stessi passi senza disegni in mezzo
  static Canvas<250, 122> A, B;
  listBench(b);
  listBench(ref);
  TEST_ASSERT_TRUE(&b.ui.items(b.now, b.wc()) == &cal::sharedMenuList());
  Model &m = b.ui.model();
  // Menu aperto: lo stesso fotogramma anche con la lista sporcata da un'altra schermata.
  ui::compose(A, kHeltec213, m, 0, b.now, b.wc());
  fakeNets(m);
  cal::netItems(m, cal::sharedMenuList());
  ui::compose(B, kHeltec213, m, 0, b.now, b.wc());
  TEST_ASSERT_EQUAL_MEMORY(A.data(), B.data(), 250 * 122 / 8);
  cal::menuList(MenuId::Info, kHeltec213, m, b.now, b.wc(), cal::sharedMenuList());
  ui::compose(B, kHeltec213, m, 0, b.now, b.wc());
  TEST_ASSERT_EQUAL_MEMORY(A.data(), B.data(), 250 * 122 / 8);
  // Dopo il disegno di «Reti vicine», Next e Ok sul Menu aprono la stessa voce del banco che non disegna.
  fakeNets(ref.ui.model());
  ScreenEntry w = makeScreen(ScreenId::WifiScan);
  w.scanUntil = 0;
  pushScreen(m, w);
  ui::compose(B, kHeltec213, m, 0, b.now, b.wc());
  popScreen(m);
  b.next(2);
  ref.next(2);
  TEST_ASSERT_EQUAL_STRING(ref.ui.lastAction(), b.ui.lastAction());
  ui::compose(B, kHeltec213, m, 0, b.now, b.wc());
  pushScreen(m, w);
  ui::compose(B, kHeltec213, m, 0, b.now, b.wc());
  popScreen(m);
  b.act(Action::Ok);
  ref.act(Action::Ok);
  TEST_ASSERT_EQUAL(int(top(ref.ui.model()).id), int(b.topId()));
  TEST_ASSERT_EQUAL(int(top(ref.ui.model()).menu), int(top(m).menu));
  TEST_ASSERT_EQUAL(ref.ui.model().ui.depth, m.ui.depth);
  TEST_ASSERT_EQUAL_STRING(ref.ui.lastAction(), b.ui.lastAction());
  // Next su «Reti vicine» dopo un disegno del Menu: «Voce: <rete>» come senza disegno.
  pushScreen(m, w);
  pushScreen(ref.ui.model(), w);
  ScreenEntry menu = makeScreen(ScreenId::List);
  menu.menu = MenuId::Main;
  pushScreen(m, menu);
  ui::compose(B, kHeltec213, m, 0, b.now, b.wc());
  popScreen(m);
  b.act(Action::Next);
  ref.act(Action::Next);
  TEST_ASSERT_EQUAL_STRING("Voce: Ospiti", ref.ui.lastAction());
  TEST_ASSERT_EQUAL_STRING(ref.ui.lastAction(), b.ui.lastAction());
}

// Riconnessione (Wi-Fi, mDNS, WebSocket): subito, 5 s, 5 s, poi ogni 30 s. Orologio finto a passi di 100 ms.
static void retryTimes(uint32_t start, uint32_t busyMs, uint32_t until, std::vector<uint32_t> &at) {
  cal::RetrySchedule r;
  r.restart(start);
  uint32_t busyTill = start;
  for (uint32_t t = start; int32_t(t - until) < 0; t += 100) {
    if (int32_t(t - busyTill) < 0) continue;  // tentativo in corso (bloccante o in attesa dell'esito)
    if (r.due(t)) { r.attempt(t); at.push_back(t - start); busyTill = t + busyMs; }
  }
}

static void test_retry_sequence() {
  std::vector<uint32_t> at;
  retryTimes(1000, 0, 1000 + 131000, at);
  const uint32_t want[] = {0, 5000, 10000, 40000, 70000, 100000, 130000};
  TEST_ASSERT_EQUAL_INT(7, int(at.size()));
  for (int i = 0; i < 7; i++) TEST_ASSERT_EQUAL_UINT32(want[i], at[i]);
  TEST_ASSERT_EQUAL_UINT32(5000, cal::RetrySchedule::delayAfter(1));
  TEST_ASSERT_EQUAL_UINT32(5000, cal::RetrySchedule::delayAfter(2));
  TEST_ASSERT_EQUAL_UINT32(30000, cal::RetrySchedule::delayAfter(3));
}

static void test_retry_slow_attempts_and_wrap() {
  // Wi-Fi: l'esito arriva dopo 6 s (grazia di startJoin). Il tentativo dovuto a 5 s parte appena il primo
  // fallisce (6 s), una volta sola; poi si torna sull'orario previsto (40 s).
  std::vector<uint32_t> at;
  retryTimes(0, 6000, 75000, at);
  const uint32_t want[] = {0, 6000, 12000, 40000, 70000};
  TEST_ASSERT_EQUAL_INT(5, int(at.size()));
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT32(want[i], at[i]);
  // millis() che fa il giro a 32 bit durante la sequenza.
  at.clear();
  retryTimes(0xFFFFF000u, 0, 0xFFFFF000u + 41000, at);
  const uint32_t want2[] = {0, 5000, 10000, 40000};
  TEST_ASSERT_EQUAL_INT(4, int(at.size()));
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(want2[i], at[i]);
  // restart() dopo un collegamento caduto: di nuovo subito.
  cal::RetrySchedule r;
  r.restart(0);
  r.attempt(0);
  r.attempt(5000);
  r.restart(9000);
  TEST_ASSERT_TRUE(r.due(9000));
  TEST_ASSERT_EQUAL_INT(1, r.attempt(9000));
  TEST_ASSERT_FALSE(r.due(13900));
  TEST_ASSERT_TRUE(r.due(14000));
  r.stop();
  TEST_ASSERT_FALSE(r.due(99999));
}

// M6: il filtro della deserializzazione deve tenere summary.off del bye (prima lo scartava: «off 0» dal vivo).
void test_bye_keeps_off() {
  const std::string js = readFile("../protocol/examples/bye.json");
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 20, "protocol/examples/bye.json non trovato");
  JsonDocument doc;
  TEST_ASSERT_TRUE(cal::parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_TRUE(doc["summary"]["off"].is<int64_t>());
  TEST_ASSERT_TRUE(doc["summary"]["off"].as<int64_t>() == 1791738000000LL);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_board_names);
  RUN_TEST(test_bye_keeps_off);
  RUN_TEST(test_wifi_qr_text);
  RUN_TEST(test_ap_password);
  RUN_TEST(test_mdns_answer_valid);
  RUN_TEST(test_qr_wifi_is_version3);
  RUN_TEST(test_example_state);
  RUN_TEST(test_example_state_empty);
  RUN_TEST(test_log_merge_in_place_matches_old);
  RUN_TEST(test_welcome_and_types);
  RUN_TEST(test_malformed_ignored);
  RUN_TEST(test_missing_and_null_fields);
  RUN_TEST(test_long_utf8_texts);
  RUN_TEST(test_more_than_six_sessions_and_twelve_tasks);
  RUN_TEST(test_log_capped);
  RUN_TEST(test_live_starts_empty_and_offline);
  RUN_TEST(test_live_state_and_pc_lost);
  RUN_TEST(test_live_forget_needs_confirmation);
  RUN_TEST(test_live_saved_networks);
  RUN_TEST(test_live_provisioning_flow);
  RUN_TEST(test_live_scan_and_join);
  RUN_TEST(test_live_menu_values);
  RUN_TEST(test_live_qr_screen_draws_real_qr);
  RUN_TEST(test_live_screens_without_data_dont_crash);
  RUN_TEST(test_list_frames_match_0140);
  RUN_TEST(test_shared_menu_list_draw_then_ui);
  RUN_TEST(test_retry_sequence);
  RUN_TEST(test_retry_slow_attempts_and_wrap);
  return UNITY_END();
}
