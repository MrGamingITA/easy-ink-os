// Modello dell'interfaccia: porting di STATES, CONTROLS, newModel() e delle utilità pure del
// simulatore (reference/calamaio-os-browser/assets/calamaio-test.js). Niente disegno, niente
// Arduino: tutto riceve `now` (Ms) e, dove servono ore e date, un WallClock.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "gb_model.h"
#include "pad.h"

namespace cal {

// Versione del firmware (hello.fw del protocollo). In schermo: «Calamaio OS 0.5» con la rete (vedi uiVersion).
#define CALAMAIO_FW_VERSION "0.14.1"
extern const char *const kFwVersion;

// ---------- costanti ----------

// Ordine delle chiavi di STATES nel simulatore.
enum class SessionState : uint8_t { Working, Permission, Input, Error, Compact, Done, Idle, Count };

struct StateInfo {
  const char *key;       // 'working', 'permission', … (per log e golden)
  const char *label;     // STATES[k].label
  const char *longText;  // STATES[k].long
  uint8_t prio;
};
extern const StateInfo kStates[static_cast<int>(SessionState::Count)];
inline const StateInfo &stateInfo(SessionState s) { return kStates[static_cast<int>(s)]; }

// Pose degli avatar: stesso ordine di Pose in gfx/avatar.h (verificato da test_core).
// Run (AVATAR.md §12): solo la corsa sulla barra dell'aggiornamento, mai lo stato di una sessione.
enum class MascotPose : uint8_t { Working, Permission, Input, Done, Error, Idle, Compact, Tired, Stuffy, Run, Count };
const char *poseKey(MascotPose p);  // 'working', …, 'tired', 'stuffy', 'run'

struct ControlRow { const char *key; const char *text; };
extern const ControlRow kControlsSingle[6];  // CONTROLS.single

enum class HomeStyle : uint8_t { Goccia, Righe, Contatori, Una, Count };
enum class AnimMode : uint8_t { Auto, Raffiche, Continue, Spente, Count };
enum class SpeedMode : uint8_t { Profilo, Massima, Count };
extern const char *const kHomeLabel[4];   // HOME_LABEL
extern const char *const kAnimLabel[4];   // ANIM_LABEL
extern const char *const kSpeedLabel[2];  // SPEED_LABEL
const char *homeKey(HomeStyle h);        // 'goccia', …
const char *animKey(AnimMode a);         // 'auto', …

struct NetInfo { const char *ssid; int rssi; bool lock; };
extern const NetInfo kNets[5];  // NETS
const int kNetCount = 5;

enum class InputKind : uint8_t { Single, Wheel, Three };

// Profilo del dispositivo (DEVICES[id] del simulatore). Solo quello che serve alla logica.
struct DeviceProfile {
  const char *id;
  const char *name;
  const char *size;  // '2,13″'
  int W, H;
  char cls;          // 'S', 'M', 'W', 'L'
  bool gray;
  int fps;           // aggiornamenti al secondo (stima)
  float fpsMax;      // velocità «Massima»: refresh parziali uno dopo l'altro (misurata); 0 = usa fps
  int fullEvery;     // refresh completo ogni N parziali
  InputKind input;
  bool sd, touch, buzzer, led, sensor;
};
extern const DeviceProfile kHeltec213;  // DEVICES.heltec213

// ---------- capienze fisse ----------
const int kMaxSessions = 6;
const int kMaxTasks = 12;
const int kLogCap = 80;     // logEv tiene gli ultimi 80 eventi
const int kStackCap = 12;   // profondità massima della pila di navigazione
// Testi: limiti del protocollo (docs/PROTOCOLLO.md) + il terminatore. Oltre si tronca su un confine UTF-8.
const int kIdLen = 40;       // session_id (UUID, 36 caratteri)
const int kNameLen = 32;     // name: max 31 byte
const int kDetailLen = 96;   // detail: max 95 byte
const int kSummaryLen = 128; // summary: max 127 byte
const int kTaskLen = 48;
const int kNetCap = 12;      // reti di Cerca reti (≤ kMaxItems di menu.h)
const int kServiceCap = 6;
const int kPrCap = 6;
const int kAgendaCap = 3;    // la pagina Agenda ne mostra 3
const int kHostLen = 48;
const int kApPassLen = 12;   // 8 caratteri + terminatore, con margine
const int kLogTextLen = 64;
const int kSsidLen = 33;
const int kSavedMax = 5;     // reti Wi-Fi salvate (SAVED_MAX del simulatore, firmware 0.8.0)
const int kToastLen = 64;
const int kAvatarRefLen = 25; // id di un avatar: kebab-case, max 24 byte (docs/AVATAR.md §2) = kAvatarIdLen

const Ms kToastMs = 2600;
const Ms kBootMs = 2600;
const Ms kSelMs = 15000;    // la selezione della Home scade dopo 15 s
const Ms kPairMs = 180000;  // finestra dell'abbinamento (M5 §1: 3 minuti)
const Ms kArmMs = 10000;    // Proteggi le chiavi, Ripristino di fabbrica: dopo 2× c'è tanto tempo per tenere 3 s
const Ms kMuteMs = 3600000; // silenzia 1 ora
const Ms kForgetMs = 6000;  // finestra per confermare «Dimentica rete»
const Ms kScanMaxMs = 15000;  // Cerca reti: oltre, la schermata di ricerca si chiude comunque
const Ms kJoinMaxMs = 45000;  // collegamento a una rete: oltre, si considera fallito
const Ms kDismissMs = 10000;  // Archivia con la rete: attesa dell'ack del servizio, poi si ripristina
const int kReqIdLen = 33;     // id di una richiesta: dismiss del display (1–32 byte), perm-request (32 esadecimali)
const int kToolLen = 64;      // perm-request: tool, max 63 byte
const int kArgLen = 97;       // perm-request: arg, max 96 byte (M5 §3)
const int kPermCap = 8;       // richieste di permesso in coda sul display
const int kCodeLen = 8;       // codice di abbinamento: 6 cifre + terminatore

// ---------- modello ----------

enum class TestResult : uint8_t { None, Ok, Ko };

// Avatar richiesto dal servizio (docs/AVATAR.md §10): id '' = nessuno. Senza crc vale qualunque versione
// con quell'id (i predefiniti del firmware per primi); con il crc solo quella versione.
struct AvatarRef {
  char id[kAvatarRefLen];
  uint32_t crc;
  bool hasCrc;
};
bool operator==(const AvatarRef &a, const AvatarRef &b);
inline bool operator!=(const AvatarRef &a, const AvatarRef &b) { return !(a == b); }
void setAvatarRef(AvatarRef &r, const char *id);  // id senza crc ('' = nessuno)
enum class TaskStatus : uint8_t { Todo = 0, Doing = 1, Done = 2 };

struct Task {
  char name[kTaskLen];
  TaskStatus status;
};

struct Session {
  char id[kIdLen];   // session_id del servizio ('' nel finto)
  char name[kNameLen];
  SessionState state;
  char detail[kDetailLen];
  Ms since;
  bool hasAttnAt;  // s.attnAt valorizzato
  Ms attnAt;
  int ctx;
  int subs;
  TestResult test;
  char summary[kSummaryLen];
  Task tasks[kMaxTasks];
  int nTasks;
  int total, done;   // count()
  int ticks;         // s._t: passi di step() dall'ultimo cambio
  AvatarRef avatar;  // s.avatar: '' = l'avatar predefinito (set.defAvatar)
};

struct LogEntry {
  Ms t;
  char name[kNameLen];
  char text[kLogTextLen];
  bool local;  // evento della scheda (logEv), non arrivato dal PC: resta quando arriva un nuovo state
};

// m.log: il più recente in testa (unshift), al massimo kLogCap voci.
struct Log {
  LogEntry e[kLogCap];
  int head;  // indice fisico della voce più recente
  int n;
  const LogEntry &at(int i) const { return e[(head + i) % kLogCap]; }
};

enum class ScreenId : uint8_t {
  Boot, Home, Detail, Perm, List, WifiScan, Qr, Connecting, Oggi, Storico, Batteria, Stato,
  Limiti, Focus, Guest, Servizi, Agenda, Ambiente, Sleep, Pair, Confirm,
  BattLow,  // M6: batteria scarica, ultima schermata prima del sonno profondo senza timer
  Calib,    // M6: calibrazione della batteria
  Ota,      // aggiornamento remoto (docs/OTA.md §1.6): a tutto schermo, sopra tutto
  Game,     // 0.9.0: Menu › Gioco › Alto o basso (fino alla 0.10.x in Menu › Strumenti)
  Pet,      // 0.10.0: Menu › La mascotte (gioco della mascotte, solo con il gioco acceso)
  GbLoad,   // giochi Game Boy: avvio della partita (modalità gioco, ROM, salvataggio, pannello veloce)
  GbPlay,   // giochi Game Boy: partita in corso (pannello del driver veloce; ui::draw disegna solo le bande)
  PadCode,  // 0.14.0: codice di associazione del telefono (passkey, 6 cifre in ScreenEntry::code)
  Count
};
const char *screenKey(ScreenId id);  // 'boot', 'home', … come tp.id

// Saved: Menu › Wi-Fi › Reti salvate; SavedNet: una rete (tp.ssid), con Dimentica.
// Work (0.9.0): Menu › Strumenti › Giornata di lavoro. PetAcc (0.10.0): La mascotte › Accessorio.
// Gb, GbPad, GbRoms (0.11.0): Menu › Gioco, › Controller, › ROM. GbPause (0.12.0): menu di pausa della partita.
enum class MenuId : uint8_t {
  Main, Wifi, Display, Avvisi, Info, Tools, Security, Battery, Saved, SavedNet, Work, PetAcc, Gb, GbPad, GbRoms, GbPause, Theme, Count
};
const char *menuKey(MenuId id);

// Schermata di conferma (Menu › Sicurezza): avviso, 2× e poi tieni 3 s.
enum class ConfirmKind : uint8_t { Protect, Factory };

// Una voce di m.ui.stack. I campi valgono solo per le schermate che li usano.
struct ScreenEntry {
  ScreenId id;
  Ms until;       // boot, connecting; pair: fine della finestra (solo senza rete); confirm: armata fino a (0 = no)
  int i;          // perm: indice della sessione
  int choice;     // perm: 0 = CONSENTI, 1 = NEGA
  MenuId menu;    // list
  int sel;        // list, wifiScan; pair: 0 = codice da scrivere sul PC, 1 = codice giusto, si conferma
  int off;        // storico, servizi
  Ms scanUntil;   // wifiScan
  char ssid[kSsidLen];  // qr, connecting ('' = assente); list SavedNet: la rete mostrata
  char rid[kReqIdLen];  // perm: id della richiesta del servizio ('' = senza rete, dal Dettaglio come nel simulatore)
  char code[kCodeLen];  // pair: 6 cifre
  ConfirmKind kind;     // confirm
  Ms armedAt;           // confirm: istante dell'armatura (R1, regola 2: conta solo un tenuto iniziato dopo)
};

enum class MuteKind : uint8_t { None, Hour, Night };

struct Settings {
  HomeStyle homeStyle;
  AnimMode anim;
  SpeedMode speed;
  bool invert, rot;
  Ms mute;  // silenziato fino a (0 = no)
  MuteKind muteKind;
  bool night, permOn, sound, led, save, hookDirect;
  // set.defaultAvatar del simulatore: l'avatar delle sessioni senza `avatar` e delle schermate senza
  // sessione (avvio, Riposo, PC non raggiungibile, PC spento…). Lo decide il servizio (welcome e state:
  // defaultAvatar, defaultAvatarCrc); senza servizio 'goccia'. Non è un'impostazione remota.
  AvatarRef defAvatar;
  // 0.9.0: Giornata di lavoro, orari predefiniti in minuti dalla mezzanotte (ora locale), 09:00–18:00. Impostazioni
  // remote workStart/workEnd; nella NVS «calamaio-wd» insieme allo stato della giornata (core/workday).
  int16_t workStart, workEnd;
  // 0.10.0: gioco della mascotte (Menu › Display, impostazione remota `game`), spento di default. Nella NVS con le altre.
  bool game;
  // 0.11.0: Bluetooth del controller acceso: vero da «Associa controller» a «Dimentica controller», falso di default.
  // NVS «calamaio-gb/pad», non remota.
  bool padOn;
};

// Le 11 impostazioni remote (docs/PROTOCOLLO.md, «Impostazioni remote»): quello che il display riferisce
// in `settings` e che un `set` può cambiare. mute = silenzio in corso (None quando è finito).
struct RemoteValues {
  HomeStyle homeStyle;
  AnimMode anim;
  SpeedMode speed;
  bool invert, rot, night, permOn, sound, led;
  MuteKind mute;
  bool save;  // Risparmio energetico (set.save, non quello automatico sotto il 15%)
  int16_t workStart, workEnd;  // 0.9.0: «HH:MM» nel protocollo, qui minuti dalla mezzanotte
  bool game;                   // 0.10.0: gioco della mascotte
};
bool operator==(const RemoteValues &a, const RemoteValues &b);
inline bool operator!=(const RemoteValues &a, const RemoteValues &b) { return !(a == b); }
RemoteValues remoteValues(const Settings &s, Ms now);

// Chiavi di un `set` (bit di SettingsPatch::has), nell'ordine del protocollo.
enum class RemoteKey : uint8_t { HomeStyle, Anim, Speed, Invert, Rot, Night, PermOn, Sound, Led, Mute, Save, WorkStart, WorkEnd, Game, Count };
const char *remoteKeyName(RemoteKey k);  // 'homeStyle', …
// Un `set` già controllato: solo le chiavi con il bit acceso in `has`.
struct SettingsPatch {
  uint16_t has;
  RemoteValues v;
  bool on(RemoteKey k) const { return has & (1u << static_cast<int>(k)); }
};

enum class PairState : uint8_t { None, Paired, Redo };

struct Sys {
  bool wifi;
  char ssid[kSsidLen];
  int rssi;
  char ip[16];
  bool pc;
  Ms lastPc;
  double batt;
  bool usb;
  double temp, rh;
  Ms bootAt;
  bool pcOff;
  Ms offAt;
  char boardId[8];  // ultime cifre del MAC in minuscolo: AP Calamaio-3F2A, nome calamaio-3f2a.local
  // M3: rete vera. live = i dati arrivano dal PC (niente stato finto, niente simulazioni dei flussi Wi-Fi).
  bool live;
  PairState pair;           // abbinamento (M5 §1): senza rete «abbinato» come nel simulatore
  bool authed;              // auth-ok valido su questa connessione (M5 §2): solo così permessi e set remoti
  char pairHost[kHostLen];  // PC con cui si è abbinato ('' = sconosciuto)
  bool keysProtected;       // chiave madre in eFuse in uso (M5 §4)
  int usbHigh, usbN;        // M6: ultima lettura del cavo (Info › Cavo USB); usbN = 0: nessuna lettura («?»)
  bool idle;                // M6: stato Attesa (core/power): animazioni ferme, ridisegni solo per eventi e orologio
  char apPass[kApPassLen];  // password dell'AP Calamaio-XXXX durante la configurazione ('' = AP spento)
  char pcHost[kHostLen];    // PC trovato via mDNS ('' = non ancora: si mostra calamaio.local)
  // 0.9.0: l'ora del muro è vera (welcome o SNTP). Il simulatore ce l'ha sempre; sulla scheda la mette il loop.
  // Senza, la giornata di lavoro non parte e non si aggiorna (il WallClock sarebbe l'ora di compilazione).
  bool wallOk;
  // 0.11.0: controller Bluetooth (hw/blepad). padHw = la scheda ha il Bluetooth (solo sulla scheda vera: nel
  // simulatore e nei test golden Menu › Gioco ha solo «Alto o basso»).
  bool padHw;
  pad::Link padLink;
  bool padBonded;
  bool padHasBatt;
  uint8_t padBatt;  // 0–100 con padHasBatt
  pad::Kind padKind;    // 0.14.0: tipo del collegamento o dell'associazione in corso
  bool padPhoneBonded;  // 0.14.0: telefono associato (padBonded = l'Xbox)
};

struct Stats {
  int tasks, permDisplay, permTerminal;
  Ms waitSum;
  int waitN;
  long linesAdd, linesDel;
  double cost;
};

// Dal PC ogni finestra può mancare (piani senza limiti, o prima della prima risposta): has* falso.
struct Limits { double h5; Ms h5Reset; double d7; Ms d7Reset; bool hasH5, hasD7; };
struct Focus { Ms until; int len; };
struct Air { double co2; };
struct Service { char name[kNameLen]; bool ok; int ms; };
enum class PrChecks : uint8_t { Ok, Ko, Run };
struct Pr { char repo[kNameLen]; int n; char title[kLogTextLen]; PrChecks checks; };
struct AgendaItem { Ms t; char title[kLogTextLen]; };
// Una rete di Cerca reti: NETS nel finto, la scansione vera con la rete.
struct NetEntry { char ssid[kSsidLen]; int rssi; bool lock; };
// Una rete salvata (m.saved del simulatore), nell'ordine di inserimento. usedLocal = ultimo collegamento in ora
// locale (ms, 0 = mai); visible/rssi = vista nell'ultima ricerca (per la rete in uso vale sys.rssi).
struct SavedNet { char ssid[kSsidLen]; int64_t usedLocal; bool visible; int rssi; };
struct Toast { char msg[kToastLen]; Ms until; };
// Sessione archiviata (2× nel Dettaglio di una sessione ferma): fuori da m.sessions finché non torna attiva.
// Senza rete resta qui finché step() non le dà un nuovo prompt. Con la rete il servizio la toglie dagli
// state: qui resta solo finché non arriva l'ack (req) e poi uno state senza di lei (confirmed).
struct Archived {
  Session s;
  char req[kReqIdLen];  // id del dismiss in attesa ('' = archiviata solo in locale)
  Ms deadline;          // senza ack entro quest'ora si ripristina
  bool confirmed;       // ack ok: si aspetta lo state del servizio senza la sessione
};

struct UiState {
  ScreenEntry stack[kStackCap];
  int depth;
  int sel;
  Ms selUntil;
  int page;
  Ms forgetUntil;  // «Dimentica tutte le reti» armata fino a
  Ms forgetNetUntil;  // Reti salvate › una rete › «Dimentica» armata fino a
  Ms unpairUntil;  // «Dimentica abbinamento» armata fino a
  Ms newBattUntil; // «Nuova batteria» armata fino a (M6)
  // Tenuto 3 s (m.ui.hold del simulatore): dal tasto giù al tasto su.
  bool holdOn;     // tasto giù
  Ms holdSince;    // istante del tasto giù
  Ms holdNext;     // prossimo ridisegno dell'indicatore (0,7 · 1,4 · 2,1 s)
  bool holdDone;   // tenuto 3 s già scattato: «FATTO · RILASCIA», il rilascio non conta
  bool holdOk;     // allo scatto il tenuto aveva un effetto (holdLabel non nullo)
  // 0.11.0: tenuto di A sul controller (core/pad). Nel Permesso conferma la scelta solo se è iniziato lì, sulla
  // stessa richiesta (holdI, holdRid) e con la stessa scelta (holdChoice); un tasto PRG premuto nel frattempo lo annulla.
  bool holdPad;
  bool holdPerm;
  int holdI;
  char holdRid[kReqIdLen];
  int holdChoice;  // scelta del Permesso all'inizio del tenuto: se cambia, il tenuto non risponde (§7)
  bool holdPhone;  // 0.14.0: il tenuto di A in corso è del telefono (lo scatto del tick non ha sorgente)
  Ms gbForgetUntil;  // Menu › Gioco › Controller › «Dimentica controller» armata fino a
  Ms gbForgetPhoneUntil;  // «Dimentica telefono» armata fino a
};

// Tenuto 3 s: mostra l'indicatore da 0,7 s, ridisegni ogni 0,7 s fino a 2,1 s, scatta a 3 s.
constexpr Ms kHoldShowMs = 700, kHoldMs = 3000, kHoldStepMs = 700, kHoldLastMs = 2100;

// Richiesta di permesso arrivata dal servizio e già verificata (mac con SK, M5 §3). In coda nell'ordine
// di arrivo finché qualcuno risponde, qui o nel terminale (perm-close).
struct PermReq {
  char id[kReqIdLen];
  char session[kIdLen];
  char name[kNameLen];
  char tool[kToolLen];
  char arg[kArgLen];
  int64_t at;  // ms epoch del servizio (entra nel mac)
  Ms since;    // arrivo, in tempo monotono (per «IN ATTESA DA …»)
};

// ---------- M6: batteria, calibrazione, autonomia ----------
// Calibrazione: due fasi da 60 minuti (massimo consumo, attesa), una lettura al minuto, batteria ≥ 90%.
const Ms kCalPhaseMs = 3600000, kCalEveryMs = 60000;
const int kCalMinPct = 90, kCalMinSamples = 5;
const int kCalCap = 64;  // letture per fase: 61 (minuto 0…60) + margine
// Sonno profondo non ancora misurato dalle notti: %/h prudente [stimato]. Ragionamento in DECISIONI (M6):
// 18 µA di sonno + un risveglio ogni 15 min con ~20 s di Wi-Fi a ~100 mA ≈ 2,2 mA medi; su 1000 mAh ≈ 0,22 %/h.
const double kSleepRateDef = 0.3;

// Calibrazione salvata (NVS «calamaio-bat»): consumo in %/h in Attivo e in Attesa, quando (ora locale in ms).
struct BattCal { bool valid; double active, idle; int64_t atLocal; };
struct BatInfo {
  BattCal cal;
  bool hasSleep;   // sonno misurato dalle notti (intervalli ≥ 4 h)
  double sleep;    // %/h
  int mix[3];      // quota del tempo in Attivo, Attesa, Sonno nelle ultime 24 h (registro dei tempi)
  double trend;    // %/h dalla pendenza della scarica (0 = non ancora abbastanza storia)
};
enum class CalibPhase : uint8_t { Off, Wait, Max, Idle, Done };
struct Calib {
  CalibPhase phase;
  Ms ms;       // tempo della fase senza le pause (cavo collegato)
  Ms next;     // prossima lettura (ms della fase)
  Ms last;     // ultimo avanzamento
  bool paused;
  bool due;    // con la rete: lettura dovuta, la fa il loop (ADC, mai durante un refresh)
  int n;
  double h[kCalCap], p[kCalCap];  // ore della fase, percentuale (curva OCV)
  int mv;      // ultima tensione letta
  bool hasMax;
  double rMax;
};

// ---------- aggiornamento remoto (docs/OTA.md) ----------
// Fasi: quelle di ota-status (rifiutato, ricevo, verifico, scrivo, riavvio, fatto, errore) più None.
enum class OtaPhase : uint8_t { None, Rejected, Receive, Verify, Write, Reboot, Done, Error };
const char *otaPhaseKey(OtaPhase p);  // 'rifiutato', 'ricevo', … (ota-status.phase); '' per None
const int kOtaVerLen = 32;   // versione: 1–31 byte (schema: [0-9A-Za-z][0-9A-Za-z.+_-]{0,30})
const int kOtaWhyLen = 96;   // motivo: al massimo 95 byte (come ack.error)
const Ms kOtaResultMs = 15000;   // errore e rifiuto: la schermata si chiude da sola (o con un gesto)
const Ms kOtaRedrawMs = 2000;    // ridisegno della barra: ogni ~5% ma non più spesso di ogni 2 s
const int kOtaRedrawPct = 5;
// La schermata AGGIORNAMENTO (ScreenId::Ota). step = passi di ridisegno: il fotogramma della corsa (AVATAR.md §12).
struct OtaView {
  OtaPhase phase;
  char from[kOtaVerLen], to[kOtaVerLen];
  uint32_t done, total;
  char why[kOtaWhyLen];  // errore o motivo del rifiuto
  int step;
  int drawnPct;   // percentuale dell'ultimo ridisegno della barra
  Ms drawnAt;     // istante dell'ultimo ridisegno della barra
  Ms until;       // errore e rifiuto: chiusura automatica
};
int otaPct(const OtaView &o);  // ricezione: floor(done·100/total); dalla verifica in poi 100

// ---------- 0.9.0: Giornata di lavoro (core/workday) ----------
// La fine è fissa all'orario di fine del giorno in cui si avvia (regola A). Istanti in ora locale (WallClock.localMs):
// sopravvivono al sonno profondo e ai riavvii nella NVS (l'ora del muro sì, il tempo monotono no).
const int64_t kWorkDoneMs = 3600000;  // «GIORNATA FINITA» a schermo per un'ora dopo la fine (mai oltre la mezzanotte)
const Ms kWorkPartyMs = 6000;         // festa alla fine: mascotte contenta e animazione (se non silenziata)
struct Workday {
  bool on;
  int64_t endLocal;        // fine della giornata attiva
  int64_t fromLocal;       // attivazione (la barra parte da qui se l'orario d'inizio non viene prima della fine)
  int64_t doneUntilLocal;  // «GIORNATA FINITA» fino a (0 = no)
  Ms party;                // festa fino a (tempo monotono, 0 = no)
};

// ---------- 0.9.0: gioco «Alto o basso» (core/game) ----------
enum class GameResult : uint8_t { None, Win, Tie, Lose };
const uint32_t kGameSeed = 0x2545F491u;  // GAME_SEED del simulatore: stessa sequenza di carte nei test
struct Game {
  uint32_t seed;   // xorshift32 (simulatore e test); sulla scheda le carte vengono da esp_random (Ui::setRandom)
  int best;        // record (NVS «calamaio-game»)
  int card, prev;  // 1–13: A, 2…10, J, Q, K
  int streak;
  GameResult last;
  bool over;
  int startBest;   // record all'inizio della partita: «NUOVO RECORD!» se la serie lo supera
};

// ---------- 0.10.0: gioco della mascotte (core/pet) ----------
// L'ultimo messaggio `game` del servizio più quello che tiene la scheda (ultimi traguardi, festa). Il gioco conta solo
// con set.game e on: spento, niente accessori, feste né posa stanca dal gioco (i progressi restano sul PC).
const int kPetUnlockedCap = 20;  // accessori sbloccati: al massimo 20 nel messaggio game (PROTOCOLLO.md)
const int kPetGoals = 3;         // ultimi traguardi nella schermata La mascotte
const int kPetTitleLen = 48;     // titolo di un traguardo: max 47 byte (achievements.json: max 24)
const Ms kPetPartyMs = 3000;     // festa di un traguardo
struct PetUnlocked { char id[kAvatarRefLen]; char name[kAvatarRefLen]; };
struct Pet {
  bool known;  // arrivato (o ripristinato) almeno un game: senza, La mascotte dice «Dati dal PC non ancora arrivati»
  bool on;
  int level;
  int32_t xp, xpLevelStart, xpNext;
  int energy;
  bool tired;
  AvatarRef wear;  // id '' = nessuno; senza crc = qualunque versione (scelta dal menu prima della conferma del PC)
  PetUnlocked unlocked[kPetUnlockedCap];
  int nUnlocked;
  char goals[kPetGoals][kPetTitleLen];  // il più recente per primo (dai celebrate, in RAM)
  int nGoals;
  char lastGoal[kAvatarRefLen];  // id dell'ultimo celebrate: lo stesso non rifà la festa
  Ms party;                      // festa fino a (0 = no)
  bool special;       // c'è un accessorio speciale da chiedere a Claude (game.special)
  bool specialSeen;   // toast «Nuovo accessorio speciale» già mostrato da questa accensione (in RAM)
  bool specialToast;  // toast da mostrare appena finisce quello del traguardo dello stesso messaggio
};

// ---------- 0.13.0: temi delle schermate (core/theme_runtime, docs/TEMI.md) ----------
// Catalogo dei temi sulla scheda per Menu › Display › Tema; lo aggiorna il loop dall'archivio (hw/theme_fs).
const int kThemeCap = 8;
struct ThemeItem { char id[kAvatarRefLen]; char name[kAvatarRefLen]; };
struct Themes {
  ThemeItem items[kThemeCap];
  int n;
  int cur;  // indice in items, -1 = Calamaio (di serie)
  bool picked;  // scelta appena fatta (menu o theme-use) che il loop non ha ancora applicato: vale anche se cur non
                // cambia (Calamaio scelto durante l'uscita di emergenza va salvato nella NVS)
};

struct Model {
  Session sessions[kMaxSessions];
  int nSessions;
  Log log;
  UiState ui;
  Settings set;
  Sys sys;
  Stats stats;
  Limits limits;
  Focus focus;
  bool acc;  // accessori I²C (CO2 e buzzer)
  Air air;
  Service services[kServiceCap];
  int nServices;
  Pr prs[kPrCap];
  int nPrs;
  AgendaItem agenda[kAgendaCap];
  int nAgenda;
  NetEntry nets[kNetCap];
  int nNets;
  SavedNet saved[kSavedMax];
  int nSaved;
  Toast toast;
  Ms burstUntil, nextBurst;
  Archived archived[kMaxSessions];
  int nArchived;
  PermReq perms[kPermCap];
  int nPerms;
  BatInfo bat;  // M6
  Calib calib;  // M6
  OtaView ota;  // aggiornamento remoto
  Workday work; // 0.9.0
  Game game;    // 0.9.0
  Pet pet;      // 0.10.0
  GbView gb;    // 0.12.0: giochi Game Boy (core/gb_model)
  Themes themes;  // 0.13.0
};

// ---------- funzioni del modello (stessi nomi del simulatore) ----------

void newModel(Model &m, Ms now, const WallClock &wc);
void defaultSettings(Settings &s);  // set di newModel (anche per il Ripristino di fabbrica)
void countTasks(Session &s);  // count(s)
void logEv(Model &m, const Session *s, const char *text, Ms t);
void toast(Model &m, const char *msg, Ms now, Ms ms = kToastMs);
// 0.13.0: la scelta del tema non ancora applicata (picked): id ('' = Calamaio) in id, picked azzerato. false = nessuna.
bool takeThemePick(Themes &t, char *id, size_t cap);
bool isAttn(const Session &s);
bool canArchive(const Session &s);  // done, idle, error
bool attention(const Model &m);
int urgentIdx(const Model &m);
int sIdx(const Model &m, Ms now);
int unaIdx(const Model &m, Ms now);
bool muted(const Model &m, Ms now);
bool focusOn(const Model &m, Ms now);
bool quiet(const Model &m, Ms now);
bool hasAlerts(const Model &m, Ms now);
void extras(const Session &s, char *out, size_t cap, const char *sep);  // extras(s).join(sep)
const char *lineFor(const Session &s);
const char *stateText(const Session &s);
void metaLine(const Session &s, Ms now, char *out, size_t cap);
MascotPose pose(const Model &m, const Session &s);
bool powerSave(const Model &m);
ScreenEntry &top(Model &m);
const ScreenEntry &top(const Model &m);
// holdLabel() del simulatore: che cosa fa il tenuto 3 s qui, o nullptr dove non ha effetto.
const char *holdLabel(const Model &m, Ms now);
void addWait(Model &m, Session &s, Ms now);
int rssiBars(int r);
// Rete vera: nome del PC da mostrare (trovato via mDNS, altrimenti l'alias calamaio.local del mockup).
const char *pcLabel(const Model &m);
// Versione in schermo: «0.2» del simulatore nel finto, «0.3» con la rete.
const char *uiVersion(const Model &m);
// Si può rispondere ai permessi dal display? Con la rete serve auth-ok (M5 §2), senza l'abbinamento.
bool permFromDisplay(const Model &m);
// Richiesta in coda più vecchia per la sessione (-1 = nessuna) e per id.
int permReqFor(const Model &m, const char *session);
int permReqById(const Model &m, const char *id);
void dropPermReq(Model &m, int k);
// Arrivo della richiesta mostrata nella schermata Permesso: dalla coda con la rete, altrimenti da quando
// la sessione aspetta (attnAt), come nel simulatore.
Ms permSince(const Model &m, const ScreenEntry &tp);
// «ARRIVATA ORA», «IN ATTESA DA 4 MIN», «IN ATTESA DA 1 H 05»: minuti interi trascorsi.
void permWaitText(Ms now, Ms since, char *out, size_t cap);
// Valore di Menu › Sicurezza › Abbinamento e della voce Sicurezza del Menu.
void pairLabel(const Model &m, char *out, size_t cap);
const char *pairShort(const Model &m);
int jsRound(double v);  // Math.round

// M6: batteria. Percentuale continua dalla tensione (curva OCV di core/battery) e viceversa.
double ocvPercent(int mv);
int mvFromPercent(double pct);
// Consumo in %/h: pendenza della percentuale nel tempo (minimi quadrati), cambiata di segno. false = meno di
// kCalMinSamples letture o tempi tutti uguali.
bool calRate(const double *h, const double *p, int n, double &rate);
bool calRunning(const Model &m);
double battRate(const Model &m);  // consumo medio pesato con m.bat.mix (0 senza calibrazione)
// «Autonomia: circa 3 giorni con il tuo uso» · «… (dalla scarica)» · «stima in corso» · «calibra per una stima».
void autonomyText(const Model &m, char *out, size_t cap);
void rateText(double r, char *out, size_t cap);   // «8,1 %/h»
void voltText(int mv, char *out, size_t cap);     // «4,17 V»

// Pila di navigazione.
ScreenEntry makeScreen(ScreenId id);
bool pushScreen(Model &m, const ScreenEntry &e);  // false se la pila è piena (non succede nei flussi noti)
void popScreen(Model &m);
void dropScreens(Model &m, ScreenId id);  // toglie ogni voce con quell'id, ovunque nella pila (mai la prima)
void resetStack(Model &m, const ScreenEntry &e);  // m.ui.stack = [e]

// Formati.
void ago(Ms now, Ms t, char *out, size_t cap);
void fmt1(double v, char *out, size_t cap);  // toFixed(1) con la virgola
const char *yes(bool v);                     // 'sì' / 'no'
// splitCmd(d): strumento e argomento, separati dal primo «: ». arg vuoto se manca.
void splitCmd(const char *detail, char *tool, size_t toolCap, char *arg, size_t argCap);

// Animazioni e refresh.
const char *profileName(const DeviceProfile &dev);
bool animOff(const Model &m, Ms now);
bool animScreen(ScreenId id);  // ANIM_SCREENS
// Velocità animazioni «Massima»: il loop del firmware fa i parziali uno dopo l'altro senza attesa.
bool maxSpeed(const Model &m);
float effFps(const DeviceProfile &dev, const Model &m);
float animRate(const DeviceProfile &dev, const Model &m, Ms t);
int frameFor(const DeviceProfile &dev, const Model &m, int f, Ms t);
void profileText(const DeviceProfile &dev, const Model &m, Ms now, char *out, size_t cap);

}  // namespace cal
