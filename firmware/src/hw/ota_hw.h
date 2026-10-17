// Aggiornamento remoto sulla scheda (docs/OTA.md §3): esp_ota_* (core/ota: Flash), download HTTP in streaming
// (core/ota: Source), registro nella NVS («calamaio-ota»), conferma o ritorno dopo il riavvio (rollback del
// bootloader, CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y nella sdkconfig del core 2.0.17), rapporti di crash.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/crash.h"
#include "core/ota.h"

namespace otahw {

// Partizione libera (app0/app1 da 2 359 296 byte con partitions_gb.csv, 3 342 336 con la tabella vecchia
// default_8MB.csv): dimensione massima dell'immagine, letta dalla partizione.
uint32_t maxImageSize();
const char *nextLabel();     // etichetta della partizione che verrà scritta
const char *runningLabel();
cal::ota::ImgState runningState();

cal::ota::Flash &flash();
cal::ota::Source &source();

bool loadRecord(cal::ota::Record &r);
bool saveRecord(const cal::ota::Record &r);
void clearRecord();

// Dopo il riavvio (setup): decide il caso e, se la versione è «in prova», avvia i 120 s.
cal::ota::BootCase boot(cal::ota::Record &rec, bool &hasRec);
bool trialActive();
// auth-ok entro i 120 s: esp_ota_mark_app_valid_cancel_rollback. true se ha confermato adesso.
bool confirm();
// Scaduti i 120 s senza conferma: esp_ota_mark_app_invalid_rollback_and_reboot (non torna). Da chiamare spesso.
void checkTrial();

// Rapporto di crash: motivo del riavvio, riepilogo del coredump (se c'è) e righe RTC (hw/logtee).
// capture() in setup(); json() il messaggio da mandare dopo l'autenticazione (0 = niente); sent() lo cancella
// insieme al coredump in flash.
void captureCrash(const char *fw);
size_t crashJson(char *out, size_t cap);
bool crashPending();  // c'è un rapporto da mandare (0.14.1: il buffer si alloca solo allora)
void crashSent();

}  // namespace otahw
