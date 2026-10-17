#include "hw/avatar_fs.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <string.h>

#include "core/strbuf.h"
#include "hw/logtee.h"

namespace avfs {

namespace {

const char *const kDir = "/av";
const char *const kIndex = "/av/index";
const char *const kTmp = "/av/tmp";
const char *const kDl = "/av/dl";  // 0.14.0 (R4): avatar scaricato a pezzi dal task di rete, prima di adoptTemp
const char *const kAccDir = "/acc";  // 0.10.0: accessori ricevuti
const char *const kAncDir = "/anc";  // 0.10.0: ancore ricevute
const char *const kPet = "/av/pet";  // 0.10.0: ultimo riassunto del gioco

// 0.14.1: buffer stdio da 512 invece dei ~4 KB di vfs_api (allocati alla prima lettura o scrittura); prima di ogni I/O.
File openSmall(const char *path, const char *mode) {
  File f = LittleFS.open(path, mode);
  if (f) f.setBufferSize(512);
  return f;
}

// Tutto o niente: file temporaneo, poi rinomina sul nome vero.
bool writeAtomic(const char *path, const uint8_t *data, int n) {
  File f = openSmall(kTmp, "w");
  if (!f) return false;
  const bool ok = f.write(data, n) == size_t(n);
  f.close();
  if (!ok) { LittleFS.remove(kTmp); return false; }
  if (LittleFS.exists(path)) LittleFS.remove(path);
  return LittleFS.rename(kTmp, path);
}

void slotPath(int slot, char *out, size_t cap) { snprintf(out, cap, "/av/%d.cavt", slot); }

class LittleFsStorage : public cal::AvatarStorage {
 public:
  int slotSize(int slot) override {
    char p[16];
    slotPath(slot, p, sizeof p);
    if (!LittleFS.exists(p)) return -1;
    File f = LittleFS.open(p, "r");
    if (!f) return -1;
    const int n = int(f.size());
    f.close();
    return n;
  }
  bool readSlot(int slot, uint32_t off, uint8_t *dst, uint16_t n) override {
    char p[16];
    slotPath(slot, p, sizeof p);
    File f = openSmall(p, "r");
    if (!f) return false;
    const bool ok = f.seek(off) && f.read(dst, n) == n;
    f.close();
    return ok;
  }
  bool writeSlot(int slot, const uint8_t *data, uint16_t n) override {
    char p[16];
    slotPath(slot, p, sizeof p);
    return writeFile(p, data, n);
  }
  void eraseSlot(int slot) override {
    char p[16];
    slotPath(slot, p, sizeof p);
    if (LittleFS.exists(p)) LittleFS.remove(p);
  }
  int readIndex(uint8_t *dst, int cap) override {
    if (!LittleFS.exists(kIndex)) return -1;
    File f = openSmall(kIndex, "r");
    if (!f) return -1;
    const int n = int(f.read(dst, cap));
    f.close();
    return n;
  }
  bool writeIndex(const uint8_t *data, int n) override { return writeFile(kIndex, data, n); }
  void eraseAll() override {
    for (int i = 0; i < cal::AvatarCache::kSlots; i++) eraseSlot(i);
    if (LittleFS.exists(kIndex)) LittleFS.remove(kIndex);
    if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);
    tempAbort();
  }
  // 0.14.0 (R4): download a pezzi nel task di rete (AvatarDownload) in /av/dl; writeSlot e writeIndex usano /av/tmp
  // dal loop, mai lo stesso file. adoptTemp (loop, AvatarCache::adopt) rinomina /av/dl sul posto: rinomina diretta
  // di littlefs sopra il file vecchio, come i temi. Il task di rete e il loop non lo toccano mai insieme (net.cpp).
  bool tempBegin() override {
    tempAbort();
    dl_ = LittleFS.open(kDl, "w");
    // Buffer stdio da 512 invece dei ~4 KB di vfs_api (setvbuf alla prima write, tenuti per tutto il download): i pezzi
    // sono già da 512 e littlefs ha la sua cache. Senza prove native: hw/ non si compila su native (LittleFS vero).
    if (dl_) dl_.setBufferSize(512);
    return bool(dl_);
  }
  bool tempAppend(const uint8_t *p, uint16_t n) override { return dl_ && dl_.write(p, n) == n; }
  bool tempEnd() override {
    if (!dl_) return false;
    dl_.close();
    return LittleFS.exists(kDl);
  }
  void tempAbort() override {
    if (dl_) dl_.close();
    if (LittleFS.exists(kDl)) LittleFS.remove(kDl);
  }
  // len = byte attesi (hdr.len): il temporaneo deve averne proprio tanti, se no il posto resta com'era.
  bool adoptTemp(int slot, uint32_t len) override {
    if (slot < 0 || slot >= cal::AvatarCache::kSlots || !LittleFS.exists(kDl)) return false;
    File f = LittleFS.open(kDl, "r");
    if (!f) return false;
    const uint32_t n = uint32_t(f.size());
    f.close();
    if (n != len) return false;
    char p[16];
    slotPath(slot, p, sizeof p);
    return LittleFS.rename(kDl, p);
  }

 private:
  File dl_;
  // Tutto o niente: file temporaneo, poi rinomina sul nome vero.
  static bool writeFile(const char *path, const uint8_t *data, int n) {
    File f = openSmall(kTmp, "w");
    if (!f) return false;
    const bool ok = f.write(data, n) == size_t(n);
    f.close();
    if (!ok) { LittleFS.remove(kTmp); return false; }
    if (LittleFS.exists(path)) LittleFS.remove(path);
    return LittleFS.rename(kTmp, path);
  }
};

// 0.10.0: un file per id in /acc e /anc. Il nome del file è l'id (kebab-case, ≤ 24 byte: sta nei 31 di LittleFS).
class LittleFsAccStorage : public cal::AccStorage {
 public:
  int read(Kind k, const char *id, uint8_t *dst, int cap) override {
    char p[48];
    path(k, id, p, sizeof p);
    if (!LittleFS.exists(p)) return -1;
    File f = openSmall(p, "r");
    if (!f) return -1;
    const int n = int(f.size());
    const bool ok = n <= cap && f.read(dst, n) == size_t(n);
    f.close();
    return ok ? n : -1;
  }
  bool write(Kind k, const char *id, const uint8_t *data, int n) override {
    char p[48];
    path(k, id, p, sizeof p);
    return writeAtomic(p, data, n);
  }
  void remove(Kind k, const char *id) override {
    char p[48];
    path(k, id, p, sizeof p);
    if (LittleFS.exists(p)) LittleFS.remove(p);
  }
  int list(Kind k, char (*ids)[cal::kAvatarRefLen], int cap) override {
    File d = LittleFS.open(k == Acc ? kAccDir : kAncDir);
    if (!d || !d.isDirectory()) return 0;
    const char *ext = k == Acc ? ".cacc" : ".canc";
    int n = 0;
    for (File f = d.openNextFile(); f && n < cap; f = d.openNextFile()) {
      const char *name = f.name();  // solo il nome (core 2.0.x)
      const char *slash = strrchr(name, '/');
      if (slash) name = slash + 1;
      const size_t len = strlen(name);
      if (len > 5 && len - 5 < size_t(cal::kAvatarRefLen) && strcmp(name + len - 5, ext) == 0) {
        memcpy(ids[n], name, len - 5);
        ids[n][len - 5] = 0;
        n++;
      }
      f.close();
    }
    d.close();
    return n;
  }
  void eraseAll() override {
    for (Kind k : {Acc, Anc}) {
      char ids[24][cal::kAvatarRefLen];
      const int n = list(k, ids, 24);
      for (int i = 0; i < n; i++) remove(k, ids[i]);
    }
  }

 private:
  static void path(Kind k, const char *id, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s%s", k == Acc ? kAccDir : kAncDir, id, k == Acc ? ".cacc" : ".canc");
  }
};

LittleFsStorage gStorage;
LittleFsAccStorage gAccStorage;
Preferences gPrefs;
bool gPrefsOpen = false;

bool prefs() {
  if (!gPrefsOpen) gPrefsOpen = gPrefs.begin("calamaio-av", false);
  return gPrefsOpen;
}

}  // namespace

bool begin() {
  const uint32_t t0 = millis();
  // Partizione «spiffs» di default_8MB.csv; se non contiene un LittleFS valido la si formatta.
  if (!LittleFS.begin(true, "/littlefs", kMaxOpenFiles, "spiffs")) {
    LogSerial.println("[avatar] LittleFS non montato: solo gli avatar predefiniti");
    return false;
  }
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  if (!LittleFS.exists(kAccDir)) LittleFS.mkdir(kAccDir);
  if (!LittleFS.exists(kAncDir)) LittleFS.mkdir(kAncDir);
  if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);  // scrittura interrotta da un riavvio
  if (LittleFS.exists(kDl)) LittleFS.remove(kDl);    // 0.14.0: download interrotto da un riavvio
  LogSerial.printf("[avatar] LittleFS: %u/%u byte usati, montato in %lu ms\n", unsigned(LittleFS.usedBytes()),
                unsigned(LittleFS.totalBytes()), (unsigned long)(millis() - t0));
  return true;
}

cal::AvatarStorage &storage() { return gStorage; }
cal::AccStorage &accStorage() { return gAccStorage; }

int loadPet(uint8_t *dst, int cap) {
  if (!LittleFS.exists(kPet)) return -1;
  File f = openSmall(kPet, "r");
  if (!f) return -1;
  const int n = int(f.size());
  const bool ok = n <= cap && f.read(dst, n) == size_t(n);
  f.close();
  return ok ? n : -1;
}

bool savePet(const uint8_t *data, size_t n) { return writeAtomic(kPet, data, int(n)); }

void forgetPet() {
  if (LittleFS.exists(kPet)) LittleFS.remove(kPet);
}

bool loadDefault(cal::AvatarRef &out) {
  if (!prefs() || !gPrefs.isKey("def")) return false;
  uint8_t b[cal::kAvatarRefLen + 5];
  if (gPrefs.getBytes("def", b, sizeof b) != sizeof b) return false;
  memset(&out, 0, sizeof out);
  memcpy(out.id, b, cal::kAvatarRefLen);
  out.id[cal::kAvatarRefLen - 1] = 0;
  out.crc = uint32_t(b[25]) | uint32_t(b[26]) << 8 | uint32_t(b[27]) << 16 | uint32_t(b[28]) << 24;
  out.hasCrc = b[29] != 0;
  return cal::validAvatarId(out.id);
}

void saveDefault(const cal::AvatarRef &r) {
  if (!prefs()) return;
  uint8_t b[cal::kAvatarRefLen + 5] = {};
  memcpy(b, r.id, cal::kAvatarRefLen);
  b[25] = r.crc;
  b[26] = r.crc >> 8;
  b[27] = r.crc >> 16;
  b[28] = r.crc >> 24;
  b[29] = r.hasCrc;
  gPrefs.putBytes("def", b, sizeof b);
}

void forgetDefault() {
  if (prefs()) gPrefs.remove("def");
}

}  // namespace avfs
