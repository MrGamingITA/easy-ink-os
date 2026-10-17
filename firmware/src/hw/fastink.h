// Driver veloce del pannello (contratto dei giochi, piano A): forma d'onda corta nel registro 0x32 e budget per pixel
// (core/fastink_engine). Prende il pannello da heltec-eink-modules e lo restituisce. Mai insieme al disegno di serie.
// Solo durante la partita del Game Boy (piano B) e nell'app di prova: l'interfaccia di Calamaio resta sulla libreria.
// Uso: begin(profilo) → submit(fotogramma) e tick() finché serve → end().
#pragma once
#include <Arduino.h>
#include <SPI.h>

#include "core/fastink_engine.h"

namespace cal {
namespace fastink {

// ---- contratto (docs/superpowers/plans/2026-10-12-giochi-contratti.md) ----
bool begin(Profile p);              // completo di serie col bianco, poi LUT veloce e analogico acceso; false senza memoria.
                                    // Si può richiamare dopo end() (ogni partita, ogni ripresa dalla pausa).
void submit(const uint8_t *frame);  // bersaglio 250×122, righe di 32 byte, MSB = x più a sinistra, 1 = nero;
                                    // ignora Inverti e Ruota: li applica chi chiama (piano B) al fotogramma
bool tick();                        // un passo di spinta (blocca su BUSY), poi vTaskDelay(1): cede il core 0 (watchdog
                                    // del task idle); false se non c'è niente da spingere
bool settled();                     // tutti i pixel al bersaglio e pulizia a immagine ferma già fatta
void cleanFull();                   // completo di serie con l'immagine corrente, stato dei pixel riallineato
void end();                         // analogico spento, pannello restituito (onReturn fa la pulizia completa)

// ---- piano A: collegamento al firmware e messa a punto ----
// bus = SPI della libreria; onReturn = chiamata alla fine di end(): deve riconfigurare la libreria e far seguire un
// completo; log = registro (LogSerial nel firmware, Serial nella demo).
void attachBus(SPIClass *bus, void (*onReturn)(), Print *log);
bool active();                      // tra un begin() riuscito ed end()
bool reserve();                     // memoria del driver presa subito (blocchi grandi prima che lo heap si frammenti)
                                    // e tenuta anche dopo end(), per le riprese dalla pausa; false senza memoria
void unreserve();                   // fine partita: la memoria si libera (subito, o all'end() se acceso)
void setParams(const Params &p);    // budget, F, P, FR, spinte extra (la LUT si riscrive subito)
const Params &params();
void setTickCtl(uint8_t ctl);       // valore di 0x22 del tick (Task 6)
uint8_t tickCtl();
uint32_t lastTickUs();              // ultimo tick: da 0x20 a BUSY basso, µs
uint32_t measureHoldTick();         // tick senza spingere nessun pixel (lo schermo non cambia): costo fisso, µs; 0 = errore

}  // namespace fastink
}  // namespace cal
