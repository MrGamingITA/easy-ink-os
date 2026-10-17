# sim-capture

Cattura dal simulatore (`reference/calamaio-os-browser/test-heltec-wireless-paper.html`) le schermate della Heltec Wireless Paper (classe S, 250×122, 1 tasto) come immagini 1 bit di riferimento, sempre identiche da un'esecuzione all'altra. Servono per il confronto con il firmware.

## Comandi

```sh
cd tools/sim-capture
npm install                      # solo playwright 1.64.0 (usa chromium-1248 già in ~/Library/Caches/ms-playwright)
node capture.mjs                 # rigenera out/heltec213/ (PNG, PBM e manifest.json)
node capture.mjs --only menu,stato --out /tmp/prova   # con --only il manifest esistente si aggiorna solo per quegli scenari
node verify-determinism.mjs      # due catture in cartelle temporanee: confronto byte per byte, e con out/heltec213
node gen-avatars.mjs             # avatars/ (avatar, ancore, accessori) → assets/avatars.js del simulatore, firmware/src/gfx/avatar_builtin.cpp e accessory_builtin.cpp
node gen-avatars.mjs --check     # esce con 1 se i due file non corrispondono più alla libreria
node diff.mjs out/heltec213/menu-f0.pbm firmware/menu-f0.pbm --out diff.png --threshold 0.5
node diff.mjs --ref-dir out/heltec213 --fw-dir DIR_FW --out-dir DIR_DIFF --threshold 0.5 [--allow-missing] [--json]
```

`diff.mjs`: percentuale di pixel diversi (pixel diversi / 30 500 × 100), PNG di differenza (rosso = inchiostro solo nel riferimento, blu = solo nel firmware, grigio = in entrambi). Uscita 0 entro la soglia, 1 oltre la soglia o schermate mancanti, 2 errore d'uso o di formato. Legge PBM P1 e P4.

## Che cosa si cattura

Il riferimento è `Screen.compose(m, frameFor(dev, m, f, now), ctl)`: la matrice 0/1 (1 = inchiostro) prima dei colori carta/inchiostro e del ghosting del canvas visibile. Ogni schermata esce con i contatori di fotogramma 0 e 1 (`f`); a schermo va `frameFor(f)`, che sulla Heltec dà `f % 2` e 0 con il risparmio energetico (M5: la Goccia resta ferma). Nel manifest `frame` è il contatore e `drawnFrame` il fotogramma disegnato.

- `out/heltec213/<nome>-f<f>.png`: scala di grigi a 1 bit, nero = inchiostro.
- `out/heltec213/<nome>-f<f>.pbm`: P4, 1 = inchiostro, stesso contenuto.
- `out/heltec213/manifest.json`: per ogni immagine la sequenza di comandi, l'ora della cattura, la schermata, lo stile home, la pila `ui.stack`, la sessione selezionata e quella mostrata, il toast, gli hash dei sorgenti del simulatore e i testi disegnati (stringa già accorciata, posizione, font).

L'elenco delle schermate, con i passi, sta in `scenarios.mjs`. Ci si arriva solo con i gesti del tasto PRG (`1x` `2x` `3x` `long` `vlong`, tramite `keyDown`/`keyUp` del componente) e con i pulsanti di Simula (`sim:pc`, `sim:limit`…). Nessuna schermata modifica il modello direttamente. I pulsanti aggiunti per M3 (`sim:empty`, `sim:nolimits`, `sim:joinfail`, `sim:elsewhere`) per M5 (`sim:pair`, `sim:pairok`, `sim:pairbad`, `sim:redo`) e per la batteria in alto (`sim:battlvl`: 100 → 50 → 10 %, a giro) rendono raggiungibili gli stati che sul firmware arrivano dalla rete vera; per M6 `sim:battcrit` (batteria scarica) e `sim:calsaved` (calibrazione salvata). Per l'aggiornamento remoto `sim:ota` (docs/OTA.md §1.6: a giro 0 % → 37 % → 80 % → verifica → riavvio → errore → rifiutato → chiusa; ogni pressione è un ridisegno e la corsa, posa `run` di AVATAR.md §12, avanza di un fotogramma), scenari `ota-*`. Per le reti salvate (firmware 0.8.0) gli scenari `reti-salvate*` e `rete-salvata-*` ci arrivano solo con i gesti (Menu › Wi-Fi › Reti salvate, voce 3) sulle 4 reti finte di `m.saved`, senza pulsanti Simula nuovi. Per la giornata di lavoro (0.9.0) `sim:work` (come il comando `workday` del PC: avvia, poi ferma), `sim:worklast` (la giornata finisce fra 25 minuti) e `sim:workend` (finisce adesso: al tick dopo «GIORNATA FINITA»), scenari `giornata-*`, `riposo-giornata*`, `home-*-giornata*`; per il gioco «Alto o basso» (Menu › Gioco › Alto o basso dalla 0.11.0, prima in Menu › Strumenti) `sim:gameseed` (seme fisso) e `sim:perm` (richiesta di permesso: interrompe la partita), scenari `gioco-*` e `menu-gioco`. Per l'ora non ancora nota (0.10.1: accensione a freddo senza welcome né SNTP) `sim:noclock` (a giro: «--:--» e niente data, poi di nuovo l'ora), scenari `riposo-senza-ora`, `home-senza-ora`, `menu-senza-ora`, `offline-senza-ora`, `riposo-ora-arrivata`. Gli scenari della calibrazione aspettano fino a 2 ore di orologio finto: la cattura completa dura circa 25 minuti (misurato il 2026-10-11 su questo Mac); `firmware/test/test_ui` li rifà con la stessa logica (`Replay::sim`, `joinfail` passa da `Ui::netConnectFailed`).

## Avatar

Le sessioni finte hanno un avatar (api-server Gatto, web-app Demone, firmware-esp32 Calamaro, docs-site nessuno, cioè il predefinito) e `m.set.defaultAvatar` vale `goccia`. La libreria arriva al simulatore da `assets/avatars.js` (CAVT v1 in base64, docs/AVATAR.md §8), generato da `gen-avatars.mjs` con il pacchetto di `tools/avatar/lib/pack.mjs`; gli stessi byte vanno nel firmware. Pulsanti Simula: `sim:avatar` (avatar della sessione mostrata: predefinito → gatto → demone → calamaro → predefinito) e `sim:defavatar` (predefinito: goccia → gatto → demone → calamaro). Scenari `avatar-*` in `scenarios.mjs`; `capture.mjs` si ferma se la libreria non è caricata e scrive nel manifest l'avatar di ogni sessione e il predefinito.

## Gioco della mascotte (0.10.0)

Pulsanti Simula che fanno le veci del servizio: `sim:game` (impostazione remota `game`, a giro), `sim:petlevel` (sale di livello; ai pari sblocca il primo accessorio del catalogo non ancora sbloccato), `sim:petgoal` (traguardo: Primo task, Primo verde, 10 task, 7 giorni di serie, Livello 5, a giro), `sim:petwear` (indossa: nessuno → gli 8 del catalogo → nessuno), `sim:pettired` (stanca / riposata), `sim:petspecial` (accessorio speciale da chiedere a Claude). Riassunto finto di partenza: livello 3, 66/417 XP, energia 80%, cappellino sbloccato. Scenari `gioco-*` e `mascotte*`: accessori su Home Goccia, Una alla volta, Contatori e Dettaglio, schermata «La mascotte» e «Accessorio», festa, toast nei casi bloccati (permesso, silenzio, notturno, risparmio), gioco spento, corsa dell'aggiornamento. Il disegno degli accessori del simulatore si confronta con il riferimento Node con `node tools/golden/accessory-golden.mjs --check` (1 297 casi).

## Determinismo

- L'orologio è quello di Playwright (`page.clock`), fermato prima del caricamento sul 2026-10-11 alle 10:30:00, ora di Roma. Avanza solo con `runFor`, timer compresi.
- `Math.random` è sostituito da un generatore con seme fisso.
- Le sessioni automatiche e lo `slowTick` sono spenti (batteria 78%, −58 dBm restano fissi).
- Il codificatore PNG e PBM è in `lib/image.mjs`, senza dipendenze.

Il testo è disegnato dal canvas con l'antialiasing e poi tagliato alla soglia 0,5. I pixel dei caratteri dipendono quindi da Chromium e dal sistema: le immagini sono identiche byte per byte solo con la stessa versione di Chromium sulla stessa piattaforma (indicate nel manifest). Se cambiano i sorgenti del simulatore, cambiano gli hash nel manifest: ricattura.
