// 0.10.1: l'ora nel Riposo in sonno profondo (logica pura, provata in native: test_clock).
//
//   - Prossimo risveglio da timer (planWake): nel Riposo con l'ora vera, all'inizio del minuto successivo (ogni 5 minuti
//     con il risparmio energetico o la batteria sotto il 15%) più un piccolo margine. Il risveglio dell'ora (Clock) fa
//     solo un parziale del Riposo, senza Wi-Fi; ogni 15 minuti il risveglio è pieno (Full: Wi-Fi e ricerca del PC, come
//     prima); la fine della giornata di lavoro sveglia proprio allora (pieno). Senza l'ora vera, o dopo un `bye`,
//     restano i soli risvegli pieni ogni 15 minuti; con la batteria scarica nessun timer.
//   - Ora del muro attraverso il sonno (WallKeep): epoch e riferimento dell'RTC salvati prima di dormire; al risveglio,
//     se l'orologio di sistema non l'avesse conservata, si ricostruisce. Un RTC ripartito (spegnimento) la invalida.
//   - Immagine sul pannello (RestKeep, memoria RTC): senza Vext il controller perde le sue due RAM; per un parziale
//     pulito dopo il sonno la RAM «vecchia» si riscrive da qui. Con lo stato del Riposo che serve al risveglio
//     dell'ora (Inverti, Ruota, giornata di lavoro, riquadro della mascotte), così quel risveglio non apre né NVS
//     né LittleFS.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "power.h"
#include "workday.h"

namespace cal {

constexpr Ms kClockWakeMs = 60000;          // ora in sonno: ogni minuto
constexpr Ms kClockWakeSaveMs = 5 * 60000;  // con il risparmio energetico: ogni 5 minuti (come da sveglio)
// Dopo il cambio di minuto. Il timer e l'ora del sistema contano sullo stesso RTC: il margine copre solo gli
// arrotondamenti; al risveglio passano comunque ~0,3 s di avvio prima di leggere l'ora [stimato].
constexpr Ms kClockMarginMs = 300;
constexpr Ms kMinSleepMs = 1000;            // come workSleepMs: mai un timer più corto

enum class WakeKind : uint8_t { None, Clock, Full };
const char *wakeKindName(WakeKind k);  // «nessuno», «ora», «Wi-Fi»

struct WakeIn {
  SleepReason reason;
  bool wallOk;         // ora vera (welcome o SNTP da questa accensione a freddo)
  int64_t epochMs;     // adesso
  bool saver;          // powerSave(m): risparmio energetico o batteria sotto il 15% senza cavo
  int64_t wifiDueMs;   // prossimo risveglio con il Wi-Fi (epoch); 0 = fra kTimerWakeMs da adesso
  bool workOn;         // giornata di lavoro attiva
  int64_t workEndMs;   // sua fine (epoch, workEndEpoch)
};

struct WakePlan {
  WakeKind kind;       // None: niente timer (batteria scarica)
  Ms sleepMs;          // durata del sonno
  int64_t atMs;        // epoch previsto del risveglio (0 senza l'ora vera)
  int64_t wifiDueMs;   // da conservare per il giro dopo
  bool workEnd;        // il risveglio è la fine della giornata
};
WakePlan planWake(const WakeIn &in);

// ---------- ora del muro attraverso il sonno profondo ----------
constexpr uint32_t kWallKeepMagic = 0xCA1A0C10u;
struct WallKeep {
  uint32_t magic;
  int64_t epochMs;  // ora del muro prima di dormire
  int64_t rtcUs;    // contatore dell'RTC (esp_clk_rtc_time) nello stesso istante
  uint32_t sum;
};
void wallKeepSave(WallKeep &k, int64_t epochMs, int64_t rtcUs);
// epoch salvato + tempo dell'RTC trascorso. false: non valida, o RTC più indietro del salvato (ripartito).
bool wallKeepRestore(const WallKeep &k, int64_t rtcNowUs, int64_t &epochMs);

// ---------- immagine del pannello e Riposo nella memoria RTC ----------
constexpr uint32_t kRestKeepMagic = 0xCA1A0C12u;  // 0.13.0: tema del Riposo (un RestKeep vecchio non vale)
constexpr size_t kKeepImgBytes = 32 * 122;  // Heltec 250×122: Canvas::kBytes (main.cpp lo controlla)
struct RestKeep {
  uint32_t magic;
  uint8_t imgOk;      // img = immagine che il pannello mostra adesso
  uint8_t rest;       // l'immagine è il Riposo (si può fare il risveglio dell'ora)
  uint8_t partials;   // parziali dall'ultimo completo (RefreshCounter)
  uint8_t nextKind;   // WakeKind del risveglio da timer in arrivo
  uint8_t saver, invert, rot, game;
  int16_t mx, my, mw, mh;  // riquadro della mascotte (ui::sleepMascotRect, coordinate logiche)
  int16_t workStart, workEnd;
  uint8_t workOn, pad0[3];
  int64_t workEndLocal, workFromLocal, workDoneUntilLocal;
  int64_t wifiDueMs;  // prossimo risveglio pieno (epoch)
  int64_t plannedMs;  // epoch previsto del risveglio in arrivo (al risveglio: ritardo dell'avvio)
  // Risvegli dell'ora dall'ultimo risveglio pieno: quanti, con refresh, tempo da svegli (millis prima di dormire).
  uint16_t nClock, nDrawn;
  uint32_t sumAwakeMs;
  uint16_t maxAwakeMs, maxLateMs;
  char themeId[25];  // 0.13.0: tema che ha disegnato il Riposo ('' = di serie)
  uint8_t pad1[3];
  uint32_t themeCrc;  // CRC dell'intestazione di quel tema (Runtime::activateKnown)
  uint8_t img[kKeepImgBytes];
  uint32_t sum;
};
void restSeal(RestKeep &k);
bool restValid(const RestKeep &k);
// Risveglio dell'ora possibile: immagine valida del Riposo, risveglio previsto «ora», e si dormiva per il Riposo.
bool clockWakeOk(const RestKeep &k, bool snapOk, SleepReason slept);
// 0.10.2: dopo il sonno profondo (tasto o timer) il primo disegno può essere un parziale: l'immagine sul pannello è in k
// (valida e con il CRC giusto). fromSleep falso (accensione, reset per crash o brownout): completo.
bool wakePartialOk(const RestKeep &k, bool fromSleep);
// Stato del Riposo che serve al risveglio dell'ora, dal modello: Inverti, Ruota, risparmio (powerSave), giornata.
void restStateFrom(RestKeep &k, const Model &m);
// Modello del Riposo per il risveglio dell'ora, su un modello appena creato (Ui::begin): sessioni e riepilogo
// dall'istantanea del sonno, il resto da k; niente cavo, ora vera, pila Home › Riposo.
void restModel(Model &m, const RestKeep &k, const SleepSnap &s, int64_t epochMs, Ms now);

}  // namespace cal
