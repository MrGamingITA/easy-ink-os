// 0.12.1-gb: piano B per trovare il PC senza mDNS (core/pc_fallback): indirizzo manuale, ultimo PC per rete,
// scelta della riserva, formato della NVS.
#include <string.h>
#include <unity.h>

#include "core/pc_fallback.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static PcAddr addr(const char *ip, uint16_t port, const char *path = "/ws") {
  PcAddr a;
  memset(&a, 0, sizeof a);
  strcpy(a.ip, ip);
  a.port = port;
  strcpy(a.path, path);
  return a;
}

static void test_parse_host_ok() {
  PcAddr a;
  TEST_ASSERT_TRUE(parsePcHost("192.168.1.20", "/ws", a));
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", a.ip);
  TEST_ASSERT_EQUAL_UINT16(8766, a.port);  // porta predefinita
  TEST_ASSERT_EQUAL_STRING("/ws", a.path);
  TEST_ASSERT_TRUE(parsePcHost("10.0.0.5:9000", "/ws", a));
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", a.ip);
  TEST_ASSERT_EQUAL_UINT16(9000, a.port);
  TEST_ASSERT_TRUE(parsePcHost("10.0.0.5:1", "/ws", a));
  TEST_ASSERT_EQUAL_UINT16(1, a.port);
  TEST_ASSERT_TRUE(parsePcHost("10.0.0.5:65535", "/ws", a));
  TEST_ASSERT_EQUAL_UINT16(65535, a.port);
}

static void test_parse_host_rejects() {
  PcAddr a = addr("1.2.3.4", 5);
  const char *bad[] = {"", "pc.local", "mac-sviluppo", "192.168.1", "192.168.1.256", "0.0.0.0", "255.255.255.255",
                       "1.2.3.4:", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:123456", "1.2.3.4:80x", "1.2.3.4:-1",
                       " 1.2.3.4", "1.2.3.4 ", "1.2.3.4:80:90", "fe80::1", "[::1]:8766", "1.2.3.4/ws", "http://1.2.3.4"};
  for (const char *s : bad) {
    TEST_ASSERT_FALSE_MESSAGE(parsePcHost(s, "/ws", a), s);
  }
  TEST_ASSERT_FALSE(parsePcHost(nullptr, "/ws", a));
  TEST_ASSERT_EQUAL_STRING("1.2.3.4", a.ip);  // invariato
  TEST_ASSERT_EQUAL_UINT16(5, a.port);
}

static void test_texts() {
  char b[32];
  pcHostText(addr("192.168.1.20", 8766), b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", b);
  pcHostText(addr("192.168.1.20", 9000), b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20:9000", b);
  pcAddrText(addr("192.168.1.20", 8766), b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20:8766", b);
  PcAddr none;
  memset(&none, 0, sizeof none);
  TEST_ASSERT_EQUAL(0, pcAddrText(none, b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("", b);
  TEST_ASSERT_EQUAL(0, pcHostText(none, b, sizeof b));
}

static void test_pick_fallback() {
  TEST_ASSERT_EQUAL(int(PcSrc::None), int(pickFallback(false, false, PcSrc::None)));
  TEST_ASSERT_EQUAL(int(PcSrc::None), int(pickFallback(false, false, PcSrc::Manual)));
  // prima il manuale, poi l'ultimo noto, poi di nuovo il manuale
  TEST_ASSERT_EQUAL(int(PcSrc::Manual), int(pickFallback(true, true, PcSrc::None)));
  TEST_ASSERT_EQUAL(int(PcSrc::Manual), int(pickFallback(true, true, PcSrc::Mdns)));
  TEST_ASSERT_EQUAL(int(PcSrc::Last), int(pickFallback(true, true, PcSrc::Manual)));
  TEST_ASSERT_EQUAL(int(PcSrc::Manual), int(pickFallback(true, true, PcSrc::Last)));
  // uno solo: sempre quello
  TEST_ASSERT_EQUAL(int(PcSrc::Manual), int(pickFallback(true, false, PcSrc::Manual)));
  TEST_ASSERT_EQUAL(int(PcSrc::Last), int(pickFallback(false, true, PcSrc::Last)));
  TEST_ASSERT_EQUAL(int(PcSrc::Last), int(pickFallback(false, true, PcSrc::None)));
}

static void test_table_write_only_on_change() {
  PcMemory m;
  const PcAddr a = addr("192.168.1.20", 8766);
  TEST_ASSERT_TRUE(m.setLast("Casa", a));
  TEST_ASSERT_FALSE(m.setLast("Casa", a));  // stesso valore: niente scrittura
  TEST_ASSERT_TRUE(m.setLast("Casa", addr("192.168.1.21", 8766)));
  TEST_ASSERT_TRUE(m.setLast("Casa", addr("192.168.1.21", 8766, "/ws2")));
  TEST_ASSERT_EQUAL(1, m.count());
  TEST_ASSERT_TRUE(m.setManual("Casa", &a));
  TEST_ASSERT_FALSE(m.setManual("Casa", &a));
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", m.find("Casa")->manual.ip);
  TEST_ASSERT_EQUAL_STRING("/ws2", m.find("Casa")->last.path);
  TEST_ASSERT_TRUE(m.setManual("Casa", nullptr));   // cancellato
  TEST_ASSERT_FALSE(m.setManual("Casa", nullptr));  // già vuoto
  TEST_ASSERT_EQUAL_STRING("", m.find("Casa")->manual.ip);
  TEST_ASSERT_EQUAL_STRING("192.168.1.21", m.find("Casa")->last.ip);
  TEST_ASSERT_NULL(m.find("Altra"));
  TEST_ASSERT_FALSE(m.setManual("Altra", nullptr));  // niente da cancellare
  TEST_ASSERT_NULL(m.find("Altra"));
}

static void test_table_rejects_invalid() {
  PcMemory m;
  TEST_ASSERT_FALSE(m.setLast("", addr("1.2.3.4", 8766)));
  TEST_ASSERT_FALSE(m.setLast("Casa", addr("0.0.0.0", 8766)));
  TEST_ASSERT_FALSE(m.setLast("Casa", addr("1.2.3.4", 0)));
  TEST_ASSERT_FALSE(m.setLast("Casa", addr("1.2.3.4", 8766, "ws")));
  TEST_ASSERT_FALSE(m.setLast("123456789012345678901234567890123", addr("1.2.3.4", 8766)));  // 33 byte
  TEST_ASSERT_EQUAL(0, m.count());
}

static void test_table_empty_entry_goes() {
  PcMemory m;
  const PcAddr a = addr("10.0.0.2", 8766);
  TEST_ASSERT_TRUE(m.setManual("Ufficio", &a));
  TEST_ASSERT_TRUE(m.setManual("Ufficio", nullptr));
  TEST_ASSERT_EQUAL(0, m.count());
}

static void test_table_cap_drops_oldest() {
  PcMemory m;
  const char *nets[] = {"A", "B", "C", "D", "E"};
  for (int i = 0; i < 5; i++) TEST_ASSERT_TRUE(m.setLast(nets[i], addr("10.0.0.1", uint16_t(8000 + i))));
  TEST_ASSERT_EQUAL(5, m.count());
  // A cambia: diventa la più recente; esce B (la più vecchia) quando arriva F
  TEST_ASSERT_TRUE(m.setLast("A", addr("10.0.0.9", 8766)));
  TEST_ASSERT_TRUE(m.setLast("F", addr("10.0.0.6", 8766)));
  TEST_ASSERT_EQUAL(5, m.count());
  TEST_ASSERT_NULL(m.find("B"));
  TEST_ASSERT_NOT_NULL(m.find("A"));
  TEST_ASSERT_NOT_NULL(m.find("F"));
  TEST_ASSERT_EQUAL_STRING("C", m.at(0).ssid);
  // un valore uguale non sposta la voce
  TEST_ASSERT_FALSE(m.setLast("C", addr("10.0.0.1", 8002)));
  TEST_ASSERT_TRUE(m.setLast("G", addr("10.0.0.7", 8766)));
  TEST_ASSERT_NULL(m.find("C"));
}

static void test_table_prune_and_forget() {
  PcMemory m;
  m.setLast("A", addr("10.0.0.1", 8766));
  m.setLast("B", addr("10.0.0.2", 8766));
  m.setLast("C", addr("10.0.0.3", 8766));
  const char keep[2][33] = {"C", "A"};
  TEST_ASSERT_TRUE(m.prune(keep, 2));
  TEST_ASSERT_FALSE(m.prune(keep, 2));
  TEST_ASSERT_EQUAL(2, m.count());
  TEST_ASSERT_NULL(m.find("B"));
  TEST_ASSERT_TRUE(m.forget("A"));
  TEST_ASSERT_FALSE(m.forget("A"));
  TEST_ASSERT_EQUAL(1, m.count());
  TEST_ASSERT_TRUE(m.prune(keep, 0));
  TEST_ASSERT_EQUAL(0, m.count());
}

static void test_blob_roundtrip() {
  PcMemory m;
  const PcAddr man = addr("192.168.1.20", 9000);
  m.setLast("Casa", addr("192.168.1.21", 8766));
  m.setManual("Casa", &man);
  m.setLast("Ufficio: 2° piano", addr("10.1.2.3", 8766, "/ws"));
  m.setManual("Mare", &man);
  uint8_t blob[PcMemory::kBlobMax];
  const size_t n = m.encode(blob, sizeof blob);
  TEST_ASSERT_TRUE(n > 3);
  PcMemory r;
  TEST_ASSERT_TRUE(r.decode(blob, n));
  TEST_ASSERT_EQUAL(3, r.count());
  for (int i = 0; i < 3; i++) {  // stesso ordine (età) e stessi valori
    TEST_ASSERT_EQUAL_STRING(m.at(i).ssid, r.at(i).ssid);
    TEST_ASSERT_TRUE(pcAddrSame(m.at(i).manual, r.at(i).manual));
    TEST_ASSERT_TRUE(pcAddrSame(m.at(i).last, r.at(i).last));
  }
  uint8_t again[PcMemory::kBlobMax];
  TEST_ASSERT_EQUAL(n, r.encode(again, sizeof again));
  TEST_ASSERT_EQUAL_MEMORY(blob, again, n);
  // corto: niente blob a metà
  TEST_ASSERT_EQUAL(0, m.encode(blob, 10));
  PcMemory e;
  TEST_ASSERT_EQUAL(3, e.encode(blob, sizeof blob));  // tabella vuota: solo l'intestazione
  TEST_ASSERT_TRUE(r.decode(blob, 3));
  TEST_ASSERT_EQUAL(0, r.count());
}

static void test_blob_worst_case_fits() {
  PcMemory m;
  const PcAddr a = addr("255.255.255.254", 65535, "/123456789012345678901234567890");  // 31 byte
  for (int i = 0; i < 5; i++) {
    char s[33];
    memset(s, 'a' + i, 32);
    s[32] = 0;
    TEST_ASSERT_TRUE(m.setLast(s, a));
    TEST_ASSERT_TRUE(m.setManual(s, &a));
  }
  uint8_t blob[PcMemory::kBlobMax];
  const size_t n = m.encode(blob, sizeof blob);
  TEST_ASSERT_EQUAL(PcMemory::kBlobMax, n);
  PcMemory r;
  TEST_ASSERT_TRUE(r.decode(blob, n));
  TEST_ASSERT_EQUAL(5, r.count());
}

static void test_blob_rejects_garbage() {
  PcMemory m;
  m.setLast("Casa", addr("192.168.1.21", 8766));
  uint8_t blob[PcMemory::kBlobMax];
  const size_t n = m.encode(blob, sizeof blob);
  PcMemory r;
  r.setLast("Vecchia", addr("1.1.1.1", 8766));
  TEST_ASSERT_FALSE(r.decode(nullptr, 0));
  TEST_ASSERT_EQUAL(0, r.count());  // non valido = tabella vuota
  TEST_ASSERT_FALSE(r.decode(blob, n - 1));  // tronco
  uint8_t bad[PcMemory::kBlobMax];
  memcpy(bad, blob, n);
  bad[0] = 'X';
  TEST_ASSERT_FALSE(r.decode(bad, n));
  memcpy(bad, blob, n);
  bad[1] = 2;  // versione sconosciuta
  TEST_ASSERT_FALSE(r.decode(bad, n));
  memcpy(bad, blob, n);
  bad[2] = 6;  // oltre il tetto
  TEST_ASSERT_FALSE(r.decode(bad, n));
  uint8_t extra[PcMemory::kBlobMax + 1];
  memcpy(extra, blob, n);
  extra[n] = 0;
  TEST_ASSERT_FALSE(r.decode(extra, n + 1));  // byte in più
  // indirizzo rovinato: «192.168.1.21:8766:/ws» → «192.168.1.21:8766:xws»
  memcpy(bad, blob, n);
  for (size_t i = 0; i + 1 < n; i++)
    if (bad[i] == ':' && bad[i + 1] == '/') bad[i + 1] = 'x';
  TEST_ASSERT_FALSE(r.decode(bad, n));
  TEST_ASSERT_TRUE(r.decode(blob, n));
  TEST_ASSERT_EQUAL(1, r.count());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_host_ok);
  RUN_TEST(test_parse_host_rejects);
  RUN_TEST(test_texts);
  RUN_TEST(test_pick_fallback);
  RUN_TEST(test_table_write_only_on_change);
  RUN_TEST(test_table_rejects_invalid);
  RUN_TEST(test_table_empty_entry_goes);
  RUN_TEST(test_table_cap_drops_oldest);
  RUN_TEST(test_table_prune_and_forget);
  RUN_TEST(test_blob_roundtrip);
  RUN_TEST(test_blob_worst_case_fits);
  RUN_TEST(test_blob_rejects_garbage);
  return UNITY_END();
}
