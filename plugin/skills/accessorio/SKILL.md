---
name: accessorio
description: Disegna un accessorio speciale per la mascotte del display Calamaio (gioco della mascotte) e lo installa sulla scheda senza riflashare. Usare quando il servizio Calamaio segnala che l'utente ha diritto a un accessorio speciale e l'utente accetta, quando l'utente chiede di creare un accessorio, un cappello, un oggetto da far indossare alla mascotte o all'avatar del display, oppure quando dice «crea un accessorio speciale per Calamaio».
---

# Accessorio speciale per la mascotte di Calamaio

Specifica completa: `docs/AVATAR.md` §13–14 del repository Calamaio, `~/CalamaiOS` (se lì non c'è, è tre livelli sopra la cartella di questa skill). Strumenti: `AV` = `node ~/CalamaiOS/tools/avatar/avatar.mjs` (Node 22, senza dipendenze). Catalogo da cui imparare: `avatars/accessori/*.accessory.json` e i fogli `avatars/anteprime/accessori-*.png`.

## Quando parte

- Il contesto del prompt dice «Calamaio, gioco della mascotte: l'utente ha raggiunto il traguardo «…»… chiediglielo UNA volta con AskUserQuestion». Fai **una** domanda con AskUserQuestion: «Creo un accessorio nuovo e speciale per la mascotte?», opzioni **Sì**, **Più tardi**, **No**.
  - **Più tardi** → `calamaio game special later` (il servizio lo riproporrà un altro giorno), una riga di conferma, poi torna al lavoro.
  - **No** → `calamaio game special decline`, una riga, poi torna al lavoro.
  - **Sì** → procedimento qui sotto. Se salta la domanda, non insistere.
- L'utente lo chiede direttamente: stesso procedimento. Se `calamaio game status` non dice «Accessorio speciale disponibile», l'accessorio si aggiunge lo stesso ma come normale (senza `--special`, `"special": false`): si sbloccherà salendo di livello. Dillo all'utente.

## Il formato in breve

File `<id>.accessory.json` (id kebab-case ≤ 24, uguale al nome del file):

```json
{
  "format": "calamaio-accessory", "version": 1,
  "id": "corona-di-stelle", "name": "Corona di stelle", "author": "Claude",
  "description": "Una corona con tre stelle al posto delle punte.",
  "special": true,
  "w": 9, "h": 5, "frames": 1,
  "anchor": "head-top", "origin": [4, 4],
  "outline": false, "hidePoses": ["run"], "perAvatar": {},
  "pixels": [[ "…9 simboli…", "… 5 righe …" ]]
}
```

- `w` ≤ 14, `h` ≤ 10, `frames` 1 o 2 (sul display il fotogramma è `f mod frames`, in fase con la mascotte).
- Simboli: `.` trasparente, `#` inchiostro, `+` carta, **`x` inverso** (il contrario di quello che c'è sotto: bianco sul corpo nero, nero sul fondo). `name` ≤ 24 byte: è il testo del menu della scheda.
- `anchor` (dove si aggancia, `origin` è il punto dell'accessorio che ci va sopra): `head-top` = la sede della tesa di un cappello (non la punta della testa); `eyes` = colonna centrale tra gli occhi, riga in cima agli occhi; `mouth`; `neck` = dove la testa entra nel corpo; `hand` (esiste solo in poche pose: evitarla).
- `outline: true` = contorno di carta dove l'accessorio d'inchiostro tocca il corpo: solo per forme compatte (papillon), mai per tratti sottili.
- `hidePoses`: pose in cui non si disegna (`working permission input done error idle compact tired stuffy run`). `run` è di profilo: nascondi lì tutto ciò che non sta bene di lato.
- `perAvatar`: ritocchi per avatar, `{"dx": 0, "dy": -2}` e/o `"pixels"` sostitutivi (stesse `w`, `h`, `frames`). Avatar della libreria: `goccia`, `gatto`, `demone`, `calamaro`.
- `"special": true` per l'accessorio premio (serve a `--special`).

## Regole di disegno (imparate guardando i fogli, docs/AVATAR.md §14.5)

- **Il nero sul nero sparisce.** La mascotte è una sagoma piena d'inchiostro: un accessorio `#` sopra il corpo non si vede. Metti la massa **fuori dalla sagoma** (sopra la testa, ai lati) e dove tocca il corpo usa `x` (diventa bianco): la tesa `x` è ciò che stacca un cappello dalla testa.
- **Le fasce `x` attraverso il corpo sembrano tagli.** Funziona ciò che esce dalla sagoma (il capo della sciarpa che sventola, i padiglioni ai lati, una punta piegata).
- **Il salto di `done` comanda l'altezza:** al culmine le teste a punta (Goccia, Calamaro) arrivano alle righe 0–1; un cappello alto più di 5 punti esce dal riquadro. Il Demone ha i cornetti: ha posto per 3 punti (ritocco `pixels` più basso).
- **Angolo della nuvoletta** (x 17..27, y 0..8) libero nelle pose `working permission input compact`, anche col sobbalzo di `permission`: stai a sinistra o al centro.
- **Occhi bianchi a filo:** le linee bianche stanno sempre a un punto d'inchiostro da occhi e bocca, o si fondono con loro.
- Leggibile a scala 1: forme piene, niente scacchiere di pixel alterni; 2 fotogrammi solo se l'animazione si vede (una bandierina, una fiamma) e cambia 1–2 punti.
- Almeno il 60% dei pixel deve staccarsi dal fondo (lo controlla `check`).

## Procedimento (con un subagente che disegna)

1. Proponi all'utente l'idea in una riga (cosa, dove si aggancia, il tratto speciale) e un id; se vuole altro, segui la sua idea.
2. Affida il disegno a un **subagente** (strumento Agent), dandogli: questa skill, il percorso del repository, l'idea, l'id, la cartella di lavoro (una cartella temporanea, **non** `avatars/`). Il subagente:
   1. scrive `<cartella>/<id>.accessory.json` (parte da un accessorio simile del catalogo);
   2. `AV accessory validate <file>` → zero errori, avvisi corretti o motivati;
   3. `AV accessory check <file>` → nessun problema su tutti gli avatar con le ancore (fuori riquadro, nuvoletta, contrasto);
   4. `AV accessory preview <file> --png <cartella>/<id>.png` e **guarda il foglio con Read** (un avatar per riga, 10 pose × 4 fotogrammi; bordo rosso = problema). Chiediti: si capisce cos'è a scala 1? si legge sul corpo nero e sul fondo chiaro? sta bene su tutti e quattro gli avatar, anche nel salto di `done` e nella posa `error` più bassa? Per i dettagli `AV accessory preview <file> --text --avatar gatto --pose done`;
   5. corregge (forma, `x`, ritocchi `perAvatar`, `hidePoses`) e ripete 2–4 finché è bello e leggibile, non solo finché i controlli passano;
   6. restituisce il percorso del file e del foglio PNG.
3. **Guarda tu il foglio** (Read) prima di installarlo; se qualcosa non va, rimanda al subagente con il difetto preciso. Mostra all'utente il risultato in due righe (com'è fatto, dove l'hai nascosto e perché).
4. Installa: `calamaio accessory add <cartella>/<id>.accessory.json --special` (aggiunge alla libreria dell'utente, lo sblocca, chiude la proposta, lo fa indossare: arriva sulla scheda senza riflashare). Se l'id esiste già, scegline un altro (non usare `--replace` su un accessorio dell'utente senza chiederglielo).
5. Conferma in una riga (`calamaio game status` dice cosa indossa) e **torna al lavoro** che l'utente aveva chiesto.

## Comandi utili

- `calamaio game status` · `calamaio accessory list` · `calamaio accessory show <id|file>` (anteprima nel terminale) · `calamaio accessory wear <id>|none`.
- Pagina «Gioco» della webapp: `http://127.0.0.1:8765/game.html` (galleria con l'accessorio su ogni avatar).
- Se `calamaio accessory` risponde «comando sconosciuto», il binario è più vecchio del gioco: dillo all'utente e fermati dopo aver salvato il file.

Mai modificare `avatars/accessori/` (catalogo del repository) per un accessorio speciale: è dell'utente e vive nella sua libreria (`<dati>/calamaio/accessories/`).
