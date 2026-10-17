# Giochi Game Boy — contratti tra i tre piani

Spec: `docs/superpowers/specs/2026-10-12-giochi-gameboy-design.md`. Prove: `spikes/fastink/README.md`, `spikes/blepad/README.md`.

I tre piani (A fondamenta sulla scheda, B gioco sulla scheda, C PC) si parlano **solo** attraverso i nomi qui sotto. Chi scrive un piano non li cambia senza aggiornare questo file.

**Versione del 2026-10-12, decisa dal coordinatore e dall'utente.** Sostituisce la prima stesura: le «proposte di modifica al contratto» dei tre piani sono accolte qui (porta 8766, mac obbligatori, token del salvataggio scelto dalla scheda, Range sullo stesso token, `.gbc`, limiti), con queste differenze rispetto alle proposte: token di 10 minuti (non 5); un caricamento fallito non consuma il token del salvataggio; il servizio riprova l'OTA a fine partita; menu «Gioco» con tre voci (sotto).

## Regole comuni

- **C++ della scheda**: GCC 8.4 con `-std=gnu++11`. In tutto il codice dei piani A e B (anche nei test, anche se `env:native` compila con C++17) niente C++14/17: namespace scritti `namespace cal { namespace fastink {`, niente inizializzatori nei membri di struct inizializzate con le graffe, `constexpr` solo a una riga, niente `std::make_unique`, `auto` nei parametri delle lambda, `if constexpr`, binding strutturati, `std::optional`. Ogni `static const` intero di classe usato in un'espressione è definito anche nel `.cpp`.
- **Driver veloce solo durante la partita.** Tutte le altre schermate di Calamaio (menu, Home, Permesso, pausa del gioco compresa) restano su heltec-eink-modules con la regola dei refresh della 0.10.2. Le prove dal vivo del driver (piano A, Task 5–6) restano nell'env di prova `fastink-demo`.
- Testi a schermo in italiano; identificatori in inglese; commenti brevi in italiano.

## Nomi

«game» è già usato due volte: `core/game.*` (minigioco «Alto o basso») e `game`/`service/internal/game` (gioco della mascotte). Per il Game Boy si usa sempre il prefisso **`gb`**: file `gb_*`, pacchetto Go `gbgames`, messaggi `gb-*`, schermate `ScreenId::Gb*`, menu `MenuId::Gb*`, NVS `calamaio-gb`. A schermo: il menu si chiama **«Gioco»**; la pagina della webapp «Giochi».

## Menu › Gioco (scheda e simulatore)

Voce **«Gioco»** del Menu, dopo «Strumenti» e «La mascotte» (se c'è), prima di «Wi-Fi». **Esiste sempre**, anche nel simulatore e nei test golden. Dentro, in quest'ordine:

| Voce | Tipo | Quando | Contenuto |
|---|---|---|---|
| «Controller» | sottomenu `MenuId::GbPad` | solo con `sys.padHw` (scheda vera) | «Stato», «Associa controller», «Dimentica controller» |
| «ROM» | sottomenu `MenuId::GbRoms` | solo con `sys.padHw` | le ROM del PC (da `gb-list`) e il gioco già sulla scheda; **sempre visibile**, anche senza controller. Scegliere un gioco senza controller collegato (`sys.padLink != Ready`) mostra il toast **«Collega il controller per giocare»** e non parte |
| «Alto o basso» | `ScreenId::Game` | sempre | il minigioco esistente, **spostato** da Menu › Strumenti («Gioco: alto o basso»): stessa schermata, stesso record, stesso aiuto |

Nel simulatore (`sys.padHw` falso) Menu › Gioco contiene solo «Alto o basso»: lo spostamento si allinea in `reference/calamaio-os-browser/assets/calamaio-test.js` (allineamento annotato in DECISIONI), poi si rigenerano `golden_core.h` e le catture di `tools/sim-capture` (piano A, Task 12). Chiavi: `menuKey(MenuId::Gb) = "gb"` (stesso id nel simulatore), `"gbpad"`, `"gbroms"`, `"gbpause"`.

Il Bluetooth non ha più un interruttore a sé: si accende con «Associa controller» e si spegne con «Dimentica controller» (`Settings::padOn`, NVS `calamaio-gb/pad`).

## A → B: driver veloce (`firmware/src/hw/fastink.{h,cpp}` + logica pura `firmware/src/core/fastink_engine.{h,cpp}`)

```cpp
namespace cal {
namespace fastink {
enum class Profile : uint8_t { Clean, Smooth };   // Clean = F8 P1 N10 FR7 (~11 fps), Smooth = F4 P1 N6 FR7 (~15 fps)
struct Params { uint8_t budget, tp, phases, fr, edge, polish; };  // core/fastink_engine.h
// ---- contratto ----
bool begin(Profile p);                 // prende il pannello da heltec-eink-modules (completo di serie col bianco), analogico sempre acceso;
                                       // si può richiamare dopo end() (ogni partita, ogni ripresa dalla pausa); false senza memoria
void submit(const uint8_t *frame);     // bersaglio 250×122, 1 bit per pixel, righe di 32 byte (MSB = x più a sinistra), 1 = nero;
                                       // ignora Inverti e Ruota: li applica il piano B al fotogramma
bool tick();                           // un passo di spinta (~50–90 ms, blocca su BUSY), poi vTaskDelay(1) (cede il core 0);
                                       // false se nessun pixel ha budget residuo
bool settled();                        // tutti i pixel al bersaglio
void cleanFull();                      // refresh completo di serie (OTP) + stato dei pixel azzerato sul bersaglio corrente
void end();                            // analogico spento, pannello a heltec-eink-modules: onReturn riconfigura la libreria e il suo
                                       // completo è la pulizia finale (nessun secondo lampo); main.cpp rimette fastmode = false
// ---- aggiunte del piano A ----
void attachBus(SPIClass *bus, void (*onReturn)(), Print *log);  // in setup(): bus della libreria, panelBack()
bool active();                         // tra un begin() riuscito ed end()
void setParams(const Params &p);
const Params &params();
void setTickCtl(uint8_t ctl);          // valore di 0x22 del tick
uint8_t tickCtl();
uint32_t lastTickUs();
uint32_t measureHoldTick();
}  // namespace fastink
}  // namespace cal
```
Indirizzi Y da 1 byte (0x45/0x4F), LUT da 153 byte nel registro 0x32 dopo il caricamento OTP (0x22=B1), tensioni di fabbrica: vedi spike.

Uso dal piano B: solo il task «gb-ink» (core 0) chiama `submit`/`tick`/`settled`/`cleanFull` mentre gira; `begin` ed `end` li chiama il loop a task fermo. **Pulizia**: `cleanFull()` periodico durante la partita, ogni `N` minuti (NVS `calamaio-gb/clean`, minuti, 0 = mai; **predefinito 10, valore da tarare** sul Task 15 del piano B: minuti di gioco dopo cui le scie residue si vedono a occhio); alla pausa e all'uscita la pulizia è il completo di `end()` (un lampo solo, nessun `cleanFull()` in più).

## A → B: controller (`firmware/src/hw/blepad.{h,cpp}` + logica pura `firmware/src/core/pad.{h,cpp}`)

```cpp
namespace cal {
namespace pad {
enum Button : uint16_t { A=1<<0, B=1<<1, X=1<<2, Y=1<<3, LB=1<<4, RB=1<<5, View=1<<6, Menu=1<<7,
                         Guide=1<<8, LS=1<<9, RS=1<<10, Share=1<<11, Up=1<<12, Down=1<<13, Left=1<<14, Right=1<<15 };
struct State { uint16_t buttons; int8_t lx, ly, rx, ry; uint16_t lt, rt; };   // stick −100..100, grilletti 0..1023
const size_t kReportLen = 16;
bool parseXboxReport(const uint8_t *r, size_t n, State &out);                  // report id 1, 16 byte (spike blepad)
uint16_t hatToButtons(uint8_t hat);                                            // croce → Up/Down/Left/Right
int8_t stickPercent(uint16_t raw);
bool sameState(const State &a, const State &b);
enum class Link : uint8_t { Off, Pairing, Connecting, Ready };
const char *linkKey(Link l);                                                   // "off"|"pairing"|"connecting"|"ready" (gb-pad)
enum class MenuEvent : uint8_t { None, Up, Down, Select, Back, Menu, HoldDown, HoldUp, HoldFire };
const char *menuEventName(MenuEvent e);
class MenuMapper {                     // tasti → eventi dei menu, A tenuto 3 s = conferma «tieni premuto»
 public:
  static const uint32_t kHoldMs = 3000, kTapMs = 700;
  int update(const State &s, uint32_t nowMs, MenuEvent *out, int cap);
  int tick(uint32_t nowMs, MenuEvent *out, int cap);
  int reset(MenuEvent *out, int cap);
  bool holding() const;
};
class LinkMachine;                     // piano A, Task 9 (enable, pair, forget, connected, lost, tick, link, bonded, acceptNew)
struct RadioIn { bool setting, asleep, wifiPsOk, usb, idle; Link link; uint32_t now, searchFrom; };
struct RadioOut { bool radio, scan; };
RadioOut radioPlan(const RadioIn &in);
}  // namespace pad
namespace blepad {
void begin();                          // NimBLE-Arduino 2.5.1, namespace NVS `nimble_bond`
void end();
bool active();
void startPairing();
void forget();
bool bonded();
int battery();                         // 0–100, −1 = sconosciuta
void setScan(bool on);
pad::Link link();
bool poll(pad::State &s);              // un report cambiato alla volta, in ordine d'arrivo; true se ne ha dato uno
uint32_t polledAt();                   // millis() d'arrivo dello stato appena dato da poll()
}  // namespace blepad
}  // namespace cal
```
La croce (hat) diventa i bit Up/Down/Left/Right. Nei menu: Up/Down = sposta, A = scegli, B = indietro, **A tenuto 3 s = la stessa conferma «tieni premuto» dei permessi** (mai una pressione singola).

**Chi legge `blepad::poll`**: fuori dalla partita il loop del piano A (mappatura dei menu). Mentre `gUi.gbPlaying()` è vero la mappatura del piano A è **sospesa** (il suo `MenuMapper` si azzera e gli eventi si scartano) e `poll` lo legge solo il piano B.

## A ↔ B: agganci nell'interfaccia (vincono i nomi del piano A)

- `MenuId::Gb` («Gioco»), `MenuId::GbPad` («Controller»), `MenuId::GbRoms` («ROM») — piano A; `MenuId::GbPause` — piano B, dopo `GbRoms`.
- `ActId::GbPair`, `ActId::GbForget` — piano A; `ActId::GbPick`, `GbResume`, `GbExit`, `GbProfile` — piano B. (`ActId::GbPad` della prima stesura non esiste più: niente interruttore del Bluetooth.)
- `int cal::gbGameItems(const Model &m, Ms now, MenuItem *out, int cap)` in `core/gb_menu.h`: il piano A dichiara, scrive la versione vuota (ritorna 0, e la lista «ROM» mostra «Nessuna ROM») e la chiama dal blocco `MenuId::GbRoms` con `out = out.items`, `cap = kMaxItems`; il piano B ne scrive il corpo in `core/gb_menu.cpp` (voci azzerate e complete, ritorna quante, mai più di `cap`).
- `bool Ui::gbPlaying() const` in `core/ui.h`: il piano A la dichiara con il corpo `return false;` e la usa in `padLoop()`; il piano B ne sostituisce il corpo con `return top(m_).id == ScreenId::GbPlay;`.
- Stato del controller nel modello: `Settings::padOn`, `Sys::{padHw, padLink, padBonded, padHasBatt, padBatt}`; `enum class PadReq : uint8_t { None, Pair, Forget }`, `Ui::padLinkChanged`, `Ui::takePadReq` (piano A).
- NVS `calamaio-gb`: `pad` (u8 0/1, piano A), `rom` (registro di 62 byte), `prof` (0 Pulito, 1 Fluido), `clean` (u8 minuti) (piano B).

## B: memoria

Nuova `firmware/partitions_gb.csv` (flash via cavo una volta, poi OTA come oggi):

| nome | tipo | sottotipo | offset | dimensione |
|---|---|---|---|---|
| nvs | data | nvs | 0x9000 | 0x5000 |
| otadata | data | ota | 0xe000 | 0x2000 |
| app0 | app | ota_0 | 0x10000 | 0x240000 |
| app1 | app | ota_1 | 0x250000 | 0x240000 |
| gbrom | data | 0x40 | 0x490000 | 0x200000 |
| spiffs | data | spiffs | 0x690000 | 0x160000 |
| coredump | data | coredump | 0x7f0000 | 0x10000 |

NVS invariata (chiavi, abbinamento). LittleFS si riformatta (è una cache: avatar e accessori tornano dal servizio). Salvataggi in LittleFS `/gb/<id>.sav`. RAM della cartuccia sulla scheda ≤ 32 KB: oltre, «Gioco non supportato».

## B ↔ C: protocollo (`docs/PROTOCOLLO.md`, sezione nuova «Giochi», scritta dal piano C; tutti solo autenticati, facoltativi e compatibili)

**Formati**
- `id` = 16 cifre esadecimali minuscole = primi 16 caratteri dello SHA-256 della ROM.
- `size` della ROM: 0x150..0x200000 (336..2 097 152) byte.
- token (`token`, `saveToken`): `[A-Za-z0-9_-]{1,64}`; il servizio li genera di 32 cifre esadecimali minuscole (16 byte casuali); il token di `gb-save` lo crea la scheda: 32 cifre esadecimali minuscole (16 byte casuali), mai riusato.
- `crc32` = u32, CRC-32 IEEE, intero JSON.
- `title` = ASCII stampabile (0x20..0x7E), 1..16 caratteri.
- `gb-list` al massimo **64** giochi (la scheda ne mostra fino a 16 in Menu › Gioco › ROM: le altre si contano come scartate nel registro).
- salvataggio: 1..131 136 byte (128 KB + 64 dell'orologio MBC3) sul servizio; la scheda ne usa al massimo 32 KB.
- File accettati dal servizio: `.gb` e `.gbc`; decide l'intestazione; rifiutati i giochi solo Color (0x143 = 0xC0).

**Mac.** I messaggi servizio → display portano `mac` = HMAC-SHA256(SK, etichetta ␟ campi) troncato a 16 byte, `␟` = 0x1F, interi in decimale senza zeri iniziali, stringhe tali e quali. **Obbligatori**: la scheda li verifica e scarta (con una riga di registro) un messaggio senza `mac` o con `mac` sbagliato. I messaggi display → servizio non hanno mac (come `ota-status`).
- `gb-list`: `"gb-list" ␟ id₁ ␟ size₁ ␟ title₁ ␟ id₂ ␟ size₂ ␟ title₂ ␟ …` nell'ordine dell'elenco; elenco vuoto = `"gb-list"`.
- `gb-start`: `"gb-start" ␟ id ␟ size ␟ sha256 ␟ token ␟ saveToken` (`saveToken` assente = campo vuoto).
- `gb-save-ok`: `"gb-save-ok" ␟ id ␟ crc32`.
- Vettori (SK = 32 × 0x02, verificati con Python `hmac`): `gb-list` [`3b5f0c9a1d2e4f60`, 32768, «HOMEBREW DEMO»; `a1b2c3d4e5f60718`, 65536, «Puzzle»] → `f6d53e39660c529c836fafb597a847e8`; `gb-list` vuoto → `c41604f236b545be2f894c2bb645b6e1`; `gb-start` (`3b5f0c9a1d2e4f60`, 32768, sha `3b5f0c9a1d2e4f607c1e9d22a8b04f3165e0d7a9c2b18f4e5d6a7b8c9d0e1f20`, token `5d2c8e1f0a9b4c3d6e7f8091a2b3c4d5`, nessun saveToken) → `79de567f8a40b0efdf73f04badd19aa6`; stesso con saveToken `9e8d7c6b5a4f3e2d1c0b0a9988776655` → `d57133ff50a083ee56d6005253591fff`; `gb-save-ok` (`3b5f0c9a1d2e4f60`, 305419896) → `7f690a4c01c28ed6e0f773b54866f6ef`.

**Messaggi**
- `gb-list` servizio → display: `{games:[{id, title, size}], mac}` — `title` dall'intestazione della cartuccia o dal nome del file; inviato dopo `auth-ok` e a ogni cambio della cartella.
- `gb-pick` display → servizio: `{id}` — scelta dalla scheda; risposta `gb-start` (se il gioco non c'è più: `gb-list`).
- `gb-start` servizio → display: `{id, size, sha256, token, saveToken?, mac}` — avvio (dalla webapp, dalla CLI o in risposta a `gb-pick`). Se la scheda ha già quella ROM (sha256 uguale nell'area `gbrom`) parte subito; altrimenti la scarica con `GET http://<host>:8766/gb/rom/<token>` con `Range` per la ripresa (stesso schema di `/ota/{token}`), verifica lo SHA-256 e solo allora parte. Con `saveToken` la scheda scarica prima il salvataggio da `GET http://<host>:8766/gb/save/<saveToken>` e sostituisce il suo.
- `gb-save` display → servizio: `{id, size, crc32, token?}`. Con `token`: la scheda fa `PUT http://<host>:8766/gb/save/<token>` **300 ms** dopo il `gb-save` e ritenta **2 volte** su 404 (700 ms fra un tentativo e l'altro); il servizio risponde `gb-save-ok {id, crc32, mac}`. **Senza `token`** è un annuncio: il servizio risponde subito `gb-save-ok` se ha già una copia uguale (stesso `crc32` e stessa dimensione), altrimenti niente. La scheda lo manda a ogni connessione autenticata e all'uscita dal gioco se la RAM della cartuccia è cambiata.
- `gb-state` display → servizio: `{playing: id|null}` — per la webapp e per la ripresa dell'OTA.
- `gb-pad` display → servizio: `{link: off|pairing|connecting|ready}` — per la webapp.

**HTTP: porta dei display 8766**, stesso host della connessione WebSocket della scheda, **mai 8765** (API locale, solo 127.0.0.1).
- Token della ROM: valido **10 minuti**, legato alla scheda (solo l'IP della sua connessione WebSocket), riusabile per richieste `Range` finché la consegna completa non è finita, poi morto (410). Token sconosciuto 404, scaduto o consegnato 410, IP diverso 403 (il token resta valido).
- Su 403/404/410 la scheda manda `gb-pick {id}` **una volta per gioco**; il servizio risponde con un `gb-start` nuovo e la scheda riprende dal punto salvato.
- Token del salvataggio (`gb-save`): il servizio accetta `PUT` dall'IP della scheda per quel token **finché un caricamento completo non riesce** (dimensione e CRC uguali a `gb-save`), entro **10 minuti**; un caricamento fallito (400) non consuma il token; dopo il successo 410.
- `saveToken` del ripristino: 10 minuti, solo l'IP della scheda, consegnato tutto una volta, poi 410.

## OTA e partita

- Durante la partita (caricamento, gioco o pausa: `Ui::gbOnStack()` del piano B) la scheda **rifiuta `ota-offer`** con `ota-status {phase: "rifiutato", error: "partita in corso: riprova a fine partita"}` (a schermo e nella webapp: «rifiutato: partita in corso: riprova a fine partita»), senza coprire la partita.
- La scheda manda `gb-state {playing: null}` a ogni fine: uscita dalla partita, caricamento annullato, fallito o senza risposta del PC.
- Il servizio, ricevuto quel rifiuto, tiene l'immagine e **rifà l'offerta una volta** quando la stessa scheda manda `gb-state {playing: null}`, se intanto non è partito un altro aggiornamento.
- `POST /api/displays/{board}/gb/start` risponde **409** mentre su quella scheda c'è un OTA in corso.

## C: servizio

Pacchetto `service/internal/gbgames`: cartella `<dati>/calamaio/games/` (`<id>.gb` + `index.json`), salvataggi `<dati>/calamaio/games/saves/<id>/` (ultime 3 copie). API locale: `GET /api/gb/games`, `POST /api/gb/games` (caricamento, `.gb` o `.gbc`, ≤ 2 MiB, intestazione valida, rifiuta CGB-only), `DELETE /api/gb/games/{id}`, `POST /api/displays/{board}/gb/start {id}` (409 durante un OTA), `GET /api/gb/saves/{id}`, `POST /api/gb/saves/{id}/restore {n, board?}`. Comandi `calamaio gb list|add|rm|start|saves`. Validità dei token dei giochi: `Hub.GbTokenTTL` = 10 minuti (il `Hub.TokenTTL` dell'OTA resta 5 minuti).

## Punti aperti per l'utente

1. **Due «Gioco» nel firmware**: il minigioco «Alto o basso» (prima «Gioco: alto o basso» in Strumenti) ora sta dentro Menu › Gioco; in Menu › Display resta la voce «Gioco della mascotte» (impostazione remota `game`) e c'è la schermata «La mascotte». Va bene che «Gioco» indichi sia il menu dei giochi sia, in Display, il gioco della mascotte?
2. **Niente interruttore del Bluetooth**: col controller associato il Bluetooth resta acceso (ricerca 2 minuti dopo ogni tasto, poi 3 s ogni 30 s a batteria: piano A, Task 9) finché non si sceglie «Dimentica controller». Serve anche una voce per spegnerlo senza dimenticare il controller?
