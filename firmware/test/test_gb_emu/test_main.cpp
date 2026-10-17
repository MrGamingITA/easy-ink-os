// Emulatore (core/gb_emu): ROM costruite qui (intestazione, MBC, RAM della cartuccia, schermo noto), dmg-acid2
// (test/roms, MIT) contro l'immagine di riferimento, cpu_instrs di Blargg se scaricata (tools/gb/fetch-test-roms.sh).
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/gb_emu.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {

std::vector<uint8_t> readRom(const char *path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string s = ss.str();
  return std::vector<uint8_t>(s.begin(), s.end());
}

void fixChecksum(std::vector<uint8_t> &r) {
  uint8_t x = 0;
  for (int i = 0x134; i <= 0x14C; i++) x = uint8_t(x - r[i] - 1);
  r[0x14D] = x;
}

// ROM da 32 KB: tipo di cartuccia, codice della RAM e programma da 0x150.
std::vector<uint8_t> makeRom(uint8_t cartType, uint8_t ramCode, const std::vector<uint8_t> &prog) {
  std::vector<uint8_t> r(0x8000, 0x00);
  const uint8_t entry[] = {0x00, 0xC3, 0x50, 0x01};  // NOP; JP 0x0150
  memcpy(&r[0x100], entry, sizeof entry);
  memcpy(&r[0x134], "CALAMAIO TEST", 13);
  r[0x147] = cartType;
  r[0x148] = 0x00;  // 32 KB
  r[0x149] = ramCode;
  memcpy(&r[0x150], prog.data(), prog.size());
  fixChecksum(r);
  return r;
}

// Abilita la RAM, scrive 0x42 in 0xA000, poi gira in tondo.
const std::vector<uint8_t> kRamProg = {0x3E, 0x0A, 0xEA, 0x00, 0x00, 0x3E, 0x42, 0xEA, 0x00, 0xA0, 0x18, 0xFE};

// Tessera 0 = righe FF/00 (colore 1), mappa già a zero, BGP = E4, LCD acceso: tutto lo schermo in tonalità 1.
const std::vector<uint8_t> kScreenProg = {
    0xF3,                    // DI
    0xAF, 0xE0, 0x40,        // XOR A; LDH (40),A   LCD spento
    0x21, 0x00, 0x80,        // LD HL,8000
    0x06, 0x08,              // LD B,8
    0x3E, 0xFF, 0x22,        // loop: LD A,FF; LD (HL+),A
    0x3E, 0x00, 0x22,        //       LD A,00; LD (HL+),A
    0x05, 0x20, 0xF7,        //       DEC B; JR NZ,loop
    0x3E, 0xE4, 0xE0, 0x47,  // LD A,E4; LDH (47),A  BGP
    0x3E, 0x91, 0xE0, 0x40,  // LD A,91; LDH (40),A  LCD acceso, sfondo da 8000/9800
    0x18, 0xFE,              // JR -2
};

uint32_t fnv(const uint8_t *frame) {
  uint32_t h = 2166136261u;
  for (int y = 0; y < kLcdH; y++)
    for (int x = 0; x < kLcdW; x++) {
      h ^= shadeAt(frame, x, y);
      h *= 16777619u;
    }
  return h;
}

}  // namespace

void test_bad_checksum() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, kRamProg);
  r[0x14D] ^= 0xFF;
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::BadChecksum), int(e.load(r.data(), r.size())));
  TEST_ASSERT_FALSE(e.loaded());
}

void test_too_small_and_old_rom_size_code() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, kRamProg);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::TooSmall), int(e.load(r.data(), 0x100)));
  TEST_ASSERT_EQUAL(int(Load::TooSmall), int(e.load(nullptr, 0x8000)));
  r[0x148] = 0x52;  // 72 banchi: codice vecchio, fuori dalla tabella di Peanut-GB
  fixChecksum(r);
  TEST_ASSERT_EQUAL(int(Load::Unsupported), int(e.load(r.data(), r.size())));
}

// Intestazione che promette 128 KB (MBC1, 8 banchi) ma file da 32 KB: il programma passa al banco 7, che non c'è.
// Le letture oltre la fine danno 0xFF (RST 38 all'infinito): niente letture fuori dal buffer, nessun crash.
void test_truncated_rom_reads_ff() {
  const std::vector<uint8_t> prog = {0x3E, 0x07, 0xEA, 0x00, 0x20, 0xC3, 0x00, 0x40};  // LD A,7; LD (2000),A; JP 4000
  std::vector<uint8_t> r = makeRom(0x01, 0x00, prog);
  r[0x148] = 0x02;  // 128 KB
  fixChecksum(r);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL_HEX8(0xFF, e.romRead(7 * 0x4000));
  for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(e.runFrame(0));
}

void test_unsupported_mbc() {
  Emu e;
  std::vector<uint8_t> r = makeRom(0xFC, 0x00, kRamProg);  // Pocket Camera
  TEST_ASSERT_EQUAL(int(Load::Unsupported), int(e.load(r.data(), r.size())));
  r = makeRom(0x22, 0x00, kRamProg);  // MBC7
  TEST_ASSERT_EQUAL(int(Load::Unsupported), int(e.load(r.data(), r.size())));
}

void test_ram_limits() {
  Emu e;
  std::vector<uint8_t> r = makeRom(0x1B, 0x03, kRamProg);  // MBC5+RAM+BATTERY, 32 KB
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0x8000, int(e.ramSize()));
  r = makeRom(0x1B, 0x04, kRamProg);  // 128 KB
  TEST_ASSERT_EQUAL(int(Load::RamTooBig), int(e.load(r.data(), r.size())));
  r = makeRom(0x1B, 0x05, kRamProg);  // 64 KB
  TEST_ASSERT_EQUAL(int(Load::RamTooBig), int(e.load(r.data(), r.size())));
  r = makeRom(0x06, 0x00, kRamProg);  // MBC2: 512 mezzi byte
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0x200, int(e.ramSize()));
  r = makeRom(0x00, 0x00, kRamProg);  // solo ROM
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0, int(e.ramSize()));
}

void test_cart_ram_write_marks_dirty() {
  std::vector<uint8_t> r = makeRom(0x03, 0x02, kRamProg);  // MBC1+RAM+BATTERY, 8 KB
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(0x2000, int(e.ramSize()));
  std::vector<uint8_t> ram(e.ramSize(), 0xFF);
  e.attachRam(ram.data());
  TEST_ASSERT_FALSE(e.ramDirty());
  for (int i = 0; i < 3; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_EQUAL_HEX8(0x42, ram[0]);
  TEST_ASSERT_TRUE(e.ramDirty());
  e.clearDirty();
  TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_FALSE(e.ramDirty());  // stesso valore riscritto: niente da salvare
}

// RAM della cartuccia in pezzi da 2 KB separati (sulla scheda: niente blocco da 32 KB contiguo nello heap).
void test_cart_ram_in_banks() {
  std::vector<uint8_t> r = makeRom(0x1B, 0x03, kRamProg);  // MBC5+RAM+BATTERY, 32 KB
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(16, e.ramBanks());
  static uint8_t b[16][0x800];
  memset(b, 0xFF, sizeof b);
  for (int i = 0; i < 16; i++) e.attachBank(i, b[i]);
  b[9][5] = 0x77;
  TEST_ASSERT_EQUAL_HEX8(0x77, e.ramRead(9 * 0x800 + 5));
  e.ramWrite(15 * 0x800 + 0x7FF, 0x12);
  TEST_ASSERT_EQUAL_HEX8(0x12, b[15][0x7FF]);
  TEST_ASSERT_TRUE(e.ramDirty());
  TEST_ASSERT_EQUAL_HEX8(0xFF, e.ramRead(0x8000));  // oltre la RAM dichiarata
  r = makeRom(0x06, 0x00, kRamProg);  // MBC2: 512 mezzi byte, un banco
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_EQUAL(1, e.ramBanks());
}

// Fotogrammi disegnati: con il salto (frameSkip) uno su due; il loop compone il pannello solo quando cambia.
void test_drawn_frames_counter() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, kScreenProg);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  const uint32_t all = e.frames();
  TEST_ASSERT_TRUE(all >= 8);
  e.frameSkip(true);
  for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_EQUAL_UINT32(all + 5, e.frames());
}

// Scritture vere nella RAM della cartuccia (valore cambiato): il salvataggio automatico aspetta che smettano.
void test_ram_write_counter() {
  std::vector<uint8_t> r = makeRom(0x03, 0x02, kRamProg);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  std::vector<uint8_t> ram(e.ramSize(), 0xFF);
  e.attachRam(ram.data());
  TEST_ASSERT_EQUAL_UINT32(0, e.ramWrites());
  e.ramWrite(1, 0x10);
  e.ramWrite(1, 0x10);  // stesso valore: non conta
  e.ramWrite(2, 0x11);
  TEST_ASSERT_EQUAL_UINT32(2, e.ramWrites());
  e.clearDirty();
  TEST_ASSERT_EQUAL_UINT32(2, e.ramWrites());  // il contatore non si azzera col salvataggio
}

void test_known_screen() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, kScreenProg);
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  for (int y = 0; y < kLcdH; y++)
    for (int x = 0; x < kLcdW; x++)
      if (shadeAt(e.frame(), x, y) != 1) TEST_FAIL_MESSAGE("pixel diverso dalla tonalità 1");
  char t[17];
  e.title(t);
  TEST_ASSERT_EQUAL_STRING("CALAMAIO TEST", t);
}

void test_invalid_opcode_stops() {
  std::vector<uint8_t> r = makeRom(0x00, 0x00, {0xD3});  // codice inesistente sul Game Boy
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  TEST_ASSERT_FALSE(e.runFrame(0));
  TEST_ASSERT_FALSE(e.runFrame(0));  // resta fermo
}

// dmg-acid2 (Matt Currie, MIT): al fotogramma 60 lo schermo è identico a reference-dmg.png (verificato
// pixel per pixel il 2026-10-12 con Peanut-GB d0bcca7). FNV-1a 32 delle tonalità 0–3, riga per riga.
void test_dmg_acid2_frame_hash() {
  const std::vector<uint8_t> r = readRom("test/roms/dmg-acid2.gb");
  TEST_ASSERT_EQUAL_MESSAGE(32768, int(r.size()), "manca test/roms/dmg-acid2.gb");
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  for (int i = 0; i < 60; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_EQUAL_HEX32(0xb312e6f6u, fnv(e.frame()));
  for (int i = 0; i < 60; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_EQUAL_HEX32(0xb312e6f6u, fnv(e.frame()));  // immagine ferma: deterministico
}

// cpu_instrs di Blargg (senza licenza esplicita: non nel repository). La scarica tools/gb/fetch-test-roms.sh.
std::string gSerial;
void test_blargg_cpu_instrs() {
  const std::vector<uint8_t> r = readRom("test/roms/.fetched/cpu_instrs.gb");
  if (r.empty()) TEST_IGNORE_MESSAGE("cpu_instrs.gb assente: tools/gb/fetch-test-roms.sh");
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(r.data(), r.size())));
  gSerial.clear();
  e.onSerial([](uint8_t c) { gSerial.push_back(char(c)); });
  for (int i = 0; i < 6000 && gSerial.find("Passed all tests") == std::string::npos && gSerial.find("Failed") == std::string::npos; i++)
    TEST_ASSERT_TRUE(e.runFrame(0));
  TEST_ASSERT_TRUE_MESSAGE(gSerial.find("Passed all tests") != std::string::npos, gSerial.c_str());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_bad_checksum);
  RUN_TEST(test_too_small_and_old_rom_size_code);
  RUN_TEST(test_truncated_rom_reads_ff);
  RUN_TEST(test_unsupported_mbc);
  RUN_TEST(test_ram_limits);
  RUN_TEST(test_cart_ram_write_marks_dirty);
  RUN_TEST(test_cart_ram_in_banks);
  RUN_TEST(test_drawn_frames_counter);
  RUN_TEST(test_ram_write_counter);
  RUN_TEST(test_known_screen);
  RUN_TEST(test_invalid_opcode_stops);
  RUN_TEST(test_dmg_acid2_frame_hash);
  RUN_TEST(test_blargg_cpu_instrs);
  return UNITY_END();
}
