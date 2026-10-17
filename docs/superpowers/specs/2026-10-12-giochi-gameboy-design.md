# Giochi Game Boy con controller — design

Data: 2026-10-12 · Stato: prove 1 e 2 superate (2026-10-12); decisioni dell'utente del 2026-10-12 sul menu «Gioco» e sul driver veloce applicate (contratto: `docs/superpowers/plans/2026-10-12-giochi-contratti.md`)

## Scopo

Giocare ai giochi del Game Boy classico sulla Heltec Wireless Paper V1.2 con un controller Bluetooth (Xbox Series X|S, modello 1914). Il controller, quando è collegato, serve anche per i menu di Calamaio. Il driver veloce del pannello serve **solo alla partita**. Ispirazione: video «GameBoy on E-Paper ESP32 at 30FPS» (forma d'onda accorciata + budget di impulsi per pixel, sulla scia di Paperboy di Zephray).

Decisioni dell'utente (2026-10-12): emulatore **sulla scheda** (via A: ROM sul PC, inviate dal servizio); **immagine intera** rimpicciolita in un quadrato; scelta del gioco **dalla scheda e dalla webapp**; **solo Game Boy classico**; controller anche per i menu.

## Vincoli della scheda

- Schermo 250×122 contro 160×144 del Game Boy: l'immagine si riduce in proporzione a circa **136×122**, centrata; le bande laterali (~57 px) ospitano batteria e indicatori. Le righe e colonne in eccesso si fondono (≈ 2 in 1 ogni 7): con i grigi si ammorbidiscono, senza grigi diventano retinatura.
- ESP32-S3: **solo Bluetooth LE** (niente classico). Il 1914 ha BLE di serie; va tenuto aggiornato con l'app «Accessori Xbox».
- Nessun altoparlante: niente audio.
- Senza PSRAM [stimato, da verificare]: ROM letta dalla flash mappata in memoria, non copiata in RAM.

## 1. Sulla scheda

- **Driver veloce** (componente nuovo, usato **solo durante la partita**: tutte le altre schermate di Calamaio, pausa del gioco compresa, restano su heltec-eink-modules con la regola dei refresh della 0.10.2): forma d'onda corta caricata nel controller del pannello (famiglia SSD168x, registro LUT), stato per pixel con budget di impulsi, codifica delle direzioni con la coppia RAM nuova/vecchia (0x24/0x26). Grigi a 4 livelli solo se la prova li rende stabili; altrimenti retinatura. Tensioni del pannello non modificate.
- **Emulatore**: Peanut-GB (Game Boy classico, MBC1/2/3/5), ROM nell'area flash dedicata, RAM della cartuccia salvata in flash.
- **Controller**: Bluetooth LE (NimBLE-Arduino 2.5.1, scelta nella prova 2), associato solo da Menu › Gioco › Controller › «Associa controller»; un solo controller associato; si ricollega da solo. Il Bluetooth si accende con «Associa controller» e si spegne con «Dimentica controller».
- **Menu col controller**: croce su/giù = sposta, **A** = scegli, **B** = indietro; il tasto PRG resta com'è. **Permessi**: risposta anche dal controller, ma con la stessa conferma «tieni premuto» con conto alla rovescia (A tenuto premuto); una pressione singola non approva mai. Le regole della sezione 7 di ISTRUZIONI-SVILUPPO restano invariate.
- **Menu › Gioco** (nel firmware non c'è un menu Impostazioni): voce del Menu sempre presente, anche nel simulatore. Dentro: «Controller» (stato, «Associa controller», «Dimentica controller») e «ROM» (elenco delle ROM del PC da `gb-list` e gioco già sulla scheda, sempre visibile; scegliere un gioco senza controller collegato mostra «Collega il controller per giocare» e non parte), solo sulla scheda vera; «Alto o basso», il minigioco esistente spostato qui da Menu › Strumenti.
- **In gioco**: Start+Select = menu di pausa (riprendi, salva ed esci, grigi/puntini se disponibili). Una richiesta di permesso mette in pausa e si mostra subito.

## 2. Sul PC

- **Cartella giochi** nei dati del servizio (`<dati>/calamaio/games/`); la webapp carica, elenca, cancella le ROM e avvia un gioco sulla scheda.
- **Invio**: solo se la scheda non ha già quella ROM (confronto per hash); a blocchi con ripresa; la scheda avvia solo una ROM completa e verificata (CRC/hash).
- **Salvataggi**: copiati dalla scheda al PC a ogni connessione; ultime 3 copie per gioco, ripristinabili dalla webapp.
- **Diritti**: si caricano solo file forniti dall'utente (cartucce proprie, homebrew); nessuno scaricamento da internet.
- **Protocollo**: messaggi nuovi facoltativi e compatibili (elenco giochi, avvio, ROM con ripresa, salvataggi), definiti nel contratto dei piani e in PROTOCOLLO.md «Giochi»; HTTP sulla porta dei display (8766).

## 3. Memoria

Nuova tabella delle partizioni: i due spazi app passano da 3264 KB a circa 2,5 MB ciascuno (firmware oggi 1,7 MB, stimato < 2,5 MB con emulatore e BLE) e nasce un'area **rom** da 2 MB (basta per Pokémon Oro/Cristallo). NVS resta a 0x9000 con le stesse dimensioni (chiavi e abbinamento intatti). Richiede **un flash via cavo**, con backup e conferma.

## 4. Batteria, rete, imprevisti

- In gioco: Wi-Fi acceso in risparmio (i permessi arrivano), Riposo e sonno profondo sospesi, OTA rimandato a fine partita.
- Consumo stimato 100–130 mA (≈ 7–9 h con 1000 mAh) [stimato, da misurare].
- Batteria < 15%: avviso nella banda; < 5%: salva ed esce.
- Controller scollegato: pausa subito; 5 min senza tasti: pausa; 10 min in pausa: salva, esce, torna al Riposo.
- Pulizia: ridisegno completo all'apertura della pausa e all'uscita (il completo con cui il pannello torna a heltec-eink-modules); periodico in gioco ogni 10 minuti (impostabile, valore da tarare nella prima partita dal vivo).
- ROM interrotta: ripresa; mai avvio di una ROM incompleta. Cartuccia non supportata: «Gioco non supportato». PC irraggiungibile: si gioca il gioco presente, salvataggi copiati al ritorno.

## 4bis. Esito delle prove (2026-10-12)

- **Driver veloce** (`spikes/fastink/README.md`): il controller accetta la LUT nel registro 0x32 (formato da 153 byte), ma usa Y da 1 byte negli indirizzi. Con l'analogico sempre acceso il costo fisso è ~40 ms per aggiornamento: 11–20 immagini/s. Scelta dell'utente: impulso lungo (F8, N10, ~11 immagini/s), l'unico in cui il quadrato lascia solo i bordi. Da risolvere nel driver vero: frange ai bordi (spinte ai vicini, pulizie), costo fisso, budget oltre 15. La soglia di 15 immagini/s va rivista: il compromesso pulizia/velocità si regola nel piano.
- **Controller** (`spikes/blepad/README.md`): 1914 associato e ricollegato da solo; tutti i tasti corretti; ~24 ms tra notifiche; Wi-Fi e BLE insieme senza perdite (0 su 1216 ping); NimBLE costa ~47 KB di heap e ~200 KB di flash.

## 5. Prove

1. **Driver veloce** (spike in `spikes/fastink/`): immagini al secondo e scie con forme d'onda diverse. Soglia per proseguire: ≥ ~15 immagini/s con scie accettabili.
2. **Controller**: associazione del 1914, ritardo dei tasti, convivenza BLE + Wi-Fi senza perdere il PC.
3. **Automatiche**: emulatore su ROM di test homebrew (correttezza della CPU), riduzione dell'immagine, mappatura tasti → azioni di menu (incluso «tieni premuto A»), invio ROM con interruzioni, salvataggi a 3 copie.
4. **Dal vivo**: associazione, menu col controller, gioco da webapp e da scheda, permesso durante la partita, salvataggio che sopravvive al cambio gioco, consumo in un'ora.

## Fuori da questa specifica

Game Boy Color, audio, più controller, streaming delle immagini dal PC (via B), giochi su CrowPanel.
