# SPIKE usa e getta · blepad

Prova 2 della specifica giochi (`docs/superpowers/specs/2026-10-12-giochi-gameboy-design.md`): il controller
**Xbox Series X|S modello 1914** si associa alla Heltec Wireless Paper V1.2 via Bluetooth LE? I tasti arrivano
in fretta? Il Wi-Fi verso il PC regge mentre il controller manda dati?

Progetto separato: non tocca `firmware/`, `service/`, `spikes/fastink/`. Non usa il pannello né altri pin:
solo radio e seriale. Lo schermo resta con l'ultima immagine di Calamaio.

## Scelta della libreria

**NimBLE-Arduino 2.5.1** come centrale BLE, con il protocollo HID-over-GATT dell'Xbox scritto qui
(`src/xbox_report.*`). Motivo: è una libreria normale per il core Arduino 2.0.17 su espressif32@6.9.0, la
stessa piattaforma del firmware, quindi si porta dentro senza cambiare nulla. Bluepad32 supporta bene l'Xbox
(anche su S3, solo BLE) ma porta il suo stack BTstack e vuole il suo pacchetto Arduino o ESP-IDF come base:
per il firmware vorrebbe dire cambiare piattaforma. Il layout del report viene da Bluepad32
(`uni_hid_parser_xboxone.c`, firmware v5) e dalla libreria asukiaaa provata sul 1914; lo spike stampa il
report map vero per confermarlo.

## Come si flasha (per il coordinatore)

Solo l'app. Tabella partizioni e bootloader sono **identici byte per byte** a quelli del firmware Calamaio
(`partitions.bin` SHA-256 `1d9cca96de0fe07a…`, `bootloader.bin` `1776e4dd896a69d0…` in entrambi): nvs 0x9000
(20 KB), otadata 0xe000, app0 0x10000 (3264 KB), app1 0x340000, spiffs 0x670000, coredump 0x7f0000.
Si scrivono solo otadata (`boot_app0.bin`, così parte app0 anche se l'ultimo OTA aveva scelto app1) e app0.
eFuse: lo spike non le tocca. Baud 115200.

```sh
cd ~/CalamaiOS/spikes/blepad
pio run                      # build
pio test -e native           # test della parte pura

pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 ~/CalamaiOS/spikes/blepad/.pio/build/heltec213/firmware.bin

pio device monitor -b 115200 -p /dev/cu.usbserial-0001   # comandi: help
```

Non usare `pio run -t upload`: riscriverebbe anche bootloader e tabella (identici, ma inutile).

### Cosa scrive nella NVS

La NVS è condivisa con Calamaio. Lo spike **non** tocca i namespace `calamaio-*` né `nvs.net80211`
(il Wi-Fi parte con `persistent(false)`: SSID e password restano in RAM e vanno ridati a ogni avvio).
Scrive solo:

- `nimble_bond` (NimBLE, al primo abbinamento): `our_sec_1`, `peer_sec_1` (chiavi dell'abbinamento),
  `cccd_sec_1`… (iscrizioni alle notifiche), `p_dev_rec_1`; eventualmente `local_irk_1`, `rpa_rec_1`.
  Blob piccoli (decine di byte l'uno).
- `phy` (calibrazione radio dell'IDF): la scrive già anche Calamaio.

Prima di tornare a Calamaio si può dare `forget` per togliere gli abbinamenti (facoltativo: Calamaio ignora
`nimble_bond` e la sua pulizia profonda lo conserva).

## Prova per l'utente

Serve: la scheda col cavo, il controller 1914 con le pile cariche, il PC sulla stessa rete Wi-Fi.
Controller aggiornato con l'app «Accessori Xbox» (serve il firmware 5.x per il Bluetooth LE).

1. **Associazione.** Accendi il controller col tasto Xbox, poi tieni premuto il **tasto di associazione**
   (piccolo, sul bordo in alto vicino alla porta USB) finché il logo Xbox **lampeggia veloce**.
   Sulla seriale: `[ble] visto … IN ASSOCIAZIONE`, poi `[ble] collego`, `[ble] sicurezza: cifrata 1, abbinata 1`,
   `[ble] PRONTO`. Il logo smette di lampeggiare e resta acceso.
   Se il controller era già associato a un altro dispositivo (console, telefono), dopo l'associazione qui
   va riassociato lì.
2. **Tasti.** Premi tutti i tasti uno per uno: A B X Y LB RB View Menu Xbox, gli stick premuti, Share, la croce
   nelle 8 direzioni, i due grilletti fino in fondo, gli stick in tutte le direzioni. Ogni cambio è una riga:
   ```
   [pad] t=12345678 A.......V....... croce=U--- L=+0,-100 R=+0,+0 LT=0 RT=1023
   ```
   Segni dei tasti in ordine: A B X Y LB RB V(iew) M(enu) G(uida/Xbox) LS RS S(hare); `.` = rilasciato.
   Stick da -100 a +100 (Y negativo = su), grilletti 0–1023. Le righe dovute solo agli stick sono al massimo
   20 al secondo. Se un tasto non compare o compare quello sbagliato: `raw on`, ripremi il tasto, annota la riga
   `[raw]` e il report map stampato al collegamento.
3. **Menu.** Croce su/giù → `[menu] su` / `[menu] giu`; A → `[menu] scegli`; B → `[menu] indietro`.
   **Tieni premuto A**: `A tenuto: inizio`, due volte `un secondo in piu`, a 3 s `A tenuto 3 s: CONFERMA (permesso)`.
   Rilasciato prima: `annullato`.
4. **Ritardo.** Muovi gli stick di continuo per 10 s. Ogni 10 s c'è una riga `[lat]`: notifiche al secondo e
   intervallo min/media/max fra una notifica e l'altra (dipende dall'intervallo di connessione stampato in
   `[ble] connessione: intervallo … ms`). Il controller manda dati solo quando qualcosa cambia: da fermo il
   numero scende, è normale. Si può provare un altro intervallo con `itv 6 6` (7,5 ms) o `itv 12 24`
   (15–30 ms); il controller può rifiutare.
5. **Ricollegamento.** Spegni il controller (tieni premuto Xbox 6 s) e riaccendilo col tasto Xbox: deve tornare
   `PRONTO` senza tasto di associazione. Poi premi il tasto RST della scheda: al riavvio `abbinati in NVS: 1`,
   accendi il controller → `PRONTO`.
6. **Wi-Fi insieme al controller.** Sul PC: `python3 tools/echo_server.py` (porta 7777; consenti la connessione
   se macOS lo chiede). Sulla seriale, una riga per volta:
   ```
   ssid NomeRete
   pass LaPassword
   wifi on
   host 192.168.1.42:7777
   ```
   (l'indirizzo del PC: `ipconfig getifaddr en0`). SSID e password non compaiono mai sulla seriale, solo la
   lunghezza. Ogni 5 s una riga `[tcp]`: ping inviati/risposti/persi e RTT. Lascia 1 minuto col controller
   fermo, poi 1 minuto muovendo gli stick di continuo, e confronta. Prova anche `ps max` (risparmio massimo,
   come Calamaio a batteria) e `ps nessuno` (come col cavo: con il Bluetooth acceso l'IDF potrebbe rifiutarlo
   o fermarsi, annota cosa succede).
7. **Memoria.** Ogni 5 s `[heap] … libera, minima`. Annota anche le righe `[heap] avvio`, `dopo NimBLE init`,
   `controller pronto`, `dopo il Wi-Fi`.

### Cosa annotare

- associazione riuscita? quanto tempo (`[ble] PRONTO: collegamento … ms`)? riga `sicurezza`;
- ricollegamento dopo spegnimento del controller e dopo RST della scheda: sì/no, quanti secondi;
- tasti mancanti o sbagliati (con la riga `[raw]`), report map (righe `[ble] report map`);
- `[lat]` muovendo gli stick: intervallo min/media/max, intervallo di connessione;
- `[tcp]` controller fermo vs stick in movimento: persi %, RTT media/max, `ps max`/`ps nessuno`;
- `[heap]` minima dopo qualche minuto con tutto acceso;
- disconnessioni inattese (`controller SCOLLEGATO, motivo 0x…`).

## Tornare al firmware Calamaio

Stesso comando con il firmware Calamaio 0.10.1 al posto dello spike:

```sh
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 ~/CalamaiOS/firmware/.pio/build/heltec213/firmware.bin
```

oppure dalla webapp (stesso layout: boot_app0 a 0xe000, firmware a 0x10000). La NVS di Calamaio non è stata
toccata: abbinamento col PC e Wi-Fi restano quelli di prima.

## Misure di build (2026-10-12)

- `pio run`: Flash 891 437 byte (26,7% di app0 da 3264 KB), RAM statica 51 292 byte.
- Stesso core con solo Wi-Fi + client TCP: Flash 693 825 byte, RAM statica 44 240 byte.
- Costo stimato di NimBLE + codice dello spike: **~198 KB di flash, ~7 KB di RAM statica**. La RAM dinamica
  dello stack (controller + host + buffer) si legge dal vivo nelle righe `[heap]`.

## Risultati dal vivo (2026-10-12, Heltec V1.2 + Xbox Series 1914)

- **Associazione**: riuscita al primo tentativo (Just Works, cifrata, chiave 16 byte). Il primo collegamento ha letto un report map troncato (22 byte) e ha scollegato; il ricollegamento immediato ha letto quello vero (283 byte), report d'ingresso id 1. Pronto in ~1,9 s.
- **Tasti**: A B X Y LB RB View Menu, croce, grilletti 0–1023, stick ±100: tutti corretti con il layout previsto.
- **Menu**: su/giù/scegli/indietro corretti; A tenuto 3 s = CONFERMA, rilasciato prima = annullato.
- **Ricollegamento** dopo spegnimento del controller: automatico, senza tasto di associazione, PRONTO in < 1 s. Il controller chiede intervallo 7,5 ms (accettato); al primo collegamento 15 ms.
- **Ritardo**: stick in movimento ~42 notifiche/s, intervallo medio ~24 ms, max ~75 ms; coda→stampa < 2,2 ms.
- **Wi-Fi + BLE** (risparmio Wi-Fi predefinito, ping TCP ogni 100 ms per ~2 minuti, controller fermo e poi stick in continuo): **0 persi su 1216**, RTT medio 28–125 ms, picchi fino a ~360 ms; nessuna disconnessione del controller.
- **RAM**: heap libera 311 KB all'avvio → 264 KB dopo NimBLE (−47 KB) → ~204 KB con Wi-Fi e TCP, minima 193 KB. Da verificare nel firmware principale (oggi ~180 KB statici).
- Non provati: `ps nessuno`/`ps max`, riavvio della scheda con controller abbinato.
