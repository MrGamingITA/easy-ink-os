#include "hw/ota_hw.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <string.h>

#include <atomic>

#include "core/strbuf.h"
#include "hw/logtee.h"

// Il core Arduino 2.0.17 (esp32-hal-misc.c, initArduino) conferma da solo una versione «in prova» appena parte,
// a meno che verifyRollbackLater() (debole, C) dica di aspettare: qui si conferma solo dopo Wi-Fi + auth-ok.
extern "C" bool verifyRollbackLater() { return true; }

namespace otahw {
namespace {

using cal::ota::ImgState;

const esp_partition_t *nextPart() { return esp_ota_get_next_update_partition(nullptr); }

class EspFlash : public cal::ota::Flash {
 public:
  bool begin(uint32_t size, char *why, size_t cap) override {
    part_ = nextPart();
    if (!part_) { cal::scopy(why, cap, "nessuna partizione OTA libera"); return false; }
    if (size > part_->size) { cal::scopy(why, cap, "immagine più grande della partizione"); return false; }
    // OTA_WITH_SEQUENTIAL_WRITES: si cancella un settore alla volta mentre si scrive, niente blocco iniziale di secondi.
    const esp_err_t e = esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &h_);
    if (e != ESP_OK) { cal::Sb(why, cap).s("esp_ota_begin: ").s(esp_err_to_name(e)); return false; }
    open_ = true;
    LogSerial.printf("[ota] scrivo in %s (0x%06lx, %lu byte)\n", part_->label, (unsigned long)part_->address, (unsigned long)part_->size);
    return true;
  }
  bool write(const uint8_t *d, size_t n) override {
    const esp_err_t e = esp_ota_write(h_, d, n);
    if (e != ESP_OK) LogSerial.printf("[ota] esp_ota_write: %s\n", esp_err_to_name(e));
    return e == ESP_OK;
  }
  bool end(char *why, size_t cap) override {
    open_ = false;
    const esp_err_t e = esp_ota_end(h_);  // valida l'immagine (intestazione, segmenti, SHA-256 in coda)
    if (e != ESP_OK) { cal::scopy(why, cap, esp_err_to_name(e)); return false; }
    return true;
  }
  void abort() override {
    if (open_) esp_ota_abort(h_);
    open_ = false;
  }
  bool setBoot() override {
    const esp_err_t e = esp_ota_set_boot_partition(part_);
    if (e != ESP_OK) LogSerial.printf("[ota] esp_ota_set_boot_partition: %s\n", esp_err_to_name(e));
    return e == ESP_OK;
  }

 private:
  const esp_partition_t *part_ = nullptr;
  esp_ota_handle_t h_ = 0;
  bool open_ = false;
};

// HTTP/1.0: niente «chunked», quindi lo stream è l'immagine così com'è (Content-Length o fino alla chiusura).
class HttpSource : public cal::ota::Source {
 public:
  bool open(const char *url, char *why, size_t cap) override {
    http_.useHTTP10(true);
    http_.setReuse(false);
    http_.setConnectTimeout(5000);
    http_.setTimeout(5000);
    if (!http_.begin(client_, url)) { cal::scopy(why, cap, "indirizzo dell'immagine non valido"); return false; }
    const int code = http_.GET();
    if (code != 200) {
      char t[64];
      if (code < 0) snprintf(t, sizeof t, "download non riuscito (%s)", HTTPClient::errorToString(code).c_str());
      else snprintf(t, sizeof t, "download non riuscito (HTTP %d)", code);
      cal::scopy(why, cap, t);
      http_.end();
      return false;
    }
    stream_ = http_.getStreamPtr();
    LogSerial.printf("[ota] GET (link monouso): 200, %d byte annunciati\n", http_.getSize());
    return stream_ != nullptr;
  }
  int read(uint8_t *buf, size_t cap) override {
    if (!stream_) return -1;
    const int av = stream_->available();
    if (av > 0) return stream_->read(buf, size_t(av) < cap ? size_t(av) : cap);
    return stream_->connected() ? 0 : -1;
  }
  void close() override {
    stream_ = nullptr;
    http_.end();
  }

 private:
  WiFiClient client_;
  HTTPClient http_;
  WiFiClient *stream_ = nullptr;
};

EspFlash gFlash;
HttpSource gSource;
cal::ota::Trial gTrial;
std::atomic<uint8_t> gTrialState{0};  // 0 = nessuna prova, 1 = in prova, 2 = confermata, 3 = ritorno
char gRunLabel[17] = "";

// Crash
bool gCrashOk = false;
cal::crash::Report gCrash;

}  // namespace

uint32_t maxImageSize() {
  const esp_partition_t *p = nextPart();
  return p ? p->size : 0;
}
const char *nextLabel() {
  const esp_partition_t *p = nextPart();
  return p ? p->label : "";
}
const char *runningLabel() {
  if (!gRunLabel[0]) {
    const esp_partition_t *p = esp_ota_get_running_partition();
    cal::scopy(gRunLabel, p ? p->label : "");
  }
  return gRunLabel;
}

ImgState runningState() {
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK) return ImgState::Unknown;
  switch (st) {
    case ESP_OTA_IMG_NEW: return ImgState::New;
    case ESP_OTA_IMG_PENDING_VERIFY: return ImgState::PendingVerify;
    case ESP_OTA_IMG_VALID: return ImgState::Valid;
    case ESP_OTA_IMG_INVALID: return ImgState::Invalid;
    case ESP_OTA_IMG_ABORTED: return ImgState::Aborted;
    default: return ImgState::Undefined;
  }
}

cal::ota::Flash &flash() { return gFlash; }
cal::ota::Source &source() { return gSource; }

bool loadRecord(cal::ota::Record &r) {
  Preferences p;
  if (!p.begin("calamaio-ota", true)) return false;
  uint8_t b[cal::ota::kRecordLen];
  const size_t n = p.isKey("rec") ? p.getBytes("rec", b, sizeof b) : 0;
  p.end();
  return n && cal::ota::unpackRecord(b, n, r);
}

bool saveRecord(const cal::ota::Record &r) {
  uint8_t b[cal::ota::kRecordLen];
  const size_t n = cal::ota::packRecord(r, b, sizeof b);
  Preferences p;
  if (!n || !p.begin("calamaio-ota", false)) return false;
  const bool ok = p.putBytes("rec", b, n) == n;
  p.end();
  return ok;
}

void clearRecord() {
  Preferences p;
  if (!p.begin("calamaio-ota", false)) return;
  p.remove("rec");
  p.end();
}

cal::ota::BootCase boot(cal::ota::Record &rec, bool &hasRec) {
  hasRec = loadRecord(rec);
  const ImgState st = runningState();
  const cal::ota::BootCase c = cal::ota::bootCase(hasRec, rec, runningLabel(), st);
  if (c == cal::ota::BootCase::Trial) {
    gTrial.begin(cal::Ms(millis()));
    gTrialState = 1;
  }
  static const char *const kCase[] = {"normale", "nuova versione in prova: si conferma con Wi-Fi + auth-ok entro 120 s",
                                      "nuova versione già confermata", "tornata la versione di prima (rollback)"};
  LogSerial.printf("[ota] avvio da %s: %s%s%s\n", runningLabel(), kCase[int(c)], hasRec ? " · aggiornamento " : "", hasRec ? rec.id : "");
  return c;
}

bool trialActive() { return gTrialState.load() == 1; }

bool confirm() {
  uint8_t want = 1;
  if (!gTrialState.compare_exchange_strong(want, 2)) return false;
  gTrial.onAuthed(cal::Ms(millis()));
  const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  LogSerial.printf("[ota] nuova versione confermata (%s)\n", e == ESP_OK ? "ok" : esp_err_to_name(e));
  return true;
}

void checkTrial() {
  if (gTrialState.load() != 1) return;
  if (gTrial.tick(cal::Ms(millis())) != cal::ota::Trial::Act::Rollback) return;
  uint8_t want = 1;
  if (!gTrialState.compare_exchange_strong(want, 3)) return;
  LogSerial.println("[ota] 120 s senza Wi-Fi e autenticazione: torno alla versione di prima");
  Serial.flush();
  esp_ota_mark_app_invalid_rollback_and_reboot();
}

void captureCrash(const char *fw) {
  cal::crash::Summary core;
  memset(&core, 0, sizeof core);
  core.cause = 0xFFFFFFFFu;
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
  if (esp_core_dump_image_check() == ESP_OK) {
    esp_core_dump_summary_t *s = static_cast<esp_core_dump_summary_t *>(malloc(sizeof(esp_core_dump_summary_t)));
    if (s && esp_core_dump_get_summary(s) == ESP_OK) {
      core.has = true;
      cal::scopy(core.task, s->exc_task);
      core.pc = s->exc_pc;
      core.cause = s->ex_info.exc_cause;
      core.nBt = int(s->exc_bt_info.depth) < cal::crash::kBtMax ? int(s->exc_bt_info.depth) : cal::crash::kBtMax;
      for (int i = 0; i < core.nBt; i++) core.bt[i] = s->exc_bt_info.bt[i];
      core.btCorrupt = s->exc_bt_info.corrupted;
    }
    free(s);
  }
#endif
  const cal::crash::Reset r = static_cast<cal::crash::Reset>(int(esp_reset_reason()));
  char elf[65] = "";
  esp_ota_get_app_elf_sha256(elf, sizeof elf);  // impronta della build che gira: va nel rapporto solo se è lei la caduta
  gCrashOk = cal::crash::build(r, core, logtee::previousTail(), fw, elf, gCrash);
  if (gCrashOk) LogSerial.printf("[crash] rapporto pronto: fw %s, %s%s\n", gCrash.fw, gCrash.reason, core.has ? " (coredump in flash)" : "");
}

size_t crashJson(char *out, size_t cap) { return gCrashOk ? cal::crash::encode(gCrash, out, cap) : 0; }
bool crashPending() { return gCrashOk; }

void crashSent() {
  gCrashOk = false;
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
  if (esp_core_dump_image_check() == ESP_OK) esp_core_dump_image_erase();
#endif
  LogSerial.println("[crash] rapporto mandato al servizio, coredump cancellato");
}

}  // namespace otahw
