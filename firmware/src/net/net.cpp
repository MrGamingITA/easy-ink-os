// Task di rete (core 0): Wi-Fi, configurazione dal telefono, mDNS, WebSocket, HTTP. Vedi net.h.
#include "net/net.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>

#include "core/close_storm.h"
#include "core/crypto.h"
#include "core/link.h"
#include "core/logring.h"
#include "core/netfmt.h"
#include "core/ota.h"
#include "core/pc_fallback.h"
#include "core/power.h"
#include "core/remote_settings.h"
#include "core/retry.h"
#include "core/strbuf.h"
#include "core/theme_proto.h"
#include "core/theme_store.h"
#include "core/wifi_proto.h"
#include "core/wifi_roam.h"
#include "hw/avatar_fs.h"
#include "hw/logtee.h"
#include "hw/ota_hw.h"
#include "hw/secrets.h"
#include "hw/theme_fs.h"
#include "net/portal_assets.h"

bool tcpipInit();  // core: libraries/WiFi/src/WiFiGeneric.cpp (globale, non statica)

namespace net {

static_assert(cal::kNetIdLen == cal::kCmdIdLen && cal::kNetErrLen == cal::kAckErrLen, "id ed error: stesse misure");

namespace {

using cal::scopy;

// ---------- tempi (docs/PROTOCOLLO.md, brief §8.5) ----------
const uint32_t kRxTimeoutMs = 45000;   // niente dal PC (messaggi o ping) per 45 s → non raggiungibile
// Riconnessione di Wi-Fi, ricerca mDNS e WebSocket: subito, 5 s, 5 s, poi ogni 30 s (core/retry.h).
const uint32_t kJoinTimeoutMs = 20000; // collegamento a una rete
const uint32_t kJoinGraceMs = 6000;    // prima di credere a «password errata» / «rete assente»
const int kWsRefreshFrom = 4;          // dal 4° tentativo WebSocket (quelli a 30 s) si rifà prima la ricerca mDNS
const uint32_t kRssiEveryMs = 10000;   // come slowTick() del simulatore
const uint32_t kApLingerMs = 10000;    // dopo il collegamento l'AP resta un po' (il telefono legge l'esito)
const uint32_t kHeapLogMs = 60000;
const char *const kDefaultPath = "/ws";
const uint32_t kProbeMs = 2000;        // 0.12.1: prova TCP di un indirizzo di riserva prima del WebSocket (che ne aspetta 5)

QueueHandle_t gReq = nullptr, gEv = nullptr, gPerm = nullptr, gAv = nullptr;
QueueHandle_t gGame = nullptr, gAcc = nullptr, gAnc = nullptr;  // 0.10.0: gioco della mascotte
QueueHandle_t gAvFile = nullptr;  // 0.14.0 (R4): avatar scaricato a pezzi, da adottare nel loop (uno alla volta)
SemaphoreHandle_t gMx = nullptr;
// ~9 KB l'uno, sullo heap (fuori dallo stack del task): 0.12.0, in modalità gioco si liberano (suspend).
cal::StateMsg *gScratch = nullptr, *gMailbox = nullptr;
bool gPending = false;
int gLastSessions = 0;
cal::NetEntry gScan[cal::kNetCap];
int gNScan = 0;

char gBoard[8], gHost[24], gApSsid[24];
WebServer server(80);
DNSServer dns;
// Il client della libreria, ma i tentativi li decide wsTry. Causa dei ~20 s fra «PC trovato» e «connesso»
// dopo un avvio: begin() azzera _lastConnectionFail e loop() non prova finché millis() − 0 <
// _reconnectInterval (era 30 s): con il PC trovato a ~10 s di uptime, il primo collegamento partiva a 30 s.
// Qui l'intervallo è «infinito» (la libreria da sola non riprova mai) e armNow() rende dovuto il prossimo loop().
class WsClient : public WebSocketsClient {
 public:
  void manual() { _reconnectInterval = 0x7FFFFFFFUL; }
  void armNow() { _lastConnectionFail = millis() - _reconnectInterval; }
  // Il tipo di rete 5 non lo fa più (il 4 sì, in connectedCb): niente Nagle sul canale.
  void noDelay() { if (_client.tcp) _client.tcp->setNoDelay(true); }
};
WsClient ws;
JsonDocument gDoc;
const IPAddress kApIp(192, 168, 4, 1);

// Wi-Fi. Reti salvate (0.8.0, fino a 5): in RAM solo SSID e ultimo uso; la password si legge dal vault quando serve.
struct Saved { char ssid[33]; int64_t lastUsed; };
Saved gSaved[cal::kSavedMax];
int gNSaved = 0;
bool haveSaved = false;
bool staUp = false;
cal::RetrySchedule wifiTry, mdnsTry, wsTry;
struct Join {
  bool active, announce, save;
  char ssid[33], pass[65];
  char drop[33];  // portale con la lista piena: rete da togliere se questa si collega
  uint32_t started;
} join = {};
// pagina /api/save: il collegamento parte poco dopo, così la risposta HTTP esce prima
struct PendingJoin { uint32_t at; char ssid[33], pass[65], drop[33]; } pendingJoin = {};
// Ultima ricerca: tutte le reti viste (la più forte per SSID), non solo le kNetCap della schermata.
const int kSeenCap = 32;
cal::wifi::Seen gSeen[kSeenCap];
int gNSeen = 0;
// Giro di collegamento (core/wifi_roam): salvate visibili dalla più forte, poi le altre. Anche il cambio automatico
// è un giro: prima la rete più forte, poi di nuovo quella di prima.
struct Round {
  bool active, roam;
  cal::wifi::Candidate c[cal::kSavedMax];
  int n, next;
} round_ = {};
cal::wifi::RoamMonitor roam;
std::atomic<bool> gRoamPerm{false}, gRoamCal{false};  // dal loop: permesso aperto, calibrazione (setRoamGuard)
char usedPending[33] = "";  // collegata prima che l'ora fosse valida: l'ultimo uso si scrive appena lo è
bool scrubOnProvEnd = false;  // portale: tolta una rete per far posto a una nuova, pulizia quando si chiude
bool listDirty = false;       // lista o visibilità cambiate: al loop (Saved) e al servizio (wifi-list)
cal::SavedNet gSavedOut[cal::kSavedMax];  // copia per il loop (takeSaved), sotto gMx; usedLocal = ms epoch qui
int gNSavedOut = 0;

// Configurazione dal telefono
bool prov = false;
char provPass[cal::kApPassLen];
char provPreselect[33];
uint32_t provStopAt = 0;
enum class PortalSt : uint8_t { Wait, Join, Ok, Fail } portalSt = PortalSt::Wait;
char portalSsid[33];

// PC e WebSocket
bool mdnsUp = false, sntpUp = false;
bool pcKnown = false;
char pcIp[16], pcPath[32], pcHostName[48];
uint16_t pcPort = 0;
bool wsBegun = false, wsConn = false;
// 0.12.1-gb, piano B senza mDNS (core/pc_fallback): indirizzo manuale e ultimo PC per rete, NVS «calamaio-pc/v1».
cal::PcMemory gPcMem;
uint8_t gPcBlob[cal::PcMemory::kBlobMax];  // ultima copia scritta (o letta): si riscrive solo se cambia
size_t gPcBlobLen = 0;
cal::PcSrc pcSrc = cal::PcSrc::None;   // da dove viene pcIp
cal::PcSrc fbPrev = cal::PcSrc::None;  // ultima riserva provata in questa serie (alterna manuale e ultimo noto)
// La ricerca mDNS blocca ~3 s, la prova di una riserva fino a kProbeMs: mai nello stesso giro del task.
bool fbDue = false;     // mDNS senza risposta: al prossimo giro la riserva (discover)
bool probeDue = false;  // tentativo WebSocket verso una riserva dopo la ricerca mDNS: al prossimo giro la prova
bool probedOk = false;  // riserva appena provata da useFallback: il primo tentativo non la riprova
cal::RemoteValues gSettings;  // ultima copia dal loop: si manda dopo ogni hello e a ogni cambio
bool haveSettings = false;
cal::RemoteCmd gCmd;          // set/full-refresh appena letto (statico: fuori dallo stack del task)
cal::Link gLink;              // M5: abbinamento, autenticazione, permessi
const char *const kNotAuthed = "display non abbinato: cambia le impostazioni dal menu del display";
uint32_t lastRx = 0, lastRssi = 0, lastHeapLog = 0, rebootAt = 0;
// M6: dopo un `bye` per kByeGraceMs i tentativi lenti sono ogni kByeRetryMs (anche un riavvio del servizio manda bye).
uint32_t byeAt = 0;
volatile int8_t gWantPs = -1;   // modem sleep richiesto dal loop (wifi_ps_type_t), −1 = nessun cambio
volatile int8_t gPs = -1;    // applicato dal task di rete; lo legge anche il loop (appliedPowerSave)
volatile bool gWantOff = false; // sonno profondo: Wi-Fi spento, il task si ferma
volatile bool gOff = false;
int lastRssiVal = 0;

// Aggiornamento remoto, registro da remoto, crash (docs/OTA.md)
cal::ota::Updater gUpd;
cal::ota::Offer gOffer;           // offerta verificata in attesa della risposta del loop (condizioni)
bool gOfferWaiting = false;
bool gOtaPubOk = false;
uint8_t gOtaPub[32];
std::atomic<uint32_t> gOtaDone{0};
cal::ota::Record gBootRec;
bool gBootHasRec = false, gBootReported = false;
cal::ota::BootCase gBootCase = cal::ota::BootCase::Normal;
cal::LogStream gLog;
cal::ota::PowerStatus gPower = {-1, -1, false};  // ultimo status dal loop: si manda dopo ogni hello
bool havePower = false;
bool gAuthRetry = false;  // auth non calcolata (memoria): il giro del task chiude il WebSocket e si riprova
int gAuthRetries = 0;     // AuthRetry di fila (azzerato da auth-ok): il primo riprova fra 5 s, i seguenti fra 30 s
uint32_t gWsHoldMs = 0;   // prossima chiusura: primo tentativo rimandato di tanto (0 = subito)
bool gWsQuiet = false;    // prossima chiusura senza PcLost (il PC c'è): lo si deve se il tentativo seguente non riesce
bool gPcLostOwed = false;
uint32_t gPcLostOwedAt = 0;          // da quando: oltre kPcLostOwedMs si manda comunque (pcLoop)
const uint32_t kPcLostOwedMs = 40000;  // attesa di 30 s + un tentativo da 5 s + margine
const size_t kLogBufLen = cal::LogStream::kMsgMax + 1;
char *gLogBuf = nullptr;  // 8 KB sullo heap solo col registro da remoto acceso (log-start), fuori dallo stack del task

// Diagnostica dello heap al cambio di rete (0.13.0-ed): riga su buffer dello stack, Print::printf alloca sopra 64 byte.
// stack = anche il minimo di stack libero del task di rete (0.14.0: righe dell'avatar a pezzi, prova L13).
void memLog(const char *step, bool stack = false) {
  char b[160];
  int n = snprintf(b, sizeof b, "[memoria] %s: heap libero %u, minimo %u, blocco max %u", step, unsigned(ESP.getFreeHeap()),
                   unsigned(ESP.getMinFreeHeap()), unsigned(ESP.getMaxAllocHeap()));
  if (n > 0 && size_t(n) < sizeof b)
    n += snprintf(b + n, sizeof b - n, stack ? ", stack rete libero %u\n" : "\n", unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  if (n > 0) LogSerial.write(reinterpret_cast<const uint8_t *>(b), size_t(n) < sizeof b ? size_t(n) : sizeof b - 1);
}

// Allocazione fallita: il callback gira nel task (o nella sezione critica) di chi ha fallito, con poco stack. Solo
// contatori atomici, niente I/O né lucchetti; la riga la scrive il task di rete (allocReport), al più una al secondo.
std::atomic<uint32_t> gAllocFails{0}, gAllocLastSize{0}, gAllocLastCaps{0}, gAllocMaxSize{0};
std::atomic<const char *> gAllocLastFn{nullptr};
uint32_t gAllocShown = 0, gAllocShownAt = 0;
void IRAM_ATTR onAllocFailed(size_t size, uint32_t caps, const char *fn) {
  if (!size) return;  // malloc(0) dà NULL anche con lo heap pieno: non è una mancanza
  gAllocLastSize.store(uint32_t(size), std::memory_order_relaxed);
  gAllocLastCaps.store(caps, std::memory_order_relaxed);
  gAllocLastFn.store(fn, std::memory_order_relaxed);
  if (size > gAllocMaxSize.load(std::memory_order_relaxed)) gAllocMaxSize.store(uint32_t(size), std::memory_order_relaxed);
  gAllocFails.fetch_add(1, std::memory_order_relaxed);
}

void allocReport() {
  const uint32_t n = gAllocFails.load(std::memory_order_relaxed);
  if (n == gAllocShown || millis() - gAllocShownAt < 1000) return;
  gAllocShownAt = millis();
  const char *fn = gAllocLastFn.load(std::memory_order_relaxed);
  char b[160];
  const int k = snprintf(b, sizeof b,
                         "[memoria] %u allocazioni fallite (%u in tutto): ultima %u byte, caps 0x%x, %s; la più grande %u byte\n",
                         unsigned(n - gAllocShown), unsigned(n), unsigned(gAllocLastSize.load(std::memory_order_relaxed)),
                         unsigned(gAllocLastCaps.load(std::memory_order_relaxed)), fn ? fn : "?",
                         unsigned(gAllocMaxSize.load(std::memory_order_relaxed)));
  gAllocShown = n;
  if (k > 0) LogSerial.write(reinterpret_cast<const uint8_t *>(b), size_t(k) < sizeof b ? size_t(k) : sizeof b - 1);
}

// Registro da remoto spento: i suoi 8 KB tornano allo heap (log-stop, fine del dump, ws chiuso, modalità gioco).
void logEnd() {
  gLog.drop();
  free(gLogBuf);
  gLogBuf = nullptr;
}
// 0.13.0, temi delle schermate (PROTOCOLLO.md «Temi»): l'offerta verificata aspetta il giro del task (themeLoop),
// così il download non avviene dentro la callback del WebSocket.
char gThemeCur[cal::theme::kIdLen] = "";
cal::theme::Offer gThemeOffer;
bool gThemePending = false;
cal::theme::Installer gThemeInst;
// 0.14.0 (R4), avatar a pezzi (PROTOCOLLO.md «Avatar»): un'offerta alla volta. Idle → Pending (offerta valida, si
// scarica al prossimo giro: avatarLoop) → Adopting (file pronto in avfs, il loop lo adotta e risponde AvatarAck) → Idle.
// Atomico: un AvatarAck che non entra nella coda lo riporta a Idle dal loop (request), se no resterebbe in Adopting.
enum AvSt : uint8_t { kAvIdle, kAvPending, kAvAdopting };
std::atomic<uint8_t> gAvSt{kAvIdle};
cal::AvatarOffer gAvOffer;
// 0.14.0 (R4): chiusure del WebSocket a raffica (core/close_storm), attesa crescente prima del primo tentativo.
cal::CloseStorm gStorm;
uint32_t gAllocAtOpen = 0;  // allocazioni fallite all'apertura: cambiate alla chiusura = chiusura per memoria
// 0.12.0, modalità gioco (suspend/resume): Wi-Fi, WebSocket, mDNS e HTTP spenti, buffer liberati, task chiuso.
enum Susp : uint8_t { kRunning, kWant, kSuspending, kSuspended, kRefused };
std::atomic<uint8_t> gSusp{kRunning};

// ---------- eventi verso il loop ----------
void post(EvType t, const char *ssid = "", const char *text = "", int rssi = 0, int64_t time = 0, const char *id = "") {
  Event e;
  memset(&e, 0, sizeof e);
  e.type = t;
  scopy(e.id, id ? id : "");
  scopy(e.ssid, ssid ? ssid : "");
  scopy(e.text, text ? text : "");
  e.rssi = rssi;
  e.time = time;
  if (xQueueSend(gEv, &e, 0) != pdTRUE) LogSerial.printf("[net] coda eventi piena: evento %d perso\n", int(t));
}

uint32_t hwRand(void *) { return esp_random(); }  // casuale vero con la radio accesa
void linkRand(uint8_t *out, size_t n, void *) { esp_fill_random(out, n); }

void sendSettings();
void sendWifiList();
void sendThemes();
void otaStatus(const char *id, cal::OtaPhase p, uint32_t done, uint32_t total, const char *error);
void otaAfterAuth();
void sendPower();
void pcMemRemember();
void pcMemForget(const char *ssid);

// Uscite di core/link: i frame vanno al servizio, gli esiti al loop (e K al vault).
struct NetOut : cal::LinkOut {
  void send(const char *json, size_t n) override;
  void event(const cal::LinkEvent &e) override;
} gOut;

// ---------- aggiornamento remoto: ota-status e uscite di core/ota ----------
void otaStatus(const char *id, cal::OtaPhase p, uint32_t done, uint32_t total, const char *error) {
  char buf[256];
  const size_t n = cal::ota::encodeStatus(id, p, done, total, error, buf, sizeof buf);
  if (!n) return;
  if (wsConn) ws.sendTXT(buf, n);
  LogSerial.printf("[ota] %s%s\n", wsConn ? "" : "(PC scollegato, non mandato) ", buf);
}

// Uscite di core/ota: ota-status al servizio, fasi al loop (schermata), byte ricevuti in un atomico.
struct UpdOut : cal::ota::UpdaterOut {
  void status(cal::OtaPhase p, uint32_t done, uint32_t total, const char *error) override { otaStatus(gUpd.offer().id, p, done, total, error); }
  void progress(cal::OtaPhase p, uint32_t done, uint32_t, const char *error) override {
    gOtaDone = done;
    if (p == cal::OtaPhase::Receive) return;
    Event e;
    memset(&e, 0, sizeof e);
    e.type = EvType::OtaPhase;
    e.rssi = int(p);
    scopy(e.text, error ? error : "");
    if (xQueueSend(gEv, &e, pdMS_TO_TICKS(100)) != pdTRUE) LogSerial.println("[ota] coda eventi piena: fase non mostrata");
  }
  bool saveRecord(const cal::ota::Record &r) override { return otahw::saveRecord(r); }
  void clearRecord() override { otahw::clearRecord(); }
} gUpdOut;

// ---------- reti salvate ----------
bool epochValid() { return time(nullptr) > 1700000000; }  // dopo il 2023: welcome del servizio o SNTP
int64_t epochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

int savedIndex(const char *ssid) {
  for (int k = 0; k < gNSaved; k++)
    if (!strcmp(gSaved[k].ssid, ssid)) return k;
  return -1;
}

void loadSaved() {
  cal::Vault::NetMeta m[cal::kSavedMax];
  const int n = secrets::loadNets(m, cal::kSavedMax);
  gNSaved = n < 0 ? 0 : n;
  for (int k = 0; k < gNSaved; k++) {
    scopy(gSaved[k].ssid, m[k].ssid);
    gSaved[k].lastUsed = m[k].lastUsed;
  }
  haveSaved = gNSaved > 0;
  listDirty = true;
}

const char *currentSsid() {
  static char cur[33];
  if (!staUp) return "";
  scopy(cur, WiFi.SSID().c_str());
  return cur;
}

// Lista per il loop (menu Reti salvate) e per il servizio (wifi-list): ordine di inserimento, visibilità dall'ultima
// ricerca. Si chiama dal task quando listDirty.
void publishSaved() {
  listDirty = false;
  cal::SavedNet out[cal::kSavedMax];
  memset(out, 0, sizeof out);
  for (int k = 0; k < gNSaved; k++) {
    scopy(out[k].ssid, gSaved[k].ssid);
    out[k].usedLocal = gSaved[k].lastUsed;  // ms epoch: lo converte il loop (net::takeSaved)
    out[k].visible = cal::wifi::seenRssi(gSeen, gNSeen, gSaved[k].ssid, out[k].rssi);
  }
  xSemaphoreTake(gMx, portMAX_DELAY);
  memcpy(gSavedOut, out, sizeof out);
  gNSavedOut = gNSaved;
  xSemaphoreGive(gMx);
  post(EvType::Saved);
  sendWifiList();
}

// Ultimo uso della rete appena collegata (ms epoch); senza l'ora valida si scrive appena lo diventa.
void markUsed(const char *ssid) {
  const int k = savedIndex(ssid);
  if (k < 0) return;
  if (!epochValid()) { scopy(usedPending, ssid); return; }
  usedPending[0] = 0;
  gSaved[k].lastUsed = epochMs();
  if (!secrets::touchNet(ssid, gSaved[k].lastUsed)) LogSerial.printf("[wifi] rete «%s»: ultimo uso non salvato (vault)\n", ssid);
  listDirty = true;
}

// ---------- scansione ----------
// quick: ricerca rapida (kQuickScanMsPerChan per canale) per i giri di collegamento e il cambio automatico.
void doScan(bool notify, bool quick = false) {
  LogSerial.printf("[wifi] scansione%s…\n", quick ? " rapida" : "");
  const int n = quick ? WiFi.scanNetworks(false, false, false, cal::wifi::kQuickScanMsPerChan) : WiFi.scanNetworks(false, false);
  cal::NetEntry list[cal::kNetCap];
  int cnt = 0;
  gNSeen = 0;
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;  // reti nascoste
    const int rssi = WiFi.RSSI(i);
    {  // tutte le reti viste (per le salvate): la più forte per SSID; piena, si tiene la più forte
      int s = -1;
      for (int k = 0; k < gNSeen && s < 0; k++)
        if (ssid == gSeen[k].ssid) s = k;
      if (s >= 0) { if (rssi > gSeen[s].rssi) gSeen[s].rssi = rssi; }
      else if (gNSeen < kSeenCap) { scopy(gSeen[gNSeen].ssid, ssid.c_str()); gSeen[gNSeen++].rssi = rssi; }
      else {
        int weakest = 0;
        for (int k = 1; k < gNSeen; k++)
          if (gSeen[k].rssi < gSeen[weakest].rssi) weakest = k;
        if (rssi > gSeen[weakest].rssi) { scopy(gSeen[weakest].ssid, ssid.c_str()); gSeen[weakest].rssi = rssi; }
      }
    }
    int dup = -1;
    for (int k = 0; k < cnt; k++)
      if (ssid == list[k].ssid) dup = k;
    if (dup >= 0) { if (rssi > list[dup].rssi) list[dup].rssi = rssi; continue; }  // stessa rete, più antenne
    cal::NetEntry e;
    scopy(e.ssid, ssid.c_str());
    e.rssi = rssi;
    e.lock = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    if (cnt < cal::kNetCap) list[cnt++] = e;
    else {  // tiene le più forti
      int weakest = 0;
      for (int k = 1; k < cnt; k++)
        if (list[k].rssi < list[weakest].rssi) weakest = k;
      if (e.rssi > list[weakest].rssi) list[weakest] = e;
    }
  }
  WiFi.scanDelete();
  for (int a = 0; a < cnt; a++)  // dalla più forte
    for (int b = a + 1; b < cnt; b++)
      if (list[b].rssi > list[a].rssi) { cal::NetEntry t = list[a]; list[a] = list[b]; list[b] = t; }
  xSemaphoreTake(gMx, portMAX_DELAY);
  memcpy(gScan, list, sizeof(cal::NetEntry) * cnt);
  gNScan = cnt;
  xSemaphoreGive(gMx);
  int savedSeen = 0;
  for (int k = 0; k < gNSaved; k++) {
    int r;
    if (cal::wifi::seenRssi(gSeen, gNSeen, gSaved[k].ssid, r)) savedSeen++;
  }
  LogSerial.printf("[wifi] scansione: %d reti (%d visibili), %d salvate su %d\n", cnt, n < 0 ? 0 : n, savedSeen, gNSaved);
  if (gNSaved) listDirty = true;  // visibilità delle salvate
  memLog("dopo la scansione");
  if (notify) post(EvType::ScanDone);
}

// ---------- configurazione dal telefono ----------
void startProv(const char *preselect) {
  scopy(provPreselect, preselect ? preselect : "");
  portalSt = PortalSt::Wait;
  provStopAt = 0;
  if (!prov) {
    WiFi.mode(WIFI_AP_STA);
    cal::makeApPassword(provPass, sizeof provPass, hwRand, nullptr);
    WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(gApSsid, provPass)) LogSerial.println("[wifi] ERRORE: AP non partito");
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", kApIp);
    prov = true;
    // La password dell'AP va solo sulla seriale (cavo): mai nel registro da remoto, che viaggia in chiaro.
    LogSerial.printf("[wifi] configurazione: rete %s, portale http://192.168.4.1/setup (password sulla seriale e nel QR)\n", gApSsid);
    Serial.printf("[wifi] password della rete %s: %s\n", gApSsid, provPass);
    doScan(false);  // elenco per il portale
  }
  post(EvType::ApReady, "", provPass);
}

void stopProv() {
  if (!prov) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  prov = false;
  provPass[0] = 0;
  provStopAt = 0;
  LogSerial.println("[wifi] configurazione chiusa: AP e portale spenti");
  if (scrubOnProvEnd) {  // una rete tolta per far posto alla nuova: la sua password va via anche dalla flash
    scrubOnProvEnd = false;
    secrets::requestScrub("Rete tolta dal portale (lista piena)");
  }
  if (haveSaved && !staUp && !join.active) wifiTry.restart(millis());  // torna alle reti salvate
}

// ---------- Wi-Fi ----------
// Offerta OTA in attesa della risposta del loop: senza collegamento non vale più (mai bloccata a vero).
void dropOffer(const char *why) {
  if (!gOfferWaiting) return;
  gOfferWaiting = false;
  if (wsConn) otaStatus(gOffer.id, cal::OtaPhase::Rejected, 0, gOffer.m.size, why);
  LogSerial.printf("[ota] offerta %s lasciata: %s\n", gOffer.id, why);
}

// PcLost tenuto da una chiusura silenziosa (AuthRetry): si manda adesso, se c'è. Per chi ferma i tentativi.
void pcLostFlush() {
  if (!gPcLostOwed) return;
  gPcLostOwed = false;
  post(EvType::PcLost);
}

void wsDrop() {
  dropOffer("collegamento chiuso");
  if (wsBegun) ws.disconnect();
  if (wsConn) { wsConn = false; gLink.onDisconnected(gOut); post(EvType::PcLost); }
  pcLostFlush();
}

void startJoin(const char *ssid, const char *pass, bool save, bool announce, const char *drop = "") {
  if (announce && round_.active) {  // una rete scelta a mano (portale, display) prende il posto del giro
    round_.active = false;
    LogSerial.println("[wifi] giro interrotto: rete scelta a mano");
  }
  join.active = true;
  join.announce = announce;
  join.save = save;
  scopy(join.ssid, ssid);
  scopy(join.pass, pass ? pass : "");
  scopy(join.drop, drop ? drop : "");
  join.started = millis();
  roam.stop();
  if (staUp) { wsDrop(); staUp = false; }
  WiFi.mode(prov ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect(false, false);
  if (join.pass[0]) WiFi.begin(join.ssid, join.pass);
  else WiFi.begin(join.ssid);
  portalSt = PortalSt::Join;
  scopy(portalSsid, ssid);
  LogSerial.printf("[wifi] connessione a «%s»%s…\n", ssid, join.pass[0] ? "" : " (rete aperta)");
  if (announce) post(EvType::Connecting, ssid);
}

void onStaUp() {
  const String ip = WiFi.localIP().toString();
  lastRssiVal = WiFi.RSSI();
  LogSerial.printf("[wifi] connesso a «%s» · IP %s · RSSI %d dBm · canale %d\n", WiFi.SSID().c_str(), ip.c_str(),
                lastRssiVal, WiFi.channel());
  post(EvType::Connected, WiFi.SSID().c_str(), ip.c_str(), lastRssiVal);
  if (!mdnsUp) {
    if (MDNS.begin(gHost)) {
      MDNS.addService("calamaio-board", "tcp", 80);
      mdnsUp = true;
      LogSerial.printf("[mdns] annunciato %s.local, servizio _calamaio-board._tcp porta 80\n", gHost);
    } else {
      LogSerial.println("[mdns] ERRORE: avvio non riuscito");
    }
  }
  if (!sntpUp) {
    // Riserva dell'ora del muro: SNTP senza toccare TZ (il fuso lo imposta il loop, da welcome).
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char *)"pool.ntp.org");
    sntp_set_time_sync_notification_cb([](struct timeval *) { LogSerial.println("[ora] SNTP: ora sincronizzata da pool.ntp.org"); });
    sntp_init();
    sntpUp = true;
    LogSerial.println("[ora] SNTP avviato: richiesta a pool.ntp.org");
  } else {
    // 0.10.1: a ogni nuova connessione una richiesta subito (altrimenti la prossima arriverebbe dopo
    // CONFIG_LWIP_SNTP_UPDATE_DELAY = 3 h dall'ultima).
    sntp_restart();
    LogSerial.println("[ora] SNTP: nuova richiesta dopo la riconnessione");
  }
  wifiTry.stop();
  wsTry.stop();
  pcLostFlush();  // rete nuova: il PC di prima non è più collegato
  mdnsTry.restart(millis());  // cerca subito il PC
  fbPrev = cal::PcSrc::None;  // rete nuova: riserve dall'inizio (prima il manuale)
  fbDue = probeDue = probedOk = false;
  lastRssi = millis();
  roam.reset(millis());
  roam.reading(lastRssiVal);
  markUsed(WiFi.SSID().c_str());
  listDirty = true;  // rete in uso cambiata
  memLog("dopo la connessione Wi-Fi");
}

// ---------- giro di collegamento e cambio automatico (core/wifi_roam) ----------
void wipeJoinPass() { cal::cry::wipe(join.pass, sizeof join.pass); }

// Prossimo candidato del giro. false = giro finito senza rete.
bool roundNext() {
  while (round_.active && round_.next < round_.n) {
    const cal::wifi::Candidate c = round_.c[round_.next++];
    if (c.idx < 0 || c.idx >= gNSaved) continue;
    char pass[65];
    if (!secrets::loadNetPass(gSaved[c.idx].ssid, pass, sizeof pass)) {
      LogSerial.printf("[wifi] giro: «%s» senza password leggibile nel vault, salto\n", gSaved[c.idx].ssid);
      continue;
    }
    if (c.visible)
      LogSerial.printf("[wifi] giro: provo «%s» (%d dBm, %d di %d)\n", gSaved[c.idx].ssid, c.rssi, round_.next, round_.n);
    else
      LogSerial.printf("[wifi] giro: provo «%s» (non vista dalla ricerca, %d di %d)\n", gSaved[c.idx].ssid, round_.next, round_.n);
    startJoin(gSaved[c.idx].ssid, pass, false, false);
    cal::cry::wipe(pass, sizeof pass);
    return true;
  }
  round_.active = false;
  return false;
}

// Un tentativo di wifiTry = un giro intero: ricerca rapida, poi le salvate dalla più forte.
void startRound(uint32_t now, int attemptNo) {
  doScan(false, true);
  char names[cal::kSavedMax][cal::kSsidLen];
  for (int k = 0; k < gNSaved; k++) scopy(names[k], gSaved[k].ssid);
  round_.active = true;
  round_.roam = false;
  round_.n = cal::wifi::roundOrder(names, gNSaved, gSeen, gNSeen, round_.c);
  round_.next = 0;
  int vis = 0;
  for (int k = 0; k < round_.n; k++) vis += round_.c[k].visible;
  LogSerial.printf("[wifi] giro %d: %d reti salvate, %d visibili%s; prossimo giro fra %lu s se nessuna risponde\n", attemptNo,
                   gNSaved, vis, vis ? " (dalla più forte)" : "", (unsigned long)(wifiTry.nextDelay() / 1000));
  (void)now;
  roundNext();
}

bool otaBusy() { return gUpd.active() || gOfferWaiting; }

// Controllo del cambio automatico (ogni 60 s, in Attesa ogni 5 min). Registro: ogni decisione presa.
void roamCheck(uint32_t now) {
  roam.checked(now);
  if (!roam.weak()) return;  // segnale buono: niente da decidere
  const char *cur = currentSsid();
  const cal::wifi::RoamBlock b = {gRoamPerm.load(), otaBusy(), gRoamCal.load()};
  if (const char *why = cal::wifi::roamBlockText(b)) {
    LogSerial.printf("[wifi] cambio rete: «%s» debole (%d dBm, %d letture di fila sotto %d) ma resto: %s\n", cur, roam.last(),
                     roam.streak(), cal::wifi::kRoamWeakDbm, why);
    return;
  }
  if (gNSaved < 2 || savedIndex(cur) < 0) {
    LogSerial.printf("[wifi] cambio rete: «%s» debole (%d dBm) ma non ci sono altre reti salvate\n", cur, roam.last());
    return;
  }
  doScan(false, true);
  char names[cal::kSavedMax][cal::kSsidLen];
  for (int k = 0; k < gNSaved; k++) scopy(names[k], gSaved[k].ssid);
  int tr = 0;
  const int t = cal::wifi::roamTarget(names, gNSaved, cur, roam.last(), gSeen, gNSeen, &tr);
  if (t < 0) {
    int bestRssi = 0, best = -1;
    for (int k = 0; k < gNSaved; k++) {
      int r;
      if (strcmp(names[k], cur) && cal::wifi::seenRssi(gSeen, gNSeen, names[k], r) && (best < 0 || r > bestRssi)) { best = k; bestRssi = r; }
    }
    if (best < 0)
      LogSerial.printf("[wifi] cambio rete: «%s» debole (%d dBm), nessun'altra rete salvata in vista: resto\n", cur, roam.last());
    else
      LogSerial.printf("[wifi] cambio rete: «%s» debole (%d dBm), la migliore è «%s» a %d dBm (meno di %d dB in più): resto\n", cur,
                       roam.last(), names[best], bestRssi, cal::wifi::kRoamMarginDb);
    return;
  }
  LogSerial.printf("[wifi] cambio rete: da «%s» (%d dBm) a «%s» (%d dBm, +%d dB); se non risponde si torna a «%s»\n", cur, roam.last(),
                   names[t], tr, tr - roam.last(), cur);
  // Il giro del cambio: la rete nuova, poi quella di prima, poi le altre come in un giro normale.
  cal::wifi::Candidate all[cal::kSavedMax];
  const int n = cal::wifi::roundOrder(names, gNSaved, gSeen, gNSeen, all);
  const int curIdx = savedIndex(cur);
  round_.n = 0;
  round_.c[round_.n++] = {t, true, tr};
  round_.c[round_.n++] = {curIdx, true, roam.last()};
  for (int k = 0; k < n; k++)
    if (all[k].idx != t && all[k].idx != curIdx) round_.c[round_.n++] = all[k];
  round_.active = true;
  round_.roam = true;
  round_.next = 0;
  roundNext();
}

void checkWifi() {
  const wl_status_t st = WiFi.status();
  const uint32_t now = millis();
  if (join.active) {
    const uint32_t el = now - join.started;
    if (st == WL_CONNECTED) {
      join.active = false;
      if (round_.active) {
        LogSerial.printf("[wifi] %s: collegato a «%s» al tentativo %d di %d\n", round_.roam ? "cambio rete" : "giro", join.ssid,
                         round_.next, round_.n);
        round_.active = false;
      }
      if (join.save) {
        // Portale con la lista piena: la rete scelta per far posto va via solo ora che la nuova funziona.
        if (join.drop[0] && savedIndex(join.drop) >= 0) {
          const bool d = secrets::dropNet(join.drop);
          LogSerial.printf("[wifi] rete «%s» %s per far posto a «%s» (pulizia profonda e riavvio quando si chiude il portale)\n",
                           join.drop, d ? "tolta" : "NON tolta (vault)", join.ssid);
          scrubOnProvEnd = scrubOnProvEnd || d;
        }
        const cal::Vault::AddNet r = secrets::addNet(join.ssid, join.pass, epochValid() ? epochMs() : 0);
        using A = cal::Vault::AddNet;
        const char *what = r == A::Added ? "aggiunta alle reti salvate" : r == A::Updated ? "già salvata: password aggiornata"
                         : r == A::Full ? "NON salvata: ci sono già 5 reti" : r == A::Invalid ? "NON salvata: SSID o password non validi"
                                                                                : "NON salvata (vault)";
        LogSerial.printf("[wifi] rete «%s» %s%s\n", join.ssid, what,
                         r == A::Added || r == A::Updated ? (secrets::keysProtected() ? ", password cifrata" : " (chiavi non protette)") : "");
        loadSaved();
      }
      wipeJoinPass();
      portalSt = PortalSt::Ok;
      staUp = true;
      onStaUp();
      if (prov) provStopAt = now + kApLingerMs;
      return;
    }
    const bool failed = (el > kJoinGraceMs && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL)) || el > kJoinTimeoutMs;
    if (failed) {
      join.active = false;
      wipeJoinPass();
      LogSerial.printf("[wifi] connessione a «%s» non riuscita (stato %d dopo %lu ms)\n", join.ssid, int(st), (unsigned long)el);
      WiFi.disconnect(false, false);
      portalSt = PortalSt::Fail;
      if (join.announce) post(EvType::ConnectFailed, join.ssid);
      if (!join.announce && round_.active) {  // giro: la prossima rete subito (con il portale aperto, dopo)
        if (!prov && roundNext()) return;
        round_.active = false;
        if (!prov) LogSerial.printf("[wifi] %s: nessuna rete salvata ha risposto\n", round_.roam ? "cambio rete" : "giro");
        if (!wifiTry.active()) wifiTry.restart(now);  // cambio automatico fallito del tutto: come una connessione persa
      }
      if (haveSaved) {  // si torna alle reti salvate (se la configurazione è aperta, quando si chiude)
        if (join.announce || !wifiTry.active()) wifiTry.restart(now + (join.announce ? 1000 : 0));
        if (!prov) {
          const int32_t wait = int32_t(wifiTry.dueAt() - now);
          LogSerial.printf("[wifi] prossimo giro sulle %d reti salvate fra %ld ms\n", gNSaved, long(wait > 0 ? wait : 0));
        }
      }
    }
    return;
  }
  if (staUp && st != WL_CONNECTED) {
    staUp = false;
    roam.stop();
    LogSerial.println("[wifi] connessione persa: giro sulle reti salvate subito, poi a 5 s, 5 s e ogni 30 s");
    wsDrop();
    post(EvType::WifiLost);
    listDirty = true;
    if (haveSaved) wifiTry.restart(now);
  } else if (!staUp && st == WL_CONNECTED) {
    staUp = true;
    onStaUp();
  } else if (!staUp && haveSaved && !prov && wifiTry.due(now)) {
    const int n = wifiTry.attempt(now);
    startRound(now, n);
  }
  if (staUp && now - lastRssi >= kRssiEveryMs) {
    lastRssi = now;
    const int r = WiFi.RSSI();
    roam.reading(r);
    if (r != lastRssiVal) { lastRssiVal = r; post(EvType::Rssi, "", "", r); }
  }
  // Cambio automatico: in Attivo ogni 60 s, in Attesa (modem sleep massimo) ogni 5 min.
  if (staUp && !join.active && !prov && roam.due(now, gPs == int8_t(PowerSave::Max))) roamCheck(now);
  // Ultimo uso di una rete collegata prima che l'ora fosse valida.
  if (usedPending[0] && epochValid()) {
    char s[33];
    scopy(s, usedPending);
    markUsed(s);
  }
  if (listDirty) publishSaved();
}

// ---------- PC: mDNS e WebSocket ----------
void sendHello() {
  JsonDocument d;  // campi esatti di protocol/examples/hello-avatar-dl.json (schema chiuso)
  d["t"] = "hello";
  d["v"] = cal::kProtoVersion;
  d["board"] = gHost;
  d["device"] = cal::kHeltec213.id;
  const char cls[2] = {cal::kHeltec213.cls, 0};
  d["class"] = cls;
  d["fw"] = cal::kFwVersion;
  d["w"] = cal::kHeltec213.W;
  d["h"] = cal::kHeltec213.H;
  d["paired"] = gLink.hasKey();  // M5 (proposta): il display ha una K; il servizio la confronta con la sua
  d["caps"].add("avatar-dl");    // 0.14.0 (R4): avatar a pezzi (avatar-offer → GET /avatars/<token>)
  String s;
  serializeJson(d, s);
  ws.sendTXT(s);
  LogSerial.printf("[ws] hello: %s\n", s.c_str());
}

void sendSettings() {
  char buf[320];
  // remote: solo dopo auth-ok (M5 §2): senza, la webapp mostra le impostazioni in sola lettura.
  const size_t n = cal::encodeSettings(gSettings, buf, sizeof buf, gLink.authed());
  if (!n) { LogSerial.println("[ws] settings: buffer corto, non mandato"); return; }
  ws.sendTXT(buf, n);
  LogSerial.printf("[ws] settings: %s\n", buf);
}

void sendAck(const char *id, bool ok, const char *error) {
  char buf[200];
  const size_t n = cal::encodeAck(id, ok, error, buf, sizeof buf);
  if (!n) return;
  ws.sendTXT(buf, n);
  LogSerial.printf("[ws] ack: %s\n", buf);
}

// wifi-list (0.8.0): solo a una connessione autenticata, dopo auth-ok e a ogni cambio. Mai password.
void sendWifiList() {
  if (!wsConn || !gLink.authed()) return;
  cal::wifi::ListEntry e[cal::kSavedMax];
  memset(e, 0, sizeof e);
  const char *cur = currentSsid();
  for (int k = 0; k < gNSaved; k++) {
    scopy(e[k].ssid, gSaved[k].ssid);
    e[k].current = cur[0] && !strcmp(cur, gSaved[k].ssid);
    if (e[k].current) { e[k].hasRssi = true; e[k].rssi = WiFi.RSSI(); }
    else e[k].hasRssi = cal::wifi::seenRssi(gSeen, gNSeen, gSaved[k].ssid, e[k].rssi);
    e[k].lastUsed = gSaved[k].lastUsed;
  }
  char *buf = static_cast<char *>(malloc(cal::wifi::kListMax));  // 0.14.1: 1 KB solo durante l'invio
  if (!buf) { LogSerial.println("[ws] wifi-list: memoria non disponibile, non mandato"); return; }
  const size_t n = cal::wifi::encodeList(e, gNSaved, buf, cal::wifi::kListMax);
  if (!n) LogSerial.println("[ws] wifi-list: buffer corto, non mandato");
  else {
    ws.sendTXT(buf, n);
    LogSerial.printf("[ws] %s\n", buf);
  }
  free(buf);
}

// wifi-add (0.8.0): password cifrata con la SK della connessione. La rete si aggiunge e basta: anche se è più forte
// di quella in uso non ci si sposta adesso (al prossimo giro o con il cambio automatico).
void onWifiAdd(JsonVariantConst root) {
  static cal::wifi::AddCheck a;  // fuori dallo stack del task
  cal::wifi::checkAdd(root, gLink.authed(), gLink.sessionKey(), a);
  if (!a.hasId) { LogSerial.println("[wifi] wifi-add senza id valido (1-32 byte): ignorato, niente ack"); return; }
  const char *err = a.error;
  using A = cal::Vault::AddNet;
  A r = A::Failed;
  if (!err) {
    r = secrets::addNet(a.ssid, a.pass, 0);
    if (r == A::Full) err = cal::wifi::kErrFull;
    else if (r == A::Invalid) err = cal::wifi::kErrPass;
    else if (r == A::Failed) err = "salvataggio non riuscito";
  }
  cal::cry::wipe(a.pass, sizeof a.pass);
  sendAck(a.id, !err, err);
  if (err) { LogSerial.printf("[wifi] wifi-add %s «%s» rifiutato: %s\n", a.id, a.ssid, err); return; }
  loadSaved();
  LogSerial.printf("[wifi] wifi-add %s: rete «%s» %s (%d di %d)%s; non ci si sposta adesso\n", a.id, a.ssid,
                   r == A::Added ? "aggiunta" : "già salvata, password aggiornata", gNSaved, cal::kSavedMax,
                   secrets::keysProtected() ? ", password cifrata" : " (chiavi non protette)");
}

// wifi-forget (0.8.0): ack, poi rete via dal vault, pulizia profonda della NVS e riavvio dopo 3 s. Anche l'ultima
// rete si può dimenticare: al riavvio parte la configurazione dal telefono.
void onWifiForget(JsonVariantConst root) {
  cal::wifi::ForgetCheck f;
  cal::wifi::checkForget(root, gLink.authed(), gLink.sessionKey(), f);
  if (!f.hasId) { LogSerial.println("[wifi] wifi-forget senza id valido (1-32 byte): ignorato, niente ack"); return; }
  if (!f.error && savedIndex(f.ssid) < 0) f.error = cal::wifi::kErrUnknown;
  sendAck(f.id, !f.error, f.error);
  if (f.error) { LogSerial.printf("[wifi] wifi-forget %s «%s» rifiutato: %s\n", f.id, f.ssid, f.error); return; }
  const bool last = gNSaved == 1;
  LogSerial.printf("[wifi] wifi-forget %s: rete «%s» dimenticata dal PC%s\n", f.id, f.ssid,
                   last ? "; era l'ultima rete: al riavvio parte la configurazione dal telefono" : "");
  secrets::forgetNet(f.ssid, "wifi-forget dal PC");  // pulizia profonda e riavvio fra 3 s
  loadSaved();
  pcMemForget(f.ssid);
  post(EvType::WifiForgotten, f.ssid);
}

void NetOut::send(const char *json, size_t n) {
  if (!wsConn) { LogSerial.printf("[m5] non mandato (PC scollegato): %.*s\n", int(n), json); return; }
  ws.sendTXT(json, n);
  LogSerial.printf("[m5] → %.*s\n", int(n), json);
}

void NetOut::event(const cal::LinkEvent &e) {
  using cal::LinkEvType;
  switch (e.type) {
    case LinkEvType::PairCode: LogSerial.println("[m5] abbinamento: impegno verificato, codice sul display"); post(EvType::PairCode, "", e.text); break;
    case LinkEvType::PairReady: LogSerial.println("[m5] abbinamento: il PC ha il codice giusto, si aspetta la conferma sul display"); post(EvType::PairReady); break;
    case LinkEvType::PairDone: {
      const bool ok = secrets::savePair(gLink.key(), pcHostName);
      gOtaPubOk = false;  // abbinamento nuovo: la chiave OTA va ripresa (ota-key, OTA.md §2)
      LogSerial.printf("[m5] abbinato a «%s»: chiave %s\n", pcHostName, !ok ? "NON salvata" : secrets::keysProtected() ? "salvata cifrata" : "salvata (chiavi non protette)");
      post(EvType::PairDone);
      break;
    }
    case LinkEvType::PairAbort: LogSerial.printf("[m5] abbinamento annullato: %s\n", e.text); post(EvType::PairAbort, "", e.text); break;
    case LinkEvType::Authed:
      LogSerial.println("[m5] servizio autenticato (auth-ok): permessi e impostazioni remote attivi");
      gAuthRetries = 0;
      post(EvType::Authed);
      if (haveSettings) sendSettings();  // ora con remote: true
      if (havePower) sendPower();        // status anche dopo auth-ok (PROTOCOLLO.md «OTA»)
      sendWifiList();                    // reti salvate (0.8.0), solo autenticato
      sendThemes();                      // 0.13.0: temi sulla scheda, solo autenticato
      pcMemRemember();                   // 0.12.1: ultimo PC di questa rete (scritto solo se cambia)
      otaAfterAuth();
      memLog("dopo auth-ok");
      break;
    case LinkEvType::Redo: LogSerial.printf("[m5] abbinamento da rifare: %s\n", e.text); post(EvType::Redo, "", e.text); break;
    case LinkEvType::PermRequest:
      LogSerial.printf("[m5] perm-request %s verificata: %s · %s %s\n", e.req.id, e.req.name, e.req.tool, e.req.arg);
      if (xQueueSend(gPerm, &e.req, 0) != pdTRUE) LogSerial.println("[m5] coda dei permessi piena: la richiesta resta nel terminale");
      break;
    case LinkEvType::PermClose: LogSerial.printf("[m5] perm-close %s (%s)\n", e.id, e.text); post(EvType::PermClose, "", e.text, 0, 0, e.id); break;
    case LinkEvType::Unpaired: secrets::forgetPair(); gOtaPubOk = false; LogSerial.println("[m5] abbinamento dimenticato: chiave cancellata"); break;
    case LinkEvType::Rejected: LogSerial.printf("[m5] scartato: %s\n", e.text); break;
    case LinkEvType::AuthRetry:  // chiusa dal giro del task, fuori dalla callback del WebSocket
      gAuthRetries++;
      LogSerial.printf("[m5] %s (heap libero %u, blocco max %u): chiudo e riprovo fra %d s (%d di fila)\n", e.text,
                       unsigned(ESP.getFreeHeap()), unsigned(ESP.getMaxAllocHeap()), gAuthRetries > 1 ? 30 : 5, gAuthRetries);
      gAuthRetry = true;
      break;
  }
}

void postCmd(EvType t, const cal::RemoteCmd &c) {
  Event e;
  memset(&e, 0, sizeof e);
  e.type = t;
  scopy(e.id, c.id);
  e.patch = c.patch;
  if (xQueueSend(gEv, &e, 0) != pdTRUE) {
    // Il loop non lo vedrà mai: meglio dirlo subito al servizio che lasciarlo scadere.
    LogSerial.printf("[net] coda eventi piena: comando %s rifiutato\n", c.id);
    sendAck(c.id, false, "display occupato, riprova");
  }
}

// ---------- temi (0.13.0, PROTOCOLLO.md «Temi») ----------
void sendThemes() {
  if (!wsConn || !gLink.authed()) return;
  cal::theme::CatalogEntry e[cal::theme::kMaxThemes];
  themefs::lock();  // il loop (themeBroken) può togliere un tema nello stesso momento
  const int n = cal::theme::catalog(themefs::storage(), e, cal::theme::kMaxThemes);
  themefs::unlock();
  char buf[640];
  const size_t len = cal::theme::encodeThemes(e, n, gThemeCur, buf, sizeof buf);
  if (len) ws.sendTXT(buf, len);
  LogSerial.printf("[temi] themes: %d sulla scheda, attivo «%s»\n", n, gThemeCur[0] ? gThemeCur : "Calamaio");
}

void sendThemeAck(const char *id, bool ok, const char *why) {
  char buf[200];
  const size_t n = cal::theme::encodeAck(id, ok, why, buf, sizeof buf);
  if (n && wsConn) ws.sendTXT(buf, n);
  LogSerial.printf("[temi] theme-ack %s: %s%s%s\n", id, ok ? "ok" : "rifiutato", ok ? "" : " · ", ok ? "" : why);
}

// Dove vanno i pezzi di un download (temi; dalla 0.14.0 anche gli avatar).
struct BlobSink {
  virtual bool write(const uint8_t *p, size_t n, char *why, size_t cap) = 0;
};

// GET di url nel task di rete, fuori dalla callback: HTTP/1.0 come l'OTA, connessione e GET 5 s ciascuno, poi al
// massimo 15 s per il corpo, a pezzi da 512 byte, mai oltre size. true = size byte passati al sink. noinline: i suoi
// ~1 KB di stack (HTTPClient, pezzo) non restano nel frame di task().
__attribute__((noinline)) bool fetchBlob(const char *url, uint32_t size, BlobSink &sink, char *why, size_t cap) {
  WiFiClient client;
  HTTPClient http;
  http.useHTTP10(true);
  http.setReuse(false);
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  bool ok = http.begin(client, url);
  const int code = ok ? http.GET() : -1;
  if (code != 200) {
    ok = false;
    if (code < 0) snprintf(why, cap, "download non riuscito (%s)", HTTPClient::errorToString(code).c_str());
    else snprintf(why, cap, "download non riuscito (HTTP %d)", code);
  }
  uint8_t chunk[512];
  const uint32_t t0 = millis();
  uint32_t got = 0;
  WiFiClient *s = ok ? http.getStreamPtr() : nullptr;
  if (ok && !s) { ok = false; cal::scopy(why, cap, "download interrotto"); }
  while (ok && got < size) {
    if (millis() - t0 > 15000) { ok = false; cal::scopy(why, cap, "download troppo lento"); break; }
    const int av = s->available();
    if (av <= 0) {
      if (!s->connected()) { ok = false; cal::scopy(why, cap, "download interrotto"); break; }
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    uint32_t want = size - got;  // mai oltre la dimensione offerta
    if (want > uint32_t(av)) want = uint32_t(av);
    if (want > sizeof chunk) want = sizeof chunk;
    const int n = s->read(chunk, want);
    if (n <= 0) continue;
    ok = sink.write(chunk, size_t(n), why, cap);
    got += uint32_t(n);
  }
  http.end();
  return ok;
}

struct ThemeSink : BlobSink {
  bool write(const uint8_t *p, size_t n, char *why, size_t cap) override { return gThemeInst.write(p, n, why, cap); }
};

// Download e installazione del tema (nel task di rete, fuori dalla callback). noinline come fetchBlob.
__attribute__((noinline)) void themeLoop() {
  if (!gThemePending) return;
  gThemePending = false;
  const cal::theme::Offer &o = gThemeOffer;
  char url[160], why[96] = "";
  const uint32_t t0 = millis();
  bool ok = cal::theme::downloadUrl(pcIp, pcPort, o.token, url, sizeof url) &&
            gThemeInst.begin(themefs::storage(), o.id, o.crc, o.size, why, sizeof why);
  if (!why[0] && !ok) cal::scopy(why, "indirizzo del download non valido");
  ThemeSink sink;
  ok = ok && fetchBlob(url, o.size, sink, why, sizeof why);
  if (ok) {
    uint8_t *scratch = static_cast<uint8_t *>(malloc(cal::theme::kMaxBlock));  // solo durante il controllo
    // il controllo (CRC e blocchi) legge solo /th/tmp: fuori dal lucchetto
    ok = scratch && gThemeInst.verify(scratch, why, sizeof why);
    if (!scratch) cal::scopy(why, "memoria insufficiente");
    free(scratch);
  }
  if (ok) {
    themefs::lock();  // il loop (themeBroken) può togliere un tema e riscrivere /th/order nello stesso momento
    ok = gThemeInst.commit(gThemeCur, why, sizeof why, themefs::protectedId());  // né l'attivo né quello in attivazione
    themefs::unlock();
  }
  if (!ok) gThemeInst.abort();
  sendThemeAck(o.id, ok, why);
  if (ok) {
    LogSerial.printf("[temi] %s installato (%lu byte, crc %08lx) in %lu ms\n", o.id, (unsigned long)o.size, (unsigned long)o.crc,
                     (unsigned long)(millis() - t0));
    post(EvType::ThemeStored, "", "", 0, 0, o.id);
    sendThemes();
  }
}

// ---------- 0.14.0 (R4): avatar a pezzi ----------
void sendAvatarAck(const char *id, uint32_t crc, bool ok, const char *why) {
  char buf[220];
  const size_t n = cal::encodeAvatarAck(id, crc, ok, why, buf, sizeof buf);
  if (n && wsConn) ws.sendTXT(buf, n);
  LogSerial.printf("[avatar] avatar-ack %s: %s%s%s\n", id, ok ? "salvato" : "rifiutato", ok ? "" : " · ", ok ? "" : why);
}

struct AvatarSink : BlobSink {
  cal::AvatarDownload &d;
  explicit AvatarSink(cal::AvatarDownload &dl) : d(dl) {}
  bool write(const uint8_t *p, size_t n, char *why, size_t cap) override { return d.write(p, n, why, cap); }
};

// Download nel temporaneo di avfs con i controlli del CAVT man mano (cal::AvatarDownload, sullo stack: niente blocco
// da 9,5 KB né base64), poi il file pronto al loop. noinline come themeLoop.
__attribute__((noinline)) void avatarLoop() {
  if (gAvSt != kAvPending) return;
  const cal::AvatarOffer o = gAvOffer;
  char url[160], why[96] = "";
  const uint32_t t0 = millis();
  memLog("prima dell'avatar a pezzi", true);
  cal::AvatarDownload dl;
  ::Avatar hdr;
  bool ok = cal::avatarUrl(pcIp, pcPort, o.token, url, sizeof url);
  if (!ok) cal::scopy(why, "indirizzo del download non valido");
  ok = ok && dl.begin(avfs::storage(), o, why, sizeof why);
  AvatarSink sink(dl);
  ok = ok && fetchBlob(url, o.size, sink, why, sizeof why);
  ok = ok && dl.finish(hdr, why, sizeof why);
  if (ok) gAvSt = kAvAdopting;  // prima della coda: il loop può prenderlo e rispondere subito (anche request)
  if (ok && xQueueSend(gAvFile, &hdr, 0) != pdTRUE) { ok = false; cal::scopy(why, "un altro avatar in arrivo, riprova"); }
  if (!ok) {
    dl.abort();
    gAvSt = kAvIdle;
    sendAvatarAck(o.id, o.crc, false, why);
    if (dl.badContent()) {  // CAVT rotto sul PC: il loop non lo richiede fino alla prossima connessione (AvatarNeeds)
      Event e;
      memset(&e, 0, sizeof e);
      e.type = EvType::AvatarRejected;
      cal::scopy(e.avatar.id, o.id);
      e.avatar.crc = o.crc;
      e.avatar.hasCrc = true;
      if (xQueueSend(gEv, &e, 0) != pdTRUE) LogSerial.println("[net] coda eventi piena: rifiuto dell'avatar perso");
    }
    memLog("avatar a pezzi non riuscito", true);
    return;
  }
  // Adopting: il loop adotta e risponde (NetCmd::AvatarAck)
  LogSerial.printf("[avatar] %s scaricato a pezzi (%lu byte, crc %08lx) in %lu ms\n", o.id, (unsigned long)o.size,
                   (unsigned long)o.crc, (unsigned long)(millis() - t0));
  memLog("dopo l'avatar a pezzi", true);
}

void onText(const uint8_t *data, size_t len) {
  if (len > cal::kMaxFrame) { LogSerial.printf("[ws] messaggio di %u byte ignorato (oltre %u)\n", unsigned(len), unsigned(cal::kMaxFrame)); return; }
  gDoc.clear();
  if (!cal::parseMessage(reinterpret_cast<const char *>(data), len, gDoc)) {
    LogSerial.printf("[ws] messaggio malformato ignorato (%u byte)\n", unsigned(len));
    return;
  }
  const JsonVariantConst root = gDoc.as<JsonVariantConst>();
  if (gLink.onMessage(root, millis(), gOut)) return;  // M5: abbinamento, auth, permessi
  switch (cal::msgType(root)) {
    case cal::MsgType::Welcome: {
      cal::WelcomeMsg w;
      cal::decodeWelcome(root, w);
      LogSerial.printf("[ws] welcome da «%s» (servizio %s), fuso %s%s\n", w.host, w.service, w.tz,
                    !w.hasPaired ? "" : w.paired ? ", il PC ha una chiave per noi" : ", il PC non ha chiavi per noi");
      {
        Event e;
        memset(&e, 0, sizeof e);
        e.type = EvType::Welcome;
        scopy(e.ssid, w.host);
        scopy(e.text, w.tz);
        e.time = w.time;
        e.hasAvatar = w.hasDefAvatar;
        e.avatar = w.defAvatar;
        if (xQueueSend(gEv, &e, 0) != pdTRUE) LogSerial.println("[net] coda eventi piena: welcome perso");
      }
      gLink.onWelcome(w.hasPaired, w.paired, gOut);
      break;
    }
    case cal::MsgType::State:
      if (!gScratch || !gMailbox) { LogSerial.println("[ws] state ignorato: memoria non disponibile"); break; }
      if (!cal::decodeState(root, *gScratch)) { LogSerial.println("[ws] state senza time: ignorato"); break; }
      xSemaphoreTake(gMx, portMAX_DELAY);
      memcpy(gMailbox, gScratch, sizeof *gMailbox);
      gPending = true;
      gLastSessions = gScratch->nSessions;
      xSemaphoreGive(gMx);
      LogSerial.printf("[ws] state: %d sessioni%s, %u byte\n", gScratch->nSessions,
                    gScratch->sessionsDropped ? " (altre scartate oltre 6)" : "", unsigned(len));
      break;
    case cal::MsgType::Avatar: {
      // Il CAVT lo valida e lo salva il loop (AvatarCache::store): qui solo base64 → byte, sullo heap.
      cal::AvatarMsg *p = static_cast<cal::AvatarMsg *>(malloc(sizeof(cal::AvatarMsg)));
      if (!p) { LogSerial.println("[avatar] memoria esaurita: avatar scartato (si richiede fra 30 s)"); break; }
      const cal::AvatarMsgError err = cal::decodeAvatarMsg(root, *p);
      if (err != cal::AvatarMsgError::Ok) {
        static const char *const kWhy[] = {"ok", "tipo", "id", "crc", "data (base64 o oltre 7 072 byte)"};
        LogSerial.printf("[avatar] messaggio avatar scartato: %s (%u byte)\n", kWhy[int(err)], unsigned(len));
        free(p);
        break;
      }
      if (xQueueSend(gAv, &p, 0) != pdTRUE) {
        LogSerial.printf("[avatar] coda piena: %s scartato (si richiede fra 30 s)\n", p->id);
        free(p);
        break;
      }
      LogSerial.printf("[avatar] ricevuto %s crc %08lx: %u byte\n", p->id, (unsigned long)p->crc, unsigned(p->len));
      break;
    }
    case cal::MsgType::Game: {  // 0.10.0: riassunto del gioco della mascotte; lo applica il loop (Ui::netGame)
      cal::GameMsg *p = static_cast<cal::GameMsg *>(malloc(sizeof(cal::GameMsg)));
      if (!p) { LogSerial.println("[gioco] memoria esaurita: game scartato"); break; }
      cal::decodeGame(root, *p);
      if (xQueueSend(gGame, &p, 0) != pdTRUE) { LogSerial.println("[gioco] coda piena: game scartato"); free(p); break; }
      LogSerial.printf("[gioco] game: %s, livello %d, indossa %s%s%s\n", p->on ? "acceso" : "spento", p->level,
                       p->hasWear ? p->wear.id : "niente", p->hasCelebrate ? ", traguardo " : "", p->hasCelebrate ? p->celebrateId : "");
      break;
    }
    case cal::MsgType::Accessory: {  // 0.10.0: CACC in base64; lo valida e lo salva il loop (AccessoryStore::store)
      cal::AccessoryMsg *p = static_cast<cal::AccessoryMsg *>(malloc(sizeof(cal::AccessoryMsg)));
      if (!p) { LogSerial.println("[gioco] memoria esaurita: accessorio scartato (si richiede fra 30 s)"); break; }
      const cal::BlobMsgError err = cal::decodeAccessoryMsg(root, *p);
      if (err != cal::BlobMsgError::Ok || xQueueSend(gAcc, &p, 0) != pdTRUE) {
        LogSerial.printf("[gioco] accessorio scartato (%s, %u byte): si richiede fra 30 s\n", err != cal::BlobMsgError::Ok ? "messaggio" : "coda piena", unsigned(len));
        free(p);
      }
      break;
    }
    case cal::MsgType::Anchors: {  // 0.10.0: CANC in base64 legato a (avatar, avatarCrc); lo valida il loop
      cal::AnchorsMsg *p = static_cast<cal::AnchorsMsg *>(malloc(sizeof(cal::AnchorsMsg)));
      if (!p) { LogSerial.println("[gioco] memoria esaurita: ancore scartate (si richiedono fra 30 s)"); break; }
      const cal::BlobMsgError err = cal::decodeAnchorsMsg(root, *p);
      if (err != cal::BlobMsgError::Ok || xQueueSend(gAnc, &p, 0) != pdTRUE) {
        LogSerial.printf("[gioco] ancore scartate (%s, %u byte): si richiedono fra 30 s\n", err != cal::BlobMsgError::Ok ? "messaggio" : "coda piena", unsigned(len));
        free(p);
      }
      break;
    }
    case cal::MsgType::Bye: {
      // M6 (PROTOCOLLO.md, bye): summary.off (ms epoch) è l'unico campo obbligatorio; il resto (sessions, last…)
      // il display non lo usa: il riepilogo si disegna dall'ultimo state, che ha gli stessi dati.
      const JsonVariantConst sum = root["summary"];
      const int64_t off = sum["off"].is<int64_t>() ? sum["off"].as<int64_t>() : 0;
      LogSerial.printf("[ws] bye: il PC si spegne o il servizio riavvia (off %lld): cerco il PC per 60 s (subito, 5 s, 5 s, poi ogni 15 s)\n",
                    (long long)off);
      byeAt = millis() ? millis() : 1;
      post(EvType::Bye, "", "", 0, off);
      break;
    }
    case cal::MsgType::Set:
      if (!cal::decodeSet(root, gCmd)) { LogSerial.println("[ws] set senza id valido (1-32 byte): ignorato, niente ack"); break; }
      if (!gLink.authed()) {  // M5 §2: senza auth-ok niente set remoti (le impostazioni restano dal menu)
        LogSerial.printf("[ws] set %s rifiutato: connessione non autenticata\n", gCmd.id);
        sendAck(gCmd.id, false, kNotAuthed);
        break;
      }
      if (!gCmd.ok) {  // niente da applicare: risponde subito la rete, il modello non cambia
        LogSerial.printf("[ws] set %s rifiutato: %s\n", gCmd.id, gCmd.error);
        sendAck(gCmd.id, false, gCmd.error);
        break;
      }
      LogSerial.printf("[ws] set %s: %u byte, al loop\n", gCmd.id, unsigned(len));
      postCmd(EvType::RemoteSet, gCmd);
      break;
    case cal::MsgType::Ack: {
      cal::AckMsg a;
      if (!cal::decodeAck(root, a)) { LogSerial.println("[ws] ack senza id od ok: ignorato"); break; }
      LogSerial.printf("[ws] ack %s: %s%s%s\n", a.id, a.ok ? "ok" : "rifiutato", a.error[0] ? " · " : "", a.error);
      post(EvType::Ack, "", a.error, a.ok ? 1 : 0, 0, a.id);
      break;
    }
    case cal::MsgType::OtaKey: {
      const cal::ota::KeyCheck k = cal::ota::checkKey(root, gLink.authed(), gLink.sessionKey(), gOtaPubOk, gOtaPub);
      if (k.verdict == cal::ota::KeyVerdict::Save) {
        const bool ok = secrets::saveOtaKey(k.pub);
        memcpy(gOtaPub, k.pub, 32);
        gOtaPubOk = true;
        LogSerial.printf("[ota] chiave degli aggiornamenti %s\n", ok ? "salvata nel vault" : "tenuta in RAM (vault: salvataggio non riuscito)");
      } else if (k.verdict == cal::ota::KeyVerdict::Rejected) {
        LogSerial.printf("[ota] ota-key scartato: %s\n", k.why);
      }
      break;
    }
    case cal::MsgType::OtaOffer: {
      char id[cal::ota::kIdLen];
      if (!cal::ota::offerId(root, id)) { LogSerial.println("[ota] ota-offer senza id valido: ignorato"); break; }
      static cal::ota::Offer o;  // ~450 byte: fuori dallo stack
      char why[cal::ota::kWhyLen] = "";
      const cal::ota::Identity me = {gHost, cal::kHeltec213.id, otahw::maxImageSize()};
      if (gUpd.active() || gOfferWaiting) {
        otaStatus(id, cal::OtaPhase::Rejected, 0, 0, "aggiornamento già in corso");
        break;
      }
      if (!cal::ota::checkOffer(root, gLink.authed(), gLink.sessionKey(), gOtaPubOk, gOtaPub, me, o, why, sizeof why)) {
        LogSerial.printf("[ota] ota-offer %s rifiutato: %s\n", id, why);
        otaStatus(id, cal::OtaPhase::Rejected, 0, o.m.size, why);
        break;
      }
      gOffer = o;
      gOfferWaiting = true;
      LogSerial.printf("[ota] ota-offer %s verificato: %s → %s, %lu byte, firma ok; il display controlla le condizioni\n", id,
                       cal::kFwVersion, o.m.version, (unsigned long)o.m.size);
      Event e;
      memset(&e, 0, sizeof e);
      e.type = EvType::OtaOffer;
      scopy(e.id, id);
      scopy(e.text, o.m.version);
      e.time = o.m.size;
      if (xQueueSend(gEv, &e, 0) != pdTRUE) {
        gOfferWaiting = false;
        otaStatus(id, cal::OtaPhase::Rejected, 0, o.m.size, "display occupato, riprova");
      }
      break;
    }
    case cal::MsgType::OtaCancel: {
      const char *why = nullptr;
      const char *current = gOfferWaiting ? gOffer.id : gUpd.phase() == cal::OtaPhase::Receive ? gUpd.offer().id : "";
      if (!cal::ota::checkCancel(root, gLink.authed(), gLink.sessionKey(), current, &why)) {
        LogSerial.printf("[ota] %s\n", why);
        break;
      }
      LogSerial.printf("[ota] %s annullato dal PC\n", current);
      if (gOfferWaiting) {  // non ancora partito: il loop risponderà con OtaGo, che non troverà più l'offerta
        gOfferWaiting = false;
        otaStatus(gOffer.id, cal::OtaPhase::Error, 0, gOffer.m.size, cal::ota::kCancelled);
      } else {
        gUpd.cancel(cal::ota::kCancelled, gUpdOut);  // errore: immagine abbandonata, ota-status errore, schermata
      }
      break;
    }
    case cal::MsgType::LogStart: {
      if (!gLogBuf) gLogBuf = static_cast<char *>(malloc(kLogBufLen));
      if (!gLogBuf) {  // registro non avviato: quello in corso (se c'era) aveva già il buffer
        LogSerial.printf("[log] log-start scartato: memoria insufficiente (heap libero %u, blocco max %u)\n",
                         unsigned(ESP.getFreeHeap()), unsigned(ESP.getMaxAllocHeap()));
        break;
      }
      char why[64] = "";
      if (logtee::start(gLog, root, gLink.authed(), cal::Ms(millis()), why, sizeof why))
        LogSerial.printf("[log] registro da remoto acceso (%s)\n", gLog.id());
      else
        LogSerial.printf("[log] log-start scartato: %s\n", why);
      if (!gLog.active()) logEnd();
      break;
    }
    case cal::MsgType::LogStop:
      if (gLog.stop(root, gLink.authed())) {
        LogSerial.printf("[log] registro da remoto spento (%s)\n", gLog.id());
        logEnd();
      }
      break;
    case cal::MsgType::WifiAdd: onWifiAdd(root); break;
    case cal::MsgType::WifiForget: onWifiForget(root); break;
    case cal::MsgType::Workday: {  // 0.9.0 (PROTOCOLLO.md, «Giornata di lavoro»): solo autenticato, poi status e ack
      cal::WorkdayCmd w;
      if (!cal::decodeWorkday(root, w)) { LogSerial.println("[ws] workday senza id valido (1-32 byte): ignorato, niente ack"); break; }
      if (!gLink.authed()) { sendAck(w.id, false, kNotAuthed); break; }
      if (!w.ok) { LogSerial.printf("[ws] workday %s rifiutato: %s\n", w.id, w.error); sendAck(w.id, false, w.error); break; }
      LogSerial.printf("[ws] workday %s: %s, al loop\n", w.id, w.start ? "start" : "stop");
      post(EvType::Workday, "", "", w.start ? 1 : 0, 0, w.id);
      break;
    }
    case cal::MsgType::AvatarOffer: {  // 0.14.0 (R4): anche senza autenticazione, come `avatar`; uno alla volta
      cal::AvatarOffer o;
      if (!cal::decodeAvatarOffer(root, o)) { LogSerial.println("[avatar] avatar-offer non valido: ignorato"); break; }
      if (gAvSt != kAvIdle) { sendAvatarAck(o.id, o.crc, false, "un altro avatar in arrivo, riprova"); break; }
      gAvOffer = o;
      gAvSt = kAvPending;
      LogSerial.printf("[avatar] avatar-offer %s: %lu byte, scarico al prossimo giro\n", o.id, (unsigned long)o.size);
      break;
    }
    case cal::MsgType::ThemeOffer: {  // 0.13.0 (PROTOCOLLO.md «Temi»): solo autenticato (acceptOffer, provata in B4)
      cal::theme::Offer o;
      if (!cal::theme::decodeOffer(root, o)) { LogSerial.println("[temi] theme-offer non valido: ignorato"); break; }
      const cal::theme::OfferVerdict v = cal::theme::acceptOffer(gLink.authed(), gThemePending || gThemeInst.busy());
      if (v == cal::theme::OfferVerdict::NotAuthed) { sendThemeAck(o.id, false, kNotAuthed); break; }
      if (v == cal::theme::OfferVerdict::Busy) { sendThemeAck(o.id, false, "un altro tema in arrivo, riprova"); break; }
      gThemeOffer = o;
      gThemePending = true;
      LogSerial.printf("[temi] theme-offer %s: %lu byte, scarico al prossimo giro\n", o.id, (unsigned long)o.size);
      break;
    }
    case cal::MsgType::ThemeUse: {
      char id[cal::theme::kIdLen];
      if (!cal::theme::acceptUse(gLink.authed()) || !cal::theme::decodeUse(root, id, sizeof id)) { LogSerial.println("[temi] theme-use ignorato"); break; }
      post(EvType::ThemeUse, "", "", 0, 0, id);
      break;
    }
    case cal::MsgType::FullRefresh:
      if (!cal::decodeFullRefresh(root, gCmd)) { LogSerial.println("[ws] full-refresh senza id valido: ignorato"); break; }
      if (!gLink.authed()) { sendAck(gCmd.id, false, kNotAuthed); break; }
      LogSerial.printf("[ws] full-refresh %s: al loop\n", gCmd.id);
      postCmd(EvType::FullRefresh, gCmd);
      break;
    default:
      LogSerial.printf("[ws] messaggio sconosciuto ignorato (%u byte)\n", unsigned(len));
  }
}

void wsClosed(uint32_t slowMs);

void onWs(WStype_t type, uint8_t *payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      ws.noDelay();
      wsConn = true;
      lastRx = millis();
      gStorm.opened(millis());  // 0.14.0 (R4)
      gAllocAtOpen = gAllocFails.load(std::memory_order_relaxed);
      LogSerial.printf("[ws] connesso a ws://%s:%u%s (tentativo %d)\n", pcIp, pcPort, pcPath, wsTry.count());
      wsTry.stop();
      gPcLostOwed = false;
      gLink.onConnected();
      sendHello();
      if (haveSettings) sendSettings();  // subito dopo hello (PROTOCOLLO.md: il servizio lo applica dopo)
      if (havePower) sendPower();        // status: cavo e batteria (OTA), anche senza autenticazione
      memLog("dopo il WebSocket connesso");
      break;
    case WStype_DISCONNECTED:
      if (wsConn) {
        // 0.14.0 (R4): chiusure brevi a raffica, o con un'allocazione fallita in mezzo (la 1011 della libreria quando
        // non ha il blocco per un messaggio): il primo tentativo aspetta, in silenzio (il PC c'è).
        const bool mem = gAllocFails.load(std::memory_order_relaxed) != gAllocAtOpen;
        const uint32_t hold = gStorm.closed(millis(), mem);
        if (hold > gWsHoldMs) {
          gWsHoldMs = hold;
          gWsQuiet = true;
          LogSerial.printf("[ws] %d chiusure brevi di fila%s\n", gStorm.shortCloses(), mem ? ", con memoria mancata" : "");
        }
        memLog("WebSocket chiuso");
        const bool afterBye = byeAt && millis() - byeAt < uint32_t(cal::kByeGraceMs);
        if (gWsHoldMs) LogSerial.printf("[ws] disconnesso: primo tentativo fra %u s\n", unsigned(gWsHoldMs / 1000));
        else LogSerial.printf("[ws] disconnesso: riprovo subito, poi a 5 s, 5 s e ogni %d s\n", afterBye ? 15 : 30);
        wsClosed(afterBye ? cal::kByeRetryMs : cal::kRetrySlowMs);
      }
      break;
    case WStype_TEXT:
      lastRx = millis();
      onText(payload, len);
      break;
    case WStype_PING:
    case WStype_PONG:
      lastRx = millis();  // anche i ping del servizio (ogni 15 s) contano come segno di vita
      break;
    case WStype_ERROR:
      LogSerial.printf("[ws] errore %.*s\n", int(len), payload ? reinterpret_cast<const char *>(payload) : "");
      break;
    default:
      lastRx = millis();
      break;
  }
}

// ---------- 0.12.1-gb: PC senza mDNS (indirizzo manuale, ultimo noto per rete) ----------
const char *const kPcNs = "calamaio-pc";

void pcMemLoad() {
  Preferences p;
  gPcMem.clear();
  gPcBlobLen = gPcMem.encode(gPcBlob, sizeof gPcBlob);  // niente in NVS = tabella vuota: nessuna scrittura per niente
  if (!p.begin(kPcNs, true)) return;  // namespace mai scritto
  const size_t n = p.getBytesLength("v1");
  if (n && n <= sizeof gPcBlob) {
    // 0.14.1: si legge direttamente nella copia; se non vale, la copia torna quella della tabella vuota
    const bool got = p.getBytes("v1", gPcBlob, n) == n;
    if (got && gPcMem.decode(gPcBlob, n)) {
      gPcBlobLen = n;
      LogSerial.printf("[pc] indirizzi dei PC: %d reti\n", gPcMem.count());
    } else {
      if (got) LogSerial.println("[pc] indirizzi dei PC in NVS non validi: ignorati");
      gPcMem.clear();
      gPcBlobLen = gPcMem.encode(gPcBlob, sizeof gPcBlob);
    }
  }
  p.end();
}

// Scrive la tabella solo se è diversa dall'ultima copia (niente usura a ogni ricollegamento). Prima toglie le voci
// delle reti non più salvate (tranne quella in uso). false = scrittura non riuscita.
bool pcMemSave(const char *why) {
  static char keep[cal::kSavedMax + 1][33];  // statici: solo il task di rete, niente stack
  int nk = 0;
  for (int k = 0; k < gNSaved; k++) scopy(keep[nk++], gSaved[k].ssid);
  scopy(keep[nk++], currentSsid());
  gPcMem.prune(keep, nk);
  // 0.14.1: la copia da scrivere sta sullo heap solo qui; senza memoria «non salvato», come per un errore della NVS
  uint8_t *blob = static_cast<uint8_t *>(malloc(cal::PcMemory::kBlobMax));
  if (!blob) { LogSerial.printf("[pc] %s: NON salvato (memoria)\n", why); return false; }
  const size_t n = gPcMem.encode(blob, cal::PcMemory::kBlobMax);
  if (n == gPcBlobLen && !memcmp(blob, gPcBlob, n)) { free(blob); return true; }
  Preferences p;
  const bool ok = p.begin(kPcNs, false) && p.putBytes("v1", blob, n) == n;
  p.end();
  if (!ok) {
    free(blob);
    LogSerial.printf("[pc] %s: NON salvato (NVS)\n", why);
    return false;
  }
  memcpy(gPcBlob, blob, n);
  free(blob);
  gPcBlobLen = n;
  LogSerial.printf("[pc] %s: salvato (%d reti con un indirizzo)\n", why, gPcMem.count());
  return true;
}

// Rete tolta: via anche i suoi indirizzi.
void pcMemForget(const char *ssid) {
  if (gPcMem.forget(ssid)) pcMemSave("indirizzi della rete dimenticata tolti");
}

// Dopo auth-ok: questo è il PC di questa rete.
void pcMemRemember() {
  const char *ssid = currentSsid();
  if (!pcKnown || !ssid[0]) return;
  cal::PcAddr a;
  memset(&a, 0, sizeof a);
  scopy(a.ip, pcIp);
  a.port = pcPort;
  scopy(a.path, pcPath);
  if (!gPcMem.setLast(ssid, a)) return;
  char t[24];
  cal::pcAddrText(a, t, sizeof t);
  char why[96];
  snprintf(why, sizeof why, "ultimo PC di «%s»: %s", ssid, t);
  pcMemSave(why);
}

// Prova TCP (al massimo kProbeMs): la connessione della libreria a un indirizzo che non risponde ne bloccherebbe 5.
bool pcReachable(const char *ip, uint16_t port) {
  IPAddress a;
  if (!a.fromString(ip)) return false;
  WiFiClient c;
  const bool ok = c.connect(a, port, int32_t(kProbeMs));
  c.stop();
  return ok;
}

void setTarget(const cal::PcAddr &a, cal::PcSrc src, bool &changed) {
  changed = !pcKnown || strcmp(a.ip, pcIp) || a.port != pcPort || strcmp(a.path, pcPath);
  scopy(pcIp, a.ip);
  pcPort = a.port;
  scopy(pcPath, a.path);
  cal::pcHostText(a, pcHostName, sizeof pcHostName);  // a schermo l'indirizzo stesso
  pcKnown = true;
  pcSrc = src;
  post(EvType::PcHost, "", pcHostName);
}

// Dopo un tentativo mDNS senza risposta: indirizzo manuale, poi ultimo noto (alternati). true = destinazione
// impostata (e raggiungibile); false = nessuna riserva o non risponde, si riprova mDNS con la sua cadenza.
bool useFallback(bool &changed) {
  const cal::PcMemEntry *e = gPcMem.find(currentSsid());
  if (!e) return false;
  const bool hasManual = e->manual.ip[0] != 0;
  const bool hasLast = e->last.ip[0] && !(hasManual && cal::pcAddrSame(e->manual, e->last));
  const cal::PcSrc src = cal::pickFallback(hasManual, hasLast, fbPrev);
  if (src == cal::PcSrc::None) return false;
  fbPrev = src;
  const cal::PcAddr &a = src == cal::PcSrc::Manual ? e->manual : e->last;
  const char *what = src == cal::PcSrc::Manual ? "l'indirizzo manuale" : "l'ultimo noto";
  LogSerial.printf("[mdns] nessuna risposta: provo %s %s:%u\n", what, a.ip, a.port);
  if (!pcReachable(a.ip, a.port)) {
    LogSerial.printf("[mdns] %s %s:%u non risponde: riprovo mDNS fra %lu s\n", what, a.ip, a.port,
                     (unsigned long)(mdnsTry.nextDelay() / 1000));
    return false;
  }
  setTarget(a, src, changed);
  probedOk = true;
  return true;
}

// Una ricerca mDNS. true = PC valido trovato (indirizzo in pcIp/pcPort/pcPath; changed se è diverso).
bool queryPc(bool &changed) {
  const int n = MDNS.queryService("calamaio", "tcp");  // blocca fino a ~3 s: va bene, è il task di rete
  int pick = -1, empty = 0;
  for (int i = 0; i < n && pick < 0; i++) {
    if (MDNS.hasTxt(i, "proto") && MDNS.txt(i, "proto") != "1") continue;
    // Risposta vuota (servizio appena sparito: 0.0.0.0, porta 0, host vuoto): non è un PC.
    if (!cal::mdnsAnswerValid(MDNS.IP(i).toString().c_str(), MDNS.port(i))) { empty++; continue; }
    pick = i;
  }
  if (pick < 0) {
    LogSerial.printf("[mdns] nessun PC con _calamaio._tcp (%d risposte, %d vuote o senza indirizzo)\n", n < 0 ? 0 : n, empty);
    return false;
  }
  const String ip = MDNS.IP(pick).toString();
  const uint16_t port = MDNS.port(pick);
  String host = MDNS.hostname(pick);
  if (host.length() && host.indexOf('.') < 0) host += ".local";
  if (!host.length()) host = ip;  // senza nome si mostra l'indirizzo
  String path = MDNS.hasTxt(pick, "path") ? MDNS.txt(pick, "path") : String(kDefaultPath);
  if (!path.length() || path[0] != '/') path = kDefaultPath;
  changed = !pcKnown || ip != pcIp || port != pcPort || path != pcPath;
  scopy(pcIp, ip.c_str());
  pcPort = port;
  scopy(pcPath, path.c_str());
  scopy(pcHostName, host.c_str());
  pcKnown = true;
  pcSrc = cal::PcSrc::Mdns;  // vince mDNS
  fbPrev = cal::PcSrc::None;
  LogSerial.printf("[mdns] PC trovato: %s (%s:%u, percorso %s)\n", pcHostName, pcIp, pcPort, pcPath);
  post(EvType::PcHost, "", pcHostName);
  return true;
}

// Indirizzo nuovo (o primo): begin() della libreria, che da sola non prova (vedi WsClient).
void wsTarget(bool changed) {
  if (!changed && wsBegun) return;
  if (wsBegun) ws.disconnect();
  ws.begin(pcIp, pcPort, pcPath, "");  // nessun sottoprotocollo
  ws.manual();
  wsBegun = true;
}

void discover() {
  const uint32_t now = millis();
  if (wsConn) { fbDue = false; return; }
  bool changed = false;
  if (fbDue) {  // 0.12.1: mDNS non ha risposto al giro prima
    fbDue = false;
    if (!useFallback(changed)) return;
  } else {
    if (!mdnsTry.due(now)) return;
    const int n = mdnsTry.attempt(now);
    LogSerial.printf("[mdns] tentativo %d: cerco _calamaio._tcp, prossimo fra %lu s se non lo trovo\n", n,
                  (unsigned long)(mdnsTry.nextDelay() / 1000));
    if (!queryPc(changed)) { fbDue = gPcMem.find(currentSsid()) != nullptr; return; }
  }
  mdnsTry.stop();
  wsTarget(changed);
  wsTry.restart(millis());  // il primo collegamento parte subito, in questo stesso giro
}

// WebSocket chiuso (dalla libreria o da noi): tentativi, collegamento, registro, offerta, PcLost.
void wsClosed(uint32_t slowMs) {
  wsConn = false;
  wsTry.restart(millis() + gWsHoldMs, slowMs);
  gLink.onDisconnected(gOut);
  logEnd();
  dropOffer("collegamento chiuso");
  if (gAvSt == kAvPending) gAvSt = kAvIdle;  // 0.14.0: offerta dell'avatar scartata, si richiede al ritorno
  if (gWsQuiet) {
    gPcLostOwed = true;
    gPcLostOwedAt = millis();
  } else {
    post(EvType::PcLost);
  }
  gWsHoldMs = 0;
  gWsQuiet = false;
}

bool fallbackTarget() { return pcSrc == cal::PcSrc::Manual || pcSrc == cal::PcSrc::Last; }

void wsAttempt() {
  const uint32_t now = millis();
  if (wsConn || !pcKnown) return;
  int n = wsTry.count();
  if (probeDue) {
    probeDue = false;
    if (!fallbackTarget() || !wsTry.active()) return;
  } else {
    if (!wsTry.due(now)) return;
    n = wsTry.attempt(now);
    if (n >= 2) pcLostFlush();  // chiusura silenziosa e il tentativo dopo non è riuscito: il PC non c'è più
    if (n >= kWsRefreshFrom) {  // l'indirizzo del PC può essere cambiato
      bool changed = false;
      if (queryPc(changed)) wsTarget(changed);
      else if (fallbackTarget()) { probeDue = true; return; }  // la prova al prossimo giro
    }
  }
  // 0.12.1: indirizzo di riserva che non risponde più: si torna alla ricerca mDNS (che riprova anche le riserve).
  const bool fresh = probedOk;
  probedOk = false;
  if (fallbackTarget() && !fresh && !pcReachable(pcIp, pcPort)) {
    LogSerial.printf("[ws] tentativo %d: %s:%u (%s) non risponde, torno a cercare il PC con mDNS\n", n, pcIp, pcPort,
                     pcSrc == cal::PcSrc::Manual ? "indirizzo manuale" : "ultimo noto");
    wsTry.stop();
    pcLostFlush();
    mdnsTry.restart(millis());
    return;
  }
  LogSerial.printf("[ws] tentativo %d a ws://%s:%u%s, prossimo fra %lu s se non riesce\n", n, pcIp, pcPort, pcPath,
                (unsigned long)(wsTry.nextDelay() / 1000));
  ws.armNow();
}

void pcLoop() {
  // Fine della grazia dopo `bye`: si torna ai tentativi ogni 30 s (col cavo; senza, il loop va in sonno profondo).
  if (byeAt && millis() - byeAt >= uint32_t(cal::kByeGraceMs)) {
    byeAt = 0;
    wsTry.setSlow(cal::kRetrySlowMs);
    mdnsTry.setSlow(cal::kRetrySlowMs);
  }
  if (wsConn) byeAt = 0;
  if (gPcLostOwed && !wsConn && millis() - gPcLostOwedAt >= kPcLostOwedMs) {  // qualunque strada abbia fermato i tentativi
    LogSerial.println("[ws] PC non ricollegato dopo la chiusura silenziosa: PC non raggiungibile");
    pcLostFlush();
  }
  discover();
  if (!wsBegun) return;
  wsAttempt();
  ws.loop();
  const bool authRetry = gAuthRetry;
  gAuthRetry = false;
  const bool rxTimeout = wsConn && millis() - lastRx > kRxTimeoutMs;
  if (rxTimeout) LogSerial.println("[ws] 45 s senza niente dal PC: chiudo e riprovo");
  if (wsConn && (rxTimeout || authRetry)) {
    if (authRetry && !rxTimeout) {  // il PC risponde: niente raffica di riconnessioni, PcLost solo alla prima
      gWsHoldMs = gAuthRetries > 1 ? cal::kRetrySlowMs : cal::kRetryFastMs;
      gWsQuiet = gAuthRetries > 1;
    }
    ws.disconnect();  // la libreria chiama subito WStype_DISCONNECTED (wsClosed)
    if (wsConn) wsClosed(cal::kRetrySlowMs);
  }
  gLink.tick(millis(), gOut);
}

// ---------- aggiornamento remoto, registro, crash (docs/OTA.md) ----------
void sendPower() {
  char buf[160];  // con workday: ~100 byte
  const size_t n = cal::ota::encodePowerStatus(gPower, buf, sizeof buf);
  if (!n) return;
  ws.sendTXT(buf, n);
  LogSerial.printf("[ws] %s\n", buf);
}

void otaLoop() {
  if (gUpd.active()) {
    // Fino a 16 KB per giro: il giro dopo serve di nuovo WebSocket e HTTP.
    const cal::ota::Step st = gUpd.step(cal::Ms(millis()), gUpdOut);
    if (st == cal::ota::Step::Reboot) {
      LogSerial.printf("[ota] immagine verificata e scritta: riavvio fra 3 s nella %s\n", gUpd.offer().m.version);
      rebootAt = millis() + 3000;  // il tempo di mandare «riavvio» e di disegnare «Riavvio…»
    }
  }
  if (wsConn && gLog.active() && gLogBuf) {
    const size_t n = logtee::poll(gLog, cal::Ms(millis()), gLogBuf, kLogBufLen);
    if (n) ws.sendTXT(gLogBuf, n);
  }
  if (gLogBuf && !gLog.active()) logEnd();  // dump finito senza seguito, o scaduto
}

// Dopo auth-ok: com'è finito l'aggiornamento prima del riavvio (OTA.md §3.4) e l'eventuale rapporto di crash.
void otaAfterAuth() {
  if (!gBootReported) {
    gBootReported = true;
    using cal::ota::BootCase;
    const uint32_t sz = gBootRec.size;
    if (gBootCase == BootCase::Trial) {
      if (otahw::confirm()) post(EvType::OtaUpdated);
      if (gBootHasRec) otaStatus(gBootRec.id, cal::OtaPhase::Done, sz, sz, nullptr);
    } else if (gBootCase == BootCase::Confirmed) {
      otaStatus(gBootRec.id, cal::OtaPhase::Done, sz, sz, nullptr);
    } else if (gBootCase == BootCase::RolledBack) {
      otaStatus(gBootRec.id, cal::OtaPhase::Error, sz, sz, cal::ota::kRolledBack);
    }
    if (gBootHasRec && wsConn) otahw::clearRecord();
    else if (gBootHasRec) gBootReported = false;  // non mandato: al prossimo collegamento
  }
  if (!otahw::crashPending() || !wsConn) return;
  // 0.14.1: 2 KB sullo heap solo con un rapporto da mandare; senza memoria si riprova al prossimo auth-ok
  char *buf = static_cast<char *>(malloc(2048));
  if (!buf) { LogSerial.println("[crash] memoria non disponibile: rapporto rimandato al prossimo collegamento"); return; }
  const size_t n = otahw::crashJson(buf, 2048);
  if (n) {
    ws.sendTXT(buf, n);
    LogSerial.printf("[crash] → %s\n", buf);
    otahw::crashSent();
  }
  free(buf);
}

// ---------- comandi dall'interfaccia ----------
void handle(const cal::NetRequest &r) {
  if (r.cmd == cal::NetCmd::AvatarAck) {  // 0.14.0 (R4): il loop ha adottato (o no) l'avatar scaricato
    gAvSt = kAvIdle;
    sendAvatarAck(r.id, r.avatarCrcs[0], r.ok, r.error);
    return;
  }
  if (r.cmd == cal::NetCmd::ThemeCur) {
    cal::scopy(gThemeCur, r.id);
    sendThemes();
    return;
  }
  if (r.cmd == cal::NetCmd::Settings) {
    gSettings = r.values;
    haveSettings = true;
    if (wsConn) sendSettings();
    return;
  }
  if (r.cmd == cal::NetCmd::AvatarNeed) {
    char buf[256];
    const size_t n = cal::encodeAvatarNeed(r.avatarIds, r.nAvatarIds, buf, sizeof buf);
    if (wsConn && n) { ws.sendTXT(buf, n); LogSerial.printf("[ws] %s\n", buf); }
    else LogSerial.println("[ws] avatar-need non mandato: PC scollegato (si richiede dopo il prossimo state)");
    return;
  }
  // 0.10.0, gioco della mascotte: accessory-wear, accessory-need, anchors-need.
  if (r.cmd == cal::NetCmd::AccWear || r.cmd == cal::NetCmd::AccNeed || r.cmd == cal::NetCmd::AnchorsNeed) {
    char buf[400];
    const size_t n = r.cmd == cal::NetCmd::AccWear ? cal::encodeAccessoryWear(r.avatarIds[0], buf, sizeof buf)
                     : r.cmd == cal::NetCmd::AccNeed ? cal::encodeAccessoryNeed(r.avatarIds, r.nAvatarIds, buf, sizeof buf)
                                                     : cal::encodeAnchorsNeed(r.avatarIds, r.avatarCrcs, r.nAvatarIds, buf, sizeof buf);
    if (wsConn && n) { ws.sendTXT(buf, n); LogSerial.printf("[ws] %s\n", buf); }
    else LogSerial.printf("[ws] %s non mandato: PC scollegato\n", cal::netCmdKey(r.cmd));
    return;
  }
  if (r.cmd == cal::NetCmd::Dismiss) {
    char buf[160];
    const size_t n = cal::encodeDismiss(r.id, r.session, buf, sizeof buf);
    if (wsConn && n) { ws.sendTXT(buf, n); LogSerial.printf("[ws] dismiss: %s\n", buf); }
    else LogSerial.printf("[ws] dismiss %s non mandato: PC scollegato (si ripristina fra 10 s)\n", r.id);
    return;
  }
  if (r.cmd == cal::NetCmd::Status) {
    gPower = {r.battPct, r.usbOn, r.cal, r.hasWork, r.workOn, r.workEnd};
    havePower = true;
    if (wsConn) sendPower();
    return;
  }
  if (r.cmd == cal::NetCmd::OtaGo) {
    if (!gOfferWaiting || strcmp(r.id, gOffer.id) != 0) {
      // annullato dal PC mentre il loop decideva: se aveva mostrato la schermata, la chiude come errore
      if (r.ok) post(EvType::OtaPhase, "", cal::ota::kCancelled, int(cal::OtaPhase::Error));
      return;
    }
    gOfferWaiting = false;
    if (!r.ok) {
      LogSerial.printf("[ota] %s rifiutato dal display: %s\n", r.id, r.error);
      otaStatus(r.id, cal::OtaPhase::Rejected, 0, gOffer.m.size, r.error);
      return;
    }
    LogSerial.printf("[ota] %s: scarico l'immagine (link monouso, non registrato)\n", r.id);
    gOtaDone = 0;
    gUpd.start(gOffer, cal::kFwVersion, otahw::nextLabel(), otahw::flash(), otahw::source(), cal::Ms(millis()), gUpdOut);
    return;
  }
  switch (r.cmd) {  // M5
    case cal::NetCmd::PairConfirm:
      if (!gLink.confirmPair(gOut)) { LogSerial.println("[m5] conferma senza abbinamento pronto"); post(EvType::PairAbort, "", "connessione"); }
      return;
    case cal::NetCmd::PairCancel: gLink.cancelPair("display", gOut); return;
    case cal::NetCmd::PermReply:
      if (!gLink.reply(r.id, r.ok, gOut)) LogSerial.printf("[m5] perm-reply %s non mandata: connessione non autenticata\n", r.id);
      return;
    case cal::NetCmd::Unpair: gLink.unpair(gOut); return;
    default: break;
  }
  if (r.cmd == cal::NetCmd::Ack) {
    if (wsConn) sendAck(r.id, r.ok, r.error);
    else LogSerial.printf("[ws] ack %s perso: PC scollegato\n", r.id);
    return;
  }
  LogSerial.printf("[net] richiesta: %s %s\n", cal::netCmdKey(r.cmd), r.ssid);
  switch (r.cmd) {
    case cal::NetCmd::Scan: doScan(true); break;
    case cal::NetCmd::Provision: startProv(r.ssid); break;
    case cal::NetCmd::CancelProvision: stopProv(); break;
    case cal::NetCmd::Join: startJoin(r.ssid, "", true, true); break;
    case cal::NetCmd::ForgetNet: {
      const bool inUse = staUp && !strcmp(currentSsid(), r.ssid);
      const bool had = secrets::forgetNet(r.ssid, "Dimentica rete dal display");  // pulizia profonda e riavvio fra 3 s
      LogSerial.printf("[wifi] rete «%s» %s%s\n", r.ssid, had ? "dimenticata dal display" : "non trovata nel vault",
                       had && gNSaved == 1 ? " (era l'ultima: al riavvio parte la configurazione)" : inUse ? " (era in uso: al riavvio si collega a un'altra)" : "");
      loadSaved();
      if (had) pcMemForget(r.ssid);
      break;
    }
    case cal::NetCmd::Forget:
      secrets::forgetWifi();
      gPcMem.clear();
      pcMemSave("tutte le reti dimenticate: indirizzi dei PC tolti");
      haveSaved = false;
      gNSaved = 0;
      round_.active = false;
      roam.stop();
      listDirty = true;
      wifiTry.stop();
      mdnsTry.stop();
      wsTry.stop();
      join.active = false;
      wsDrop();
      wsBegun = false;
      pcKnown = false;
      staUp = false;
      WiFi.disconnect(false, true);
      LogSerial.println("[wifi] tutte le reti dimenticate (vault svuotato): pulizia profonda, al riavvio parte la configurazione");
      break;
    case cal::NetCmd::Reboot:
      rebootAt = millis() + 2500;  // il tempo di disegnare l'avvio
      LogSerial.println("[net] riavvio fra 2,5 s");
      break;
    default:
      break;
  }
}

// ---------- HTTP (porta 80) ----------
void sendAsset(const portal::Asset &a, bool cache) {
  if (a.gzip) server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", cache ? "public, max-age=31536000" : "no-store");
  server.send_P(200, a.type, reinterpret_cast<const char *>(a.data), a.len);
}

bool redirectCaptive() {
  // Captive portal: ogni host che non è la scheda va al portale (iOS, Android, Windows lo riconoscono).
  if (!prov) return false;
  const String h = server.hostHeader();
  if (h == kApIp.toString() || h.startsWith(gHost)) return false;
  server.sendHeader("Location", "http://192.168.4.1/setup", true);
  server.send(302, "text/plain", "");
  return true;
}

void httpPage() {
  if (redirectCaptive()) return;
  sendAsset(portal::kPageAsset, false);
}

void httpNets() {
  if (server.hasArg("rescan")) doScan(true);
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  xSemaphoreTake(gMx, portMAX_DELAY);
  // due passate: la rete scelta sul display (Cerca reti › rete protetta) va in cima
  for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < gNScan; k++) {
      const bool pre = provPreselect[0] && strcmp(gScan[k].ssid, provPreselect) == 0;
      if (pre != (pass == 0)) continue;
      JsonObject o = a.add<JsonObject>();
      o["s"] = gScan[k].ssid;
      o["r"] = gScan[k].rssi;
      o["l"] = gScan[k].lock ? 1 : 0;
    }
  xSemaphoreGive(gMx);
  String s;
  serializeJson(d, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void httpStatus() {
  JsonDocument d;
  static const char *const kSt[] = {"wait", "join", "ok", "fail"};
  d["mode"] = prov ? "ap" : "sta";
  d["st"] = kSt[int(portalSt)];
  d["ssid"] = staUp ? WiFi.SSID().c_str() : portalSsid;
  d["ip"] = staUp ? WiFi.localIP().toString() : String("");
  d["board"] = gHost;
  d["ap"] = gApSsid;
  d["fw"] = cal::kFwVersion;
  d["rssi"] = staUp ? WiFi.RSSI() : 0;
  d["pc"] = wsConn;
  d["pcHost"] = pcHostName;
  d["pre"] = prov ? provPreselect : "";  // rete scelta sul display: il portale apre subito la password
  // 0.8.0: reti salvate (solo SSID, ordine di inserimento) e quante al massimo: con la lista piena il portale chiede
  // quale togliere prima di aggiungerne una nuova.
  JsonArray sv = d["saved"].to<JsonArray>();
  for (int k = 0; k < gNSaved; k++) sv.add(gSaved[k].ssid);
  d["max"] = cal::kSavedMax;
  {  // 0.12.1: indirizzo manuale e ultimo PC noto della rete in uso
    const cal::PcMemEntry *e = staUp ? gPcMem.find(currentSsid()) : nullptr;
    char man[24] = "", last[24] = "";
    if (e) {
      cal::pcHostText(e->manual, man, sizeof man);
      cal::pcAddrText(e->last, last, sizeof last);
    }
    d["pcManual"] = man;
    d["pcLast"] = last;
  }
  xSemaphoreTake(gMx, portMAX_DELAY);
  d["sessions"] = gLastSessions;
  xSemaphoreGive(gMx);
  String s;
  serializeJson(d, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void httpSave(bool form) {
  const String ssid = server.arg("ssid"), pass = server.arg("pass"), drop = server.arg("drop");
  const bool ok = ssid.length() >= 1 && ssid.length() <= 32 && (pass.length() == 0 || (pass.length() >= 8 && pass.length() <= 63));
  if (!ok) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  // «Aggiungi rete» (0.8.0): una rete nuova con 5 reti già salvate vuole drop = una di quelle, che va via solo se la
  // nuova si collega. Senza: 409 e il portale chiede quale togliere.
  const bool full = savedIndex(ssid.c_str()) < 0 && gNSaved >= cal::kSavedMax;
  if (full && (!drop.length() || savedIndex(drop.c_str()) < 0)) {
    LogSerial.printf("[http] portale: «%s» non aggiunta, ci sono già %d reti: serve quale togliere\n", ssid.c_str(), gNSaved);
    if (form) server.send(409, "text/plain; charset=utf-8", "Ci sono già 5 reti salvate: togline una dal display (Menu > Wi-Fi > Reti salvate) e riprova.");
    else server.send(409, "application/json", "{\"ok\":false,\"full\":true}");
    return;
  }
  scopy(pendingJoin.ssid, ssid.c_str());
  scopy(pendingJoin.pass, pass.c_str());
  scopy(pendingJoin.drop, full ? drop.c_str() : "");
  pendingJoin.at = millis() + 400;
  portalSt = PortalSt::Join;
  scopy(portalSsid, pendingJoin.ssid);
  LogSerial.printf("[http] portale: rete «%s» scelta%s%s%s\n", pendingJoin.ssid, pendingJoin.drop[0] ? " (al posto di «" : "",
                   pendingJoin.drop, pendingJoin.drop[0] ? "»)" : "");
  if (form) {  // senza JavaScript
    server.sendHeader("Location", "/setup?s=join", true);
    server.send(303, "text/plain", "");
  } else {
    server.send(200, "application/json", "{\"ok\":true}");
  }
}

// 0.12.1: POST /api/pc, campo host: indirizzo manuale del PC per la rete in uso (vuoto = cancella).
void httpPcReply(int code, bool ok, const char *key, const char *val) {
  JsonDocument d;
  d["ok"] = ok;
  d[key] = val;
  String s;
  serializeJson(d, s);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", s);
}

void httpPc() {
  String h = server.arg("host");
  h.trim();
  const char *ssid = currentSsid();
  if (!staUp || !ssid[0]) { httpPcReply(409, false, "err", "il display non è collegato a una rete"); return; }
  cal::PcAddr a;
  memset(&a, 0, sizeof a);
  if (h.length() && !cal::parsePcHost(h.c_str(), kDefaultPath, a)) {
    LogSerial.println("[http] /api/pc: indirizzo non valido, rifiutato");
    httpPcReply(400, false, "err", "indirizzo non valido: scrivi un IPv4, per esempio 192.168.1.20 o 192.168.1.20:8766");
    return;
  }
  const bool changed = gPcMem.setManual(ssid, h.length() ? &a : nullptr);
  char text[24] = "";
  cal::pcHostText(a, text, sizeof text);
  if (changed) {
    char why[96];
    snprintf(why, sizeof why, "indirizzo manuale per «%s»: %s", ssid, text[0] ? text : "cancellato");
    if (!pcMemSave(why)) { httpPcReply(500, false, "err", "salvataggio non riuscito"); return; }
  }
  httpPcReply(200, true, "host", text);
  // PC non collegato: la ricerca riparte subito e prova anche il nuovo indirizzo
  if (changed && text[0] && !wsConn) {
    wsTry.stop();
    pcLostFlush();
    fbPrev = cal::PcSrc::None;
    fbDue = probeDue = probedOk = false;
    mdnsTry.restart(millis());
  }
}

void httpSetup() {
  server.on("/", HTTP_GET, httpPage);
  server.on("/setup", HTTP_GET, httpPage);
  for (const portal::Asset &f : portal::kFonts) server.on(f.path, HTTP_GET, [&f]() { sendAsset(f, true); });
  server.on("/api/nets", HTTP_GET, httpNets);
  server.on("/api/status", HTTP_GET, httpStatus);
  server.on("/api/save", HTTP_POST, []() { httpSave(false); });
  server.on("/save", HTTP_POST, []() { httpSave(true); });
  server.on("/api/pc", HTTP_POST, httpPc);
  // Hook diretti (M8): qui arriverà la macchina a stati ridotta. Regola §7.5: mai una decisione su un
  // PermissionRequest arrivato per questa via, sempre 200 con corpo vuoto.
  server.on("/hook", []() {
    LogSerial.println("[http] /hook ricevuto: gli hook diretti arrivano in M8 (risposta vuota)");
    server.send(200, "text/plain", "");
  });
  server.onNotFound([]() {
    if (redirectCaptive()) return;
    server.send(404, "text/plain", "Non trovato");
  });
  server.begin();
}

// ---------- task ----------
// Driver Wi-Fi senza NVS. Il core 2.0.17 (wifiLowLevelInit) chiama esp_wifi_init() con
// WIFI_INIT_CONFIG_DEFAULT(), cioè nvs_enable = 1 (CONFIG_ESP32_WIFI_NVS_ENABLED = 1 nell'SDK), e solo DOPO
// esp_wifi_set_storage(WIFI_STORAGE_RAM): nell'intervallo il driver usa «nvs.net80211». Qui si inizializza
// prima, con gli stessi buffer del core e nvs_enable = 0; esp_wifi_init() dell'IDF 4.4 è idempotente
// (s_wifi_inited), quindi la chiamata del core diventa un no-op. tcpipInit() del core prima, come fa lui:
// crea il loop degli eventi che esp_wifi_init() usa per i gestori predefiniti.

void wifiPreInit() {
  if (!::tcpipInit()) { LogSerial.println("[wifi] ERRORE: tcpipInit non riuscito, il core inizializzerà il driver"); return; }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (!WiFiGenericClass::useStaticBuffers()) {  // come wifiLowLevelInit()
    cfg.static_tx_buf_num = 0;
    cfg.dynamic_tx_buf_num = 32;
    cfg.tx_buf_type = 1;
    cfg.cache_tx_buf_num = 4;
    cfg.static_rx_buf_num = 4;
    cfg.dynamic_rx_buf_num = 32;
  }
  cfg.nvs_enable = 0;
  const esp_err_t e = esp_wifi_init(&cfg);
  if (e == ESP_OK) LogSerial.println("[wifi] driver avviato senza NVS (nvs_enable 0): nvs.net80211 non viene più scritta");
  else LogSerial.printf("[wifi] ERRORE: esp_wifi_init senza NVS (%d), il core inizializzerà il driver\n", int(e));
}

// 0.12.0, modalità gioco: tutto spento come prima del sonno profondo, poi buffer liberati e task chiuso (il suo stack
// da 12 KB torna allo heap). resume() rifà buffer e task. Mai durante un aggiornamento remoto.
bool allocBufs() {
  if (!gScratch) gScratch = static_cast<cal::StateMsg *>(malloc(sizeof(cal::StateMsg)));
  if (!gMailbox) gMailbox = static_cast<cal::StateMsg *>(malloc(sizeof(cal::StateMsg)));
  return gScratch && gMailbox;  // il buffer del registro si prende solo al log-start
}

void freeBufs() {
  xSemaphoreTake(gMx, portMAX_DELAY);
  gPending = false;
  free(gMailbox);
  gMailbox = nullptr;
  xSemaphoreGive(gMx);
  free(gScratch);
  gScratch = nullptr;
  logEnd();
}

void suspendNow() {
  uint8_t want = kWant;
  if (!gSusp.compare_exchange_strong(want, kSuspending)) return;  // annullata da suspend() (tempo scaduto)
  dropOffer("partita in corso: riprova a fine partita");  // un'offerta senza risposta non blocca la partita
  if (gUpd.active()) {
    LogSerial.println("[wifi] modalità gioco rifiutata: aggiornamento remoto in corso");
    gSusp = kRefused;
    return;
  }
  if (gThemeInst.busy()) gThemeInst.abort();
  gThemePending = false;  // 0.13.0: offerta del tema scartata, il servizio ripropone al ritorno
  if (gAvSt == kAvPending) gAvSt = kAvIdle;  // 0.14.0: idem per l'avatar (Adopting lo azzera resume)
  if (prov) stopProv();
  pendingJoin.at = 0;
  cal::cry::wipe(pendingJoin.pass, sizeof pendingJoin.pass);
  join.active = false;
  wipeJoinPass();
  round_.active = false;
  roam.stop();
  wifiTry.stop();
  mdnsTry.stop();
  wsTry.stop();
  wsDrop();
  if (mdnsUp) { MDNS.end(); mdnsUp = false; }
  if (sntpUp) { sntp_stop(); sntpUp = false; }
  server.stop();
  const bool wasUp = staUp;
  staUp = false;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);  // esp_wifi_stop() + esp_wifi_deinit(): buffer e task del Wi-Fi tornano allo heap
  if (wasUp) post(EvType::WifiLost);
  logEnd();
  gDoc.clear();
  gDoc.shrinkToFit();
  // Richieste del loop arrivate intanto: settings e status restano come ultima copia (si mandano dopo il prossimo
  // hello), le altre sono vecchie alla ripresa.
  cal::NetRequest q;
  while (xQueueReceive(gReq, &q, 0) == pdTRUE) {
    if (q.cmd == cal::NetCmd::Settings) {
      gSettings = q.values;
      haveSettings = true;
    } else if (q.cmd == cal::NetCmd::Status) {
      gPower = {q.battPct, q.usbOn, q.cal, q.hasWork, q.workOn, q.workEnd};
      havePower = true;
    } else if (q.cmd == cal::NetCmd::ThemeCur) {
      scopy(gThemeCur, q.id);  // 0.13.0: si dice dopo il prossimo auth-ok
    }
  }
  xQueueReset(gPerm);  // permessi non ancora presi: il servizio li rimanda dopo il nuovo collegamento
  freeBufs();
  gPs = -1;  // alla ripresa il risparmio voluto dal loop si riapplica
  LogSerial.println("[wifi] modalità gioco: Wi-Fi, WebSocket, mDNS e HTTP spenti, buffer liberati, task di rete chiuso");
  gSusp = kSuspended;
  vTaskDelete(nullptr);
}

void task(void *arg) {
  const bool resumed = arg != nullptr;  // 0.12.0: dopo la modalità gioco
  // Le credenziali le tiene solo il vault (cifrate dopo Proteggi le chiavi): con persistent(false) il core chiama
  // esp_wifi_set_storage(WIFI_STORAGE_RAM) e il driver non ne scrive copia in nvs.net80211 (M5 §4).
  WiFi.persistent(false);
  wifiPreInit();
  memLog(resumed ? "dopo wifiPreInit (ripresa)" : "dopo wifiPreInit", true);
  WiFi.setHostname(gHost);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // i tentativi li decide wifiTry (stessa sequenza di mDNS e WebSocket)
  if (!resumed) {
    httpSetup();
    ws.onEvent(onWs);
  } else {
    server.begin();
    LogSerial.println("[wifi] fine della modalità gioco: Wi-Fi di nuovo acceso, si ricollega");
  }
  if (haveSaved) wifiTry.restart(millis());  // tentativo 1 al primo giro del ciclo qui sotto
  else {
    LogSerial.println("[wifi] nessuna rete salvata: parte la configurazione dal telefono");
    startProv("");
  }
  for (;;) {
    if (gWantOff) {  // M6: sonno profondo. Il loop aspetta gOff prima di esp_deep_sleep_start().
      if (wsBegun) ws.disconnect();
      wsConn = false;
      if (mdnsUp) { MDNS.end(); mdnsUp = false; }
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_OFF);
      LogSerial.println("[wifi] spento per il sonno profondo");
      gOff = true;
      for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (gSusp.load() == kWant) suspendNow();  // 0.12.0: modalità gioco (non torna, salvo rifiuto)
    if (gWantPs >= 0 && gWantPs != gPs) {  // M6: modem sleep secondo lo stato di alimentazione
      gPs = gWantPs;
      const esp_err_t e = esp_wifi_set_ps(static_cast<wifi_ps_type_t>(gPs));
      static const char *const kPs[] = {"nessun risparmio", "modem sleep minimo (ogni DTIM)", "modem sleep massimo (ogni 3 beacon)"};
      LogSerial.printf("[wifi] risparmio: %s%s\n", kPs[gPs < 3 ? gPs : 0], e == ESP_OK ? "" : " · ERRORE esp_wifi_set_ps");
    }
    cal::NetRequest r;
    while (xQueueReceive(gReq, &r, 0) == pdTRUE) handle(r);
    if (pendingJoin.at && millis() >= pendingJoin.at) {
      pendingJoin.at = 0;
      startJoin(pendingJoin.ssid, pendingJoin.pass, true, true, pendingJoin.drop);
      cal::cry::wipe(pendingJoin.pass, sizeof pendingJoin.pass);
    }
    checkWifi();
    if (prov) dns.processNextRequest();
    server.handleClient();
    if (staUp) pcLoop();
    otaLoop();
    themeLoop();
    avatarLoop();  // 0.14.0 (R4)
    if (prov && provStopAt && millis() >= provStopAt) stopProv();
    if (rebootAt && millis() >= rebootAt) ESP.restart();
    allocReport();
    if (millis() - lastHeapLog >= kHeapLogMs) {
      lastHeapLog = millis();
      LogSerial.printf("[memoria] heap libero %u (minimo %u), stack rete libero %u byte\n", unsigned(ESP.getFreeHeap()),
                    unsigned(ESP.getMinFreeHeap()), unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    }
    vTaskDelay(pdMS_TO_TICKS(prov ? 2 : 5));
  }
}

}  // namespace

void begin(const char *boardId) {
  scopy(gBoard, boardId);
  cal::mdnsHost(gBoard, gHost, sizeof gHost);
  cal::apSsid(gBoard, gApSsid, sizeof gApSsid);
  gReq = xQueueCreate(12, sizeof(cal::NetRequest));  // un set ne produce due (settings, ack)
  gEv = xQueueCreate(24, sizeof(Event));
  gPerm = xQueueCreate(cal::kPermCap, sizeof(cal::PermReq));
  gAv = xQueueCreate(4, sizeof(cal::AvatarMsg *));
  gAvFile = xQueueCreate(1, sizeof(::Avatar));  // 0.14.0 (R4)
  gGame = xQueueCreate(6, sizeof(cal::GameMsg *));        // 0.10.0: un traguardo non si perde dietro un game seguente
  gAcc = xQueueCreate(2, sizeof(cal::AccessoryMsg *));
  gAnc = xQueueCreate(4, sizeof(cal::AnchorsMsg *));
  gMx = xSemaphoreCreateMutex();
  heap_caps_register_failed_alloc_callback(onAllocFailed);  // diagnostica: contatori, riassunto dal task (allocReport)
  if (!allocBufs()) LogSerial.println("[net] ERRORE: memoria per state non disponibile");
  memLog("dopo allocBufs");
  // Rete e abbinamento dal vault (hw/secrets, già avviato): illeggibili = assenti (backup di un'altra scheda).
  loadSaved();  // reti salvate dal vault (solo SSID e ultimo uso)
  pcMemLoad();  // 0.12.1: indirizzi dei PC per rete
  gLink.begin(gHost, linkRand, nullptr);
  uint8_t K[32];
  char host[48];
  const bool paired = secrets::loadPair(K, host, sizeof host);
  gLink.setKey(paired ? K : nullptr);
  cal::cry::wipe(K, sizeof K);
  gOtaPubOk = secrets::loadOtaKey(gOtaPub);
  gBootCase = otahw::boot(gBootRec, gBootHasRec);
  LogSerial.printf("[net] scheda %s · reti salvate: %d%s%s · abbinamento: %s%s\n", gHost, gNSaved, gNSaved ? ", prima " : "",
                   gNSaved ? gSaved[0].ssid : "",
                paired ? "sì, con " : "no", paired ? (host[0] ? host : "PC") : "");
  // core 0 (il loop Arduino è sul core 1); 12 KB di stack: JSON, mDNS, HTTP e WebSocket nello stesso task
  xTaskCreatePinnedToCore(task, "calamaio-net", 12288, nullptr, 2, nullptr, 0);
  memLog("dopo l'avvio del task di rete");
}

void request(const cal::NetRequest &r) {
  if (gSusp.load() == kSuspended) {  // modalità gioco: si tiene solo l'ultima copia di settings e status
    if (r.cmd == cal::NetCmd::Settings) {
      gSettings = r.values;
      haveSettings = true;
    } else if (r.cmd == cal::NetCmd::Status) {
      gPower = {r.battPct, r.usbOn, r.cal, r.hasWork, r.workOn, r.workEnd};
      havePower = true;
    } else if (r.cmd == cal::NetCmd::ThemeCur) {
      scopy(gThemeCur, r.id);  // 0.13.0: l'ultimo tema attivo si dice dopo il prossimo auth-ok
    }
    return;
  }
  if (gReq && xQueueSend(gReq, &r, 0) != pdTRUE) {
    LogSerial.println("[net] coda richieste piena");
    // 0.14.0 (R4): l'avatar-ack perso non lascia il task in Adopting (il loop ha finito col temporaneo): il servizio
    // conta la consegna come fallita dopo 30 s e la ripropone.
    if (r.cmd == cal::NetCmd::AvatarAck) gAvSt = kAvIdle;
  }
}

bool pollEvent(Event &e) { return gEv && xQueueReceive(gEv, &e, 0) == pdTRUE; }

bool withState(void (*fn)(const cal::StateMsg &msg, void *ctx), void *ctx) {
  if (!gMx) return false;
  xSemaphoreTake(gMx, portMAX_DELAY);
  const bool had = gPending && gMailbox;
  if (had) {
    gPending = false;
    fn(*gMailbox, ctx);
  }
  xSemaphoreGive(gMx);
  return had;
}

int takeScan(cal::NetEntry *out, int cap) {
  xSemaphoreTake(gMx, portMAX_DELAY);
  const int n = gNScan < cap ? gNScan : cap;
  memcpy(out, gScan, sizeof(cal::NetEntry) * n);
  xSemaphoreGive(gMx);
  return n;
}

bool takePerm(cal::PermReq &out) { return gPerm && xQueueReceive(gPerm, &out, 0) == pdTRUE; }

bool takeAvatar(cal::AvatarMsg *&out) { return gAv && xQueueReceive(gAv, &out, 0) == pdTRUE; }
bool takeAvatarFile(::Avatar &out) { return gAvFile && xQueueReceive(gAvFile, &out, 0) == pdTRUE; }
bool takeGame(cal::GameMsg *&out) { return gGame && xQueueReceive(gGame, &out, 0) == pdTRUE; }
bool takeAccessory(cal::AccessoryMsg *&out) { return gAcc && xQueueReceive(gAcc, &out, 0) == pdTRUE; }
bool takeAnchors(cal::AnchorsMsg *&out) { return gAnc && xQueueReceive(gAnc, &out, 0) == pdTRUE; }

bool hasSavedNetwork() { return haveSaved; }

void setPowerSave(PowerSave ps) { gWantPs = static_cast<int8_t>(ps); }
int8_t appliedPowerSave() { return gPs; }

bool shutdown(uint32_t timeoutMs) {
  if (!gReq || gSusp.load() == kSuspended) return true;  // task mai avviato, o Wi-Fi già spento (modalità gioco)
  gWantOff = true;
  const uint32_t t0 = millis();
  while (!gOff && millis() - t0 < timeoutMs) delay(10);
  return gOff;
}
const char *savedNetwork() { return haveSaved ? gSaved[0].ssid : ""; }

int takeSaved(cal::SavedNet *out, int cap) {
  if (!gMx) return 0;
  xSemaphoreTake(gMx, portMAX_DELAY);
  const int n = gNSavedOut < cap ? gNSavedOut : cap;
  memcpy(out, gSavedOut, sizeof(cal::SavedNet) * n);
  xSemaphoreGive(gMx);
  return n;
}

void setRoamGuard(bool permOpen, bool calibrating) {
  gRoamPerm = permOpen;
  gRoamCal = calibrating;
}

uint32_t otaDone() { return gOtaDone.load(); }

bool suspend(uint32_t timeoutMs) {
  if (!gReq) return true;
  uint8_t st = kRunning;
  if (!gSusp.compare_exchange_strong(st, kWant)) return st == kSuspended;
  const uint32_t t0 = millis();
  for (;;) {
    st = gSusp.load();
    if (st == kSuspended) return true;
    if (st == kRefused) {
      gSusp = kRunning;
      return false;
    }
    if (millis() - t0 >= timeoutMs) {
      uint8_t want = kWant;
      if (gSusp.compare_exchange_strong(want, kRunning)) {  // il task non l'ha ancora presa: si annulla
        LogSerial.printf("[net] modalità gioco: il task di rete non ha risposto in %lu ms\n", (unsigned long)timeoutMs);
        return false;
      }
      if (millis() - t0 >= timeoutMs + 10000) {  // presa ma non finita (Wi-Fi bloccato): il chiamante riprova resume()
        LogSerial.println("[net] modalità gioco: lo spegnimento della rete non finisce, partita non avviata");
        return false;
      }
    }
    delay(10);
  }
}

bool resume() {
  const uint8_t st = gSusp.load();
  if (st == kWant || st == kSuspending) return false;  // spegnimento ancora in corso: riprovare
  if (st != kSuspended) return true;
  if (!allocBufs()) {
    LogSerial.printf("[net] fine della modalità gioco: memoria insufficiente (heap libero %u), riprovo\n", unsigned(ESP.getFreeHeap()));
    return false;
  }
  // 0.14.0 (R4): un avatar scaricato prima della partita e non ancora adottato si richiede; il task non c'è, il loop
  // è qui: nessuno tocca il temporaneo nello stesso momento.
  if (gAvFile) xQueueReset(gAvFile);
  avfs::storage().tempAbort();
  gAvSt = kAvIdle;
  gSusp = kRunning;
  if (xTaskCreatePinnedToCore(task, "calamaio-net", 12288, reinterpret_cast<void *>(1), 2, nullptr, 0) != pdPASS) {
    gSusp = kSuspended;
    LogSerial.println("[net] fine della modalità gioco: task di rete non creato, riprovo");
    return false;
  }
  return true;
}

bool suspended() { return gSusp.load() == kSuspended; }

}  // namespace net
