# fastink · SPIKE usa e getta

Esperimento separato dal firmware Calamaio. Domanda: il pannello della Heltec Wireless Paper V1.2
(E0213A367, 250×122, comandi SSD1680) si può pilotare molto più in fretta con una waveform scritta da noi?
Oggi il parziale di serie (OTP) impiega 644 ms e il completo 1540 ms. Obiettivo: 20–30 aggiornamenti al secondo.

Idea (dal video «GameBoy on E-Paper ESP32 at 30FPS», che riprende Paperboy di Zephray):

1. **Waveform corta**: la LUT del controller ha un solo gruppo con 1–2 fasi brevi invece di ~17.
2. **Budget per pixel**: il firmware tiene per ogni pixel un livello stimato 0..N (0 bianco, N nero). A ogni
   tick un pixel che non è ancora al bersaglio riceve **un** impulso verso il bersaglio; quando ci arriva non
   riceve più nulla. Una transizione completa costa N impulsi (il video dice 6–8).

Il codice è un driver SPI diretto (niente heltec-eink-modules), con menu seriale e una demo automatica.

## Cosa è verificato e cosa no

| Punto | Stato | Fonte |
|---|---|---|
| `0x32` prende 153 byte (VS 5×12, TP/SR/RP 12×7, FR 6, XON 3) | verificato | SSD1680 datasheet Rev 0.14, §6.6–6.7, comando 0x32 |
| Ordine dei 7 byte di gruppo: TP A, TP B, SR AB, TP C, TP D, SR CD, RP | verificato su codice | LUT parziali di GxEPD2_213_BN (`heltec-eink-modules/.../DEPG0213BNS800/lut_partial.inc`) e Waveshare `EPD_2in13_V3.c` |
| LUT scelta = (bit RAM 0x26 << 1) \| bit RAM 0x24 | verificato | datasheet tabella 6-4/6-5 |
| VSH1 = verso il nero, VSL = verso il bianco | verificato su codice | stesse LUT parziali (LUT2 «bianco→nero» = 0x40 VSH1, LUT1 «nero→bianco» = 0x80 VSL) |
| `0x22`: F7 completo OTP, FF parziale OTP, C7/CF LUT dal registro, B1 solo caricamento WS da OTP, C0 accende l'analogico, 04/0C solo display, 03 spegne | verificato | datasheet pag. 26; C0 + 0C è la sequenza di Waveshare 2in13_V3 |
| `0x37` = 00 80 03 0E, bordo 0x01 (completo) e 0x80 (parziale) | copiati | heltec-eink-modules 4.6.0, `E0213A367/mode.cpp` |
| Il controller della E0213A367 è proprio un SSD1680 | **non verificato** | la libreria lo tratta da SSD168x; il comando `R` prova a leggere i registri |
| Durata di un fotogramma per ogni FR | **non nota** | il datasheet dà solo «25–200 Hz»; si misura col comando `t` / `T` |
| Polarità VSH1/VSL su questo pannello | da confermare a occhio | se tutto esce al contrario: comando `x` |
| Tempo SPI per tick | stimato 6,4 ms (2 × 4000 byte a 10 MHz) | il firmware stampa quello vero (`spi=`) |

La variante di LUT da **227 byte** non è implementata: quel formato (controller più grandi della famiglia) ha
un'altra disposizione e mandarlo a un SSD1680 non avrebbe senso. La variante **159** (`L159`) accoda a `0x32`
i 6 byte di Waveshare 2in13_V3 (EOPT 22, VGH 17, VSH1 41, VSH2 00, VSL 32, VCOM 36), come fa il codice di
alcuni fornitori; su un SSD1680 vero quei 6 byte in più dovrebbero essere ignorati.

## Sicurezza del pannello (perché è fatto così)

- **Tensioni**: per default (`v0`) non si scrive nessuna tensione. Prima di usare la LUT veloce il firmware fa
  caricare al controller la waveform di fabbrica dall'OTP (`0x22 = B1`: nessun disegno, solo caricamento):
  così VGH, VSH1, VSL, VCOM ed EOPT sono quelli che il pannello usa di serie. Poi si sovrascrive **solo** la
  parte di tempi e fasi (`0x32`, 153 byte). Senza quel caricamento il VCOM resterebbe al valore di accensione
  (00h, che il datasheet segna come non valido). `v1` usa i valori di Waveshare 2in13_V3 (pannello simile, non
  lo stesso): solo per confronto.
- **VSH2 mai usato**, LUT4 (VCOM) sempre a DCVCOM, nessun «tutti i gate accesi» (XON = 0).
- **Bilanciamento DC**: un pixel riceve impulsi solo verso il bersaglio e il livello stimato resta fra 0 e N.
  La carica netta di un pixel (impulsi neri − impulsi bianchi) è quindi sempre fra −N e +N: un pixel fermo
  non viene mai spinto all'infinito nella stessa direzione, e un andata-e-ritorno completo vale zero. I test
  `test_dc_bounded` e `test_reversal_is_symmetric` lo controllano su 2000 tick casuali.
- I pixel fermi stanno a VSS (come le fasi di attesa di ogni waveform parziale): restano soggetti al piccolo
  scarto del VCOM, come col parziale di serie.
- **Pulizia**: ogni sessione finisce con un refresh completo di serie (`f`/`z`, e da sola la demo
  automatica). Dopo ogni comando il pannello va in sonno profondo (`0x10 01`) e Vext si spegne. Se resta
  un'immagine «veloce» sul pannello per 5 minuti, il firmware fa da solo un refresh completo.
- Le misure di `t`/`T` usano piani «tutti fermi»: nessun pixel viene spinto. **Lo schermo non deve cambiare**;
  se cambia, la mappa RAM → LUT non è quella attesa: fermarsi e annotarlo.
- Il waveform non tiene conto della temperatura: provare a temperatura ambiente.

## Come si flasha (per il coordinatore)

Solo l'app. Tabella partizioni e bootloader dello spike sono **identici byte per byte** a quelli del firmware
Calamaio (`partitions.bin` SHA-256 `1d9cca96…c80c` in entrambi; stesso `bootloader.bin`):
nvs 0x9000 (20 KB), otadata 0xe000, app0 0x10000, app1 0x340000, spiffs 0x670000, coredump 0x7f0000.
Si scrivono solo **otadata** (`boot_app0.bin`, così parte app0 anche se l'ultimo aggiornamento OTA aveva
scelto app1) e **app0**. NVS (chiavi, abbinamento, Wi-Fi), spiffs (avatar) e coredump restano intatti.
eFuse: lo spike non le legge e non le scrive. Baud 115200 (più alto dà errori con questo CP2102).

```sh
cd ~/CalamaiOS/spikes/fastink
pio run                      # build
pio test -e native           # test della parte pura

pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 ~/CalamaiOS/spikes/fastink/.pio/build/heltec213/firmware.bin

pio device monitor -b 115200 -p /dev/cu.usbserial-0001   # menu: h
```

Non usare `pio run -t upload`: riscriverebbe anche bootloader e tabella (identici, ma inutile).

### Tornare al firmware Calamaio

Stesso comando con il firmware Calamaio 0.10.1 al posto dello spike:

```sh
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --port /dev/cu.usbserial-0001 --baud 115200 \
  --before default_reset --after hard_reset write_flash \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 ~/CalamaiOS/firmware/.pio/build/heltec213/firmware.bin
```

oppure dalla webapp (stesso layout: boot_app0 a 0xe000, firmware a 0x10000). La NVS non è stata toccata,
quindi abbinamento e Wi-Fi restano quelli di prima. Prima di tornare conviene lanciare `z` (o lasciar finire la
demo) così il pannello è pulito.

## Prova per l'utente

Serve: la scheda col cavo, il telefono per foto e un video (meglio al rallentatore se c'è).

1. All'accensione compare una schermata «FASTINK · SPIKE usa e getta» (refresh di serie).
2. Premere **PRG** una volta: parte la demo automatica (circa 2–4 minuti). Si ferma con PRG o un tasto.
3. Cosa succede e cosa guardare:
   - **Calibrazione** (subito, invisibile): lo schermo **non deve cambiare**. Se cambia, annotarlo.
   - Per ogni prova compare per 2 s un cartello «1/7 QUADRATO · F1 P2 N6 FR2», poi l'animazione per 100
     fotogrammi.
   - **Quadrato** (due impostazioni), **barra**, **scacchi** che lampeggiano, **testo «Calamaio» che scorre**.
     Guardare: fluidità (scatti al secondo), lunghezza della scia dietro al quadrato, se i neri sono neri pieni
     o grigi, se lo sfondo resta bianco o si ingrigisce col tempo, se il testo si legge mentre scorre.
   - **OTP**: lo stesso quadrato col parziale di serie, 10 fotogrammi: è il riferimento lento.
   - **Fantasmi**: scritta GHOST e quadrato, poi metà sinistra nera, poi metà sinistra bianca e scacchiera a
     destra. Resta ferma 15 s: **fotografare** il pannello dritto, con luce uniforme. La metà sinistra dovrebbe
     essere bianca pulita: si vede ancora la scritta o il rettangolo?
   - Alla fine refresh completo di serie (lampeggio lungo) e schermo bianco.
4. Fare un **video** di tutta la demo. Foto: il fantasma finale e, se possibile, un fotogramma a metà del
   quadrato e della scacchiera.
5. Il registro seriale (se il cavo è sul PC) stampa i tempi di ogni fotogramma e un riepilogo finale.

### Comandi (seriale, invio per confermare)

| Comando | Effetto |
|---|---|
| `h` `s` | aiuto, parametri attuali |
| `f` / `z` | refresh completo di serie (bianco) / fine sessione |
| `o` | un parziale di serie, stampa i ms (riferimento: 644 ms) |
| `t` / `T` | durata del fotogramma senza spingere nulla (TP 1..32 / FR 0..7) |
| `l<n>` `p<n>` `q<n>` | fotogrammi per fase, fasi per tick, pausa a VSS dopo la fase |
| `b<n>` `r<n>` `k<n>` | budget N, frame rate FR, SPI in MHz |
| `w0`/`w1` | analogico acceso e spento a ogni tick (C7) / sempre acceso (C0 + 04) |
| `d1`/`d2` | modo di display 1 o 2 |
| `v0`/`v1` | tensioni da OTP / Waveshare V3 |
| `L153`/`L159` | lunghezza della LUT |
| `x` `g` `n<n>` `O` | inverti polarità, 250 gate, fotogrammi per animazione, animazioni col parziale di serie |
| `a1`..`a5` | quadrato, barra, scacchi, testo, fantasmi |
| `A` | demo automatica |
| `R` | lettura registri 0x2D/0x2E (sperimentale) |

Ordine consigliato a mano: `t`, `T`, poi `a1` con `w0` e `w1`, poi variare `l`, `p`, `b`, `r`; chiudere con `z`.

## Risultati da compilare

| Data | Prova | F | P | N | FR | w | ms/fotogramma | fps | Scia / grigio / fantasma (1–5) | Note |
|---|---|---|---|---|---|---|---|---|---|---|
| | calibrazione `t` | – | – | – | 2 | 0 | fotogramma = … ms, fisso = … ms | – | lo schermo è rimasto fermo? | |
| | quadrato | 1 | 2 | 6 | 2 | 0 | | | | |
| | quadrato | 2 | 1 | 6 | 2 | 0 | | | | |
| | barra | 1 | 2 | 8 | 2 | 0 | | | | |
| | scacchi | 1 | 2 | 6 | 2 | 0 | | | | |
| | testo | 1 | 1 | 8 | 2 | 0 | | | | |
| | OTP parziale | – | – | – | – | – | | | | riferimento 644 ms |
| | fantasmi | 1 | 2 | 6 | 2 | 0 | | | | foto: |
| | quadrato `w1` | 1 | 2 | 6 | 2 | 1 | | | | |

## File

- `src/engine.*`: motore a budget e codifica dei piani (puro, testato).
- `src/lut.*`: costruzione della LUT 153/159 (puro, testato).
- `src/canvas.*`: disegno nel formato della RAM, carattere 5×7 (puro, testato).
- `src/panel.*`: driver SPI diretto; `src/main.cpp`: menu, animazioni, demo.
- `test/test_engine`: 9 test (`pio test -e native`).

## Risultati dal vivo (2026-10-12, Heltec V1.2)

- **Correzione**: il controller usa Y da **1 byte** in 0x45/0x4F (come heltec-eink-modules), non 2 come SSD1680. Con 2 byte la finestra era sbagliata e lo schermo mostrava rumore.
- La LUT da 153 byte nel registro 0x32 viene accettata: i tempi scalano con TP e FR come previsto, polarità giusta (VSH1 = nero), la calibrazione senza spinta non cambia lo schermo.
- Fotogramma: ~19,8 ms a FR2, ~5,7 ms a FR7. Costo fisso per aggiornamento: ~209 ms con `w0` (analogico acceso/spento a ogni tick), **~40 ms con `w1`**.
- SPI 20 MHz: 3,6 ms per due piani.

| Impostazione | fps | Giudizio dell'utente |
|---|---|---|
| F1 P2 N6 FR2 w0 (demo) | 3,9 | funziona, lento, scia del percorso |
| F1 P1 N6 FR7 w1 k20 (A) | 20,3 | scia evidente |
| F2 P1 N6 FR7 w1 k20 (B) | 18,2 | scia evidente |
| F4 P1 N6 FR7 w1 k20 (C) | 15,1 | testo più leggibile, scia evidente |
| F4 P1 N12/N15 FR7 w1 k20 | 15,1 | scia ancora presente |
| **F8 P1 N10 FR7 w1 k20 (C3)** | 11,2 | **la migliore**: il quadrato lascia solo i bordi, non il riempimento |

Conclusione: fattibile. Da fare nel driver vero: spinte extra verso il bianco per i vicini dei pixel che cambiano (frange ai bordi), pulizia quando l'immagine si ferma, pulizia periodica in gioco; budget oltre 15; ridurre il costo fisso di ~40 ms (valori di 0x22, gruppi della LUT).
