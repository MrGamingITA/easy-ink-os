# Memoria, pacchetto rapido (firmware 0.14.1) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recuperare RAM statica e flash sulla Heltec (ESP32-S3, niente PSRAM) senza cambiare nessuna funzione: le voci 1, 2, 3, 6, 7, 10 della classifica dell'analisi della memoria più le righe `[memoria]` dell'avvio. Stima: RAM statica da 169 408 a ~146 200 byte (−23 KB, che diventano heap libero alla Home), flash −121 KB, picco di heap −3,5 KB per file letto da LittleFS. Versione 0.14.1.

**Architecture:** Nessun modulo nuovo. Due flag di compilazione (WebSocket senza TLS, NimBLE con 2 voci di scrittura preparata); buffer statici che diventano temporanei (`malloc` per il JSON del crash, la `wifi-list` e la memoria dei PC), condivisi (griglie dell'avatar, una sola `MenuList` per `cal::Ui` e `ui::draw`) o spariscono (fusione del registro sul posto in `applyState`); buffer stdio da 512 B nelle letture di LittleFS. La logica pura (core/, ui/) resta provata in `env:native`, con prove di equivalenza vecchio/nuovo sugli stessi ingressi.

**Tech Stack:** PlatformIO (`espressif32@6.9.0`, Arduino 2.0.17 / IDF 4.4), links2004/WebSockets 2.7.3, NimBLE-Arduino 2.5.1, ArduinoJson 7.4.3, Unity in `env:native` e `env:native_asan`. Nessuna dipendenza nuova.

**Fonte:** analisi della memoria in `<scratchpad>/mem-explore/report.md` (§2, §6 «Pacchetto rapido», «Misure da aggiungere»); copia di prova in `…/mem-explore/fw/` (env `nossl` = flag dei Task 2, già compilato: nessun simbolo `ssl`/`WiFiClientSecure` nell'ELF), `firmware.map`, `topsym.py` (simboli più grandi di `.dram0.*` da un file map).

**Prerequisito:** worktree `~/CalamaiOS-memoria`, ramo `memoria` (da `editor-2` 65ef468), albero pulito. Base 0.14.0 da misurare e annotare prima del Task 1:

```bash
cd ~/CalamaiOS-memoria/firmware
pio run -e heltec213 2>&1 | tail -n 5
S=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-size
$S -A .pio/build/heltec213/firmware.elf | grep -E "^\.dram0\.(data|bss)|^\.flash\.(text|rodata) "
ls -l .pio/build/heltec213/firmware.bin
```
Atteso (ELF del 17 ottobre, stesso codice): `.dram0.data` 23 728 + `.dram0.bss` 145 680 = **169 408**; `.flash.text` 1 213 067, `.flash.rodata` 652 868; immagine **1 973 024** byte. Se i numeri sono diversi, valgono quelli misurati: annotarli nel messaggio del primo commit.

## Voci escluse o ridotte (leggendo il codice)

- **Voce 6, griglie dell'avatar: −700 invece di −1 400.** `accOverlay` (ui/screens.cpp:130) confronta due griglie intere (`avatarGrid` contro `figureGrid`, che riscrive tutta la griglia): una sola non basta senza cambiare `figureGrid`. Si condivide solo la griglia di `partyFx` con una delle due.
- **Voce 6, nessuna parte tocca lo stato di §7.** `otaAfterAuth` gira dopo `auth-ok` ma cambia solo il buffer del rapporto di crash; `sendWifiList` legge `gLink.authed()` come prima. `gLink` (880 B), `perms` del modello, conferme e abbinamento: non si toccano.
- **Voce 7 senza i salvataggi della partita** (`hw/gb_hw.cpp`): solo avatar, accessori, mascotte e temi. I tempi della partita restano quelli provati dal vivo.
- Nessun'altra voce è risultata più rischiosa del previsto.

## Global Constraints

- **Regole dei permessi (brief §7) invariate e non negoziabili.** Nessuna modifica a `core/link.*`, `gLink`, `Model::perms`/`nPerms`, `confirmPerm`/`emitPerm`, `onPerm`, `dropFromPhone`, `phoneForbidden`, abbinamento, sfida-risposta, HMAC, nonce. Se un passo sembra richiederlo: fermarsi e chiedere. I flag del Task 2 cambiano il canale (WebSocket senza TLS: era già `ws://`), non le regole: le prove di §7 dal vivo si rifanno tutte.
- **Funzioni identiche.** Stessi testi a schermo, stesse righe di registro (tranne le nuove `[memoria]` del Task 1 e le righe d'errore nuove di una `malloc` fallita, che oggi non possono capitare), stesso protocollo. Una `malloc` fallita rimanda, non perde: il crash e la `wifi-list` si rimandano al prossimo `auth-ok`/cambio, la memoria dei PC risponde «non salvato» come per un errore della NVS.
- **Prove**: ogni task che cambia logica in `src/core`, `src/gfx`, `src/ui` aggiunge prove native e fa passare `pio test -e native`; dove tocca `ui/screens.cpp` (usato dall'interprete dei temi) anche `pio test -e native_asan`. Ogni task finisce con `pio run -e heltec213` verde. `hw/` e `net/` non si compilano in native: lì vale la compilazione più la prova dal vivo.
- **RAM statica**: mai sopra il tetto attuale 169 488 B in nessun commit; dopo ogni task annotare `.dram0.data + .dram0.bss` nel messaggio del commit.
- **Lingua**: commenti brevi in italiano; identificatori in inglese.
- **Uscite**: ogni comando finisce con `2>&1 | tail -n 40` (o `| grep …`).
- **Commit**: uno o due per task, messaggio in italiano, **senza** riga `Co-Authored-By`; niente push, niente merge.
- **Niente hardware**: niente flash, niente NVS, niente `~/.claude/settings.json`. La scheda si usa solo nelle «Prove dal vivo», con l'utente.

Comandi (da `~/CalamaiOS-memoria/firmware`):

```bash
pio test -e native 2>&1 | tail -n 40               # tutte le prove native
pio test -e native -f test_net 2>&1 | tail -n 40   # una sola cartella
pio test -e native_asan 2>&1 | tail -n 40          # test_theme_* con ASan/UBSan
pio run -e heltec213 2>&1 | tail -n 20
S=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-size
$S -A .pio/build/heltec213/firmware.elf | grep -E "^\.dram0\.(data|bss)"
N=~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm
$N -S -C --size-sort .pio/build/heltec213/firmware.elf | grep -iE "<simbolo>"
```

## Review Focus

- **Registro dopo uno state** (eventi locali a metà anello, anello pieno di locali, tempi uguali, log del PC non ordinato): l'ordine e il contenuto di `m.log` restano quelli di prima, voce per voce — `test_log_merge_in_place_matches_old` (Task 5, 5 000 casi casuali più i casi limite).
- **La `MenuList` condivisa** fra `cal::Ui` e `ui::draw`: nessuno legge la lista senza averla riempita nello stesso giro, nessun riferimento sopravvive a un disegno; i fotogrammi delle liste sono identici bit per bit a quelli della 0.14.0 — `test_list_frames_match_0140`, `test_shared_menu_list_draw_then_ui` (Task 6), `test_ui` (scenari del simulatore).
- **§7 col canale senza TLS**: abbinamento, `auth`, `perm-request`/`perm-reply` con HMAC funzionano come prima; l'HMAC usa ancora mbedTLS (`hw/crypto_mbedtls.cpp`: `md`, `gcm`, `ecp`), che il flag non toglie — controllo con `nm` (Task 2) e prove dal vivo P1–P7.
- **`malloc` fallita** nei buffer diventati temporanei (Task 4): nessun rapporto di crash perso (`crashSent` solo dopo l'invio), nessun salvataggio a metà nella NVS.

---

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `firmware/src/main.cpp` | riga `[memoria] avvio`, stack del `loopTask` nelle righe del telefono | 1 |
| `firmware/src/net/net.cpp` | passi `[memoria]` dopo `allocBufs`, avvio del task, `wifiPreInit`; buffer temporanei e memoria dei PC | 1, 4 |
| `firmware/platformio.ini` | `WEBSOCKETS_NETWORK_TYPE=5`, `MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES=2` | 2 |
| `firmware/src/hw/avatar_fs.cpp`, `firmware/src/hw/theme_fs.cpp` | buffer stdio da 512 B nelle letture e nelle scritture piccole | 3 |
| `firmware/src/hw/ota_hw.h`, `ota_hw.cpp` | `crashPending()` | 4 |
| `firmware/src/ui/screens.cpp` | griglie dell'avatar condivise (4); lista del disegno condivisa (6) | 4, 6 |
| `firmware/src/core/apply_state.cpp` | fusione del registro sul posto | 5 |
| `firmware/src/core/menu.h`, `menu.cpp`, `core/ui.h`, `core/ui.cpp` | `sharedMenuList()`, `Ui::list_` come riferimento | 6 |
| `firmware/test/test_pet/test_main.cpp` | impronte dei fotogrammi con festa e accessorio | 4 |
| `firmware/test/test_net/test_main.cpp` | equivalenza della fusione; lista condivisa; versione | 5, 6, 7 |
| `firmware/src/core/model.h` | `CALAMAIO_FW_VERSION "0.14.1"` | 7 |
| `docs/DECISIONI.md` | sezione «Memoria (firmware 0.14.1)» | 7 |

---

### Task 1: Righe `[memoria]` dell'avvio

Solo diagnostica: serve a dividere i 104 KB che spariscono fra `setup()` e «prima del Bluetooth» e a misurare lo stack del `loopTask` (voce 8, futura). Nessun cambio di comportamento.

**Files:**
- Modify: `firmware/src/main.cpp:1927-1928` (riga `[memoria] Ui …`), `firmware/src/main.cpp:267-280` (`heapLine`)
- Modify: `firmware/src/net/net.cpp:2076-2080` (`task`, dopo `wifiPreInit()`), `firmware/src/net/net.cpp:2156` (dopo `allocBufs()`), `firmware/src/net/net.cpp:2172` (dopo `xTaskCreatePinnedToCore`)

- [ ] **Step 1: riga `[memoria] avvio` in `setup()`.** Sostituire la riga `[memoria] Ui %u byte, canvas %u byte, heap libero %u` con una sola riga che tiene gli stessi tre numeri e aggiunge le regioni (RTC a parte) e lo stack del loop:

```cpp
  // 0.14.1: heap interno e RTC FAST separati (blocco massimo di ciascuno) e minimo di stack libero del loopTask.
  LogSerial.printf("[memoria] avvio: Ui %u byte, canvas %u byte, heap libero %u, interna %u (blocco %u), RTC %u (blocco %u), "
                   "stack loop libero %u\n",
                   (unsigned)sizeof(Ui), (unsigned)sizeof(canvas), (unsigned)ESP.getFreeHeap(),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_RTCRAM),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_RTCRAM),
                   (unsigned)uxTaskGetStackHighWaterMark(nullptr));
```
`MALLOC_CAP_RTCRAM` c'è nell'IDF del core (`esp_heap_caps.h:37`). `heap_caps_get_largest_free_block` è già incluso in main.cpp (riga 279). Se `Print::printf` tronca la riga (buffer interno), spezzarla in due `printf` con lo stesso prefisso `[memoria] avvio`.

- [ ] **Step 2: stack del loop nelle righe del telefono.** In `heapLine`, dentro `if (netStack)`, dopo `stackMark(st, sizeof st, "nimble_host", "host BLE");` aggiungere `stackMark(st, sizeof st, "loopTask", "loop");` e portare `char st[80]` a `char st[96]` (quattro voci). Nessun'altra riga cambia.

- [ ] **Step 3: passi della rete.** In `net.cpp` usare il `memLog` esistente (riga 189):
  - in `begin()`, subito dopo `if (!allocBufs()) …`: `memLog("dopo allocBufs");`
  - in `begin()`, subito dopo `xTaskCreatePinnedToCore(task, "calamaio-net", …)`: `memLog("dopo l'avvio del task di rete");`
  - in `task()`, subito dopo `wifiPreInit();`: `memLog(resumed ? "dopo wifiPreInit (ripresa)" : "dopo wifiPreInit", true);` (stack del task di rete compreso).

  Non toccare il percorso di `resume()` (riga 2287) oltre alla riga in `task()`.

- [ ] **Step 4: compilare e misurare.** `pio run -e heltec213` verde; RAM statica invariata (le stringhe stanno in flash).

- [ ] **Step 5: commit** `Memoria: righe [memoria] dell'avvio (regioni, stack del loop, passi della rete)` con RAM statica e immagine nel corpo.

---

### Task 2: Flag di compilazione: WebSocket senza TLS (voce 1) e NimBLE con 2 scritture preparate (voce 10)

Verificato su `.pio/libdeps/heltec213/WebSockets/src/WebSockets.h` (2.7.3): `NETWORK_ESP32 (4)` include `WiFiClientSecure.h` e definisce `WEBSOCKETS_NETWORK_SSL_CLASS` (quindi `HAS_SSL`); `NETWORK_ESP32_ETH (5)` usa `WiFiClient`/`WiFiServer` (con `<ETH.h>`, che non porta codice se non si usa) e **non** definisce la classe SSL. Per NimBLE: `esp_nimble_cfg.h:549` ha `#ifndef MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES … (64)`; il pool `ble_att_svr_prep_entry_mem` (768 B) serve solo al server GATT della scheda per le scritture lunghe ricevute. La scheda è solo centrale (`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED`, nessun `createServer` in `hw/blepad.cpp`): il telefono e l'Xbox sono server, la scheda scrive a loro.

Uso di TLS oggi, controllato nel codice: nessuno.
- WebSocket: `ws.begin(pcIp, pcPort, pcPath, "")` (net.cpp:1513), mai `beginSSL`.
- Download dei temi e degli avatar: `fetchBlob` (net.cpp:933) con `WiFiClient client; http.begin(client, url)`; l'URL lo costruisce la scheda (`theme_proto.cpp:97`, `"http://%s:%u/%s/%s"`).
- OTA: `HttpSource::open` (ota_hw.cpp:76) con `WiFiClient client_; http_.begin(client_, url)`; `ota.cpp:199` rifiuta ogni URL che non comincia con `http://`.
- Nessun `WiFiClientSecure`, `setCACert`, `setInsecure` in `firmware/src`. `HTTPClient` include `WiFiClientSecure.h`, ma il ramo TLS (`begin(url)` senza client) non è mai chiamato e il linker lo toglie (provato nell'env `nossl`).
- mbedTLS resta per la crittografia di §7 e dei temi (`hw/crypto_mbedtls.cpp`: `ecp`, `gcm`, `md`, `sha256`): il flag toglie solo lo strato SSL.

**Files:**
- Modify: `firmware/platformio.ini` (`[env:heltec213]`, `build_flags`)

- [ ] **Step 1: WebSocket senza TLS.** In `build_flags` di `[env:heltec213]`, dopo le righe di NimBLE:

```ini
  ; 0.14.1, memoria: WebSocket senza TLS (la scheda usa solo ws:// e http://). 5 = NETWORK_ESP32_ETH di
  ; links2004/WebSockets 2.7.3: WiFiClient senza WiFiClientSecure (−121 KB di flash, −1 008 B di RAM statica)
  -D WEBSOCKETS_NETWORK_TYPE=5
```
`fastink-demo` estende `heltec213` e prende il flag: non usa WebSocket, nessun effetto.

- [ ] **Step 2: compilare e controllare i simboli.**

```bash
pio run -e heltec213 2>&1 | tail -n 20
$N -C .pio/build/heltec213/firmware.elf | grep -ciE "WiFiClientSecure|mbedtls_ssl_|ssl_client"   # atteso 0
$N -C .pio/build/heltec213/firmware.elf | grep -cE "mbedtls_(gcm|md_hmac|ecp_mul|sha256)"          # atteso > 0 (§7)
$S -A .pio/build/heltec213/firmware.elf | grep -E "^\.dram0\.(data|bss)"
```
Atteso: RAM statica −1 008 (circa), immagine −121 KB (circa) rispetto alla base. Se il primo `grep` trova simboli: fermarsi e cercare chi li porta (`firmware.map` con `-Wl,-Map`, come l'env `mapx` della copia di prova).

- [ ] **Step 3: commit** `Memoria: WebSocket senza TLS (WEBSOCKETS_NETWORK_TYPE=5), −121 KB di flash`.

- [ ] **Step 4: NimBLE.** Sempre in `build_flags`, nel blocco «0.12.0, memoria della partita»:

```ini
  ; 0.14.1: la scheda è solo centrale; il pool delle scritture preparate serve al suo server GATT (solo GAP/GATT):
  ; 2 voci invece di 64 (−744 B). Una scrittura lunga ricevuta oltre 2 pezzi ha «coda piena» dall'ATT.
  -D MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES=2
```

- [ ] **Step 5: compilare.** `pio run -e heltec213` verde; `$N -S -C .pio/build/heltec213/firmware.elf | grep ble_att_svr_prep_entry_mem` atteso 24 B (0x18); RAM statica −744 sul passo 2.

- [ ] **Step 6: commit** `Memoria: NimBLE con 2 scritture preparate (−744 B)`.

---

### Task 3: Buffer stdio da 512 B nelle letture di LittleFS (voce 7)

`VFSFileImpl` apre ogni file con `setvbuf(_f, NULL, _IOFBF, DEFAULT_FILE_BUFFER_SIZE)` (vfs_api.cpp:305, ~4 KB) e newlib alloca quel buffer alla prima lettura o scrittura: ogni `open` + `read` chiede un blocco contiguo da 4 KB. Dalla 0.14.0 i download (`/av/dl`, `/th/tmp`) usano già `setBufferSize(512)` (DECISIONI, «Avatar a pezzi», giro 1). Qui lo stesso per ogni file aperto per leggere o scrivere dati; chi apre solo per `size()` non alloca e resta com'è.

**Files:**
- Modify: `firmware/src/hw/avatar_fs.cpp`, `firmware/src/hw/theme_fs.cpp`

- [ ] **Step 1: un aiuto per file.** In ciascuno dei due file, nel namespace anonimo in alto:

```cpp
// 0.14.1: buffer stdio da 512 invece dei ~4 KB di vfs_api (allocati alla prima lettura o scrittura); prima di ogni I/O.
File openSmall(const char *path, const char *mode) {
  File f = LittleFS.open(path, mode);
  if (f) f.setBufferSize(512);
  return f;
}
```

- [ ] **Step 2: usarlo dove si legge o si scrive.**
  - `avatar_fs.cpp`: `writeAtomic` (riga 25, accessori, ancore, mascotte), `readSlot` (51), `readIndex` (69), `writeFile` (120), `LittleFsAccStorage::read` (137), `loadPet` (222). L'aiuto va prima di `writeAtomic`.
  - `theme_fs.cpp`: `readAt` (46), `readOrder` (130), `writeOrder` (137).
  - Restano con `LittleFS.open`: `slotSize`, `adoptTemp`, `tmpSize` (solo `size()`), le cartelle (`openNextFile`), `tempBegin`/`tmpBegin` (hanno già 512).

- [ ] **Step 3: compilare.** `pio run -e heltec213` verde; RAM statica invariata. Il guadagno è di heap (picco −3,5 KB per file aperto) e si vede solo dal vivo (prova P9).

- [ ] **Step 4: commit** `Memoria: buffer stdio da 512 B nelle letture e scritture piccole di LittleFS`.

---

### Task 4: Buffer piccoli (voce 6): crash, wifi-list, memoria dei PC, griglie dell'avatar

Quattro statici: `otaAfterAuth()::buf` 2 048, `sendWifiList()::buf` 1 024, `pcMemLoad()::blob` + `pcMemSave()::blob` 2 × 708, una griglia da 700 di `partyFx`. Totale −5 188 B. Tutti nel task di rete tranne le griglie (loop, disegno).

**Files:**
- Modify: `firmware/src/hw/ota_hw.h:36-41`, `firmware/src/hw/ota_hw.cpp:238`
- Modify: `firmware/src/net/net.cpp:783-799` (`sendWifiList`), `:1369-1408` (`pcMemLoad`, `pcMemSave`), `:1647-1671` (`otaAfterAuth`)
- Modify: `firmware/src/ui/screens.cpp:130-166` (`accOverlay`, `partyFx`)
- Test: `firmware/test/test_pet/test_main.cpp`

- [ ] **Step 1: impronte dei fotogrammi PRIMA di toccare `screens.cpp`.** In `test_pet` (aggiungere `#include "gfx/canvas.h"` e `#include "ui/screens.h"`), una prova che compone con `ui::compose` su `static Canvas<250, 122>` i fotogrammi 0 e 1 di: Home con festa (`petParty` vero, come in `test_party_rules`) senza accessorio; Home con festa e accessorio indossato (`cappellino`, come `test_menus_and_wear`); Dettaglio con festa e accessorio; schermata Ota con l'accessorio (`m.ota.phase` diverso da `None`, usa `accOverlay` a riga 1007). Per ognuno un'impronta FNV-1a a 32 bit dei byte di `cv.data()` (`250 * 122 / 8` byte). Primo giro col codice della 0.14.0: stampare le impronte (`printf`), copiarle in `const uint32_t kPartyFrames[]` e fare `TEST_ASSERT_EQUAL_HEX32`. Nome: `test_party_and_accessory_frames_match_0140`. Deve passare prima della modifica.

- [ ] **Step 2: griglie condivise.** In `screens.cpp`, nel namespace anonimo prima di `accOverlay`:

```cpp
// 0.14.1: griglie 28×25 dell'avatar condivise da accOverlay (due) e partyFx (una): solo il loop disegna, mai annidate.
uint8_t gGridBase[kAvatarCells], gGridFig[kAvatarCells];
```
`accOverlay`: togliere `static uint8_t g0[…], g[…]` e usare `gGridBase` al posto di `g0`, `gGridFig` al posto di `g`. `partyFx`: togliere `static uint8_t g[kAvatarCells]` e usare `gGridFig`. Nessun'altra riga cambia (`partyFx` non chiama `accOverlay` e viceversa).

- [ ] **Step 3: prove.** `pio test -e native 2>&1 | tail -n 40` e `pio test -e native_asan 2>&1 | tail -n 40` (l'interprete dei temi chiama `accOverlay` da `kit::mascot`): tutto verde, impronte invariate.

- [ ] **Step 4: crash.** In `ota_hw.h`, accanto a `crashJson`: `bool crashPending();  // c'è un rapporto da mandare (0.14.1: il buffer si alloca solo allora)`; in `ota_hw.cpp`: `bool crashPending() { return gCrashOk; }`. In `otaAfterAuth()` sostituire il blocco da `static char buf[2048];` a fine funzione con:

```cpp
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
```
Prima la condizione era `n && wsConn` dopo `crashJson`: con `wsConn` falso non si mandava e non si cancellava; ora uguale. La parte `gBootReported` sopra non cambia.

- [ ] **Step 5: wifi-list.** In `sendWifiList()` sostituire `static char buf[cal::wifi::kListMax];` e le righe seguenti con:

```cpp
  char *buf = static_cast<char *>(malloc(cal::wifi::kListMax));  // 0.14.1: 1 KB solo durante l'invio
  if (!buf) { LogSerial.println("[ws] wifi-list: memoria non disponibile, non mandato"); return; }
  const size_t n = cal::wifi::encodeList(e, gNSaved, buf, cal::wifi::kListMax);
  if (!n) LogSerial.println("[ws] wifi-list: buffer corto, non mandato");
  else {
    ws.sendTXT(buf, n);
    LogSerial.printf("[ws] %s\n", buf);
  }
  free(buf);
```
(`sizeof buf` non vale più: usare `kListMax`.) Il controllo `if (!wsConn || !gLink.authed()) return;` in testa resta identico.

- [ ] **Step 6: memoria dei PC, un solo blob statico.** `gPcBlob` (ultima copia scritta) resta. `pcMemLoad()` legge direttamente in `gPcBlob`:

```cpp
void pcMemLoad() {
  Preferences p;
  gPcMem.clear();
  gPcBlobLen = gPcMem.encode(gPcBlob, sizeof gPcBlob);  // niente in NVS = tabella vuota: nessuna scrittura per niente
  if (!p.begin(kPcNs, true)) return;  // namespace mai scritto
  const size_t n = p.getBytesLength("v1");
  // 0.14.1: si legge direttamente nella copia; se non vale, la copia torna quella della tabella vuota
  if (n && n <= sizeof gPcBlob && p.getBytes("v1", gPcBlob, n) == n && gPcMem.decode(gPcBlob, n)) {
    gPcBlobLen = n;
    LogSerial.printf("[pc] indirizzi dei PC: %d reti\n", gPcMem.count());
  } else {
    if (n && n <= sizeof gPcBlob) LogSerial.println("[pc] indirizzi dei PC in NVS non validi: ignorati");
    gPcMem.clear();
    gPcBlobLen = gPcMem.encode(gPcBlob, sizeof gPcBlob);
  }
  p.end();
}
```
Attenzione all'equivalenza: oggi il messaggio «non validi» esce solo se la lettura riesce e `decode` fallisce; tenere la stessa condizione (scrivere il ramo in modo che la riga esca solo quando `getBytes` ha letto `n` byte e `decode` ha detto falso). `decode` azzera la tabella in testa (`n_ = 0`, pc_fallback.cpp:203), quindi `clear()` dopo un fallimento non cambia niente.

`pcMemSave()`: `static uint8_t blob[…]` diventa `uint8_t *blob = static_cast<uint8_t *>(malloc(cal::PcMemory::kBlobMax));` subito prima di `encode`; se è `nullptr`: `LogSerial.printf("[pc] %s: NON salvato (memoria)\n", why); return false;`. `free(blob)` su ogni uscita (uguale alla copia, dopo la scrittura). `keep` (statico, 198 B) resta.

- [ ] **Step 7: compilare e misurare.** `pio run -e heltec213` verde. Con `nm`: spariti `otaAfterAuth()::buf`, `sendWifiList()::buf`, `pcMemLoad()::blob`, `pcMemSave(char const*)::blob`, `partyFx(...)::g`. RAM statica −5 188 sul Task 2.

- [ ] **Step 8: commit** `Memoria: crash, wifi-list e memoria dei PC sullo heap solo quando servono; griglie dell'avatar condivise (−5,2 KB)`.

---

### Task 5: Fusione del registro sul posto (voce 2)

Oggi `applyState` (apply_state.cpp:289-308) fonde gli eventi locali di `m.log` (anello, `at(i) = e[(head + i) % kLogCap]`) con il log del PC (`msg.log`, al più 20) in `static LogEntry merged[kLogCap]` (8 960 B), poi copia in `m.log.e[0..n)` con `head = 0`. Regola: a ogni passo il prossimo locale `a` (saltando i non locali) e il prossimo del PC `b` (tempo `bt = mono(b->t)`); si prende `a` se `a && (!b || a->t >= bt)`, altrimenti `b` col tempo convertito; al più `kLogCap` voci.

**Nuovo algoritmo, sul posto, stesso risultato voce per voce:**
1. **Raddrizzare l'anello:** `std::rotate(L.e, L.e + L.head, L.e + kLogCap)`; ora `at(i) == e[i]` per `i < n` (vale anche con `n < kLogCap`: le voci occupate sono `head … head+n−1` modulo 80). `head = 0`.
2. **Compattare i locali in testa**, in ordine: per `k` da 0 a `n−1`, se `e[k].local` allora `e[nl++] = e[k]` (indice di scrittura ≤ lettura: sicuro). I non locali spariscono come prima.
3. **Decidere l'ordine** con la stessa identica regola, senza copiare: `bool fromLocal[kLogCap]` sullo stack (80 B), `li`, `pi`, `n`.
4. **Scrivere dal fondo:** per `w` da `n−1` a 0, se `fromLocal[w]` allora `e[w] = e[--li]` (salta se uguali), altrimenti `e[w] = msg.log[--pi]` con `t = mono(msg.log[pi].t)`. Invariante: fra le decisioni `0…w` ci sono `li` locali, quindi `li − 1 ≤ w`; quando in `w` va una voce del PC, i locali ancora da spostare stanno in `0…li−1` con `li ≤ w`: la scrittura non ne tocca nessuno. I locali scartati (oltre `kLogCap`) sono quelli in coda alla compattazione e vengono sovrascritti o ignorati.

Le voci fisiche oltre `n` restano sporche, come oggi (oggi restavano quelle di prima): nessuno le legge (`m.log` si legge solo con `at(i)`, `i < n`; controllato con `grep`: `log.e[` solo in apply_state.cpp e in `logEv`). Proprietario: `applyState` nel loop, unico scrittore di `m.log` (come oggi).

**Files:**
- Modify: `firmware/src/core/apply_state.cpp` (righe 289-308; `#include <algorithm>` in testa)
- Test: `firmware/test/test_net/test_main.cpp`

- [ ] **Step 1: prova di equivalenza (rossa no: verde anche prima, è il riferimento).** In `test_net`, vicino a `test_example_state`, copiare **alla lettera** il ciclo attuale in una funzione di riferimento e scrivere la prova:

```cpp
// Fusione del registro della 0.14.0 (buffer da 80 voci), copiata alla lettera: riferimento per la 0.14.1 sul posto.
static void refMergeLog(Log &L0, const StateMsg &msg, Ms now) {
  auto mono = [&](int64_t t) -> Ms { return t ? now + (t - msg.time) : 0; };
  static LogEntry merged[kLogCap];
  int n = 0, li = 0, pi = 0;
  const Log &L = L0;
  auto nextLocal = [&]() -> const LogEntry * {
    while (li < L.n && !L.at(li).local) li++;
    return li < L.n ? &L.at(li) : nullptr;
  };
  while (n < kLogCap) {
    const LogEntry *a = nextLocal();
    const LogEntry *b = pi < msg.nLog ? &msg.log[pi] : nullptr;
    if (!a && !b) break;
    Ms bt = b ? mono(b->t) : 0;
    if (a && (!b || a->t >= bt)) { merged[n++] = *a; li++; }
    else { merged[n] = *b; merged[n].t = bt; n++; pi++; }
  }
  L0.head = 0;
  L0.n = n;
  for (int k = 0; k < n; k++) L0.e[k] = merged[k];
}

static void assertSameLog(const Log &want, const Log &got, int caseNo) {
  char why[48];
  snprintf(why, sizeof why, "caso %d", caseNo);
  TEST_ASSERT_EQUAL_MESSAGE(want.n, got.n, why);
  TEST_ASSERT_EQUAL_MESSAGE(want.head, got.head, why);
  for (int i = 0; i < want.n; i++) {
    TEST_ASSERT_EQUAL_INT64_MESSAGE(want.at(i).t, got.at(i).t, why);
    TEST_ASSERT_EQUAL_MESSAGE(want.at(i).local, got.at(i).local, why);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want.at(i).name, got.at(i).name, why);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want.at(i).text, got.at(i).text, why);
  }
}
```
`test_log_merge_in_place_matches_old`: `static Model a; static Log want; static StateMsg s;` generatore xorshift32 con seme fisso. Per 5 000 casi: `memset(&a.log, 0, …)`, `a.log.head = rnd(80)`, `a.log.n = rnd(81)`, per ogni voce occupata `at(i)`: `t` in `{0, 1000…1500}` (con ripetizioni, per i pari merito), `local` vero 2 volte su 3, `name`/`text` unici (`"L<caso>-<i>"`); `memset(&s, 0, sizeof s)`, `s.time = kNow - rnd(5000)`, `s.nLog = rnd(kStateLogCap + 1)`, tempi epoch in modo che `mono` cada nello stesso intervallo (anche `0` e anche non ordinati), testi `"P<caso>-<j>"`, `local = false`. Poi `want = a.log; refMergeLog(want, s, kNow); applyState(a, s, kNow); assertSameLog(want, a.log, caso);`.
Casi fissi in più (stessa funzione): anello vuoto e log del PC vuoto; 80 locali tutti più nuovi del PC; 80 locali tutti più vecchi; 80 locali con `head = 79` e 20 del PC più nuovi (si perdono 20 locali in coda); locali e PC con tempi tutti uguali (vince il locale); solo non locali (spariscono tutti); log del PC non ordinato.

- [ ] **Step 2: eseguire.** `pio test -e native -f test_net 2>&1 | tail -n 40`: verde col codice della 0.14.0 (il riferimento è lo stesso codice). Commit della sola prova: `Prova: fusione del registro, riferimento della 0.14.0`.

- [ ] **Step 3: implementare.** In `apply_state.cpp`, funzione libera nel namespace anonimo (o `static`) e chiamata da `applyState` al posto del blocco attuale:

```cpp
// Log: eventi locali della scheda + quelli del PC, il più recente in testa, al massimo kLogCap.
// 0.14.1, sul posto (prima un buffer statico da 8 960 B): anello raddrizzato, locali compattati in testa, ordine
// deciso con la regola di sempre, poi scrittura dal fondo (non tocca mai un locale ancora da spostare).
static void mergeLog(Log &L, const StateMsg &msg, Ms now) {
  auto mono = [&](int64_t t) -> Ms { return t ? now + (t - msg.time) : 0; };
  std::rotate(L.e, L.e + L.head, L.e + kLogCap);
  L.head = 0;
  int nl = 0;
  for (int k = 0; k < L.n; k++)
    if (L.e[k].local) {
      if (nl != k) L.e[nl] = L.e[k];
      nl++;
    }
  bool fromLocal[kLogCap];
  int n = 0, li = 0, pi = 0;
  while (n < kLogCap && (li < nl || pi < msg.nLog)) {
    const bool loc = li < nl && (pi >= msg.nLog || L.e[li].t >= mono(msg.log[pi].t));
    fromLocal[n++] = loc;
    if (loc) li++;
    else pi++;
  }
  for (int w = n - 1; w >= 0; w--) {
    if (fromLocal[w]) {
      if (--li != w) L.e[w] = L.e[li];
    } else {
      --pi;
      L.e[w] = msg.log[pi];
      L.e[w].t = mono(msg.log[pi].t);
    }
  }
  L.n = n;
}
```
In `applyState`: `mergeLog(m.log, msg, now);` al posto delle righe 289-308. `mono` dentro `applyState` resta per gli altri campi.

- [ ] **Step 4: prove.** `pio test -e native 2>&1 | tail -n 40`: tutto verde, compresa l'equivalenza e `test_example_state` (log 2 del PC + 1 locale). Con `nm`: `applyState(...)::merged` sparito; RAM statica −8 960.

- [ ] **Step 5: compilare e commit.** `pio run -e heltec213` verde. Commit `Memoria: fusione del registro sul posto in applyState (−8 960 B)`.

---

### Task 6: Una sola `MenuList` per `cal::Ui` e `ui::draw` (voce 3)

Oggi ci sono due `MenuList` da 7 316 B: `Ui::list_` (membro di `gUi`, core/ui.h:386) e `static cal::MenuList list` in `drawScreen` (ui/screens.cpp:1579). Lette nel codice, entrambe si **riempiono sempre subito prima dell'uso** e nessun riferimento sopravvive alla chiamata:
- `Ui::normalize` (ui.cpp:127, riempie con `menuList`), `Ui::act` Next/Prev (ui.cpp:471, `fillItems` poi legge), `Ui::activate` (ui.cpp:681, `fillItems` poi legge `it`), `Ui::items` (riempie e restituisce, solo nelle prove);
- `drawScreen` riempie con `menuList`/`netItems` e passa a `listScreen` nello stesso `case`.
- Chiamanti di `ui::draw`/`ui::compose`: `render` e il risveglio dell'ora in main.cpp (326, 1556) e `gbmain::bands` (gb_main.cpp:53, GbPlay: non disegna liste), tutti dal loop, mai dentro un metodo di `Ui`. I callback che `Ui` chiama a metà (`onPerm`, `onNetRequest` in main.cpp:592-630) non disegnano.

**Cambio di proprietà:** una sola `MenuList` in `core/menu.cpp`, raggiunta con `cal::sharedMenuList()`. `Ui::list_` diventa `MenuList &list_` (riferimento a quella), `drawScreen` usa `cal::sharedMenuList()`. Regola scritta in `menu.h`: «chi la legge la riempie nello stesso giro; nessun riferimento oltre la chiamata; solo il loop». Risparmio −7 312 B (7 316 meno il riferimento da 4).

**Files:**
- Modify: `firmware/src/core/menu.h` (dopo `struct MenuList`), `firmware/src/core/menu.cpp`
- Modify: `firmware/src/core/ui.h:386`, `firmware/src/core/ui.cpp:98-102` (costruttore)
- Modify: `firmware/src/ui/screens.cpp:1579`
- Test: `firmware/test/test_net/test_main.cpp`

- [ ] **Step 1: impronte delle liste PRIMA della modifica.** In `test_net` (ha già `ui/screens.h`, `Canvas`, `LiveBench`), `test_list_frames_match_0140`: un `static LiveBench b`; per ogni `MenuId` da `Main` a `Count − 1` mettere in cima alla pila una voce `ScreenId::List` con quel `menu` e `sel = 1` (scrivendo `b.ui.model().ui.stack[b.ui.model().ui.depth - 1]` dopo aver aperto il Menu con `b.act(Action::Menu)`; per `SavedNet` `ssid` = una rete salvata del banco, come in `test_live_saved_networks`), poi `ui::compose` su `static Canvas<250, 122>` e impronta FNV-1a dei byte; più la lista «Reti vicine» (`ScreenId::WifiScan` con `scanUntil = 0` e reti finte in `m.nets`). Primo giro col codice attuale: stampare, copiare in `const uint32_t kListFrames[]`, asserire. Commit della sola prova: `Prova: impronte dei fotogrammi delle liste (0.14.0)`.

- [ ] **Step 2: la lista condivisa.** In `menu.h`, dopo `struct MenuList`:

```cpp
// 0.14.1: una sola MenuList (7 KB) per cal::Ui e ui::draw. Chi la legge la riempie nello stesso giro (menuList,
// netItems) e non tiene riferimenti oltre la chiamata; solo il loop la usa.
MenuList &sharedMenuList();
```
In `menu.cpp`: `MenuList &sharedMenuList() { static MenuList list; return list; }` (POD: zero in `.bss`, nessuna guardia di inizializzazione).

- [ ] **Step 3: `Ui`.** In `ui.h:386` `MenuList list_;` → `MenuList &list_;  // sharedMenuList() (0.14.1)`. Nel costruttore (ui.cpp:98) aggiungere `list_(sharedMenuList())` nella lista d'inizializzazione **nella posizione del membro** (stesso ordine della dichiarazione, altrimenti `-Wreorder`); il `memset(&list_, 0, sizeof list_)` (ui.cpp:102) resta. Se qualcosa assegna un `Ui` a un altro (il riferimento lo vieta) la compilazione lo dice: oggi nessuno lo fa (`grep` su src e test).

- [ ] **Step 4: `drawScreen`.** In `screens.cpp:1579` `static cal::MenuList list;  // ~4 KB…` → `cal::MenuList &list = cal::sharedMenuList();  // 0.14.1: la stessa di cal::Ui, riempita qui`. Nient'altro cambia.

- [ ] **Step 5: prova nuova sull'alternanza.** In `test_net`, `test_shared_menu_list_draw_then_ui`:
  - `&b.ui.items(b.now, b.wc()) == &cal::sharedMenuList()`;
  - Menu aperto, `compose` in `A`; sporcare la lista con `cal::netItems(m, cal::sharedMenuList())` e con un altro `menuList(MenuId::Info, …)`; `compose` in `B`; `memcmp(A.data(), B.data(), 250 * 122 / 8) == 0`;
  - dopo un `compose` della lista Wi-Fi, `b.next(2)` e `b.act(Action::Ok)` aprono la stessa voce che aprono senza il disegno in mezzo (confrontare `top(m).menu`/`id` con un secondo banco che non disegna);
  - `Ui::act` Next dopo un `compose` di «Reti vicine»: «Ultimo comando» `Voce: <label>` uguale a quello del banco senza disegno.

- [ ] **Step 6: prove.** `pio test -e native 2>&1 | tail -n 40` e `pio test -e native_asan 2>&1 | tail -n 40`: tutto verde, `kListFrames` invariate, `test_ui` (scenari del simulatore, che alternano `Ui` e `compose`) invariato. Se una prova esistente tiene un `const MenuList &X = b.ui.items(…)` e poi disegna prima di leggere `X` (controllare `test_gb_ui`, `test_m5_ui`, `test_net`, `test_pad_ui`, `test_workday`): copiarla per valore (`const MenuList X = …`) **nella prova**, mai nel firmware, e dirlo nel commit.

- [ ] **Step 7: compilare e misurare.** `pio run -e heltec213` verde; con `nm`: `drawScreen(...)::list` sparito, `cal::sharedMenuList()::list` 7 316 B, `gUi` 38 624 → 31 312. RAM statica −7 312.

- [ ] **Step 8: commit** `Memoria: una sola MenuList per Ui e disegno (−7,3 KB)`.

---

### Task 7: Versione 0.14.1, misure e DECISIONI

**Files:**
- Modify: `firmware/src/core/model.h:15`, `firmware/test/test_net/test_main.cpp:279`
- Modify: `docs/DECISIONI.md` (sezione nuova in fondo)

- [ ] **Step 1: versione.** `#define CALAMAIO_FW_VERSION "0.14.1"`; in `test_net` `TEST_ASSERT_EQUAL_STRING("0.14.1", kFwVersion);  // 0.14.1: memoria, pacchetto rapido`. `grep -rn '"0\.14\.0"' firmware/src firmware/test` non trova più niente (i commenti del servizio restano: parlano della 0.14.0).

- [ ] **Step 2: tutte le prove.** `pio test -e native 2>&1 | tail -n 40`, `pio test -e native_asan 2>&1 | tail -n 40`, `cd ../service && go test ./... 2>&1 | tail -n 5` (non cambia nulla, controllo di regressione).

- [ ] **Step 3: compilare e misurare** `pio run -e heltec213`, poi `$S -A` (`.dram0.data`, `.dram0.bss`, `.flash.text`, `.flash.rodata`), `ls -l firmware.bin`, e con `nm -S -C --size-sort` i primi 20 simboli di `.bss`/`.data` (o `topsym.py` della copia di prova su un map ottenuto con `-Wl,-Map`). Tabella base → 0.14.1 per task. Attese: RAM statica 169 408 → ~146 200 (−1 008 −744 −5 188 −8 960 −7 312 = −23 212), immagine 1 973 024 → ~1 849 000 (−121 KB).

- [ ] **Step 4: DECISIONI.** Nuova sezione `## Memoria (firmware 0.14.1)` con: fonte (analisi della memoria, voci 1, 2, 3, 6, 7, 10); le misure del passo 3 (base e dopo, per task); il tetto della RAM statica (169 488) che resta come tetto e la nuova base misurata; le scelte: `WEBSOCKETS_NETWORK_TYPE=5` (perché 5, dove si usava TLS: da nessuna parte; mbedTLS resta per §7), prep entries 2 (solo centrale), fusione sul posto (algoritmo e prova di equivalenza), `sharedMenuList` (regola d'uso), buffer temporanei (cosa succede con `malloc` fallita), griglie (−700 e non −1 400: `accOverlay` ne vuole due), LittleFS 512 (solo avatar e temi, non i salvataggi della partita); le righe `[memoria]` nuove (`avvio`, `dopo allocBufs`, `dopo l'avvio del task di rete`, `dopo wifiPreInit`, `loop` nelle righe del telefono); prossimi passi dell'analisi (4 un solo `StateMsg`, 9 Wi-Fi più parco, 8 stack, da misurare con le righe nuove). Esiti delle prove dal vivo: «da annotare».

- [ ] **Step 5: commit** `Firmware 0.14.1: memoria, pacchetto rapido (RAM statica −23 KB, flash −121 KB)`.

---

## Prove dal vivo (con l'utente, dopo il Task 7; backup e conferma prima del flash)

Flash solo con il consenso dell'utente, come sempre (via cavo o OTA dalla 0.14.0). Registro aperto per tutte le prove.

- **P0 Memoria all'avvio**: righe `[memoria] avvio`, `dopo allocBufs`, `dopo l'avvio del task di rete`, `dopo wifiPreInit`, poi quelle note fino alla Home; annotare heap libero e blocco massimo alla Home con e senza Bluetooth, contro la tabella della 0.14.0 (Home 20–25 KB, blocco 7,6–13,8 KB). Atteso: circa +23 KB.
- **P1–P7 §7 (canale senza TLS)**: i sette test di accettazione del brief §7 (consenti, nega, 20 s, gesto lungo, risposta dal terminale, display spento, risposta falsa rifiutata), più un abbinamento nuovo (`calamaio pair`) e la riconnessione con sfida-risposta.
- **P8 Download**: un tema dalla webapp («Invia alla scheda»), un avatar a pezzi, un aggiornamento OTA (con conferma dopo `auth-ok`).
- **P9 LittleFS**: Riposo col tema per 3 risvegli dell'ora, cambio di tema dal menu, Home con avatar della cache: tempi `[temi]`/`[orologio]` come prima.
- **P10 Bluetooth**: telefono come controller (associazione col codice, partita breve), Xbox collegato; righe `[memoria] telefono collegato` con `loop`.
- **P11 Registro**: «Storico» dopo eventi locali (Wi-Fi, abbinamento annullato) e uno state del PC: ordine come sulla 0.14.0.
- **P12 Menu**: giro completo del Menu, «Reti vicine», «Reti salvate», Accessorio con la festa della mascotte.
- **P13 Crash**: dopo un riavvio con rapporto di crash (se ce n'è uno), il rapporto arriva al servizio dopo `auth-ok`.
