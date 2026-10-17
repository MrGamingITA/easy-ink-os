// Intestazione della ROM nella partizione «gbrom» (core/gb_rom): titolo, dimensione, checksum, id del gioco.
// ROM costruite qui; la ROM dell'utente (Pokémon Rosso) solo se c'è al suo percorso, mai nel repository.
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/gb_emu.h"
#include "core/gb_frame.h"
#include "core/gb_rom.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {

// Percorso della ROM di Pokémon Rosso dalla variabile d'ambiente CALAMAIO_GB_ROM; se manca la prova è saltata.
const char *userRomPath() { return getenv("CALAMAIO_GB_ROM"); }

std::vector<uint8_t> readRom(const char *path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::vector<uint8_t>();
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string s = ss.str();
  return std::vector<uint8_t>(s.begin(), s.end());
}

void fixChecksums(std::vector<uint8_t> &r) {
  uint8_t x = 0;
  for (int i = 0x134; i <= 0x14C; i++) x = uint8_t(x - r[i] - 1);
  r[0x14D] = x;
  r[0x14E] = r[0x14F] = 0;
  const uint16_t g = globalChecksum(r.data(), uint32_t(r.size()));
  r[0x14E] = uint8_t(g >> 8);
  r[0x14F] = uint8_t(g);
}

std::vector<uint8_t> makeRom(const char *title, uint8_t sizeCode, uint8_t cart, uint8_t ram) {
  std::vector<uint8_t> r(size_t(0x8000) << sizeCode, 0x00);
  memcpy(&r[0x134], title, strlen(title));
  r[0x147] = cart;
  r[0x148] = sizeCode;
  r[0x149] = ram;
  fixChecksums(r);
  return r;
}

}  // namespace

void test_parse_title_size_and_ram() {
  const std::vector<uint8_t> r = makeRom("TETRIS", 1, 0x13, 0x03);  // 64 KB, MBC3+RAM+BATTERY, 32 KB
  RomInfo info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), info));
  TEST_ASSERT_EQUAL_STRING("TETRIS", info.title);
  TEST_ASSERT_EQUAL_UINT32(0x10000, info.size);
  TEST_ASSERT_EQUAL_HEX8(0x13, info.cartType);
  TEST_ASSERT_EQUAL_UINT32(0x8000, info.ramBytes);
  TEST_ASSERT_TRUE(globalOk(r.data(), info));
}

void test_bad_header_checksum_or_empty_flash() {
  std::vector<uint8_t> r = makeRom("TETRIS", 0, 0x00, 0x00);
  RomInfo info;
  r[0x14D] ^= 1;
  TEST_ASSERT_FALSE(parseHeader(r.data(), r.size(), info));
  std::vector<uint8_t> blank(0x150, 0xFF);  // partizione cancellata
  TEST_ASSERT_FALSE(parseHeader(blank.data(), blank.size(), info));
  TEST_ASSERT_FALSE(parseHeader(r.data(), 0x100, info));  // intestazione incompleta
}

void test_size_code_limits() {
  std::vector<uint8_t> r = makeRom("BIG", 0, 0x19, 0x00);
  RomInfo info;
  r[0x148] = 0x06;  // 2 MB: entra in gbrom
  fixChecksums(r);
  TEST_ASSERT_TRUE(parseHeader(r.data(), 0x150, info));
  TEST_ASSERT_EQUAL_UINT32(0x200000, info.size);
  r[0x148] = 0x07;  // 4 MB: oltre la partizione
  fixChecksums(r);
  TEST_ASSERT_TRUE(parseHeader(r.data(), 0x150, info));
  TEST_ASSERT_FALSE(fitsPartition(info, 0x200000));
  r[0x148] = 0x52;  // codici vecchi: non supportati
  fixChecksums(r);
  TEST_ASSERT_FALSE(parseHeader(r.data(), 0x150, info));
}

void test_title_cleanup() {
  std::vector<uint8_t> r = makeRom("ZELDA  ", 0, 0x00, 0x00);
  r[0x143] = 0x80;  // flag CGB (compatibile): non fa parte del titolo
  fixChecksums(r);
  RomInfo info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), info));
  TEST_ASSERT_EQUAL_STRING("ZELDA", info.title);
}

void test_corrupt_rom_fails_global_checksum() {
  std::vector<uint8_t> r = makeRom("TETRIS", 1, 0x01, 0x00);
  RomInfo info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), info));
  r[0x9000] ^= 0x5A;  // scrittura interrotta o flash rovinata
  TEST_ASSERT_FALSE(globalOk(r.data(), info));
}

void test_game_id_from_sha256() {
  uint8_t sha[32];
  for (int i = 0; i < 32; i++) sha[i] = uint8_t(i * 17);
  char id[kIdLen];
  gameId(sha, id);
  TEST_ASSERT_EQUAL_STRING("0011223344556677", id);  // 16 cifre minuscole (contratto)
}

// La ROM dell'utente, se c'è: intestazione, id e 600 fotogrammi di emulazione senza errori, schermo non vuoto.
void test_user_rom_if_present() {
  const char *path = userRomPath();
  const std::vector<uint8_t> r = path && *path ? readRom(path) : std::vector<uint8_t>();
  if (r.empty()) TEST_IGNORE_MESSAGE("ROM dell'utente assente (CALAMAIO_GB_ROM): prova saltata");
  RomInfo info;
  TEST_ASSERT_TRUE(parseHeader(r.data(), r.size(), info));
  TEST_ASSERT_EQUAL_STRING("POKEMON RED", info.title);
  TEST_ASSERT_EQUAL_UINT32(1048576, info.size);
  TEST_ASSERT_EQUAL_UINT32(1048576, uint32_t(r.size()));
  TEST_ASSERT_EQUAL_HEX8(0x13, info.cartType);  // MBC3+RAM+BATTERY
  TEST_ASSERT_EQUAL_UINT32(0x8000, info.ramBytes);
  TEST_ASSERT_TRUE(globalOk(r.data(), info));
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), uint32_t(r.size()))));
  TEST_ASSERT_EQUAL(0x8000, int(e.ramSize()));
  static uint8_t ram[0x8000];
  memset(ram, 0xFF, sizeof ram);
  e.attachRam(ram);
  e.frameSkip(true);
  int ink = 0;
  for (int i = 0; i < 600; i++) TEST_ASSERT_TRUE(e.runFrame(i > 400 && i % 40 < 3 ? KeyStart : 0));
  for (int y = 0; y < kLcdH; y++)
    for (int x = 0; x < kLcdW; x++) ink += shadeAt(e.frame(), x, y) != 0;
  TEST_ASSERT_TRUE_MESSAGE(ink > 500, "schermo vuoto dopo 10 s di gioco");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_title_size_and_ram);
  RUN_TEST(test_bad_header_checksum_or_empty_flash);
  RUN_TEST(test_size_code_limits);
  RUN_TEST(test_title_cleanup);
  RUN_TEST(test_corrupt_rom_fails_global_checksum);
  RUN_TEST(test_game_id_from_sha256);
  RUN_TEST(test_user_rom_if_present);
  return UNITY_END();
}
