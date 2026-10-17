// Tabella delle partizioni dei giochi (firmware/partitions_gb.csv) contro il contratto dei piani giochi e contro la
// tabella di oggi (default_8MB.csv del core 2.0.17): NVS e coredump identici, niente sovrapposizioni, allineamenti.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

void setUp() {}
void tearDown() {}

namespace {

struct Row {
  std::string name, type, subtype;
  uint32_t offset, size;
};

std::string trim(const std::string &s) {
  const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<Row> readCsv(const char *path) {
  std::vector<Row> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> c;
    size_t at = 0;
    for (;;) {
      const size_t k = line.find(',', at);
      c.push_back(trim(line.substr(at, k == std::string::npos ? std::string::npos : k - at)));
      if (k == std::string::npos) break;
      at = k + 1;
    }
    TEST_ASSERT_TRUE_MESSAGE(c.size() >= 5, line.c_str());
    out.push_back(Row{c[0], c[1], c[2], uint32_t(strtoul(c[3].c_str(), nullptr, 0)), uint32_t(strtoul(c[4].c_str(), nullptr, 0))});
  }
  return out;
}

const Row *find(const std::vector<Row> &t, const char *name) {
  for (const Row &r : t)
    if (r.name == name) return &r;
  return nullptr;
}

}  // namespace

void test_matches_contract() {
  const std::vector<Row> t = readCsv("partitions_gb.csv");
  const Row want[] = {
      {"nvs", "data", "nvs", 0x9000, 0x5000},          {"otadata", "data", "ota", 0xe000, 0x2000},
      {"app0", "app", "ota_0", 0x10000, 0x240000},     {"app1", "app", "ota_1", 0x250000, 0x240000},
      {"gbrom", "data", "0x40", 0x490000, 0x200000},   {"spiffs", "data", "spiffs", 0x690000, 0x160000},
      {"coredump", "data", "coredump", 0x7f0000, 0x10000},
  };
  TEST_ASSERT_EQUAL(7, int(t.size()));
  for (size_t i = 0; i < 7; i++) {
    TEST_ASSERT_EQUAL_STRING(want[i].name.c_str(), t[i].name.c_str());
    TEST_ASSERT_EQUAL_STRING(want[i].type.c_str(), t[i].type.c_str());
    TEST_ASSERT_EQUAL_STRING(want[i].subtype.c_str(), t[i].subtype.c_str());
    TEST_ASSERT_EQUAL_HEX32(want[i].offset, t[i].offset);
    TEST_ASSERT_EQUAL_HEX32(want[i].size, t[i].size);
  }
}

void test_layout_is_sound() {
  const std::vector<Row> t = readCsv("partitions_gb.csv");
  for (size_t i = 0; i + 1 < t.size(); i++)
    TEST_ASSERT_TRUE_MESSAGE(t[i].offset + t[i].size <= t[i + 1].offset, t[i].name.c_str());  // niente sovrapposizioni
  const Row &last = t.back();
  TEST_ASSERT_TRUE(last.offset + last.size <= 0x800000);  // 8 MB
  for (const Row &r : t) {
    TEST_ASSERT_EQUAL_MESSAGE(0, r.offset % 0x1000, r.name.c_str());  // settori da 4 KB
    if (r.type == "app") TEST_ASSERT_EQUAL_MESSAGE(0, r.offset % 0x10000, r.name.c_str());  // app a 64 KB
  }
  TEST_ASSERT_EQUAL(0, int(find(t, "gbrom")->offset % 0x10000));  // mappata a pagine MMU da 64 KB
}

// NVS e coredump come in default_8MB.csv: chiavi, abbinamento, reti e rapporti di crash restano dove sono.
void test_nvs_and_coredump_unchanged() {
  const std::vector<Row> t = readCsv("partitions_gb.csv");
  TEST_ASSERT_EQUAL_HEX32(0x9000, find(t, "nvs")->offset);
  TEST_ASSERT_EQUAL_HEX32(0x5000, find(t, "nvs")->size);
  TEST_ASSERT_EQUAL_HEX32(0xe000, find(t, "otadata")->offset);
  TEST_ASSERT_EQUAL_HEX32(0x7f0000, find(t, "coredump")->offset);
  TEST_ASSERT_EQUAL_HEX32(0x10000, find(t, "coredump")->size);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_matches_contract);
  RUN_TEST(test_layout_is_sound);
  RUN_TEST(test_nvs_and_coredump_unchanged);
  return UNITY_END();
}
