#include "hw/theme_fs.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/strbuf.h"
#include "core/theme_store.h"
#include "hw/avatar_fs.h"
#include "hw/logtee.h"

namespace themefs {

namespace {

const char *const kDir = "/th";
const char *const kTmp = "/th/tmp";
const char *const kOrder = "/th/order";
const char *const kOrderTmp = "/th/order.tmp";

const char *const kMount = "/littlefs";  // come avfs::begin e mountNoFormat

void pathOf(const char *id, char *out, size_t cap) { snprintf(out, cap, "/th/%s.cthm", id); }

// Stato di un file con stat POSIX, senza aprirlo: LittleFS.exists() apre il file, e con lo heap esaurito direbbe
// «manca». 1 c'è (dimensione in *n), 0 manca (solo ENOENT), -1 errore di I/O.
int fileStat(const char *path, int32_t *n) {
  char full[64];
  snprintf(full, sizeof full, "%s%s", kMount, path);
  struct stat st;
  errno = 0;
  if (::stat(full, &st) == 0) {
    if (n) *n = int32_t(st.st_size);
    return 1;
  }
  return errno == ENOENT ? 0 : -1;
}

// 0.14.1: buffer stdio da 512 invece dei ~4 KB di vfs_api (allocati alla prima lettura o scrittura); prima di ogni I/O.
File openSmall(const char *path, const char *mode) {
  File f = LittleFS.open(path, mode);
  if (f) f.setBufferSize(512);
  return f;
}

bool readAt(const char *path, uint32_t off, uint8_t *dst, uint32_t n) {
  File f = openSmall(path, "r");
  if (!f) return false;
  const bool ok = f.seek(off) && f.read(dst, n) == n;
  f.close();
  return ok;
}

class FsStorage : public cal::theme::Storage {
 public:
  int list(char (*ids)[cal::theme::kIdLen], int cap) override {
    File d = LittleFS.open(kDir);
    if (!d || !d.isDirectory()) return 0;
    int n = 0;
    for (File f = d.openNextFile(); f && n < cap; f = d.openNextFile()) {
      const char *name = f.name();
      const char *slash = strrchr(name, '/');
      if (slash) name = slash + 1;
      const size_t len = strlen(name);
      if (len > 5 && len - 5 < size_t(cal::theme::kIdLen) && !strcmp(name + len - 5, ".cthm")) {
        memcpy(ids[n], name, len - 5);
        ids[n][len - 5] = 0;
        n++;
      }
      f.close();
    }
    d.close();
    return n;
  }
  int32_t size(const char *id) override {
    char p[48];
    pathOf(id, p, sizeof p);
    int32_t n = 0;
    const int r = fileStat(p, &n);
    if (r == 0) return -1;
    return r > 0 ? n : cal::theme::kSizeIo;  // stat fallito per altro (memoria, flash): non è «manca»
  }
  bool read(const char *id, uint32_t off, uint8_t *dst, uint32_t n) override {
    char p[48];
    pathOf(id, p, sizeof p);
    return readAt(p, off, dst, n);
  }
  void remove(const char *id) override {
    char p[48];
    pathOf(id, p, sizeof p);
    if (fileStat(p, nullptr) != 1) return;  // manca o non si sa: non si tocca
    char full[64];
    snprintf(full, sizeof full, "%s%s", kMount, p);
    ::unlink(full);  // LittleFS.remove riaprirebbe il file (exists) per controllare
  }
  bool tmpBegin() override {
    if (tmp_) tmp_.close();
    tmp_ = LittleFS.open(kTmp, "w");
    // 0.14.0: buffer stdio da 512 invece dei ~4 KB di vfs_api, come /av/dl (pezzi da 512, littlefs ha la sua cache).
    // Senza prove native: hw/ non si compila su native (LittleFS vero).
    if (tmp_) tmp_.setBufferSize(512);
    return bool(tmp_);
  }
  bool tmpAppend(const uint8_t *p, uint32_t n) override { return tmp_ && tmp_.write(p, n) == n; }
  int32_t tmpSize() override {
    if (tmp_) tmp_.flush();
    if (!LittleFS.exists(kTmp)) return -1;
    File f = LittleFS.open(kTmp, "r");
    const int32_t n = f ? int32_t(f.size()) : -1;
    if (f) f.close();
    return n;
  }
  bool tmpRead(uint32_t off, uint8_t *dst, uint32_t n) override {
    if (tmp_) tmp_.flush();
    return readAt(kTmp, off, dst, n);
  }
  bool tmpCommit(const char *id) override {
    if (tmp_) tmp_.close();
    char p[48];
    pathOf(id, p, sizeof p);
    // rename diretto: littlefs sostituisce il file esistente in modo atomico. Con remove prima, fra le due chiamate il
    // tema attivo non ci sarebbe (corrente che manca lì → «Tema rovinato» e tema perso). Se fallisce: tutto o niente.
    return LittleFS.rename(kTmp, p);
  }
  void tmpDrop() override {
    if (tmp_) tmp_.close();
    if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);
  }
  int readOrder(char *dst, int cap) override {
    if (fileStat(kOrder, nullptr) != 1) return -1;
    File f = openSmall(kOrder, "r");
    if (!f) return -1;
    const int n = int(f.read(reinterpret_cast<uint8_t *>(dst), cap));
    f.close();
    return n;
  }
  bool writeOrder(const char *src, int n) override {
    File f = openSmall(kOrderTmp, "w");
    if (!f) return false;
    const bool ok = f.write(reinterpret_cast<const uint8_t *>(src), n) == size_t(n);
    f.close();
    if (ok && LittleFS.rename(kOrderTmp, kOrder)) return true;  // rename diretto, come tmpCommit
    LittleFS.remove(kOrderTmp);
    return false;
  }
  // Ripristino di fabbrica: /th resta vuota (ogni .cthm, anche oltre i 9 di list(), temporaneo e ordine).
  void eraseAll() override {
    cal::theme::dropAllThemes(*this);
    if (LittleFS.exists(kOrder)) LittleFS.remove(kOrder);
    if (LittleFS.exists(kOrderTmp)) LittleFS.remove(kOrderTmp);
  }

 private:
  File tmp_;
};

FsStorage gSt;
Preferences gPrefs;
bool gOpen = false;
SemaphoreHandle_t gLock = nullptr;  // creato in begin(), nel setup, prima che parta il task di rete
bool prefs() {
  if (!gOpen) gOpen = gPrefs.begin("calamaio-th", false);
  return gOpen;
}

}  // namespace

bool begin() {
  if (!gLock) gLock = xSemaphoreCreateMutex();
  if (!LittleFS.exists(kDir) && !LittleFS.mkdir(kDir)) {
    LogSerial.println("[temi] /th non creata: solo il tema di serie");
    return false;
  }
  if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);  // download interrotto da un riavvio
  return true;
}

bool mountNoFormat() { return LittleFS.begin(false, kMount, avfs::kMaxOpenFiles, "spiffs"); }  // come avfs::begin, senza formattare

cal::theme::Storage &storage() { return gSt; }

void lock() {
  if (gLock) xSemaphoreTake(gLock, portMAX_DELAY);
}
void unlock() {
  if (gLock) xSemaphoreGive(gLock);
}

namespace {
char gProtected[cal::theme::kIdLen] = "";
}  // namespace

void setProtected(const char *id) {
  lock();
  cal::scopy(gProtected, id ? id : "");
  unlock();
}

const char *protectedId() { return gProtected; }

bool loadCur(char *id, size_t cap) {
  if (!cap) return false;
  id[0] = 0;
  if (!prefs() || !gPrefs.isKey("cur")) return false;
  gPrefs.getString("cur", id, cap);
  return id[0] != 0;
}

void saveCur(const char *id) {
  char old[cal::theme::kIdLen];
  loadCur(old, sizeof old);
  if (!strcmp(old, id ? id : "") || !prefs()) return;  // la NVS si scrive solo se cambia
  gPrefs.putString("cur", id ? id : "");
}

void forget() {
  lock();
  gSt.eraseAll();
  unlock();
  if (prefs()) gPrefs.remove("cur");
}

}  // namespace themefs
