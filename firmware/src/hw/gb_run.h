// Giochi Game Boy: la partita sulla scheda. Emulatore nel task del loop (core 1, Pacer a 59,7 Hz), pannello veloce
// (cal::fastink del piano A, profilo Pulito) in un task «gb-ink» sul core 0; i fotogrammi passano dal fotogramma
// condiviso (core/gb_handoff). Memoria sullo heap solo durante la partita: contesto di Peanut-GB (~17 KB),
// fotogrammi (~10 KB), RAM della cartuccia in banchi da 8 KB (fino a 32 KB; in modalità gioco i primi due
// nell'anello del registro prestato, hw/logtee), driver veloce (36 KB, solo fra resume e pause). La ROM è la partizione «gbrom» mappata in memoria, mai copiata in RAM.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/gb_rom.h"

namespace gbrun {

const size_t kHeapFloor = 8 * 1024;     // sotto, la partita non parte (NimBLE, LittleFS e il loop ne vogliono un po')
const size_t kHeapTarget = 15 * 1024;   // obiettivo durante la partita (sotto: avviso nel registro)
const uint8_t kCleanMin = 10;           // pulizia completa periodica in gioco, minuti (contratto; da tarare dal vivo)

// Memoria dello heap che servirà durante la partita (per il controllo prima di partire e per il registro).
// lentBytes: RAM della cartuccia che viene dall'anello del registro prestato (0.12.2-gb, modalità gioco).
size_t needBytes(const cal::gb::RomInfo &info, size_t lentBytes = 0);
// Prepara la partita: ROM mappata, id (SHA-256), emulatore, RAM della cartuccia dal salvataggio. Non tocca il
// pannello. false → why: testo per lo schermo («Gioco non supportato», «Memoria insufficiente», …).
bool begin(const cal::gb::RomInfo &info, const char **why);
const char *id();  // id del gioco aperto ('' = nessuno)
// Pannello al driver veloce e task del pannello acceso. bands: schermo intero con le bande (kScreenBytes), resta di
// chi chiama finché la partita gira (main.cpp: il canvas, che intanto non disegna). rot, inv: Ruota e Inverti colori.
// false = driver veloce senza memoria: il pannello resta alla libreria.
bool resume(bool rot, bool inv, const uint8_t *bands, int64_t nowUs);
// Task del pannello fermo e pannello restituito pulito (fastink::end). Il salvataggio va in LittleFS se è cambiato.
void pause();
bool running();  // task del pannello acceso
bool saveFailed();  // l'ultimo salvataggio in LittleFS non è riuscito (la RAM cambiata è solo in memoria)
// Fotogrammi dovuti con i tasti dati; false = la CPU del Game Boy si è fermata (codice non valido).
bool step(uint8_t keys, int64_t nowUs);
void bandsChanged();  // le bande sono state ridisegnate: si ricompone il prossimo fotogramma
// Fine: pannello restituito, salvataggio (save = false dopo un errore della CPU: resta quello di prima), memoria
// liberata, ROM non più mappata. true = salvataggio nuovo; saveFailed = l'ultimo salvataggio non è riuscito.
bool end(bool save = true, bool *saveFailed = nullptr);
bool active();

}  // namespace gbrun
