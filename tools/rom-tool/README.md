# Strumento ROM di Calamaio OS

Legge (backup) e scrive la flash delle schede ESP32 / ESP32-S3 con una barra di
avanzamento vera. Usa `esptool` come libreria (API verificata sulla versione 5.5.0).

## Avvio

**Doppio clic su `avvia.command`.** La prima volta crea l'ambiente `.venv` qui
dentro, installa `esptool` e `pyserial` e apre la finestra. Le volte successive
parte subito. Se macOS lo blocca: tasto destro → Apri.

Dalla riga di comando (dopo il primo avvio):

```sh
.venv/bin/python rom_tool.py porte                 # elenca le porte, senza aprirle
.venv/bin/python rom_tool.py info                  # chip, MAC, flash
.venv/bin/python rom_tool.py read                  # backup completo in <chip>_<mac>_<data-ora>.bin
.venv/bin/python rom_tool.py read -o ~/backup/ --riprendi
.venv/bin/python rom_tool.py write file.bin --indirizzo 0x0
```

Opzioni comuni: `--porta /dev/cu.xxx` (altrimenti la sceglie da sola), `--baud`
(default **115200**), `--dimensione 8MB` (se la dimensione non viene rilevata).

## Cosa fa

- **Rilevamento**: riconosce i bridge CP210x, CH340/CH9102, FTDI e l'USB nativo
  Espressif; ignora Bluetooth, debug-console e porte non USB. Con una sola
  scheda la sceglie da sola, con più schede chiede quale.
- **Porta occupata**: prima di connettersi controlla con `lsof` che nessun altro
  programma stia usando la porta, e in quel caso si rifiuta di aprirla.
- **Lettura**: legge a blocchi da 256 KB con 5 tentativi per blocco; dopo un
  errore (per esempio "Invalid head of packet") si riconnette e rilegge solo quel
  blocco. I dati vanno in `file.bin.part` man mano; se il backup si interrompe si
  riprende con `--riprendi` (nella finestra lo chiede da sé). Alla fine scrive
  `file.bin.sha256`, verificabile con `shasum -a 256 -c file.bin.sha256`.
- **Scrittura** (distruttiva): mostra file, dimensione e indirizzo, rifiuta file
  più grandi della flash o indirizzi non allineati a 4 KB, e scrive solo dopo che
  hai digitato **SCRIVI**. Dopo la scrittura confronta l'MD5 calcolato dalla
  scheda con quello del file. Nella finestra controlla anche che la scheda sia
  ancora quella rilevata (stesso MAC).

## Velocità

A 115200 baud un backup da 8 MB richiede circa 12 minuti (stima: 8 388 608 byte
÷ 11 520 byte/s ≈ 728 s, senza contare l'overhead del protocollo). Velocità più
alte (460800, 921600) sono selezionabili ma su un CP2102 rumoroso danno errori
di pacchetto: il tentativo per blocco li assorbe, al prezzo di rileggere.

## Test

```sh
.venv/bin/python -m unittest discover -s tests -v
```

I test sostituiscono esptool con dei finti e non aprono nessuna porta seriale.
