// M6: tre stati di alimentazione (brief §8.6) e tutto quello che serve a deciderli, in logica pura
// (provata in native, test_power). La parte che tocca la scheda (Wi-Fi, CPU, pannello, sonno profondo,
// memoria RTC, NVS) sta in main.cpp e net/; qui solo regole, contatori e formati.
//
//   Attivo  : (0.9.0: anche il gioco a schermo) aggiornamento remoto in corso o in prova (mai sonno profondo: un risveglio farebbe tornare la
//             versione di prima), una sessione working/permission/input/compact, una richiesta di permesso aperta, un tasto
//             negli ultimi 60 s, il cavo USB (col cavo si resta sempre qui), la fase 1 della calibrazione.
//   Attesa  : tutto il resto da sveglio. Wi-Fi in modem sleep, CPU più lenta, animazioni ferme, schermo
//             aggiornato solo da eventi veri e dall'orologio (una volta al minuto). Fase 2 della calibrazione.
//   Sonno   : sonno profondo, senza cavo e fuori dalla calibrazione: dopo `bye` (e 60 s senza PC: anche un
//             riavvio del servizio manda `bye`), con «Riposo», con la batteria sotto la soglia critica.
//             Risveglio: tasto PRG, o timer ogni 15 minuti (non con la batteria scarica).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "model.h"

namespace cal {

enum class PowerState : uint8_t { Active, Idle, Sleep };
enum class SleepReason : uint8_t { None, Bye, Rest, Battery };
const char *powerStateName(PowerState s);    // «Attivo», «Attesa», «Sonno profondo»
const char *sleepReasonName(SleepReason r);  // «bye», «riposo», «batteria scarica»

constexpr Ms kIdleAfterMs = 60000;         // nessun tasto da 60 s (brief §8.6)
constexpr Ms kByeGraceMs = 60000;          // dopo `bye`: 60 s a cercare il PC prima del sonno
constexpr uint32_t kByeRetryMs = 15000;    // in quei 60 s: subito, 5 s, 5 s, poi ogni 15 s
constexpr Ms kTimerWakeMs = 15 * 60000;    // risveglio da timer
constexpr Ms kProbeMs = 20000;             // risveglio da timer: tempo massimo per trovare il PC
// Sotto, senza cavo: sonno senza timer. 3300 mV = 10 % sulla curva OCV (core/battery): sotto il 15 % del
// risparmio automatico. 3400 mV (l'esempio del brief) sarebbe già il 18 % su questa curva. [stimato]
constexpr int kBattCriticalMv = 3300;
constexpr int kBattCriticalReads = 2;      // letture di fila sotto soglia (una sola può essere un calo sotto carico)

struct PowerInputs {
  bool usb;          // cavo collegato (stato stabile di UsbFilter)
  bool busy;         // con il PC collegato: una sessione working/permission/input/compact
  bool permOpen;     // richiesta di permesso in coda o a schermo
  bool calibrating;  // calibrazione in corso (attesa del cavo compresa)
  bool calibIdle;    // calibrazione nella fase 2 «attesa»
  bool pc;           // WebSocket collegato
  bool pcOff;        // riepilogo «PC spento» (bye ricevuto, anche prima di un sonno)
  bool battCritical; // CriticalBattery::update() ha detto sì
  bool ota;          // aggiornamento in corso, o nuova versione ancora «in prova» (docs/OTA.md): Attivo, mai sonno
  bool playing;      // 0.9.0: gioco «Alto o basso» a schermo: Attivo (si pensa anche più di 60 s fra due carte)
};

// Ricava gli ingressi dal modello (sessioni, permessi, calibrazione, PC). usb e battCritical li mette il loop.
PowerInputs powerInputs(const Model &m);

class PowerManager {
 public:
  void begin(Ms now);
  void key(Ms now);   // tasto premuto (anche il risveglio dal tasto)
  void bye(Ms now);   // `bye` del servizio
  void rest(Ms now);  // «Riposo» (tenuto 3 s o Menu › Riposo)
  void cancelRest() { rest_ = false; }
  // Valuta lo stato. true se è cambiato: state(), reason() e why() dicono quale e perché (per il log).
  bool update(const PowerInputs &in, Ms now);
  PowerState state() const { return state_; }
  SleepReason reason() const { return reason_; }
  const char *why() const { return why_; }
  Ms lastKey() const { return lastKey_; }

 private:
  PowerState state_ = PowerState::Active;
  SleepReason reason_ = SleepReason::None;
  const char *why_ = "avvio";
  Ms lastKey_ = 0;
  Ms byeAt_ = 0;
  bool rest_ = false;
};

// Risveglio dal timer: dopo il tentativo veloce (al massimo kProbeMs) si resta svegli o si torna a dormire.
struct TimerWakeOutcome {
  bool stayAwake;
  bool redraw;      // tornando a dormire: ridisegnare (completo) prima del sonno
  const char *why;
};
// pcFound: arrivato almeno uno state; busy/permOpen da quello state; key: tasto premuto durante il tentativo.
TimerWakeOutcome timerWakeDecision(SleepReason r, bool pcFound, bool busy, bool usb, bool key);

// Batteria critica: kBattCriticalReads letture valide di fila sotto kBattCriticalMv, senza cavo.
class CriticalBattery {
 public:
  bool update(int mv, bool usb);  // mv < 0 = lettura scartata (non conta né azzera)
  bool critical() const { return n_ >= kBattCriticalReads; }
  void reset() { n_ = 0; }

 private:
  int n_ = 0;
};

// Pendenza della scarica (autonomia senza calibrazione): una lettura ogni 5 minuti, solo a batteria, sulle
// ultime 4 ore; serve almeno 2 ore di storia. Col cavo si ricomincia.
class BatteryTrend {
 public:
  static constexpr Ms kEveryMs = 5 * 60000;
  static constexpr int kCap = 48;            // 4 h
  static constexpr double kMinSpanH = 2.0;
  void add(Ms now, int mv, bool usb);
  bool rate(double &pctPerH) const;          // false = storia insufficiente o tensione che non scende
  int count() const { return n_; }

 private:
  Ms t_[kCap];
  int16_t mv_[kCap];
  int n_ = 0;
  Ms last_ = 0;
};

// Registro dei tempi passati in Attivo, Attesa e Sonno, a fasce di un'ora sulle ultime 24 (ora del muro).
// Serve a pesare i consumi della calibrazione. 24 × 8 byte: sta nella memoria RTC e nella NVS.
struct LedgerBucket { int32_t hour; uint16_t sec[3]; };  // hour = epoch / 3600 (0 = vuota)
struct EnergyLedger {
  LedgerBucket b[24];
  void clear();
  // Aggiunge secs secondi nello stato s finiti all'istante endEpochS (si spezzano sulle ore giuste).
  void add(PowerState s, int64_t endEpochS, int64_t secs);
  // Secondi per stato nelle 24 ore prima di nowEpochS. false se in tutto meno di un'ora (si usa l'uso tipico).
  bool totals(int64_t nowEpochS, uint32_t out[3]) const;
  // Quote in percentuale (somma 100), pronte per Model::bat.mix.
  bool mix(int64_t nowEpochS, int out[3]) const;
};

// Sonno misurato dalle notti: tensione all'inizio e alla fine di un episodio di sonno (risvegli da timer
// compresi) lungo almeno 4 ore, senza cavo. false = intervallo troppo corto o letture assurde.
constexpr int64_t kSleepSampleMinS = 4 * 3600;
bool sleepRateSample(int mvStart, int mvEnd, int64_t secs, double &rate);
double mergeSleepRate(bool has, double old, double sample);  // media con il valore precedente

// Calibrazione nella NVS («calamaio-bat/v1»): versione, %/h × 100 di Attivo, Attesa, Sonno, ora locale.
constexpr size_t kBatBlobLen = 20;
size_t packBat(const BatInfo &b, uint8_t *out, size_t cap);
bool unpackBat(const uint8_t *in, size_t n, BatInfo &b);  // false = assente, versione o somma sbagliate

// Istantanea per il sonno profondo (memoria RTC): perché si dorme, il riepilogo, l'episodio di sonno.
constexpr uint32_t kSnapMagic = 0xCA1A3106u;
struct SnapSession { char name[kNameLen]; uint8_t state; char summary[kSummaryLen]; int64_t attnEpochMs; };
struct SleepSnap {
  uint32_t magic;
  uint8_t reason;          // SleepReason
  uint8_t n;
  uint8_t pcOff;
  uint8_t usbAtSleep;
  int64_t sleptEpochMs;    // ultimo addormentamento
  int64_t offEpochMs;      // ora del bye (riepilogo)
  int64_t episodeEpochMs;  // inizio dell'episodio di sonno (resta uguale fra i risvegli da timer)
  int32_t episodeMv;       // tensione all'inizio dell'episodio (−1 = non letta)
  uint8_t episodeUsb;      // il cavo c'era in qualche momento dell'episodio
  SnapSession s[kMaxSessions];
  char tz[48];
  uint32_t sum;
};
// Registro dei tempi nella memoria RTC (sopravvive a sonno e riavvii, non allo spegnimento): magia e somma.
struct LedgerStore { uint32_t magic; EnergyLedger l; uint32_t sum; };
void ledgerSeal(LedgerStore &s);
bool ledgerValid(const LedgerStore &s);

void snapSeal(SleepSnap &s);
bool snapValid(const SleepSnap &s);
// Dal modello all'istantanea e ritorno. epochNowMs/now: lo stesso istante nelle due scale di tempo.
void snapFromModel(SleepSnap &s, const Model &m, int64_t epochNowMs, Ms now);
void snapToModel(const SleepSnap &s, Model &m, int64_t epochNowMs, Ms now);


// Risparmio del Wi-Fi (valori di wifi_ps_type_t: 0 nessuno, 1 minimo, 2 massimo). Con il Bluetooth acceso l'IDF vuole
// il modem sleep (coesistenza): «nessuno» diventa «minimo». Unica regola: la usa applyPower() in main.cpp.
uint8_t wifiPowerSave(bool calibMax, bool idle, bool ble);

}  // namespace cal
