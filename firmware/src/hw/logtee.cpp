#include "hw/logtee.h"

#include "core/strbuf.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <string.h>

LogTee LogSerial;

namespace logtee {
namespace {

cal::LogRing gRing;                              // 16 KB in RAM
RTC_NOINIT_ATTR cal::crash::Tail gTail;          // ultime righe, sopravvive a panic e watchdog
cal::crash::Tail gPrev;                          // copia della coda dell'avvio precedente
bool gPrevOk = false;
SemaphoreHandle_t gMx = nullptr;
StaticSemaphore_t gMxBuf;
vprintf_like_t gIdfPrintf = nullptr;

void toTail(const char *line, size_t n, void *) { cal::crash::tailAppend(gTail, line, n); }

// Senza lucchetto (prima di begin, o da un contesto che non può aspettare) la riga va solo in seriale.
bool lock(TickType_t wait) { return gMx && xSemaphoreTake(gMx, wait) == pdTRUE; }
void unlock() { xSemaphoreGive(gMx); }

void feed(const char *d, size_t n) {
  if (xPortInIsrContext() || !lock(pdMS_TO_TICKS(20))) return;
  gRing.write(d, n, uint32_t(millis()));
  unlock();
}

// esp_log dell'IDF: stessa riga anche nell'anello, poi la stampa di sempre.
int idfLog(const char *fmt, va_list ap) {
  char buf[200];
  va_list cp;
  va_copy(cp, ap);
  const int n = vsnprintf(buf, sizeof buf, fmt, cp);
  va_end(cp);
  if (n > 0) feed(buf, size_t(n) < sizeof buf ? size_t(n) : sizeof buf - 1);
  return gIdfPrintf ? gIdfPrintf(fmt, ap) : vprintf(fmt, ap);
}

}  // namespace

void begin(const char *fw) {
  gPrevOk = cal::crash::tailValid(gTail);
  if (gPrevOk) memcpy(&gPrev, &gTail, sizeof gPrev);
  cal::crash::tailReset(gTail, fw);
  gMx = xSemaphoreCreateMutexStatic(&gMxBuf);
  gRing.setHook(toTail, nullptr);
  gIdfPrintf = esp_log_set_vprintf(idfLog);
}

const cal::crash::Tail *previousTail() { return gPrevOk ? &gPrev : nullptr; }

size_t poll(cal::LogStream &s, cal::Ms now, char *out, size_t cap) {
  if (!s.active() || !lock(pdMS_TO_TICKS(50))) return 0;
  const size_t n = s.poll(now, gRing, out, cap);
  unlock();
  return n;
}

uint8_t *lendRing(size_t n) {
  if (xPortInIsrContext() || !lock(pdMS_TO_TICKS(200))) return nullptr;
  uint8_t *p = gRing.lend(n);
  unlock();  // da qui feed() vede lent(): il buffer non lo tocca più nessuno del registro
  return p;
}

void returnRing() {
  if (!gMx) return;
  xSemaphoreTake(gMx, portMAX_DELAY);  // deve riuscire: dopo, l'anello torna a scrivere nel buffer
  gRing.giveBack();
  unlock();
}

bool start(cal::LogStream &s, JsonVariantConst root, bool authed, cal::Ms now, char *why, size_t cap) {
  if (!lock(pdMS_TO_TICKS(50))) { cal::scopy(why, cap, "registro occupato"); return false; }
  const bool ok = s.start(root, authed, now, gRing, why, cap);
  unlock();
  return ok;
}

}  // namespace logtee

size_t LogTee::write(const uint8_t *d, size_t n) {
  logtee::feed(reinterpret_cast<const char *>(d), n);
  return Serial.write(d, n);
}
