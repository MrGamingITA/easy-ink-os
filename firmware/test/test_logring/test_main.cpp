// Registro da remoto (docs/OTA.md §5) e rapporti di crash (§6): anello da 16 KB, messaggi log ≤ 8 KB con
// raggruppamento di ~1 s, scadenza e fine; coda RTC delle ultime righe; messaggio crash.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/crash.h"
#include "core/logring.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static LogRing gRing;  // 16 KB: statico
static char gOut[LogStream::kMsgMax + 64];

static bool gEnd = false;  // l'ultimo messaggio letto aveva end: true
static std::vector<std::string> lines(const char *js) {
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, js));
  TEST_ASSERT_EQUAL_STRING("log", d["t"]);
  gEnd = d["end"].is<bool>() && d["end"].as<bool>();
  TEST_ASSERT_TRUE(d["lines"].size() <= 400);  // schema: maxItems
  std::vector<std::string> v;
  for (JsonVariantConst l : d["lines"].as<JsonArrayConst>()) v.push_back(l.as<std::string>());
  return v;
}

static void start(LogStream &s, const char *js, Ms now, bool authed = true) {
  JsonDocument d;
  TEST_ASSERT_TRUE(parseMessage(js, strlen(js), d));
  char why[64] = "";
  TEST_ASSERT_TRUE_MESSAGE(s.start(d.as<JsonVariantConst>(), authed, now, gRing, why, sizeof why), why);
}

static void test_ring_lines_prefix_and_fragments() {
  gRing.clear();
  gRing.write("[ws] connesso", 13, 12345);
  gRing.write(" a ws://pc\r\n[m5] ok\n", 20, 12400);
  TEST_ASSERT_EQUAL(2, gRing.nextSeq());
  std::vector<std::string> got;
  gRing.each(0, [&](uint32_t, const char *l, size_t) { got.push_back(l); return true; });
  TEST_ASSERT_EQUAL_STRING("12.345 [ws] connesso a ws://pc", got[0].c_str());  // istante del primo byte
  TEST_ASSERT_EQUAL_STRING("12.400 [m5] ok", got[1].c_str());
  // riga troppo lunga: tagliata a kLineMax senza spezzare «è»
  std::string lng(300, 'a');
  lng[197] = '\xC3';
  lng[198] = '\xA8';
  lng += "\n";
  gRing.write(lng.c_str(), lng.size(), 1);
  gRing.each(2, [&](uint32_t, const char *l, size_t n) {
    TEST_ASSERT_TRUE(n <= LogRing::kLineMax);
    const unsigned char last = static_cast<unsigned char>(l[n - 1]);
    TEST_ASSERT_TRUE(last < 0x80 || last == 0xA8);  // mai un byte iniziale senza seguito
    return true;
  });
}

static void test_ring_keeps_last_16k() {
  gRing.clear();
  char l[64];
  for (int i = 0; i < 2000; i++) {
    const int n = snprintf(l, sizeof l, "[riga] numero %d\n", i);
    gRing.write(l, n, uint32_t(i));
  }
  TEST_ASSERT_TRUE(gRing.used() <= LogRing::kCap);
  TEST_ASSERT_TRUE(gRing.used() > LogRing::kCap - 64);
  TEST_ASSERT_EQUAL(2000, gRing.nextSeq());
  TEST_ASSERT_TRUE(gRing.firstSeq() > 0);
  std::string lastLine;
  int count = 0;
  gRing.each(0, [&](uint32_t, const char *s, size_t) { lastLine = s; count++; return true; });
  TEST_ASSERT_EQUAL(int(gRing.nextSeq() - gRing.firstSeq()), count);
  TEST_ASSERT_EQUAL_STRING("1.999 [riga] numero 1999", lastLine.c_str());
}

static void test_dump_chunks_under_8k_then_end() {
  // anello pieno → il dump va in più messaggi, ognuno ≤ 8 KB, nell'ordine; end: true solo nell'ultimo
  LogStream s;
  start(s, "{\"t\":\"log-start\",\"v\":1,\"id\":\"c31\"}", 0);
  std::vector<std::string> all;
  int msgs = 0;
  for (;;) {
    const size_t n = s.poll(1, gRing, gOut, sizeof gOut);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n <= LogStream::kMsgMax);
    TEST_ASSERT_EQUAL(strlen(gOut), n);
    std::vector<std::string> v = lines(gOut);
    msgs++;
    all.insert(all.end(), v.begin(), v.end());
    if (gEnd) break;
    TEST_ASSERT_TRUE(msgs < 10);
  }
  TEST_ASSERT_FALSE(s.active());
  TEST_ASSERT_TRUE(msgs >= 3);  // 16 KB di testo (con le virgolette) in messaggi da 8 KB
  TEST_ASSERT_EQUAL(int(gRing.nextSeq() - gRing.firstSeq()), int(all.size()));
  TEST_ASSERT_EQUAL_STRING("1.999 [riga] numero 1999", all.back().c_str());
  TEST_ASSERT_EQUAL(0, s.poll(2, gRing, gOut, sizeof gOut));
}

static void test_follow_groups_every_second_until_expiry() {
  gRing.clear();
  gRing.write("prima\n", 6, 0);
  LogStream s;
  start(s, "{\"t\":\"log-start\",\"v\":1,\"id\":\"c32\",\"follow\":true,\"minutes\":1}", 1000);
  TEST_ASSERT_TRUE(s.poll(1000, gRing, gOut, sizeof gOut) > 0);
  TEST_ASSERT_EQUAL(1, lines(gOut).size());
  TEST_ASSERT_TRUE(gEnd);  // fine del contenuto dell'anello; poi in diretta
  TEST_ASSERT_TRUE(s.active());
  // righe nuove: raggruppate, al massimo una volta al secondo
  gRing.write("a\nb\n", 4, 1200);
  TEST_ASSERT_EQUAL(0, s.poll(1500, gRing, gOut, sizeof gOut));
  gRing.write("c\n", 2, 1800);
  TEST_ASSERT_TRUE(s.poll(2000, gRing, gOut, sizeof gOut) > 0);
  std::vector<std::string> v = lines(gOut);
  TEST_ASSERT_EQUAL(3, v.size());
  TEST_ASSERT_EQUAL_STRING("1.800 c", v[2].c_str());
  TEST_ASSERT_FALSE(gEnd);  // in diretta niente end
  TEST_ASSERT_EQUAL(0, s.poll(5000, gRing, gOut, sizeof gOut));  // niente di nuovo: niente
  // scadenza a 1 minuto dall'inizio: si smette, senza messaggio
  gRing.write("d\n", 2, 60000);
  TEST_ASSERT_EQUAL(0, s.poll(61000, gRing, gOut, sizeof gOut));
  TEST_ASSERT_FALSE(s.active());
}

static void test_stop_lost_lines_and_auth() {
  gRing.clear();
  LogStream s;
  start(s, "{\"t\":\"log-start\",\"v\":1,\"id\":\"c33\",\"follow\":true,\"minutes\":60}", 0);
  TEST_ASSERT_TRUE(s.poll(0, gRing, gOut, sizeof gOut) > 0);  // anello vuoto: lines [] ed end
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"log\",\"v\":1,\"id\":\"c33\",\"lines\":[],\"end\":true}", gOut);
  // il ritardo fa perdere righe (anello pieno): si dice quante
  char l[64];
  for (int i = 0; i < 3000; i++) gRing.write(l, size_t(snprintf(l, sizeof l, "riga di prova numero %d\n", i)), 10);
  TEST_ASSERT_TRUE(s.poll(2000, gRing, gOut, sizeof gOut) > 0);
  std::vector<std::string> v = lines(gOut);
  TEST_ASSERT_TRUE(v[0].find("righe perse (anello pieno)") != std::string::npos);
  JsonDocument d;
  TEST_ASSERT_TRUE(parseMessage("{\"t\":\"log-stop\",\"v\":1,\"id\":\"altro\"}", 37, d));
  TEST_ASSERT_FALSE(s.stop(d.as<JsonVariantConst>(), true));
  d.clear();
  TEST_ASSERT_TRUE(parseMessage("{\"t\":\"log-stop\",\"v\":1,\"id\":\"c33\"}", 35, d));
  TEST_ASSERT_EQUAL(int(MsgType::LogStop), int(msgType(d.as<JsonVariantConst>())));
  TEST_ASSERT_FALSE(s.stop(d.as<JsonVariantConst>(), false));  // senza autenticazione no
  TEST_ASSERT_TRUE(s.stop(d.as<JsonVariantConst>(), true));
  TEST_ASSERT_FALSE(s.active());
  TEST_ASSERT_EQUAL(0, s.poll(3000, gRing, gOut, sizeof gOut));  // dopo log-stop non parte più niente
  // log-start rifiutati
  LogStream t;
  char why[64];
  const char *bad[] = {"{\"t\":\"log-start\",\"v\":1,\"id\":\"c1\",\"minutes\":61}", "{\"t\":\"log-start\",\"v\":1,\"id\":\"c1\",\"minutes\":0}",
                       "{\"t\":\"log-start\",\"v\":1}", "{\"t\":\"log-start\",\"v\":1,\"id\":\"c1\",\"follow\":\"si\"}",
                       "{\"t\":\"log-start\",\"v\":1,\"id\":\"c 1\"}"};
  for (const char *js : bad) {
    d.clear();
    TEST_ASSERT_TRUE(parseMessage(js, strlen(js), d));
    TEST_ASSERT_FALSE_MESSAGE(t.start(d.as<JsonVariantConst>(), true, 0, gRing, why, sizeof why), js);
  }
  d.clear();
  TEST_ASSERT_TRUE(parseMessage("{\"t\":\"log-start\",\"v\":1,\"id\":\"c1\"}", 34, d));
  TEST_ASSERT_FALSE(t.start(d.as<JsonVariantConst>(), false, 0, gRing, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("log-start senza autenticazione", why);
}

static void test_escape() {
  gRing.clear();
  const char raw[] = "virgolette \" barra \\ tab\t fine\n";
  gRing.write(raw, sizeof raw - 1, 0);
  LogStream s;
  start(s, "{\"t\":\"log-start\",\"v\":1,\"id\":\"c34\"}", 0);
  TEST_ASSERT_TRUE(s.poll(0, gRing, gOut, sizeof gOut) > 0);
  TEST_ASSERT_EQUAL_STRING("0.000 virgolette \" barra \\ tab\t fine", lines(gOut)[0].c_str());
}

// ---------------------------------------------------------------- crash
static crash::Tail gTail;

// 0.12.2-gb: anello prestato alla partita (RAM della cartuccia). Le righe vanno solo al gancio (coda RTC), il
// buffer resta intatto, gli stream non danno righe; alla restituzione anello vuoto e «righe perse».
static int gHooked = 0;
static void countHook(const char *, size_t, void *) { gHooked++; }

static void test_ring_lend_and_give_back() {
  gRing.clear();
  gRing.setHook(countHook, nullptr);
  gHooked = 0;
  gRing.write("prima\nseconda\n", 14, 5);
  TEST_ASSERT_EQUAL(2, gHooked);
  LogStream s;
  start(s, "{\"t\":\"log-start\",\"v\":1,\"id\":\"c40\",\"follow\":true}", 0);  // dump in corso, mai letto
  TEST_ASSERT_NULL(gRing.lend(LogRing::kCap + 1));  // troppo grande: niente prestito
  TEST_ASSERT_FALSE(gRing.lent());
  uint8_t *p = gRing.lend(2 * 8192);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_TRUE(gRing.lent());
  TEST_ASSERT_NULL(gRing.lend(16));  // già prestato
  TEST_ASSERT_EQUAL(0, gRing.used());
  memset(p, 0xA5, 2 * 8192);
  char l[64];
  for (int i = 0; i < 500; i++) gRing.write(l, size_t(snprintf(l, sizeof l, "[gb] riga in partita %d\n", i)), 100);
  TEST_ASSERT_EQUAL(502, gHooked);  // la coda RTC continua
  for (int i = 0; i < 2 * 8192; i++) TEST_ASSERT_EQUAL_HEX8(0xA5, p[i]);  // il buffer non si tocca
  TEST_ASSERT_EQUAL(0, gRing.used());
  int seen = 0;
  gRing.each(0, [&](uint32_t, const char *, size_t) { seen++; return true; });
  TEST_ASSERT_EQUAL(0, seen);
  TEST_ASSERT_EQUAL(0, s.poll(5000, gRing, gOut, sizeof gOut));  // stream: niente righe
  JsonDocument d;
  TEST_ASSERT_TRUE(parseMessage("{\"t\":\"log-start\",\"v\":1,\"id\":\"c41\"}", 35, d));
  LogStream t;
  char why[64] = "";
  TEST_ASSERT_FALSE(t.start(d.as<JsonVariantConst>(), true, 0, gRing, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("registro prestato alla partita", why);
  gRing.giveBack();
  TEST_ASSERT_FALSE(gRing.lent());
  TEST_ASSERT_EQUAL(0, gRing.used());
  // lo stream di prima finisce pulito: le due righe sono perse, poi end
  TEST_ASSERT_TRUE(s.poll(6000, gRing, gOut, sizeof gOut) > 0);
  std::vector<std::string> v = lines(gOut);
  TEST_ASSERT_EQUAL(1, v.size());
  TEST_ASSERT_TRUE(v[0].find("2 righe perse") != std::string::npos);
  TEST_ASSERT_TRUE(gEnd);
  // anello di nuovo in uso, sopra i 0xA5 della partita si legge solo il nuovo
  gRing.write("dopo\n", 5, 7000);
  TEST_ASSERT_TRUE(t.start(d.as<JsonVariantConst>(), true, 7000, gRing, why, sizeof why));
  TEST_ASSERT_TRUE(t.poll(7000, gRing, gOut, sizeof gOut) > 0);
  v = lines(gOut);
  TEST_ASSERT_EQUAL(1, v.size());
  TEST_ASSERT_EQUAL_STRING("7.000 dopo", v[0].c_str());
  gRing.giveBack();  // non prestato: niente
  TEST_ASSERT_EQUAL(1, int(gRing.nextSeq() - gRing.firstSeq()));
  gRing.setHook(nullptr, nullptr);
}

static void test_tail_last_lines() {
  crash::tailReset(gTail, "0.7.0");
  TEST_ASSERT_TRUE(crash::tailValid(gTail));
  char l[64];
  for (int i = 0; i < 100; i++) crash::tailAppend(gTail, l, size_t(snprintf(l, sizeof l, "riga %d del registro prima del crash", i)));
  char out[crash::kLastMax][160];
  const int n = crash::tailLines(gTail, out, crash::kLastMax);
  TEST_ASSERT_EQUAL(8, n);
  TEST_ASSERT_EQUAL_STRING("riga 92 del registro prima del crash", out[0]);
  TEST_ASSERT_EQUAL_STRING("riga 99 del registro prima del crash", out[7]);
  crash::tailReset(gTail, "0.7.0");
  crash::tailAppend(gTail, "una", 3);
  TEST_ASSERT_EQUAL(1, crash::tailLines(gTail, out, crash::kLastMax));
  TEST_ASSERT_EQUAL_STRING("una", out[0]);
  gTail.magic = 0;  // memoria RTC dopo un'accensione: non valida
  TEST_ASSERT_EQUAL(0, crash::tailLines(gTail, out, crash::kLastMax));
}

static void test_crash_report() {
  crash::tailReset(gTail, "0.7.0");
  crash::tailAppend(gTail, "814.100 [ws] state: 4 sessioni", 30);
  crash::tailAppend(gTail, "815.002 [refresh] partial 644 ms", 32);
  crash::Summary core = {};
  core.cause = 0xFFFFFFFFu;
  crash::Report r;
  // riavvio normale e niente coredump: nessun rapporto
  TEST_ASSERT_FALSE(crash::build(crash::Reset::PowerOn, core, &gTail, "0.7.0", nullptr, r));
  TEST_ASSERT_FALSE(crash::build(crash::Reset::Sw, core, &gTail, "0.7.0", nullptr, r));
  TEST_ASSERT_FALSE(crash::build(crash::Reset::DeepSleep, core, &gTail, "0.7.0", nullptr, r));
  // watchdog senza coredump: motivo e ultime righe (unite da a capo, ≤ 255 byte); fw = chi ha scritto la coda
  TEST_ASSERT_TRUE(crash::build(crash::Reset::TaskWdt, core, &gTail, "0.6.0", "ab12", r));
  char out[1024];
  crash::encode(r, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"crash\",\"v\":1,\"fw\":\"0.7.0\",\"reason\":\"ESP_RST_TASK_WDT\",\"last\":\"814.100 [ws] state: 4 sessioni\\n815.002 [refresh] partial 644 ms\"}", out);
  // panic con il coredump: causa, task, PC, backtrace; elf solo se la versione caduta è quella che gira
  core.has = true;
  strcpy(core.task, "loopTask");
  core.pc = 0x42012345;
  core.cause = 28;
  core.nBt = 3;
  core.bt[0] = 0x42012345;
  core.bt[1] = 0x42008abc;
  core.bt[2] = 0x4037a1b2;
  TEST_ASSERT_TRUE(crash::build(crash::Reset::Panic, core, &gTail, "0.7.0", "bf9fc49ac40ee901", r));
  crash::encode(r, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(
      "{\"t\":\"crash\",\"v\":1,\"fw\":\"0.7.0\",\"reason\":\"LoadProhibited\",\"exc\":\"EXCCAUSE 28\",\"task\":\"loopTask\",\"pc\":\"0x42012345\","
      "\"backtrace\":[\"0x42012345\",\"0x42008abc\",\"0x4037a1b2\"],\"last\":\"814.100 [ws] state: 4 sessioni\\n815.002 [refresh] partial 644 ms\","
      "\"elf\":\"bf9fc49ac40ee901\"}",
      out);
  // coredump rimasto da un avvio precedente (non inviato): niente righe della coda, che sono di adesso
  TEST_ASSERT_TRUE(crash::build(crash::Reset::DeepSleep, core, &gTail, "0.6.0", nullptr, r));
  TEST_ASSERT_EQUAL_STRING("", r.last);
  TEST_ASSERT_EQUAL_STRING("0.6.0", r.fw);
  TEST_ASSERT_EQUAL_STRING("LoadProhibited", r.reason);
  // last ≤ 255 byte: si tengono le righe più recenti intere
  crash::tailReset(gTail, "0.7.0");
  char l[160];
  for (int i = 0; i < 20; i++) crash::tailAppend(gTail, l, size_t(snprintf(l, sizeof l, "%03d riga di registro lunga una sessantina di caratteri circa ...", i)));
  core.has = false;
  TEST_ASSERT_TRUE(crash::build(crash::Reset::Brownout, core, &gTail, "0.7.0", nullptr, r));
  TEST_ASSERT_TRUE(strlen(r.last) <= 255);
  TEST_ASSERT_TRUE(strstr(r.last, "019 riga") != nullptr);
  TEST_ASSERT_TRUE(strncmp(r.last, "01", 2) == 0);  // comincia con una riga intera
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ring_lines_prefix_and_fragments);
  RUN_TEST(test_ring_keeps_last_16k);
  RUN_TEST(test_dump_chunks_under_8k_then_end);
  RUN_TEST(test_follow_groups_every_second_until_expiry);
  RUN_TEST(test_stop_lost_lines_and_auth);
  RUN_TEST(test_escape);
  RUN_TEST(test_ring_lend_and_give_back);
  RUN_TEST(test_tail_last_lines);
  RUN_TEST(test_crash_report);
  return UNITY_END();
}
