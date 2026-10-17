# App iPhone, fase P3: il telefono come controller (firmware 0.14.0 e scheda «Controller») — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** L'iPhone, con la scheda «Controller» dell'app aperta, fa da secondo controller Bluetooth della scheda (menu, schermate e Game Boy come l'Xbox), associato con un codice a 6 cifre mostrato sull'e-ink, e non può mai confermare CONSENTI a un permesso, né Proteggi le chiavi, il Ripristino di fabbrica o l'abbinamento della scheda con un PC (R1), né «Dimentica abbinamento» o un cambio del Wi-Fi (F11). Nella stessa 0.14.0 gli avatar arrivano a pezzi come i temi, senza il blocco da 9,5 KB che oggi fa ricollegare la scheda una volta al secondo (R4), e il servizio riapre da solo la porta dei telefoni occupata all'avvio (R2).

**Architecture:** Firmware: la parte pura in `core/` (decodifica del report v1 e mantenimento in `core/pad_phone`, libro degli abbinamenti per tipo, filtro di ricerca e politica di sicurezza in `core/pad_bonds`, tipo nell'associazione in `pad::LinkMachine`, regola §7 in `Ui::confirmPerm`/`Ui::emitPerm`), provata in `env:native`; `hw/blepad` la usa per un secondo tipo di dispositivo (servizio GATT «Calamaio pad» invece di HID, LE Secure Connections con passkey solo per il telefono, configurazione di sicurezza scritta prima di ogni collegamento); `main.cpp` collega codice a schermo, toast, sorgente degli eventi e righe di memoria. App: `CalamaioKit` ha codifica del report, disposizione dei tasti, tocchi multipli e il `PadController` dietro una cucitura `PadPeripheral` (provati con `swift test`, il simulatore non ha Bluetooth); l'app ha l'adattatore `CBPeripheralManager`, la scheda «Controller» e la vibrazione. Un file di vettori condiviso (`protocol/vectors/pad-report-v1.json`, scritto da Node) lega firmware e app. Avatar a pezzi (R4): il servizio offre `avatar-offer {id, crc, size, token}` alle schede che in `hello` dicono `caps: ["avatar-dl"]`, la scheda scarica `GET /avatars/<token>` sulla 8766 a pezzi da 512 byte nel temporaneo della cache in LittleFS con gli stessi controlli di `parseAvatar` fatti man mano (`CavtStream`), il loop rinomina il file nel posto della cache (`AvatarCache::adopt`) e la scheda risponde `avatar-ack`; la parte pura (`gfx/avatar`, `core/avatars`, `core/close_storm`) è provata in `env:native`, quella del servizio con `go test`; il download riusa quello dei temi (`fetchBlob` in `net.cpp`, regole del token in `blobTokenStatus`).

**Tech Stack:** C++17 (PlatformIO, `env:native` con Unity e ArduinoJson 7.4.3; `env:heltec213` con NimBLE-Arduino 2.5.1, Arduino-ESP32 di `espressif32@6.9.0`); Node 22 per il generatore dei vettori; Swift 6.4 in modo di linguaggio 5, SwiftUI, UIKit (tocchi), CoreBluetooth, CoreGraphics, XCTest; Xcode 27, simulatore iOS 27.0, XcodeGen 2.45.4.

**Spec:** `docs/superpowers/specs/2026-10-16-app-iphone-design.md` §2 (telefono come controller), §6 (firmware), §7 (prove), §8 punto 4. **Decisioni del coordinatore** (registro `.superpowers/sdd/2026-10-16-app-iphone-P3-controller/progress.md` e pre-flight `preflight.md`): R1 (più conferme vietate al telefono), R2 (Task 0), R3 (RAM di riferimento), R4 (avatar a pezzi, Task 7–9), F1–F12 e le note A1–A3, D1–D4 del pre-flight, già dentro i task qui sotto. **Prerequisito:** albero pulito sul ramo `app-iphone` nel worktree `~/CalamaiOS-iphone` (P1 e P2 fatte, `abefa36..b537227`); prima di cominciare passano `cd firmware && pio test -e native 2>&1 | tail -5`, `cd service && go test ./... 2>&1 | tail -5` e `cd ios/CalamaioKit && swift test 2>&1 | tail -5`.

## Fatti verificati (2026-10-16, sorgenti di NimBLE-Arduino 2.5.1 in `CalamaiOS-editor/firmware/.pio/libdeps/heltec213/NimBLE-Arduino`)

- `NimBLEDevice::setSecurityAuth(bond, mitm, sc)` e `setSecurityIOCap(io)` scrivono solo `ble_hs_cfg.sm_bonding/sm_mitm/sm_sc/sm_io_cap`, globali; `ble_sm.c` li legge quando **costruisce la richiesta di abbinamento** (`ble_sm_build_authreq`, `cmd->io_cap = ble_hs_cfg.sm_io_cap`), non all'avvio. Con una sola connessione (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`) e un solo task che si collega, scriverli **prima di ogni `connect()`** dà una sicurezza per tipo. Esiste anche `ble_hs_cfg.sm_sc_only`, ma `ble_sm.c` lo guarda solo quando la scheda **risponde** a un abbinamento (riga ~1934); da **iniziatore** (`ble_sm_pair_rsp_rx`) controlla solo la lunghezza della chiave, e un legacy con passkey dà `authenticated = 1` e chiave 16 (F5). Si accende lo stesso per il telefono, ma «solo SC» lo garantisce la scheda leggendo `ble_store_read_peer_sec(...).sc` dopo la cifratura (`linkSecure(..., sc)`, Task 4 e 10).
- `NimBLEClient::secureConnection()` sincrona aspetta l'esito con `BLE_NPL_TIME_FOREVER` (`NimBLEClient.cpp:350–357`): finché il codice non arriva o lo SMP non scade (30 s) il task non vede «annulla». `secureConnection(true)` parte e torna subito; l'esito positivo arriva in `NimBLEClientCallbacks::onAuthenticationComplete` (`BLE_GAP_EVENT_ENC_CHANGE` con stato 0), quello negativo no: per il telefono la scheda aspetta a giri di 20 ms con un tempo limite (F6, Task 10).
- Passkey: con `BLE_SM_IOACT_DISP` NimBLE chiama `NimBLEClientCallbacks::onPassKeyDisplay(NimBLEConnInfo&)` **solo se** il passkey globale è ancora quello di serie (123456): non si chiama mai `setSecurityPasskey`. `NimBLEConnInfo` dà `isEncrypted()`, `isAuthenticated()`, `getSecKeySize()`, `getIdAddress()` (indirizzo d'identità, `peer_id_addr`).
- Xbox con la scheda DisplayOnly ricadrebbe comunque in Just Works (tabella dei metodi), ma non serve: per l'Xbox la configurazione resta **identica alla 0.13.0-ed** (NoInputNoOutput, bonding, niente MITM, niente SC).
- `CONFIG_BT_NIMBLE_MAX_BONDS` = 3 (di serie, `nimconfig.h`): un Xbox, un telefono e quello nuovo in transito (il nuovo si salva prima di cancellare il vecchio).
- **Indirizzi privati dell'iPhone**: iOS annuncia con un indirizzo privato risolvibile che cambia ~ogni 15 minuti. Con LL privacy (ESP32-S3: `MYNEWT_VAL_BLE_HOST_BASED_PRIVACY` = `CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY`, non definito → 0) NimBLE mette gli IRK salvati nella lista del controller (`ble_hs_misc_restore_irks`, `ble_store` alla scrittura) ma **accende la risoluzione solo con un indirizzo proprio privato** (`ble_hs_pvcy_ensure_started`); la scheda usa `BLE_OWN_ADDR_PUBLIC`. Senza un intervento il telefono associato non sarebbe mai riconosciuto. Il piano chiama `ble_hs_pvcy_set_resolve_enabled(1)` (simbolo non statico, C) dopo la sincronizzazione; la prova dal vivo L4 lo verifica.
- Oggi (`hw/blepad.cpp:188-191`) dopo il collegamento si cancellano gli abbinamenti diversi dall'indirizzo **della ricerca**: per un iPhone (indirizzo privato ≠ identità) cancellerebbe l'abbinamento appena creato. Il piano usa l'indirizzo d'identità dopo la cifratura.
- `pad::State` oggi non ha la sorgente; la versione sta in `firmware/src/core/model.h` (`CALAMAIO_FW_VERSION "0.13.0-ed"`), controllata da `test_net`.
- RAM statica della 0.13.0-ed in DECISIONI: **168 936 byte** (.bss 145 192 + .data 23 744), immagine 1 945 824 byte, limite dello slot 2 359 296. L'ELF 0.13.0-ed misurato dal pre-flight (cartella `CalamaiOS-editor`) dà **168 976** (.bss 145 232): il tetto si conta dalla base misurata nello stesso ambiente (F10, Task 11). Prototipo di tutto il piano (copia, `pio run -e heltec213`): **169 192 byte** (.bss 145 448 + .data 23 744), immagine 1 965 648 byte: +72 il controller, +136 l'avatar a pezzi e le chiusure a raffica, +8 F5–F7.
- **Avatar dal vivo (0.13.0-ed, Gufo, registro in `scratchpad/ios/gufo-debug.log`)**: il messaggio `avatar` (CAVT 7 072 B = 9 432 caratteri in base64) chiede alla scheda un blocco da 9 491 B per il payload (WebSockets 2.7.3, `WebSockets.cpp:455` `malloc(payloadLen + 1)`) e poi ~8 200 B per il documento JSON. Col Bluetooth acceso: libero 18–23 KB, blocco massimo 7,6–13,8 KB → `malloc` fallisce → la libreria chiude con **1011** (`clientDisconnect(client, 1011)`), ma il codice non arriva a `WStype_DISCONNECTED` (payload vuoto): la scheda lo vede solo dal contatore delle allocazioni fallite (`onAllocFailed`, già in `net.cpp`). Poi si ricollega subito e il servizio rimanda l'avatar alla connessione nuova (la difesa dei 3 s vale per connessione): 100 connessioni in 10 minuti. Gli avatar predefiniti funzionano solo perché sono già nella cache.
- LittleFS sostituisce un file esistente con `rename` in modo atomico (lo usano già i temi, `hw/theme_fs.cpp:117`): il CAVT scaricato in `/av/dl` diventa `/av/<n>.cavt` con una rinomina sola.

## Global Constraints

- **Regole dei permessi (brief §7) e scelta A (spec §2.3)**: dal telefono si naviga, si apre un permesso, si sceglie con le frecce e si conferma **NEGA**; un HoldFire con CONSENTI selezionato che viene dal telefono **non manda mai** `perm-reply` con `allow`: la scheda resta sul permesso e mostra «Per consentire usa il tasto PRG o il controller». La regola sta in `Ui::confirmPerm`/`Ui::emitPerm` (dove nasce la risposta), non nella mappatura; `onPerm` in `main.cpp` la ricontrolla. Le domande (`input`) restano non rispondibili. Nessuna approvazione automatica. La sorgente degli eventi è quella dello stato che li ha prodotti (`State::src`), mai il tipo del collegamento di adesso (F4); il tenuto iniziato dal telefono resta del telefono anche quando lo scatto arriva dal tick (`UiState::holdPhone`, F2).
- **Altre conferme vietate al telefono (R1, F11; deviazione dalla spec §2.3, più restrittiva, e spec §0 «niente Wi-Fi né abbinamento della scheda dal telefono»)**: dal telefono **Proteggi le chiavi**, **Ripristino di fabbrica**, la **conferma dell'abbinamento col PC**, **«Dimentica abbinamento»**, **«Dimentica tutte le reti»**, **Reti salvate › Dimentica**, **«Configura da telefono»** e **una rete di Cerca reti** (protetta o aperta) si vedono (si aprono, si armano, «conferma?») ma non partono: toast «Per confermare usa il tasto PRG o il controller». Tre barriere come per CONSENTI: prima dove nasce l'azione (`Ui::confirmAction`, `Ui::pairConfirm`, `doAct`, `activate`, Cerca reti in `act`, con `Ui::phoneRefused`), seconda in `Ui::emitNet` (`phoneForbidden(NetCmd)`: `ProtectKeys`, `FactoryReset`, `PairConfirm`, `Unpair`, `Forget`, `ForgetNet`, `Provision`, `Join`), terza in `onNetRequest` di `main.cpp` (`NetRequest::src`). **Riavvia** e Cerca reti restano ammessi. Per quelle voci servono il PRG o l'Xbox.
- **Servizio GATT** (spec §2.1): servizio `526389E4-B789-4225-990D-0465AFB610B2` (nome locale «Calamaio»); stato dei tasti `6A34D990-D046-4834-96B8-0CBDCE4087FA` (notify, lettura cifrata, report v1); informazioni `9D00F77E-BB83-4667-A37F-6FBDB0F0F324` (lettura cifrata, `ver u8 = 1` + versione dell'app «x.y.z», al massimo 15 byte).
- **Report v1**: 8 byte little endian `ver=1, seq, buttons u16 (bit di pad::State: A=1<<0 … Right=1<<15), lx i8, ly i8 (−100..100), flags (bit0 = primo piano), rsv=0`; lunghezza ≠ 8 o versione ≠ 1: scartato con riga nel registro, mai interpretato. Un report a ogni cambiamento e uno ogni **1 s**; la scheda dopo **3 s** senza report, o allo scollegamento, considera **tutto rilasciato** (una tenuta finisce con HoldUp, mai Select né HoldFire).
- **Associazione**: Menu › Gioco › Controller › «Associa telefono», finestra di **2 minuti** (`pad::LinkMachine::kPairMs`); durante la finestra un dispositivo non associato è accettato solo se annuncia il servizio «Calamaio pad» ed è collegabile. Telefono: LE Secure Connections **solo SC**, MITM, scheda **DisplayOnly**, passkey di 6 cifre sull'e-ink («Codice per il telefono»), collegamento accettato solo **cifrato + autenticato + chiave da 16 byte + Secure Connections** (letto dall'archivio delle chiavi, F5); altrimenti scollega e «Associazione non riuscita». Xbox: Just Works come la 0.13.0-ed.
- **Abbinamenti**: al massimo **due, uno per tipo**, nella NVS `calamaio-gb`: `padx` (Xbox) e `padp` (telefono), 7 byte = indirizzo d'identità (6) + tipo d'indirizzo (0 pubblico, 1 casuale); `padv` = 1 dopo la migrazione dalla 0.13. Associare un telefono sostituisce solo il telefono, mai l'Xbox, e viceversa. «Dimentica controller» e «Dimentica telefono» separati.
- **Uno alla volta**: la scheda si collega al primo dispositivo associato che trova; nessuna ricerca mentre è collegata.
- **Testi a schermo nuovi** (in italiano, non nel simulatore): toast «Metti l'app Calamaio sulla scheda Controller», «Telefono collegato», «Telefono scollegato», «Associazione non riuscita», «Per consentire usa il tasto PRG o il controller», «Per confermare usa il tasto PRG o il controller» (R1, F11), «Telefono dimenticato», «Nessun telefono associato»; pausa della partita «Telefono scollegato» con l'aiuto «Riapri l'app Calamaio sulla scheda Controller: si ricollega da solo. Poi Riprendi.» (F7); schermata del codice «TELEFONO», «Codice per il telefono», «Scrivilo sull'iPhone quando lo chiede», «LUNGA ANNULLA»; voci «Associa telefono», «Dimentica telefono»; valore «telefono collegato». Elenco completo e allineamento in DECISIONI (Task 11). Il simulatore in `reference/` **non si tocca**: Menu › Gioco › Controller esiste solo sulla scheda vera (`sys.padHw`), i golden non cambiano.
- **Memoria**: RAM statica al più **base misurata + 320 byte** (base = la 0.13.0-ed costruita nello stesso ambiente, F10: 168 976 nel pre-flight; tetto alzato da +256 a +320 perché R4 aggiunge +136 misurati, tutto il piano nel prototipo +216); heap alla Home col telefono collegato non sotto quello con l'Xbox di oltre 1 KB; nessuna allocazione per report; righe `[memoria] …` (heap libero, minimo, blocco massimo) a collegamento del telefono, ogni minuto da collegato e allo scollegamento. NimBLE invariato (nessun flag nuovo in `platformio.ini`).
- **Firmware 0.14.0**: `CALAMAIO_FW_VERSION "0.14.0"`; immagine sotto 2 359 296 byte. La versione cambia nel Task 11, **dopo** i Task 7–9 (avatar a pezzi): escono insieme.
- **Avatar a pezzi (R4)**: `hello.caps = ["avatar-dl"]` (capacità, non versione: vedi Task 7); a quelle schede il servizio manda `avatar-offer {id, crc, size, token}`, una alla volta per connessione, **anche senza autenticazione** (come `avatar` e `avatar-need`: un avatar non è un dato sensibile e cambia solo la cache); token di 16 byte casuali, monouso, solo per l'IP di quella connessione, 5 minuti, morto con l'ack, la chiusura della connessione o 30 s senza ack. `GET /avatars/<token>` sulla 8766 (regole dei temi: 404, 410, 403). La scheda scarica nel task di rete a pezzi da 512 byte in `/av/dl` (niente base64, niente blocco da 9,5 KB: nessuna allocazione oltre ai 512 byte sullo stack e a quelle di `HTTPClient`), controlla man mano (`CavtStream`), il loop rinomina nella cache e risponde `avatar-ack {id, crc, ok, error?}`. Le schede senza la capacità ricevono `avatar` come prima. **Difese dal ciclo**: la scheda, dalla terza chiusura breve del WebSocket (< 20 s) entro un minuto, o subito dopo un'allocazione fallita durante la connessione, aspetta 5, 10, 20, 30 s prima del primo tentativo (`core/close_storm`); il servizio non rimanda lo stesso avatar (id e CRC) alla stessa scheda per **60 s** dopo una consegna fallita (ack negativo, nessun ack, connessione chiusa a metà, o `avatar` seguito da una chiusura entro 10 s), anche su connessioni nuove, per id della scheda (`hello.board`).
- **Servizio**: `cd service && go test ./...` verde dopo i Task 0 e 7; `go vet ./...` pulito.
- **App**: iOS 27.0, modo Swift 5; il controller funziona **solo in primo piano**: prima del secondo piano un report «tutto rilasciato» (flags bit0 = 0), poi niente annuncio. iOS non chiude il collegamento: lo chiude la scheda 5 s dopo un report di secondo piano senza report di primo piano, o dopo 10 s senza report validi, e torna a cercare (F7, `pad::PhoneIdle`). Avviso «La scheda usa un altro controller: spegnilo per usare il telefono» dopo **10 s** senza collegamento. Vibrazione leggera a ogni pressione, disattivabile.
- **Lingua**: prosa e testi in italiano; identificatori in inglese; commenti brevi in italiano.
- **Uscite**: ogni comando di prova finisce con `2>&1 | tail -n 40` (o `| grep …`).
- **Commit**: piccoli, in italiano, **senza** riga `Co-Authored-By`; niente push né merge.
- **Scheda**: nessun flash, nessuna NVS toccata nei Task 0–14. Il flash (OTA, permesso già dato dall'utente) e le prove sono nella sezione «Prove dal vivo», **dopo i backup**.

## Review Focus

- **Abbassamento della sicurezza del telefono**: un telefono che si presenta con un abbinamento non autenticato (Just Works, legacy anche con passkey, chiave corta), o che arriva mentre la configurazione è quella dell'Xbox, non deve mai diventare «Telefono collegato»; l'Xbox deve continuare ad associarsi in Just Works — `test_sec_for_kinds`, `test_link_secure_phone_needs_all_three`, `test_link_secure_phone_needs_sc` (Task 4, F5), configurazione scritta in `connectFound` prima di `connect()` e `storedSc` dopo la cifratura (Task 10), prove L3, L3b, L2.
- **Associazione annullata o sbagliata (F6)**: «LUNGA ANNULLA» o `end()` durante l'attesa del codice non lasciano un telefono associato, nemmeno se il codice arriva dopo; un codice sbagliato chiude la finestra; il codice a schermo tiene sveglia la scheda e il tenuto lì non mette Riposo (F9) — `securePhone` e i controlli dopo (Task 10), `test_padcode_hold_does_nothing`, `test_power_inputs_from_model` (Task 6), prove L3b, L3c.
- **Un abbinamento sovrascritto fra tipi**: associare un telefono nuovo non cancella l'Xbox (e viceversa), un indirizzo privato dell'iPhone non fa cancellare l'abbinamento appena creato, «Dimentica telefono» non tocca l'Xbox anche a radio spenta, la 0.13.0-ed aggiornata tiene il suo Xbox (a radio spenta e accesa con la stessa regola, D2) — `test_new_phone_keeps_xbox`, `test_same_address_moves_kind`, `test_stale_bonds`, `test_migrate_legacy_once`, `test_legacy_is_xbox` (Task 3), `test_forget_phone_keeps_controller` (Task 6), prove L1, L11.
- **Aggiramento della regola §7 e di R1/F11**: qualunque sequenza di eventi del telefono (tenuto, rilascio, B, riapertura, scelta cambiata a metà) non produce mai un `allow`, né `protect-keys`, `factory-reset`, `pair-confirm`, `unpair`, `forget`, `forget-net`, `provision`, `join`; il PRG e l'Xbox consentono e confermano come prima; Riavvia resta ammesso — `test_phone_never_allows_any_sequence` (4 096 sequenze dal Permesso e da ognuna delle otto conferme vietate, con la rete accesa e la lista dei comandi vietati scritta nella prova), `test_phone_hold_consenti_refused` (con `holdOk` guardato prima del rilascio, A1), `test_phone_hold_fire_from_tick_refused`, `test_prg_allows_after_phone_refusal`, `test_perm_event_src`, `test_phone_never_factory_reset`, `test_phone_never_protects_keys`, `test_phone_never_confirms_pc_pairing`, `test_phone_never_unpairs`, `test_phone_never_forgets_all_networks`, `test_phone_never_forgets_a_network`, `test_phone_never_provisions`, `test_phone_reboot_allowed`, `test_phone_forbidden_list` (Task 5), `test_phone_never_sends_protect_factory_pairconfirm` in `test_m5_ui` (Task 5), terza barriera in `onNetRequest` (Task 11), prove L6, L6b.
- **Tasti bloccati**: app chiusa a forza, telefono fuori portata, app in secondo piano o coda di CoreBluetooth piena con A o la croce premuti: entro 3 s tutto rilasciato, mai un HoldFire «fantasma» (Riposo o conferma) né un Select; il telefono in secondo piano non tiene occupata la scheda (F7) — `test_keepalive_releases_hold_without_fire`, `test_background_flag_releases` (Task 2), `test_phone_idle_rules` (Task 10), `testResignSendsReleasedThenStops`, `testFullQueueResendsLatest` (Task 13), prove L7, L8, L12.
- **Frammentazione dello heap**: 20 collegamenti e scollegamenti del telefono, o il passaggio Xbox ↔ telefono, non devono far scendere il blocco massimo sotto quello che serve alla partita (driver veloce 16 KB a blocchi, RAM della cartuccia a pezzi da 2 KB) — nessuna allocazione per report (coda fissa da 64), righe `[memoria]` a ogni collegamento e scollegamento (Task 11), prove L9, L10.
- **Avatar a pezzi e ciclo di riconnessione (R4)**: nessun blocco da 9,5 KB né base64 per un avatar verso una scheda 0.14.0; un CAVT non valido, troncato, di un altro id o CRC non entra mai nella cache (stessi esiti di `parseAvatar`, byte per byte); la scheda non si ricollega a raffica; il servizio non rimanda a ciclo lo stesso avatar alla stessa scheda; la scheda vecchia riceve ancora `avatar` — `TestAvatarOfferFlow`, `TestAvatarWithoutCapsStaysLegacy`, `TestAvatarFailHoldAcrossConnections`, `TestAvatarConnClosedMidOffer`, `TestAvatarLegacyCloseHeld`, `TestAvatarAckTimeout`, `TestAvatarTokenAndAckRules` (Task 7), `test_cavt_stream_matches_parse`, `test_download_then_adopt`, `test_download_rejects`, `test_avatar_offer_and_ack_messages`, `test_close_storm` (Task 8), chi tocca `/av/dl` e quando (Task 9), prova L13 col Gufo.

---

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `service/internal/server/app.go`, `server.go`, `app_test.go` | porta dei telefoni occupata all'avvio: si riprova (R2) | 0 |
| `protocol/vectors/gen-pad-vectors.mjs`, `protocol/vectors/pad-report-v1.json` | vettori del report v1 (Node) | 1 |
| `firmware/src/core/pad.h`, `pad.cpp` | `pad::Kind`, sorgente in `pad::State`, associazione per tipo in `LinkMachine` | 2, 4 |
| `firmware/src/core/pad_phone.h`, `pad_phone.cpp` | decodifica del report v1, mantenimento 3 s, telefono in secondo piano (`PhoneIdle`) | 2, 10 |
| `firmware/src/core/pad_bonds.h`, `pad_bonds.cpp` | indirizzi, libro degli abbinamenti, migrazione (`legacyIsXbox`), filtro di ricerca, sicurezza per tipo (`linkSecure` con SC) | 3, 4 |
| `firmware/test/test_pad_phone/test_main.cpp`, `test/test_pad_bonds/test_main.cpp`, `test/test_pad/test_main.cpp` | prove native | 2, 3, 4, 10 |
| `firmware/src/core/ui.h`, `ui.cpp`, `ui_pad.cpp`, `model.h`, `model.cpp`, `menu.h`, `menu.cpp`, `power.cpp` | regola §7, R1 e F11 (tre barriere), voci e toast del telefono, schermata del codice, codice che tiene sveglia la scheda | 5, 6 |
| `firmware/src/ui/screens.cpp` | disegno della schermata del codice | 6 |
| `firmware/test/test_pad_ui/test_main.cpp`, `test/test_m5_ui/test_main.cpp`, `test/test_power/test_main.cpp` | prove dell'interfaccia | 5, 6 |
| `protocol/schema/message.schema.json`, `protocol/examples/avatar-offer.json`, `avatar-ack.json`, `avatar-ack-errore.json`, `hello-avatar-dl.json` | `hello.caps`, `avatar-offer`, `avatar-ack` | 7 |
| `service/internal/display/avatar_dl.go`, `avatar_dl_test.go`, `avatar.go`, `hub.go`, `remote.go`, `themes.go`, `schema_test.go` | offerta degli avatar, `GET /avatars/<token>`, fermo di 60 s per scheda, regole del token in comune coi temi | 7 |
| `firmware/src/gfx/avatar.h`, `avatar.cpp` | `CavtStream`: i controlli di `parseAvatar` a pezzi | 8 |
| `firmware/src/core/avatars.h`, `avatars.cpp`, `theme_proto.h`, `theme_proto.cpp`, `apply_state.h`, `apply_state.cpp`, `ui.h`, `ui.cpp` | `AvatarDownload`, `AvatarCache::adopt`, `avatar-offer`/`avatar-ack`, `NetCmd::AvatarAck`, aiuti in comune coi temi | 8 |
| `firmware/src/core/close_storm.h`, `close_storm.cpp`, `firmware/test/test_close_storm/test_main.cpp`, `firmware/test/test_avatar/test_main.cpp` | chiusure del WebSocket a raffica; prove degli avatar a pezzi | 8 |
| `firmware/src/hw/avatar_fs.cpp`, `firmware/src/net/net.h`, `net.cpp`, `firmware/src/main.cpp` | `/av/dl`, download nel task di rete (`fetchBlob` in comune coi temi), `hello.caps`, attesa dopo le chiusure brevi, adozione nel loop | 9 |
| `firmware/src/hw/blepad.h`, `blepad.cpp` | secondo tipo «telefono» | 10 |
| `firmware/src/main.cpp`, `firmware/test/test_net/test_main.cpp` | collegamenti, sorgente degli eventi (F4), terza barriera di R1/F11, versione, memoria | 11 |
| `docs/DECISIONI.md`, `docs/PROTOCOLLO.md` | decisioni, allineamento, protocollo Bluetooth e degli avatar | 0, 9, 11, 14 |
| `ios/CalamaioKit/Sources/CalamaioKit/PadReport.swift`, `PadController.swift`, `PadLayout.swift`, `Texts.swift` | logica del controller | 12, 13 |
| `ios/CalamaioKit/Tests/CalamaioKitTests/PadReportTests.swift`, `PadReportDecode.swift`, `PadControllerTests.swift`, `PadLayoutTests.swift` | prove `swift test` (la decodifica del report sta solo qui, D1) | 12, 13 |
| `ios/App/CBPadPeripheral.swift`, `ControllerTab.swift`, `PadTouchView.swift`, `RootView.swift`, `SettingsTab.swift`, `CalamaioApp.swift`, `ios/AppUITests/ControllerTabUITests.swift` | app | 14 |

Ordine: Task 0, 1–6, 7–9 (R4), 10–14, poi le prove dal vivo. I Task 7–9 non dipendono dai Task 1–6 tranne che per `NetRequest::src` (Task 5, accanto al quale il Task 8 aggiunge `NetCmd::AvatarAck`).

---

### Task 0: Servizio: la porta dei telefoni occupata all'avvio si riprova

Difetto visto dal vivo (P1, Task 4, M2; `scratchpad/ios/prove-dal-vivo.md` punto 4): al riavvio del servizio la 8767
è ancora del processo di prima, `net.Listen` fallisce con «address already in use» e la porta non si riprova più
finché qualcuno non apre una finestra di abbinamento. L'app resta su «PC non trovato».

**Files:**
- Modify: `service/internal/server/app.go` (`appService`: `retryIn`, `retry`, `tries`; `sync`, `close`; nuove `appRetryDelay`, `scheduleRetry`, `stopRetry`)
- Modify: `service/internal/server/server.go` (`Options.AppRetry`)
- Test: `service/internal/server/app_test.go`

**Interfaces:**
- Produces: `func appRetryDelay(n int) time.Duration` → 1 s, 2 s, 5 s, poi 10 s; `Options.AppRetry func(n int) time.Duration` (nil = `appRetryDelay`; nelle prove millisecondi).
- Comportamento: con la porta voluta (`phones.Manager.Wanted()`) e `listen()` fallito, un solo tentativo in sospeso
  (`time.AfterFunc`), riga `telefoni: porta dei telefoni: … (riprovo fra 2s)`; si ferma quando la porta si apre,
  quando non è più voluta (nessun telefono e finestra chiusa) e in `close()`. `OpenPairing` resta com'è: con la porta
  occupata risponde subito 409 (la finestra si richiude e i tentativi si fermano, salvo telefoni già abbinati).
- Consumes: niente dei Task 1–14; nessun task lo consuma (va per primo).

- [ ] **Step 1: Prove che falliscono** (in fondo a `app_test.go`; `"sync/atomic"` fra gli import)

```go
func TestAppRetryDelay(t *testing.T) {
	want := []time.Duration{time.Second, 2 * time.Second, 5 * time.Second, 10 * time.Second, 10 * time.Second, 10 * time.Second}
	for n, w := range want {
		if d := appRetryDelay(n); d != w {
			t.Fatalf("tentativo %d: %s, atteso %s", n, d, w)
		}
	}
}

// busyListen: le prime fail chiamate rispondono «porta occupata», poi apre davvero (fail < 0: sempre occupata).
func busyListen(fail int, calls *atomic.Int32) func() (net.Listener, error) {
	return func() (net.Listener, error) {
		if n := calls.Add(1); fail < 0 || int(n) <= fail {
			return nil, errors.New("listen tcp :8767: bind: address already in use")
		}
		return net.Listen("tcp", "127.0.0.1:0")
	}
}

// R2: al riavvio la porta è ancora del servizio di prima. Con un telefono abbinato si riprova da sola.
func TestAppPortBusyAtStartRetries(t *testing.T) {
	dir := t.TempDir()
	files := func(o *Options) {
		o.PhonesFile = filepath.Join(dir, "phones.json")
		o.AppTLSDir = filepath.Join(dir, "app-tls")
	}
	s := startOpts(t, true, 0, files)
	pairedPhone(t, newWeb(t, s))
	s.cancel()
	err := <-s.done
	s.done <- err
	var calls atomic.Int32
	s2 := startOpts(t, true, 0, func(o *Options) {
		files(o)
		o.AppListen = busyListen(2, &calls)
		o.AppRetry = func(int) time.Duration { return 20 * time.Millisecond }
	})
	eventually(t, "porta riaperta dopo due tentativi falliti", func() bool { return s2.run.AppAddr() != "" })
	if n := calls.Load(); n != 3 {
		t.Fatalf("tentativi: %d, attesi 3", n)
	}
	if e, ok := phonesView(t, newWeb(t, s2))["app"].(map[string]any)["error"]; ok {
		t.Fatalf("errore rimasto nella vista: %v", e)
	}
}

// Nessun telefono e finestra chiusa: niente tentativi in sottofondo.
func TestAppRetryStopsWhenNotWanted(t *testing.T) {
	var calls atomic.Int32
	s := startOpts(t, true, 0, func(o *Options) {
		o.AppListen = busyListen(-1, &calls)
		o.AppRetry = func(int) time.Duration { return 20 * time.Millisecond }
	})
	w := newWeb(t, s)
	if c, _ := w.post("/api/phones/pair", `{}`); c != 409 {
		t.Fatalf("porta occupata: %d", c)
	}
	time.Sleep(50 * time.Millisecond)
	n := calls.Load()
	time.Sleep(200 * time.Millisecond)
	if m := calls.Load(); m != n || n > 2 {
		t.Fatalf("tentativi senza finestra né telefoni: %d poi %d", n, m)
	}
}

// Allo spegnimento i tentativi si fermano.
func TestAppRetryStopsOnClose(t *testing.T) {
	dir := t.TempDir()
	files := func(o *Options) {
		o.PhonesFile = filepath.Join(dir, "phones.json")
		o.AppTLSDir = filepath.Join(dir, "app-tls")
	}
	s := startOpts(t, true, 0, files)
	pairedPhone(t, newWeb(t, s))
	s.cancel()
	err := <-s.done
	s.done <- err
	var calls atomic.Int32
	s2 := startOpts(t, true, 0, func(o *Options) {
		files(o)
		o.AppListen = busyListen(-1, &calls)
		o.AppRetry = func(int) time.Duration { return 20 * time.Millisecond }
	})
	eventually(t, "almeno due tentativi", func() bool { return calls.Load() >= 2 })
	s2.cancel()
	err = <-s2.done
	s2.done <- err
	n := calls.Load()
	time.Sleep(200 * time.Millisecond)
	if m := calls.Load(); m != n {
		t.Fatalf("tentativi dopo lo spegnimento: %d poi %d", n, m)
	}
}
```

- [ ] **Step 2: Vederle fallire**

Run: `cd service && go test ./internal/server -run 'TestAppRetry|TestAppPortBusyAtStart' -count=1 2>&1 | tail -n 15`
Expected: errori di compilazione (`appRetryDelay`, `Options.AppRetry` sconosciuti).

- [ ] **Step 3: `Options.AppRetry` in `server.go`** (dopo `TuneApp`)

```go
	// AppRetry, se non nil, dà l'attesa prima del tentativo n (0, 1, …) di riaprire la porta dei telefoni
	// occupata (test: millisecondi). nil = appRetryDelay.
	AppRetry func(n int) time.Duration
```

- [ ] **Step 4: Riprova in `app.go`**

In `appService`, dopo `announce`: `retryIn func(n int) time.Duration // attesa prima del tentativo n di riaprire la porta`;
dopo `closed`: `retry *time.Timer // porta occupata: prossimo tentativo (nil = nessuno)` e `tries int`.
In `openApp`: `retryIn: o.AppRetry` nel letterale e `if a.retryIn == nil { a.retryIn = appRetryDelay }`.
In `sync`, dopo `want := …`:

```go
	if !want || a.srv != nil {
		a.stopRetry()
	}
```

e il ramo dell'errore di `a.listen()` diventa

```go
		if err != nil {
			a.lastErr = fmt.Errorf("porta dei telefoni: %w", err)
			d := a.retryIn(a.tries)
			a.lg.Printf("telefoni: %v (riprovo fra %s)", a.lastErr, d)
			a.scheduleRetry(d)
			return
		}
		a.stopRetry()
```

Nuove funzioni prima di `close()`, e `a.stopRetry()` in `close()` subito dopo `a.closed = true`:

```go
// appRetryDelay: porta occupata (di solito il servizio di prima non è ancora uscito): 1 s, 2 s, 5 s, poi ogni 10 s.
func appRetryDelay(n int) time.Duration {
	steps := []time.Duration{time.Second, 2 * time.Second, 5 * time.Second}
	if n >= 0 && n < len(steps) {
		return steps[n]
	}
	return 10 * time.Second
}

// scheduleRetry e stopRetry: con a.mu preso. Un solo tentativo in sospeso alla volta.
func (a *appService) scheduleRetry(d time.Duration) {
	if a.retry != nil {
		return
	}
	a.tries++
	a.retry = time.AfterFunc(d, func() {
		a.mu.Lock()
		a.retry = nil
		a.mu.Unlock()
		a.sync()
	})
}

func (a *appService) stopRetry() {
	if a.retry != nil {
		a.retry.Stop()
		a.retry = nil
	}
	a.tries = 0
}
```

- [ ] **Step 5: Vederle passare, con le prove vecchie della porta**

Run: `cd service && go test ./internal/server -run 'TestApp' -count=1 2>&1 | tail -n 5 && go test -race ./internal/server -run 'TestAppRetry|TestAppPortBusy' -count=3 2>&1 | tail -n 3 && go test ./... 2>&1 | tail -n 5`
Expected: `ok calamaio/internal/server` (anche `TestAppPortBusy`, `TestAppSyncAfterCloseDoesNotListen` invariati), `-race` pulito, tutto `ok`.

- [ ] **Step 6: DECISIONI**

Nella sezione «App iPhone»: «**Porta 8767 occupata** (P3 Task 0, difetto M2 di P1 visto dal vivo): con la porta voluta
e `listen` fallito si riprova da soli dopo 1, 2, 5 s e poi ogni 10 s; i tentativi si fermano quando la porta si apre,
quando non serve più e allo spegnimento. «Abbina un telefono» con la porta occupata risponde ancora subito 409.»

- [ ] **Step 7: Commit**

```bash
git add service/internal/server/app.go service/internal/server/server.go service/internal/server/app_test.go docs/DECISIONI.md
git commit -m "Servizio: la porta dei telefoni occupata all'avvio si riprova (1, 2, 5 s, poi ogni 10 s) e si ferma quando non serve"
```

Prova dal vivo (in L0.4, Ruling R2 del registro): riavviare il servizio due volte di fila; nel registro `telefoni: porta dei telefoni: … (riprovo fra 1s)`
seguito entro pochi secondi da `telefoni: HTTPS su …`; l'app si ricollega senza riabbinare.

---

### Task 1: Vettori condivisi del report v1

**Files:**
- Create: `protocol/vectors/gen-pad-vectors.mjs`
- Create: `protocol/vectors/pad-report-v1.json` (scritto dal generatore)

**Interfaces:**
- Produces: `protocol/vectors/pad-report-v1.json` con la forma
  `{"format":"calamaio-pad-report","version":1,"length":8,"bits":{"A":0,…,"Right":15},"cases":[…]}`; ogni caso valido è
  `{"name","hex","valid":true,"encode":bool,"seq","buttons","lx","ly","fg"}` (campi **dopo** la decodifica: quello che
  la scheda deve ottenere), ogni caso non valido `{"name","hex","valid":false,"reason":"lunghezza"|"versione"}`.
  `encode: true` = l'app, con `seq/buttons/lx/ly/fg`, deve produrre esattamente `hex`. Lo leggono `test_pad_phone`
  (Task 2) e `PadReportTests` (Task 12).

- [ ] **Step 1: Scrivere il generatore**

```js
#!/usr/bin/env node
// Genera protocol/vectors/pad-report-v1.json: report v1 del telefono come controller (spec app iPhone §2.1).
// Implementazione indipendente da firmware (core/pad_phone, test_pad_phone) e app (CalamaioKit/PadReport,
// PadReportTests), che leggono lo stesso file. Uso, dalla radice del repo:
//   node protocol/vectors/gen-pad-vectors.mjs           scrive il file
//   node protocol/vectors/gen-pad-vectors.mjs --check   esce con 1 se il file non è quello che scriverebbe
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, 'pad-report-v1.json');

// stessi bit di pad::State (firmware/src/core/pad.h)
const BITS = { A: 0, B: 1, X: 2, Y: 3, LB: 4, RB: 5, View: 6, Menu: 7, Guide: 8, LS: 9, RS: 10, Share: 11,
  Up: 12, Down: 13, Left: 14, Right: 15 };
const mask = (...names) => names.reduce((m, n) => m | (1 << BITS[n]), 0);

function enc({ ver = 1, seq = 0, buttons = 0, lx = 0, ly = 0, flags = 1, rsv = 0 }) {
  return Buffer.from([ver, seq, buttons & 0xff, (buttons >> 8) & 0xff, lx & 0xff, ly & 0xff, flags, rsv]).toString('hex');
}

// Regole della scheda: lunghezza 8 e versione 1, levette limitate a ±100, secondo piano = tutto rilasciato,
// rsv e bit di flags sconosciuti ignorati.
function decode(hex) {
  const b = Buffer.from(hex, 'hex');
  if (b.length !== 8) return { valid: false, reason: 'lunghezza' };
  if (b[0] !== 1) return { valid: false, reason: 'versione' };
  const clamp = (v) => Math.max(-100, Math.min(100, v));
  const fg = (b[6] & 1) === 1;
  return { valid: true, seq: b[1], buttons: fg ? b.readUInt16LE(2) : 0,
    lx: fg ? clamp(b.readInt8(4)) : 0, ly: fg ? clamp(b.readInt8(5)) : 0, fg };
}

const valid = [
  ['vuoto', { seq: 0 }, true],
  ['A', { seq: 1, buttons: mask('A') }, true],
  ['A+B', { seq: 2, buttons: mask('A', 'B') }, true],
  ['su-sinistra', { seq: 3, buttons: mask('Up', 'Left') }, true],
  ['start = Menu', { seq: 4, buttons: mask('Menu') }, true],
  ['select = View', { seq: 5, buttons: mask('View') }, true],
  ['guida', { seq: 6, buttons: mask('Guide') }, true],
  ['giù-destra', { seq: 7, buttons: mask('Down', 'Right') }, true],
  ['tutti i bit', { seq: 8, buttons: 0xffff }, false],
  ['levetta ai bordi', { seq: 9, lx: -100, ly: 100 }, true],
  ['levetta fuori scala', { seq: 10, lx: 127, ly: -128 }, false],
  ['seq 255', { seq: 255, buttons: mask('B') }, true],
  ['secondo piano con A', { seq: 11, buttons: mask('A'), flags: 0 }, false],
  ['secondo piano vuoto', { seq: 12, flags: 0 }, true],
  ['rsv e flag sconosciuti', { seq: 13, buttons: mask('A'), flags: 0x81, rsv: 0x5a }, false],
];
const invalid = [
  ['vuoto 0 byte', ''],
  ['corto 7 byte', enc({}).slice(0, 14)],
  ['lungo 9 byte', enc({}) + '00'],
  ['versione 0', enc({ ver: 0 })],
  ['versione 2', enc({ ver: 2 })],
];

const cases = [];
for (const [name, fields, encode] of valid) {
  const hex = enc(fields);
  const d = decode(hex);
  if (!d.valid) throw new Error(`${name}: dovrebbe essere valido`);
  cases.push({ name, hex, valid: true, encode, seq: d.seq, buttons: d.buttons, lx: d.lx, ly: d.ly, fg: d.fg });
}
for (const [name, hex] of invalid) {
  const d = decode(hex);
  if (d.valid) throw new Error(`${name}: dovrebbe essere scartato`);
  cases.push({ name, hex, valid: false, reason: d.reason });
}
const text = JSON.stringify({ format: 'calamaio-pad-report', version: 1, length: 8, bits: BITS, cases }, null, 1) + '\n';

if (process.argv.includes('--check')) {
  const old = readFileSync(OUT, 'utf8');
  if (old !== text) { console.error('pad-report-v1.json non aggiornato: rigenerarlo'); process.exit(1); }
  console.log(`pad-report-v1.json aggiornato (${cases.length} casi)`);
} else {
  writeFileSync(OUT, text);
  console.log(`scritto ${OUT} (${cases.length} casi)`);
}
```

- [ ] **Step 2: Provare che `--check` fallisce senza file**

Run: `cd ~/CalamaiOS-iphone && node protocol/vectors/gen-pad-vectors.mjs --check 2>&1 | tail -3`
Expected: errore `ENOENT` su `pad-report-v1.json`.

- [ ] **Step 3: Generare e ricontrollare**

Run: `node protocol/vectors/gen-pad-vectors.mjs && node protocol/vectors/gen-pad-vectors.mjs --check && grep -c '"name"' protocol/vectors/pad-report-v1.json`
Expected: `scritto …(20 casi)`, `pad-report-v1.json aggiornato (20 casi)`, `20`. A occhio: `"A"` ha `"hex": "0101010000000100"`,
`"levetta fuori scala"` ha `"lx": 100, "ly": -100`, `"secondo piano con A"` ha `"buttons": 0, "fg": false`.

- [ ] **Step 4: Commit**

```bash
git add protocol/vectors/gen-pad-vectors.mjs protocol/vectors/pad-report-v1.json
git commit -m "Controller dal telefono: vettori condivisi del report v1 (Node), letti da firmware e app"
```

---

### Task 2: Decodifica del report v1 e mantenimento (core/pad_phone)

**Files:**
- Modify: `firmware/src/core/pad.h` (`Kind`, campo `src` in `State`), `firmware/src/core/pad.cpp` (`parseXboxReport` mette `src`)
- Create: `firmware/src/core/pad_phone.h`, `firmware/src/core/pad_phone.cpp`
- Test: `firmware/test/test_pad_phone/test_main.cpp`

**Interfaces:**
- Consumes: `protocol/vectors/pad-report-v1.json` (Task 1).
- Produces (in `cal::pad`):
  - `enum class Kind : uint8_t { Xbox, Phone };` `const char *kindName(Kind k);` → `"controller"`, `"telefono"`.
  - `struct State { uint16_t buttons; int8_t lx, ly, rx, ry; uint16_t lt, rt; Kind src; };` (`sameState` non guarda `src`).
  - `const size_t kPhoneReportLen = 8; const uint8_t kPhoneReportVer = 1; const uint8_t kPhoneFlagForeground = 0x01;`
  - `enum class PhoneParse : uint8_t { Ok, BadLen, BadVer };` `const char *phoneParseName(PhoneParse p);` → `"ok"`, `"lunghezza"`, `"versione"`.
  - `struct PhoneMeta { uint8_t seq; bool fg; };`
  - `PhoneParse parsePhoneReport(const uint8_t *r, size_t n, State &out, PhoneMeta &meta);` (out e meta intatti se non Ok).
  - `class Keepalive { static const uint32_t kMs = 3000; void feed(uint32_t at); void stop(); bool on() const; bool due(uint32_t now) const; uint32_t cap(uint32_t now) const; }`
  - `bool keepaliveRelease(Keepalive &k, const State &last, uint32_t now, State &out, uint32_t &at);`

- [ ] **Step 1: Scrivere i test che falliscono**

```cpp
// Report v1 del telefono (spec app iPhone §2.1) e mantenimento 3 s: contro protocol/vectors/pad-report-v1.json.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/pad.h"
#include "core/pad_phone.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::vector<uint8_t> hexBytes(const char *s) {
  std::vector<uint8_t> v;
  for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
    char b[3] = {s[i], s[i + 1], 0};
    v.push_back(static_cast<uint8_t>(strtoul(b, nullptr, 16)));
  }
  return v;
}

static JsonDocument gDoc;

static void test_vectors_loaded() {
  const std::string js = readFile("../protocol/vectors/pad-report-v1.json");
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 500, "manca protocol/vectors/pad-report-v1.json");
  TEST_ASSERT_FALSE(deserializeJson(gDoc, js));
  TEST_ASSERT_EQUAL_STRING("calamaio-pad-report", gDoc["format"]);
  TEST_ASSERT_EQUAL(8, gDoc["length"].as<int>());
  TEST_ASSERT_EQUAL(20, gDoc["cases"].size());
}

// I bit dei vettori sono quelli di pad::Button (un'app con bit diversi muoverebbe altri tasti).
static void test_vector_bits_match_button_enum() {
  JsonObjectConst b = gDoc["bits"];
  TEST_ASSERT_EQUAL(pad::A, 1u << b["A"].as<int>());
  TEST_ASSERT_EQUAL(pad::B, 1u << b["B"].as<int>());
  TEST_ASSERT_EQUAL(pad::View, 1u << b["View"].as<int>());
  TEST_ASSERT_EQUAL(pad::Menu, 1u << b["Menu"].as<int>());
  TEST_ASSERT_EQUAL(pad::Guide, 1u << b["Guide"].as<int>());
  TEST_ASSERT_EQUAL(pad::Up, 1u << b["Up"].as<int>());
  TEST_ASSERT_EQUAL(pad::Down, 1u << b["Down"].as<int>());
  TEST_ASSERT_EQUAL(pad::Left, 1u << b["Left"].as<int>());
  TEST_ASSERT_EQUAL(pad::Right, 1u << b["Right"].as<int>());
}

static void test_vectors_decode() {
  for (JsonObjectConst c : gDoc["cases"].as<JsonArrayConst>()) {
    const char *name = c["name"];
    const std::vector<uint8_t> d = hexBytes(c["hex"]);
    pad::State s;
    memset(&s, 0x55, sizeof s);  // sporco: la decodifica deve scrivere tutto
    pad::PhoneMeta meta = {77, false};
    const pad::PhoneParse p = pad::parsePhoneReport(d.data(), d.size(), s, meta);
    if (!c["valid"].as<bool>()) {
      TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(p), name);
      TEST_ASSERT_EQUAL_STRING_MESSAGE(c["reason"].as<const char *>(), pad::phoneParseName(p), name);
      TEST_ASSERT_EQUAL_MESSAGE(77, meta.seq, name);  // intatto
      continue;
    }
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(p), name);
    TEST_ASSERT_EQUAL_MESSAGE(c["buttons"].as<int>(), s.buttons, name);
    TEST_ASSERT_EQUAL_MESSAGE(c["lx"].as<int>(), s.lx, name);
    TEST_ASSERT_EQUAL_MESSAGE(c["ly"].as<int>(), s.ly, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.rx, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.ry, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.lt, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.rt, name);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(pad::Kind::Phone), static_cast<int>(s.src), name);
    TEST_ASSERT_EQUAL_MESSAGE(c["seq"].as<int>(), meta.seq, name);
    TEST_ASSERT_EQUAL_MESSAGE(c["fg"].as<bool>(), meta.fg, name);
  }
}

static void test_null_report_rejected() {
  pad::State s;
  pad::PhoneMeta m;
  TEST_ASSERT_EQUAL(static_cast<int>(pad::PhoneParse::BadLen), static_cast<int>(pad::parsePhoneReport(nullptr, 8, s, m)));
}

static void test_xbox_report_has_xbox_source() {
  uint8_t r[16] = {0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0, 0, 0, 0, 0x01, 0, 0};
  pad::State s;
  s.src = pad::Kind::Phone;
  TEST_ASSERT_TRUE(pad::parseXboxReport(r, 16, s));
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Xbox), static_cast<int>(s.src));
  TEST_ASSERT_EQUAL(pad::A, s.buttons);
}

static void test_keepalive_due_after_3s() {
  pad::Keepalive k;
  TEST_ASSERT_FALSE(k.due(10000));  // mai alimentato
  k.feed(1000);
  TEST_ASSERT_FALSE(k.due(3999));
  TEST_ASSERT_TRUE(k.due(4000));
  k.feed(3500);  // report di mantenimento
  TEST_ASSERT_FALSE(k.due(4000));
  k.stop();
  TEST_ASSERT_FALSE(k.due(100000));
}

// Orologio della tenuta: mai oltre l'ultimo report + 3 s − 1 ms (un telefono sparito non arriva al HoldFire).
static void test_keepalive_cap() {
  pad::Keepalive k;
  TEST_ASSERT_EQUAL(50000, k.cap(50000));  // spento: nessun limite (Xbox)
  k.feed(1000);
  TEST_ASSERT_EQUAL(3000, k.cap(3000));
  TEST_ASSERT_EQUAL(3999, k.cap(4000));
  TEST_ASSERT_EQUAL(3999, k.cap(9000));
}

static void test_keepalive_release_once() {
  pad::Keepalive k;
  pad::State last;
  memset(&last, 0, sizeof last);
  last.buttons = pad::A | pad::Up;
  last.src = pad::Kind::Phone;
  pad::State out;
  uint32_t at = 0;
  k.feed(1000);
  TEST_ASSERT_FALSE(pad::keepaliveRelease(k, last, 3999, out, at));
  TEST_ASSERT_TRUE(pad::keepaliveRelease(k, last, 4200, out, at));
  TEST_ASSERT_EQUAL(0, out.buttons);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(out.src));
  TEST_ASSERT_EQUAL(3999, at);  // istante d'arrivo del «rilasciato»: prima che la tenuta arrivi a 3 s
  TEST_ASSERT_FALSE(pad::keepaliveRelease(k, last, 9000, out, at));  // una volta sola
}

static void test_keepalive_nothing_to_release() {
  pad::Keepalive k;
  pad::State last;
  memset(&last, 0, sizeof last);
  pad::State out;
  uint32_t at = 0;
  k.feed(0);
  TEST_ASSERT_FALSE(pad::keepaliveRelease(k, last, 5000, out, at));
  TEST_ASSERT_FALSE(k.on());  // spento comunque
}

// Review Focus «tasti bloccati»: A premuto, poi il telefono sparisce. Col mappatore vero e l'orologio limitato:
// la tenuta finisce con HoldUp, mai HoldFire né Select.
static void test_keepalive_releases_hold_without_fire() {
  pad::MenuMapper map;
  pad::Keepalive k;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  int fires = 0, selects = 0, ups = 0;
  int n = map.update(s, 1000, ev, 8);
  k.feed(1000);
  for (uint32_t now = 1000; now <= 8000; now += 50) {
    pad::State out;
    uint32_t at;
    if (pad::keepaliveRelease(k, s, now, out, at)) n = map.update(out, at, ev, 8);
    else n = map.tick(k.cap(now), ev, 8);
    for (int i = 0; i < n; i++) {
      fires += ev[i] == pad::MenuEvent::HoldFire;
      selects += ev[i] == pad::MenuEvent::Select;
      ups += ev[i] == pad::MenuEvent::HoldUp;
    }
  }
  TEST_ASSERT_EQUAL(0, fires);
  TEST_ASSERT_EQUAL(0, selects);
  TEST_ASSERT_EQUAL(1, ups);
}

// Il telefono vivo (mantenimento ogni 1 s) tiene A 3 s: HoldFire arriva come con l'Xbox.
static void test_alive_phone_hold_fires() {
  pad::MenuMapper map;
  pad::Keepalive k;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  map.update(s, 1000, ev, 8);
  k.feed(1000);
  int fires = 0;
  for (uint32_t now = 1000; now <= 4500; now += 50) {
    if ((now - 1000) % 1000 == 0) k.feed(now);  // report uguale: alimenta e basta
    const int n = map.tick(k.cap(now), ev, 8);
    for (int i = 0; i < n; i++) fires += ev[i] == pad::MenuEvent::HoldFire;
  }
  TEST_ASSERT_EQUAL(1, fires);
}

// App in secondo piano (flags bit0 = 0): anche con bit accesi è «tutto rilasciato».
static void test_background_flag_releases() {
  const uint8_t r[8] = {1, 9, 0x03, 0x30, 50, -50 & 0xff, 0x00, 0};
  pad::State s;
  pad::PhoneMeta m;
  TEST_ASSERT_EQUAL(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(pad::parsePhoneReport(r, 8, s, m)));
  TEST_ASSERT_EQUAL(0, s.buttons);
  TEST_ASSERT_EQUAL(0, s.lx);
  TEST_ASSERT_FALSE(m.fg);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_vectors_loaded);
  RUN_TEST(test_vector_bits_match_button_enum);
  RUN_TEST(test_vectors_decode);
  RUN_TEST(test_null_report_rejected);
  RUN_TEST(test_xbox_report_has_xbox_source);
  RUN_TEST(test_keepalive_due_after_3s);
  RUN_TEST(test_keepalive_cap);
  RUN_TEST(test_keepalive_release_once);
  RUN_TEST(test_keepalive_nothing_to_release);
  RUN_TEST(test_keepalive_releases_hold_without_fire);
  RUN_TEST(test_alive_phone_hold_fires);
  RUN_TEST(test_background_flag_releases);
  return UNITY_END();
}
```

- [ ] **Step 2: Vederli fallire**

Run: `cd firmware && pio test -e native -f test_pad_phone 2>&1 | tail -n 20`
Expected: errore di compilazione, `core/pad_phone.h` non trovato.

- [ ] **Step 3: `Kind` e sorgente in `core/pad.h`**

Prima di `enum Button` aggiungere:

```cpp
// 0.14.0: due tipi di controller (spec app iPhone §2): l'Xbox via HID e il telefono via il servizio «Calamaio pad».
enum class Kind : uint8_t { Xbox, Phone };
const char *kindName(Kind k);  // 'controller', 'telefono' (registro)
```

e in `struct State`, dopo `uint16_t lt, rt;`:

```cpp
  Kind src;                // 0.14.0: da dove viene (il telefono non dà mai CONSENTI, §7); sameState non lo guarda
```

In `pad.cpp`, in `parseXboxReport` prima di `out = s;`: `s.src = Kind::Xbox;`, e dopo `linkKey`:

```cpp
const char *kindName(Kind k) { return k == Kind::Phone ? "telefono" : "controller"; }
```

- [ ] **Step 4: Scrivere `core/pad_phone.h`**

```cpp
// Telefono come controller (spec app iPhone §2.1): report v1 del servizio «Calamaio pad» e mantenimento.
// Parte pura, provata in env:native (test_pad_phone) contro protocol/vectors/pad-report-v1.json.
//   0 ver = 1 · 1 seq · 2-3 buttons u16 LE (bit di pad::Button) · 4 lx i8 · 5 ly i8 · 6 flags (bit0 primo piano) · 7 rsv
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pad.h"

namespace cal {
namespace pad {

const size_t kPhoneReportLen = 8;
const uint8_t kPhoneReportVer = 1;
const uint8_t kPhoneFlagForeground = 0x01;

enum class PhoneParse : uint8_t { Ok, BadLen, BadVer };
const char *phoneParseName(PhoneParse p);  // 'ok', 'lunghezza', 'versione' (come «reason» dei vettori)

struct PhoneMeta {
  uint8_t seq;  // cresce a ogni report: solo per il registro, mai per scartare (gira a 255)
  bool fg;      // app in primo piano
};

// Lunghezza ≠ 8 o versione ≠ 1: scartato, out e meta intatti. Levette limitate a ±100, rsv e bit sconosciuti di
// flags ignorati; app in secondo piano = tutto rilasciato. rx, ry, lt, rt sempre 0; src = Phone.
PhoneParse parsePhoneReport(const uint8_t *r, size_t n, State &out, PhoneMeta &meta);

// Mantenimento: l'app manda un report ogni 1 s; 3 s senza = tutto rilasciato (spec §2.1).
class Keepalive {
 public:
  static const uint32_t kMs = 3000;
  void feed(uint32_t at) { at_ = at; on_ = true; }  // ora d'ARRIVO di un report valido
  void stop() { on_ = false; }                      // Xbox, scollegato, già rilasciato
  bool on() const { return on_; }
  bool due(uint32_t now) const { return on_ && now - at_ >= kMs; }
  // Orologio per MenuMapper::tick: mai oltre l'ultimo report + 3 s − 1 ms, così un telefono sparito con A giù
  // non arriva al HoldFire prima che il «rilasciato» lo chiuda con HoldUp.
  uint32_t cap(uint32_t now) const { return on_ && now - at_ >= kMs ? at_ + kMs - 1 : now; }

 private:
  uint32_t at_ = 0;
  bool on_ = false;
};

// Se è ora: spegne k e, se last aveva qualcosa premuto, scrive in out lo stato vuoto (src Phone) con l'istante
// d'arrivo at = ultimo report + 3 s − 1 ms; true solo in quel caso.
bool keepaliveRelease(Keepalive &k, const State &last, uint32_t now, State &out, uint32_t &at);

}  // namespace pad
}  // namespace cal
```

- [ ] **Step 5: Scrivere `core/pad_phone.cpp`**

```cpp
#include "pad_phone.h"

#include <string.h>

namespace cal {
namespace pad {

const uint32_t Keepalive::kMs;

const char *phoneParseName(PhoneParse p) {
  switch (p) {
    case PhoneParse::BadLen: return "lunghezza";
    case PhoneParse::BadVer: return "versione";
    default: return "ok";
  }
}

namespace {
int8_t clampStick(uint8_t raw) {
  const int v = static_cast<int8_t>(raw);
  return static_cast<int8_t>(v < -100 ? -100 : (v > 100 ? 100 : v));
}
}  // namespace

PhoneParse parsePhoneReport(const uint8_t *r, size_t n, State &out, PhoneMeta &meta) {
  if (!r || n != kPhoneReportLen) return PhoneParse::BadLen;
  if (r[0] != kPhoneReportVer) return PhoneParse::BadVer;
  State s;
  memset(&s, 0, sizeof s);
  s.src = Kind::Phone;
  const bool fg = (r[6] & kPhoneFlagForeground) != 0;
  if (fg) {
    s.buttons = static_cast<uint16_t>(r[2] | (r[3] << 8));
    s.lx = clampStick(r[4]);
    s.ly = clampStick(r[5]);
  }
  meta.seq = r[1];
  meta.fg = fg;
  out = s;
  return PhoneParse::Ok;
}

bool keepaliveRelease(Keepalive &k, const State &last, uint32_t now, State &out, uint32_t &at) {
  if (!k.due(now)) return false;
  const uint32_t when = k.cap(now);
  k.stop();
  const bool any = last.buttons || last.lx || last.ly || last.rx || last.ry || last.lt || last.rt;
  if (!any) return false;
  memset(&out, 0, sizeof out);
  out.src = Kind::Phone;
  at = when;
  return true;
}

}  // namespace pad
}  // namespace cal
```

- [ ] **Step 6: Vederli passare, e le prove vecchie del controller ancora verdi**

Run: `cd firmware && pio test -e native -f test_pad_phone -f test_pad -f test_pad_ui -f test_gb_session 2>&1 | tail -n 15`
Expected: tutti `PASSED`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/core/pad.h firmware/src/core/pad.cpp firmware/src/core/pad_phone.h firmware/src/core/pad_phone.cpp firmware/test/test_pad_phone
git commit -m "Controller dal telefono: decodifica del report v1 contro i vettori, sorgente nello stato, mantenimento 3 s senza HoldFire fantasma"
```

---

### Task 3: Libro degli abbinamenti per tipo (core/pad_bonds)

**Files:**
- Create: `firmware/src/core/pad_bonds.h`, `firmware/src/core/pad_bonds.cpp`
- Test: `firmware/test/test_pad_bonds/test_main.cpp`

**Interfaces:**
- Consumes: `pad::Kind` (Task 2).
- Produces (in `cal::pad`):
  - `struct Addr { uint8_t v[6]; uint8_t type; };` (type 0 pubblico, 1 casuale); `bool sameAddr(const Addr &a, const Addr &b);` (confronta i 6 byte e `type & 1`: un indirizzo risolto può arrivare come tipo «identità» 2/3).
  - `const size_t kBondBlob = 7;`
  - `class BondBook { bool has(Kind) const; const Addr &get(Kind) const; void set(Kind, const Addr&); void clear(Kind); bool kindOf(const Addr&, Kind&) const; bool any() const; static void encode(const Addr&, uint8_t out[kBondBlob]); static bool decode(const uint8_t *b, size_t n, Addr &out); };`
  - `int staleBonds(const BondBook &book, const Addr *bonds, int n, int *out, int cap);`
  - `bool migrateLegacy(BondBook &book, bool migrated, const Addr *bonds, int n);`
  - `bool legacyIsXbox(bool migrated, const BondBook &book, int nBonds);` (D2: la regola di `migrateLegacy`, usata anche da `blepad::bonded(Kind::Xbox)` a radio spenta, Task 10)
  - `const char *bondKey(Kind k);` → `"padx"`, `"padp"`; `extern const char *const kBondVersionKey;` = `"padv"`.

- [ ] **Step 1: Scrivere i test che falliscono**

```cpp
// Abbinamenti per tipo (spec app iPhone §2.2): un Xbox e un telefono, nella NVS calamaio-gb padx/padp.
#include <string.h>
#include <unity.h>

#include "core/pad_bonds.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static pad::Addr A(uint8_t last, uint8_t type = 0) {
  pad::Addr a = {{0x10, 0x20, 0x30, 0x40, 0x50, last}, type};
  return a;
}

static void test_empty_book() {
  pad::BondBook b;
  TEST_ASSERT_FALSE(b.has(pad::Kind::Xbox));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Phone));
  TEST_ASSERT_FALSE(b.any());
  pad::Kind k;
  TEST_ASSERT_FALSE(b.kindOf(A(1), k));
}

// Review Focus: associare un telefono nuovo sostituisce solo il telefono.
static void test_new_phone_keeps_xbox() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  b.set(pad::Kind::Phone, A(3, 1));  // telefono nuovo
  TEST_ASSERT_TRUE(b.has(pad::Kind::Xbox));
  TEST_ASSERT_TRUE(pad::sameAddr(A(1), b.get(pad::Kind::Xbox)));
  TEST_ASSERT_TRUE(pad::sameAddr(A(3, 1), b.get(pad::Kind::Phone)));
  pad::Kind k;
  TEST_ASSERT_FALSE(b.kindOf(A(2, 1), k));  // il vecchio telefono non c'è più
}

static void test_new_xbox_keeps_phone() {
  pad::BondBook b;
  b.set(pad::Kind::Phone, A(2, 1));
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Xbox, A(4));
  TEST_ASSERT_TRUE(pad::sameAddr(A(2, 1), b.get(pad::Kind::Phone)));
  TEST_ASSERT_TRUE(pad::sameAddr(A(4), b.get(pad::Kind::Xbox)));
}

// Lo stesso indirizzo non è mai di due tipi: passa al tipo nuovo.
static void test_same_address_moves_kind() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(1));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Xbox));
  pad::Kind k;
  TEST_ASSERT_TRUE(b.kindOf(A(1), k));
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(k));
}

static void test_clear_one_kind() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  b.clear(pad::Kind::Phone);
  TEST_ASSERT_TRUE(b.has(pad::Kind::Xbox));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Phone));
  TEST_ASSERT_TRUE(b.any());
}

// Un indirizzo risolto dal controller arriva come tipo «identità» (2 pubblico, 3 casuale): vale come 0/1.
static void test_identity_type_matches() {
  pad::BondBook b;
  b.set(pad::Kind::Phone, A(2, 1));
  pad::Kind k;
  TEST_ASSERT_TRUE(b.kindOf(A(2, 3), k));
  TEST_ASSERT_FALSE(b.kindOf(A(2, 0), k));  // pubblico ≠ casuale
  TEST_ASSERT_FALSE(b.kindOf(A(9, 1), k));
}

static void test_blob_round_trip() {
  uint8_t blob[pad::kBondBlob];
  pad::BondBook::encode(A(0xAB, 1), blob);
  const uint8_t want[pad::kBondBlob] = {0x10, 0x20, 0x30, 0x40, 0x50, 0xAB, 1};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, blob, pad::kBondBlob);
  pad::Addr out;
  TEST_ASSERT_TRUE(pad::BondBook::decode(blob, sizeof blob, out));
  TEST_ASSERT_TRUE(pad::sameAddr(A(0xAB, 1), out));
}

static void test_blob_rejects_bad() {
  uint8_t blob[8] = {1, 2, 3, 4, 5, 6, 0, 0};
  pad::Addr out;
  TEST_ASSERT_FALSE(pad::BondBook::decode(blob, 6, out));
  TEST_ASSERT_FALSE(pad::BondBook::decode(blob, 8, out));
  TEST_ASSERT_FALSE(pad::BondBook::decode(nullptr, 7, out));
  blob[6] = 2;  // il libro salva solo 0 o 1
  TEST_ASSERT_FALSE(pad::BondBook::decode(blob, 7, out));
}

// Abbinamenti di NimBLE da cancellare: quelli che il libro non conosce (il vecchio telefono dopo il nuovo).
static void test_stale_bonds() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(3, 1));
  const pad::Addr bonds[3] = {A(1), A(2, 1), A(3, 3)};
  int out[3] = {-1, -1, -1};
  TEST_ASSERT_EQUAL(1, pad::staleBonds(b, bonds, 3, out, 3));
  TEST_ASSERT_EQUAL(1, out[0]);
  TEST_ASSERT_EQUAL(0, pad::staleBonds(b, bonds, 1, out, 3));
  pad::BondBook empty;
  TEST_ASSERT_EQUAL(2, pad::staleBonds(empty, bonds, 3, out, 2));  // al più cap
}

// Prima accensione della 0.14.0: l'unico abbinamento della 0.13.0-ed è l'Xbox.
static void test_migrate_legacy_once() {
  pad::BondBook b;
  const pad::Addr one[1] = {A(1)};
  TEST_ASSERT_TRUE(pad::migrateLegacy(b, false, one, 1));
  TEST_ASSERT_TRUE(pad::sameAddr(A(1), b.get(pad::Kind::Xbox)));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Phone));
  pad::BondBook c;
  TEST_ASSERT_FALSE(pad::migrateLegacy(c, true, one, 1));  // già migrato: «Dimentica» poi riavvio non lo riporta
  TEST_ASSERT_FALSE(c.any());
  const pad::Addr two[2] = {A(1), A(2)};
  TEST_ASSERT_FALSE(pad::migrateLegacy(c, false, two, 2));  // due: non si indovina, li toglie la riconciliazione
  TEST_ASSERT_FALSE(pad::migrateLegacy(c, false, one, 0));
  pad::BondBook d;
  d.set(pad::Kind::Phone, A(5, 1));
  TEST_ASSERT_FALSE(pad::migrateLegacy(d, false, one, 1));  // libro non vuoto
}

// D2: «associato» a radio spenta e migrazione con la stessa regola (esattamente un abbinamento).
static void test_legacy_is_xbox() {
  pad::BondBook b;
  TEST_ASSERT_TRUE(pad::legacyIsXbox(false, b, 1));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(false, b, 0));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(false, b, 2));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(true, b, 1));
  b.set(pad::Kind::Phone, A(5, 1));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(false, b, 1));
}

static void test_keys() {
  TEST_ASSERT_EQUAL_STRING("padx", pad::bondKey(pad::Kind::Xbox));
  TEST_ASSERT_EQUAL_STRING("padp", pad::bondKey(pad::Kind::Phone));
  TEST_ASSERT_EQUAL_STRING("padv", pad::kBondVersionKey);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_book);
  RUN_TEST(test_new_phone_keeps_xbox);
  RUN_TEST(test_new_xbox_keeps_phone);
  RUN_TEST(test_same_address_moves_kind);
  RUN_TEST(test_clear_one_kind);
  RUN_TEST(test_identity_type_matches);
  RUN_TEST(test_blob_round_trip);
  RUN_TEST(test_blob_rejects_bad);
  RUN_TEST(test_stale_bonds);
  RUN_TEST(test_migrate_legacy_once);
  RUN_TEST(test_legacy_is_xbox);
  RUN_TEST(test_keys);
  return UNITY_END();
}
```

- [ ] **Step 2: Vederli fallire**

Run: `cd firmware && pio test -e native -f test_pad_bonds 2>&1 | tail -n 10`
Expected: errore di compilazione, `core/pad_bonds.h` non trovato.

- [ ] **Step 3: Scrivere `core/pad_bonds.h`**

```cpp
// Abbinamenti per tipo (spec app iPhone §2.2): al massimo un Xbox e un telefono. NimBLE tiene le chiavi
// («nimble_bond», fino a 3 abbinamenti); questo libro dice di che tipo è ognuno ed è la sola verità: un
// abbinamento di NimBLE che il libro non conosce si cancella. NVS «calamaio-gb»: padx, padp (7 byte), padv = 1.
// Parte pura, provata in env:native (test_pad_bonds).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pad.h"

namespace cal {
namespace pad {

struct Addr {
  uint8_t v[6];   // come ble_addr_t.val (byte meno significativo per primo)
  uint8_t type;   // 0 pubblico, 1 casuale (d'identità)
};
// 6 byte uguali e stesso tipo a meno del bit «identità» (2/3 = 0/1 risolti dal controller).
bool sameAddr(const Addr &a, const Addr &b);

const size_t kBondBlob = 7;
const char *bondKey(Kind k);           // 'padx', 'padp'
extern const char *const kBondVersionKey;  // 'padv': 1 = libro già scritto dalla 0.14.0

class BondBook {
 public:
  BondBook() : has_{false, false} {}
  bool has(Kind k) const { return has_[idx(k)]; }
  const Addr &get(Kind k) const { return a_[idx(k)]; }
  bool any() const { return has_[0] || has_[1]; }
  // Il nuovo del suo tipo sostituisce il vecchio dello stesso tipo, mai l'altro; un indirizzo che era dell'altro
  // tipo passa a questo (mai lo stesso dispositivo due volte).
  void set(Kind k, const Addr &a);
  void clear(Kind k) { has_[idx(k)] = false; }
  bool kindOf(const Addr &a, Kind &k) const;
  static void encode(const Addr &a, uint8_t out[kBondBlob]);
  static bool decode(const uint8_t *b, size_t n, Addr &out);  // false: lunghezza ≠ 7, nullo, tipo > 1

 private:
  static int idx(Kind k) { return k == Kind::Phone ? 1 : 0; }
  bool has_[2];
  Addr a_[2];
};

// Indici (in bonds) degli abbinamenti che il libro non conosce, al massimo cap; ritorna quanti.
int staleBonds(const BondBook &book, const Addr *bonds, int n, int *out, int cap);
// Migrazione dalla 0.13.0-ed (un solo controller, nessun libro): con libro vuoto, mai migrato e un solo
// abbinamento, quello è l'Xbox. true se il libro è cambiato (va salvato, con padv = 1).
bool migrateLegacy(BondBook &book, bool migrated, const Addr *bonds, int n);
// D2: la stessa regola per «associato» a radio spenta (hw/blepad bonded): l'unico abbinamento di NimBLE di una
// 0.13 non ancora migrata è l'Xbox.
bool legacyIsXbox(bool migrated, const BondBook &book, int nBonds);

}  // namespace pad
}  // namespace cal
```

- [ ] **Step 4: Scrivere `core/pad_bonds.cpp`**

```cpp
#include "pad_bonds.h"

#include <string.h>

namespace cal {
namespace pad {

const char *const kBondVersionKey = "padv";

const char *bondKey(Kind k) { return k == Kind::Phone ? "padp" : "padx"; }

bool sameAddr(const Addr &a, const Addr &b) {
  return (a.type & 1) == (b.type & 1) && memcmp(a.v, b.v, sizeof a.v) == 0;
}

void BondBook::set(Kind k, const Addr &a) {
  const int i = idx(k), o = 1 - i;
  if (has_[o] && sameAddr(a_[o], a)) has_[o] = false;
  a_[i] = a;
  a_[i].type &= 1;
  has_[i] = true;
}

bool BondBook::kindOf(const Addr &a, Kind &k) const {
  for (int i = 0; i < 2; i++)
    if (has_[i] && sameAddr(a_[i], a)) {
      k = i ? Kind::Phone : Kind::Xbox;
      return true;
    }
  return false;
}

void BondBook::encode(const Addr &a, uint8_t out[kBondBlob]) {
  memcpy(out, a.v, 6);
  out[6] = a.type & 1;
}

bool BondBook::decode(const uint8_t *b, size_t n, Addr &out) {
  if (!b || n != kBondBlob || b[6] > 1) return false;
  memcpy(out.v, b, 6);
  out.type = b[6];
  return true;
}

int staleBonds(const BondBook &book, const Addr *bonds, int n, int *out, int cap) {
  int k = 0;
  for (int i = 0; i < n && k < cap; i++) {
    Kind kind;
    if (!book.kindOf(bonds[i], kind)) out[k++] = i;
  }
  return k;
}

bool legacyIsXbox(bool migrated, const BondBook &book, int nBonds) { return !migrated && !book.any() && nBonds == 1; }

bool migrateLegacy(BondBook &book, bool migrated, const Addr *bonds, int n) {
  if (!legacyIsXbox(migrated, book, n)) return false;
  book.set(Kind::Xbox, bonds[0]);
  return true;
}

}  // namespace pad
}  // namespace cal
```

- [ ] **Step 5: Vederli passare**

Run: `cd firmware && pio test -e native -f test_pad_bonds 2>&1 | tail -n 8`
Expected: `12 Tests 0 Failures 0 Ignored`, `PASSED`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/core/pad_bonds.h firmware/src/core/pad_bonds.cpp firmware/test/test_pad_bonds
git commit -m "Controller dal telefono: libro degli abbinamenti per tipo (padx/padp), abbinamenti orfani, migrazione dell'Xbox della 0.13 (stessa regola a radio spenta)"
```

---

### Task 4: Filtro di ricerca, sicurezza e associazione per tipo

**Files:**
- Modify: `firmware/src/core/pad_bonds.h`, `firmware/src/core/pad_bonds.cpp`
- Modify: `firmware/src/core/pad.h`, `firmware/src/core/pad.cpp` (`LinkMachine`)
- Test: `firmware/test/test_pad_bonds/test_main.cpp`, `firmware/test/test_pad/test_main.cpp`

**Interfaces:**
- Consumes: `Addr`, `BondBook` (Task 3), `Kind`, `Link` (Task 2 e già in `pad.h`).
- Produces (in `cal::pad`):
  - `struct ScanAd { Addr addr; bool gamepad, hid, phoneSvc, connectable; };`
  - `struct ScanPick { bool take; Kind kind; bool bonded; };`
  - `ScanPick pickAd(const ScanAd &ad, const BondBook &book, Link link, Kind pairKind);`
  - `const uint8_t kIoDisplayOnly = 0, kIoNoInputOutput = 3;` (valori HCI di `BLE_HS_IO_*`, controllati con `static_assert` in `hw/blepad.cpp`)
  - `struct SecCfg { uint8_t ioCap; bool bond, mitm, sc, scOnly; };` `SecCfg secFor(Kind k);`
  - `bool linkSecure(Kind k, bool encrypted, bool authenticated, uint8_t keySize, bool sc);` (F5: `sc` = abbinamento Secure Connections letto dall'archivio delle chiavi; per l'Xbox non conta)
  - `LinkMachine::pair(uint32_t now, Kind k = Kind::Xbox)`, `LinkMachine::connected(bool bonded, Kind k = Kind::Xbox)`, `Kind LinkMachine::kind() const` (tipo dell'associazione in corso o del collegamento), `void LinkMachine::setBonded(bool b)`, `void LinkMachine::cancel()`.

- [ ] **Step 1: Test che falliscono in `test_pad_bonds`**

Aggiungere prima di `main` (e i `RUN_TEST` in fondo):

```cpp
static pad::ScanAd ad(const pad::Addr &a, bool gamepad, bool hid, bool phone, bool conn = true) {
  pad::ScanAd s;
  s.addr = a;
  s.gamepad = gamepad;
  s.hid = hid;
  s.phoneSvc = phone;
  s.connectable = conn;
  return s;
}

static void test_pick_bonded_any_time() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  pad::ScanPick p = pad::pickAd(ad(A(1), true, true, false), b, pad::Link::Connecting, pad::Kind::Xbox);
  TEST_ASSERT_TRUE(p.take && p.bonded);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Xbox), static_cast<int>(p.kind));
  p = pad::pickAd(ad(A(2, 3), false, false, true), b, pad::Link::Connecting, pad::Kind::Xbox);  // risolto
  TEST_ASSERT_TRUE(p.take && p.bonded);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(p.kind));
}

static void test_pick_unknown_outside_pairing() {
  pad::BondBook b;
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, true, false), b, pad::Link::Connecting, pad::Kind::Xbox).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Connecting, pad::Kind::Phone).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Ready, pad::Kind::Phone).take);
}

static void test_pick_pairing_xbox() {
  pad::BondBook b;
  TEST_ASSERT_TRUE(pad::pickAd(ad(A(7), true, true, false), b, pad::Link::Pairing, pad::Kind::Xbox).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, false, false), b, pad::Link::Pairing, pad::Kind::Xbox).take);  // senza HID
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, true, false, false), b, pad::Link::Pairing, pad::Kind::Xbox).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Xbox).take);  // telefono
}

static void test_pick_pairing_phone() {
  pad::BondBook b;
  const pad::ScanPick p = pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Phone);
  TEST_ASSERT_TRUE(p.take);
  TEST_ASSERT_FALSE(p.bonded);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(p.kind));
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true, false), b, pad::Link::Pairing, pad::Kind::Phone).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, true, false), b, pad::Link::Pairing, pad::Kind::Phone).take);  // Xbox
}

// Durante l'associazione di un tipo l'associato dell'altro tipo non si prende: non chiuderebbe la finestra.
static void test_pick_pairing_ignores_other_kind() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(1), true, true, false), b, pad::Link::Pairing, pad::Kind::Phone).take);
  TEST_ASSERT_TRUE(pad::pickAd(ad(A(2, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Phone).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(2, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Xbox).take);
}

// Review Focus «abbassamento della sicurezza»: l'Xbox come la 0.13.0-ed, il telefono solo SC con MITM, DisplayOnly.
static void test_sec_for_kinds() {
  const pad::SecCfg x = pad::secFor(pad::Kind::Xbox);
  TEST_ASSERT_EQUAL(pad::kIoNoInputOutput, x.ioCap);
  TEST_ASSERT_TRUE(x.bond);
  TEST_ASSERT_FALSE(x.mitm);
  TEST_ASSERT_FALSE(x.sc);
  TEST_ASSERT_FALSE(x.scOnly);
  const pad::SecCfg p = pad::secFor(pad::Kind::Phone);
  TEST_ASSERT_EQUAL(pad::kIoDisplayOnly, p.ioCap);
  TEST_ASSERT_TRUE(p.bond);
  TEST_ASSERT_TRUE(p.mitm);
  TEST_ASSERT_TRUE(p.sc);
  TEST_ASSERT_TRUE(p.scOnly);
}

static void test_link_secure_phone_needs_all_three() {
  TEST_ASSERT_TRUE(pad::linkSecure(pad::Kind::Phone, true, true, 16, true));
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, true, false, 16, true));  // Just Works
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, false, true, 16, true));
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, true, true, 7, true));    // chiave corta
  TEST_ASSERT_TRUE(pad::linkSecure(pad::Kind::Xbox, true, false, 16, false));   // Xbox: come oggi
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Xbox, false, false, 0, false));
}

// F5: da iniziatore NimBLE accetta il legacy con passkey (autenticato, chiave 16): per il telefono serve anche SC.
static void test_link_secure_phone_needs_sc() {
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, true, true, 16, false));
  TEST_ASSERT_TRUE(pad::linkSecure(pad::Kind::Xbox, true, false, 16, true));  // all'Xbox sc non serve
}
```

```cpp
  RUN_TEST(test_pick_bonded_any_time);
  RUN_TEST(test_pick_unknown_outside_pairing);
  RUN_TEST(test_pick_pairing_xbox);
  RUN_TEST(test_pick_pairing_phone);
  RUN_TEST(test_pick_pairing_ignores_other_kind);
  RUN_TEST(test_sec_for_kinds);
  RUN_TEST(test_link_secure_phone_needs_all_three);
  RUN_TEST(test_link_secure_phone_needs_sc);
```

(`test_sec_for_kinds` resta: dice che `sm_sc_only` si accende per il telefono, non che basti; A2 del pre-flight.)

- [ ] **Step 2: Test che falliscono in `test_pad` (LinkMachine)**

Aggiungere in `firmware/test/test_pad/test_main.cpp` (con i `RUN_TEST`). Senza il prefisso `pad::`, come il resto
del file: ha `using namespace cal::pad;` apposta per non confondere `pad::Link` con `cal::Link` (F1):

```cpp
static void test_link_pair_kind() {
  LinkMachine m;
  m.enable(true, true);
  m.pair(1000, Kind::Phone);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Pairing), static_cast<int>(m.link()));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Phone), static_cast<int>(m.kind()));
  m.connected(true, Kind::Phone);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Ready), static_cast<int>(m.link()));
  m.lost(5000, false);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
  m.connected(true, Kind::Xbox);
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Xbox), static_cast<int>(m.kind()));
}

static void test_link_cancel_pairing() {
  LinkMachine m;
  m.enable(true, false);
  m.pair(0, Kind::Phone);
  m.cancel();
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Off), static_cast<int>(m.link()));
  m.setBonded(true);
  m.pair(0, Kind::Phone);
  m.cancel();
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
  m.cancel();  // fuori dall'associazione: niente
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
}

// Abbinamento del telefono non valido, Xbox ancora associato: scaduta la finestra si cerca ancora l'Xbox.
static void test_link_set_bonded_after_invalid() {
  LinkMachine m;
  m.enable(true, true);
  m.connected(true, Kind::Phone);
  m.lost(1000, true);  // riapre l'associazione del telefono
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Pairing), static_cast<int>(m.link()));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Phone), static_cast<int>(m.kind()));
  m.setBonded(true);
  m.tick(1000 + LinkMachine::kPairMs);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
}
```

- [ ] **Step 3: Vederli fallire**

Run: `cd firmware && pio test -e native -f test_pad_bonds -f test_pad 2>&1 | tail -n 15`
Expected: errori di compilazione (`ScanAd`, `pickAd`, `secFor`, `pair(…, Kind)`, `kind()`, `cancel`, `setBonded` mancanti).

- [ ] **Step 4: Aggiungere a `core/pad_bonds.h`** (prima della chiusura dei namespace)

```cpp
// Ricerca (hw/blepad, callback dei risultati). Fuori dall'associazione solo gli associati; durante quella di un
// tipo l'associato di quel tipo o un nuovo di quel tipo, mai l'associato dell'altro (chiuderebbe la finestra).
// Xbox nuovo: aspetto «gamepad» 0x03C4 + servizio HID 0x1812 + collegabile (come la 0.13). Telefono nuovo:
// servizio «Calamaio pad» + collegabile.
struct ScanAd {
  Addr addr;
  bool gamepad, hid, phoneSvc, connectable;
};
struct ScanPick {
  bool take;
  Kind kind;
  bool bonded;  // nel libro
};
ScanPick pickAd(const ScanAd &ad, const BondBook &book, Link link, Kind pairKind);

// Sicurezza per tipo: si scrive in ble_hs_cfg prima di ogni collegamento (NimBLE la legge all'inizio
// dell'abbinamento). Xbox: come la 0.13.0-ed (Just Works). Telefono: solo Secure Connections, MITM, scheda
// DisplayOnly → passkey a schermo, iOS lo fa digitare.
const uint8_t kIoDisplayOnly = 0, kIoNoInputOutput = 3;  // BLE_HS_IO_DISPLAY_ONLY, BLE_HS_IO_NO_INPUT_OUTPUT
struct SecCfg {
  uint8_t ioCap;
  bool bond, mitm, sc, scOnly;
};
SecCfg secFor(Kind k);
// Collegamento accettabile dopo la cifratura: telefono cifrato + autenticato + chiave da 16 byte + Secure Connections
// (sc: dall'archivio delle chiavi, F5: da iniziatore NimBLE non applica sm_sc_only); Xbox cifrato.
bool linkSecure(Kind k, bool encrypted, bool authenticated, uint8_t keySize, bool sc);
```

- [ ] **Step 5: Aggiungere a `core/pad_bonds.cpp`**

```cpp
ScanPick pickAd(const ScanAd &ad, const BondBook &book, Link link, Kind pairKind) {
  ScanPick p = {false, Kind::Xbox, false};
  Kind k;
  const bool pairing = link == Link::Pairing;
  if (book.kindOf(ad.addr, k)) {
    if (pairing && k != pairKind) return p;
    p.take = true;
    p.kind = k;
    p.bonded = true;
    return p;
  }
  if (!pairing || !ad.connectable) return p;
  if (pairKind == Kind::Xbox && ad.gamepad && ad.hid) p.take = true;
  if (pairKind == Kind::Phone && ad.phoneSvc) p.take = true;
  p.kind = pairKind;
  return p;
}

SecCfg secFor(Kind k) {
  if (k == Kind::Phone) return SecCfg{kIoDisplayOnly, true, true, true, true};
  return SecCfg{kIoNoInputOutput, true, false, false, false};
}

bool linkSecure(Kind k, bool encrypted, bool authenticated, uint8_t keySize, bool sc) {
  if (k == Kind::Phone) return encrypted && authenticated && keySize == 16 && sc;
  return encrypted;
}
```

- [ ] **Step 6: `LinkMachine` per tipo (`core/pad.h` e `pad.cpp`)**

In `class LinkMachine` di `pad.h`: costruttore con `kind_(Kind::Xbox)`; sostituire le dichiarazioni di `pair` e
`connected` e aggiungere:

```cpp
  void pair(uint32_t now, Kind k = Kind::Xbox);  // «Associa controller» / «Associa telefono»
  void connected(bool bonded, Kind k = Kind::Xbox);  // collegato, cifrato e iscritto ai report
  void setBonded(bool b) { bonded_ = b; }            // 0.14.0: c'è ancora un associato (di un tipo o dell'altro)
  void cancel();                                     // 0.14.0: «LUNGA ANNULLA» sul codice del telefono
  Kind kind() const { return kind_; }                // tipo dell'associazione in corso o dell'ultimo collegamento
```

e in `private:` `Kind kind_;`. In `pad.cpp`:

```cpp
void LinkMachine::pair(uint32_t now, Kind k) {
  if (!on_) return;
  link_ = Link::Pairing;
  kind_ = k;
  pairUntil_ = now + kPairMs;
}

void LinkMachine::connected(bool bonded, Kind k) {
  if (!on_) return;
  link_ = Link::Ready;
  bonded_ = bonded;
  kind_ = k;
}

void LinkMachine::cancel() {
  if (link_ == Link::Pairing) link_ = bonded_ ? Link::Connecting : Link::Off;
}
```

`lost(now, true)` chiama già `pair(now)`: cambiarla in `pair(now, kind_)` (riapre l'associazione dello stesso tipo).
Aggiornare il commento della classe: «Un associato per tipo (core/pad_bonds); bonded = almeno uno».

- [ ] **Step 7: Vederli passare**

Run: `cd firmware && pio test -e native -f test_pad_bonds -f test_pad -f test_pad_ui 2>&1 | tail -n 10`
Expected: tutti `PASSED`.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/core/pad_bonds.h firmware/src/core/pad_bonds.cpp firmware/src/core/pad.h firmware/src/core/pad.cpp firmware/test/test_pad_bonds firmware/test/test_pad
git commit -m "Controller dal telefono: filtro di ricerca per tipo, sicurezza per tipo (telefono solo SC con MITM), associazione per tipo"
```

---

### Task 5: Regola §7: dal telefono mai CONSENTI, né le conferme di R1 e F11

**Files:**
- Modify: `firmware/src/core/ui.h` (`InputSrc`, `kPhoneAllowToast`, `kPhoneConfirmToast`, `PermEvent::src`, `NetRequest::src`, `phoneForbidden`, `padEvent` con la sorgente, `src_`, `fromPhone`, `confirmPerm` e `confirmAction` che ritornano `bool`, `phoneRefused`)
- Modify: `firmware/src/core/model.h` (`UiState::holdPhone`)
- Modify: `firmware/src/core/ui.cpp` (`phoneForbidden`, `gesture`, `keyDown`, `keyUp`, `emitNet`, `emitPerm`, `phoneRefused`, `confirmPerm`, `confirmAction`, `pairConfirm`, `act`: ramo `Sleep` su `Confirm` e Cerca reti, `activate`: «Configura da telefono», `doAct`: `Unpair`, `Forget`, `ForgetNet`)
- Modify: `firmware/src/core/ui_pad.cpp` (`padEvent`, `padHoldDown`, `padHoldUp`, `padHoldFire`)
- Test: `firmware/test/test_pad_ui/test_main.cpp`, `firmware/test/test_m5_ui/test_main.cpp`

**Interfaces:**
- Consumes: `pad::Kind` (Task 2).
- Produces:
  - `enum class InputSrc : uint8_t { Prg, Xbox, Phone };` in `ui.h`, campo `InputSrc src;` in `PermEvent` e in `NetRequest` (lo legge `onNetRequest`, Task 11).
  - `bool phoneForbidden(NetCmd c);` (R1, F11: `ProtectKeys`, `FactoryReset`, `PairConfirm`, `Unpair`, `Forget`, `ForgetNet`, `Provision`, `Join`; la usano `Ui::emitNet` e `onNetRequest`).
  - `RefreshKind Ui::padEvent(pad::MenuEvent e, Ms now, const WallClock &wc, pad::Kind src = pad::Kind::Xbox);`
  - `bool Ui::confirmPerm(ScreenEntry &tp, Ms now);` (false = rifiutato, la richiesta resta aperta), `bool Ui::confirmAction(ScreenEntry &tp, Ms now);` (false = rifiutato dal telefono: schermata e armatura restano), `bool Ui::phoneRefused(const char *action, Ms now);` (privata: toast di R1 e `true` se l'ingresso è del telefono).
  - Testi: `kPhoneAllowToast = "Per consentire usa il tasto PRG o il controller"`, `kPhoneConfirmToast = "Per confermare usa il tasto PRG o il controller"` (costanti in `ui.h`, le usano i test).

- [ ] **Step 1: Test che falliscono**

In `test_pad_ui/test_main.cpp`, in cima `#include <memory>` e, prima di `struct Bench`:

```cpp
// R1 e F11: i comandi che dal telefono non devono mai uscire, scritti qui e non presi da phoneForbidden (una prova
// che usasse la stessa lista del codice non vedrebbe un comando tolto per sbaglio).
static const NetCmd kNeverFromPhone[] = {NetCmd::ProtectKeys, NetCmd::FactoryReset, NetCmd::PairConfirm,
                                         NetCmd::Unpair,      NetCmd::Forget,       NetCmd::ForgetNet,
                                         NetCmd::Provision,   NetCmd::Join};
```

In `Bench`, dopo `int nEvents;`, il costruttore e `feed` diventano (il resto resta):

```cpp
  pad::Kind src = pad::Kind::Xbox;
  NetCmd cmds[16];  // R1, F11: comandi usciti verso la rete (solo con live)
  int nCmds = 0;
  explicit Bench(bool live = false) : ui(kHeltec213), now(0), nEvents(0) {
    ui.setPermListener(&Bench::onPerm, this);
    ui.setNetListener(&Bench::onNet, this);
    ui.begin(now, wc());
    if (live) ui.goLive();
  }
  static void onNet(const NetRequest &r, void *ctx) {
    Bench *b = static_cast<Bench *>(ctx);
    if (b->nCmds < 16) b->cmds[b->nCmds++] = r.cmd;
  }
  int forbidden() const {
    int n = 0;
    for (int i = 0; i < nCmds; i++)
      for (NetCmd c : kNeverFromPhone) n += cmds[i] == c;
    return n;
  }
```

```cpp
  void feed(const pad::MenuEvent *ev, int n) { for (int i = 0; i < n; i++) ui.padEvent(ev[i], now, wc(), src); }
```

Poi, prima di `main` (dopo `findItem` e `pushList`, che le prove usano). `test_phone_hold_consenti_refused` guarda
`holdOk` **prima** del rilascio: `keyUp()` lo azzera sempre, dopo il `tap` l'asserzione non potrebbe fallire (A1 del
pre-flight). `test_phone_never_allows_any_sequence` prova le stesse 4 096 sequenze dal Permesso e da ognuna delle otto
conferme vietate (R1, F11), con la rete accesa così ogni comando passa da `onNet`:

```cpp
// §7 scelta A (spec app iPhone §2.3): dal telefono CONSENTI scelto e tenuto 3 s non consente.
static void test_phone_hold_consenti_refused() {
  Bench b;
  openPerm(b);  // aperto col controller: aprire non è rispondere
  b.src = pad::Kind::Phone;
  b.tap(pad::Down);
  TEST_ASSERT_EQUAL(0, top(b.ui.model()).choice);  // CONSENTI scelto: si vede
  b.state(pad::A);
  b.run(3200);
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOk);  // niente «FATTO · RILASCIA», guardato prima del rilascio (keyUp lo azzera)
  b.state(0);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneAllowToast, b.ui.model().toast.msg);
}

static void test_phone_hold_nega_answers() {
  Bench b;
  openPerm(b);
  b.src = pad::Kind::Phone;
  b.tap(pad::A, 3200);  // NEGA preselezionato
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Deny, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Phone, b.events[0].src);
  TEST_ASSERT_EQUAL(ScreenId::Detail, b.topId());
}

// Dopo il rifiuto il PRG consente come sempre (e la sorgente dice PRG).
static void test_prg_allows_after_phone_refusal() {
  Bench b;
  openPerm(b);
  b.src = pad::Kind::Phone;
  b.tap(pad::Down);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  b.press(2);  // 2× del PRG conferma la scelta a schermo (CONSENTI)
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Prg, b.events[0].src);
}

// L'Xbox resta com'era: CONSENTI tenuto 3 s consente.
static void test_xbox_still_allows() {
  Bench b;
  openPerm(b);
  b.tap(pad::Down);
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Allow, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Xbox, b.events[0].src);
}

// Il tenuto iniziato dal telefono resta del telefono anche se lo scatto arriva da un tick (sorgente persa).
static void test_phone_hold_fire_from_tick_refused() {
  Bench b;
  openPerm(b);
  b.src = pad::Kind::Phone;
  b.tap(pad::Down);
  b.state(pad::A);       // HoldDown dal telefono
  b.src = pad::Kind::Xbox;  // gli eventi del tick arrivano con la sorgente sbagliata
  b.run(3200);
  b.state(0);
  TEST_ASSERT_EQUAL(0, b.nEvents);
  TEST_ASSERT_EQUAL(ScreenId::Perm, b.topId());
}

// R1 e F11: da dove partono le sequenze vietate al telefono. Bench con live: ogni comando passa da onNet.
enum class From : uint8_t { Protect, Factory, PcPair, Unpair, ForgetAll, ForgetNet, Qr, ScanNet, Count };
static const char *const kFromName[] = {"Proteggi", "Ripristino", "abbinamento col PC", "Dimentica abbinamento",
                                        "Dimentica tutte le reti", "Dimentica rete", "Configura da telefono",
                                        "Cerca reti"};

static void phoneSelect(Bench &b) {
  b.ui.padEvent(pad::MenuEvent::Select, b.now, b.wc(), pad::Kind::Phone);
  b.now += 300;
  b.tick();
}

static void listAt(Bench &b, MenuId menu, ItemKind k, ScreenId open, ActId act, const char *ssid = "") {
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = menu;
  snprintf(e.ssid, sizeof e.ssid, "%s", ssid);
  pushScreen(b.ui.model(), e);
  const int i = findItem(b, k, open, act);
  TEST_ASSERT_TRUE(i >= 0);
  top(b.ui.model()).sel = i;
}

static void startFrom(Bench &b, From f) {
  b.boot();
  Model &m = b.ui.model();
  b.src = pad::Kind::Phone;
  switch (f) {
    case From::Protect:
    case From::Factory:
      listAt(b, MenuId::Security, ItemKind::Act, ScreenId::Home, f == From::Protect ? ActId::Protect : ActId::Factory);
      phoneSelect(b);  // avviso
      phoneSelect(b);  // armata
      TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
      break;
    case From::PcPair:
      m.sys.pair = PairState::None;
      b.ui.netPairCode("175397", b.now);
      b.ui.netPairReady(b.now);
      TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
      break;
    case From::Unpair:
      m.sys.pair = PairState::Paired;
      listAt(b, MenuId::Security, ItemKind::Act, ScreenId::Home, ActId::Unpair);
      phoneSelect(b);  // «2 pressioni ancora per confermare»
      break;
    case From::ForgetAll:
      listAt(b, MenuId::Wifi, ItemKind::Act, ScreenId::Home, ActId::Forget);
      phoneSelect(b);
      break;
    case From::ForgetNet: {
      SavedNet s;
      memset(&s, 0, sizeof s);
      snprintf(s.ssid, sizeof s.ssid, "Casa");
      b.ui.netSaved(&s, 1, b.now);
      listAt(b, MenuId::SavedNet, ItemKind::Act, ScreenId::Home, ActId::ForgetNet, "Casa");
      phoneSelect(b);
      break;
    }
    case From::Qr:
      listAt(b, MenuId::Wifi, ItemKind::Open, ScreenId::Qr, ActId::Style);
      break;
    case From::ScanNet: {
      ScreenEntry e = makeScreen(ScreenId::WifiScan);
      e.scanUntil = b.now + 1;
      pushScreen(m, e);
      NetEntry n[2];
      memset(n, 0, sizeof n);
      snprintf(n[0].ssid, sizeof n[0].ssid, "Ufficio");
      n[0].lock = true;
      snprintf(n[1].ssid, sizeof n[1].ssid, "Bar");
      b.ui.netScanDone(n, 2, b.now);
      break;
    }
    default: break;
  }
  TEST_ASSERT_EQUAL(0, b.forbidden());
}

// Review Focus §7: ogni sequenza di 4 eventi del telefono dal Permesso con CONSENTI scelto (B, riapertura dal
// Dettaglio, scelta cambiata, tenuti e rilasci in ogni ordine) non produce mai un «consenti».
static void test_phone_never_allows_any_sequence() {
  const pad::MenuEvent kEv[8] = {pad::MenuEvent::Up,       pad::MenuEvent::Down,   pad::MenuEvent::Select,
                                 pad::MenuEvent::Back,     pad::MenuEvent::Menu,   pad::MenuEvent::HoldDown,
                                 pad::MenuEvent::HoldUp,   pad::MenuEvent::HoldFire};
  int allows = 0, denies = 0;
  for (int code = 0; code < 8 * 8 * 8 * 8; code++) {
    std::unique_ptr<Bench> b(new Bench());
    openPerm(*b);
    b->src = pad::Kind::Phone;
    b->tap(pad::Down);
    int c = code;
    for (int k = 0; k < 4; k++) {
      b->ui.padEvent(kEv[c % 8], b->now, b->wc(), pad::Kind::Phone);
      c /= 8;
      b->now += 3100;
      b->tick();
    }
    for (int i = 0; i < b->nEvents; i++) {
      allows += b->events[i].outcome == PermOutcome::Allow;
      denies += b->events[i].outcome == PermOutcome::Deny;
    }
  }
  TEST_ASSERT_EQUAL(0, allows);
  TEST_ASSERT_TRUE(denies > 0);  // la prova ha raggiunto davvero delle risposte (NEGA dopo una riapertura)
  // R1 e F11: le stesse 4 096 sequenze da ogni conferma vietata al telefono (armata o pronta), con la rete accesa
  // (ogni comando si vede): mai un NetRequest vietato, mai le chiavi protette.
  for (int f = 0; f < static_cast<int>(From::Count); f++) {
    int sent = 0, refused = 0;
    for (int code = 0; code < 8 * 8 * 8 * 8; code++) {
      std::unique_ptr<Bench> b(new Bench(true));
      startFrom(*b, static_cast<From>(f));
      int c = code;
      bool seen = false;
      for (int k = 0; k < 4; k++) {
        b->ui.padEvent(kEv[c % 8], b->now, b->wc(), pad::Kind::Phone);
        seen = seen || strcmp(b->ui.model().toast.msg, kPhoneConfirmToast) == 0;
        c /= 8;
        b->now += 3100;
        b->tick();
      }
      sent += b->forbidden();
      sent += b->ui.model().sys.keysProtected;
      refused += seen;
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, sent, kFromName[f]);
    TEST_ASSERT_TRUE_MESSAGE(refused > 0, kFromName[f]);  // la barriera è stata raggiunta davvero
  }
}

static void test_perm_event_src() {
  Bench b;
  openPerm(b);
  b.tap(pad::B);  // annulla dal controller
  TEST_ASSERT_EQUAL(1, b.nEvents);
  TEST_ASSERT_EQUAL(PermOutcome::Cancelled, b.events[0].outcome);
  TEST_ASSERT_EQUAL(InputSrc::Xbox, b.events[0].src);
}
```

```cpp
// R1: dal telefono Ripristino di fabbrica e Proteggi le chiavi non si confermano (si aprono e si armano, si vede).
static void phoneConfirm(ActId act, int &topAfter) {
  Bench b;
  b.boot();
  pushList(b, MenuId::Security);
  const int k = findItem(b, ItemKind::Act, ScreenId::Home, act);
  TEST_ASSERT_TRUE(k >= 0);
  top(b.ui.model()).sel = k;
  b.src = pad::Kind::Phone;
  b.tap(pad::A);  // apre la conferma
  TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  b.tap(pad::A);  // arma
  b.state(pad::A);
  b.run(3200);    // tenuto 3 s dal telefono
  TEST_ASSERT_FALSE(b.ui.model().ui.holdOk);  // niente «FATTO · RILASCIA» (prima del rilascio)
  b.state(0);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  topAfter = static_cast<int>(b.topId());
  TEST_ASSERT_FALSE(b.ui.model().sys.keysProtected);
}
static void test_phone_never_factory_reset() {
  int t;
  phoneConfirm(ActId::Factory, t);
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::Confirm), t);
}
static void test_phone_never_protects_keys() {
  int t;
  phoneConfirm(ActId::Protect, t);
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenId::Confirm), t);
}
static void test_phone_never_confirms_pc_pairing() {
  Bench b;
  b.boot();
  b.ui.model().sys.pair = PairState::None;
  b.ui.netPairCode("175397", b.now);
  b.ui.netPairReady(b.now);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  TEST_ASSERT_TRUE(b.ui.model().sys.pair != PairState::Paired);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.src = pad::Kind::Xbox;
  b.tap(pad::A);  // il controller sì, come oggi
  TEST_ASSERT_TRUE(b.ui.model().sys.pair == PairState::Paired);
}

// F11: dal telefono «Dimentica abbinamento», «Dimentica tutte le reti» e Reti salvate › Dimentica si armano (si
// vede «conferma?») ma non partono; il controller conferma come oggi.
static void phoneArmTwice(Bench &b, MenuId menu, ActId act, const char *ssid = "") {
  b.boot();
  listAt(b, menu, ItemKind::Act, ScreenId::Home, act, ssid);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("2 pressioni ancora per confermare", b.ui.model().toast.msg);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.src = pad::Kind::Xbox;
}

static void test_phone_never_unpairs() {
  Bench b;
  phoneArmTwice(b, MenuId::Security, ActId::Unpair);
  TEST_ASSERT_EQUAL(PairState::Paired, b.ui.model().sys.pair);  // il banco parte abbinato
  b.tap(pad::A);  // ancora armata: il controller conferma
  TEST_ASSERT_EQUAL(PairState::None, b.ui.model().sys.pair);
}

static void test_phone_never_forgets_all_networks() {
  Bench b;
  phoneArmTwice(b, MenuId::Wifi, ActId::Forget);
  TEST_ASSERT_EQUAL(4, b.ui.model().nSaved);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(0, b.ui.model().nSaved);
}

static void test_phone_never_forgets_a_network() {
  Bench b;
  b.boot();
  char ssid[kSsidLen];
  snprintf(ssid, sizeof ssid, "%s", b.ui.model().saved[0].ssid);
  phoneArmTwice(b, MenuId::SavedNet, ActId::ForgetNet, ssid);
  TEST_ASSERT_EQUAL(4, b.ui.model().nSaved);
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(3, b.ui.model().nSaved);
}

// F11: «Configura da telefono» e una rete di Cerca reti dal telefono non partono; Cerca reti si apre.
static void test_phone_never_provisions() {
  Bench b;
  b.boot();
  listAt(b, MenuId::Wifi, ItemKind::Open, ScreenId::Qr, ActId::Style);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  const int k = findItem(b, ItemKind::Open, ScreenId::WifiScan, ActId::Style);
  top(b.ui.model()).sel = k;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::WifiScan, b.topId());  // cercare si può
  b.run(1600);  // fine della ricerca finta
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::WifiScan, b.topId());  // ma non collegarsi né configurare
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.ui.model().toast.msg);
  b.src = pad::Kind::Xbox;
  b.tap(pad::A);
  TEST_ASSERT_TRUE(b.topId() == ScreenId::Qr || b.topId() == ScreenId::Connecting);
}

// Riavvia resta ammesso dal telefono (innocuo).
static void test_phone_reboot_allowed() {
  Bench b(true);
  b.boot();
  listAt(b, MenuId::Info, ItemKind::Act, ScreenId::Home, ActId::Reboot);
  b.src = pad::Kind::Phone;
  b.tap(pad::A);
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
  TEST_ASSERT_EQUAL(1, b.nCmds);
  TEST_ASSERT_EQUAL(NetCmd::Reboot, b.cmds[0]);
}

static void test_phone_forbidden_list() {
  for (NetCmd c : kNeverFromPhone) TEST_ASSERT_TRUE_MESSAGE(phoneForbidden(c), netCmdKey(c));
  const NetCmd yes[] = {NetCmd::Scan, NetCmd::CancelProvision, NetCmd::Reboot, NetCmd::PermReply, NetCmd::PairCancel,
                        NetCmd::Dismiss, NetCmd::AvatarNeed, NetCmd::AccWear, NetCmd::ThemeCur};
  for (NetCmd c : yes) TEST_ASSERT_FALSE_MESSAGE(phoneForbidden(c), netCmdKey(c));
}
```

e i `RUN_TEST` di: `test_phone_hold_consenti_refused`, `test_phone_hold_nega_answers`,
`test_prg_allows_after_phone_refusal`, `test_xbox_still_allows`, `test_phone_hold_fire_from_tick_refused`,
`test_phone_never_allows_any_sequence`, `test_perm_event_src`, `test_phone_never_factory_reset`,
`test_phone_never_protects_keys`, `test_phone_never_confirms_pc_pairing` (il banco parte `Paired`: la prova mette
`sys.pair = None` prima), `test_phone_never_unpairs`, `test_phone_never_forgets_all_networks`,
`test_phone_never_forgets_a_network`, `test_phone_never_provisions`, `test_phone_reboot_allowed`,
`test_phone_forbidden_list`.

In `test_m5_ui/test_main.cpp` (rete accesa e PC autenticato: la seconda barriera da sola), `#include "core/pad.h"`
in cima e prima di `test_power_save_redraws_only_real_changes`:

```cpp
// R1 e F11 con la rete accesa e il PC autenticato: dal telefono Proteggi, Ripristino, conferma dell'abbinamento,
// «Dimentica abbinamento», «Dimentica tutte le reti» e «Configura da telefono» non mandano niente; col PRG sì.
static void phoneEv(Bench &b, pad::MenuEvent e) {
  b.now += 300;
  b.ui.padEvent(e, b.now, b.wc(), pad::Kind::Phone);
}

static void listItem(Bench &b, MenuId menu, ItemKind k, ScreenId open, ActId act) {
  while (b.topId() != ScreenId::Home) b.act(Action::Back);
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = menu;
  pushScreen(b.ui.model(), e);
  const MenuList &L = b.ui.items(b.now, b.wc());
  int sel = -1;
  for (int i = 0; i < L.n && sel < 0; i++)
    if (L.items[i].kind == k && (k == ItemKind::Open ? L.items[i].open == open : L.items[i].act == act)) sel = i;
  TEST_ASSERT_TRUE(sel >= 0);
  top(b.ui.model()).sel = sel;
}

static void test_phone_never_sends_protect_factory_pairconfirm() {
  static Bench b;
  b.ui.netAuthed(b.now);
  const size_t n0 = b.reqs.size();
  for (ActId a : {ActId::Protect, ActId::Factory}) {
    listItem(b, MenuId::Security, ItemKind::Act, ScreenId::Home, a);
    phoneEv(b, pad::MenuEvent::Select);  // avviso
    phoneEv(b, pad::MenuEvent::Select);  // armata
    phoneEv(b, pad::MenuEvent::HoldDown);
    b.now += 3000;
    phoneEv(b, pad::MenuEvent::HoldFire);
    phoneEv(b, pad::MenuEvent::HoldUp);
    TEST_ASSERT_EQUAL(ScreenId::Confirm, b.topId());
  }
  for (ActId a : {ActId::Unpair}) {
    listItem(b, MenuId::Security, ItemKind::Act, ScreenId::Home, a);
    phoneEv(b, pad::MenuEvent::Select);
    phoneEv(b, pad::MenuEvent::Select);
  }
  listItem(b, MenuId::Wifi, ItemKind::Act, ScreenId::Home, ActId::Forget);
  phoneEv(b, pad::MenuEvent::Select);
  phoneEv(b, pad::MenuEvent::Select);
  listItem(b, MenuId::Wifi, ItemKind::Open, ScreenId::Qr, ActId::Style);
  phoneEv(b, pad::MenuEvent::Select);
  TEST_ASSERT_EQUAL(ScreenId::List, b.topId());
  while (b.topId() != ScreenId::Home) b.act(Action::Back);
  b.ui.netPairCode("175397", b.now);
  b.ui.netPairReady(b.now);
  phoneEv(b, pad::MenuEvent::Select);
  TEST_ASSERT_EQUAL(ScreenId::Pair, b.topId());
  for (size_t i = n0; i < b.reqs.size(); i++) TEST_ASSERT_FALSE_MESSAGE(phoneForbidden(b.reqs[i].cmd), netCmdKey(b.reqs[i].cmd));
  TEST_ASSERT_EQUAL_STRING(kPhoneConfirmToast, b.m().toast.msg);
  b.now += 500;
  b.ui.gesture(Gesture::Press2, b.now, b.wc());  // 2× del PRG: pair-done parte, con la sorgente PRG
  TEST_ASSERT_FALSE(b.reqs.empty());
  TEST_ASSERT_EQUAL(NetCmd::PairConfirm, b.reqs.back().cmd);
  TEST_ASSERT_EQUAL(InputSrc::Prg, b.reqs.back().src);
}
```

con `RUN_TEST(test_phone_never_sends_protect_factory_pairconfirm);` in fondo a `main`.

- [ ] **Step 2: Vederli fallire**

Run: `cd firmware && pio test -e native -f test_pad_ui -f test_m5_ui 2>&1 | tail -n 15`
Expected: errori di compilazione (`InputSrc`, `kPhoneAllowToast`, `kPhoneConfirmToast`, `phoneForbidden`, quarto argomento di `padEvent`, `NetRequest::src`).

- [ ] **Step 3: `ui.h` e `model.h`**

In `ui.h`, prima di `struct PermEvent`:

```cpp
// 0.14.0: da dove viene il gesto che risponde (spec app iPhone §2.3, scelta A): il telefono non dà mai CONSENTI.
enum class InputSrc : uint8_t { Prg, Xbox, Phone };
constexpr const char *kPhoneAllowToast = "Per consentire usa il tasto PRG o il controller";
// R1 e F11: Proteggi, Ripristino, abbinamento col PC e Wi-Fi mai dal telefono.
constexpr const char *kPhoneConfirmToast = "Per confermare usa il tasto PRG o il controller";
```

In `struct PermEvent` aggiungere `InputSrc src;  // 0.14.0: onPerm scarta un Allow dal telefono (terza barriera)`.
In `struct NetRequest`, dopo `bool cal;`:
`InputSrc src;          // R1, F11: onNetRequest scarta dal telefono i comandi di phoneForbidden`.
Dopo `typedef void (*NetListener)(const NetRequest &r, void *ctx);`:

```cpp
// R1 e F11 (spec app iPhone §0): comandi che dal telefono non partono mai. eFuse, Ripristino, abbinamento della scheda
// col PC (conferma e «Dimentica abbinamento») e Wi-Fi (configurazione, rete aperta, dimentica). Riavvia resta ammesso.
// Seconda barriera in Ui::emitNet, terza in onNetRequest (main.cpp).
bool phoneForbidden(NetCmd c);
```

Firma di `padEvent`: `RefreshKind padEvent(pad::MenuEvent e, Ms now, const WallClock &wc, pad::Kind src = pad::Kind::Xbox);`.
`void confirmPerm(…)` → `bool confirmPerm(ScreenEntry &tp, Ms now);  // false: CONSENTI dal telefono, rifiutato`.
`void confirmAction(…)` → `bool confirmAction(ScreenEntry &tp, Ms now);  // false: dal telefono (R1), resta armata`.
Fra i privati:

```cpp
  bool phoneRefused(const char *action, Ms now);  // R1, F11: ingresso del telefono → toast, action, true
  InputSrc src_ = InputSrc::Prg;  // 0.14.0: sorgente dell'ultimo ingresso
  bool fromPhone() const { return src_ == InputSrc::Phone || (m_.ui.holdPad && m_.ui.holdPhone); }
```

In `UiState` di `model.h`, dopo `int holdChoice;`:
`bool holdPhone;  // 0.14.0: il tenuto di A in corso è del telefono (lo scatto del tick non ha sorgente)`.

- [ ] **Step 4: `ui.cpp`**

Dopo `netCmdKey`:

```cpp
bool phoneForbidden(NetCmd c) {
  switch (c) {
    case NetCmd::ProtectKeys: case NetCmd::FactoryReset: case NetCmd::PairConfirm: case NetCmd::Unpair:
    case NetCmd::Forget: case NetCmd::ForgetNet: case NetCmd::Provision: case NetCmd::Join:
      return true;
    default:
      return false;
  }
}
```

All'inizio di `Ui::gesture`, `Ui::keyDown` e `Ui::keyUp`: `src_ = InputSrc::Prg;`. In `keyDown` la riga
`u.holdPad = u.holdPerm = false;` diventa `u.holdPad = u.holdPerm = u.holdPhone = false;`.

`emitNet` (seconda barriera di R1 e F11):

```cpp
void Ui::emitNet(NetCmd c, const char *ssid) {
  if (!netFn_) return;
  // R1 e F11, seconda barriera: dal telefono niente eFuse, ripristino, abbinamento col PC né Wi-Fi.
  const bool phone = fromPhone();
  if (phone && phoneForbidden(c)) return;
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = c;
  r.src = phone ? InputSrc::Phone : src_;
  scopy(r.ssid, ssid ? ssid : "");
  netFn_(r, netCtx_);
}
```

`emitPerm`:

```cpp
void Ui::emitPerm(PermOutcome o, int session, const char *rid, Ms now) {
  // §7, scelta A: seconda barriera dopo confirmPerm. Un «consenti» dal telefono non esce mai.
  if (o == PermOutcome::Allow && fromPhone()) return;
  if (!permFn_) return;
  PermEvent e;
  e.outcome = o;
  e.session = session;
  e.requestId = rid ? rid : "";
  e.sessionName = session >= 0 && session < m_.nSessions ? m_.sessions[session].name : "";
  e.at = now;
  e.src = fromPhone() ? InputSrc::Phone : src_;
  permFn_(e, permCtx_);
}
```

Dopo `keyUp`:

```cpp
bool Ui::phoneRefused(const char *action, Ms now) {
  if (!fromPhone()) return false;
  toast(m_, kPhoneConfirmToast, now);
  redraw(RefreshKind::Partial);
  setAction(action);
  return true;
}
```

`confirmPerm`: all'inizio, dopo `const bool ok = tp.choice == 0;` (spostare quella riga in cima):

```cpp
bool Ui::confirmPerm(ScreenEntry &tp, Ms now) {
  const bool ok = tp.choice == 0;
  // §7, scelta A (spec app iPhone §2.3): qui nasce perm-reply. Dal telefono si conferma solo NEGA; CONSENTI
  // resta a schermo e la richiesta aperta, per consentire servono il PRG o l'Xbox.
  if (ok && fromPhone()) {
    toast(m_, kPhoneAllowToast, now);
    redraw(RefreshKind::Partial);
    setAction("Telefono: CONSENTI non si conferma dal telefono, la richiesta resta aperta");
    return false;
  }
  const int i = tp.i;
  Session &s = m_.sessions[i];
```

e `return true;` in fondo. La chiamata in `act()` (`confirmPerm(tp, t); return finish(t);`) resta com'è.

Prima barriera di R1 (Proteggi, Ripristino, abbinamento col PC). `confirmAction` ritorna `bool` e in testa:

```cpp
bool Ui::confirmAction(ScreenEntry &tp, Ms now) {
  if (phoneRefused("Telefono: Proteggi e Ripristino si confermano col tasto PRG o col controller", now)) return false;
```

(niente `popScreen`: la conferma resta a schermo e armata; i due `return;` del ramo Proteggi diventano `return true;`
e in fondo, dopo `setAction("Ripristino di fabbrica: …")`, `return true;`). In
`act()`, ramo `a == Action::Sleep && tp.id == ScreenId::Confirm`:

```cpp
    if (tp.until > t) { if (!confirmAction(tp, t)) m.ui.holdOk = false; return finish(t); }
```

`pairConfirm`, in testa:

```cpp
  if (phoneRefused("Telefono: l'abbinamento col PC si conferma col tasto PRG o col controller", now)) return;
```

Prima barriera di F11. In `doAct`, `case ActId::Unpair:` subito prima di `m_.ui.unpairUntil = 0;` (la seconda doppia
pressione):

```cpp
      // F11: armata anche dal telefono (si vede), ma si conferma solo col PRG o l'Xbox.
      if (phoneRefused("Telefono: l'abbinamento col PC si dimentica col tasto PRG o col controller", now)) return;
```

`case ActId::Forget:` subito prima di `m_.ui.forgetUntil = 0;` e `case ActId::ForgetNet:` subito prima di
`m_.ui.forgetNetUntil = 0;`:

```cpp
      if (phoneRefused("Telefono: le reti si dimenticano col tasto PRG o col controller", now)) return;  // F11
```

In `activate()`, in testa al ramo `if (it.kind == ItemKind::Open) {`:

```cpp
    // F11: «Configura da telefono» apre l'AP e il portale: dal telefono no.
    if (it.open == ScreenId::Qr && phoneRefused("Telefono: il Wi-Fi si cambia col tasto PRG o col controller", now)) return;
```

In `act()`, ramo `ScreenId::WifiScan`, subito dopo `const NetEntry net = m.nets[tp.sel];` (prima di `if (net.lock)`):

```cpp
      // F11: dal telefono si cerca e si guarda, ma il Wi-Fi non si cambia (configurazione o rete aperta).
      if (phoneRefused("Telefono: il Wi-Fi si cambia col tasto PRG o col controller", t)) return finish(t);
```

- [ ] **Step 5: `ui_pad.cpp`**

`padEvent` con la nuova firma; prima riga del corpo:
`src_ = src == pad::Kind::Phone ? InputSrc::Phone : InputSrc::Xbox;`. `keyDown()` e `keyUp()` rimettono `src_` a
`Prg` (Step 4): `padHoldDown` e `padHoldUp` la salvano attorno alla chiamata (F2: senza, `holdPhone` sarebbe sempre
falso e un CONSENTI dal telefono scattato dal tick uscirebbe, come mostra `test_phone_hold_fire_from_tick_refused`).
`padHoldDown`:

```cpp
RefreshKind Ui::padHoldDown(Ms now) {
  const InputSrc from = src_;          // keyDown() la rimette a Prg
  const RefreshKind r = keyDown(now);  // indicatore come il tasto PRG
  src_ = from;
  UiState &u = m_.ui;
  const ScreenEntry &tp = top(m_);
  u.holdPad = true;
  u.holdPhone = src_ == InputSrc::Phone;
  u.holdPerm = tp.id == ScreenId::Perm;
  u.holdI = tp.i;
  u.holdChoice = tp.choice;
  scopy(u.holdRid, u.holdPerm ? tp.rid : "");
  scopy(lastInput_, "Controller: A tenuto…");
  return r;
}
```

In `padHoldUp` la chiamata a `keyUp` diventa

```cpp
  const InputSrc from = src_;
  RefreshKind r = keyUp(now);  // keyUp guarda holdLabel: holdPad si spegne dopo
  src_ = from;
  u.holdPad = u.holdPerm = u.holdPhone = false;
```

(al posto di `RefreshKind r = keyUp(now);` e `u.holdPad = u.holdPerm = false;`). In `padHoldFire` sostituire

```cpp
    confirmPerm(tp, now);
    redraw(RefreshKind::Partial);  // «FATTO · RILASCIA»
    return finish(now);
```

con

```cpp
    u.holdOk = confirmPerm(tp, now);  // false: CONSENTI dal telefono, niente «FATTO · RILASCIA»
    redraw(RefreshKind::Partial);
    return finish(now);
```

(`padHoldUp` legge `holdDone` prima di `keyUp`: dopo un rifiuto `holdDone` è vero, il rilascio non dà «Troppo presto».)
Il ramo finale di `padHoldFire` (`gesture(Gesture::VeryLong, …)`) resta: `gesture()` rimette `src_` a `Prg`, ma
`fromPhone()` regge con `holdPad && holdPhone` (Proteggi e Ripristino tenuti dal telefono: `test_phone_never_*`).

- [ ] **Step 6: Vederli passare, con tutte le prove dell'interfaccia**

Run: `cd firmware && pio test -e native -f test_pad_ui -f test_ui -f test_m5_ui -f test_core -f test_core_golden 2>&1 | tail -n 12`
Expected: tutti `PASSED`; `test_phone_never_allows_any_sequence` (9 × 4 096 sequenze) sotto i 30 s (nel pre-flight
meno di 1 s). Prova a mano facoltativa: togliendo la riga di `Forget` da `phoneForbidden` e la sua prima barriera,
`test_phone_never_allows_any_sequence` fallisce su «Dimentica tutte le reti».

- [ ] **Step 7: Commit**

```bash
git add firmware/src/core/ui.h firmware/src/core/ui.cpp firmware/src/core/ui_pad.cpp firmware/src/core/model.h firmware/test/test_pad_ui firmware/test/test_m5_ui
git commit -m "Controller dal telefono: regola §7 dove nasce perm-reply (CONSENTI dal telefono rifiutato, NEGA sì), R1 e F11 (Proteggi, Ripristino, abbinamento col PC, Dimentica abbinamento e Wi-Fi mai dal telefono, tre barriere), sorgente nel PermEvent e nel NetRequest, 9 × 4 096 sequenze provate"
```

---

### Task 6: Voci, toast e schermata del codice del telefono

**Files:**
- Modify: `firmware/src/core/model.h` (`ScreenId::PadCode`, `Sys::padKind`, `Sys::padPhoneBonded`, `UiState::gbForgetPhoneUntil`), `firmware/src/core/model.cpp` (`screenKey`, `holdLabel`: F9)
- Modify: `firmware/src/core/menu.h` (`ActId::GbPairPhone`, `GbForgetPhone`), `firmware/src/core/menu.cpp` (`actKey`, `padStatus`, Menu › Gioco e › Controller, pausa della partita per tipo: F7)
- Modify: `firmware/src/core/ui.h` (`PadReq`, `padLinkChanged`, `padPasskey`, `padPairFailed`, `quietLost_`), `firmware/src/core/ui.cpp` (`act`: PadCode, anche `Sleep` (F9); `activate`: armata; `tick` e Ripristino: `gbForgetPhoneUntil` (F8)), `firmware/src/core/ui_pad.cpp`
- Modify: `firmware/src/core/power.cpp` (`powerInputs`: il codice a schermo tiene Attivo, F6)
- Modify: `firmware/src/ui/screens.cpp` (`padCodeS`)
- Test: `firmware/test/test_pad_ui/test_main.cpp`, `firmware/test/test_power/test_main.cpp`

**Interfaces:**
- Consumes: `pad::Kind`, `pad::Link` (Task 2, 4).
- Produces:
  - `enum class PadReq : uint8_t { None, Pair, Forget, PairPhone, ForgetPhone, CancelPair };`
  - `RefreshKind Ui::padLinkChanged(pad::Link l, pad::Kind kind, bool xboxBonded, bool phoneBonded, int batt, Ms now);` (la firma vecchia a 4 argomenti resta e passa `pad::Kind::Xbox` e `m_.sys.padPhoneBonded`)
  - `RefreshKind Ui::padPasskey(uint32_t key, Ms now);` `RefreshKind Ui::padPairFailed(Ms now);`
  - `ScreenId::PadCode` (chiave `"padcode"`), codice in `ScreenEntry::code` («123456»).

- [ ] **Step 1: Test che falliscono**

In `test_pad_ui/test_main.cpp`:

```cpp
static MenuList padMenu(Bench &b) {
  Model &m = b.ui.model();
  m.sys.padHw = true;
  MenuList L;
  menuList(MenuId::GbPad, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_FALSE(L.truncated);
  return L;
}

static int itemIndex(const MenuList &L, const char *label) {
  for (int i = 0; i < L.n; i++)
    if (strcmp(L.items[i].label, label) == 0) return i;
  return -1;
}

// Sceglie la voce sel di Menu › Gioco › Controller col controller (gbAct è privato: si passa dal menu, come sulla scheda).
static void pick(Bench &b, int sel) {
  b.ui.model().sys.padHw = true;
  if (b.topId() != ScreenId::List || top(b.ui.model()).menu != MenuId::GbPad) pushList(b, MenuId::GbPad);
  top(b.ui.model()).sel = sel;
  b.tap(pad::A);
}

static void test_gb_pad_phone_items() {
  Bench b;
  b.boot();
  const MenuList L = padMenu(b);
  TEST_ASSERT_EQUAL(5, L.n);
  TEST_ASSERT_EQUAL(1, itemIndex(L, "Associa controller"));
  TEST_ASSERT_EQUAL(2, itemIndex(L, "Dimentica controller"));
  TEST_ASSERT_EQUAL(3, itemIndex(L, "Associa telefono"));
  TEST_ASSERT_EQUAL(4, itemIndex(L, "Dimentica telefono"));
  TEST_ASSERT_EQUAL(ActId::GbPairPhone, L.items[3].act);
  TEST_ASSERT_EQUAL(ActId::GbForgetPhone, L.items[4].act);
}

static void test_pair_phone_request() {
  Bench b;
  b.boot();
  pick(b, 3);  // Associa telefono
  TEST_ASSERT_TRUE(b.ui.model().set.padOn);
  TEST_ASSERT_EQUAL(PadReq::PairPhone, b.ui.takePadReq());
  TEST_ASSERT_EQUAL_STRING("Metti l'app Calamaio sulla scheda Controller", b.ui.model().toast.msg);
}

// Review Focus «abbinamento sovrascritto»: dimenticare il telefono non tocca il controller, il Bluetooth resta acceso.
static void test_forget_phone_keeps_controller() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.set.padOn = true;
  m.sys.padBonded = true;
  m.sys.padPhoneBonded = true;
  pick(b, 4);  // Dimentica telefono
  TEST_ASSERT_EQUAL_STRING("Scegli ancora per confermare", m.toast.msg);
  pick(b, 4);
  TEST_ASSERT_EQUAL(PadReq::ForgetPhone, b.ui.takePadReq());
  TEST_ASSERT_FALSE(m.sys.padPhoneBonded);
  TEST_ASSERT_TRUE(m.sys.padBonded);
  TEST_ASSERT_TRUE(m.set.padOn);
  TEST_ASSERT_EQUAL_STRING("Telefono dimenticato", m.toast.msg);
}

static void test_forget_controller_keeps_phone_radio() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.set.padOn = true;
  m.sys.padBonded = true;
  m.sys.padPhoneBonded = true;
  pick(b, 2);  // Dimentica controller
  pick(b, 2);
  TEST_ASSERT_EQUAL(PadReq::Forget, b.ui.takePadReq());
  TEST_ASSERT_TRUE(m.set.padOn);  // resta il telefono
  TEST_ASSERT_TRUE(m.sys.padPhoneBonded);
}

static void test_forget_phone_none() {
  Bench b;
  b.boot();
  pick(b, 4);
  TEST_ASSERT_EQUAL_STRING("Nessun telefono associato", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(PadReq::None, b.ui.takePadReq());
}

static void test_phone_link_toasts() {
  Bench b;
  b.boot();
  b.ui.model().set.padOn = true;
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, false, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Telefono collegato", b.ui.model().toast.msg);
  const MenuList L = padMenu(b);
  TEST_ASSERT_EQUAL_STRING("telefono collegato", L.items[0].val);
  b.now += 5000;
  b.ui.padLinkChanged(pad::Link::Connecting, pad::Kind::Phone, false, true, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Telefono scollegato", b.ui.model().toast.msg);
  b.now += 5000;
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Xbox, true, true, 80, b.now);
  TEST_ASSERT_EQUAL_STRING("Controller collegato", b.ui.model().toast.msg);
}

// Dopo «Dimentica telefono» lo scollegamento che segue non si annuncia (come «Dimentica controller»).
static void test_forget_phone_then_disconnect_is_quiet() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.set.padOn = true;
  m.sys.padBonded = true;
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, true, true, -1, b.now);
  pick(b, 4);
  pick(b, 4);
  b.now += 200;
  b.ui.padLinkChanged(pad::Link::Connecting, pad::Kind::Phone, true, false, -1, b.now);
  TEST_ASSERT_EQUAL_STRING("Telefono dimenticato", m.toast.msg);
}

static void test_passkey_screen() {
  Bench b;
  b.boot();
  b.ui.model().set.padOn = true;
  b.ui.padLinkChanged(pad::Link::Pairing, pad::Kind::Phone, false, false, -1, b.now);
  b.ui.padPasskey(42, b.now);
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  TEST_ASSERT_EQUAL_STRING("000042", top(b.ui.model()).code);
  b.ui.padPasskey(987654, b.now + 10);  // secondo tentativo: stesso schermo, codice nuovo
  TEST_ASSERT_EQUAL_STRING("987654", top(b.ui.model()).code);
  TEST_ASSERT_EQUAL(1, [&] { int n = 0; for (int i = 0; i < b.ui.model().ui.depth; i++) n += b.ui.model().ui.stack[i].id == ScreenId::PadCode; return n; }());
  b.ui.padLinkChanged(pad::Link::Ready, pad::Kind::Phone, false, true, -1, b.now + 20);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
  TEST_ASSERT_EQUAL_STRING("Telefono collegato", b.ui.model().toast.msg);
}

static void test_passkey_failed() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  b.ui.padPairFailed(b.now + 10);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
  TEST_ASSERT_EQUAL_STRING("Associazione non riuscita", b.ui.model().toast.msg);
}

// LUNGA ANNULLA sul codice: via la schermata, l'associazione si ferma; gli altri gesti non portano via.
static void test_passkey_long_cancels() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  b.press(3);  // Menu: resta
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  b.tap(pad::B);  // B = indietro come la pressione lunga
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
  TEST_ASSERT_EQUAL(PadReq::CancelPair, b.ui.takePadReq());
}

static void test_padcode_key() { TEST_ASSERT_EQUAL_STRING("padcode", screenKey(ScreenId::PadCode)); }

// La schermata si disegna e non è vuota (testi presenti nei caratteri).
static void test_padcode_draws() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  static Canvas<250, 122> cv;
  ui::compose(cv, kHeltec213, b.ui.model(), 0, b.now, b.wc());
  int ink = 0;
  for (int y = 30; y < 80; y++)
    for (int x = 0; x < 250; x++) ink += cv.get(x, y);
  TEST_ASSERT_TRUE(ink > 300);  // le 6 cifre grandi
}
```

Le correzioni del pre-flight F7, F8, F9 (nello stesso file, prima di `main`):

```cpp
// F8: «conferma?» di «Dimentica telefono» scade a schermo come quello del controller; il Ripristino la chiude.
static void test_gb_forget_phone_confirm_expires() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.sys.padHw = true;
  m.set.padOn = true;
  m.sys.padPhoneBonded = true;
  pushList(b, MenuId::GbPad);
  top(m).sel = 4;  // Dimentica telefono
  b.tap(pad::A);
  TEST_ASSERT_EQUAL_STRING("conferma?", b.ui.items(b.now, b.wc()).items[4].val);
  const Ms until = m.ui.gbForgetPhoneUntil;
  TEST_ASSERT_TRUE(until > b.now);
  b.run(until - b.now);
  TEST_ASSERT_EQUAL(until, m.ui.gbForgetPhoneUntil);
  b.now += 1;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.tick());
  TEST_ASSERT_EQUAL(0, m.ui.gbForgetPhoneUntil);
  TEST_ASSERT_EQUAL_STRING("", b.ui.items(b.now, b.wc()).items[4].val);
  pushList(b, MenuId::Security);
  const int k = findItem(b, ItemKind::Act, ScreenId::Home, ActId::Factory);
  top(m).sel = k;
  b.tap(pad::A);
  b.tap(pad::A);  // arma
  m.ui.gbForgetPhoneUntil = b.now + kForgetMs;
  b.tap(pad::A, 3200);  // conferma dal controller
  TEST_ASSERT_EQUAL(ScreenId::Boot, b.topId());
  TEST_ASSERT_EQUAL(0, m.ui.gbForgetPhoneUntil);
}

// F9: sul codice del telefono il tenuto di 3 s (PRG o controller) non mette Riposo sopra il codice.
static void test_padcode_hold_does_nothing() {
  Bench b;
  b.boot();
  b.ui.padPasskey(123456, b.now);
  TEST_ASSERT_NULL(holdLabel(b.ui.model(), b.now));
  b.tap(pad::A, 3200);
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  b.now += 100;
  b.ui.gesture(Gesture::VeryLong, b.now, b.wc());
  TEST_ASSERT_EQUAL(ScreenId::PadCode, b.topId());
  b.ui.padPasskey(654321, b.now);  // un secondo codice non ne impila un altro
  int codes = 0;
  for (int k = 0; k < b.ui.model().ui.depth; k++) codes += b.ui.model().ui.stack[k].id == ScreenId::PadCode;
  TEST_ASSERT_EQUAL(1, codes);
  b.tap(pad::B);
  TEST_ASSERT_TRUE(b.topId() != ScreenId::PadCode);
}

// F7: in partita la pausa dice quale controller si è scollegato.
static void test_gb_pause_text_by_kind() {
  Bench b;
  b.boot();
  Model &m = b.ui.model();
  m.gb.pause = gb::PauseWhy::Controller;
  MenuList L;
  m.sys.padKind = pad::Kind::Phone;
  menuList(MenuId::GbPause, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Telefono scollegato", L.items[0].label);
  m.sys.padKind = pad::Kind::Xbox;
  menuList(MenuId::GbPause, kHeltec213, m, b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Controller scollegato", L.items[0].label);
}
```

In `test_power/test_main.cpp`, in fondo a `test_power_inputs_from_model` (F6):

```cpp
  TEST_ASSERT_FALSE(in.playing);
  pushScreen(m, makeScreen(ScreenId::PadCode));  // F6 (0.14.0): il codice del telefono tiene sveglia la scheda
  TEST_ASSERT_TRUE(powerInputs(m).playing);
```

(`#include "ui/screens.h"` e `#include "gfx/canvas.h"` in cima, come in `test_m5_ui`.) Aggiungere i `RUN_TEST` (anche
`test_gb_forget_phone_confirm_expires`, `test_padcode_hold_does_nothing`, `test_gb_pause_text_by_kind`) e
aggiornare `test_gb_pad_items` (oggi conta 3 voci): ora 5, con le stesse prime tre.

- [ ] **Step 2: Vederli fallire**

Run: `cd firmware && pio test -e native -f test_pad_ui -f test_power 2>&1 | tail -n 15`
Expected: errori di compilazione (`GbPairPhone`, `PadReq::PairPhone`, `padPasskey`, `ScreenId::PadCode`).

- [ ] **Step 3: Modello e menu**

`model.h`: in `ScreenId` dopo `GbPlay,`:
`PadCode,  // 0.14.0: codice di associazione del telefono (passkey, 6 cifre in ScreenEntry::code)`; in `Sys` dopo
`uint8_t padBatt;`:

```cpp
  pad::Kind padKind;    // 0.14.0: tipo del collegamento o dell'associazione in corso
  bool padPhoneBonded;  // 0.14.0: telefono associato (padBonded = l'Xbox)
```

in `UiState` dopo `Ms gbForgetUntil;`: `Ms gbForgetPhoneUntil;  // «Dimentica telefono» armata fino a`.
`model.cpp`: in `screenKey` aggiungere `"padcode"` in fondo all'elenco; in `holdLabel` la prima riga diventa (F9: il
tenuto di 3 s sul codice metterebbe Riposo sopra il codice, e un secondo `padPasskey` ne spingerebbe un altro)

```cpp
  // F9 (0.14.0): sul codice del telefono il tenuto non fa niente (Riposo coprirebbe il codice).
  if (tp.id == ScreenId::Boot || tp.id == ScreenId::Sleep || tp.id == ScreenId::BattLow || tp.id == ScreenId::Ota ||
      tp.id == ScreenId::PadCode)
    return nullptr;
```

`menu.h`: in `ActId` dopo `Theme,`: `GbPairPhone, GbForgetPhone,  // 0.14.0: Menu › Gioco › Controller, telefono`.
`menu.cpp`: in `actKey` aggiungere `"gbpairphone", "gbforgetphone"` dopo `"theme"`. `padStatus`, caso `Ready`:

```cpp
    case pad::Link::Ready:
      if (w.padKind == pad::Kind::Phone) return "telefono collegato";
      if (!w.padHasBatt) return "collegato";
```

e il `default`: `return w.padBonded || w.padPhoneBonded ? "lo cerco" : "nessuno associato";`.
Menu › Gioco, voce «Controller»: aiuto `"Associa il controller Xbox o il telefono, guardane lo stato o dimenticali. Uno alla volta."`.
Menu › Gioco › Controller diventa:

```cpp
  if (id == MenuId::GbPad) {  // 0.11.0: Menu › Gioco › Controller; 0.14.0: anche il telefono
    out.title = "Controller";
    const bool pairing = w.padLink == pad::Link::Pairing;
    B.info("Stato", padStatus(m, v, sizeof v),
           "Croce su e giù: sposta. A: scegli. B: indietro. Tasto Menu: apre il Menu. Ai permessi rispondi tenendo premuto A per 3 secondi. Dal telefono non si confermano CONSENTI, Proteggi, Ripristino, abbinamento col PC e Wi-Fi.");
    B.act("Associa controller", pairing && w.padKind == pad::Kind::Xbox ? "in corso" : "", ActId::GbPair,
          "Tieni premuto il tasto di associazione del controller finché il logo Xbox lampeggia veloce. Il nuovo sostituisce il vecchio controller, mai il telefono.");
    B.act("Dimentica controller", m.ui.gbForgetUntil > now ? "conferma?" : "", ActId::GbForget,
          "Cancella l'associazione del controller. Il Bluetooth si spegne se non resta associato il telefono.");
    B.act("Associa telefono", pairing && w.padKind == pad::Kind::Phone ? "in corso" : "", ActId::GbPairPhone,
          "Apri l'app Calamaio sull'iPhone, scheda Controller: la scheda la trova e mostra un codice da scrivere sul telefono. Il nuovo sostituisce il vecchio telefono, mai il controller.");
    B.act("Dimentica telefono", m.ui.gbForgetPhoneUntil > now ? "conferma?" : "", ActId::GbForgetPhone,
          "Cancella l'associazione del telefono. Il Bluetooth si spegne se non resta associato il controller.");
    return;
  }
```

Contare a mano i byte degli aiuti: tutti sotto `kHelpLen` (224); il test `L.truncated` lo controlla (l'aiuto di
«Stato», riscritto per R1 e F11, è di 219 byte).

Menu › Gioco › Pausa (`MenuId::GbPause`), F7: la riga del controller scollegato dice di che tipo è:

```cpp
    if (m.gb.pause == gb::PauseWhy::Controller && w.padKind == pad::Kind::Phone)  // F7 (0.14.0)
      B.info("Telefono scollegato", "", "Riapri l'app Calamaio sulla scheda Controller: si ricollega da solo. Poi Riprendi.");
    else if (m.gb.pause == gb::PauseWhy::Controller)
      B.info("Controller scollegato", "", "Riaccendi il controller con il tasto Xbox: si ricollega da solo. Poi Riprendi.");
```

(al posto delle due righe `if (m.gb.pause == gb::PauseWhy::Controller)` / `B.info("Controller scollegato", …)`).

- [ ] **Step 4: Interfaccia (`ui.h`, `ui.cpp`, `ui_pad.cpp`)**

`ui.h`: `enum class PadReq : uint8_t { None, Pair, Forget, PairPhone, ForgetPhone, CancelPair };`; dichiarazioni

```cpp
  RefreshKind padLinkChanged(pad::Link l, pad::Kind kind, bool xboxBonded, bool phoneBonded, int batt, Ms now);
  RefreshKind padLinkChanged(pad::Link l, bool bonded, int batt, Ms now) {
    return padLinkChanged(l, pad::Kind::Xbox, bonded, m_.sys.padPhoneBonded, batt, now);
  }
  RefreshKind padPasskey(uint32_t key, Ms now);  // 0.14.0: codice da mostrare (mai nel registro)
  RefreshKind padPairFailed(Ms now);             // 0.14.0: telefono non autenticato
```

e fra i privati `bool quietLost_ = false;  // dopo un «Dimentica»: il prossimo scollegamento non si annuncia`.

`ui.cpp`, in `activate()` accanto alle altre armate:
`if (!(it.kind == ItemKind::Act && it.act == ActId::GbForgetPhone)) m_.ui.gbForgetPhoneUntil = 0;` e nello `switch`
di `doAct` (oggi `case ActId::GbPair: case ActId::GbForget: gbAct(id, now, msg, sizeof msg);`, ~riga 841) aggiungere
`case ActId::GbPairPhone: case ActId::GbForgetPhone:` agli stessi rami. In `act()`, ramo `Action::Menu`, accanto a Pair:
`if (tp.id == ScreenId::PadCode) { setAction("Prima scrivi il codice sul telefono o annulla"); return finish(t); }`;
in testa al ramo `Action::Back`:

```cpp
    if (tp.id == ScreenId::PadCode) {  // 0.14.0: LUNGA ANNULLA, l'associazione del telefono si ferma
      popScreen(m);
      padReq_ = PadReq::CancelPair;
      redraw(RefreshKind::Quick);
      setAction("Associazione del telefono annullata");
      return finish(t);
    }
```

e in testa al ramo `Action::Ok` e `Action::Next`/`Action::Prev`:
`if (tp.id == ScreenId::PadCode) { setAction("Scrivi il codice sul telefono"); return finish(t); }`. Prima del ramo
`if (a == Action::Sleep) {` che mette Riposo (F9):

```cpp
  if (a == Action::Sleep && tp.id == ScreenId::PadCode) {  // F9: il tenuto non manda a riposo sul codice
    setAction("Scrivi il codice sul telefono o annulla");
    return finish(t);
  }
```

In `Ui::tick`, sotto la riga di `gbForgetUntil` (F8: «conferma?» di «Dimentica telefono» deve scadere anche a schermo):

```cpp
  if (m.ui.gbForgetPhoneUntil && t > m.ui.gbForgetPhoneUntil) { m.ui.gbForgetPhoneUntil = 0; redraw(RefreshKind::Partial); }  // F8
```

e nel Ripristino di fabbrica (`confirmAction`) la riga delle armate diventa
`m_.ui.forgetUntil = m_.ui.forgetNetUntil = m_.ui.unpairUntil = m_.ui.gbForgetUntil = m_.ui.gbForgetPhoneUntil = 0;`.

`ui_pad.cpp`, `padLinkChanged` nuova:

```cpp
RefreshKind Ui::padLinkChanged(pad::Link l, pad::Kind kind, bool xboxBonded, bool phoneBonded, int batt, Ms now) {
  req_ = RefreshKind::None;
  Sys &w = m_.sys;
  const pad::Link was = w.padLink;
  const pad::Kind wasKind = w.padKind;
  const bool hasBatt = batt >= 0 && batt <= 100;
  const uint8_t b = hasBatt ? static_cast<uint8_t>(batt) : 0;
  if (l == was && kind == wasKind && xboxBonded == w.padBonded && phoneBonded == w.padPhoneBonded &&
      hasBatt == w.padHasBatt && b == w.padBatt)
    return finish(now);
  w.padLink = l;
  w.padKind = kind;
  w.padBonded = xboxBonded;
  w.padPhoneBonded = phoneBonded;
  w.padHasBatt = hasBatt;
  w.padBatt = b;
  const bool phone = kind == pad::Kind::Phone;
  if (top(m_).id == ScreenId::PadCode && l != pad::Link::Pairing) popScreen(m_);  // codice non più utile
  // Col Bluetooth spento di proposito, o dopo un «Dimentica», lo scollegamento che segue non si annuncia.
  const bool on = m_.set.padOn && !quietLost_;
  if (l != was) {
    if (l == pad::Link::Ready) toast(m_, phone ? "Telefono collegato" : "Controller collegato", now);
    else if (on && was == pad::Link::Ready)
      toast(m_, wasKind == pad::Kind::Phone ? "Telefono scollegato" : "Controller scollegato", now);
    else if (on && was == pad::Link::Pairing) toast(m_, "Associazione scaduta", now);
    quietLost_ = false;
  }
  redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::padPasskey(uint32_t key, Ms now) {
  req_ = RefreshKind::None;
  char code[8];
  Sb c(code, sizeof code);
  for (uint32_t d = 100000; d; d /= 10) c.ch(static_cast<char>('0' + (key / d) % 10));
  if (top(m_).id != ScreenId::PadCode) pushScreen(m_, makeScreen(ScreenId::PadCode));
  scopy(top(m_).code, code);
  redraw(RefreshKind::Partial);
  setAction("Codice per il telefono a schermo");
  return finish(now);
}

RefreshKind Ui::padPairFailed(Ms now) {
  req_ = RefreshKind::None;
  if (top(m_).id == ScreenId::PadCode) popScreen(m_);
  toast(m_, "Associazione non riuscita", now);
  redraw(RefreshKind::Partial);
  setAction("Telefono: associazione non riuscita (non autenticato)");
  return finish(now);
}
```

(`Sb::ch` esiste: la usa `codeGroups` in screens.cpp; se `Sb` non l'avesse, `snprintf(code, sizeof code, "%06lu", …)`.)

`gbAct`:

```cpp
void Ui::gbAct(ActId id, Ms now, char *msg, size_t cap) {
  Settings &st = m_.set;
  Sys &w = m_.sys;
  Sb b(msg, cap);
  if (id == ActId::GbPair || id == ActId::GbPairPhone) {
    const bool phone = id == ActId::GbPairPhone;
    st.padOn = true;  // associare accende il Bluetooth (resta acceso finché c'è un associato)
    padReq_ = phone ? PadReq::PairPhone : PadReq::Pair;
    toast(m_, phone ? "Metti l'app Calamaio sulla scheda Controller" : "Metti il controller in associazione", now);
    b.s(phone ? "Associa telefono: apri l'app Calamaio, scheda Controller, e scrivi il codice che compare qui"
              : "Associa controller: tieni premuto il tasto di associazione finché il logo lampeggia veloce");
    return;
  }
  const bool phone = id == ActId::GbForgetPhone;
  Ms &until = phone ? m_.ui.gbForgetPhoneUntil : m_.ui.gbForgetUntil;
  if (!(phone ? w.padPhoneBonded : w.padBonded)) {
    toast(m_, phone ? "Nessun telefono associato" : "Nessun controller associato", now);
    b.s(phone ? "Dimentica telefono: nessun telefono associato" : "Dimentica controller: nessun controller associato");
    return;
  }
  if (!(until > now)) {  // come «Dimentica rete», seconda scelta entro kForgetMs
    until = now + kForgetMs;
    toast(m_, "Scegli ancora per confermare", now);
    b.s(phone ? "Dimentica telefono: scegli ancora per confermare" : "Dimentica controller: scegli ancora per confermare");
    return;
  }
  until = 0;
  padReq_ = phone ? PadReq::ForgetPhone : PadReq::Forget;
  if (phone) w.padPhoneBonded = false;
  else w.padBonded = false;
  st.padOn = w.padBonded || w.padPhoneBonded;  // il Bluetooth si spegne solo senza associati
  quietLost_ = true;
  toast(m_, phone ? "Telefono dimenticato" : "Controller dimenticato", now);
  b.s(phone ? "Telefono dimenticato: per usarlo di nuovo, Associa telefono"
            : "Controller dimenticato: per usarlo di nuovo, Associa controller");
}
```

Il test vecchio `test_forget_then_disconnect_is_quiet` resta verde (con `quietLost_` e `padOn` falso).

- [ ] **Step 5: Disegno (`ui/screens.cpp`)**

Accanto a `pairS`:

```cpp
// 0.14.0: codice di associazione del telefono (passkey di LE Secure Connections). Bloccata per i temi.
void padCodeS(Surface &c, const DeviceProfile &dev, const Model &m) {
  const int W = dev.W;
  const ScreenEntry &tp = cal::top(m);
  c.fillRect(0, 0, W, 15, INK);
  T(c, "TELEFONO", 4, 1, 12, Wt(700).col(PAPER));
  T(c, "Codice per il telefono", W / 2.0, 21, 10, Wt(600).center().mx(W - 8));
  char code[16];
  codeGroups(tp.code, code, sizeof code);
  T(c, code, W / 2.0, 33, 44, Wt(700).center());
  T(c, "Scrivilo sull'iPhone quando lo chiede", W / 2.0, 86, 10, Wt(500).center().mx(W - 8));
  dotsH(c, 0, W, 104);
  T(c, "LUNGA ANNULLA", 4, 109, 8, Px(400).mx(W - 8));
}
```

e nel `switch` di `draw`: `case ScreenId::PadCode: padCodeS(c, dev, m); break;`. Se `codeGroups` è dopo, spostare
`padCodeS` sotto `pairS`. Controllare che PadCode non sia nell'elenco delle schermate personalizzabili dei temi
(`grep -n '"pair"' src/core/theme_*.cpp`): se c'è un elenco delle bloccate, aggiungere `"padcode"` lì.

- [ ] **Step 6: Il codice a schermo tiene sveglia la scheda (`core/power.cpp`, F6)**

Il sonno chiuderebbe il Bluetooth a metà associazione. In `powerInputs`:

```cpp
  // F6 (0.14.0): anche il codice del telefono a schermo tiene Attivo (il sonno chiuderebbe l'associazione)
  in.playing = top(m).id == ScreenId::Game || top(m).id == ScreenId::PadCode;
```

(al posto di `in.playing = top(m).id == ScreenId::Game;`).

- [ ] **Step 7: Vederli passare, con golden e temi**

Run: `cd firmware && pio test -e native -f test_pad_ui -f test_power -f test_ui -f test_core_golden -f test_theme_menu -f test_theme_parity 2>&1 | tail -n 12`
Expected: tutti `PASSED`; i golden non cambiano (Menu › Gioco › Controller solo con `padHw`).

- [ ] **Step 8: Commit**

```bash
git add firmware/src/core firmware/src/ui/screens.cpp firmware/test/test_pad_ui firmware/test/test_power
git commit -m "Controller dal telefono: Associa e Dimentica telefono separati dal controller, toast per tipo, schermata del codice (LUNGA ANNULLA, tiene sveglia, niente Riposo), conferma che scade, pausa della partita per tipo"
```

---

### Task 7: Servizio: avatar a pezzi (avatar-offer, GET /avatars, nessun rinvio per 60 s dopo un fallimento)

R4 del registro (difetto dal vivo del Gufo, «Fatti verificati»). Due decisioni:

- **Capacità in `hello`, non versione del firmware.** `hello.caps = ["avatar-dl"]`. Ci sono due rami con numerazioni
  parallele (`0.13.0-ed` dell'editor, `0.14.0` qui): un confronto di versioni (come `ota.KnowsRunPose` per la posa
  `run`) sbaglierebbe una build dell'editor che prende la funzione dopo, o una di prova. La capacità la dichiara chi sa
  scaricare, è provata dallo schema (che oggi chiude `hello`: senza il campo nuovo una scheda 0.14.0 non passerebbe), e
  i servizi vecchi la ignorano (Go scarta i campi sconosciuti): una scheda 0.14.0 con un servizio vecchio riceve ancora
  `avatar`, che il firmware continua a capire.
- **Niente autenticazione per l'offerta.** `avatar-need` e `avatar` oggi valgono anche senza (PROTOCOLLO «Avatar»: gli
  avatar non sono dati sensibili, come lo `state`). Se l'offerta la chiedesse, una scheda non abbinata (sola lettura)
  resterebbe proprio sul messaggio `avatar` che si rompe. I temi la chiedono perché cambiano l'aspetto in modo
  persistente su comando del servizio; un avatar arriva solo dopo un `avatar-need` della scheda e finisce in una cache
  da 8 posti. Regola del token al posto dell'autenticazione: 16 byte casuali, monouso, valido solo per l'IP della
  connessione che ha ricevuto l'offerta, 5 minuti (`TokenTTL`), e muore con l'`avatar-ack`, con la chiusura della
  connessione e allo scadere dei 30 s senza ack. Chi sta in rete non ottiene niente che lo `state` non dica già.

**Files:**
- Modify: `protocol/schema/message.schema.json` (`hello.caps`, `avatarOffer`, `avatarAck`, tipi e `$comment`)
- Create: `protocol/examples/avatar-offer.json`, `avatar-ack.json`, `avatar-ack-errore.json`, `hello-avatar-dl.json`
- Create: `service/internal/display/avatar_dl.go`, `service/internal/display/avatar_dl_test.go`
- Modify: `service/internal/display/avatar.go` (`sendAvatars`, `avatarPacket`), `hub.go` (`Hello.Caps`, campi di `Hub` e `conn`, `NewHub`, `Handler`, chiusura, `run`, `readLoop`), `remote.go` (`inbound.avatarAck`), `themes.go` (`ServeTheme` con le regole in comune), `schema_test.go`

**Interfaces:**
- Consumes: niente dei Task 1–6.
- Produces (filo, li consumano i Task 8 e 9): `hello.caps` (array di id, al massimo 8); `avatar-offer {id, crc, size, token}` (servizio → display, `size` 2 172–7 072); `GET /avatars/<token>` → 200 `application/octet-stream`, `Content-Length`, `X-Calamaio-Crc`, `Cache-Control: no-store`; 404/410/403 come i temi; `avatar-ack {id, crc, ok, error?}` (display → servizio, `error` ≤ 127). Esempio vero: `protocol/examples/avatar-offer.json` (il Demone, `crc` 2 735 932 229 = `0xA3130345`).
- Produces (Go): `const CapAvatarDl = "avatar-dl"`, `Hub.AvatarAckTimeout` (30 s), `Hub.AvatarFailHold` (60 s), `Hub.ServeAvatar`, `blobTokenStatus(tok, known, used, exp, ip, want, now) int`, `serveBlob(w, r, bin, crc)`, `avatarPacket(src, id, legacy)`.

- [ ] **Step 1: Schema ed esempi**

In `message.schema.json`: nell'`enum` di `t`, `"avatar", "avatar-need",` → `"avatar", "avatar-need", "avatar-offer", "avatar-ack",`;
dopo la riga `allOf` di `avatar-need`:

```json
    { "if": { "properties": { "t": { "const": "avatar-offer" } } }, "then": { "$ref": "#/$defs/avatarOffer" } },
    { "if": { "properties": { "t": { "const": "avatar-ack" } } }, "then": { "$ref": "#/$defs/avatarAck" } },
```

in `hello.properties`, dopo `paired` (con la virgola):

```json
        "caps": { "type": "array", "maxItems": 8, "uniqueItems": true, "items": { "type": "string", "minLength": 1, "maxLength": 24, "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$" },
                  "$comment": "firmware 0.14.0: capacità del display. avatar-dl = avatar a pezzi (avatar-offer → GET /avatars/<token> → avatar-ack). Il servizio ignora quelle che non conosce; assente nei firmware vecchi" }
```

e prima di `"accessoryId"`:

```json
    "avatarOffer": {
      "$comment": "servizio → display, solo a un display con hello.caps avatar-dl, anche non autenticato (gli avatar non sono dati sensibili, come avatar): CAVT da scaricare con GET http://<pc>:<porta>/avatars/<token> (token monouso, 5 minuti, solo per l'IP del display). Una alla volta per connessione. Risposta: avatar-ack",
      "type": "object",
      "required": ["t", "v", "id", "crc", "size", "token"],
      "additionalProperties": false,
      "properties": {
        "t": true, "v": true,
        "id": { "$ref": "#/$defs/avatarId" },
        "crc": { "$ref": "#/$defs/crc32" },
        "size": { "type": "integer", "minimum": 2172, "maximum": 7072, "$comment": "byte del CAVT: 72 + pose × fotogrammi × 175 (6 pose e 2 fotogrammi … 10 pose e 4 fotogrammi)" },
        "token": { "type": "string", "pattern": "^[0-9a-f]{32}$" }
      }
    },
    "avatarAck": {
      "$comment": "display → servizio: esito di un avatar-offer dopo download, controllo del CAVT e salvataggio nella cache",
      "type": "object",
      "required": ["t", "v", "id", "crc", "ok"],
      "additionalProperties": false,
      "properties": {
        "t": true, "v": true,
        "id": { "$ref": "#/$defs/avatarId" },
        "crc": { "$ref": "#/$defs/crc32" },
        "ok": { "type": "boolean" },
        "error": { "type": "string", "maxLength": 127 }
      }
    },

```

Nel `$comment` di testa, dopo «… e avatar-need (display → servizio)»: «; il firmware 0.14.0 aggiunge hello.caps e, per i
display con caps avatar-dl, avatar-offer (servizio → display, l'avatar si scarica con GET /avatars/<token>) e
avatar-ack (display → servizio)».

Esempi (una riga ciascuno):

```json
{"t":"avatar-offer","v":1,"id":"demone","crc":2735932229,"size":7072,"token":"0123456789abcdef0123456789abcdef"}
```
(`protocol/examples/avatar-offer.json`)

```json
{"t":"avatar-ack","v":1,"id":"demone","crc":2735932229,"ok":true}
```
(`protocol/examples/avatar-ack.json`)

```json
{"t":"avatar-ack","v":1,"id":"demone","crc":2735932229,"ok":false,"error":"download troppo lento"}
```
(`protocol/examples/avatar-ack-errore.json`)

```json
{"t":"hello","v":1,"board":"calamaio-3f2a","device":"heltec213","class":"S","fw":"0.14.0","w":250,"h":122,"paired":true,"caps":["avatar-dl"]}
```
(`protocol/examples/hello-avatar-dl.json`: è quello che il firmware manda dal Task 9)

In `schema_test.go`, `TestExamplesMatchSchema`: `"avatar", "avatar-need", "bye",` → `"avatar", "avatar-need", "avatar-offer", "avatar-ack", "bye",`.

- [ ] **Step 2: Prove che falliscono (`avatar_dl_test.go`)**

```go
package display

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

const avBoard = "calamaio-a7a7"

// helloDl: hello del firmware 0.14.0, con la capacità avatar-dl.
func helloDl(board string) string {
	return fmt.Sprintf(`{"t":"hello","v":1,"board":%q,"device":"heltec213","class":"S","fw":"0.14.0","w":250,"h":122,"paired":false,"caps":["avatar-dl"]}`, board)
}

// dlDisplay: scheda con avatar-dl (non abbinata: l'offerta non chiede l'autenticazione), fino allo state.
func dlDisplay(t *testing.T, r *rig, hello string) *client {
	t.Helper()
	c := r.dial(nil)
	c.send(hello)
	if w := c.next(2 * time.Second); w["t"] != "welcome" {
		t.Fatalf("atteso welcome: %v", w)
	}
	if s := c.next(2 * time.Second); s["t"] != "state" {
		t.Fatalf("atteso state: %v", s)
	}
	return c
}

func avGet(t *testing.T, r *rig, tok string) (int, []byte, http.Header) {
	t.Helper()
	res, err := http.Get(r.srv.URL + "/avatars/" + tok)
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	b, _ := io.ReadAll(res.Body)
	return res.StatusCode, b, res.Header
}

func ackJSON(id string, crc any, ok bool, why string) string {
	m := map[string]any{"t": "avatar-ack", "v": 1, "id": id, "crc": crc, "ok": ok}
	if why != "" {
		m["error"] = why
	}
	b, _ := json.Marshal(m)
	return string(b)
}

// Il giro intero: avatar-need → avatar-offer (uno alla volta) → GET a pezzi → avatar-ack → offerta seguente.
func TestAvatarOfferFlow(t *testing.T) {
	sch := compileSchema(t)
	r, m := avatarRig(t)
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto","demone"]}`)
	off := c.next(2 * time.Second)
	ob, _ := json.Marshal(off)
	validate(t, sch, "avatar-offer", ob)
	g := m.Lib.Get("gatto")
	if off["t"] != "avatar-offer" || off["id"] != "gatto" || off["crc"] != float64(g.CRC) || off["size"] != float64(len(g.Bin)) {
		t.Fatalf("offerta: %s", ob)
	}
	c.none(200 * time.Millisecond) // un'offerta alla volta
	tok := off["token"].(string)
	code, body, hd := avGet(t, r, tok)
	if code != 200 || !bytes.Equal(body, g.Bin) || hd.Get("X-Calamaio-Crc") != fmt.Sprintf("%08x", g.CRC) || hd.Get("Cache-Control") != "no-store" {
		t.Fatalf("download: %d, %d byte, %v", code, len(body), hd)
	}
	if code, _, _ := avGet(t, r, tok); code != http.StatusGone {
		t.Fatalf("token riusato: %d", code)
	}
	ack := ackJSON("gatto", g.CRC, true, "")
	validate(t, sch, "avatar-ack", []byte(ack))
	c.send(ack)
	off2 := c.next(2 * time.Second)
	if off2["t"] != "avatar-offer" || off2["id"] != "demone" || off2["token"] == tok {
		t.Fatalf("seconda offerta: %v", off2)
	}
	c.send(ackJSON("demone", off2["crc"], true, ""))
	waitLog(t, r, "avatar demone salvato dalla scheda", 1)
	c.none(200 * time.Millisecond)
}

// Senza la capacità (firmware ≤ 0.13): il messaggio avatar di sempre, mai un'offerta.
func TestAvatarWithoutCapsStaysLegacy(t *testing.T) {
	r, _ := avatarRig(t)
	c := dlDisplay(t, r, strings.Replace(helloDl(avBoard), `,"caps":["avatar-dl"]`, "", 1))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if a := c.next(2 * time.Second); a["t"] != "avatar" || a["id"] != "gatto" {
		t.Fatalf("atteso avatar: %v", a)
	}
}

// R4: dopo una consegna fallita lo stesso avatar non si rimanda alla stessa scheda per AvatarFailHold, nemmeno su una
// connessione nuova; passato il tempo sì.
func TestAvatarFailHoldAcrossConnections(t *testing.T) {
	r, m := avatarRig(t)
	r.hub.AvatarFailHold = 600 * time.Millisecond
	g := m.Lib.Get("gatto")
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c.next(2 * time.Second)
	c.send(ackJSON("gatto", g.CRC, false, "memoria insufficiente"))
	waitLog(t, r, "la scheda l'ha rifiutato: memoria insufficiente", 1)
	_ = c.ws.Close(websocket.StatusNormalClosure, "")
	c2 := dlDisplay(t, r, helloDl(avBoard))
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c2.none(250 * time.Millisecond)
	waitLog(t, r, "avatar gatto non rimandato", 1)
	time.Sleep(400 * time.Millisecond)
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if off := c2.next(2 * time.Second); off["t"] != "avatar-offer" || off["id"] != "gatto" {
		t.Fatalf("dopo la pausa: %v", off)
	}
	// un'altra scheda non è toccata dal fallimento della prima
	c3 := dlDisplay(t, r, helloDl("calamaio-b8b8"))
	c3.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if off := c3.next(2 * time.Second); off["t"] != "avatar-offer" {
		t.Fatalf("altra scheda: %v", off)
	}
}

// La connessione si chiude con l'offerta in corso (la 1011 vista dal vivo): fallita, niente rinvio subito.
func TestAvatarConnClosedMidOffer(t *testing.T) {
	r, _ := avatarRig(t)
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	tok := c.next(2 * time.Second)["token"].(string)
	_ = c.ws.Close(websocket.StatusInternalError, "")
	waitLog(t, r, "connessione chiusa durante la consegna", 1)
	if code, _, _ := avGet(t, r, tok); code != http.StatusNotFound {
		t.Fatalf("token dopo la chiusura: %d", code)
	}
	c2 := dlDisplay(t, r, helloDl(avBoard))
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c2.none(300 * time.Millisecond)
}

// Il ciclo visto dal vivo con la 0.13.0-ed: avatar mandato, connessione chiusa subito, riconnessione, avatar-need.
// Prima della 0.14.0 il servizio lo rimandava a ogni connessione (1 al secondo); ora aspetta AvatarFailHold.
func TestAvatarLegacyCloseHeld(t *testing.T) {
	r, _ := avatarRig(t)
	c := dlDisplay(t, r, hello(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if a := c.next(2 * time.Second); a["t"] != "avatar" {
		t.Fatalf("atteso avatar: %v", a)
	}
	_ = c.ws.Close(websocket.StatusInternalError, "")
	waitLog(t, r, "connessione chiusa subito dopo il messaggio avatar", 1)
	c2 := dlDisplay(t, r, hello(avBoard))
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c2.none(300 * time.Millisecond)
}

// Nessun avatar-ack entro AvatarAckTimeout: fallita, token morto, si passa al prossimo id.
func TestAvatarAckTimeout(t *testing.T) {
	r, _ := avatarRig(t)
	r.hub.AvatarAckTimeout = 200 * time.Millisecond
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto","demone"]}`)
	tok := c.next(2 * time.Second)["token"].(string)
	if off := c.next(2 * time.Second); off["id"] != "demone" {
		t.Fatalf("dopo la scadenza: %v", off)
	}
	waitLog(t, r, "nessun avatar-ack entro", 1)
	if code, _, _ := avGet(t, r, tok); code != http.StatusNotFound {
		t.Fatalf("token scaduto con l'offerta: %d", code)
	}
}

// Token: sconosciuto 404, IP diverso 403 (resta valido), poi 200. Ack sbagliati o senza offerta: ignorati.
func TestAvatarTokenAndAckRules(t *testing.T) {
	r, m := avatarRig(t)
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	tok := c.next(2 * time.Second)["token"].(string)
	if code, _, _ := avGet(t, r, strings.Repeat("b", 32)); code != http.StatusNotFound {
		t.Fatalf("sconosciuto: %d", code)
	}
	req := httptest.NewRequest(http.MethodGet, "/avatars/"+tok, nil)
	req.RemoteAddr = "10.0.0.99:4321"
	req.SetPathValue("token", tok)
	rec := httptest.NewRecorder()
	r.hub.ServeAvatar(rec, req)
	if rec.Code != http.StatusForbidden {
		t.Fatalf("IP sbagliato: %d", rec.Code)
	}
	if code, _, _ := avGet(t, r, tok); code != 200 {
		t.Fatalf("download dopo il 403: %d", code)
	}
	before := r.hub.Ignored()
	for _, bad := range []string{`{"t":"avatar-ack","v":1,"id":"gatto","ok":true}`, `{"t":"avatar-ack","v":1,"id":"Gatto","crc":1,"ok":true}`,
		`{"t":"avatar-ack","v":1,"id":"gatto","crc":1}`} {
		c.send(bad)
	}
	c.send(ackJSON("gatto", 12345, true, "")) // CRC diverso dall'offerta
	waitLog(t, r, "avatar-ack gatto senza un'offerta in corso", 1)
	if got := r.hub.Ignored() - before; got != 3 {
		t.Fatalf("ignorati %d su 3", got)
	}
	c.send(ackJSON("gatto", m.Lib.Get("gatto").CRC, true, ""))
	waitLog(t, r, "avatar gatto salvato dalla scheda", 1)
}
```

- [ ] **Step 3: Vederle fallire**

Run: `cd service && go test ./internal/display -run 'TestAvatar|TestExamples' -count=1 2>&1 | tail -n 15`
Expected: errori di compilazione (`ServeAvatar`, `AvatarFailHold`, `AvatarAckTimeout` sconosciuti).

- [ ] **Step 4: `avatar_dl.go`**

```go
package display

import (
	"encoding/json"
	"fmt"
	"net/http"
	"slices"
	"time"

	"calamaio/internal/m5"
)

// Avatar a pezzi (firmware 0.14.0, docs/PROTOCOLLO.md «Avatar»). A una scheda che in hello dice caps ["avatar-dl"]
// il servizio non manda il CAVT in base64 dentro il WebSocket (9,5 KB in un blocco solo per la scheda, che dopo il
// Bluetooth spesso non li ha): offre l'avatar come un tema e la scheda lo scarica con GET /avatars/<token> a pezzi,
// direttamente nella cache. Le schede senza la capacità ricevono ancora `avatar`.
//
//	servizio → display   avatar-offer {id, crc, size, token}   una alla volta per connessione, anche senza auth
//	display → servizio   avatar-ack   {id, crc, ok, error?}    dopo il download, i controlli e il salvataggio
//
// Niente autenticazione, come avatar-need e `avatar`: un avatar non è un dato sensibile e non cambia niente sulla
// scheda oltre alla cache. Il token (16 byte casuali, monouso, solo per l'IP di quella connessione) dice soltanto
// «questa connessione l'ha chiesto»; muore con l'ack, con la chiusura della connessione e alla scadenza dell'ack.

// CapAvatarDl: capacità di hello per l'avatar a pezzi.
const CapAvatarDl = "avatar-dl"

// avatarFailHold: dopo una consegna fallita (ack negativo, nessun ack, connessione chiusa a metà, o con `avatar`
// connessione chiusa subito dopo l'invio) lo stesso avatar (id e CRC) non si rimanda alla stessa scheda per questo
// tempo, anche su connessioni nuove: è il ciclo visto dal vivo (1011 → riconnessione → avatar di nuovo, 1 al secondo).
const avatarFailHold = 60 * time.Second

// avatarLegacyGrace: con `avatar`, una connessione che si chiude entro questo tempo dall'invio conta come fallimento.
const avatarLegacyGrace = 10 * time.Second

type avatarOfferOut struct {
	T     string `json:"t"`
	V     int    `json:"v"`
	ID    string `json:"id"`
	CRC   uint32 `json:"crc"`
	Size  int    `json:"size"`
	Token string `json:"token"`
}

type avatarAckMsg struct {
	ID    string  `json:"id"`
	CRC   *uint32 `json:"crc"`
	OK    *bool   `json:"ok"`
	Error string  `json:"error"`
}

// avatarItem: un avatar da offrire (binario fissato quando è stato chiesto).
type avatarItem struct {
	id  string
	crc uint32
	bin []byte
}

type avatarOffer struct {
	avatarItem
	token string
	timer *time.Timer
}

type avatarToken struct {
	conn *conn
	item avatarItem
	ip   string
	exp  time.Time
	used bool
}

type avatarLegacySent struct {
	id  string
	crc uint32
	at  time.Time
}

func parseAvatarAck(data []byte) (*avatarAckMsg, bool) {
	var m avatarAckMsg
	if json.Unmarshal(data, &m) != nil || len(m.ID) > 24 || !avatarIDRe.MatchString(m.ID) || m.CRC == nil || m.OK == nil {
		return nil, false
	}
	m.Error = cleanField(m.Error, 127)
	return &m, true
}

func hasCap(caps []string, c string) bool { return slices.Contains(caps, c) }

func avatarKey(id string, crc uint32) string { return fmt.Sprintf("%s/%08x", id, crc) }

// avatarHeld: id e crc falliti da poco su questa scheda (h.AvatarFailHold). Con h.mu.
func (h *Hub) avatarHeld(board, id string, crc uint32) bool {
	until, ok := h.avFail[board][avatarKey(id, crc)]
	if ok && !h.Now().Before(until) {
		delete(h.avFail[board], avatarKey(id, crc))
		return false
	}
	return ok
}

// avatarFailed segna la consegna fallita. Con h.mu.
func (h *Hub) avatarFailed(board, id string, crc uint32, why string) {
	if board == "" {
		return
	}
	if h.avFail == nil {
		h.avFail = map[string]map[string]time.Time{}
	}
	if h.avFail[board] == nil {
		h.avFail[board] = map[string]time.Time{}
	}
	h.avFail[board][avatarKey(id, crc)] = h.Now().Add(h.AvatarFailHold)
	h.Log.Printf("avatar: %s (crc %08x) non consegnato a %s: %s; niente rinvii per %s", id, crc, board, why, h.AvatarFailHold)
}

// avatarQueue mette in coda un avatar per una scheda con avatar-dl (un'offerta alla volta). Con h.mu.
func (h *Hub) avatarQueue(c *conn, it avatarItem) {
	if c.avOffer != nil && c.avOffer.id == it.id && c.avOffer.crc == it.crc {
		return
	}
	for _, q := range c.avQueue {
		if q.id == it.id && q.crc == it.crc {
			return
		}
	}
	c.avQueue = append(c.avQueue, it)
	h.avatarNext(c)
}

// avatarNext offre il prossimo avatar in coda, se non c'è un'offerta in corso. Con h.mu.
func (h *Hub) avatarNext(c *conn) {
	for c.avOffer == nil && len(c.avQueue) > 0 {
		it := c.avQueue[0]
		c.avQueue = c.avQueue[1:]
		if h.avatarHeld(c.info.Board, it.id, it.crc) {
			continue
		}
		tok := m5.Hex(m5.Random(16))
		if !h.enqueueOut(c, avatarOfferOut{T: "avatar-offer", V: ProtocolVersion, ID: it.id, CRC: it.crc, Size: len(it.bin), Token: tok}) {
			return
		}
		if h.avTokens == nil {
			h.avTokens = map[string]*avatarToken{}
		}
		h.avTokens[tok] = &avatarToken{conn: c, item: it, ip: c.info.IP, exp: h.Now().Add(h.TokenTTL)}
		o := &avatarOffer{avatarItem: it, token: tok}
		o.timer = time.AfterFunc(h.AvatarAckTimeout, func() {
			h.mu.Lock()
			defer h.mu.Unlock()
			if c.avOffer == o {
				h.avatarEnd(c, "nessun avatar-ack entro "+h.AvatarAckTimeout.String())
			}
		})
		c.avOffer = o
		h.Log.Printf("ws: #%d avatar-offer %s (crc %08x, %d byte)", c.info.ID, it.id, it.crc, len(it.bin))
	}
}

// avatarEnd chiude l'offerta in corso (why non vuoto = fallita) e passa alla prossima. Con h.mu.
func (h *Hub) avatarEnd(c *conn, why string) {
	o := c.avOffer
	if o == nil {
		return
	}
	o.timer.Stop()
	delete(h.avTokens, o.token)
	c.avOffer = nil
	if why != "" {
		h.avatarFailed(c.info.Board, o.id, o.crc, why)
	}
	h.avatarNext(c)
}

func (h *Hub) onAvatarAck(c *conn, m *avatarAckMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	o := c.avOffer
	if o == nil || o.id != m.ID || o.crc != *m.CRC {
		h.Log.Printf("ws: #%d avatar-ack %s senza un'offerta in corso: ignorato", c.info.ID, m.ID)
		return
	}
	if !*m.OK {
		h.avatarEnd(c, "la scheda l'ha rifiutato: "+m.Error)
		return
	}
	h.Log.Printf("ws: #%d avatar %s salvato dalla scheda", c.info.ID, m.ID)
	h.avatarEnd(c, "")
}

// avatarConnClosed: offerta a metà e `avatar` mandati da poco contano come falliti. Con h.mu.
func (h *Hub) avatarConnClosed(c *conn) {
	if c.avOffer != nil {
		c.avQueue = nil
		h.avatarEnd(c, "connessione chiusa durante la consegna")
	}
	for _, s := range c.avLegacy {
		if h.Now().Sub(s.at) < avatarLegacyGrace {
			h.avatarFailed(c.info.Board, s.id, s.crc, "connessione chiusa subito dopo il messaggio avatar")
		}
	}
	c.avLegacy = nil
}

// ServeAvatar: GET /avatars/{token} sulla porta dei display, stesse regole dei temi (blobTokenStatus).
func (h *Hub) ServeAvatar(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	t := h.avTokens[tok]
	var exp time.Time
	var want string
	if t != nil {
		exp, want = t.exp, t.ip
	}
	code := blobTokenStatus(tok, t != nil, t != nil && t.used, exp, ip, want, h.Now())
	if code != 0 {
		if code == http.StatusGone && t != nil && !t.used {
			delete(h.avTokens, tok)
		}
		h.mu.Unlock()
		h.Log.Printf("avatar: download da %s rifiutato (%d)", ip, code)
		http.Error(w, http.StatusText(code), code)
		return
	}
	t.used = true
	it := t.item
	h.mu.Unlock()
	serveBlob(w, r, it.bin, it.crc)
	h.Log.Printf("avatar: %s scaricato da %s (%d byte)", it.id, ip, len(it.bin))
}

// blobTokenStatus: regole comuni dei download con token (temi, avatar; come ServeOTA). 404 token malformato o
// sconosciuto, 410 già usato o scaduto, 403 IP diverso da quello della scheda (il token resta valido). 0 = servire.
func blobTokenStatus(tok string, known, used bool, exp time.Time, ip, want string, now time.Time) int {
	switch {
	case len(tok) != 32 || !m5.IsLowerHex(tok) || !known:
		return http.StatusNotFound
	case used, now.After(exp):
		return http.StatusGone
	case ip != want:
		return http.StatusForbidden
	}
	return 0
}

// serveBlob: 200 application/octet-stream con Content-Length, X-Calamaio-Crc e no-store; HEAD solo le intestazioni.
func serveBlob(w http.ResponseWriter, r *http.Request, bin []byte, crc uint32) {
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Content-Length", fmt.Sprint(len(bin)))
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Crc", fmt.Sprintf("%08x", crc))
	w.WriteHeader(http.StatusOK)
	if r.Method != http.MethodHead {
		_, _ = w.Write(bin)
	}
}
```

- [ ] **Step 5: `avatar.go`**

`avatarMessage` prende CRC e binario da `avatarPacket` (stessa scelta di sempre per i firmware senza `run`):

```go
// avatarMessage: con legacy il CAVT senza run (firmware ≤ 0.6.0, docs/AVATAR.md §12.4).
func avatarMessage(src AvatarSource, id string, legacy bool) (AvatarMsg, bool) {
	crc, bin, ok := avatarPacket(src, id, legacy)
	if !ok {
		return AvatarMsg{}, false
	}
	m := AvatarMsg{T: "avatar", V: ProtocolVersion, ID: id, CRC: crc, Data: base64.StdEncoding.EncodeToString(bin)}
	if b, err := json.Marshal(m); err != nil || len(b) > MaxFrame {
		return AvatarMsg{}, false
	}
	return m, true
}
```

e `sendAvatars` intera, con `avatarPacket` sotto:

```go
// sendAvatars risponde a un avatar-need (chiamata da run): `avatar-offer` in coda per le schede con avatar-dl
// (avatar_dl.go), il messaggio `avatar` per le altre. Mai un avatar fallito da poco su questa scheda (avatarHeld).
func (h *Hub) sendAvatars(ctx context.Context, c *conn, ids []string, sent map[string]avatarSent) error {
	for _, id := range ids {
		crc, bin, ok := avatarPacket(h.Avatars, id, c.legacyAv)
		if !ok {
			h.Log.Printf("ws: #%d avatar-need: %q sconosciuto (il display usa il predefinito)", c.info.ID, id)
			continue
		}
		if s, ok := sent[id]; ok && s.crc == crc && h.Now().Sub(s.at) < avatarResend {
			continue
		}
		h.mu.Lock()
		held, dl := h.avatarHeld(c.info.Board, id, crc), c.avatarDl
		if !held && dl {
			h.avatarQueue(c, avatarItem{id: id, crc: crc, bin: bin})
		}
		h.mu.Unlock()
		if held {
			h.Log.Printf("ws: #%d avatar %s non rimandato: consegna fallita da meno di %s", c.info.ID, id, h.AvatarFailHold)
			continue
		}
		sent[id] = avatarSent{crc: crc, at: h.Now()}
		if dl {
			continue
		}
		m, ok := avatarMessage(h.Avatars, id, c.legacyAv)
		if !ok {
			continue
		}
		if err := h.write(ctx, c, m); err != nil {
			return err
		}
		h.mu.Lock()
		c.avLegacy = append(c.avLegacy, avatarLegacySent{id: id, crc: m.CRC, at: h.Now()})
		h.mu.Unlock()
		h.Log.Printf("ws: #%d avatar %s mandato (crc %08x, %d caratteri)", c.info.ID, id, m.CRC, len(m.Data))
	}
	return nil
}

// avatarPacket: CRC e binario di id (senza la posa run con legacy).
func avatarPacket(src AvatarSource, id string, legacy bool) (uint32, []byte, bool) {
	if src == nil {
		return 0, nil, false
	}
	if l, isL := src.(LegacyAvatarSource); legacy && isL {
		return l.LegacyAvatarPacket(id)
	}
	return src.AvatarPacket(id)
}
```

- [ ] **Step 6: `hub.go`, `remote.go`, `themes.go`**

`hub.go`, in `Hello` dopo `Paired`:

```go
	// Caps (firmware 0.14.0): capacità dichiarate, per ora solo "avatar-dl" (avatar_dl.go). Assente nei firmware vecchi.
	Caps []string `json:"caps,omitempty"`
```

in `Hub`, dopo `themeTokens`:

```go

	// Avatar a pezzi (avatar_dl.go): token di download e consegne fallite per scheda (board → id/crc → fino a quando).
	AvatarAckTimeout time.Duration // senza avatar-ack entro questo tempo dall'offerta: fallita
	AvatarFailHold   time.Duration // avatarFailHold; nei test più corto
	avTokens         map[string]*avatarToken
	avFail           map[string]map[string]time.Time
```

in `conn`, dopo `legacyAv`:

```go
	// Avatar a pezzi (avatar_dl.go, protetti da Hub.mu): hello con caps avatar-dl, offerta in corso, coda, `avatar`
	// mandati alle schede senza la capacità (per riconoscere una chiusura subito dopo).
	avatarDl bool
	avOffer  *avatarOffer
	avQueue  []avatarItem
	avLegacy []avatarLegacySent
```

in `NewHub`, dopo la riga dei temi:
`h.AvatarAckTimeout, h.AvatarFailHold = 30*time.Second, avatarFailHold // avatar a pezzi (avatar_dl.go)`;
in `Handler`, dopo `/themes/{token}`:
`mux.HandleFunc("GET /avatars/{token}", h.ServeAvatar) // avatar a pezzi (avatar_dl.go)` (poi `gofmt -w`);
in `ServeHTTP`, dopo `h.themeConnClosed(c)`: `h.avatarConnClosed(c)`;
in `run`, nello `switch m.t` dopo `case "theme-ack":`:

```go
				case "avatar-ack":
					h.onAvatarAck(c, m.avatarAck)
					continue
```

nel ramo `hello`, sotto `c.info.DisplayPaired = hl.Paired` (con `h.mu` preso):
`c.avatarDl = hasCap(hl.Caps, CapAvatarDl)`;
in `readLoop`, prima di `case "pair-key", …`:

```go
		case "avatar-ack":
			m, ok := parseAvatarAck(data)
			if !ok {
				h.ignore(id, "avatar-ack non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, avatarAck: m}); err != nil {
				return err
			}
```

`remote.go`, in `inbound` dopo `themeAck`: `avatarAck *avatarAckMsg // avatar-ack (avatar_dl.go)`.

`themes.go`: `ServeTheme` usa le regole in comune (stessi codici e stesse righe di registro di prima; `strconv` non
serve più fra gli import):

```go
// ServeTheme: GET /themes/{token} sulla porta dei display. Stesse regole di ServeOTA (blobTokenStatus): 404 token
// sconosciuto, 410 usato o scaduto, 403 IP diverso da quello della scheda che ha ricevuto l'offerta (il token resta valido).
func (h *Hub) ServeTheme(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	t := h.themeTokens[tok]
	var exp time.Time
	var want string
	if t != nil {
		exp, want = t.exp, t.ip
	}
	switch blobTokenStatus(tok, t != nil, t != nil && t.used, exp, ip, want, h.Now()) {
	case http.StatusNotFound:
		h.mu.Unlock()
		h.Log.Printf("temi: download con un token sconosciuto da %s", ip)
		http.Error(w, "token sconosciuto", http.StatusNotFound)
		return
	case http.StatusGone:
		if t.used {
			h.mu.Unlock()
			h.Log.Printf("temi %s: download ripetuto da %s: il token vale una volta sola", t.job.id, ip)
			http.Error(w, "token già usato", http.StatusGone)
			return
		}
		delete(h.themeTokens, tok)
		h.mu.Unlock()
		h.Log.Printf("temi %s: download da %s con il token scaduto", t.job.id, ip)
		http.Error(w, "token scaduto", http.StatusGone)
		return
	case http.StatusForbidden:
		h.mu.Unlock()
		h.Log.Printf("temi %s: download rifiutato a %s: il token vale solo per %s", t.job.id, ip, t.ip)
		http.Error(w, "non per questo indirizzo", http.StatusForbidden)
		return
	}
	t.used = true
	j := t.job
	bin, crc, id := j.want.Bin, j.want.CRC, j.want.ID
	h.themeStage(j, ThemeDownloaded)
	h.mu.Unlock()
	serveBlob(w, r, bin, crc)
	if r.Method != http.MethodHead {
		h.Log.Printf("temi %s: %s scaricato da %s (%d byte)", j.id, id, ip, len(bin))
	}
}
```

- [ ] **Step 7: Vederle passare, con temi e schema**

Run:
```bash
cd service && gofmt -l internal/display; go test ./internal/display -count=1 2>&1 | tail -n 3 \
 && go test -race ./internal/display -run 'TestAvatar|TestTheme' -count=2 2>&1 | tail -n 3 \
 && go vet ./... && go test ./... 2>&1 | tail -n 5
```
Expected: `gofmt` non elenca file; `ok calamaio/internal/display` (anche `TestAvatarProtocol`, `TestTheme*`,
`TestExamplesMatchSchema`, `TestSchemaRejects` di prima); `-race` pulito; tutto `ok`. Prova a mano facoltativa: con
`held` sempre falso in `sendAvatars`, `TestAvatarFailHoldAcrossConnections` e `TestAvatarLegacyCloseHeld` falliscono.

- [ ] **Step 8: Commit**

```bash
git add protocol/schema/message.schema.json protocol/examples/avatar-offer.json protocol/examples/avatar-ack.json protocol/examples/avatar-ack-errore.json protocol/examples/hello-avatar-dl.json service/internal/display
git commit -m "Servizio: avatar a pezzi per le schede con hello.caps avatar-dl (avatar-offer, GET /avatars/<token>, avatar-ack), nessun rinvio dello stesso avatar alla stessa scheda per 60 s dopo una consegna fallita, regole del token in comune coi temi"
```

---

### Task 8: Firmware, parte pura: avatar a pezzi e chiusure a raffica

Il CAVT non passa più tutto in RAM: `CavtStream` fa i controlli di `parseAvatar` man mano (stessa funzione dei
controlli, `checkCavt`, quindi stessi esiti byte per byte), `AvatarDownload` scrive i pezzi nel temporaneo della
memoria (`/av/dl` in LittleFS dal Task 9) e `AvatarCache::adopt` lo rinomina nel posto scelto come per `store`.
`CloseStorm` è la regola dell'attesa dopo chiusure brevi del WebSocket. Tutto provato in `env:native`.

**Files:**
- Modify: `firmware/src/gfx/avatar.h`, `firmware/src/gfx/avatar.cpp` (`CavtStream`; `parseAvatar` e `avatarCrc32` sulle stesse funzioni)
- Modify: `firmware/src/core/theme_proto.h`, `theme_proto.cpp` (`lowerHex`, `cutUtf8`, `blobUrl` pubbliche: le usano anche gli avatar)
- Modify: `firmware/src/core/avatars.h`, `avatars.cpp` (temporaneo di `AvatarStorage`, `MemAvatarStorage`, `AvatarCache::adopt`/`pickSlot`, `AvatarOffer`, `decodeAvatarOffer`, `encodeAvatarAck`, `avatarUrl`, `AvatarDownload`)
- Modify: `firmware/src/core/apply_state.h`, `apply_state.cpp` (`MsgType::AvatarOffer`)
- Modify: `firmware/src/core/ui.h`, `ui.cpp` (`NetCmd::AvatarAck`, chiave `"avatar-ack"`)
- Create: `firmware/src/core/close_storm.h`, `firmware/src/core/close_storm.cpp`
- Test: `firmware/test/test_avatar/test_main.cpp`, `firmware/test/test_close_storm/test_main.cpp`

**Interfaces:**
- Consumes: `protocol/examples/avatar-offer.json` (Task 7); `NetRequest` (Task 5: il campo `src` c'è già).
- Produces:
  - `class CavtStream { void begin(); void feed(const uint8_t *p, size_t n); AvatarError finish(Avatar &out) const; size_t size() const; }` (in `gfx/avatar.h`, fuori dai namespace come `Avatar`).
  - In `cal`: `AvatarStorage::tempBegin/tempAppend/tempEnd/tempAbort/adoptTemp` (virtuali, di serie `false`), `AvatarError AvatarCache::adopt(const Avatar &hdr, int *slotOut = nullptr)`, `constexpr uint32_t kCavtMinBytes` (2 172), `struct AvatarOffer { char id[kAvatarRefLen]; uint32_t crc, size; char token[33]; }`, `bool decodeAvatarOffer(JsonVariantConst, AvatarOffer&)`, `size_t encodeAvatarAck(const char *id, uint32_t crc, bool ok, const char *error, char *out, size_t cap)`, `bool avatarUrl(host, port, token, out, cap)`, `class AvatarDownload { begin; write; finish(Avatar &hdr, …); abort; }`, `MsgType::AvatarOffer`, `NetCmd::AvatarAck` (`id`, `avatarCrcs[0]`, `ok`, `error`), `class CloseStorm { opened(now); uint32_t closed(now, allocFailed); int shortCloses() const; }`.
  - In `cal::theme`: `bool blobUrl(host, port, dir, token, out, cap)`, `bool lowerHex(const char *s, size_t n)`, `void cutUtf8(char *dst, const char *src, size_t max)`.
  - Testi di `avatar-ack` (in `why`): «spazio per gli avatar non disponibile», «più byte di quelli offerti», «scrittura non riuscita», «download incompleto», `avatarErrorText(…)`, «l'avatar non è quello offerto», «CRC diverso da quello offerto»; quelli del download (Task 9) sono gli stessi dei temi.

- [ ] **Step 1: Prove che falliscono**

In `test_avatar/test_main.cpp`, prima di `int main() {`:

```cpp
// ---------- 0.14.0 (R4): avatar a pezzi ----------

// Il CAVT dato a pezzi a CavtStream dà lo stesso esito di parseAvatar sugli stessi byte, con ogni misura dei pezzi,
// anche rovinato in qualunque punto, accorciato o allungato.
static void streamSame(const std::vector<uint8_t> &b, size_t chunk, const char *what) {
  Avatar a, s;
  const AvatarError want = parse(b, a);
  CavtStream cs;
  for (size_t i = 0; i < b.size(); i += chunk) cs.feed(b.data() + i, b.size() - i < chunk ? b.size() - i : chunk);
  const AvatarError got = cs.finish(s);
  char msg[96];
  snprintf(msg, sizeof msg, "%s (pezzi da %u)", what, unsigned(chunk));
  TEST_ASSERT_EQUAL_MESSAGE(int(want), int(got), msg);
  if (want != AvatarError::Ok) return;
  TEST_ASSERT_NULL(s.data);
  TEST_ASSERT_EQUAL_STRING(a.id, s.id);
  TEST_ASSERT_EQUAL_STRING(a.name, s.name);
  TEST_ASSERT_EQUAL(a.len, s.len);
  TEST_ASSERT_EQUAL(a.poseMask, s.poseMask);
  TEST_ASSERT_EQUAL(a.frames, s.frames);
  TEST_ASSERT_EQUAL(a.header, s.header);
  TEST_ASSERT_EQUAL_UINT32(a.crc, s.crc);
}

static void test_cavt_stream_matches_parse() {
  const size_t chunks[] = {1, 7, 12, 72, 175, 512, 8000};
  for (int i = 0; i < kBuiltinAvatarCount; i++) {
    const std::vector<uint8_t> b = builtinBytes(kBuiltinAvatars[i].id);
    for (size_t c : chunks) streamSame(b, c, kBuiltinAvatars[i].id);
    for (size_t k = 0; k < b.size(); k += 37) {  // un bit cambiato ogni 37 byte: intestazione, CRC, pixel
      std::vector<uint8_t> x = b;
      x[k] ^= 0x40;
      streamSame(x, 512, "rovinato");
      recrc(x);  // CRC giusto: restano gli altri controlli (pixel 11, campi)
      streamSame(x, 175, "rovinato col CRC giusto");
    }
    for (size_t n : {size_t(0), size_t(11), size_t(71), size_t(72), b.size() - 1})
      streamSame(std::vector<uint8_t>(b.begin(), b.begin() + n), 64, "corto");
    std::vector<uint8_t> longer = b;
    longer.push_back(0);
    streamSame(longer, 512, "lungo");
  }
}

static AvatarOffer offerOf(const std::vector<uint8_t> &b, const char *id) {
  AvatarOffer o;
  memset(&o, 0, sizeof o);
  snprintf(o.id, sizeof o.id, "%s", id);
  o.crc = crcOf(b);
  o.size = uint32_t(b.size());
  snprintf(o.token, sizeof o.token, "0123456789abcdef0123456789abcdef");
  return o;
}

// Scarica b a pezzi da chunk byte: true e hdr se tutto va; altrimenti why.
static bool download(MemAvatarStorage &st, const AvatarOffer &o, const std::vector<uint8_t> &b, size_t chunk, Avatar &hdr,
                     char *why, size_t cap) {
  AvatarDownload d;
  why[0] = 0;
  bool ok = d.begin(st, o, why, cap);
  for (size_t i = 0; ok && i < b.size(); i += chunk) ok = d.write(b.data() + i, b.size() - i < chunk ? b.size() - i : chunk, why, cap);
  ok = ok && d.finish(hdr, why, cap);
  if (!ok) d.abort();
  return ok;
}

// Il Demone (10 pose, 7 072 byte) scaricato a pezzi da 512 entra nella cache e si disegna come quello compilato.
static void test_download_then_adopt() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  const std::vector<uint8_t> b = builtinBytes("demone");
  TEST_ASSERT_EQUAL(7072, b.size());
  Avatar hdr;
  char why[96];
  TEST_ASSERT_TRUE_MESSAGE(download(st, offerOf(b, "demone"), b, 512, hdr, why, sizeof why), why);
  int slot = -1;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.adopt(hdr, &slot)));
  TEST_ASSERT_TRUE(slot >= 0);
  TEST_ASSERT_EQUAL(-1, st.tempLen());  // il temporaneo è diventato il posto
  const Avatar *a = c.find("demone", crcOf(b), true);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NULL(a->data);  // si legge a pezzi dalla memoria
  const Avatar *ref = builtinAvatarById("demone");
  uint8_t f1[kCavtFrameBytes], f2[kCavtFrameBytes];
  for (int p = 0; p < int(Pose::Count); p++)
    for (int f = 0; f < 4; f++) {
      TEST_ASSERT_TRUE(avatarFrameBytes(*a, Pose(p), f, f1));
      TEST_ASSERT_TRUE(avatarFrameBytes(*ref, Pose(p), f, f2));
      TEST_ASSERT_EQUAL_MEMORY(f2, f1, kCavtFrameBytes);
    }
  // al riavvio la cache lo rivalida dalla memoria come quelli arrivati con `avatar`
  AvatarCache again;
  TEST_ASSERT_EQUAL(1, again.begin(&st));
  TEST_ASSERT_TRUE(again.has("demone", crcOf(b), true));
  // già in cache: adopt non scrive niente e butta il temporaneo
  TEST_ASSERT_TRUE(download(st, offerOf(b, "demone"), b, 175, hdr, why, sizeof why));
  const int w = st.writes;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.adopt(hdr)));
  TEST_ASSERT_EQUAL(w, st.writes);
  TEST_ASSERT_EQUAL(-1, st.tempLen());
}

// Ogni errore ha il suo motivo per avatar-ack e la cache resta com'era.
static void test_download_rejects() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  const std::vector<uint8_t> b = builtinBytes("gatto");
  Avatar hdr;
  char why[96];
  AvatarOffer o = offerOf(b, "gatto");
  o.crc ^= 1;
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("CRC diverso da quello offerto", why);
  o = offerOf(b, "demone");
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("l'avatar non è quello offerto", why);
  o = offerOf(b, "gatto");
  o.size += 1;
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("download incompleto", why);
  o = offerOf(b, "gatto");
  std::vector<uint8_t> longer = b;
  longer.push_back(0);
  TEST_ASSERT_FALSE(download(st, o, longer, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("più byte di quelli offerti", why);
  std::vector<uint8_t> bad = b;
  bad[500] ^= 0x40;
  TEST_ASSERT_FALSE(download(st, o, bad, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING(avatarErrorText(AvatarError::Crc), why);
  st.failWrites = true;
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("spazio per gli avatar non disponibile", why);
  st.failWrites = false;
  TEST_ASSERT_EQUAL(0, c.count());
  TEST_ASSERT_EQUAL(-1, st.tempLen());  // abort butta il temporaneo
  // rinomina fallita nella cache: «non salvato», posto libero
  TEST_ASSERT_TRUE(download(st, o, b, 512, hdr, why, sizeof why));
  st.failWrites = true;
  TEST_ASSERT_EQUAL(int(AvatarError::Short), int(c.adopt(hdr)));
  st.failWrites = false;
  TEST_ASSERT_EQUAL(0, c.count());
}

static void test_avatar_offer_and_ack_messages() {
  JsonDocument doc;
  const std::string js = [] {
    std::ifstream in(repoRoot() + "protocol/examples/avatar-offer.json");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }();
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 50, "manca protocol/examples/avatar-offer.json");
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  AvatarOffer o;
  TEST_ASSERT_TRUE(decodeAvatarOffer(doc.as<JsonVariantConst>(), o));
  TEST_ASSERT_EQUAL_STRING("demone", o.id);
  TEST_ASSERT_EQUAL_UINT32(0xA3130345u, o.crc);
  TEST_ASSERT_EQUAL_UINT32(7072, o.size);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", o.token);
  const char *bad[] = {
      R"({"t":"avatar-offer","v":1,"id":"Demone","crc":1,"size":7072,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","crc":1,"size":7073,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","crc":1,"size":2171,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","crc":1,"size":7072,"token":"0123456789ABCDEF0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","size":7072,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"demone","crc":1,"size":7072,"token":"0123456789abcdef0123456789abcdef"})",
  };
  for (const char *b : bad) {
    doc.clear();
    TEST_ASSERT_TRUE(parseMessage(b, strlen(b), doc));
    AvatarOffer k = o;
    TEST_ASSERT_FALSE_MESSAGE(decodeAvatarOffer(doc.as<JsonVariantConst>(), k), b);
    TEST_ASSERT_EQUAL_STRING("demone", k.id);  // intatta
  }
  char buf[256];
  TEST_ASSERT_TRUE(encodeAvatarAck("demone", 0xA3130345u, true, "", buf, sizeof buf) > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"avatar-ack","v":1,"id":"demone","crc":2735932229,"ok":true})", buf);
  TEST_ASSERT_TRUE(encodeAvatarAck("demone", 1, false, "download troppo lento", buf, sizeof buf) > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"avatar-ack","v":1,"id":"demone","crc":1,"ok":false,"error":"download troppo lento"})", buf);
  TEST_ASSERT_EQUAL(0, encodeAvatarAck("Demone", 1, true, "", buf, sizeof buf));
  TEST_ASSERT_EQUAL(0, encodeAvatarAck("demone", 1, true, "", buf, 20));  // non ci sta: niente JSON a metà
  TEST_ASSERT_TRUE(avatarUrl("192.168.1.20", 8766, o.token, buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:8766/avatars/0123456789abcdef0123456789abcdef", buf);
}
```

e in `main`, dopo `RUN_TEST(test_run_edges);`:

```cpp
  RUN_TEST(test_cavt_stream_matches_parse);
  RUN_TEST(test_download_then_adopt);
  RUN_TEST(test_download_rejects);
  RUN_TEST(test_avatar_offer_and_ack_messages);
```

`firmware/test/test_close_storm/test_main.cpp` (nuovo):

```cpp
// 0.14.0 (R4): chiusure del WebSocket a raffica (core/close_storm). La prima e la seconda breve riprovano subito come
// prima; dalla terza, o subito dopo un'allocazione fallita, il primo tentativo aspetta 5, 10, 20, 30 s.
#include <unity.h>

#include "core/close_storm.h"

using namespace cal;

void setUp() {}
void tearDown() {}

// Connessione aperta a t e chiusa dopo lived ms: l'attesa data.
static uint32_t cycle(CloseStorm &s, uint32_t &t, uint32_t lived, bool alloc = false) {
  s.opened(t);
  t += lived;
  return s.closed(t, alloc);
}

static void test_two_short_closes_retry_now() {
  CloseStorm s;
  uint32_t t = 1000;
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(10000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(20000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(30000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(30000, cycle(s, t, 800));  // mai oltre i tentativi lenti
}

static void test_alloc_failure_waits_at_once() {
  CloseStorm s;
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 900, true));
  TEST_ASSERT_EQUAL_UINT32(10000, cycle(s, t, 900, true));
  // anche dopo una connessione di 40 s (né breve né stabile): l'allocazione fallita conta sempre
  TEST_ASSERT_EQUAL_UINT32(20000, cycle(s, t, 40000, true));
}

static void test_stable_connection_ends_storm() {
  CloseStorm s;
  uint32_t t = 0;
  for (int i = 0; i < 4; i++) cycle(s, t, 500);
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 61000));  // un minuto collegata: finita
  TEST_ASSERT_EQUAL(0, s.shortCloses());
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 500));
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 500));
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 500));
}

static void test_far_apart_closes_are_not_a_storm() {
  CloseStorm s;
  uint32_t t = 0;
  for (int i = 0; i < 10; i++) {
    TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 1000));
    t += 70000;  // senza connessione per più della finestra (Wi-Fi giù, PC spento)
  }
}

static void test_medium_connection_keeps_count() {
  CloseStorm s;
  uint32_t t = 0;
  cycle(s, t, 500);
  cycle(s, t, 500);
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 30000));  // né breve né stabile
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 500));
}

// Il ciclo visto dal vivo: ogni connessione cade dopo ~1 s per l'avatar (allocazione fallita) e la scheda si
// ricollegava subito (100 connessioni in 10 minuti). Con l'attesa: al più 25 in 10 minuti.
static void test_live_loop_slows_down() {
  CloseStorm s;
  uint32_t t = 0;
  int conns = 0;
  while (t < 600000) {
    conns++;
    t += 300;  // collegamento
    const uint32_t hold = cycle(s, t, 1000, true);
    t += hold;
  }
  TEST_ASSERT_TRUE_MESSAGE(conns <= 25, "troppe connessioni in 10 minuti");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_two_short_closes_retry_now);
  RUN_TEST(test_alloc_failure_waits_at_once);
  RUN_TEST(test_stable_connection_ends_storm);
  RUN_TEST(test_far_apart_closes_are_not_a_storm);
  RUN_TEST(test_medium_connection_keeps_count);
  RUN_TEST(test_live_loop_slows_down);
  return UNITY_END();
}
```

- [ ] **Step 2: Vederle fallire**

Run: `cd firmware && pio test -e native -f test_avatar -f test_close_storm 2>&1 | tail -n 10`
Expected: errori di compilazione (`CavtStream`, `AvatarOffer`, `AvatarDownload`, `core/close_storm.h`).

- [ ] **Step 3: `CavtStream` (`gfx/avatar.h`, `gfx/avatar.cpp`)**

In `avatar.h`, sotto la dichiarazione di `parseAvatar`:

```cpp
// 0.14.0 (R4): gli stessi controlli di parseAvatar su un CAVT che arriva a pezzi (download nella cache in LittleFS,
// mai tutto in RAM). Tiene i 72 byte d'intestazione, calcola CRC e pixel man mano; finish() dà lo stesso esito di
// parseAvatar sugli stessi byte (stessa funzione dei controlli), con out.data nullo: la vista si legge dal file.
class CavtStream {
 public:
  CavtStream() { begin(); }
  void begin();
  void feed(const uint8_t *p, size_t n);
  AvatarError finish(Avatar &out) const;
  size_t size() const { return n_; }

 private:
  uint8_t hdr_[kCavtHeader];
  size_t n_;
  uint32_t crc_;  // CRC-32 dei byte dal 12 in poi, prima dello xor finale
  bool pixel_;    // un pixel col codice 11 riservato
};
```

In `avatar.cpp`, `avatarCrc32` diventa (il passo del CRC e il controllo dei pixel servono anche a pezzi):

```cpp
namespace {
// Passo del CRC-32 IEEE senza lo xor finale: a pezzi (CavtStream) e tutto insieme (avatarCrc32) danno lo stesso.
uint32_t crcStep(uint32_t c, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  }
  return c;
}
// Codice 11 in qualche coppia di bit: (bit alto) & (bit basso spostato in alto).
bool badPixels(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (p[i] & 0xAA & (p[i] << 1)) return true;
  return false;
}
}  // namespace

uint32_t avatarCrc32(const uint8_t *p, size_t n) { return crcStep(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu; }
```

e `parseAvatar` intera (i controlli di sempre, nello stesso ordine, spostati in `checkCavt`) con `CavtStream` sotto:

```cpp
namespace {
// I controlli di parseAvatar nell'ordine di sempre, sull'intestazione (72 byte), con lunghezza, CRC dei byte dal 12
// in poi e pixel già calcolati da chi chiama (tutto il buffer o un pezzo alla volta).
AvatarError checkCavt(const uint8_t *buf, size_t len, uint32_t crc, bool pixelBad, Avatar &out) {
  if (memcmp(buf, "CAVT", 4) != 0) return AvatarError::Magic;
  if (buf[4] != 1) return AvatarError::Version;
  // v1: l'intestazione è di 72 byte esatti (come unpack() di pack.mjs, che calcola la dimensione con 72).
  if (buf[5] != kCavtHeader) return AvatarError::Header;
  const uint16_t total = u16(buf + 6);
  if (total != len) return AvatarError::Length;
  if (u32(buf + 8) != crc) return AvatarError::Crc;
  if (buf[12] != kAvatarW || buf[13] != kAvatarH) return AvatarError::Frames;
  const uint8_t frames = buf[14];
  if (frames != 2 && frames != 4) return AvatarError::Frames;
  if (buf[15] != 0) return AvatarError::Reserved;
  const uint16_t mask = u16(buf + 16);
  if ((mask & kRequiredPoses) != kRequiredPoses || (mask >> static_cast<int>(Pose::Count))) return AvatarError::Mask;
  if (u16(buf + 18) != kCavtFrameBytes) return AvatarError::FrameBytes;
  if (size_t(kCavtHeader) + size_t(popcount(mask)) * frames * kCavtFrameBytes != len) return AvatarError::Size;
  if (!fixedField(buf + 20, out.id, true)) return AvatarError::Id;
  if (!fixedField(buf + 44, out.name, false)) return AvatarError::Name;
  if (pixelBad) return AvatarError::Pixel;
  out.len = total;
  out.poseMask = mask;
  out.frames = frames;
  out.header = buf[5];
  out.crc = u32(buf + 8);
  return AvatarError::Ok;
}
}  // namespace

AvatarError parseAvatar(const uint8_t *buf, size_t len, Avatar &out) {
  memset(&out, 0, sizeof out);
  if (!buf || len < size_t(kCavtHeader)) return AvatarError::Short;
  const uint32_t crc = avatarCrc32(buf + 12, len - 12);
  const AvatarError e = checkCavt(buf, len, crc, badPixels(buf + kCavtHeader, len - kCavtHeader), out);
  if (e != AvatarError::Ok) return e;
  out.data = buf;
  return AvatarError::Ok;
}

void CavtStream::begin() {
  memset(hdr_, 0, sizeof hdr_);
  n_ = 0;
  crc_ = 0xFFFFFFFFu;
  pixel_ = false;
}

void CavtStream::feed(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n;) {
    if (n_ < size_t(kCavtHeader)) {  // intestazione: tenuta, CRC dal byte 12
      const size_t k = n - i < size_t(kCavtHeader) - n_ ? n - i : size_t(kCavtHeader) - n_;
      memcpy(hdr_ + n_, p + i, k);
      if (n_ + k > 12) {
        const size_t from = n_ > 12 ? n_ : 12;
        crc_ = crcStep(crc_, hdr_ + from, n_ + k - from);
      }
      n_ += k;
      i += k;
      continue;
    }
    crc_ = crcStep(crc_, p + i, n - i);  // fotogrammi: CRC e pixel, niente copia
    pixel_ = pixel_ || badPixels(p + i, n - i);
    n_ += n - i;
    i = n;
  }
}

AvatarError CavtStream::finish(Avatar &out) const {
  memset(&out, 0, sizeof out);
  if (n_ < size_t(kCavtHeader)) return AvatarError::Short;
  return checkCavt(hdr_, n_, crc_ ^ 0xFFFFFFFFu, pixel_, out);
}
```

(`checkCavt` usa `u16`, `u32`, `popcount`, `fixedField`: va dopo il loro `namespace`, dove oggi sta `parseAvatar`.)

- [ ] **Step 4: Aiuti in comune coi temi (`core/theme_proto`)**

In `theme_proto.h`, sotto `downloadUrl`:

```cpp
// 0.14.0: in comune coi download degli avatar (core/avatars). http://<host>:<port>/<dir>/<token>; false se non ci sta.
bool blobUrl(const char *host, uint16_t port, const char *dir, const char *token, char *out, size_t cap);
// s ha esattamente n cifre esadecimali minuscole (i token).
bool lowerHex(const char *s, size_t n);
// Copia al massimo max byte senza spezzare un carattere UTF-8 (dst: max + 1 byte).
void cutUtf8(char *dst, const char *src, size_t max);
```

In `theme_proto.cpp` `lowerHex` e `cutUtf8` escono dal `namespace {}` (stesso corpo, il blocco anonimo sparisce) e
`downloadUrl` passa da `blobUrl`:

```cpp
bool blobUrl(const char *host, uint16_t port, const char *dir, const char *token, char *out, size_t cap) {
  const int n = snprintf(out, cap, "http://%s:%u/%s/%s", host, unsigned(port), dir, token);
  return n > 0 && size_t(n) < cap;
}

bool downloadUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap) {
  return blobUrl(host, port, "themes", token, out, cap);
}
```

- [ ] **Step 5: `core/avatars.h` e `avatars.cpp`**

In `AvatarStorage`, dopo `virtual void eraseAll() = 0;`:

```cpp
  // 0.14.0 (R4): un CAVT scaricato a pezzi (AvatarDownload, task di rete) in un temporaneo suo, mai quello di
  // writeSlot; adoptTemp (loop, AvatarCache::adopt) lo rinomina nel posto, tutto o niente. Di serie: non c'è.
  virtual bool tempBegin() { return false; }
  virtual bool tempAppend(const uint8_t *, uint16_t) { return false; }
  virtual bool tempEnd() { return false; }  // chiude il temporaneo dopo l'ultimo pezzo
  virtual void tempAbort() {}
  virtual bool adoptTemp(int) { return false; }
```

In `MemAvatarStorage`, dopo `void eraseAll() override;`:

```cpp
  bool tempBegin() override;
  bool tempAppend(const uint8_t *p, uint16_t n) override;
  bool tempEnd() override;
  void tempAbort() override;
  bool adoptTemp(int slot) override;
```

dopo `int writes = 0;` `int tempLen() const { return tempLen_; }  // −1 = nessun temporaneo` e fra i privati
`uint8_t temp_[kCavtMaxBytes]; int tempLen_ = -1; bool tempOpen_ = false;` (solo nelle prove: sulla scheda c'è
`hw/avatar_fs`). In `AvatarCache`, sotto `store`:

```cpp
  // 0.14.0 (R4): il CAVT già scaricato e controllato a pezzi (AvatarDownload::finish → hdr) entra nella cache
  // rinominando il temporaneo della memoria nel posto scelto come per store. Ok anche se c'era già (temporaneo buttato).
  AvatarError adopt(const Avatar &hdr, int *slotOut = nullptr);
```

e fra i privati, sotto `lookup`:
`int pickSlot(const char *id) const;  // versione vecchia dello stesso id, poi un posto libero, poi il meno usato`.
Prima di `base64Decode`:

```cpp
// ---------- 0.14.0 (R4): avatar a pezzi, come i temi (PROTOCOLLO.md «Avatar») ----------
// avatar-offer {id, crc, size, token} (anche senza autenticazione, come `avatar`) → GET /avatars/<token> sulla porta
// del WebSocket, a pezzi nella cache → avatar-ack {id, crc, ok, error?}. Niente base64, niente blocco da 9,5 KB.
constexpr uint32_t kCavtMinBytes = kCavtHeader + 6 * 2 * kCavtFrameBytes;  // 2 172: sei pose obbligatorie, 2 fotogrammi
struct AvatarOffer {
  char id[kAvatarRefLen];
  uint32_t crc;
  uint32_t size;   // kCavtMinBytes..kCavtMaxBytes
  char token[33];  // 16 byte in 32 cifre esadecimali minuscole
};
// Un'offerta non valida (id, token, size fuori regola) → false, out com'era.
bool decodeAvatarOffer(JsonVariantConst root, AvatarOffer &out);
// avatar-ack: id non valido → 0 (non si manda); error al massimo 127 byte, tagliato su un confine UTF-8.
size_t encodeAvatarAck(const char *id, uint32_t crc, bool ok, const char *error, char *out, size_t cap);
// http://<host>:<port>/avatars/<token>; false se non ci sta.
bool avatarUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap);

// Download a pezzi nel temporaneo della memoria, con i controlli di parseAvatar man mano (CavtStream): begin, write
// per ogni pezzo (mai oltre size), finish (lunghezza, CAVT, id e crc dell'offerta) → hdr per AvatarCache::adopt.
// Un errore lascia il motivo in why (testi di avatar-ack); chi chiama poi abort(). RAM: ~140 byte, sullo stack del task di rete.
class AvatarDownload {
 public:
  bool begin(AvatarStorage &st, const AvatarOffer &o, char *why, size_t cap);
  bool write(const uint8_t *p, size_t n, char *why, size_t cap);
  bool finish(Avatar &hdr, char *why, size_t cap);
  void abort();

 private:
  AvatarStorage *st_ = nullptr;
  char id_[kAvatarRefLen] = "";
  uint32_t crc_ = 0, size_ = 0, got_ = 0;
  CavtStream cs_;
};
```

In `avatars.cpp`: `#include <stdio.h>` e `#include "theme_proto.h"`; `MemAvatarStorage::eraseAll` finisce con
`tempAbort();`; sotto di lei:

```cpp
bool MemAvatarStorage::tempBegin() {
  if (failWrites) return false;
  tempLen_ = 0;
  tempOpen_ = true;
  return true;
}
bool MemAvatarStorage::tempAppend(const uint8_t *p, uint16_t n) {
  if (failWrites || !tempOpen_ || tempLen_ + n > kCavtMaxBytes) return false;
  memcpy(temp_ + tempLen_, p, n);
  tempLen_ += n;
  return true;
}
bool MemAvatarStorage::tempEnd() {
  if (!tempOpen_) return false;
  tempOpen_ = false;
  return true;
}
void MemAvatarStorage::tempAbort() {
  tempOpen_ = false;
  tempLen_ = -1;
}
bool MemAvatarStorage::adoptTemp(int slot) {
  if (failWrites || tempOpen_ || tempLen_ < 0 || slot < 0 || slot >= kSlots) return false;
  memcpy(data_[slot], temp_, tempLen_);
  size_[slot] = tempLen_;
  tempLen_ = -1;
  writes++;
  return true;
}
```

In `AvatarCache::store` la scelta del posto (le righe da `slot = lookup(id, 0, false);` alla fine del ciclo del meno
usato) diventa `slot = pickSlot(id);`; prima di `AvatarCache::find`:

```cpp
int AvatarCache::pickSlot(const char *id) const {
  int slot = lookup(id, 0, false);  // versione vecchia dello stesso avatar
  for (int i = 0; i < kSlots && slot < 0; i++)
    if (!used_[i]) slot = i;
  if (slot < 0) {  // pieno: il meno usato
    slot = 0;
    for (int i = 1; i < kSlots; i++)
      if (stamp_[i] < stamp_[slot]) slot = i;
  }
  return slot;
}

AvatarError AvatarCache::adopt(const Avatar &hdr, int *slotOut) {
  if (slotOut) *slotOut = -1;
  if (!st_) return AvatarError::Short;  // niente memoria: non salvato
  int slot = lookup(hdr.id, hdr.crc, true);
  if (slot >= 0) {  // già presente (arrivato nel frattempo con `avatar`)
    st_->tempAbort();
    stamp_[slot] = ++clock_;
    if (slotOut) *slotOut = slot;
    return AvatarError::Ok;
  }
  slot = pickSlot(hdr.id);
  used_[slot] = false;
  if (!st_->adoptTemp(slot)) {
    st_->tempAbort();
    st_->eraseSlot(slot);
    saveIndex();
    return AvatarError::Short;  // rinomina fallita: per chi chiama è «non salvato»
  }
  Avatar a = hdr;
  a.data = nullptr;
  ctx_[slot] = Ctx{st_, slot};
  a.read = readCb;
  a.ctx = &ctx_[slot];
  av_[slot] = a;
  used_[slot] = true;
  stamp_[slot] = ++clock_;
  saveIndex();
  if (slotOut) *slotOut = slot;
  return AvatarError::Ok;
}
```

e in fondo, prima della chiusura di `namespace cal`:

```cpp
// ---------- 0.14.0 (R4): avatar a pezzi ----------
bool decodeAvatarOffer(JsonVariantConst root, AvatarOffer &out) {
  if (msgType(root) != MsgType::AvatarOffer) return false;
  const char *id = root["id"].as<const char *>(), *tok = root["token"].as<const char *>();
  if (!validAvatarId(id) || !theme::lowerHex(tok, 32) || !root["crc"].is<uint32_t>() || !root["size"].is<uint32_t>())
    return false;
  const uint32_t size = root["size"].as<uint32_t>();
  if (size < kCavtMinBytes || size > uint32_t(kCavtMaxBytes)) return false;
  AvatarOffer o;
  memset(&o, 0, sizeof o);
  scopy(o.id, id);
  o.crc = root["crc"].as<uint32_t>();
  o.size = size;
  scopy(o.token, tok);
  out = o;
  return true;
}

size_t encodeAvatarAck(const char *id, uint32_t crc, bool ok, const char *error, char *out, size_t cap) {
  if (!validAvatarId(id)) return 0;  // il servizio lo scarterebbe
  JsonDocument d;
  d["t"] = "avatar-ack";
  d["v"] = 1;
  d["id"] = id;
  d["crc"] = crc;
  d["ok"] = ok;
  if (!ok && error && error[0]) {
    char e[theme::kAckErrorMax + 1];
    theme::cutUtf8(e, error, theme::kAckErrorMax);
    d["error"] = e;  // copiato nel documento (char* non costante)
  }
  const size_t len = measureJson(d);
  if (len + 1 > cap) return 0;
  return serializeJson(d, out, cap);
}

bool avatarUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap) {
  return theme::blobUrl(host, port, "avatars", token, out, cap);
}

bool AvatarDownload::begin(AvatarStorage &st, const AvatarOffer &o, char *why, size_t cap) {
  st_ = &st;
  scopy(id_, o.id);
  crc_ = o.crc;
  size_ = o.size;
  got_ = 0;
  cs_.begin();
  if (st.tempBegin()) return true;
  st_ = nullptr;
  snprintf(why, cap, "spazio per gli avatar non disponibile");
  return false;
}

bool AvatarDownload::write(const uint8_t *p, size_t n, char *why, size_t cap) {
  if (!st_) return false;
  if (n > size_ - got_) { snprintf(why, cap, "più byte di quelli offerti"); return false; }
  for (size_t i = 0; i < n;) {  // la memoria vuole pezzi da al più 65 535 byte
    const uint16_t k = n - i > 0xFFFF ? 0xFFFF : uint16_t(n - i);
    if (!st_->tempAppend(p + i, k)) { snprintf(why, cap, "scrittura non riuscita"); return false; }
    i += k;
  }
  cs_.feed(p, n);
  got_ += uint32_t(n);
  return true;
}

bool AvatarDownload::finish(Avatar &hdr, char *why, size_t cap) {
  if (!st_) return false;
  if (got_ != size_) { snprintf(why, cap, "download incompleto"); return false; }
  if (!st_->tempEnd()) { snprintf(why, cap, "scrittura non riuscita"); return false; }
  const AvatarError e = cs_.finish(hdr);
  if (e != AvatarError::Ok) { snprintf(why, cap, "%s", avatarErrorText(e)); return false; }
  if (strcmp(hdr.id, id_) != 0) { snprintf(why, cap, "l'avatar non è quello offerto"); return false; }
  if (hdr.crc != crc_) { snprintf(why, cap, "CRC diverso da quello offerto"); return false; }
  return true;
}

void AvatarDownload::abort() {
  if (st_) st_->tempAbort();
  st_ = nullptr;
}
```

- [ ] **Step 6: `MsgType::AvatarOffer` e `NetCmd::AvatarAck`**

`apply_state.h`, in fondo a `MsgType` (dopo `ThemeOffer, ThemeUse,`):

```cpp
  // 0.14.0 (R4): avatar a pezzi (core/avatars): servizio → display, anche senza autenticazione
  AvatarOffer,
```

`apply_state.cpp`, in `msgType` dopo `theme-use`: `if (streq(t, "avatar-offer")) return MsgType::AvatarOffer;` (il
filtro fa già passare `id`, `crc`, `size`, `token`: aggiornare solo il commento «0.13.0: theme-offer … ; 0.14.0:
avatar-offer {id, crc, size, token}»).
`ui.h`, in fondo a `NetCmd` (dopo `ThemeCur,`):
`AvatarAck,        // 0.14.0 (R4): avatar scaricato e adottato (o no) dalla cache: id, avatarCrcs[0], ok, error`;
`ui.cpp`, in `netCmdKey` `"themecur"` → `"themecur", "avatar-ack"`.

- [ ] **Step 7: `core/close_storm`**

```cpp
// 0.14.0 (R4): chiusure del WebSocket a raffica. Dal vivo (0.13.0-ed, avatar del Gufo): il messaggio `avatar` chiede
// un blocco da 9,5 KB che dopo il Bluetooth non c'è, la libreria chiude con 1011, la scheda si ricollega subito e il
// servizio rimanda l'avatar: 100 connessioni in 10 minuti. Qui la regola pura (provata in test_close_storm); il task
// di rete la chiama a ogni connessione e chiusura e usa l'attesa per il primo tentativo (gWsHoldMs).
#pragma once
#include <stdint.h>

namespace cal {

class CloseStorm {
 public:
  static constexpr uint32_t kShortMs = 20000;   // connessione «breve»: chiusa prima di 20 s
  static constexpr uint32_t kWindowMs = 60000;  // brevi entro 60 s l'una dall'altra (più l'attesa data) = raffica
  static constexpr uint32_t kStableMs = 60000;  // una connessione vissuta 60 s chiude la raffica
  static constexpr uint32_t kFirstMs = 5000;    // prima attesa, poi doppia fino a kMaxMs
  static constexpr uint32_t kMaxMs = 30000;     // = cal::kRetrySlowMs: mai oltre i tentativi lenti
  static constexpr int kFreeShort = 2;          // le prime due brevi riprovano subito, come prima

  void opened(uint32_t now);
  // Chiusura di una connessione aperta: attesa prima del primo tentativo (0 = subito). allocFailed = un'allocazione
  // fallita durante la connessione (la 1011 della libreria non arriva fino a noi: si vede dal contatore).
  uint32_t closed(uint32_t now, bool allocFailed);
  int shortCloses() const { return n_; }

 private:
  uint32_t openedAt_ = 0, last_ = 0, hold_ = 0;
  bool open_ = false;
  uint8_t n_ = 0;
};

}  // namespace cal
```

```cpp
#include "core/close_storm.h"

namespace cal {

void CloseStorm::opened(uint32_t now) {
  openedAt_ = now;
  open_ = true;
}

uint32_t CloseStorm::closed(uint32_t now, bool allocFailed) {
  const uint32_t lived = open_ ? now - openedAt_ : 0;
  open_ = false;
  if (!allocFailed && lived >= kStableMs) {  // connessione stabile: raffica finita
    n_ = 0;
    hold_ = 0;
    return 0;
  }
  if (!allocFailed && lived >= kShortMs) return 0;  // né breve né stabile: niente di nuovo
  if (n_ && now - last_ > kWindowMs + hold_) {  // la breve di prima è lontana: si ricomincia
    n_ = 0;
    hold_ = 0;
  }
  last_ = now;
  if (n_ < 255) n_++;
  if (!allocFailed && n_ <= kFreeShort) return 0;
  hold_ = hold_ ? (hold_ * 2 > kMaxMs ? kMaxMs : hold_ * 2) : kFirstMs;
  return hold_;
}

}  // namespace cal
```

- [ ] **Step 8: Vederle passare, con tutte le prove native**

Run: `cd firmware && pio test -e native -f test_avatar -f test_close_storm 2>&1 | tail -n 6 && pio test -e native 2>&1 | tail -n 3 && pio test -e native_asan 2>&1 | tail -n 3`
Expected: tutto `PASSED` (`test_avatar` 20 prove, `test_close_storm` 6); i temi (`theme_proto` toccato) verdi anche
con AddressSanitizer. Prova a mano facoltativa: togliendo il controllo dei pixel da `CavtStream::feed`,
`test_cavt_stream_matches_parse` fallisce «rovinato col CRC giusto».

- [ ] **Step 9: Commit**

```bash
git add firmware/src/gfx/avatar.h firmware/src/gfx/avatar.cpp firmware/src/core/theme_proto.h firmware/src/core/theme_proto.cpp firmware/src/core/avatars.h firmware/src/core/avatars.cpp firmware/src/core/apply_state.h firmware/src/core/apply_state.cpp firmware/src/core/ui.h firmware/src/core/ui.cpp firmware/src/core/close_storm.h firmware/src/core/close_storm.cpp firmware/test/test_avatar firmware/test/test_close_storm
git commit -m "Firmware: avatar a pezzi nella parte pura (CavtStream con i controlli di parseAvatar, AvatarDownload, AvatarCache::adopt, avatar-offer e avatar-ack) e attesa dopo chiusure del WebSocket a raffica (CloseStorm)"
```

---

### Task 9: Firmware: avatar a pezzi nel task di rete, attesa dopo le chiusure a raffica, documenti di R4

Chi tocca `/av/dl` e quando: solo il task di rete fra `avatar-offer` e la fine del download (stato `Pending`); solo il
loop dopo, finché non risponde `NetCmd::AvatarAck` (stato `Adopting`: il task di rete non accetta altre offerte, ne
risponde `avatar-ack` negativo «un altro avatar in arrivo, riprova»); `resume()` (nel loop, task di rete fermo)
svuota la coda e butta il temporaneo. Il WebSocket chiuso con l'offerta in `Pending` la butta, come i temi; in
`Adopting` il loop finisce comunque (l'avatar è valido) e l'ack si perde se il PC non c'è più: il servizio l'ha già
contata come fallita e lo riofferta dopo 60 s, se la scheda lo chiede ancora (non lo chiede: ce l'ha).

**Files:**
- Modify: `firmware/src/hw/avatar_fs.cpp` (temporaneo `/av/dl`)
- Modify: `firmware/src/net/net.h` (`takeAvatarFile`), `firmware/src/net/net.cpp` (`hello.caps`, `fetchBlob` in comune coi temi, `avatar-offer`, `avatarLoop`, `NetCmd::AvatarAck`, `CloseStorm`, coda, `resume`)
- Modify: `firmware/src/main.cpp` (`avatarsLoop`: adozione e risposta)
- Modify: `docs/PROTOCOLLO.md` («Sequenza», `hello`, «Avatar»), `docs/DECISIONI.md`

**Interfaces:**
- Consumes: tutto il Task 8; il filo del Task 7.
- Produces: `bool net::takeAvatarFile(::Avatar &out);` (coda da 1, `sizeof(Avatar)`); righe del registro
  `[avatar] avatar-offer <id>: <n> byte, scarico al prossimo giro`, `[memoria] prima dell'avatar a pezzi: …`,
  `[avatar] <id> scaricato a pezzi (<n> byte, crc …) in <ms> ms`, `[memoria] dopo l'avatar a pezzi: …`,
  `[avatar] <id> crc … a pezzi, nel posto <k>`, `[avatar] avatar-ack <id>: salvato | rifiutato · <motivo>`,
  `[ws] <n> chiusure brevi di fila[, con memoria mancata]`, `[memoria] WebSocket chiuso: …` (le usa la prova L13).

Il task non ha prove native nuove (la logica è nel Task 8): si controlla con la build e con la prova dal vivo L13.

- [ ] **Step 1: `hw/avatar_fs.cpp`**

Sotto `const char *const kTmp = "/av/tmp";`:
`const char *const kDl = "/av/dl";  // 0.14.0 (R4): avatar scaricato a pezzi dal task di rete, prima di adoptTemp`.
In `LittleFsStorage`, `eraseAll()` finisce con `tempAbort();` e sotto di lei:

```cpp
  // 0.14.0 (R4): download a pezzi nel task di rete (AvatarDownload) in /av/dl; writeSlot e writeIndex usano /av/tmp
  // dal loop, mai lo stesso file. adoptTemp (loop, AvatarCache::adopt) rinomina /av/dl sul posto: rinomina diretta
  // di littlefs sopra il file vecchio, come i temi. Il task di rete e il loop non lo toccano mai insieme (net.cpp).
  bool tempBegin() override {
    tempAbort();
    dl_ = LittleFS.open(kDl, "w");
    return bool(dl_);
  }
  bool tempAppend(const uint8_t *p, uint16_t n) override { return dl_ && dl_.write(p, n) == n; }
  bool tempEnd() override {
    if (!dl_) return false;
    dl_.close();
    return LittleFS.exists(kDl);
  }
  void tempAbort() override {
    if (dl_) dl_.close();
    if (LittleFS.exists(kDl)) LittleFS.remove(kDl);
  }
  bool adoptTemp(int slot) override {
    char p[16];
    slotPath(slot, p, sizeof p);
    return LittleFS.exists(kDl) && LittleFS.rename(kDl, p);
  }
```

con `File dl_;` fra i privati (prima di `writeFile`). In `avfs::begin()`, dopo la riga di `kTmp`:
`if (LittleFS.exists(kDl)) LittleFS.remove(kDl);    // 0.14.0: download interrotto da un riavvio`.

- [ ] **Step 2: `net/net.h`**

Sotto `takeAvatar`:

```cpp
// 0.14.0 (R4): avatar scaricato a pezzi e controllato (avatar-offer → GET /avatars/<token>), ancora nel temporaneo di
// avfs: il loop lo adotta (AvatarCache::adopt) e risponde con NetCmd::AvatarAck (id, avatarCrcs[0], ok, error). Fino
// alla risposta il task di rete non scarica altri avatar. Uno alla volta.
bool takeAvatarFile(::Avatar &out);
```

(`Avatar` sta fuori dai namespace, in `gfx/avatar.h`: `cal::Avatar` non esiste.)

- [ ] **Step 3: `net/net.cpp`**

Include `"core/close_storm.h"` e `"hw/avatar_fs.h"`. Sotto la riga delle code:
`QueueHandle_t gAvFile = nullptr;  // 0.14.0 (R4): avatar scaricato a pezzi, da adottare nel loop (uno alla volta)`.
Sotto `cal::theme::Installer gThemeInst;`:

```cpp
// 0.14.0 (R4), avatar a pezzi (PROTOCOLLO.md «Avatar»): un'offerta alla volta. Idle → Pending (offerta valida, si
// scarica al prossimo giro: avatarLoop) → Adopting (file pronto in avfs, il loop lo adotta e risponde AvatarAck) → Idle.
enum AvSt : uint8_t { kAvIdle, kAvPending, kAvAdopting };
uint8_t gAvSt = kAvIdle;
cal::AvatarOffer gAvOffer;
// 0.14.0 (R4): chiusure del WebSocket a raffica (core/close_storm), attesa crescente prima del primo tentativo.
cal::CloseStorm gStorm;
uint32_t gAllocAtOpen = 0;  // allocazioni fallite all'apertura: cambiate alla chiusura = chiusura per memoria
```

In `sendHello` il commento del documento diventa `// campi esatti di protocol/examples/hello-avatar-dl.json (schema chiuso)`
e dopo `d["paired"] = …`:

```cpp
  d["caps"].add("avatar-dl");    // 0.14.0 (R4): avatar a pezzi (avatar-offer → GET /avatars/<token>)
```

`themeLoop` (dal suo commento fino a prima di `onText`) si sostituisce con il download in comune, `themeLoop` che lo
usa e la parte degli avatar:

```cpp
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
  memLog("prima dell'avatar a pezzi");
  cal::AvatarDownload dl;
  ::Avatar hdr;
  bool ok = cal::avatarUrl(pcIp, pcPort, o.token, url, sizeof url);
  if (!ok) cal::scopy(why, "indirizzo del download non valido");
  ok = ok && dl.begin(avfs::storage(), o, why, sizeof why);
  AvatarSink sink(dl);
  ok = ok && fetchBlob(url, o.size, sink, why, sizeof why);
  ok = ok && dl.finish(hdr, why, sizeof why);
  if (ok && xQueueSend(gAvFile, &hdr, 0) != pdTRUE) { ok = false; cal::scopy(why, "un altro avatar in arrivo, riprova"); }
  if (!ok) {
    dl.abort();
    gAvSt = kAvIdle;
    sendAvatarAck(o.id, o.crc, false, why);
    memLog("avatar a pezzi non riuscito");
    return;
  }
  gAvSt = kAvAdopting;  // il loop adotta e risponde (NetCmd::AvatarAck)
  LogSerial.printf("[avatar] %s scaricato a pezzi (%lu byte, crc %08lx) in %lu ms\n", o.id, (unsigned long)o.size,
                   (unsigned long)o.crc, (unsigned long)(millis() - t0));
  memLog("dopo l'avatar a pezzi");
}
```

In `onText`, prima di `case cal::MsgType::ThemeOffer:`:

```cpp
    case cal::MsgType::AvatarOffer: {  // 0.14.0 (R4): anche senza autenticazione, come `avatar`; uno alla volta
      cal::AvatarOffer o;
      if (!cal::decodeAvatarOffer(root, o)) { LogSerial.println("[avatar] avatar-offer non valido: ignorato"); break; }
      if (gAvSt != kAvIdle) { sendAvatarAck(o.id, o.crc, false, "un altro avatar in arrivo, riprova"); break; }
      gAvOffer = o;
      gAvSt = kAvPending;
      LogSerial.printf("[avatar] avatar-offer %s: %lu byte, scarico al prossimo giro\n", o.id, (unsigned long)o.size);
      break;
    }
```

In `handle()`, in testa:

```cpp
  if (r.cmd == cal::NetCmd::AvatarAck) {  // 0.14.0 (R4): il loop ha adottato (o no) l'avatar scaricato
    gAvSt = kAvIdle;
    sendAvatarAck(r.id, r.avatarCrcs[0], r.ok, r.error);
    return;
  }
```

In `onWs`, ramo `WStype_CONNECTED`, sotto `lastRx = millis();`:

```cpp
      gStorm.opened(millis());  // 0.14.0 (R4)
      gAllocAtOpen = gAllocFails.load(std::memory_order_relaxed);
```

e il ramo `WStype_DISCONNECTED` diventa:

```cpp
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
```

In `wsClosed`, sotto `dropOffer("collegamento chiuso");`:
`if (gAvSt == kAvPending) gAvSt = kAvIdle;  // 0.14.0: offerta dell'avatar scartata, si richiede al ritorno`.
In `suspendNow`, sotto `gThemePending = false;`:
`if (gAvSt == kAvPending) gAvSt = kAvIdle;  // 0.14.0: idem per l'avatar (Adopting lo azzera resume)`.
Nel ciclo di `task()`, sotto `themeLoop();`: `avatarLoop();  // 0.14.0 (R4)`.
In `begin()`, sotto `gAv = xQueueCreate(…)`: `gAvFile = xQueueCreate(1, sizeof(::Avatar));  // 0.14.0 (R4)`.
Sotto `takeAvatar`:
`bool takeAvatarFile(::Avatar &out) { return gAvFile && xQueueReceive(gAvFile, &out, 0) == pdTRUE; }`.
In `resume()`, prima di `gSusp = kRunning;`:

```cpp
  // 0.14.0 (R4): un avatar scaricato prima della partita e non ancora adottato si richiede; il task non c'è, il loop
  // è qui: nessuno tocca il temporaneo nello stesso momento.
  if (gAvFile) xQueueReset(gAvFile);
  avfs::storage().tempAbort();
  gAvSt = kAvIdle;
```

- [ ] **Step 4: `main.cpp`**

In `avatarsLoop`, dopo il ciclo di `net::takeAvatar` (prima di «avatar-need: subito …»):

```cpp
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
```

- [ ] **Step 5: Compilare e misurare**

Run:
```bash
cd firmware && pio test -e native 2>&1 | tail -n 3 && pio run -e heltec213 2>&1 | grep -E "error|warning|RAM:|Flash:|SUCCESS|FAILED" | grep -v -E "framework|libdeps" \
 && ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-size -A .pio/build/heltec213/firmware.elf | awk '/dram0.bss|dram0.data/ {s += $2} END {print "RAM statica", s}'
```
Expected: prove native verdi; `SUCCESS` senza warning in `net/`, `hw/avatar_fs`, `main.cpp`; RAM statica annotata
(nel prototipo +136 byte rispetto allo stesso albero senza i Task 7–9: `gAvOffer` 68, `gStorm` 20, `File dl_`,
`gAvSt`, `gAllocAtOpen`, `gAvFile`). Il tetto di tutto il piano si controlla nel Task 11.

- [ ] **Step 6: PROTOCOLLO**

In `docs/PROTOCOLLO.md`:
- intestazione, elenco delle aggiunte: «…; poi i temi delle schermate, sezione «Temi»; poi (firmware 0.14.0) gli
  avatar a pezzi, sezione «Avatar»».
- «Sequenza», dopo la riga di `avatar`:
  `servizio → display   avatar-offer → display: GET /avatars/<token> → avatar-ack   (al posto di avatar, con hello.caps avatar-dl)`.
- tabella di `hello`, riga nuova (dopo `paired`): campo `caps`, tipo «array di string, facoltativo», note «firmware 0.14.0: capacità; `avatar-dl` = avatar a pezzi (sezione «Avatar»). Il servizio ignora quelle che non conosce».
- «Avatar», `avatar-need`: «Il servizio risponde con un messaggio `avatar` per ogni id che conosce» →
  «Il servizio risponde, per ogni id che conosce, con un messaggio `avatar` o, se la scheda ha detto `caps:
  ["avatar-dl"]`, con un'offerta `avatar-offer` (una alla volta)»; dopo «… prima di 3 s (difesa da un display che chiede
  in un ciclo).» aggiungere «Dopo una consegna fallita (`avatar-ack` negativo, nessun `avatar-ack` entro 30 s, connessione
  chiusa con l'offerta in corso, o connessione chiusa entro 10 s da un messaggio `avatar`) lo stesso avatar (id e CRC)
  non si rimanda alla stessa scheda (`hello.board`) per 60 s, nemmeno su una connessione nuova: è il ciclo visto dal
  vivo con la 0.13.0-ed (1011, riconnessione, di nuovo `avatar`).»
- dopo la sezione `avatar`, due sezioni nuove:

```markdown
### `avatar-offer` (servizio → display, solo con `hello.caps` `avatar-dl`)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | id dell'avatar |
| `crc` | u32 | CRC del CAVT, come `avatarCrc` |
| `size` | int | byte del CAVT (2 172–7 072) |
| `token` | string | 16 byte casuali in 32 cifre esadecimali minuscole |

Anche **senza autenticazione**, come `avatar` (gli avatar non sono dati sensibili; una scheda non abbinata resterebbe
altrimenti col messaggio `avatar` da 9,5 KB che col Bluetooth acceso non ha il blocco per riceverlo). Una offerta alla
volta per connessione; la successiva parte dopo l'`avatar-ack` o dopo 30 s senza. La scheda costruisce l'URL come per i
temi: `http://<host>:<porta>/avatars/<token>`. **Download**: `GET /avatars/<token>` sulla porta dei display → 200
`application/octet-stream`, `Content-Length` = `size`, `X-Calamaio-Crc`, `Cache-Control: no-store`; token monouso,
5 minuti, solo per l'IP della connessione che ha ricevuto l'offerta: sconosciuto o malformato 404, già usato o scaduto
410, IP diverso 403 (il token resta valido); muore anche all'`avatar-ack`, alla chiusura della connessione e allo scadere
dei 30 s. La scheda scarica nel task di rete (HTTP/1.0, connessione e GET 5 s, corpo al massimo 15 s, mai oltre
`size`) a pezzi da 512 byte in `/av/dl`, con i controlli di `unpack()` (AVATAR.md §8) fatti man mano, controlla che
`id` e CRC dell'intestazione siano quelli dell'offerta, poi il loop rinomina il file nella cache (rinomina diretta di
LittleFS). Un errore in qualunque punto butta il temporaneo. Un'offerta non valida (id, token, `size` fuori regola) si
ignora; con un'altra in corso `avatar-ack` negativo «un altro avatar in arrivo, riprova». Esempio:
`protocol/examples/avatar-offer.json`.

### `avatar-ack` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `id`, `crc` | string, u32 | quelli dell'offerta |
| `ok` | bool | salvato nella cache |
| `error` | string, facoltativo | solo con `ok: false`, ≤ 127 byte: i testi dei temi per il download («download non riuscito (HTTP 410)», «download interrotto», «download troppo lento», «più byte di quelli offerti», «scrittura non riuscita», «download incompleto»), un errore del CAVT («checksum errato», …), «l'avatar non è quello offerto», «CRC diverso da quello offerto», «spazio per gli avatar non disponibile», «salvataggio non riuscito», «un altro avatar in arrivo, riprova» |

Il servizio accetta un `avatar-ack` solo per l'offerta in corso su quella connessione (stesso `id` e `crc`); gli altri
li ignora («senza un'offerta in corso»). Esempi: `protocol/examples/avatar-ack.json`, `avatar-ack-errore.json`.
Scheda: dalla terza chiusura del WebSocket entro 20 s dall'apertura nel giro di un minuto, o subito dopo una connessione
in cui un'allocazione è fallita (la chiusura 1011 della libreria quando non ha il blocco per un messaggio), il primo
tentativo dopo la chiusura aspetta 5 s, poi 10, 20, 30, in silenzio; una connessione che dura un minuto azzera.
```

- [ ] **Step 7: DECISIONI**

In `docs/DECISIONI.md`, sezione «App iPhone» (accanto alle voci della Fase P3):

```markdown
- **Avatar a pezzi (R4, firmware 0.14.0, difetto dal vivo del Gufo)**: col Bluetooth acceso (0.11, ~48 KB) il messaggio `avatar` (CAVT 7 072 B in base64 = 9 432 caratteri) chiedeva alla scheda un blocco da 9 491 B e poi ~8 200 B per il documento JSON: blocco massimo 7,6–13,8 KB, `malloc` fallita, chiusura 1011, riconnessione subito, `avatar` di nuovo (100 connessioni in 10 minuti); gli avatar predefiniti funzionavano solo perché già nella cache. Ora gli avatar viaggiano come i temi: `avatar-offer {id, crc, size, token}` → `GET /avatars/<token>` sulla 8766 a pezzi da 512 byte in `/av/dl` (niente base64, niente blocco da 9,5 KB) → controlli di `unpack()` man mano (`CavtStream`, stessa funzione dei controlli di `parseAvatar`) → rinomina nella cache nel loop (`AvatarCache::adopt`) → `avatar-ack`. **Capacità in `hello` (`caps: ["avatar-dl"]`), non versione**: due rami con numerazioni parallele (`0.13.0-ed`, `0.14.0`) sbaglierebbero un confronto di versioni; lo schema chiude `hello`, quindi il campo è nuovo e provato; i servizi vecchi lo ignorano e la scheda nuova capisce ancora `avatar`. **Senza autenticazione**, come `avatar-need` e `avatar`: un avatar non è un dato sensibile e finisce solo nella cache, mentre una scheda non abbinata resterebbe sul messaggio che si rompe; il token (16 byte casuali, monouso, solo per l'IP della connessione, 5 minuti, morto con ack, chiusura o 30 s) tiene il download legato a chi l'ha chiesto. **Difese dal ciclo**: la scheda aspetta 5, 10, 20, 30 s prima del primo tentativo dalla terza chiusura breve (< 20 s) in un minuto o subito dopo un'allocazione fallita durante la connessione (la 1011 non arriva a `WStype_DISCONNECTED`: si vede dal contatore di `onAllocFailed`), in silenzio (`gWsQuiet`); il servizio non rimanda lo stesso avatar (id e CRC) alla stessa scheda per 60 s dopo una consegna fallita (ack negativo, niente ack in 30 s, connessione chiusa a metà, `avatar` seguito da una chiusura entro 10 s), anche su connessioni nuove: protegge anche le schede 0.13.0-ed. Riuso: `fetchBlob` (download dei temi) e `blobTokenStatus`/`serveBlob` (regole del token dei temi). RAM statica: +136 byte nel prototipo. Il messaggio `avatar` resta per le schede vecchie (se è una scelta sbagliata: un protocollo in più da tenere). Prova dal vivo: L13 (esito da annotare).
```

- [ ] **Step 8: Commit**

```bash
git add firmware/src/hw/avatar_fs.cpp firmware/src/net/net.h firmware/src/net/net.cpp firmware/src/main.cpp docs/PROTOCOLLO.md docs/DECISIONI.md
git commit -m "Firmware: avatar a pezzi nel task di rete (avatar-offer, GET /avatars a pezzi in /av/dl, adozione nel loop, avatar-ack, hello.caps avatar-dl, download in comune coi temi), attesa dopo chiusure del WebSocket a raffica; PROTOCOLLO e DECISIONI"
```

---

### Task 10: hw/blepad: il telefono come secondo tipo

**Files:**
- Modify: `firmware/src/core/pad_phone.h`, `firmware/src/core/pad_phone.cpp` (`PhoneIdle`, F7)
- Test: `firmware/test/test_pad_phone/test_main.cpp`
- Modify: `firmware/src/hw/blepad.h`
- Modify: `firmware/src/hw/blepad.cpp` (riscritto: sotto il file intero)

**Interfaces:**
- Consumes: `pad::parsePhoneReport`, `pad::Keepalive`, `pad::keepaliveRelease` (Task 2); `pad::Addr`, `pad::BondBook`, `pad::staleBonds`, `pad::migrateLegacy`, `pad::legacyIsXbox`, `pad::bondKey`, `pad::kBondVersionKey` (Task 3); `pad::ScanAd`, `pad::pickAd`, `pad::secFor`, `pad::linkSecure(…, sc)`, `LinkMachine::pair/connected/kind/setBonded/cancel` (Task 4).
- Produces anche, in `cal::pad` (F7): `class PhoneIdle { static const uint32_t kSilentMs = 10000, kBackgroundMs = 5000; void start(uint32_t now); void stop(); void report(uint32_t now, bool fg); bool due(uint32_t now) const; }`.
- Produces (in `cal::blepad`, usati dal Task 11):
  - `void startPairing(pad::Kind k = pad::Kind::Xbox);` `void cancelPairing();`
  - `void forget(pad::Kind k);` `void forgetAll();` (Ripristino di fabbrica: NVS di NimBLE e libro)
  - `bool bonded();` (almeno uno) `bool bonded(pad::Kind k);` (a radio spenta legge `calamaio-gb`; un abbinamento della 0.13 non ancora migrato vale come Xbox)
  - `pad::Kind kind();` (collegamento o associazione in corso)
  - `bool takePasskey(uint32_t &key);` `bool takePairFailed();`
  - `uint32_t tickClock(uint32_t now);` (orologio limitato per `MenuMapper::tick`, Task 2)
  - `poll()` invariata nell'uso; consegna anche il «tutto rilasciato» del mantenimento.

`blepad` non ha prove native (NimBLE esiste solo sulla scheda): la logica è nei Task 2–4 e in `PhoneIdle` qui sotto.
Il resto si controlla con la build e con le prove dal vivo L1–L12. Correzioni del pre-flight dentro il file intero:
F3 (`bit()` è una macro di `Arduino.h`: `kindBit()`), F5 (`storedSc`: SC letto da `ble_store_read_peer_sec`), F6
(`securePhone`: cifratura asincrona del telefono, attesa annullabile con tempo limite, abbinamento appena fatto
cancellato dopo un «annulla», finestra chiusa dopo un codice sbagliato), F7 (`gIdle`: telefono in secondo piano o muto
scollegato), D2 (`bonded(Kind::Xbox)` a radio spenta con `legacyIsXbox` e `bondsInNvs` che conta), D3 (`listBonds` per
`pruneBonds` e `reconcile`).

- [ ] **Step 1: Prova che fallisce per `PhoneIdle` (F7)**

In `test_pad_phone/test_main.cpp`, prima di `main` (e il `RUN_TEST`):

```cpp
// F7: secondo piano (flags bit0 = 0) e poi silenzio 5 s, o 10 s senza report: la scheda scollega il telefono.
static void test_phone_idle_rules() {
  pad::PhoneIdle p;
  TEST_ASSERT_FALSE(p.due(100000));  // mai collegato
  p.start(1000);
  TEST_ASSERT_FALSE(p.due(10999));
  TEST_ASSERT_TRUE(p.due(11000));    // 10 s senza report
  p.report(11000, true);             // di nuovo vivo
  TEST_ASSERT_FALSE(p.due(15000));
  p.report(15000, false);            // l'app va in secondo piano: «tutto rilasciato»
  TEST_ASSERT_FALSE(p.due(19999));
  TEST_ASSERT_TRUE(p.due(20000));    // 5 s dopo
  p.report(19000, true);             // tornata in primo piano prima dei 5 s
  TEST_ASSERT_FALSE(p.due(28999));
  p.stop();
  TEST_ASSERT_FALSE(p.due(100000));
}
```

Run: `cd firmware && pio test -e native -f test_pad_phone 2>&1 | tail -n 5` → Expected: errore di compilazione (`PhoneIdle`).

- [ ] **Step 2: `PhoneIdle` in `core/pad_phone`**

In `pad_phone.h`, dopo `keepaliveRelease`:

```cpp
// F7 (0.14.0): un telefono in secondo piano o sparito non tiene occupata la scheda. iOS non chiude il collegamento
// quando l'app va in secondo piano: la scheda lo scollega dopo kSilentMs senza report validi, o kBackgroundMs dopo
// un report di secondo piano (flags bit0 = 0) senza report di primo piano dopo, e torna a cercare (l'Xbox).
class PhoneIdle {
 public:
  static const uint32_t kSilentMs = 10000, kBackgroundMs = 5000;
  void start(uint32_t now) { last_ = now; bg_ = false; on_ = true; }  // telefono collegato
  void stop() { on_ = false; }
  void report(uint32_t now, bool fg) { last_ = now; bg_ = !fg; }  // report valido
  bool due(uint32_t now) const { return on_ && now - last_ >= (bg_ ? kBackgroundMs : kSilentMs); }

 private:
  uint32_t last_ = 0;
  bool bg_ = false, on_ = false;
};
```

In `pad_phone.cpp`, sotto `const uint32_t Keepalive::kMs;`:

```cpp
const uint32_t PhoneIdle::kSilentMs;
const uint32_t PhoneIdle::kBackgroundMs;
```

Run: `cd firmware && pio test -e native -f test_pad_phone 2>&1 | tail -n 5` → Expected: `PASSED`.

- [ ] **Step 3: `blepad.h`**

Sostituire il blocco `// ---- contratto ----` … `// ---- piano A ----` con:

```cpp
// ---- contratto ----
void begin();          // NimBLE e task del controller; si ricollega da solo al primo associato che trova
// 0.14.0: due tipi (spec app iPhone §2.2): un Xbox e un telefono, uno per tipo (libro in calamaio-gb padx/padp)
void startPairing(pad::Kind k = pad::Kind::Xbox);  // 2 minuti per un nuovo di quel tipo (sostituisce solo quel tipo)
void cancelPairing();  // «LUNGA ANNULLA» sul codice del telefono
void forget(pad::Kind k);  // cancella l'abbinamento di quel tipo (a radio spenta: libro subito, NimBLE all'accensione)
void forgetAll();      // Ripristino di fabbrica: tutti, NVS di NimBLE compresa (a radio accesa al più tardi in end())
pad::Link link();
pad::Kind kind();      // tipo del collegamento o dell'associazione in corso
// true con il prossimo stato diverso dall'ultimo (un report alla volta, in ordine); dopo un ricollegamento
// il primo report si consegna sempre. Telefono: 3 s senza report = uno stato vuoto (mantenimento).
bool poll(pad::State &s);
bool takePasskey(uint32_t &key);  // codice nuovo da mostrare (una volta); mai nel registro
bool takePairFailed();            // telefono non autenticato: «Associazione non riuscita»
uint32_t tickClock(uint32_t now); // orologio per MenuMapper::tick (telefono sparito: mai oltre l'ultimo report + 3 s)
```

e `bool bonded();` diventa le due righe

```cpp
bool bonded();              // almeno un associato
bool bonded(pad::Kind k);   // associato di quel tipo
```

Nel commento di testa: «Controller Xbox (1914) e telefono (app Calamaio, servizio «Calamaio pad») via Bluetooth LE».

- [ ] **Step 4: `blepad.cpp` intero**

```cpp
#include "hw/blepad.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_random.h>
#include <string.h>

#ifdef USING_NIMBLE_ARDUINO_HEADERS
#include "nimble/nimble/host/include/host/ble_hs.h"
#else
#include "host/ble_hs.h"
#endif

#include "core/pad_bonds.h"
#include "core/pad_phone.h"
#include "hw/logtee.h"

// 0.14.0: NimBLE riempie la lista di risoluzione del controller con gli IRK salvati ma la accende solo con un
// indirizzo proprio privato (ble_hs_pvcy_ensure_started); la scheda usa quello pubblico. Senza, l'iPhone (che
// annuncia con un indirizzo privato che cambia) non sarebbe mai riconosciuto come associato.
extern "C" int ble_hs_pvcy_set_resolve_enabled(int enable);

namespace cal {
namespace blepad {
namespace {

static_assert(pad::kIoDisplayOnly == BLE_HS_IO_DISPLAY_ONLY, "core/pad_bonds: valore HCI");
static_assert(pad::kIoNoInputOutput == BLE_HS_IO_NO_INPUT_OUTPUT, "core/pad_bonds: valore HCI");

const uint16_t kAppearanceGamepad = 0x03C4;
const uint32_t kScanMs = 5000;
const size_t kMinReportMap = 64;  // dal vivo: 22 byte (troncato) al primo collegamento, 283 quello vero
const int kShortMapRetries = 2;
const uint16_t kItvMin = 6, kItvMax = 12;             // Xbox: 7,5–15 ms; il 1914 chiede 7,5 ms e lo si accetta
const uint16_t kPhoneItvMin = 12, kPhoneItvMax = 24;  // telefono: 15–30 ms (regole di Apple per le periferiche)
const char *const kBondNs = "nimble_bond";
const char *const kGbNs = "calamaio-gb";
const char *const kPhoneSvc = "526389E4-B789-4225-990D-0465AFB610B2";
const char *const kPhoneState = "6A34D990-D046-4834-96B8-0CBDCE4087FA";
const char *const kPhoneInfo = "9D00F77E-BB83-4667-A37F-6FBDB0F0F324";
const int kMaxBonds = CONFIG_BT_NIMBLE_MAX_BONDS;
const uint32_t kSecureMs = 35000;  // F6: attesa del codice del telefono, oltre i 30 s dello SMP

struct Rx {
  uint32_t t;     // millis() d'arrivo
  uint8_t conn;   // collegamento del report (gConn): al primo di un collegamento nuovo poll() riparte da zero
  uint8_t kind;   // pad::Kind del collegamento
  uint8_t len;
  uint8_t d[24];
};

QueueHandle_t gRx = nullptr;
TaskHandle_t gTask = nullptr;
volatile bool gActive = false, gStop = false, gTaskDone = false;
volatile bool gScanAllowed = false, gWantPair = false, gWantCancel = false;
volatile uint8_t gWantForget = 0;  // bit 0 Xbox, bit 1 telefono, 0x80 tutti (Ripristino)
volatile pad::Kind gWantPairKind = pad::Kind::Xbox;
volatile bool gFound = false, gLost = false, gBondInvalid = false;
volatile pad::Link gLinkPub = pad::Link::Off;
volatile pad::Kind gKindPub = pad::Kind::Xbox;
volatile bool gXboxPub = false, gPhonePub = false;
volatile int gBatt = -1;
volatile uint16_t gBatHandle = 0;
volatile pad::Kind gKind = pad::Kind::Xbox;  // tipo del collegamento in corso (callback di NimBLE)
volatile bool gPasskeyNew = false, gPairFail = false;
volatile uint32_t gPasskey = 0;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
NimBLEAddress gFoundAddr;           // protetti da gMux
pad::Kind gFoundKind = pad::Kind::Xbox;
pad::BondBook gBook;                // protetto da gMux (lo legge la callback della ricerca)
NimBLEClient *gClient = nullptr;
pad::LinkMachine gMachine;  // solo nel task del controller
int gShortMaps = 0;
pad::State gLast;  // ultimo stato restituito da poll() (loop)
uint32_t gLastAt = 0;
volatile uint8_t gConn = 0;  // cresce a ogni scollegamento (task di NimBLE)
uint8_t gLastConn = 0;       // collegamento di gLast (loop)
pad::Keepalive gKeep;        // loop
pad::PhoneIdle gIdle;        // F7: protetto da gMux (lo scrive onNotify, lo legge il task)
volatile bool gAuthDone = false;  // F6: onAuthenticationComplete (cifratura asincrona del telefono)
uint32_t gBadAt = 0;         // loop: ultima riga «report scartato» (al più una al secondo)

uint8_t kindBit(pad::Kind k) { return k == pad::Kind::Phone ? 2 : 1; }

void toAddr(const NimBLEAddress &a, pad::Addr &out) {
  memcpy(out.v, a.getBase()->val, 6);
  out.type = a.getType() & 1;
}

pad::BondBook bookCopy() {
  portENTER_CRITICAL(&gMux);
  const pad::BondBook b = gBook;
  portEXIT_CRITICAL(&gMux);
  return b;
}

// ---- libro nella NVS (calamaio-gb) ----
void loadBook(pad::BondBook &b, bool &migrated) {
  Preferences p;
  b = pad::BondBook();
  migrated = false;
  if (!p.begin(kGbNs, true)) return;
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    uint8_t blob[pad::kBondBlob];
    pad::Addr a;
    if (p.getBytesLength(pad::bondKey(k)) == pad::kBondBlob && p.getBytes(pad::bondKey(k), blob, sizeof blob) == sizeof blob &&
        pad::BondBook::decode(blob, sizeof blob, a))
      b.set(k, a);
  }
  migrated = p.getUChar(pad::kBondVersionKey, 0) == 1;
  p.end();
}

void saveBook(const pad::BondBook &b) {
  Preferences p;
  if (!p.begin(kGbNs, false)) {
    LogSerial.println("[pad] libro degli abbinamenti non salvato (NVS)");
    return;
  }
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    if (b.has(k)) {
      uint8_t blob[pad::kBondBlob];
      pad::BondBook::encode(b.get(k), blob);
      p.putBytes(pad::bondKey(k), blob, sizeof blob);
    } else if (p.isKey(pad::bondKey(k))) {
      p.remove(pad::bondKey(k));
    }
  }
  p.putUChar(pad::kBondVersionKey, 1);
  p.end();
}

int bondsInNvs() {
  Preferences p;
  if (!p.begin(kBondNs, true)) return 0;
  // Chiavi di NimBLE «peer_sec_1..N» (ble_store_nvs.c): un abbinamento nuovo va nel primo indice libero e il
  // vecchio si cancella dopo, quindi l'unico rimasto può stare in peer_sec_2 o oltre.
  int n = 0;
  char key[16];
  for (int i = 1; i <= kMaxBonds; i++) {
    snprintf(key, sizeof key, "peer_sec_%d", i);
    n += p.isKey(key);
  }
  p.end();
  return n;
}

// D3: abbinamenti di NimBLE, come indirizzi di NimBLE e del libro (pruneBonds, reconcile).
int listBonds(NimBLEAddress *raw, pad::Addr *list) {
  int n = NimBLEDevice::getNumBonds();
  if (n > kMaxBonds) n = kMaxBonds;
  for (int i = 0; i < n; i++) {
    raw[i] = NimBLEDevice::getBondedAddress(i);
    toAddr(raw[i], list[i]);
  }
  return n;
}

// F5: da iniziatore NimBLE non applica sm_sc_only (ble_sm.c lo guarda solo da responder): Secure Connections si
// legge dall'archivio delle chiavi dell'abbinamento appena fatto (o ritrovato).
bool storedSc(const NimBLEAddress &id) {
  ble_store_key_sec key;
  memset(&key, 0, sizeof key);
  key.peer_addr = *id.getBase();
  ble_store_value_sec v;
  return ble_store_read_peer_sec(&key, &v) == 0 && v.sc;
}

void publish() {
  gLinkPub = gMachine.link();
  gKindPub = gMachine.kind();
  const pad::BondBook b = bookCopy();
  gXboxPub = b.has(pad::Kind::Xbox);
  gPhonePub = b.has(pad::Kind::Phone);
}

// Via gli abbinamenti di NimBLE che il libro non conosce (vecchio telefono dopo il nuovo, orfani, «Dimentica» a radio
// spenta). Mai prima che il libro sia scritto.
void pruneBonds(const pad::BondBook &book) {
  NimBLEAddress raw[kMaxBonds];
  pad::Addr list[kMaxBonds];
  const int n = listBonds(raw, list);
  int stale[kMaxBonds];
  const int k = pad::staleBonds(book, list, n, stale, kMaxBonds);
  for (int j = 0; j < k; j++) {
    NimBLEDevice::deleteBond(raw[stale[j]]);
    LogSerial.println("[pad] abbinamento fuori dal libro: cancellato");
  }
}

// Il nuovo associato di tipo k: libro (sostituisce solo quel tipo), NVS, poi via quello che il libro non conosce.
void remember(pad::Kind k, const pad::Addr &id) {
  portENTER_CRITICAL(&gMux);
  gBook.set(k, id);
  const pad::BondBook b = gBook;
  portEXIT_CRITICAL(&gMux);
  saveBook(b);
  pruneBonds(b);
}

void dropKind(pad::Kind k) {
  portENTER_CRITICAL(&gMux);
  gBook.clear(k);
  const pad::BondBook b = gBook;
  portEXIT_CRITICAL(&gMux);
  saveBook(b);
  pruneBonds(b);
}

// Migrazione dalla 0.13.0-ed e abbinamenti orfani, all'avvio del task (serve l'host sincronizzato per cancellare).
void reconcile() {
  pad::BondBook b;
  bool migrated;
  loadBook(b, migrated);
  NimBLEAddress raw[kMaxBonds];
  pad::Addr list[kMaxBonds];
  const int n = listBonds(raw, list);
  const bool changed = pad::migrateLegacy(b, migrated, list, n);
  portENTER_CRITICAL(&gMux);
  gBook = b;
  portEXIT_CRITICAL(&gMux);
  if (changed) LogSerial.println("[pad] abbinamento della 0.13: è il controller (padx)");
  if (changed || !migrated) saveBook(b);
  pruneBonds(b);
  LogSerial.printf("[pad] libro: controller %s, telefono %s\n", b.has(pad::Kind::Xbox) ? "sì" : "no",
                   b.has(pad::Kind::Phone) ? "sì" : "no");
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (gFound) return;
    pad::ScanAd ad;
    toAddr(dev->getAddress(), ad.addr);
    ad.gamepad = dev->haveAppearance() && dev->getAppearance() == kAppearanceGamepad;
    ad.hid = dev->isAdvertisingService(NimBLEUUID("1812"));
    ad.phoneSvc = dev->isAdvertisingService(NimBLEUUID(kPhoneSvc));
    ad.connectable = dev->isConnectable();
    const pad::ScanPick pk = pad::pickAd(ad, bookCopy(), gLinkPub, gKindPub);
    if (!pk.take) return;
    portENTER_CRITICAL(&gMux);
    gFoundAddr = dev->getAddress();
    gFoundKind = pk.kind;
    portEXIT_CRITICAL(&gMux);
    gFound = true;
    NimBLEDevice::getScan()->stop();
  }
} gScanCb;

class ClientCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    LogSerial.printf("[pad] scollegato, motivo 0x%x\n", reason);
    gConn = gConn + 1;  // i report dopo il ricollegamento non si confrontano con quelli di prima
    gLost = true;
  }
  bool onConnParamsUpdateRequest(NimBLEClient *, const ble_gap_upd_params *) override { return true; }
  // Xbox: Just Works come la 0.13. Telefono (DisplayOnly): solo il codice a schermo, mai un confronto o un codice
  // inserito dalla scheda (non avrebbero MITM: linkSecure li rifiuterebbe comunque).
  void onConfirmPasskey(NimBLEConnInfo &ci, uint32_t) override {
    NimBLEDevice::injectConfirmPasskey(ci, gKind == pad::Kind::Xbox);
  }
  void onPassKeyEntry(NimBLEConnInfo &ci) override {
    if (gKind == pad::Kind::Xbox) NimBLEDevice::injectPassKey(ci, 0);
    else LogSerial.println("[pad] telefono: la scheda non inserisce codici, abbinamento lasciato scadere");
  }
  void onAuthenticationComplete(NimBLEConnInfo &) override { gAuthDone = true; }  // F6: cifratura riuscita
  uint32_t onPassKeyDisplay(NimBLEConnInfo &) override {
    const uint32_t k = esp_random() % 1000000;  // mai nel registro: è a schermo
    gPasskey = k;
    gPasskeyNew = true;
    LogSerial.println("[pad] telefono: codice a schermo");
    return k;
  }
} gClientCb;

void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool) {
  if (c->getHandle() == gBatHandle) {
    if (n) gBatt = d[0];
    return;
  }
  Rx r;
  r.t = millis();
  r.conn = gConn;
  r.kind = static_cast<uint8_t>(gKind);
  r.len = n > sizeof r.d ? sizeof r.d : static_cast<uint8_t>(n);
  memcpy(r.d, d, r.len);
  if (gKind == pad::Kind::Phone) {  // F7: report valido e primo piano, per scollegare un telefono in secondo piano
    pad::State st;
    pad::PhoneMeta meta;
    if (pad::parsePhoneReport(r.d, r.len, st, meta) == pad::PhoneParse::Ok) {
      portENTER_CRITICAL(&gMux);
      gIdle.report(r.t, meta.fg);
      portEXIT_CRITICAL(&gMux);
    }
  }
  xQueueSend(gRx, &r, 0);  // coda piena: il report si perde (64 report = ~1,5 s di stick)
}

enum class Gatt : uint8_t { Ok, NoHid, NoInput, ShortMap };

Gatt setupGatt() {
  NimBLERemoteService *hid = gClient->getService(NimBLEUUID("1812"));
  if (!hid) return Gatt::NoHid;
  size_t mapLen = 0;
  int subscribed = 0;
  for (NimBLERemoteCharacteristic *c : hid->getCharacteristics(true)) {
    if (c->getUUID() == NimBLEUUID("2a4b")) {
      mapLen = c->readValue().size();
      continue;
    }
    if (c->getUUID() != NimBLEUUID("2a4d")) continue;
    uint8_t id = 0, type = 0;
    if (NimBLERemoteDescriptor *rr = c->getDescriptor(NimBLEUUID("2908"))) {
      const NimBLEAttValue v = rr->readValue();
      if (v.size() >= 2) {
        id = v[0];
        type = v[1];
      }
    }
    if (type == 1 && id == 1 && c->canNotify() && c->subscribe(true, onNotify, true)) subscribed++;
  }
  if (NimBLERemoteService *bat = gClient->getService(NimBLEUUID("180f"))) {
    if (NimBLERemoteCharacteristic *c = bat->getCharacteristic(NimBLEUUID("2a19"))) {
      gBatHandle = c->getHandle();
      const NimBLEAttValue v = c->readValue();
      if (v.size()) gBatt = v[0];
      if (c->canNotify()) c->subscribe(true, onNotify, true);
    }
  }
  LogSerial.printf("[pad] report map %u byte, report d'ingresso id 1 iscritti: %d\n", static_cast<unsigned>(mapLen), subscribed);
  if (!subscribed) return Gatt::NoInput;
  if (mapLen < kMinReportMap) return Gatt::ShortMap;
  return Gatt::Ok;
}

// Telefono: servizio «Calamaio pad», informazioni (ver 1 + versione dell'app) e iscrizione allo stato dei tasti.
Gatt setupPhoneGatt() {
  NimBLERemoteService *svc = gClient->getService(NimBLEUUID(kPhoneSvc));
  if (!svc) return Gatt::NoHid;
  if (NimBLERemoteCharacteristic *info = svc->getCharacteristic(NimBLEUUID(kPhoneInfo))) {
    const NimBLEAttValue v = info->readValue();
    char app[16] = {0};
    const size_t n = v.size() > 1 ? (v.size() - 1 < 15 ? v.size() - 1 : 15) : 0;
    if (n) memcpy(app, v.data() + 1, n);
    LogSerial.printf("[pad] telefono: informazioni ver %u, app «%s»\n", v.size() ? v[0] : 0, app);
    if (!v.size() || v[0] != 1) return Gatt::NoInput;  // versione sconosciuta: mai interpretare i report
  }
  NimBLERemoteCharacteristic *st = svc->getCharacteristic(NimBLEUUID(kPhoneState));
  if (!st || !st->canNotify() || !st->subscribe(true, onNotify, true)) return Gatt::NoInput;
  return Gatt::Ok;
}

// Sicurezza del tipo k in ble_hs_cfg: NimBLE la legge quando costruisce la richiesta di abbinamento, quindi va
// scritta prima di connect() (una connessione sola, solo questo task si collega).
void applySecurity(pad::Kind k) {
  const pad::SecCfg c = pad::secFor(k);
  NimBLEDevice::setSecurityAuth(c.bond, c.mitm, c.sc);
  NimBLEDevice::setSecurityIOCap(c.ioCap);
  ble_hs_cfg.sm_sc_only = c.scOnly ? 1 : 0;
}

// F6: per il telefono la cifratura è asincrona e l'attesa del codice annullabile: «LUNGA ANNULLA», end() o
// kSecureMs la fermano (con secureConnection() sincrona il task resterebbe fermo fino alla fine dello SMP).
// 1 riuscita, 0 fallita, −1 annullata.
int securePhone() {
  gAuthDone = false;
  if (!gClient->secureConnection(true)) return 0;
  const uint32_t t0 = millis();
  while (!gAuthDone) {
    if (gWantCancel || gStop) return -1;
    if (!gClient->isConnected() || millis() - t0 > kSecureMs) return 0;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return 1;
}

void connectFound() {
  NimBLEAddress a;
  pad::Kind kind;
  portENTER_CRITICAL(&gMux);
  a = gFoundAddr;
  kind = gFoundKind;
  portEXIT_CRITICAL(&gMux);
  const bool phone = kind == pad::Kind::Phone;
  const bool pairing = gMachine.link() == pad::Link::Pairing && gMachine.kind() == kind;
  gKind = kind;
  applySecurity(kind);
  if (!gClient) {
    gClient = NimBLEDevice::createClient();
    gClient->setClientCallbacks(&gClientCb, false);
    gClient->setConnectTimeout(5000);
  }
  gClient->setConnectionParams(phone ? kPhoneItvMin : kItvMin, phone ? kPhoneItvMax : kItvMax, 0, 300);
  const uint32_t t0 = millis();
  if (!gClient->connect(a, true)) {
    LogSerial.printf("[pad] %s: collegamento non riuscito\n", pad::kindName(kind));
    return;
  }
  // Indirizzo d'identità (risolto dal controller, se ha l'IRK): dice se è associato davvero.
  const bool wasBonded = NimBLEDevice::isBonded(gClient->getConnInfo().getIdAddress());
  if (!wasBonded && !pairing) {  // fuori dalla finestra mai un abbinamento nuovo
    LogSerial.printf("[pad] %s non associato fuori dall'associazione: scollego\n", pad::kindName(kind));
    gClient->disconnect();
    return;
  }
  bool secured;
  if (phone) {
    const int r = securePhone();
    if (r < 0 || gWantCancel || gStop) {  // F6: annullata durante il codice (anche se il codice è arrivato dopo)
      const NimBLEAddress idA = gClient->getConnInfo().getIdAddress();
      if (!wasBonded && NimBLEDevice::isBonded(idA)) NimBLEDevice::deleteBond(idA);
      gClient->disconnect();
      LogSerial.println("[pad] telefono: associazione annullata durante il codice, niente abbinamento");
      return;  // gWantCancel lo consuma il giro del task (gMachine.cancel)
    }
    secured = r == 1;
  } else {
    secured = gClient->secureConnection();
    if (!secured && wasBonded && gClient->isConnected()) {  // 0.12.0: un secondo tentativo prima di dimenticarlo
      LogSerial.println("[pad] cifratura non riuscita: riprovo una volta");
      secured = gClient->secureConnection();
    }
  }
  NimBLEConnInfo ci = gClient->getConnInfo();
  const bool sc = phone && secured && storedSc(ci.getIdAddress());
  const bool ok = secured && pad::linkSecure(kind, ci.isEncrypted(), ci.isAuthenticated(), ci.getSecKeySize(), sc);
  if (!ok) {
    LogSerial.printf("[pad] %s: sicurezza insufficiente (cifrato %d, autenticato %d, chiave %u, SC %d)\n", pad::kindName(kind),
                     ci.isEncrypted(), ci.isAuthenticated(), static_cast<unsigned>(ci.getSecKeySize()), sc);
    pad::Addr id;
    toAddr(ci.getIdAddress(), id);
    if (phone) {  // un telefono mai con un abbinamento debole: via subito (anche quello appena salvato da NimBLE)
      if (NimBLEDevice::isBonded(ci.getIdAddress())) NimBLEDevice::deleteBond(ci.getIdAddress());
      const pad::BondBook b = bookCopy();
      if (b.has(pad::Kind::Phone) && pad::sameAddr(b.get(pad::Kind::Phone), id)) {
        dropKind(pad::Kind::Phone);
        LogSerial.println("[pad] abbinamento del telefono non più valido: serve «Associa telefono»");
      }
      if (!wasBonded) {  // F6: un codice sbagliato chiude la finestra, serve di nuovo «Associa telefono»
        gPairFail = true;
        gMachine.cancel();
      }
    } else if (wasBonded && !secured) {  // Xbox associato altrove: come la 0.13
      dropKind(pad::Kind::Xbox);
      gBondInvalid = true;
      LogSerial.println("[pad] abbinamento non più valido: cancellato, serve «Associa controller»");
    }
    gClient->disconnect();
    return;
  }
  const Gatt g = phone ? setupPhoneGatt() : setupGatt();
  if (g == Gatt::NoHid || g == Gatt::NoInput) {
    LogSerial.printf("[pad] %s: servizio mancante o sconosciuto, scollego\n", pad::kindName(kind));
    if (phone && !wasBonded) {
      gPairFail = true;
      gMachine.cancel();
    }
    gClient->disconnect();
    return;
  }
  if (g == Gatt::ShortMap && gShortMaps < kShortMapRetries) {
    gShortMaps++;
    LogSerial.println("[pad] report map corto: ricollego subito");
    gClient->disconnect();
    return;
  }
  gShortMaps = 0;
  pad::Addr id;
  toAddr(gClient->getConnInfo().getIdAddress(), id);  // dopo la cifratura: l'identità, mai l'indirizzo privato
  remember(kind, id);  // uno per tipo: il nuovo sostituisce solo il suo tipo
  gClient->updateConnParams(phone ? kPhoneItvMin : kItvMin, phone ? kPhoneItvMax : kItvMax, 0, 300);
  gMachine.connected(true, kind);
  if (phone) {  // F7
    portENTER_CRITICAL(&gMux);
    gIdle.start(millis());
    portEXIT_CRITICAL(&gMux);
  }
  publish();
  LogSerial.printf("[pad] PRONTO in %lu ms (%s, %s)\n", static_cast<unsigned long>(millis() - t0), pad::kindName(kind),
                   wasBonded ? "ricollegamento" : "nuovo");
}

void forgetNow(uint8_t mask) {
  if (mask & 0x80) {
    NimBLEDevice::deleteAllBonds();
    portENTER_CRITICAL(&gMux);
    gBook = pad::BondBook();
    portEXIT_CRITICAL(&gMux);
    saveBook(pad::BondBook());
    LogSerial.println("[pad] tutti gli abbinamenti cancellati");
    return;
  }
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone})
    if (mask & kindBit(k)) {
      dropKind(k);
      LogSerial.printf("[pad] %s dimenticato\n", pad::kindName(k));
    }
}

void padTask(void *) {
  // 0.14.0: host pronto, poi risoluzione degli indirizzi privati e riconciliazione del libro
  for (int i = 0; i < 300 && !ble_hs_synced() && !gStop; i++) vTaskDelay(pdMS_TO_TICKS(10));
  const int rc = ble_hs_pvcy_set_resolve_enabled(1);
  LogSerial.printf("[pad] risoluzione degli indirizzi privati: %s (rc %d)\n", rc == 0 ? "accesa" : "NON accesa", rc);
  reconcile();
  gMachine.setBonded(bookCopy().any());
  publish();
  while (!gStop) {
    const uint32_t now = millis();
    NimBLEScan *s = NimBLEDevice::getScan();
    if (gWantForget) {
      const uint8_t mask = gWantForget;
      gWantForget = 0;
      if (s->isScanning()) s->stop();
      const bool current = (mask & 0x80) || (mask & kindBit(gMachine.kind()));
      if (current && gClient && gClient->isConnected()) gClient->disconnect();
      forgetNow(mask);
      const bool any = bookCopy().any();
      if (!any) gMachine.forget();
      else gMachine.setBonded(true);
    }
    if (gWantPair) {
      gWantPair = false;
      if (gClient && gClient->isConnected()) gClient->disconnect();
      gMachine.pair(now, gWantPairKind);
    }
    if (gWantCancel) {
      gWantCancel = false;
      if (gMachine.link() == pad::Link::Pairing && gClient && gClient->isConnected()) gClient->disconnect();
      gMachine.cancel();
    }
    if (gLost) {
      gLost = false;
      portENTER_CRITICAL(&gMux);
      gIdle.stop();
      portEXIT_CRITICAL(&gMux);
      const bool invalid = gBondInvalid;
      gBondInvalid = false;
      gBatHandle = 0;
      gBatt = -1;
      gMachine.lost(now, invalid);
      gMachine.setBonded(bookCopy().any());
    }
    gMachine.tick(now);
    publish();
    const pad::Link l = gMachine.link();
    if (l == pad::Link::Ready) {
      // F7: iOS tiene il collegamento anche con l'app in secondo piano; la scheda lo chiude e torna a cercare.
      portENTER_CRITICAL(&gMux);
      const bool idle = gMachine.kind() == pad::Kind::Phone && gIdle.due(now);
      if (idle) gIdle.stop();
      portEXIT_CRITICAL(&gMux);
      if (idle && gClient && gClient->isConnected()) {
        LogSerial.println("[pad] telefono in secondo piano o muto: scollego e torno a cercare");
        gClient->disconnect();
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (gFound) {
      if (s->isScanning()) s->stop();
      connectFound();
      gFound = false;
      continue;
    }
    const bool want = gScanAllowed && (l == pad::Link::Pairing || l == pad::Link::Connecting);
    if (want && !s->isScanning()) s->start(kScanMs, false, true);
    else if (!want && s->isScanning()) s->stop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  NimBLEScan *s = NimBLEDevice::getScan();
  if (s->isScanning()) s->stop();
  if (gClient && gClient->isConnected()) gClient->disconnect();
  gTaskDone = true;
  vTaskDelete(nullptr);
}

}  // namespace

void begin() {
  if (gActive) return;
  // 0.12.0: cache dei duplicati del controller da 100 a 20 indirizzi (heap del controller, durante la partita)
  NimBLEDevice::setScanDuplicateCacheSize(20);
  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  applySecurity(pad::Kind::Xbox);  // 0.14.0: per tipo, riscritta prima di ogni collegamento (connectFound)
  NimBLEScan *sc = NimBLEDevice::getScan();
  sc->setScanCallbacks(&gScanCb, true);
  sc->setActiveScan(true);
  sc->setInterval(100);
  sc->setWindow(60);  // non il 100%: lascia tempo al Wi-Fi
  sc->setMaxResults(0);
  if (!gRx) gRx = xQueueCreate(64, sizeof(Rx));
  pad::BondBook b;
  bool migrated;
  loadBook(b, migrated);  // subito, per la ricerca; il task la riconcilia con NimBLE
  portENTER_CRITICAL(&gMux);
  gBook = b;
  portEXIT_CRITICAL(&gMux);
  gMachine.enable(true, NimBLEDevice::getNumBonds() > 0);
  publish();
  gStop = gTaskDone = gFound = gLost = gBondInvalid = gWantPair = gWantCancel = false;
  gPasskeyNew = gPairFail = false;
  gBatt = -1;
  gShortMaps = 0;
  gKeep.stop();
  memset(&gLast, 0, sizeof gLast);
  gConn = gLastConn + 1;  // il primo report dopo begin() si consegna sempre
  gActive = true;
  xTaskCreatePinnedToCore(padTask, "pad", 6144, nullptr, 1, &gTask, 0);
}

bool end() {
  if (!gActive) return true;
  gStop = true;
  const uint32_t t0 = millis();
  while (!gTaskDone && millis() - t0 < 3000) delay(10);
  if (!gTaskDone) {  // mai deinit né cancellazioni col task vivo: si riprova più tardi
    LogSerial.println("[pad] il task del controller non si è fermato in 3 s: radio lasciata accesa, riprovare end()");
    return false;
  }
  gTask = nullptr;
  if (gWantForget) {  // forget() subito prima di end() («Dimentica», ripristino): il task non l'ha fatto
    const uint8_t mask = gWantForget;
    gWantForget = 0;
    forgetNow(mask);
    LogSerial.println("[pad] abbinamenti cancellati prima di spegnere la radio");
  }
  NimBLEDevice::deinit(true);
  gClient = nullptr;
  gMachine.enable(false, false);
  gActive = false;
  gLinkPub = pad::Link::Off;
  gBatt = -1;
  gKeep.stop();
  if (gRx) xQueueReset(gRx);
  return true;
}

bool active() { return gActive; }

void startPairing(pad::Kind k) {
  if (!gActive) return;
  gWantPairKind = k;
  gWantPair = true;
}

void cancelPairing() {
  if (gActive) gWantCancel = true;
}

void forget(pad::Kind k) {
  if (k == pad::Kind::Phone) gPhonePub = false;
  else gXboxPub = false;
  if (gActive) {
    gWantForget = gWantForget | kindBit(k);
    return;
  }
  // Radio spenta: solo il libro (padv = 1: la 0.13 non si migra più); l'abbinamento di NimBLE lo toglie la
  // riconciliazione alla prossima accensione, e fino ad allora la ricerca non lo prende (pickAd usa il libro).
  pad::BondBook b;
  bool migrated;
  loadBook(b, migrated);
  b.clear(k);
  saveBook(b);
}

void forgetAll() {
  gXboxPub = gPhonePub = false;
  if (gActive) {
    gWantForget = 0x80;
    return;
  }
  Preferences p;
  if (p.begin(kBondNs, false)) {
    p.clear();
    p.end();
  }
  saveBook(pad::BondBook());
}

pad::Link link() { return gActive ? gLinkPub : pad::Link::Off; }
pad::Kind kind() { return gKindPub; }

bool bonded(pad::Kind k) {
  if (gActive) return k == pad::Kind::Phone ? gPhonePub : gXboxPub;
  pad::BondBook b;
  bool migrated;
  loadBook(b, migrated);
  if (b.has(k)) return true;
  return k == pad::Kind::Xbox && pad::legacyIsXbox(migrated, b, bondsInNvs());  // D2: stessa regola di migrateLegacy
}

bool bonded() { return bonded(pad::Kind::Xbox) || bonded(pad::Kind::Phone); }
int battery() { return gActive ? gBatt : -1; }
void setScan(bool on) { gScanAllowed = on; }
uint32_t polledAt() { return gLastAt; }
uint32_t tickClock(uint32_t now) { return gKeep.cap(now); }

bool takePasskey(uint32_t &key) {
  if (!gPasskeyNew) return false;
  key = gPasskey;
  gPasskeyNew = false;
  return true;
}

bool takePairFailed() {
  if (!gPairFail) return false;
  gPairFail = false;
  return true;
}

bool poll(pad::State &s) {
  if (!gRx) return false;
  if (link() != pad::Link::Ready) gKeep.stop();  // scollegato: lo stato lo azzera il loop (MenuMapper::reset)
  Rx r;
  while (xQueueReceive(gRx, &r, 0) == pdTRUE) {
    pad::State n;
    if (r.kind == static_cast<uint8_t>(pad::Kind::Phone)) {
      pad::PhoneMeta meta;
      const pad::PhoneParse p = pad::parsePhoneReport(r.d, r.len, n, meta);
      if (p != pad::PhoneParse::Ok) {
        if (r.t - gBadAt >= 1000) {
          gBadAt = r.t;
          LogSerial.printf("[pad] report del telefono scartato: %s (%u byte)\n", pad::phoneParseName(p), r.len);
        }
        continue;
      }
      gKeep.feed(r.t);
    } else {
      if (!pad::parseXboxReport(r.d, r.len, n)) continue;
      gKeep.stop();
    }
    const bool fresh = r.conn != gLastConn;  // primo report di un collegamento nuovo: sempre consegnato
    if (!fresh && pad::sameState(n, gLast) && n.src == gLast.src) continue;
    gLastConn = r.conn;
    gLast = n;
    gLastAt = r.t;
    s = n;
    return true;
  }
  // Telefono: 3 s senza report = tutto rilasciato (spec §2.1), una volta sola fino al report dopo.
  uint32_t at;
  if (pad::keepaliveRelease(gKeep, gLast, millis(), s, at)) {
    gLast = s;
    gLastAt = at;
    LogSerial.println("[pad] telefono: 3 s senza report, tutto rilasciato");
    return true;
  }
  return false;
}

}  // namespace blepad
}  // namespace cal
```

Note per chi implementa:
- `NimBLEAddress::getBase()` e `getType()`, `NimBLEClient::getConnInfo()`, `NimBLEConnInfo::getIdAddress/isEncrypted/
  isAuthenticated/getSecKeySize`, `onPassKeyDisplay` esistono in NimBLE-Arduino 2.5.1 (controllare con
  `grep -n "getBase\|getConnInfo\|onPassKeyDisplay" .pio/libdeps/heltec213/NimBLE-Arduino/src/*.h` dopo la prima
  build). Se `ble_hs_cfg` non risulta dichiarato, l'intestazione giusta è quella che `NimBLEDevice.cpp` include nello
  stesso ramo `#ifdef`.
- `begin()` usa `NimBLEDevice::getNumBonds() > 0` solo per partire in «Connecting»: il task, dopo la riconciliazione,
  rimette `setBonded(libro non vuoto)`.
- Nessuna allocazione per report: `Rx` resta in una coda fissa da 64 voci; il campo `kind` sta nel riempimento (30 → 31
  byte, la voce resta di 32): nessun byte in più.
- F5: `ble_store_key_sec`/`ble_store_value_sec` e `ble_store_read_peer_sec` vengono da `host/ble_store.h`, che
  `ble_hs.h` include; `NimBLEAddress::getBase()` dà il `ble_addr_t` della chiave. Provato col pre-flight: compila.
- F6: `onAuthenticationComplete` arriva solo con la cifratura riuscita (`BLE_GAP_EVENT_ENC_CHANGE` stato 0); un codice
  sbagliato si vede dallo scollegamento di iOS o dai 35 s. L'Xbox resta sincrono come la 0.13.
- F7: `gIdle` lo scrive `onNotify` (task host di NimBLE) e lo legge il task del controller: sempre sotto `gMux`.

- [ ] **Step 5: Compilare**

Run: `cd firmware && pio run -e heltec213 2>&1 | grep -E "error|warning: unused|RAM:|Flash:|SUCCESS|FAILED" | head -20`
Expected: errori solo in `main.cpp` (chiamate vecchie a `blepad::forget()`, `padLinkChanged`): si correggono nel Task 11.
Nessun errore né warning in `hw/blepad.cpp` (con `bit()` al posto di `kindBit()` non compilerebbe: F3).

- [ ] **Step 6: Commit** (insieme al Task 11 se la build non passa ancora: allora qui niente commit e si va avanti; `core/pad_phone` e la sua prova si possono committare subito)

```bash
git add firmware/src/core/pad_phone.h firmware/src/core/pad_phone.cpp firmware/test/test_pad_phone firmware/src/hw/blepad.h firmware/src/hw/blepad.cpp
git commit -m "Controller dal telefono: hw/blepad con due tipi (servizio Calamaio pad, sicurezza per tipo prima di ogni collegamento, solo SC dall'archivio delle chiavi, passkey annullabile, libro padx/padp, risoluzione degli indirizzi privati, mantenimento, telefono in secondo piano scollegato)"
```

---

### Task 11: Collegamenti nel loop, firmware 0.14.0, memoria e documenti

**Files:**
- Modify: `firmware/src/main.cpp` (`padLoad`, `padHandle`, `padLoop`, `onPerm`, Ripristino di fabbrica)
- Modify: `firmware/src/core/model.h` (`CALAMAIO_FW_VERSION`), `firmware/test/test_net/test_main.cpp`
- Modify: `docs/DECISIONI.md`, `docs/PROTOCOLLO.md`

**Interfaces:**
- Consumes: tutto il Task 10 (`blepad::kind/startPairing/cancelPairing/forget/forgetAll/bonded/takePasskey/takePairFailed/tickClock`), `pad::State::src` (Task 2, F4), `Ui::padEvent(…, src)`, `Ui::padLinkChanged(l, kind, xb, ph, batt, now)`, `Ui::padPasskey`, `Ui::padPairFailed`, `PadReq::PairPhone/ForgetPhone/CancelPair`, `PermEvent::src`, `NetRequest::src`, `phoneForbidden` (Task 5, 6). I Task 7–9 (avatar a pezzi) sono già dentro: questa è la build che diventa 0.14.0.
- Produces: firmware 0.14.0 che compila; righe del registro `[memoria] telefono collegato: …`, `[memoria] telefono collegato (ogni minuto): …`, `[memoria] telefono scollegato: …`, `[memoria] controller collegato: …`.

- [ ] **Step 1: Versione (test prima)**

In `test/test_net/test_main.cpp` la riga 279 diventa
`TEST_ASSERT_EQUAL_STRING("0.14.0", kFwVersion);  // 0.14.0: il telefono come controller (app iPhone, P3) e avatar a pezzi`.
Run: `cd firmware && pio test -e native -f test_net 2>&1 | tail -n 5` → Expected: FAIL `Expected '0.14.0' Was '0.13.0-ed'`.
Poi in `core/model.h`: `#define CALAMAIO_FW_VERSION "0.14.0"`. Rilanciare → PASS.

- [ ] **Step 2: `main.cpp`**

`onPerm`, prima di `if (e.outcome == PermOutcome::Cancelled …`:

```cpp
  // §7, scelta A: terza barriera. Un «consenti» dal telefono non diventa mai perm-reply (Ui non lo emette già).
  if (e.outcome == PermOutcome::Allow && e.src == InputSrc::Phone) {
    LogSerial.println("[permesso] CONSENTI dal telefono scartato (§7)");
    return;
  }
```

e nella riga del registro aggiungere la sorgente: `… · richiesta %s · da %s\n` con
`e.src == InputSrc::Phone ? "telefono" : (e.src == InputSrc::Xbox ? "controller" : "tasto")`.

Variabili del controller vicino a `padBondNow`:

```cpp
static pad::Kind padPairKind = pad::Kind::Xbox;  // 0.14.0: tipo dell'associazione chiesta
static bool padXboxNow = false, padPhoneNow = false;  // associati per tipo (a radio spenta letti dalla NVS)
static pad::Kind padKindSeen = pad::Kind::Xbox;
static uint32_t padPhoneHeapAt = 0;
```

`padLoad`: dopo `m.sys.padHw = true;`

```cpp
  padXboxNow = m.sys.padBonded = blepad::bonded(pad::Kind::Xbox);
  padPhoneNow = m.sys.padPhoneBonded = blepad::bonded(pad::Kind::Phone);
  padBondNow = padXboxNow || padPhoneNow;
  LogSerial.printf("[pad] Bluetooth %s, controller %s, telefono %s\n", padSaved ? "acceso" : "spento",
                   padXboxNow ? "associato" : "no", padPhoneNow ? "associato" : "no");
```

(togliendo la riga `padBondNow = m.sys.padBonded = blepad::bonded();` e il `printf` vecchio).

R1 e F11, terza barriera: `onNetRequest`, in testa (prima di `wantProtect`/`wantFactory`):

```cpp
  // R1 e F11, terza barriera: dal telefono niente eFuse, Ripristino, abbinamento col PC né Wi-Fi (Ui non li emette già).
  if (r.src == InputSrc::Phone && phoneForbidden(r.cmd)) {
    LogSerial.printf("[sicurezza] %s dal telefono scartato (R1)\n", netCmdKey(r.cmd));
    return;
  }
```

`padHandle` riceve la sorgente dello stato che ha prodotto gli eventi (F4: con `blepad::kind()` un report del telefono
ancora in coda quando la scheda è già passata all'Xbox diventerebbe un tenuto «Xbox» e il primo report dell'Xbox
potrebbe farlo scattare come consenso). Sopra `padHandle`:

```cpp
// F4: sorgente dello stato che ha prodotto gli eventi (State::src), mai il tipo del collegamento di adesso: un report
// del telefono ancora in coda quando la scheda è già passata all'Xbox resta del telefono. Per tick e reset vale la
// sorgente dell'ultimo stato consegnato.
static pad::Kind padSrc = pad::Kind::Xbox;
```

e la firma diventa `static void padHandle(const pad::MenuEvent *ev, int n, Ms now, pad::Kind src)`, con
`gUi.padEvent(ev[i], now, wallAt(now))` → `gUi.padEvent(ev[i], now, wallAt(now), src)`.

`padLoop`, lo `switch (gUi.takePadReq())` (Pair/Forget) si sostituisce per intero con:

```cpp
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
```

`if (blepad::active()) padBondNow = blepad::bonded();` diventa

```cpp
  if (blepad::active()) {
    padXboxNow = blepad::bonded(pad::Kind::Xbox);
    padPhoneNow = blepad::bonded(pad::Kind::Phone);
    padBondNow = padXboxNow || padPhoneNow;
  }
```

`blepad::startPairing();` → `blepad::startPairing(padPairKind);` e la riga del registro dice
`padPairKind == pad::Kind::Phone ? "[pad] associazione: 2 minuti per l'app Calamaio (scheda Controller)" : "[pad] associazione: 2 minuti per il tasto di associazione del controller"`.

Il blocco del cambio di collegamento diventa:

```cpp
  const pad::Link l = blepad::link();
  const pad::Kind kd = blepad::kind();
  const int batt = blepad::battery();
  if (l != padSeen || kd != padKindSeen || batt != padBattSeen || padXboxNow != m.sys.padBonded ||
      padPhoneNow != m.sys.padPhoneBonded) {
    if (padSeen == pad::Link::Ready && l != pad::Link::Ready) {  // scollegato: una tenuta di A si annulla
      pad::MenuEvent ev[4];
      padHandle(ev, padMenu.reset(ev, 4), now, padSrc);
      padSearchFrom = ms;
      heapLine(padKindSeen == pad::Kind::Phone ? "telefono scollegato" : "controller scollegato");
    }
    if (l == pad::Link::Ready && padSeen != pad::Link::Ready) {
      heapLine(kd == pad::Kind::Phone ? "telefono collegato" : "controller collegato");
      padPhoneHeapAt = ms;
    }
    if (l != padSeen) LogSerial.printf("[pad] %s -> %s (%s)\n", pad::linkKey(padSeen), pad::linkKey(l), pad::kindName(kd));
    padSeen = l;
    padKindSeen = kd;
    padBattSeen = batt;
    pending = mergeRefresh(pending, gUi.padLinkChanged(l, kd, padXboxNow, padPhoneNow, batt, now));
  }
  if (l == pad::Link::Ready && kd == pad::Kind::Phone && ms - padPhoneHeapAt >= 60000) {
    padPhoneHeapAt = ms;
    heapLine("telefono collegato (ogni minuto)");  // misura della spec §6: heap e blocco massimo col telefono
  }
  uint32_t key;
  if (blepad::takePasskey(key)) pending = mergeRefresh(pending, gUi.padPasskey(key, now));
  if (blepad::takePairFailed()) pending = mergeRefresh(pending, gUi.padPairFailed(now));
```

e in fondo le due righe del `poll` e del `tick` diventano

```cpp
  while (blepad::poll(s)) {
    padSrc = s.src;
    padHandle(ev, padMenu.update(s, blepad::polledAt(), ev, 8), now, s.src);
  }
  padHandle(ev, padMenu.tick(blepad::tickClock(millis()), ev, 8), now, padSrc);
```

(prima: `while (blepad::poll(s)) padHandle(ev, padMenu.update(s, blepad::polledAt(), ev, 8), now);` e
`padHandle(ev, padMenu.tick(millis(), ev, 8), now);`).
Il blocco della partita (`if (gUi.gbPlaying())`) resta prima del `poll`, com'è: in gioco il codice del telefono non
può comparire (l'associazione si apre solo dal menu, che in partita è sospeso).

Ripristino di fabbrica: `blepad::forget();` → `blepad::forgetAll();` e `padBondNow = padPairWanted = false;` →
`padBondNow = padXboxNow = padPhoneNow = padPairWanted = false;`.

- [ ] **Step 3: Build, prove native, misura**

Run:
```bash
cd firmware && pio test -e native 2>&1 | tail -n 5 && pio run -e heltec213 2>&1 | grep -E "RAM:|Flash:|SUCCESS|FAILED" \
 && ls -l .pio/build/heltec213/firmware.bin | awk '{print $5}' \
 && xtensa-esp32s3-elf-size -A .pio/build/heltec213/firmware.elf 2>/dev/null | grep -E "dram0.bss|dram0.data" \
 || ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-size -A .pio/build/heltec213/firmware.elf | grep -E "dram0.bss|dram0.data"
```
Base (F10): la stessa misura sulla base del ramo, costruita nello **stesso ambiente** in una copia temporanea (il
numero di DECISIONI, 168 936, e l'ELF di `CalamaiOS-editor`, 168 976, vengono da build diverse):

```bash
BASE=$(git rev-parse 5831fa1) && TMP=$(mktemp -d) && git worktree add --detach $TMP $BASE >/dev/null \
 && cd $TMP/firmware && pio run -e heltec213 >/dev/null 2>&1 \
 && ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-size -A .pio/build/heltec213/firmware.elf \
    | awk '/dram0.bss|dram0.data/ {s += $2} END {print "base", s}' \
 && cd - >/dev/null && git worktree remove --force $TMP
```

Expected: tutte le prove native `PASSED`; `SUCCESS`; immagine sotto 2 359 296 byte (nel prototipo del pre-flight e di
R4: 1 965 648); `.dram0.bss` + `.dram0.data` al più **base + 320** (prototipo: 169 192 su una base di 168 976, +216:
+72 il controller, +136 avatar a pezzi e chiusure a raffica dei Task 7–9, +8 F5–F7). Annotare base, totale e immagine.
Se la RAM statica supera il tetto: trovare i simboli nuovi con
`nm -S --size-sort .pio/build/heltec213/firmware.elf | grep -iE "blepad|pad::|Keep|Book|gAv|Storm|gIdle" | tail`
e giustificarli in DECISIONI, o ridurli, prima di andare avanti.

- [ ] **Step 4: DECISIONI**

In `docs/DECISIONI.md`, sezione «App iPhone», aggiungere (con i numeri misurati allo Step 3):

```markdown
- **Fase P3, firmware 0.14.0** (spec §2 e §6, piano `docs/superpowers/plans/2026-10-16-app-iphone-P3-controller.md`): secondo tipo di controller «telefono» in `hw/blepad`; parte pura in `core/pad_phone` (report v1, mantenimento) e `core/pad_bonds` (libro degli abbinamenti, filtro di ricerca, sicurezza per tipo). Vettori del report in `protocol/vectors/pad-report-v1.json` (Node), letti da `test_pad_phone` e da `PadReportTests`.
- **Sicurezza per tipo**: NimBLE legge `ble_hs_cfg` (IO, MITM, SC, `sm_sc_only`) quando costruisce la richiesta di abbinamento: la scheda la riscrive prima di ogni `connect()` (una connessione sola). Telefono: DisplayOnly, MITM, solo Secure Connections, accettato solo cifrato + autenticato + chiave da 16 byte + SC; Xbox identico alla 0.13.0-ed. Da iniziatore NimBLE non applica `sm_sc_only` (lo guarda solo da responder, `ble_sm.c`): «solo SC» lo controlla la scheda leggendo `ble_store_read_peer_sec(...).sc` dopo la cifratura (pre-flight F5); un legacy con passkey verrebbe altrimenti accettato.
- **Codice del telefono annullabile (F6)**: per il telefono la cifratura è asincrona (`secureConnection(true)` e `onAuthenticationComplete`), con un'attesa di al più 35 s che «LUNGA ANNULLA» e `end()` fermano; dopo un annullamento l'abbinamento appena salvato da NimBLE si cancella (anche se il codice è arrivato dopo); un codice sbagliato chiude la finestra (serve di nuovo «Associa telefono»); il codice a schermo tiene la scheda Attiva e il tenuto lì non mette Riposo (F9).
- **Telefono in secondo piano (F7)**: iOS non chiude il collegamento quando l'app va in secondo piano; la scheda scollega il telefono 5 s dopo un report con `flags` bit0 = 0 senza report di primo piano, o dopo 10 s senza report validi (`pad::PhoneIdle`), e torna a cercare: così l'Xbox si prende. In partita la pausa dice «Telefono scollegato» o «Controller scollegato» secondo il tipo (testo nuovo, non nel simulatore).
- **Tasti bloccati, casi lasciati così (F12)**: `CBPadPeripheral.send` dice `true` anche senza servizio (innocuo); una tenuta di A col telefono sparito dopo almeno un report di mantenimento arriva al HoldFire entro «ultimo report + 3 s» (è il contratto dei 3 s; dal telefono vale solo per Riposo e le azioni ammesse). Il codice (casuale, `esp_random`) è a schermo e **mai nel registro**; la scheda non conferma né inserisce codici col telefono.
- **Abbinamenti**: libro in `calamaio-gb` `padx`/`padp` (7 byte: indirizzo d'identità + tipo d'indirizzo 0/1; il tipo di controller è la chiave), `padv` = 1 dopo la migrazione. Il libro è la sola verità: gli abbinamenti di NimBLE che non conosce si cancellano (all'avvio del task e dopo ogni associazione). Migrazione: alla prima accensione l'unico abbinamento della 0.13 è l'Xbox. «Dimentica» a radio spenta scrive solo il libro; NimBLE si pulisce alla prossima accensione e intanto la ricerca non prende quel dispositivo. Il confronto degli indirizzi ignora il bit «identità» (tipi 2/3 risolti dal controller). Dopo la cifratura si usa l'indirizzo d'identità, mai quello della ricerca (la 0.13 avrebbe cancellato l'abbinamento appena fatto di un iPhone).
- **Indirizzi privati dell'iPhone**: NimBLE non accende la risoluzione nel controller con un indirizzo proprio pubblico; la scheda chiama `ble_hs_pvcy_set_resolve_enabled(1)` dopo la sincronizzazione (riga `[pad] risoluzione degli indirizzi privati: accesa`). Verificato dal vivo: prova L4 (esito da annotare).
- **Associazione per tipo**: durante la finestra di un tipo l'associato dell'altro tipo non si prende (chiuderebbe la finestra); il Bluetooth (`set.padOn`) resta acceso finché c'è un associato di un tipo o dell'altro. Un telefono con l'abbinamento non più valido si cancella e serve «Associa telefono» (niente associazione riaperta da sola, diversa dall'Xbox).
- **Report v1**: levette limitate a ±100; `rsv` e bit sconosciuti di `flags` ignorati; `flags` bit0 = 0 (app in secondo piano) = tutto rilasciato; `seq` solo per il registro. Mantenimento: 3 s senza report = stato vuoto consegnato una volta con l'istante «ultimo report + 3 s − 1 ms», e l'orologio della tenuta (`blepad::tickClock`) non va oltre: un telefono sparito con A giù dà HoldUp, mai HoldFire. Collegamento del telefono a 15–30 ms (regole di Apple per le periferiche), Xbox invariato.
- **Regola §7, scelta A**: in `Ui::confirmPerm` (dove nasce `perm-reply`), ricontrollata in `Ui::emitPerm` e in `onPerm` (`PermEvent::src`). Il tenuto iniziato dal telefono resta del telefono anche se lo scatto arriva dal tick (`UiState::holdPhone`, salvata attorno a `keyDown`/`keyUp`: F2); gli eventi portano la sorgente dello stato che li ha prodotti (`State::src`), mai il tipo del collegamento di adesso (F4). Dal telefono restano menu, gioco e Riposo; **Proteggi le chiavi, Ripristino e la conferma dell'abbinamento col PC no** (R1, deviazione dalla spec §2.3, più restrittiva: due sono irreversibili, l'abbinamento della scheda la spec §0 lo esclude dal telefono), con tre barriere come per CONSENTI (`Ui::confirmAction`/`Ui::pairConfirm` → `Ui::emitNet` → `onNetRequest`). Allo stesso modo (F11, spec §0 «niente Wi-Fi né abbinamento della scheda dal telefono») «Dimentica abbinamento», «Dimentica tutte le reti», Reti salvate › Dimentica, «Configura da telefono» e una rete di Cerca reti: si vedono e si armano, non partono (`phoneForbidden`: `protect-keys`, `factory-reset`, `pair-confirm`, `unpair`, `forget`, `forget-net`, `provision`, `join`). Riavvia e Cerca reti restano ammessi. Per tutte quelle voci servono il PRG o l'Xbox. Prova: 4 096 sequenze di eventi del telefono dal Permesso e da ognuna delle otto conferme, senza un «consenti» né un comando vietato.
- **Testi nuovi** (non nel simulatore; Menu › Gioco › Controller esiste solo sulla scheda vera, `sys.padHw`, quindi `reference/` resta com'è e golden e catture non cambiano): voci «Associa telefono», «Dimentica telefono»; valore «telefono collegato»; toast «Metti l'app Calamaio sulla scheda Controller», «Telefono collegato», «Telefono scollegato», «Telefono dimenticato», «Nessun telefono associato», «Associazione non riuscita», «Per consentire usa il tasto PRG o il controller», «Per confermare usa il tasto PRG o il controller» (R1, F11); schermata `padcode` (bloccata per i temi): «TELEFONO», «Codice per il telefono», «Scrivilo sull'iPhone quando lo chiede», «LUNGA ANNULLA»; aiuti di Gioco › Controller riscritti (uno alla volta, il nuovo sostituisce solo il suo tipo, «Dal telefono non si confermano CONSENTI, Proteggi, Ripristino, abbinamento col PC e Wi-Fi.», il Bluetooth si spegne solo senza associati); pausa della partita «Telefono scollegato» con «Riapri l'app Calamaio sulla scheda Controller: si ricollega da solo. Poi Riprendi.» (F7).
- **Memoria (0.14.0)**: RAM statica <N> byte (+<d> rispetto alla base <B> misurata nello stesso ambiente, F10; DECISIONI diceva 168 936, l'ELF di `CalamaiOS-editor` 168 976; tetto base + 320: +72 il controller, +136 l'avatar a pezzi e le chiusure a raffica, +8 F5–F7 nel prototipo), immagine <M> byte. Heap: righe `[memoria] telefono collegato / scollegato / (ogni minuto)` e `[memoria] prima/dopo l'avatar a pezzi`, `[memoria] WebSocket chiuso`; misure dal vivo in L10 e L13.
```

e nella voce «Idea futura: controller sullo smartphone con QR» (sezione 2026-10-12) la frase «il telefono comanda solo
il gioco, mai i permessi di Claude né i menu» diventa «il telefono comanda gioco e menu; mai il consenso a un permesso,
né Proteggi, Ripristino, abbinamento col PC e Wi-Fi (app iPhone, P3)».

- [ ] **Step 5: PROTOCOLLO**

In `docs/PROTOCOLLO.md`, in fondo, una sezione «Controller dal telefono (Bluetooth LE)»: la tabella GATT della spec
§2.1 (tre righe con UUID, proprietà e contenuto), il formato del report v1 byte per byte, le regole di scarto
(lunghezza ≠ 8, versione ≠ 1), mantenimento (1 s dall'app, 3 s alla scheda), sicurezza (scheda centrale DisplayOnly,
LE Secure Connections con passkey, collegamento solo cifrato e autenticato) e il rimando ai vettori
`protocol/vectors/pad-report-v1.json` (`node protocol/vectors/gen-pad-vectors.mjs --check`).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/main.cpp firmware/src/core/model.h firmware/test/test_net docs/DECISIONI.md docs/PROTOCOLLO.md firmware/src/hw/blepad.h firmware/src/hw/blepad.cpp
git commit -m "Firmware 0.14.0: il telefono come controller nel loop (codice a schermo, toast, sorgente degli eventi dallo stato, terza barriera §7 e R1/F11, righe di memoria), DECISIONI e PROTOCOLLO"
```

---
### Task 12: CalamaioKit: report v1 e servizio GATT

**Files:**
- Create: `ios/CalamaioKit/Sources/CalamaioKit/PadReport.swift`
- Test: `ios/CalamaioKit/Tests/CalamaioKitTests/PadReportTests.swift`, `ios/CalamaioKit/Tests/CalamaioKitTests/PadReportDecode.swift` (D1: la decodifica serve solo alle prove)

**Interfaces:**
- Consumes: `protocol/vectors/pad-report-v1.json` (Task 1).
- Produces:
  - `public struct PadButtons: OptionSet, Hashable, Sendable` (`rawValue: UInt16`; `.a .b .view .menu .guide .up .down .left .right`, `.dpad`)
  - `public struct PadReport: Equatable, Sendable { seq: UInt8; buttons: PadButtons; lx: Int8; ly: Int8; foreground: Bool; init(seq:buttons:lx:ly:foreground:); func encoded() -> Data }`
  - Solo nel target delle prove (D1 del pre-flight: l'app non decodifica report): `extension PadReport { static func decode(_:) -> PadReport? }` in `PadReportDecode.swift`, usata da `PadReportTests` e `PadControllerTests` (Task 13).
  - `public enum PadGATT { static let service, state, info: String; static let localName = "Calamaio"; static func infoValue(appVersion: String) -> Data }`

- [ ] **Step 1: Test che falliscono**

```swift
import Foundation
import XCTest
@testable import CalamaioKit

// protocol/vectors/pad-report-v1.json (Node, gen-pad-vectors.mjs): gli stessi che legge la scheda (test_pad_phone).
private struct PadVectors: Decodable {
    struct Case: Decodable {
        let name: String
        let hex: String
        let valid: Bool
        let encode: Bool?
        let seq: Int?
        let buttons: Int?
        let lx: Int?
        let ly: Int?
        let fg: Bool?
    }
    let format: String
    let length: Int
    let bits: [String: Int]
    let cases: [Case]

    static func load() throws -> PadVectors {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() }  // …/ios/CalamaioKit/Tests/CalamaioKitTests/ → radice
        let data = try Data(contentsOf: url.appendingPathComponent("protocol/vectors/pad-report-v1.json"))
        return try JSONDecoder().decode(PadVectors.self, from: data)
    }
}

private func hex(_ d: Data) -> String { d.map { String(format: "%02x", $0) }.joined() }
private func bytes(_ s: String) -> Data {
    var d = Data()
    var i = s.startIndex
    while i < s.endIndex, let j = s.index(i, offsetBy: 2, limitedBy: s.endIndex) {
        d.append(UInt8(s[i..<j], radix: 16)!)
        i = j
    }
    return d
}

final class PadReportTests: XCTestCase {
    func testVectorsLoaded() throws {
        let v = try PadVectors.load()
        XCTAssertEqual(v.format, "calamaio-pad-report")
        XCTAssertEqual(v.length, PadReport.length)
        XCTAssertEqual(v.cases.count, 20)
    }

    func testBitsMatchFirmware() throws {
        let b = try PadVectors.load().bits
        XCTAssertEqual(PadButtons.a.rawValue, 1 << b["A"]!)
        XCTAssertEqual(PadButtons.b.rawValue, 1 << b["B"]!)
        XCTAssertEqual(PadButtons.view.rawValue, 1 << b["View"]!)
        XCTAssertEqual(PadButtons.menu.rawValue, 1 << b["Menu"]!)
        XCTAssertEqual(PadButtons.guide.rawValue, 1 << b["Guide"]!)
        XCTAssertEqual(PadButtons.up.rawValue, 1 << b["Up"]!)
        XCTAssertEqual(PadButtons.down.rawValue, 1 << b["Down"]!)
        XCTAssertEqual(PadButtons.left.rawValue, 1 << b["Left"]!)
        XCTAssertEqual(PadButtons.right.rawValue, 1 << b["Right"]!)
    }

    func testEncodeMatchesVectors() throws {
        var n = 0
        for c in try PadVectors.load().cases where c.valid && c.encode == true {
            let r = PadReport(seq: UInt8(c.seq!), buttons: PadButtons(rawValue: UInt16(c.buttons!)),
                              lx: Int8(c.lx!), ly: Int8(c.ly!), foreground: c.fg!)
            XCTAssertEqual(hex(r.encoded()), c.hex, c.name)
            n += 1
        }
        XCTAssertEqual(n, 11)
    }

    func testDecodeLikeTheBoard() throws {
        for c in try PadVectors.load().cases {
            let r = PadReport.decode(bytes(c.hex))
            if !c.valid { XCTAssertNil(r, c.name); continue }
            XCTAssertEqual(r?.seq, UInt8(c.seq!), c.name)
            XCTAssertEqual(r?.buttons.rawValue, UInt16(c.buttons!), c.name)
            XCTAssertEqual(r.map { Int($0.lx) }, c.lx, c.name)
            XCTAssertEqual(r.map { Int($0.ly) }, c.ly, c.name)
            XCTAssertEqual(r?.foreground, c.fg, c.name)
        }
    }

    func testInfoValue() {
        XCTAssertEqual(PadGATT.infoValue(appVersion: "0.2.0"), Data([1]) + Data("0.2.0".utf8))
        let long = PadGATT.infoValue(appVersion: "123456789.123456789")
        XCTAssertEqual(long.count, 16)  // ver + 15 byte
        XCTAssertEqual(long.first, 1)
    }

    func testUUIDs() {
        XCTAssertEqual(PadGATT.service, "526389E4-B789-4225-990D-0465AFB610B2")
        XCTAssertEqual(PadGATT.state, "6A34D990-D046-4834-96B8-0CBDCE4087FA")
        XCTAssertEqual(PadGATT.info, "9D00F77E-BB83-4667-A37F-6FBDB0F0F324")
    }
}
```

- [ ] **Step 2: Vederli fallire**

Run: `cd ios/CalamaioKit && swift test --filter PadReportTests 2>&1 | tail -n 15`
Expected: errori di compilazione (`PadReport`, `PadButtons`, `PadGATT` sconosciuti).

- [ ] **Step 3: Scrivere `PadReport.swift`**

```swift
import Foundation

/// Tasti del report v1: stessi bit di pad::State del firmware (firmware/src/core/pad.h).
public struct PadButtons: OptionSet, Hashable, Sendable {
    public let rawValue: UInt16
    public init(rawValue: UInt16) { self.rawValue = rawValue }
    public static let a = PadButtons(rawValue: 1 << 0)
    public static let b = PadButtons(rawValue: 1 << 1)
    public static let view = PadButtons(rawValue: 1 << 6)   // tasto SELECT dell'app
    public static let menu = PadButtons(rawValue: 1 << 7)   // tasto START: apre il Menu della scheda
    public static let guide = PadButtons(rawValue: 1 << 8)  // tasto GUIDA: pausa del Game Boy
    public static let up = PadButtons(rawValue: 1 << 12)
    public static let down = PadButtons(rawValue: 1 << 13)
    public static let left = PadButtons(rawValue: 1 << 14)
    public static let right = PadButtons(rawValue: 1 << 15)
    public static let dpad: PadButtons = [.up, .down, .left, .right]
}

/// Report v1 (spec app iPhone §2.1): ver, seq, buttons u16 LE, lx, ly, flags (bit0 primo piano), rsv.
public struct PadReport: Equatable, Sendable {
    public static let version: UInt8 = 1
    public static let length = 8

    public var seq: UInt8
    public var buttons: PadButtons
    public var lx: Int8
    public var ly: Int8
    public var foreground: Bool

    public init(seq: UInt8, buttons: PadButtons, lx: Int8 = 0, ly: Int8 = 0, foreground: Bool = true) {
        self.seq = seq
        self.buttons = buttons
        self.lx = lx
        self.ly = ly
        self.foreground = foreground
    }

    public func encoded() -> Data {
        Data([Self.version, seq, UInt8(buttons.rawValue & 0xff), UInt8(buttons.rawValue >> 8),
              UInt8(bitPattern: lx), UInt8(bitPattern: ly), foreground ? 1 : 0, 0])
    }
}

/// Servizio «Calamaio pad» (spec §2.1). Stringhe: CoreBluetooth sta solo nell'app (CBUUID(string:)).
public enum PadGATT {
    public static let service = "526389E4-B789-4225-990D-0465AFB610B2"
    public static let state = "6A34D990-D046-4834-96B8-0CBDCE4087FA"
    public static let info = "9D00F77E-BB83-4667-A37F-6FBDB0F0F324"
    public static let localName = "Calamaio"

    /// ver = 1, poi la versione dell'app in UTF-8, al massimo 15 byte (si tronca su un carattere intero).
    public static func infoValue(appVersion: String) -> Data {
        var v = Data()
        for ch in appVersion {
            let u = Data(String(ch).utf8)
            if v.count + u.count > 15 { break }
            v.append(u)
        }
        return Data([1]) + v
    }
}
```

E la decodifica, solo per le prove, in `Tests/CalamaioKitTests/PadReportDecode.swift` (D1):

```swift
import Foundation
@testable import CalamaioKit

// D1: la decodifica del report serve solo alle prove (vettori, PadController): l'app non la usa, resta fuori dal
// pacchetto. Come la scheda (core/pad_phone): nil con lunghezza o versione sbagliata, levette a ±100, secondo
// piano = vuoto.
extension PadReport {
    static func decode(_ d: Data) -> PadReport? {
        guard d.count == length else { return nil }
        let b = [UInt8](d)
        guard b[0] == version else { return nil }
        let fg = b[6] & 1 == 1
        func clamp(_ v: UInt8) -> Int8 { Int8(max(-100, min(100, Int(Int8(bitPattern: v))))) }
        return PadReport(seq: b[1], buttons: fg ? PadButtons(rawValue: UInt16(b[2]) | UInt16(b[3]) << 8) : [],
                         lx: fg ? clamp(b[4]) : 0, ly: fg ? clamp(b[5]) : 0, foreground: fg)
    }
}
```

- [ ] **Step 4: Vederli passare**

Run: `cd ios/CalamaioKit && swift test --filter PadReportTests 2>&1 | tail -n 8`
Expected: `Executed 6 tests, with 0 failures`.

- [ ] **Step 5: Commit**

```bash
git add ios/CalamaioKit/Sources/CalamaioKit/PadReport.swift ios/CalamaioKit/Tests/CalamaioKitTests/PadReportTests.swift ios/CalamaioKit/Tests/CalamaioKitTests/PadReportDecode.swift
git commit -m "App iPhone: report v1 e servizio Calamaio pad in CalamaioKit, contro i vettori condivisi con la scheda"
```

---

### Task 13: CalamaioKit: PadController, disposizione dei tasti e tocchi multipli

**Files:**
- Create: `ios/CalamaioKit/Sources/CalamaioKit/PadController.swift`, `ios/CalamaioKit/Sources/CalamaioKit/PadLayout.swift`
- Modify: `ios/CalamaioKit/Sources/CalamaioKit/Texts.swift`
- Test: `ios/CalamaioKit/Tests/CalamaioKitTests/PadControllerTests.swift`, `ios/CalamaioKit/Tests/CalamaioKitTests/PadLayoutTests.swift`

**Interfaces:**
- Consumes: `PadButtons`, `PadReport` (Task 12).
- Produces:
  - `public enum PadPeripheralEvent: Equatable, Sendable { case poweredOn, poweredOff, unauthorized, unsupported, subscribed, unsubscribed, readyToSend }`
  - `@MainActor public protocol PadPeripheral: AnyObject { var onEvent: ((PadPeripheralEvent) -> Void)? { get set }; func startAdvertising(); func stopAdvertising(); func send(_ report: Data) -> Bool }`
  - `@MainActor public final class PadController: ObservableObject` con `enum Status { bluetoothOff, unauthorized, unsupported, searching, otherController, connected, paused }`, `@Published status`, `@Published buttons`, `var haptics: Bool`, `var onHaptic: (() -> Void)?`, `var lastReport: Data`, `init(peripheral:now:)`, `setButtons(_:)`, `becameActive()`, `willResignActive()`, `tick()`; costanti `keepalive = 1`, `hintAfter = 10`.
  - `public struct PadLayout` (`enum Key { dpad, a, b, select, start, guide }`, `frames: [Key: CGRect]`, `portrait`, `init(size:)`, `buttons(at:) -> PadButtons`) e `public struct TouchTracker` (`moved(_:to:in:)`, `ended(_:)`, `cancelAll()`, `buttons`).
  - `Texts.padStatus(_:) -> String` e le costanti dei testi della scheda Controller.

- [ ] **Step 1: Test che falliscono (`PadControllerTests.swift`)**

```swift
import Foundation
import XCTest
@testable import CalamaioKit

@MainActor
private final class FakePeripheral: PadPeripheral {
    var onEvent: ((PadPeripheralEvent) -> Void)?
    var advertising = false
    var starts = 0, stops = 0
    var sent: [Data] = []
    var accept = true
    func startAdvertising() { advertising = true; starts += 1 }
    func stopAdvertising() { advertising = false; stops += 1 }
    func send(_ report: Data) -> Bool {
        guard accept else { return false }
        sent.append(report)
        return true
    }
    func fire(_ e: PadPeripheralEvent) { onEvent?(e) }
    var last: PadReport? { sent.last.flatMap(PadReport.decode) }
}

@MainActor
final class PadControllerTests: XCTestCase {
    private var t: TimeInterval = 100
    private var p: FakePeripheral!
    private var c: PadController!

    override func setUp() async throws {
        t = 100
        p = FakePeripheral()
        c = PadController(peripheral: p, now: { [unowned self] in self.t })
    }

    private func connected() {
        p.fire(.poweredOn)
        c.becameActive()
        p.fire(.subscribed)
    }

    func testAdvertisesOnlyActivePoweredAndUnsubscribed() {
        c.becameActive()
        XCTAssertFalse(p.advertising)  // Bluetooth non ancora acceso
        p.fire(.poweredOn)
        XCTAssertTrue(p.advertising)
        p.fire(.subscribed)
        XCTAssertFalse(p.advertising)  // collegati: niente annuncio
        p.fire(.unsubscribed)
        XCTAssertTrue(p.advertising)
        c.willResignActive()
        XCTAssertFalse(p.advertising)
    }

    func testSubscribeSendsCurrentState() {
        connected()
        XCTAssertEqual(p.sent.count, 1)
        XCTAssertEqual(p.last?.buttons, [])
        XCTAssertEqual(p.last?.foreground, true)
        XCTAssertEqual(c.status, .connected)
    }

    func testChangeSendsReportWithGrowingSeq() {
        connected()
        c.setButtons(.a)
        c.setButtons([.a, .b])  // multi-tocco
        c.setButtons([.a, .b])  // uguale: niente
        XCTAssertEqual(p.sent.count, 3)
        XCTAssertEqual(p.last?.buttons, [.a, .b])
        let seqs = p.sent.compactMap(PadReport.decode).map(\.seq)
        XCTAssertEqual(seqs, [seqs[0], seqs[0] &+ 1, seqs[0] &+ 2])
    }

    func testSeqWraps() {
        connected()
        for i in 0..<300 { c.setButtons(i % 2 == 0 ? .a : []) }
        let seqs = p.sent.compactMap(PadReport.decode).map(\.seq)
        for (a, b) in zip(seqs, seqs.dropFirst()) { XCTAssertEqual(b, a &+ 1) }
    }

    func testKeepaliveEverySecond() {
        connected()
        c.setButtons(.up)
        let n = p.sent.count
        t = 100.9; c.tick()  // assegnati, non sommati: niente arrotondamenti dei Double
        XCTAssertEqual(p.sent.count, n)
        t = 101; c.tick()
        XCTAssertEqual(p.sent.count, n + 1)
        XCTAssertEqual(p.last?.buttons, .up)  // il mantenimento ripete lo stato
        t = 102; c.tick()
        XCTAssertEqual(p.sent.count, n + 2)
    }

    // Review Focus «tasti bloccati»: prima del secondo piano un «tutto rilasciato» (flags bit0 = 0), poi silenzio.
    func testResignSendsReleasedThenStops() {
        connected()
        c.setButtons([.a, .left])
        c.willResignActive()
        XCTAssertEqual(p.last?.buttons, [])
        XCTAssertEqual(p.last?.foreground, false)
        XCTAssertEqual(c.status, .paused)
        let n = p.sent.count
        c.setButtons(.b)  // tocchi a app non attiva: ignorati
        t += 5; c.tick()  // niente mantenimento in secondo piano
        XCTAssertEqual(p.sent.count, n)
        XCTAssertEqual(c.buttons, [])
    }

    // Coda piena: al «pronto» parte lo stato di adesso, mai quello vecchio rimasto indietro.
    func testFullQueueResendsLatest() {
        connected()
        p.accept = false
        c.setButtons(.a)
        c.setButtons([])  // rilasciato mentre la coda era piena
        p.accept = true
        p.fire(.readyToSend)
        XCTAssertEqual(p.last?.buttons, [])
        p.fire(.readyToSend)  // niente in sospeso: niente
        XCTAssertEqual(p.sent.count, 2)  // quello dell'iscrizione e il «rilasciato»
    }

    func testHintAfterTenSeconds() {
        p.fire(.poweredOn)
        c.becameActive()
        XCTAssertEqual(c.status, .searching)
        t = 109.9; c.tick()
        XCTAssertEqual(c.status, .searching)
        t = 110; c.tick()
        XCTAssertEqual(c.status, .otherController)
        p.fire(.subscribed)
        XCTAssertEqual(c.status, .connected)
        p.fire(.unsubscribed)
        XCTAssertEqual(c.status, .searching)  // il conto riparte da 110
        t = 120; c.tick()
        XCTAssertEqual(c.status, .otherController)
    }

    func testPowerStates() {
        c.becameActive()
        p.fire(.unauthorized)
        XCTAssertEqual(c.status, .unauthorized)
        p.fire(.unsupported)
        XCTAssertEqual(c.status, .unsupported)
        p.fire(.poweredOn)
        p.fire(.subscribed)
        c.setButtons(.a)
        p.fire(.poweredOff)
        XCTAssertEqual(c.status, .bluetoothOff)
        XCTAssertEqual(c.buttons, [])
    }

    func testHapticOnlyOnNewPressWhenEnabled() {
        connected()
        var n = 0
        c.onHaptic = { n += 1 }
        c.setButtons(.a)
        c.setButtons([.a, .b])
        c.setButtons(.b)  // rilascio di A: niente
        XCTAssertEqual(n, 2)
        c.haptics = false
        c.setButtons([.b, .up])
        XCTAssertEqual(n, 2)
    }

    func testLastReportForReads() {
        connected()
        c.setButtons(.menu)
        XCTAssertEqual(PadReport.decode(c.lastReport)?.buttons, .menu)
    }
}
```

- [ ] **Step 2: Test che falliscono (`PadLayoutTests.swift`)**

```swift
import CoreGraphics
import XCTest
@testable import CalamaioKit

final class PadLayoutTests: XCTestCase {
    private let portrait = PadLayout(size: CGSize(width: 393, height: 700))
    private let landscape = PadLayout(size: CGSize(width: 852, height: 360))

    private func center(_ l: PadLayout, _ k: PadLayout.Key) -> CGPoint {
        let f = l.frames[k]!
        return CGPoint(x: f.midX, y: f.midY)
    }

    func testFramesInsideAndApart() {
        for l in [portrait, landscape] {
            let bounds = CGRect(origin: .zero, size: l.size)
            XCTAssertEqual(l.frames.count, PadLayout.Key.allCases.count)
            for (k, f) in l.frames { XCTAssertTrue(bounds.contains(f), "\(k) fuori schermo") }
            let keys = PadLayout.Key.allCases
            for i in keys.indices { for j in keys.indices where j > i {
                XCTAssertFalse(l.frames[keys[i]]!.intersects(l.frames[keys[j]]!), "\(keys[i]) tocca \(keys[j])")
            } }
        }
        XCTAssertTrue(portrait.portrait)
        XCTAssertFalse(landscape.portrait)
    }

    func testKeysMapToFirmwareBits() {
        XCTAssertEqual(portrait.buttons(at: center(portrait, .a)), .a)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .b)), .b)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .select)), .view)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .start)), .menu)
        XCTAssertEqual(portrait.buttons(at: center(portrait, .guide)), .guide)
        XCTAssertEqual(portrait.buttons(at: CGPoint(x: 5, y: 5)), [])  // zona dello stato
    }

    func testDpadSectorsAndDiagonals() {
        for l in [portrait, landscape] {
            let f = l.frames[.dpad]!
            let c = CGPoint(x: f.midX, y: f.midY), r = f.width / 2
            func at(_ dx: CGFloat, _ dy: CGFloat) -> PadButtons { l.buttons(at: CGPoint(x: c.x + dx * r, y: c.y + dy * r)) }
            XCTAssertEqual(at(0.8, 0), .right)
            XCTAssertEqual(at(-0.8, 0), .left)
            XCTAssertEqual(at(0, -0.8), .up)    // y dello schermo verso il basso
            XCTAssertEqual(at(0, 0.8), .down)
            XCTAssertEqual(at(-0.6, -0.6), [.up, .left])
            XCTAssertEqual(at(0.6, 0.6), [.down, .right])
            XCTAssertEqual(at(0.05, 0.05), [])  // centro morto
        }
    }

    func testTrackerTwoFingers() {
        var tr = TouchTracker()
        tr.moved(1, to: center(portrait, .a), in: portrait)
        tr.moved(2, to: center(portrait, .b), in: portrait)
        XCTAssertEqual(tr.buttons, [.a, .b])
        let f = portrait.frames[.dpad]!
        tr.moved(3, to: CGPoint(x: f.midX - f.width * 0.3, y: f.midY - f.width * 0.3), in: portrait)
        XCTAssertEqual(tr.buttons, [.a, .b, .up, .left])
        tr.ended(1)
        XCTAssertEqual(tr.buttons, [.b, .up, .left])
        tr.cancelAll()
        XCTAssertEqual(tr.buttons, [])
    }

    func testTrackerSlideFromAToB() {
        var tr = TouchTracker()
        tr.moved(7, to: center(portrait, .a), in: portrait)
        tr.moved(7, to: center(portrait, .b), in: portrait)
        XCTAssertEqual(tr.buttons, .b)
    }
}
```

- [ ] **Step 3: Vederli fallire**

Run: `cd ios/CalamaioKit && swift test --filter 'PadControllerTests|PadLayoutTests' 2>&1 | tail -n 15`
Expected: errori di compilazione (`PadController`, `PadPeripheral`, `PadLayout`, `TouchTracker` sconosciuti).

- [ ] **Step 4: Scrivere `PadController.swift`**

```swift
import Combine  // ObservableObject e @Published (F12a: senza, compila solo per l'import di AppController.swift)
import Foundation

public enum PadPeripheralEvent: Equatable, Sendable {
    case poweredOn, poweredOff, unauthorized, unsupported
    case subscribed, unsubscribed, readyToSend
}

/// Cucitura verso CoreBluetooth (CBPeripheralManager nell'app). Il simulatore non ha Bluetooth: i test usano un finto.
@MainActor public protocol PadPeripheral: AnyObject {
    var onEvent: ((PadPeripheralEvent) -> Void)? { get set }
    func startAdvertising()
    func stopAdvertising()
    /// false = coda di CoreBluetooth piena: si rimanda a .readyToSend lo stato di quel momento.
    func send(_ report: Data) -> Bool
}

/// Il telefono come controller (spec app iPhone §2): un report a ogni cambiamento e uno ogni 1 s, solo in primo
/// piano; prima del secondo piano «tutto rilasciato»; avviso dopo 10 s senza la scheda.
@MainActor public final class PadController: ObservableObject {
    public enum Status: Equatable, Sendable {
        case bluetoothOff, unauthorized, unsupported, searching, otherController, connected, paused
    }
    public static let keepalive: TimeInterval = 1
    public static let hintAfter: TimeInterval = 10

    @Published public private(set) var status: Status = .bluetoothOff
    @Published public private(set) var buttons: PadButtons = []
    public var haptics = true
    public var onHaptic: (() -> Void)?
    /// Ultimo report mandato: lo legge l'adattatore per le letture della caratteristica.
    public private(set) var lastReport = PadReport(seq: 0, buttons: []).encoded()

    private let peripheral: PadPeripheral
    private let now: () -> TimeInterval
    private var power: PadPeripheralEvent = .poweredOff
    private var active = false, subscribed = false, advertising = false, pending = false
    private var seq: UInt8 = 0
    private var lastSent: TimeInterval = -.infinity
    private var searchingSince: TimeInterval = 0

    public init(peripheral: PadPeripheral, now: @escaping () -> TimeInterval = { ProcessInfo.processInfo.systemUptime }) {
        self.peripheral = peripheral
        self.now = now
        peripheral.onEvent = { [weak self] e in self?.handle(e) }
    }

    /// Tasti premuti adesso (tutte le dita insieme). Vibrazione solo per un tasto nuovo.
    public func setButtons(_ b: PadButtons) {
        guard active, b != buttons else { return }
        let added = b.subtracting(buttons)
        buttons = b
        if haptics && !added.isEmpty { onHaptic?() }
        if subscribed { send(foreground: true) }
    }

    public func becameActive() {
        active = true
        updateAdvertising()
        refresh()
    }

    public func willResignActive() {
        buttons = []
        if subscribed { send(foreground: false) }  // «tutto rilasciato» prima che iOS fermi tutto
        active = false
        updateAdvertising()
        refresh()
    }

    /// Dal timer dell'app (ogni 0,25 s): mantenimento e avviso.
    public func tick() {
        if active && subscribed && now() - lastSent >= Self.keepalive { send(foreground: true) }
        refresh()
    }

    private func handle(_ e: PadPeripheralEvent) {
        switch e {
        case .poweredOn:
            power = .poweredOn
        case .poweredOff, .unauthorized, .unsupported:
            power = e
            subscribed = false
            advertising = false
            pending = false
            buttons = []
        case .subscribed:
            subscribed = true
            if active { send(foreground: true) }  // lo stato attuale subito
        case .unsubscribed:
            subscribed = false
            buttons = []
        case .readyToSend:
            if pending && subscribed { send(foreground: active) }
        }
        updateAdvertising()
        refresh()
    }

    private func updateAdvertising() {
        let want = power == .poweredOn && active && !subscribed
        if want && !advertising {
            peripheral.startAdvertising()
            advertising = true
            searchingSince = now()
        } else if !want && advertising {
            peripheral.stopAdvertising()
            advertising = false
        }
    }

    private func send(foreground: Bool) {
        seq &+= 1
        let data = PadReport(seq: seq, buttons: foreground ? buttons : [], foreground: foreground).encoded()
        lastReport = data
        if peripheral.send(data) {
            pending = false
            lastSent = now()
        } else {
            pending = true  // a .readyToSend parte lo stato di allora, non questo
        }
    }

    private func refresh() {
        let s: Status
        switch power {
        case .unauthorized: s = .unauthorized
        case .unsupported: s = .unsupported
        case .poweredOn:
            if !active { s = .paused }
            else if subscribed { s = .connected }
            else if now() - searchingSince >= Self.hintAfter { s = .otherController }
            else { s = .searching }
        default: s = .bluetoothOff
        }
        if s != status { status = s }
    }
}
```

- [ ] **Step 5: Scrivere `PadLayout.swift`**

```swift
import CoreGraphics

/// Pulsantiera (spec §2.4): verticale con croce a sinistra, A/B a destra, Select/Start sotto e Guida; orizzontale con
/// croce e A/B ai lati. Le stesse cornici servono al disegno e ai tocchi (PadTouchView).
public struct PadLayout: Equatable, Sendable {
    public enum Key: CaseIterable, Sendable { case dpad, a, b, select, start, guide }

    public let size: CGSize
    public let frames: [Key: CGRect]
    public var portrait: Bool { size.height >= size.width }

    public init(size: CGSize) {
        self.size = size
        let w = size.width, h = size.height, u = min(w, h)
        func round(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) -> CGRect { CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r) }
        func pill(_ x: CGFloat, _ y: CGFloat, _ pw: CGFloat, _ ph: CGFloat) -> CGRect {
            CGRect(x: x - pw / 2, y: y - ph / 2, width: pw, height: ph)
        }
        if h >= w {
            frames = [.dpad: round(0.25 * w, 0.55 * h, 0.22 * u), .a: round(0.82 * w, 0.50 * h, 0.11 * u),
                      .b: round(0.60 * w, 0.64 * h, 0.11 * u), .select: pill(0.36 * w, 0.80 * h, 0.24 * u, 0.10 * u),
                      .start: pill(0.64 * w, 0.80 * h, 0.24 * u, 0.10 * u), .guide: pill(0.5 * w, 0.91 * h, 0.24 * u, 0.09 * u)]
        } else {
            frames = [.dpad: round(0.17 * w, 0.58 * h, 0.30 * u), .a: round(0.88 * w, 0.48 * h, 0.13 * u),
                      .b: round(0.72 * w, 0.66 * h, 0.13 * u), .select: pill(0.40 * w, 0.86 * h, 0.20 * u, 0.12 * u),
                      .start: pill(0.60 * w, 0.86 * h, 0.20 * u, 0.12 * u), .guide: pill(0.5 * w, 0.62 * h, 0.20 * u, 0.12 * u)]
        }
    }

    /// Tasti sotto un dito: al più un tasto, o due direzioni della croce in diagonale. Bersagli un po' più larghi
    /// del disegno; fra due tasti vince il più vicino.
    public func buttons(at p: CGPoint) -> PadButtons {
        if let d = frames[.dpad] {
            let dx = p.x - d.midX, dy = p.y - d.midY, r = d.width / 2
            let dist = (dx * dx + dy * dy).squareRoot()
            if dist <= r * 1.2 {
                if dist < r * 0.2 { return [] }  // centro morto
                let deg = atan2(-dy, dx) * 180 / .pi  // 0 = destra, 90 = su
                let sector = Int((deg + 360 + 22.5).truncatingRemainder(dividingBy: 360) / 45)
                let map: [PadButtons] = [.right, [.up, .right], .up, [.up, .left], .left, [.down, .left], .down, [.down, .right]]
                return map[sector]
            }
        }
        var best: (Key, CGFloat)?
        for k in [Key.a, .b, .select, .start, .guide] {
            guard let f = frames[k], f.insetBy(dx: -f.width * 0.15, dy: -f.height * 0.15).contains(p) else { continue }
            let d = hypot(p.x - f.midX, p.y - f.midY)
            if best == nil || d < best!.1 { best = (k, d) }
        }
        switch best?.0 {
        case .a: return .a
        case .b: return .b
        case .select: return .view
        case .start: return .menu
        case .guide: return .guide
        default: return []
        }
    }
}

/// Dita sullo schermo → tasti premuti (OR di tutte le dita). Un dito che scivola cambia tasto.
public struct TouchTracker: Sendable {
    private var touches: [Int: PadButtons] = [:]
    public init() {}
    public var buttons: PadButtons { touches.values.reduce(into: []) { $0.formUnion($1) } }
    public mutating func moved(_ id: Int, to p: CGPoint, in layout: PadLayout) { touches[id] = layout.buttons(at: p) }
    public mutating func ended(_ id: Int) { touches[id] = nil }
    public mutating func cancelAll() { touches.removeAll() }
}
```

- [ ] **Step 6: Testi (`Texts.swift`)**

Dentro `enum Texts`:

```swift
    // Scheda Controller (P3)
    public static let padTab = "Controller"
    public static let padConnected = "Collegato alla scheda"
    public static let padSearching = "Cerco la scheda…"
    public static let padOther = "La scheda usa un altro controller: spegnilo per usare il telefono"
    public static let padBluetoothOff = "Bluetooth spento: accendilo dal Centro di Controllo."
    public static let padUnauthorized = "Bluetooth non permesso: consentilo in Impostazioni › Calamaio."
    public static let padUnsupported = "Bluetooth non disponibile su questo dispositivo."
    public static let padPaused = "In pausa"
    public static let padFirstTime = "La prima volta: sulla scheda Menu › Gioco › Controller › «Associa telefono», poi scrivi qui il codice che mostra."
    public static let padHaptics = "Vibrazione al tocco"
    public static let padBluetooth = "Bluetooth"

    public static func padStatus(_ s: PadController.Status) -> String {
        switch s {
        case .connected: return padConnected
        case .searching: return padSearching
        case .otherController: return padOther
        case .bluetoothOff: return padBluetoothOff
        case .unauthorized: return padUnauthorized
        case .unsupported: return padUnsupported
        case .paused: return padPaused
        }
    }
```

(`Texts` è un `enum` senza isolamento: `padStatus` prende solo il valore di `Status`, che è `Sendable`.)

- [ ] **Step 7: Vederli passare, con tutto il pacchetto**

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 8`
Expected: `PadControllerTests` (11) e `PadLayoutTests` (5) verdi; nessuna prova vecchia rotta.

- [ ] **Step 8: Commit**

```bash
git add ios/CalamaioKit/Sources/CalamaioKit/PadController.swift ios/CalamaioKit/Sources/CalamaioKit/PadLayout.swift ios/CalamaioKit/Sources/CalamaioKit/Texts.swift ios/CalamaioKit/Tests/CalamaioKitTests/PadControllerTests.swift ios/CalamaioKit/Tests/CalamaioKitTests/PadLayoutTests.swift
git commit -m "App iPhone: PadController dietro una cucitura Bluetooth (mantenimento 1 s, rilasciato prima del secondo piano, coda piena, avviso dopo 10 s), pulsantiera e tocchi multipli"
```

---

### Task 14: App: scheda «Controller», CoreBluetooth, vibrazione e impostazioni

**Files:**
- Create: `ios/App/CBPadPeripheral.swift`, `ios/App/PadTouchView.swift`, `ios/App/ControllerTab.swift`
- Create: `ios/AppUITests/ControllerTabUITests.swift`
- Modify: `ios/App/CalamaioApp.swift`, `ios/App/RootView.swift`, `ios/App/SettingsTab.swift`
- Modify: `docs/DECISIONI.md` (testi dell'app, scelte della scheda Controller)

**Interfaces:**
- Consumes: `PadController`, `PadPeripheral`, `PadPeripheralEvent`, `PadLayout`, `TouchTracker`, `PadGATT`, `Texts.pad*` (Task 12, 13).
- Produces: scheda «Controller» (`systemImage: "gamecontroller"`) fra «Scheda» e «Impostazioni»; preferenza `padHaptics` (UserDefaults, di serie vera); identificatori di accessibilità `pad-status`, `pad-a`, `pad-b`, `pad-select`, `pad-start`, `pad-guide`, `pad-dpad`.

- [ ] **Step 1: Prova UI che fallisce**

```swift
import XCTest

// Scheda Controller nel simulatore (niente Bluetooth): i tasti ci sono e lo stato dice perché non si collega.
final class ControllerTabUITests: XCTestCase {
    func testControllerTabShowsPadAndStatus() {
        let app = XCUIApplication()
        app.launchArguments = ["--reset-for-tests"]
        app.launch()
        app.tabBars.buttons["Controller"].tap()
        for id in ["pad-a", "pad-b", "pad-select", "pad-start", "pad-guide", "pad-dpad"] {
            XCTAssertTrue(app.otherElements[id].waitForExistence(timeout: 5) || app.staticTexts[id].exists, id)
        }
        let status = app.staticTexts["pad-status"]
        XCTAssertTrue(status.waitForExistence(timeout: 5))
        let known = ["Bluetooth non disponibile su questo dispositivo.", "Bluetooth spento: accendilo dal Centro di Controllo.",
                     "Bluetooth non permesso: consentilo in Impostazioni › Calamaio.", "Cerco la scheda…"]
        XCTAssertTrue(known.contains(status.label), "stato inatteso: \(status.label)")
        app.tabBars.buttons["Impostazioni"].tap()
        XCTAssertTrue(app.switches["Vibrazione al tocco"].waitForExistence(timeout: 5))
    }
}
```

Run: `cd ios && xcodegen generate >/dev/null && xcodebuild test -project Calamaio.xcodeproj -scheme Calamaio -destination 'platform=iOS Simulator,name=iPhone 18 Pro,OS=27.0' -only-testing:CalamaioUITests/ControllerTabUITests 2>&1 | tail -n 15`
Expected: FAIL (nessuna scheda «Controller»).

- [ ] **Step 2: `CBPadPeripheral.swift`**

```swift
import CoreBluetooth
import CalamaioKit

/// Adattatore CoreBluetooth del controller: periferica col servizio «Calamaio pad» (spec §2.1). Le due
/// caratteristiche si leggono solo cifrate; le letture rispondono con l'ultimo report e con le informazioni.
/// CoreBluetooth non dice se chi si iscrive ha cifrato: la scheda cifra sempre prima di iscriversi.
@MainActor
final class CBPadPeripheral: NSObject, PadPeripheral, CBPeripheralManagerDelegate {
    var onEvent: ((PadPeripheralEvent) -> Void)?
    var currentReport: () -> Data = { Data() }

    private let info: Data
    private var manager: CBPeripheralManager!
    private var state: CBMutableCharacteristic?
    private var added = false, wantAdvertising = false
    private var centrals = Set<UUID>()

    init(appVersion: String) {
        info = PadGATT.infoValue(appVersion: appVersion)
        super.init()
        manager = CBPeripheralManager(delegate: self, queue: .main)
    }

    func startAdvertising() {
        wantAdvertising = true
        if added && !manager.isAdvertising {
            manager.startAdvertising([CBAdvertisementDataServiceUUIDsKey: [CBUUID(string: PadGATT.service)],
                                      CBAdvertisementDataLocalNameKey: PadGATT.localName])
        }
    }

    func stopAdvertising() {
        wantAdvertising = false
        if manager.isAdvertising { manager.stopAdvertising() }
    }

    func send(_ report: Data) -> Bool {
        guard let state else { return true }  // servizio non ancora pronto: niente da mandare
        return manager.updateValue(report, for: state, onSubscribedCentrals: nil)
    }

    nonisolated func peripheralManagerDidUpdateState(_ p: CBPeripheralManager) {
        MainActor.assumeIsolated {
            switch p.state {
            case .poweredOn:
                addService()
                onEvent?(.poweredOn)
            case .unauthorized: onEvent?(.unauthorized)
            case .unsupported: onEvent?(.unsupported)
            default:
                added = false
                state = nil
                centrals.removeAll()
                onEvent?(.poweredOff)
            }
        }
    }

    private func addService() {
        guard !added else { return }
        let st = CBMutableCharacteristic(type: CBUUID(string: PadGATT.state), properties: [.notify, .read],
                                         value: nil, permissions: [.readEncryptionRequired])
        let inf = CBMutableCharacteristic(type: CBUUID(string: PadGATT.info), properties: [.read],
                                          value: nil, permissions: [.readEncryptionRequired])
        let svc = CBMutableService(type: CBUUID(string: PadGATT.service), primary: true)
        svc.characteristics = [st, inf]
        state = st
        manager.removeAllServices()
        manager.add(svc)
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        MainActor.assumeIsolated {
            added = error == nil
            if added && wantAdvertising { startAdvertising() }
        }
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        MainActor.assumeIsolated {
            let value: Data
            switch request.characteristic.uuid {
            case CBUUID(string: PadGATT.state): value = currentReport()
            case CBUUID(string: PadGATT.info): value = info
            default: p.respond(to: request, withResult: .attributeNotFound); return
            }
            guard request.offset <= value.count else { p.respond(to: request, withResult: .invalidOffset); return }
            request.value = value.subdata(in: request.offset..<value.count)
            p.respond(to: request, withResult: .success)
        }
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, central: CBCentral, didSubscribeTo c: CBCharacteristic) {
        MainActor.assumeIsolated {
            centrals.insert(central.identifier)
            onEvent?(.subscribed)
        }
    }

    nonisolated func peripheralManager(_ p: CBPeripheralManager, central: CBCentral, didUnsubscribeFrom c: CBCharacteristic) {
        MainActor.assumeIsolated {
            centrals.remove(central.identifier)
            if centrals.isEmpty { onEvent?(.unsubscribed) }
        }
    }

    nonisolated func peripheralManagerIsReady(toUpdateSubscribers p: CBPeripheralManager) {
        MainActor.assumeIsolated { onEvent?(.readyToSend) }
    }
}
```

- [ ] **Step 3: `PadTouchView.swift`**

```swift
import SwiftUI
import UIKit
import CalamaioKit

/// Superficie dei tocchi: più dita insieme (A+B, croce in diagonale), ogni dito seguito finché si stacca.
struct PadTouchSurface: UIViewRepresentable {
    let layout: PadLayout
    let onChange: (PadButtons) -> Void

    func makeUIView(context: Context) -> PadTouchView {
        let v = PadTouchView()
        v.isMultipleTouchEnabled = true
        v.backgroundColor = .clear
        return v
    }

    func updateUIView(_ v: PadTouchView, context: Context) {
        v.layout = layout
        v.onChange = onChange
    }
}

final class PadTouchView: UIView {
    var layout = PadLayout(size: .zero)
    var onChange: (PadButtons) -> Void = { _ in }
    private var tracker = TouchTracker()

    private func id(_ t: UITouch) -> Int { ObjectIdentifier(t).hashValue }
    private func update(_ touches: Set<UITouch>) {
        for t in touches { tracker.moved(id(t), to: t.location(in: self), in: layout) }
        onChange(tracker.buttons)
    }
    override func touchesBegan(_ t: Set<UITouch>, with e: UIEvent?) { update(t) }
    override func touchesMoved(_ t: Set<UITouch>, with e: UIEvent?) { update(t) }
    override func touchesEnded(_ t: Set<UITouch>, with e: UIEvent?) {
        for x in t { tracker.ended(id(x)) }
        onChange(tracker.buttons)
    }
    override func touchesCancelled(_ t: Set<UITouch>, with e: UIEvent?) {
        tracker.cancelAll()  // chiamata, Centro di Controllo: tutto rilasciato
        onChange(tracker.buttons)
    }
}
```

- [ ] **Step 4: `ControllerTab.swift`**

```swift
import SwiftUI
import CalamaioKit

/// Scheda «Controller» (spec §2.4): in alto lo stato del collegamento, sotto la pulsantiera.
struct ControllerTab: View {
    @EnvironmentObject var pad: PadController

    var body: some View {
        GeometryReader { geo in
            let layout = PadLayout(size: geo.size)
            ZStack(alignment: .top) {
                ForEach(PadLayout.Key.allCases, id: \.self) { key in
                    keyView(key, layout: layout)
                }
                VStack(spacing: 6) {
                    Text(Texts.padStatus(pad.status))
                        .font(.headline)
                        .multilineTextAlignment(.center)
                        .accessibilityIdentifier("pad-status")
                    if pad.status != .connected {
                        Text(Texts.padFirstTime).font(.footnote).foregroundStyle(.secondary).multilineTextAlignment(.center)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.top, layout.portrait ? 24 : 8)
                .allowsHitTesting(false)
                PadTouchSurface(layout: layout) { pad.setButtons($0) }
            }
        }
        .ignoresSafeArea(.keyboard)
    }

    @ViewBuilder private func keyView(_ key: PadLayout.Key, layout: PadLayout) -> some View {
        let f = layout.frames[key] ?? .zero
        let on = isOn(key)
        Group {
            switch key {
            case .dpad: DpadShape(pressed: pad.buttons.intersection(.dpad))
            case .a, .b:
                Circle().fill(on ? Color.primary : Color.secondary.opacity(0.25))
                    .overlay(Text(key == .a ? "A" : "B").font(.title.bold()).foregroundStyle(on ? Color(.systemBackground) : .primary))
            case .select, .start, .guide:
                Capsule().fill(on ? Color.primary : Color.secondary.opacity(0.25))
                    .overlay(Text(label(key)).font(.caption.bold()).foregroundStyle(on ? Color(.systemBackground) : .primary))
            }
        }
        .frame(width: f.width, height: f.height)
        .position(x: f.midX, y: f.midY)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label(key))
        .accessibilityIdentifier("pad-\(id(key))")
        .allowsHitTesting(false)
    }

    private func isOn(_ k: PadLayout.Key) -> Bool {
        switch k {
        case .a: return pad.buttons.contains(.a)
        case .b: return pad.buttons.contains(.b)
        case .select: return pad.buttons.contains(.view)
        case .start: return pad.buttons.contains(.menu)
        case .guide: return pad.buttons.contains(.guide)
        case .dpad: return !pad.buttons.intersection(.dpad).isEmpty
        }
    }

    private func label(_ k: PadLayout.Key) -> String {
        switch k {
        case .a: return "A"
        case .b: return "B"
        case .select: return "SELECT"
        case .start: return "START"
        case .guide: return "GUIDA"
        case .dpad: return "Croce"
        }
    }

    private func id(_ k: PadLayout.Key) -> String {
        switch k {
        case .a: return "a"
        case .b: return "b"
        case .select: return "select"
        case .start: return "start"
        case .guide: return "guide"
        case .dpad: return "dpad"
        }
    }
}

/// Croce disegnata: le quattro braccia, piene quelle premute.
private struct DpadShape: View {
    let pressed: PadButtons
    var body: some View {
        GeometryReader { g in
            let s = min(g.size.width, g.size.height), k = s / 3
            ZStack {
                arm(.up, x: s / 2, y: k / 2, w: k, h: k)
                arm(.down, x: s / 2, y: s - k / 2, w: k, h: k)
                arm(.left, x: k / 2, y: s / 2, w: k, h: k)
                arm(.right, x: s - k / 2, y: s / 2, w: k, h: k)
                Rectangle().fill(Color.secondary.opacity(0.25)).frame(width: k, height: k).position(x: s / 2, y: s / 2)
            }
        }
    }
    private func arm(_ b: PadButtons, x: CGFloat, y: CGFloat, w: CGFloat, h: CGFloat) -> some View {
        RoundedRectangle(cornerRadius: 6).fill(pressed.contains(b) ? Color.primary : Color.secondary.opacity(0.25))
            .frame(width: w, height: h).position(x: x, y: y)
    }
}
```


- [ ] **Step 5: `CalamaioApp.swift`, `RootView.swift`, `SettingsTab.swift`**

`CalamaioApp`: accanto a `model`

```swift
    @StateObject private var pad = CalamaioApp.makePad()

    @MainActor static func makePad() -> PadController {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.0.0"
        let ble = CBPadPeripheral(appVersion: version)
        let pad = PadController(peripheral: ble)
        ble.currentReport = { [unowned pad] in pad.lastReport }
        pad.haptics = UserDefaults.standard.object(forKey: "padHaptics") as? Bool ?? true
        let haptic = UIImpactFeedbackGenerator(style: .light)
        haptic.prepare()
        pad.onHaptic = { haptic.impactOccurred() }
        return pad
    }
```

e in `WindowGroup`: `RootView().environmentObject(model).environmentObject(pad)` più
`.onReceive(Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()) { _ in pad.tick() }`
(`import UIKit` in cima per il generatore della vibrazione).

`RootView`: `@EnvironmentObject var pad: PadController`; nel `TabView`, fra «Scheda» e «Impostazioni»:
`Tab(Texts.padTab, systemImage: "gamecontroller") { ControllerTab() }`; `onChange(of: scenePhase)` diventa

```swift
        .onChange(of: scenePhase) { _, phase in
            if phase == .active {
                model.becameActive()
                pad.becameActive()
            } else {
                pad.willResignActive()  // solo in primo piano (spec §2.1): prima «tutto rilasciato»
            }
        }
```

e `.onAppear { if scenePhase == .active { pad.becameActive() } }` sul `TabView` (al primo avvio `onChange` non scatta).

`SettingsTab`: `@EnvironmentObject var pad: PadController`, `@AppStorage("padHaptics") private var haptics = true`, e
una sezione prima di «App»:

```swift
                Section(Texts.padTab) {
                    Toggle(Texts.padHaptics, isOn: $haptics)
                        .onChange(of: haptics) { _, on in pad.haptics = on }
                    LabeledContent(Texts.padBluetooth, value: Texts.padStatus(pad.status))
                }
```

- [ ] **Step 6: Build e prove**

Run:
```bash
cd ios && xcodegen generate >/dev/null && xcodebuild test -project Calamaio.xcodeproj -scheme Calamaio \
  -destination 'platform=iOS Simulator,name=iPhone 18 Pro,OS=27.0' -only-testing:CalamaioUITests/ControllerTabUITests \
  -only-testing:CalamaioTests 2>&1 | tail -n 15 && cd CalamaioKit && swift test 2>&1 | tail -n 4
```
Expected: `** TEST SUCCEEDED **`, `swift test` verde. Se `ControllerTabUITests` non trova i tasti come `otherElements`,
cercarli con `app.descendants(matching: .any)[id]` (la prova controlla che ci siano, non il tipo).

- [ ] **Step 7: DECISIONI (app)**

In `docs/DECISIONI.md`, sezione «App iPhone»:
- nella riga «Schede in P2» aggiungere: «P3: scheda «Controller» fra «Scheda» e «Impostazioni».»;
- nuova voce **Scheda Controller (P3)**: periferica `CBPeripheralManager` col servizio «Calamaio pad»; annuncio solo
  con l'app attiva, Bluetooth acceso e nessuna scheda iscritta (si ferma da collegati, riparte allo scollegamento);
  report a ogni cambiamento e ogni 1 s; prima del secondo piano (anche `inactive`: Centro di Controllo, chiamata) un
  report vuoto con flags bit0 = 0; coda piena = al «pronto» lo stato di allora, mai quello vecchio; avviso
  «La scheda usa un altro controller…» dopo 10 s di annuncio senza iscrizione (anche prima della prima associazione:
  sotto lo stato c'è sempre la riga «La prima volta…»); tasti SELECT → View, START → Menu, GUIDA → Guide (pausa del Game
  Boy), A, B, croce con diagonali; tocchi in una `UIView` multi-touch con le stesse cornici del disegno
  (`PadLayout`); vibrazione leggera solo per un tasto nuovo (`padHaptics`, di serie sì); CoreBluetooth non dice se
  l'iscrizione è cifrata: le letture lo sono (`readEncryptionRequired`) e la scheda cifra sempre prima di iscriversi;
  la levetta non c'è (lx, ly = 0);
- in «Testi nuovi dell'app» aggiungere: scheda «Controller»; stati «Collegato alla scheda», «Cerco la scheda…», «La
  scheda usa un altro controller: spegnilo per usare il telefono», «Bluetooth spento: accendilo dal Centro di
  Controllo.», «Bluetooth non permesso: consentilo in Impostazioni › Calamaio.», «Bluetooth non disponibile su questo
  dispositivo.», «In pausa»; riga «La prima volta: sulla scheda Menu › Gioco › Controller › «Associa telefono», poi
  scrivi qui il codice che mostra.»; tasti «A», «B», «SELECT», «START», «GUIDA»; Impostazioni: sezione «Controller»,
  «Vibrazione al tocco», «Bluetooth».

- [ ] **Step 8: Commit**

```bash
git add ios/App ios/AppUITests/ControllerTabUITests.swift docs/DECISIONI.md
git commit -m "App iPhone: scheda Controller (CoreBluetooth, pulsantiera multi-tocco, vibrazione, solo in primo piano), preferenza in Impostazioni, testi in DECISIONI"
```

---

## Prove dal vivo (con l'utente, scheda vera e iPhone)

Copione per il coordinatore, **dopo** i Task 0–14 tutti verdi. Il flash è già autorizzato dall'utente; i **backup no:
si fanno sempre prima**. Avvisare l'utente prima di ogni passo col cavo. Ogni prova: precondizioni, passi, atteso, se
fallisce. I numeri vanno in `docs/DECISIONI.md`, voci «Fase P3, firmware 0.14.0» e «Memoria (0.14.0)».

**Situazione di partenza** (da ricontrollare con `st` sotto): scheda **calamaio-48e8**, firmware **0.13.0-ed in
app1**, **0.12.5-gb in app0**; l'aggiornamento remoto scrive la partizione libera, quindi la 0.14.0 va in **app0 al
posto della 0.12.5-gb** e la 0.13.0-ed resta in app1 come ripiego. Controller Xbox già associato (NVS `nimble_bond`).
iPhone 17 Pro con iOS 27, app installata da Xcode con l'Apple ID gratuito (scade dopo 7 giorni).
**Nota sul classificatore**: le scritture grezze di `esptool` sull'otadata le blocca la modalità automatica: quei
comandi li lancia **l'utente** col prefisso `!` nel prompt.

```bash
R=~/CalamaiOS-iphone
SP=<scratchpad>/p3
B=calamaio-48e8
PORT=/dev/cu.usbserial-0001
BK=~/CalamaiOS/backup-fabbrica      # ignorata da git: contiene chiavi e password
ESP=(pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port $PORT --baud 115200)   # da lanciare in $R/firmware
CAL=$SP/calamaio-p3
st() { curl -s http://127.0.0.1:8765/api/status | python3 -c '
import json,sys; d=json.load(sys.stdin); x=[x for x in d["displays"] if x["board"]=="'$B'"][0]; print("fw", x["fw"], "| authed", x["authed"])'; }
```

Registro della scheda: `$CAL logs --board $B --follow --minutes 60` in un terminale a parte (fuori dalla partita);
in partita solo seriale: `pio device monitor -b 115200 -p $PORT`.

### Ordine delle prove (checklist unica)
Questa è la **sola sequenza da seguire** (revisione finale del ramo, con D1–D6, I1 e I2 corretti e le prove extra
della revisione del Task 10 incluse). Le sezioni L0–L13 sotto danno i dettagli di ogni passo; l'ordine è questo.
Ripieghi: **R-OTA** = `$CAL ota push` della build 0.13.0-ed (`s` alla domanda sulla versione più vecchia, vedi L1);
**R-USB** = **l'utente** con `!`, otadata dal backup L0.2 (comando in L1). `$CAL logs --follow` sempre aperto.

**Prima del flash (0.13.0-ed)**
1. **L0.1** build e prove verdi, `calamaio-fw 0.14.0`. → Se qualcosa fallisce: niente flash.
2. **L0.2** backup: NVS + otadata, LittleFS, app0 (tre file, dimensioni, sha256). Poi, offline, controllare che il
   dump NVS nuovo abbia ancora un solo `peer_sec_N` con `ltk_present` = 1. → File mancante o dimensione sbagliata:
   niente flash.
3. **L0.3** misure di riferimento con l'Xbox (`[memoria] Bluetooth acceso…`, `[gb] memoria per la partita…`).
4. **L0.4** servizio nuovo con backup della cartella dati; 8767 riavviato due volte: `riprovo fra 1s` poi `HTTPS su`,
   l'app si ricollega senza riabbinare. → Se fallisce: servizio vecchio di nuovo (la scheda non c'entra).

**Flash**
5. **L1** `ota push` della 0.14.0 in app0. Registro: `fw 0.14.0`; `[pad] Bluetooth acceso, controller associato,
   telefono no`; `[pad] risoluzione degli indirizzi privati: accesa`; `[pad] abbinamento della 0.13: è il controller
   (padx)`; `[pad] libro: controller sì, telefono no`. → Non si avvia o non valida: torna da sola alla 0.13.0-ed,
   altrimenti R-USB. `libro … illeggibile`: fermarsi, R-OTA (niente cancellato).

**L-a, l'Xbox si ricollega (primo evento Bluetooth; niente altro prima)**
6. Svegliare l'Xbox, nessuna azione nei menu. Atteso `[pad] PRONTO in … ms (controller, ricollegamento)` e il toast
   «Controller collegato»; i menu col controller.
   - `controller: abbinamento senza LTK: scollego senza toccare niente` → chiavi intatte. **R-OTA**, poi ruling.
   - `controller: abbinamento rifatto fuori dall'associazione: rimesse le chiavi di prima, scollego` (I1, NimBLE si è
     riabbinato da sé su una richiesta di sicurezza dell'Xbox) → **spegnere subito l'Xbox** (tasto Xbox tenuto); poi
     `controller in quarantena…` e il toast «Abbinamento perso: scegli Associa controller» una volta (N1). Sulla scheda
     chiavi e libro intatti, ma l'Xbox ha ora le chiavi nuove. **R-OTA**; se con la 0.13.0-ed l'Xbox non si ricollega, «Associa controller» lì. Poi ruling su
     un Xbox che si riabbina a ogni collegamento.
   - `controller: abbinamento rifatto fuori dall'associazione, rifiutato` (copia non rimessa) → abbinamento perso in
     tutti e due i firmware. **R-OTA**, «Associa controller» con la 0.13.0-ed, ruling.
   - `cifratura non riuscita` ripetuta, o nessun `PRONTO` in 2 minuti di ricerca → chiavi intatte. **R-OTA**.

**Xbox e avatar, prima del telefono**
7. **L-b / L2** Xbox spento e riacceso due volte (`ricollegamento`, mai `rifatto`). Poi «Associa controller» e il tasto
   di associazione: `PRONTO … (controller, nuovo|ricollegamento)`, niente `sicurezza insufficiente`,
   `libro: controller sì`. → Se fallisce: R-OTA (Just Works non come nella 0.13).
8. **L6.3 con l'Xbox**: Permesso → CONSENTI → A tenuto 3 s → `[permesso] … consentito … da controller`. → Se fallisce:
   R-OTA (regressione del percorso della 0.11).
9. **L13** Gufo a pezzi: offerta, `[memoria]` prima/dopo con `stack rete libero`, `avatar-ack gufo: salvato`, il Gufo
   sulla Home. 5 minuti senza `[ws] disconnesso`, connessioni ferme. Riavvio → `cache: … avatar validi` col Gufo.
   → Se fallisce: `$CAL avatar default demone`; si tiene la 0.14.0 salvo chiusure WS a ciclo (allora R-OTA).

**Telefono**
10. **L3** «Associa telefono»: codice sull'e-ink, scritto su iOS → `PRONTO … (telefono, nuovo)`; il codice compare 0
    volte nel registro. Nessuno `stack overflow` né riavvio (F10); annotare la riga `[memoria] telefono collegato: …,
    stack libero: rete N, pad N, host BLE N byte` (minimi di stack dopo l'ECDH). **t10 #3**: dopo questa associazione
    niente `sicurezza insufficiente` (SC letto dall'archivio). → Riavvio o overflow: R-OTA. Associazione non riuscita:
    «Dimentica telefono», l'Xbox non è toccato.
11. **L3b** codice sbagliato → «Associazione non riuscita», finestra chiusa, `libro: … telefono no`. **L3c** LUNGA
    ANNULLA → `associazione del telefono annullata`; codice scritto lo stesso → `[pad] telefono: associazione annullata
    o scaduta durante la cifratura`, nessun abbinamento. **L-d** lasciare scadere i 2 minuti col codice a schermo →
    «Associazione scaduta», nessun abbinamento. → Se in uno di questi casi nasce un abbinamento (libro o `PRONTO`):
    fermarsi, «Dimentica telefono».
12. **L4** secondo piano e primo piano (5 s → `telefono in secondo piano o muto`, poi ricollegato senza codice). App
    chiusa ≥ 20 minuti → ricollegata senza codice (risoluzione degli indirizzi privati). → Nessun `PRONTO`
    (`non associato fuori dall'associazione`): fermarsi e decidere con l'utente; con l'Xbox si può restare sulla 0.14.
13. **L-c (t10 #5)** su iOS «Dissocia» da associato, poi riaprire l'app fuori dalla finestra. Atteso:
    `codice chiesto fuori dall'associazione, rifiuto`; **nessun codice a schermo**; toast «Abbinamento perso: scegli
    Associa telefono»; `libro: … telefono no`; nessuna finestra aperta. → Un codice a schermo è un difetto di
    sicurezza: fermarsi, **R-OTA**.
14. **L5** menu dal telefono (croce, A al rilascio, B, START). → Se fallisce: solo funzionale, annotare.
15. **L6** Permesso dal telefono: CONSENTI + A 3 s → «Per consentire usa il tasto PRG o il controller», nessun
    `perm-reply`; NEGA + A 3 s → `negato … da telefono`. Telefono su CONSENTI, **aspettare più di 2 s**, PRG 2× →
    `consentito … da tasto`; entro 2 s → «Il telefono ha cambiato la schermata: ripeti». → **Un consenso dal telefono:
    R-OTA subito.**
16. **L6b** conferma dell'abbinamento col PC dal telefono rifiutata (toast, niente `pair-done`), PRG 2× accettato;
    «Configura da telefono» rifiutato; «Cerca reti» ammesso. → **Uno degli 8 comandi dal telefono: R-OTA subito.**
17. **L7** (1) A e app scorsa via → nessuna scelta né HoldFire, niente `3 s senza report` (report di secondo piano);
    (3) A e Centro di Controllo → nessuna scelta, nessun HoldFire (I2); (2) croce tenuta in Pokémon + modalità aereo →
    si ferma entro 3 s (`3 s senza report, tutto rilasciato`); **t10 #9** A tenuto sul telefono + modalità aereo →
    HoldUp, mai Select né HoldFire. → Una scelta o un HoldFire in uno di questi casi: fermarsi (I2 è corretto).
18. **t10 #8** app in primo piano e ferma per 5+ minuti → mai scollegata; app chiusa a forza → scollegata entro 10 s.
    → Scollegata in primo piano: regressione di F7, fermarsi.
19. **t10 #11 (I2 del Task 10)** telefono associato: spegnere e riaccendere il Bluetooth dell'iPhone durante il
    ricollegamento → solo uno scollegamento, abbinamento tenuto (`libro: … telefono sì`, si ricollega senza codice).
    → Abbinamento perso: annotare e riassociare (solo disservizio).
20. **L8 + t10 #6** passaggio Xbox ↔ telefono nei due sensi, testo della pausa in partita. Annotare `libro` dopo ogni
    passo; abbinamenti di NimBLE ≤ 3. → Dispositivo sbagliato o libro sbagliato: fermarsi, R-OTA se l'abbinamento
    dell'Xbox si è perso.
21. **L9** latenza in Pokémon (voto dell'utente 1–5). **L10** heap: 5 minuti col telefono, 20 cicli secondo piano/primo
    piano (blocco massimo giù di ≤ 2 KB), partita col telefono contro L0.3; righe di stack del telefono annotate.
    → Memoria peggiore: annotare i numeri e decidere con l'utente, niente ritocchi improvvisati.
22. **L11** dimenticare per tipo (l'altro tipo e il Bluetooth restano), poi riassociare tutti e due. **L12** solo in
    primo piano (nessun tocco dalle altre schede dell'app; il collegamento resta anche sulle schede «Scheda» e
    «Impostazioni», m5 della revisione finale).
23. **L-e** (facoltativa) reset dell'host durante una ricerca → `accesa`, non `NON accesa`.

**Chiusura**: vedi «Chiusura» in fondo (L-a, L2, L3, L4, L-c, L6, L6b, L7, L10 e L13 obbligatorie).

### L0 Preparazione
- **L0.1 Build e prove.**
  ```bash
  mkdir -p $SP && cd $R && node protocol/vectors/gen-pad-vectors.mjs --check && cd $R/service && go test -count=1 ./... 2>&1 | tail -3 \
   && go build -o $CAL ./cmd/calamaio && cd $R/firmware && pio test -e native 2>&1 | tail -3 && pio run -e heltec213 | tail -3 \
   && strings .pio/build/heltec213/firmware.bin | grep -m1 calamaio-fw && cd $R/ios/CalamaioKit && swift test 2>&1 | tail -3
  ```
  Atteso: tutto verde, `calamaio-fw 0.14.0 heltec213`, immagine sotto 2 359 296. Se fallisce: niente flash.
- **L0.2 Backup della scheda (cavo).** Prima di tutto.
  ```bash
  cd $R/firmware && D=$(date +%Y%m%d-%H%M)
  $ESP read_flash 0x9000 0x7000 $BK/nvs-otadata-$D-pre-0.14.0.bin       # NVS (nimble_bond, calamaio-gb) + otadata
  $ESP read_flash 0x690000 0x160000 $BK/littlefs-$D-pre-0.14.0.bin      # /gb (salvataggi), temi, avatar
  $ESP read_flash 0x10000 0x240000 $BK/app0-$D-0.12.5-gb.bin            # app0: la 0.12.5-gb che l'OTA sovrascrive
  chmod 600 $BK/*-$D-*.bin; shasum -a 256 $BK/*-$D-*.bin
  ```
  Atteso: 28 672, 1 441 792 e 2 359 296 byte. Senza i tre file non si va avanti.
- **L0.3 Misure di riferimento con l'Xbox (0.13.0-ed).** Xbox collegato, alla Home: la riga `[memoria] Bluetooth
  acceso…` più recente (heap libero, minimo, **blocco massimo**). Poi una partita di Pokémon Red col cavo: `[gb] memoria
  per la partita: servono … liberi … blocco massimo …` e `[gb] emulatore: … heap libero …`; Salva ed esci.
- **L0.4 Servizio nuovo (Task 0 e 7).** Il servizio di prova di P1 (`scratchpad/ios/calamaio-iphone serve`, registro
  `scratchpad/ios/serve.log`) va sostituito con `$CAL` sugli stessi dati: backup della cartella dati prima (come in
  `scratchpad/ios/prove-dal-vivo.md`), poi fermarlo e avviare `$CAL serve` con il registro in `$SP/serve.log`.
  Prova del Task 0 (R2): fermarlo e riavviarlo **due volte di fila** subito: nel registro
  `telefoni: porta dei telefoni: … (riprovo fra 1s)` seguito entro pochi secondi da `telefoni: HTTPS su …`, e l'app si
  ricollega senza riabbinare. `st` → `fw 0.13.0-ed | authed True`. La 0.13.0-ed non dice `caps`: riceve ancora
  `avatar` (e col fermo di 60 s non si ricollega più una volta al secondo se le capita il Gufo: non serve provarlo).

### L1 Flash e avvio della 0.14.0
```bash
$CAL ota push --board $B --build $R/firmware/.pio/build/heltec213
```
Atteso: fasi fino a `fatto`, riavvio, valida entro 120 s; `st` → `fw 0.14.0`. Registro: `[pad] Bluetooth acceso,
controller associato, telefono no`, `[pad] risoluzione degli indirizzi privati: accesa`, `[pad] abbinamento
della 0.13: è il controller (padx)`, `[pad] libro: controller sì, telefono no`.
Se fallisce: una versione che non si avvia torna da sola alla 0.13.0-ed. Ripiego a mano: `$CAL ota push` della build
0.13.0-ed (`~/CalamaiOS-editor/firmware/.pio/build/heltec213`, con `s` alla domanda sulla
versione più vecchia); col cavo, **l'utente** con `!`: otadata dal backup L0.2
(`dd if=$BK/nvs-otadata-<D>-pre-0.14.0.bin of=$SP/otadata.bin bs=4096 skip=5 count=2 && $ESP write_flash 0xe000 $SP/otadata.bin`).
Se il registro dice `[pad] risoluzione degli indirizzi privati NON accesa (rc …): riprovo` senza un `accesa` dopo:
annotare `rc` e proseguire con L-a; L4 dirà se il telefono si ricollega. Se dice `libro … illeggibile`: fermarsi e
tornare alla 0.13.0-ed (niente è stato cancellato).

### L2 Xbox si ricollega e si riassocia (prova obbligatoria della spec)
1. Accendere l'Xbox: `[pad] PRONTO … (controller, ricollegamento)`, toast «Controller collegato». Menu dal controller.
2. Menu › Gioco › Controller › «Associa controller», tasto di associazione dell'Xbox: `PRONTO … (controller, nuovo)`
   o `ricollegamento`; registro senza `sicurezza insufficiente`; `[pad] libro: …` invariato per il telefono.
Se fallisce: la configurazione Just Works non è tornata quella della 0.13 (controllare `applySecurity`): ci si ferma.

### L3 Associazione del telefono con passkey
1. iPhone: app Calamaio, scheda «Controller» («Cerco la scheda…»). Scheda: Menu › Gioco › Controller › «Associa
   telefono» (toast «Metti l'app Calamaio sulla scheda Controller»).
2. Atteso: schermata «TELEFONO / Codice per il telefono / 123 456» sull'e-ink (parziale); iOS chiede il codice;
   digitarlo. Scheda: «Telefono collegato», registro `[pad] telefono: informazioni ver 1, app «0.1.0»`, `[pad]
   PRONTO … (telefono, nuovo)`, `[memoria] telefono collegato: …`. App: «Collegato alla scheda». Il codice **non**
   compare nel registro (`grep -c` delle 6 cifre nel log = 0). Durante tutto l'abbinamento nessun riavvio né
   `stack overflow` del task host di NimBLE (lo ECDH P-256 di Secure Connections gira lì, 4 096 byte di stack: F10);
   la scheda resta sveglia col codice a schermo (F6).
3. **L3b codice sbagliato**: «Dimentica telefono» (2×), su iOS Impostazioni › Bluetooth › Calamaio › Dissocia,
   poi «Associa telefono» e un codice sbagliato. Atteso: «Associazione non riuscita», la schermata del codice sparisce,
   registro `sicurezza insufficiente` o scollegamento, nessun `PRONTO`, `[pad] libro: … telefono no`; la finestra è
   chiusa (F6): la scheda non chiede un altro codice da sola, serve di nuovo «Associa telefono».
4. **L3c annulla**: «Associa telefono», alla schermata del codice pressione lunga del PRG: torna il menu, registro
   `[pad] associazione del telefono annullata`. Poi di nuovo, ma dopo la pressione lunga scrivere comunque il codice
   sull'iPhone (se iOS lo chiede ancora): registro `[pad] telefono: associazione annullata o scaduta durante la
   cifratura`, nessun «Telefono collegato», `[pad] libro: … telefono no` (F6). Rifare L3 fino a collegato.

### L4 Ricollegamento e indirizzo privato
1. App in secondo piano e di nuovo in primo piano: entro 5 s `[pad] telefono in secondo piano o muto: scollego e torno
   a cercare` e «Telefono scollegato» (F7), poi «Telefono collegato» senza codice.
2. Chiudere l'app, aspettare **almeno 20 minuti** (l'indirizzo privato di iOS cambia ~ogni 15), riaprire la scheda
   Controller: la scheda si ricollega **senza codice** (`ricollegamento`).
Se al punto 2 la scheda non lo trova (nessun `PRONTO`, o `non associato fuori dall'associazione`): la risoluzione
degli indirizzi non funziona. Ci si ferma e si decide con l'utente (strada da provare: `ble_hs_pvcy_set_resolve_enabled`
dopo ogni `reconcile` e alla fine di ogni associazione, poi `CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY`), con una misura
della memoria per ogni tentativo.

### L5 Menu dal telefono
Dalla Home col telefono: croce giù/su sposta, A sceglie (al rilascio), B indietro, START apre il Menu; aprire Menu ›
Gioco › Controller («telefono collegato» nello Stato), tornare alla Home. Atteso come col controller; registro
`[pad] su -> …` per ogni evento.

### L6 Permessi: CONSENTI dal telefono rifiutato, NEGA accettato
Sessione di Claude Code collegata al display (skill calamaio) che chiede un permesso Bash.
1. Dal telefono: A sul Dettaglio → Permesso (NEGA preselezionato); croce giù → CONSENTI; A tenuto 3 s. Atteso:
   toast «Per consentire usa il tasto PRG o il controller», il permesso resta a schermo, il terminale continua a
   chiedere; registro `Telefono: CONSENTI non si conferma dal telefono…`, nessun `perm-reply`.
2. Croce su → NEGA, A tenuto 3 s dal telefono. Atteso «Negato · inviato a Claude Code», Claude riceve il rifiuto;
   registro `[permesso] … negato … da telefono`.
3. Nuovo permesso: dal telefono scegliere CONSENTI, **aspettare più di 2 s**, poi **PRG 2×**: consentito (`da tasto`).
   Lo stesso con il PRG 2× entro 2 s dalla mossa del telefono: toast «Il telefono ha cambiato la schermata: ripeti»,
   niente parte (`kPhoneSettleMs`, revisione R2-I1). Con l'Xbox (passo 8 della checklist): A tenuto 3 s su CONSENTI
   consente (`da controller`).

### L6b Abbinamento col PC e Wi-Fi: mai dal telefono (R1, F11)
1. Dal PC `$CAL pair --board $B` (o «Abbina» nella webapp), scrivere sul PC il codice giusto; sulla scheda «Il PC ha il
   codice giusto». Dal telefono A: toast «Per confermare usa il tasto PRG o il controller», la schermata resta, registro
   `Telefono: l'abbinamento col PC si conferma col tasto PRG o col controller`, nessun `pair-done` (né
   `[sicurezza] pair-confirm dal telefono scartato (R1)`: la prima barriera basta). Poi PRG 2×: abbinato come sempre,
   `st` → `authed True`.
2. Dal telefono Menu › Wi-Fi › «Configura da telefono», A: stesso toast, nessun QR, registro senza la riga della
   configurazione dal telefono. «Cerca reti» invece si apre.
**Mai dal vivo** Proteggi (eFuse irreversibile), Ripristino, «Dimentica abbinamento» e le «Dimentica» delle reti:
bastano le prove native (`test_pad_ui`, `test_m5_ui`).

### L7 Tasti bloccati e mantenimento
1. In un menu (non sul Permesso), dal telefono A premuto e, entro 1 s, chiudere l'app a forza (scorrere via).
   L'app diventa prima inattiva e manda il report di secondo piano: atteso nessuna scelta, nessun HoldFire, nessun
   Riposo; il registro **non** ha `3 s senza report` (ha rilasciato il report di secondo piano, revisione finale I2),
   poi entro 5 s `[pad] telefono in secondo piano o muto: scollego e torno a cercare`.
2. Croce destra tenuta in Pokémon, poi telefono in modalità aereo: il personaggio si ferma entro 3 s, registro
   `[pad] telefono: 3 s senza report, tutto rilasciato` (il vero caso di silenzio). Lo stesso alla Home con A tenuto
   e modalità aereo (t10 #9): solo HoldUp, mai una scelta né HoldFire.
3. In un menu, A premuto e Centro di Controllo aperto prima di rilasciare: nessuna scelta, nessun HoldFire (report di
   secondo piano = rilascio forzato, I2).

### L8 Passaggio Xbox ↔ telefono
1. Telefono collegato, accendere l'Xbox: la scheda resta sul telefono. Chiudere l'app: entro 5 s la scheda scollega
   il telefono (F7) e prende l'Xbox. L'app riaperta dopo 10 s mostra «La scheda usa un altro controller: spegnilo per
   usare il telefono».
2. Spegnere l'Xbox (tasto Xbox tenuto): la scheda trova il telefono (entro la ricerca: 2 minuti continui, poi 3 s ogni
   30 s a batteria). Poi al contrario: chiudere l'app, accendere l'Xbox: entro 5–10 s «Controller collegato».
3. In partita col telefono: chiudere l'app. La pausa dice «Telefono scollegato» e «Riapri l'app Calamaio sulla scheda
   Controller…» (F7); con l'Xbox spento dice ancora «Controller scollegato».
Atteso: un solo collegamento alla volta, toast giusti per tipo, righe `[memoria] … collegato/scollegato` a ogni passo.

### L9 Latenza in Pokémon
Partita di Pokémon Red col telefono (Menu › Gioco › ROM): camminare, menu del gioco (START), pausa con GUIDA, B per
riprendere. Giudizio dell'utente (1–5) rispetto all'Xbox, annotato. Seriale: le righe `[gb] emulatore …` non devono
mostrare più fotogrammi persi che con l'Xbox (L0.3).

### L10 Heap e frammentazione
1. Alla Home col telefono collegato 5 minuti: righe `[memoria] telefono collegato (ogni minuto)`. Confronto con L0.3:
   heap libero non sotto di oltre 1 KB, blocco massimo annotato.
2. 20 cicli app in secondo piano / primo piano (collegato, scollegato): dopo il ventesimo il blocco massimo non deve
   essere sceso di oltre 2 KB rispetto al primo `telefono collegato`.
3. Partita di Pokémon col telefono collegato: `[gb] memoria per la partita: … blocco massimo …` e
   `[gb] emulatore: … heap libero …` contro L0.3. Obiettivo della spec: nessun peggioramento rispetto all'Xbox.
Se peggiora: annotare i numeri, ci si ferma e si decide con l'utente (niente ottimizzazioni improvvisate).

### L11 Dimenticare per tipo
1. «Dimentica telefono» (2×): «Telefono dimenticato», l'Xbox resta (`[pad] libro: controller sì, telefono no`), il
   Bluetooth resta acceso; l'app non si ricollega più senza «Associa telefono».
2. Riassociare il telefono, poi «Dimentica controller»: il telefono resta e si ricollega; il Bluetooth resta acceso.
3. Riassociare l'Xbox (alla fine si torna con tutti e due associati).

### L12 Solo in primo piano
App in secondo piano col telefono collegato: la scheda rilascia subito (report con flags bit0 = 0), scollega entro 5 s
(`[pad] telefono in secondo piano o muto: scollego e torno a cercare`, «Telefono scollegato», F7) e non riceve più
niente; la scheda non riceve tocchi dalla scheda «Scheda» o «Impostazioni» dell'app.

### L13 Avatar a pezzi: il Gufo (R4)
Precondizioni: L1 fatta (0.14.0), servizio nuovo (L0.4), registro della scheda in un terminale
(`$CAL logs --board $B --follow --minutes 60`) e quello del servizio (`tail -f $SP/serve.log`). Il Gufo è nella
libreria dell'utente (`~/Library/Application Support/calamaio/avatars/gufo.avatar.json`, 41 145 byte; il suo CAVT è di
7 072 byte, 10 pose × 4 fotogrammi) e **non** è nella cache della scheda (l'avatar predefinito era tornato «demone»).
1. Telefono collegato (Bluetooth acceso: è il caso che falliva), alla Home. `$CAL avatar default gufo`.
2. Atteso entro pochi secondi, nel registro della scheda: `[avatar] avatar-offer gufo: 7072 byte, scarico al prossimo
   giro`, `[memoria] prima dell'avatar a pezzi: heap libero …, minimo …, blocco max …`, `[avatar] gufo scaricato a
   pezzi (7072 byte, crc …) in … ms`, `[memoria] dopo l'avatar a pezzi: …`, `[avatar] gufo crc … a pezzi, nel posto
   …`, `[avatar] avatar-ack gufo: salvato`; nel servizio `ws: #… avatar-offer gufo (crc …, 7072 byte)`,
   `avatar: gufo scaricato da …`, `ws: #… avatar gufo salvato dalla scheda`. Sulla Home il Gufo (foto per l'utente).
   Nessuna riga `messaggio malformato ignorato`, `allocazioni fallite`, `chiusure brevi di fila`.
3. **Niente ciclo**: per 5 minuti nessun `[ws] disconnesso` nel registro della scheda e il numero di connessioni nel
   servizio fermo (`grep -c 'connessione #' $SP/serve.log` uguale all'inizio e alla fine).
4. Riavvio (Menu › Info › Riavvia): `[avatar] cache: … avatar validi` con il Gufo fra i posti, niente `avatar-need`
   per il Gufo, il Gufo a schermo subito dopo l'avvio.
5. Annotare in DECISIONI («Avatar a pezzi», esito di L13) le due righe `[memoria]` del passo 2 e il tempo del download.
Se fallisce: `$CAL avatar default demone` (come il ripiego di oggi), annotare le righe e fermarsi con l'utente. Se il
registro mostra `avatar-ack gufo: rifiutato · …`, il servizio non lo riofferta per 60 s (`avatar gufo non rimandato`):
è la difesa, non un altro errore. Alla fine chiedere all'utente se lasciare il Gufo come predefinito.

### Chiusura
Numeri e esiti in DECISIONI («Fase P3», «Memoria (0.14.0)», «Avatar a pezzi», esiti di L-a, L4 e L13, righe di stack
del telefono). Si lascia la 0.14.0 in app0 solo se **L-a, L2, L3, L4, L-c, L6, L6b, L7, L10 e L13** sono passate (una
scheda che non ricollega il telefono senza codice non ha la funzione, anche se è sicura); altrimenti si torna alla
0.13.0-ed (R-OTA) e il predefinito torna «demone».

---

## Self-review (fatta scrivendo il piano, rifatta dopo il pre-flight e R1–R4)

- **Copertura della spec**: §2.1 (servizio, report, mantenimento, secondo piano) → Task 1, 2, 10, 12, 13, 14; §2.2
  (associazione, passkey, due abbinamenti, uno alla volta, avviso 10 s) → Task 3, 4, 6, 10, 13; §2.3 (sorgente,
  regola §7, NEGA) e §0 (niente abbinamento della scheda né Wi-Fi dal telefono) → Task 5, 11, R1 e F11, prove L6, L6b;
  §2.4 (pulsantiera, mappatura, vibrazione, multi-tocco) → Task 13, 14; §6 (firmware, toast, allineamento, memoria) →
  Task 6, 11, prove L10; §7 «Nativo» → Task 2–6, 8, 10, «Swift» → Task 12, 13, «Dal vivo» → L2, L3, L5, L6, L9
  (abbinamento QR, webapp, revoca e 7 giorni sono di P1/P2). Fuori dalla spec, dal registro: R2 → Task 0 e L0.4;
  R4 → Task 7–9 e L13.
- **Pre-flight**: F1 → Task 4 Step 2; F2 → Task 5 Step 5; F3, F5, F6, F7 → Task 10 (F5 anche Task 4, F6 anche
  Task 6 per il sonno, F7 anche Task 6 per la pausa); F4 → Task 11 Step 2 (la prova è
  `test_phone_hold_fire_from_tick_refused`, Task 5); F8, F9 → Task 6; F10 → Task 11 Step 3 e L3; F11 → Task 5, 6
  (aiuto), 11 (terza barriera), L6b; F12a, F12b → Task 13; F12c, F12d → DECISIONI (Task 11); A1 → Task 5; A2 →
  Task 4; A3 accettato (la prova di `test_m5_ui` copre la seconda barriera di R1 con la rete); D1 → Task 12; D2 →
  Task 3 e 10; D3 → Task 10; D4 lasciato. R1: tutti i punti di §3 del pre-flight (Goal, vincoli, fatti, Review Focus,
  mappa, Task 5, 6, 11, DECISIONI rovesciata, L6b, chiusura). R3: base 168 936 superata da F10 (base misurata).
- **Interfacce fra i task**: `NetRequest::src` e `phoneForbidden` (Task 5) → `onNetRequest` (Task 11);
  `NetCmd::AvatarAck` (Task 8, in fondo all'enum, chiave `"avatar-ack"` in fondo a `netCmdKey`) → `avatarsLoop` e
  `handle()` (Task 9); `CavtStream`, `AvatarDownload`, `AvatarCache::adopt`, `AvatarStorage::temp*` (Task 8) →
  `avatar_fs`, `avatarLoop`, `avatarsLoop` (Task 9); `theme::blobUrl`, `lowerHex`, `cutUtf8` (Task 8) → `avatarUrl`,
  `decodeAvatarOffer`, `encodeAvatarAck`; filo `hello.caps`/`avatar-offer`/`avatar-ack` ed esempio
  `avatar-offer.json` (Task 7) → `test_avatar` (Task 8) e `sendHello` (Task 9); `pad::PhoneIdle` (Task 10) → `gIdle`;
  `pad::legacyIsXbox` (Task 3) → `blepad::bonded` (Task 10); `linkSecure(…, sc)` (Task 4) → `connectFound`
  (Task 10); `State::src` (Task 2) → `padHandle` (Task 11); `PadReport.decode` solo nel target delle prove (Task 12)
  → `PadControllerTests` (Task 13).
- **Nomi coerenti**: `pad::Kind`, `State::src`, `Keepalive::cap`/`blepad::tickClock`, `PhoneIdle`, `BondBook`,
  `legacyIsXbox`, `pickAd`, `secFor`/`linkSecure`, `storedSc`, `securePhone`, `InputSrc`, `PermEvent::src`,
  `NetRequest::src`, `phoneForbidden`, `phoneRefused`, `kPhoneAllowToast`/`kPhoneConfirmToast`,
  `PadReq::PairPhone/ForgetPhone/CancelPair`, `padLinkChanged(l, kind, xb, ph, batt, now)`, `CavtStream`,
  `AvatarDownload`, `AvatarCache::adopt`, `AvatarOffer`, `CloseStorm`, `fetchBlob`, `avatarLoop`, `takeAvatarFile`,
  `CapAvatarDl`, `AvatarFailHold`, `AvatarAckTimeout`, `blobTokenStatus`, `serveBlob`, `PadController.Status`,
  `PadPeripheralEvent`.
- **Numerazione**: Task 0, 1–6 come prima, 7–9 nuovi (R4), 10–14 = i vecchi 7–11 (riferimenti interni aggiornati:
  il registro e il pre-flight parlano ancora coi numeri vecchi: T7 → 10, T8 → 11, T9 → 12, T10 → 13, T11 → 14).
- **Verifica**: il codice dei Task 4–11 (con F1–F9, R1, F11, D2, D3 e R4) è stato provato in una copia del ramo
  (`pio test -e native` 866 prove, 865 verdi e 1 saltata come prima, `native_asan` verde, `pio run -e heltec213` SUCCESS, RAM 169 192, immagine
  1 965 648); quello dei Task 7 con `go test ./...` e `-race`; D1 e F12a con `swift test --filter Pad` (22 verdi).
  Le prove dichiarate «a mano facoltative» (mutazioni) sono state fatte lì.
- **Review Focus**: ogni riga ha i suoi test nel task che possiede il codice e una prova dal vivo.
