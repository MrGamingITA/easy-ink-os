// theme_fs.h — Temi sulla scheda (PROTOCOLLO.md «Temi»): /th/<id>.cthm in LittleFS (la partizione «spiffs» già montata
// da avfs::begin), scrittura in /th/tmp e rinomina, ordine d'installazione in /th/order; tema attivo nella NVS
// «calamaio-th», chiave «cur». LittleFS ha il suo lucchetto (VFS dell'IDF) per ogni chiamata; le modifiche fatte di più
// chiamate (installazione, tema rotto tolto, ordine riscritto) dal task di rete e dal loop passano per lock()/unlock().
#pragma once
#include <stddef.h>

#include "core/theme_runtime.h"

namespace themefs {

bool begin();
// Risveglio dell'ora: monta senza mai formattare (avfs::begin formatta una partizione che non monta: lì
// cancellerebbe temi e avatar) e senza scrivere niente. false = risveglio pieno.
bool mountNoFormat();
cal::theme::Storage &storage();
void lock();
void unlock();
// 0.13.0: tema che il loop sta attivando o ha attivato, scritto PRIMA di ThemeCur (che il task di rete vede più tardi).
// L'installazione del nono tema non lo toglie mai. setProtected prende il lucchetto; protectedId si legge solo col
// lucchetto già preso (commit nel task di rete).
void setProtected(const char *id);
const char *protectedId();
bool loadCur(char *id, size_t cap);
void saveCur(const char *id);
void forget();

}  // namespace themefs
