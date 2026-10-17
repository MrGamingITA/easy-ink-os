// SPIKE usa e getta · blepad: controller Xbox Series X|S (1914) via BLE + Wi-Fi + ping TCP sulla Wireless Paper.
// Non tocca il pannello né altri pin: solo radio e seriale (115200). Comandi seriali: «help».
// Libreria: NimBLE-Arduino 2.5.1 (ruolo centrale, HID-over-GATT scritto qui). Vedi README.md.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

#include "menu_map.h"
#include "stats.h"
#include "xbox_report.h"

namespace {

// ---------- costanti ----------
const uint16_t kAppearanceGamepad = 0x03C4;  // 964
const NimBLEUUID kHidService("1812"), kReport("2a4d"), kReportMap("2a4b"), kReportRef("2908");
const NimBLEUUID kBatService("180f"), kBatLevel("2a19");
const uint32_t kScanMs = 5000;
const uint16_t kStickThreshold = 655;  // ~1% della corsa: sotto non si stampa
const uint16_t kTrigThreshold = 8;
const uint32_t kAxisPrintMs = 50;      // righe dovute solo agli assi: al massimo 20 al secondo
const uint32_t kLatWindowMs = 10000, kHeapMs = 5000, kPingMs = 100, kPingReportMs = 5000;

// ---------- dati dalle callback di NimBLE (altro task) ----------
struct Rx {
  uint32_t t;  // micros() all'arrivo della notifica
  uint16_t handle;
  uint8_t len;
  uint8_t d[24];
};
QueueHandle_t gRx = nullptr;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
pad::Window gNotifyGap;            // intervallo fra notifiche HID (µs), protetto da gMux
uint32_t gLastNotifyUs = 0;        // protetto da gMux
volatile uint32_t gQueueDropped = 0;
volatile uint16_t gBatHandle = 0;

volatile bool gFound = false;      // annuncio giusto trovato durante la scansione
NimBLEAddress gFoundAddr;          // protetto da gMux
volatile bool gFoundPairing = false;
volatile bool gDisconnected = false;
volatile int gDisconnectReason = 0;

// ---------- stato del loop ----------
bool gPairWanted = false;          // accetta un controller nuovo (nessun abbinamento o comando «pair»)
NimBLEClient *gClient = nullptr;
bool gPadUp = false;
uint32_t gConnectFails = 0, gConnects = 0;
uint16_t gItvMin = 6, gItvMax = 12;  // intervallo di connessione richiesto (unità 1,25 ms): 7,5–15 ms
bool gRaw = false;
int gShortReports = 0;

pad::State gState, gPrinted;
bool gAxisPending = false;
uint32_t gAxisPrintedAt = 0;
pad::MenuMapper gMenu;
pad::Window gQueueDelay;           // dall'arrivo della notifica alla stampa (µs)
uint32_t gNotifies = 0;

// Wi-Fi e ping
String gSsid, gPass, gHost;
uint16_t gPort = 0;
bool gWifiWanted = false, gPingWanted = false;
wl_status_t gWifiLast = WL_IDLE_STATUS;
bool gHeapAfterWifi = false;
WiFiClient gTcp;
bool gTcpUp = false;
uint32_t gTcpTryAt = 0, gTcpReconnects = 0, gPingAt = 0, gPingReportAt = 0;
pad::PingTracker gPing(1000000);
pad::Window gRtt;
uint32_t gPingSent0 = 0, gPingRecv0 = 0, gPingLost0 = 0;
char gTcpLine[48];
size_t gTcpLineLen = 0;

uint32_t gLatAt = 0, gHeapAt = 0;
String gCmd;

// ---------- utilità ----------
void heapLine(const char *when) {
  Serial.printf("[heap] %s: libera %u, minima %u, blocco max %u, interna libera %u\n", when, ESP.getFreeHeap(),
                ESP.getMinFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

String hexOf(const uint8_t *d, size_t n) {
  String s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof b, "%02x", d[i]);
    s += b;
  }
  return s;
}

const char *masked(const String &s) {  // mai il valore: solo la lunghezza
  static char b[40];
  snprintf(b, sizeof b, "%s (%u caratteri)", s.length() ? "********" : "(vuoto)", unsigned(s.length()));
  return b;
}

// ---------- BLE: callback ----------
class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    const NimBLEAddress &a = dev->getAddress();
    const bool bonded = NimBLEDevice::isBonded(a);
    const bool gamepad = dev->haveAppearance() && dev->getAppearance() == kAppearanceGamepad;
    const bool hid = dev->isAdvertisingService(kHidService);
    const std::string md = dev->getManufacturerData();
    // Dati del costruttore Microsoft (0x0006): «06 00 03 00 80» = in associazione, «06 00 00» = normale
    const bool msft = md.size() >= 2 && uint8_t(md[0]) == 0x06 && uint8_t(md[1]) == 0x00;
    const bool pairing = msft && md.size() >= 5 && uint8_t(md[2]) == 0x03;
    if (!bonded && !(gamepad || msft)) return;  // non è un controller: zitti
    static uint32_t lastLog = 0;
    if (millis() - lastLog > 1000) {  // un annuncio al secondo al massimo nel log
      lastLog = millis();
      Serial.printf("[ble] visto %s tipo %u «%s» rssi %d aspetto %s hid %d costruttore %s%s%s\n", a.toString().c_str(),
                    a.getType(), dev->getName().c_str(), dev->getRSSI(), gamepad ? "gamepad" : "-", hid,
                    hexOf(reinterpret_cast<const uint8_t *>(md.data()), md.size()).c_str(),
                    pairing ? " · IN ASSOCIAZIONE" : "", bonded ? " · GIA' ABBINATO" : "");
    }
    const bool take = bonded || (gPairWanted && gamepad && hid && dev->isConnectable());
    if (!take || gFound) return;
    portENTER_CRITICAL(&gMux);
    gFoundAddr = a;
    portEXIT_CRITICAL(&gMux);
    gFoundPairing = pairing;
    gFound = true;
    NimBLEDevice::getScan()->stop();
  }
} gScanCb;

class ClientCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    gDisconnectReason = reason;
    gDisconnected = true;
  }
  bool onConnParamsUpdateRequest(NimBLEClient *, const ble_gap_upd_params *p) override {
    // Il controller chiede i suoi parametri: si accettano, ma si stampano
    Serial.printf("[ble] il controller chiede intervallo %.2f-%.2f ms, latenza %u, timeout %u ms: accettato\n",
                  p->itvl_min * 1.25f, p->itvl_max * 1.25f, p->latency, p->supervision_timeout * 10);
    return true;
  }
  void onAuthenticationComplete(NimBLEConnInfo &ci) override {
    Serial.printf("[ble] sicurezza: cifrata %d, abbinata %d, autenticata %d, chiave %u byte\n", ci.isEncrypted(),
                  ci.isBonded(), ci.isAuthenticated(), ci.getSecKeySize());
  }
  void onConfirmPasskey(NimBLEConnInfo &ci, uint32_t) override { NimBLEDevice::injectConfirmPasskey(ci, true); }
  void onPassKeyEntry(NimBLEConnInfo &ci) override { NimBLEDevice::injectPassKey(ci, 0); }
} gClientCb;

void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool) {
  Rx r;
  r.t = micros();
  r.handle = c->getHandle();
  r.len = n > sizeof r.d ? sizeof r.d : n;
  memcpy(r.d, d, r.len);
  if (r.handle != gBatHandle) {
    portENTER_CRITICAL(&gMux);
    if (gLastNotifyUs) gNotifyGap.add(r.t - gLastNotifyUs);
    gLastNotifyUs = r.t;
    portEXIT_CRITICAL(&gMux);
  }
  if (xQueueSend(gRx, &r, 0) != pdTRUE) gQueueDropped++;
}

// ---------- BLE: connessione ----------
void printConnInfo(const char *when) {
  if (!gClient || !gClient->isConnected()) return;
  const NimBLEConnInfo ci = gClient->getConnInfo();
  Serial.printf("[ble] %s: intervallo %.2f ms, latenza %u, timeout %u ms, MTU %u, rssi %d\n", when,
                ci.getConnInterval() * 1.25f, ci.getConnLatency(), ci.getConnTimeout() * 10, ci.getMTU(),
                gClient->getRssi());
}

bool setupGatt() {
  NimBLERemoteService *hid = gClient->getService(kHidService);
  if (!hid) {
    Serial.println("[ble] servizio HID 0x1812 assente");
    return false;
  }
  int subscribed = 0;
  for (NimBLERemoteCharacteristic *c : hid->getCharacteristics(true)) {
    if (c->getUUID() == kReportMap) {
      const NimBLEAttValue v = c->readValue();
      Serial.printf("[ble] report map: %u byte\n", unsigned(v.size()));
      for (size_t i = 0; i < v.size(); i += 32)  // a righe, per confrontare il layout col decodificatore
        Serial.printf("[ble] report map %03u: %s\n", unsigned(i),
                      hexOf(v.data() + i, v.size() - i < 32 ? v.size() - i : 32).c_str());
      continue;
    }
    if (c->getUUID() != kReport) continue;
    uint8_t id = 0, type = 0;
    if (NimBLERemoteDescriptor *rr = c->getDescriptor(kReportRef)) {
      const NimBLEAttValue v = rr->readValue();
      if (v.size() >= 2) id = v[0], type = v[1];
    }
    const char *kind = type == 1 ? "ingresso" : type == 2 ? "uscita" : type == 3 ? "feature" : "?";
    bool ok = false;
    if (type == 1 && c->canNotify()) ok = c->subscribe(true, onNotify, true);
    Serial.printf("[ble] report handle %u: id %u %s%s\n", c->getHandle(), id, kind,
                  type == 1 ? (ok ? " · iscritto" : " · ISCRIZIONE FALLITA") : "");
    subscribed += ok;
  }
  if (NimBLERemoteService *bat = gClient->getService(kBatService)) {
    if (NimBLERemoteCharacteristic *c = bat->getCharacteristic(kBatLevel)) {
      gBatHandle = c->getHandle();
      const NimBLEAttValue v = c->readValue();
      if (v.size()) Serial.printf("[pad] batteria %u%%\n", v[0]);
      if (c->canNotify()) c->subscribe(true, onNotify, true);
    }
  }
  return subscribed > 0;
}

void connectFound() {
  NimBLEAddress a;
  portENTER_CRITICAL(&gMux);
  a = gFoundAddr;
  portEXIT_CRITICAL(&gMux);
  const bool wasBonded = NimBLEDevice::isBonded(a);
  Serial.printf("[ble] collego %s (%s)\n", a.toString().c_str(),
                wasBonded ? "abbinato: ricollegamento" : gFoundPairing ? "nuovo, in associazione" : "nuovo");
  const uint32_t t0 = millis();
  if (!gClient) {
    gClient = NimBLEDevice::createClient();
    gClient->setClientCallbacks(&gClientCb, false);
    gClient->setConnectTimeout(5000);
  }
  // Valori iniziali: intervallo corto (latenza), timeout 3 s
  gClient->setConnectionParams(gItvMin, gItvMax, 0, 300);
  if (!gClient->connect(a, true)) {
    gConnectFails++;
    Serial.printf("[ble] collegamento NON riuscito (%u fallimenti)\n", gConnectFails);
    return;
  }
  const uint32_t t1 = millis();
  printConnInfo("collegato");
  // L'Xbox vuole la cifratura per leggere i report: con abbinamento si cifra con le chiavi salvate,
  // altrimenti si abbina adesso (Just Works) e le chiavi vanno nella NVS («nimble_bond»).
  if (!gClient->secureConnection()) {
    Serial.println("[ble] cifratura NON riuscita");
    if (wasBonded) {
      // Il controller ha perso l'abbinamento (associato altrove?): si dimentica e si torna in associazione
      Serial.println("[ble] abbinamento non più valido: cancellato, rimetti il controller in associazione");
      NimBLEDevice::deleteBond(a);
      gPairWanted = true;
    }
    gClient->disconnect();
    gConnectFails++;
    return;
  }
  const uint32_t t2 = millis();
  if (!setupGatt()) {
    Serial.println("[ble] nessun report d'ingresso: scollego");
    gClient->disconnect();
    gConnectFails++;
    return;
  }
  gClient->updateConnParams(gItvMin, gItvMax, 0, 300);
  gConnects++;
  gPadUp = true;
  gPairWanted = false;
  gShortReports = 0;
  portENTER_CRITICAL(&gMux);
  gNotifyGap.clear();
  gLastNotifyUs = 0;
  portEXIT_CRITICAL(&gMux);
  Serial.printf("[ble] PRONTO: collegamento %u ms, cifratura %u ms, servizi %u ms · abbinati in NVS: %d\n",
                t1 - t0, t2 - t1, unsigned(millis() - t2), NimBLEDevice::getNumBonds());
  heapLine("controller pronto");
}

void bleLoop() {
  if (gDisconnected) {
    gDisconnected = false;
    if (gPadUp) Serial.printf("[ble] controller SCOLLEGATO, motivo 0x%x\n", gDisconnectReason);
    gPadUp = false;
    gBatHandle = 0;
    pad::MenuEvent ev[4];
    const int n = gMenu.reset(ev, 4);
    for (int i = 0; i < n; i++) Serial.printf("[menu] %s\n", pad::menuEventName(ev[i]));
    gState = pad::State();
  }
  if (gPadUp && gClient && gClient->isConnected()) return;
  gPadUp = false;
  if (gFound) {
    connectFound();
    gFound = false;
    return;
  }
  NimBLEScan *s = NimBLEDevice::getScan();
  if (!s->isScanning()) s->start(kScanMs, false, true);
}

// ---------- report HID → stampa ----------
void printState(uint32_t t) {
  char line[96];
  pad::format(gState, line, sizeof line);
  Serial.printf("[pad] t=%lu %s\n", (unsigned long)t, line);
  gPrinted = gState;
  gAxisPending = false;
  gAxisPrintedAt = millis();
}

void handleRx(const Rx &r) {
  if (r.handle == gBatHandle) {
    if (r.len) Serial.printf("[pad] batteria %u%%\n", r.d[0]);
    return;
  }
  gNotifies++;
  if (gRaw) Serial.printf("[raw] t=%lu h=%u n=%u %s\n", (unsigned long)r.t, r.handle, r.len, hexOf(r.d, r.len).c_str());
  pad::State s;
  if (!pad::parse(r.d, r.len, &s)) {
    if (gShortReports++ < 5)
      Serial.printf("[pad] report di %u byte su handle %u (atteso >= 16): %s\n", r.len, r.handle,
                    hexOf(r.d, r.len).c_str());
    return;
  }
  gState = s;
  gQueueDelay.add(micros() - r.t);
  pad::MenuEvent ev[8];
  const int n = gMenu.update(s, millis(), ev, 8);
  if (pad::buttonsDiffer(s, gPrinted)) printState(r.t);
  else if (pad::differs(s, gPrinted, kStickThreshold, kTrigThreshold)) gAxisPending = true;
  for (int i = 0; i < n; i++) Serial.printf("[menu] t=%lu %s\n", (unsigned long)r.t, pad::menuEventName(ev[i]));
}

void padLoop() {
  Rx r;
  while (xQueueReceive(gRx, &r, 0) == pdTRUE) handleRx(r);
  if (gAxisPending && millis() - gAxisPrintedAt >= kAxisPrintMs) printState(micros());
  pad::MenuEvent ev[4];
  const int n = gMenu.tick(millis(), ev, 4);  // il controller manda report solo ai cambiamenti
  for (int i = 0; i < n; i++) Serial.printf("[menu] t=%lu %s\n", (unsigned long)micros(), pad::menuEventName(ev[i]));
}

void latencyReport() {
  pad::Window g;
  portENTER_CRITICAL(&gMux);
  g = gNotifyGap;
  gNotifyGap.clear();
  portEXIT_CRITICAL(&gMux);
  Serial.printf("[lat] 10 s: %u notifiche (%.1f/s), intervallo min/media/max %.1f/%.1f/%.1f ms, oltre 20 ms %u · "
                "coda->stampa media/max %.2f/%.2f ms · perse in coda %u\n",
                gNotifies, gNotifies / 10.0f, g.min / 1000.0f, g.avg() / 1000.0f, g.max / 1000.0f, g.over,
                gQueueDelay.avg() / 1000.0f, gQueueDelay.max / 1000.0f, unsigned(gQueueDropped));
  gNotifies = 0;
  gQueueDelay.clear();
  if (gPadUp) printConnInfo("connessione");
}

// ---------- Wi-Fi e ping TCP ----------
void wifiLoop() {
  const wl_status_t st = WiFi.status();
  if (st != gWifiLast) {
    gWifiLast = st;
    if (st == WL_CONNECTED) {
      Serial.printf("[wifi] collegato, ip %s, rssi %d, canale %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                    WiFi.channel());
      if (!gHeapAfterWifi) {
        gHeapAfterWifi = true;
        heapLine("dopo il Wi-Fi");
      }
      wifi_ps_type_t ps;
      if (esp_wifi_get_ps(&ps) == ESP_OK) Serial.printf("[wifi] risparmio attuale: %d (0 nessuno, 1 min, 2 max)\n", ps);
    } else {
      Serial.printf("[wifi] stato %d\n", st);
    }
  }
  if (!gPingWanted || st != WL_CONNECTED) {
    if (gTcpUp) {
      gTcp.stop();
      gTcpUp = false;
      gPing.dropAll();
    }
    return;
  }
  const uint32_t now = millis();
  if (!gTcpUp) {
    if (now - gTcpTryAt < 2000) return;
    gTcpTryAt = now;
    if (gTcp.connect(gHost.c_str(), gPort, 1000)) {  // bloccante al massimo 1 s
      gTcp.setNoDelay(true);
      gTcpUp = true;
      gTcpReconnects++;
      gTcpLineLen = 0;
      Serial.printf("[tcp] collegato a %s:%u (collegamenti %u)\n", gHost.c_str(), gPort, gTcpReconnects);
    } else {
      Serial.printf("[tcp] collegamento a %s:%u non riuscito\n", gHost.c_str(), gPort);
    }
    return;
  }
  if (!gTcp.connected()) {
    Serial.println("[tcp] connessione CADUTA");
    gTcp.stop();
    gTcpUp = false;
    gPing.dropAll();
    return;
  }
  if (now - gPingAt >= kPingMs) {
    gPingAt = now;
    const uint32_t seq = gPing.send(micros());
    char b[24];
    const int n = snprintf(b, sizeof b, "P %lu\n", (unsigned long)seq);
    gTcp.write(reinterpret_cast<const uint8_t *>(b), n);
  }
  while (gTcp.available() > 0) {
    const int c = gTcp.read();
    if (c < 0) break;
    if (c == '\n') {
      gTcpLine[gTcpLineLen] = 0;
      uint32_t rtt;
      if (gTcpLine[0] == 'P' && gPing.reply(strtoul(gTcpLine + 1, nullptr, 10), micros(), &rtt)) gRtt.add(rtt);
      gTcpLineLen = 0;
    } else if (gTcpLineLen < sizeof gTcpLine - 1) {
      gTcpLine[gTcpLineLen++] = char(c);
    }
  }
  gPing.expire(micros());
}

void pingReport() {
  if (!gPingWanted) return;
  const uint32_t s = gPing.sent - gPingSent0, r = gPing.received - gPingRecv0, l = gPing.lost - gPingLost0;
  Serial.printf("[tcp] 5 s: inviati %u, risposti %u, persi %u · RTT min/media/max %.1f/%.1f/%.1f ms, oltre 100 ms %u · "
                "totale persi %u su %u (%.2f%%) · collegamenti %u · controller %s\n",
                s, r, l, gRtt.min / 1000.0f, gRtt.avg() / 1000.0f, gRtt.max / 1000.0f, gRtt.over, gPing.lost, gPing.sent,
                gPing.sent ? 100.0f * gPing.lost / gPing.sent : 0.0f, gTcpReconnects, gPadUp ? "collegato" : "no");
  gPingSent0 = gPing.sent;
  gPingRecv0 = gPing.received;
  gPingLost0 = gPing.lost;
  gRtt.clear();
}

// ---------- comandi seriali ----------
void help() {
  Serial.println(
      "[cmd] comandi: ssid <nome> · pass <password> · wifi on|off · host <ip>:<porta> · ping off · "
      "ps nessuno|min|max · pair · forget · raw on|off · itv <min> <max> (unita' 1,25 ms) · info · help");
}

void command(String c) {
  c.trim();
  if (!c.length()) return;
  const int sp = c.indexOf(' ');
  String k = sp < 0 ? c : c.substring(0, sp), v = sp < 0 ? "" : c.substring(sp + 1);
  v.trim();
  if (k == "ssid") {
    gSsid = v;
    Serial.printf("[cmd] ssid %s\n", masked(gSsid));
  } else if (k == "pass") {
    gPass = v;
    Serial.printf("[cmd] password %s\n", masked(gPass));
  } else if (k == "wifi" && v == "on") {
    if (!gSsid.length()) return (void)Serial.println("[cmd] prima: ssid <nome>");
    gWifiWanted = true;
    WiFi.persistent(false);  // credenziali solo in RAM: niente copia in «nvs.net80211»
    WiFi.mode(WIFI_STA);
    WiFi.begin(gSsid.c_str(), gPass.c_str());
    Serial.printf("[wifi] collego a ssid %s\n", masked(gSsid));
  } else if (k == "wifi" && v == "off") {
    gWifiWanted = false;
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    Serial.println("[wifi] spento");
  } else if (k == "host") {
    const int col = v.lastIndexOf(':');
    if (col <= 0) return (void)Serial.println("[cmd] uso: host 192.168.1.10:7777");
    gHost = v.substring(0, col);
    gPort = uint16_t(v.substring(col + 1).toInt());
    gPingWanted = true;
    gTcpTryAt = millis() - 2000;
    gPingReportAt = millis();
    Serial.printf("[tcp] ping verso %s:%u ogni %u ms\n", gHost.c_str(), gPort, kPingMs);
  } else if (k == "ping" && v == "off") {
    gPingWanted = false;
    Serial.println("[tcp] ping fermo");
  } else if (k == "ps") {
    const wifi_ps_type_t ps = v == "nessuno" ? WIFI_PS_NONE : v == "max" ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM;
    const esp_err_t e = esp_wifi_set_ps(ps);
    Serial.printf("[wifi] risparmio %d: %s\n", ps, esp_err_to_name(e));
  } else if (k == "pair") {
    gPairWanted = true;
    if (gClient && gClient->isConnected()) gClient->disconnect();
    Serial.println("[ble] cerco un controller nuovo: tieni premuto il tasto di associazione finche' il logo lampeggia veloce");
  } else if (k == "forget") {
    if (gClient && gClient->isConnected()) gClient->disconnect();
    const bool ok = NimBLEDevice::deleteAllBonds();
    gPairWanted = true;
    Serial.printf("[ble] abbinamenti cancellati: %s (restano %d)\n", ok ? "ok" : "ERRORE", NimBLEDevice::getNumBonds());
  } else if (k == "raw") {
    gRaw = v == "on";
    Serial.printf("[cmd] report grezzi %s\n", gRaw ? "accesi" : "spenti");
  } else if (k == "itv") {
    const int sp2 = v.indexOf(' ');
    const long a = v.toInt(), b = sp2 < 0 ? a : v.substring(sp2 + 1).toInt();
    if (a < 6 || b < a || b > 3200) return (void)Serial.println("[cmd] uso: itv 6 12 (6 = 7,5 ms)");
    gItvMin = uint16_t(a);
    gItvMax = uint16_t(b);
    Serial.printf("[ble] intervallo richiesto %.2f-%.2f ms\n", gItvMin * 1.25f, gItvMax * 1.25f);
    if (gPadUp) Serial.printf("[ble] richiesta al controller: %s\n",
                              gClient->updateConnParams(gItvMin, gItvMax, 0, 300) ? "inviata" : "ERRORE");
  } else if (k == "info") {
    Serial.printf("[info] abbinati %d", NimBLEDevice::getNumBonds());
    for (int i = 0; i < NimBLEDevice::getNumBonds(); i++)
      Serial.printf(" · %s", NimBLEDevice::getBondedAddress(i).toString().c_str());
    Serial.printf(" · controller %s · collegamenti %u, falliti %u · wifi %d · ssid %s\n", gPadUp ? "collegato" : "no",
                  gConnects, gConnectFails, WiFi.status(), masked(gSsid));
    printConnInfo("connessione");
    heapLine("ora");
  } else {
    help();
  }
}

void serialLoop() {
  while (Serial.available()) {
    const char ch = char(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      command(gCmd);
      gCmd = "";
    } else if (gCmd.length() < 128) {
      gCmd += ch;
    }
  }
}

}  // namespace

void setup() {
  Serial.setTxBufferSize(8192);  // le stampe non devono fermare il loop
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[blepad] SPIKE controller Xbox BLE · NimBLE-Arduino 2.5.1");
  heapLine("avvio");
  gRx = xQueueCreate(64, sizeof(Rx));
  gNotifyGap.threshold = 20000;  // µs
  gRtt.threshold = 100000;       // µs

  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  // Come la libreria asukiaaa provata sul 1914: abbinamento sì, MITM no, Secure Connections no (Just Works)
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEScan *s = NimBLEDevice::getScan();
  s->setScanCallbacks(&gScanCb, true);  // tutti gli annunci: il controller può comparire in ritardo
  s->setActiveScan(true);               // la risposta alla scansione porta il nome
  s->setInterval(100);
  s->setWindow(60);  // non il 100%: lascia tempo al Wi-Fi
  s->setMaxResults(0);  // niente elenco dei risultati in RAM: basta la callback
  heapLine("dopo NimBLE init");

  const int bonds = NimBLEDevice::getNumBonds();
  gPairWanted = bonds == 0;
  Serial.printf("[ble] abbinati in NVS (nimble_bond): %d%s\n", bonds,
                bonds ? " · accendi il controller col tasto Xbox" : " · metti il controller in associazione");
  for (int i = 0; i < bonds; i++) Serial.printf("[ble] abbinato: %s\n", NimBLEDevice::getBondedAddress(i).toString().c_str());
  help();
  gLatAt = gHeapAt = millis();
}

void loop() {
  serialLoop();
  bleLoop();
  padLoop();
  if (gWifiWanted || gPingWanted) wifiLoop();
  const uint32_t now = millis();
  if (now - gLatAt >= kLatWindowMs) {
    gLatAt = now;
    latencyReport();
  }
  if (now - gHeapAt >= kHeapMs) {
    gHeapAt = now;
    heapLine("5 s");
  }
  if (gPingWanted && now - gPingReportAt >= kPingReportMs) {
    gPingReportAt = now;
    pingReport();
  }
  delay(1);
}
