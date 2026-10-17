// Ogni riga di registro va in tre posti (docs/OTA.md §5 e §6): la seriale (se c'è il cavo: senza, la TX resta
// staccata, core/usb.h), l'anello in RAM del registro da remoto (core/logring, ~16 KB) e la coda delle ultime
// righe nella memoria RTC_NOINIT (core/crash: sopravvive a panic e watchdog). Il codice scrive con
// LogSerial.printf/println/print al posto di Serial; anche i messaggi ESP_LOG dell'IDF passano di qui
// (esp_log_set_vprintf). Un mutex mette in fila il loop, il task di rete e i task dell'IDF.
#pragma once
#include <Arduino.h>

#include "core/crash.h"
#include "core/logring.h"

class LogTee : public Print {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *d, size_t n) override;
  using Print::write;
};
extern LogTee LogSerial;

namespace logtee {

// Da chiamare per prima cosa in setup(), dopo Serial.begin(): copia la coda RTC dell'avvio precedente (per il
// rapporto di crash) e la ricomincia per questo avvio, con la versione che gira.
void begin(const char *fw);
// Coda RTC dell'avvio precedente (valida solo se lo era al riavvio), per crash::build.
const cal::crash::Tail *previousTail();
// Prossimo messaggio `log` dello stream (sotto il lucchetto dell'anello). 0 = niente.
size_t poll(cal::LogStream &s, cal::Ms now, char *out, size_t cap);
// log-start: legge l'anello sotto il lucchetto.
bool start(cal::LogStream &s, JsonVariantConst root, bool authed, cal::Ms now, char *why, size_t cap);

// 0.12.2-gb, modalità gioco: l'anello (cal::LogRing::kCap byte) prestato alla partita come RAM della cartuccia,
// sotto il lucchetto (anche contro NimBLE e i task dell'IDF). Intanto le righe vanno solo in seriale e nella coda
// RTC. nullptr = n troppo grande, già prestato o lucchetto non preso. returnRing: anello di nuovo vuoto e in uso,
// da chiamare quando nessuno tocca più il buffer.
uint8_t *lendRing(size_t n);
void returnRing();

}  // namespace logtee
