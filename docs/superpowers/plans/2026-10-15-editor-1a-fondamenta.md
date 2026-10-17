# Editor delle schermate, piano 1a: formato, compilatore, interprete e renderer JS — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un tema scritto a mano in JSON si controlla e si compila nel servizio in un file `CTHM` v1; il firmware lo legge, lo controlla e lo disegna (interprete C++ in `env:native`) e la webapp lo disegna uguale pixel per pixel (renderer JS con i caratteri della scheda esportati). Niente rete: protocollo, archivio sulla scheda, menu e uscita di emergenza sono nel piano 1b.

**Architecture:** Il JSON (`docs/TEMI.md`) è la fonte; il servizio Go (`service/internal/theme`) lo controlla e lo «abbassa» in blocchi compatti per schermata (≤ 4 096 byte, stringhe nel blocco) dentro un file con intestazione e CRC come il CAVT. Il firmware ha un lettore puro (`core/theme_format`), valori e condizioni (`core/theme_values`), l'interprete di disegno (`ui/theme_draw`, che usa `drawText`, `drawAvatar`, `prims`, `qr` tramite `ui/kit.h`) e un `Runtime` con buffer statico da 4 096 byte (`core/theme_runtime`) agganciato a `ui::draw`. Il renderer JS (`service/internal/webui/static/assets/js/theme/`) ripete lo stesso algoritmo sul JSON; la parità si prova con un golden generato dal JS (`tools/golden/theme-golden.mjs`) che il test C++ confronta e che `go test` ricontrolla.

**Tech Stack:** Go 1.24 (solo libreria standard + `santhosh-tekuri/jsonschema/v6` nei test), C++ (scheda: GCC 8.4 `-std=gnu++11`; test native C++17, Unity, ArduinoJson 7.4.3), JavaScript ES moduli (browser e Node 22 senza dipendenze).

**Spec:** `docs/superpowers/specs/2026-10-15-editor-schermate-design.md` (§1, §2, §3 «Interprete» e «Robustezza», §7 «Renderer JS», §8 punto 1, §9). Piano gemello: `docs/superpowers/plans/2026-10-15-editor-1b-fondamenta.md` (parte dopo questo).

## Global Constraints

- Ramo `editor`, firmware di partenza 0.12.5-gb. La versione diventa **0.13.0-ed** nel piano 1b (Task B7), non qui.
- Schermo dei temi: **`250x122`** (classe S). Limiti: **4 096 byte per schermata, 64 KB (65 536 byte) per tema, 8 temi sulla scheda**; coordinate x, y in **±512**, w, h in **0..512**.
- Formato binario **`CTHM` v1**, little-endian, intestazione di **72 byte**, CRC-32 IEEE (come `avatarCrc32`) dei byte da 12 alla fine — dettagli in `docs/TEMI.md` §5 (Task 2), fonte unica per Go, C++ e JS.
- **Permessi** (ScreenId::Perm), Pair, Confirm e le schermate di sistema (Boot, Ota, BattLow, Calib, GbLoad, GbPlay) **sempre di serie**: il compilatore rifiuta le loro chiavi, il formato non ha un codice per loro e `ui::draw` non le passa mai all'interprete (ISTRUZIONI-SVILUPPO §7, non negoziabile).
- Caratteri: solo le 22 combinazioni di `firmware/src/gfx/fonts/f_registry.h` (Chakra Petch 500: 10, 11, 12; 600: 10, 11, 12, 13; 700: 10, 12, 13, 14, 15, 16, 18, 20, 22, 26, 40, 44, 46; Silkscreen 400 e 700: 8). Mai caratteri nuovi.
- Nessuna allocazione nello heap durante il disegno sulla scheda: buffer statici (`static`), testo espanso al massimo 255 byte (+ terminatore), troncato su un confine UTF-8.
- Elemento o versione sconosciuti, blocco rovinato → **schermata di serie, mai a metà**; valore mancante → stringa vuota; fuori schermo → tagliato dal canvas.
- Codice compilato per la scheda (`core/`, `ui/`, `gfx/`): niente `namespace a::b {` (si scrive `namespace cal { namespace theme {`), niente inizializzatori nei membri di struct inizializzate con le graffe, `constexpr` solo a una riga.
- Testi a schermo in italiano, identificatori in inglese, commenti brevi in italiano. Nomi nel JSON in inglese; i **valori dei testi** (`{sessione.nome}`…) e i **filtri** (`|maiuscolo`, `|minuscolo`) restano in italiano come nella spec §2.
- Messaggi di commit in italiano nello stile del repository («Area: cosa»), **senza** riga `Co-Authored-By`.
- Nessun flash in questo piano. Prove: `cd service && go test ./...` · `cd firmware && pio test -e native` (oggi 722 verdi) · `cd firmware && pio run -e heltec213` (immagine ≤ 2 359 296 byte) · `cd tools/webui-dev && npm test`.

## Review Focus

- **Testo che non entra o con caratteri che il carattere grande non ha** (maiuscole a 40 px): C++ e JS disegnano lo stesso «?» e lo stesso «…», mai un crash — scenari `sei` e `notte` della parità (Task 10) su `elementi.theme.json` (testi lunghi a 40 px) e avviso del compilatore (Task 1, `TestParseWarnsBigFontChars`).
- **Valore lunghissimo dentro un modello** (riassunto di 127 byte + testo fisso, più di 255 byte): troncato su un confine UTF-8, mai fuori dal buffer — `test_expand_truncates_utf8` (Task 4) e lo stesso caso in `test-theme.mjs` (Task 9).
- **Elementi del tutto o in parte fuori schermo** (x = −512, w = 512, cerchi sul bordo): niente scritture fuori dal canvas, stessi pixel in C++ e JS — `test_offscreen_elements_clip` (Task 5) e i rettangoli fuori schermo di `elementi.theme.json` (Task 10).
- **Riquadro lista con 0 sessioni, con più sessioni delle righe e allo scatto della pagina**: niente righe, pagina giusta, `nextChange` al confine — `test_list_paging` (Task 5), scenari `zero`, `sei`, `sei-pagina2` (Task 10).
- **Blocco rovinato o troncato in un punto qualunque**: `checkBlock` rifiuta senza leggere fuori, l'interprete non disegna niente (schermata di serie) — `test_check_block_every_truncation` e `test_check_block_byte_flips` (Task 3), `test_broken_active_theme_falls_back` (Task 6).

---

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `docs/TEMI.md` | formato JSON (§1–4), valori, condizioni, binario CTHM (§5), regole di disegno (§6) | 1, 2, 5 |
| `protocol/schema/theme.schema.json` | schema JSON del tema (per editor e documentazione) | 1 |
| `protocol/themes/fixtures/{prova,elementi,lista}.theme.json` e `.cthm` | temi di prova scritti a mano e loro compilazione | 1, 2 |
| `protocol/themes/scenarios.json` | dati di prova della parità (modello in forma neutra) | 10 |
| `service/internal/theme/{theme,values,fonts,template,parse,compile}.go` | controllo del JSON e compilazione in CTHM | 1, 2 |
| `firmware/src/core/theme_format.{h,cpp}` | lettura e controllo di CTHM (interprete in sola lettura) | 3 |
| `firmware/src/core/theme_values.{h,cpp}` | valori dei testi, condizioni, sessione di riferimento, varianti | 4 |
| `firmware/src/ui/kit.h`, `firmware/src/ui/screens.cpp` (in fondo) | accesso dell'interprete a `T()`, mascotte con accessorio | 5 |
| `firmware/src/ui/theme_draw.{h,cpp}` | interprete di disegno | 5 |
| `firmware/src/core/theme_runtime.{h,cpp}` | archivio astratto, `Runtime` (buffer da 4 096 byte), `SafeBoot` | 6 |
| `firmware/src/ui/screens.cpp` (`drawScreen`, `sleepMascotRect`) | aggancio dei temi alle schermate personalizzabili | 6 |
| `tools/fonts/export-js.mjs`, `assets/js/theme/fonts.js`, `assets/js/theme/text.js` | caratteri della scheda in JS e testo come `text.cpp` | 7 |
| `assets/js/theme/{surface,prims,qr}.js` | superficie 1 bit, primitive come `prims.cpp`, QR come `qrcode_ricmoo` | 8 |
| `assets/js/theme/{values,lower}.js` | valori e condizioni come `theme_values`, abbassamento del JSON come il compilatore | 9 |
| `assets/js/theme/render.js` | interprete JS | 10 |
| `tools/golden/theme-golden.mjs`, `firmware/test/test_theme_parity/*`, `service/internal/webui/theme_parity_test.go` | parità C++/JS | 10 |
| `tools/webui-dev/test-theme.mjs`, `tools/webui-dev/package.json` | prove JS | 7–10 |

(`assets/js/theme/` = `service/internal/webui/static/assets/js/theme/`.)

---

### Task 1: Formato JSON dei temi e controlli nel servizio

**Files:**
- Create: `docs/TEMI.md` (§1–§4), `protocol/schema/theme.schema.json`, `protocol/themes/fixtures/prova.theme.json`, `protocol/themes/fixtures/elementi.theme.json`, `protocol/themes/fixtures/lista.theme.json`
- Create: `service/internal/theme/theme.go`, `values.go`, `fonts.go`, `template.go`, `parse.go`
- Test: `service/internal/theme/parse_test.go`, `service/internal/theme/fonts_test.go`

**Interfaces:**
- Consumes: niente.
- Produces (package `calamaio/internal/theme`):
  - costanti `Format = "calamaio-theme"`, `Version = 1`, `ScreenS = "250x122"`, `ReservedID = "calamaio"`, `MaxBlock = 4096`, `MaxFile = 65536`, `HeaderLen = 72`, `MaxVariants = 8`, `MaxElements = 64`, `MaxConds = 8`, `MaxSegs = 16`, `MaxRow = 32`, `MaxLiteral = 255`, `CoordMax = 512`;
  - `type Problem struct { Path, Msg string }` (JSON `path`, `msg`), `func (Problem) String() string`;
  - `type Theme struct { Format string; Version int; ID, Name, Screen string; Screens map[string]*Layout }`, `type Layout struct { Variants []*Variant }`, `type Variant struct { When, Ref string; Elements []*Element }`, `type Element` (campi sotto), `type Font struct { Family string; Size, Weight int }`, `type Cond struct { Op byte; Arg byte }`;
  - `func Parse(data []byte) (*Theme, []Problem, []string)` — tema, errori (vuoto = valido), avvisi;
  - `func ScreenCode(key string) (byte, bool)`, `var ScreenKeys = []string{"home","detail","sleep","offline","riepilogo","oggi","batteria","stato","limiti","focus","ambiente","pet"}` (codice = indice + 1);
  - `func FontOK(family string, size, weight int) bool`, `var Fonts []FontSpec` (`FontSpec{Family string; Size, Weight int}`);
  - `type ValueSpec struct { Name string; ID uint16; Row bool; Example string }`, `var Values []ValueSpec`, `func ValueID(name string) (uint16, bool)`;
  - `type Seg struct { Lit string; Val uint16; IsVal bool; Filter byte }`, `func ParseTemplate(s string, inRow bool) ([]Seg, error)`.

- [ ] **Step 1: Scrivere `docs/TEMI.md` §1–§4**

Il documento è la fonte unica del formato (come `docs/AVATAR.md` per il CAVT). Contenuto, in italiano, con queste tabelle esatte (il §5 lo aggiunge il Task 2, il §6 il Task 5):

```markdown
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
| `.stato` | `Sta lavorando` | `stateText` |
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

## 4. Condizioni (`visibleIf`, tutte vere = «e»)

Ogni condizione è un oggetto con una sola chiave: `{"perm": bool}` (sessioni che chiedono un permesso > 0),
`{"battBelow": 1..100}`, `{"charging": bool}`, `{"night": bool}` (Notturno 22-8), `{"pc": bool}` (PC collegato),
`{"workday": bool}` (giornata attiva), `{"game": bool}` (gioco della mascotte acceso), `{"sessions": {"min": 0..6,
"max": 0..6}}` (una o due condizioni), `{"state": "working"·"permission"·"input"·"error"·"compact"·"done"·"idle"}`
(stato della sessione di riferimento; in una riga quella della riga; senza sessione falsa), `{"blink": "even"·"odd"}`
(fotogramma pari/dispari).
```

- [ ] **Step 2: Scrivere i tre temi di prova**

`protocol/themes/fixtures/prova.theme.json` (il tema della prova dal vivo, piano 1b):

```json
{
  "format": "calamaio-theme", "version": 1, "id": "prova", "name": "Mascotte grande", "screen": "250x122",
  "screens": {
    "home": { "variants": [
      { "when": "none", "elements": [
        { "type": "mascot", "x": 83, "y": 6, "scale": 3, "pose": "idle", "ref": "nobody" },
        { "type": "text", "x": 0, "y": 86, "w": 250, "text": "Nessuna sessione", "font": { "family": "ui", "size": 13, "weight": 700 }, "align": "center" },
        { "type": "clock", "x": 4, "y": 110, "w": 60, "font": { "family": "pix", "size": 8, "weight": 400 } },
        { "type": "battery", "x": 230, "y": 110 }
      ] },
      { "when": "one", "elements": [
        { "type": "mascot", "x": 2, "y": 11, "scale": 4 },
        { "type": "text", "x": 120, "y": 8, "w": 126, "text": "{sessione.nome|maiuscolo}", "font": { "family": "ui", "size": 14, "weight": 700 } },
        { "type": "text", "x": 120, "y": 28, "w": 126, "text": "{sessione.stato}", "font": { "family": "ui", "size": 10, "weight": 500 }, "lines": 3, "lineHeight": 12 },
        { "type": "bar", "x": 120, "y": 70, "w": 126, "h": 8, "source": "tasks", "style": "bar" },
        { "type": "text", "x": 120, "y": 82, "w": 126, "text": "{sessione.task} · {sessione.da}", "font": { "family": "ui", "size": 10, "weight": 600 } },
        { "type": "clock", "x": 120, "y": 110, "w": 60, "font": { "family": "pix", "size": 8, "weight": 400 } },
        { "type": "battery", "x": 230, "y": 110, "visibleIf": [ { "charging": false } ] },
        { "type": "sym", "x": 238, "y": 110, "sym": "bolt", "visibleIf": [ { "charging": true } ] }
      ] },
      { "when": "many", "elements": [
        { "type": "list", "x": 0, "y": 0, "w": 250, "h": 104, "rowH": 34, "gap": 1, "pageSec": 5, "row": [
          { "type": "mascot", "x": 2, "y": 4, "scale": 1 },
          { "type": "text", "x": 34, "y": 2, "w": 150, "text": "{riga.nome}", "font": { "family": "ui", "size": 13, "weight": 700 } },
          { "type": "text", "x": 186, "y": 4, "w": 60, "text": "{riga.task}", "font": { "family": "ui", "size": 10, "weight": 700 }, "align": "right" },
          { "type": "bar", "x": 34, "y": 20, "w": 212, "h": 7, "source": "tasks", "style": "bar" },
          { "type": "line", "x": 0, "y": 33, "w": 250, "h": 1, "style": "dotted" }
        ] },
        { "type": "clock", "x": 4, "y": 110, "w": 60, "font": { "family": "pix", "size": 8, "weight": 400 } },
        { "type": "text", "x": 70, "y": 110, "w": 150, "text": "{sessioni} sessioni", "font": { "family": "pix", "size": 8, "weight": 700 }, "align": "center", "visibleIf": [ { "perm": false } ] },
        { "type": "text", "x": 70, "y": 110, "w": 150, "text": "{inattesa} in attesa", "font": { "family": "pix", "size": 8, "weight": 700 }, "align": "center", "upper": true, "visibleIf": [ { "perm": true }, { "blink": "even" } ] },
        { "type": "battery", "x": 230, "y": 110 }
      ] }
    ] },
    "sleep": { "variants": [ { "when": "always", "elements": [
      { "type": "clock", "x": 8, "y": 8, "w": 140, "font": { "family": "ui", "size": 44, "weight": 700 } },
      { "type": "text", "x": 8, "y": 58, "w": 140, "text": "{data}", "font": { "family": "ui", "size": 11, "weight": 600 } },
      { "type": "text", "x": 8, "y": 74, "w": 140, "text": "{giornata.mancano}", "font": { "family": "ui", "size": 12, "weight": 700 }, "visibleIf": [ { "workday": true } ] },
      { "type": "mascot", "x": 158, "y": 30, "scale": 3, "ref": "nobody", "pose": "idle" }
    ] } ] }
  }
}
```

`protocol/themes/fixtures/elementi.theme.json` — ogni tipo, ogni valore, ogni condizione, `invert`, bordi e fuori schermo
(le coordinate fuori schermo sono volute):

```json
{
  "format": "calamaio-theme", "version": 1, "id": "elementi", "name": "Tutti gli elementi", "screen": "250x122",
  "screens": {
    "detail": { "variants": [ { "when": "always", "elements": [
      { "type": "rect", "x": 0, "y": 0, "w": 250, "h": 15, "border": 0, "fill": true },
      { "type": "text", "x": 4, "y": 4, "w": 170, "text": "{sessione.nome}", "font": { "family": "pix", "size": 8, "weight": 700 }, "upper": true, "invert": true },
      { "type": "text", "x": 176, "y": 2, "w": 70, "text": "ctx {sessione.contesto}", "font": { "family": "ui", "size": 10, "weight": 700 }, "align": "right", "invert": true },
      { "type": "stateIcon", "x": 4, "y": 20, "scale": 2 },
      { "type": "text", "x": 22, "y": 19, "w": 224, "text": "{sessione.attività}", "font": { "family": "ui", "size": 12, "weight": 600 } },
      { "type": "text", "x": 4, "y": 36, "w": 242, "text": "{sessione.meta}", "font": { "family": "ui", "size": 10, "weight": 500 }, "lines": 4, "lineHeight": 12 },
      { "type": "text", "x": 4, "y": 86, "w": 120, "text": "test {sessione.test} · sub {sessione.sub} · da {sessione.da}", "font": { "family": "ui", "size": 10, "weight": 600 } },
      { "type": "text", "x": 4, "y": 98, "w": 242, "text": "{sessione.riassunto|minuscolo}", "font": { "family": "ui", "size": 11, "weight": 500 } },
      { "type": "bar", "x": 4, "y": 110, "w": 242, "h": 8, "source": "context" }
    ] } ] },
    "oggi": { "variants": [ { "when": "always", "elements": [
      { "type": "text", "x": 0, "y": 4, "w": 125, "text": "{oggi.task}", "font": { "family": "ui", "size": 22, "weight": 700 }, "align": "center" },
      { "type": "text", "x": 125, "y": 4, "w": 125, "text": "{oggi.righe}", "font": { "family": "ui", "size": 16, "weight": 700 }, "align": "center" },
      { "type": "line", "x": 124, "y": 2, "w": 1, "h": 60, "dir": "v", "style": "dotted" },
      { "type": "rect", "x": 10, "y": 66, "w": 110, "h": 40, "border": 2, "radius": 3 },
      { "type": "rect", "x": 130, "y": 66, "w": 110, "h": 40, "border": 1, "radius": 8, "fill": true },
      { "type": "text", "x": 130, "y": 78, "w": 110, "text": "{oggi.costo}", "font": { "family": "ui", "size": 15, "weight": 700 }, "align": "center", "invert": true },
      { "type": "rect", "x": -20, "y": 110, "w": 300, "h": 30, "border": 3 },
      { "type": "text", "x": 240, "y": 30, "w": 60, "text": "FUORI SCHERMO", "font": { "family": "ui", "size": 13, "weight": 700 } }
    ] } ] },
    "batteria": { "variants": [ { "when": "always", "elements": [
      { "type": "battery", "x": 6, "y": 6, "scale": 2, "percent": true },
      { "type": "bar", "x": 6, "y": 30, "w": 238, "h": 9, "source": "battery", "style": "bar" },
      { "type": "text", "x": 6, "y": 44, "w": 238, "text": "{autonomia}", "font": { "family": "ui", "size": 11, "weight": 500 }, "lines": 2 },
      { "type": "sym", "x": 6, "y": 80, "sym": "plug" },
      { "type": "sym", "x": 16, "y": 80, "sym": "bell", "scale": 2 },
      { "type": "sym", "x": 34, "y": 80, "sym": "moon", "scale": 3 },
      { "type": "sym", "x": 58, "y": 80, "sym": "lock" },
      { "type": "sym", "x": 68, "y": 80, "sym": "bolt" },
      { "type": "sym", "x": 78, "y": 80, "sym": "dot" },
      { "type": "sym", "x": 88, "y": 80, "sym": "glass" },
      { "type": "sym", "x": 98, "y": 80, "sym": "gauge", "invert": true },
      { "type": "text", "x": 120, "y": 100, "w": 126, "text": "SCARICA", "font": { "family": "ui", "size": 14, "weight": 700 }, "visibleIf": [ { "battBelow": 15 }, { "charging": false } ] }
    ] } ] },
    "stato": { "variants": [ { "when": "always", "elements": [
      { "type": "wifi", "x": 6, "y": 6 },
      { "type": "wifi", "x": 20, "y": 4, "scale": 2 },
      { "type": "text", "x": 50, "y": 6, "w": 196, "text": "{wifi} · {pc}", "font": { "family": "ui", "size": 12, "weight": 600 } },
      { "type": "text", "x": 6, "y": 30, "w": 238, "text": "{giorno} {ora} — {data|maiuscolo}", "font": { "family": "ui", "size": 12, "weight": 700 } },
      { "type": "clock", "x": 6, "y": 50, "w": 238, "format": "hms", "font": { "family": "ui", "size": 26, "weight": 700 }, "align": "center" },
      { "type": "text", "x": 6, "y": 90, "w": 238, "text": "{sessioni} SESSIONI, {inattesa} IN ATTESA", "font": { "family": "pix", "size": 8, "weight": 400 } },
      { "type": "text", "x": 6, "y": 104, "w": 238, "text": "NOTTE", "font": { "family": "ui", "size": 12, "weight": 700 }, "visibleIf": [ { "night": true }, { "pc": true } ] },
      { "type": "text", "x": 120, "y": 104, "w": 120, "text": "{{graffe}}", "font": { "family": "ui", "size": 12, "weight": 600 }, "visibleIf": [ { "sessions": { "min": 1, "max": 3 } } ] }
    ] } ] },
    "limiti": { "variants": [ { "when": "always", "elements": [
      { "type": "text", "x": 6, "y": 4, "w": 120, "text": "5 ORE", "font": { "family": "pix", "size": 8, "weight": 700 } },
      { "type": "text", "x": 126, "y": 2, "w": 118, "text": "{limiti.5h}", "font": { "family": "ui", "size": 13, "weight": 700 }, "align": "right" },
      { "type": "bar", "x": 6, "y": 16, "w": 238, "h": 9, "source": "limit5h" },
      { "type": "text", "x": 126, "y": 40, "w": 118, "text": "{limiti.7g}", "font": { "family": "ui", "size": 13, "weight": 700 }, "align": "right" },
      { "type": "bar", "x": 6, "y": 54, "w": 238, "h": 9, "source": "limit7d", "style": "bar" },
      { "type": "bar", "x": 6, "y": 70, "w": 238, "h": 9, "source": "workday" },
      { "type": "text", "x": 6, "y": 84, "w": 238, "text": "{giornata.mancano} fino alle {giornata.fine}", "font": { "family": "ui", "size": 10, "weight": 600 } },
      { "type": "text", "x": 6, "y": 98, "w": 238, "text": "Più di quanto ci sta in una riga sola, anche con tante parole: àèìòù …", "font": { "family": "ui", "size": 40, "weight": 700 } }
    ] } ] },
    "focus": { "variants": [ { "when": "always", "elements": [
      { "type": "qr", "x": 2, "y": 2, "w": 70, "h": 70, "text": "123456" },
      { "type": "qr", "x": 80, "y": 2, "w": 80, "h": 80, "text": "HTTP://CALAMAIO.LOCAL" },
      { "type": "qr", "x": 166, "y": 2, "w": 82, "h": 82, "text": "Ciao è {sessioni} {sessione.nome}" },
      { "type": "qr", "x": 2, "y": 90, "w": 30, "h": 30, "text": "{sessione.meta}{sessione.meta}{sessione.meta}" }
    ] } ] },
    "ambiente": { "variants": [ { "when": "always", "elements": [
      { "type": "circle", "x": 10, "y": 10, "w": 40, "h": 40, "fill": true },
      { "type": "circle", "x": 60, "y": 10, "w": 41, "h": 50, "border": 3 },
      { "type": "circle", "x": 230, "y": -10, "w": 40, "h": 40, "border": 2, "invert": true },
      { "type": "text", "x": 110, "y": 20, "w": 130, "text": "{co2}", "font": { "family": "ui", "size": 18, "weight": 700 } },
      { "type": "line", "x": 0, "y": 70, "w": 250, "h": 2 },
      { "type": "line", "x": 0, "y": 80, "w": 250, "h": 3, "style": "dotted" }
    ] } ] },
    "pet": { "variants": [ { "when": "always", "elements": [
      { "type": "mascot", "x": 2, "y": 4, "scale": 3, "pose": "done", "ref": "nobody" },
      { "type": "mascot", "x": 100, "y": 4, "scale": 2, "accessory": false, "animated": false },
      { "type": "text", "x": 160, "y": 4, "w": 86, "text": "LIVELLO {mascotte.livello}", "font": { "family": "ui", "size": 14, "weight": 700 } },
      { "type": "text", "x": 160, "y": 24, "w": 86, "text": "{mascotte.xp}", "font": { "family": "ui", "size": 10, "weight": 600 } },
      { "type": "text", "x": 160, "y": 36, "w": 86, "text": "{mascotte.energia}", "font": { "family": "ui", "size": 10, "weight": 600 }, "visibleIf": [ { "game": true } ] },
      { "type": "group", "x": 100, "y": 60, "visibleIf": [ { "state": "working" } ], "children": [
        { "type": "rect", "x": 0, "y": 0, "w": 60, "h": 20, "fill": true },
        { "type": "text", "x": 0, "y": 5, "w": 60, "text": "LAVORA", "font": { "family": "pix", "size": 8, "weight": 700 }, "align": "center", "invert": true },
        { "type": "rect", "x": 70, "y": 0, "w": 10, "h": 10, "hidden": true }
      ] },
      { "type": "mascot", "x": -40, "y": 100, "scale": 2, "pose": "error", "ref": "urgent" }
    ] } ] },
    "offline": { "variants": [ { "when": "always", "elements": [
      { "type": "mascot", "x": 6, "y": 20, "scale": 3, "pose": "idle", "ref": "nobody" },
      { "type": "text", "x": 98, "y": 30, "w": 146, "text": "PC non raggiungibile", "font": { "family": "ui", "size": 18, "weight": 700 }, "lines": 2, "lineHeight": 20 },
      { "type": "text", "x": 98, "y": 76, "w": 146, "text": "{pc}", "font": { "family": "ui", "size": 10, "weight": 600 } }
    ] } ] },
    "riepilogo": { "variants": [
      { "when": "none", "elements": [ { "type": "text", "x": 0, "y": 50, "w": 250, "text": "PC spento", "font": { "family": "ui", "size": 20, "weight": 700 }, "align": "center" } ] },
      { "when": "always", "ref": "urgent", "elements": [
        { "type": "text", "x": 0, "y": 40, "w": 250, "text": "{sessioni} sessioni · {sessione.nome}", "font": { "family": "ui", "size": 20, "weight": 700 }, "align": "center" },
        { "type": "stateIcon", "x": 120, "y": 80, "scale": 3 }
      ] }
    ] }
  }
}
```

`protocol/themes/fixtures/lista.theme.json`:

```json
{
  "format": "calamaio-theme", "version": 1, "id": "lista", "name": "Righe a pagine", "screen": "250x122",
  "screens": {
    "home": { "variants": [ { "when": "always", "ref": "urgent", "elements": [
      { "type": "text", "x": 4, "y": 2, "w": 242, "text": "{sessione.nome} · {sessione.stato}", "font": { "family": "ui", "size": 12, "weight": 700 } },
      { "type": "list", "x": 0, "y": 18, "w": 250, "h": 94, "rowH": 20, "gap": 2, "pageSec": 3, "row": [
        { "type": "stateIcon", "x": 4, "y": 6 },
        { "type": "text", "x": 16, "y": 4, "w": 120, "text": "{riga.nome}", "font": { "family": "ui", "size": 12, "weight": 600 } },
        { "type": "text", "x": 140, "y": 5, "w": 106, "text": "{riga.da}", "font": { "family": "ui", "size": 10, "weight": 600 }, "align": "right" },
        { "type": "rect", "x": 0, "y": 0, "w": 250, "h": 20, "fill": true, "border": 0, "visibleIf": [ { "state": "permission" } ] },
        { "type": "text", "x": 16, "y": 4, "w": 120, "text": "{riga.nome|maiuscolo}", "font": { "family": "ui", "size": 12, "weight": 700 }, "invert": true, "visibleIf": [ { "state": "permission" } ] },
        { "type": "line", "x": 0, "y": 21, "w": 250, "h": 1, "style": "dotted" }
      ] },
      { "type": "clock", "x": 4, "y": 114, "w": 60, "font": { "family": "pix", "size": 8, "weight": 400 } }
    ] } ] },
    "detail": { "variants": [ { "when": "many", "ref": "urgent", "elements": [
      { "type": "mascot", "x": 4, "y": 4, "scale": 2, "ref": "urgent" },
      { "type": "text", "x": 64, "y": 4, "w": 182, "text": "{sessione.nome}", "font": { "family": "ui", "size": 16, "weight": 700 } }
    ] } ] }
  }
}
```

- [ ] **Step 3: Scrivere i test che falliscono**

`service/internal/theme/parse_test.go`:

```go
package theme

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const fixtures = "../../../protocol/themes/fixtures"

func read(t *testing.T, name string) []byte {
	t.Helper()
	b, err := os.ReadFile(filepath.Join(fixtures, name))
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestParseFixtures(t *testing.T) {
	for _, n := range []string{"prova.theme.json", "elementi.theme.json", "lista.theme.json"} {
		th, ps, _ := Parse(read(t, n))
		if len(ps) > 0 {
			t.Fatalf("%s: %v", n, ps)
		}
		if th.ID+".theme.json" != n {
			t.Fatalf("%s: id %q", n, th.ID)
		}
	}
	th, _, _ := Parse(read(t, "elementi.theme.json"))
	if got := len(th.Screens); got != 10 {
		t.Fatalf("elementi: %d schermate", got)
	}
	// il gruppo si scioglie già qui: 2 figli visibili, quello nascosto sparisce, x/y sommati, condizione ereditata
	pet := th.Screens["pet"].Variants[0].Elements
	var r *Element
	for _, e := range pet {
		if e.Type == "rect" {
			r = e
		}
	}
	if r == nil || r.X != 100 || r.Y != 60 || len(r.Conds) != 1 || r.Conds[0].Op != CondState {
		t.Fatalf("gruppo non sciolto: %+v", r)
	}
}

// Ogni caso: modifica del tema «prova» (json già valido) → frammento atteso nell'errore.
func TestParseRejects(t *testing.T) {
	base := string(read(t, "prova.theme.json"))
	cases := []struct{ from, to, want string }{
		{`"version": 1`, `"version": 2`, "versione 2 non supportata"},
		{`"id": "prova"`, `"id": "Prova"`, "id: kebab-case"},
		{`"id": "prova"`, `"id": "calamaio"`, "tema di serie"},
		{`"name": "Mascotte grande"`, `"name": "Un nome davvero troppo lungo, sì"`, "1–24 byte"},
		{`"screen": "250x122"`, `"screen": "400x300"`, "solo 250x122"},
		{`"sleep": {`, `"perm": {`, "schermata bloccata"},
		{`"sleep": {`, `"agenda": {`, "non ancora personalizzabile"},
		{`"scale": 4 }`, `"scale": 5 }`, "scale: da 1 a 4"},
		{`"size": 14, "weight": 700`, `"size": 17, "weight": 700`, "carattere che la scheda non ha"},
		{`{sessione.nome|maiuscolo}`, `{sessione.nomi}`, "valore sconosciuto {sessione.nomi}"},
		{`{sessione.nome|maiuscolo}`, `{sessione.nome|grassetto}`, "filtro sconosciuto"},
		{`{sessione.nome|maiuscolo}`, `{riga.nome}`, "riga.* solo dentro un riquadro lista"},
		{`{sessione.nome|maiuscolo}`, `{sessione.nome`, "graffa non chiusa"},
		{`"x": 83, "y": 6`, `"x": 600, "y": 6`, "x: fuori da ±512"},
		{`"type": "battery", "x": 230, "y": 110 }`, `"type": "battery", "x": 230, "y": 110, "colore": 1 }`, "campo sconosciuto «colore»"},
		{`"type": "line", "x": 0, "y": 33`, `"type": "list", "rowH": 5, "row": [], "x": 0, "y": 33`, "dentro un altro"},
		{`"when": "none"`, `"when": "spesso"`, "when: always, none, one o many"},
		{`{ "charging": false }`, `{ "charging": false, "pc": true }`, "una sola chiave"},
		{`{ "blink": "even" }`, `{ "blink": "pari" }`, "blink: even o odd"},
		{`"screens": {`, `"menu": {}, "screens": {`, "fase 4"},
	}
	for _, c := range cases {
		if !strings.Contains(base, c.from) {
			t.Fatalf("caso %q: testo di partenza assente", c.want)
		}
		_, ps, _ := Parse([]byte(strings.Replace(base, c.from, c.to, 1)))
		found := false
		for _, p := range ps {
			if strings.Contains(p.String(), c.want) {
				found = true
			}
		}
		if !found {
			t.Errorf("atteso %q, avuto %v", c.want, ps)
		}
	}
	if _, ps, _ := Parse([]byte(`{`)); len(ps) == 0 || !strings.Contains(ps[0].Msg, "JSON non valido") {
		t.Fatalf("JSON rotto: %v", ps)
	}
}

func TestParseLimits(t *testing.T) {
	// 65 elementi in una variante
	var b strings.Builder
	b.WriteString(`{"format":"calamaio-theme","version":1,"id":"tanti","name":"Tanti","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[`)
	for i := 0; i < 65; i++ {
		if i > 0 {
			b.WriteString(",")
		}
		b.WriteString(`{"type":"rect","x":0,"y":0,"w":1,"h":1}`)
	}
	b.WriteString(`]}]}}}`)
	_, ps, _ := Parse([]byte(b.String()))
	if len(ps) == 0 || !strings.Contains(ps[0].String(), "al massimo 64 elementi") {
		t.Fatalf("limite elementi: %v", ps)
	}
}

func TestParseWarnsBigFontChars(t *testing.T) {
	_, ps, warns := Parse(read(t, "elementi.theme.json"))
	if len(ps) > 0 {
		t.Fatal(ps)
	}
	if len(warns) == 0 || !strings.Contains(strings.Join(warns, "\n"), "Chakra Petch 40") {
		t.Fatalf("avviso dei caratteri grandi mancante: %v", warns)
	}
}

func TestTemplate(t *testing.T) {
	segs, err := ParseTemplate("A {{x}} {sessione.attività|maiuscolo}!", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(segs) != 3 || segs[0].Lit != "A {x} " || !segs[1].IsVal || segs[1].Val != 0x02 || segs[1].Filter != 1 || segs[2].Lit != "!" {
		t.Fatalf("%+v", segs)
	}
	if _, err := ParseTemplate(strings.Repeat("x", 256), false); err == nil {
		t.Fatal("testo fisso oltre 255 byte accettato")
	}
}
```

Aggiungere in fondo allo stesso file la prova dello schema (lo schema deve accettare i tre temi di prova):

```go
func TestSchemaAcceptsFixtures(t *testing.T) {
	f, err := os.Open("../../../protocol/schema/theme.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	doc, err := jsonschema.UnmarshalJSON(f)
	if err != nil {
		t.Fatal(err)
	}
	c := jsonschema.NewCompiler()
	c.DefaultDraft(jsonschema.Draft2020)
	if err := c.AddResource("theme.schema.json", doc); err != nil {
		t.Fatal(err)
	}
	sch, err := c.Compile("theme.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	for _, n := range []string{"prova.theme.json", "elementi.theme.json", "lista.theme.json"} {
		v, _ := jsonschema.UnmarshalJSON(strings.NewReader(string(read(t, n))))
		if err := sch.Validate(v); err != nil {
			t.Fatalf("%s: %v", n, err)
		}
	}
	bad, _ := jsonschema.UnmarshalJSON(strings.NewReader(`{"format":"calamaio-theme","version":1,"id":"x","name":"x","screen":"250x122","screens":{"perm":{"variants":[]}}}`))
	if sch.Validate(bad) == nil {
		t.Fatal("lo schema accetta la schermata Permessi")
	}
}
```

(import in più: `"github.com/santhosh-tekuri/jsonschema/v6"`.)

`service/internal/theme/fonts_test.go`:

```go
package theme

import (
	"os"
	"regexp"
	"strconv"
	"testing"
)

// La tabella dei caratteri del servizio è quella del firmware (f_registry.h, generato da tools/fonts/gen.py).
func TestFontsMatchFirmware(t *testing.T) {
	b, err := os.ReadFile("../../../firmware/src/gfx/fonts/f_registry.h")
	if err != nil {
		t.Fatal(err)
	}
	re := regexp.MustCompile(`\{(\d+), (\d+), (true|false), (?:true|false), \d+,`)
	got := map[FontSpec]bool{}
	for _, m := range re.FindAllStringSubmatch(string(b), -1) {
		size, _ := strconv.Atoi(m[1])
		w, _ := strconv.Atoi(m[2])
		fam := "ui"
		if m[3] == "true" {
			fam = "pix"
		}
		got[FontSpec{fam, size, w}] = true
	}
	if len(got) != len(Fonts) {
		t.Fatalf("firmware %d caratteri, servizio %d", len(got), len(Fonts))
	}
	for _, f := range Fonts {
		if !got[f] || !FontOK(f.Family, f.Size, f.Weight) {
			t.Fatalf("%+v manca nel firmware", f)
		}
	}
	if FontOK("ui", 17, 700) || FontOK("pix", 8, 600) {
		t.Fatal("combinazione inesistente accettata")
	}
}
```

- [ ] **Step 4: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/theme/ -v`
Expected: FAIL (package senza sorgenti: `undefined: Parse`).

- [ ] **Step 5: Scrivere `theme.go`, `fonts.go`, `values.go`, `template.go`**

`service/internal/theme/theme.go`:

```go
// Package theme: temi delle schermate della scheda (docs/TEMI.md). Lettura e controllo del JSON (parse.go),
// compilazione nel binario CTHM v1 (compile.go). Le regole sono le stesse dell'interprete del firmware.
package theme

import "fmt"

const (
	Format      = "calamaio-theme"
	Version     = 1
	ScreenS     = "250x122"
	ReservedID  = "calamaio"
	MaxBlock    = 4096
	MaxFile     = 65536
	HeaderLen   = 72
	MaxVariants = 8
	MaxElements = 64
	MaxConds    = 8
	MaxSegs     = 16
	MaxRow      = 32
	MaxLiteral  = 255
	CoordMax    = 512
)

// Schermate personalizzabili: codice nel binario = indice + 1 (docs/TEMI.md §1.1).
var ScreenKeys = []string{"home", "detail", "sleep", "offline", "riepilogo", "oggi", "batteria", "stato", "limiti", "focus", "ambiente", "pet"}

var lockedScreens = map[string]bool{"perm": true, "pair": true, "confirm": true, "boot": true, "ota": true, "battlow": true,
	"calib": true, "gbload": true, "gbplay": true}
var laterScreens = map[string]bool{"list": true, "wifiScan": true, "qr": true, "connecting": true, "storico": true, "guest": true,
	"servizi": true, "agenda": true, "game": true}

func ScreenCode(key string) (byte, bool) {
	for i, k := range ScreenKeys {
		if k == key {
			return byte(i + 1), true
		}
	}
	return 0, false
}

type Problem struct {
	Path string `json:"path"`
	Msg  string `json:"msg"`
}

func (p Problem) String() string {
	if p.Path == "" {
		return p.Msg
	}
	return p.Path + ": " + p.Msg
}

type problems []Problem

func (ps *problems) add(path, f string, a ...any) { *ps = append(*ps, Problem{path, fmt.Sprintf(f, a...)}) }

type Theme struct {
	Format  string
	Version int
	ID      string
	Name    string
	Screen  string
	Screens map[string]*Layout
}

type Layout struct{ Variants []*Variant }

type Variant struct {
	When     string
	Ref      string
	Elements []*Element // già sciolti i gruppi, senza i nascosti
}

type Font struct {
	Family string `json:"family"`
	Size   int    `json:"size"`
	Weight int    `json:"weight"`
}

// Codici delle condizioni nel binario (docs/TEMI.md §5.4).
const (
	CondPerm byte = 1 + iota
	CondBattBelow
	CondCharging
	CondNight
	CondPC
	CondWorkday
	CondGame
	CondSessionsMin
	CondSessionsMax
	CondState
	CondBlink
)

type Cond struct{ Op, Arg byte }

// Element: un elemento già controllato e con i predefiniti applicati (W e H calcolati per i tipi a misura fissa).
type Element struct {
	Type       string
	X, Y, W, H int
	Invert     bool
	Conds      []Cond
	Scale      int
	Pose       string
	Ref        string // "" = quello della variante (in una riga: row)
	Accessory  bool
	Animated   bool
	Segs       []Seg // text, clock (già abbassato), qr
	Font       Font
	Align      string
	Lines      int
	LineHeight int
	Upper      bool
	Percent    bool
	Source     string
	Style      string
	Sym        string
	Dir        string
	Border     int
	Fill       bool
	Radius     int
	RowH       int
	Gap        int
	PageSec    int
	PageDots   bool
	Row        []*Element
}
```

`service/internal/theme/fonts.go`:

```go
package theme

type FontSpec struct {
	Family string
	Size   int
	Weight int
}

// Le 22 combinazioni di firmware/src/gfx/fonts/f_registry.h (TestFontsMatchFirmware).
var Fonts = []FontSpec{
	{"ui", 10, 500}, {"ui", 10, 600}, {"ui", 10, 700}, {"ui", 11, 500}, {"ui", 11, 600}, {"ui", 12, 500}, {"ui", 12, 600},
	{"ui", 12, 700}, {"ui", 13, 600}, {"ui", 13, 700}, {"ui", 14, 700}, {"ui", 15, 700}, {"ui", 16, 700}, {"ui", 18, 700},
	{"ui", 20, 700}, {"ui", 22, 700}, {"ui", 26, 700}, {"ui", 40, 700}, {"ui", 44, 700}, {"ui", 46, 700},
	{"pix", 8, 400}, {"pix", 8, 700},
}

func FontOK(family string, size, weight int) bool {
	for _, f := range Fonts {
		if f.Family == family && f.Size == size && f.Weight == weight {
			return true
		}
	}
	return false
}

// I caratteri Chakra Petch da 26 px in su hanno solo questi glifi (tools/fonts/gen.py, BIG_CHARS): gli altri escono «?».
const bigChars = " 0123456789:.,%+-/?…abcdefghijklmnopqrstuvwxyz"

func bigFontMissing(f Font, lit string) []rune {
	if f.Family != "ui" || f.Size < 26 {
		return nil
	}
	var out []rune
	for _, r := range lit {
		if !containsRune(bigChars, r) {
			out = append(out, r)
		}
	}
	return out
}

func containsRune(s string, r rune) bool {
	for _, c := range s {
		if c == r {
			return true
		}
	}
	return false
}
```

`service/internal/theme/values.go`:

```go
package theme

// Valori nei testi (docs/TEMI.md §3). ID: sessione 0x00–0x09, riga 0x40–0x49, generali 0x80–0x95.
// 0x81 (ora con i secondi) lo produce solo l'orologio `hms`: non ha un nome.
type ValueSpec struct {
	Name    string
	ID      uint16
	Row     bool
	Example string
}

const ValClockSec uint16 = 0x81

var sessionNames = []struct{ name, example string }{
	{"nome", "api-server"}, {"stato", "Sta lavorando"}, {"attività", "Bash: npm run test"}, {"riassunto", "Guida aggiornata"},
	{"meta", "3/7 task  ·  12 min"}, {"contesto", "42%"}, {"task", "3/7"}, {"da", "12 min"}, {"sub", "2"}, {"test", "✓"},
}

var generalNames = []struct {
	name    string
	id      uint16
	example string
}{
	{"ora", 0x80, "10:30"}, {"data", 0x82, "domenica 11 ottobre"}, {"giorno", 0x83, "domenica"}, {"sessioni", 0x84, "3"},
	{"inattesa", 0x85, "1"}, {"batteria", 0x86, "78%"}, {"autonomia", 0x87, "Autonomia: circa 3 giorni con il tuo uso"},
	{"wifi", 0x88, "CasaWiFi"}, {"pc", 0x89, "calamaio.local"}, {"giornata.mancano", 0x8A, "6 h 25"}, {"giornata.fine", 0x8B, "18:00"},
	{"oggi.task", 0x8C, "4"}, {"oggi.righe", 0x8D, "+120 −30"}, {"oggi.costo", 0x8E, "1,20 $"}, {"limiti.5h", 0x8F, "37%"},
	{"limiti.7g", 0x90, "12%"}, {"mascotte.livello", 0x91, "3"}, {"mascotte.xp", 0x92, "66/417 XP"}, {"mascotte.energia", 0x93, "80%"},
	{"co2", 0x94, "620 ppm"}, {"versione", 0x95, "0.13.0-ed"},
}

var Values = func() []ValueSpec {
	var v []ValueSpec
	for i, s := range sessionNames {
		v = append(v, ValueSpec{"sessione." + s.name, uint16(i), false, s.example})
		v = append(v, ValueSpec{"riga." + s.name, uint16(0x40 + i), true, s.example})
	}
	for _, g := range generalNames {
		v = append(v, ValueSpec{g.name, g.id, false, g.example})
	}
	return v
}()

func ValueID(name string) (uint16, bool) {
	for _, v := range Values {
		if v.Name == name {
			return v.ID, true
		}
	}
	return 0, false
}
```

`service/internal/theme/template.go`:

```go
package theme

import (
	"fmt"
	"strings"
)

// Seg: un pezzo di modello. Filter: 0 nessuno, 1 maiuscolo, 2 minuscolo.
type Seg struct {
	Lit    string
	Val    uint16
	IsVal  bool
	Filter byte
}

// ParseTemplate: «testo {valore|filtro} testo»; {{ e }} = graffe. inRow: dentro un riquadro lista (riga.* ammessi).
func ParseTemplate(s string, inRow bool) ([]Seg, error) {
	var out []Seg
	var lit strings.Builder
	flush := func() error {
		if lit.Len() == 0 {
			return nil
		}
		if lit.Len() > MaxLiteral {
			return fmt.Errorf("testo fisso di %d byte: al massimo %d", lit.Len(), MaxLiteral)
		}
		out = append(out, Seg{Lit: lit.String()})
		lit.Reset()
		return nil
	}
	for i := 0; i < len(s); i++ {
		c := s[i]
		switch {
		case c == '{' && i+1 < len(s) && s[i+1] == '{':
			lit.WriteByte('{')
			i++
		case c == '}' && i+1 < len(s) && s[i+1] == '}':
			lit.WriteByte('}')
			i++
		case c == '{':
			j := strings.IndexByte(s[i:], '}')
			if j < 0 {
				return nil, fmt.Errorf("graffa non chiusa in %q", s)
			}
			body := s[i+1 : i+j]
			name, filter, _ := strings.Cut(body, "|")
			id, ok := ValueID(name)
			if !ok {
				return nil, fmt.Errorf("valore sconosciuto {%s}", name)
			}
			if id >= 0x40 && id < 0x80 && !inRow {
				return nil, fmt.Errorf("{%s}: riga.* solo dentro un riquadro lista", name)
			}
			var f byte
			switch filter {
			case "":
			case "maiuscolo":
				f = 1
			case "minuscolo":
				f = 2
			default:
				return nil, fmt.Errorf("filtro sconosciuto «%s» (maiuscolo o minuscolo)", filter)
			}
			if err := flush(); err != nil {
				return nil, err
			}
			out = append(out, Seg{Val: id, IsVal: true, Filter: f})
			i += j
		default:
			lit.WriteByte(c)
		}
	}
	if err := flush(); err != nil {
		return nil, err
	}
	if len(out) > MaxSegs {
		return nil, fmt.Errorf("%d pezzi nel testo: al massimo %d", len(out), MaxSegs)
	}
	return out, nil
}
```

- [ ] **Step 6: Scrivere `parse.go`**

```go
package theme

import (
	"encoding/json"
	"fmt"
	"regexp"
	"sort"
	"unicode/utf8"
)

var idRe = regexp.MustCompile(`^[a-z0-9]+(-[a-z0-9]+)*$`)

// Campi ammessi per tipo (oltre ai comuni).
var commonKeys = []string{"type", "id", "name", "x", "y", "w", "h", "visibleIf", "invert", "hidden", "locked"}
var typeKeys = map[string][]string{
	"mascot":    {"scale", "pose", "ref", "accessory", "animated"},
	"text":      {"text", "font", "align", "lines", "lineHeight", "upper"},
	"clock":     {"format", "font", "align"},
	"battery":   {"scale", "percent"},
	"wifi":      {"scale"},
	"stateIcon": {"scale", "ref"},
	"bar":       {"source", "style"},
	"qr":        {"text"},
	"sym":       {"sym", "scale"},
	"line":      {"dir", "style"},
	"rect":      {"border", "fill", "radius"},
	"circle":    {"border", "fill"},
	"list":      {"rowH", "gap", "pageSec", "pageDots", "row"},
	"group":     {"children"},
}
var groupKeys = []string{"type", "id", "name", "x", "y", "visibleIf", "hidden", "locked", "children"}

var poses = []string{"working", "permission", "input", "done", "error", "idle", "compact", "tired", "stuffy"}
var states = []string{"working", "permission", "input", "error", "compact", "done", "idle"}
var syms = []string{"plug", "bell", "moon", "lock", "bolt", "dot", "glass", "gauge"}
var sources = []string{"context", "tasks", "workday", "battery", "limit5h", "limit7d"}

func index(list []string, s string) int {
	for i, v := range list {
		if v == s {
			return i
		}
	}
	return -1
}

// rawElem: un elemento come arriva, con i campi facoltativi a puntatore per i predefiniti.
type rawElem struct {
	Type       string            `json:"type"`
	ID         string            `json:"id"`
	Name       string            `json:"name"`
	X          int               `json:"x"`
	Y          int               `json:"y"`
	W          *int              `json:"w"`
	H          *int              `json:"h"`
	VisibleIf  []json.RawMessage `json:"visibleIf"`
	Invert     bool              `json:"invert"`
	Hidden     bool              `json:"hidden"`
	Locked     bool              `json:"locked"`
	Scale      *int              `json:"scale"`
	Pose       string            `json:"pose"`
	Ref        string            `json:"ref"`
	Accessory  *bool             `json:"accessory"`
	Animated   *bool             `json:"animated"`
	Text       *string           `json:"text"`
	Font       *Font             `json:"font"`
	Align      string            `json:"align"`
	Lines      *int              `json:"lines"`
	LineHeight *int              `json:"lineHeight"`
	Upper      bool              `json:"upper"`
	Format     string            `json:"format"`
	Percent    bool              `json:"percent"`
	Source     string            `json:"source"`
	Style      string            `json:"style"`
	Sym        string            `json:"sym"`
	Dir        string            `json:"dir"`
	Border     *int              `json:"border"`
	Fill       bool              `json:"fill"`
	Radius     int               `json:"radius"`
	RowH       int               `json:"rowH"`
	Gap        int               `json:"gap"`
	PageSec    *int              `json:"pageSec"`
	PageDots   *bool             `json:"pageDots"`
	Row        []json.RawMessage `json:"row"`
	Children   []json.RawMessage `json:"children"`
}

type parser struct {
	ps    problems
	warns []string
}

// Parse controlla un tema. Errori vuoti = valido; gli avvisi non impediscono la compilazione.
func Parse(data []byte) (*Theme, []Problem, []string) {
	p := &parser{}
	var top map[string]json.RawMessage
	if err := json.Unmarshal(data, &top); err != nil {
		p.ps.add("", "JSON non valido: %v", err)
		return nil, p.ps, nil
	}
	p.keys(top, "", []string{"format", "version", "id", "name", "screen", "screens", "menu"})
	t := &Theme{Screens: map[string]*Layout{}}
	p.field(top, "format", &t.Format)
	p.field(top, "version", &t.Version)
	p.field(top, "id", &t.ID)
	p.field(top, "name", &t.Name)
	p.field(top, "screen", &t.Screen)
	if t.Format != Format {
		p.ps.add("format", "deve essere %q", Format)
	}
	if t.Version != Version {
		p.ps.add("version", "versione %d non supportata (questa è la %d)", t.Version, Version)
	}
	switch {
	case t.ID == ReservedID:
		p.ps.add("id", "«calamaio» è il tema di serie: scegli un altro id")
	case len(t.ID) < 1 || len(t.ID) > 24 || !idRe.MatchString(t.ID):
		p.ps.add("id", "id: kebab-case (a-z, 0-9, trattini), da 1 a 24 caratteri")
	}
	if !textOK(t.Name, 24) {
		p.ps.add("name", "nome: da 1–24 byte, senza a capo né caratteri di controllo")
	}
	if t.Screen != ScreenS {
		p.ps.add("screen", "schermo %q: per ora solo 250x122", t.Screen)
	}
	if _, ok := top["menu"]; ok {
		p.ps.add("menu", "la sezione menu arriva con la fase 4: per ora toglila")
	}
	var screens map[string]json.RawMessage
	p.field(top, "screens", &screens)
	keys := make([]string, 0, len(screens))
	for k := range screens {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		path := "screens." + k
		switch {
		case lockedScreens[k]:
			p.ps.add(path, "schermata bloccata: regole di sicurezza (Permessi, abbinamento e sistema restano di serie)")
			continue
		case laterScreens[k]:
			p.ps.add(path, "schermata non ancora personalizzabile")
			continue
		}
		if _, ok := ScreenCode(k); !ok {
			p.ps.add(path, "schermata sconosciuta")
			continue
		}
		if l := p.layout(screens[k], path); l != nil {
			t.Screens[k] = l
		}
	}
	if len(screens) == 0 {
		p.ps.add("screens", "nessuna schermata")
	}
	return t, p.ps, p.warns
}

func textOK(s string, max int) bool {
	if len(s) < 1 || len(s) > max || !utf8.ValidString(s) {
		return false
	}
	for _, r := range s {
		if r < 0x20 || r == 0x7f {
			return false
		}
	}
	return true
}

func (p *parser) keys(m map[string]json.RawMessage, path string, allowed []string) {
	ks := make([]string, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	for _, k := range ks {
		if index(allowed, k) < 0 {
			p.ps.add(path, "campo sconosciuto «%s»", k)
		}
	}
}

func (p *parser) field(m map[string]json.RawMessage, k string, dst any) {
	raw, ok := m[k]
	if !ok {
		return
	}
	if err := json.Unmarshal(raw, dst); err != nil {
		p.ps.add(k, "valore del tipo sbagliato")
	}
}

func (p *parser) layout(raw json.RawMessage, path string) *Layout {
	var l struct {
		Variants []json.RawMessage `json:"variants"`
	}
	var m map[string]json.RawMessage
	if json.Unmarshal(raw, &m) != nil || json.Unmarshal(raw, &l) != nil {
		p.ps.add(path, "serve un oggetto {variants: [...]}")
		return nil
	}
	p.keys(m, path, []string{"variants"})
	if len(l.Variants) < 1 || len(l.Variants) > MaxVariants {
		p.ps.add(path+".variants", "da 1 a %d varianti", MaxVariants)
		return nil
	}
	out := &Layout{}
	for i, vr := range l.Variants {
		vp := fmt.Sprintf("%s.variants[%d]", path, i)
		var vm map[string]json.RawMessage
		var v struct {
			When     string            `json:"when"`
			Ref      string            `json:"ref"`
			Elements []json.RawMessage `json:"elements"`
		}
		if json.Unmarshal(vr, &vm) != nil || json.Unmarshal(vr, &v) != nil {
			p.ps.add(vp, "variante non valida")
			continue
		}
		p.keys(vm, vp, []string{"when", "ref", "elements"})
		if index([]string{"always", "none", "one", "many"}, v.When) < 0 {
			p.ps.add(vp, "when: always, none, one o many")
		}
		if v.Ref == "" {
			v.Ref = "selected"
		}
		if v.Ref != "selected" && v.Ref != "urgent" {
			p.ps.add(vp, "ref: selected o urgent")
		}
		var els []*Element
		for j, er := range v.Elements {
			els = append(els, p.element(er, fmt.Sprintf("%s.elements[%d]", vp, j), false, 0, 0, 0, nil)...)
		}
		n := len(els)
		for _, e := range els {
			n += len(e.Row)
		}
		if n > MaxElements {
			p.ps.add(vp, "%d elementi: al massimo %d elementi per variante (righe comprese)", n, MaxElements)
		}
		out.Variants = append(out.Variants, &Variant{When: v.When, Ref: v.Ref, Elements: els})
	}
	return out
}

// element controlla un elemento e lo restituisce pronto (un gruppo diventa i suoi figli).
// dx, dy: posizione del gruppo che lo contiene; inherited: condizioni del gruppo.
func (p *parser) element(raw json.RawMessage, path string, inRow bool, depth, dx, dy int, inherited []Cond) []*Element {
	var m map[string]json.RawMessage
	var r rawElem
	if json.Unmarshal(raw, &m) != nil {
		p.ps.add(path, "serve un oggetto")
		return nil
	}
	if err := json.Unmarshal(raw, &r); err != nil {
		p.ps.add(path, "valore del tipo sbagliato (%v)", err)
		return nil
	}
	extra, ok := typeKeys[r.Type]
	if !ok {
		p.ps.add(path, "tipo sconosciuto «%s»", r.Type)
		return nil
	}
	if r.Type == "group" {
		p.keys(m, path, groupKeys)
	} else {
		p.keys(m, path, append(append([]string{}, commonKeys...), extra...))
	}
	if r.Hidden {
		return nil
	}
	if len(r.Name) > 32 || len(r.ID) > 32 {
		p.ps.add(path, "id e name: al massimo 32 byte")
	}
	conds := append(append([]Cond{}, inherited...), p.conds(r.VisibleIf, path)...)
	if len(conds) > MaxConds {
		p.ps.add(path, "%d condizioni: al massimo %d (con quelle dei gruppi)", len(conds), MaxConds)
	}
	x, y := r.X+dx, r.Y+dy
	if x < -CoordMax || x > CoordMax {
		p.ps.add(path, "x: fuori da ±512")
	}
	if y < -CoordMax || y > CoordMax {
		p.ps.add(path, "y: fuori da ±512")
	}
	if r.Type == "group" {
		if depth >= 4 {
			p.ps.add(path, "gruppi annidati: al massimo 4 livelli")
			return nil
		}
		if len(r.Children) == 0 {
			p.ps.add(path, "gruppo vuoto")
		}
		var out []*Element
		for i, c := range r.Children {
			out = append(out, p.element(c, fmt.Sprintf("%s.children[%d]", path, i), inRow, depth+1, x, y, conds)...)
		}
		return out
	}
	e := &Element{Type: r.Type, X: x, Y: y, Invert: r.Invert, Conds: conds, Ref: r.Ref}
	need := func(v *int, name string) int {
		if v == nil {
			p.ps.add(path, "%s obbligatoria", name)
			return 0
		}
		if *v < 0 || *v > CoordMax {
			p.ps.add(path, "%s: da 0 a 512", name)
		}
		return *v
	}
	rng := func(v *int, def, lo, hi int, name string) int {
		if v == nil {
			return def
		}
		if *v < lo || *v > hi {
			p.ps.add(path, "%s: da %d a %d", name, lo, hi)
		}
		return *v
	}
	ref := func(allowNobody bool) {
		switch e.Ref {
		case "", "selected", "urgent":
		case "nobody":
			if !allowNobody {
				p.ps.add(path, "ref: selected, urgent o row")
			}
		case "row":
			if !inRow {
				p.ps.add(path, "ref row solo dentro un riquadro lista")
			}
		default:
			p.ps.add(path, "ref: selected, urgent, row o nobody")
		}
	}
	font := func() {
		if r.Font == nil || !FontOK(r.Font.Family, r.Font.Size, r.Font.Weight) {
			p.ps.add(path, "font: carattere che la scheda non ha (vedi docs/TEMI.md §2)")
			return
		}
		e.Font = *r.Font
	}
	align := func() {
		e.Align = r.Align
		if e.Align == "" {
			e.Align = "left"
		}
		if index([]string{"left", "center", "right"}, e.Align) < 0 {
			p.ps.add(path, "align: left, center o right")
		}
	}
	tmpl := func() {
		if r.Text == nil || *r.Text == "" {
			p.ps.add(path, "text obbligatorio")
			return
		}
		segs, err := ParseTemplate(*r.Text, inRow)
		if err != nil {
			p.ps.add(path, "%v", err)
			return
		}
		e.Segs = segs
	}
	switch r.Type {
	case "mascot":
		e.Scale = rng(r.Scale, 1, 1, 4, "scale")
		e.W, e.H = 28*e.Scale, 25*e.Scale
		e.Pose = r.Pose
		if e.Pose == "" {
			e.Pose = "auto"
		}
		if e.Pose != "auto" && index(poses, e.Pose) < 0 {
			p.ps.add(path, "pose sconosciuta «%s»", e.Pose)
		}
		ref(true)
		e.Accessory = r.Accessory == nil || *r.Accessory
		e.Animated = r.Animated == nil || *r.Animated
	case "text", "clock":
		e.W = need(r.W, "w")
		font()
		align()
		e.Lines = 1
		if r.Type == "text" {
			tmpl()
			e.Lines = rng(r.Lines, 1, 1, 8, "lines")
			e.Upper = r.Upper
		} else {
			switch r.Format {
			case "", "hm":
				e.Segs = []Seg{{Val: 0x80, IsVal: true}}
			case "hms":
				e.Segs = []Seg{{Val: ValClockSec, IsVal: true}}
			default:
				p.ps.add(path, "format: hm o hms")
			}
		}
		e.LineHeight = rng(r.LineHeight, e.Font.Size+2, 1, 64, "lineHeight")
		e.H = e.Lines * e.LineHeight
		for _, s := range e.Segs {
			if miss := bigFontMissing(e.Font, s.Lit); len(miss) > 0 {
				p.warns = append(p.warns, fmt.Sprintf("%s: Chakra Petch %d non ha %q: si vedrà «?» (solo cifre, «:.,%%+-/?…» e minuscole)", path, e.Font.Size, string(miss)))
			}
		}
	case "battery":
		e.Scale = rng(r.Scale, 1, 1, 2, "scale")
		e.W, e.H, e.Percent = 16*e.Scale, 8*e.Scale, r.Percent
	case "wifi":
		e.Scale = rng(r.Scale, 1, 1, 2, "scale")
		e.W, e.H = 11*e.Scale, 9*e.Scale
	case "stateIcon":
		e.Scale = rng(r.Scale, 1, 1, 3, "scale")
		e.W, e.H = 7*e.Scale, 7*e.Scale
		ref(true)
	case "sym":
		e.Scale = rng(r.Scale, 1, 1, 3, "scale")
		e.W, e.H, e.Sym = 7*e.Scale, 7*e.Scale, r.Sym
		if index(syms, r.Sym) < 0 {
			p.ps.add(path, "sym: plug, bell, moon, lock, bolt, dot, glass o gauge")
		}
	case "bar":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.Source, e.Style = r.Source, r.Style
		if index(sources, r.Source) < 0 {
			p.ps.add(path, "source: context, tasks, workday, battery, limit5h o limit7d")
		}
		if e.Style == "" {
			e.Style = "big"
		}
		if e.Style != "big" && e.Style != "bar" {
			p.ps.add(path, "style: big o bar")
		}
	case "qr":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		tmpl()
	case "line":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.Dir, e.Style = r.Dir, r.Style
		if e.Dir == "" {
			e.Dir = "h"
		}
		if e.Style == "" {
			e.Style = "solid"
		}
		if (e.Dir != "h" && e.Dir != "v") || (e.Style != "solid" && e.Style != "dotted") {
			p.ps.add(path, "dir: h o v; style: solid o dotted")
		}
	case "rect", "circle":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.Border = rng(r.Border, 1, 0, 8, "border")
		e.Fill = r.Fill
		if r.Type == "rect" {
			e.Radius = r.Radius
			if e.Radius < 0 || e.Radius > 8 {
				p.ps.add(path, "radius: da 0 a 8")
			}
		}
		if e.Border == 0 && !e.Fill {
			p.warns = append(p.warns, path+": né bordo né riempimento, non si vede")
		}
	case "list":
		if inRow {
			p.ps.add(path, "un riquadro lista non può stare dentro un altro")
			return nil
		}
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.RowH, e.Gap = r.RowH, r.Gap
		if e.RowH < 1 || e.RowH > 122 {
			p.ps.add(path, "rowH: da 1 a 122")
		}
		if e.Gap < 0 || e.Gap > 60 {
			p.ps.add(path, "gap: da 0 a 60")
		}
		e.PageSec = rng(r.PageSec, 5, 1, 60, "pageSec")
		e.PageDots = r.PageDots == nil || *r.PageDots
		for i, c := range r.Row {
			e.Row = append(e.Row, p.element(c, fmt.Sprintf("%s.row[%d]", path, i), true, depth, 0, 0, nil)...)
		}
		if len(e.Row) < 1 || len(e.Row) > MaxRow {
			p.ps.add(path, "row: da 1 a %d elementi", MaxRow)
		}
	}
	return []*Element{e}
}

func (p *parser) conds(raws []json.RawMessage, path string) []Cond {
	var out []Cond
	boolOps := map[string]byte{"perm": CondPerm, "charging": CondCharging, "night": CondNight, "pc": CondPC, "workday": CondWorkday, "game": CondGame}
	for i, raw := range raws {
		cp := fmt.Sprintf("%s.visibleIf[%d]", path, i)
		var m map[string]json.RawMessage
		if json.Unmarshal(raw, &m) != nil || len(m) != 1 {
			p.ps.add(cp, "ogni condizione è un oggetto con una sola chiave")
			continue
		}
		for k, v := range m {
			if op, ok := boolOps[k]; ok {
				var b bool
				if json.Unmarshal(v, &b) != nil {
					p.ps.add(cp, "%s: true o false", k)
				}
				out = append(out, Cond{op, b2u(b)})
				continue
			}
			switch k {
			case "battBelow":
				var n int
				if json.Unmarshal(v, &n) != nil || n < 1 || n > 100 {
					p.ps.add(cp, "battBelow: da 1 a 100")
				}
				out = append(out, Cond{CondBattBelow, byte(n)})
			case "sessions":
				var s struct{ Min, Max *int }
				if json.Unmarshal(v, &s) != nil || (s.Min == nil && s.Max == nil) {
					p.ps.add(cp, "sessions: {min, max} da 0 a 6")
					continue
				}
				for _, mm := range []struct {
					v  *int
					op byte
				}{{s.Min, CondSessionsMin}, {s.Max, CondSessionsMax}} {
					if mm.v == nil {
						continue
					}
					if *mm.v < 0 || *mm.v > 6 {
						p.ps.add(cp, "sessions: {min, max} da 0 a 6")
					}
					out = append(out, Cond{mm.op, byte(*mm.v)})
				}
			case "state":
				var s string
				_ = json.Unmarshal(v, &s)
				k := index(states, s)
				if k < 0 {
					p.ps.add(cp, "state: working, permission, input, error, compact, done o idle")
				}
				out = append(out, Cond{CondState, byte(k)})
			case "blink":
				var s string
				_ = json.Unmarshal(v, &s)
				if s != "even" && s != "odd" {
					p.ps.add(cp, "blink: even o odd")
				}
				out = append(out, Cond{CondBlink, b2u(s == "odd")})
			default:
				p.ps.add(cp, "condizione sconosciuta «%s»", k)
			}
		}
	}
	return out
}

func b2u(b bool) byte {
	if b {
		return 1
	}
	return 0
}
```

Nota: le coordinate di un elemento dentro una riga restano relative alla riga (`dx, dy = 0`); il controllo ±512 vale lo stesso.

- [ ] **Step 7: Scrivere `protocol/schema/theme.schema.json`**

Schema Draft 2020-12 che descrive §1–§4: radice (`required`: format, version, id, name, screen, screens; `additionalProperties: false`; `id` con `pattern` `^[a-z0-9]+(-[a-z0-9]+)*$` e `maxLength` 24; `screens` con `propertyNames` `enum` delle 12 chiavi di §1.1, così la schermata `perm` non passa), `$defs/layout` (`variants` 1–8), `$defs/variant` (`when` enum, `ref` enum, `elements`), `$defs/element` con `type` enum dei 14 tipi e un `allOf` di `if {properties: {type: {const: T}}} then {$ref: "#/$defs/T"}` per tipo (come `message.schema.json`), `$defs/cond` (`minProperties`/`maxProperties` 1), `$defs/font` (`family` enum `ui`/`pix`, `size`, `weight` interi). Le regole incrociate (font esistenti, valori nei testi, conteggi) restano solo nel validatore Go: lo dice il `$comment` della radice.

- [ ] **Step 8: Eseguire i test**

Run: `cd service && go test ./internal/theme/ -v`
Expected: PASS (`TestParseFixtures`, `TestParseRejects`, `TestParseLimits`, `TestParseWarnsBigFontChars`, `TestTemplate`, `TestSchemaAcceptsFixtures`, `TestFontsMatchFirmware`).

- [ ] **Step 9: Commit**

```bash
git add docs/TEMI.md protocol/schema/theme.schema.json protocol/themes/fixtures service/internal/theme
git commit -m "Temi: formato JSON, schema, temi di prova e controlli nel servizio"
```

---
### Task 2: Compilatore JSON → CTHM v1

**Files:**
- Modify: `docs/TEMI.md` (aggiungere §5)
- Create: `service/internal/theme/compile.go`
- Create (generati dal test con `-update`): `protocol/themes/fixtures/{prova,elementi,lista}.cthm`
- Test: `service/internal/theme/compile_test.go`

**Interfaces:**
- Consumes: `Parse`, `Theme`, `Element`, `Seg`, `Cond`, costanti del Task 1.
- Produces:
  - `type Compiled struct { Bin []byte; CRC uint32; Screens map[string]int; Warnings []string }` (`Screens`: byte del blocco per chiave);
  - `type InvalidError struct { Problems []Problem }` con `Error() string` (problemi uniti da «; »);
  - `func Compile(t *Theme) (*Compiled, error)` (errore = `*InvalidError` per limiti di dimensione);
  - `func CompileJSON(data []byte) (*Theme, *Compiled, error)` (Parse + Compile; errori di Parse come `*InvalidError`);
  - codici dei tipi `TypeMascot = 1 … TypeList = 12` (ordine: mascot, text, battery, wifi, stateIcon, bar, qr, sym, line, rect, circle, list).

- [ ] **Step 1: Aggiungere a `docs/TEMI.md` il §5 «Binario CTHM v1»**

```markdown
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
```

- [ ] **Step 2: Scrivere il test che fallisce**

`service/internal/theme/compile_test.go`:

```go
package theme

import (
	"bytes"
	"encoding/binary"
	"errors"
	"flag"
	"hash/crc32"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

var update = flag.Bool("update", false, "riscrive i .cthm di protocol/themes/fixtures")

func TestCompileFixtures(t *testing.T) {
	for _, n := range []string{"prova", "elementi", "lista"} {
		_, c, err := CompileJSON(read(t, n+".theme.json"))
		if err != nil {
			t.Fatalf("%s: %v", n, err)
		}
		path := filepath.Join(fixtures, n+".cthm")
		if *update {
			if err := os.WriteFile(path, c.Bin, 0o644); err != nil {
				t.Fatal(err)
			}
		}
		want, err := os.ReadFile(path)
		if err != nil {
			t.Fatalf("%v (go test ./internal/theme -run TestCompileFixtures -update)", err)
		}
		if !bytes.Equal(want, c.Bin) {
			t.Fatalf("%s.cthm vecchio: rigenera con -update", n)
		}
	}
}

func TestCompileHeader(t *testing.T) {
	_, c, err := CompileJSON(read(t, "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	b := c.Bin
	le := binary.LittleEndian
	if string(b[:4]) != "CTHM" || b[4] != 1 || b[5] != HeaderLen || le.Uint16(b[16:]) != 250 || le.Uint16(b[18:]) != 122 {
		t.Fatalf("intestazione: % x", b[:24])
	}
	if int(le.Uint32(b[12:])) != len(b) || le.Uint32(b[8:]) != crc32.ChecksumIEEE(b[12:]) || c.CRC != le.Uint32(b[8:]) {
		t.Fatal("lunghezza o CRC")
	}
	if strings.TrimRight(string(b[20:44]), "\x00") != "prova" || strings.TrimRight(string(b[44:68]), "\x00") != "Mascotte grande" {
		t.Fatal("id o nome")
	}
	if b[68] != 2 || b[72] != 1 || b[80] != 3 { // home (1), sleep (3)
		t.Fatalf("indice: % x", b[68:88])
	}
	home := int(le.Uint32(b[76:]))
	n := int(le.Uint16(b[74:]))
	if c.Screens["home"] != n || b[home] != 3 { // 3 varianti
		t.Fatalf("blocco della home: %d byte, %d varianti", n, b[home])
	}
	// prima variante: when none (1), ref selected (1)
	if b[home+4] != 1 || b[home+5] != 1 {
		t.Fatalf("variante 0: % x", b[home+4:home+10])
	}
}

func TestCompileDeterministic(t *testing.T) {
	_, a, _ := CompileJSON(read(t, "elementi.theme.json"))
	_, b, _ := CompileJSON(read(t, "elementi.theme.json"))
	if !bytes.Equal(a.Bin, b.Bin) {
		t.Fatal("due compilazioni diverse")
	}
}

func TestCompileBlockLimit(t *testing.T) {
	// 60 testi da 60 byte diversi: più di 4 096 byte in una schermata
	var sb strings.Builder
	sb.WriteString(`{"format":"calamaio-theme","version":1,"id":"grosso","name":"Grosso","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[`)
	for i := 0; i < 60; i++ {
		if i > 0 {
			sb.WriteString(",")
		}
		sb.WriteString(`{"type":"text","x":0,"y":0,"w":100,"font":{"family":"ui","size":10,"weight":500},"text":"`)
		sb.WriteString(strings.Repeat(string(rune('a'+i%26)), 50) + string(rune('A'+i/26)) + strings.Repeat("x", 9))
		sb.WriteString(`"}`)
	}
	sb.WriteString(`]}]}}}`)
	_, _, err := CompileJSON([]byte(sb.String()))
	var ie *InvalidError
	if !errors.As(err, &ie) || !strings.Contains(err.Error(), "oltre 4096 byte") {
		t.Fatalf("limite del blocco: %v", err)
	}
}

func TestCompileStringsShared(t *testing.T) {
	// lo stesso testo fisso usato due volte si scrive una volta sola nella tabella
	j := `{"format":"calamaio-theme","version":1,"id":"due","name":"Due","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[` +
		`{"type":"text","x":0,"y":0,"w":10,"font":{"family":"ui","size":10,"weight":500},"text":"uguale"},` +
		`{"type":"text","x":0,"y":20,"w":10,"font":{"family":"ui","size":10,"weight":500},"text":"uguale"}]}]}}}`
	_, c, err := CompileJSON([]byte(j))
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Count(c.Bin, []byte("uguale")) != 1 {
		t.Fatal("stringa ripetuta")
	}
}
```

- [ ] **Step 3: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/theme/ -run TestCompile -v`
Expected: FAIL (`undefined: CompileJSON`).

- [ ] **Step 4: Scrivere `compile.go`**

```go
package theme

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"strings"
)

const (
	TypeMascot byte = 1 + iota
	TypeText
	TypeBattery
	TypeWifi
	TypeStateIcon
	TypeBar
	TypeQr
	TypeSym
	TypeLine
	TypeRect
	TypeCircle
	TypeList
)

var typeCodes = map[string]byte{"mascot": TypeMascot, "text": TypeText, "clock": TypeText, "battery": TypeBattery, "wifi": TypeWifi,
	"stateIcon": TypeStateIcon, "bar": TypeBar, "qr": TypeQr, "sym": TypeSym, "line": TypeLine, "rect": TypeRect, "circle": TypeCircle,
	"list": TypeList}

type Compiled struct {
	Bin      []byte
	CRC      uint32
	Screens  map[string]int
	Warnings []string
}

type InvalidError struct{ Problems []Problem }

func (e *InvalidError) Error() string {
	s := make([]string, len(e.Problems))
	for i, p := range e.Problems {
		s[i] = p.String()
	}
	return strings.Join(s, "; ")
}

func CompileJSON(data []byte) (*Theme, *Compiled, error) {
	t, ps, warns := Parse(data)
	if len(ps) > 0 {
		return t, nil, &InvalidError{ps}
	}
	c, err := Compile(t)
	if c != nil {
		c.Warnings = append(warns, c.Warnings...)
	}
	return t, c, err
}

// strtab: tabella delle stringhe di un blocco, senza doppioni; posizioni dall'inizio della tabella.
type strtab struct {
	data []byte
	at   map[string]int
}

func (s *strtab) ref(v string) int {
	if k, ok := s.at[v]; ok {
		return k
	}
	k := len(s.data)
	s.at[v] = k
	s.data = append(append(s.data, byte(len(v))), v...)
	return k
}

func le16(b []byte, v int) []byte { return binary.LittleEndian.AppendUint16(b, uint16(int16(v))) }

func refCode(r string, inRow bool) byte {
	switch r {
	case "urgent":
		return 0
	case "selected":
		return 1
	case "row":
		return 2
	case "nobody":
		return 3
	}
	if inRow {
		return 2
	}
	return 4
}

func segs(b []byte, ss []Seg, st *strtab) []byte {
	b = append(b, byte(len(ss)))
	for _, s := range ss {
		if s.IsVal {
			b = append(b, 1|s.Filter<<2)
			b = le16(b, int(s.Val))
		} else {
			b = append(b, 0)
			b = le16(b, st.ref(s.Lit))
		}
	}
	return b
}

func payload(e *Element, inRow bool, st *strtab) []byte {
	var p []byte
	switch e.Type {
	case "mascot":
		pose := byte(0xFF)
		if e.Pose != "auto" {
			pose = byte(index(poses, e.Pose))
		}
		p = []byte{byte(e.Scale), pose, refCode(e.Ref, inRow), b2u(e.Accessory) | b2u(e.Animated)<<1}
	case "text", "clock":
		wp := byte(e.Font.Weight / 100)
		if e.Font.Family == "pix" {
			wp |= 0x80
		}
		p = []byte{byte(e.Font.Size), wp, byte(index([]string{"left", "center", "right"}, e.Align)), byte(e.Lines), byte(e.LineHeight), b2u(e.Upper)}
		p = segs(p, e.Segs, st)
	case "battery":
		p = []byte{byte(e.Scale), b2u(e.Percent)}
	case "wifi":
		p = []byte{byte(e.Scale)}
	case "stateIcon":
		p = []byte{byte(e.Scale), refCode(e.Ref, inRow)}
	case "bar":
		p = []byte{byte(index(sources, e.Source)), b2u(e.Style == "bar")}
	case "qr":
		p = segs(nil, e.Segs, st)
	case "sym":
		p = []byte{byte(index(syms, e.Sym)), byte(e.Scale)}
	case "line":
		p = []byte{b2u(e.Dir == "v"), b2u(e.Style == "dotted")}
	case "rect":
		p = []byte{byte(e.Border), b2u(e.Fill), byte(e.Radius)}
	case "circle":
		p = []byte{byte(e.Border), b2u(e.Fill)}
	case "list":
		p = []byte{byte(e.RowH), byte(e.Gap), byte(e.PageSec), b2u(e.PageDots), byte(len(e.Row)), 0}
	}
	return p
}

func elem(b []byte, e *Element, inRow bool, st *strtab) []byte {
	p := payload(e, inRow, st)
	b = append(b, typeCodes[e.Type], b2u(e.Invert))
	b = le16(b, e.X)
	b = le16(b, e.Y)
	b = le16(b, e.W)
	b = le16(b, e.H)
	b = append(b, byte(len(e.Conds)), byte(len(p)))
	for _, c := range e.Conds {
		b = append(b, c.Op, c.Arg)
	}
	b = append(b, p...)
	for _, r := range e.Row {
		b = elem(b, r, true, st)
	}
	return b
}

func block(l *Layout) []byte {
	st := &strtab{at: map[string]int{}}
	nv := len(l.Variants)
	var bodies [][]byte
	for _, v := range l.Variants {
		var b []byte
		for _, e := range v.Elements {
			b = elem(b, e, false, st)
		}
		bodies = append(bodies, b)
	}
	off := 4 + 6*nv
	so := off
	for _, b := range bodies {
		so += len(b)
	}
	out := []byte{byte(nv), 0}
	out = le16(out, so)
	for i, v := range l.Variants {
		when := byte(index([]string{"always", "none", "one", "many"}, v.When))
		out = append(out, when, refCode(v.Ref, false))
		out = le16(out, off)
		out = le16(out, len(v.Elements))
		off += len(bodies[i])
	}
	for _, b := range bodies {
		out = append(out, b...)
	}
	return append(out, st.data...)
}

// Compile scrive il CTHM v1 (docs/TEMI.md §5). Il tema deve venire da Parse senza errori.
func Compile(t *Theme) (*Compiled, error) {
	c := &Compiled{Screens: map[string]int{}}
	var ps problems
	type scr struct {
		code byte
		b    []byte
	}
	var list []scr
	for _, k := range ScreenKeys { // ordine dei codici
		l, ok := t.Screens[k]
		if !ok {
			continue
		}
		code, _ := ScreenCode(k)
		b := block(l)
		c.Screens[k] = len(b)
		if len(b) > MaxBlock {
			ps.add("screens."+k, "%d byte: oltre %d byte per schermata (togli elementi o testi)", len(b), MaxBlock)
		}
		list = append(list, scr{code, b})
	}
	out := make([]byte, HeaderLen, HeaderLen+8*len(list))
	copy(out, "CTHM")
	out[4], out[5] = 1, HeaderLen
	binary.LittleEndian.PutUint16(out[16:], 250)
	binary.LittleEndian.PutUint16(out[18:], 122)
	copy(out[20:44], t.ID)
	copy(out[44:68], t.Name)
	out[68] = byte(len(list))
	pos := HeaderLen + 8*len(list)
	for _, s := range list {
		out = append(out, s.code, 0)
		out = binary.LittleEndian.AppendUint16(out, uint16(len(s.b)))
		out = binary.LittleEndian.AppendUint32(out, uint32(pos))
		pos += len(s.b)
	}
	for _, s := range list {
		out = append(out, s.b...)
	}
	if len(out) > MaxFile {
		ps.add("", "%d byte: oltre %d byte per tema", len(out), MaxFile)
	}
	if len(ps) > 0 {
		return c, &InvalidError{ps}
	}
	binary.LittleEndian.PutUint32(out[12:], uint32(len(out)))
	c.CRC = crc32.ChecksumIEEE(out[12:])
	binary.LittleEndian.PutUint32(out[8:], c.CRC)
	c.Bin = out
	return c, nil
}

var _ = fmt.Sprintf
```

(togliere l'ultima riga se `fmt` non serve dopo la stesura.)

- [ ] **Step 5: Generare i `.cthm` e far passare i test**

Run: `cd service && go test ./internal/theme/ -run TestCompileFixtures -update && go test ./internal/theme/ -v`
Expected: PASS; tre file nuovi in `protocol/themes/fixtures/` (ognuno sotto 64 KB, ogni blocco sotto 4 096 byte).

- [ ] **Step 6: Commit**

```bash
git add docs/TEMI.md service/internal/theme/compile.go service/internal/theme/compile_test.go protocol/themes/fixtures/*.cthm
git commit -m "Temi: compilatore JSON → CTHM v1 e binari dei temi di prova"
```

---
### Task 3: Firmware — lettura e controllo di CTHM (`core/theme_format`)

**Files:**
- Create: `firmware/src/core/theme_format.h`, `firmware/src/core/theme_format.cpp`
- Test: `firmware/test/test_theme_format/test_main.cpp`

**Interfaces:**
- Consumes: i `.cthm` del Task 2 (letti da disco nel test), `hasFont()` di `gfx/text.h`, `avatarCrc32()` di `gfx/avatar.h` solo nel test.
- Produces (namespace `cal::theme`, tutto puro):
  - costanti `kHeaderLen = 72`, `kIdLen = 25`, `kNameLen = 25`, `kMaxFile = 65536`, `kMaxBlock = 4096`, `kMaxScreens = 12`, `kMaxThemes = 8`, `kMaxVariants = 8`, `kMaxElements = 64`, `kMaxConds = 8`, `kMaxSegs = 16`, `kMaxRow = 32`, `kCoordMax = 512`;
  - `enum class Screen : uint8_t { None, Home, Detail, Sleep, Offline, Riepilogo, Oggi, Batteria, Stato, Limiti, Focus, Ambiente, Pet, Count }`, `const char *screenName(Screen)`;
  - `enum class Type : uint8_t { None, Mascot, Text, Battery, Wifi, StateIcon, Bar, Qr, Sym, Line, Rect, Circle, List }`;
  - `enum class When : uint8_t { Always, None, One, Many }`, `enum class Ref : uint8_t { Urgent, Selected, Row, Nobody, Variant }`;
  - `enum class CondOp : uint8_t { None, Perm, BattBelow, Charging, Night, Pc, Workday, Game, SessionsMin, SessionsMax, State, Blink }`;
  - `enum class Error : uint8_t { Ok, Short, Magic, Version, Header, Length, Crc, Size, Id, Name, Index, Block, Variant, Element, Cond, Payload, Font, Value, String, Utf8, Nesting }`, `const char *errorText(Error)`;
  - `struct Header { uint32_t crc, length; char id[kIdLen]; char name[kNameLen]; uint8_t nScreens; }`, `struct IndexEntry { Screen screen; uint16_t len; uint32_t off; }`;
  - `struct Elem { Type type; bool invert; int16_t x, y, w, h; uint8_t nConds; const uint8_t *conds; uint8_t plen; const uint8_t *payload; const uint8_t *next; }`;
  - `struct Variant { When when; Ref ref; uint16_t off; uint16_t count; }`;
  - `uint32_t crc32Update(uint32_t crc, const uint8_t *p, size_t n)` (continua un CRC-32 IEEE; si parte da 0);
  - `Error parseHeader(const uint8_t *p, size_t n, Header &out)` (bastano i primi 72 byte);
  - `Error parseIndex(const uint8_t *idx, size_t n, const Header &h, IndexEntry *out, int cap)` (`idx` = i `8·nScreens` byte da 72);
  - `Error checkBlock(const uint8_t *b, size_t n)`;
  - `Error checkFile(const uint8_t *p, size_t n, Header *h = nullptr)`;
  - `bool findScreen(const uint8_t *file, size_t n, Screen s, const uint8_t *&block, uint16_t &len)` (solo su file già controllati);
  - `int variantCount(const uint8_t *b)`, `bool variantAt(const uint8_t *b, size_t n, int k, Variant &out)`, `bool readElem(const uint8_t *b, size_t n, const uint8_t *p, Elem &out)`, `bool stringAt(const uint8_t *b, size_t n, uint16_t rel, const char *&s, uint8_t &len)`;
  - valori: `const uint16_t kValClock = 0x80, kValClockSec = 0x81, kValRow = 0x40; bool validValue(uint16_t id)`.

- [ ] **Step 1: Scrivere il test che fallisce**

`firmware/test/test_theme_format/test_main.cpp`:

```cpp
// Temi (docs/TEMI.md §5): lettura e controllo di CTHM. I file veri vengono dal compilatore del servizio
// (protocol/themes/fixtures/*.cthm); i casi rovinati si costruiscono cambiando i byte e rifacendo il CRC.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "core/theme_format.h"

using namespace cal::theme;

void setUp() {}
void tearDown() {}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  if (k != std::string::npos) return f.substr(0, k);
  return "../";
}

static std::vector<uint8_t> load(const char *name) {
  std::ifstream in(repoRoot() + "protocol/themes/fixtures/" + name, std::ios::binary);
  TEST_ASSERT_TRUE_MESSAGE(in.good(), name);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void reseal(std::vector<uint8_t> &f) {
  const uint32_t c = crc32Update(0, f.data() + 12, f.size() - 12);
  f[8] = c; f[9] = c >> 8; f[10] = c >> 16; f[11] = c >> 24;
}

static void test_fixtures_ok() {
  for (const char *n : {"prova.cthm", "elementi.cthm", "lista.cthm"}) {
    std::vector<uint8_t> f = load(n);
    Header h;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", errorText(checkFile(f.data(), f.size(), &h)), n);
    TEST_ASSERT_EQUAL_UINT32(f.size(), h.length);
  }
  std::vector<uint8_t> f = load("prova.cthm");
  Header h;
  checkFile(f.data(), f.size(), &h);
  TEST_ASSERT_EQUAL_STRING("prova", h.id);
  TEST_ASSERT_EQUAL_STRING("Mascotte grande", h.name);
  TEST_ASSERT_EQUAL(2, h.nScreens);
  const uint8_t *b;
  uint16_t len;
  TEST_ASSERT_TRUE(findScreen(f.data(), f.size(), Screen::Home, b, len));
  TEST_ASSERT_EQUAL(3, variantCount(b));
  Variant v;
  TEST_ASSERT_TRUE(variantAt(b, len, 0, v));
  TEST_ASSERT_EQUAL(int(When::None), int(v.when));
  TEST_ASSERT_EQUAL(4, v.count);
  TEST_ASSERT_FALSE(findScreen(f.data(), f.size(), Screen::Oggi, b, len));
}

static void test_header_rejects() {
  std::vector<uint8_t> f = load("prova.cthm");
  struct { int off; uint8_t v; Error want; } cases[] = {
      {0, 'X', Error::Magic}, {4, 2, Error::Version}, {5, 64, Error::Header}, {16, 144, Error::Size},
      {20, 'P', Error::Id}, {44, 0, Error::Name}, {68, 13, Error::Index}, {69, 1, Error::Header},
  };
  for (auto &c : cases) {
    std::vector<uint8_t> g = f;
    g[c.off] = c.v;
    reseal(g);
    TEST_ASSERT_EQUAL_STRING(errorText(c.want), errorText(checkFile(g.data(), g.size())));
  }
  std::vector<uint8_t> g = f;
  g[g.size() - 1] ^= 0x01;  // senza rifare il CRC
  TEST_ASSERT_EQUAL(int(Error::Crc), int(checkFile(g.data(), g.size())));
  TEST_ASSERT_EQUAL(int(Error::Short), int(checkFile(f.data(), 40)));
  TEST_ASSERT_EQUAL(int(Error::Length), int(checkFile(f.data(), f.size() - 1)));
}

// Ogni troncamento di ogni blocco: mai una lettura fuori (ASan/valgrind in native), mai Ok.
static void test_check_block_every_truncation() {
  for (const char *n : {"prova.cthm", "elementi.cthm", "lista.cthm"}) {
    std::vector<uint8_t> f = load(n);
    for (int s = 1; s < int(Screen::Count); s++) {
      const uint8_t *b;
      uint16_t len;
      if (!findScreen(f.data(), f.size(), Screen(s), b, len)) continue;
      TEST_ASSERT_EQUAL(int(Error::Ok), int(checkBlock(b, len)));
      for (uint16_t k = 0; k < len; k++) {
        std::vector<uint8_t> cut(b, b + k);  // copia esatta: fuori = fuori dal vettore
        TEST_ASSERT_NOT_EQUAL(int(Error::Ok), int(checkBlock(cut.data(), cut.size())));
      }
    }
  }
}

// Byte cambiati uno alla volta (tutti i valori su ogni posizione di un blocco): può essere Ok o no, mai un crash.
static void test_check_block_byte_flips() {
  std::vector<uint8_t> f = load("elementi.cthm");
  const uint8_t *b;
  uint16_t len;
  TEST_ASSERT_TRUE(findScreen(f.data(), f.size(), Screen::Detail, b, len));
  std::vector<uint8_t> blk(b, b + len);
  int bad = 0;
  for (size_t i = 0; i < blk.size(); i++) {
    const uint8_t keep = blk[i];
    for (int v = 0; v < 256; v += 7) {
      blk[i] = uint8_t(v);
      bad += checkBlock(blk.data(), blk.size()) != Error::Ok;
    }
    blk[i] = keep;
  }
  TEST_ASSERT_TRUE(bad > 0);
}

// Blocchi costruiti a mano: un elemento per caso.
static std::vector<uint8_t> one(const std::vector<uint8_t> &elem, const std::vector<uint8_t> &strs = {}) {
  std::vector<uint8_t> b = {1, 0, 0, 0, 0 /*always*/, 1 /*selected*/, 10, 0, 1, 0};
  b.insert(b.end(), elem.begin(), elem.end());
  const uint16_t so = uint16_t(b.size());
  b[2] = so; b[3] = so >> 8;
  b.insert(b.end(), strs.begin(), strs.end());
  return b;
}
static std::vector<uint8_t> el(uint8_t type, const std::vector<uint8_t> &pl, uint8_t flags = 0, std::vector<uint8_t> conds = {}) {
  std::vector<uint8_t> e = {type, flags, 0, 0, 0, 0, 10, 0, 10, 0, uint8_t(conds.size() / 2), uint8_t(pl.size())};
  e.insert(e.end(), conds.begin(), conds.end());
  e.insert(e.end(), pl.begin(), pl.end());
  return e;
}

static void test_check_block_rules() {
  auto ok = [](const std::vector<uint8_t> &b) { return checkBlock(b.data(), b.size()); };
  TEST_ASSERT_EQUAL(int(Error::Ok), int(ok(one(el(1, {2, 0xFF, 4, 3})))));
  TEST_ASSERT_EQUAL(int(Error::Payload), int(ok(one(el(1, {5, 0xFF, 4, 3})))));        // scala 5
  TEST_ASSERT_EQUAL(int(Error::Nesting), int(ok(one(el(1, {2, 0xFF, 2, 3})))));        // ref row fuori da una lista
  TEST_ASSERT_EQUAL(int(Error::Element), int(ok(one(el(13, {})))));                    // tipo sconosciuto
  TEST_ASSERT_EQUAL(int(Error::Element), int(ok(one(el(4, {1}, 0x02)))));              // opzione sconosciuta
  // testo: 10 px 500 Chakra, un pezzo fisso «ciao»
  const std::vector<uint8_t> s = {4, 'c', 'i', 'a', 'o'};
  TEST_ASSERT_EQUAL(int(Error::Ok), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 0, 0, 0}), s))));
  TEST_ASSERT_EQUAL(int(Error::Font), int(ok(one(el(2, {17, 7, 0, 1, 12, 0, 1, 0, 0, 0}), s))));
  TEST_ASSERT_EQUAL(int(Error::String), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 0, 9, 0}), s))));   // fuori tabella
  TEST_ASSERT_EQUAL(int(Error::Value), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 1, 0x30, 0}), s)))); // valore 0x30
  TEST_ASSERT_EQUAL(int(Error::Nesting), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 1, 0x40, 0}), s))));// riga.* fuori
  TEST_ASSERT_EQUAL(int(Error::Utf8), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 0, 0, 0}), {2, 0xC3, 0x28}))));
  TEST_ASSERT_EQUAL(int(Error::Cond), int(ok(one(el(4, {1}, 0, {12, 0})))));          // condizione 12
  TEST_ASSERT_EQUAL(int(Error::Cond), int(ok(one(el(4, {1}, 0, {2, 0})))));           // battBelow 0
  // lista con una riga che contiene un'altra lista
  std::vector<uint8_t> inner = el(12, {10, 0, 5, 1, 1, 0});
  std::vector<uint8_t> list = el(12, {20, 0, 5, 1, 1, 0});
  list.insert(list.end(), inner.begin(), inner.end());
  TEST_ASSERT_EQUAL(int(Error::Nesting), int(ok(one(list))));
  // coordinate fuori da ±512
  std::vector<uint8_t> far = el(4, {1});
  far[2] = 0x01; far[3] = 0x02;  // x = 513
  TEST_ASSERT_EQUAL(int(Error::Element), int(ok(one(far))));
}

static void test_crc_update_matches_whole() {
  const uint8_t d[] = "calamaio-tema";
  const uint32_t whole = crc32Update(0, d, 13);
  TEST_ASSERT_EQUAL_HEX32(whole, crc32Update(crc32Update(0, d, 5), d + 5, 8));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Update(0, reinterpret_cast<const uint8_t *>("123456789"), 9));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fixtures_ok);
  RUN_TEST(test_header_rejects);
  RUN_TEST(test_check_block_every_truncation);
  RUN_TEST(test_check_block_byte_flips);
  RUN_TEST(test_check_block_rules);
  RUN_TEST(test_crc_update_matches_whole);
  return UNITY_END();
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd firmware && pio test -e native -f test_theme_format`
Expected: FAIL (manca `core/theme_format.h`).

- [ ] **Step 3: Scrivere `core/theme_format.h`**

Le dichiarazioni dell'elenco «Produces» sopra, con un commento di testa: «Temi (docs/TEMI.md §5): formato CTHM v1. Logica pura: nessuna allocazione, nessun accesso fuori dai byte passati (test_theme_format).» Le enum hanno i valori nell'ordine dei codici del §5 (per esempio `Type::Mascot` = 1, `CondOp::Blink` = 11, `Screen::Pet` = 12).

- [ ] **Step 4: Scrivere `core/theme_format.cpp`**

```cpp
#include "core/theme_format.h"

#include <string.h>

#include "gfx/text.h"

namespace cal {
namespace theme {

namespace {

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
int16_t rds16(const uint8_t *p) { return int16_t(rd16(p)); }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool utf8ok(const uint8_t *s, int n) {
  for (int i = 0; i < n;) {
    const uint8_t c = s[i];
    // byte iniziale: ASCII, 2 byte (mai 0xC0 e 0xC1, sovralunghe), 3 o 4 byte
    const int k = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
    if (k < 0 || i + k >= n) return false;
    for (int j = 1; j <= k; j++)
      if ((s[i + j] & 0xC0) != 0x80) return false;
    i += k + 1;
  }
  return true;
}

// Campo di testo a lunghezza fissa completato con zeri: n byte, almeno uno a zero alla fine del contenuto.
bool padded(const uint8_t *p, int n, char *out, bool kebab) {
  int len = 0;
  while (len < n && p[len]) len++;
  if (len == 0 || len > 24) return false;
  for (int i = len; i < n; i++)
    if (p[i]) return false;
  if (kebab) {
    for (int i = 0; i < len; i++) {
      const char c = char(p[i]);
      const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
      if (!alnum && !(c == '-' && i > 0 && i < len - 1 && p[i - 1] != '-')) return false;
    }
  } else {
    if (!utf8ok(p, len)) return false;
    for (int i = 0; i < len; i++)
      if (p[i] < 0x20 || p[i] == 0x7F) return false;
  }
  memcpy(out, p, len);
  out[len] = 0;
  return true;
}

bool coordOk(int v, bool size) { return size ? v >= 0 && v <= kCoordMax : v >= -kCoordMax && v <= kCoordMax; }

Error checkSegs(const uint8_t *b, size_t n, const uint8_t *p, int nseg, bool inRow) {
  const uint16_t so = rd16(b + 2);
  for (int i = 0; i < nseg; i++, p += 3) {
    const uint8_t kf = p[0];
    if ((kf & 0xF0) || (kf & 3) > 1 || ((kf >> 2) & 3) > 2) return Error::Payload;
    const uint16_t arg = rd16(p + 1);
    if ((kf & 3) == 0) {
      const char *s;
      uint8_t len;
      if (!stringAt(b, n, arg, s, len)) return Error::String;
      if (!utf8ok(reinterpret_cast<const uint8_t *>(s), len)) return Error::Utf8;
    } else {
      if (!validValue(arg)) return Error::Value;
      if (arg >= kValRow && arg < kValClock && !inRow) return Error::Nesting;
    }
  }
  (void)so;
  return Error::Ok;
}

Error checkCond(uint8_t op, uint8_t arg) {
  switch (CondOp(op)) {
    case CondOp::Perm: case CondOp::Charging: case CondOp::Night: case CondOp::Pc: case CondOp::Workday:
    case CondOp::Game: case CondOp::Blink: return arg <= 1 ? Error::Ok : Error::Cond;
    case CondOp::BattBelow: return arg >= 1 && arg <= 100 ? Error::Ok : Error::Cond;
    case CondOp::SessionsMin: case CondOp::SessionsMax: case CondOp::State: return arg <= 6 ? Error::Ok : Error::Cond;
    default: return Error::Cond;
  }
}

Error checkRef(uint8_t r, bool inRow) {
  if (r > uint8_t(Ref::Variant)) return Error::Payload;
  if (r == uint8_t(Ref::Row) && !inRow) return Error::Nesting;
  return Error::Ok;
}

// Un elemento (con le righe, se è una lista) da *pos dentro [.., end). count: elementi visti finora.
Error checkElem(const uint8_t *b, size_t end, size_t &pos, bool inRow, int &count) {
  Elem e;
  if (!readElem(b, end, b + pos, e)) return Error::Element;
  if (++count > kMaxElements) return Error::Variant;
  if (!coordOk(e.x, false) || !coordOk(e.y, false) || !coordOk(e.w, true) || !coordOk(e.h, true)) return Error::Element;
  for (int i = 0; i < e.nConds; i++) {
    const Error c = checkCond(e.conds[2 * i], e.conds[2 * i + 1]);
    if (c != Error::Ok) return c;
  }
  const uint8_t *p = e.payload;
  const int pl = e.plen;
  Error r = Error::Ok;
  switch (e.type) {
    case Type::Mascot:
      if (pl != 4 || p[0] < 1 || p[0] > 4 || (p[1] > 8 && p[1] != 0xFF) || (p[3] & ~3)) return Error::Payload;
      r = checkRef(p[2], inRow);
      break;
    case Type::Text: {
      if (pl < 7 || p[6] > kMaxSegs || pl != 7 + 3 * p[6]) return Error::Payload;
      if (p[2] > 2 || p[3] < 1 || p[3] > 8 || p[4] < 1 || p[4] > 64 || (p[5] & ~1)) return Error::Payload;
      const bool pix = p[1] & 0x80;
      if (!hasFont(p[0], (p[1] & 0x7F) * 100, pix)) return Error::Font;
      r = checkSegs(b, end, p + 7, p[6], inRow);
      break;
    }
    case Type::Battery: if (pl != 2 || p[0] < 1 || p[0] > 2 || p[1] > 1) return Error::Payload; break;
    case Type::Wifi: if (pl != 1 || p[0] < 1 || p[0] > 2) return Error::Payload; break;
    case Type::StateIcon:
      if (pl != 2 || p[0] < 1 || p[0] > 3) return Error::Payload;
      r = checkRef(p[1], inRow);
      break;
    case Type::Bar: if (pl != 2 || p[0] > 5 || p[1] > 1) return Error::Payload; break;
    case Type::Qr:
      if (pl < 1 || p[0] > kMaxSegs || pl != 1 + 3 * p[0]) return Error::Payload;
      r = checkSegs(b, end, p + 1, p[0], inRow);
      break;
    case Type::Sym: if (pl != 2 || p[0] > 7 || p[1] < 1 || p[1] > 3) return Error::Payload; break;
    case Type::Line: if (pl != 2 || p[0] > 1 || p[1] > 1) return Error::Payload; break;
    case Type::Rect: if (pl != 3 || p[0] > 8 || p[1] > 1 || p[2] > 8) return Error::Payload; break;
    case Type::Circle: if (pl != 2 || p[0] > 8 || p[1] > 1) return Error::Payload; break;
    case Type::List: {
      if (inRow) return Error::Nesting;
      if (pl != 6 || p[0] < 1 || p[0] > 122 || p[1] > 60 || p[2] < 1 || p[2] > 60 || p[3] > 1 || p[4] < 1 || p[4] > kMaxRow || p[5])
        return Error::Payload;
      pos = size_t(e.next - b);
      for (int i = 0; i < p[4]; i++) {
        const Error rr = checkElem(b, end, pos, true, count);
        if (rr != Error::Ok) return rr;
      }
      return Error::Ok;
    }
    default: return Error::Element;
  }
  if (r != Error::Ok) return r;
  pos = size_t(e.next - b);
  return Error::Ok;
}

}  // namespace

const char *screenName(Screen s) {
  static const char *const k[] = {"", "home", "detail", "sleep", "offline", "riepilogo", "oggi", "batteria", "stato", "limiti",
                                  "focus", "ambiente", "pet"};
  return int(s) < int(Screen::Count) ? k[int(s)] : "";
}

const char *errorText(Error e) {
  static const char *const k[] = {
      "ok", "file troppo corto", "non è un tema (magia)", "versione del tema non supportata", "intestazione non valida",
      "lunghezza sbagliata", "CRC sbagliato", "tema per un altro schermo", "id non valido", "nome non valido",
      "indice delle schermate non valido", "blocco di una schermata non valido", "variante non valida", "elemento non valido",
      "condizione non valida", "dati di un elemento non validi", "carattere che la scheda non ha", "valore sconosciuto",
      "testo fuori dal blocco", "testo non UTF-8", "riquadro lista annidato o valore di riga fuori da una lista"};
  return k[int(e)];
}

bool validValue(uint16_t id) { return id <= 9 || (id >= kValRow && id <= kValRow + 9) || (id >= kValClock && id <= 0x95); }

uint32_t crc32Update(uint32_t crc, const uint8_t *p, size_t n) {
  crc = ~crc;
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

Error parseHeader(const uint8_t *p, size_t n, Header &out) {
  memset(&out, 0, sizeof out);
  if (n < size_t(kHeaderLen)) return Error::Short;
  if (memcmp(p, "CTHM", 4)) return Error::Magic;
  if (p[4] != 1) return Error::Version;
  if (p[5] != kHeaderLen || p[6] || p[7] || p[69] || p[70] || p[71]) return Error::Header;
  out.crc = rd32(p + 8);
  out.length = rd32(p + 12);
  if (out.length < uint32_t(kHeaderLen) || out.length > kMaxFile) return Error::Length;
  if (rd16(p + 16) != 250 || rd16(p + 18) != 122) return Error::Size;
  if (!padded(p + 20, 24, out.id, true)) return Error::Id;
  if (!padded(p + 44, 24, out.name, false)) return Error::Name;
  out.nScreens = p[68];
  if (out.nScreens > kMaxScreens || kHeaderLen + 8u * out.nScreens > out.length) return Error::Index;
  return Error::Ok;
}

Error parseIndex(const uint8_t *idx, size_t n, const Header &h, IndexEntry *out, int cap) {
  if (n < 8u * h.nScreens || cap < h.nScreens) return Error::Index;
  uint32_t next = kHeaderLen + 8u * h.nScreens;
  int prev = 0;
  for (int i = 0; i < h.nScreens; i++) {
    const uint8_t *e = idx + 8 * i;
    const uint16_t len = rd16(e + 2);
    const uint32_t off = rd32(e + 4);
    if (e[0] <= prev || e[0] >= uint8_t(Screen::Count) || e[1] || len < 4 || len > kMaxBlock || off < next ||
        off + len > h.length)
      return Error::Index;
    out[i].screen = Screen(e[0]);
    out[i].len = len;
    out[i].off = off;
    prev = e[0];
    next = off + len;
  }
  return Error::Ok;
}

int variantCount(const uint8_t *b) { return b[0]; }

bool variantAt(const uint8_t *b, size_t n, int k, Variant &out) {
  if (n < 4 || k < 0 || k >= b[0] || 4u + 6u * (k + 1) > n) return false;
  const uint8_t *v = b + 4 + 6 * k;
  out.when = When(v[0]);
  out.ref = Ref(v[1]);
  out.off = rd16(v + 2);
  out.count = rd16(v + 4);
  return true;
}

bool readElem(const uint8_t *b, size_t n, const uint8_t *p, Elem &out) {
  if (p < b || size_t(p - b) + 12 > n) return false;
  out.type = Type(p[0]);
  if (p[0] < 1 || p[0] > uint8_t(Type::List) || (p[1] & ~1)) return false;
  out.invert = p[1] & 1;
  out.x = rds16(p + 2);
  out.y = rds16(p + 4);
  out.w = rds16(p + 6);
  out.h = rds16(p + 8);
  out.nConds = p[10];
  out.plen = p[11];
  if (out.nConds > kMaxConds) return false;
  const size_t sz = 12u + 2u * out.nConds + out.plen;
  if (size_t(p - b) + sz > n) return false;
  out.conds = p + 12;
  out.payload = p + 12 + 2 * out.nConds;
  out.next = p + sz;
  return true;
}

bool stringAt(const uint8_t *b, size_t n, uint16_t rel, const char *&s, uint8_t &len) {
  if (n < 4) return false;
  const size_t at = size_t(rd16(b + 2)) + rel;
  if (at + 1 > n) return false;
  len = b[at];
  if (at + 1 + len > n) return false;
  s = reinterpret_cast<const char *>(b + at + 1);
  return true;
}

Error checkBlock(const uint8_t *b, size_t n) {
  if (n < 4 || n > kMaxBlock) return Error::Block;
  const int nv = b[0];
  if (nv < 1 || nv > kMaxVariants || b[1]) return Error::Block;
  const size_t head = 4u + 6u * nv, so = rd16(b + 2);
  if (head > so || so > n) return Error::Block;
  for (int k = 0; k < nv; k++) {
    Variant v;
    if (!variantAt(b, n, k, v)) return Error::Block;
    if (uint8_t(v.when) > 3 || uint8_t(v.ref) > 1 || v.off < head || v.off > so || v.count > kMaxElements) return Error::Variant;
    size_t pos = v.off;
    int count = 0;
    for (int i = 0; i < v.count; i++) {
      const Error e = checkElem(b, so, pos, false, count);
      if (e != Error::Ok) return e;
    }
  }
  for (size_t at = so; at < n;) {  // tabella delle stringhe: tutta ben formata fino alla fine
    const size_t len = b[at];
    if (at + 1 + len > n) return Error::String;
    if (!utf8ok(b + at + 1, int(len))) return Error::Utf8;
    at += 1 + len;
  }
  return Error::Ok;
}

Error checkFile(const uint8_t *p, size_t n, Header *hOut) {
  Header h;
  Error e = parseHeader(p, n, h);
  if (hOut) *hOut = h;
  if (e != Error::Ok) return e;
  if (h.length != n) return Error::Length;
  if (crc32Update(0, p + 12, n - 12) != h.crc) return Error::Crc;
  IndexEntry idx[kMaxScreens];
  e = parseIndex(p + kHeaderLen, n - kHeaderLen, h, idx, kMaxScreens);
  if (e != Error::Ok) return e;
  for (int i = 0; i < h.nScreens; i++) {
    e = checkBlock(p + idx[i].off, idx[i].len);
    if (e != Error::Ok) return e;
  }
  return Error::Ok;
}

bool findScreen(const uint8_t *file, size_t n, Screen s, const uint8_t *&block, uint16_t &len) {
  if (n < size_t(kHeaderLen)) return false;
  const int ns = file[68];
  for (int i = 0; i < ns && kHeaderLen + 8u * (i + 1) <= n; i++) {
    const uint8_t *e = file + kHeaderLen + 8 * i;
    if (e[0] != uint8_t(s)) continue;
    const uint32_t off = rd32(e + 4);
    len = rd16(e + 2);
    if (off + len > n) return false;
    block = file + off;
    return true;
  }
  return false;
}

}  // namespace theme
}  // namespace cal
```

- [ ] **Step 5: Eseguire i test**

Run: `cd firmware && pio test -e native -f test_theme_format`
Expected: PASS (6 test).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/core/theme_format.h firmware/src/core/theme_format.cpp firmware/test/test_theme_format
git commit -m "Temi: lettura e controllo dei CTHM sulla scheda (interprete in sola lettura)"
```

---
### Task 4: Firmware — valori, condizioni e varianti (`core/theme_values`)

**Files:**
- Create: `firmware/src/core/theme_values.h`, `firmware/src/core/theme_values.cpp`
- Test: `firmware/test/test_theme_values/test_main.cpp`

**Interfaces:**
- Consumes: `cal::theme::{Screen, When, Ref, CondOp, stringAt, kValRow, kValClock, kValClockSec}` (Task 3); dal modello `urgentIdx`, `sIdx`, `stateText`, `lineFor`, `metaLine`, `ago`, `pcLabel`, `autonomyText`, `rssiBars`, `jsRound`, `petOn`, `kFwVersion`, `hmLocal`, `dateStr`, `civilFrom`, `kWeekdays`, `workLine`, `workDone`, `minHm`, `Sb`, `scopy`.
- Produces (namespace `cal::theme`):
  - `struct Ctx { const Model *m; Ms now; WallClock wc; int frame; Screen screen; int ref; }` (`ref` = indice della sessione di riferimento della variante, −1 = nessuna);
  - `bool whenTrue(const Model &m, When w)`;
  - `int refIndex(const Model &m, Ms now, Screen s, Ref r)` (per `Urgent` e `Selected`; −1 senza sessioni);
  - `const Session *refSession(const Ctx &cx, Ref r, const Session *row)` (per gli elementi: `Row` → la riga, `Variant` → la riga dentro una lista, altrimenti quella della variante; `Nobody` → nullptr);
  - `int permWaiting(const Model &m)` (sessioni in `Permission`);
  - `void valueText(uint16_t id, const Ctx &cx, const Session *row, char *out, size_t cap)`;
  - `bool condTrue(uint8_t op, uint8_t arg, const Ctx &cx, const Session *subject)`;
  - `bool condsTrue(const uint8_t *conds, int n, const Ctx &cx, const Session *subject)`;
  - `void expand(const uint8_t *b, size_t n, const uint8_t *segs, int nSeg, const Ctx &cx, const Session *row, bool upperAll, char *out, size_t cap)` — testo finale (≤ cap − 1 byte, troncato su un confine UTF-8);
  - `void upperUtf8(char *out, size_t cap, const char *s)`, `void lowerUtf8(char *out, size_t cap, const char *s)` (ASCII e À–þ come `upper()` di `ui/screens.cpp`, senza × e ÷).

- [ ] **Step 1: Scrivere il test che fallisce**

`firmware/test/test_theme_values/test_main.cpp`:

```cpp
// Temi: valori nei testi, condizioni, sessione di riferimento, scelta della variante (docs/TEMI.md §1.2, §3, §4).
#include <string.h>
#include <unity.h>

#include "core/model.h"
#include "core/strbuf.h"
#include "core/theme_values.h"

using namespace cal;
using namespace cal::theme;

void setUp() {}
void tearDown() {}

static Model gM;
static const Ms kNow = 1791707400000LL;

static Model &fresh(int nSessions) {
  newModel(gM, kNow, WallClock{kNow});
  gM.nSessions = 0;
  for (int k = 0; k < nSessions; k++) {
    Session &s = gM.sessions[gM.nSessions++];
    memset(&s, 0, sizeof s);
    Sb(s.name, sizeof s.name).s("sessione-").i(k);
    s.state = SessionState::Working;
    scopy(s.detail, "Bash: npm run test");
    s.since = kNow - 12 * 60000;
    s.ctx = 42;
    s.total = 7;
    s.done = 3;
  }
  // newModel riempie anche i dati finti del simulatore: qui si parte da zero
  gM.sys.wallOk = true;
  gM.sys.batt = 78;
  gM.sys.usb = false;
  gM.ui.selUntil = 0;
  gM.ui.sel = 0;
  memset(&gM.limits, 0, sizeof gM.limits);
  memset(&gM.stats, 0, sizeof gM.stats);
  gM.pet.known = false;
  gM.acc = false;
  return gM;
}

static Ctx ctx(Screen s = Screen::Home) {
  Ctx c;
  c.m = &gM;
  c.now = kNow;
  c.wc = wallFromCivil(2026, 10, 11, 10, 30, 5);
  c.frame = 0;
  c.screen = s;
  c.ref = refIndex(gM, kNow, s, Ref::Selected);
  return c;
}

static const char *val(uint16_t id, const Session *row = nullptr) {
  static char out[256];
  valueText(id, ctx(), row, out, sizeof out);
  return out;
}

static void test_session_values() {
  fresh(2);
  gM.sessions[1].state = SessionState::Permission;  // la più urgente
  TEST_ASSERT_EQUAL_STRING("sessione-1", val(0x00));   // selezionata = urgente quando non c'è selezione
  TEST_ASSERT_EQUAL_STRING("Chiede un permesso", val(0x01));
  TEST_ASSERT_EQUAL_STRING("Bash: npm run test", val(0x02));
  TEST_ASSERT_EQUAL_STRING("42%", val(0x05));
  TEST_ASSERT_EQUAL_STRING("3/7", val(0x06));
  TEST_ASSERT_EQUAL_STRING("12 min", val(0x07));
  TEST_ASSERT_EQUAL_STRING("", val(0x08));             // nessun agente
  TEST_ASSERT_EQUAL_STRING("", val(0x09));             // nessun test
  gM.sessions[1].test = TestResult::Ko;
  TEST_ASSERT_EQUAL_STRING("\xe2\x9c\x97", val(0x09));
  TEST_ASSERT_EQUAL_STRING("sessione-0", val(0x40, &gM.sessions[0]));
  TEST_ASSERT_EQUAL_STRING("", val(0x40));             // riga senza riga: vuoto
  fresh(0);
  TEST_ASSERT_EQUAL_STRING("", val(0x00));             // senza sessioni: vuoto
}

static void test_general_values() {
  fresh(1);
  TEST_ASSERT_EQUAL_STRING("10:30", val(0x80));
  TEST_ASSERT_EQUAL_STRING("10:30:05", val(0x81));
  TEST_ASSERT_EQUAL_STRING("domenica 11 ottobre", val(0x82));
  TEST_ASSERT_EQUAL_STRING("domenica", val(0x83));
  TEST_ASSERT_EQUAL_STRING("1", val(0x84));
  TEST_ASSERT_EQUAL_STRING("0", val(0x85));
  TEST_ASSERT_EQUAL_STRING("78%", val(0x86));
  TEST_ASSERT_EQUAL_STRING("—", val(0x8F));            // limiti senza dati
  gM.limits.hasH5 = true;
  gM.limits.h5 = 36.5;
  TEST_ASSERT_EQUAL_STRING("37%", val(0x8F));
  gM.stats.linesAdd = 120;
  gM.stats.linesDel = 30;
  gM.stats.cost = 1.2;
  TEST_ASSERT_EQUAL_STRING("+120 \xe2\x88\x92" "30", val(0x8D));
  TEST_ASSERT_EQUAL_STRING("1,20 $", val(0x8E));
  TEST_ASSERT_EQUAL_STRING("", val(0x94));             // senza sensore
  TEST_ASSERT_EQUAL_STRING(kFwVersion, val(0x95));
  TEST_ASSERT_EQUAL_STRING("—", val(0x91));            // gioco: nessun dato
  gM.sys.wallOk = false;
  TEST_ASSERT_EQUAL_STRING("--:--", val(0x80));
  TEST_ASSERT_EQUAL_STRING("--:--:--", val(0x81));
  TEST_ASSERT_EQUAL_STRING("", val(0x82));
}

static void test_refs_and_variants() {
  fresh(3);
  gM.sessions[2].state = SessionState::Input;
  TEST_ASSERT_EQUAL(2, refIndex(gM, kNow, Screen::Home, Ref::Urgent));
  gM.ui.sel = 1;
  gM.ui.selUntil = kNow + 1000;
  TEST_ASSERT_EQUAL(1, refIndex(gM, kNow, Screen::Home, Ref::Selected));
  gM.ui.selUntil = 0;
  TEST_ASSERT_EQUAL(1, refIndex(gM, kNow, Screen::Detail, Ref::Selected));  // nel Dettaglio: quella aperta
  TEST_ASSERT_TRUE(whenTrue(gM, When::Many));
  TEST_ASSERT_FALSE(whenTrue(gM, When::One));
  fresh(0);
  TEST_ASSERT_EQUAL(-1, refIndex(gM, kNow, Screen::Home, Ref::Urgent));
  TEST_ASSERT_TRUE(whenTrue(gM, When::None));
  TEST_ASSERT_TRUE(whenTrue(gM, When::Always));
}

static void test_conditions() {
  fresh(2);
  const Ctx c = ctx();
  const Session *s0 = &gM.sessions[0];
  TEST_ASSERT_FALSE(condTrue(1, 1, c, s0));             // perm: nessuna in permesso
  gM.sessions[1].state = SessionState::Permission;
  TEST_ASSERT_TRUE(condTrue(1, 1, c, s0));
  TEST_ASSERT_TRUE(condTrue(2, 80, c, s0));             // batteria 78 < 80
  TEST_ASSERT_FALSE(condTrue(2, 78, c, s0));
  TEST_ASSERT_TRUE(condTrue(3, 0, c, s0));              // non in carica
  TEST_ASSERT_TRUE(condTrue(5, 1, c, s0) == gM.sys.pc);
  TEST_ASSERT_TRUE(condTrue(8, 2, c, s0));              // almeno 2
  TEST_ASSERT_FALSE(condTrue(9, 1, c, s0));             // al massimo 1
  TEST_ASSERT_TRUE(condTrue(10, 0, c, s0));             // state working
  TEST_ASSERT_FALSE(condTrue(10, 0, c, nullptr));       // senza sessione: falso
  TEST_ASSERT_TRUE(condTrue(11, 0, c, s0));             // fotogramma 0: pari
  Ctx odd = c;
  odd.frame = -3;
  TEST_ASSERT_TRUE(condTrue(11, 1, odd, s0));
  const uint8_t two[] = {3, 0, 8, 2};
  TEST_ASSERT_TRUE(condsTrue(two, 2, c, s0));
  const uint8_t no[] = {3, 0, 8, 3};
  TEST_ASSERT_FALSE(condsTrue(no, 2, c, s0));
}

// Blocco minimo con una tabella delle stringhe: «Ciao » e i pezzi passati a mano.
static void test_expand_and_filters() {
  fresh(1);
  scopy(gM.sessions[0].name, "àbc-ÈFG");
  uint8_t b[64] = {1, 0, 10, 0};
  b[10] = 5;
  memcpy(b + 11, "Ciao ", 5);
  const uint8_t segs[] = {0, 0, 0, 1 | (1 << 2), 0x00, 0x00, 1 | (2 << 2), 0x00, 0x00};
  char out[256];
  expand(b, 16, segs, 3, ctx(), nullptr, false, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("Ciao ÀBC-ÈFGàbc-èfg", out);
  expand(b, 16, segs, 1, ctx(), nullptr, true, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("CIAO ", out);
}

// Più di 255 byte: troncato su un confine UTF-8, mai fuori dal buffer.
static void test_expand_truncates_utf8() {
  fresh(1);
  char big[kSummaryLen];
  memset(big, 0, sizeof big);
  for (int i = 0; i + 2 < kSummaryLen - 1; i += 2) memcpy(big + i, "\xc3\xa8", 2);  // «è» ripetuta
  scopy(gM.sessions[0].summary, big);
  uint8_t b[16] = {1, 0, 10, 0};
  const uint8_t segs[] = {1, 0x03, 0x00, 1, 0x03, 0x00, 1, 0x03, 0x00};
  char out[256];
  out[255] = 0x55;
  expand(b, 10, segs, 3, ctx(), nullptr, false, out, 255);
  const size_t n = strlen(out);
  TEST_ASSERT_TRUE(n <= 254);
  TEST_ASSERT_EQUAL(0, int(n % 2));                     // nessuna «è» spezzata
  TEST_ASSERT_EQUAL_HEX8(0x55, uint8_t(out[255]));      // il byte dopo cap non si tocca
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_session_values);
  RUN_TEST(test_general_values);
  RUN_TEST(test_refs_and_variants);
  RUN_TEST(test_conditions);
  RUN_TEST(test_expand_and_filters);
  RUN_TEST(test_expand_truncates_utf8);
  return UNITY_END();
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd firmware && pio test -e native -f test_theme_values`
Expected: FAIL (manca `core/theme_values.h`).

- [ ] **Step 3: Scrivere `core/theme_values.h`** con le dichiarazioni di «Produces» (include `core/model.h`, `core/clock.h`, `core/theme_format.h`; commento di testa «Temi: valori, condizioni, sessione di riferimento (docs/TEMI.md §3–§4). Puro.»).

- [ ] **Step 4: Scrivere `core/theme_values.cpp`**

```cpp
#include "core/theme_values.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/pet.h"
#include "core/strbuf.h"
#include "core/workday.h"

namespace cal {
namespace theme {

namespace {

void sessionValue(int k, const Session &s, Ms now, char *out, size_t cap) {
  Sb b(out, cap);
  switch (k) {
    case 0: b.s(s.name); break;
    case 1: b.s(stateText(s)); break;
    case 2: b.s(lineFor(s)); break;
    case 3: b.s(s.summary); break;
    case 4: metaLine(s, now, out, cap); break;
    case 5: b.i(s.ctx).s("%"); break;
    case 6: if (s.total) b.i(s.done).s("/").i(s.total); break;
    case 7: ago(now, s.since, out, cap); break;
    case 8: if (s.subs) b.i(s.subs); break;
    case 9: if (s.test != TestResult::None) b.s(s.test == TestResult::Ok ? "\xe2\x9c\x93" : "\xe2\x9c\x97"); break;
    default: break;
  }
}

// Lunghezza di s che sta in cap - 1 byte senza spezzare un carattere UTF-8.
size_t utf8Fit(const char *s, size_t len, size_t room) {
  if (len <= room) return len;
  size_t n = room;
  while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) n--;
  return n;
}

void append(char *out, size_t cap, size_t &n, const char *s) {
  if (!cap) return;
  const size_t add = utf8Fit(s, strlen(s), cap - 1 - n);
  memcpy(out + n, s, add);
  n += add;
  out[n] = 0;
}

void caseMap(char *out, size_t cap, const char *s, bool up) {
  size_t n = 0;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
  while (*p && n + 1 < cap) {
    unsigned char ch = *p++;
    if (up && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
    else if (!up && ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
    else if (ch == 0xC3 && *p) {
      if (n + 2 >= cap) break;
      unsigned char c2 = *p++;
      if (up && c2 >= 0xA0 && c2 <= 0xBE && c2 != 0xB7) c2 -= 0x20;
      else if (!up && c2 >= 0x80 && c2 <= 0x9E && c2 != 0x97) c2 += 0x20;
      out[n++] = char(ch);
      ch = c2;
    }
    out[n++] = char(ch);
  }
  if (cap) out[n] = 0;
}

}  // namespace

void upperUtf8(char *out, size_t cap, const char *s) { caseMap(out, cap, s, true); }
void lowerUtf8(char *out, size_t cap, const char *s) { caseMap(out, cap, s, false); }

int permWaiting(const Model &m) {
  int n = 0;
  for (int k = 0; k < m.nSessions; k++) n += m.sessions[k].state == SessionState::Permission;
  return n;
}

bool whenTrue(const Model &m, When w) {
  switch (w) {
    case When::None: return m.nSessions == 0;
    case When::One: return m.nSessions == 1;
    case When::Many: return m.nSessions >= 2;
    default: return true;
  }
}

int refIndex(const Model &m, Ms now, Screen s, Ref r) {
  if (m.nSessions <= 0) return -1;
  int k = r == Ref::Urgent ? urgentIdx(m) : s == Screen::Detail ? m.ui.sel : sIdx(m, now);
  if (k < 0) k = 0;
  if (k >= m.nSessions) k = m.nSessions - 1;
  return k;
}

const Session *refSession(const Ctx &cx, Ref r, const Session *row) {
  switch (r) {
    case Ref::Row: return row;
    case Ref::Nobody: return nullptr;
    case Ref::Variant: if (row) return row; break;
    case Ref::Urgent: { const int k = refIndex(*cx.m, cx.now, cx.screen, Ref::Urgent); return k < 0 ? nullptr : &cx.m->sessions[k]; }
    case Ref::Selected: { const int k = refIndex(*cx.m, cx.now, cx.screen, Ref::Selected); return k < 0 ? nullptr : &cx.m->sessions[k]; }
  }
  return cx.ref < 0 ? nullptr : &cx.m->sessions[cx.ref];
}

void valueText(uint16_t id, const Ctx &cx, const Session *row, char *out, size_t cap) {
  if (!cap) return;
  out[0] = 0;
  const Model &m = *cx.m;
  if (id < kValClock) {
    const Session *s = id >= kValRow ? row : (cx.ref >= 0 ? &m.sessions[cx.ref] : nullptr);
    if (s) sessionValue(id & 0x3F, *s, cx.now, out, cap);
    return;
  }
  Sb b(out, cap);
  const Civil c = civilFrom(cx.wc.localMs);
  char t[64];
  switch (id) {
    case 0x80: if (m.sys.wallOk) hmLocal(cx.wc.localMs, out, cap); else b.s("--:--"); break;
    case 0x81:
      if (m.sys.wallOk) b.pad2(c.hour).ch(':').pad2(c.minute).ch(':').pad2(c.second);
      else b.s("--:--:--");
      break;
    case 0x82: if (m.sys.wallOk) dateStr(cx.wc, out, cap); break;
    case 0x83: if (m.sys.wallOk) b.s(kWeekdays[c.weekday]); break;
    case 0x84: b.i(m.nSessions); break;
    case 0x85: b.i(permWaiting(m)); break;
    case 0x86: b.i(jsRound(m.sys.batt)).s("%"); break;
    case 0x87: autonomyText(m, out, cap); break;
    case 0x88: if (m.sys.wifi) b.s(m.sys.ssid); break;
    case 0x89: b.s(pcLabel(m)); break;
    case 0x8A: workLine(m, cx.wc, out, cap); break;
    case 0x8B:
      if (m.work.on || workDone(m, cx.wc)) hmLocal(m.work.endLocal, out, cap);
      else minHm(m.set.workEnd, out, cap);
      break;
    case 0x8C: b.i(m.stats.tasks); break;
    case 0x8D: b.s("+").i(m.stats.linesAdd).s(" \xe2\x88\x92").i(m.stats.linesDel); break;
    case 0x8E:
      snprintf(t, sizeof t, "%.2f $", m.stats.cost);  // come oggi(): toFixed(2) con la virgola
      for (char *p = t; *p; p++)
        if (*p == '.') *p = ',';
      b.s(t);
      break;
    case 0x8F: if (m.limits.hasH5) b.i(jsRound(m.limits.h5)).s("%"); else b.s("\xe2\x80\x94"); break;
    case 0x90: if (m.limits.hasD7) b.i(jsRound(m.limits.d7)).s("%"); else b.s("\xe2\x80\x94"); break;
    case 0x91: if (m.pet.known) b.i(m.pet.level); else b.s("\xe2\x80\x94"); break;
    case 0x92: if (m.pet.known) b.i(m.pet.xp - m.pet.xpLevelStart).s("/").i(m.pet.xpNext - m.pet.xpLevelStart).s(" XP"); break;
    case 0x93: if (m.pet.known) b.i(m.pet.energy).s("%"); break;
    case 0x94: if (m.acc) b.i(jsRound(m.air.co2)).s(" ppm"); break;
    case 0x95: b.s(kFwVersion); break;
    default: break;
  }
}

bool condTrue(uint8_t op, uint8_t arg, const Ctx &cx, const Session *subject) {
  const Model &m = *cx.m;
  const bool want = arg != 0;
  switch (CondOp(op)) {
    case CondOp::Perm: return (permWaiting(m) > 0) == want;
    case CondOp::BattBelow: return m.sys.batt < arg;
    case CondOp::Charging: return m.sys.usb == want;
    case CondOp::Night: return m.set.night == want;
    case CondOp::Pc: return m.sys.pc == want;
    case CondOp::Workday: return m.work.on == want;
    case CondOp::Game: return petOn(m) == want;
    case CondOp::SessionsMin: return m.nSessions >= arg;
    case CondOp::SessionsMax: return m.nSessions <= arg;
    case CondOp::State: return subject && uint8_t(subject->state) == arg;
    case CondOp::Blink: return (((cx.frame % 2) + 2) % 2) == arg;
    default: return false;
  }
}

bool condsTrue(const uint8_t *conds, int n, const Ctx &cx, const Session *subject) {
  for (int i = 0; i < n; i++)
    if (!condTrue(conds[2 * i], conds[2 * i + 1], cx, subject)) return false;
  return true;
}

void expand(const uint8_t *b, size_t n, const uint8_t *segs, int nSeg, const Ctx &cx, const Session *row, bool upperAll,
            char *out, size_t cap) {
  if (!cap) return;
  static char piece[256], mapped[256];  // fuori dallo stack del loop
  out[0] = 0;
  size_t len = 0;
  for (int i = 0; i < nSeg; i++) {
    const uint8_t *p = segs + 3 * i;
    const uint16_t arg = uint16_t(p[1] | (p[2] << 8));
    if ((p[0] & 3) == 0) {
      const char *s;
      uint8_t l;
      if (!stringAt(b, n, arg, s, l)) continue;
      memcpy(piece, s, l);
      piece[l] = 0;
    } else {
      valueText(arg, cx, row, piece, sizeof piece);
    }
    const int filter = (p[0] >> 2) & 3;
    if (filter == 1) { upperUtf8(mapped, sizeof mapped, piece); append(out, cap, len, mapped); }
    else if (filter == 2) { lowerUtf8(mapped, sizeof mapped, piece); append(out, cap, len, mapped); }
    else append(out, cap, len, piece);
  }
  if (upperAll) {
    upperUtf8(mapped, sizeof mapped, out);
    scopy(out, cap, mapped);
  }
}

}  // namespace theme
}  // namespace cal
```

Verificare `minHm` (dichiarata in `core/workday.h`), `dateStr`/`hmLocal`/`civilFrom`/`kWeekdays` (`core/clock.h`), `autonomyText`, `petOn` (`core/pet.h`): nomi e firme sono quelli del repository di oggi.

- [ ] **Step 5: Eseguire i test**

Run: `cd firmware && pio test -e native -f test_theme_values`
Expected: PASS (6 test). Se `test_general_values` fallisce su «10:30» controllare che `Ctx.wc` venga da `wallFromCivil` (ora locale), non da `kNow`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/core/theme_values.h firmware/src/core/theme_values.cpp firmware/test/test_theme_values
git commit -m "Temi: valori nei testi, condizioni e sessione di riferimento sulla scheda"
```

---
### Task 5: Firmware — interprete di disegno (`ui/theme_draw`)

**Files:**
- Create: `firmware/src/ui/kit.h`, `firmware/src/ui/theme_draw.h`, `firmware/src/ui/theme_draw.cpp`
- Modify: `firmware/src/ui/screens.cpp` (in fondo, fuori dal namespace anonimo: le funzioni di `kit`)
- Modify: `docs/TEMI.md` (aggiungere §6 «Come si disegna»)
- Test: `firmware/test/test_theme_draw/test_main.cpp`

**Interfaces:**
- Consumes: Task 3 (`readElem`, `variantAt`, `Elem`, `Variant`…), Task 4 (`Ctx`, `whenTrue`, `refIndex`, `refSession`, `condsTrue`, `expand`), `gfx/prims.h`, `gfx/qr.h`, `gfx/text.h`, `cal::pose`, `cal::rssiBars`, `cal::workFrac`, `cal::stateInfo`.
- Produces:
  - `ui/kit.h` (namespace `ui::kit`): `void text(Surface &c, const char *s, double x, double y, int size, int weight, bool pix, Align a, bool ink, int max)` (= `T()` di `screens.cpp`, quindi passa dal `TextHook` dei test); `void mascot(Surface &c, const cal::Model &m, const cal::Session *s, int ox, int oy, int sc, cal::MascotPose p, int f, bool accessory, bool ink)` (= `avatarS()` + `accOverlay()`, con i colori scambiati se `!ink`);
  - `ui/theme_draw.h` (namespace `ui`): `struct ThemeDrawInfo { bool drawn; cal::Ms nextChange; bool hasMascot; int mx, my, mw, mh; }`; `bool drawThemeBlock(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m, int f, cal::Ms now, const cal::WallClock &wc, cal::theme::Screen s, const uint8_t *block, uint16_t len, ThemeDrawInfo *info)` — false = nessuna variante vera o blocco non valido (si disegna la schermata di serie); `nextChange` = 0 se non serve un ridisegno a tempo;
  - `struct SwapSurface : Surface` (inverte il colore di ogni `fillRect`) e `struct NullSurface : Surface` (non disegna) in `ui/theme_draw.h`.

- [ ] **Step 1: Aggiungere a `docs/TEMI.md` il §6 «Come si disegna»** (le stesse regole valgono per C++ e JS):

```markdown
## 6. Come si disegna (interprete C++ e renderer JS)

- Variante: la prima con `when` vero; sessione di riferimento: `urgent` = `urgentIdx`, `selected` = `sIdx` (nel Dettaglio
  `ui.sel`), limitata a 0..n−1; senza sessioni nessuna. Elementi nell'ordine; `visibleIf` tutte vere (soggetto di
  `state`: la riga, altrimenti la sessione della variante).
- `invert`: il testo si disegna color carta (varianti chiaro su scuro dei caratteri); gli altri elementi passano da una
  superficie che scambia inchiostro e carta in ogni `fillRect`.
- `text`: testo espanso (§3), `upper` dopo i filtri. Àncora x: `left` = x, `center` = x + w/2, `right` = x + w (come
  `T()`, arrotondata con `Math.round`). Una riga: `T()` con `max = w`. Più righe: `wrapLines(…, w)` e ogni riga con
  `T()` a y + i·interlinea, `max = w`. Testo vuoto: niente.
- `mascot`: sessione dal `ref`; posa `auto` = `pose(m, s)`, senza sessione `idle`; fotogramma = f se `animated`,
  altrimenti 0; avatar = quello della sessione (o il predefinito); accessorio come `accOverlay` se `accessory`.
- `battery`: `battIcon(x, y, s, batt)`; con `percent` «78%» in Silkscreen 400 8 a (x + 16·s + 3, y + (8·s − 8)/2).
- `wifi`: livello `rssiBars(rssi)`, −1 senza rete. `stateIcon`: `iconForState(chiave dello stato, x, y, s, f)`, niente
  senza sessione. `sym`: `sym(g, x, y, s)`.
- `bar`: percentuale context = ctx della sessione, tasks = done/total·100, workday = `workFrac`·100 (0 se spenta),
  battery, limit5h/7d (0 senza dati). `big` = `bigBar(x, y, w, h, pct)`; `bar` = `bar(x, y, w, h, …, f)` con la
  sessione vera per `tasks`, altrimenti `{done: round(pct), total: 100, working: false}`.
- `qr`: testo espanso → `qr::encode` (ECC M, versioni 1–5; troppo lungo = niente). Modulo `px = max(1,
  floor(min(w, h) / (size + 2)))`: quadrato di carta `(size + 2)·px` in (x, y), moduli neri da (x + px, y + px).
- `line`: `solid` = `fillRect(x, y, w, h)`; `dotted` orizzontale = `dotsH(x, x + w, y + r)` per ogni r < h, verticale =
  `dotsV(x + k, y, y + h)` per ogni k < w.
- `rect`: pixel (px, py) del riquadro «dentro» se non è in un angolo tagliato: con dx = distanza dal bordo sinistro o
  destro più vicino, dy dal bordo alto o basso, tagliato se dx < r e dy < r e dx + dy < r. Bordo t: dentro il riquadro
  e non dentro quello rientrato di t (raggio max(0, r − t)). `fill` riempie il «dentro» (prima), poi il bordo.
- `circle`: d = min(w, h); dentro se (2px + 1 − 2x − d)² + (2py + 1 − 2y − d)² ≤ d², per px, py nel quadrato d × d in
  (x, y). Bordo t: dentro d e non dentro d − 2t (stesso centro). `fill` e bordo come `rect`.
- `list`: righe per pagina `k = max(1, floor((h + gap) / (rowH + gap)))`, pagine `ceil(n / k)`, pagina =
  `floor(now / (pageSec·1000)) mod pagine`; riga i in (x, y + i·(rowH + gap)), elementi della riga spostati lì, con
  `riga.*` e `ref` `row` = la sessione. Pallini (con più di una pagina): per ogni pagina p uno slot in
  (x + w − 4·(pagine − p), y + h − 3): la corrente piena 3 × 3, le altre un punto in (+1, +1). `nextChange` = inizio
  della pagina dopo.
- Orologio `hms`: `nextChange` = prossimo secondo. Riquadro della mascotte (per il Riposo in sonno profondo): unione
  dei riquadri delle mascotte disegnate.
```

- [ ] **Step 2: Scrivere il test che fallisce**

`firmware/test/test_theme_draw/test_main.cpp` — i blocchi si costruiscono con lo stesso aiuto di `test_theme_format` (`one()`, `el()`; copiarli in testa al file), il canvas è `Canvas<250, 122>`:

```cpp
// Temi: interprete di disegno (docs/TEMI.md §6). La parità pixel per pixel col JS sta in test_theme_parity.
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "core/avatars.h"
#include "core/model.h"
#include "core/strbuf.h"
#include "core/theme_format.h"
#include "gfx/canvas.h"
#include "gfx/prims.h"
#include "ui/screens.h"
#include "ui/theme_draw.h"

using namespace cal;
using theme::Screen;

void setUp() {}
void tearDown() {}

typedef Canvas<250, 122> Cv;
static Model gM;
static const Ms kNow = 1791707400000LL;
static const WallClock kWc = wallFromCivil(2026, 10, 11, 10, 30, 0);

// (one() e el() come in test_theme_format)

static Model &fresh(int n) {
  newModel(gM, kNow, kWc);
  gM.nSessions = n;
  for (int k = 0; k < n; k++) {
    Session &s = gM.sessions[k];
    memset(&s, 0, sizeof s);
    Sb(s.name, sizeof s.name).s("s").i(k);
    s.state = SessionState::Working;
    s.total = 4;
    s.done = 1;
  }
  gM.sys.wallOk = true;
  gM.ui.selUntil = 0;
  return gM;
}

static bool draw(Cv &cv, const std::vector<uint8_t> &b, ui::ThemeDrawInfo *info = nullptr, Ms now = kNow, int f = 0) {
  cv.clear(false);
  CanvasSurface<250, 122> s(cv);
  ui::ThemeDrawInfo tmp;
  return ui::drawThemeBlock(s, kHeltec213, gM, f, now, kWc, Screen::Home, b.data(), uint16_t(b.size()), info ? info : &tmp);
}

static int inkIn(const Cv &cv, int x, int y, int w, int h) {
  int n = 0;
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) n += cv.get(xx, yy);
  return n;
}

static void test_rect_fill_and_radius() {
  fresh(0);
  Cv cv;
  std::vector<uint8_t> e = el(10, {0, 1, 3});  // pieno, senza bordo, raggio 3 (riquadro 0,0,10,10)
  TEST_ASSERT_TRUE(draw(cv, one(e)));
  TEST_ASSERT_FALSE(cv.get(0, 0));
  TEST_ASSERT_FALSE(cv.get(1, 1));   // dx + dy = 2 < 3
  TEST_ASSERT_TRUE(cv.get(2, 1));    // dx + dy = 3: dentro
  TEST_ASSERT_TRUE(cv.get(5, 5));
  TEST_ASSERT_FALSE(cv.get(9, 9));
  TEST_ASSERT_EQUAL(100 - 4 * 6, inkIn(cv, 0, 0, 10, 10));  // 6 pixel tagliati per angolo
  // bordo 1 senza raggio: 36 pixel, interno vuoto
  TEST_ASSERT_TRUE(draw(cv, one(el(10, {1, 0, 0}))));
  TEST_ASSERT_EQUAL(36, inkIn(cv, 0, 0, 10, 10));
}

static void test_circle() {
  fresh(0);
  Cv cv;
  TEST_ASSERT_TRUE(draw(cv, one(el(11, {0, 1}))));  // pieno, d = 10
  TEST_ASSERT_TRUE(cv.get(5, 5));
  TEST_ASSERT_FALSE(cv.get(0, 0));
  TEST_ASSERT_TRUE(cv.get(0, 5) || cv.get(0, 4));
  const int full = inkIn(cv, 0, 0, 10, 10);
  TEST_ASSERT_TRUE(full > 70 && full < 90);  // ≈ π·25
  TEST_ASSERT_TRUE(draw(cv, one(el(11, {2, 0}))));  // anello di 2
  TEST_ASSERT_FALSE(cv.get(5, 5));
  TEST_ASSERT_TRUE(inkIn(cv, 0, 0, 10, 10) < full);
}

static void test_dotted_line_and_invert() {
  fresh(0);
  Cv cv;
  std::vector<uint8_t> e = el(9, {0, 1});  // orizzontale a puntini, 10 × 10
  TEST_ASSERT_TRUE(draw(cv, one(e)));
  TEST_ASSERT_TRUE(cv.get(0, 0));
  TEST_ASSERT_FALSE(cv.get(1, 0));
  TEST_ASSERT_TRUE(cv.get(3, 0));    // dotsH: un punto ogni 3
  TEST_ASSERT_TRUE(cv.get(0, 9));    // ogni riga di h
  // invert su un riquadro pieno disegnato prima: i punti diventano carta
  std::vector<uint8_t> two = {1, 0, 0, 0, 0, 1, 10, 0, 2, 0};
  std::vector<uint8_t> r = el(10, {0, 1, 0}), l = el(9, {0, 1}, 1);
  two.insert(two.end(), r.begin(), r.end());
  two.insert(two.end(), l.begin(), l.end());
  two[2] = uint8_t(two.size());
  TEST_ASSERT_TRUE(draw(cv, two));
  TEST_ASSERT_FALSE(cv.get(0, 0));
  TEST_ASSERT_TRUE(cv.get(1, 0));
}

static void test_offscreen_elements_clip() {
  fresh(1);
  Cv cv;
  std::vector<uint8_t> e = el(10, {3, 1, 8});
  e[2] = 0x00; e[3] = 0xFF;  // x = −256
  e[6] = 0x00; e[7] = 0x02;  // w = 512: fino a x = 256, oltre il bordo destro
  e[8] = 0x00; e[9] = 0x02;  // h = 512
  TEST_ASSERT_TRUE(draw(cv, one(e)));
  TEST_ASSERT_TRUE(cv.get(0, 0));          // il riempimento copre lo schermo
  std::vector<uint8_t> m = el(1, {4, 0xFF, 4, 3});
  m[2] = 0x00; m[3] = 0xFE;                // mascotte a x = −512: niente, senza crash
  TEST_ASSERT_TRUE(draw(cv, one(m)));
  TEST_ASSERT_EQUAL(0, cv.countInk());
}

static void test_variant_choice_and_conditions() {
  fresh(0);
  Cv cv;
  // una sola variante «one»: con 0 sessioni non c'è niente da disegnare → schermata di serie
  std::vector<uint8_t> b = one(el(10, {0, 1, 0}));
  b[4] = 2;
  TEST_ASSERT_FALSE(draw(cv, b));
  fresh(1);
  TEST_ASSERT_TRUE(draw(cv, b));
  // condizione falsa: variante scelta, elemento non disegnato
  std::vector<uint8_t> c = one(el(10, {0, 1, 0}, 0, {5, 0}));  // pc: false, ma il PC c'è
  gM.sys.pc = true;
  TEST_ASSERT_TRUE(draw(cv, c));
  TEST_ASSERT_EQUAL(0, cv.countInk());
  // blocco rovinato: mai a metà
  c.resize(c.size() - 1);
  TEST_ASSERT_FALSE(draw(cv, c));
}

static void test_list_paging() {
  fresh(6);
  Cv cv;
  // lista 0,0,10,10 → righe alte 2 senza spazio: 5 righe per pagina, 2 pagine; riga = riquadro pieno 3×1
  std::vector<uint8_t> list = el(12, {2, 0, 5, 0, 1, 0});  // senza pallini: non sporcano il conto
  std::vector<uint8_t> row = el(10, {0, 1, 0});
  row[6] = 3; row[8] = 1;
  list.insert(list.end(), row.begin(), row.end());
  ui::ThemeDrawInfo info;
  TEST_ASSERT_TRUE(draw(cv, one(list), &info, 0));
  TEST_ASSERT_EQUAL(5, inkIn(cv, 0, 0, 3, 10) / 3);         // pagina 0: 5 righe
  TEST_ASSERT_EQUAL(5000, int(info.nextChange));
  TEST_ASSERT_TRUE(draw(cv, one(list), &info, 5000));
  TEST_ASSERT_EQUAL(1, inkIn(cv, 0, 0, 3, 9) / 3);          // pagina 1: la sesta
  TEST_ASSERT_EQUAL(10000, int(info.nextChange));
  fresh(0);
  TEST_ASSERT_TRUE(draw(cv, one(list), &info, 0));
  TEST_ASSERT_EQUAL(0, cv.countInk());
  TEST_ASSERT_EQUAL(0, int(info.nextChange));
}

static std::vector<std::string> gTexts;
static void hook(const char *s, int, int, int, int, bool, Align, void *) { gTexts.push_back(s); }

static void test_text_goes_through_T() {
  fresh(1);
  Cv cv;
  // testo «{sessione.nome}» in 12/600 maiuscolo, largo 100
  std::vector<uint8_t> b = one(el(2, {12, 6, 0, 1, 14, 1, 1, 1, 0x00, 0x00}));
  ui::setTextHook(hook, nullptr);
  gTexts.clear();
  TEST_ASSERT_TRUE(draw(cv, b));
  ui::setTextHook(nullptr, nullptr);
  TEST_ASSERT_EQUAL(1, int(gTexts.size()));
  TEST_ASSERT_EQUAL_STRING("S0", gTexts[0].c_str());
}

static void test_mascot_rect_and_clock_wake() {
  fresh(1);
  Cv cv;
  ui::ThemeDrawInfo info;
  std::vector<uint8_t> m = el(1, {2, 0xFF, 4, 3});
  m[2] = 10; m[4] = 20;
  TEST_ASSERT_TRUE(draw(cv, one(m), &info));
  TEST_ASSERT_TRUE(info.hasMascot);
  TEST_ASSERT_EQUAL(10, info.mx);
  TEST_ASSERT_EQUAL(20, info.my);
  TEST_ASSERT_EQUAL(56, info.mw);
  TEST_ASSERT_EQUAL(50, info.mh);
  TEST_ASSERT_TRUE(inkIn(cv, 10, 20, 56, 50) > 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_rect_fill_and_radius);
  RUN_TEST(test_circle);
  RUN_TEST(test_dotted_line_and_invert);
  RUN_TEST(test_offscreen_elements_clip);
  RUN_TEST(test_variant_choice_and_conditions);
  RUN_TEST(test_list_paging);
  RUN_TEST(test_text_goes_through_T);
  RUN_TEST(test_mascot_rect_and_clock_wake);
  return UNITY_END();
}
```

Note: in `el()` la x è in `e[2..3]`, y in `e[4..5]`, w in `e[6..7]`, h in `e[8..9]` (little-endian). Nel testo il peso 600 è `6` (Chakra Petch), `opts` 1 = maiuscolo.

- [ ] **Step 3: Eseguire e vedere fallire**

Run: `cd firmware && pio test -e native -f test_theme_draw`
Expected: FAIL (manca `ui/theme_draw.h`).

- [ ] **Step 4: `ui/kit.h` e le funzioni in fondo a `screens.cpp`**

`firmware/src/ui/kit.h`:

```cpp
// Accesso dell'interprete dei temi (ui/theme_draw) alle funzioni di disegno di screens.cpp, che restano private lì.
#pragma once
#include "core/model.h"
#include "gfx/canvas.h"
#include "gfx/text.h"

namespace ui {
namespace kit {

// T() delle schermate di serie: arrotonda x e y, accorcia con fitText se max > 0, passa dal TextHook dei test.
void text(Surface &c, const char *s, double x, double y, int size, int weight, bool pix, Align a, bool ink, int max);
// avatarS() + accOverlay(): con ink falso inchiostro e carta si scambiano (anche nei punti dell'accessorio).
void mascot(Surface &c, const cal::Model &m, const cal::Session *s, int ox, int oy, int sc, cal::MascotPose p, int f,
            bool accessory, bool ink);

}  // namespace kit
}  // namespace ui
```

In fondo a `firmware/src/ui/screens.cpp`, prima della `}` di `namespace ui` (fuori dal namespace anonimo, così vedono `T`, `O`, `P` e i buffer di `accOverlay`):

```cpp
namespace kit {

void text(Surface &c, const char *s, double x, double y, int size, int weight, bool pix, Align a, bool ink, int max) {
  O o = {weight, pix, a, ink, max};
  T(c, s, x, y, size, o);
}

void mascot(Surface &c, const Model &m, const Session *s, int ox, int oy, int sc, cal::MascotPose st, int f, bool accessory,
            bool ink) {
  const Avatar &a = s ? cal::sessionAvatar(m, *s) : cal::defaultAvatar(m);
  drawAvatar(c, a, ox, oy, sc, P(st), f, ink);
  if (!accessory || !cal::wornAccessory(m)) return;
  static uint8_t g0[kAvatarCells], g[kAvatarCells];
  if (!avatarGrid(a, P(st), f, g0) || !cal::figureGrid(m, a, P(st), f, g)) return;
  for (int k = 0; k < kAvatarCells; k++)
    if (g[k] != g0[k]) c.fillRect(ox + (k % kAvatarW) * sc, oy + (k / kAvatarW) * sc, sc, sc, (g[k] == 1) == ink);
}

}  // namespace kit
```

(include `ui/kit.h` in testa a `screens.cpp`.)

- [ ] **Step 5: `ui/theme_draw.h`**

```cpp
// Interprete dei temi (docs/TEMI.md §6): disegna il blocco di una schermata con le funzioni di oggi. Nessuna allocazione.
#pragma once
#include <stdint.h>

#include "core/model.h"
#include "core/theme_format.h"
#include "gfx/canvas.h"

namespace ui {

struct ThemeDrawInfo {
  bool drawn;
  cal::Ms nextChange;  // 0 = nessun ridisegno a tempo (orologio coi secondi, pagine della lista)
  bool hasMascot;
  int mx, my, mw, mh;  // unione dei riquadri delle mascotte disegnate
};

// Scambia inchiostro e carta (elementi con invert, tranne il testo).
struct SwapSurface : Surface {
  explicit SwapSurface(Surface &s) : s(s) {}
  void fillRect(int x, int y, int w, int h, bool ink) override { s.fillRect(x, y, w, h, !ink); }
  Surface &s;
};
struct NullSurface : Surface {
  void fillRect(int, int, int, int, bool) override {}
};

bool drawThemeBlock(Surface &c, const cal::DeviceProfile &dev, const cal::Model &m, int f, cal::Ms now, const cal::WallClock &wc,
                    cal::theme::Screen s, const uint8_t *block, uint16_t len, ThemeDrawInfo *info);

}  // namespace ui
```

- [ ] **Step 6: `ui/theme_draw.cpp`**

```cpp
#include "ui/theme_draw.h"

#include <math.h>
#include <string.h>

#include "core/clock.h"
#include "core/theme_values.h"
#include "core/workday.h"
#include "gfx/prims.h"
#include "gfx/qr.h"
#include "gfx/text.h"
#include "ui/kit.h"

namespace ui {

using namespace cal::theme;
using cal::Model;
using cal::Ms;
using cal::Session;

namespace {

int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

// dentro un riquadro con gli angoli tagliati di raggio r (docs/TEMI.md §6)
bool inRound(int px, int py, int x, int y, int w, int h, int r) {
  if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h) return false;
  const int dx = px - x < x + w - 1 - px ? px - x : x + w - 1 - px;
  const int dy = py - y < y + h - 1 - py ? py - y : y + h - 1 - py;
  return !(dx < r && dy < r && dx + dy < r);
}

void rect(Surface &c, int x, int y, int w, int h, int t, bool fill, int r, bool ink) {
  const int x0 = clampi(x, 0, 250), x1 = clampi(x + w, 0, 250), y0 = clampi(y, 0, 122), y1 = clampi(y + h, 0, 122);
  for (int py = y0; py < y1; py++)
    for (int px = x0; px < x1; px++) {
      if (!inRound(px, py, x, y, w, h, r)) continue;
      const bool border = t > 0 && !inRound(px, py, x + t, y + t, w - 2 * t, h - 2 * t, r - t > 0 ? r - t : 0);
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}

bool inCircle(int px, int py, int x2c, int y2c, int d) {
  const long dx = 2L * px + 1 - x2c, dy = 2L * py + 1 - y2c;
  return d > 0 && dx * dx + dy * dy <= long(d) * d;
}

void circle(Surface &c, int x, int y, int w, int h, int t, bool fill, bool ink) {
  const int d = w < h ? w : h, cx2 = 2 * x + d, cy2 = 2 * y + d;
  for (int py = clampi(y, 0, 122); py < clampi(y + d, 0, 122); py++)
    for (int px = clampi(x, 0, 250); px < clampi(x + d, 0, 250); px++) {
      if (!inCircle(px, py, cx2, cy2, d)) continue;
      const bool border = t > 0 && !inCircle(px, py, cx2, cy2, d - 2 * t);
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}

double barPct(const Ctx &cx, uint8_t src, const Session *s) {
  const Model &m = *cx.m;
  switch (src) {
    case 0: return s ? s->ctx : 0;
    case 1: return s && s->total ? 100.0 * s->done / s->total : 0;
    case 2: return m.work.on ? cal::workFrac(m, cx.wc) * 100 : 0;
    case 3: return m.sys.batt;
    case 4: return m.limits.hasH5 ? m.limits.h5 : 0;
    default: return m.limits.hasD7 ? m.limits.d7 : 0;
  }
}

struct Run {
  Surface *c;
  const Ctx *cx;
  const uint8_t *b;
  uint16_t n;
  int f;
  ThemeDrawInfo *info;
};

void wake(ThemeDrawInfo *info, Ms t) {
  if (t > 0 && (!info->nextChange || t < info->nextChange)) info->nextChange = t;
}

void mascotBox(ThemeDrawInfo *info, int x, int y, int w, int h) {
  if (!info->hasMascot) { info->hasMascot = true; info->mx = x; info->my = y; info->mw = w; info->mh = h; return; }
  const int x1 = x + w > info->mx + info->mw ? x + w : info->mx + info->mw, y1 = y + h > info->my + info->mh ? y + h : info->my + info->mh;
  if (x < info->mx) info->mx = x;
  if (y < info->my) info->my = y;
  info->mw = x1 - info->mx;
  info->mh = y1 - info->my;
}

// Un elemento con origine (ox, oy) (la riga di una lista, altrimenti 0, 0). row: sessione della riga o nullptr.
void drawElem(Run &R, const Elem &e, int ox, int oy, const Session *row) {
  const Ctx &cx = *R.cx;
  const Model &m = *cx.m;
  const Session *subject = row ? row : (cx.ref >= 0 ? &m.sessions[cx.ref] : nullptr);
  if (!condsTrue(e.conds, e.nConds, cx, subject)) return;
  const int x = e.x + ox, y = e.y + oy;
  const uint8_t *p = e.payload;
  const bool ink = !e.invert;
  SwapSurface swap(*R.c);
  Surface &c = e.invert ? static_cast<Surface &>(swap) : *R.c;
  static char t[256];
  switch (e.type) {
    case Type::Mascot: {
      const Session *s = refSession(cx, Ref(p[2]), row);
      const cal::MascotPose pose = p[1] == 0xFF ? (s ? cal::pose(m, *s) : cal::MascotPose::Idle) : cal::MascotPose(p[1]);
      kit::mascot(*R.c, m, s, x, y, p[0], pose, (p[3] & 2) ? R.f : 0, p[3] & 1, ink);
      mascotBox(R.info, x, y, 28 * p[0], 25 * p[0]);
      break;
    }
    case Type::Text: {
      expand(R.b, R.n, p + 7, p[6], cx, row, p[5] & 1, t, sizeof t);
      if (!t[0]) break;
      const int size = p[0], weight = (p[1] & 0x7F) * 100, lines = p[3], lh = p[4];
      const bool pix = p[1] & 0x80;
      const Align a = Align(p[2]);
      const double ax = a == Align::Center ? x + e.w / 2.0 : a == Align::Right ? double(x + e.w) : double(x);
      for (int i = 0; i < p[6]; i++)
        if ((p[7 + 3 * i] & 3) == 1 && (p[8 + 3 * i] | (p[9 + 3 * i] << 8)) == kValClockSec)
          wake(R.info, cx.now + (1000 - ((cx.wc.localMs % 1000) + 1000) % 1000));
      if (lines <= 1) { kit::text(*R.c, t, ax, y, size, weight, pix, a, ink, e.w); break; }
      static char ls[8][96];
      const int k = wrapLines(ls, lines, t, size, e.w, weight, pix);
      for (int i = 0; i < k; i++) kit::text(*R.c, ls[i], ax, y + i * lh, size, weight, pix, a, ink, e.w);
      break;
    }
    case Type::Battery: {
      battIcon(c, x, y, p[0], m.sys.batt, true);
      if (p[1]) {
        cal::Sb(t, sizeof t).i(cal::jsRound(m.sys.batt)).s("%");
        kit::text(*R.c, t, x + 16 * p[0] + 3, y + (8 * p[0] - 8) / 2, 8, 400, true, Align::Left, ink, 0);
      }
      break;
    }
    case Type::Wifi: wifiIcon(c, x, y, p[0], m.sys.wifi ? cal::rssiBars(m.sys.rssi) : -1, true); break;
    case Type::StateIcon: {
      const Session *s = refSession(cx, Ref(p[1]), row);
      if (s) iconForState(c, cal::stateInfo(s->state).key, x, y, p[0], R.f, true);
      break;
    }
    case Type::Bar: {
      const Session *s = row ? row : subject;
      const double pct = barPct(cx, p[0], s);
      if (!p[1]) { bigBar(c, x, y, e.w, e.h, pct); break; }
      BarState st;
      if (p[0] == 1 && s) { st.done = s->done; st.total = s->total; st.working = s->state == cal::SessionState::Working; }
      else { st.done = cal::jsRound(pct); st.total = 100; st.working = false; }
      bar(c, x, y, e.w, e.h, st, R.f);
      break;
    }
    case Type::Qr: {
      expand(R.b, R.n, p + 1, p[0], cx, row, false, t, sizeof t);
      static qr::Bits q;
      if (!t[0] || !qr::encode(t, q)) break;
      const int side = e.w < e.h ? e.w : e.h, px = side / (q.size + 2) > 1 ? side / (q.size + 2) : 1;
      c.fillRect(x, y, (q.size + 2) * px, (q.size + 2) * px, false);
      for (int r = 0; r < q.size; r++)
        for (int k = 0; k < q.size; k++)
          if (q.get(k, r)) c.fillRect(x + px + k * px, y + px + r * px, px, px, true);
      break;
    }
    case Type::Sym: sym(c, Sym(p[0]), x, y, p[1], true); break;
    case Type::Line:
      if (!p[1]) c.fillRect(x, y, e.w, e.h, true);
      else if (!p[0]) for (int r = 0; r < e.h; r++) dotsH(c, x, x + e.w, y + r);
      else for (int k = 0; k < e.w; k++) dotsV(c, x + k, y, y + e.h);
      break;
    case Type::Rect: rect(*R.c, x, y, e.w, e.h, p[0], p[1], p[2], ink); break;
    case Type::Circle: circle(*R.c, x, y, e.w, e.h, p[0], p[1], ink); break;
    default: break;
  }
}

// Lista: le righe seguono subito l'elemento lista.
void drawList(Run &R, const Elem &e) {
  const Ctx &cx = *R.cx;
  const Model &m = *cx.m;
  const uint8_t *p = e.payload;
  const int rowH = p[0], gap = p[1], nRow = p[4];
  const Ms period = Ms(p[2]) * 1000;
  int per = (e.h + gap) / (rowH + gap);
  if (per < 1) per = 1;
  const int n = m.nSessions, pages = (n + per - 1) / per;
  const bool visible = condsTrue(e.conds, e.nConds, cx, cx.ref >= 0 ? &m.sessions[cx.ref] : nullptr);
  const int page = pages > 1 ? int(cal::floorMod(cal::floorDiv(cx.now, period), pages)) : 0;
  if (visible && pages > 1) wake(R.info, (cal::floorDiv(cx.now, period) + 1) * period);
  for (int i = 0; visible && i < per; i++) {
    const int k = page * per + i;
    if (k >= n) break;
    const uint8_t *q = e.next;
    for (int j = 0; j < nRow; j++) {
      Elem r;
      if (!readElem(R.b, R.n, q, r)) return;
      drawElem(R, r, e.x, e.y + i * (rowH + gap), &m.sessions[k]);
      q = r.next;
    }
  }
  if (visible && (p[3] & 1) && pages > 1) {
    SwapSurface swap(*R.c);
    Surface &c = e.invert ? static_cast<Surface &>(swap) : *R.c;
    for (int pg = 0; pg < pages; pg++) {
      const int sx = e.x + e.w - 4 * (pages - pg), sy = e.y + e.h - 3;
      if (pg == page) c.fillRect(sx, sy, 3, 3, true);
      else c.fillRect(sx + 1, sy + 1, 1, 1, true);
    }
  }
}

// L'elemento dopo una lista con le sue righe.
const uint8_t *skip(const uint8_t *b, uint16_t n, const Elem &e) {
  if (e.type != Type::List) return e.next;
  const uint8_t *q = e.next;
  for (int j = 0; j < e.payload[4]; j++) {
    Elem r;
    if (!readElem(b, n, q, r)) return nullptr;
    q = r.next;
  }
  return q;
}

}  // namespace

bool drawThemeBlock(Surface &c, const cal::DeviceProfile &dev, const Model &m, int f, Ms now, const cal::WallClock &wc, Screen s,
                    const uint8_t *block, uint16_t len, ThemeDrawInfo *info) {
  (void)dev;
  memset(info, 0, sizeof *info);
  if (!block || checkBlock(block, len) != Error::Ok) return false;  // mai a metà
  Variant v;
  int k = 0;
  for (; k < variantCount(block); k++)
    if (variantAt(block, len, k, v) && whenTrue(m, v.when)) break;
  if (k >= variantCount(block)) return false;
  Ctx cx;
  cx.m = &m;
  cx.now = now;
  cx.wc = wc;
  cx.frame = f;
  cx.screen = s;
  cx.ref = refIndex(m, now, s, v.ref);
  Run R = {&c, &cx, block, len, f, info};
  const uint8_t *p = block + v.off;
  for (int i = 0; i < v.count && p; i++) {
    Elem e;
    if (!readElem(block, len, p, e)) break;
    if (e.type == Type::List) drawList(R, e);
    else drawElem(R, e, 0, 0, nullptr);
    p = skip(block, len, e);
  }
  info->drawn = true;
  return true;
}

}  // namespace ui
```

Note:
- `cal::floorDiv`/`floorMod` sono in `core/clock.h`; `BarState` in `gfx/prims.h`; `Sym(p[0])` usa l'ordine di `enum class Sym` (plug … gauge) che coincide col §2.
- Il controllo `checkBlock` a ogni disegno costa poco (≤ 4 096 byte); il `Runtime` del Task 6 lo fa già al caricamento, ma qui resta: il disegno non deve mai fidarsi dei byte.

- [ ] **Step 7: Eseguire i test**

Run: `cd firmware && pio test -e native -f test_theme_draw && pio test -e native`
Expected: PASS; tutti i test di prima ancora verdi (le funzioni nuove in fondo a `screens.cpp` non cambiano nessuna schermata di serie).

- [ ] **Step 8: Commit**

```bash
git add docs/TEMI.md firmware/src/ui/kit.h firmware/src/ui/theme_draw.h firmware/src/ui/theme_draw.cpp firmware/src/ui/screens.cpp firmware/test/test_theme_draw
git commit -m "Temi: interprete di disegno sulla scheda (elementi, varianti, condizioni, lista a pagine)"
```

---
### Task 6: Firmware — `Runtime` del tema attivo e aggancio alle schermate

**Files:**
- Create: `firmware/src/core/theme_runtime.h`, `firmware/src/core/theme_runtime.cpp`
- Modify: `firmware/src/ui/theme_draw.h`, `firmware/src/ui/theme_draw.cpp` (registro del runtime e ultimo disegno)
- Modify: `firmware/src/ui/screens.cpp` (`draw`, `drawScreen`, `sleepMascotRect`)
- Test: `firmware/test/test_theme_runtime/test_main.cpp`

**Interfaces:**
- Consumes: Task 3 (`parseHeader`, `parseIndex`, `checkBlock`, `crc32Update`, `Header`, `IndexEntry`), Task 5 (`drawThemeBlock`, `ThemeDrawInfo`, `NullSurface`).
- Produces (namespace `cal::theme`):
  - `class Storage` (interfaccia, usata anche dall'installatore del piano 1b): `virtual int list(char (*ids)[kIdLen], int cap)`, `virtual int32_t size(const char *id)` (−1 se manca), `virtual bool read(const char *id, uint32_t off, uint8_t *dst, uint32_t n)`, `virtual void remove(const char *id)`, `virtual bool tmpBegin()`, `virtual bool tmpAppend(const uint8_t *p, uint32_t n)`, `virtual int32_t tmpSize()`, `virtual bool tmpRead(uint32_t off, uint8_t *dst, uint32_t n)`, `virtual bool tmpCommit(const char *id)` (rinomina sopra `<id>`), `virtual void tmpDrop()`, `virtual int readOrder(char *dst, int cap)` (−1 se manca), `virtual bool writeOrder(const char *src, int n)`, `virtual void eraseAll()`;
  - `class MemStorage : public Storage` (in RAM, per i test; `uint8_t *raw(const char *id)` per rovinare i byte, `bool failWrites`);
  - `struct CatalogEntry { char id[kIdLen]; char name[kNameLen]; uint32_t crc; uint32_t size; }`, `int catalog(Storage &st, CatalogEntry *out, int cap)` (solo intestazioni valide, ordine di `list`);
  - `Error verifyStored(Storage &st, const char *id, uint8_t *scratch /* kMaxBlock */, Header *h)` (file intero: intestazione, lunghezza, CRC a pezzi da 256 byte, indice, ogni blocco);
  - `class Runtime`: `void begin(Storage *st)`, `bool activate(const char *id)` ('' = di serie; false = mancante o rovinato, resta di serie e il tema risulta rotto), `const char *active() const`, `uint32_t activeCrc() const`, `bool has(Screen s) const`, `bool block(Screen s, const uint8_t *&b, uint16_t &n)` (buffer statico da 4 096 byte, ricaricato al cambio di schermata), `void suspend(bool on)`, `bool suspended() const`, `bool takeBroken(char *id, size_t cap)`;
  - `class SafeBoot`: `static const Ms kHoldMs` (= 1500), `bool step(bool down, bool boot, Ms now)` (vero una volta: tasto giù da 1,5 s durante l'avvio), `bool swallow(bool downNow)` (vero per il gesto di quella pressione).
- Produces (namespace `ui`, in `theme_draw.h`): `void setThemeRuntime(cal::theme::Runtime *rt)`, `cal::theme::Runtime *themeRuntime()`, `const ThemeDrawInfo &lastThemeDraw()` (ultimo `draw()`: `drawn` falso se la schermata era di serie).

- [ ] **Step 1: Scrivere il test che fallisce**

`firmware/test/test_theme_runtime/test_main.cpp`:

```cpp
// Temi: archivio astratto, tema attivo (buffer da 4 096 byte), uscita di emergenza e aggancio a ui::draw.
// Regola §7 di ISTRUZIONI-SVILUPPO: Permessi e schermate di sistema sempre di serie, anche con un tema attivo.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/theme_runtime.h"
#include "gfx/canvas.h"
#include "ui/screens.h"
#include "ui/theme_draw.h"

using namespace cal;
using namespace cal::theme;

void setUp() {}
void tearDown() {}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  return k != std::string::npos ? f.substr(0, k) : "../";
}
static std::vector<uint8_t> load(const char *name) {
  std::ifstream in(repoRoot() + "protocol/themes/fixtures/" + name, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static MemStorage gSt;
static Runtime gRt;
static uint8_t gScratch[kMaxBlock];

static void install(const char *id, const std::vector<uint8_t> &f) {
  TEST_ASSERT_TRUE(gSt.tmpBegin());
  TEST_ASSERT_TRUE(gSt.tmpAppend(f.data(), uint32_t(f.size())));
  TEST_ASSERT_TRUE(gSt.tmpCommit(id));
}

static void reset() {
  gSt.eraseAll();
  install("prova", load("prova.cthm"));
  install("lista", load("lista.cthm"));
  gRt.begin(&gSt);
}

static void test_catalog_and_verify() {
  reset();
  CatalogEntry e[kMaxThemes];
  TEST_ASSERT_EQUAL(2, catalog(gSt, e, kMaxThemes));
  Header h;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h)));
  TEST_ASSERT_EQUAL_STRING("Mascotte grande", h.name);
  gSt.raw("lista")[200] ^= 0xFF;
  TEST_ASSERT_EQUAL(int(Error::Crc), int(verifyStored(gSt, "lista", gScratch, &h)));
  TEST_ASSERT_NOT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "manca", gScratch, &h)));
}

static void test_activate_and_block() {
  reset();
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  TEST_ASSERT_EQUAL_STRING("prova", gRt.active());
  TEST_ASSERT_TRUE(gRt.has(Screen::Home));
  TEST_ASSERT_TRUE(gRt.has(Screen::Sleep));
  TEST_ASSERT_FALSE(gRt.has(Screen::Detail));
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_TRUE(gRt.block(Screen::Home, b, n));
  TEST_ASSERT_EQUAL(int(Error::Ok), int(checkBlock(b, n)));
  TEST_ASSERT_FALSE(gRt.block(Screen::Detail, b, n));
  TEST_ASSERT_TRUE(gRt.activate(""));
  TEST_ASSERT_EQUAL_STRING("", gRt.active());
  TEST_ASSERT_FALSE(gRt.block(Screen::Home, b, n));
  char id[kIdLen];
  TEST_ASSERT_FALSE(gRt.takeBroken(id, sizeof id));
}

static void test_broken_active_theme_falls_back() {
  reset();
  gSt.raw("prova")[100] ^= 0x01;  // CRC sbagliato: non si attiva
  TEST_ASSERT_FALSE(gRt.activate("prova"));
  TEST_ASSERT_EQUAL_STRING("", gRt.active());
  char id[kIdLen];
  TEST_ASSERT_TRUE(gRt.takeBroken(id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("prova", id);
  TEST_ASSERT_FALSE(gRt.takeBroken(id, sizeof id));  // una volta sola
  // rovinato dopo l'attivazione (flash che cede): al prossimo caricamento torna di serie
  reset();
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  std::vector<uint8_t> f = load("prova.cthm");
  const uint32_t off = f[76] | f[77] << 8 | f[78] << 16 | uint32_t(f[79]) << 24;  // blocco della home
  gSt.raw("prova")[off] = 0;  // 0 varianti
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_FALSE(gRt.block(Screen::Home, b, n));
  TEST_ASSERT_EQUAL_STRING("", gRt.active());
  TEST_ASSERT_TRUE(gRt.takeBroken(id, sizeof id));
}

typedef Canvas<250, 122> Cv;
static Model gM;
static const Ms kNow = 1791707400000LL;
static const WallClock kWc = wallFromCivil(2026, 10, 11, 10, 30, 0);

static void compose(Cv &cv) { ui::compose(cv, kHeltec213, gM, 0, kNow, kWc); }

static void test_dispatch_home_and_perm() {
  reset();
  newModel(gM, kNow, kWc);
  resetStack(gM, makeScreen(ScreenId::Home));
  gM.sys.pc = true;
  Cv base, themed;
  ui::setThemeRuntime(nullptr);
  compose(base);
  ui::setThemeRuntime(&gRt);
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  compose(themed);
  TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
  TEST_ASSERT_TRUE(memcmp(base.data(), themed.data(), Cv::kBytes) != 0);
  // Permessi: identica a quella di serie con o senza tema
  gM.sessions[0].state = SessionState::Permission;
  ScreenEntry p = makeScreen(ScreenId::Perm);
  p.i = 0;
  pushScreen(gM, p);
  ui::setThemeRuntime(nullptr);
  compose(base);
  ui::setThemeRuntime(&gRt);
  compose(themed);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  TEST_ASSERT_EQUAL_MEMORY(base.data(), themed.data(), Cv::kBytes);
  // Boot: di serie
  resetStack(gM, makeScreen(ScreenId::Boot));
  compose(themed);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  ui::setThemeRuntime(nullptr);
}

static void test_suspend_and_sleep_rect() {
  reset();
  newModel(gM, kNow, kWc);
  resetStack(gM, makeScreen(ScreenId::Home));
  pushScreen(gM, makeScreen(ScreenId::Sleep));
  ui::setThemeRuntime(&gRt);
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  const ui::SleepRect r = ui::sleepMascotRect(kHeltec213, gM, kWc);
  TEST_ASSERT_EQUAL(158, r.x);  // la mascotte del Riposo di «prova»: scala 3 in (158, 30)
  TEST_ASSERT_EQUAL(30, r.y);
  TEST_ASSERT_EQUAL(84, r.w);
  TEST_ASSERT_EQUAL(75, r.h);
  gRt.suspend(true);  // PRG all'accensione: di serie per questo avvio
  Cv cv;
  compose(cv);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  TEST_ASSERT_EQUAL_STRING("prova", gRt.active());  // il tema scelto resta per il prossimo avvio
  ui::setThemeRuntime(nullptr);
}

static void test_safe_boot() {
  SafeBoot s;
  TEST_ASSERT_FALSE(s.step(true, true, 0));
  TEST_ASSERT_FALSE(s.step(true, true, 1499));
  TEST_ASSERT_TRUE(s.step(true, true, 1500));
  TEST_ASSERT_FALSE(s.step(true, true, 1600));     // una volta sola
  TEST_ASSERT_TRUE(s.swallow(true));
  TEST_ASSERT_TRUE(s.swallow(false));              // il gesto del rilascio si scarta
  TEST_ASSERT_FALSE(s.swallow(false));
  SafeBoot t;
  TEST_ASSERT_FALSE(t.step(true, true, 0));
  TEST_ASSERT_FALSE(t.step(false, true, 1000));    // rilasciato prima: si ricomincia
  TEST_ASSERT_FALSE(t.step(true, true, 1100));
  TEST_ASSERT_FALSE(t.step(true, false, 3000));    // avvio finito: non vale più
  TEST_ASSERT_FALSE(t.swallow(false));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_and_verify);
  RUN_TEST(test_activate_and_block);
  RUN_TEST(test_broken_active_theme_falls_back);
  RUN_TEST(test_dispatch_home_and_perm);
  RUN_TEST(test_suspend_and_sleep_rect);
  RUN_TEST(test_safe_boot);
  return UNITY_END();
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd firmware && pio test -e native -f test_theme_runtime`
Expected: FAIL (manca `core/theme_runtime.h`).

- [ ] **Step 3: Scrivere `core/theme_runtime.h`** con le dichiarazioni di «Produces». `MemStorage`:

```cpp
// In RAM: per i test native (sulla scheda c'è hw/theme_fs, piano 1b). Posti: 8 temi + il file temporaneo.
class MemStorage : public Storage {
 public:
  MemStorage() { eraseAll(); }
  int list(char (*ids)[kIdLen], int cap) override;
  int32_t size(const char *id) override;
  bool read(const char *id, uint32_t off, uint8_t *dst, uint32_t n) override;
  void remove(const char *id) override;
  bool tmpBegin() override;
  bool tmpAppend(const uint8_t *p, uint32_t n) override;
  int32_t tmpSize() override { return tmpUsed_ ? int32_t(tmpLen_) : -1; }
  bool tmpRead(uint32_t off, uint8_t *dst, uint32_t n) override;
  bool tmpCommit(const char *id) override;
  void tmpDrop() override { tmpUsed_ = false; tmpLen_ = 0; }
  int readOrder(char *dst, int cap) override;
  bool writeOrder(const char *src, int n) override;
  void eraseAll() override;
  uint8_t *raw(const char *id);
  bool failWrites;
  int commits;

 private:
  static const int kSlots = kMaxThemes + 1;  // uno in più: si rinomina prima di togliere il più vecchio
  char id_[kSlots][kIdLen];
  uint32_t len_[kSlots];
  uint8_t data_[kSlots][kMaxFile];
  uint8_t tmp_[kMaxFile];
  uint32_t tmpLen_;
  bool tmpUsed_;
  char order_[kMaxThemes * kIdLen + 16];
  int orderLen_;
};
```

`Runtime`:

```cpp
class Runtime {
 public:
  Runtime();
  void begin(Storage *st);
  bool activate(const char *id);
  const char *active() const { return id_; }
  uint32_t activeCrc() const { return id_[0] ? h_.crc : 0; }
  bool has(Screen s) const;
  bool block(Screen s, const uint8_t *&b, uint16_t &n);
  void suspend(bool on) { suspended_ = on; loaded_ = Screen::None; }
  bool suspended() const { return suspended_; }
  bool takeBroken(char *id, size_t cap);

 private:
  void broke();
  Storage *st_;
  char id_[kIdLen];
  Header h_;
  IndexEntry idx_[kMaxScreens];
  uint8_t buf_[kMaxBlock];  // il blocco della schermata corrente (spec §3: buffer statico da 4 096 byte)
  Screen loaded_;
  bool suspended_;
  bool broken_;
  char brokenId_[kIdLen];
};
```

`SafeBoot`:

```cpp
// Uscita di emergenza (spec §3): PRG tenuto 1,5 s durante la schermata di avvio → tema di serie per questo avvio.
// Non all'accensione col tasto già giù: GPIO0 è il pin di boot e la ROM entrerebbe nel caricatore seriale.
class SafeBoot {
 public:
  static const Ms kHoldMs;
  SafeBoot() : armed_(false), fired_(false), swallow_(false), since_(0) {}
  bool step(bool down, bool boot, Ms now);
  bool swallow(bool downNow);

 private:
  bool armed_, fired_, swallow_;
  Ms since_;
};
```

- [ ] **Step 4: Scrivere `core/theme_runtime.cpp`**

```cpp
#include "core/theme_runtime.h"

#include <string.h>

namespace cal {
namespace theme {

const Ms SafeBoot::kHoldMs = 1500;

bool SafeBoot::step(bool down, bool boot, Ms now) {
  if (fired_) return false;
  if (!down || !boot) { armed_ = false; return false; }
  if (!armed_) { armed_ = true; since_ = now; return false; }
  if (now - since_ < kHoldMs) return false;
  fired_ = swallow_ = true;
  return true;
}

bool SafeBoot::swallow(bool downNow) {
  if (!swallow_) return false;
  if (!downNow) swallow_ = false;
  return true;
}

// ---------- MemStorage ----------
namespace {
int findSlot(const char (*ids)[kIdLen], int n, const char *id) {
  for (int i = 0; i < n; i++)
    if (ids[i][0] && !strcmp(ids[i], id)) return i;
  return -1;
}
}  // namespace

void MemStorage::eraseAll() {
  memset(id_, 0, sizeof id_);
  memset(len_, 0, sizeof len_);
  tmpLen_ = 0;
  tmpUsed_ = false;
  orderLen_ = -1;
  failWrites = false;
  commits = 0;
}
int MemStorage::list(char (*ids)[kIdLen], int cap) {
  int n = 0;
  for (int i = 0; i < kSlots && n < cap; i++)
    if (id_[i][0]) memcpy(ids[n++], id_[i], kIdLen);
  return n;
}
int32_t MemStorage::size(const char *id) {
  const int k = findSlot(id_, kSlots, id);
  return k < 0 ? -1 : int32_t(len_[k]);
}
bool MemStorage::read(const char *id, uint32_t off, uint8_t *dst, uint32_t n) {
  const int k = findSlot(id_, kSlots, id);
  if (k < 0 || off + n > len_[k]) return false;
  memcpy(dst, data_[k] + off, n);
  return true;
}
void MemStorage::remove(const char *id) {
  const int k = findSlot(id_, kSlots, id);
  if (k >= 0) id_[k][0] = 0;
}
bool MemStorage::tmpBegin() { tmpUsed_ = !failWrites; tmpLen_ = 0; return tmpUsed_; }
bool MemStorage::tmpAppend(const uint8_t *p, uint32_t n) {
  if (!tmpUsed_ || failWrites || tmpLen_ + n > kMaxFile) return false;
  memcpy(tmp_ + tmpLen_, p, n);
  tmpLen_ += n;
  return true;
}
bool MemStorage::tmpRead(uint32_t off, uint8_t *dst, uint32_t n) {
  if (!tmpUsed_ || off + n > tmpLen_) return false;
  memcpy(dst, tmp_ + off, n);
  return true;
}
bool MemStorage::tmpCommit(const char *id) {
  if (!tmpUsed_ || failWrites) return false;
  int k = findSlot(id_, kSlots, id);
  for (int i = 0; k < 0 && i < kSlots; i++)
    if (!id_[i][0]) k = i;
  if (k < 0) return false;
  strncpy(id_[k], id, kIdLen - 1);
  id_[k][kIdLen - 1] = 0;
  memcpy(data_[k], tmp_, tmpLen_);
  len_[k] = tmpLen_;
  tmpDrop();
  commits++;
  return true;
}
int MemStorage::readOrder(char *dst, int cap) {
  if (orderLen_ < 0 || orderLen_ > cap) return -1;
  memcpy(dst, order_, orderLen_);
  return orderLen_;
}
bool MemStorage::writeOrder(const char *src, int n) {
  if (failWrites || n > int(sizeof order_)) return false;
  memcpy(order_, src, n);
  orderLen_ = n;
  return true;
}
uint8_t *MemStorage::raw(const char *id) {
  const int k = findSlot(id_, kSlots, id);
  return k < 0 ? nullptr : data_[k];
}

// ---------- controlli su un file salvato ----------
int catalog(Storage &st, CatalogEntry *out, int cap) {
  char ids[kMaxThemes + 1][kIdLen];
  const int n = st.list(ids, kMaxThemes + 1);
  int k = 0;
  for (int i = 0; i < n && k < cap; i++) {
    uint8_t hb[kHeaderLen];
    Header h;
    if (!st.read(ids[i], 0, hb, kHeaderLen) || parseHeader(hb, kHeaderLen, h) != Error::Ok || strcmp(h.id, ids[i])) continue;
    memcpy(out[k].id, h.id, kIdLen);
    memcpy(out[k].name, h.name, kNameLen);
    out[k].crc = h.crc;
    out[k].size = h.length;
    k++;
  }
  return k;
}

// Lettore comune per i file salvati e per il temporaneo (piano 1b): id nullptr = temporaneo.
static bool rd(Storage &st, const char *id, uint32_t off, uint8_t *dst, uint32_t n) {
  return id ? st.read(id, off, dst, n) : st.tmpRead(off, dst, n);
}

Error verifyAt(Storage &st, const char *id, int32_t size, uint8_t *scratch, Header *hOut, IndexEntry *idx) {
  Header h;
  if (size < kHeaderLen) return Error::Short;
  uint8_t hb[kHeaderLen];
  if (!rd(st, id, 0, hb, kHeaderLen)) return Error::Short;
  Error e = parseHeader(hb, kHeaderLen, h);
  if (hOut) *hOut = h;
  if (e != Error::Ok) return e;
  if (h.length != uint32_t(size)) return Error::Length;
  uint32_t crc = 0;
  uint8_t chunk[256];
  for (uint32_t off = 12; off < h.length;) {
    const uint32_t n = h.length - off < sizeof chunk ? h.length - off : uint32_t(sizeof chunk);
    if (!rd(st, id, off, chunk, n)) return Error::Short;
    crc = crc32Update(crc, chunk, n);
    off += n;
  }
  if (crc != h.crc) return Error::Crc;
  if (!rd(st, id, kHeaderLen, scratch, 8u * h.nScreens)) return Error::Index;
  e = parseIndex(scratch, 8u * h.nScreens, h, idx, kMaxScreens);
  if (e != Error::Ok) return e;
  for (int i = 0; i < h.nScreens; i++) {
    if (!rd(st, id, idx[i].off, scratch, idx[i].len)) return Error::Block;
    e = checkBlock(scratch, idx[i].len);
    if (e != Error::Ok) return e;
  }
  return Error::Ok;
}

Error verifyStored(Storage &st, const char *id, uint8_t *scratch, Header *h) {
  IndexEntry idx[kMaxScreens];
  return verifyAt(st, id, st.size(id), scratch, h, idx);
}

// ---------- Runtime ----------
Runtime::Runtime() : st_(nullptr), loaded_(Screen::None), suspended_(false), broken_(false) {
  id_[0] = 0;
  brokenId_[0] = 0;
  memset(&h_, 0, sizeof h_);
}

void Runtime::begin(Storage *st) {
  st_ = st;
  id_[0] = 0;
  loaded_ = Screen::None;
}

void Runtime::broke() {
  if (id_[0]) {
    memcpy(brokenId_, id_, kIdLen);
    broken_ = true;
  }
  id_[0] = 0;
  loaded_ = Screen::None;
}

bool Runtime::activate(const char *id) {
  loaded_ = Screen::None;
  if (!id || !id[0]) { id_[0] = 0; return true; }
  if (!st_) return false;
  strncpy(id_, id, kIdLen - 1);
  id_[kIdLen - 1] = 0;
  if (verifyAt(*st_, id_, st_->size(id_), buf_, &h_, idx_) != Error::Ok) { broke(); return false; }
  return true;
}

bool Runtime::has(Screen s) const {
  if (!id_[0] || suspended_) return false;
  for (int i = 0; i < h_.nScreens; i++)
    if (idx_[i].screen == s) return true;
  return false;
}

bool Runtime::block(Screen s, const uint8_t *&b, uint16_t &n) {
  if (!id_[0] || suspended_ || !st_) return false;
  for (int i = 0; i < h_.nScreens; i++) {
    if (idx_[i].screen != s) continue;
    if (loaded_ != s) {
      loaded_ = Screen::None;
      if (!st_->read(id_, idx_[i].off, buf_, idx_[i].len) || checkBlock(buf_, idx_[i].len) != Error::Ok) { broke(); return false; }
      loaded_ = s;
    }
    b = buf_;
    n = idx_[i].len;
    return true;
  }
  return false;
}

bool Runtime::takeBroken(char *id, size_t cap) {
  if (!broken_) return false;
  broken_ = false;
  if (cap) { strncpy(id, brokenId_, cap - 1); id[cap - 1] = 0; }
  return true;
}

}  // namespace theme
}  // namespace cal
```

Nota: `verifyAt` è dichiarata anche in `theme_runtime.h` (la usa l'installatore del piano 1b con `id = nullptr` per il file temporaneo): `Error verifyAt(Storage &st, const char *id, int32_t size, uint8_t *scratch, Header *h, IndexEntry *idx)`.

- [ ] **Step 5: Registro del runtime in `ui/theme_draw`**

In `ui/theme_draw.h` (dopo `drawThemeBlock`):

```cpp
namespace cal { namespace theme { class Runtime; } }
// Tema attivo per ui::draw (nullptr = sempre di serie, come nei test delle schermate).
void setThemeRuntime(cal::theme::Runtime *rt);
cal::theme::Runtime *themeRuntime();
// Ultimo disegno fatto da ui::draw: drawn falso se la schermata era di serie.
const ThemeDrawInfo &lastThemeDraw();
ThemeDrawInfo &lastThemeDrawMut();  // solo per screens.cpp
```

(la dichiarazione anticipata va **prima** di `namespace ui {`.) In `theme_draw.cpp`:

```cpp
namespace {
cal::theme::Runtime *gRt = nullptr;
ThemeDrawInfo gLast;
}
void setThemeRuntime(cal::theme::Runtime *rt) { gRt = rt; }
cal::theme::Runtime *themeRuntime() { return gRt; }
const ThemeDrawInfo &lastThemeDraw() { return gLast; }
ThemeDrawInfo &lastThemeDrawMut() { return gLast; }
```

(il namespace anonimo nuovo non deve nascondere quello già presente: unire i due.)

- [ ] **Step 6: Aggancio in `ui/screens.cpp`**

In testa: `#include "core/theme_runtime.h"` e `#include "ui/theme_draw.h"`. Prima di `drawScreen`:

```cpp
// Schermata personalizzata dal tema attivo (docs/TEMI.md §1.1). false = di serie.
static bool themed(Surface &c, const DeviceProfile &dev, const Model &m, int f, Ms now, const WallClock &wc, cal::theme::Screen s) {
  cal::theme::Runtime *rt = themeRuntime();
  const uint8_t *b;
  uint16_t n;
  if (!rt || !rt->block(s, b, n)) return false;
  return drawThemeBlock(c, dev, m, f, now, wc, s, b, n, &lastThemeDrawMut());
}
```

`draw()` azzera l'ultimo disegno prima di `drawScreen`: `memset(&lastThemeDrawMut(), 0, sizeof(ThemeDrawInfo));`.

In `drawScreen` (le righe di Boot, BattLow, Calib, Ota, GbLoad, GbPlay, Perm, Pair, Confirm **non cambiano**):

```cpp
  using TS = cal::theme::Screen;
  if (tp.id == ScreenId::Sleep) { if (!themed(c, dev, m, f, now, wc, TS::Sleep)) sleepScreen(c, dev, m, f, wc); return; }
  ...
  if (!m.sys.pc && (tp.id == ScreenId::Home || tp.id == ScreenId::Detail)) {
    if (m.sys.pcOff) { if (!themed(c, dev, m, f, now, wc, TS::Riepilogo)) riepilogo(c, dev, m, now, wc); }
    else if (!themed(c, dev, m, f, now, wc, TS::Offline)) offline(c, dev, m, f, now, wc);
    drawToast(c, dev, m, now);
    return;
  }
  ...
  switch (tp.id) {
    case ScreenId::Home: if (!themed(c, dev, m, f, now, wc, TS::Home)) homeS(c, dev, m, f, now, wc); break;
    case ScreenId::Detail: if (!themed(c, dev, m, f, now, wc, TS::Detail)) detailS(c, dev, m, f, now); break;
    case ScreenId::Oggi: if (!themed(c, dev, m, f, now, wc, TS::Oggi)) oggi(c, dev, m, wc); break;
    case ScreenId::Batteria: if (!themed(c, dev, m, f, now, wc, TS::Batteria)) batteria(c, dev, m, wc); break;
    case ScreenId::Stato: if (!themed(c, dev, m, f, now, wc, TS::Stato)) stato(c, dev, m, wc); break;
    case ScreenId::Limiti: if (!themed(c, dev, m, f, now, wc, TS::Limiti)) limiti(c, dev, m, now, wc); break;
    case ScreenId::Focus: if (!themed(c, dev, m, f, now, wc, TS::Focus)) focusScreen(c, dev, m, f, now, wc); break;
    case ScreenId::Ambiente: if (!themed(c, dev, m, f, now, wc, TS::Ambiente)) ambiente(c, dev, m, f, wc); break;
    case ScreenId::Pet: if (!themed(c, dev, m, f, now, wc, TS::Pet)) petScreen(c, dev, m, f, wc); break;
    ... (Perm, List, WifiScan, Qr, Connecting, Storico, Game, Guest, Servizi, Agenda, Pair, Confirm invariati)
```

`sleepMascotRect` (all'inizio della funzione):

```cpp
  if (cal::theme::Runtime *rt = themeRuntime()) {
    const uint8_t *b;
    uint16_t n;
    if (rt->block(cal::theme::Screen::Sleep, b, n)) {
      NullSurface ns;
      ThemeDrawInfo info;
      if (drawThemeBlock(ns, dev, m, 0, 0, wc, cal::theme::Screen::Sleep, b, n, &info))
        return info.hasMascot ? SleepRect{info.mx, info.my, info.mw, info.mh} : SleepRect{0, 0, 0, 0};
    }
  }
```

(`now = 0` va bene: il riquadro della mascotte non dipende dal tempo.)

- [ ] **Step 7: Eseguire i test**

Run: `cd firmware && pio test -e native -f test_theme_runtime && pio test -e native`
Expected: PASS; `test_ui`, `test_core_golden` e gli altri invariati (senza runtime registrato `themed()` è sempre falso).

- [ ] **Step 8: Build della scheda**

Run: `cd firmware && pio run -e heltec213 2>&1 | tail -5`
Expected: SUCCESS. `MemStorage` non va istanziata nel firmware (nessun `static MemStorage` fuori dai test): controllare che `.bss` non cresca di centinaia di KB (`pio run` stampa `RAM: … used`; la crescita attesa di questo piano è 0: il `Runtime` statico arriva nel piano 1b).

- [ ] **Step 9: Commit**

```bash
git add firmware/src/core/theme_runtime.h firmware/src/core/theme_runtime.cpp firmware/src/ui/theme_draw.h firmware/src/ui/theme_draw.cpp firmware/src/ui/screens.cpp firmware/test/test_theme_runtime
git commit -m "Temi: tema attivo con buffer da 4 KB, uscita di emergenza e aggancio alle schermate (Permessi sempre di serie)"
```

---
### Task 7: Caratteri della scheda in JS e testo come `text.cpp`

**Files:**
- Create: `tools/fonts/export-js.mjs`
- Create (generato): `service/internal/webui/static/assets/js/theme/fonts.js`
- Create: `service/internal/webui/static/assets/js/theme/text.js`
- Create: `tools/webui-dev/test-theme.mjs`
- Modify: `tools/webui-dev/package.json` (`"test": "node test-units.mjs && node test-theme.mjs"`), `tools/fonts/README.md` (sezione «Export per la webapp»)
- Test: `tools/webui-dev/test-theme.mjs`

**Interfaces:**
- Consumes: `firmware/src/gfx/fonts/f_registry.h`, `f_*.h`, `f_kern.h` (fonte unica: i bitmap della scheda, nessuna nuova rasterizzazione).
- Produces:
  - `fonts.js`: `export const FONTS` (array nell'ordine di `kFonts`: `{size, weight, pix, inv, cps: Uint16Array, adv: Uint16Array, first: Uint16Array, glyphs: Int16Array /* 5 per glifo: off, w, h, x, y */, bitmap: Uint8Array, kern: 500 | 600 | 700 | null}`), `export const KERN_KEYS` (`Uint32Array`), `export const KERN` (`{500: Int16Array, 600: …, 700: …}`);
  - `text.js`: `jsRound(v)`, `hasFont(size, weight, pix)`, `textWidth(s, size, weight = 600, pix = false)`, `textWidthMilli(s, size, weight = 600, pix = false)`, `fitText(s, size, weight, pix, max)` (stringa, al massimo 255 byte UTF-8), `wrapLines(s, maxLines, size, maxW, weight = 500, pix = false)` (array, righe al massimo di 95 byte come `char[96]`), `drawText(surf, s, x, y, size, {weight, pix, align, ink})` (`align` 0 sinistra, 1 centro, 2 destra), `T(surf, s, x, y, size, {weight, pix, align, ink, max})` (come `T()` di `screens.cpp`: `Math.round` di x e y, poi `fitText` se `max`).
  - `surf` = qualunque oggetto con `fillRect(x, y, w, h, ink)` (Task 8).

- [ ] **Step 1: Scrivere il test che fallisce**

`tools/webui-dev/test-theme.mjs` (le sezioni dei Task 8–10 si aggiungono in fondo):

```js
// Prove del renderer dei temi (docs/TEMI.md): caratteri, testo, primitive, QR, valori, parità col firmware.
//   node test-theme.mjs
import { FONTS } from '../../service/internal/webui/static/assets/js/theme/fonts.js';
import * as Tx from '../../service/internal/webui/static/assets/js/theme/text.js';

let fail = 0;
const check = (ok, msg) => { if (!ok) { fail++; console.log('FAIL', msg); } };
const eq = (a, b, msg) => check(a === b, msg + ': ' + JSON.stringify(a) + ' invece di ' + JSON.stringify(b));

// 1. caratteri: le 22 combinazioni di f_registry.h
eq(FONTS.length, 22, 'caratteri');
check(Tx.hasFont(46, 700, false) && Tx.hasFont(8, 400, true) && !Tx.hasFont(17, 700, false), 'hasFont');
check(Tx.hasFont(8, 600, true), 'Silkscreen 600 = 700, come normWeight() in text.cpp');

// 2. le stesse prove di firmware/test/test_text (test_fit, test_wrap, «?» per i glifi assenti)
eq(Tx.textWidth('', 12), 0, 'larghezza vuota');
eq(Tx.fitText('api-server', 13, 700, false, 150), 'api-server', 'fit: sta già');
const cut = Tx.fitText('calamaio-firmware-esp32-progetto', 13, 700, false, 150);
check(cut.endsWith('…') && Tx.textWidthMilli(cut, 13, 700) <= 150000, 'fit con «…»');
const px = Tx.fitText('CONTESTO: CALAMAIO-FIRMWARE-ESP32 87%', 8, 400, true, 80);
check(px.endsWith('..') && Tx.textWidthMilli(px, 8, 400, true) <= 80000, 'fit Silkscreen con «..»');
for (let max = 20; max < 120; max++) {
  const s = Tx.fitText('Ultimo dato alle 14:32 di oggi', 10, 600, false, max);
  if (s.length >= 2) check(s[s.length - 2] !== ' ', 'trimEnd a ' + max);
}
eq(Tx.fitText('PERMESSO', 20, 700, false, 1), 'P…', 'resta un carattere');
eq(Tx.wrapLines('', 6, 10, 100).length, 0, 'wrap vuoto');
eq(Tx.wrapLines('   ', 6, 10, 100).length, 0, 'wrap spazi');
eq(JSON.stringify(Tx.wrapLines('  uno   due  ', 6, 10, 200)), '["uno due "]', 'wrap come split(/\\s+/)');
eq(Tx.wrapLines('parolalunghissimasenzaspazi corta', 6, 10, 30).length, 2, 'parola lunga');
eq(Tx.textWidthMilli('\u{1F600}', 12), Tx.textWidthMilli('?', 12), 'fuori BMP = ?');
const long = Tx.wrapLines('è'.repeat(80), 2, 10, 2000);
check(new TextEncoder().encode(long[0]).length <= 95, 'riga al massimo 95 byte come char[96]');

// 3. disegno: testo chiaro su scuro toglie inchiostro
const surf = (() => { const px = new Uint8Array(250 * 122); return { px, fillRect(x, y, w, h, ink) {
  for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++) if (xx >= 0 && yy >= 0 && xx < 250 && yy < 122) px[yy * 250 + xx] = ink ? 1 : 0; } }; })();
surf.fillRect(0, 0, 250, 20, true);
const before = surf.px.reduce((a, b) => a + b, 0);
Tx.drawText(surf, 'NEGA', 10, 2, 14, { weight: 700, ink: false });
check(surf.px.reduce((a, b) => a + b, 0) < before, 'testo carta su inchiostro');

console.log(fail ? fail + ' prove fallite' : 'temi: tutto ok');
process.exit(fail ? 1 : 0);
```

(Le sezioni dei task successivi vanno inserite **prima** delle due righe finali.)

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd tools/webui-dev && node test-theme.mjs`
Expected: FAIL (`Cannot find module …/theme/fonts.js`).

- [ ] **Step 3: Scrivere `tools/fonts/export-js.mjs`**

```js
#!/usr/bin/env node
// Esporta i caratteri della scheda (firmware/src/gfx/fonts/*.h, generati da gen.py) nel modulo della webapp
// service/internal/webui/static/assets/js/theme/fonts.js: stessi bitmap, avanzamenti e crenatura, così il renderer
// dei temi disegna come text.cpp. Niente browser: si leggono i .h.
//   node tools/fonts/export-js.mjs          scrive fonts.js
//   node tools/fonts/export-js.mjs --check  esce con 1 se fonts.js è vecchio
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const DIR = path.join(REPO, 'firmware', 'src', 'gfx', 'fonts');
const OUT = path.join(REPO, 'service', 'internal', 'webui', 'static', 'assets', 'js', 'theme', 'fonts.js');

const read = (f) => fs.readFileSync(path.join(DIR, f), 'utf8');
function array(src, name) {
  const m = new RegExp('\\b' + name + '\\[\\] FONT_DATA = \\{([\\s\\S]*?)\\n\\};').exec(src);
  if (!m) throw new Error(name + ' non trovato');
  return m[1].replace(/\/\/[^\n]*/g, '');
}
const nums = (s) => (s.match(/-?0x[0-9a-fA-F]+|-?\d+/g) || []).map((v) => Number(v));
const b64 = (typed) => Buffer.from(typed.buffer, typed.byteOffset, typed.byteLength).toString('base64');

function build() {
  const reg = read('f_registry.h');
  const rows = [...reg.matchAll(/\{(\d+), (\d+), (true|false), (true|false), (\d+), (\w+)_entries, \w+_glyphs, \w+_bitmap, (?:&kern_(\d+)|nullptr)\}/g)];
  if (rows.length !== 22) throw new Error('f_registry.h: ' + rows.length + ' caratteri invece di 22');
  const kern = read('f_kern.h');
  let out = '// Generato da tools/fonts/export-js.mjs dai .h del firmware: non modificare a mano.\n';
  out += 'const b64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));\n';
  out += 'const u16 = (s) => new Uint16Array(b64(s).buffer);\nconst i16 = (s) => new Int16Array(b64(s).buffer);\n';
  out += 'export const KERN_KEYS = new Uint32Array(b64(' + JSON.stringify(b64(Uint32Array.from(nums(array(kern, 'kern_keys_all'))))) + ').buffer);\n';
  out += 'export const KERN = {\n';
  for (const w of [500, 600, 700]) out += '  ' + w + ': i16(' + JSON.stringify(b64(Int16Array.from(nums(array(kern, 'kern_vals_' + w))))) + '),\n';
  out += '};\nexport const FONTS = [\n';
  for (const r of rows) {
    const [, size, weight, pix, inv, n, name, kw] = r;
    const src = read('f_' + name + '.h');
    const ent = nums(array(src, name + '_entries'));
    const gl = nums(array(src, name + '_glyphs'));
    const bm = Uint8Array.from(nums(array(src, name + '_bitmap')));
    if (ent.length !== 3 * Number(n)) throw new Error(name + ': voci ' + ent.length / 3);
    const cps = Uint16Array.from(ent.filter((_, i) => i % 3 === 0));
    const adv = Uint16Array.from(ent.filter((_, i) => i % 3 === 1));
    const first = Uint16Array.from(ent.filter((_, i) => i % 3 === 2));
    out += '  { size: ' + size + ', weight: ' + weight + ', pix: ' + pix + ', inv: ' + inv + ', kern: ' + (kw || 'null') +
      ', cps: u16(' + JSON.stringify(b64(cps)) + '), adv: u16(' + JSON.stringify(b64(adv)) + '), first: u16(' + JSON.stringify(b64(first)) +
      '), glyphs: i16(' + JSON.stringify(b64(Int16Array.from(gl))) + '), bitmap: b64(' + JSON.stringify(b64(bm)) + ') },\n';
  }
  return out + '];\n';
}

const text = build();
if (process.argv.includes('--check')) {
  const old = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '';
  if (old !== text) { console.error('fonts.js vecchio: node tools/fonts/export-js.mjs'); process.exit(1); }
  console.log('fonts.js aggiornato');
} else {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, text);
  console.log('scritto ' + path.relative(REPO, OUT) + ' (' + text.length + ' byte)');
}
```

Nota: i glifi nei `.h` sono `{off, w, h, x, y}` (5 numeri per glifo, `Glyph` di `font_types.h`): `nums()` su tutto l'array dà già la sequenza piatta. Le voci sono `{0xCP, adv, first}` con un commento a fine riga (tolto da `array()`).

- [ ] **Step 4: Generare `fonts.js`**

Run: `node tools/fonts/export-js.mjs && node tools/fonts/export-js.mjs --check`
Expected: «scritto service/internal/webui/static/assets/js/theme/fonts.js (… byte)» poi «fonts.js aggiornato». Dimensione attesa sotto 1 MB.

- [ ] **Step 5: Scrivere `text.js`** (porting riga per riga di `firmware/src/gfx/text.cpp`)

```js
// Testo come text.cpp del firmware (che a sua volta è T(), fit(), wrapLines() del simulatore): stessi bitmap
// (fonts.js), avanzamenti in 16.16 come HarfBuzz in Chrome, crenatura, legature fi/fl, fase a 1/4 di pixel.
import { FONTS, KERN_KEYS, KERN } from './fonts.js';

const MAX_CPS = 256, LIG_FI = 0xe000, LIG_FL = 0xe001;
export const jsRound = (v) => Math.floor(v + 0.5);
const floorDiv = (a, b) => Math.floor(a / b);

const normWeight = (w, pix) => (pix ? (w <= 500 ? 400 : 700) : w <= 500 ? 500 : w <= 600 ? 600 : 700);
function findFont(size, weight, pix) {
  const w = normWeight(weight, pix);
  let best = null, bs = 1 << 30;
  for (const f of FONTS) {
    if (f.pix !== pix) continue;
    if (f.size === size && f.weight === w) return f;
    const sc = Math.abs(f.size - size) * 4 + (f.weight === w ? 0 : 1);
    if (sc < bs) { bs = sc; best = f; }
  }
  return best;
}
export function hasFont(size, weight, pix) {
  const f = findFont(size, weight, pix);
  return !!f && f.size === size && f.weight === normWeight(weight, pix);
}
function glyphIndex(f, cp) {
  let lo = 0, hi = f.cps.length - 1;
  while (lo <= hi) { const mid = (lo + hi) >> 1, c = f.cps[mid]; if (c === cp) return mid; if (c < cp) lo = mid + 1; else hi = mid - 1; }
  return -1;
}
function kernOf(f, a, b) {
  if (!f.kern) return 0;
  const key = ((a << 16) | b) >>> 0;
  let lo = 0, hi = KERN_KEYS.length - 1;
  while (lo <= hi) { const mid = (lo + hi) >> 1, k = KERN_KEYS[mid]; if (k === key) return KERN[f.kern][mid]; if (k < key) lo = mid + 1; else hi = mid - 1; }
  return 0;
}
const isJsSpace = (c) => (c >= 0x09 && c <= 0x0d) || c === 0x20 || c === 0xa0 || c === 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
  c === 0x2028 || c === 0x2029 || c === 0x202f || c === 0x205f || c === 0x3000 || c === 0xfeff;

// stringa → codepoint della BMP (fuori BMP: U+FFFD come decode() in C++), al massimo cap
function decode(s, cap = MAX_CPS) {
  const out = [];
  for (const ch of s || '') { if (out.length >= cap) break; const c = ch.codePointAt(0); out.push(c > 0xffff ? 0xfffd : c); }
  return out;
}
const utf8Len = (c) => (c < 0x80 ? 1 : c < 0x800 ? 2 : 3);
// codepoint → stringa, senza superare cap − 1 byte UTF-8 (come encode() in C++ con un buffer di cap byte)
function encode(cps, cap) {
  let len = 0, s = '';
  for (const c of cps) { const k = utf8Len(c); if (len + k > cap - 1) break; len += k; s += String.fromCharCode(c); }
  return s;
}
const advFx = (adv, size) => Math.floor((adv * size * 65536) / 1000);
const kernFx = (k, size) => floorDiv(k * Math.floor((size * 65536 * 65536) / 1000) + 32768, 65536);

function walk(f, cps, fn) {
  const q = glyphIndex(f, 0x3f);
  let u = 0, prev = 0, have = false;
  for (let i = 0; i < cps.length; i++) {
    let c = cps[i];
    if (c === 0x09 || c === 0x0a || c === 0x0c || c === 0x0d) c = 0x20;
    if (!f.pix && c === 0x66 && i + 1 < cps.length) {
      const nx = cps[i + 1];
      if (nx === 0x69 || nx === 0x6c) { c = nx === 0x69 ? LIG_FI : LIG_FL; i++; }
    }
    let g = glyphIndex(f, c);
    if (g < 0) { g = q; c = 0x3f; }
    if (g < 0) continue;
    if (have) u += kernFx(kernOf(f, prev, c), f.size);
    if (fn) fn(g, u);
    u += advFx(f.adv[g], f.size);
    prev = c;
    have = true;
  }
  return u;
}
const fits = (f, cps, max) => walk(f, cps) <= max * 65536;

function fitCps(f, pix, cps, max) {
  if (fits(f, cps, max)) return cps;
  const ell = pix ? [0x2e, 0x2e] : [0x2026];
  let n = cps.length;
  while (n > 1 && !fits(f, cps.slice(0, n).concat(ell), max)) n--;
  while (n > 0 && isJsSpace(cps[n - 1])) n--;
  return cps.slice(0, n).concat(ell);
}

export function textWidth(s, size, weight = 600, pix = false) {
  const f = findFont(size, weight, pix);
  return f ? floorDiv(walk(f, decode(s)) + 32768, 65536) : 0;
}
export function textWidthMilli(s, size, weight = 600, pix = false) {
  const f = findFont(size, weight, pix);
  return f ? floorDiv(walk(f, decode(s)) * 1000 + 32768, 65536) : 0;
}
export function fitText(s, size, weight, pix, max) {
  const f = findFont(size, weight, pix);
  let cps = decode(s);
  if (f) cps = fitCps(f, pix, cps, max);
  return encode(cps, 256);
}

function blit(surf, f, gi, x0, y0, ink) {
  const off = f.glyphs[5 * gi], w = f.glyphs[5 * gi + 1], h = f.glyphs[5 * gi + 2];
  let bit = 0;
  for (let yy = 0; yy < h; yy++) {
    let run = -1;
    for (let xx = 0; xx < w; xx++, bit++) {
      const on = f.bitmap[off + (bit >> 3)] & (0x80 >> (bit & 7));
      if (on && run < 0) run = xx;
      if (!on && run >= 0) { surf.fillRect(x0 + run, y0 + yy, xx - run, 1, ink); run = -1; }
    }
    if (run >= 0) surf.fillRect(x0 + run, y0 + yy, w - run, 1, ink);
  }
}

export function drawText(surf, s, x, y, size, o = {}) {
  const weight = o.weight ?? 600, pix = !!o.pix, ink = o.ink ?? true, a = o.align || 0;
  const f = findFont(size, weight, pix);
  if (!f) return;
  const cps = decode(s);
  const total = walk(f, cps);
  const variant = !ink && f.inv ? 4 : 0;
  walk(f, cps, (g, u) => {
    const p2 = x * 131072 - a * total + 2 * u;
    const q = floorDiv(4 * p2 + 65536, 131072);
    const ix = floorDiv(q, 4), ph = q - ix * 4;
    const gi = f.first[g] + variant + ph;
    blit(surf, f, gi, ix + f.glyphs[5 * gi + 3], y + f.glyphs[5 * gi + 4], ink);
  });
}

export function T(surf, s, x, y, size, o = {}) {
  let str = s || '';
  if (o.max) str = fitText(str, size, o.weight ?? 600, !!o.pix, o.max);
  drawText(surf, str, jsRound(x), jsRound(y), size, o);
}

export function wrapLines(s, maxLines, size, maxW, weight = 500, pix = false) {
  const f = findFont(size, weight, pix);
  const out = [];
  if (!f || maxLines <= 0) return out;
  const src = decode(s);
  let line = [], i = 0;
  for (;;) {
    let j = i;
    while (j < src.length && !isJsSpace(src[j])) j++;
    const wd = src.slice(i, j);
    let t = line.length ? line.concat([0x20]) : [];
    t = t.concat(wd).slice(0, MAX_CPS);
    if (fits(f, t, maxW)) line = t;
    else {
      if (line.length) { out.push(encode(line, 96)); if (out.length >= maxLines) return out; }
      line = wd;
    }
    if (j >= src.length) break;
    while (j < src.length && isJsSpace(src[j])) j++;
    i = j;
  }
  if (line.length) out.push(encode(line, 96));
  return out;
}
```

Controllo del porting: in `wrapLines` di C++ `t` si costruisce con `ln` + spazio + parola fino a `kMaxCps`; qui `slice(0, MAX_CPS)` fa lo stesso. In `drawText` di C++ il testo passa da `decode(s, cps, kMaxCps)`: `decode()` qui ha lo stesso tetto di 256.

- [ ] **Step 6: Eseguire i test**

Run: `cd tools/webui-dev && node test-theme.mjs`
Expected: «temi: tutto ok».

- [ ] **Step 7: README e package.json**

`tools/webui-dev/package.json`: `"test": "node test-units.mjs && node test-theme.mjs"`. In `tools/fonts/README.md`, dopo «Comando», una sezione:

```markdown
## Export per la webapp

`node tools/fonts/export-js.mjs` legge i `.h` appena generati e scrive
`service/internal/webui/static/assets/js/theme/fonts.js` (renderer dei temi, docs/TEMI.md §6). Va rifatto dopo ogni
`gen.py`: `go test ./internal/webui/` fallisce se `fonts.js` è vecchio (`--check`).
```

Run: `cd tools/webui-dev && npm test`
Expected: le prove di prima più «temi: tutto ok».

- [ ] **Step 8: Commit**

```bash
git add tools/fonts/export-js.mjs tools/fonts/README.md service/internal/webui/static/assets/js/theme/fonts.js service/internal/webui/static/assets/js/theme/text.js tools/webui-dev/test-theme.mjs tools/webui-dev/package.json
git commit -m "Temi: caratteri della scheda esportati in JS e testo come text.cpp"
```

---
### Task 8: Superficie, primitive e QR in JS

**Files:**
- Create: `service/internal/webui/static/assets/js/theme/surface.js`, `prims.js`, `qr.js`
- Modify: `tools/webui-dev/test-theme.mjs` (sezioni 4 e 5)

**Interfaces:**
- Consumes: `jsRound` di `text.js` (Task 7); riferimenti: `firmware/src/gfx/prims.cpp`, `firmware/test/test_prims/golden.h`, `firmware/src/gfx/qr.cpp`, `firmware/src/gfx/qr/qrcode_ricmoo.cpp`.
- Produces:
  - `surface.js`: `class Bitmap { constructor(w = 250, h = 122); fillRect(x, y, w, h, ink); get(x, y); clear(ink = false); packed() /* Uint8Array come Canvas::data(): righe di ceil(w/8) byte, bit 7 = x più a sinistra, 1 = inchiostro */; countInk() }`, `class Swap { constructor(s); fillRect(x, y, w, h, ink) /* !ink */ }`, `class Null { fillRect() {} }`, `crc32(bytes)` (IEEE, come `avatarCrc32`);
  - `prims.js`: `box(c, x, y, w, h, col = true, t = 1)`, `dotsH(c, x0, x1, y)`, `dotsV(c, x, y0, y1)`, `glyph(c, rows, x, y, s, col)`, `icon(c, name, x, y, s, col = true)` (nomi `done`, `error`, `input`, `permission`, `idle`, `compact`, `working0`, `working1`), `iconForState(c, state, x, y, s, f, col = true)`, `SYMS` (array nell'ordine di `enum class Sym`), `sym(c, k, x, y, s, col)`, `bar(c, x, y, w, h, {done, total, working}, f)`, `wifiIcon(c, x, y, s, level, col)`, `battIcon(c, x, y, s, pct, col)`, `bigBar(c, x, y, w, h, pct)` — righe identiche a `prims.cpp` (stesse costanti `ICONS` e `SYMS`, stessa aritmetica con `jsRound`);
  - `qr.js`: `encode(text) → { size, version, get(x, y) } | null` come `qr::encode()` del firmware: versione = la più piccola da 1 a 5 con `len(UTF-8) ≤ [14, 26, 42, 62, 84][v−1]`, poi `qrcode_initText(…, v, ECC_MEDIUM, text)` di ricmoo (che sceglie da sé il modo numerico, alfanumerico o byte e la maschera migliore).

- [ ] **Step 1: Aggiungere le prove a `test-theme.mjs`** (le righe `import` vanno in testa al file, con le altre)

```js
// 4. primitive: stessi casi di tools/golden/prims-golden.js, confronto con firmware/test/test_prims/golden.h
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as Pr from '../../service/internal/webui/static/assets/js/theme/prims.js';
import { Bitmap, crc32 } from '../../service/internal/webui/static/assets/js/theme/surface.js';
import * as Qr from '../../service/internal/webui/static/assets/js/theme/qr.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const gh = fs.readFileSync(path.resolve(here, '../../firmware/test/test_prims/golden.h'), 'utf8');
const pgNames = JSON.parse('[' + /PG_NAMES\[\] = \{([^}]*)\}/.exec(gh)[1] + ']');
const pg = [...gh.matchAll(/^  "([.#]+)",$/gm)].map((m) => m[1]);
const S = (st, done, total) => ({ done, total, working: st === 'working' });
const CASES = {
  box: (c) => Pr.box(c, 2, 3, 20, 10, true, 1),
  box_t2: (c) => Pr.box(c, 1, 1, 30, 20, true, 2),
  dots: (c) => { Pr.dotsH(c, 0, 60, 2); Pr.dotsV(c, 5, 4, 30); },
  icons: (c) => { ['done', 'error', 'input', 'permission', 'idle', 'compact'].forEach((s, k) => Pr.iconForState(c, s, 1 + k * 9, 1, 1, 0)); Pr.iconForState(c, 'working', 1, 12, 2, 0); Pr.iconForState(c, 'working', 20, 12, 2, 1); },
  syms: (c) => { for (let k = 0; k < 8; k++) Pr.sym(c, k, 1 + k * 8, 1, 1, true); },
  bar_work: (c) => Pr.bar(c, 1, 1, 60, 9, S('working', 3, 7), 0),
  bar_work_f1: (c) => Pr.bar(c, 1, 1, 60, 9, S('working', 3, 7), 1),
  bar_done: (c) => Pr.bar(c, 1, 1, 60, 9, S('done', 6, 6), 0),
  bar_many: (c) => Pr.bar(c, 1, 12, 61, 7, S('working', 5, 20), 2),
  bar_none: (c) => Pr.bar(c, 1, 1, 50, 8, S('working', 0, 0), 3),
  bar_none_idle: (c) => Pr.bar(c, 1, 1, 50, 8, S('idle', 0, 0), 3),
  wifi: (c) => { Pr.wifiIcon(c, 1, 1, 1, 0, true); Pr.wifiIcon(c, 14, 1, 1, 2, true); Pr.wifiIcon(c, 27, 1, 1, 4, true); Pr.wifiIcon(c, 40, 1, 1, -1, true); Pr.wifiIcon(c, 1, 12, 2, 3, true); },
  batt: (c) => { Pr.battIcon(c, 1, 1, 1, 78, true); Pr.battIcon(c, 20, 1, 1, 5, true); Pr.battIcon(c, 1, 12, 2, 33, true); },
  bigbar: (c) => { Pr.bigBar(c, 1, 1, 60, 10, 62); Pr.bigBar(c, 1, 16, 41, 7, 100); },
};
for (const [name, draw] of Object.entries(CASES)) {
  const k = pgNames.indexOf(name);
  const b = new Bitmap(64, 32);
  draw(b);
  let s = '';
  for (let y = 0; y < 32; y++) for (let x = 0; x < 64; x++) s += b.get(x, y) ? '#' : '.';
  eq(s === pg[k], true, 'primitiva ' + name);
}

// 5. QR: versione come qr::encode del firmware, motivi di posizione, capienza
const q1 = Qr.encode('123456');
check(q1 && q1.version === 1 && q1.size === 21, 'QR numerico in versione 1');
check(Qr.encode('x'.repeat(15)).version === 2 && Qr.encode('x'.repeat(84)).version === 5, 'scelta della versione');
check(Qr.encode('x'.repeat(85)) === null, 'oltre 84 byte: niente');
const finder = (q, ox, oy) => { for (let y = 0; y < 7; y++) for (let x = 0; x < 7; x++) {
  const ring = x === 0 || y === 0 || x === 6 || y === 6, core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
  if (q.get(ox + x, oy + y) !== (ring || core)) return false; } return true; };
const q5 = Qr.encode('Ciao è 3 api-server');
check(finder(q5, 0, 0) && finder(q5, q5.size - 7, 0) && finder(q5, 0, q5.size - 7), 'motivi di posizione');
eq(crc32(new TextEncoder().encode('123456789')), 0xcbf43926, 'crc32');
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd tools/webui-dev && node test-theme.mjs`
Expected: FAIL (`Cannot find module …/theme/prims.js`).

- [ ] **Step 3: `surface.js`**

```js
// Superficie 1 bit del renderer dei temi: stesso formato di Canvas<W, H>::data() del firmware.
export class Bitmap {
  constructor(w = 250, h = 122) { this.w = w; this.h = h; this.px = new Uint8Array(w * h); }
  clear(ink = false) { this.px.fill(ink ? 1 : 0); }
  fillRect(x, y, w, h, ink) {
    const x0 = Math.max(0, x), y0 = Math.max(0, y), x1 = Math.min(this.w, x + w), y1 = Math.min(this.h, y + h);
    for (let yy = y0; yy < y1; yy++) this.px.fill(ink ? 1 : 0, yy * this.w + x0, yy * this.w + Math.max(x0, x1));
  }
  get(x, y) { return x >= 0 && y >= 0 && x < this.w && y < this.h && this.px[y * this.w + x] === 1; }
  countInk() { let n = 0; for (const v of this.px) n += v; return n; }
  packed() {
    const stride = (this.w + 7) >> 3, out = new Uint8Array(stride * this.h);
    for (let y = 0; y < this.h; y++) for (let x = 0; x < this.w; x++) if (this.px[y * this.w + x]) out[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    return out;
  }
}
export class Swap { constructor(s) { this.s = s; } fillRect(x, y, w, h, ink) { this.s.fillRect(x, y, w, h, !ink); } }
export class Null { fillRect() {} }

let table = null;
export function crc32(bytes) {
  if (!table) { table = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; table[n] = c >>> 0; } }
  let c = 0xffffffff;
  for (const b of bytes) c = table[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
```

- [ ] **Step 4: `prims.js`** — traduzione 1:1 di `firmware/src/gfx/prims.cpp`

```js
// Primitive come firmware/src/gfx/prims.cpp (porting delle funzioni del simulatore). c.fillRect(x, y, w, h, ink).
import { jsRound } from './text.js';

const clampd = (v, a, b) => (v < a ? a : v > b ? b : v);
const ICONS = {
  done: ['.......', '......#', '.....##', '#...##.', '##.##..', '.###...', '..#....'],
  error: ['#.....#', '##...##', '.##.##.', '..###..', '.##.##.', '##...##', '#.....#'],
  input: ['.#####.', '##...##', '.....##', '...###.', '..##...', '.......', '..##...'],
  permission: ['..##...', '..##...', '..##...', '..##...', '..##...', '.......', '..##...'],
  idle: ['#######', '....##.', '...##..', '..##...', '.##....', '#######', '.......'],
  compact: ['#######', '.#####.', '.......', '..###..', '.......', '...#...', '...#...'],
  working0: ['#######', '.#####.', '..###..', '...#...', '..#.#..', '.#...#.', '#######'],
  working1: ['#######', '.#...#.', '..#.#..', '...#...', '..###..', '.#####.', '#######'],
};
export const SYMS = [
  ['.#...#.', '.#...#.', '#######', '#######', '.#####.', '..###..', '...#...'],
  ['...#...', '..###..', '.#####.', '.#####.', '.#####.', '#######', '...#...'],
  ['..###..', '.##....', '##.....', '##.....', '##.....', '.##....', '..###..'],
  ['..###..', '.#...#.', '.#...#.', '#######', '###.###', '###.###', '#######'],
  ['...##', '..##.', '.##..', '#####', '..##.', '.##..', '##...'],
  ['..###..', '.#####.', '#######', '#######', '#######', '.#####.', '..###..'],
  ['#######', '.#...#.', '..#.#..', '...#...', '..###..', '.#####.', '#######'],
  ['.#####.', '#.....#', '#.#...#', '#..#..#', '#...#.#', '#.....#', '.#####.'],
];

export function box(c, x, y, w, h, col = true, t = 1) {
  if (t < 1) t = 1;
  c.fillRect(x, y, w, t, col); c.fillRect(x, y + h - t, w, t, col); c.fillRect(x, y, t, h, col); c.fillRect(x + w - t, y, t, h, col);
}
export function dotsH(c, x0, x1, y) { for (let x = x0; x < x1; x += 3) c.fillRect(x, y, 1, 1, true); }
export function dotsV(c, x, y0, y1) { for (let y = y0; y < y1; y += 2) c.fillRect(x, y, 1, 1, true); }
export function glyph(c, rows, x, y, s, col) {
  rows.forEach((row, r) => { for (let q = 0; q < row.length; q++) if (row[q] === '#') c.fillRect(x + q * s, y + r * s, s, s, col); });
}
export function icon(c, name, x, y, s, col = true) { glyph(c, ICONS[name], x, y, s, col); }
export function iconForState(c, state, x, y, s, f, col = true) {
  let n = 'idle';
  if (state === 'working') n = ((f % 2) + 2) % 2 ? 'working1' : 'working0';
  else if (['done', 'error', 'input', 'permission', 'compact'].includes(state)) n = state;
  icon(c, n, x, y, s, col);
}
export function sym(c, k, x, y, s, col) { glyph(c, SYMS[k], x, y, s, col); }
export function bar(c, x, y, w, h, st, f) {
  box(c, x, y, w, h, true, 1);
  const ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 4;
  if (ih <= 0 || iw <= 0) return;
  if (st.total > 0) {
    const per = iw / st.total, fw = jsRound(per * st.done);
    c.fillRect(ix, iy, fw, ih, true);
    if (st.working && st.done < st.total) {
      const sx = ix + fw, sw = jsRound(per) > 2 ? jsRound(per) : 2;
      for (let py = 0; py < ih; py++) for (let px = 0; px < sw; px++) if ((px + py + f) % 4 === 0) c.fillRect(sx + px, iy + py, 1, 1, true);
    }
    if (st.total <= 16) for (let k = 1; k < st.total; k++) {
      const tx = ix + jsRound(per * k);
      if (tx < ix + fw) c.fillRect(tx, iy, 1, ih, false); else c.fillRect(tx, iy + ih - 2, 1, 2, true);
    }
  } else {
    const mv = st.working ? f * 2 : 0;
    for (let px = 0; px < iw; px++) if ((px + mv) % 8 < 3) c.fillRect(ix + px, iy, 1, ih, true);
  }
}
export function wifiIcon(c, x, y, s, level, col) {
  for (let i = 0; i < 4; i++) {
    const h = (3 + 2 * i) * s, bx = x + i * 3 * s;
    if (level > i) c.fillRect(bx, y + 9 * s - h, 2 * s, h, col); else c.fillRect(bx, y + 8 * s, 2 * s, s, col);
  }
  if (level < 0) for (let k = 0; k < 5; k++) { c.fillRect(x + k * s, y + k * s, s, s, col); c.fillRect(x + (4 - k) * s, y + k * s, s, s, col); }
}
export function battIcon(c, x, y, s, pct, col) {
  box(c, x, y, 14 * s, 8 * s, col, s);
  c.fillRect(x + 14 * s, y + 2 * s, 2 * s, 4 * s, col);
  c.fillRect(x + 2 * s, y + 2 * s, jsRound((10 * s * clampd(pct, 0, 100)) / 100), 4 * s, col);
}
export function bigBar(c, x, y, w, h, pct) {
  box(c, x, y, w, h, true, 1);
  c.fillRect(x + 2, y + 2, jsRound(((w - 4) * clampd(pct, 0, 100)) / 100), h - 4 > 1 ? h - 4 : 1, true);
  for (let k = 1; k < 4; k++) c.fillRect(x + jsRound((w * k) / 4), y + h, 1, 3, true);
}
```

Nota: in C++ `(px + py + f) % 4` con `f` negativo dà un resto negativo; il firmware passa sempre `f ≥ 0`, il renderer anche (Task 10). In C++ `(double)iw / s.total` e qui `iw / st.total` sono la stessa divisione in virgola mobile.

- [ ] **Step 5: `qr.js`** — porting di `qrcode_ricmoo.cpp`

Traduzione funzione per funzione (stessi nomi in camelCase, stessa aritmetica intera, `uint8_t` → numeri con `& 0xff`): `getAlphanumeric`, `isAlphanumeric`, `isNumeric`, `bb_*` (bitbucket: `bb_initBuffer`, `bb_initGrid`, `bb_appendBits`, `bb_setBit`, `bb_invertBit`, `bb_getBit`), `applyMask`, `setFunctionModule`, `drawFormatBits`, `drawFunctionPatterns`, `drawCodewords`, `getPenaltyScore`, `rs_init`, `rs_getRemainder`, `encodeDataCodewords`, `performErrorCorrection`, `qrcode_initBytes`, `qrcode_initText`, `qrcode_getModule`, con le tabelle `NUM_ERROR_CORRECTION_CODEWORDS` e `NUM_ERROR_CORRECTION_BLOCKS` copiate per le versioni 1–5 (le altre non servono). Il testo entra come byte UTF-8 (`new TextEncoder().encode(text)`), come `strlen` in C++. In testa:

```js
// QR come firmware/src/gfx/qr.cpp + gfx/qr/qrcode_ricmoo.cpp (ricmoo/QRCode, licenza MIT: vedi
// firmware/src/gfx/qr/qrcode_ricmoo.LICENSE.txt). Solo versioni 1–5, correzione M. Stessa scelta del modo e della maschera.
const CAP_M = [14, 26, 42, 62, 84];
export function encode(text) {
  const bytes = new TextEncoder().encode(text);
  for (let v = 1; v <= 5; v++) {
    if (bytes.length > CAP_M[v - 1]) continue;
    const q = qrcodeInitText(v, 0 /* ECC_MEDIUM come in qrcode_ricmoo.h */, bytes);
    if (!q) return null;
    return { size: q.size, version: v, get: (x, y) => x >= 0 && y >= 0 && x < q.size && y < q.size && q.getModule(x, y) };
  }
  return null;
}
```

Controllare in `qrcode_ricmoo.h` il valore numerico di `ECC_MEDIUM` (e l'ordine `ECC_LOW, ECC_MEDIUM, …` usato come indice nelle tabelle) e usarlo identico. La parità di pixel del Task 10 (`focus` di `elementi.theme.json`: numerico, alfanumerico, byte con «è», testo lungo) è la prova vera del porting.

- [ ] **Step 6: Eseguire i test**

Run: `cd tools/webui-dev && node test-theme.mjs`
Expected: «temi: tutto ok».

- [ ] **Step 7: Commit**

```bash
git add service/internal/webui/static/assets/js/theme/surface.js service/internal/webui/static/assets/js/theme/prims.js service/internal/webui/static/assets/js/theme/qr.js tools/webui-dev/test-theme.mjs
git commit -m "Temi: superficie, primitive e QR in JS come nel firmware"
```

---
### Task 9: Valori, condizioni e abbassamento del JSON in JS

**Files:**
- Create: `service/internal/webui/static/assets/js/theme/values.js`, `service/internal/webui/static/assets/js/theme/lower.js`
- Modify: `tools/webui-dev/test-theme.mjs` (sezioni 6 e 7)

**Interfaces:**
- Consumes: niente di nuovo (logica come `core/theme_values.cpp`, `core/model.cpp`, `core/clock.cpp`, `core/workday.cpp`, e il Go dei Task 1–2).
- Produces:
  - `values.js`:
    - `makeModel(base, over = {})` → modello del renderer (fusione profonda di `over` su `base`, poi normalizzazione): `{now, local, frame, wallOk, ui: {sel, selUntil}, sys: {batt, usb, wifi, ssid, rssi, pc, pcOff, pcHost}, set: {night, game, workStart, workEnd, defAvatar}, sessions: [{name, state, detail, summary, since, ctx, subs, test, done, total, avatar}], work: {on, endLocal, fromLocal, doneUntilLocal}, stats: {tasks, linesAdd, linesDel, cost}, limits: {h5, d7} /* null = nessun dato */, pet: {known, on, level, xp, xpLevelStart, xpNext, energy, tired, wear}, co2 /* null = senza sensore */, bat: {cal: {valid, active, idle}, hasSleep, sleep, mix, trend}, fw}`. Nella forma dei dati di prova le sessioni hanno `ago` (ms) invece di `since`, e `sel` in radice (≥ 0 = selezionata da adesso per 15 s);
    - `VALUES` (`{nome → id}`, gli stessi ID di `values.go`), `STATES` (chiavi nell'ordine di `SessionState`);
    - `refIndex(m, screenKey, ref)`, `whenTrue(m, when)`, `refSession(m, cx, ref, row)`, `permWaiting(m)`, `pose(m, s)` (nome della posa), `petOn(m)`;
    - `valueText(id, cx, row)` (stringa), `condTrue(op, arg, cx, subject)`, `condsTrue(conds, cx, subject)`, `expand(segs, cx, row, upperAll)` (al massimo 255 byte UTF-8);
    - `cx` = `{m, now, local, frame, screen, ref}` (`screen` = chiave come nel JSON, `ref` = indice o −1);
    - `workFrac(m)`, `utf8Cut(s, maxBytes)`;
  - `lower.js`: `lowerTheme(json)` → `{id, name, screens: {chiave: {variants: [{when, ref, elems}]}}}` con gli stessi codici del binario (§5): elemento `{type, invert, x, y, w, h, conds: [[op, arg]], …}` e i campi propri `scale, pose, ref, acc, anim` (mascot), `size, weight, pix, align, lines, lh, upper, segs` (text; `segs` = `[{lit} | {val, filter}]`), `percent` (battery), `src, big` (bar), `sym`, `vertical, dotted` (line), `border, fill, radius` (rect, circle), `rowH, gap, pageSec, dots, row` (list). Gruppi sciolti, nascosti tolti, `clock` → text con il valore 0x80 o 0x81, predefiniti e riquadri calcolati come in `parse.go`. Si aspetta un tema già valido (lo controlla il servizio); su un campo sbagliato non lancia, mette il predefinito.

- [ ] **Step 1: Aggiungere le prove a `test-theme.mjs`** (import in testa)

```js
// 6. valori: gli stessi casi di firmware/test/test_theme_values
import * as V from '../../service/internal/webui/static/assets/js/theme/values.js';
import { lowerTheme } from '../../service/internal/webui/static/assets/js/theme/lower.js';

const NOW = 1791707400000;
const LOCAL = Date.UTC(2026, 9, 11, 10, 30, 5);  // ora locale come WallClock.localMs
const sess = (k, o = {}) => ({ name: 'sessione-' + k, state: 'working', detail: 'Bash: npm run test', ago: 12 * 60000, ctx: 42, done: 3, total: 7, ...o });
const base = { now: NOW, local: LOCAL, frame: 0, wallOk: true, sys: { batt: 78, usb: false, wifi: true, ssid: 'CasaWiFi', rssi: -58, pc: true }, sessions: [] };
const cxOf = (m, screen = 'home') => ({ m, now: m.now, local: m.local, frame: m.frame, screen, ref: V.refIndex(m, screen, 1) });
let m = V.makeModel(base, { sessions: [sess(0), sess(1, { state: 'permission' })] });
let cx = cxOf(m);
eq(V.valueText(0x00, cx), 'sessione-1', 'selezionata = urgente');
eq(V.valueText(0x01, cx), 'Chiede un permesso', 'stato');
eq(V.valueText(0x05, cx), '42%', 'contesto');
eq(V.valueText(0x06, cx), '3/7', 'task');
eq(V.valueText(0x07, cx), '12 min', 'da');
eq(V.valueText(0x08, cx), '', 'sub vuoto');
eq(V.valueText(0x40, cx, m.sessions[0]), 'sessione-0', 'riga');
eq(V.valueText(0x80, cx), '10:30', 'ora');
eq(V.valueText(0x81, cx), '10:30:05', 'ora con i secondi');
eq(V.valueText(0x82, cx), 'domenica 11 ottobre', 'data');
eq(V.valueText(0x85, cx), '1', 'in attesa');
eq(V.valueText(0x86, cx), '78%', 'batteria');
eq(V.valueText(0x8F, cx), '—', 'limiti senza dati');
m = V.makeModel(base, { sessions: [sess(0)], limits: { h5: 36.5, d7: null }, stats: { tasks: 4, linesAdd: 120, linesDel: 30, cost: 1.2 } });
cx = cxOf(m);
eq(V.valueText(0x8F, cx), '37%', 'limiti 5 h');
eq(V.valueText(0x8D, cx), '+120 −30', 'righe');
eq(V.valueText(0x8E, cx), '1,20 $', 'costo');
eq(V.valueText(0x03, cx), '', 'riassunto vuoto');
eq(V.valueText(0x04, cx), '3/7 task  ·  12 min  ·  Bash: npm run test', 'meta');
eq(V.valueText(0x80, cxOf(V.makeModel(base, { wallOk: false }))), '--:--', 'ora mai sincronizzata');
check(V.condTrue(2, 80, cx, m.sessions[0]) && !V.condTrue(2, 78, cx, m.sessions[0]), 'battBelow');
check(V.condTrue(10, 0, cx, m.sessions[0]) && !V.condTrue(10, 0, cx, null), 'state');
check(V.condTrue(11, 1, { ...cx, frame: -3 }, null), 'blink dispari con fotogramma negativo');
m = V.makeModel(base, { sessions: [sess(0, { name: 'àbc-ÈFG' })] });
cx = cxOf(m);
eq(V.expand([{ lit: 'Ciao ' }, { val: 0, filter: 1 }, { val: 0, filter: 2 }], cx, null, false), 'Ciao ÀBC-ÈFGàbc-èfg', 'filtri');
m = V.makeModel(base, { sessions: [sess(0, { summary: 'è'.repeat(63) })] });
cx = cxOf(m);
const big = V.expand([{ val: 3 }, { val: 3 }, { val: 3 }], cx, null, false);
check(new TextEncoder().encode(big).length <= 255 && !big.includes('�'), 'troncato su un confine UTF-8');

// 7. abbassamento: come parse.go
const fx = (n) => JSON.parse(fs.readFileSync(path.resolve(here, '../../protocol/themes/fixtures/' + n), 'utf8'));
const lp = lowerTheme(fx('prova.theme.json'));
eq(lp.screens.home.variants.length, 3, 'varianti');
eq(lp.screens.home.variants[0].when, 1, 'when none');
eq(lp.screens.home.variants[0].elems[2].type, 2, 'orologio → testo');
eq(lp.screens.home.variants[0].elems[2].segs[0].val, 0x80, 'orologio = {ora}');
eq(lp.screens.home.variants[2].elems[0].row.length, 5, 'righe della lista');
eq(lp.screens.home.variants[2].elems[0].row[0].ref, 2, 'mascotte in una riga: ref row');
const le = lowerTheme(fx('elementi.theme.json'));
const rect = le.screens.pet.variants[0].elems.find((e) => e.type === 10);
check(rect && rect.x === 100 && rect.y === 60 && rect.conds.length === 1 && rect.conds[0][0] === 10, 'gruppo sciolto');
eq(le.screens.pet.variants[0].elems.filter((e) => e.type === 10).length, 1, 'nascosto tolto');
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd tools/webui-dev && node test-theme.mjs`
Expected: FAIL (`Cannot find module …/theme/values.js`).

- [ ] **Step 3: `values.js`**

```js
// Valori, condizioni e sessione di riferimento dei temi come core/theme_values.cpp (docs/TEMI.md §3–§4) e le funzioni
// del modello che usano (stateText, lineFor, metaLine, ago, autonomyText, workLine, pose…).
export const STATES = ['working', 'permission', 'input', 'error', 'compact', 'done', 'idle'];
const INFO = {
  working: ['LAVORA', 'Sta lavorando', 3], permission: ['PERMESSO', 'Chiede un permesso', 6], input: ['ATTENDE TE', 'Aspetta una risposta', 5],
  error: ['ERRORE', 'Errore API', 4], compact: ['RIORDINA', 'Riordina la memoria', 3], done: ['FINITO', 'Turno completato', 2], idle: ['IN PAUSA', 'Sessione aperta', 1],
};
const SNAMES = ['nome', 'stato', 'attività', 'riassunto', 'meta', 'contesto', 'task', 'da', 'sub', 'test'];
const GNAMES = [['ora', 0x80], ['data', 0x82], ['giorno', 0x83], ['sessioni', 0x84], ['inattesa', 0x85], ['batteria', 0x86], ['autonomia', 0x87],
  ['wifi', 0x88], ['pc', 0x89], ['giornata.mancano', 0x8a], ['giornata.fine', 0x8b], ['oggi.task', 0x8c], ['oggi.righe', 0x8d], ['oggi.costo', 0x8e],
  ['limiti.5h', 0x8f], ['limiti.7g', 0x90], ['mascotte.livello', 0x91], ['mascotte.xp', 0x92], ['mascotte.energia', 0x93], ['co2', 0x94], ['versione', 0x95]];
export const VALUES = Object.fromEntries([...SNAMES.flatMap((n, i) => [['sessione.' + n, i], ['riga.' + n, 0x40 + i]]), ...GNAMES]);
const WEEKDAYS = ['domenica', 'lunedì', 'martedì', 'mercoledì', 'giovedì', 'venerdì', 'sabato'];
const MONTHS = ['gennaio', 'febbraio', 'marzo', 'aprile', 'maggio', 'giugno', 'luglio', 'agosto', 'settembre', 'ottobre', 'novembre', 'dicembre'];
const jsRound = (v) => Math.floor(v + 0.5);
const pad2 = (v) => (v < 10 ? '0' : '') + v;
const civil = (localMs) => { const d = new Date(localMs); return { y: d.getUTCFullYear(), mo: d.getUTCMonth() + 1, d: d.getUTCDate(), h: d.getUTCHours(), mi: d.getUTCMinutes(), s: d.getUTCSeconds(), wd: d.getUTCDay() }; };
const hmLocal = (l) => { const c = civil(l); return pad2(c.h) + ':' + pad2(c.mi); };
const minHm = (mins) => { mins = Math.max(0, mins); return pad2(Math.floor(mins / 60)) + ':' + pad2(mins % 60); };

const isObj = (v) => v && typeof v === 'object' && !Array.isArray(v);
function merge(a, b) {
  const out = { ...a };
  for (const [k, v] of Object.entries(b || {})) out[k] = isObj(v) && isObj(a[k]) ? merge(a[k], v) : v;
  return out;
}
export function makeModel(base, over = {}) {
  const s = merge(base, over);
  const now = s.now ?? 0;
  return {
    now, local: s.local ?? now, frame: s.frame ?? 0, wallOk: s.wallOk ?? true, fw: s.fw ?? '',
    ui: { sel: Math.max(0, s.sel ?? 0), selUntil: (s.sel ?? -1) >= 0 ? now + 15000 : 0 },
    sys: { batt: 78, usb: false, wifi: true, ssid: '', rssi: -58, pc: true, pcOff: false, pcHost: '', ...(s.sys || {}) },
    set: { night: false, game: false, workStart: 540, workEnd: 1080, defAvatar: 'goccia', ...(s.set || {}) },
    sessions: (s.sessions || []).slice(0, 6).map((x) => ({ name: '', state: 'idle', detail: '', summary: '', ctx: 0, subs: 0, test: null,
      done: 0, total: 0, avatar: '', ...x, since: now - (x.ago ?? 0) })),
    work: { on: false, endLocal: 0, fromLocal: 0, doneUntilLocal: 0, ...(s.work || {}) },
    stats: { tasks: 0, linesAdd: 0, linesDel: 0, cost: 0, ...(s.stats || {}) },
    limits: { h5: null, d7: null, ...(s.limits || {}) },
    pet: { known: false, on: false, level: 0, xp: 0, xpLevelStart: 0, xpNext: 0, energy: 0, tired: false, wear: '', ...(s.pet || {}) },
    co2: s.co2 ?? null,
    bat: { cal: { valid: false, active: 0, idle: 0 }, hasSleep: false, sleep: 0, mix: [0, 0, 0], trend: 0, ...(s.bat || {}) },
  };
}

const prio = (s) => INFO[s.state][2];
const isAttn = (s) => s.state === 'permission' || s.state === 'input' || s.state === 'error';
export const urgentIdx = (m) => { let b = 0; m.sessions.forEach((s, k) => { if (prio(s) > prio(m.sessions[b])) b = k; }); return b; };
const sIdx = (m) => (m.ui.selUntil > m.now ? m.ui.sel : urgentIdx(m));
export const petOn = (m) => m.set.game && m.pet.on;
export const permWaiting = (m) => m.sessions.filter((s) => s.state === 'permission').length;
export function refIndex(m, screen, ref) {
  const n = m.sessions.length;
  if (n <= 0) return -1;
  const k = ref === 0 ? urgentIdx(m) : screen === 'detail' ? m.ui.sel : sIdx(m);
  return Math.min(Math.max(k, 0), n - 1);
}
export function whenTrue(m, w) { const n = m.sessions.length; return w === 1 ? n === 0 : w === 2 ? n === 1 : w === 3 ? n >= 2 : true; }
export function refSession(m, cx, ref, row) {
  if (ref === 2) return row;
  if (ref === 3) return null;
  if (ref === 4) { if (row) return row; return cx.ref < 0 ? null : m.sessions[cx.ref]; }
  const k = refIndex(m, cx.screen, ref);
  return k < 0 ? null : m.sessions[k];
}
export function pose(m, s) {
  const calm = s.state === 'working' || s.state === 'done' || s.state === 'idle';
  if (m.limits.h5 !== null && m.limits.h5 >= 90 && calm) return 'tired';
  if (m.set.game && m.pet.on && m.pet.tired && calm) return 'tired';
  if (m.co2 !== null && m.co2 > 1000 && !isAttn(s)) return 'stuffy';
  return s.state === 'compact' ? 'compact' : s.state;
}
const stateText = (s) => (s.state === 'working' ? s.detail : s.state === 'done' && s.summary ? s.summary : INFO[s.state][1]);
const lineFor = (s) => (s.state === 'done' && s.summary ? s.summary : s.detail);
const roundDiv = (a, b) => Math.floor((2 * a + b) / (2 * b));
function ago(now, t) { const mins = Math.max(0, roundDiv(now - t, 60000)); return mins < 60 ? mins + ' min' : Math.floor(mins / 60) + ' h ' + pad2(mins % 60); }
function extras(s, sep) {
  const e = [];
  if (s.subs) e.push('+' + s.subs + (s.subs === 1 ? ' agente' : ' agenti'));
  if (s.test) e.push(s.test === 'ok' ? 'test ✓' : 'test ✗');
  if (s.ctx >= 80) e.push('contesto ' + s.ctx + '%');
  return e.join(sep);
}
function metaLine(s, now) {
  const sep = '  ·  ';
  let out = (s.total ? s.done + '/' + s.total + ' task' : 'senza task') + sep + ago(now, s.since);
  const ex = extras(s, sep);
  if (ex) out += sep + ex;
  return out + sep + lineFor(s);
}
const hoursText = (h) => (h >= 48 ? 'circa ' + jsRound(h / 24) + ' giorni' : 'circa ' + (h < 1 ? 1 : jsRound(h)) + ' ore');
function autonomyText(m) {
  const c = m.bat.cal, x = m.bat.mix, tot = x[0] + x[1] + x[2];
  const r = c.valid && tot > 0 ? (x[0] * c.active + x[1] * c.idle + x[2] * (m.bat.hasSleep ? m.bat.sleep : 0.3)) / tot : 0;
  if (c.valid && r > 0.01) return 'Autonomia: ' + hoursText(m.sys.batt / r) + ' con il tuo uso';
  if (!m.sys.usb && m.bat.trend > 0.01) return 'Autonomia: ' + hoursText(m.sys.batt / m.bat.trend) + ' (dalla scarica)';
  return m.sys.usb ? 'Autonomia: calibra per una stima' : 'Autonomia: stima in corso';
}
const workDone = (m) => m.work.doneUntilLocal > m.local;
function workLine(m) {
  if (m.work.on) {
    const d = m.work.endLocal - m.local, n = d <= 0 ? 0 : Math.floor((d + 59999) / 60000);
    return n >= 60 ? Math.floor(n / 60) + ' h ' + pad2(n % 60) : n + ' min';
  }
  return workDone(m) ? 'finita' : '';
}
const DAY = 86400000;
export function workFrac(m) {
  const w = m.work;
  let a = w.endLocal - (((w.endLocal % DAY) + DAY) % DAY) + m.set.workStart * 60000;
  if (a >= w.endLocal) a = w.fromLocal;
  if (w.endLocal <= a) return 1;
  return Math.min(1, Math.max(0, (m.local - a) / (w.endLocal - a)));
}
const pct = (v) => (v === null ? '—' : jsRound(v) + '%');

function sessionValue(k, s, now) {
  switch (k) {
    case 0: return s.name;
    case 1: return stateText(s);
    case 2: return lineFor(s);
    case 3: return s.summary;
    case 4: return metaLine(s, now);
    case 5: return s.ctx + '%';
    case 6: return s.total ? s.done + '/' + s.total : '';
    case 7: return ago(now, s.since);
    case 8: return s.subs ? String(s.subs) : '';
    case 9: return s.test ? (s.test === 'ok' ? '✓' : '✗') : '';
  }
  return '';
}

export function valueText(id, cx, row = null) {
  const m = cx.m;
  if (id < 0x80) {
    const s = id >= 0x40 ? row : cx.ref >= 0 ? m.sessions[cx.ref] : null;
    return s ? utf8Cut(sessionValue(id & 0x3f, s, cx.now), 255) : '';
  }
  const c = civil(cx.local);
  switch (id) {
    case 0x80: return m.wallOk ? hmLocal(cx.local) : '--:--';
    case 0x81: return m.wallOk ? pad2(c.h) + ':' + pad2(c.mi) + ':' + pad2(c.s) : '--:--:--';
    case 0x82: return m.wallOk ? WEEKDAYS[c.wd] + ' ' + c.d + ' ' + MONTHS[c.mo - 1] : '';
    case 0x83: return m.wallOk ? WEEKDAYS[c.wd] : '';
    case 0x84: return String(m.sessions.length);
    case 0x85: return String(permWaiting(m));
    case 0x86: return jsRound(m.sys.batt) + '%';
    case 0x87: return autonomyText(m);
    case 0x88: return m.sys.wifi ? m.sys.ssid : '';
    case 0x89: return m.sys.pcHost || 'calamaio.local';
    case 0x8a: return workLine(m);
    case 0x8b: return m.work.on || workDone(m) ? hmLocal(m.work.endLocal) : minHm(m.set.workEnd);
    case 0x8c: return String(m.stats.tasks);
    case 0x8d: return '+' + m.stats.linesAdd + ' −' + m.stats.linesDel;
    case 0x8e: return m.stats.cost.toFixed(2).replace('.', ',') + ' $';  // come snprintf("%.2f") tranne i pari merito esatti
    case 0x8f: return pct(m.limits.h5);
    case 0x90: return pct(m.limits.d7);
    case 0x91: return m.pet.known ? String(m.pet.level) : '—';
    case 0x92: return m.pet.known ? m.pet.xp - m.pet.xpLevelStart + '/' + (m.pet.xpNext - m.pet.xpLevelStart) + ' XP' : '';
    case 0x93: return m.pet.known ? m.pet.energy + '%' : '';
    case 0x94: return m.co2 !== null ? jsRound(m.co2) + ' ppm' : '';
    case 0x95: return m.fw;
  }
  return '';
}

export function condTrue(op, arg, cx, subject) {
  const m = cx.m, want = arg !== 0;
  switch (op) {
    case 1: return (permWaiting(m) > 0) === want;
    case 2: return m.sys.batt < arg;
    case 3: return m.sys.usb === want;
    case 4: return m.set.night === want;
    case 5: return m.sys.pc === want;
    case 6: return m.work.on === want;
    case 7: return petOn(m) === want;
    case 8: return m.sessions.length >= arg;
    case 9: return m.sessions.length <= arg;
    case 10: return !!subject && STATES.indexOf(subject.state) === arg;
    case 11: return (((cx.frame % 2) + 2) % 2) === arg;
  }
  return false;
}
export const condsTrue = (conds, cx, subject) => conds.every(([op, arg]) => condTrue(op, arg, cx, subject));

const mapCase = (s, up) => Array.from(s, (ch) => {
  const c = ch.charCodeAt(0);
  if (up && ((c >= 0x61 && c <= 0x7a) || (c >= 0xe0 && c <= 0xfe && c !== 0xf7))) return String.fromCharCode(c - 0x20);
  if (!up && ((c >= 0x41 && c <= 0x5a) || (c >= 0xc0 && c <= 0xde && c !== 0xd7))) return String.fromCharCode(c + 0x20);
  return ch;
}).join('');
export function utf8Cut(s, maxBytes) {
  let n = 0, out = '';
  for (const ch of s) { const c = ch.codePointAt(0), k = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4; if (n + k > maxBytes) break; n += k; out += ch; }
  return out;
}
export function expand(segs, cx, row, upperAll) {
  let out = '';
  for (const sg of segs) {
    let p = sg.lit !== undefined ? sg.lit : valueText(sg.val, cx, row);
    if (sg.filter === 1) p = mapCase(p, true);
    else if (sg.filter === 2) p = mapCase(p, false);
    out += utf8Cut(p, 255 - new TextEncoder().encode(out).length);
  }
  return upperAll ? mapCase(out, true) : out;
}
```

Note di porting da tenere identiche al C++: `ago` usa `roundDiv` (arrotondamento di `Math.round` su interi); `Sb::i` scrive interi senza separatori; nel C++ `valueText` di un valore di sessione scrive in un buffer da 256 byte, qui `utf8Cut(…, 255)`.

- [ ] **Step 4: `lower.js`**

```js
// Abbassa un tema JSON (docs/TEMI.md §1–§4) nella forma che disegna render.js, con gli stessi codici del binario (§5)
// e le stesse regole di service/internal/theme/parse.go (predefiniti, gruppi sciolti, orologio → testo). Tema già valido.
import { VALUES } from './values.js';

const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'];
const STATES = ['working', 'permission', 'input', 'error', 'compact', 'done', 'idle'];
const SYMS = ['plug', 'bell', 'moon', 'lock', 'bolt', 'dot', 'glass', 'gauge'];
const SOURCES = ['context', 'tasks', 'workday', 'battery', 'limit5h', 'limit7d'];
const TYPES = { mascot: 1, text: 2, clock: 2, battery: 3, wifi: 4, stateIcon: 5, bar: 6, qr: 7, sym: 8, line: 9, rect: 10, circle: 11, list: 12 };
const WHEN = { always: 0, none: 1, one: 2, many: 3 };
const BOOL = { perm: 1, charging: 3, night: 4, pc: 5, workday: 6, game: 7 };

export function parseTemplate(s) {
  const out = [];
  let lit = '';
  const flush = () => { if (lit) out.push({ lit }); lit = ''; };
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '{' && s[i + 1] === '{') { lit += '{'; i++; continue; }
    if (c === '}' && s[i + 1] === '}') { lit += '}'; i++; continue; }
    if (c === '{') {
      const j = s.indexOf('}', i);
      if (j < 0) { lit += s.slice(i); break; }
      const [name, filter] = s.slice(i + 1, j).split('|');
      flush();
      out.push({ val: VALUES[name] ?? 0x84, filter: filter === 'maiuscolo' ? 1 : filter === 'minuscolo' ? 2 : 0 });
      i = j;
      continue;
    }
    lit += c;
  }
  flush();
  return out;
}

function conds(list) {
  const out = [];
  for (const c of list || []) {
    const [k, v] = Object.entries(c)[0] || [];
    if (k in BOOL) out.push([BOOL[k], v ? 1 : 0]);
    else if (k === 'battBelow') out.push([2, v]);
    else if (k === 'sessions') { if (v.min !== undefined) out.push([8, v.min]); if (v.max !== undefined) out.push([9, v.max]); }
    else if (k === 'state') out.push([10, Math.max(0, STATES.indexOf(v))]);
    else if (k === 'blink') out.push([11, v === 'odd' ? 1 : 0]);
  }
  return out;
}

const refCode = (r, inRow) => ({ urgent: 0, selected: 1, row: 2, nobody: 3 }[r] ?? (inRow ? 2 : 4));

function element(j, inRow, dx, dy, inherited) {
  if (j.hidden) return [];
  const cs = inherited.concat(conds(j.visibleIf));
  const x = (j.x || 0) + dx, y = (j.y || 0) + dy;
  if (j.type === 'group') return (j.children || []).flatMap((c) => element(c, inRow, x, y, cs));
  const e = { type: TYPES[j.type], invert: !!j.invert, x, y, w: j.w || 0, h: j.h || 0, conds: cs };
  const sc = (def) => j.scale ?? def;
  switch (j.type) {
    case 'mascot':
      e.scale = sc(1); e.w = 28 * e.scale; e.h = 25 * e.scale;
      e.pose = !j.pose || j.pose === 'auto' ? 255 : POSES.indexOf(j.pose);
      e.ref = refCode(j.ref, inRow); e.acc = j.accessory !== false; e.anim = j.animated !== false;
      break;
    case 'text': case 'clock': {
      const f = j.font || { family: 'ui', size: 10, weight: 500 };
      e.size = f.size; e.weight = f.weight; e.pix = f.family === 'pix';
      e.align = ['left', 'center', 'right'].indexOf(j.align || 'left');
      e.lines = j.type === 'text' ? j.lines ?? 1 : 1; e.lh = j.lineHeight ?? f.size + 2; e.upper = !!j.upper;
      e.segs = j.type === 'text' ? parseTemplate(j.text || '') : [{ val: j.format === 'hms' ? 0x81 : 0x80, filter: 0 }];
      e.h = e.lines * e.lh;
      break;
    }
    case 'battery': e.scale = sc(1); e.w = 16 * e.scale; e.h = 8 * e.scale; e.percent = !!j.percent; break;
    case 'wifi': e.scale = sc(1); e.w = 11 * e.scale; e.h = 9 * e.scale; break;
    case 'stateIcon': e.scale = sc(1); e.w = e.h = 7 * e.scale; e.ref = refCode(j.ref, inRow); break;
    case 'sym': e.scale = sc(1); e.w = e.h = 7 * e.scale; e.sym = Math.max(0, SYMS.indexOf(j.sym)); break;
    case 'bar': e.src = Math.max(0, SOURCES.indexOf(j.source)); e.big = (j.style || 'big') === 'big'; break;
    case 'qr': e.segs = parseTemplate(j.text || ''); break;
    case 'line': e.vertical = j.dir === 'v'; e.dotted = j.style === 'dotted'; break;
    case 'rect': e.border = j.border ?? 1; e.fill = !!j.fill; e.radius = j.radius || 0; break;
    case 'circle': e.border = j.border ?? 1; e.fill = !!j.fill; break;
    case 'list':
      e.rowH = j.rowH; e.gap = j.gap || 0; e.pageSec = j.pageSec ?? 5; e.dots = j.pageDots !== false;
      e.row = (j.row || []).flatMap((c) => element(c, true, 0, 0, []));
      break;
  }
  return [e];
}

export function lowerTheme(t) {
  const screens = {};
  for (const [k, l] of Object.entries(t.screens || {})) {
    screens[k] = { variants: (l.variants || []).map((v) => ({ when: WHEN[v.when] ?? 0, ref: v.ref === 'urgent' ? 0 : 1,
      elems: (v.elements || []).flatMap((e) => element(e, false, 0, 0, [])) })) };
  }
  return { id: t.id, name: t.name, screens };
}
```

Controllo incrociato col Go: in `parse.go` le righe di una lista ricevono `dx, dy = 0` e nessuna condizione ereditata (le condizioni della lista valgono per la lista intera); qui `element(c, true, 0, 0, [])` fa lo stesso. Un gruppo dentro una riga resta dentro la riga (`inRow` passa ai figli).

- [ ] **Step 5: Eseguire i test**

Run: `cd tools/webui-dev && node test-theme.mjs`
Expected: «temi: tutto ok».

- [ ] **Step 6: Commit**

```bash
git add service/internal/webui/static/assets/js/theme/values.js service/internal/webui/static/assets/js/theme/lower.js tools/webui-dev/test-theme.mjs
git commit -m "Temi: valori, condizioni e abbassamento del JSON in JS"
```

---
### Task 10: Renderer JS e prova di parità C++/JS

**Files:**
- Create: `service/internal/webui/static/assets/js/theme/render.js`
- Create: `protocol/themes/scenarios.json`
- Create: `tools/golden/theme-golden.mjs`
- Create (generato): `firmware/test/test_theme_parity/golden.json`
- Test: `firmware/test/test_theme_parity/test_main.cpp`, `service/internal/webui/theme_parity_test.go`, `tools/webui-dev/test-theme.mjs` (sezione 8)

**Interfaces:**
- Consumes: Task 7 (`T`, `wrapLines`, `jsRound`), Task 8 (`prims.js`, `qr.js`, `Swap`, `Null`, `Bitmap`, `crc32`), Task 9 (`values.js`, `lowerTheme`), `assets/js/accessory.js` (`renderWith`); lato C++: Task 3 (`findScreen`, `screenName`), Task 5 (`drawThemeBlock`).
- Produces:
  - `render.js`: `renderScreen(lowered, key, m, surf, assets) → {drawn, nextChange, hasMascot, mx, my, mw, mh}` (stessi campi di `ThemeDrawInfo`); `assets = {avatar(id) → avatar JSON con le ancore | null, accessory(id) → accessorio JSON | null}`;
  - `scenarios.json`: `{base, scenarios: [{name, extends?, …}]}` nella forma dei dati di prova di `makeModel`;
  - `golden.json`: `{cases: [{theme, screen, scenario, drawn, next, mascot: [x, y, w, h] | null, crc}]}`;
  - comando `node tools/golden/theme-golden.mjs [--check] [--pbm DIR]`.

- [ ] **Step 1: Scrivere `protocol/themes/scenarios.json`**

```json
{
  "base": {
    "now": 1791707400000, "local": 1791714600000, "frame": 0, "wallOk": true, "fw": "",
    "sys": { "batt": 78, "usb": false, "wifi": true, "ssid": "CasaWiFi", "rssi": -58, "pc": true, "pcOff": false, "pcHost": "" },
    "set": { "night": false, "game": false, "workStart": 540, "workEnd": 1080, "defAvatar": "goccia" },
    "sessions": [], "stats": { "tasks": 4, "linesAdd": 120, "linesDel": 30, "cost": 1.2 }, "limits": { "h5": null, "d7": null }
  },
  "scenarios": [
    { "name": "zero" },
    { "name": "una", "sessions": [
      { "name": "api-server", "state": "working", "detail": "Bash: npm run test", "ago": 720000, "ctx": 42, "done": 3, "total": 7, "avatar": "gatto" } ] },
    { "name": "sei", "frame": 1, "limits": { "h5": 37, "d7": 12 }, "sessions": [
      { "name": "api-server", "state": "working", "detail": "Bash: npm run test", "ago": 720000, "ctx": 42, "done": 3, "total": 7, "avatar": "gatto" },
      { "name": "web-app", "state": "permission", "detail": "Edit: src/app.ts", "ago": 60000, "ctx": 81, "subs": 2, "avatar": "demone" },
      { "name": "firmware-esp32-progetto-lungo", "state": "input", "detail": "Quale scheda uso per la prova di domani mattina?", "ago": 4500000, "avatar": "calamaro" },
      { "name": "docs-site", "state": "done", "summary": "Guida di installazione aggiornata e 4 link rotti corretti.", "ago": 300000, "done": 6, "total": 6, "test": "ok" },
      { "name": "ricerca", "state": "error", "detail": "Errore API 529", "ago": 30000, "test": "ko" },
      { "name": "note", "state": "idle", "ago": 7200000 } ] },
    { "name": "sei-pagina2", "extends": "sei", "now": 1791707403000, "local": 1791714603000, "frame": 2 },
    { "name": "selezione", "extends": "sei", "sel": 2 },
    { "name": "batteria-scarica", "sys": { "batt": 9, "wifi": false }, "bat": { "trend": 1.5 }, "sessions": [
      { "name": "api-server", "state": "compact", "detail": "Riordino", "ago": 1000 } ] },
    { "name": "notte", "frame": 1, "set": { "night": true }, "sys": { "usb": true },
      "work": { "on": true, "endLocal": 1791722100000, "fromLocal": 1791703800000 }, "sessions": [
      { "name": "uno", "state": "idle", "ago": 600000 }, { "name": "due", "state": "working", "detail": "Read: README.md", "ago": 61000, "done": 1, "total": 3 } ] },
    { "name": "pc-spento", "sys": { "pc": false, "pcOff": true } },
    { "name": "gioco", "co2": 620, "set": { "game": true },
      "pet": { "known": true, "on": true, "level": 3, "xp": 132, "xpLevelStart": 66, "xpNext": 483, "energy": 80, "tired": false, "wear": "cappellino" },
      "sessions": [ { "name": "api-server", "state": "done", "summary": "Fatto", "ago": 90000, "done": 2, "total": 2 } ] },
    { "name": "senza-ora", "wallOk": false, "sessions": [ { "name": "solo", "state": "working", "detail": "Bash: make", "ago": 5000 } ] }
  ]
}
```

Regola di fusione (identica in JS e C++): base, poi lo scenario di `extends` (se c'è), poi lo scenario; gli oggetti si fondono campo per campo, gli array (`sessions`) e i valori si sostituiscono.

- [ ] **Step 2: Scrivere il test C++ che fallisce**

`firmware/test/test_theme_parity/test_main.cpp`:

```cpp
// Parità C++/JS dei temi (spec §7): per ogni tema di prova × schermata × scenario l'interprete C++ (sul CTHM compilato
// dal servizio) disegna esattamente come render.js (sul JSON). Il golden lo scrive tools/golden/theme-golden.mjs.
// In caso di differenza scrive il PBM del firmware in firmware/.pio/theme-parity/ (confronto con --pbm del generatore).
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/strbuf.h"
#include "core/theme_format.h"
#include "gfx/avatar.h"
#include "gfx/canvas.h"
#include "ui/theme_draw.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  return k != std::string::npos ? f.substr(0, k) : "../";
}
static std::string readText(const std::string &p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static SessionState stateOf(const char *k) {
  for (int i = 0; i < int(SessionState::Count); i++)
    if (!strcmp(kStates[i].key, k)) return SessionState(i);
  return SessionState::Idle;
}

// Stessa normalizzazione di makeModel() in values.js: si parte da zero, poi i campi presenti.
static void apply(Model &m, Ms &now, int64_t &local, int &frame, JsonVariantConst o) {
  if (!o["now"].isNull()) now = o["now"].as<long long>();
  if (!o["local"].isNull()) local = o["local"].as<long long>();
  if (!o["frame"].isNull()) frame = o["frame"].as<int>();
  if (!o["wallOk"].isNull()) m.sys.wallOk = o["wallOk"].as<bool>();
  if (!o["sel"].isNull()) { m.ui.sel = o["sel"].as<int>(); m.ui.selUntil = now + 15000; }
  JsonVariantConst y = o["sys"];
  if (!y["batt"].isNull()) m.sys.batt = y["batt"].as<double>();
  if (!y["usb"].isNull()) m.sys.usb = y["usb"].as<bool>();
  if (!y["wifi"].isNull()) m.sys.wifi = y["wifi"].as<bool>();
  if (!y["ssid"].isNull()) scopy(m.sys.ssid, y["ssid"].as<const char *>());
  if (!y["rssi"].isNull()) m.sys.rssi = y["rssi"].as<int>();
  if (!y["pc"].isNull()) m.sys.pc = y["pc"].as<bool>();
  if (!y["pcOff"].isNull()) m.sys.pcOff = y["pcOff"].as<bool>();
  if (!y["pcHost"].isNull()) scopy(m.sys.pcHost, y["pcHost"].as<const char *>());
  JsonVariantConst s = o["set"];
  if (!s["night"].isNull()) m.set.night = s["night"].as<bool>();
  if (!s["game"].isNull()) m.set.game = s["game"].as<bool>();
  if (!s["workStart"].isNull()) m.set.workStart = s["workStart"].as<int>();
  if (!s["workEnd"].isNull()) m.set.workEnd = s["workEnd"].as<int>();
  if (!s["defAvatar"].isNull()) setAvatarRef(m.set.defAvatar, s["defAvatar"].as<const char *>());
  if (o["sessions"].is<JsonArrayConst>()) {
    m.nSessions = 0;
    for (JsonVariantConst x : o["sessions"].as<JsonArrayConst>()) {
      if (m.nSessions >= kMaxSessions) break;
      Session &z = m.sessions[m.nSessions++];
      memset(&z, 0, sizeof z);
      scopy(z.name, x["name"] | "");
      z.state = stateOf(x["state"] | "idle");
      scopy(z.detail, x["detail"] | "");
      scopy(z.summary, x["summary"] | "");
      z.since = now - (x["ago"] | 0LL);
      z.ctx = x["ctx"] | 0;
      z.subs = x["subs"] | 0;
      const char *t = x["test"] | "";
      z.test = !strcmp(t, "ok") ? TestResult::Ok : !strcmp(t, "ko") ? TestResult::Ko : TestResult::None;
      z.done = x["done"] | 0;
      z.total = x["total"] | 0;
      setAvatarRef(z.avatar, x["avatar"] | "");
    }
  }
  JsonVariantConst w = o["work"];
  if (!w["on"].isNull()) m.work.on = w["on"].as<bool>();
  if (!w["endLocal"].isNull()) m.work.endLocal = w["endLocal"].as<long long>();
  if (!w["fromLocal"].isNull()) m.work.fromLocal = w["fromLocal"].as<long long>();
  if (!w["doneUntilLocal"].isNull()) m.work.doneUntilLocal = w["doneUntilLocal"].as<long long>();
  JsonVariantConst st = o["stats"];
  if (!st["tasks"].isNull()) m.stats.tasks = st["tasks"].as<int>();
  if (!st["linesAdd"].isNull()) m.stats.linesAdd = st["linesAdd"].as<long>();
  if (!st["linesDel"].isNull()) m.stats.linesDel = st["linesDel"].as<long>();
  if (!st["cost"].isNull()) m.stats.cost = st["cost"].as<double>();
  JsonVariantConst l = o["limits"];
  // null = nessun dato: è già così dopo zero(), quindi basta il numero
  if (l["h5"].is<double>()) { m.limits.hasH5 = true; m.limits.h5 = l["h5"].as<double>(); }
  if (l["d7"].is<double>()) { m.limits.hasD7 = true; m.limits.d7 = l["d7"].as<double>(); }
  JsonVariantConst p = o["pet"];
  if (!p.isNull()) {
    m.pet.known = p["known"] | false;
    m.pet.on = p["on"] | false;
    m.pet.level = p["level"] | 0;
    m.pet.xp = p["xp"] | 0;
    m.pet.xpLevelStart = p["xpLevelStart"] | 0;
    m.pet.xpNext = p["xpNext"] | 0;
    m.pet.energy = p["energy"] | 0;
    m.pet.tired = p["tired"] | false;
    setAvatarRef(m.pet.wear, p["wear"] | "");
  }
  if (o["co2"].is<double>()) { m.acc = true; m.air.co2 = o["co2"].as<double>(); }
  JsonVariantConst b = o["bat"];
  if (!b["trend"].isNull()) m.bat.trend = b["trend"].as<double>();
}

static void zero(Model &m, Ms now, int64_t local) {
  newModel(m, now, WallClock{local});
  m.nSessions = 0;
  memset(&m.limits, 0, sizeof m.limits);
  memset(&m.stats, 0, sizeof m.stats);
  memset(&m.work, 0, sizeof m.work);
  memset(&m.pet, 0, sizeof m.pet);
  memset(&m.bat, 0, sizeof m.bat);
  m.acc = false;
  m.ui.sel = 0;
  m.ui.selUntil = 0;
}

static void writePbm(const std::string &name, const Canvas<250, 122> &cv) {
  const std::string dir = repoRoot() + "firmware/.pio/theme-parity";
  std::filesystem::create_directories(dir);
  std::ofstream out(dir + "/" + name + "-cpp.pbm", std::ios::binary);
  out << "P4\n250 122\n";
  out.write(reinterpret_cast<const char *>(cv.data()), Canvas<250, 122>::kBytes);
}

static void test_parity() {
  static JsonDocument sc, gold;
  TEST_ASSERT_FALSE(deserializeJson(sc, readText(repoRoot() + "protocol/themes/scenarios.json")));
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(gold, readText(repoRoot() + "firmware/test/test_theme_parity/golden.json")),
                            "golden.json mancante: node tools/golden/theme-golden.mjs");
  static Model m;
  static Canvas<250, 122> cv;
  int n = 0, bad = 0;
  for (JsonVariantConst c : gold["cases"].as<JsonArrayConst>()) {
    const std::string theme = c["theme"].as<const char *>(), screen = c["screen"].as<const char *>(), name = c["scenario"].as<const char *>();
    const std::string bin = readText(repoRoot() + "protocol/themes/fixtures/" + theme + ".cthm");
    theme::Screen key = theme::Screen::None;
    for (int s = 1; s < int(theme::Screen::Count); s++)
      if (screen == theme::screenName(theme::Screen(s))) key = theme::Screen(s);
    const uint8_t *blk;
    uint16_t len;
    TEST_ASSERT_TRUE(theme::findScreen(reinterpret_cast<const uint8_t *>(bin.data()), bin.size(), key, blk, len));
    JsonVariantConst over;
    for (JsonVariantConst s : sc["scenarios"].as<JsonArrayConst>())
      if (name == (s["name"] | "")) over = s;
    Ms now = sc["base"]["now"].as<long long>();
    int64_t local = sc["base"]["local"].as<long long>();
    int frame = 0;
    zero(m, now, local);
    apply(m, now, local, frame, sc["base"]);
    if (!over["extends"].isNull())
      for (JsonVariantConst s : sc["scenarios"].as<JsonArrayConst>())
        if (!strcmp(s["name"] | "", over["extends"].as<const char *>())) apply(m, now, local, frame, s);
    apply(m, now, local, frame, over);
    cv.clear(false);
    CanvasSurface<250, 122> surf(cv);
    ui::ThemeDrawInfo info;
    ui::drawThemeBlock(surf, kHeltec213, m, frame, now, WallClock{local}, key, blk, len, &info);
    const uint32_t crc = avatarCrc32(cv.data(), Canvas<250, 122>::kBytes);
    const bool same = crc == c["crc"].as<uint32_t>() && info.drawn == c["drawn"].as<bool>() &&
                      info.nextChange == c["next"].as<long long>() &&
                      (c["mascot"].isNull() ? !info.hasMascot
                                            : info.hasMascot && info.mx == c["mascot"][0].as<int>() && info.my == c["mascot"][1].as<int>() &&
                                                  info.mw == c["mascot"][2].as<int>() && info.mh == c["mascot"][3].as<int>());
    n++;
    if (!same) {
      bad++;
      const std::string id = theme + "-" + screen + "-" + name;
      writePbm(id, cv);
      printf("diverso: %s (crc %08x, drawn %d, next %lld)\n", id.c_str(), unsigned(crc), int(info.drawn), (long long)info.nextChange);
    }
  }
  printf("parità dei temi: %d casi, %d diversi\n", n, bad);
  TEST_ASSERT_TRUE(n > 100);
  TEST_ASSERT_EQUAL(0, bad);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parity);
  return UNITY_END();
}
```

- [ ] **Step 3: Eseguire e vedere fallire**

Run: `cd firmware && pio test -e native -f test_theme_parity`
Expected: FAIL («golden.json mancante: node tools/golden/theme-golden.mjs»).

- [ ] **Step 4: Scrivere `render.js`** (stesso algoritmo di `ui/theme_draw.cpp`, docs/TEMI.md §6)

```js
// Renderer dei temi (docs/TEMI.md §6): lo stesso algoritmo dell'interprete della scheda (firmware/src/ui/theme_draw.cpp)
// sul tema abbassato da lower.js. La parità pixel per pixel la prova firmware/test/test_theme_parity.
import { T, wrapLines, jsRound } from './text.js';
import * as P from './prims.js';
import { encode as qrEncode } from './qr.js';
import { Swap } from './surface.js';
import * as V from './values.js';
import { renderWith } from '../accessory.js';

const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'];
const clampi = (v, a, b) => (v < a ? a : v > b ? b : v);

function inRound(px, py, x, y, w, h, r) {
  if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h) return false;
  const dx = Math.min(px - x, x + w - 1 - px), dy = Math.min(py - y, y + h - 1 - py);
  return !(dx < r && dy < r && dx + dy < r);
}
function rect(c, x, y, w, h, t, fill, r, ink) {
  for (let py = clampi(y, 0, 122); py < clampi(y + h, 0, 122); py++)
    for (let px = clampi(x, 0, 250); px < clampi(x + w, 0, 250); px++) {
      if (!inRound(px, py, x, y, w, h, r)) continue;
      const border = t > 0 && !inRound(px, py, x + t, y + t, w - 2 * t, h - 2 * t, Math.max(0, r - t));
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}
const inCircle = (px, py, cx2, cy2, d) => { const dx = 2 * px + 1 - cx2, dy = 2 * py + 1 - cy2; return d > 0 && dx * dx + dy * dy <= d * d; };
function circle(c, x, y, w, h, t, fill, ink) {
  const d = Math.min(w, h), cx2 = 2 * x + d, cy2 = 2 * y + d;
  for (let py = clampi(y, 0, 122); py < clampi(y + d, 0, 122); py++)
    for (let px = clampi(x, 0, 250); px < clampi(x + d, 0, 250); px++) {
      if (!inCircle(px, py, cx2, cy2, d)) continue;
      const border = t > 0 && !inCircle(px, py, cx2, cy2, d - 2 * t);
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}

function avatarOf(m, assets, s) {
  const id = (s && s.avatar) || m.set.defAvatar || 'goccia';
  return assets.avatar(id) || assets.avatar('goccia');
}
function mascot(c, m, assets, s, ox, oy, sc, pose, f, acc, ink) {
  const av = avatarOf(m, assets, s);
  const base = renderWith(av, null, pose, f);
  if (!base) return;
  // only = la mascotte senza accessorio: si ridisegnano solo i punti cambiati, '#' inchiostro e il resto carta (accOverlay)
  const paint = (rows, only) => rows.forEach((row, y) => { for (let x = 0; x < row.length; x++) {
    const ch = row[x];
    if (only ? only[y][x] === ch : ch !== '#' && ch !== '+') continue;
    c.fillRect(ox + x * sc, oy + y * sc, sc, sc, ch === '#' ? ink : !ink);
  } });
  paint(base, null);
  if (!acc || !V.petOn(m) || !m.pet.wear) return;
  const a = assets.accessory(m.pet.wear);
  const g = a && renderWith(av, a, pose, f);
  if (g) paint(g, base);
}

function barPct(m, src, s) {
  switch (src) {
    case 0: return s ? s.ctx : 0;
    case 1: return s && s.total ? (100 * s.done) / s.total : 0;
    case 2: return m.work.on ? V.workFrac(m) * 100 : 0;
    case 3: return m.sys.batt;
    case 4: return m.limits.h5 ?? 0;
    default: return m.limits.d7 ?? 0;
  }
}

function wake(info, t) { if (t > 0 && (!info.nextChange || t < info.nextChange)) info.nextChange = t; }
function box(info, x, y, w, h) {
  if (!info.hasMascot) { Object.assign(info, { hasMascot: true, mx: x, my: y, mw: w, mh: h }); return; }
  const x1 = Math.max(x + w, info.mx + info.mw), y1 = Math.max(y + h, info.my + info.mh);
  info.mx = Math.min(x, info.mx); info.my = Math.min(y, info.my); info.mw = x1 - info.mx; info.mh = y1 - info.my;
}

function elem(R, e, ox, oy, row) {
  const { cx, m } = R;
  const subject = row || (cx.ref >= 0 ? m.sessions[cx.ref] : null);
  if (!V.condsTrue(e.conds, cx, subject)) return;
  const x = e.x + ox, y = e.y + oy, ink = !e.invert;
  const c = e.invert ? new Swap(R.surf) : R.surf;
  switch (e.type) {
    case 1: {
      const s = V.refSession(m, cx, e.ref, row);
      const pose = e.pose === 255 ? (s ? V.pose(m, s) : 'idle') : POSES[e.pose];
      mascot(R.surf, m, R.assets, s, x, y, e.scale, pose, e.anim ? R.f : 0, e.acc, ink);
      box(R.info, x, y, 28 * e.scale, 25 * e.scale);
      break;
    }
    case 2: {
      const t = V.expand(e.segs, cx, row, e.upper);
      if (!t) break;
      const ax = e.align === 1 ? x + e.w / 2 : e.align === 2 ? x + e.w : x;
      if (e.segs.some((s) => s.val === 0x81)) wake(R.info, cx.now + (1000 - (((cx.local % 1000) + 1000) % 1000)));
      const o = { weight: e.weight, pix: e.pix, align: e.align, ink, max: e.w };
      if (e.lines <= 1) { T(R.surf, t, ax, y, e.size, o); break; }
      wrapLines(t, e.lines, e.size, e.w, e.weight, e.pix).forEach((l, i) => T(R.surf, l, ax, y + i * e.lh, e.size, o));
      break;
    }
    case 3:
      P.battIcon(c, x, y, e.scale, m.sys.batt, true);
      if (e.percent) T(R.surf, jsRound(m.sys.batt) + '%', x + 16 * e.scale + 3, y + Math.trunc((8 * e.scale - 8) / 2), 8, { weight: 400, pix: true, ink });
      break;
    case 4: P.wifiIcon(c, x, y, e.scale, m.sys.wifi ? rssiBars(m.sys.rssi) : -1, true); break;
    case 5: { const s = V.refSession(m, cx, e.ref, row); if (s) P.iconForState(c, s.state, x, y, e.scale, R.f, true); break; }
    case 6: {
      const s = row || subject, pct = barPct(m, e.src, s);
      if (e.big) { P.bigBar(c, x, y, e.w, e.h, pct); break; }
      const st = e.src === 1 && s ? { done: s.done, total: s.total, working: s.state === 'working' } : { done: jsRound(pct), total: 100, working: false };
      P.bar(c, x, y, e.w, e.h, st, R.f);
      break;
    }
    case 7: {
      const t = V.expand(e.segs, cx, row, false);
      const q = t && qrEncode(t);
      if (!q) break;
      const px = Math.max(1, Math.floor(Math.min(e.w, e.h) / (q.size + 2)));
      c.fillRect(x, y, (q.size + 2) * px, (q.size + 2) * px, false);
      for (let r = 0; r < q.size; r++) for (let k = 0; k < q.size; k++) if (q.get(k, r)) c.fillRect(x + px + k * px, y + px + r * px, px, px, true);
      break;
    }
    case 8: P.sym(c, e.sym, x, y, e.scale, true); break;
    case 9:
      if (!e.dotted) c.fillRect(x, y, e.w, e.h, true);
      else if (!e.vertical) for (let r = 0; r < e.h; r++) P.dotsH(c, x, x + e.w, y + r);
      else for (let k = 0; k < e.w; k++) P.dotsV(c, x + k, y, y + e.h);
      break;
    case 10: rect(R.surf, x, y, e.w, e.h, e.border, e.fill, e.radius, ink); break;
    case 11: circle(R.surf, x, y, e.w, e.h, e.border, e.fill, ink); break;
  }
}
const rssiBars = (r) => (r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0);

function list(R, e) {
  const { cx, m } = R;
  const period = e.pageSec * 1000, per = Math.max(1, Math.floor((e.h + e.gap) / (e.rowH + e.gap)));
  const n = m.sessions.length, pages = Math.ceil(n / per);
  if (!V.condsTrue(e.conds, cx, cx.ref >= 0 ? m.sessions[cx.ref] : null)) return;
  const page = pages > 1 ? (((Math.floor(cx.now / period) % pages) + pages) % pages) : 0;
  if (pages > 1) wake(R.info, (Math.floor(cx.now / period) + 1) * period);
  for (let i = 0; i < per; i++) {
    const k = page * per + i;
    if (k >= n) break;
    for (const r of e.row) elem(R, r, e.x, e.y + i * (e.rowH + e.gap), m.sessions[k]);
  }
  if (e.dots && pages > 1) {
    const c = e.invert ? new Swap(R.surf) : R.surf;
    for (let pg = 0; pg < pages; pg++) {
      const sx = e.x + e.w - 4 * (pages - pg), sy = e.y + e.h - 3;
      if (pg === page) c.fillRect(sx, sy, 3, 3, true); else c.fillRect(sx + 1, sy + 1, 1, 1, true);
    }
  }
}

export function renderScreen(lowered, key, m, surf, assets) {
  const info = { drawn: false, nextChange: 0, hasMascot: false, mx: 0, my: 0, mw: 0, mh: 0 };
  const scr = lowered.screens[key];
  const v = scr && scr.variants.find((x) => V.whenTrue(m, x.when));
  if (!v) return info;
  const cx = { m, now: m.now, local: m.local, frame: m.frame, screen: key, ref: V.refIndex(m, key, v.ref) };
  const R = { cx, m, surf, assets, f: m.frame, info };
  for (const e of v.elems) (e.type === 12 ? list(R, e) : elem(R, e, 0, 0, null));
  info.drawn = true;
  return info;
}
```

Punti da tenere uguali al C++ (controllarli uno per uno con `theme_draw.cpp` aperto accanto): `(8·s − 8)/2` è una divisione intera (`Math.trunc`); l'àncora del testo centrato è `x + w/2` non arrotondata (la arrotonda `T`); la pagina usa `floorDiv`/`floorMod` su `now`; nella lista la condizione si valuta con la sessione della variante; l'accessorio ridisegna solo i punti che cambiano, con `#` = inchiostro e il resto = carta.

- [ ] **Step 5: Scrivere `tools/golden/theme-golden.mjs`**

```js
#!/usr/bin/env node
// Golden della parità C++/JS dei temi (spec §7). Per ogni tema di protocol/themes/fixtures × schermata × scenario di
// protocol/themes/scenarios.json disegna con render.js e scrive firmware/test/test_theme_parity/golden.json (CRC-32
// dell'immagine 250×122 nel formato di Canvas::data(), drawn, nextChange, riquadro della mascotte). Il test C++ rifà
// gli stessi casi sul CTHM compilato dal servizio.
//   node tools/golden/theme-golden.mjs            scrive golden.json
//   node tools/golden/theme-golden.mjs --check    esce con 1 se golden.json non corrisponde al renderer di oggi
//   node tools/golden/theme-golden.mjs --pbm DIR  scrive anche le immagini JS (DIR/<tema>-<schermata>-<scenario>-js.pbm)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadWithAnchors } from '../avatar/lib/anchors.mjs';
import { lowerTheme } from '../../service/internal/webui/static/assets/js/theme/lower.js';
import { renderScreen } from '../../service/internal/webui/static/assets/js/theme/render.js';
import { makeModel } from '../../service/internal/webui/static/assets/js/theme/values.js';
import { Bitmap, crc32 } from '../../service/internal/webui/static/assets/js/theme/surface.js';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const OUT = path.join(REPO, 'firmware', 'test', 'test_theme_parity', 'golden.json');
const FX = path.join(REPO, 'protocol', 'themes', 'fixtures');
const THEMES = ['prova', 'elementi', 'lista'];

const avs = {};
for (const f of fs.readdirSync(path.join(REPO, 'avatars')).filter((f) => f.endsWith('.avatar.json'))) {
  const { av } = loadWithAnchors(path.join(REPO, 'avatars', f));
  avs[av.id] = av;
}
const accs = {};
for (const f of fs.readdirSync(path.join(REPO, 'avatars', 'accessori')).filter((f) => f.endsWith('.accessory.json'))) {
  const a = JSON.parse(fs.readFileSync(path.join(REPO, 'avatars', 'accessori', f), 'utf8'));
  accs[a.id] = a;
}
const assets = { avatar: (id) => avs[id] || null, accessory: (id) => accs[id] || null };

const sc = JSON.parse(fs.readFileSync(path.join(REPO, 'protocol', 'themes', 'scenarios.json'), 'utf8'));
const byName = Object.fromEntries(sc.scenarios.map((s) => [s.name, s]));
const merge = (a, b) => { const o = { ...a }; for (const [k, v] of Object.entries(b)) o[k] = v && typeof v === 'object' && !Array.isArray(v) && a[k] && typeof a[k] === 'object' ? merge(a[k], v) : v; return o; };

const pbmDir = process.argv.includes('--pbm') ? process.argv[process.argv.indexOf('--pbm') + 1] : null;
const cases = [];
for (const t of THEMES) {
  const lowered = lowerTheme(JSON.parse(fs.readFileSync(path.join(FX, t + '.theme.json'), 'utf8')));
  for (const screen of Object.keys(lowered.screens).sort()) {
    for (const s of sc.scenarios) {
      const over = s.extends ? merge(byName[s.extends], s) : s;
      const m = makeModel(sc.base, over);
      const b = new Bitmap();
      const info = renderScreen(lowered, screen, m, b, assets);
      const packed = b.packed();
      cases.push({ theme: t, screen, scenario: s.name, drawn: info.drawn, next: info.nextChange,
        mascot: info.hasMascot ? [info.mx, info.my, info.mw, info.mh] : null, crc: crc32(packed) });
      if (pbmDir) {
        fs.mkdirSync(pbmDir, { recursive: true });
        fs.writeFileSync(path.join(pbmDir, t + '-' + screen + '-' + s.name + '-js.pbm'), Buffer.concat([Buffer.from('P4\n250 122\n'), Buffer.from(packed)]));
      }
    }
  }
}
const text = JSON.stringify({ comment: 'Generato da tools/golden/theme-golden.mjs: non modificare a mano.', cases }, null, 1) + '\n';
if (process.argv.includes('--check')) {
  const old = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '';
  if (old !== text) { console.error('golden.json non corrisponde al renderer: node tools/golden/theme-golden.mjs, poi pio test -f test_theme_parity'); process.exit(1); }
  console.log('golden dei temi aggiornato: ' + cases.length + ' casi');
} else {
  fs.writeFileSync(OUT, text);
  console.log('scritto ' + path.relative(REPO, OUT) + ': ' + cases.length + ' casi');
}
```

Nota: la fusione di `extends` (`merge(byName[s.extends], s)`, poi `makeModel(base, …)`) dà lo stesso risultato dell'ordine base → esteso → scenario del C++, perché ogni passo sostituisce solo i campi presenti.

- [ ] **Step 6: Generare il golden e far passare la parità**

Run: `node tools/golden/theme-golden.mjs && cd firmware && pio test -e native -f test_theme_parity`
Expected: «scritto firmware/test/test_theme_parity/golden.json: 154 casi» (3 temi: 2 + 10 + 2 schermate = 14 × 11 scenari) e il test C++ PASS con «0 diversi».

Se ci sono differenze: `node tools/golden/theme-golden.mjs --pbm /tmp/tp` e confrontare con `firmware/.pio/theme-parity/*-cpp.pbm` usando `node tools/sim-capture/diff.mjs /tmp/tp/X-js.pbm firmware/.pio/theme-parity/X-cpp.pbm --out /tmp/tp/X.png` (rosso = solo JS, blu = solo C++). La correzione va nel lato che si allontana da `docs/TEMI.md` §6 o dal comportamento delle funzioni del firmware; il golden non si scrive mai a mano.

- [ ] **Step 7: Prova di parità in `go test`**

`service/internal/webui/theme_parity_test.go`:

```go
package webui

import (
	"os/exec"
	"path/filepath"
	"testing"
)

// Renderer dei temi (static/assets/js/theme): fonts.js è l'export dei .h del firmware e golden.json, che il test
// C++ test_theme_parity confronta con l'interprete della scheda, corrisponde al renderer di oggi. Insieme danno la
// parità C++ = JS della spec §7.
func TestThemeRendererParity(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node non disponibile")
	}
	repo, _ := filepath.Abs("../../..")
	for _, args := range [][]string{
		{filepath.Join(repo, "tools", "fonts", "export-js.mjs"), "--check"},
		{filepath.Join(repo, "tools", "golden", "theme-golden.mjs"), "--check"},
	} {
		out, err := exec.Command(node, args...).CombinedOutput()
		if err != nil {
			t.Fatalf("%s: %v\n%s", filepath.Base(args[0]), err, out)
		}
		t.Logf("%s", out)
	}
}
```

Run: `cd service && go test ./internal/webui/ -run TestThemeRendererParity -v`
Expected: PASS.

- [ ] **Step 8: Sezione 8 di `test-theme.mjs`** (prova veloce che il renderer gira nel pacchetto di prova senza il golden)

```js
// 8. renderer: un tema di prova disegna, una schermata assente no
import { renderScreen } from '../../service/internal/webui/static/assets/js/theme/render.js';
const noAvatar = { avatar: () => ({ id: 'vuoto', poses: { working: [Array(25).fill('.'.repeat(28))], idle: [Array(25).fill('.'.repeat(28))] } }), accessory: () => null };
const bm = new Bitmap();
const ri = renderScreen(lp, 'home', V.makeModel(base, {}), bm, noAvatar);
check(ri.drawn && bm.countInk() > 0, 'home di «prova» con 0 sessioni');
check(!renderScreen(lp, 'oggi', V.makeModel(base, {}), new Bitmap(), noAvatar).drawn, 'schermata assente: di serie');
```

(gli import in testa al file.) Run: `cd tools/webui-dev && npm test` → tutto ok.

- [ ] **Step 9: Prova completa e build**

Run: `cd firmware && pio test -e native && pio run -e heltec213 | tail -3 && cd ../service && go test ./... && cd ../tools/webui-dev && npm test`
Expected: tutto verde; la build della scheda passa (il codice dei temi c'è ma non è ancora agganciato a un `Runtime` sulla scheda: crescita della flash di qualche KB, nessuna crescita della RAM statica).

- [ ] **Step 10: Commit**

```bash
git add service/internal/webui/static/assets/js/theme/render.js protocol/themes/scenarios.json tools/golden/theme-golden.mjs firmware/test/test_theme_parity service/internal/webui/theme_parity_test.go tools/webui-dev/test-theme.mjs
git commit -m "Temi: renderer JS e parità pixel per pixel con l'interprete della scheda"
```

---

## Fine del piano 1a

Dopo il Task 10: il servizio controlla e compila i temi, la scheda li disegna (in native), la webapp li disegna uguali. Il piano 1b (`docs/superpowers/plans/2026-10-15-editor-1b-fondamenta.md`) porta il tema fino alla scheda vera: archivio del servizio e API, protocollo «Temi» e HTTP, LittleFS, Menu › Display › Tema, uscita di emergenza, versione 0.13.0-ed, documentazione.
