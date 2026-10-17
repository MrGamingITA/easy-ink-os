// Salvataggi della cartuccia (core/gb_save): CRC-32 a pezzi del file, coda di controllo, salvataggio automatico.
#include <string.h>
#include <unity.h>

#include "core/gb_save.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

void test_crc32_known_vector_and_in_pieces() {
  const char *s = "123456789";
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Finish(crc32Update(kCrcInit, reinterpret_cast<const uint8_t *>(s), 9)));
  uint32_t c = kCrcInit;
  c = crc32Update(c, reinterpret_cast<const uint8_t *>(s), 4);
  c = crc32Update(c, reinterpret_cast<const uint8_t *>(s) + 4, 5);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Finish(c));
}

void test_trailer_round_trip() {
  uint8_t t[kTrailerLen];
  packTrailer(0xA1B2C3D4u, t);
  TEST_ASSERT_EQUAL_HEX32(0xA1B2C3D4u, unpackTrailer(t));
  TEST_ASSERT_EQUAL(4, int(kTrailerLen));
  TEST_ASSERT_EQUAL_UINT32(0x8000 + 4, uint32_t(fileLen(0x8000)));
}

// Salvataggio automatico: 3 s dopo l'ultima scrittura vera; il primo subito (nessuna attesa di un minuto
// dall'avvio), poi al più uno al minuto.
void test_autosave_waits_for_quiet() {
  AutoSave a;
  a.start(1000, 0);
  TEST_ASSERT_FALSE(a.due(2000, 0, false));  // niente di cambiato
  TEST_ASSERT_FALSE(a.due(5000, 3, true));   // il gioco sta scrivendo
  TEST_ASSERT_FALSE(a.due(7000, 9, true));   // scrive ancora: si riparte da qui
  TEST_ASSERT_FALSE(a.due(9999, 9, true));
  TEST_ASSERT_TRUE(a.due(10000, 9, true));   // 3 s di calma: il primo, subito
  a.saved(10000);
  TEST_ASSERT_FALSE(a.due(20000, 12, true));
  TEST_ASSERT_FALSE(a.due(30000, 12, true));  // calmo, ma meno di un minuto dal precedente
  TEST_ASSERT_TRUE(a.due(70000, 12, true));
}

void test_autosave_failure_retries_later() {
  AutoSave a;
  a.start(0, 0);
  TEST_ASSERT_FALSE(a.due(100, 1, true));
  TEST_ASSERT_TRUE(a.due(3100, 1, true));
  a.failed(3100);  // flash piena o errore: niente raffica di tentativi
  TEST_ASSERT_FALSE(a.due(4000, 1, true));
  TEST_ASSERT_TRUE(a.due(63100, 1, true));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_known_vector_and_in_pieces);
  RUN_TEST(test_trailer_round_trip);
  RUN_TEST(test_autosave_waits_for_quiet);
  RUN_TEST(test_autosave_failure_retries_later);
  return UNITY_END();
}
