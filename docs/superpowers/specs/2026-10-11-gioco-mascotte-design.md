# Gioco della mascotte — design

Data: 2026-10-11 · Stato: da rivedere con l'utente

## Scopo

Rendere più divertente e motivante il lavoro con Claude Code (scelta B dell'utente): la mascotte guadagna esperienza, sale di livello, raggiunge traguardi e sblocca accessori da indossare. Il gioco è **opzionale e spento di default**. Quando il catalogo degli accessori è esaurito, a ogni nuovo traguardo Claude propone di creare con un subagente un accessorio speciale, che arriva sulla scheda **senza riflashare**.

Principio: gioco leggero e gentile, mai un controllo né una gara di produttività. Nessun incentivo a comportamenti sbagliati (approvare permessi in fretta, spendere di più, lavorare senza pause o di notte).

## 1. Accessori

### 1.1 Formato `calamaio-accessory` v1 (JSON, `<id>.accessory.json`)

- `id` (kebab-case, ≤ 24), `name` (≤ 24 byte), `author`, `description` (≤ 120), `special` (bool: creato come premio speciale), `version: 1`.
- `w` ≤ 14, `h` ≤ 10, `frames` 1 o 2 (sincronizzati con il fotogramma della mascotte: `f mod frames`).
- `anchor`: uno tra `head-top`, `eyes`, `mouth`, `neck`, `hand`; `origin`: punto dell'accessorio (x, y) che va sul punto di aggancio.
- `pixels`: per fotogramma, `h` stringhe di `w` simboli:
  - `.` trasparente, `#` inchiostro, `+` carta,
  - `x` **inverso**: il colore opposto a quello già disegnato sotto (così un cappello è bianco sul corpo nero e nero sullo sfondo).
- `outline` (bool): contorno automatico di 1 punto color carta dove l'accessorio tocca l'inchiostro della mascotte.
- `hidePoses`: pose in cui non si disegna (es. occhiali in `error`, `idle`, `tired`).
- `perAvatar`: ritocchi facoltativi per id avatar: `{dx, dy}` oppure `pixels` sostitutivi.
- Binario compatto `CACC` v1 per il display (2 bit per simbolo, intestazione con versione, dimensioni, ancora, CRC-32); ≤ 1 KB.

### 1.2 Punti di aggancio negli avatar

- Estensione compatibile del formato avatar: campo facoltativo `anchors` = per posa e fotogramma, le coordinate di `head-top`, `eyes`, `mouth`, `neck`, `hand` (o `null` se il punto non esiste in quella posa). Nel CAVT un blocco facoltativo in coda (5 punti × pose × fotogrammi × 2 byte), segnalato da un bit nell'intestazione; i lettori senza supporto lo ignorano.
- Gli ancoraggi dei 4 avatar esistenti si ricavano con uno strumento semiautomatico (`tools/avatar anchors`) e si rivedono a occhio sul foglio di anteprima.
- Un avatar senza `anchors`: l'accessorio non si disegna (mai posizioni inventate).

### 1.3 Disegno

Ordine: mascotte, poi contorno (se `outline`), poi accessorio con `x` calcolato sui pixel già disegnati. Firmware e simulatore usano la stessa funzione `drawAccessory`, verificata pixel per pixel come gli avatar.

### 1.4 Validazione (`tools/avatar accessory validate|preview`)

Rifiuta: fuori dal riquadro 28×25 dell'avatar in qualunque posa/fotogramma; sovrapposizione all'area della nuvoletta (x 17..27, y 0..8) nelle pose che la usano; contrasto insufficiente (meno del 60% dei pixel dell'accessorio distinguibili dal fondo sottostante). Il foglio di anteprima mostra l'accessorio su tutti gli avatar × tutte le pose.

### 1.5 Catalogo iniziale (8)

cappellino, occhiali, sciarpa (2 fotogrammi), papillon, corona, cuffie, fiocco, berretto da mago.

## 2. Regole del gioco

- **XP**: task completato +10; turno finito con riepilogo +20; test da rosso a verde +15; sessione chiusa finita o archiviata +10; pausa saggia (≥ 10 min di inattività dopo ≥ 90 min di lavoro) +15; primo lavoro del giorno +5 × giorni di serie (max +35).
- **Mai punti** per: approvare permessi, costi o token, lavoro notturno, ore filate.
- **Energia** 0–100: scende col lavoro continuo, sale con le pause. Dopo 3 h senza pausa saggia la mascotte è «stanca» (posa `tired`, dove il gioco è acceso) e gli XP sono dimezzati fino alla pausa.
- **Tetto** di 300 XP al giorno (tarato con la simulazione di un mese: livello 2 il primo giorno, ~10 a trenta giorni; con 400 si arrivava al livello 3 il primo giorno).
- **Livelli**: XP necessari per passare dal livello n a n+1 = 100 × n^1,3 (arrotondato). Taratura: livello 2 in un giorno normale, livello 10 in circa un mese (verificata con una simulazione sulle sessioni catturate).
- **Traguardi** (~20, definiti in un file dati del servizio): prima sessione; 10/100/1000 task; 7 e 30 giorni di serie; 10 test verdi di fila; 3 sessioni contemporanee finite bene; una settimana con almeno una pausa saggia al giorno; …
- **Ricompense**: a ogni livello pari si sblocca un accessorio del catalogo (scelto dall'utente tra i bloccati, o il successivo in ordine se l'utente non sceglie entro la sessione); ogni traguardo fa una festa. Catalogo esaurito → a ogni nuovo traguardo «accessorio speciale disponibile».

### 2.1 Regole precisate dal motore (2026-10-11)

- La serie regge fino a 2 giorni di riposo (il fine settimana non la rompe).
- Dalle 00 alle 06 il lavoro non dà XP e non conta per contatori e serie (conta solo per l'energia).
- Una pausa oltre le 3 ore è fine lavoro, non pausa saggia.
- Energia: −25 per ora di lavoro, +120 per ora di pausa.
- «Settimana di pause sagge» = 5 giorni lavorativi di fila con almeno una pausa saggia.
- Una sola proposta speciale aperta alla volta.
- Gli eventi `test_ok`/`test_fail` (0 XP) servono a definire «verdi di fila».
- 21 traguardi in `service/internal/game/achievements.json`.

## 3. Dove si vede

- **Interruttore** «Gioco della mascotte» in Menu › Display (scheda) e nella webapp; impostazione remota `game` (bool). Spento: niente accessori, niente feste, niente posa stanca dal gioco; i progressi restano salvati.
- **Scheda**: accessorio indossato su ogni mascotte mostrata; voce di menu «La mascotte» (livello e barra XP, energia, ultimi 3 traguardi, «Accessorio»: scegli tra gli sbloccati o nessuno); festa di 3 s a ogni traguardo (non durante permesso aperto, silenzio, notturno, risparmio: solo toast).
- **Webapp**: pagina «Gioco» con livello, storico XP, traguardi, galleria accessori con anteprima su ogni avatar e «Indossa», pulsante «Crea accessorio speciale» quando disponibile.

## 4. La proposta di Claude

1. Catalogo esaurito + nuovo traguardo → stato «speciale disponibile» (una proposta per traguardo), toast sulla scheda «Nuovo accessorio speciale: chiedilo a Claude».
2. Al primo `UserPromptSubmit` di una sessione collegata, un hook sincrono (come `hook --context`) aggiunge a Claude il contesto: traguardo raggiunto, proponi con AskUserQuestion «Creo un accessorio nuovo? Sì / Più tardi / No». «Più tardi» = ripropone al prossimo prompt di un'altra giornata; «No» = chiude la proposta per quel traguardo.
3. «Sì» → Claude usa la skill `calamaio:accessorio`: subagente che disegna, valida, guarda il foglio di anteprima e corregge; poi `calamaio accessory add <file> --special` lo aggiunge e lo sblocca.
4. Nessuna sessione collegata: la proposta aspetta; la webapp mostra cosa chiedere a Claude.

## 5. Dati e protocollo

- **Servizio**: stato del gioco in `<dati>/calamaio/game.json` (XP, livello, energia, serie, traguardi con data, accessori sbloccati e indossato, proposta speciale). Libreria accessori: predefiniti incorporati + `<dati>/calamaio/accessories/`. Comandi `calamaio game status|on|off`, `calamaio accessory list|add|wear|show`.
- **Messaggi** (facoltativi, compatibili): servizio→display `game {on, level, xp, xpNext, energy, wear?:{id, crc}, celebrate?:{id, title}}` a ogni cambio; `accessory {id, crc, data}` su richiesta; display→servizio `accessory-need {ids}` e `accessory-wear {id|null}` (scelta dal menu della scheda). Cache accessori in LittleFS con gli avatar.

## 6. Test

- Servizio: regole XP e tetto, energia, livelli, traguardi, nessun punto dai permessi, proposta una volta per traguardo, rigiocando le fixture delle sessioni catturate; simulazione di un mese per la taratura.
- Strumenti: validate/pack/preview degli accessori; ancoraggi dei 4 avatar.
- Firmware e simulatore: `drawAccessory` pixel per pixel contro il simulatore (inverso, contorno, pose nascoste, ritocchi), schermata «La mascotte», festa; scenari di cattura.
- Dal vivo: accensione del gioco, primo livello, accessorio indossato in Home, festa, proposta di Claude e accessorio speciale arrivato senza flash.

## Fuori da questa specifica

Gioco per più utenti, classifiche, sincronizzazione tra PC, suoni.
