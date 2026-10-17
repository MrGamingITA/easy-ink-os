// 0.9.0: Giornata di lavoro, logica pura (provata in native, test_workday). Porting delle funzioni work* del
// simulatore (reference/calamaio-os-browser/assets/calamaio-test.js): ore, testi, barra, fine, mezzanotte, timer di
// risveglio, orari remoti e salvataggio nella NVS. Le conseguenze a schermo (toast, festa) le decide cal::Ui.
//
// Regola A (scelta dell'utente): la fine è fissa all'orario di fine del giorno in cui si avvia; se si inizia alle
// 9:40 di un giorno 9–18 mancano 8 h 20. Tutti gli istanti sono in ora locale (WallClock.localMs).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "model.h"

namespace cal {

const int64_t kDayMs = 86400000;

// «HH:MM» a due cifre, 00:00–23:59 (lo schema del protocollo: ^([01][0-9]|2[0-3]):[0-5][0-9]$). false = non valido.
bool parseHm(const char *s, int &mins);
void minHm(int mins, char *out, size_t cap);  // minHm() del simulatore: 540 → «09:00»
// Orari validi: tutti e due dentro il giorno e l'inizio prima della fine.
bool workTimesOk(int start, int end);

// L'istante di oggi (giorno di localMs) a mins minuti dalla mezzanotte; la mezzanotte dopo localMs; stesso giorno.
int64_t dayAtLocal(int64_t localMs, int mins);
int64_t nextMidnightLocal(int64_t localMs);
bool sameDayLocal(int64_t a, int64_t b);

// Minuti che mancano alla fine, per eccesso: alle 9:40:00,2 con la fine alle 18:00 fanno 500 (8 h 20, come
// «fine − orologio»; per difetto sarebbero 8 h 19 subito dopo il ridisegno del minuto).
int workLeftMin(const Model &m, const WallClock &wc);
// «6 h 25» / «25 min» (up: «6 H 25» / «25 MIN» del Riposo).
void workLeftText(const Model &m, const WallClock &wc, bool up, char *out, size_t cap);
bool workDone(const Model &m, const WallClock &wc);  // «GIORNATA FINITA» a schermo
// Riga della Home accanto all'orologio: il conto, «finita» dopo la fine, '' se spenta.
void workLine(const Model &m, const WallClock &wc, char *out, size_t cap);
// Barra della giornata: dall'orario d'inizio di quel giorno alla fine (se l'inizio non viene prima: dall'attivazione).
double workFrac(const Model &m, const WallClock &wc);
bool workParty(const Model &m, Ms now);

// Cosa deve fare il tick con l'ora attuale: niente, azzerare (mezzanotte passata), finire (fine raggiunta), togliere
// «GIORNATA FINITA» (ora passata). Puro: le conseguenze le applica Ui.
enum class WorkStep : uint8_t { None, Midnight, Finish, DoneOver };
WorkStep workStep(const Model &m, const WallClock &wc);
// Fine raggiunta: on = false e «GIORNATA FINITA» fino a un'ora dopo la fine (mai oltre la mezzanotte). true se è
// ancora il momento di mostrarla (toast e festa): dopo un'ora (scheda spenta alla fine) si spegne in silenzio.
bool workFinish(Model &m, const WallClock &wc);

// Avvio: errore (testo per l'ack) o nullptr. Senza l'ora vera non parte; dopo l'orario di fine di oggi nemmeno.
// Già attiva: nessun cambiamento, nessun errore (alreadyOn = true).
extern const char *const kWorkPastError;  // «orario di fine già passato»
extern const char *const kWorkNoClock;    // «ora non ancora nota»
const char *workStart(Model &m, const WallClock &wc, bool &alreadyOn);
bool workStop(Model &m);  // false = era già spenta

// Sonno profondo (Riposo): il timer di risveglio è min(def, tempo alla fine), così la scheda si sveglia proprio
// alla fine per mostrare «GIORNATA FINITA». Almeno 1 s; senza giornata attiva def.
Ms workSleepMs(const Model &m, const WallClock &wc, Ms def);
// Fine in ms epoch per status.workday, dall'ora del muro adesso (epochNow) e quella locale (wc): -1 se spenta.
int64_t workEndEpoch(const Model &m, const WallClock &wc, int64_t epochNow);

// NVS «calamaio-wd/v1»: versione, orari, stato della giornata, «GIORNATA FINITA» fino a, somma. Sopravvive al sonno
// profondo e ai riavvii; il ripristino aspetta l'ora vera (poi il tick decide mezzanotte e fine).
const size_t kWorkBlobLen = 32;
struct StoredWork {
  int16_t start, end;
  bool on;
  int64_t endLocal, fromLocal, doneUntilLocal;
};
StoredWork storedWork(const Model &m);
size_t packWork(const StoredWork &w, uint8_t *out, size_t cap);
bool unpackWork(const uint8_t *in, size_t n, StoredWork &w);  // false = assente, versione, somma o orari sbagliati
// Copia lo stato salvato nel modello (gli orari subito, la giornata solo se il giorno è ancora oggi o la fine è
// passata da poco: ci pensa poi workStep). Da chiamare con l'ora vera.
void restoreWork(Model &m, const StoredWork &w);

}  // namespace cal
