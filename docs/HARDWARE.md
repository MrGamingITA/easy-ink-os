# Hardware

Solo dati verificati sulla scheda o su schema/wiki ufficiale. Il resto resta `TODO(hw)`.

## Heltec Wireless Paper

| Dato | Valore | Fonte |
|---|---|---|
| MCU | ESP32-S3 (QFN56) rev v0.2, dual core 240 MHz | `esptool flash-id`, 2026-10-11 |
| Flash | 8 MB integrata (GD, id c8/4017), quad | `esptool flash-id` |
| MAC | aa:bb:cc:dd:48:e8 | `esptool` |
| Bridge USB | Silicon Labs CP2102 (10c4:ea60), `/dev/cu.usbserial-0001` | `system_profiler` |
| Seriale | 115200 affidabile; 460800 e 921600 danno errori di pacchetto | prove di lettura |
| Firmware di fabbrica | stampa `ESP32ChipID=<MAC al contrario>` all'avvio | monitor seriale |
| Revisione scheda | **V1.2** | indicata dall'utente, 2026-10-11 |
| Pannello | E0213A367, 250×122 (memoria 128×250), comandi famiglia SSD168x | heltec-eink-modules 4.6.0, `class-aliases.h` |
| Pin pannello | MOSI 2, CLK 3, CS 4, DC 5, RST 6, BUSY 7 | heltec-eink-modules, `Platforms/WirelessPaper/WirelessPaper.h` |
| Alimentazione periferiche | Vext GPIO45, attiva BASSA | idem |
| Tasto PRG | GPIO0, attivo basso (pin RTC: può svegliare dal sonno) | idem; gesti verificati in M1 |
| LED | GPIO18 | idem |
| Batteria (ADC, partitore) | VBAT su GPIO20 (ADC2 canale 9) con partitore 1:2, abilitato da GPIO19 attivo BASSO; nessun segnale di carica verso l'MCU | vedi «Batteria» sotto |

### Batteria (M6, 2026-10-11)

Circuito (schema ufficiale `Wireless_Paper_V0.4.SchDoc`, 2023-08-25):

| Rete | Dove | Valore |
|---|---|---|
| `ADC_IN` | pin 26 del modulo = **GPIO20** (ADC2 canale 9) | VBAT → R21 10 kΩ → Q3 → `ADC_IN` → R26 10 kΩ → GND |
| `ADC_Ctrl` | pin 25 = **GPIO19** | gate di Q3 (P-MOSFET AO3401) attraverso R23 1 kΩ, pull-up R22 10 kΩ a 3,3 V: partitore **acceso con il livello BASSO** |
| Rapporto | R21 = R26 = 10 kΩ al 5 % | VBAT = 2 × V(`ADC_IN`); con le tolleranze fra 1,905 e 2,105 (20 / 10,5 e 21 / 9,5). Corrente a partitore acceso: VBAT / 20 kΩ ≈ 0,21 mA a 4,2 V |
| Carica | TP4054, `PROG` R14 2 kΩ | corrente di carica ≈ 1000 V / 2 kΩ = 500 mA con la formula usuale del TP4054 [non verificata sul datasheet] |
| Segnale di carica | `CHRG` del TP4054 va **solo** al LED arancione LED2; `VDD_5V` non arriva a nessun GPIO | lo stato di carica non si legge |
| Cavo USB (indiretto) | CP2102: `REGIN` (pin 7) e `VBUS` (pin 8) su `VDD_5V` = solo USB; `TXD` (pin 26) diretta a `U0RXD` = GPIO44, senza resistenze (la `U0TXD` dell'ESP passa da R2 499 Ω) | con il cavo il CP2102 è acceso e la sua TXD sta alta a riposo; senza cavo è spento |

Fonti:

1. Schema: <https://resource.heltec.cn/download/Wireless_Paper/Wireless_Paper_V0.4_Schematic_Diagram.pdf>. SHA-256 `282d9ed38a76e2f1b27711e003f744492a307a178319a7e957110fd0aee2e730`, identico a `docs/Datasheets/WirelessPaperSchematic.pdf` di heltec-eink-modules 4.6.0. Reti lette sul foglio: blocco Q3/R21/R26/R22/R23, pin 24–26 dell'ESP32-S3FN8 (`LED`, `ADC_Ctrl`, `ADC_IN`), blocco TP4054.
2. Registro hardware Heltec: <https://docs.heltec.cn/en/node/esp32/wireless_paper/hardware_update_log.html>. Lo schema V0.4 è quello indicato per la serie; V1.1 cambia solo il pannello, V1.2 «E-ink screen model updated» e «Improved crystal oscillator stability». Nessuna modifica dichiarata al partitore.
3. Meshtastic (scheda in uso da anni, supporta V1.1, V1.1.1 e V1.2 con `einkDetect.h`): <https://github.com/meshtastic/firmware/blob/master/variants/esp32s3/heltec_wireless_paper/variant.h>, commit `a5419574`: `ADC_CTRL 19`, `BATTERY_PIN 20`, `ADC_CHANNEL ADC2_GPIO20_CHANNEL`, `ADC_MULTIPLIER 2`, `ADC_ATTENUATION ADC_ATTEN_DB_12`, `ADC_CTRL_ENABLED LOW`. Uguale in `heltec_wireless_paper_v1/variant.h` (V1.0).
4. Curva: `OCV_ARRAY` predefinito di Meshtastic, `src/power.h`: 4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100 mV per 100 … 0 % a passi del 10 %. È una curva a vuoto: sotto carico (Wi-Fi) la tensione è più bassa e la percentuale esce un po' sotto il vero.
5. ADC2 e Wi-Fi: <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/adc.html> («ADC Limitations»): con il Wi-Fi acceso `adc2_get_raw()` può fallire; va controllato il codice di ritorno. Attenuazione 12 dB: 0–3100 mV al pin (la batteria piena dà ~2100 mV dopo il partitore).

Cosa **non** dice nessuna fonte: uno schema marcato V1.2. Le fonti concordano (schema ufficiale, registro delle revisioni, firmware di terzi provato sulla V1.2), quindi la lettura è implementata; la conferma sul pezzo vero resta da fare (sotto).

Fonti scartate: `variants/heltec_wireless_paper/pins_arduino.h` di arduino-esp32 e del pacchetto Heltec non definiscono pin di batteria (e dichiarano `LED_BUILTIN = 35`, copiato da un'altra scheda); gli esempi Heltec `Wireless_Paper_*_FactoryTest` non leggono la batteria (`battery_levl` dichiarata e mai usata).

Firmware (`hw/battery.cpp`, `core/battery.cpp`, `main.cpp › readBattery`):

- prima lettura in `setup()` prima del Wi-Fi (ADC2 libero), poi ogni 60 s nel loop; `display.update()` è bloccante, quindi la lettura non cade mai durante un refresh;
- GPIO19 BASSO, 10 ms di assestamento, 16 campioni `adc2_get_raw` (si scartano quelli falliti), conversione `esp_adc_cal_raw_to_voltage` (ADC2, 12 dB, 12 bit), GPIO19 ALTO;
- lettura scartata se meno di 8 campioni su 16 sono validi o se VBAT è fuori da 2600–4500 mV: resta il valore precedente;
- percentuale per interpolazione lineare sulla curva; `Ui::battery()` toglie la scarica finta di `slowTick()`;
- **Cavo USB** (`hw/usb.cpp`, `core/usb.h`, `main.cpp › readUsb`): al boot e ogni 10 s si legge GPIO44 dal registro d'ingresso (la UART non si tocca), con un pull-down messo per ~0,5 ms al posto dei pull del pad e poi il registro IO_MUX rimesso com'era. 8 campioni a 60 µs: uno alto = cavo (solo la TXD del CP2102 acceso può alzare la linea contro il pull-down); tutti bassi = no, ma vale dopo due letture di fila (un byte di zeri dal PC può dare tutti bassi); ingresso del pad spento = sconosciuto. Il cambio va in `sys.usb` con il toast «In carica» / «A batteria», nel log «[usb] cavo collegato/scollegato». Diagnosi: per i primi 120 s, ogni 2 s, una riga `[usb] diagnosi` con i campioni e i pull trovati sul pad.
- Perché il pull-down: con UART0 sul suo pin predefinito l'IDF 4.4.7 passa da `uart_try_set_iomux_pin` e non imposta pull (solo il percorso con la matrice GPIO fa `gpio_set_pull_mode(PULLUP)`, `components/driver/uart.c › uart_set_pin`); i pull lasciati dal reset o dalla ROM non li conosco, la diagnosi li stampa.
- Con il cavo la tensione è quella del caricatore (fino a 4,2 V): la percentuale non è la carica vera. Lo stato «in carica» del TP4054 non si legge: il fulmine vuol dire «cavo collegato».

Da verificare sulla scheda (TODO(hw)):

- cavo USB: staccare e riattaccare il cavo nei primi 120 s e leggere le righe `[usb] diagnosi` (attesi: alto 8/8 con il cavo, 0/8 senza); con un alimentatore senza dati il CP2102 non viene enumerato e la TXD potrebbe non stare alta;

- confronto con il multimetro: VBAT ai capi del connettore JST contro la riga `[batteria] … mV` della seriale, a batteria e senza USB, in due punti (carica e quasi scarica). Scarto atteso entro il 5 % delle resistenze più l'errore dell'ADC calibrato;
- quante letture vengono scartate con il Wi-Fi acceso (riga `campioni validi`);
- GPIO19 ALTO spegne davvero Q3: il gate sta a 3,3 V e la sorgente a VBAT, quindi con la batteria piena V_GS ≈ −0,9 V, vicino alla soglia dell'AO3401 (valore di soglia non verificato sul datasheet). Misura: corrente dalla batteria in sonno profondo con GPIO19 alto e con GPIO19 basso (differenza attesa ~0,2 mA se il partitore resta acceso).

### Refresh misurati (M1, firmware 2026-10-11, temperatura ambiente)

| Operazione | Tempo | Note |
|---|---|---|
| Pulizia iniziale (`clear()`) | 1626 ms | all'avvio |
| Refresh completo | 1540 ms | `fastmodeOff()` + `update()` |
| Refresh parziale | 644 ms | `fastmodeOn()` (doppio passaggio), costante su 29 misure |

Conseguenza: al massimo ~1,5 aggiornamenti al secondo. Il profilo del simulatore (2 agg./s) è un po' ottimista: vedi DECISIONI.

### eFuse (lettura 2026-10-11, `espefuse summary`, sola lettura)

- 6 spazi chiave (BLOCK_KEY0–5): tutti liberi, scopo USER, contenuto a zero, R/W.
- Cifratura della flash spenta (SPI_BOOT_CRYPT_CNT 0b000), secure boot spento, nessun RD_DIS/WR_DIS, modalità download attiva.
- Nota strumenti: `espefuse` di esptool 5.5 non parte con `bitstring` ≥ 4.3; si usa `uvx --from esptool --with "bitstring<4.3" espefuse …`.

### eFuse dopo «Proteggi le chiavi» (2026-10-11 17:50, firmware 0.5.0)

- BLOCK_KEY0: scopo HMAC_UP (0x8), protetto in lettura (RD_DIS bit 0) e in scrittura; `espefuse summary` lo mostra come `?? … -/-`. Gli altri 5 spazi restano liberi.
- Al riavvio la scheda decifra Wi-Fi e chiave di abbinamento con la chiave del chip (Wi-Fi e autenticazione riuscite).

### Alimentazione e carica (schema ufficiale V0.4, resource.heltec.cn, 2023/8/25)

- Caricatore TP4054 (U4): VCC = VDD_5V, PROG = R14 2 kΩ a massa (~500 mA), CHRG solo al LED arancione. Nessun collegamento con l'MCU: il firmware non può fermare o limitare la carica. Termina da solo a 4,2 V e riparte sotto ~4,05 V.
- Selettore di alimentazione: Q1 AO3401 + D2 1N5819. Con la USB la scheda va da VDD_5V e la batteria è staccata dal carico (viene solo caricata); senza USB Q1 conduce e alimenta il regolatore CE6260B33M dalla batteria.
- Limite all'80%: possibile solo con una modifica hardware (MOSFET in serie a R14 pilotato da un GPIO; da confermare sul datasheet TP4054). L'utente ha scelto di non farla (2026-10-11).
- CP2102 alimentato solo da VDD_5V (VBUS, REGIN): base per rilevare il cavo dalla linea U0RXD.

### Driver veloce (firmware), prova dal vivo del 2026-10-12

Scheda Heltec V1.2, env `fastink-demo` (commit d753d67), cartello e demo con l'utente. Valori dei profili invariati dal Task 1. Heap libera dopo `begin()`: 313928 B (Pulito), 313664 B (Fluido), nell'app di prova, non in Calamaio.

| Profilo | N F P FR | bordi | pulizia | immagini/s | tick medio / massimo | tick per fermarsi | voto dell'utente |
|---|---|---|---|---|---|---|---|
| Pulito | 10 8 1 7 | 2 | 2 | 9,7 (quadrato, 150 immagini in 15422 ms), 9,8 (testo), 9,8 (insieme) | 85,3 / 85,4 ms | 13 | 4/5 |
| Fluido | 6 4 1 7 | 1 | 2 | 12,5–12,6 | 62,6 / 62,6 ms | 8 | 3/5 |

- Registro 0x22 = 04 (profilo Pulito).
- Pulito lascia molti meno residui del Fluido; l'utente lo preferisce. Nessun giro di regolazione: i valori restano quelli del Task 1.
- Revisione del video: dietro al quadrato lo sfondo resta pulito col Pulito (nessuna scia, a differenza della prova preliminare); immagini ferme pulite; Fluido lascia residuo/sfocatura attorno agli oggetti in movimento.
- Passaggio alla libreria: riuscito. I cartelli disegnati dopo il driver veloce («FINE») sono puliti, senza fantasmi, e la riconsegna alla libreria è andata a buon fine.
- Problema noto (solo demo): la scritta «Calamaio» scorrevole appare «?alamaio». `drawText` a dimensione 26, peso 700 non ha il glifo «C» maiuscolo nel sottoinsieme di font incorporato. Non corretto qui; per il piano B i testi delle schermate di gioco devono usare dimensioni e pesi i cui glifi esistono.

#### Costo fisso per 0x22

Prova dal vivo del 2026-10-12 (22:33–22:37), env `fastink-demo` (d753d67), profilo Pulito N10 F8 P1 FR7. `t` = media di 10 tick senza disegno; `q` = quadrato che rimbalza, 150 immagini. Bit di 0x22: 7 clock, 6 analogico, 3 modo 2, 2 display.

| 0x22 | t (ms) | q (immagini/s) | tick medio / massimo (ms) | schermo durante t |
|---|---|---|---|---|
| 04 | 85,2 | 9,8 | 85,1 / 85,2 | invariato |
| 0C | 85,1 | 9,8 | 85,0 / 85,1 | invariato |
| C4 | 175,2 | 5,2 | 175,2 / 175,2 | invariato |
| CC | 175,2 | 5,2 | 175,2 / 175,3 | invariato |
| 84 | 85,1 | 9,8 | 85,1 / 85,1 | invariato |

- Nessun blocco di BUSY; l'utente: niente rumore né inversioni, la variante 04 è la migliore.
- `t` è il tick intero a F8 senza disegno: 8 immagini × ~5,7 ms ≈ 45 ms + ~40 ms fissi, coerente con la misura dello spike.
- Accendere e spegnere clock e analogico a ogni tick (C4, CC) raddoppia il costo (~+90 ms). 0C e 84 non guadagnano niente.
