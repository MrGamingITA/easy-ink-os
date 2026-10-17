# Aggiornamento remoto (OTA) e registro da remoto

Specifica. Firmware, servizio e webapp la seguono; i messaggi entrano in `PROTOCOLLO.md` e nello schema. Requisiti dell'utente raccolti in `DECISIONI.md`.

## 1. Requisiti

1. Solo dal servizio **abbinato e autenticato** (M5).
2. Immagine **firmata** sul PC e verificata dalla scheda prima di avviarla.
3. **Ritorno automatico** alla versione precedente se la nuova non parte o non si collega.
4. Mai durante una richiesta di permesso aperta, né durante la calibrazione della batteria.
5. **Con il cavo USB parte sempre**; senza cavo solo con la batteria ≥ 50% misurata subito prima; se la scheda non sa stabilire né l'una né l'altra cosa, non parte.
6. Schermata a tutto schermo: titolo «AGGIORNAMENTO», versione da → a, barra grande con percentuale e byte, avviso «Non spegnere», fasi (ricezione, verifica, scrittura, riavvio). **La mascotte predefinita corre sulla barra**, sul fronte della parte completata, verso destra, con un fotogramma diverso a ogni passo (posa `run`, AVATAR.md §12). Ridisegno ogni ~5% o ogni 2 s (il più lento). Fine: posa «finito» e «Riavvio…»; errore: posa «errore» e «Aggiornamento non riuscito: resta la versione di prima».

## 2. Firma

- Chiave Ed25519 del PC: `calamaio ota keygen` la crea in `<config>/calamaio/ota-key` (0600, mai nel repository). Firma con la stessa implementazione su PC e scheda (TweetNaCl `crypto_sign`, già usata per X25519; in Go `crypto/ed25519`), vettori condivisi in `protocol/vectors/`.
- La scheda impara la **chiave pubblica** dal servizio abbinato: messaggio `ota-key {pub, mac = HMAC(SK, "ota-key" ␟ pub)}` dopo l'autenticazione; la salva nel vault (cifrata come gli altri segreti). Una chiave diversa da quella salvata viene accettata solo dopo un nuovo abbinamento (che cancella la vecchia).
- **Manifesto firmato**: `{board, device, version, size, sha256, built}` serializzato in forma canonica (campi in quest'ordine, separati da 0x1F); firma Ed25519 sul manifesto.

## 3. Flusso

```
servizio → display  ota-offer   {id, manifest:{…}, sig, url, mac}     mac = HMAC(SK, "ota-offer" ␟ id ␟ sha256)
display → servizio  ota-status  {id, phase, done, total, error?}      phase: rifiutato | ricevo | verifico | scrivo | riavvio | fatto | errore
```

1. La scheda controlla: autenticazione, mac, firma del manifesto con la chiave salvata, `board`/`device` uguali ai suoi, condizioni del §1 (punti 4 e 5). Se no → `ota-status {phase: rifiutato, error: <motivo leggibile>}`.
2. Mostra la schermata e scarica l'immagine con **HTTP GET** dal servizio (`url` sulla porta dei display, token monouso nel percorso, valido 5 minuti) scrivendola in streaming nella partizione OTA libera (`esp_ota_begin/write`), con SHA-256 calcolato al volo. Avanzamento a `ota-status` ogni ~5%.
3. Verifica: SHA-256 uguale al manifesto e dimensione esatta → `esp_ota_end`, `esp_ota_set_boot_partition`, riavvio.
4. Dopo il riavvio la nuova versione resta «in prova» (`ESP_OTA_IMG_PENDING_VERIFY`, rollback già attivo nel bootloader): diventa valida (`esp_ota_mark_app_valid_cancel_rollback`) solo dopo Wi-Fi + autenticazione con il servizio entro 120 s. Altrimenti `esp_ota_mark_app_invalid_rollback_and_reboot` → torna la versione di prima, che al collegamento successivo manda `ota-status {phase: errore, error: "la nuova versione non si è avviata: tornato alla precedente"}`.
5. Interruzioni (rete, corrente, batteria): l'immagine parziale non viene mai avviata; la scheda resta sulla versione corrente.

## 4. Servizio e webapp

- `calamaio ota keygen`, `calamaio ota push [--board B] [FILE|--build DIR]` (default: il pacchetto firmware del servizio), con avanzamento nel terminale; firma automatica.
- Webapp, pagina Firmware, sezione «Aggiornamento senza cavo» (oggi «in arrivo»): scheda, versione attuale → nuova, batteria e cavo, motivo se non si può, pulsante «Aggiorna», barra di avanzamento in diretta dagli `ota-status`, esito.
- Un solo aggiornamento alla volta per scheda; annullabile prima della fase «scrivo».

## 5. Registro da remoto

- La scheda tiene in RAM gli ultimi ~16 KB di righe di registro (anello), comprese quelle di quando la seriale è spenta.
- `log-start {id, follow: bool, minutes ≤ 60}` (servizio → display, solo autenticato): la scheda manda il contenuto dell'anello in messaggi `log {id, lines: [...]}` (≤ 8 KB ciascuno) e, con `follow`, le righe nuove man mano (raggruppate ogni ~1 s), fino a `log-stop {id}` o alla scadenza. Spento di default.
- Servizio: `calamaio logs --board B [--follow] [--minutes N]` stampa e salva in `<dati>/calamaio/logs/<board>-<data>.log`; webapp: riquadro «Registro» nella pagina Dispositivo con «Segui» e «Scarica».

## 6. Rapporti di crash

- Se il core lo permette (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` nella sdkconfig del core 2.0.17: da verificare), al riavvio dopo un crash la scheda legge il riepilogo (`esp_core_dump_get_summary`: causa, PC, backtrace) e lo manda con `crash {fw, reason, pc, backtrace[]}` appena autenticata, poi cancella il coredump. Senza coredump: almeno `esp_reset_reason()` e l'ultima riga di registro salvata in RTC.
- Servizio: salva il rapporto in `<dati>/calamaio/crash/`, e se ha il `firmware.elf` della stessa versione traduce gli indirizzi (addr2line di PlatformIO). `calamaio status` mostra l'ultimo crash.
