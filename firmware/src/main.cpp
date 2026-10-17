// Calamaio OS · M3: interfaccia classe S sulla Heltec Wireless Paper V1.2, con la rete vera.
// La logica è in core/ (cal::Ui, come il componente del simulatore), il disegno in ui/ (draw()
// del simulatore, ramo S), la rete in net/ (task sul core 0). Qui: tasto PRG, orologi, ritmo dei
// refresh, copia sul pannello e passaggio degli eventi di rete a cal::Ui (unico proprietario del modello).
//
// Ritmo come il simulatore: tick ogni ~100 ms, slowTick ogni 10 s. Batteria vera ogni 60 s (M6, hw/battery).
// M6: stati di alimentazione (core/power): Attivo, Attesa (modem sleep, CPU a 80 MHz, animazioni ferme),
// sonno profondo (dopo bye + 60 s, Riposo, batteria scarica; mai col cavo né durante la calibrazione).
// Con la rete le sessioni arrivano solo dal PC (state); step() resta per la build senza rete
// (-D CALAMAIO_NO_NET: lo stato finto di M2).
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_mac.h>
#include <esp_private/esp_clk.h>
#include <esp_sleep.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <Preferences.h>
#include <heltec-eink-modules.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "boards/heltec213.h"
#include "core/apply_state.h"
#include "core/battery.h"
#include "core/usb.h"
#include "core/workday.h"
#include "core/clock.h"
#include "core/crypto.h"
#include "core/gesture.h"
#include "core/netfmt.h"
#include "core/power.h"
#include "core/refresh.h"
#include "core/remote_settings.h"
#include "core/strbuf.h"
#include "core/theme_runtime.h"
#include "core/ui.h"
#include "core/wake.h"
#include "gfx/canvas.h"
#include "hw/battery.h"
#include "hw/fastink.h"
#include "hw/logtee.h"
#include "hw/usb.h"
#include "ui/screens.h"
#include "ui/theme_draw.h"
#ifndef CALAMAIO_NO_NET
#include "core/accessories.h"
#include "core/avatars.h"
#include "core/pet.h"
#include "core/ota.h"
#include "core/theme_store.h"
#include "hw/avatar_fs.h"
#include "hw/gb_main.h"
#include "hw/ota_hw.h"
#include "hw/secrets.h"
#include "hw/theme_fs.h"
#include "net/net.h"
#define CALAMAIO_NET 1
#else
#define CALAMAIO_NET 0
#endif
#ifndef CALAMAIO_NO_PAD
#include <esp_heap_caps.h>
#include "core/pad.h"
#include "core/pad_phone.h"
#include "hw/blepad.h"
#define CALAMAIO_PAD 1
#else
#define CALAMAIO_PAD 0
#endif

using namespace cal;

// 0.10.1: la RAM «vecchia» del controller (0x26) si riscrive senza refresh. Dopo il sonno profondo (Vext spento) il
// controller ha perso le sue due RAM: per un parziale pulito la vecchia torna uguale all'immagine sul pannello (salvata
// nella memoria RTC), poi update() scrive la nuova (0x24), fa il parziale e copia la nuova nella vecchia, come sempre.
// Solo in fastmode: lì BaseDisplay::sendImageData() al secondo passaggio scrive proprio la 0x26 (heltec-eink-modules 4.6.0).
class Panel : public EInkDisplay_WirelessPaperV1_2 {
 public:
  void writeOldRam() {
    fastmode_secondpass = true;
    writePage();
    endImageTxQuiet();
    fastmode_secondpass = false;
  }
  // 0.11.0: bus SPI della libreria per il driver veloce (hw/fastink); begin() della libreria gira una volta sola.
  SPIClass *bus() {
    begin();
    return display_spi;
  }
};
static Panel display;
static Canvas<BOARD_W, BOARD_H> canvas;
// 0.10.1 (core/wake): immagine sul pannello e stato del Riposo; ora del muro con il riferimento dell'RTC. Memoria RTC
// lenta, che resta accesa nel sonno profondo (come rtcSnap).
RTC_NOINIT_ATTR static RestKeep rtcRest;
RTC_NOINIT_ATTR static WallKeep rtcWall;
static_assert(sizeof(rtcRest.img) == Canvas<BOARD_W, BOARD_H>::kBytes, "RestKeep::img = canvas della Heltec");
static Ui gUi(kHeltec213);  // ~17,5 KB: globale, mai sullo stack del loop (8 KB)
static PanelRefresh counter(kHeltec213.fullEvery);  // 0.10.2: quando fare il completo (core/refresh)
static Rng rng(1);  // seme vero in setup()
static GestureRecognizer gestures;

// ---------- 0.13.0: temi delle schermate (docs/TEMI.md) ----------
static theme::Runtime themeRt;   // buffer statico da 4 096 byte del blocco della schermata corrente
static theme::SafeBoot safeBoot; // PRG tenuto 1,5 s durante l'avvio: tema di serie per questo avvio
static bool themeFs = false;
static Ms themeWakeAt = 0;       // ridisegno a tempo chiesto dal tema (orologio coi secondi, pagine della lista)

// ---------- tasto PRG ----------
// Fronti raccolti nell'interrupt: il refresh blocca il loop per centinaia di ms,
// così i gesti si riconoscono lo stesso con i loro istanti veri.
struct Edge { uint32_t t; bool down; };
static volatile Edge edges[32];
static volatile uint8_t edgeHead = 0, edgeTail = 0;

static void IRAM_ATTR onButton() {
  uint8_t next = (edgeHead + 1) % 32;
  if (next == edgeTail) return;  // coda piena: si perde il fronte
  edges[edgeHead].t = millis();
  edges[edgeHead].down = digitalRead(PIN_BUTTON_PRG) == LOW;
  edgeHead = next;
}

// ---------- orologi ----------
// Tempo monotono del modello (Ms): parte dall'ora di compilazione (__DATE__ __TIME__, ora locale del PC
// che compila) e avanza con il timer della scheda; non salta mai, anche quando arriva l'ora vera.
// Ora del muro (WallClock): dall'orologio di sistema appena è valido (welcome del servizio o SNTP,
// fuso POSIX in TZ), altrimenti la stessa scala del tempo monotono, come in M2.
static const char *const kDefaultTz = "CET-1CEST,M3.5.0,M10.5.0/3";  // brief §8.5
// Impronta per il servizio (PROTOCOLLO.md, «Versione e profilo di un'immagine per l'OTA»): la stringa C intera
// «calamaio-fw <versione> <profilo>» dentro firmware.bin, con uno zero prima e uno dopo (lo zero iniziale è nel
// vettore: in .rodata il byte precedente può essere qualunque). Usata nel log d'avvio, così il linker la tiene.
static const char kFwTagZ[] = "\0calamaio-fw " CALAMAIO_FW_VERSION " heltec213";
static const char *const kFwTag = kFwTagZ + 1;
static int64_t buildLocalMs = 0;
static int64_t bootUs = 0;

static int64_t parseBuildTime() {
  static const char *const kMon = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *d = __DATE__;  // "Oct 11 2026"
  const char *t = __TIME__;  // "13:52:07"
  char mon[4] = {d[0], d[1], d[2], 0};
  const int month = (strstr(kMon, mon) - kMon) / 3 + 1;
  const int day = atoi(d + 4), year = atoi(d + 7);
  const int hh = atoi(t), mm = atoi(t + 3), ss = atoi(t + 6);
  return wallFromCivil(year, month, day, hh, mm, ss).localMs;
}

static Ms nowMs() { return buildLocalMs + (esp_timer_get_time() - bootUs) / 1000; }

static bool wallValid() { return time(nullptr) > 1700000000; }  // dopo il 2023: impostata da welcome o SNTP

static WallClock wallAt(Ms now) {
  WallClock w;
  w.localMs = now;
  if (!wallValid()) return w;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm lt;
  localtime_r(&tv.tv_sec, &lt);
  w.localMs = wallFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec).localMs +
              tv.tv_usec / 1000;
  return w;
}

// ---------- pannello ----------
static bool fastmode = false;
static bool firstDraw = true;
static int maxFrame = 0;  // fotogrammi a velocità «Massima»

// Copia il canvas (già invertito/ruotato da ui::compose) nel buffer della libreria.
// Si saltano i byte vuoti: con il fondo carta la copia tocca solo i pixel d'inchiostro.
static void blit(const uint8_t *d) {
  display.clearMemory();
  const int stride = Canvas<BOARD_W, BOARD_H>::kStride;
  for (int y = 0; y < BOARD_H; y++)
    for (int xb = 0; xb < stride; xb++) {
      const uint8_t b = d[y * stride + xb];
      if (!b) continue;
      for (int k = 0; k < 8; k++) {
        const int x = xb * 8 + k;
        if (x < BOARD_W && (b & (0x80 >> k))) display.drawPixel(x, y, BLACK);
      }
    }
}

// Calibrazione, fase 1: refresh parziali uno dopo l'altro con tutti e 4 i fotogrammi, come «Massima».
static bool calibMax(const Model &m) {
  return top(m).id == ScreenId::Calib && m.calib.phase == CalibPhase::Max && !m.calib.paused;
}
static bool fastFrames(const Model &m) { return maxSpeed(m) || calibMax(m); }

// Fotogramma da disegnare: quello di cal::Ui (frameFor), oppure a velocità «Massima» un
// contatore proprio che avanza a ogni refresh d'animazione (tutti e 4 i fotogrammi).
static int frameNow(Ms now) {
  const Model &m = gUi.model();
  if (fastFrames(m)) return frameFor(kHeltec213, m, maxFrame, now);
  return gUi.frame(now);
}

static bool shownRest = false;  // 0.10.2: l'ultimo disegno era il Riposo (entrarci è il momento della pulizia dovuta)
static bool primeOld = false;  // 0.10.1: dopo il sonno il primo parziale riscrive prima la RAM vecchia da rtcRest

// 0.10.1: l'immagine appena mandata al pannello va nella memoria RTC (il pannello la tiene anche senza corrente).
static void keepPanel(const Model &m, const WallClock &wc) {
  if (!restValid(rtcRest)) memset(&rtcRest, 0, sizeof rtcRest);
  memcpy(rtcRest.img, canvas.data(), sizeof rtcRest.img);
  rtcRest.imgOk = 1;
  rtcRest.rest = top(m).id == ScreenId::Sleep;
  rtcRest.partials = uint8_t(counter.partials());
  restStateFrom(rtcRest, m);
  const ui::SleepRect r = ui::sleepMascotRect(kHeltec213, m, wc);
  rtcRest.mx = int16_t(r.x);
  rtcRest.my = int16_t(r.y);
  rtcRest.mw = int16_t(r.w);
  rtcRest.mh = int16_t(r.h);
  // 0.13.0: il tema va in RestKeep solo se ha disegnato davvero il Riposo. Un tema senza «sleep», senza varianti vere o
  // in emergenza lascia il Riposo di serie: con themeId vuoto il risveglio dell'ora resta leggero come prima (altrimenti
  // un tema solo della Home farebbe un risveglio pieno a ogni minuto). sleepMascotRect qui sopra ha già letto lo
  // stesso lastThemeDraw del disegno appena fatto.
  const ui::ThemeDrawInfo &ti = ui::lastThemeDraw();
  const bool themedRest = ti.drawn && ti.screen == theme::Screen::Sleep && top(m).id == ScreenId::Sleep;
  scopy(rtcRest.themeId, themedRest ? themeRt.active() : "");
  rtcRest.themeCrc = themedRest ? themeRt.activeCrc() : 0;
  restSeal(rtcRest);
}

static RefreshKind pending = RefreshKind::None;

// ---------- controller Bluetooth (0.11.0: core/pad, hw/blepad), stato ----------
// Fuori da CALAMAIO_NET: servono anche a runSecurityOps() e ai fronti del PRG, e la build senza rete deve compilare.
#if CALAMAIO_PAD
static Preferences gbPrefs;
static bool padSaved = false;     // impostazione nella NVS
static bool padPsBle = false;     // Bluetooth già passato ad applyPower() (padOn o radio ancora accesa)
static bool padBondNow = false;   // abbinamento (a radio spenta blepad::bonded() legge la NVS: copia qui)
static pad::Kind padPairKind = pad::Kind::Xbox;  // 0.14.0: tipo dell'associazione chiesta
static bool padXboxNow = false, padPhoneNow = false;  // associati per tipo (a radio spenta letti dalla NVS)
static pad::Kind padKindSeen = pad::Kind::Xbox;
static uint32_t padPhoneHeapAt = 0;
static uint8_t padConnSeen = 0;  // round 1 (I1): blepad::connId() dell'ultimo giro
static pad::MenuMapper padMenu;
static pad::Link padSeen = pad::Link::Off;
static int padBattSeen = -1;
static bool padPairWanted = false;  // «Associa controller» chiesto, finché il task non è in associazione
static bool padPairSent = false;    // startPairing() già chiamato per questa richiesta
static uint32_t padPairAt = 0;
static uint32_t padSearchFrom = 0;
static uint32_t padHeapAt = 0;
static bool padEndFailed = false;   // end() non riuscito (task fermo oltre 3 s): si riprova dopo kPadEndRetryMs
static uint32_t padEndFailAt = 0;
static const uint32_t kPadEndRetryMs = 10000;  // ogni tentativo può bloccare il loop 3 s

// 0.14.0: netStack = righe del telefono (prova L10): anche il minimo di stack libero del task di rete (le altre lo
// hanno già dalla riga periodica del task di rete), del task del pad e dell'host di NimBLE (revisione finale m2:
// lì gira l'ECDH di Secure Connections). Un task assente non si stampa.
static void stackMark(char *st, size_t cap, const char *task, const char *label) {
  TaskHandle_t h = xTaskGetHandle(task);
  if (!h) return;
  const size_t n = strlen(st);
  snprintf(st + n, cap - n, "%s%s %u", n ? ", " : ", stack libero: ", label, (unsigned)uxTaskGetStackHighWaterMark(h));
}

static void heapLine(const char *when, bool netStack = false) {
  char st[96] = "";
  if (netStack) {
#if CALAMAIO_NET
    // n2: in partita il task di rete si cancella da sé (suspendNow): mai leggere un TCB che si sta liberando
    if (!gUi.gbPlaying() && !net::suspended()) stackMark(st, sizeof st, "calamaio-net", "rete");
#endif
    stackMark(st, sizeof st, "pad", "pad");
    stackMark(st, sizeof st, "nimble_host", "host BLE");
    stackMark(st, sizeof st, "loopTask", "loop");
    if (st[0]) strncat(st, " byte", sizeof st - strlen(st) - 1);
  }
  LogSerial.printf("[memoria] %s: heap libero %u, minimo %u, blocco max %u%s\n", when, (unsigned)ESP.getFreeHeap(),
                   (unsigned)ESP.getMinFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), st);
}

// Spegne la radio (blepad::end, che esegue anche un forget() in sospeso) con al massimo `tries` tentativi.
// false: radio ancora accesa (mai darla per spenta).
static bool padRadioOff(const char *why, int tries) {
  for (int i = 0; i < tries && blepad::active(); i++) {
    if (blepad::end()) break;
    LogSerial.printf("[pad] %s: end() non riuscito (%d/%d)\n", why, i + 1, tries);
  }
  padEndFailed = blepad::active();
  padEndFailAt = millis();
  if (padEndFailed) LogSerial.printf("[pad] %s: Bluetooth ancora acceso\n", why);
  else heapLine(why);
  return !padEndFailed;
}
#endif

// 0.11.0: fine del driver veloce (fastink::end). La libreria si riconfigura (reset + completo) e il prossimo disegno
// è un completo: è la pulizia del pannello dopo il gioco.
static void panelBack() {
  display.fastmodeOff();
  fastmode = false;
  pending = mergeRefresh(pending, RefreshKind::Full);
}

static void present(RefreshKind req, Ms now) {
  if (fastink::active()) {  // 0.11.0: il pannello è del driver veloce; si disegna dopo end()
    pending = mergeRefresh(pending, req);
    return;
  }
  if (req == RefreshKind::None) return;
  // Primo disegno: completo (il pannello vero parte sporco). Poi decide PanelRefresh (core/refresh, 0.10.2).
  // 0.10.1: dopo il sonno profondo l'immagine sul pannello è nota (rtcRest): il primo disegno può essere un parziale.
  // 0.10.2: anche il risveglio col tasto (Quick, conta 2), non solo il ridisegno del risveglio da timer.
  const bool afterSleep = firstDraw && primeOld && req != RefreshKind::Full;
  const bool boot = firstDraw && !afterSleep;
  const Model &m = gUi.model();
  const bool rest = top(m).id == ScreenId::Sleep;
  // 0.10.2: Quick si esegue come parziale (conta 2); il completo solo se chiesto, dovuto e rimandato, o al tetto.
  const RefreshKind k = counter.apply(boot ? RefreshKind::Full : req, rest && !shownRest, now);
  firstDraw = false;
  primeOld = false;
  shownRest = rest;
  if (fastFrames(m) && animRate(kHeltec213, m, now) > 0) maxFrame++;
  const int f = frameNow(now);
  const WallClock wc = wallAt(now);
  ui::compose(canvas, kHeltec213, m, f, now, wc);
  {  // 0.13.0: ridisegno a tempo chiesto dal tema (themeSync)
    const ui::ThemeDrawInfo &ti = ui::lastThemeDraw();
    themeWakeAt = ti.drawn ? ti.nextChange : 0;
  }

  const bool full = k == RefreshKind::Full;
  if (full) { display.fastmodeOff(); fastmode = false; }
  else if (!fastmode) { display.fastmodeOn(false); fastmode = true; }
  if (afterSleep && !full) {  // RAM vecchia = immagine sul pannello, poi il parziale come sempre
    blit(rtcRest.img);
    display.writeOldRam();
  }
  blit(canvas.data());
  const uint32_t t0 = millis();
  display.update();
  const uint32_t dt = millis() - t0;
  keepPanel(m, wc);
  if (full) {
    display.fastmodeOn(false);
    fastmode = true;
  }
  const FullWhy w = counter.why();
  const char *note = boot ? " (primo disegno)"
                     : !full ? (afterSleep ? (req == RefreshKind::Quick ? " (dopo il sonno, conta 2: RAM vecchia dalla memoria RTC)"
                                                                         : " (dopo il sonno: RAM vecchia dalla memoria RTC)")
                                : req == RefreshKind::Quick ? " (rapido come parziale, conta 2)" : "")
                     : w == FullWhy::Deferred ? " (pulizia rimandata)"
                     : w == FullWhy::Cap ? " (tetto dei parziali)"
                     : w == FullWhy::BeforeSleep ? " (prima del sonno)" : " (richiesto)";
  LogSerial.printf("[refresh] %s%s %lu ms · %s · f%d · parziali %d/%d%s\n", refreshKey(k), note, (unsigned long)dt,
                screenKey(top(m).id), f, counter.partials(), kHeltec213.fullEvery, counter.due() ? " · pulizia dovuta" : "");
}

// ---------- impostazioni: NVS e servizio (brief §8.7, docs/PROTOCOLLO.md) ----------
// Una sola regola per menu, set remoto e fine del silenzio: a ogni giro del loop si confrontano le
// impostazioni con l'ultima copia salvata (NVS) e con l'ultima mandata (settings al servizio).
static Preferences setPrefs;
static uint8_t savedBlob[kStoredLen];
static StoredMute nvsMute = {MuteKind::None, 0};  // silenzio letto dall'NVS, in attesa dell'ora vera
static bool nvsMuteOpen = false;
static Ms muteSeen = 0;           // set.mute di cui si conosce la fine in ms epoch
static int64_t muteEndEpoch = 0;  // 0 = sconosciuta (silenzio iniziato senza ora valida)
#if CALAMAIO_NET
static RemoteValues sentValues;
static bool sentOnce = false;
#endif

static int64_t epochNowMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

static void settingsLoad() {
  setPrefs.begin("calamaio-set", false);
  uint8_t blob[kStoredLen];
  const size_t n = setPrefs.isKey("v1") ? setPrefs.getBytes("v1", blob, sizeof blob) : 0;
  Settings &st = gUi.model().set;
  if (n && unpackSettings(blob, n, st, nvsMute)) {
    memcpy(savedBlob, blob, sizeof blob);
    nvsMuteOpen = nvsMute.kind != MuteKind::None;
    LogSerial.printf("[nvs] impostazioni caricate: home %s, animazioni %s, velocità %s, inverti %d, ruota %d, notturno %d, "
                  "permessi %d, cicalino %d, LED %d%s\n", homeKey(st.homeStyle), animKey(st.anim), speedKey(st.speed),
                  st.invert, st.rot, st.night, st.permOn, st.sound, st.led,
                  nvsMuteOpen ? ", silenzio da ripristinare quando arriva l'ora" : "");
  } else {
    LogSerial.printf("[nvs] impostazioni %s: valori predefiniti\n", n ? "non valide" : "assenti");
  }
}

#if CALAMAIO_NET
static void sendSettings(const RemoteValues &v) {
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = NetCmd::Settings;
  r.values = v;
  net::request(r);
  sentValues = v;
  sentOnce = true;
}

static void sendAck(const char *id, bool ok, const char *error) {
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = NetCmd::Ack;
  scopy(r.id, id);
  r.ok = ok;
  scopy(r.error, error ? error : "");
  net::request(r);
}
#endif

// force: manda settings anche se non è cambiato niente (dopo un set il servizio lo aspetta prima dell'ack).
static void settingsSync(Ms now, bool force = false) {
  Settings &st = gUi.model().set;
  if (nvsMuteOpen) {
    if (st.muteKind != MuteKind::None) {
      nvsMuteOpen = false;  // silenzio scelto di nuovo dopo il riavvio: vale quello
    } else if (wallValid()) {
      nvsMuteOpen = false;
      const bool on = restoreMute(st, nvsMute, epochNowMs(), now);
      LogSerial.printf("[nvs] silenzio salvato (%s): %s\n", muteKey(nvsMute.kind), on ? "ripristinato" : "scaduto, non ripristinato");
    }
  }
  const bool mutedNow = st.muteKind != MuteKind::None && st.mute > now;
  if (!mutedNow) {
    muteSeen = 0;
    muteEndEpoch = 0;
  } else if (st.mute != muteSeen || (!muteEndEpoch && wallValid())) {
    muteSeen = st.mute;
    muteEndEpoch = wallValid() ? epochNowMs() + (st.mute - now) : 0;
  }
  StoredMute sm = {MuteKind::None, 0};
  if (mutedNow) sm = {st.muteKind, muteEndEpoch};
  else if (nvsMuteOpen) sm = nvsMute;  // non ancora deciso: resta salvato
  uint8_t blob[kStoredLen];
  packSettings(st, sm, blob, sizeof blob);
  if (memcmp(blob, savedBlob, sizeof blob) != 0) {
    const size_t w = setPrefs.putBytes("v1", blob, sizeof blob);
    memcpy(savedBlob, blob, sizeof blob);
    LogSerial.printf("[nvs] impostazioni salvate (%u byte)\n", unsigned(w));
  }
#if CALAMAIO_NET
  const RemoteValues v = remoteValues(st, now);
  if (force || !sentOnce || v != sentValues) sendSettings(v);
#else
  (void)force;
#endif
}

// ---------- 0.9.0: Giornata di lavoro e record del gioco nella NVS ----------
// «calamaio-wd/v1» (core/workday: orari e stato della giornata, così sopravvive al sonno profondo e ai riavvii) e
// «calamaio-game/best». Lo stato della giornata si ripristina solo con l'ora vera (poi il tick decide mezzanotte e
// fine); finché non arriva, nella NVS resta quello salvato.
static Preferences wdPrefs, gamePrefs;
static uint8_t wdSaved[kWorkBlobLen];
static StoredWork wdStored;
static bool wdOpen = false;  // stato salvato in attesa dell'ora vera
static int gameSaved = 0;

static void workLoad() {
  wdPrefs.begin("calamaio-wd", false);
  uint8_t b[kWorkBlobLen];
  const size_t n = wdPrefs.isKey("v1") ? wdPrefs.getBytes("v1", b, sizeof b) : 0;
  Model &m = gUi.model();
  if (n && unpackWork(b, n, wdStored)) {
    memcpy(wdSaved, b, sizeof b);
    m.set.workStart = wdStored.start;
    m.set.workEnd = wdStored.end;
    wdOpen = true;
    char a[8], e[8];
    minHm(wdStored.start, a, sizeof a);
    minHm(wdStored.end, e, sizeof e);
    LogSerial.printf("[nvs] giornata di lavoro: orari %s-%s%s\n", a, e, wdStored.on ? ", attiva (si ripristina con l'ora vera)" : "");
  } else {
    LogSerial.printf("[nvs] giornata di lavoro %s: orari predefiniti\n", n ? "non valida" : "assente");
  }
  gamePrefs.begin("calamaio-game", false);
  gameSaved = gamePrefs.isKey("best") ? gamePrefs.getUShort("best", 0) : 0;
  m.game.best = gameSaved;
}

static void workSync() {
  Model &m = gUi.model();
  if (wdOpen && wallValid()) {
    wdOpen = false;
    StoredWork w = wdStored;
    w.start = m.set.workStart;  // un set remoto arrivato prima dell'ora vale più del salvato
    w.end = m.set.workEnd;
    restoreWork(m, w);
    LogSerial.printf("[nvs] giornata di lavoro ripristinata: %s\n", w.on ? "attiva" : w.doneUntilLocal ? "finita da poco" : "spenta");
  }
  StoredWork cur = storedWork(m);
  if (wdOpen) {  // non ancora deciso: resta salvato lo stato di prima
    cur.on = wdStored.on;
    cur.endLocal = wdStored.endLocal;
    cur.fromLocal = wdStored.fromLocal;
    cur.doneUntilLocal = wdStored.doneUntilLocal;
  }
  uint8_t b[kWorkBlobLen];
  packWork(cur, b, sizeof b);
  if (memcmp(b, wdSaved, sizeof b) != 0) {
    wdPrefs.putBytes("v1", b, sizeof b);
    memcpy(wdSaved, b, sizeof b);
    LogSerial.println("[nvs] giornata di lavoro salvata (calamaio-wd/v1)");
  }
  if (m.game.best != gameSaved) {
    gameSaved = m.game.best;
    gamePrefs.putUShort("best", uint16_t(gameSaved > 65535 ? 65535 : gameSaved));
    LogSerial.printf("[nvs] gioco: record %d salvato\n", gameSaved);
  }
}

// ---------- comandi ----------

// ---------- energia (M6, core/power) ----------
static PowerManager power;
static char tzNow[48] = "CET-1CEST,M3.5.0,M10.5.0/3";  // ultimo fuso (welcome), per il risveglio
RTC_NOINIT_ATTR static SleepSnap rtcSnap;      // perché si dorme, riepilogo, episodio di sonno
RTC_NOINIT_ATTR static LedgerStore rtcLedger;  // tempi in Attivo, Attesa, Sonno nelle ultime 24 h

// ---------- batteria (M6) ----------
// Ogni 60 s, nel loop: mai durante un refresh (display.update() è bloccante). Una lettura scartata
// lascia il valore precedente. Il cavo USB lo dice hw/usb (sotto), non la tensione.
static constexpr Ms kBattEveryMs = 60000;
static int lastMv = -1;            // ultima tensione buona
static CriticalBattery critBatt;   // sotto kBattCriticalMv senza cavo, due volte di fila
static BatteryTrend trend;         // pendenza della scarica (autonomia senza calibrazione)
static UsbMonitor usbMon;

static bool usbOn() { return usbMon.state() == UsbPresent::Yes; }

static void readBattery(Ms now, int samples = 16) {
  const int mv = battery::readMv(samples);
  critBatt.update(mv, usbOn());
  if (mv < 0) return;
  lastMv = mv;
  trend.add(now, mv, usbOn());
  double r = 0;
  gUi.model().bat.trend = trend.rate(r) ? r : 0;
  pending = mergeRefresh(pending, gUi.battery(lipoPercent(mv), now));
}

// ---------- cavo USB ----------
// Al boot e ogni 10 s (core/usb UsbMonitor, hw/usb): GPIO43 staccato dalla UART e basso per 150 ms (scarica il
// CP2102 alimentato «di fantasma» dalla TX), poi GPIO44 con il pull-down. Con lo stato «no» la TX resta staccata:
// niente log su UART0 finché il cavo non torna. Diagnosi a schermo: Menu › Info › Cavo USB.
static constexpr Ms kUsbEveryMs = 10000;

static void readUsb(Ms now, bool boot) {
  const bool changed = usbMon.check(usb::pins());
  const UsbProbe &p = usbMon.last();
  Sys &y = gUi.model().sys;
  if (y.usbHigh != p.high || y.usbN != p.n) {
    y.usbHigh = p.high;
    y.usbN = p.n;
    if (top(gUi.model()).id == ScreenId::List) pending = mergeRefresh(pending, RefreshKind::Partial);  // Info › Cavo USB
  }
  if (boot) {
    const usb::Pad pad = usb::lastPad();
    LogSerial.printf("[usb] avvio: GPIO44 alto %d/%d dopo %s · pad prima: pull-up %d, pull-down %d, ingresso %d -> %s\n", p.high, p.n,
                  p.drained ? "150 ms con GPIO43 basso" : "nessuna attesa", pad.wpu, pad.wpd, pad.ie, usbName(p.present));
  }
  if (!changed) return;
  const bool on = usbOn();
  // Con «no» la TX è già staccata: questa riga non esce (e va bene). Con «sì» è appena tornata.
  LogSerial.println(on ? "[usb] cavo collegato · log ripreso" : "[usb] cavo scollegato · log sospeso");
  if (on) trend.add(now, -1, true);  // col cavo la pendenza della scarica ricomincia
  if (boot) gUi.model().sys.usb = on;  // all'avvio senza toast: il primo disegno lo mostra già
  else pending = mergeRefresh(pending, gUi.usb(on, now));
}

static void onGesture(Gesture g) {
  // 0.13.0: la pressione dell'uscita di emergenza non arriva al menu (fino al rilascio)
  if (safeBoot.swallow()) {
    LogSerial.printf("[tasto] %s scartato (uscita di emergenza)\n", gestureName(g));
    return;
  }
  const Ms now = nowMs();
  const RefreshKind r = gUi.gesture(g, now, wallAt(now));
  LogSerial.printf("[tasto] %s -> %s\n", gestureName(g), gUi.lastAction());
  pending = mergeRefresh(pending, r);
}

// Punto d'uscita dei permessi (core/ui): con la rete Consenti e Nega diventano perm-reply firmato (M5 §3).
static void onPerm(const PermEvent &e, void *) {
  static const char *const kOut[] = {"consentito", "negato", "annullato (resta aperta)"};
  // §7, scelta A: terza barriera. Un «consenti» dal telefono non diventa mai perm-reply (Ui non lo emette già);
  // nel registro mai «consentito» per una risposta scartata.
  if (dropFromPhone(e)) {
    LogSerial.printf("[permesso] %s · CONSENTI dal telefono scartato (§7) · richiesta %s\n", e.sessionName,
                     e.requestId[0] ? e.requestId : "(senza rete)");
    return;
  }
  LogSerial.printf("[permesso] %s · %s · richiesta %s · da %s\n", e.sessionName, kOut[static_cast<int>(e.outcome)],
                e.requestId[0] ? e.requestId : "(senza rete)",
                e.src == InputSrc::Phone ? "telefono" : (e.src == InputSrc::Xbox ? "controller" : "tasto"));
#if CALAMAIO_NET
  if (e.outcome == PermOutcome::Cancelled || !e.requestId[0]) return;
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = NetCmd::PermReply;
  scopy(r.id, e.requestId);
  r.ok = e.outcome == PermOutcome::Allow;
  r.src = e.src;
  if (dropFromPhone(r)) return;  // stessa barriera sul perm-reply già fatto (ok dal telefono: mai)
  net::request(r);
#endif
}

#if CALAMAIO_NET
// ---------- rete ----------
// Proteggi le chiavi e Ripristino di fabbrica li esegue il loop, dopo il comando (mai dentro cal::Ui).
static bool wantProtect = false, wantFactory = false;
static uint32_t factoryRestartAt = 0;

static void onNetRequest(const NetRequest &r, void *) {
  // R1 e F11, terza barriera: dal telefono niente eFuse, Ripristino, abbinamento col PC né Wi-Fi (Ui non li emette già).
  if (dropFromPhone(r)) {
    LogSerial.printf("[sicurezza] %s dal telefono scartato (R1)\n", netCmdKey(r.cmd));
    return;
  }
  if (r.cmd == NetCmd::ProtectKeys) { wantProtect = true; return; }
  if (r.cmd == NetCmd::FactoryReset) { wantFactory = true; return; }
  net::request(r);
}

// ---------- avatar (docs/AVATAR.md §10) ----------
// Cache in LittleFS (hw/avatar_fs), avatar-need per quelli che mancano, ultimo predefinito nella NVS.
static AvatarCache avCache;
static AvatarNeeds avNeeds;
static AvatarRef savedDefAvatar;  // ultimo predefinito salvato nella NVS
static bool avFs = false;

static void avatarsBegin() {
  avFs = avfs::begin();
  if (avFs) {
    const int n = avCache.begin(&avfs::storage());
    setAvatarCache(&avCache);
    LogSerial.printf("[avatar] cache: %d avatar validi\n", n);
    for (int i = 0; i < AvatarCache::kSlots; i++)
      if (const Avatar *a = avCache.slot(i)) LogSerial.printf("[avatar]   %d: %s crc %08lx\n", i, a->id, (unsigned long)a->crc);
  }
  // Prima della rete: l'avvio usa già il predefinito dell'ultima volta, se è un predefinito o è in cache.
  AvatarRef d;
  if (avfs::loadDefault(d)) {
    gUi.model().set.defAvatar = d;
    savedDefAvatar = d;
    LogSerial.printf("[avatar] predefinito dalla NVS: %s%s\n", d.id, lookupAvatar(d) ? "" : " (non in cache: Goccia finché non arriva)");
  } else {
    savedDefAvatar = gUi.model().set.defAvatar;
  }
}

static void avatarsLoop(Ms now) {
  Model &m = gUi.model();
  AvatarMsg *p = nullptr;
  while (net::takeAvatar(p)) {
    int slot = -1;
    const AvatarError err = avCache.store(p->id, p->crc, p->data, p->len, &slot);
    if (err == AvatarError::Ok) {
      LogSerial.printf("[avatar] %s crc %08lx valido, nel posto %d\n", p->id, (unsigned long)p->crc, slot);
      if (avatarWanted(m, p->id)) pending = mergeRefresh(pending, RefreshKind::Partial);
    } else {
      LogSerial.printf("[avatar] %s crc %08lx rifiutato: %s\n", p->id, (unsigned long)p->crc, avatarErrorText(err));
      avNeeds.rejected(p->id, p->crc);
    }
    free(p);
  }
  // 0.14.0 (R4): avatar scaricato a pezzi dal task di rete e già controllato: si rinomina nella cache e si risponde.
  Avatar hdr;
  while (net::takeAvatarFile(hdr)) {
    int slot = -1;
    const AvatarError err = avCache.adopt(hdr, &slot);
    const bool ok = err == AvatarError::Ok;
    if (ok) {
      LogSerial.printf("[avatar] %s crc %08lx a pezzi, nel posto %d\n", hdr.id, (unsigned long)hdr.crc, slot);
      if (avatarWanted(m, hdr.id)) pending = mergeRefresh(pending, RefreshKind::Partial);
    } else {
      LogSerial.printf("[avatar] %s crc %08lx non salvato: %s\n", hdr.id, (unsigned long)hdr.crc, avatarErrorText(err));
    }
    NetRequest r;
    memset(&r, 0, sizeof r);
    r.cmd = NetCmd::AvatarAck;
    scopy(r.id, hdr.id);
    r.avatarCrcs[0] = hdr.crc;
    r.ok = ok;
    scopy(r.error, ok ? "" : "salvataggio non riuscito");
    net::request(r);
  }
  // avatar-need: subito per quelli nuovi, poi ogni 30 s finché non arrivano (solo con il PC collegato).
  static Ms nextNeed = 0;
  if (m.sys.pc && now >= nextNeed) {
    nextNeed = now + 500;
    NetRequest r;
    memset(&r, 0, sizeof r);
    r.cmd = NetCmd::AvatarNeed;
    r.nAvatarIds = uint8_t(avNeeds.due(m, now, r.avatarIds, kMaxSessions + 1));
    if (r.nAvatarIds) net::request(r);
  }
  if (m.set.defAvatar != savedDefAvatar) {  // welcome o state con un predefinito nuovo
    savedDefAvatar = m.set.defAvatar;
    avfs::saveDefault(savedDefAvatar);
    LogSerial.printf("[avatar] predefinito %s salvato nella NVS\n", savedDefAvatar.id);
    pending = mergeRefresh(pending, RefreshKind::Partial);
  }
}

// ---------- 0.13.0: temi delle schermate (core/theme_runtime, hw/theme_fs, docs/TEMI.md) ----------
// Catalogo dell'archivio → Menu › Display › Tema; cur = il tema attivo del runtime. Il catalogo si legge sotto il
// lucchetto (il task di rete installa, il loop toglie i temi rotti). Se cambia con la lista dei temi (o Display) a
// schermo si ridisegna subito: un 2× sceglie solo quello che si vede.
// Impronta di quello che la lista mostra (n, cur, id e nomi), FNV-1a.
static uint32_t themesPrint(const Themes &t) {
  uint32_t h = 2166136261u;
  auto add = [&h](const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  };
  add(&t.n, sizeof t.n);
  add(&t.cur, sizeof t.cur);
  for (int k = 0; k < t.n && k < kThemeCap; k++) {
    add(t.items[k].id, strlen(t.items[k].id) + 1);
    add(t.items[k].name, strlen(t.items[k].name) + 1);
  }
  return h;
}

static void themesToModel() {
  Model &m = gUi.model();
  Themes &t = m.themes;
  const uint32_t before = themesPrint(t);  // impronta, non una copia: 4 byte invece di ~400 di RAM statica
  const bool picked = t.picked;
  memset(&t, 0, sizeof t);
  t.cur = -1;
  t.picked = picked;  // una scelta non ancora applicata resta (la applica themeSync)
  if (themeFs) {
    theme::CatalogEntry e[theme::kMaxThemes];  // ~470 byte sullo stack del loop (8 KB), solo qui
    themefs::lock();
    const int n = theme::catalog(themefs::storage(), e, theme::kMaxThemes);
    themefs::unlock();
    t.n = n < kThemeCap ? n : kThemeCap;
    for (int k = 0; k < t.n; k++) {
      scopy(t.items[k].id, e[k].id);
      scopy(t.items[k].name, e[k].name);
      if (!themeRt.suspended() && !strcmp(e[k].id, themeRt.active())) t.cur = k;  // in emergenza il menu mostra Calamaio
    }
  }
  const ScreenEntry &tp = top(m);
  if (before != themesPrint(t) && tp.id == ScreenId::List && (tp.menu == MenuId::Theme || tp.menu == MenuId::Display))
    pending = mergeRefresh(pending, RefreshKind::Partial);
}

static void themeReport() {
  themefs::setProtected(themeRt.active());  // prima di ThemeCur: il task di rete lo vede subito
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = NetCmd::ThemeCur;
  scopy(r.id, themeRt.active());
  net::request(r);
}

// Un tema si è rivelato rovinato (all'avvio, all'attivazione, al caricamento di un blocco). Mai cancellare un tema
// buono: prima si ricontrolla il file com'è adesso (un errore da indice vecchio, per un file appena sostituito dal task
// di rete o un ThemeStored scartato in partita, non è un tema rotto). Integro → si riattiva; rovinato → di serie, toast,
// file tolto; illeggibile (open/read fallito, di solito memoria) → di serie per adesso, file lasciato. Il lucchetto
// copre controllo e cancellazione: il task di rete non installa in mezzo.
static void themeBroken(const char *id, Ms now, Ms toastMs = kToastMs) {
  char bad[theme::kIdLen];
  scopy(bad, id);  // id può essere themeRt.active(), che activate() cambia
  uint8_t *scratch = static_cast<uint8_t *>(malloc(theme::kMaxBlock));
  if (!scratch) {  // non si può ricontrollare: di serie per adesso, il file resta (si riprova al prossimo avvio)
    LogSerial.printf("[temi] %s non attivato e memoria insufficiente per ricontrollarlo: di serie, file lasciato\n", bad);
    themesToModel();
    pending = mergeRefresh(pending, RefreshKind::Quick);
    return;
  }
  themefs::lock();
  theme::Header h;
  bool io = false;
  const theme::Error e = theme::verifyStored(themefs::storage(), bad, scratch, &h, &io);
  const bool good = e == theme::Error::Ok && !strcmp(h.id, bad);
  if (!good && !io) {
    themefs::storage().remove(bad);
    theme::orderDrop(themefs::storage(), bad);
  }
  themefs::unlock();
  free(scratch);
  if (!good && io) {  // non si legge adesso (memoria): come sopra, si riprova al prossimo avvio
    LogSerial.printf("[temi] %s non attivato e illeggibile adesso (%s, heap libero %u): di serie, file lasciato\n", bad,
                     theme::errorText(e), unsigned(ESP.getFreeHeap()));
    themesToModel();
    pending = mergeRefresh(pending, RefreshKind::Quick);
    return;
  }
  if (good) {
    char again[theme::kIdLen];
    const bool ok = themeRt.activate(bad);
    if (!ok) themeRt.takeBroken(again, sizeof again);  // si è rotto fra il controllo e l'attivazione: al prossimo avvio
    LogSerial.printf("[temi] %s: il file è integro (indice vecchio), %s\n", bad, ok ? "riattivato" : "di serie per adesso");
    themesToModel();
    pending = mergeRefresh(pending, RefreshKind::Quick);
    return;
  }
  LogSerial.printf("[temi] %s rovinato: tolto, si usa il tema di serie\n", bad);
  themefs::saveCur("");
  toast(gUi.model(), "Tema rovinato: uso quello di serie", now, toastMs);
  themesToModel();
  themeReport();
  pending = mergeRefresh(pending, RefreshKind::Quick);
}

// Il Runtime ha segnato un tema rotto (activate() o block() falso): si decide in themeBroken.
static void themeCheckBroken(Ms now) {
  char bad[theme::kIdLen];
  if (themeRt.takeBroken(bad, sizeof bad)) themeBroken(bad, now);
}

static void themesBegin(Ms now) {
  themeFs = avFs && themefs::begin();
  if (!themeFs) {
    // senza archivio il Runtime resta di serie (anche dopo un risveglio dell'ora passato al pieno), ma il suo buffer
    // si presta lo stesso alla partita (hw/gb_run lo trova con ui::themeRuntime)
    themeRt.begin(nullptr);
    ui::setThemeRuntime(&themeRt);
    return;
  }
  themeRt.begin(&themefs::storage());
  ui::setThemeRuntime(&themeRt);
  char cur[theme::kIdLen];
  themefs::loadCur(cur, sizeof cur);
  // Riavvio fra la rinomina del nono tema e l'ordine: si torna a 8 (mai l'attivo), l'ordine si riscrive.
  themefs::lock();
  const int trimmed = theme::orderTrim(themefs::storage(), cur);
  themefs::unlock();
  if (trimmed) LogSerial.printf("[temi] %d temi oltre gli 8 (riavvio durante un'installazione): tolti i più vecchi\n", trimmed);
  uint8_t *scratch = static_cast<uint8_t *>(malloc(theme::kMaxBlock));  // solo per il controllo dell'avvio
  theme::BootPlan p;
  memset(&p, 0, sizeof p);
  if (scratch) p = theme::themeBootPlan(themefs::storage(), cur, scratch);
  free(scratch);
  if (p.use && !themeRt.activate(p.id)) themeCheckBroken(now);
  themesToModel();
  LogSerial.printf("[temi] %d sulla scheda, attivo «%s»\n", gUi.model().themes.n, themeRt.active()[0] ? themeRt.active() : "Calamaio");
  // 5 s come «Tema di serie»: l'avvio (2,6 s) non disegna i toast, si vede sulla prima Home
  if (p.broken) themeBroken(p.id, now, 5000);
}

static bool themeAway = false;  // al giro prima: buffer prestato alla partita o partita aperta

// Il menu (Ui::themePick) cambia m.themes.cur: qui si attiva, si salva nella NVS e si dice al PC.
static void themeSync(Ms now) {
  if (!themeFs) return;
  // In partita il buffer è della cartuccia (Runtime::lend, hw/gb_run): activate() e block() rispondono false senza che
  // il tema sia rotto. Niente da fare finché non torna.
  if (themeRt.lent()) {
    themeAway = true;
    return;
  }
  // La scelta dell'utente (menu o theme-use) si legge per prima: il ritorno dalla partita e il tema rotto rifanno il
  // catalogo (themesToModel), che riscrive cur dal tema attivo. Una scelta non si perde mai.
  char pick[theme::kIdLen];
  const bool picked = takeThemePick(gUi.model().themes, pick, sizeof pick);
  if (picked) themefs::setProtected(pick);  // il nono tema in arrivo non lo toglie mentre si attiva
  if (gbmain::playing() || gUi.gbOnStack()) {
    themeAway = true;  // pumpNet scarta gli eventi (ThemeStored compreso) anche senza prestito
  } else if (themeAway) {
    // Fine partita: il file del tema attivo può essere cambiato sotto il Runtime (intestazione e indice vecchi): si
    // riattiva dall'archivio e si rifà il catalogo.
    themeAway = false;
    char cur[theme::kIdLen];
    scopy(cur, themeRt.active());
    if (cur[0] && !themeRt.activate(cur)) themeCheckBroken(now);
    themesToModel();
    pending = mergeRefresh(pending, RefreshKind::Quick);
  }
  themeCheckBroken(now);  // prima di activate: un blocco rotto per un indice vecchio non si riattiva per poi cancellarlo
  Themes &t = gUi.model().themes;
  const char *want = picked ? pick : t.cur >= 0 && t.cur < t.n ? t.items[t.cur].id : "";
  const char *shown = themeRt.suspended() ? "" : themeRt.active();  // quello che si vede adesso
  // anche una scelta uguale a quello che si vede, durante l'uscita di emergenza: Calamaio scelto lì va nella NVS
  if (strcmp(want, shown) || (picked && themeRt.suspended())) {
    char id[theme::kIdLen];
    scopy(id, want);
    themefs::setProtected(id);  // di nuovo: themeBroken qui sopra può averlo riportato all'attivo
    themeRt.suspend(false);
    if (!themeRt.activate(id)) {
      themeCheckBroken(now);
      themesToModel();  // cur torna al tema attivo davvero
    } else {
      themefs::saveCur(id);
      themeReport();
      themesToModel();  // cur dal tema attivo (il catalogo può essere cambiato nel frattempo)
      LogSerial.printf("[temi] attivo «%s»\n", id[0] ? id : "Calamaio");
      pending = mergeRefresh(pending, RefreshKind::Quick);
    }
  }
  if (themeWakeAt && now >= themeWakeAt) {
    themeWakeAt = 0;
    pending = mergeRefresh(pending, RefreshKind::Partial);
  }
}

static RefreshKind themeEvent(const net::Event &e, Ms now) {
  // In partita pumpNet scarta già questi eventi; comunque niente col buffer prestato: all'uscita ci pensa themeSync.
  if (!themeFs || themeRt.lent()) return RefreshKind::None;
  if (e.type == net::EvType::ThemeStored) {
    // un tema nuovo o aggiornato: se è quello attivo si ricarica (CRC nuovo; anche in emergenza, così intestazione e
    // indice non restano vecchi); la lista a schermo la ridisegna themesToModel
    const bool act = !strcmp(e.id, themeRt.active());
    if (act && !themeRt.activate(e.id)) themeCheckBroken(now);
    themesToModel();
    return act && !themeRt.suspended() ? RefreshKind::Quick : RefreshKind::None;
  }
  // ThemeUse dal PC
  if (!theme::canUse(themefs::storage(), e.id)) {
    LogSerial.printf("[temi] theme-use %s: tema che la scheda non ha, resta «%s»\n", e.id, themeRt.active());
    themeReport();  // themes invariato: il servizio vede che manca e lo riofferta
    return RefreshKind::None;
  }
  Themes &t = gUi.model().themes;
  t.cur = -1;
  for (int k = 0; k < t.n; k++)
    if (!strcmp(t.items[k].id, e.id)) t.cur = k;
  t.picked = true;
  themeSync(now);
  return RefreshKind::None;  // il ridisegno lo chiede themeSync
}

// ---------- 0.10.0: gioco della mascotte (core/pet, core/accessories) ----------
// Accessori e ancore ricevuti in LittleFS accanto agli avatar (hw/avatar_fs), accessory-need e anchors-need ogni 30 s
// finché mancano, ultimo riassunto del gioco salvato (così all'avvio la mascotte indossa già l'accessorio).
static AccessoryStore accStore;
static AccNeeds accNeeds;
static uint8_t petSaved[kPetBlobMax];
static size_t petSavedLen = 0;

static void petBegin() {  // dopo goLive: clearForLive azzera il riassunto finto
  if (avFs) {
    const int n = accStore.begin(&avfs::accStorage());
    LogSerial.printf("[gioco] ancore ricevute in memoria: %d\n", n);
  } else {
    accStore.begin(nullptr);  // solo il catalogo compilato e quello che arriva, in RAM
  }
  setAccessoryStore(&accStore);
  uint8_t b[kPetBlobMax];
  const int n = avFs ? avfs::loadPet(b, sizeof b) : -1;
  Pet &p = gUi.model().pet;
  if (n > 0 && unpackPet(b, size_t(n), p)) {
    memcpy(petSaved, b, size_t(n));
    petSavedLen = size_t(n);
    LogSerial.printf("[gioco] ultimo riassunto: %s, livello %d, indossa %s%s\n", p.on ? "acceso" : "spento", p.level,
                     p.wear.id[0] ? p.wear.id : "niente", gUi.model().set.game ? "" : " (interruttore della scheda spento)");
  } else if (n >= 0) {
    LogSerial.println("[gioco] riassunto salvato non valido: si aspetta il servizio");
  }
}

static void petLoop(Ms now) {
  Model &m = gUi.model();
  GameMsg *g = nullptr;
  while (net::takeGame(g)) {
    pending = mergeRefresh(pending, gUi.netGame(*g, now));
    if (g->hasCelebrate) LogSerial.printf("[gioco] %s\n", gUi.lastAction());
    free(g);
  }
  AccessoryMsg *a = nullptr;
  while (net::takeAccessory(a)) {
    const AccError err = accStore.store(a->id, a->crc, a->data, a->len, m.pet.wear.id);
    if (err == AccError::Ok) {
      LogSerial.printf("[gioco] accessorio %s crc %08lx valido (%u byte)\n", a->id, (unsigned long)a->crc, unsigned(a->len));
      pending = mergeRefresh(pending, gUi.netAccessoryArrived(now));
    } else {
      LogSerial.printf("[gioco] accessorio %s crc %08lx rifiutato: %s\n", a->id, (unsigned long)a->crc, accErrorText(err));
      accNeeds.rejected(false, a->id, a->crc);
    }
    free(a);
  }
  AnchorsMsg *n = nullptr;
  while (net::takeAnchors(n)) {
    const AncError err = accStore.storeAnchors(n->avatar, n->avatarCrc, n->data, n->len);
    if (err == AncError::Ok) {
      LogSerial.printf("[gioco] ancore di %s crc %08lx valide\n", n->avatar, (unsigned long)n->avatarCrc);
      pending = mergeRefresh(pending, gUi.netAccessoryArrived(now));
    } else {
      LogSerial.printf("[gioco] ancore di %s crc %08lx rifiutate: %s\n", n->avatar, (unsigned long)n->avatarCrc, ancErrorText(err));
      accNeeds.rejected(true, n->avatar, n->avatarCrc);
    }
    free(n);
  }
  // accessory-need e anchors-need: subito per quelli nuovi, poi ogni 30 s (solo con il PC collegato).
  static Ms nextNeed = 0;
  if (m.sys.pc && now >= nextNeed) {
    nextNeed = now + 500;
    NetRequest r;
    memset(&r, 0, sizeof r);
    r.cmd = NetCmd::AccNeed;
    r.nAvatarIds = uint8_t(accNeeds.dueAccessories(m, now, r.avatarIds, 1));
    if (r.nAvatarIds) net::request(r);
    memset(&r, 0, sizeof r);
    r.cmd = NetCmd::AnchorsNeed;
    r.nAvatarIds = uint8_t(accNeeds.dueAnchors(m, now, r.avatarIds, r.avatarCrcs, kMaxSessions + 1));
    if (r.nAvatarIds) net::request(r);
  }
  // Riassunto cambiato: si salva solo se i byte cambiano (le scritture in flash restano poche).
  if (gUi.takePetChanged() && avFs) {
    uint8_t b[kPetBlobMax];
    const size_t len = packPet(m.pet, b, sizeof b);
    if (len && (len != petSavedLen || memcmp(b, petSaved, len) != 0)) {
      if (avfs::savePet(b, len)) {
        memcpy(petSaved, b, len);
        petSavedLen = len;
        LogSerial.printf("[gioco] riassunto salvato (%u byte)\n", unsigned(len));
      }
    }
  }
}

static void runSecurityOps(Ms now) {
  if (wantProtect) {
    wantProtect = false;
    const ProtectResult res = secrets::protect();
    const bool ok = res == ProtectResult::Ok || res == ProtectResult::Already;
    pending = mergeRefresh(pending, gUi.netKeysProtected(ok, now));
  }
  if (wantFactory) {
    wantFactory = false;
    secrets::factoryReset();
    setPrefs.clear();
    wdPrefs.clear();    // 0.9.0: orari e stato della giornata
    gamePrefs.clear();  // 0.9.0: record del gioco
    {  // 0.12.1: indirizzo manuale e ultimo PC per rete (li scrive il task di rete, solo quando cambiano)
      Preferences pc;
      if (pc.begin("calamaio-pc", false)) { pc.clear(); pc.end(); }
    }
#if CALAMAIO_PAD
    // 0.11.0: abbinamento del controller (NVS nimble_bond): a radio accesa lo cancella end(), prima del deinit.
    blepad::forgetAll();  // 0.14.0: Xbox, telefono e libro
    padBondNow = padXboxNow = padPhoneNow = padPairWanted = false;
    if (blepad::active() && !padRadioOff("ripristino di fabbrica", 3))
      LogSerial.println("[pad] ripristino di fabbrica: abbinamento del controller forse non cancellato (radio non spenta)");
    gbPrefs.clear();
    gUi.model().set.padOn = false;
    padSaved = false;
#endif
    memset(&gUi.model().work, 0, sizeof gUi.model().work);  // il loop non deve riscriverli prima del riavvio
    gUi.model().game.best = 0;
    memset(wdSaved, 0, sizeof wdSaved);
    gameSaved = 0;
    avCache.clear();
    avfs::forgetDefault();
    accStore.clear();  // 0.10.0: accessori, ancore e riassunto del gioco
    themefs::forget();  // 0.13.0: temi in /th e tema attivo nella NVS
    themeRt.activate("");
    themesToModel();
    avfs::forgetPet();
    petSavedLen = 0;
    memset(savedBlob, 0, sizeof savedBlob);
    LogSerial.println("[nvs] ripristino di fabbrica: impostazioni cancellate, riavvio fra 2,6 s");
    factoryRestartAt = millis() + 2600;  // il tempo di disegnare l'avvio
  }
  if (factoryRestartAt && millis() >= factoryRestartAt) ESP.restart();
}

static bool stateSeen = false;  // M6: almeno uno state da questo avvio (risveglio da timer)

static void onState(const StateMsg &msg, void *) {
  stateSeen = true;
  const Ms now = nowMs();
  pending = mergeRefresh(pending, gUi.netState(msg, now));
}

// status {batt, usb, cal, workday} al servizio (PROTOCOLLO.md «OTA» e «Giornata di lavoro»): cavo, calibrazione e
// giornata subito, batteria al massimo ogni 10 s. force: dopo un comando workday, prima dell'ack.
static ota::PowerReporter powerRep;
static void sendStatus(Ms now, bool force) {
  const Model &m = gUi.model();
  const UsbPresent u = usbMon.state();
  ota::PowerStatus ps;
  memset(&ps, 0, sizeof ps);
  ps.batt = lastMv > 0 ? lipoPercent(lastMv) : -1;
  ps.usb = u == UsbPresent::Yes ? 1 : u == UsbPresent::No ? 0 : -1;
  ps.cal = calRunning(m);
  ps.hasWork = true;
  ps.workOn = m.work.on && wallValid();
  ps.workEnd = ps.workOn ? workEndEpoch(m, wallAt(now), epochNowMs()) : 0;
  if (!powerRep.due(ps, now) && !force) return;
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = NetCmd::Status;
  r.battPct = int8_t(ps.batt);
  r.usbOn = int8_t(ps.usb);
  r.cal = ps.cal;
  r.hasWork = ps.hasWork;
  r.workOn = ps.workOn;
  r.workEnd = ps.workEnd;
  net::request(r);
}

// full-refresh: l'ack parte dopo il refresh completo vero (present()).
static char fullAcks[4][kCmdIdLen];
static int nFullAcks = 0;

// ota-offer già verificato dalla rete (mac, firma, scheda): qui le condizioni di docs/OTA.md §1.4–1.5, che sa il
// loop. Senza cavo la batteria si legge adesso (fino a 3 tentativi: con il Wi-Fi acceso l'ADC2 può fallire).
static RefreshKind otaOffer(const net::Event &e, Ms now) {
  Model &m = gUi.model();
  ota::Conditions c;
  memset(&c, 0, sizeof c);
  c.trial = otahw::trialActive();
  c.permOpen = powerInputs(m).permOpen;
  c.calibrating = calRunning(m);
  c.playing = gbmain::playing() || gUi.gbOnStack();  // 0.12.0: «rifiutato: partita in corso: riprova a fine partita»
  c.usb = usbMon.state();
  c.battPct = -1;
  if (c.usb != UsbPresent::Yes)
    for (int k = 0; k < 3 && c.battPct < 0; k++) {
      const int mv = battery::readMv(16);
      if (mv > 0) c.battPct = lipoPercent(mv);
    }
  char why[ota::kWhyLen] = "";
  bool shown = false;
  const bool ok = ota::conditionsOk(c, why, sizeof why, &shown);
  LogSerial.printf("[ota] condizioni per %s: cavo %s, batteria %d%%, permesso %d, calibrazione %d -> %s%s\n", e.id, usbName(c.usb),
                   c.battPct, c.permOpen, c.calibrating, ok ? "si parte" : "no: ", why);
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = NetCmd::OtaGo;
  scopy(r.id, e.id);
  r.ok = ok;
  scopy(r.error, why);
  net::request(r);
  if (ok) {
    power.key(now);  // Attivo subito (core/power: in.ota tiene sveglio per tutto l'aggiornamento)
    return gUi.otaStart(kFwVersion, e.text, uint32_t(e.time), now);
  }
  if (c.playing) return RefreshKind::None;  // 0.12.0: il rifiuto va al servizio, la partita resta a schermo
  return shown ? gUi.otaRejected(kFwVersion, e.text, why, now) : RefreshKind::None;
}

// Reti salvate dal task di rete (ultimo uso in ms epoch) → modello (ultimo uso in ora locale, come calDate).
static RefreshKind savedToModel(Ms now) {
  static SavedNet list[kSavedMax];
  const int n = net::takeSaved(list, kSavedMax);
  for (int k = 0; k < n; k++) {
    const int64_t ep = list[k].usedLocal;
    list[k].usedLocal = 0;
    if (ep <= 0) continue;
    const time_t s = static_cast<time_t>(ep / 1000);
    struct tm lt;
    localtime_r(&s, &lt);
    list[k].usedLocal = wallFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec).localMs;
  }
  return gUi.netSaved(list, n, now);
}

// 0.12.0, giochi Game Boy (hw/gb_main): quello che il loop sa e la partita no.
static gbmain::Env gbEnv(Ms now) { return gbmain::Env{usbOn(), critBatt.critical(), wallAt(now), &canvas}; }

// Eventi del task di rete. Durante l'avvio (2,6 s) si aspettano: l'avvio resta com'è nel simulatore.
static void pumpNet(Ms now) {
  if (top(gUi.model()).id == ScreenId::Boot) return;
  net::Event e;
  // 0.12.0: partita aperta (avvio compreso): la rete è spenta, quello che resta in coda è vecchio. Contano solo Wi-Fi e
  // PC persi (stato a schermo dopo l'uscita); il resto (bye, abbinamento, state, permessi…) cambierebbe la pila sotto
  // il gioco e si scarta: all'uscita il servizio rimanda tutto dopo il nuovo collegamento.
  // In avvio, con la rete ancora accesa, la coda aspetta: se la partita non parte tutto arriva com'era.
  const bool gaming = gbmain::playing() || gUi.gbOnStack();
  if (gaming && !net::suspended()) return;
  while (net::pollEvent(e)) {
    RefreshKind r = RefreshKind::None;
    if (gaming && e.type == net::EvType::OtaOffer) {  // rifiuto esplicito (c.playing), mai un'offerta senza risposta
      otaOffer(e, now);
      continue;
    }
    if (gaming && e.type == net::EvType::FullRefresh) {  // il completo non si fa in gioco: risposta subito
      sendAck(e.id, false, "partita in corso: riprova a fine partita");
      continue;
    }
    if (gaming && e.type != net::EvType::WifiLost && e.type != net::EvType::PcLost) {
      LogSerial.printf("[gb] evento di rete %d scartato: partita in corso\n", int(e.type));
      continue;
    }
    switch (e.type) {
      case net::EvType::ApReady: r = gUi.netApReady(e.text, now); break;
      case net::EvType::Connecting: r = gUi.netConnecting(e.ssid, now); break;
      case net::EvType::Connected: r = gUi.netConnected(e.ssid, e.rssi, e.text, now); break;
      case net::EvType::ConnectFailed: r = gUi.netConnectFailed(e.ssid, now); break;
      case net::EvType::WifiLost: r = gUi.netWifiLost(now); break;
      case net::EvType::Rssi: r = gUi.netRssi(e.rssi, now); break;
      case net::EvType::ScanDone: {
        static NetEntry nets[kNetCap];
        const int n = net::takeScan(nets, kNetCap);
        r = gUi.netScanDone(nets, n, now);
        break;
      }
      case net::EvType::PcHost: r = gUi.netPcHost(e.text, now); break;
      case net::EvType::PcLost:
        avNeeds.reset();  // alla prossima connessione si richiede subito
        accNeeds.reset();
        r = gUi.netPcLost(now);
        break;
      case net::EvType::Bye: {
        // M6: riepilogo «PC spento» con refresh completo; il sonno lo decide core/power dopo 60 s senza PC.
        avNeeds.reset();
        accNeeds.reset();
        r = gUi.netBye(now);
        power.bye(now);
        Model &m = gUi.model();
        const int64_t ep = wallValid() ? epochNowMs() : 0;
        if (e.time > 0 && ep && ep - e.time >= 0 && ep - e.time < 86400000LL) m.sys.offAt = m.sys.lastPc = now - (ep - e.time);
        break;
      }
      case net::EvType::Welcome: {
        if (e.text[0]) scopy(tzNow, e.text);  // fuso per il risveglio dal sonno profondo
        // Ora del muro dal PC (riserva: SNTP con lo stesso fuso, avviato dal task di rete).
        if (e.text[0]) { setenv("TZ", e.text, 1); tzset(); }
        if (e.time > 1700000000000LL) {
          struct timeval tv;
          tv.tv_sec = static_cast<time_t>(e.time / 1000);
          tv.tv_usec = static_cast<suseconds_t>((e.time % 1000) * 1000);
          settimeofday(&tv, nullptr);
        }
        char hm[8];
        hmLocal(wallAt(now).localMs, hm, sizeof hm);
        LogSerial.printf("[ora] dal PC «%s»: %s, fuso %s\n", e.ssid, hm, e.text[0] ? e.text : "(assente)");
        if (e.hasAvatar) gUi.model().set.defAvatar = e.avatar;  // avatarsLoop lo salva e ridisegna
        r = RefreshKind::Partial;  // l'ora a schermo cambia
        break;
      }
      case net::EvType::RemoteSet: {
        // Stesse conseguenze delle voci del menu; poi settings aggiornato e infine l'ack (PROTOCOLLO.md).
        char why[kAckErrLen];
        if (!checkPatch(e.patch, gUi.model().set, why, sizeof why)) {  // 0.9.0: orari con l'inizio prima della fine
          LogSerial.printf("[remoto] set %s rifiutato: %s\n", e.id, why);
          sendAck(e.id, false, why);
          break;
        }
        if (e.patch.on(RemoteKey::Mute)) nvsMuteOpen = false;  // il PC ha deciso il silenzio
        r = gUi.applyRemote(e.patch, now, wallAt(now));
        LogSerial.printf("[remoto] set %s: %s -> %s\n", e.id, gUi.lastAction(), refreshKey(r));
        settingsSync(now, true);
        sendAck(e.id, true, nullptr);
        break;
      }
      case net::EvType::Ack:  // Archivia: ok → resta nascosta; rifiutato → torna con «Non archiviata»
        r = gUi.netAck(e.id, e.rssi != 0, now);
        LogSerial.printf("[remoto] ack %s %s: %s\n", e.id, e.rssi ? "ok" : "rifiutato", gUi.lastAction());
        break;
      case net::EvType::PairCode: r = gUi.netPairCode(e.text, now); break;
      case net::EvType::PairReady: r = gUi.netPairReady(now); break;
      case net::EvType::PairDone: r = gUi.netPairDone(now); break;
      case net::EvType::PairAbort: r = gUi.netPairAborted(e.text, now); break;
      case net::EvType::Authed: r = gUi.netAuthed(now); break;
      case net::EvType::Redo: r = gUi.netRedo(now); break;
      case net::EvType::PermClose: r = gUi.netPermClose(e.id, now); break;
      case net::EvType::OtaOffer: r = otaOffer(e, now); break;
      case net::EvType::OtaPhase:
        r = gUi.otaPhase(static_cast<OtaPhase>(e.rssi), e.text, now);
        LogSerial.printf("[ota] schermata: %s%s%s\n", otaPhaseKey(static_cast<OtaPhase>(e.rssi)), e.text[0] ? " · " : "", e.text);
        break;
      case net::EvType::OtaUpdated: r = gUi.otaUpdated(kFwVersion, now); break;
      case net::EvType::Saved: r = savedToModel(now); break;
      case net::EvType::WifiForgotten:
        r = gUi.netForgotten(e.ssid, now);  // la scheda riavvia fra 3 s (pulizia profonda della NVS)
        LogSerial.printf("[remoto] wifi-forget: %s\n", gUi.lastAction());
        break;
      case net::EvType::Workday: {
        // 0.9.0: come Strumenti › Giornata di lavoro; poi status con workday aggiornato, subito, e infine l'ack.
        char why[kAckErrLen];
        r = gUi.workdayRemote(e.rssi != 0, now, wallAt(now), why, sizeof why);
        LogSerial.printf("[remoto] workday %s: %s\n", e.id, gUi.lastAction());
        sendStatus(now, true);
        sendAck(e.id, why[0] == 0, why);
        break;
      }
      case net::EvType::ThemeStored:
      case net::EvType::ThemeUse: r = themeEvent(e, now); break;
      case net::EvType::AvatarRejected:  // 0.14.0 (R4): come il ramo «rifiutato» di avatarsLoop
        LogSerial.printf("[avatar] %s crc %08lx rifiutato a pezzi: non lo richiedo fino alla prossima connessione\n", e.avatar.id,
                         (unsigned long)e.avatar.crc);
        avNeeds.rejected(e.avatar.id, e.avatar.crc);
        break;
      case net::EvType::FullRefresh:
        r = RefreshKind::Full;  // come Menu › Display › Refresh completo
        if (nFullAcks < 4) scopy(fullAcks[nFullAcks++], e.id);
        else sendAck(e.id, true, nullptr);  // già 4 in coda: lo stesso completo vale anche per questo
        LogSerial.printf("[remoto] full-refresh %s\n", e.id);
        break;
    }
    pending = mergeRefresh(pending, r);
  }
  // Barra dell'aggiornamento: Ui::otaProgress ridisegna solo ogni ~5% e non prima di 2 s.
  if (gUi.model().ota.phase == OtaPhase::Receive) pending = mergeRefresh(pending, gUi.otaProgress(net::otaDone(), now));
  static PermReq req;  // ~300 byte: fuori dallo stack del loop
  if (gaming) {  // rete spenta: nessuna richiesta di permesso a schermo (restano nel terminale); lo state aspetta
    while (net::takePerm(req)) LogSerial.printf("[gb] richiesta di permesso %s non mostrata: partita in corso\n", req.id);
    return;
  }
  while (net::takePerm(req)) pending = mergeRefresh(pending, gUi.netPermRequest(req, now, wallValid() ? epochNowMs() : 0));
  net::withState(onState, nullptr);
}
#endif

// ---------- energia (M6): stati, sonno profondo, risvegli ----------
static Preferences batPrefs;
static bool timerWake = false;          // risveglio dal timer con un'istantanea valida: tentativo veloce
static Ms probeEnd = 0;                 // fine del tentativo veloce (kProbeMs)
static SleepReason probeReason = SleepReason::None;
static bool keySinceBoot = false;
static int64_t ledgerAcctS = 0;         // ultimo istante (epoch s) messo nel registro dei tempi
static int64_t ledgerNvsS = 0;          // ultima copia del registro nella NVS
static constexpr int64_t kLedgerNvsEveryS = 6 * 3600;  // al massimo 4 scritture al giorno

static void batLoad() {
  batPrefs.begin("calamaio-bat", false);
  uint8_t b[kBatBlobLen];
  const size_t n = batPrefs.isKey("v1") ? batPrefs.getBytes("v1", b, sizeof b) : 0;
  BatInfo &bi = gUi.model().bat;
  BatInfo tmp = bi;
  if (n && unpackBat(b, n, tmp)) {
    bi.cal = tmp.cal;
    bi.hasSleep = tmp.hasSleep;
    bi.sleep = tmp.sleep;
    LogSerial.printf("[batteria] calibrazione %s: attivo %.2f %%/h, attesa %.2f %%/h, sonno %s%.2f %%/h\n", bi.cal.valid ? "salvata" : "assente",
                  bi.cal.active, bi.cal.idle, bi.hasSleep ? "" : "(non misurato) ", bi.hasSleep ? bi.sleep : kSleepRateDef);
  } else {
    LogSerial.println("[batteria] nessuna calibrazione salvata");
  }
  if (!ledgerValid(rtcLedger)) {  // spegnimento vero: si riparte dalla copia nella NVS (se c'è)
    memset(&rtcLedger, 0, sizeof rtcLedger);
    if (batPrefs.isKey("led") && batPrefs.getBytes("led", &rtcLedger.l, sizeof rtcLedger.l) == sizeof rtcLedger.l)
      LogSerial.println("[energia] registro dei tempi dalla NVS");
    ledgerSeal(rtcLedger);
  }
}

static void batSave() {
  uint8_t b[kBatBlobLen];
  const size_t n = packBat(gUi.model().bat, b, sizeof b);
  batPrefs.putBytes("v1", b, n);
  LogSerial.println("[batteria] calibrazione salvata nella NVS (calamaio-bat/v1)");
}

static void ledgerNvs(int64_t nowS) {
  batPrefs.putBytes("led", &rtcLedger.l, sizeof rtcLedger.l);
  ledgerNvsS = nowS;
}

// Tempo nello stato corrente → registro (ora del muro); quote delle ultime 24 h → Model::bat.mix.
static void ledgerTick(PowerState st) {
  if (!wallValid()) return;
  const int64_t nowS = epochNowMs() / 1000;
  if (!ledgerAcctS) ledgerAcctS = nowS;
  if (nowS - ledgerAcctS < 10) return;
  rtcLedger.l.add(st, nowS, nowS - ledgerAcctS);
  ledgerAcctS = nowS;
  int mix[3];
  if (rtcLedger.l.mix(nowS, mix)) memcpy(gUi.model().bat.mix, mix, sizeof mix);
  ledgerSeal(rtcLedger);
  if (!ledgerNvsS) ledgerNvsS = nowS;
  if (nowS - ledgerNvsS >= kLedgerNvsEveryS) ledgerNvs(nowS);
}

// Fine di un episodio di sonno (si resta svegli): se è durato almeno 4 h senza cavo, misura del sonno.
static void endSleepEpisode() {
  if (!rtcSnap.episodeEpochMs || !wallValid()) return;
  const int64_t secs = (epochNowMs() - rtcSnap.episodeEpochMs) / 1000;
  double r = 0;
  if (!rtcSnap.episodeUsb && !usbOn() && sleepRateSample(rtcSnap.episodeMv, lastMv, secs, r)) {
    BatInfo &b = gUi.model().bat;
    b.sleep = mergeSleepRate(b.hasSleep, b.sleep, r);
    b.hasSleep = true;
    LogSerial.printf("[energia] sonno misurato: %.2f %%/h su %lld min (media %.2f %%/h)\n", r, (long long)(secs / 60), b.sleep);
    batSave();
  } else {
    LogSerial.printf("[energia] episodio di sonno di %lld min: non misurato (servono 4 h senza cavo)\n", (long long)(secs / 60));
  }
  rtcSnap.episodeEpochMs = 0;
  snapSeal(rtcSnap);
}

// ---------- 0.10.1: ora del muro attraverso il sonno (core/wake) ----------
// Con CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER (sdkconfig del core 2.0.17) l'orologio di sistema conta sull'RTC e passa il
// sonno profondo: lo si verifica a ogni risveglio contro la copia salvata (epoch + contatore dell'RTC prima di dormire)
// e, se mancasse, si ricostruisce da lì. Uno spegnimento azzera l'RTC: niente copia valida, si aspetta welcome o SNTP.
static int64_t rtcUsNow() { return static_cast<int64_t>(esp_clk_rtc_time()); }

static void restoreWall() {
  static bool done = false;  // una volta per avvio (il risveglio dell'ora che passa al pieno l'ha già fatto)
  if (done) return;
  done = true;
  int64_t kept = 0;
  const bool ok = wallKeepRestore(rtcWall, rtcUsNow(), kept);
  if (wallValid()) {
    if (ok) LogSerial.printf("[ora] dopo il sonno l'orologio di sistema c'è; scarto dalla copia RTC %lld ms\n", (long long)(epochNowMs() - kept));
    return;
  }
  if (!ok || kept <= 1700000000000LL) {
    LogSerial.println("[ora] dopo il sonno nessuna ora valida: «--:--» finché non arriva welcome o SNTP");
    return;
  }
  struct timeval tv;
  tv.tv_sec = static_cast<time_t>(kept / 1000);
  tv.tv_usec = static_cast<suseconds_t>((kept % 1000) * 1000);
  settimeofday(&tv, nullptr);
  LogSerial.println("[ora] orologio di sistema perso nel sonno: ricostruito dalla copia RTC");
}

// Periferiche e timer, poi esp_deep_sleep_start(). timerMs = 0: solo il tasto.
static void sleepHw(Ms timerMs) {
  // Il pannello ha finito (update() è bloccante). La libreria: SX1262 in sleep, Vext spento (pannello senza
  // corrente: l'immagine e-ink resta), pin della radio in alta impedenza, NSS tenuto alto.
  Platform::prepareToSleep();
  battery::prepareSleep();  // partitore spento e GPIO19 bloccato ALTO
  // Vext: il gate di Q2 ha già R19 verso 3,3 V, ma GPIO45 (pin di strapping) senza blocco potrebbe tirarlo giù.
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, HIGH);
  gpio_hold_en(static_cast<gpio_num_t>(PIN_VEXT));
  gpio_deep_sleep_hold_en();
  // Tasto PRG: GPIO0 con R18 (10 kΩ) verso 3,3 V sullo schema V0.4. ext1 e non ext0: prepareToSleep() spegne
  // il dominio RTC_PERIPH, che ext0 richiede; ext1 funziona anche così.
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_BUTTON_PRG, ESP_EXT1_WAKEUP_ANY_LOW);
  if (timerMs > 0) esp_sleep_enable_timer_wakeup(uint64_t(timerMs) * 1000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}

// Prossimo risveglio (core/wake, planWake) e sua registrazione in rtcRest; poi l'ora per la verifica al risveglio.
static WakePlan planSleep(SleepReason r, const Model &m, Ms now, int64_t wifiDue) {
  WakeIn in;
  memset(&in, 0, sizeof in);
  in.reason = r;
  in.wallOk = wallValid();
  in.epochMs = in.wallOk ? epochNowMs() : 0;
  in.saver = powerSave(m);
  in.wifiDueMs = wifiDue;
  in.workOn = m.work.on && in.wallOk;
  in.workEndMs = in.workOn ? workEndEpoch(m, wallAt(now), in.epochMs) : 0;
  const WakePlan p = planWake(in);
  if (!restValid(rtcRest)) memset(&rtcRest, 0, sizeof rtcRest);  // imgOk = 0: niente risveglio dell'ora
  rtcRest.nextKind = static_cast<uint8_t>(p.kind);
  rtcRest.wifiDueMs = p.wifiDueMs;
  rtcRest.plannedMs = p.atMs;
  restStateFrom(rtcRest, m);
  restSeal(rtcRest);
  if (in.wallOk) wallKeepSave(rtcWall, epochNowMs(), rtcUsNow());
  return p;
}

static void logPlan(const WakePlan &p) {
  if (p.kind == WakeKind::None) return;
  char hms[16] = "?";
  if (p.atMs) {
    const time_t s = static_cast<time_t>(p.atMs / 1000);
    struct tm lt;
    localtime_r(&s, &lt);
    snprintf(hms, sizeof hms, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
  }
  LogSerial.printf("[energia] prossimo risveglio alle %s, fra %lld ms: %s%s\n", hms, (long long)p.sleepMs, wakeKindName(p.kind),
                   p.workEnd ? " (fine della giornata di lavoro)" : "");
}

// Pannello e periferiche, poi esp_deep_sleep_start(). keepEpisode: si torna a dormire da un risveglio da timer.
static void deepSleep(SleepReason r, bool keepEpisode) {
#if CALAMAIO_PAD
  if (blepad::active()) padRadioOff("Bluetooth spento per il sonno profondo", 3);
#endif
  workSync();  // 0.9.0: la giornata finita in questo risveglio va nella NVS prima di dormire
  const Model &m = gUi.model();
  const Ms now = nowMs();
  const int64_t ep = wallValid() ? epochNowMs() : 0;
  if (r == SleepReason::Battery) LogSerial.println("[energia] sonno profondo: batteria scarica, risveglio solo con il tasto");
  else LogSerial.printf("[energia] sonno profondo (%s): risveglio con il tasto o dal timer\n", sleepReasonName(r));
  // Istantanea nella memoria RTC: perché si dorme, il riepilogo, l'inizio dell'episodio di sonno.
  const bool hadEpisode = keepEpisode && snapValid(rtcSnap) && rtcSnap.episodeEpochMs;
  const int64_t epStart = hadEpisode ? rtcSnap.episodeEpochMs : ep;
  const int32_t epMv = hadEpisode ? rtcSnap.episodeMv : lastMv;
  const uint8_t epUsb = (hadEpisode && rtcSnap.episodeUsb) || usbOn();
  memset(&rtcSnap, 0, sizeof rtcSnap);
  rtcSnap.reason = static_cast<uint8_t>(r);
  rtcSnap.sleptEpochMs = ep;
  rtcSnap.episodeEpochMs = epStart;
  rtcSnap.episodeMv = epMv;
  rtcSnap.episodeUsb = epUsb;
  rtcSnap.usbAtSleep = usbOn();
  if (ep) snapFromModel(rtcSnap, m, ep, now);
  scopy(rtcSnap.tz, tzNow);
  snapSeal(rtcSnap);
  ledgerTick(PowerState::Sleep);
  if (!keepEpisode && wallValid()) ledgerNvs(epochNowMs() / 1000);
#if CALAMAIO_NET
  if (!net::shutdown(2000)) LogSerial.println("[energia] il task di rete non ha risposto: si dorme lo stesso");
#endif
  // 0.10.1 (core/wake): nel Riposo con l'ora vera risveglio dell'ora a ogni cambio di minuto (5 con il risparmio),
  // senza Wi-Fi; Wi-Fi e PC ogni 15 minuti (dall'ultima volta: da sveglio adesso, o dal risveglio pieno di prima);
  // la fine della giornata di lavoro sveglia proprio allora (0.9.0). Bye e senza ora: ogni 15 minuti come prima.
  const bool fromTimer = keepEpisode && restValid(rtcRest) && rtcRest.wifiDueMs > 0;
  const WakePlan plan = planSleep(r, m, now, fromTimer ? rtcRest.wifiDueMs : 0);
  logPlan(plan);
  sleepHw(plan.kind == WakeKind::None ? 0 : plan.sleepMs);
}

// ---------- 0.10.1: risveglio dell'ora ----------
// Riposo in sonno profondo, risveglio da timer previsto «ora» (core/wake, clockWakeOk): un parziale con l'ora nuova e di
// nuovo a dormire, senza Wi-Fi, NVS, LittleFS né avatar. Il modello si ricostruisce dalla memoria RTC (sessioni da
// rtcSnap; Inverti, Ruota e giornata da rtcRest); la mascotte si riprende dall'immagine salvata (ui::keepRect), così
// non serve caricarla. RAM vecchia del controller = immagine salvata, poi il parziale (Panel::writeOldRam). Un completo
// solo al tetto dei parziali (0.10.2: 2×40, core/refresh). false: serve un risveglio pieno (giornata che cambia, mascotte spostata, tasto,
// ora mancante); setup() prosegue come un risveglio da timer.
static bool keyEdge() { return edgeTail != edgeHead || digitalRead(PIN_BUTTON_PRG) == LOW; }

static bool clockWake() {
  if (!rtcSnap.usbAtSleep) usb::pins().setTx(false);  // senza cavo la TX resta staccata (CP2102 «di fantasma»)
  setCpuFrequencyMhz(80);  // l'APB resta a 80 MHz: SPI del pannello invariata
  pinMode(PIN_BUTTON_PRG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_PRG), onButton, CHANGE);
  restoreWall();
  if (!wallValid()) return false;
  const Ms now = nowMs();
  const WallClock wc = wallAt(now);
  const int64_t ep = epochNowMs();
  const int64_t late = rtcRest.plannedMs ? ep - rtcRest.plannedMs : 0;
  gUi.begin(now, wc);
  Model &m = gUi.model();
  restModel(m, rtcRest, rtcSnap, ep, now);
  if (workStep(m, wc) != WorkStep::None) {
    LogSerial.println("[orologio] la giornata di lavoro cambia: risveglio pieno");
    return false;
  }
#if CALAMAIO_NET
  // 0.13.0: Riposo di un tema. Il blocco serve per ridisegnarlo: LittleFS si monta senza mai formattare (avfs::begin
  // formatterebbe una partizione che non monta, cancellando temi e avatar) e senza scrivere; niente NVS.
  if (rtcRest.themeId[0]) {
    if (!themefs::mountNoFormat()) {
      LogSerial.println("[orologio] LittleFS non montato: risveglio pieno");
      return false;
    }
    themeRt.begin(&themefs::storage());
    // Ogni minuto: niente CRC completo (con theme_fs ogni lettura apre il file; un tema da 64 KB costerebbe circa 16
    // letture da 4 KB e il CRC a 80 MHz a ogni risveglio). Intestazione e indice contro RestKeep bastano: il blocco del
    // Riposo passa checkBlock quando si carica.
    if (!themeRt.activateKnown(rtcRest.themeId, rtcRest.themeCrc)) {
      LogSerial.println("[orologio] il tema del Riposo è cambiato o non si legge: risveglio pieno");
      return false;
    }
    ui::setThemeRuntime(&themeRt);
  }
#endif
  ui::compose(canvas, kHeltec213, m, 0, now, wc);
  // un blocco che non passa checkBlock disegna il Riposo di serie: il riquadro salvato non vale più (risveglio pieno,
  // dove themeBootPlan e themeBroken fanno il resto)
  if (rtcRest.themeId[0] && !ui::lastThemeDraw().drawn) {
    LogSerial.println("[orologio] il Riposo del tema non si disegna: risveglio pieno");
    return false;
  }
  const ui::SleepRect r = ui::sleepMascotRect(kHeltec213, m, wc);
  const ui::SleepRect kept = {rtcRest.mx, rtcRest.my, rtcRest.mw, rtcRest.mh};
  if (!(r == kept)) {
    LogSerial.println("[orologio] la mascotte si sposta: risveglio pieno");
    return false;
  }
  ui::keepRect(canvas, rtcRest.img, r, m.set.rot);
  if (keyEdge()) return false;
  const bool drawn = memcmp(canvas.data(), rtcRest.img, sizeof rtcRest.img) != 0;
  bool full = false;
  uint32_t dt = 0;
  if (drawn) {
    display.landscape();
    // 0.10.2: già nel Riposo, niente «entrata»: la pulizia dovuta aspetta il tetto (un completo ogni 2×fullEvery
    // risvegli disegnati al massimo, cioè ogni 80 minuti con l'ora a ogni minuto).
    counter.restore(rtcRest.partials, now);
    full = counter.apply(RefreshKind::Partial, false, now) == RefreshKind::Full;
    const uint32_t t0 = millis();
    if (full) {
      display.fastmodeOff();
    } else {
      display.fastmodeOn(false);
      blit(rtcRest.img);
      display.writeOldRam();
    }
    blit(canvas.data());
    display.update();
    dt = millis() - t0;
    memcpy(rtcRest.img, canvas.data(), sizeof rtcRest.img);
    rtcRest.partials = uint8_t(counter.partials());
  }
  if (keyEdge()) {
    restSeal(rtcRest);
    return false;
  }
  const WakePlan plan = planSleep(SleepReason::Rest, m, now, rtcRest.wifiDueMs);
  const uint32_t awake = millis();  // dall'avvio dell'applicazione (ROM e bootloader prima: vedi «ritardo»)
  rtcRest.nClock++;
  rtcRest.nDrawn += drawn;
  rtcRest.sumAwakeMs += awake;
  if (awake > rtcRest.maxAwakeMs) rtcRest.maxAwakeMs = uint16_t(awake > 65535 ? 65535 : awake);
  if (late > rtcRest.maxLateMs) rtcRest.maxLateMs = uint16_t(late > 65535 ? 65535 : late);
  restSeal(rtcRest);
  char hm[8];
  hmLocal(wc.localMs, hm, sizeof hm);
  LogSerial.printf("[orologio] %s: %s %lu ms · ritardo dal timer %lld ms · sveglio %lu ms prima di dormire\n", hm,
                   !drawn ? "niente da ridisegnare," : full ? "completo (tetto dei parziali)" : "parziale",
                   (unsigned long)dt, (long long)late, (unsigned long)awake);
  logPlan(plan);
  sleepHw(plan.kind == WakeKind::None ? 0 : plan.sleepMs);
  return true;  // non si arriva qui
}

// Risvegli dell'ora dall'ultimo risveglio pieno (nel registro: `calamaio logs`), poi si ricomincia a contare.
static void logClockStats() {
  if (!restValid(rtcRest) || !rtcRest.nClock) return;
  LogSerial.printf("[orologio] dall'ultimo risveglio pieno: %u risvegli dell'ora, %u con refresh, sveglio in media %lu ms "
                   "(massimo %u), ritardo massimo dal timer %u ms\n", rtcRest.nClock, rtcRest.nDrawn,
                   (unsigned long)(rtcRest.sumAwakeMs / rtcRest.nClock), rtcRest.maxAwakeMs, rtcRest.maxLateMs);
  rtcRest.nClock = rtcRest.nDrawn = 0;
  rtcRest.sumAwakeMs = 0;
  rtcRest.maxAwakeMs = rtcRest.maxLateMs = 0;
  restSeal(rtcRest);
}

static void logPower(PowerState from) {
  LogSerial.printf("[energia] %s -> %s: %s\n", powerStateName(from), powerStateName(power.state()), power.why());
}

// Wi-Fi, CPU e modello secondo lo stato (Attivo/Attesa). Il sonno lo esegue il loop quando è il momento.
static void applyPower(PowerState st, Ms now) {
  Model &m = gUi.model();
  if (st == PowerState::Sleep) return;
  const bool idle = st == PowerState::Idle;
  if (m.sys.idle != idle) {
    m.sys.idle = idle;
    if (!idle) pending = mergeRefresh(pending, RefreshKind::Partial);  // riparte l'animazione, se serve
  }
  // 0.11.0: col Bluetooth voluto o ancora acceso mai «nessun risparmio» (coesistenza, core/power).
#if CALAMAIO_PAD
  padPsBle = m.set.padOn || blepad::active();
  const bool ble = padPsBle;
#else
  const bool ble = false;
#endif
#if CALAMAIO_NET
  net::setPowerSave(static_cast<net::PowerSave>(wifiPowerSave(calibMax(m), idle, ble)));
#else
  (void)ble;
#endif
  const uint32_t mhz = idle ? 80 : 240;  // Wi-Fi: almeno 80 MHz; l'APB resta a 80 MHz (SPI del pannello invariata)
  if (getCpuFrequencyMhz() != mhz) {
    Serial.flush();
    setCpuFrequencyMhz(mhz);
    LogSerial.printf("[energia] CPU a %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  }
  if (idle) readBattery(now);  // in Attesa una lettura subito (poi ogni 60 s come sempre)
}

// Valuta lo stato di alimentazione; con il sonno pronto (tasto su, schermo pulito) si addormenta.
static void powerLoop(Ms now) {
  Model &m = gUi.model();
  if (top(m).id == ScreenId::Sleep) power.rest(now);
  else power.cancelRest();
  PowerInputs in = powerInputs(m);
  in.usb = usbOn();
  in.battCritical = critBatt.critical();
#if CALAMAIO_NET
  in.ota = in.ota || otahw::trialActive();  // nuova versione in prova: niente sonno (il risveglio la scarterebbe)
  in.playing = in.playing || gUi.gbOnStack() || gbmain::playing();  // 0.12.0: Attivo per tutta la partita (pausa compresa)
#endif
  const PowerState before = power.state();
  static bool wasCalibMax = false;
  if (power.update(in, now)) {
    logPower(before);
    if (power.state() == PowerState::Sleep && power.reason() == SleepReason::Battery && top(m).id != ScreenId::BattLow)
      pending = mergeRefresh(pending, gUi.battLow(now));
    applyPower(power.state(), now);
  } else if (calibMax(m) != wasCalibMax) {
    applyPower(power.state(), now);  // fase 1 della calibrazione: Wi-Fi senza risparmio
  }
  wasCalibMax = calibMax(m);
  ledgerTick(power.state());
  if (power.state() != PowerState::Sleep) return;
  // Prima di dormire: tasto rilasciato (ext1 sveglierebbe subito), ultimo disegno fatto. 0.10.2: completo solo con
  // almeno 10 parziali dall'ultimo (PanelRefresh::beforeSleep); il parziale dopo il sonno usa rtcRest.img, non il completo.
  if (gestures.pressed() || digitalRead(PIN_BUTTON_PRG) == LOW || edgeTail != edgeHead) return;
  if (gUi.gbOnStack() || gbmain::playing()) return;  // 0.12.0: prima si salva e si esce (gbmain::loop, batteria critica)
  const RefreshKind last = counter.beforeSleep(pending);
  if (last != RefreshKind::None) { pending = last; return; }
  deepSleep(power.reason(), false);
}

// ---------- controller Bluetooth (0.11.0: core/pad, hw/blepad) ----------
// Bluetooth solo con Settings::padOn («calamaio-gb/pad»: da «Associa controller» a «Dimentica controller») e un
// controller associato o in associazione; mai nel sonno profondo né nei risvegli dal timer. Prima il risparmio del
// Wi-Fi, poi la radio (coesistenza).
#if CALAMAIO_PAD
static void padLoad() {
  gbPrefs.begin("calamaio-gb", false);
  padSaved = gbPrefs.getUChar("pad", 0) == 1;
  Model &m = gUi.model();
  m.set.padOn = padSaved;
  m.sys.padHw = true;
  padXboxNow = m.sys.padBonded = blepad::bonded(pad::Kind::Xbox);
  padPhoneNow = m.sys.padPhoneBonded = blepad::bonded(pad::Kind::Phone);
  padBondNow = padXboxNow || padPhoneNow;
  LogSerial.printf("[pad] Bluetooth %s, controller %s, telefono %s\n", padSaved ? "acceso" : "spento",
                   padXboxNow ? "associato" : "no", padPhoneNow ? "associato" : "no");
}

// F4: sorgente dello stato che ha prodotto gli eventi (State::src), mai il tipo del collegamento di adesso: un report
// del telefono ancora in coda quando la scheda è già passata all'Xbox resta del telefono. Per tick e reset vale la
// sorgente dell'ultimo stato consegnato.
static pad::Kind padSrc = pad::Kind::Xbox;

static void padHandle(const pad::MenuEvent *ev, int n, Ms now, pad::Kind src) {
  for (int i = 0; i < n; i++) {
    if (ev[i] == pad::MenuEvent::HoldDown && gestures.pressed()) continue;  // tasto PRG giù: il tenuto è suo
    power.key(now);
    counter.input(now);  // 0.10.2: come un tasto
    keySinceBoot = true;
    padSearchFrom = millis();
    const RefreshKind r = gUi.padEvent(ev[i], now, wallAt(now), src);
    LogSerial.printf("[pad] %s -> %s\n", pad::menuEventName(ev[i]), gUi.lastAction());
    pending = mergeRefresh(pending, r);
  }
}

static void padLoop(Ms now) {
  Model &m = gUi.model();
  const uint32_t ms = millis();
  const PadReq req = gUi.takePadReq();
  if (req == PadReq::Pair || req == PadReq::PairPhone) {
    padPairKind = req == PadReq::PairPhone ? pad::Kind::Phone : pad::Kind::Xbox;
    padPairWanted = true;
    padPairSent = false;
    padSearchFrom = ms;
  } else if (req == PadReq::Forget || req == PadReq::ForgetPhone) {
    const pad::Kind k = req == PadReq::ForgetPhone ? pad::Kind::Phone : pad::Kind::Xbox;
    blepad::forget(k);  // a radio accesa lo esegue il task, al più tardi end()
    if (k == pad::Kind::Phone) padPhoneNow = false;
    else padXboxNow = false;
    padBondNow = padXboxNow || padPhoneNow;
    padPairWanted = false;
    LogSerial.printf("[pad] %s dimenticato\n", pad::kindName(k));
  } else if (req == PadReq::CancelPair) {
    padPairWanted = false;
    blepad::cancelPairing();
    LogSerial.println("[pad] associazione del telefono annullata");
  }
  if (m.set.padOn != padSaved) {
    padSaved = m.set.padOn;
    gbPrefs.putUChar("pad", padSaved ? 1 : 0);
    LogSerial.printf("[nvs] controller %s (calamaio-gb/pad)\n", padSaved ? "acceso" : "spento");
    padSearchFrom = ms;
  }
  if (!m.set.padOn) padPairWanted = false;
  if (!timerWake && (m.set.padOn || blepad::active()) != padPsBle) applyPower(power.state(), now);
  if (blepad::active()) {
    padXboxNow = blepad::bonded(pad::Kind::Xbox);
    padPhoneNow = blepad::bonded(pad::Kind::Phone);
    padBondNow = padXboxNow || padPhoneNow;
  }
  if (padPairWanted && blepad::link() == pad::Link::Pairing) padPairWanted = false;  // ora la tiene accesa il link
  if (padPairWanted && padPairSent && ms - padPairAt >= 10000) {
    padPairWanted = false;
    LogSerial.println("[pad] associazione non partita in 10 s");
  }
  pad::RadioIn in;
  in.setting = m.set.padOn && (padBondNow || padPairWanted || blepad::link() == pad::Link::Pairing);
  in.asleep = timerWake || power.state() == PowerState::Sleep;
#if CALAMAIO_NET
  in.wifiPsOk = net::appliedPowerSave() != 0;
#else
  in.wifiPsOk = true;
#endif
  in.usb = usbOn();
  in.idle = power.state() == PowerState::Idle;
  in.link = blepad::link();
  in.now = ms;
  in.searchFrom = int32_t(ms - padSearchFrom) < 0 ? ms : padSearchFrom;  // mai dopo now
  const pad::RadioOut out = pad::radioPlan(in);
  if (out.radio && !blepad::active()) {
    heapLine("prima del Bluetooth");
    blepad::begin();
    heapLine("Bluetooth acceso");
    padSearchFrom = padHeapAt = ms;
    padEndFailed = false;
  } else if (!out.radio && blepad::active() && (!padEndFailed || ms - padEndFailAt >= kPadEndRetryMs)) {
    padRadioOff("Bluetooth spento", 1);  // non riuscito: si riprova fra kPadEndRetryMs
  }
  if (blepad::active()) {
    blepad::setScan(out.scan);
    if (padPairWanted && !padPairSent) {
      padPairSent = true;
      padPairAt = ms;
      blepad::startPairing(padPairKind);
      LogSerial.println(padPairKind == pad::Kind::Phone ? "[pad] associazione: 2 minuti per l'app Calamaio (scheda Controller)"
                                                        : "[pad] associazione: 2 minuti per il tasto di associazione del controller");
    }
    if (ms - padHeapAt >= 600000) {
      padHeapAt = ms;
      heapLine("Bluetooth acceso (ogni 10 min)");
    }
  }
  const pad::Link l = blepad::link();
  const pad::Kind kd = blepad::kind();
  const int batt = blepad::battery();
  // I1 (round 1): fronti col contatore di collegamento, così anche Ready → Ready in un solo giro (Xbox → telefono,
  // Xbox → Xbox) chiude la tenuta di prima: il MenuMapper si azzera prima di mappare qualunque stato nuovo.
  const uint8_t conn = blepad::connId();
  const pad::LinkEdge edge = pad::linkEdge(padSeen, padConnSeen, l, conn);
  padConnSeen = conn;
  if (edge.reset) {
    pad::MenuEvent ev[4];
    padHandle(ev, padMenu.reset(ev, 4), now, padSrc);  // solo HoldUp, mai Select né HoldFire
  }
  if (l != padSeen || edge.lost || edge.gained || kd != padKindSeen || batt != padBattSeen ||
      padXboxNow != m.sys.padBonded || padPhoneNow != m.sys.padPhoneBonded) {
    if (edge.lost) {
      padSearchFrom = ms;
      heapLine(padKindSeen == pad::Kind::Phone ? "telefono scollegato" : "controller scollegato",
               padKindSeen == pad::Kind::Phone);
    }
    if (edge.gained) {
      heapLine(kd == pad::Kind::Phone ? "telefono collegato" : "controller collegato", kd == pad::Kind::Phone);
      padPhoneHeapAt = ms;
    }
    if (l != padSeen || edge.gained) LogSerial.printf("[pad] %s -> %s (%s)\n", pad::linkKey(padSeen), pad::linkKey(l), pad::kindName(kd));
    padSeen = l;
    padKindSeen = kd;
    padBattSeen = batt;
    pending = mergeRefresh(pending, gUi.padLinkChanged(l, kd, padXboxNow, padPhoneNow, batt, now));
  }
  if (l == pad::Link::Ready && kd == pad::Kind::Phone && ms - padPhoneHeapAt >= 60000) {
    padPhoneHeapAt = ms;
    heapLine("telefono collegato (ogni minuto)", true);  // misura della spec §6: heap e blocco massimo col telefono
  }
  uint32_t key;
  if (blepad::takePasskey(key)) pending = mergeRefresh(pending, gUi.padPasskey(key, now));  // mai nel registro
  if (blepad::takePairFailed()) pending = mergeRefresh(pending, gUi.padPairFailed(now));
  pad::Kind lost;
  if (blepad::takeBondLost(lost)) {
    LogSerial.printf("[pad] %s: abbinamento perso, va riassociato\n", pad::kindName(lost));
    pending = mergeRefresh(pending, gUi.padBondLost(lost, now));
  }
  pad::MenuEvent ev[8];
  if (gUi.gbPlaying()) {  // partita del Game Boy (piano B): blepad::poll lo legge solo il gioco, menu sospesi
    padMenu.reset(ev, 8);  // un'eventuale tenuta di A si annulla; gli eventi si scartano
    return;
  }
  // Ora d'ARRIVO di ogni report (polledAt), mai quella della lettura: una pressione breve arrivata durante un refresh
  // lungo non diventa un tenuto di 3 s. Prima tutti i report, poi tick (hw/blepad.h).
  pad::State s;
  // 0.14.0: il «tutto rilasciato» del mantenimento del telefono come reset (mai Select); tick con l'orologio limitato
  while (blepad::poll(s)) {
    padSrc = s.src;  // F4
    padHandle(ev, pad::mapPolled(padMenu, s, blepad::polledAt(), blepad::polledRelease(), ev, 8), now, s.src);
  }
  padHandle(ev, padMenu.tick(blepad::tickClock(millis()), ev, 8), now, padSrc);
}
#endif

// Risveglio dal timer: niente avvio a schermo, rete in background; dopo lo state (o kProbeMs) si decide.
static void probeLoop(Ms now) {
  Model &m = gUi.model();
  const PowerInputs in = powerInputs(m);
  const bool pcFound = m.sys.pc && stateSeen;
  if (!(pcFound || usbOn() || keySinceBoot || now >= probeEnd)) return;
  const TimerWakeOutcome o = timerWakeDecision(probeReason, pcFound, in.busy || in.permOpen, usbOn(), keySinceBoot);
  LogSerial.printf("[energia] risveglio da timer (%s): %s · %s\n", sleepReasonName(probeReason), o.why,
                o.stayAwake ? "resto sveglio" : "torno a dormire");
  timerWake = false;
  if (!o.stayAwake) {
    if (o.redraw) present(RefreshKind::Partial, now);  // Riposo: ora e sessioni aggiornate (0.10.1: parziale, RAM da rtcRest)
    deepSleep(probeReason, true);
  }
  if (top(m).id == ScreenId::Sleep && probeReason == SleepReason::Rest) popScreen(m);
  power.begin(now);
  if (keySinceBoot) power.key(now);
  endSleepEpisode();
  pending = RefreshKind::None;
  // 0.10.2: resto sveglio (PC, cavo, tasto): se nessun disegno c'è stato, l'immagine del sonno è ancora in rtcRest
  // e il primo disegno è un parziale che conta 2, come il risveglio col tasto.
  present(primeOld ? RefreshKind::Quick : RefreshKind::Full, now);
}

void setup() {
  Serial.begin(115200);
  logtee::begin(kFwVersion);  // prima riga di registro: anello in RAM e coda RTC (docs/OTA.md §5, §6)
  bootUs = esp_timer_get_time();
  buildLocalMs = parseBuildTime();
  // M6: risveglio dal sonno profondo? Prima di tutto si tolgono i blocchi messi ai pin prima di dormire (il partitore
  // della batteria resta bloccato spento fino a sotto: il risveglio dell'ora non lo legge).
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const bool fromSleep = cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_EXT1;
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(static_cast<gpio_num_t>(PIN_VEXT));
  const bool snapOk = fromSleep && snapValid(rtcSnap);
  const SleepReason slept = snapOk ? static_cast<SleepReason>(rtcSnap.reason) : SleepReason::None;
  if (snapOk && rtcSnap.tz[0]) scopy(tzNow, rtcSnap.tz);
  setenv("TZ", snapOk && rtcSnap.tz[0] ? tzNow : kDefaultTz, 1);
  tzset();
  // 0.10.1: risveglio dell'ora (Riposo in sonno profondo): parziale e di nuovo a dormire, senza il resto dell'avvio.
  if (cause == ESP_SLEEP_WAKEUP_TIMER && clockWakeOk(rtcRest, snapOk, slept)) {
    clockWake();  // torna solo se serve un risveglio pieno
    setCpuFrequencyMhz(240);
    LogSerial.println("[orologio] si passa al risveglio pieno (Wi-Fi e ricerca del PC)");
  }
  delay(200);
  battery::releaseHold();
  rng = Rng(esp_random());
  timerWake = cause == ESP_SLEEP_WAKEUP_TIMER && snapOk && slept != SleepReason::Battery;
  LogSerial.printf("\nCalamaio OS %s · %s · %s %dx%d · ora di compilazione %s %s\n", kFwVersion, kFwTag, BOARD_NAME, BOARD_W,
                   BOARD_H, __DATE__, __TIME__);
  LogSerial.printf("[energia] avvio: %s%s%s\n",
                cause == ESP_SLEEP_WAKEUP_TIMER ? "risveglio dal timer" : cause == ESP_SLEEP_WAKEUP_EXT1 ? "risveglio dal tasto" : "accensione o riavvio",
                snapOk ? " · si dormiva per " : "", snapOk ? sleepReasonName(slept) : "");
#ifdef CONFIG_PM_ENABLE
  LogSerial.println("[energia] light sleep automatico: CONFIG_PM_ENABLE attivo (non usato)");
#else
  LogSerial.println("[energia] light sleep automatico non disponibile: CONFIG_PM_ENABLE spento nel core 2.0.17 (l'IDF non si ricompila)");
#endif
  // 0.14.1: heap interno e RTC FAST separati (blocco massimo di ciascuno) e minimo di stack libero del loopTask.
  LogSerial.printf("[memoria] avvio: Ui %u byte, canvas %u byte, heap libero %u, interna %u (blocco %u), RTC %u (blocco %u), "
                   "stack loop libero %u\n",
                   (unsigned)sizeof(Ui), (unsigned)sizeof(canvas), (unsigned)ESP.getFreeHeap(),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_RTCRAM),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_RTCRAM),
                   (unsigned)uxTaskGetStackHighWaterMark(nullptr));

  pinMode(PIN_BUTTON_PRG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_PRG), onButton, CHANGE);

  display.landscape();
  fastink::attachBus(display.bus(), panelBack, &LogSerial);  // 0.11.0: il driver veloce usa lo stesso bus
  if (!fromSleep) {  // dopo il sonno il pannello mostra ancora l'ultima immagine pulita: niente pulizia
    const uint32_t t0 = millis();
    display.clear();  // pulizia completa del pannello
    LogSerial.printf("[refresh] pulizia iniziale %lu ms\n", (unsigned long)(millis() - t0));
    if (restValid(rtcRest)) { rtcRest.imgOk = 0; restSeal(rtcRest); }  // l'immagine salvata non è più sul pannello
  } else {
    restoreWall();     // 0.10.1: ora del muro dopo il sonno (verifica, o ricostruzione dalla copia RTC)
    logClockStats();   // risvegli dell'ora dall'ultimo risveglio pieno
  }

  gUi.setPermListener(onPerm, nullptr);
  gUi.setRandom(esp_random);  // 0.9.0: carte del gioco (casuale vero; sulla scheda niente seme fisso)
  const Ms now = nowMs();
  const RefreshKind first = gUi.begin(now, wallAt(now));
  gUi.model().sys.wallOk = wallValid();
#if CALAMAIO_NET
  secrets::bootScrub();  // pulizia profonda della NVS: prima di ogni handle NVS (impostazioni, rete, avatar)
#endif
  settingsLoad();  // prima del primo disegno: inverti e ruota valgono già per l'avvio
  workLoad();      // 0.9.0: orari e record; la giornata attiva torna con l'ora vera (workSync)
  workSync();
#if CALAMAIO_NET
  avatarsBegin();  // cache e predefinito prima del primo disegno
  themesBegin(now);  // 0.13.0: tema attivo (controllato tutto) prima del primo disegno
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char boardId[8];
  boardIdFromMac(mac, boardId, sizeof boardId);
  secrets::begin();  // vault, eFuse, copia di sistema del Wi-Fi; prima della rete
  otahw::captureCrash(kFwVersion);  // motivo del riavvio, coredump, ultime righe RTC: si mandano dopo auth-ok
  gUi.goLive();
  scopy(gUi.model().sys.boardId, boardId);
  {
    Sys &y = gUi.model().sys;
    uint8_t K[32];
    y.pair = secrets::loadPair(K, y.pairHost, sizeof y.pairHost) ? PairState::Paired : PairState::None;
    cry::wipe(K, sizeof K);
    y.keysProtected = secrets::keysProtected();
  }
  gUi.setNetListener(onNetRequest, nullptr);
  petBegin();  // 0.10.0: accessori e ultimo riassunto del gioco, prima del primo disegno
  gbmain::begin(gUi);  // 0.12.0: partizione «gbrom» e ROM scritta col cavo (LittleFS già montato da avatarsBegin)
#endif
#if CALAMAIO_PAD
  padLoad();  // 0.11.0: impostazione e abbinamento; la radio la accende padLoop()
#endif
  batLoad();
  Model &m = gUi.model();
  // Registro dei tempi: il sonno appena finito (l'ora del muro passa il sonno profondo).
  if (snapOk && wallValid() && rtcSnap.sleptEpochMs) {
    const int64_t nowS = epochNowMs() / 1000;
    rtcLedger.l.add(PowerState::Sleep, nowS, nowS - rtcSnap.sleptEpochMs / 1000);
    ledgerSeal(rtcLedger);
    LogSerial.printf("[energia] dormito %lld min\n", (long long)((nowS - rtcSnap.sleptEpochMs / 1000) / 60));
  }
  // Riepilogo «PC spento» (bye) e sessioni del Riposo dall'istantanea, con l'ora del muro che il sonno conserva.
  if (snapOk && wallValid() && (slept == SleepReason::Bye || (slept == SleepReason::Rest && timerWake)))
    snapToModel(rtcSnap, m, epochNowMs(), now);
  // Prima lettura prima del Wi-Fi (l'ADC2 è libero) e del primo disegno. Dal sonno due letture: la batteria
  // critica ne vuole due di fila.
  battery::begin();
  readBattery(now);
  if (fromSleep) readBattery(now);
  readUsb(now, true);
  pending = RefreshKind::None;  // il primo disegno è già first
  power.begin(now);
  if (fromSleep && critBatt.critical() && !usbOn()) {  // ancora scarica: di nuovo a dormire, solo col tasto
    present(gUi.battLow(now), now);
    deepSleep(SleepReason::Battery, true);
  }
#if CALAMAIO_NET
  net::begin(boardId);
  themeReport();  // 0.13.0: la rete lo dice nel themes dopo auth-ok
  scopy(m.sys.ssid, net::savedNetwork());  // per l'avvio: «Wi-Fi CasaWiFi …»
#endif
  settingsSync(now);  // con la rete: la prima copia per il settings dopo hello
  if (timerWake) {
    // 0.10.1: l'immagine sul pannello è in rtcRest: il ridisegno del Riposo può essere un parziale (present).
    if (wakePartialOk(rtcRest, fromSleep)) {
      primeOld = true;
      shownRest = rtcRest.rest;  // 0.10.2: già nel Riposo, il ridisegno non è un'«entrata»
      counter.restore(rtcRest.partials, now);
    }
    // Niente avvio a schermo: l'immagine del sonno resta finché non si decide (probeLoop).
    resetStack(m, makeScreen(ScreenId::Home));
    if (slept == SleepReason::Rest) pushScreen(m, makeScreen(ScreenId::Sleep));
    probeReason = slept;
    probeEnd = now + kProbeMs;
    LogSerial.printf("[energia] risveglio da timer: cerco il PC per al massimo %lu s\n", (unsigned long)(kProbeMs / 1000));
    return;
  }
  if (fromSleep) endSleepEpisode();  // risveglio dal tasto
  // 0.10.2: dal sonno col tasto l'immagine sul pannello è in rtcRest: parziale come un Quick (conta 2), completo solo
  // per la regola di PanelRefresh (tetto) o senza immagine valida (accensione, reset per crash o brownout).
  if (wakePartialOk(rtcRest, fromSleep)) {
    primeOld = true;
    shownRest = rtcRest.rest;
    counter.restore(rtcRest.partials, now);
  }
  present(primeOld ? RefreshKind::Quick : first, now);
}

void loop() {
  // Fronti raccolti dall'interrupt, in ordine.
  while (edgeTail != edgeHead) {
    Edge e{edges[edgeTail].t, edges[edgeTail].down};
    edgeTail = (edgeTail + 1) % 32;
    const Ms now = nowMs();
#if CALAMAIO_NET
    // 0.13.0: uscita di emergenza contata dall'istante del fronte (l'interrupt), non dal loop: il setup e il primo
    // completo bloccano il loop per buona parte dell'avvio. Prima del gesto: quello del rilascio si può scartare.
    safeBoot.edge(e.down, Ms(e.t), top(gUi.model()).id == ScreenId::Boot);
#endif
    if (e.down) {
      power.key(now);
      counter.input(now);  // 0.10.2: la pulizia d'inattività aspetta altri 30 s  // M6: un tasto tiene Attivo per 60 s
      keySinceBoot = true;
#if CALAMAIO_PAD
      padSearchFrom = millis();  // un tasto: il controller si cerca di nuovo per 2 minuti
#endif
      const bool was = gestures.pressed();
      gestures.down(e.t);
      if (!was && gestures.pressed()) pending = mergeRefresh(pending, gUi.keyDown(now));  // indicatore del tenuto 3 s
    } else {
      const bool was = gestures.pressed();
      const Gesture g = gestures.up(e.t);  // None se il tenuto 3 s è già scattato con il tasto giù
      if (was && !gestures.pressed()) pending = mergeRefresh(pending, gUi.keyUp(now));
      if (g != Gesture::None) onGesture(g);
    }
  }
  const Gesture g = gestures.poll(millis());  // anche VeryLong a 3 s con il tasto ancora giù
  if (g != Gesture::None) onGesture(g);
#if CALAMAIO_NET
  // Uscita di emergenza (spec §3): PRG tenuto 1,5 s dalla pressione, cominciata con la schermata di avvio → tema di serie
  // per questo avvio. Non all'accensione col tasto già giù: GPIO0 è il pin di boot (la ROM entrerebbe nel caricatore
  // seriale); la pressione deve cominciare dopo, ed è il fronte dell'interrupt che conta.
  if (safeBoot.keepBoot(Ms(millis()))) {  // pressione dell'avvio da decidere: la schermata di avvio resta (tetto 3 s)
    ScreenEntry &tp = top(gUi.model());
    if (tp.id == ScreenId::Boot && tp.until < nowMs() + 200) tp.until = nowMs() + 200;
  }
  if (safeBoot.step(Ms(millis())) && themeRt.active()[0]) {
    themeRt.suspend(true);
    themesToModel();  // il menu mostra Calamaio (e themeSync non lo riattiva)
    toast(gUi.model(), "Tema di serie", nowMs(), 5000);  // 5 s: l'avvio non disegna i toast, si vede sulla prima Home
    LogSerial.println("[temi] PRG all'avvio: tema di serie per questo avvio");
  }
#endif

  static Ms nextTick = 0, nextStep = 0, nextSlow = 0, nextBatt = 0, nextUsb = 0;
  const Ms now = nowMs();
  const WallClock wc = wallAt(now);
  if (!nextStep) {
    nextStep = now + 4200;
    nextSlow = now + 10000;
    nextBatt = now + kBattEveryMs;
    nextUsb = now + kUsbEveryMs;
  }
  // 0.10.1: l'ora vera appena arrivata (welcome o SNTP) sostituisce subito «--:--».
  if (wallValid() != gUi.model().sys.wallOk) {
    gUi.model().sys.wallOk = wallValid();
    if (gUi.model().sys.wallOk) {
      LogSerial.println("[ora] ora vera disponibile: l'orologio a schermo si aggiorna");
      pending = mergeRefresh(pending, RefreshKind::Partial);
    }
  }
  workSync();  // prima del tick: la giornata salvata torna appena c'è l'ora vera
#if CALAMAIO_NET
  themeSync(now);  // 0.13.0: tema scelto dal menu, tema rotto, ridisegno a tempo, ritorno dalla partita
#endif
#if CALAMAIO_PAD
  padLoop(now);
#endif
  if (now >= nextTick) {
    nextTick = now + 100;
    const bool wasOn = gUi.model().work.on;
    pending = mergeRefresh(pending, gUi.tick(now, wc));
    // Risveglio da timer alla fine della giornata: «GIORNATA FINITA» subito, senza aspettare la ricerca del PC.
    if (timerWake && wasOn && !gUi.model().work.on) present(RefreshKind::Full, now);
  }
  if (now >= nextStep) {
    nextStep = now + 4200;
    pending = mergeRefresh(pending, gUi.step(now, wc, rng));  // con la rete non fa niente (sys.live)
  }
#if CALAMAIO_NET
  pumpNet(now);
  avatarsLoop(now);
  petLoop(now);
  runSecurityOps(now);
  otahw::checkTrial();  // 120 s senza Wi-Fi + autenticazione dopo un aggiornamento: torna la versione di prima
  // Cambio automatico di rete: mai con un permesso aperto o durante la calibrazione (l'OTA lo sa la rete).
  net::setRoamGuard(powerInputs(gUi.model()).permOpen, calRunning(gUi.model()));
  sendStatus(now, false);  // status {batt, usb, cal, workday}: solo quando serve (ota::PowerReporter)
#endif
  if (now >= nextSlow) {
    nextSlow = now + 10000;
    pending = mergeRefresh(pending, gUi.slowTick(now, rng));
  }
  if (now >= nextBatt) {
    nextBatt = now + kBattEveryMs;
    readBattery(now);
  }
  if (now >= nextUsb) {
#if CALAMAIO_NET
    nextUsb = now + (gbmain::playing() ? 30000 : kUsbEveryMs);  // 0.12.0: in gioco la prova del cavo (150 ms) più di rado
#else
    nextUsb = now + kUsbEveryMs;
#endif
    readUsb(now, false);
  }
  // M6: calibrazione. La lettura dovuta la fa il loop (media di 64 campioni, mai durante un refresh: siamo qui).
  if (gUi.calibDue()) {
    static Ms nextTry = 0;
    if (now >= nextTry) {
      nextTry = now + 1000;
      const int mv = battery::readMv(64);
      if (mv > 0) lastMv = mv;
      if (lastMv > 0) pending = mergeRefresh(pending, gUi.calibSample(lastMv, now, wc));
    }
  }
  if (gUi.takeBatChanged()) batSave();
  // Velocità «Massima» e calibrazione fase 1: finché c'è animazione, un parziale dopo l'altro senza attesa.
  const Model &m = gUi.model();
  if (pending == RefreshKind::None && fastFrames(m) && animRate(kHeltec213, m, now) > 0) pending = RefreshKind::Partial;

  settingsSync(now);  // menu, set remoto, fine del silenzio: NVS e settings al servizio

  if (timerWake) {  // risveglio da timer: si disegna solo dopo la decisione
    probeLoop(now);
    delay(5);
    return;
  }
  // 0.10.2: pulizia dovuta e 30 s senza disegni né tasti: completo da solo.
  if (pending == RefreshKind::None && counter.idleCleanup(now)) pending = RefreshKind::Full;
#if CALAMAIO_NET
  // 0.12.0: giochi, prima del sonno e del disegno. Il risultato a parte: gbmain::loop può chiamare panelBack(), che
  // mette il completo in pending (pulizia dopo il driver veloce), e non va perso.
  const RefreshKind gbr = gbmain::loop(gUi, now, gbEnv(now));
  pending = mergeRefresh(pending, gbr);
  if (gUi.gbOnStack()) power.key(now);  // il controller non passa dal tasto PRG: niente sonno «PC spento» a metà partita
  if (gbmain::ownsPanel(gUi)) {         // il pannello è del driver veloce: un ridisegno rifà solo le bande
    if (pending != RefreshKind::None) gbmain::bandsDirty();
    pending = RefreshKind::None;
  }
#endif
  powerLoop(now);
  if (pending != RefreshKind::None) {
    const RefreshKind r = pending;
    pending = RefreshKind::None;
    present(r, now);
#if CALAMAIO_NET
    for (int k = 0; k < nFullAcks; k++) sendAck(fullAcks[k], true, nullptr);  // dopo il completo vero
    nFullAcks = 0;
#endif
  }
  delay(gUi.gbPlaying() ? 1 : 5);  // 0.12.0: in gioco il ritmo lo tiene il Pacer, il giro deve essere corto
}
