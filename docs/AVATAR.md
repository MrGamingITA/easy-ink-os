# Avatar delle sessioni — formato v1

Fonte unica per firmware, simulatore, servizio, webapp e skill. Chi implementa legge qui; se il codice e questo file divergono, si corregge uno dei due e si annota in `docs/DECISIONI.md`.

Marche dei numeri: **[misurato]** = misurato in questo repository con gli strumenti di `tools/avatar/` o letto nel codice citato; **[stimato]** = ragionamento, da confermare.

## 1. Cosa è un avatar

Ogni sessione di Claude Code può avere un personaggio diverso dalla Goccia, così sul display si riconosce **quale** sessione chiede attenzione senza leggere il nome. Un avatar è un insieme di disegni a 1 bit: 9 pose (le stesse di `mascot()`) più la corsa facoltativa `run` (§12) × 2 o 4 fotogrammi, su una griglia di 28×25 punti, disegnata a scala intera come la Goccia.

La Goccia resta l'avatar predefinito e il ripiego per tutto ciò che non ha un avatar valido.

Libreria predefinita in `avatars/` (Goccia, Gatto, Demone, Calamaro), anteprime in `avatars/anteprime/`.

## 2. Il file `<id>.avatar.json`

```json
{
  "format": "calamaio-avatar",
  "version": 1,
  "id": "gatto",
  "name": "Gatto",
  "author": "Calamaio OS",
  "description": "Un gatto nero seduto: orecchie a punta, muso a ω e coda che spazza per terra.",
  "w": 28,
  "h": 25,
  "frames": 4,
  "poses": {
    "working":    [ [ "…28 simboli…", "… 25 righe in tutto …" ], [ … ], [ … ], [ … ] ],
    "permission": [ … ],
    "…": [ … ]
  }
}
```

| Campo | Regola |
|---|---|
| `format` | sempre `"calamaio-avatar"` |
| `version` | `1` |
| `id` | kebab-case `[a-z0-9]+(-[a-z0-9]+)*`, al massimo 24 caratteri; uguale al nome del file senza `.avatar.json` |
| `name` | nome mostrato, in italiano, non vuoto, al massimo **24 byte UTF‑8** (non caratteri: «è» vale 2) |
| `author` | non vuoto |
| `description` | al massimo 120 byte UTF‑8 |
| `w`, `h` | `28` e `25` |
| `frames` | `2` oppure `4`, uguale per tutte le pose |
| `poses` | oggetto `posa → array di frames fotogrammi`; ogni fotogramma è un array di `h` stringhe lunghe `w` |
| `anchors` | facoltativo: punti di aggancio degli accessori (§13) |

Nessun altro campo è ammesso (il validatore rifiuta i campi sconosciuti, come lo schema v1 del protocollo). Attenzione: `anchors` lo conosce per ora solo il validatore di `tools/avatar/`; quello del servizio lo rifiuta ancora (§13.5).

### 2.1 Simboli

| Simbolo | Significato | Sul pannello |
|---|---|---|
| `.` | trasparente | non si disegna: resta quello che c'è sotto |
| `#` | inchiostro | colore dell'inchiostro (nero; bianco se il disegno è invertito) |
| `+` | carta | colore della carta, disegnato **sopra** lo sfondo |

`+` serve per gli occhi, la bocca, l'interno della nuvoletta: su fondo carta non si vede la differenza da `.`, ma su un fondo d'inchiostro (banner, tema invertito) sì. È esattamente la distinzione che fa `mascot()` tra `I()` e `O()`.

Coordinate: origine in alto a sinistra, `x` = colonna (0..27), `y` = riga (0..24). Il punto `(x, y)` a scala `s` occupa il quadrato `ox + x·s, oy + y·s, s, s`, come `P()` in `mascot()`.

### 2.2 Pose

Ordine fisso (è anche l'ordine dei bit nel formato binario e di `enum class Pose` in `firmware/src/gfx/mascot.h`):

| # | Posa | Obbligatoria | Ripiego | Quando la mostra il display (`pose()` del simulatore) |
|---|---|---|---|---|
| 0 | `working` | sì | — | la sessione lavora |
| 1 | `permission` | sì | — | chiede un permesso |
| 2 | `input` | sì | — | aspetta una risposta |
| 3 | `done` | sì | — | ha finito |
| 4 | `error` | sì | — | errore |
| 5 | `idle` | sì | — | in pausa |
| 6 | `compact` | no | `working` | compatta il contesto |
| 7 | `tired` | no | `idle` | limite delle 5 ore ≥ 90% e stato working/done/idle |
| 8 | `stuffy` | no | `idle` | CO₂ > 1000 ppm e nessuna attesa |
| 9 | `run` | no | `working` | aggiornamento remoto: corre sulla barra di avanzamento (§12, `docs/OTA.md` §1 punto 6) |

### 2.3 Fotogrammi e profili

Il display sceglie il fotogramma come `frameFor()` del simulatore, poi lo riduce al numero di fotogrammi dell'avatar (`f mod frames`):

- profilo **fluido** (pannelli con `fps ≥ 3` o velocità «Massima»): fotogrammi 0‑1‑2‑3;
- profilo **ridotto** (Heltec, CrowPanel: `fps = 2`): fotogrammi **0 e 1**;
- profilo **a pose** (`fps = 1`): fotogramma 0, oppure 0/1 ogni 2 s se qualcuno aspetta;
- **risparmio energetico** e animazioni spente: solo il fotogramma 0.

Eccezione: `run` non va a tempo ma a passi della barra (§12); lo stesso `f mod frames` e, sul profilo ridotto, solo 0‑1.

Quindi, con 4 fotogrammi, **anche 0‑1 da soli devono formare un ciclo sensato**, e **il fotogramma 0 deve leggersi da solo** (è quello che resta fermo sullo schermo per ore). Nel Dettaglio a più sessioni il simulatore sfasa i fotogrammi (`f + i·3`): un avatar non deve dipendere dall'essere in fase con un altro.

### 2.4 Dove viene disegnato (Heltec 250×122) [misurato: `homeGoccia`, `homeUna`, `homeNumeri` in `calamaio-test.js`]

| Schermata | Scala | Origine | Ingombro con w = 28 |
|---|---|---|---|
| Home «Goccia» | 4 | (4, 10) | x 4..115, y 10..109; il testo parte da x = 120 |
| Una sessione | 3 | (163, 4) | x 163..246, y 4..78 |
| Contatori | 1 | (3, 90) | x 3..30, y 90..114; il testo parte da x = 36 |

La Goccia usa 27 colonne (0..26) [misurato]; la colonna 27 è libera per gli avatar e ci sta in tutte e tre le schermate. Le formule di centratura del simulatore usano `27·scala` (`cx - Math.round(27 * ms / 2)`): il firmware deve usare `w` dell'avatar (spostamento di mezzo punto, trascurabile).

## 3. Regole di disegno (ricavate dalla Goccia)

Misure della Goccia fatte con `tools/avatar/` su `avatars/goccia.avatar.json`, generato eseguendo `mascot()`.

1. **Silhouette piena e riconoscibile a scala 1.** La Goccia è una massa d'inchiostro di 16×20 punti (x 5..20, y 4..23) con almeno 195 pixel d'inchiostro per fotogramma [misurato]. A 28×25 si legge la sagoma, non i dettagli: orecchie, corna, pinne, coda devono stare **sul contorno**, non dentro.
2. **Occhi e bocca in carta su inchiostro.** Occhi 4×4 con pupilla 2×2 (`open`, `down`, `up`), chiusi = riga di 4, felici = arco, errore = X; bocca piccola sotto (`flat`, `o`, `grin`, `frown`). Gli occhi sono il segnale principale dello stato: tienili grandi e separati da almeno una riga d'inchiostro da bocca e muso. Un avatar con un occhio solo (il Demone, di profilo) lo fa più grande, 6×5 con gli angoli smussati, e gli dà le stesse espressioni: pupilla 2×2 che guarda giù, su o avanti, chiuso = arco in giù, felice = arco in su, X, palpebra calata a metà per `tired` (`bigEye()` in `tools/avatar/designs/demone.mjs`).
3. **Le pose si distinguono per più di un pixel.** Nella Goccia due pose qualunque differiscono nel fotogramma 0 di almeno 22 pixel (la coppia più vicina è permission/input) [misurato]. Ogni posa cambia almeno due cose tra occhi, bocca, braccia e simbolo.
4. **Animazioni piccole.** Spostamenti di 1‑2 punti (al massimo 3 nel salto di `done`), braccia che alternano due posizioni, simboli che lampeggiano. Nella Goccia cambiano al massimo 124 pixel tra due fotogrammi consecutivi (il salto di `done`) e 4‑24 nelle pose ferme [misurato]. Ogni pixel cambiato è inchiostro che il parziale deve spostare: più cambia, più resta l'alone.
5. **Niente retini.** Nessuna scacchiera fitta (pixel alterni): sull'e‑ink sfarfalla e lascia fantasmi. Nella Goccia non ce n'è nessuna [misurato]. Le diagonali sottili (braccia, X) vanno bene.
6. **Angolo della nuvoletta riservato.** Il riquadro x 17..27, y 0..8 appartiene al simbolo dello stato nelle pose `working` (…), `permission` (!, che lampeggia invertendo la nuvoletta), `input` (?) e `compact` (due righe). Il corpo non deve entrarci, in nessun fotogramma di quelle pose (anche con lo spostamento di 1 punto in su di `permission`). Nelle altre pose l'angolo è libero per Z, scintille, onde.
7. **Margine per il salto e per l'abbassamento.** `done` sale fino a 3 punti (`dy = 0, −2, −3, −1`): lascia almeno 3 righe libere sopra la testa nella posa a terra. `error` scende di 1 punto: lascia libera la riga 24 (la Goccia appoggia i piedi sulla riga 23).
8. **Stessa grammatica della Goccia**, così l'utente non deve reimparare gli stati:

| Posa | Occhi | Bocca | Braccia (o ali, tentacoli) | Simbolo | Movimento (`dy` per fotogramma) |
|---|---|---|---|---|---|
| working | giù (3: aperti) | piatta | battono sui tasti, alternate | nuvoletta «…» che scorre | 0, 0, +1, 0 |
| permission | aperti | o | entrambe alzate, alternate | nuvoletta «!» che si inverte | 0, −1, 0, −1 |
| input | in su (3: chiusi) | o | una alzata | nuvoletta «?» | ferma |
| done | felici | sorriso | in festa quando salta | scintille «+» alternate | 0, −2, −3, −1 |
| error | X | triste | cadenti | goccia di sudore | +1 fisso |
| idle | chiusi | piatta | a riposo | Z che salgono | respiro di 1 punto (fotogrammi 2‑3) |
| compact | in su | piatta | come working | nuvoletta a due righe | ferma |
| tired | chiusi con palpebre | piatta | cadenti | sudore a sinistra | ferma |
| stuffy | chiusi | o | cadenti | onde d'aria in alto | ferma |
| run | in avanti (pupilla verso destra) | piatta | pompano, alternate | righe di velocità (facoltative), mai la nuvoletta | 0, −1, 0, −1 |

`run` è l'unica posa di profilo e ha regole sue: §12.

`tools/avatar/lib/kit.mjs` contiene questa grammatica già pronta (nuvoletta, simboli, scintille, Z, sudore, onde, occhi): un disegno nuovo definisce solo corpo, faccia e braccia (vedi `tools/avatar/designs/`).

## 4. Controlli automatici (`node tools/avatar/avatar.mjs validate`)

Errori (l'avatar non si carica): struttura e campi della sezione 2; simboli diversi da `. # +`; pose obbligatorie mancanti; numero di fotogrammi diverso tra le pose; fotogramma senza inchiostro («silhouette vuota»); due pose che nel fotogramma 0 differiscono di meno di **16 pixel** (Goccia: 22, quindi la soglia è ~73% del suo minimo [stimato]); file JSON oltre 48 KB.

Avvisi (si carica, ma va guardato): meno di **120** pixel d'inchiostro in un fotogramma (~60% del minimo della Goccia, 195 [stimato]); più di **200** pixel cambiati tra fotogrammi consecutivi (~29% di 700; Goccia 124 [stimato]); più di **2** finestre 3×3 a scacchiera perfetta (Goccia 0); fotogrammi 0 e 1 identici in una posa animata; angolo della nuvoletta vuoto in working/permission/input/compact.

Solo per `run` (avvisi, §12): bordo destro diverso dalla colonna 27 in un fotogramma, o meno di 3 pixel d'inchiostro in colonna 27; riga 24 senza inchiostro (i piedi non toccano la barra); meno di **12** pixel cambiati tra fotogrammi consecutivi (la corsa sembra ferma; la libreria ne cambia 103‑126 [misurato]); inchiostro che varia di oltre il **25%** tra i fotogrammi, `(max − min)/max` (la sagoma «pulsa»; libreria ≤ 1% [misurato]). La differenza dalle altre pose è il controllo comune: `run` partecipa al confronto dei fotogrammi 0 con soglia 16 (errore); nella libreria la posa più vicina alla corsa ne dista 174‑271 pixel [misurato].

Le soglie sono in `tools/avatar/lib/format.mjs` (`MIN_POSE_DIFF`, `MIN_INK`, `MAX_FRAME_DIFF`, `MAX_CHECKER`, e per la corsa `RUN_EDGE`, `RUN_FOOT`, `MIN_RUN_STEP`, `MAX_RUN_INK_SPREAD` usate da `runChecks()`). I controlli sono euristici: **non sostituiscono lo sguardo** sul foglio di anteprima.

## 5. Limiti

| Cosa | Limite | Misura |
|---|---|---|
| File JSON | **48 KB** | 41 122‑41 153 byte per i 4 avatar con 10 pose × 4 fotogrammi [misurato]; erano 37 039‑37 070 con 9 pose |
| Binario per il display | **16 KB** | 7 072 byte con 10 × 4 [misurato]; 6 372 con 9 × 4 |
| Cache sul display | almeno **8 avatar** | 8 × 7 072 = 56 576 byte nel caso peggiore [calcolato] |

> **OBIEZIONE (limite di 16 KB sul file).** Il compito chiedeva «dimensione file ≤ 16 KB». Con una riga di disegno per riga di testo un fotogramma JSON pesa circa 1 KB (25 righe × ~40 byte), quindi 9 × 4 = 36 fotogrammi pesano ~37 KB [misurato] e anche 9 × 2 = 18 fotogrammi superano i 16 KB [calcolato: ~18,5 KB]. Il limite di 16 KB è applicato al **binario**, che è ciò che il display riceve e conserva; al JSON, che resta sul PC, si applica 48 KB (36 KB di disegno + margine per l'indentazione). Alternative scartate: compressione o RLE dentro il JSON (il file non sarebbe più leggibile né modificabile a mano, che è il motivo per cui è testo); fotogrammi «per differenza» (il validatore e chi disegna dovrebbero ricostruirli, e un errore in un fotogramma si propagherebbe ai successivi). Da confermare con il proprietario (domanda 1).

La cache di 8 avatar non sta in RAM sulla Heltec (ESP32‑S3 senza PSRAM [stimato: va verificato sulla scheda, vedi `docs/HARDWARE.md`]): va tenuta in flash (LittleFS o una partizione dati) e se ne legge un fotogramma alla volta (175 byte).

## 6. Assegnazione e uso

- **Per progetto**: avatar legato alla cartella `cwd` della sessione (percorso assoluto, normalizzato).
- **Per sessione**: avatar legato a un `session_id`; vince su quello del progetto. Sparisce con la sessione.
- **Precedenza**: sessione → progetto → Goccia.
- **Librerie**: gli avatar predefiniti del repository (`avatars/`, inclusi nel servizio) più la libreria dell'utente in `<config utente>/calamaio/avatars/` (stessa radice di `config.json`, `os.UserConfigDir()`). A parità di `id` vince quella dell'utente.
- **Sul display**: la Home mostra l'avatar della sessione più urgente; Dettaglio, Una sessione e Contatori quello della sessione mostrata. Se l'avatar non è ancora in cache, o non è valido, il display disegna la Goccia.
- **All'avvio di una sessione** in un progetto senza avatar la skill `avatar` del plugin propone una scelta (sezione 7).

### 6.1 Avatar liberi e in uso

Un avatar è **in uso** se è assegnato (per progetto o per sessione) ad **almeno una sessione aperta e non archiviata, diversa da quella corrente**. Altrimenti è **libero**.

- Le sessioni archiviate non tengono occupato un avatar: sono fuori dal display.
- La sessione corrente non conta: riassegnarle l'avatar che ha già non è un conflitto.
- **La Goccia non è mai proposta come scelta**: è il ripiego di tutte le sessioni senza avatar, quindi non distingue niente. `calamaio avatar list` la mostra comunque come «in uso da …» quando altre sessioni attive la mostrano (informazione utile: quelle sessioni sono indistinguibili tra loro), ma `--free` non la elenca mai.

Riferimento eseguibile della definizione: `usage()` e `freeChoices()` in `tools/avatar/lib/format.mjs`, provate in `tools/avatar/test/avatar.test.mjs` con `tools/avatar/test/stato-esempio.json`; a mano: `node tools/avatar/avatar.mjs free tools/avatar/test/stato-esempio.json --session sess-corrente`.

## 7. Comandi del servizio (previsti per Calamaio 0.2)

Non esistono ancora: li implementa il servizio, la skill li usa così.

| Comando | Effetto |
|---|---|
| `calamaio avatar add <file>` | valida (stesse regole della sezione 4) e copia nella libreria utente; rifiuta un `id` già presente salvo `--replace` |
| `calamaio avatar assign <id> --project DIR` | lega l'avatar alla cartella (predefinito: la cartella corrente) |
| `calamaio avatar assign <id> --session ID` | lega l'avatar alla sessione |
| `calamaio avatar assign goccia …` | toglie l'assegnazione (torna la Goccia) |
| `calamaio avatar list` | una riga per avatar: `id`, `name`, `libero` oppure `in uso da <nome sessione>[, …]`, e `(predefinito)` per la Goccia |
| `calamaio avatar list --free` | solo gli avatar liberi, Goccia esclusa |
| `--json` (con `list`) | `{"avatars": [{"id", "name", "description", "state": "libero"\|"in-uso", "usedBy": [nomi], "default": bool, "source": "predefinito"\|"utente"}], "free": [id]}` |

Con `--session` assente, `list` considera «corrente» la sessione indicata da `CLAUDE_SESSION_ID` se presente [stimato: da verificare che Claude Code la esponga ai comandi Bash], altrimenti nessuna.

## 8. Formato binario per il display (CAVT v1)

Little‑endian. Intestazione di 72 byte, poi i fotogrammi.

| Offset | Tipo | Campo |
|---|---|---|
| 0 | 4 byte | magia `CAVT` |
| 4 | u8 | versione = 1 |
| 5 | u8 | lunghezza dell'intestazione = 72 (i dati partono da qui: versioni future possono allungarla) |
| 6 | u16 | lunghezza totale del file |
| 8 | u32 | CRC‑32 (IEEE, quello di zlib) dei byte da 12 alla fine |
| 12 | u8 | `w` = 28 |
| 13 | u8 | `h` = 25 |
| 14 | u8 | fotogrammi = 2 o 4 |
| 15 | u8 | riservato = 0 |
| 16 | u16 | `poseMask`: bit *i* = posa *i* presente (ordine della sezione 2.2); i bit 0‑5 sono obbligatori, il 9 è `run`, 10‑15 devono essere 0 |
| 18 | u16 | byte per fotogramma = ⌈w·h/4⌉ = 175 |
| 20 | 24 byte | `id`, ASCII, completato con zeri |
| 44 | 24 byte | `name`, UTF‑8, completato con zeri |
| 72 | … | per ogni posa presente, in ordine, per ogni fotogramma: 175 byte |

Pixel: 2 bit, riga per riga, pixel *k* = `y·w + x` nel byte `k >> 2`, bit `7 − 2·(k & 3)` e seguente (il primo pixel nei due bit alti). Codici: `00` trasparente, `01` inchiostro, `10` carta, `11` riservato (rifiutato in v1; potrà servire per un grigio sui pannelli a 16 livelli).

Dimensione = 72 + pose × fotogrammi × 175 [calcolato, verificato dai test]:

| Pose × fotogrammi | Byte |
|---|---|
| 10 × 4 (con `run`) | 7 072 |
| 10 × 2 (con `run`) | 3 572 |
| 9 × 4 | 6 372 |
| 6 × 4 | 4 272 |
| 9 × 2 | 3 222 |
| 6 × 2 | 2 172 |

Il CRC serve anche come «versione» dell'avatar: il display tiene in cache la coppia `(id, crc)`.

## 9. Strumenti (`tools/avatar/`, Node 22 senza dipendenze)

Scelto Node perché è già usato dagli strumenti del repository (`tools/golden/`, `tools/sim-capture/`) e serve comunque per eseguire `mascot()` del simulatore; niente pacchetti da installare.

```sh
node tools/avatar/avatar.mjs validate avatars/*.avatar.json
node tools/avatar/avatar.mjs preview avatars/gatto.avatar.json --png /tmp/gatto.png   # foglio: pose × fotogrammi, scala 4, scala 1, su inchiostro
node tools/avatar/avatar.mjs preview avatars/gatto.avatar.json --text --pose done      # anteprima nel terminale
node tools/avatar/avatar.mjs preview avatars/gatto.avatar.json --home /tmp/home.png --pose permission
node tools/avatar/avatar.mjs compare /tmp/confronto.png avatars/*.avatar.json
node tools/avatar/avatar.mjs pack avatars/gatto.avatar.json /tmp/gatto.cavt             # con verifica di andata e ritorno
node tools/avatar/avatar.mjs bar avatars/gatto.avatar.json /tmp/barra.png [--frames2]   # corsa sulla barra: 6 istantanee, scala 1 e 2
node tools/avatar/avatar.mjs sim /tmp/avatar.html avatars/*.avatar.json                 # pagina che anima come il display (anche la barra)
node tools/avatar/avatar.mjs build tools/avatar/designs/gatto.mjs                       # da disegno procedurale a JSON
node tools/avatar/avatar.mjs anchors avatars/gatto.avatar.json --auto                    # propone le ancore (§13) in avatars/ancore/gatto.anchors.json
node tools/avatar/avatar.mjs anchors avatars/gatto.avatar.json --png /tmp/a.png --pack /tmp/gatto.canc   # controlla, foglio, blocco CANC
node tools/avatar/avatar.mjs library                                                    # rigenera avatars/ e avatars/anteprime/
node --test tools/avatar/test/*.test.mjs
```

`avatars/goccia.avatar.json` è generato da `tools/avatar/goccia-export.mjs`, che estrae ed esegue `mascot()` come `tools/golden/mascot-golden.js` per le 9 pose; la corsa, che `mascot()` non ha, viene da `tools/avatar/goccia-run.mjs` (disegnata a mano). Un test verifica che coincida ancora con il simulatore.

`library` scrive in `avatars/anteprime/`: `<id>.png` (foglio, ora con la riga `run`), `barra-<id>.png` (corsa sulla barra), `ancore-<id>.png` (punti di aggancio, §13), `confronto.png`, `avatar-sim.html` (con la scheda «Aggiornamento»).

## 10. Cosa devono fare gli altri pezzi

**Servizio**
- Caricare le due librerie, validarle con le regole della sezione 4 (porting in Go di `validate()`; i file di `avatars/` e i casi di `avatar.test.mjs` come fixture), impacchettarle in CAVT.
- Tenere le assegnazioni in `config.json` (`avatars: {projects: {dir: id}, sessions: {sid: id}}`), risolvere l'avatar di ogni sessione (sessione → progetto → Goccia), offrire i comandi della sezione 7.
- Protocollo (proposta da scrivere in `docs/PROTOCOLLO.md` da chi lo possiede): nella Sessione di `state` due campi in più, `avatar` (id) e `avatarCrc` (u32); nuovo messaggio `avatar` (servizio → display) `{id, crc, data: <CAVT in base64>}`, 9 432 caratteri per 7 072 byte con `run` (8 496 per 6 372 senza), sotto il limite di 15 KB del frame; il display chiede gli avatar che non ha con `avatar-need {ids: [...]}`. Gli avatar non passano mai dentro `state` (12 KB).
- Segnalare a Claude Code che il progetto non ha un avatar, così parte il flusso di avvio della skill: risposta al hook `SessionStart` con `hookSpecificOutput.additionalContext` («Calamaio: questo progetto non ha un avatar, usa la skill avatar»). Oggi in `plugin/hooks/hooks.json` gli hook sono `async`, e un hook asincrono non può restituire contesto [stimato: da verificare nella documentazione degli hook]: per `SessionStart` serve la forma sincrona con un timeout breve, che non deve mai bloccare se il servizio è spento.
- Webapp: mostrare l'avatar di ogni sessione nella Panoramica (il JSON si disegna direttamente) e permettere di assegnarlo.

**Firmware**
- Un disegnatore unico `drawAvatar(Surface&, const Avatar&, ox, oy, s, Pose, f, ink)` che legge CAVT (2 bit → `.`/`#`/`+`), con il ripiego delle pose facoltative e `f mod frames`.
- Corsa sulla barra dell'aggiornamento: posa `run` con il ripiego su `working`, origine e fotogramma come §12.2; lettore da portare a 10 pose (§12.4).
- La Goccia resta compilata nel firmware come CAVT (7 072 byte con `run`, §12.4) generato da `avatars/goccia.avatar.json`; il test esistente `test_mascot` (golden di `mascot()`) deve passare identico disegnando dal CAVT.
- Cache in flash di almeno 8 avatar con chiave `(id, crc)`, sostituzione del meno usato; validazione completa di `unpack()` (magia, versione, lunghezza, CRC, `poseMask`, codice 11) prima di salvare; avatar mancante o rotto → Goccia.
- Centratura con `w` dell'avatar, non con 27.

**Simulatore** (con annotazione in `DECISIONI.md`, perché è in `reference/`)
- `mascot()` resta per la Goccia; un `drawAvatar()` equivalente al firmware, un campo `avatar` nelle sessioni finte e la libreria `avatars/` caricata come dati, così i test golden coprono anche gli avatar.

## 11. Domande per il proprietario

1. Il limite di 16 KB vale per il binario (6 372 byte) e non per il JSON (~37 KB)? Vedi l'OBIEZIONE della sezione 5.
2. Le assegnazioni per progetto vanno nel `config.json` del servizio (proposta) o in un file nel progetto stesso (per esempio `.calamaio/avatar`), che si porterebbe dietro con git?
3. La colonna 27 (in più rispetto ai 27 punti della Goccia) va bene così, o si preferisce `w = 27` per non toccare le formule di centratura del simulatore?

## 12. Posa facoltativa «run» (aggiornamento remoto, M9)

Richiesta dell'utente: nella schermata di aggiornamento (`docs/OTA.md` §1 punto 6) la mascotte corre sulla barra di avanzamento, sul fronte della parte completata, verso destra.

### 12.1 Regole

- Nome `run`, posa 9 (dopo `stuffy`), bit 9 di `poseMask`. Facoltativa; ripiego `working`.
- Di profilo, **rivolta verso destra**, senza nuvoletta. È l'unica posa di profilo: il Demone, che nelle altre pose guarda a sinistra, qui è specchiato.
- **Bordo destro sulla colonna 27 in tutti i fotogrammi** (il pezzo più avanzato: muso, testa o corpo), con almeno 3 pixel d'inchiostro in colonna: è il bordo che si allinea al fronte della barra, quindi non deve ballare tra un fotogramma e l'altro. Niente che sporga oltre (braccia, piedi) nemmeno per un fotogramma.
- **Piedi sulla riga 24** in ogni fotogramma (almeno un piede: la figura appoggia sul bordo superiore della barra). È l'unica posa che usa la riga 24 apposta.
- Ciclo: 0 e 2 falcata (gambe aperte), 1 e 3 gambe raccolte con sobbalzo di 1 punto (`DY.run = [0, −1, 0, −1]` in `lib/kit.mjs`); 0 e 2 (e 1 e 3) si distinguono per braccia, coda o righe di velocità. **Con i soli fotogrammi 0‑1 la corsa si legge** (aperte/raccolte).
- Righe di velocità facoltative, dietro (a sinistra), che scorrono di 1 punto nei fotogrammi dispari (`Pen.speed()`).
- Controlli automatici: §4 (`runChecks()` in `lib/format.mjs`).

### 12.2 Come la disegna il display

Per ogni ridisegno della barra (ogni ~5% o ogni 2 s, `docs/OTA.md` §1) il passo `k` cresce di 1 e il fotogramma è `k mod n`, con `n` = 4 sul profilo fluido e 2 sul ridotto, poi `mod frames` (§2.3). Barra di riferimento: 230×10 in x = 10 su uno schermo largo 250 (`BAR` in `lib/render.mjs`).

```
front = barX + round(barW · pct / 100)
ox    = clamp(front − 28·s, 0, larghezzaSchermo − 28·s)    // bordo destro (colonna 27) sul fronte
oy    = barY − 25·s                                         // riga 24 subito sopra il bordo superiore della barra
```

Sotto 28·s pixel di avanzamento (12% a scala 1, 24% a scala 2 con la barra di riferimento [calcolato: 28/230, 56/230]) la figura resta ferma al bordo sinistro dello schermo, davanti al fronte: meglio che tagliarla. Riferimento eseguibile: `runOrigin()` in `lib/render.mjs`, usato dal foglio `barra-<id>.png` e da `avatar-sim.html`.

Costo sul parziale [calcolato]: un passo del 5% sposta la figura di 11‑12 px (230 × 0,05 = 11,5); l'area da aggiornare è l'unione dei due riquadri, (28·s + 12) × 25·s = 40×25 px a scala 1, 68×50 a scala 2.

### 12.3 La libreria

| Avatar | Disegno | Inchiostro per fotogramma | Pixel cambiati tra fotogrammi | CRC‑32 con `run` (7 072 B) | CRC‑32 senza `run` (6 372 B, invariato) |
|---|---|---|---|---|---|
| Goccia | goccia inclinata, la punta resta indietro; occhi di tre quarti, braccio sottile dietro, righe di velocità (`tools/avatar/goccia-run.mjs`) | 226‑227 | 117‑119 | `0x33072CEE` | `0x0386CC22` |
| Gatto | galoppo: zampe tese avanti e indietro / raccolte sotto la pancia, coda alta (`runFrame()` in `designs/gatto.mjs`) | 209‑212 | 122‑126 | `0xB2432AD7` | `0xD6AEBFCB` |
| Demone | specchiato verso destra, occhio grande in avanti, ginocchio alzato nelle raccolte, coda a freccia in alto dietro (`runFrame()` in `designs/demone.mjs`) | 141‑143 | 111‑122 | `0xA3130345` | `0xC6E4FA9B` |
| Calamaro | mantello inclinato all'indietro, corre su due fasci di tentacoli, tentacolo lungo che pompa dietro (`runFrame()` in `designs/calamaro.mjs`) | 196‑198 | 103 | `0x2D86BCB3` | `0xC2FB3686` |

Tutti i numeri [misurato] con `tools/avatar/` sui file di `avatars/`. Il CRC senza `run` è quello di prima: le 9 pose esistenti non sono cambiate di un pixel (verificato rigenerando la libreria e confrontando i JSON). Anteprime: `avatars/anteprime/<id>.png` (riga `run`), `avatars/anteprime/barra-<id>.png` (6 istantanee a 0/20/40/60/80/100%, scala 1 e 2), `avatar-sim.html` (scheda «Aggiornamento», con il profilo ridotto o fluido).

### 12.4 Compatibilità del CAVT

**La disposizione è compatibile** [verificato dal test «CAVT con run» in `tools/avatar/test/avatar.test.mjs`]: le pose sono scritte nell'ordine dei bit e `run` è l'ultima, quindi i fotogrammi delle pose 0‑8 hanno gli stessi byte e le stesse posizioni del CAVT senza `run` (offset = 72 + pose presenti prima × fotogrammi × 175, come `avatarFrameBytes()` del firmware); l'intestazione cambia solo in lunghezza totale (6), CRC (8) e bit 9 di `poseMask` (16).

**I lettori v1 che esistono oggi però non ignorano il bit 9: rifiutano tutto il file** [letto]:
- `parseAvatar()` in `firmware/src/gfx/avatar.cpp` rifiuta `mask >> Pose::Count` ≠ 0 (`AvatarError::Mask`), e `Pose::Count` = 9 in `firmware/src/gfx/avatar.h`; prima ancora, `kCavtMaxBytes` = 6 372 (`avatar.h`) limita il messaggio `avatar` in `firmware/src/net/net.cpp` e lo slot della cache in `firmware/src/core/avatars.h`/`avatars.cpp`;
- `Unpack()` in `service/internal/avatar/pack.go` rifiuta `mask>>len(Poses)` ≠ 0 («bit di posa sconosciuti»);
- `unpack()` in `tools/avatar/lib/pack.mjs` faceva lo stesso con 9 pose (ora ne conosce 10 e rifiuta solo i bit 10‑15).

Quindi la frase di prima («un lettore v1 che non conosce il bit la ignora») era sbagliata. Conseguenza [dedotto dal codice citato]: un firmware vecchio che riceve un avatar con `run` lo scarta e disegna la Goccia (§10, «avatar mancante o rotto → Goccia»), senza danni ma senza l'avatar.

**Versione 2: non serve e non aiuterebbe.** I lettori esistenti rifiutano anche `buf[4] ≠ 1`, quindi una v2 sarebbe scartata nello stesso modo, e la disposizione non cambia. Scelta: **resta la versione 1, con il bit 9 = `run`** («v1.1»), e i tre lettori si aggiornano insieme:
- firmware: `Pose::Run` prima di `Pose::Count`, `kCavtMaxBytes = 72 + 10 × 4 × 175 = 7 072`, ripiego `Run → Working` in `avatarPose()`, testo d'errore di `net.cpp`; la Goccia compilata (`avatar_builtin.cpp`, da `tools/sim-capture/gen-avatars.mjs`) passa a 7 072 byte;
- servizio: `run` in `Poses` e il ripiego in `format.go`, i controlli di `runChecks()` nel porting di `validate()`;
- protocollo: un messaggio `avatar` con 7 072 byte sono 9 432 caratteri base64 [calcolato: ⌈7 072/3⌉ × 4], sotto `maxLength` 14 336 dello schema (`protocol/schema/message.schema.json`, `avatar.data`); da aggiornare solo il commento («6372 byte = 8496 caratteri»).

Transizione, se resta in uso un firmware vecchio [proposta]: il servizio può mandare a quel display l'avatar **senza** `run`, che è byte per byte il CAVT di oggi (CRC nell'ultima colonna di §12.3), per esempio in base a un campo di `hello` che dichiari le pose conosciute. Da decidere con chi possiede `PROTOCOLLO.md`.

Per il futuro [proposta, non adottata]: un lettore «tollerante» che accetta i bit sconosciuti, conta la dimensione con tutti i bit di `poseMask` e salta le pose che non conosce renderebbe compatibile davvero la prossima posa facoltativa. Non adottato ora perché cambia il contratto di tre lettori che si stanno aggiornando in parallelo.

## 13. Punti di aggancio (ancore) per gli accessori

Servono al gioco della mascotte (`docs/superpowers/specs/2026-10-11-gioco-mascotte-design.md` §1.2): un accessorio (§14) si disegna sopra la mascotte agganciando il suo `origin` a uno di questi punti. Un avatar **senza ancore** non porta accessori: nessuna posizione inventata.

### 13.1 I cinque punti

| Punto | Dove sta | Chi lo usa |
|---|---|---|
| `head-top` | la **sede della tesa di un cappello**: un pixel d'inchiostro della testa sulla colonna centrale della faccia, con sopra posto per un cappello alto 5 punti in ogni fotogramma (anche nel salto di `done`). Sulle teste tonde è la cima della testa; sulle teste a punta (Goccia, Calamaro) la tesa scende sulla punta, che il cappello copre; il Demone, coi cornetti, ha posto per 3 punti | cappelli, corona, fiocco, cuffie |
| `eyes` | `x` = colonna centrale tra gli occhi (la sinistra delle due se la larghezza è pari), `y` = **riga in cima** al riquadro degli occhi aperti (4×4, o l'occhio grande del Demone) | occhiali |
| `mouth` | centro della bocca: colonna centrale, riga media dei suoi pixel di carta | (pipa, baffi…) |
| `neck` | dove la testa entra nel corpo, al centro (di solito la riga sotto la bocca; di profilo, dietro la testa) | sciarpa, papillon |
| `hand` | estremità del braccio che sporge a destra della faccia; `null` se in quella posa non c'è (zampe corte, coda, tentacoli staccati) | (oggetti in mano) |

Il riquadro degli occhi non cambia con l'espressione: in `idle` (occhi chiusi) o `error` (X) `eyes` resta dove sarebbero gli occhi aperti, spostato solo col corpo (salto di `done`, `error` più in basso). Così un accessorio non «balla» cambiando espressione.

### 13.2 Il campo `anchors`

```json
"anchors": {
  "working": [
    {"head-top": [12, 4], "eyes": [12, 13], "mouth": [12, 19], "neck": [12, 20], "hand": null},
    … un elemento per fotogramma …
  ],
  … ogni posa disegnata …
}
```

Regole (errori di `validate`, `anchorErrors()` in `tools/avatar/lib/format.mjs`): un elemento per ogni posa **disegnata** (le pose di ripiego usano le ancore della posa che le sostituisce, come il disegno) e nessuna posa non disegnata; tanti elementi quanti `frames`; in ogni elemento esattamente i 5 punti, ciascuno `[x, y]` interi dentro 28×25 oppure `null`. Lettura: `getAnchor(avatar, posa, f, punto)` in `tools/avatar/lib/anchors.mjs` (fotogramma `f mod frames`, ripiego della posa, `null` se manca).

### 13.3 Le ancore della libreria predefinita: file accanto

I quattro avatar di `avatars/` **non** hanno il campo nel loro JSON: le ancore stanno in `avatars/ancore/<id>.anchors.json`:

```json
{ "format": "calamaio-anchors", "version": 1, "avatar": "gatto", "avatarCrc": "b2432ad7", "anchors": { … } }
```

Perché [letto]: (1) `TestBuiltinMatchesRepo` in `service/internal/avatar/format_test.go` confronta byte per byte `avatars/*.avatar.json` con la copia inclusa nel servizio, e `validate` di `service/internal/avatar/format.go` rifiuta i campi sconosciuti (`known`): toccare quei JSON romperebbe oggi i test del servizio e, rigenerando la copia, i predefiniti; (2) i JSON della libreria sono generati dai disegni (`library`, `serialize()`), le ancore sono corrette a mano: separate, la rigenerazione non le cancella. `avatarCrc` è il CRC‑32 del CAVT del disegno per cui sono state fatte: se il disegno cambia, `sidecarErrors()` lo segnala («fatte per un altro disegno») e vanno rifatte. Le ancore stanno **in un posto solo**: JSON e file accanto insieme sono un errore. `loadWithAnchors(file)` carica l'avatar con le ancore da uno dei due.

Un avatar dell'utente porta le ancore nel suo JSON (un file solo da condividere), quando il servizio le accetterà (§13.5).

### 13.4 Nel binario: blocco separato CANC, il CAVT non cambia

Il piano prevedeva un blocco in coda al CAVT segnalato da un bit in intestazione. **Non si può** [letto]: i lettori di oggi rifiutano un CAVT anche di un solo byte più lungo o con un byte d'intestazione riservato diverso da zero:

- `parseAvatar()` in `firmware/src/gfx/avatar.cpp`: `buf[15] != 0` → `AvatarError::Reserved`; lunghezza ≠ `72 + pose × fotogrammi × 175` → `AvatarError::Size`; `buf[5]` ≠ 72 → `AvatarError::Header`; in più lo slot della cache è di `kCavtMaxBytes` (`firmware/src/gfx/avatar.h`, usato da `AvatarCache` in `firmware/src/core/avatars.*`);
- `Unpack()` in `service/internal/avatar/pack.go`: `total != PackedSize(...)` → «dimensione incoerente con pose e fotogrammi».

Quindi il CAVT resta **identico byte per byte** con o senza ancore (CRC di §12.3 invariati, verificato dal test «CAVT: con o senza ancore gli stessi byte» in `tools/avatar/test/anchors.test.mjs`), e le ancore viaggiano in un blocco a parte, `CANC` v1, che arriverà sul display con un messaggio suo in fase 2 (da definire in `docs/PROTOCOLLO.md` da chi lo possiede):

| Offset | Tipo | Campo |
|---|---|---|
| 0 | 4 byte | magia `CANC` |
| 4 | u8 | versione = 1 |
| 5 | u8 | lunghezza dell'intestazione = 20 |
| 6 | u16 | lunghezza totale |
| 8 | u32 | CRC‑32 (IEEE) dei byte da 12 alla fine |
| 12 | u32 | CRC‑32 del CAVT dell'avatar a cui si riferisce (lo lega al disegno: cache `(id, crc)`) |
| 16 | u16 | `poseMask` (come il CAVT) |
| 18 | u8 | fotogrammi |
| 19 | u8 | punti per fotogramma = 5 |
| 20 | … | per ogni posa presente (ordine di §2.2), per ogni fotogramma, i 5 punti nell'ordine di §13.1: `x`, `y` (u8); `0xFF 0xFF` = assente |

Dimensione = 20 + pose × fotogrammi × 10: **420 byte** con 10 pose × 4 fotogrammi [calcolato, verificato dai test], 560 caratteri in base64 [calcolato: ⌈420/3⌉ × 4]. `packAnchors()`/`unpackAnchors()` in `tools/avatar/lib/anchors.mjs`; `unpackAnchors` rifiuta magia, versione, intestazione, lunghezza, CRC, bit di posa oltre il 9 e punti fuori da 28×25.

### 13.5 Cosa manca (fase 2)

- Servizio: `anchors` tra i campi ammessi di `validate` (`format.go`), lettura dei file `ancore/` per i predefiniti (o fusione nei JSON insieme alla rigenerazione di `builtin/`), porting di `packAnchors` con test di parità.
- Firmware e simulatore: ricevere e conservare il blocco CANC accanto al CAVT con la stessa chiave `(id, crc)`; senza blocco, niente accessori.

### 13.6 Come sono state fatte le ancore della libreria

`anchors --auto` (`autoAnchors()` in `lib/anchors.mjs`) misura un fotogramma di riferimento, `permission[0]` (occhi aperti, a terra; per la corsa `run[0]`): corpo = componente 4‑connessa più grande dei pixel disegnati (la nuvoletta e i simboli restano fuori); occhi = componente di carta più alta e quelle alla stessa altezza; bocca = la carta più vicina sotto; testa = cima del corpo sulla colonna degli occhi; collo = riga sotto la bocca. Negli altri fotogrammi insegue lo spostamento rigido del corpo (ricerca di `dx` in −3..3 e `dy` in −4..4 sulla finestra della faccia) e riallinea `head-top` sulla cima reale della colonna. Poi i fogli `avatars/anteprime/ancore-<id>.png` sono stati guardati e corretti a mano nei file `ancore/`, in due giri: il primo guardando le ancore, il secondo provandoci sopra i cappelli del catalogo (§14.5), che hanno mostrato che la cima della punta non è la sede giusta:

| Avatar | Correzione |
|---|---|
| Goccia | `head-top` 3 punti più in basso (sulla punta, larga 4: la sede della tesa, §13.1); nella corsa sulla fronte, occhi + (2, −3), davanti alla punta che resta indietro |
| Calamaro | `head-top` 4 punti più in basso (nel salto di `done` la punta arriva alla riga 0); nella corsa occhi + (3, −4) |
| Gatto | `hand` a `null` dove l'automatico prendeva la punta della coda (`error`, `tired`); nella corsa `mouth` = muso (occhi + (3, 4)) e `neck` = dietro la testa (occhi + (−3, 5)), che l'automatico non trova perché il naso tocca gli occhi |
| Demone | `neck` sul collo stretto sotto la testa (occhi + (1, 8)) invece che sotto il muso, che è di profilo; nella corsa occhi + (−1, 8) |

`hand` resta parziale (solo dove un braccio sporge davvero): nessun accessorio del catalogo iniziale la usa.

## 14. Accessori (formato `calamaio-accessory` v1)

Un accessorio è un piccolo disegno a 1 bit che la mascotte indossa (gioco della mascotte, specifica §1.1). Si aggancia a un punto dell'avatar (§13), quindi lo stesso file va bene per tutti gli avatar con le ancore.

### 14.1 Il file `<id>.accessory.json`

```json
{
  "format": "calamaio-accessory",
  "version": 1,
  "id": "cappellino",
  "name": "Cappellino",
  "author": "Calamaio OS",
  "description": "Un berretto con la visiera.",
  "special": false,
  "w": 12, "h": 6, "frames": 1,
  "anchor": "head-top",
  "origin": [5, 5],
  "outline": true,
  "hidePoses": ["run"],
  "perAvatar": { "demone": {"dx": 1, "dy": -1} },
  "pixels": [ [ "…12 simboli…", "… 6 righe …" ] ]
}
```

| Campo | Regola |
|---|---|
| `format`, `version` | `"calamaio-accessory"`, `1` |
| `id` | kebab-case, al massimo 24 caratteri, uguale al nome del file senza `.accessory.json` |
| `name` | non vuoto, al massimo 24 byte UTF‑8 (è il testo del menu «Accessorio») |
| `author` | non vuoto |
| `description` | al massimo 120 byte UTF‑8 |
| `special` | `true` se creato come premio speciale (specifica §4) |
| `w`, `h` | da 1 a **14** e da 1 a **10** |
| `frames` | `1` o `2`; sul display il fotogramma dell'accessorio è `f mod frames`, con lo stesso `f` della mascotte |
| `anchor` | uno tra `head-top`, `eyes`, `mouth`, `neck`, `hand` (§13.1) |
| `origin` | `[x, y]` dentro l'accessorio: il punto che va sul punto di aggancio |
| `outline` | contorno automatico di carta dove l'accessorio tocca l'inchiostro della mascotte (§14.2) |
| `hidePoses` | pose in cui non si disegna (anche vuoto) |
| `perAvatar` | facoltativo: ritocchi per id avatar, `{dx, dy}` (interi, \|valore\| ≤ 27) e/o `pixels` sostitutivi (stesse `w`, `h`, `frames`) |
| `pixels` | per fotogramma, `h` stringhe di `w` simboli |

Simboli: `.` trasparente, `#` inchiostro, `+` carta, **`x` inverso**: il colore opposto a quello già disegnato sotto (trasparente conta come carta, quindi diventa inchiostro). Con `x` un cappello è bianco sul corpo nero e nero sullo sfondo, senza disegnarlo due volte.

Errori di `validateAccessory()` (`tools/avatar/lib/accessory.mjs`): i campi e i limiti della tabella, campi sconosciuti, simboli diversi da `. # + x`, fotogramma vuoto, `hidePoses` con pose sconosciute o ripetute, ritocchi malformati, binario oltre 1 024 byte. Avvisi: i due fotogrammi uguali, nascosto in tutte le pose.

### 14.2 Come si disegna

Ordine: mascotte, poi contorno (se `outline`), poi accessorio, con `x` calcolato sui pixel già disegnati. Il riferimento eseguibile è `drawAccessory()` in `tools/avatar/lib/draw-accessory.mjs` (§14.4); firmware e simulatore ne fanno la copia esatta.

### 14.3 Binario per il display (CACC v1)

Little‑endian, come il CAVT (§8). Intestazione di 72 byte, poi i ritocchi, poi i fotogrammi.

| Offset | Tipo | Campo |
|---|---|---|
| 0 | 4 byte | magia `CACC` |
| 4 | u8 | versione = 1 |
| 5 | u8 | lunghezza dell'intestazione = 72 |
| 6 | u16 | lunghezza totale (≤ 1 024) |
| 8 | u32 | CRC‑32 (IEEE) dei byte da 12 alla fine: è anche la «versione» dell'accessorio in cache, `(id, crc)` |
| 12 | u8 | `w` (1..14) |
| 13 | u8 | `h` (1..10) |
| 14 | u8 | fotogrammi (1 o 2) |
| 15 | u8 | ancora: 0 `head-top`, 1 `eyes`, 2 `mouth`, 3 `neck`, 4 `hand` |
| 16 | u8 | `origin` x |
| 17 | u8 | `origin` y |
| 18 | u8 | opzioni: bit 0 `outline`, bit 1 `special`; gli altri a 0 |
| 19 | u8 | numero di ritocchi |
| 20 | u16 | `hidePoses`: bit *i* = posa *i* nascosta (ordine di §2.2); bit 10‑15 a 0 |
| 22 | u16 | byte per fotogramma = ⌈w·h/4⌉ |
| 24 | 24 byte | `id`, ASCII, completato con zeri |
| 48 | 24 byte | `name`, UTF‑8, completato con zeri |
| 72 | … | ritocchi, in ordine di id: `u8` lunghezza dell'id, id ASCII, `i8` dx, `i8` dy, `u8` 1 se seguono pixel sostitutivi (fotogrammi × byte per fotogramma), altrimenti 0 |
| … | … | fotogrammi: 2 bit per simbolo, riga per riga, primo simbolo nei bit alti come il CAVT; codici `00` `.`, `01` `#`, `10` `+`, `11` `x` |

Dimensione = 72 + fotogrammi × ⌈w·h/4⌉ + ritocchi [calcolato, verificato dai test]: da 73 byte (1×1) a 142 (14×10 a 2 fotogrammi) senza ritocchi; un ritocco costa 4 + lunghezza dell'id byte, più i pixel se ce li ha. Autore e descrizione restano nel JSON (il display non li mostra). `unpackAccessory()` rifiuta magia, versione, intestazione, lunghezza, CRC, dimensioni, ancora, origine, opzioni e pose sconosciute, ritocchi che sforano.

```sh
node tools/avatar/avatar.mjs accessory validate avatars/accessori/*.accessory.json
node tools/avatar/avatar.mjs accessory pack avatars/accessori/cappellino.accessory.json /tmp/cappellino.cacc
node tools/avatar/avatar.mjs accessory unpack /tmp/cappellino.cacc
```

### 14.4 Disegno di riferimento e controlli

`drawAccessory(griglia, avatar, accessorio, posa, f)` in `tools/avatar/lib/draw-accessory.mjs` modifica una griglia 28×25 (`'.'`, `'#'`, `'+'`) che contiene già la mascotte di quella posa e fotogramma:

1. non disegna niente (e restituisce `null`) se la **posa chiesta** è in `hidePoses` (anche quando l'avatar la disegna col ripiego: `tired` nascosta vale anche se si vede `idle`) o se l'ancora manca (`getAnchor` = `null`, §13);
2. fotogramma dell'accessorio `f mod frames`, ancora del fotogramma dell'avatar `f mod frames` dell'avatar;
3. ritocco `perAvatar[id]`: `pixels` al posto di quelli comuni, `dx`/`dy` sommati; angolo in alto a sinistra = ancora − `origin` + (`dx`, `dy`); i pixel fuori dal riquadro si tagliano;
4. **contorno** (se `outline`): diventano carta i pixel d'**inchiostro della mascotte** vicini in 8 direzioni a un pixel dell'accessorio che risulta inchiostro; mai sotto l'accessorio, mai sul trasparente (sulla carta non servirebbe);
5. **accessorio**: `#` inchiostro, `+` carta, `x` l'opposto di quello che c'è sotto dopo il contorno (inchiostro → carta; carta o trasparente → inchiostro).

È la funzione di riferimento: firmware e simulatore la copiano e i loro golden si generano da qui (fase 2). Casi provati in `tools/avatar/test/draw-accessory.test.mjs`: `x` sui tre fondi, contorno (e niente contorno attorno a un `x` diventato carta), pose nascoste, avatar senza ancore e punto `null` (griglia identica), ritocchi, `f mod frames`, ripiego della posa, taglio fuori riquadro.

`checkAccessory(accessorio, avatar[])` disegna l'accessorio su ogni avatar con le ancore × le 10 pose × i fotogrammi del display (0..max(frames)−1) e segnala, raggruppando i fotogrammi (`gatto working[0,2]: …`):

- **fuori dal riquadro**: pixel tagliati;
- **nuvoletta**: pixel dell'accessorio o del suo contorno nel riquadro x 17..27, y 0..8 nelle pose `working`, `permission`, `input`, `compact` (§3 regola 6). Conta anche il contorno: se tocca la coda della nuvoletta (17,8) la cancellerebbe; e conta il sobbalzo di `permission` (fotogrammi 1 e 3 un punto più in alto);
- **contrasto**: meno del **60%** dei pixel dell'accessorio distinguibili dal fondo (specifica §1.4). Distinguibile = colore finale diverso da quello sotto (il trasparente vale carta); con `outline`, anche un pixel d'inchiostro sull'inchiostro che tocca il contorno, perché è il contorno a staccarlo dal corpo [scelta di questo strumento: la specifica non dice come contare il contorno; senza, un papillon nero col filo bianco, che si legge benissimo, risulterebbe illeggibile]. La percentuale stampata è troncata, così «59%» non diventa «60%» sotto la soglia.

```sh
node tools/avatar/avatar.mjs accessory check avatars/accessori/*.accessory.json        # tutti gli avatar di avatars/, con le ancore
node tools/avatar/avatar.mjs accessory preview avatars/accessori/occhiali.accessory.json --png /tmp/o.png
node tools/avatar/avatar.mjs accessory preview avatars/accessori/occhiali.accessory.json --text --avatar gatto --pose done
```

Il foglio `accessori-<id>.png` (`accessorySheet()` in `lib/render.mjs`) mostra, per ogni avatar, le 10 pose × 4 fotogrammi a scala 3 e i fotogrammi 0‑1 a scala 1 (Contatori), con il riquadro della nuvoletta; un fotogramma con un problema ha il bordo rosso. `accessori-catalogo.png` mette tutti gli accessori su tutti gli avatar in quattro istantanee (lavora, permesso, finito al culmine del salto, corre). `library` li rigenera. I controlli non sostituiscono lo sguardo: il catalogo è passato da varianti che li superavano e non si leggevano (§14.5).

### 14.5 Il catalogo iniziale (`avatars/accessori/`)

Otto accessori, tutti senza problemi di `check` su Goccia, Gatto, Demone e Calamaro in tutte le pose [misurato: `accessory check`]. Dimensioni e CACC [misurato: `accessory validate`]:

| Accessorio | Ancora | Disegno | Tela | CACC | Nascosto in | Ritocchi |
|---|---|---|---|---|---|---|
| `cappellino` | head-top | berretto nero con visiera a sinistra (lontano dalla nuvoletta), riflesso in carta, fascia e tesa `x` (bianche dove toccano la testa) | 10×5 | 117 B | — | Gatto `dy −2` (staccato di una riga: la testa è a filo degli occhi); Demone: versione bassa 3 punti (cornetti, salto alto) |
| `corona` | head-top | tre punte, gemme in carta, cerchio `x` | 9×5 | 115 B | — | Gatto `dy −2`; Demone: versione bassa 3 punti |
| `occhiali` | eyes | montatura in alto, ponte a metà occhi, stanghette, tutto `x`: «⌐O‑O» attorno agli occhi, che restano le lenti e mantengono l'espressione | 14×10 | 196 B | error, idle, tired, stuffy, run | Gatto: senza montatura alta (gli occhi toccano la cima della testa); Demone (un occhio solo): monocolo, anello `x` a un punto dall'occhio e catenella |
| `sciarpa` | neck | capo a righe (`#`/`+`) che sventola fuori dalla sagoma, 2 fotogrammi; il giro sul collo è un tratto `x` corto che non tocca la bocca | 13×3 | 134 B | run | Calamaro `dy −1`, senza frange e con la radice in bianco (sopra i tentacoli); Demone `dx +3` (collo spostato) |
| `papillon` | neck | farfallino nero con `outline`: il filo di carta lo stacca dal corpo nero | 7×4 | 79 B | — | — |
| `cuffie` | head-top | archetto e padiglioni `x` col cuscinetto in carta | 14×10 | 196 B | run | Goccia e Calamaro di tre quarti (l'archetto gira sulla punta, il padiglione destro resta dietro la testa: lontano dalla coda della nuvoletta anche nel sobbalzo); Gatto: padiglioni sulle orecchie; Demone: di profilo, padiglione dietro la testa |
| `fiocco` | head-top | nastro nero con cappi, nodo, code; riflessi in carta | 7×5 | 131 B | run | Goccia e Calamaro `dx −4 dy −2` (accanto alla punta); Gatto `dy −3`; Demone: cappi e nodo, accanto ai cornetti |
| `mago` | head-top | cono nero con punta piegata, luna e stella in carta, tesa `x`; calato in testa | 13×8 | 191 B | run | Goccia `dy +3`; Calamaro `dy +3`: il mantello è già un cono, quindi solo punta piegata, stelle e tesa; Gatto `dy −1`; Demone: cono basso tra i cornetti |

Perché è venuto così (guardando i fogli, non dai numeri):

- **Il nero sul nero sparisce.** Un accessorio d'inchiostro sopra il corpo non si vede: si legge solo la parte che sta sul fondo. Quindi i cappelli stanno quasi tutti sopra la testa, e dove toccano il corpo usano `x` (diventano bianchi): la tesa bianca è ciò che separa il cappello dalla testa.
- **Le fasce `x` attraverso il corpo sembrano tagli.** Le prime sciarpe (una banda bianca sul collo) tagliavano la sagoma in due; le cuffie con archetto e padiglioni dentro la sagoma la facevano a pezzi. Funziona ciò che esce dalla sagoma: il capo della sciarpa che sventola, i padiglioni ai lati, la punta piegata del mago.
- **`outline` va bene per forme compatte (papillon), non per tratti sottili.** Sulle cuffie e sul fiocco il contorno ha cancellato mezza testa o la punta della Goccia.
- **Il salto di `done` comanda l'altezza.** Al culmine (fotogramma 2) la punta della Goccia è alla riga 1 e quella del Calamaro alla riga 0: per questo la sede dei cappelli sulle teste a punta è più bassa della punta (§13.6) e il Demone ha versioni di 3 punti.
- **Gli occhi bianchi a filo della testa (Gatto) o della bocca (Goccia) assorbono ogni bianco vicino**: le linee bianche stanno sempre a un punto d'inchiostro di distanza.
- **Corsa**: di profilo servirebbe un disegno diverso per ogni avatar e i ritocchi valgono per tutte le pose; occhiali, sciarpa, cuffie, fiocco e mago sono nascosti nella corsa (schermata dell'aggiornamento, pochi minuti), cappellino, corona e papillon restano.
