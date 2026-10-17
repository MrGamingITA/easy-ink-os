// Rapporti di crash (docs/OTA.md §6). Il core 2.0.17 ha il coredump su flash attivo
// (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y, formato ELF, partizione «coredump» da 64 KB: letto nella sdkconfig di
// framework-arduinoespressif32/tools/sdk/esp32s3), quindi al riavvio si legge il riepilogo
// (esp_core_dump_get_summary: task, PC, backtrace) in hw/crash_hw.cpp. In più, sempre: il motivo del riavvio
// (esp_reset_reason) e le ultime righe del registro, che hw/logtee copia a ogni riga in una coda nella memoria
// RTC_NOINIT (sopravvive al panic e al watchdog, non allo spegnimento).
//   crash {fw, reason, pc?, backtrace?, last?}   display → servizio, appena autenticato; poi si cancella
// Logica pura (test_crash).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace crash {

// Coda delle ultime righe nella memoria RTC (struttura a dimensione fissa, senza costruttori).
const uint32_t kTailMagic = 0xC4A5410Du;
const size_t kTailCap = 1024;
struct Tail {
  uint32_t magic;
  char fw[16];       // versione che ha scritto la coda
  uint16_t head;     // prossima posizione di scrittura
  uint16_t used;
  char buf[kTailCap];
};
bool tailValid(const Tail &t);
void tailReset(Tail &t, const char *fw);
void tailAppend(Tail &t, const char *line, size_t n);  // riga senza '\n'
// Le ultime maxLines righe complete, dalla più vecchia, in lines[i] (capienza lineCap). Restituisce quante.
int tailLines(const Tail &t, char (*lines)[160], int maxLines);

// Motivo del riavvio (valori di esp_reset_reason_t dell'IDF 4.4, ricopiati qui per i test).
enum class Reset : uint8_t { Unknown = 0, PowerOn = 1, Ext = 2, Sw = 3, Panic = 4, IntWdt = 5, TaskWdt = 6, Wdt = 7,
                             DeepSleep = 8, Brownout = 9, Sdio = 10 };
// Nome del motivo per crash.reason («ESP_RST_PANIC», come esp_reset_reason_t), o nullptr se il riavvio è
// normale (accensione, software, risveglio…).
const char *resetKey(Reset r);
// Nome dell'eccezione Xtensa (EXCCAUSE), nullptr se sconosciuta.
const char *excName(uint32_t cause);

// Forma del messaggio: docs/PROTOCOLLO.md «OTA» e schema `crash` (campi fw, reason, pc, backtrace, last, elf, task, exc).
const int kBtMax = 16, kLastMax = 8;
const size_t kLastBytes = 255;  // last: le righe più recenti unite da '\n', al massimo 255 byte (schema)
struct Report {
  char fw[32];
  char reason[64];   // eccezione del coredump («LoadProhibited») o motivo del riavvio («ESP_RST_TASK_WDT»)
  char exc[24];      // «EXCCAUSE 28» ('' = sconosciuta)
  char task[16];
  char elf[65];      // SHA-256 dell'ELF della build che è caduta ('' = non si sa)
  bool hasPc;
  uint32_t pc;
  int nBt;
  uint32_t bt[kBtMax];
  char last[kLastBytes + 1];
};
// Riepilogo del coredump (esp_core_dump_get_summary), se c'è.
struct Summary {
  bool has;
  char task[16];
  uint32_t pc;
  uint32_t cause;  // EXCCAUSE (0xFFFFFFFF = sconosciuta)
  int nBt;
  uint32_t bt[kBtMax];
  bool btCorrupt;
};
// Riavvio + coredump + coda RTC → rapporto. false se non c'è niente da dire (riavvio normale, nessun coredump).
// elfNow: SHA-256 dell'ELF che gira adesso (si mette solo se la versione caduta è questa).
bool build(Reset r, const Summary &core, const Tail *tail, const char *fwNow, const char *elfNow, Report &out);
size_t encode(const Report &r, char *out, size_t cap);

}  // namespace crash
}  // namespace cal
