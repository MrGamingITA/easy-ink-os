# Temi delle schermate — formato v1

Fonte unica per servizio (`service/internal/theme`), firmware (`core/theme_*`, `ui/theme_draw`) e webapp
(`assets/js/theme/`). Spec: `docs/superpowers/specs/2026-10-15-editor-schermate-design.md`.

## 1. Il file `<id>.json`

| Campo | Tipo | Regole |
|---|---|---|
| `format` | string | `"calamaio-theme"` |
| `version` | int | `1` |
| `id` | string | kebab-case `[a-z0-9]+(-[a-z0-9]+)*`, 1–24 caratteri, mai `calamaio` (il tema di serie) |
| `name` | string | 1–24 byte UTF-8, senza caratteri di controllo |
| `screen` | string | `"250x122"` |
| `screens` | oggetto | almeno una schermata; chiave = schermata (§1.1), valore `{variants: [...]}` |
| `menu` | — | arriva con la fase 4: per ora è un errore |

### 1.1 Schermate

Personalizzabili (codice nel binario): `home` 1, `detail` 2, `sleep` 3 (Riposo), `offline` 4 (PC non raggiungibile),
`riepilogo` 5 (PC spento), `oggi` 6, `batteria` 7, `stato` 8, `limiti` 9, `focus` 10, `ambiente` 11, `pet` 12 (La mascotte).
Bloccate (errore «schermata bloccata: regole di sicurezza»): `perm`, `pair`, `confirm`, `boot`, `ota`, `battlow`, `calib`,
`gbload`, `gbplay`. Non ancora personalizzabili (errore): `list`, `wifiScan`, `qr`, `connecting`, `storico`, `guest`,
`servizi`, `agenda`, `game`.

### 1.2 Varianti

`variants`: 1–8 oggetti `{when, ref?, elements}`. La scheda usa la **prima** variante vera; nessuna vera → schermata di serie.
`when`: `always` · `none` (0 sessioni) · `one` (1) · `many` (2 o più). `ref` (sessione di riferimento dei valori
`{sessione.*}` e della condizione `state`): `selected` (predefinito: la selezionata, nel Dettaglio quella aperta) ·
`urgent` (la più urgente). Elementi: in ordine di disegno (gli ultimi sopra), al massimo 64 per variante contando le
righe dei riquadri lista e dopo aver sciolto i gruppi.

## 2. Elementi

Campi comuni: `type`, `x`, `y` (interi, ±512), `w`, `h` (0..512, dove servono), `visibleIf` (§4, al massimo 8 contando
quelle dei gruppi), `invert` (disegna in carta invece che in inchiostro; nel testo usa le varianti chiaro su scuro dei
caratteri), `hidden` (non si compila), e solo per l'editor `id` e `name` (≤ 32 byte) e `locked`. Un campo che il tipo non
conosce è un errore.

| `type` | Campi propri (predefinito) | Riquadro |
|---|---|---|
| `mascot` | `scale` 1–4 (1), `pose` `auto`·`working`·`permission`·`input`·`done`·`error`·`idle`·`compact`·`tired`·`stuffy` (`auto`), `ref` `selected`·`urgent`·`row`·`nobody` (la variante; in una riga `row`), `accessory` (true), `animated` (true) | 28·s × 25·s |
| `text` | `text` (modello, §3), `font` `{family: ui·pix, size, weight}`, `align` `left`·`center`·`right` (`left`), `lines` 1–8 (1), `lineHeight` 1–64 (size + 2), `upper` (false) | `w` obbligatoria (larghezza massima), y = bordo alto |
| `clock` | `format` `hm`·`hms` (`hm`), `font`, `align` | come `text` |
| `battery` | `scale` 1–2 (1), `percent` (false: la percentuale in Silkscreen 8 a destra dell'icona) | 16·s × 8·s |
| `wifi` | `scale` 1–2 (1) | 11·s × 9·s |
| `stateIcon` | `scale` 1–3 (1), `ref` come `mascot` | 7·s × 7·s |
| `bar` | `source` `context`·`tasks`·`workday`·`battery`·`limit5h`·`limit7d`, `style` `big`·`bar` (`big`) | `w`, `h` |
| `qr` | `text` (modello) | `w`, `h`: moduli quadrati, bordo di carta di un modulo |
| `sym` | `sym` `plug`·`bell`·`moon`·`lock`·`bolt`·`dot`·`glass`·`gauge`, `scale` 1–3 (1) | 7·s × 7·s |
| `line` | `dir` `h`·`v` (`h`), `style` `solid`·`dotted` (`solid`) | `w`, `h` |
| `rect` | `border` 0–8 (1), `fill` (false), `radius` 0–8 (0) | `w`, `h` |
| `circle` | `border` 0–8 (1), `fill` (false) | diametro = min(`w`, `h`) |
| `list` | `rowH` 1–122, `gap` 0–60 (0), `pageSec` 1–60 (5), `pageDots` (true), `row` (elementi con coordinate relative alla riga; niente `list` dentro) | `w`, `h` |
| `group` | `children` (elementi; coordinate relative al gruppo); solo `x`, `y`, `visibleIf`, `hidden`, `id`, `name`, `locked` | si scioglie nel binario |

## 3. Valori nei testi

Modello: testo con `{nome}` o `{nome|filtro}`; `{{` e `}}` = parentesi graffe. Filtri: `maiuscolo`, `minuscolo`.
Valore mancante = stringa vuota. Dopo l'espansione (al massimo 255 byte) il testo passa da `fitText`/`wrapLines`.
`riga.*` vale solo dentro un riquadro lista. Formati = quelli delle schermate di serie:

| Valore | Esempio | Regola |
|---|---|---|
| `sessione.nome`, `riga.nome` | `api-server` | nome |
| `.stato` | `Modifico src/api.ts` | `stateText`: in lavoro il dettaglio, a fine lavoro il riassunto (se c'è), altrimenti il testo lungo dello stato |
| `.attività` | `Bash: npm run test` | `lineFor` |
| `.riassunto` | `Guida aggiornata` | riassunto |
| `.meta` | `3/7 task  ·  12 min  ·  …` | `metaLine` |
| `.contesto` | `42%` | contesto in % |
| `.task` | `3/7` | vuoto senza task |
| `.da` | `12 min` · `1 h 05` | `ago` dall'inizio dello stato |
| `.sub` | `2` | agenti in corso, vuoto se 0 |
| `.test` | `✓` · `✗` | vuoto senza test |
| `ora` | `10:30` | `--:--` senza ora vera |
| `data` | `domenica 11 ottobre` | vuoto senza ora vera |
| `giorno` | `domenica` | vuoto senza ora vera |
| `sessioni` | `3` | sessioni mostrate |
| `inattesa` | `1` | sessioni che chiedono un permesso |
| `batteria` | `78%` | |
| `autonomia` | `Autonomia: circa 3 giorni con il tuo uso` | `autonomyText` |
| `wifi` | `CasaWiFi` | vuoto senza rete |
| `pc` | `calamaio.local` | `pcLabel` |
| `giornata.mancano` | `6 h 25` · `finita` | `workLine`, vuoto se spenta |
| `giornata.fine` | `18:00` | fine della giornata attiva, altrimenti l'orario di fine impostato |
| `oggi.task` | `4` | |
| `oggi.righe` | `+120 −30` | |
| `oggi.costo` | `1,20 $` | `%.2f` con la virgola |
| `limiti.5h`, `limiti.7g` | `37%` · `—` | `—` senza dati dal PC |
| `mascotte.livello` | `3` · `—` | gioco della mascotte |
| `mascotte.xp` | `66/417 XP` | vuoto senza dati |
| `mascotte.energia` | `80%` | vuoto senza dati |
| `co2` | `620 ppm` | vuoto senza sensore |
| `versione` | `0.13.0-ed` | versione del firmware |

**Glifi.** I caratteri Chakra Petch da 26 px in su hanno solo cifre, minuscole e «%+,-./:?…»; gli altri escono «?». Il compilatore avvisa (non rifiuta) per il testo fisso, per ciò che un valore può mostrare (alfabeto chiuso per i valori a formato fisso, testo libero per nome, stato, attività ecc.), per i filtri e per `upper`. Fonte comune di servizio ed editor: `protocol/themes/catalog.json`.

## 4. Condizioni (`visibleIf`, tutte vere = «e»)

Ogni condizione è un oggetto con una sola chiave: `{"perm": bool}` (sessioni che chiedono un permesso > 0),
`{"battBelow": 1..100}`, `{"charging": bool}`, `{"night": bool}` (Notturno 22-8), `{"pc": bool}` (PC collegato),
`{"workday": bool}` (giornata attiva), `{"game": bool}` (gioco della mascotte acceso), `{"sessions": {"min": 0..6,
"max": 0..6}}` (una o due condizioni), `{"state": "working"·"permission"·"input"·"error"·"compact"·"done"·"idle"}`
(stato della sessione di riferimento; in una riga quella della riga; senza sessione falsa), `{"blink": "even"·"odd"}`
(fotogramma pari/dispari).

## 5. Binario CTHM v1 (un file per tema, `/th/<id>.cthm` sulla scheda)

Little-endian. Il CRC-32 è IEEE (quello di zlib e di `avatarCrc32`) e fa anche da versione del tema: `(id, crc)`.

### 5.1 Intestazione (72 byte)

| Offset | Tipo | Campo |
|---|---|---|
| 0 | 4 byte | magia `CTHM` |
| 4 | u8 | versione = 1 |
| 5 | u8 | lunghezza dell'intestazione = 72 |
| 6 | u16 | riservato = 0 |
| 8 | u32 | CRC-32 dei byte da 12 alla fine |
| 12 | u32 | lunghezza totale del file (72..65 536) |
| 16 | u16 | larghezza dello schermo = 250 |
| 18 | u16 | altezza = 122 |
| 20 | 24 byte | `id`, ASCII, completato con zeri |
| 44 | 24 byte | `name`, UTF-8, completato con zeri |
| 68 | u8 | numero di schermate (0..12) |
| 69 | u8 | opzioni = 0 (la fase 4 userà il bit 0 per la sezione menu) |
| 70 | u16 | riservato = 0 |

### 5.2 Indice (8 byte per schermata, da 72)

`u8` codice della schermata (§1.1, crescente e senza doppioni), `u8` = 0, `u16` lunghezza del blocco (4..4 096), `u32`
posizione del blocco nel file. I blocchi seguono l'indice, nell'ordine dell'indice, senza sovrapporsi.

### 5.3 Blocco di una schermata (≤ 4 096 byte, autosufficiente)

| Offset | Tipo | Campo |
|---|---|---|
| 0 | u8 | varianti `nv` (1..8) |
| 1 | u8 | = 0 |
| 2 | u16 | inizio della tabella delle stringhe `so` (dal blocco) |
| 4 | nv × 6 | per variante: `u8` when (0 always, 1 none, 2 one, 3 many), `u8` ref (0 urgent, 1 selected), `u16` posizione del primo elemento, `u16` elementi di primo livello |
| … | | elementi delle varianti, uno dopo l'altro |
| `so` | | stringhe: `u8` lunghezza + byte UTF-8, una dopo l'altra fino alla fine del blocco |

Elemento: `u8` tipo (1 mascot, 2 text, 3 battery, 4 wifi, 5 stateIcon, 6 bar, 7 qr, 8 sym, 9 line, 10 rect, 11 circle,
12 list), `u8` opzioni (bit 0 invert, gli altri 0), `i16` x, `i16` y, `i16` w, `i16` h, `u8` condizioni `nc` (0..8),
`u8` byte dei dati `pl`, poi `nc` × 2 byte (`u8` codice, `u8` argomento), poi `pl` byte di dati:

| Tipo | Dati |
|---|---|
| mascot (4) | `scale` 1–4, posa (0 working · 1 permission · 2 input · 3 done · 4 error · 5 idle · 6 compact · 7 tired · 8 stuffy · 0xFF auto), ref (0 urgent · 1 selected · 2 row · 3 nobody · 4 quello della variante), opzioni (bit 0 accessorio, bit 1 animata) |
| text (7 + 3·n) | dimensione, peso/100 + 128 se Silkscreen, allineamento (0 sinistra, 1 centro, 2 destra), righe 1–8, interlinea 1–64, opzioni (bit 0 maiuscolo), `n` pezzi (≤ 16), poi i pezzi |
| battery (2) | `scale` 1–2, opzioni (bit 0 percentuale) |
| wifi (1) | `scale` 1–2 |
| stateIcon (2) | `scale` 1–3, ref |
| bar (2) | fonte (0 context · 1 tasks · 2 workday · 3 battery · 4 limit5h · 5 limit7d), stile (0 big · 1 bar) |
| qr (1 + 3·n) | `n` pezzi, poi i pezzi |
| sym (2) | simbolo (0 plug … 7 gauge, ordine di §2), `scale` 1–3 |
| line (2) | direzione (0 h, 1 v), stile (0 solid, 1 dotted) |
| rect (3) | bordo 0–8, riempimento 0/1, raggio 0–8 |
| circle (2) | bordo 0–8, riempimento 0/1 |
| list (6) | `rowH`, `gap`, `pageSec`, opzioni (bit 0 pallini), elementi della riga `nr` (1..32), = 0; seguono subito gli `nr` elementi della riga (coordinate relative alla riga, mai un'altra lista) |

Pezzo (3 byte): `u8` = tipo (bit 0–1: 0 testo fisso, 1 valore) + filtro (bit 2–3: 0, 1 maiuscolo, 2 minuscolo), `u16`
argomento = posizione della stringa **dalla tabella** (`so`) per il testo fisso, ID del valore (§3) per i valori.
I gruppi non esistono nel binario; `clock` diventa un `text` col valore `ora` (0x80) o `ora` con i secondi (0x81).

### 5.4 Condizioni

1 perm, 2 battBelow (1–100), 3 charging, 4 night, 5 pc, 6 workday, 7 game (argomento 0/1), 8 sessioni almeno, 9 sessioni
al massimo (0–6), 10 state (0 working · 1 permission · 2 input · 3 error · 4 compact · 5 done · 6 idle, ordine di
`SessionState`), 11 blink (0 pari, 1 dispari).

## 6. Come si disegna (interprete C++ e renderer JS)

Le stesse regole valgono per `ui/theme_draw` (C++) e per il renderer JS dell'editor: devono dare gli stessi pixel. Canvas
250 × 122, carta = 0, inchiostro = 1; ogni pixel fuori dal canvas si scarta.

- **Blocco e variante**: un blocco che non passa `checkBlock` non si disegna (falso: schermata di serie, canvas intatto).
  Variante: la prima con `when` vero; nessuna = schermata di serie. Sessione di riferimento: `urgent` = `urgentIdx`,
  `selected` = `sIdx` (nel Dettaglio `ui.sel`), limitata a 0..n−1; senza sessioni nessuna. Elementi nell'ordine (gli
  ultimi sopra); un elemento si disegna se le sue `visibleIf` sono tutte vere, col soggetto di `state` = la riga dentro
  una lista, altrimenti la sessione della variante.
- **`invert`**: il testo si disegna color carta (varianti chiaro su scuro dei caratteri); la mascotte scambia inchiostro e
  carta (anche nei punti dell'accessorio); riquadro e cerchio colorano i loro pixel di carta; gli altri elementi passano
  da una superficie che scambia inchiostro e carta in ogni `fillRect` (anche il quadrato di carta del QR diventa
  inchiostro). In una lista `invert` vale per i pallini; le righe hanno il loro.
- **`text`**: testo espanso (§3; `upper` dopo i filtri). Testo vuoto: niente. Àncora x: `left` = x, `center` = x + w/2,
  `right` = x + w; x e y si arrotondano con `Math.round` come in `T()`. Una riga (`lines` = 1): `T()` con `max = w` (con
  w = 0 nessun limite). Più righe: `wrapLines(testo, lines, size, w, peso, famiglia)` (al massimo `lines` righe) e la
  riga i con `T()` a y + i·`lineHeight`, `max = w`. Se il testo contiene il valore `ora` coi secondi (0x81, l'orologio
  `hms`), `nextChange` = now + 1000 − (ora locale in ms mod 1000), cioè il secondo dopo. La richiesta vale anche se il testo espanso
  è vuoto (si guarda il modello, non il risultato), purché le `visibleIf` siano vere.
- **`mascot`**: sessione dal `ref`; posa `auto` = `pose(m, s)`, senza sessione `idle`; fotogramma = f se `animated`,
  altrimenti 0; avatar = quello della sessione (`sessionAvatar`), senza sessione il predefinito; se `accessory` e c'è un
  accessorio indossato, si ridisegnano come `accOverlay` le celle che cambiano tra `avatarGrid` e `figureGrid`. Riquadro
  della mascotte = (x, y, 28·s, 25·s), anche se esce dal canvas.
- **`battery`**: `battIcon(x, y, s, batt)`; con `percent` «78%» (`Math.round(batt)` + «%») con `T()` in Silkscreen 400
  8, senza `max`, a (x + 16·s + 3, y + floor((8·s − 8)/2)).
- **`wifi`**: `wifiIcon(x, y, s, livello)`, livello = `rssiBars(rssi)` con la rete, −1 senza. **`stateIcon`**:
  `iconForState(chiave dello stato, x, y, s, f)`, niente senza sessione. **`sym`**: `sym(g, x, y, s)`.
- **`bar`**: sessione = la riga, altrimenti quella della variante. Percentuale: context = `ctx` della sessione, tasks =
  done/total·100 (0 senza task), workday = `workFrac`·100 (0 se spenta), battery = `batt`, limit5h/limit7d (0 senza
  dati); context e tasks senza sessione = 0. `big` = `bigBar(x, y, w, h, pct)`; `bar` = `bar(x, y, w, h, stato, f)` con stato =
  `{done, total, working: stato == working}` della sessione per `tasks` (se c'è), altrimenti
  `{done: Math.round(pct), total: 100, working: false}`.
- **`qr`**: testo espanso (senza `upper`) → `qr::encode` (correzione M, versioni 1–5). Testo vuoto o troppo lungo:
  niente, nemmeno il bordo. N = moduli per lato; modulo `px = max(1, floor(min(w, h) / (N + 2)))`; quadrato di carta
  `(N + 2)·px` × `(N + 2)·px` in (x, y), poi ogni modulo nero (c, r) come quadrato `px × px` in
  (x + px + c·px, y + px + r·px).
- **`line`**: `solid` = `fillRect(x, y, w, h)` d'inchiostro. `dotted` orizzontale: per ogni r < h un punto in
  (x + 3k, y + r) per ogni k con 3k < w (`dotsH(x, x + w, y + r)`); verticale: per ogni k < w un punto in (x + k, y + 2j)
  per ogni j con 2j < h (`dotsV(x + k, y, y + h)`).
- **`rect`** (bordo t, raggio r): il pixel (px, py) è «dentro» il riquadro (x, y, w, h) se ci sta (x ≤ px < x + w,
  y ≤ py < y + h; con w ≤ 0 o h ≤ 0 nessuno) e non è in un angolo tagliato: con dx = min(px − x, x + w − 1 − px) e
  dy = min(py − y, y + h − 1 − py), tagliato se dx < r e dy < r e dx + dy < r. Bordo: dentro il riquadro e non dentro
  quello rientrato (x + t, y + t, w − 2t, h − 2t) con raggio max(0, r − t); con t = 0 nessun bordo. Un pixel dentro si
  colora d'inchiostro se è di bordo o se `fill`, altrimenti resta com'è (l'interno non riempito è trasparente).
- **`circle`** (bordo t): d = min(w, h); il pixel (px, py) è dentro il cerchio di diametro D (stesso centro, in
  coordinate doppie (2x + d, 2y + d)) se D > 0 e (2px + 1 − 2x − d)² + (2py + 1 − 2y − d)² ≤ D². Si guardano i pixel
  del quadrato d × d in (x, y): bordo = dentro D = d e non dentro D = d − 2t (con t = 0 nessun bordo); colore come `rect`.
- **`list`** (riquadro x, y, w, h): righe per pagina `k = max(1, floor((h + gap) / (rowH + gap)))`, pagine
  `P = ceil(n / k)` (n = sessioni), pagina corrente `p = floor(now / (pageSec·1000)) mod P` (divisione e modulo verso
  −∞; con P ≤ 1 la pagina 0). Le sue `visibleIf` usano la sessione della variante; se false niente lista né righe. La
  riga i (0 ≤ i < k, sessione p·k + i se esiste) mette i suoi elementi spostati in (x, y + i·(rowH + gap)), con
  `riga.*`, `ref` `row` e `state` = quella sessione. Pallini (se `pageDots` e P > 1): per ogni pagina q uno slot in
  (x + w − 4·(P − q), y + h − 3); la corrente piena 3 × 3, le altre un punto in (+1, +1). Con P > 1 `nextChange` =
  (floor(now / (pageSec·1000)) + 1)·pageSec·1000, l'inizio della pagina dopo. Ordine: prima tutte le righe (riga per riga,
  ogni riga nell'ordine dei suoi elementi), poi i pallini, sopra le righe.
- **Risultato**: `nextChange` = il più vicino fra quelli chiesti (0 = nessun ridisegno a tempo). Riquadro della mascotte
  (per il Riposo in sonno profondo) = unione dei riquadri delle mascotte disegnate (con `visibleIf` vere).

## 7. Parità C++/JS

La prova è transitiva: il JS scrive il golden, il C++ lo rifà, `go test` controlla che il golden non sia vecchio.

- `protocol/themes/scenarios.json`: 11 scenari di modello. Fusione: `base`, poi lo scenario di `extends` (se c'è), poi lo
  scenario; gli oggetti campo per campo, array (`sessions`) e valori si sostituiscono. `now`, `local` e `frame` sono
  quelli finali della fusione: `since` (= now − `ago`) e la scadenza di `sel` si contano da quel `now`, anche per le
  sessioni ereditate da `extends` (`sei-dieci-minuti` salta di 10 minuti: con un'altra regola «N min» cambierebbe da
  un lato solo). `{versione}` resta fuori (dipende dal firmware).
- `node tools/golden/theme-golden.mjs` disegna con `render.js` ogni tema di `protocol/themes/fixtures` × schermata ×
  scenario (3 temi, 15 schermate, 165 casi) e scrive `firmware/test/test_theme_parity/golden.json` (CRC-32 dell'immagine
  nel formato di `Canvas::data()`, `drawn`, `nextChange`, riquadro della mascotte). `--check` confronta e basta; `--pbm DIR`
  scrive anche le immagini JS.
- `pio test -e native -f test_theme_parity` rifà i casi con `drawThemeBlock` sul `.cthm` compilato dal servizio; in caso
  di differenza scrive il PBM del firmware in `firmware/.pio/theme-parity/`. Il golden non si scrive mai a mano: una
  differenza si corregge nel lato che si allontana da §6.
- `go test ./internal/webui/` (`TestThemeRendererParity`) esegue `tools/fonts/export-js.mjs --check` e
  `theme-golden.mjs --check`; senza node il test si salta.

Il renderer JS riceve anche JSON a metà dall'editor e si comporta come la scheda: se un elemento di una qualunque
variante della schermata è fuori dai limiti di `checkBlock`/`checkElem` (coordinate, scala, righe, interlinea, carattere
inesistente, testo oltre 255 byte, valori `riga.*` o `ref` `row` fuori da una lista, più di 8 condizioni, 64 elementi o
8 varianti, lista con `rowH`, `pageSec` o righe fuori misura) l'anteprima è la schermata di serie (`drawn` falso, niente
disegnato), come una schermata senza varianti. Il limite di 4 096 byte del blocco lo controlla solo il servizio.

## 8. Sulla scheda e nel servizio

- **PC**: `<dati utente>/calamaio/themes/<id>.json` (indentato) e la cronologia `<id>/<n>.json` (ultime 20 versioni:
  ogni salvataggio, ripristino compreso, è una versione nuova). API in `docs/PROTOCOLLO.md` «Temi» e «API locale».
- **Scheda, archivio**: LittleFS `/th/<id>.cthm` (al massimo 8), `/th/order` = ordine d'installazione (riscritto
  passando da `/th/order.tmp`), tema attivo nella NVS `calamaio-th`, chiave `cur` (vuota o assente = di serie; si
  scrive solo se cambia). L'installazione è tutto o niente: download in `/th/tmp`, controllo completo, rinomina diretta
  sopra `/th/<id>.cthm`; solo dopo, con 9 temi, esce il più vecchio che non è né l'attivo né quello che il loop sta
  attivando (id protetto, `themefs::setProtected`); se la rinomina fallisce non esce niente. Le modifiche ai file dei
  temi e a `/th/order` dal task di rete e dal loop passano per un lucchetto (`themefs::lock`). All'avvio `/th/tmp` si
  toglie (download interrotto) e, se un riavvio è caduto fra la rinomina del nono e l'ordine, `orderTrim` riporta i temi
  a 8: toglie per primi quelli che l'ordine non conosce, cioè proprio quello appena arrivato (sicuro: il servizio non ha
  avuto il `theme-ack` e lo riofferta), mai l'attivo della NVS. Il Ripristino di fabbrica svuota `/th` e toglie `cur`.
- **Avvio**: il tema della NVS si controlla tutto (CRC del file e ogni blocco, `themeBootPlan`) prima del primo disegno.
  Rovinato o mancante → di serie, toast «Tema rovinato: uso quello di serie» (5 s, si vede sulla prima Home), file
  tolto, `cur` vuota, `themes` al PC. Prima di togliere un tema «rotto» (all'avvio, all'attivazione, al caricamento di
  un blocco) la scheda lo ricontrolla con `verifyStored`: se il file è integro (indice vecchio) si riattiva e non si
  cancella mai.
- **Disegno**: il blocco della schermata corrente sta in un buffer statico da 4 096 byte, ricaricato al cambio di
  schermata; un blocco che non passa i controlli → schermata di serie, mai a metà. Un tema con un orologio coi secondi o
  una lista a pagine chiede un ridisegno parziale a `nextChange`. Il buffer è l'inizio dell'**arena** del `Runtime`
  (6 144 byte): subito dopo stanno i buffer del disegno (testo espanso 256, righe 8 × 96, pezzi di `expand` 2 × 256,
  `qr::Bits` 180: 1 716 byte dei 2 048 riservati), usati solo dentro `drawThemeBlock`. In partita l'arena intera si
  presta alla cartuccia come l'anello del registro (tre pezzi interi da 2 KB dopo l'anello, solo a rete spenta e solo
  per le cartucce con più di 16 KB di RAM) e torna all'uscita; mentre è prestata `block()` e `drawScratch()` rispondono
  falso, nessuna schermata si disegna a tema e i temi nel loop non fanno niente; all'uscita il tema attivo si riattiva
  dall'archivio e il catalogo si rifà.
- **Scelta**: Menu › Display › Tema («✓» sull'attivo, cambio immediato, «Tema: <nome>») o dalla webapp (`theme-use`).
  La scelta si legge per prima nel loop e non si perde (anche durante una partita o un tema rotto).
- **Uscita di emergenza**: PRG tenuto 1,5 s, contati dal fronte di pressione (l'istante dell'interruzione), con la
  pressione cominciata mentre c'è la schermata di avvio → tema di serie per questo avvio (toast «Tema di serie», 5 s),
  la scelta salvata resta. Finché la pressione non è decisa la schermata di avvio resta, al massimo 3 s dalla pressione;
  il rilascio di quella pressione non arriva al menu. Scegliere «Calamaio» dal menu durante l'uscita lo rende stabile
  (`cur` vuota). Limiti: col tasto già giù all'accensione la ROM dell'ESP32-S3 entra nel caricatore seriale (PRG è
  GPIO0), e una pressione nei primi ~200 ms del setup, prima che l'interruzione sia collegata, non si vede. Senza tema
  attivo non fa niente.
- **Riposo in sonno profondo**: se il Riposo l'ha disegnato il tema (`RestKeep` salva id e CRC solo in quel caso), il
  risveglio dell'ora monta LittleFS senza mai formattare (`themefs::mountNoFormat`; se non monta → risveglio pieno) e
  rilegge solo intestazione e indice (`activateKnown`: niente NVS né rete, niente CRC di tutto il file, basta il CRC
  dell'intestazione uguale a quello salvato col Riposo) e il blocco del Riposo, che passa comunque i controlli; tema
  cambiato, blocco che non si disegna o mascotte che si sposta → risveglio pieno. Disegna col modello ricostruito dal
  sonno: valgono ora, data, giorno, sessioni, batteria e giornata; gli altri valori escono vuoti fino al risveglio pieno.
  Un Riposo che cambia col fotogramma o col tempo (lampeggio) fa un risveglio pieno a ogni minuto: sicuro, costa
  energia. Un tema senza Riposo lascia i risvegli dell'ora leggeri come quelli di serie.
- **Editor** (webapp «Editor schermate», `editor.html`; anche nell'app iPhone): disegna con lo stesso renderer della
  scheda (§6–7). Salvataggio automatico: `POST /api/themes/check` 300 ms dopo ogni modifica (problemi, avvisi, byte per
  schermata, senza salvare), `PUT /api/themes/<id>?autosave=1&base=<ms>` dopo 1,5 s di quiete (409 se cambiato altrove:
  «Ricarica» o «Sovrascrivi», mai in silenzio); un tema con problemi resta in memoria e non si salva. Avvisi (mai
  errori): glifi che il carattere non ha (`protocol/themes/catalog.json`), testo che non ci sta, fuori dallo schermo,
  coperto, schermata oltre il 90 % dei 4 096 byte, schermata di serie al posto di quella del tema. «Invia alla scheda»
  salva prima, rifiuta un tema con problemi, in conflitto o con una schermata che la scheda scarterebbe, poi segue
  `displays[].themes.job` in `/api/status`.
- **Sempre di serie** (spec §3): perm, pair, confirm, boot, ota, battlow, calib, gbload, gbplay; il servizio rifiuta un
  tema che le contiene (§1.1).
