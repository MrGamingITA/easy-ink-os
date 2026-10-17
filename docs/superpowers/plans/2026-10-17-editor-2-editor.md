# Editor delle schermate, fase 2: l'editor nella webapp — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** La pagina «Editor schermate» della webapp diventa l'editor «stile Figma» della spec §6 (senza la sezione Menu): si creano e si modificano temi trascinando elementi su un'anteprima che è, pixel per pixel, quella della scheda, con avvisi in tempo reale (compresi i glifi che il carattere non ha), salvataggio automatico e «Invia alla scheda»; funziona con mouse e tastiera sul Mac e col tocco nell'app iPhone (393×852 pt).

**Architecture:** La logica dell'editor sta in moduli puri sotto `service/internal/webui/static/assets/js/editor/`, provati in Node: cronologia (annulla/ripeti), operazioni sul JSON del tema, riquadri e selezione, aggancio e allineamento, gesti del puntatore, avvisi, dati di prova, azioni sullo stato. I moduli `ui-*.js` collegano il DOM a quelle funzioni e non disegnano mai la schermata da sé: l'anteprima è sempre `renderScreen` di `theme/render.js` su una `Bitmap`, ingrandita. Il servizio aggiunge un controllo senza salvataggio (`POST /api/themes/check`), un salvataggio automatico che non riempie la cronologia e rifiuta le modifiche fatte altrove (409), e gli avvisi sui glifi mancanti; un catalogo condiviso (`protocol/themes/catalog.json`) tiene allineati Go, JS e firmware.

**Tech Stack:** Go 1.24 (`go.mod`; sul Mac Go 1.27), JavaScript della webapp (moduli ES, Node 22 per le prove), Playwright 1.64.0 (già in `tools/webui-dev/package.json`) per la prova nel browser, PlatformIO `native` per la prova del firmware. Nessuna dipendenza nuova.

**Spec:** `docs/superpowers/specs/2026-10-15-editor-schermate-design.md` (§6 senza «Sezione Menu», §1–§3 per le regole, §8 punto 2); per il telefono `docs/superpowers/specs/2026-10-16-app-iphone-design.md` §3.1 (elenco chiuso) e §5. Formato: `docs/TEMI.md`. **Prerequisito:** worktree `~/CalamaiOS-iphone`, ramo `editor-2` (da `app-iphone` 4e352e9), albero pulito; `cd service && go test ./... 2>&1 | tail -5` e `cd tools/webui-dev && npm test 2>&1 | tail -5` passano. Se `tools/webui-dev/node_modules` manca: `npm ci` (se non c'è rete, chiedere all'utente prima di collegare la cartella del repository principale).

## Global Constraints

- **Regole dei permessi (brief §7) invariate.** La schermata Permessi non si personalizza mai: l'editor la mostra nell'elenco delle schermate, disattivata, con la scritta «Permessi — bloccata (regole di sicurezza)»; nessuna operazione dell'editor scrive `screens.perm` (né le altre bloccate: `pair`, `confirm`, `boot`, `ota`, `battlow`, `calib`, `gbload`, `gbplay`), e il servizio continua a rifiutarle (`lockedScreens` di `theme.go`).
- **Elenco chiuso della porta 8767**: l'editor usa solo rotte di `phoneRoutes` (`service/internal/api/phone.go`). L'unica rotta nuova, `POST /api/themes/check`, entra nell'elenco nello stesso task che la crea. Nessuna rotta di `/api/phones*`, `/api/firmware*`, `/api/backups*`, `/hook*`, `/debug*`.
- **Parità**: tutto ciò che l'editor mostra della schermata viene da `renderScreen` (`assets/js/theme/render.js`). I moduli dell'editor non importano `theme/prims.js`, `mascot.js`, `avatar.js` (`drawAvatar`), né chiamano `T`/`drawText` di `theme/text.js`: da `text.js` solo misure (`textWidth`, `textWidthMilli`, `wrapLines`, `fitText`, `hasFont`). Sovrapposizioni (selezione, maniglie, guide, griglia) stanno su una tela separata sopra l'anteprima.
- **Formato dei temi invariato** (`docs/TEMI.md` v1): l'editor scrive solo campi già ammessi; quelli suoi sono `id` e `name` (≤ 32 byte) e `locked` degli elementi. Nessuna rigenerazione dei caratteri (`tools/fonts/gen.py` resta com'è): i glifi mancanti si segnalano e basta.
- **Zoom**: interi da ×1 a ×6 (la spec dice ×2–×6; ×1 serve solo perché a 393 pt lo schermo intero ci stia: deviazione annotata in DECISIONI). Griglia dei pixel visibile da ×3 in su. Frecce = 1 px, Maiusc+frecce = 8 px.
- **Aggancio**: griglia 1/2/4/8 px (1 = nessuna griglia); agli altri elementi e ai bordi e al centro dello schermo entro **3 px** dello schermo della scheda; l'aggancio agli elementi vince sulla griglia.
- **Tempi**: controllo dal servizio 300 ms dopo l'ultima modifica; salvataggio automatico 1 500 ms dopo l'ultima modifica e mai durante un trascinamento; nella cronologia del servizio al più una versione ogni **2 minuti** di salvataggi automatici. Anteprima animata: fotogramma 0/1 ogni **500 ms** (classe S, `fps: 2` del simulatore), orologio in tempo reale.
- **Telefono**: a 393×852 pt niente scorrimento orizzontale della pagina; bersagli di tocco ≥ **44×44** pt; pannelli sinistro e destro a cassetto sotto i **900** px di larghezza; tela con pizzica (zoom) e due dita (spostamento); ogni funzione raggiungibile senza tastiera. Le scorciatoie da tastiera restano sul Mac.
- **CSP della webapp** (`csp` di `service/internal/api/web.go`, uguale nell'app): niente script né attributi `style` in linea; gli stili dinamici solo via CSSOM (`el.style.left = …`), mai `setAttribute('style', …)`.
- **Lingua**: testi a schermo in italiano (quelli fissati qui sono vincolanti); identificatori in inglese; commenti brevi in italiano.
- **Uscite**: ogni comando di prova finisce con `2>&1 | tail -n 40` (o `| grep …`).
- **Commit**: piccoli, messaggio in italiano, **senza** riga `Co-Authored-By`; niente push, niente merge.
- **Niente hardware in nessun task**: niente flash, niente NVS, niente `~/.claude/settings.json`. La scheda si usa solo nelle «Prove dal vivo», con l'utente.

## Review Focus

- **Due editor sullo stesso tema** (Mac e iPhone insieme, o due schede del browser): il secondo salvataggio automatico non sovrascrive in silenzio quello dell'altro; riceve 409, l'editor dice «Cambiato altrove» e offre «Ricarica» o «Sovrascrivi» — `TestThemeAutosaveConflict` (Task 3), `autosave: 409 → conflict` (Task 13).
- **Servizio irraggiungibile durante le modifiche** (Mac in stop, porta 8767 spenta, Wi-Fi del telefono caduto): le modifiche restano in memoria con annulla/ripeti, lo stato dice «Non salvato: …», si riprova da solo alla modifica successiva e con «Riprova»; uscire dalla pagina con modifiche non salvate chiede conferma — `autosave: errore di rete → error, poi riprova` (Task 13).
- **Tema a metà mentre si scrive** (modello `{oggi.co` senza graffa chiusa, valore sconosciuto, carattere inesistente da un import, elemento a x = 600): l'anteprima non lancia mai, mostra «La scheda mostrerebbe la schermata di serie» quando un elemento non passa i controlli, i problemi del servizio compaiono accanto all'elemento giusto e il salvataggio automatico aspetta — `warnings: problemi del servizio sull'elemento giusto`, `preview: tema rotto non lancia` (Task 8), `autosave: tema non valido non si salva` (Task 13).
- **Elementi bloccati, nascosti e Permessi**: un elemento `locked` non si sposta con trascinamento, riquadro, frecce, allinea o distribuisci; un nascosto non si seleziona sulla tela; nessuna strada (aggiungi, incolla, «Parti dalla schermata di serie», import) scrive `screens.perm` dall'editor — `moveEls salta i bloccati`, `addEl rifiuta perm` (Task 5), `hitTest e marquee saltano bloccati e nascosti` (Task 6), `align salta i bloccati` (Task 7).
- **Temi importati o scritti a mano** (senza `id` degli elementi, con `id` doppi, gruppi annidati a 4 livelli, righe di lista): `ensureIds` dà id unici senza cambiare la resa, le operazioni trovano gli elementi a ogni profondità, un gruppo oltre il quarto livello non si crea — `ensureIds: doppi e mancanti`, `groupEls oltre 4 livelli` (Task 5), `layout: righe della lista e gruppi annidati` (Task 6).

---

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `protocol/themes/catalog.json` | glifi di ogni carattere, alfabeto ed esempio di ogni valore, casi degli avvisi: fonte comune per Go, JS e firmware | 1 |
| `service/internal/theme/glyphs.go`, `glyphs_test.go`; `fonts.go`, `values.go`, `parse.go` | glifi dei caratteri, alfabeti dei valori, avvisi del compilatore | 1 |
| `firmware/src/gfx/text.h`, `text.cpp`, `firmware/test/test_text/test_main.cpp` | `hasGlyph` e prova native contro il catalogo | 2 |
| `service/internal/webui/static/assets/js/theme/glyphs.js` | gli stessi avvisi dei glifi in JS | 2 |
| `service/internal/theme/store.go`, `store_test.go`; `service/internal/api/themes.go`, `server.go`, `phone.go`, `themes_test.go`, `phone_test.go` | `Check`, `Autosave` (unione delle versioni, 409), rotta di controllo, elenco chiuso | 3 |
| `tools/webui-dev/phone-routes.json` | copia dell'elenco chiuso per la prova nel browser | 3 |
| `tools/webui-dev/mock-server.mjs` | rotte dei temi e invio finto per le prove della webapp | 3 |
| `assets/js/editor/history.js` | cronologia pura: annulla/ripeti illimitati, unione dei passi di un gesto | 4 |
| `assets/js/editor/ops.js` | operazioni pure sul JSON del tema (elementi, gruppi, varianti, appunti) | 5 |
| `assets/js/editor/select.js` | riquadri assoluti, colpo del puntatore, riquadro di selezione, maniglie, ridimensionamento | 6 |
| `assets/js/editor/geometry.js`, `assets/js/editor/keys.js` | aggancio, allinea/distribuisci, scorciatoie | 7 |
| `assets/js/editor/testdata.js`, `assets/js/editor/warnings.js` | dati di prova, anteprima della variante, avvisi in tempo reale | 8 |
| `assets/js/editor/store.js`, `actions.js`, `assets.js`, `view.js`, `catalog.js`, `ui-canvas.js`; `editor.html`, `assets/js/editor.js`, `assets/app.css` | stato, azioni, caricamento di avatar e accessori, disegno ingrandito, impaginazione e cassetti | 9 |
| `assets/js/editor/gesture.js`, `ui-canvas.js` | gesti di mouse e tocco, tela interattiva | 10 |
| `assets/js/editor/ui-layers.js` | «Aggiungi» e «Livelli» | 11 |
| `assets/js/editor/ui-props.js` | proprietà, condizioni, valori, allinea/distribuisci, aggancio | 12 |
| `assets/js/editor/ui-topbar.js`, `ui-warnings.js`, `assets/js/theme/stock.js` | barra in alto, avvisi, salvataggio automatico, dati di prova, animazione, schermate di serie | 13 |
| `assets/js/editor/ui-send.js`; `assets/js/api.js` | «Invia alla scheda» | 14 |
| `tools/webui-dev/test-editor.mjs`, `smoke-editor.mjs`, `shots.mjs`, `package.json` | prove Node e prova nel browser (Chromium 1280, WebKit 393 col tocco) | 4–14 |
| `docs/TEMI.md`, `docs/PROTOCOLLO.md`, `docs/DECISIONI.md` | documentazione | 1, 3, 14 |

Percorsi brevi: `assets/js/…` sta per `service/internal/webui/static/assets/js/…`; `static/…` per `service/internal/webui/static/…`.

Le prove JS stanno in `tools/webui-dev/test-editor.mjs` (nuovo, stesso stile di `test-theme.mjs`: `check`/`eq`, `process.exit(fail ? 1 : 0)`), aggiunto allo script `test` di `package.json` nel Task 4: `"test": "node test-units.mjs && node test-theme.mjs && node test-editor.mjs"`. Ogni task aggiunge una sezione numerata con un commento. Comando: `cd tools/webui-dev && node test-editor.mjs 2>&1 | tail -n 40`.

---

### Task 1: Catalogo dei glifi e avvisi del compilatore

Sulla scheda un testo con «$» in Chakra Petch 44/700 è uscito «?»: i caratteri da 26 px in su hanno solo `BIG_CHARS` di `tools/fonts/gen.py:52`. Oggi `parse.go` controlla solo il testo fisso; deve controllare anche ciò che un valore può mostrare, i filtri e `upper`.

**Files:**
- Create: `protocol/themes/catalog.json`, `service/internal/theme/glyphs.go`, `service/internal/theme/glyphs_test.go`
- Modify: `service/internal/theme/fonts.go` (togliere `bigChars`, `bigFontMissing`, `containsRune`), `service/internal/theme/values.go` (`ValueSpec` + alfabeti), `service/internal/theme/parse.go:399-404`, `service/internal/theme/parse_test.go:113-119`, `docs/TEMI.md` §3

**Interfaces:**
- Consumes: `Font`, `Seg`, `Element`, `ParseTemplate`, `Values` (esistenti).
- Produces:
  - `func FontGlyphs(f Font) string` — i caratteri che il carattere ha, in ordine di codepoint, senza le legature private U+E000/U+E001;
  - `ValueSpec` con i campi nuovi `Glyphs string` (alfabeto chiuso, in ordine di codepoint) e `Free bool` (testo libero);
  - `const ClockGlyphs = "-0123456789:"`;
  - `func GlyphWarnings(e *Element) []string` — avvisi senza percorso, nell'ordine dei pezzi del modello (`text` e `clock`; niente per gli altri tipi);
  - `func FontLabel(f Font) string` — `"Chakra Petch 44"` (`ui`) o `"Silkscreen 8"` (`pix`);
  - `protocol/themes/catalog.json` = `{comment, fonts: [{family, size, weight, glyphs}] (22, ordine di Fonts), clock, values: [{name, id, example, glyphs, free}] (ordine di Values), cases: [{font, text, upper, kind, warnings}]}` (`kind`: `"text"` o `"clock"`; per `clock` `text` è `"hm"` o `"hms"`).

Alfabeti dei valori (decisione di questo piano; `alpha(s)` = runa unica di `s` in ordine di codepoint):

| Valori | Alfabeto |
|---|---|
| `nome`, `stato`, `attività`, `riassunto`, `meta` (sessione e riga), `wifi`, `pc`, `versione` | libero (`Free`) |
| `contesto`, `batteria` | `alpha("0123456789%")` |
| `task` | `alpha("0123456789/")` |
| `da` | `alpha("0123456789 min h")` |
| `sub`, `sessioni`, `inattesa`, `oggi.task` | `alpha("0123456789")` |
| `test` | `"✓✗"` |
| `ora` | `ClockGlyphs` |
| `giorno` | `alpha` dei sette nomi dei giorni di `values.js` (`WEEKDAYS`) |
| `data` | `alpha` di giorni + mesi (`MONTHS`) + `"0123456789 "` |
| `autonomia` | `alpha("Autonomia: circa giorni ore con il tuo uso (dalla scarica) calibra per una stima in corso 0123456789")` |
| `giornata.mancano` | `alpha("0123456789 h min finita")` |
| `giornata.fine` | `alpha("0123456789:")` |
| `oggi.righe` | `alpha("0123456789+−- ")` (con U+2212) |
| `oggi.costo` | `alpha("0123456789,- $")` («nan»/«inf» esclusi: non capitano) |
| `limiti.5h`, `limiti.7g` | `alpha("0123456789%—")` |
| `mascotte.livello` | `alpha("0123456789—")` |
| `mascotte.xp` | `alpha("0123456789/- XP")` |
| `mascotte.energia` | `alpha("0123456789%-")` |
| `co2` | `alpha("0123456789 ppm")` |

Regole di `GlyphWarnings` (stesse in JS nel Task 2):
1. Per ogni pezzo letterale: le rune dopo i filtri (`upper` dell'elemento → maiuscolo come `mapCase` di `values.js`: ASCII e U+00E0–U+00FE tranne U+00F7) che `FontGlyphs` non ha, senza doppioni, in ordine di comparsa → `"Chakra Petch 44 non ha «$€»: si vedrà «?»"`.
2. Per ogni valore chiuso: alfabeto dopo filtro del pezzo (`maiuscolo`/`minuscolo`) e poi `upper`; mancanti in ordine di codepoint → `"{oggi.costo} può mostrare «$», che Chakra Petch 44 non ha: si vedrà «?»"`.
3. Per ogni valore libero, solo se il carattere non ha tutto l'ASCII stampabile (0x20–0x7E) → `"{sessione.nome} è testo libero: Chakra Petch 44 ha solo cifre, minuscole e «%+,-./:?…»"` (l'elenco fra «» = i caratteri non alfanumerici di `FontGlyphs`, spazio escluso, in ordine di codepoint). Un valore ripetuto nello stesso elemento dà un solo avviso.
4. `clock`: `ClockGlyphs` come un valore chiuso col nome `{ora}`.

- [ ] **Step 1: Scrivere le prove che falliscono** in `service/internal/theme/glyphs_test.go`

```go
// I glifi del servizio sono quelli del firmware: entries[] di firmware/src/gfx/fonts/f_<ui|pix><peso>_<px>.h.
func TestFontGlyphsMatchFirmware(t *testing.T) // per ogni Fonts: codepoint delle righe «{0x....,» dentro
// <nome>_entries[], meno 0xE000 e 0xE001, uguali a FontGlyphs; e FontGlyphs(ui 44/700) non contiene '$', 'A', 'ì', '—'.

// catalog.json = tabelle di Go (fonts, clock, values con example/glyphs/free) e ogni caso dà gli avvisi scritti.
func TestGlyphCatalog(t *testing.T)

func TestGlyphWarnings(t *testing.T) {
	big := Font{"ui", 44, 700}
	cases := []struct {
		text  string
		upper bool
		want  []string
	}{
		{"{oggi.costo}", false, []string{"{oggi.costo} può mostrare «$», che Chakra Petch 44 non ha: si vedrà «?»"}},
		{"Costo $ €", false, []string{"Chakra Petch 44 non ha «C$€»: si vedrà «?»"}},
		{"{giorno}", false, []string{"{giorno} può mostrare «ì», che Chakra Petch 44 non ha: si vedrà «?»"}},
		{"{batteria}", true, nil},
		{"ok", true, []string{"Chakra Petch 44 non ha «OK»: si vedrà «?»"}},
		{"{sessione.nome} {sessione.nome}", false, []string{"{sessione.nome} è testo libero: Chakra Petch 44 ha solo cifre, minuscole e «%+,-./:?…»"}},
	}
	// ... ParseTemplate(c.text, false), Element{Type: "text", Font: big, Upper: c.upper, Segs: segs}
	// reflect.DeepEqual(GlyphWarnings(e), c.want)
	// in Chakra Petch 12/600 e in Silkscreen 8/400 nessuno di questi casi dà avvisi
}
```

E in `parse_test.go` la prova esistente («avviso dei caratteri grandi mancante») cerca `"Chakra Petch 40 non ha"`; una nuova `TestParseGlyphWarningPath` controlla che l'avviso di un `text` con `{oggi.costo}` a 44/700 in `screens.home.variants[0].elements[0]` cominci con quel percorso seguito da `": "`.

- [ ] **Step 2: Verificare che falliscano**

Run: `cd service && go test ./internal/theme/ -run 'Glyph|Parse' 2>&1 | tail -n 40`
Expected: FAIL (`FontGlyphs`, `GlyphWarnings` non definiti).

- [ ] **Step 3: Scrivere `glyphs.go`, aggiornare `values.go`, `fonts.go`, `parse.go`**

`FontGlyphs`: due insiemi costanti (i caratteri piccoli e quelli da 26 px in su, letti dai `.h`); la prova del passo 1 li tiene allineati. `parse.go` al posto del ciclo su `bigFontMissing`: `for _, w := range GlyphWarnings(e) { p.warns = append(p.warns, path+": "+w) }` per `text` e `clock` (anche nelle righe delle liste). Scrivere `catalog.json` a mano a partire dalle tabelle (il test lo confronta campo per campo; i casi: almeno quelli della tabella del passo 1 più un `clock` `hms` a 44/700 senza avvisi e un `{data|maiuscolo}` a 12/600 senza avvisi).

- [ ] **Step 4: Verificare**

Run: `cd service && go test ./internal/theme/ 2>&1 | tail -n 40` → PASS. Poi `cd service && go test ./... 2>&1 | tail -n 15` → PASS (gli avvisi finiscono in `Info.Warnings` come prima).

- [ ] **Step 5: TEMI.md** — in §3 un paragrafo «Glifi»: i caratteri da 26 px in su hanno solo cifre, minuscole e «%+,-./:?…»; il compilatore avvisa (non rifiuta) per testo fisso, valori, filtri e `upper`; fonte: `protocol/themes/catalog.json`.

- [ ] **Step 6: Commit**

```bash
git add protocol/themes/catalog.json service/internal/theme docs/TEMI.md
git commit -m "Temi: avvisi dei glifi mancanti anche per valori, filtri e maiuscolo; catalogo comune dei glifi"
```

---

### Task 2: Glifi in JS e nel firmware, contro lo stesso catalogo

**Files:**
- Create: `assets/js/theme/glyphs.js`
- Modify: `firmware/src/gfx/text.h`, `firmware/src/gfx/text.cpp`, `firmware/test/test_text/test_main.cpp`, `tools/webui-dev/test-theme.mjs`

**Interfaces:**
- Consumes: `FONTS` di `theme/fonts.js` (`cps` per carattere), `VALUES` di `theme/values.js`, `catalog.json` del Task 1.
- Produces:
  - `export function fontGlyphs(family, size, weight) → Set<number>` (codepoint, senza U+E000/U+E001; `null` se la combinazione non esiste);
  - `export const VALUE_GLYPHS` = `{ [name]: { glyphs: string, free: boolean, example: string } }`, `export const CLOCK_GLYPHS = '-0123456789:'`;
  - `export function glyphWarnings(el) → string[]` su un elemento JSON `text` o `clock` (testo del modello, `font`, `upper`, `format`), stessi messaggi e stesso ordine di `GlyphWarnings`; valori sconosciuti o graffe aperte ignorati (li segnala il servizio), mai un'eccezione;
  - firmware: `bool hasGlyph(int size, int weight, bool pix, uint16_t cp);` in `gfx/text.h` (stessa ricerca di `normWeight` e della tabella `entries`; falso se la combinazione non esiste).

- [ ] **Step 1: Prove JS che falliscono** — nuova sezione di `test-theme.mjs`:

```js
// 9. glifi: stesse tabelle e stessi avvisi del servizio (protocol/themes/catalog.json)
const cat = JSON.parse(fs.readFileSync(path.join(REPO, 'protocol/themes/catalog.json'), 'utf8'));
for (const f of cat.fonts) eq([...G.fontGlyphs(f.family, f.size, f.weight)].map((c) => String.fromCodePoint(c)).join(''), f.glyphs, 'glifi ' + f.family + f.size + '/' + f.weight);
for (const v of cat.values) eq(JSON.stringify(G.VALUE_GLYPHS[v.name]), JSON.stringify({ glyphs: v.glyphs, free: v.free, example: v.example }), 'valore ' + v.name);
for (const c of cat.cases) eq(JSON.stringify(G.glyphWarnings(c.kind === 'clock' ? { type: 'clock', format: c.text, font: c.font } : { type: 'text', text: c.text, font: c.font, upper: c.upper })), JSON.stringify(c.warnings), 'caso ' + c.text);
eq(JSON.stringify(G.glyphWarnings({ type: 'text', text: '{oggi.co', font: { family: 'ui', size: 44, weight: 700 } })), JSON.stringify(['Chakra Petch 44 non ha «{»: si vedrà «?»']), 'graffa aperta = testo fisso, come parseTemplate');
```

(La graffa aperta in `parseTemplate` di `lower.js` diventa testo: l'avviso sulla «{» è giusto.)

- [ ] **Step 2: Prova native che fallisce** — in `test_text/test_main.cpp`, `test_glyphs_match_catalog`: legge `protocol/themes/catalog.json` con lo stesso `readText(repoRoot() + …)` di `test_theme_parity` (copiare i due aiuti), per ogni carattere ogni codepoint di `glyphs` dà `hasGlyph` vero e i codepoint 0x20–0x7E fuori da `glyphs` danno falso; poi `test_missing_glyph_draws_question`: in 44/700 `"$"` e `"?"` disegnati su due `Canvas` danno gli stessi byte.

- [ ] **Step 3: Verificare che falliscano**

Run: `cd tools/webui-dev && node test-theme.mjs 2>&1 | tail -n 20` → FAIL (modulo mancante). `cd firmware && pio test -e native -f test_text 2>&1 | tail -n 20` → errore di compilazione (`hasGlyph`).

- [ ] **Step 4: Scrivere `glyphs.js` e `hasGlyph`** — `glyphs.js` legge `FONTS` (la variante non `inv`) e ha la tabella dei valori scritta come in Go; il parser dei pezzi è quello di `parseTemplate` ma tiene il nome del valore.

- [ ] **Step 5: Verificare**

Run: `cd tools/webui-dev && npm test 2>&1 | tail -n 20` → PASS. `cd firmware && pio test -e native -f test_text 2>&1 | tail -n 20` → PASS. `cd firmware && pio run -e heltec213 2>&1 | tail -n 5` → compila (la funzione nuova non usata dal firmware non entra nell'immagine).

- [ ] **Step 6: Commit**

```bash
git add service/internal/webui/static/assets/js/theme/glyphs.js firmware/src/gfx/text.h firmware/src/gfx/text.cpp firmware/test/test_text tools/webui-dev/test-theme.mjs
git commit -m "Temi: glifi mancanti anche in JS e prova native contro il catalogo"
```

---

### Task 3: Servizio: controllo senza salvataggio, salvataggio automatico, elenco chiuso

**Files:**
- Modify: `service/internal/theme/store.go`, `service/internal/theme/store_test.go`, `service/internal/api/themes.go`, `service/internal/api/server.go:176-184`, `service/internal/api/phone.go:85-93`, `service/internal/api/themes_test.go` (o il file di prova delle rotte dei temi che c'è), `service/internal/api/phone_test.go`, `tools/webui-dev/mock-server.mjs`, `docs/PROTOCOLLO.md` («API locale», «Elenco chiuso»)
- Create: `tools/webui-dev/phone-routes.json`

**Interfaces:**
- Consumes: `Store`, `info`, `check`, `save`, `phoneRoutes`.
- Produces:
  - `func (s *Store) Check(data []byte) *Info` — come `info` senza scrivere niente (problemi e avvisi dentro, `Screens` con i byte per schermata quando compila);
  - `var ErrChanged = errors.New("il tema è stato cambiato altrove")`;
  - `func (s *Store) Autosave(id string, data []byte, base int64) (*Info, error)` — come `Put`, più: `base > 0` e `Updated` del file ≠ `base` → `ErrChanged`; se l'ultima versione della cronologia l'ha scritta un `Autosave` di questo processo meno di `AutosaveMerge` fa, si riscrive quella versione invece di farne una nuova;
  - `const AutosaveMerge = 2 * time.Minute`; campo interno `auto map[string]autoMark` (`n`, istante) protetto da `mu`;
  - `POST /api/themes/check` → 200 `{theme: Info}` sempre (anche non valido), 413 oltre 256 KB; `postGuard`;
  - `PUT /api/themes/{id}?autosave=1&base=<ms>` → `Autosave`; 409 `{error: "il tema è stato cambiato altrove", theme: Info}` (con l'Info del file vivo) per `ErrChanged`; senza `autosave=1` resta `Put`;
  - `"POST /api/themes/check"` in `phoneRoutes`;
  - `tools/webui-dev/phone-routes.json` = `{"comment": "...", "routes": [...phoneRoutes]}`.

- [ ] **Step 1: Prove Go che falliscono**

```go
func TestThemeCheckDoesNotWrite(t *testing.T)   // Check di un tema valido: Screens["home"] > 0, nessun file in Dir
func TestThemeCheckInvalid(t *testing.T)        // Check di un tema con screens.perm: Problems con path "screens.perm"
func TestThemeAutosaveMerges(t *testing.T) {
	// Now finto: Autosave ×3 a 0 s, 30 s, 90 s → History ha 1 versione in più rispetto a prima;
	// a 90+121 s → una versione nuova. Un Put manuale in mezzo → versione nuova, e l'Autosave dopo non la riscrive.
}
func TestThemeAutosaveConflict(t *testing.T)    // base = Updated vecchio dopo un Put → ErrChanged, file invariato
func TestAPIThemeCheck(t *testing.T)            // 200 {theme:{problems}} anche per JSON rotto; 413 oltre 256 KB; 403 senza stessa origine
func TestAPIThemeAutosave(t *testing.T)         // ?autosave=1&base=... 200; base vecchio → 409 con theme.updated del file
func TestPhoneRoutesFixture(t *testing.T)       // phone-routes.json == phoneRoutes; con CALAMAIO_WRITE_FIXTURES=1 lo riscrive
```

e in `phone_test.go` (`TestPhoneAllowedRoutes` o equivalente) il caso `POST /api/themes/check` raggiungibile col token.

- [ ] **Step 2: Verificare che falliscano**

Run: `cd service && go test ./internal/theme/ ./internal/api/ -run 'Check|Autosave|PhoneRoutes|PhoneAllowed' 2>&1 | tail -n 40` → FAIL.

- [ ] **Step 3: Scrivere** `Check`, `Autosave` (riscrivere `<n>.json` e il file vivo nello stesso ordine di `save`: prima la cronologia), le due rotte, la voce dell'elenco; generare `phone-routes.json` con `CALAMAIO_WRITE_FIXTURES=1 go test ./internal/api/ -run TestPhoneRoutesFixture`.

- [ ] **Step 4: Server finto** — `mock-server.mjs` tiene i temi in memoria con le stesse forme: `GET/POST /api/themes`, `POST /api/themes/import` (suffisso `-2`…), `GET/PUT/DELETE /api/themes/{id}` (con `autosave`/`base` e 409), `POST /api/themes/check` (risposta `{theme: {id, name, screens: {<schermata>: byte stimati = lunghezza del JSON della schermata / 2}, warnings: [], problems: []}}`; con `screens.perm` un problema `{path: "screens.perm", msg: "schermata bloccata: regole di sicurezza (Permessi, abbinamento e sistema restano di serie)"}`), `POST /api/displays/{board}/theme` (202, poi `themes.job` del display in `/api/status` e nell'SSE passa `attesa` → `offerta` → `scaricato` → `uso` → `fatto` ogni 300 ms; con `?fail=1` nell'URL della pagina l'ultima fase è `errore` «download non riuscito (HTTP 410)»), e `themes: {supported: true, have: [], cur: null, job: null}` sul primo display. Un tema iniziale `prova` (copia di `protocol/themes/fixtures/prova.theme.json`).

- [ ] **Step 5: PROTOCOLLO.md** — righe di `POST /api/themes/check` e di `PUT … ?autosave=1&base=` nella tabella delle API; la voce nell'elenco chiuso.

- [ ] **Step 6: Verificare**

Run: `cd service && go test ./... 2>&1 | tail -n 15` → PASS. `cd tools/webui-dev && node mock-server.mjs --port=8797 & sleep 1; curl -s -X POST -H 'Content-Type: application/json' -H 'Origin: http://127.0.0.1:8797' --data @../../protocol/themes/fixtures/prova.theme.json http://127.0.0.1:8797/api/themes/check | head -c 200; kill %1` → JSON con `"screens"`.

- [ ] **Step 7: Commit**

```bash
git add service/internal/theme service/internal/api tools/webui-dev/phone-routes.json tools/webui-dev/mock-server.mjs docs/PROTOCOLLO.md
git commit -m "Temi: controllo senza salvataggio, salvataggio automatico con conflitto 409 e versioni unite"
```

---

### Task 4: Cronologia pura (annulla/ripeti)

**Files:**
- Create: `assets/js/editor/history.js`, `tools/webui-dev/test-editor.mjs`
- Modify: `tools/webui-dev/package.json` (script `test`)

**Interfaces:**
- Consumes: niente.
- Produces (oggetti immutabili; i valori si trattano come immutabili, mai modificati sul posto):
  - `createHistory(value) → H` con `H = { present, past: [], future: [], open: null }`;
  - `commit(H, next, merge = null) → H` — `next` uguale a `present` (stesso oggetto o `JSON.stringify` uguale) → `H` invariato; con `merge` uguale a `H.open` sostituisce `present` senza un passo nuovo; altrimenti `past` + `present`, `future = []`, `open = merge`;
  - `endGesture(H) → H` — `open = null`;
  - `undo(H) → H`, `redo(H) → H` (chiudono anche il gesto), `canUndo(H)`, `canRedo(H)`;
  - `replace(H, value) → H` — nuovo `present` senza passo (ricarica dopo un conflitto: `past` e `future` vuoti).

- [ ] **Step 1: Prove che falliscono** (sezione 1 di `test-editor.mjs`)

```js
// 1. cronologia
let h = Hi.createHistory({ a: 1 });
h = Hi.commit(h, { a: 2 }); h = Hi.commit(h, { a: 3 });
eq(Hi.undo(h).present.a, 2, 'annulla');
eq(Hi.redo(Hi.undo(h)).present.a, 3, 'ripeti');
check(!Hi.canRedo(Hi.commit(Hi.undo(h), { a: 9 })), 'una modifica dopo annulla cancella ripeti');
let g = Hi.createHistory({ x: 0 });
for (let i = 1; i <= 20; i++) g = Hi.commit(g, { x: i }, 'move:e1');
eq(g.past.length, 1, 'un trascinamento = un passo');
g = Hi.commit(Hi.endGesture(g), { x: 30 }, 'move:e1');
eq(g.past.length, 2, 'gesto chiuso: passo nuovo');
eq(Hi.commit(g, { x: 30 }), g, 'niente cambia: nessun passo');
let big = Hi.createHistory(0); for (let i = 1; i <= 5000; i++) big = Hi.commit(big, i);
for (let i = 0; i < 5000; i++) big = Hi.undo(big);
eq(big.present, 0, 'illimitati'); check(!Hi.canUndo(big), 'in fondo');
eq(Hi.undo(Hi.createHistory(1)).present, 1, 'annulla a vuoto');
```

- [ ] **Step 2: Verificare che falliscano** — `cd tools/webui-dev && node test-editor.mjs 2>&1 | tail -n 20` → errore d'importazione.
- [ ] **Step 3: Scrivere `history.js`** (array copiati a ogni passo vanno bene: un tema è di pochi KB).
- [ ] **Step 4: Verificare** — `npm test 2>&1 | tail -n 10` → PASS.
- [ ] **Step 5: Commit** — `git add service/internal/webui/static/assets/js/editor/history.js tools/webui-dev/test-editor.mjs tools/webui-dev/package.json && git commit -m "Editor: cronologia con annulla e ripeti illimitati"`

---

### Task 5: Operazioni sul tema

**Files:**
- Create: `assets/js/editor/ops.js`
- Modify: `tools/webui-dev/test-editor.mjs` (sezione 2)

**Interfaces:**
- Consumes: `SCREEN_KEYS` di `theme/lower.js`.
- Produces (tutte pure: restituiscono un tema nuovo, condividono le parti non toccate, non lanciano mai; un argomento non valido → tema invariato). `loc = { screen, variant }`; gli elementi si indicano con il loro `id` (unico in tutto il tema):
  - `export const LOCKED_SCREENS = ['perm', 'pair', 'confirm', 'boot', 'ota', 'battlow', 'calib', 'gbload', 'gbplay']`;
  - `ensureIds(theme) → theme` — `id` mancanti o doppi diventano `e<n>` (il più piccolo libero), anche nei figli dei gruppi e nelle righe delle liste;
  - `newTheme(name, taken: string[]) → theme` — `{format: 'calamaio-theme', version: 1, id: kebabId(name, taken), name, screen: '250x122', screens: {home: {variants: [{when: 'always', elements: []}]}}}`;
  - `kebabId(name, taken) → string` — minuscole senza accenti, non alfanumerici → `-`, al massimo 24, mai `calamaio`, `-2`, `-3`… se preso; vuoto → `tema`;
  - `elementDefaults(type, x, y) → el` (vedi tabella sotto);
  - `findEl(theme, loc, id) → { el, parentId, list: 'elements'|'children'|'row', index, ox, oy, depth } | null` (`ox, oy` = origine assoluta del contenitore; nella riga di una lista = `x, y` della lista);
  - `addEl(theme, loc, el, { parentId = null, index = end } = {}) → { theme, id }` — crea schermata e variante se mancano (`{variants: [{when: 'always', elements: []}]}`); rifiuta (tema invariato, `id` nullo) le schermate fuori da `SCREEN_KEYS`, una `list` dentro una riga, un gruppo oltre il quarto livello;
  - `updateEl(theme, loc, id, patch) → theme` (`undefined` nel patch toglie la chiave);
  - `moveEls(theme, loc, ids, dx, dy) → theme` — salta gli elementi bloccati (anche per un antenato bloccato); un elemento con un antenato già in `ids` si sposta una volta sola;
  - `setBox(theme, loc, id, { x, y, w, h }) → theme` — riquadro assoluto; i tipi a scala (`mascot` 1–4, `battery`/`wifi` 1–2, `stateIcon`/`sym` 1–3) cambiano `scale` = la scala più vicina a `w` diviso la larghezza di base; `text`/`clock`: `w` e, per `text`, `lines` = round(h / lineHeight) fra 1 e 8; `group` sposta e basta;
  - `removeEls(theme, loc, ids) → theme`;
  - `reorder(theme, loc, id, { parentId, index }) → theme` — anche dentro/fuori da gruppi e righe, la posizione assoluta resta;
  - `groupEls(theme, loc, ids) → { theme, id }` — solo con lo stesso contenitore (altrimenti invariato), gruppo in `x, y` = angolo in alto a sinistra dell'unione, figli relativi, nell'ordine di disegno, al posto del più in alto;
  - `ungroup(theme, loc, id) → { theme, ids }` — figli al posto del gruppo, posizioni assolute uguali, `visibleIf` del gruppo aggiunte a quelle dei figli (al massimo 8: oltre, invariato);
  - `duplicateEls(theme, loc, ids) → { theme, ids }` — copia con id nuovi, +8 px in x e y, subito sopra ciascun originale;
  - `copyEls(theme, loc, ids) → clip` (`{ items: [el con x, y assoluti] }`), `pasteEls(theme, loc, clip, { dx = 8, dy = 8, parentId = null } = {}) → { theme, ids }` (id nuovi, in cima);
  - `addVariant(theme, screen, when) → { theme, index }`, `duplicateVariant(theme, screen, i) → { theme, index }`, `removeVariant(theme, screen, i) → theme` (l'ultima variante toglie la schermata), `moveVariant(theme, screen, from, to) → theme`, `updateVariant(theme, screen, i, { when, ref }) → theme`;
  - `fromStock(theme, screen, stock) → theme` — `screens[screen]` = copia di `stock` con id nuovi; mai per `LOCKED_SCREENS`;
  - `renameTheme(theme, name) → theme` (l'`id` non cambia).

Predefiniti di `elementDefaults` (decisione di questo piano): `text` `{w: 80, text: 'Testo', font: {family: 'ui', size: 12, weight: 600}}`; `clock` `{w: 40, font: {family: 'ui', size: 12, weight: 700}}`; `mascot` `{scale: 1}`; `battery`, `wifi`, `stateIcon` `{}`; `sym` `{sym: 'bell'}`; `bar` `{w: 60, h: 6, source: 'context', style: 'bar'}`; `qr` `{w: 40, h: 40, text: '{pc}'}`; `line` `{w: 60, h: 1}`; `rect` `{w: 40, h: 20}`; `circle` `{w: 20, h: 20}`; `list` `{w: 120, h: 60, rowH: 20, gap: 2, row: [text {x: 0, y: 4, w: 120, text: '{riga.nome}'}]}`. Tutti con `type`, `x`, `y` e un `id` nuovo.

- [ ] **Step 1: Prove che falliscono** (sezione 2). Una per voce dell'interfaccia; quelle che fissano scelte:

```js
// 2. operazioni sul tema
const base = O.ensureIds(JSON.parse(fs.readFileSync(path.join(REPO, 'protocol/themes/fixtures/lista.theme.json'), 'utf8')));
const L = { screen: 'home', variant: 0 };
eq(new Set(allIds(base)).size, allIds(base).length, 'ensureIds: doppi e mancanti');
eq(crcOf(base, 'home', 'sei'), crcOf(rawLista, 'home', 'sei'), 'ensureIds non cambia la resa');
eq(O.addEl(base, { screen: 'perm', variant: 0 }, O.elementDefaults('text', 0, 0)).id, null, 'addEl rifiuta perm');
check(!('perm' in O.fromStock(base, 'perm', { variants: [] }).screens), 'fromStock rifiuta perm');
const locked = O.updateEl(base, L, firstId, { locked: true });
eq(O.findEl(O.moveEls(locked, L, [firstId], 5, 0), L, firstId).el.x, O.findEl(locked, L, firstId).el.x, 'moveEls salta i bloccati');
// gruppo: posizioni assolute invariate, ungroup ritorna allo stesso disegno
const g = O.groupEls(base, L, [idA, idB]);
eq(crcOf(g.theme, 'home', 'sei'), crcOf(base, 'home', 'sei'), 'raggruppa: stessa resa');
eq(crcOf(O.ungroup(g.theme, L, g.id).theme, 'home', 'sei'), crcOf(base, 'home', 'sei'), 'separa: stessa resa');
eq(nested4(base).id, null, 'groupEls oltre 4 livelli');
eq(O.kebabId('Città Notte!', ['citta-notte']), 'citta-notte-2', 'kebabId');
eq(O.findEl(O.setBox(withMascot, L, mId, { x: 0, y: 0, w: 60, h: 50 }), L, mId).el.scale, 2, 'setBox: scala della mascotte (60 / 28 → 2)');
const d = O.duplicateEls(base, L, [idA]);
eq(O.findEl(d.theme, L, d.ids[0]).el.x, O.findEl(base, L, idA).el.x + 8, 'duplica +8');
```

(`crcOf(theme, screen, scenario)` = aiuto della prova: `lowerTheme` → `renderScreen` su `Bitmap` col modello dello scenario di `scenarios.json` e gli avatar di `avatars/`, come `tools/golden/theme-golden.mjs`; `allIds`, `nested4` (quattro gruppi annidati, poi `groupEls` sul più interno), `rawLista`, `firstId`, `idA`, `idB`, `withMascot`/`mId` aiuti e figure locali della prova.)

- [ ] **Step 2: Verificare che falliscano** — `node test-editor.mjs 2>&1 | tail -n 20`.
- [ ] **Step 3: Scrivere `ops.js`.**
- [ ] **Step 4: Verificare** — `npm test 2>&1 | tail -n 10` → PASS.
- [ ] **Step 5: Commit** — `git commit -m "Editor: operazioni pure sul tema (elementi, gruppi, varianti, appunti)"` con `ops.js` e la prova.

---

### Task 6: Riquadri, selezione e maniglie

**Files:**
- Create: `assets/js/editor/select.js`
- Modify: `tools/webui-dev/test-editor.mjs` (sezione 3)

**Interfaces:**
- Consumes: `findEl` di `ops.js`.
- Produces:
  - `layout(theme, loc) → Box[]` in ordine di disegno; `Box = { id, type, x, y, w, h, parentId, depth, locked, hidden, path }` assoluti (bloccato e nascosto anche per un antenato; `path` = percorso del servizio, per esempio `screens.home.variants[0].elements[2].children[1]` o `….elements[3].row[0]`). Misure: `mascot` 28·s × 25·s; `text` `w` × `lines`·`lineHeight` (predefiniti di TEMI §2); `clock` `w` × (size + 2); `battery` 16·s × 8·s; `wifi` 11·s × 9·s; `stateIcon`, `sym` 7·s; `group` = unione dei figli non nascosti (vuoto: 0 × 0 in `x, y`); `list` `w` × `h`, le righe nella prima riga della lista;
  - `hitTest(boxes, px, py, { scope = null, slop = 0 } = {}) → id | null` — il più in alto che contiene il punto, senza bloccati e nascosti; `scope` nullo → l'antenato di primo livello; `scope` = id di un gruppo o lista → il figlio diretto; senza colpo esatto, con `slop > 0`, il più vicino entro `slop` (distanza dal bordo del riquadro);
  - `marquee(boxes, rect, scope) → id[]` — interamente dentro, stesso livello di `hitTest`;
  - `selectionBox(boxes, ids) → {x, y, w, h} | null`;
  - `handleNames(type, el) → string[]` — `text` `['w', 'e', 's']`; `clock` `['w', 'e']`; tipi a scala `['se']`; `line` orizzontale `['w', 'e']`, verticale `['n', 's']`; `group` `[]`; gli altri le 8 (`nw n ne e se s sw w`);
  - `handles(box, names, zoom, touch) → [{ name, cx, cy, r }]` in px della tela (`r` = 5 col mouse, 22 col tocco: 44 pt di diametro);
  - `handleAt(handles, sx, sy) → name | null`;
  - `resizeBox(box, name, dx, dy) → {x, y, w, h}` — il lato opposto resta fermo, `w, h` fra 1 e 512.

- [ ] **Step 1: Prove che falliscono** (sezione 3), fra cui:

```js
// 3. riquadri e selezione
const bx = S.layout(base, L);
check(bx.every((b) => Number.isInteger(b.x + b.y + b.w + b.h)), 'riquadri interi');
eq(rowBox.y, listBox.y + rowEl.y, 'layout: righe della lista e gruppi annidati'); eq(deepBox.x, g1.x + g2.x + deepEl.x, 'layout: gruppi annidati sommano le posizioni');
eq(S.hitTest(bx, inner.x + 1, inner.y + 1), groupId, 'senza scope: il gruppo');
eq(S.hitTest(bx, inner.x + 1, inner.y + 1, { scope: groupId }), inner.id, 'dentro il gruppo: il figlio');
eq(S.hitTest(lockedBoxes, lk.x + 1, lk.y + 1), null, 'hitTest salta i bloccati');
eq(S.marquee(hiddenBoxes, { x: 0, y: 0, w: 250, h: 122 }, null).includes(hid), false, 'marquee salta i nascosti');
eq(S.hitTest(bx, tiny.x + tiny.w + 3, tiny.y, { slop: 4 }), tiny.id, 'slop col tocco');
eq(JSON.stringify(S.resizeBox({ x: 10, y: 10, w: 20, h: 20 }, 'nw', 5, 5)), JSON.stringify({ x: 15, y: 15, w: 15, h: 15 }), 'resize nw');
eq(S.resizeBox({ x: 10, y: 10, w: 20, h: 20 }, 'e', -40, 0).w, 1, 'larghezza minima 1');
check(S.handles(bx[0], ['se'], 4, true)[0].r * 2 >= 44, 'maniglie da 44 pt col tocco');
```

- [ ] **Step 2–4:** fallire, scrivere `select.js`, passare (`npm test 2>&1 | tail -n 10`).
- [ ] **Step 5: Commit** — `git commit -m "Editor: riquadri, colpo del puntatore, selezione a riquadro e maniglie"`

---

### Task 7: Aggancio, allinea/distribuisci, scorciatoie

**Files:**
- Create: `assets/js/editor/geometry.js`, `assets/js/editor/keys.js`
- Modify: `tools/webui-dev/test-editor.mjs` (sezione 4)

**Interfaces:**
- Consumes: `Box` e `selectionBox` del Task 6.
- Produces:
  - `snapMove(boxes, ids, dx, dy, { grid = 1, others = true, threshold = 3 } = {}) → { dx, dy, guides: [{ axis: 'x'|'y', at }] }` — lati e centro dell'unione selezionata contro lati e centri degli altri riquadri visibili non selezionati e contro 0/125/250 in x e 0/61/122 in y; entro `threshold` vince l'aggancio più vicino; altrimenti, con `grid > 1`, l'angolo in alto a sinistra va al multiplo più vicino;
  - `snapResize(boxes, id, rect, handle, opts) → { rect, guides }` — solo i lati che la maniglia muove;
  - `align(boxes, ids, mode) → Map<id, {dx, dy}>` — `mode` `left|hcenter|right|top|vcenter|bottom`; con un solo elemento rispetto allo schermo; bloccati esclusi (né mossi né riferimento);
  - `distribute(boxes, ids, axis) → Map<id, {dx, dy}>` — `axis` `h|v`, spazi uguali fra il primo e l'ultimo, almeno 3 elementi (altrimenti vuota);
  - `keys.js`: `keyAction({ key, shiftKey, metaKey, ctrlKey, altKey }, mac) → action | null` con `action` uno di `{type: 'nudge', dx, dy}`, `'undo'`, `'redo'`, `'copy'`, `'paste'`, `'duplicate'`, `'group'`, `'ungroup'`, `'delete'`, `'escape'`, `'selectAll'`, `'enter'` (oggetti `{type}`). Tasti: frecce 1 px, Maiusc+frecce 8 px; ⌘/Ctrl+Z, ⇧⌘Z e Ctrl+Y, ⌘C, ⌘V, ⌘D, ⌘G, ⇧⌘G, Canc/Backspace, Esc, ⌘A, Invio (`mac` sceglie ⌘ o Ctrl).

- [ ] **Step 1: Prove che falliscono** (sezione 4), fra cui:

```js
// 4. aggancio, allineamento, tasti
// two: a = {x: 2, y: 10, w: 10, h: 10}, b = {x: 100, y: 24, w: 20, h: 20}
eq(G.snapMove(two, ['a'], 7, 0, { grid: 8, others: false }).dx, 6, 'griglia 8: x da 9 a 8');
const s = G.snapMove(two, ['a'], 0, 2, { grid: 1 });  // fondo di a a 22, alto di b a 24
eq(s.dy, 4, 'aggancio a un altro elemento entro 3 px'); check(s.guides.some((g) => g.axis === 'y' && g.at === 24), 'guida a y = 24');
eq(G.snapMove(two, ['a'], 50, 0, { grid: 8 }).dx, 54, 'nessun elemento entro 3 px: vince la griglia (x 52 → 56)');
eq(G.align(three, ['a', 'b', 'c'], 'left').get('b').dx, three[0].x - three[1].x, 'allinea a sinistra');
check(!G.align(withLocked, ['a', 'lk'], 'left').has('lk'), 'align salta i bloccati');
eq(G.distribute(two, ['a', 'b'], 'h').size, 0, 'distribuisci: almeno 3');
eq(JSON.stringify(K.keyAction({ key: 'ArrowLeft', shiftKey: true }, true)), JSON.stringify({ type: 'nudge', dx: -8, dy: 0 }), 'Maiusc+freccia = 8');
eq(K.keyAction({ key: 'z', metaKey: true, shiftKey: true }, true).type, 'redo', '⇧⌘Z');
eq(K.keyAction({ key: 'y', ctrlKey: true }, false).type, 'redo', 'Ctrl+Y');
```

(`three` e `withLocked`: figure scritte con numeri letterali nella prova, come `two`.)

- [ ] **Step 2–4:** fallire, scrivere, passare.
- [ ] **Step 5: Commit** — `git commit -m "Editor: aggancio a griglia ed elementi, allinea e distribuisci, scorciatoie"`

---

### Task 8: Dati di prova, anteprima della variante, avvisi

**Files:**
- Create: `assets/js/editor/testdata.js`, `assets/js/editor/warnings.js`
- Modify: `tools/webui-dev/test-editor.mjs` (sezione 5)

**Interfaces:**
- Consumes: `makeModel`, `whenTrue` di `theme/values.js`; `lowerTheme` di `theme/lower.js`; `renderScreen`; `textWidthMilli`, `wrapLines`, `hasFont` di `theme/text.js`; `glyphWarnings` del Task 2; `layout` del Task 6.
- Produces:
  - `testdata.js`: `export const BASE` (= `base` di `scenarios.json`, copiato), `export const SESSIONS = { 0: [], 1: [...], 6: [...] }` (= `sessions` degli scenari `zero`, `una`, `sei`); `testModel({ sessions = 1, lowBatt = false, night = false, perm = false, pcOff = false, frame = 0, now = BASE.now } = {}) → model` (`makeModel`): `lowBatt` → `sys.batt = 9`, `bat.trend = 1.5`; `night` → `set.night = true`; `pcOff` → `sys.pc = false`, `sys.pcOff = true`; `perm` → la prima sessione in `permission` con `detail: 'Bash: rm -rf build'` (con 0 sessioni ne aggiunge una, `name: 'api-server'`); `now` sposta anche `local` della stessa quantità;
  - `warnings.js`: `previewTheme(theme, loc) → lowered` — `lowerTheme` di una copia con la sola variante `loc.variant` in `when: 'always'` (il `ref` resta);
  - `boardVariant(theme, screen, model) → number` — indice della prima variante vera (`whenTrue`) o −1;
  - `liveWarnings(theme, loc, model, boxes, check) → Warning[]` con `Warning = { id: string | null, kind, msg }`, `kind` uno di `fit|offscreen|outside|covered|glyph|empty|service|size`. `check` = l'ultima `Info` del servizio (`{screens, warnings, problems}`) o `null`. Messaggi:
    - `fit` (testo espanso col modello: una riga, `textWidthMilli` > `w`·1000) «Il testo non ci sta: si vede troncato con «…»» (Silkscreen: «..»); più righe (`wrapLines(t, lines + 1, …)` più lunga di `lines`) «Il testo non ci sta in N righe»;
    - `offscreen` «Esce dallo schermo: si vede tagliato»; `outside` «Fuori dallo schermo: non si vede»;
    - `covered` (riquadro interamente dentro un `rect` con `fill` o un `qr` disegnato dopo, entrambi visibili) «Coperto da «<nome>»: non si vede»;
    - `glyph` = `glyphWarnings(el)`;
    - `empty` (variante senza elementi) «Variante senza elementi: la scheda mostrerà lo schermo vuoto»;
    - `service`: ogni problema e avviso di `check` il cui percorso cade in questa schermata e variante, legato all'elemento con quel `path` (o al suo antenato più vicino; `id` nullo se nessuno); messaggio = la parte dopo `": "`; gli avvisi dei glifi del servizio si saltano (li dà già `glyph`);
    - `size`: con `check.screens[screen]` > 3 686 (90 %) «Schermata di N byte: vicina al limite di 4 096»; oltre il limite lo dice già il problema del servizio;
  - `elementLabel(el) → string` — `name` o, per tipo: «Testo», «Orologio», «Mascotte», «Batteria», «Wi-Fi», «Icona di stato», «Barra», «QR», «Simbolo», «Linea», «Rettangolo», «Cerchio», «Lista», «Gruppo».

- [ ] **Step 1: Prove che falliscono** (sezione 5), fra cui:

```js
// 5. dati di prova e avvisi
const sc = JSON.parse(fs.readFileSync(path.join(REPO, 'protocol/themes/scenarios.json'), 'utf8'));
eq(JSON.stringify(D.BASE), JSON.stringify(sc.base), 'BASE = scenarios.json');
for (const [n, name] of [[0, 'zero'], [1, 'una'], [6, 'sei']]) eq(JSON.stringify(D.SESSIONS[n]), JSON.stringify(sc.scenarios.find((s) => s.name === name).sessions ?? []), 'sessioni ' + n);
eq(D.testModel({ sessions: 0, perm: true }).sessions[0].state, 'permission', 'permesso con 0 sessioni');
eq(D.testModel({ pcOff: true }).sys.pc, false, 'PC scollegato');
const costo = themeWith({ type: 'text', id: 't', x: 0, y: 0, w: 200, text: '{oggi.costo}', font: { family: 'ui', size: 44, weight: 700 } });
check(W.liveWarnings(costo, L, D.testModel(), S.layout(costo, L), null).some((w) => w.kind === 'glyph' && w.id === 't' && w.msg.includes('«$»')), 'glifo $ a 44');
check(W.liveWarnings(fuori, L, m, S.layout(fuori, L), null).some((w) => w.kind === 'outside'), 'fuori dallo schermo');
check(W.liveWarnings(lungo, L, m, S.layout(lungo, L), null).some((w) => w.kind === 'fit' && w.msg === 'Il testo non ci sta: si vede troncato con «…»'), 'non ci sta');
const svc = { screens: { home: 900 }, warnings: [], problems: [{ path: 'screens.home.variants[0].elements[1].children[0]', msg: 'x: fuori da ±512' }] };
eq(W.liveWarnings(conGruppo, L, m, S.layout(conGruppo, L), svc).find((w) => w.kind === 'service').id, figlio0Id, "problemi del servizio sull'elemento giusto");
// anteprima = scheda: per ogni caso drawn del golden, la variante scelta dalla scheda resa con previewTheme ha lo stesso CRC
for (const c of golden.cases.filter((c) => c.drawn)) {
  const t = fixture(c.theme), m = scenarioModel(c.scenario), vi = W.boardVariant(t, c.screen, m);
  eq(crc(renderScreen(W.previewTheme(t, { screen: c.screen, variant: vi }), c.screen, m, bm(), assets)), c.crc, 'parità ' + c.theme + '/' + c.screen + '/' + c.scenario);
}
// preview: tema rotto non lancia
check(!renderScreen(W.previewTheme({ screens: { home: { variants: [{ elements: [{ type: 'text', x: 600, text: '{oggi.co' }] }] } } }, L), 'home', m, bm(), assets).drawn, 'preview: tema rotto non lancia, schermata di serie');
```

(`themeWith`, `fuori`, `lungo`, `conGruppo`/`figlio0Id`: temi di una schermata scritti nella prova; `golden` = `firmware/test/test_theme_parity/golden.json`; `fixture`, `scenarioModel`, `bm`, `crc`, `assets` come in `tools/golden/theme-golden.mjs`.)

- [ ] **Step 2–4:** fallire, scrivere, passare.
- [ ] **Step 5: Commit** — `git commit -m "Editor: dati di prova, anteprima della variante e avvisi in tempo reale"`

---

### Task 9: Pagina, stato, anteprima ingrandita e cassetti

Da qui il codice tocca il DOM. Le parti logiche stanno in `actions.js` (provato in Node con un `api` finto); i moduli `ui-*.js` restano sottili.

**Files:**
- Create: `assets/js/editor/store.js`, `actions.js`, `assets.js`, `view.js`, `catalog.js`, `ui-canvas.js` (solo disegno in questo task), `tools/webui-dev/smoke-editor.mjs`
- Modify: `static/editor.html` (riscritto), `assets/js/editor.js` (riscritto), `assets/app.css` (sezione «Editor»), `tools/webui-dev/shots.mjs` (blocco dell'editor), `tools/webui-dev/package.json` (script `smoke`: `node smoke-editor.mjs`), `tools/webui-dev/test-editor.mjs` (sezione 6)

**Interfaces:**
- Consumes: tutto dai Task 4–8; `loadAvatar`, `loadAnchors`, `loadAccessory` (`avatar.js`, `api.js`); `Bitmap` (`theme/surface.js`).
- Produces:
  - `store.js`: `createStore(state) → { get(), set(patch | (s) => patch), subscribe(fn) → unsubscribe }` (notifica una volta per `set`, in microtask);
  - stato iniziale (`initialState()` in `actions.js`): `{ themes: [], themeId: null, hist: null, check: null, loc: { screen: 'home', variant: 0 }, sel: [], scope: null, zoom: 4, grid: true, snap: { grid: 1, others: true }, test: { sessions: 1, lowBatt: false, night: false, perm: false, pcOff: false }, playing: false, frame: 0, clock: 0, save: { state: 'saved', msg: '', base: 0 }, send: null, clip: null, touchMulti: false, drawer: null, error: '' }` (`drawer`: `null|'left'|'right'`);
  - `actions.js`: `createActions(store, api, { now = Date.now, setTimeout, clearTimeout } = {}) → A` con, in questo task, `A.init()`, `A.openTheme(id)`, `A.setLoc(loc)`, `A.commit(fn, merge = null)` (`fn(theme) → theme` o `{theme, ids}`; aggiorna `hist`, e `sel` se `ids`), `A.endGesture()`, `A.undo()`, `A.redo()`, `A.select(ids)`, `A.setZoom(z)` (1–6), `A.setDrawer(d)`; gli altri metodi li aggiungono i task seguenti. `api` = `{ listThemes, getTheme, createTheme, importTheme, putTheme, deleteTheme, checkTheme, sendTheme, status }` (in `api.js`, Task 13 e 14; qui solo i primi tre e `checkTheme`); cronologie per tema tenute in una `Map` per tutta la sessione;
  - `assets.js`: `createAssets() → { ensure(model) → Promise, avatar(id), accessory(id) }` — avatar con ancore nella forma di `loadWithAnchors` (`tools/avatar/lib/anchors.mjs`), da `GET /api/avatars/{id}` + `GET /api/avatars/{id}/anchors`; accessori da `GET /api/accessories/{id}`; un errore → `null` (il renderer ripiega sulla Goccia);
  - `view.js`: `renderPreview(theme, loc, model, assets) → { bitmap: Bitmap, info }` (`renderScreen(previewTheme(...))`), `paintBitmap(canvas, bitmap, zoom, dpr)` (un pixel della scheda = `zoom·dpr` pixel del canvas, inchiostro `#1E2022`, carta `#D6D7CF`, `imageSmoothingEnabled = false`), `paintOverlay(canvas, { boxes, sel, handles, guides, marquee, warned, zoom, dpr, grid })` (griglia da ×3, riquadri selezionati, maniglie, guide, riquadro di selezione, elementi con avvisi tratteggiati);
  - `catalog.js`: `SCREENS` = `[{ key, label }]` delle 12 personalizzabili (Home, Dettaglio, Riposo, PC non raggiungibile, PC spento, Oggi, Batteria, Stato, Limiti, Focus, Ambiente, La mascotte) + `{ key: 'perm', label: 'Permessi — bloccata (regole di sicurezza)', locked: true }`; `TYPES` (tipi aggiungibili con etichetta di `elementLabel`), `FONT_CHOICES` (le 22 combinazioni, etichetta «Chakra Petch 12 · 600», «Silkscreen 8 · 400»), `WHEN` (`always` «sempre», `none` «nessuna sessione», `one` «una sessione», `many` «più sessioni»), `REFS`, `CONDS` (per l'editor delle condizioni, Task 12).

Impaginazione (`editor.html`, classi con prefisso `ed-`): barra in alto `#ed-top`; sinistra `#ed-left` (`#ed-add`, `#ed-layers`); centro `#ed-stage` con `canvas#ed-screen` e `canvas#ed-overlay` sovrapposti dentro `#ed-viewport` (`overflow: hidden`, spostamento con `transform`); destra `#ed-right` (`#ed-props`); avvisi `#ed-warnings` sotto la tela. Sopra i 900 px tre colonne (sinistra 240 px, destra 280 px); sotto, i pannelli diventano cassetti a scomparsa sopra la tela (`#ed-left`/`#ed-right` con `data-open`), aperti dai pulsanti «Elementi» e «Proprietà» nella barra (≥ 44×44 pt), chiusi da «Chiudi» o toccando fuori. `nav.topbar` resta quella delle altre pagine. Zoom iniziale = il più grande intero che fa stare 250 px nella larghezza di `#ed-viewport` (minimo 1, massimo 6). Lo schermo personalizzato assente: la tela mostra la schermata vuota con la scritta «Schermata di serie: aggiungi un elemento o parti dalla schermata di serie».

- [ ] **Step 1: Prove Node che falliscono** (sezione 6)

```js
// 6. stato e azioni (api finta in memoria)
const st = createStore(initialState()); const A = createActions(st, fakeApi(), fakeTimers);
await A.init(); eq(st.get().themes.length, 1, 'elenco dei temi');
await A.openTheme('prova'); check(st.get().hist.present.id === 'prova', 'tema aperto');
A.commit((t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 10, 10)));
eq(st.get().sel.length, 1, 'il nuovo elemento è selezionato'); A.undo(); eq(st.get().sel.length, 0, 'annulla toglie la selezione che non esiste più');
A.setLoc({ screen: 'perm', variant: 0 }); eq(st.get().loc.screen, 'home', 'Permessi non si apre');
// nessun disegno fuori dal renderer condiviso
for (const f of fs.readdirSync(EDITOR_DIR)) { const src = fs.readFileSync(path.join(EDITOR_DIR, f), 'utf8');
  check(!/from '\.\.\/theme\/prims\.js'|from '\.\.\/mascot\.js'|drawAvatar|\bdrawText\b|\bT\(/.test(src), 'parità: ' + f + ' non disegna da sé'); }
```

- [ ] **Step 2: Prova nel browser che fallisce** — `smoke-editor.mjs` (stile di `shots.mjs`: server finto su 8798, `watch` della console): Chromium 1280×900 e WebKit 393×852 (`hasTouch: true`, `isMobile: true`, `deviceScaleFactor: 3`); per ora: la pagina si apre, compare `canvas#ed-screen` con il tema `prova` disegnato (almeno un pixel d'inchiostro letto con `getImageData`), `document.documentElement.scrollWidth <= innerWidth` a 393, il selettore delle schermate ha l'opzione «Permessi — bloccata (regole di sicurezza)» disattivata. Registra ogni richiesta della pagina e la confronta con `phone-routes.json` (modelli Go: `{x}` = un segmento, `/` finale = prefisso, `{$}` = esatto; metodo uguale): una fuori elenco è un errore. Esce con codice 1 se c'è un problema.

- [ ] **Step 3: Verificare che falliscano** — `cd tools/webui-dev && node test-editor.mjs 2>&1 | tail -n 20; node smoke-editor.mjs 2>&1 | tail -n 20`.

- [ ] **Step 4: Scrivere** i moduli, la pagina, il CSS; `editor.js` = `createStore` + `createActions(…, realApi)` + `mountCanvas`; in `shots.mjs` il blocco dell'editor aspetta `#ed-screen[data-drawn]` (attributo messo da `ui-canvas.js` dopo il primo disegno) invece della Goccia.

- [ ] **Step 5: Verificare** — `npm test 2>&1 | tail -n 10` e `node smoke-editor.mjs 2>&1 | tail -n 20` → PASS; `node shots.mjs 2>&1 | tail -n 10` → nessun problema; guardare `out/editor-1280.png` e `out/editor-390.png`.

- [ ] **Step 6: Commit** — `git commit -m "Editor: pagina con tre pannelli e cassetti, stato, anteprima ingrandita dal renderer condiviso"`

---

### Task 10: Tela interattiva: mouse, tocco, tastiera

**Files:**
- Create: `assets/js/editor/gesture.js`
- Modify: `assets/js/editor/ui-canvas.js`, `assets/js/editor/actions.js`, `tools/webui-dev/test-editor.mjs` (sezione 7), `tools/webui-dev/smoke-editor.mjs`

**Interfaces:**
- Consumes: `hitTest`, `marquee`, `handles`, `handleAt`, `resizeBox` (Task 6); `snapMove`, `snapResize`, `keyAction` (Task 7); `moveEls`, `setBox`, `removeEls`, `duplicateEls`, `copyEls`, `pasteEls`, `groupEls`, `ungroup` (Task 5).
- Produces:
  - `gesture.js`: `createGesture() → g` con `g.down(p, ctx) → intent | null`, `g.move(p, ctx) → intent | null`, `g.up(p, ctx) → intent | null`, `g.cancel() → intent | null`; `p = { id, x, y, type: 'mouse'|'pen'|'touch', shift, time }` in coordinate della scheda (frazioni ammesse); `ctx = { boxes, sel, scope, zoom, handles, multi }`. Intenti: `{ kind: 'select', ids, add }`, `{ kind: 'enter', id }` (doppio clic/doppio tocco entro 300 ms su un gruppo o una lista), `{ kind: 'move', dx, dy, done }`, `{ kind: 'resize', id, handle, dx, dy, done }`, `{ kind: 'marquee', rect, done }`, `{ kind: 'pan', dx, dy }` (px dello schermo), `{ kind: 'zoom', factor, cx, cy, done }`, `{ kind: 'deselect' }`. Regole: mouse/penna — su maniglia → resize; su elemento → select (Maiusc aggiunge/toglie) e move dopo 2 px; su vuoto → marquee. Tocco — su maniglia → resize; su elemento → select (con `multi` aggiunge/toglie) e move dopo **6 px**; su vuoto → pan; tocco breve su vuoto → deselect; due dita → zoom (rapporto delle distanze) e pan (centro); il secondo dito annulla un move in corso (`move` con `dx = dy = 0, done: true`). Colpi col tocco con `slop` = 22 / zoom;
  - `actions.js`: `A.moveSel(dx, dy, merge)`, `A.resizeSel(id, rect, merge)`, `A.deleteSel()`, `A.duplicateSel()`, `A.copySel()`, `A.paste()`, `A.groupSel()`, `A.ungroupSel()`, `A.enter(id)`, `A.exitScope()`, `A.selectAll()`, `A.key(action)` (esegue un'azione di `keyAction`); lo zoom del pizzico si arrotonda all'intero alla fine del gesto.
  - `ui-canvas.js`: Pointer Events con `setPointerCapture`, `touch-action: none` sulla tela; `keydown` sul documento solo quando il fuoco non è in `input`, `select`, `textarea` o `[contenteditable]`; guide e riquadro di selezione in `paintOverlay`; pulsanti «−» e «+» dello zoom e «Seleziona più» (solo col tocco, `aria-pressed`), tutti ≥ 44×44 pt.

- [ ] **Step 1: Prove che falliscono** (sezione 7), fra cui:

```js
// 7. gesti
const gs = Ge.createGesture(), ctx = ctxOf(bx, [], 4);
eq(gs.down(pt('touch', a.x + 1, a.y + 1), ctx).kind, 'select', 'tocco su elemento: seleziona');
eq(gs.move(pt('touch', a.x + 1 + 1, a.y + 1), ctx), null, 'tocco: 1 px non sposta (soglia 6 px)');
eq(gs.move(pt('touch', a.x + 1 + 10, a.y + 1), ctx).kind, 'move', 'poi sposta');
const g2 = Ge.createGesture(); g2.down(pt('touch', 240, 5, 1), ctxOf(bx, [], 4));
eq(g2.move(pt('touch', 230, 5, 1), ctxOf(bx, [], 4)).kind, 'pan', 'un dito sul vuoto: sposta la vista');
const g3 = Ge.createGesture(); g3.down(pt('touch', 100, 50, 1), ctx); g3.down(pt('touch', 120, 50, 2), ctx);
eq(g3.move(pt('touch', 130, 50, 2), ctx).kind, 'zoom', 'due dita: pizzica');
const g4 = Ge.createGesture(); eq(g4.down(pt('mouse', 240, 5), ctx), null); eq(g4.move(pt('mouse', 200, 40), ctx).kind, 'marquee', 'mouse sul vuoto: riquadro');
check(withMulti(Ge.createGesture(), b.id).add, 'Seleziona più: aggiunge');
```

e in `smoke-editor.mjs`: a 1280 clic su un elemento, trascinamento di 20 px col mouse, ⌘Z/Ctrl+Z riporta la posizione (letta dall'attributo `data-x` del pannello o dallo stato esposto su `window.__editor` solo con `?test=1`), freccia e Maiusc+freccia spostano di 1 e 8; a 393 `page.touchscreen.tap` su un elemento lo seleziona e un trascinamento col tocco (eventi `pointerdown/move/up` con `pointerType: 'touch'` inviati con `dispatchEvent`) lo sposta.

- [ ] **Step 2–4:** fallire, scrivere, passare (`npm test`, `node smoke-editor.mjs`).
- [ ] **Step 5: Commit** — `git commit -m "Editor: tela con trascinamento, maniglie, selezione multipla, pizzico e tastiera"`

---

### Task 11: «Aggiungi» e «Livelli»

**Files:**
- Create: `assets/js/editor/ui-layers.js`
- Modify: `assets/js/editor/actions.js`, `assets/js/editor.js`, `assets/app.css`, `tools/webui-dev/test-editor.mjs` (sezione 8), `tools/webui-dev/smoke-editor.mjs`

**Interfaces:**
- Consumes: `elementDefaults`, `addEl`, `reorder`, `updateEl` (Task 5); `layout` (Task 6); `elementLabel` (Task 8); `TYPES` (Task 9).
- Produces:
  - `actions.js`: `A.add(type)` — elemento al centro della parte visibile della tela (arrotondato, dentro lo schermo), nel contenitore di `scope` se c'è, selezionato, cassetto chiuso sul telefono; `A.reorderEl(id, parentId, index)`; `A.toggleHidden(id)`; `A.toggleLocked(id)`; `A.renameEl(id, name)` (al massimo 32 byte, vuoto toglie `name`);
  - `layerRows(theme, loc, sel, scope) → [{ id, depth, label, type, hidden, locked, selected, container, hiddenByData }]` in `ui-layers.js` (puro, esportato per la prova): ordine dei livelli = dal più in alto (ultimo disegnato) al più in basso, figli sotto il loro gruppo/lista con `depth + 1`; `hiddenByData` = `visibleIf` falso coi dati di prova (testo «nascosto con questi dati»).
  - Interfaccia: «Aggiungi» = griglia di pulsanti dei tipi di `TYPES` (testo e icona semplice in CSS, ≥ 44×44 pt); «Livelli» = elenco con, per riga, maniglia di trascinamento (Pointer Events, funziona col tocco), nome (doppio clic o pulsante «Rinomina» → campo di testo), «Nascondi»/«Mostra» (occhio, `aria-pressed`), «Blocca»/«Sblocca» (lucchetto, `aria-pressed`), «Su» e «Giù» (alternativa al trascinamento, accessibile da tastiera); clic seleziona, Maiusc+clic aggiunge.

- [ ] **Step 1: Prove che falliscono** (sezione 8): `layerRows` mette l'ultimo elemento in cima, i figli di un gruppo con `depth` 1 sotto il gruppo, `hiddenByData` per un elemento con `visibleIf: [{perm: true}]` coi dati senza permesso; `A.add('text')` con `scope` = un gruppo crea il testo dentro il gruppo; `A.renameEl` con 40 byte taglia a 32 su un confine di carattere; `A.toggleLocked` poi `A.moveSel` non sposta. Nella prova del browser: «Aggiungi › Testo» a 393 apre e chiude il cassetto e il nuovo livello compare in «Livelli»; trascinare la riga in fondo cambia l'ordine di disegno (il CRC della tela cambia, `__editor` con `?test=1`).
- [ ] **Step 2–4:** fallire, scrivere, passare.
- [ ] **Step 5: Commit** — `git commit -m "Editor: pannelli Aggiungi e Livelli (ordine, nascondi, blocca, rinomina)"`

---

### Task 12: Proprietà, condizioni, valori, allinea e aggancio

**Files:**
- Create: `assets/js/editor/ui-props.js`
- Modify: `assets/js/editor/actions.js`, `assets/js/editor/catalog.js`, `assets/app.css`, `tools/webui-dev/test-editor.mjs` (sezione 9), `tools/webui-dev/smoke-editor.mjs`

**Interfaces:**
- Consumes: `updateEl`, `setBox` (Task 5); `align`, `distribute` (Task 7); `VALUE_GLYPHS` (esempi, Task 2); `FONT_CHOICES`, `WHEN`, `REFS`, `CONDS` (Task 9).
- Produces:
  - `catalog.js`: `fieldsFor(type) → Field[]` (puro), `Field = { key, label, kind: 'int'|'select'|'bool'|'template'|'font'|'conds', min, max, options }`, con i campi e i limiti di TEMI §2 per tipo (più `x`, `y`, `w`, `h` dove servono, `invert`, `visibleIf`); `CONDS` = `perm` «Permesso in attesa», `battBelow` «Batteria sotto N %» (1–100), `charging` «In carica», `night` «Notte», `pc` «PC collegato», `workday` «Giornata di lavoro attiva», `game` «Gioco della mascotte acceso», `sessions` «Sessioni da/a» (`min`, `max` 0–6), `state` «Stato della sessione» (gli stati di `STATES` con le etichette di `INFO`), `blink` «Lampeggio» (`odd` «fotogramma dispari», `even` «pari»); le booleane con «sì»/«no»;
  - `actions.js`: `A.setProp(id, key, value)` (unisce i tasti successivi sullo stesso campo in un passo: `merge = 'prop:' + id + ':' + key`), `A.alignSel(mode)`, `A.distributeSel(axis)`, `A.setSnap({ grid, others })`;
  - `ui-props.js`: nessuna selezione → proprietà della variante (`when`, `ref`) e della schermata; più selezionati → solo posizione, allinea/distribuisci, raggruppa; uno → tutti i campi di `fieldsFor`. Il campo `template` ha «Inserisci valore» che apre l'elenco dei valori (nome, esempio di `VALUE_GLYPHS`, filtri «maiuscolo»/«minuscolo»), con `riga.*` solo dentro una lista. Posizione con pulsanti «−1», «+1», «−8», «+8» per x e y (per il tocco). Allinea: sei pulsanti; distribuisci: due; aggancio: griglia 1/2/4/8 e «Aggancia agli elementi».

- [ ] **Step 1: Prove che falliscono** (sezione 9): `fieldsFor('mascot')` ha `scale` 1–4 e `pose` con `auto` + le 9 pose; `fieldsFor('text')` non ha `h`; `FONT_CHOICES` ha 22 voci, una per combinazione di `f_registry.h`; `A.setProp` con 5 tasti di fila dà un solo passo di annulla; `A.alignSel('left')` su 3 elementi; un valore `riga.nome` non è proposto fuori da una lista (`valueChoices(inRow)` puro, esportato). Nel browser: a 393 il cassetto «Proprietà» si apre, il campo «Testo» accetta `{oggi.costo}` con il carattere «Chakra Petch 44 · 700» e in `#ed-warnings` compare l'avviso del «$» entro 1 s.
- [ ] **Step 2–4:** fallire, scrivere, passare.
- [ ] **Step 5: Commit** — `git commit -m "Editor: pannello Proprietà con condizioni, valori, allinea, distribuisci e aggancio"`

---

### Task 13: Barra in alto: temi, salvataggio automatico, dati di prova, animazione, avvisi

**Files:**
- Create: `assets/js/editor/ui-topbar.js`, `assets/js/editor/ui-warnings.js`, `assets/js/theme/stock.js`
- Modify: `assets/js/api.js`, `assets/js/editor/actions.js`, `assets/js/editor.js`, `assets/app.css`, `tools/webui-dev/test-editor.mjs` (sezione 10), `tools/webui-dev/smoke-editor.mjs`

**Interfaces:**
- Consumes: Task 3 (rotte), 5 (`newTheme`, `renameTheme`, varianti, `fromStock`), 8 (`testModel`, `liveWarnings`, `boardVariant`).
- Produces:
  - `api.js`: `listThemes() → Info[]`, `getTheme(id) → object`, `createTheme(theme) → Info`, `importTheme(text) → Info`, `putTheme(id, theme, { base }) → Info` (`?autosave=1&base=`; 409 → lancia `ConflictError` con `.theme` = Info del file vivo), `deleteTheme(id)`, `checkTheme(theme) → Info`; gli errori 400 lanciano `InvalidError` con `.problems`;
  - `stock.js`: `export const STOCK = {}` — le schermate di serie come dati, chiave = schermata, valore = `{variants}`; vuoto in questa fase (la riempie la fase 3, spec §7);
  - `actions.js`: `A.newTheme(name)`, `A.duplicateTheme()` (import del tema corrente con nome «<nome> copia» tagliato a 24 byte: l'id lo sceglie il servizio), `A.renameTheme(name)`, `A.deleteTheme()` (dopo la conferma della pagina), `A.exportTheme() → { filename: '<id>.theme.json', text }`, `A.importTheme(text)`, `A.setTest(patch)`, `A.togglePlay()`, `A.fromStock()`, `A.addVariant(when)`, `A.removeVariant()`, `A.moveVariant(dir)`, `A.retrySave()`, `A.reload()`, `A.overwrite()`; ciclo di salvataggio: a ogni `commit` stato `pending`, `checkTheme` dopo 300 ms (risultato in `check`, scartato se nel frattempo il tema è cambiato), `putTheme` dopo 1 500 ms senza modifiche e senza gesti aperti, solo se `check.problems` è vuoto (altrimenti `invalid`); stati e testi della barra: `saved` «Salvato», `pending` «Modifiche in sospeso…», `saving` «Salvo…», `invalid` «Non salvato: N problemi», `conflict` «Cambiato altrove» (con «Ricarica» e «Sovrascrivi»), `error` «Non salvato: <messaggio>» (con «Riprova»); `beforeunload` chiede conferma se lo stato non è `saved`;
  - animazione: con `playing` un solo timer di 500 ms alterna `frame` 0/1 e porta `clock` (ms dall'inizio) che `testModel({ now: BASE.now + clock })` usa; fermo = fotogramma 0 e ora di `BASE`;
  - `ui-topbar.js`: tema (selettore, «Nuovo», «Duplica», «Rinomina», «Elimina», «Esporta», «Importa»), schermata (`SCREENS`, Permessi disattivata), variante (selettore «1 · sempre», «2 · più sessioni», …, più «Aggiungi variante», «Togli», «Su», «Giù»), «Annulla», «Ripeti», dati di prova (sessioni 0/1/6 e quattro interruttori «Batteria scarica», «Notte», «Permesso in attesa», «PC scollegato»), «Anima», «Parti dalla schermata di serie» (disattivato con il titolo «Arriva con le schermate di serie come dati» se `STOCK[schermata]` manca; se la schermata ha già varianti chiede «Sostituire la schermata con quella di serie?»), stato del salvataggio, «Invia alla scheda» (Task 14). Sotto i 900 px le voci meno usate (tema, dati di prova, «Parti dalla schermata di serie») passano in un menu «Altro» (≥ 44 pt), senza scorrimento orizzontale. «Esporta» scarica un Blob con `<a download>` (nell'app diventa la condivisione); «Importa» = `<input type="file" accept=".json,application/json">`. Conferma di «Elimina»: «Eliminare il tema «<nome>»? Si perde anche la cronologia. Sulla scheda resta finché non ne invii un altro.»;
  - avviso della variante: se `boardVariant` coi dati di prova ≠ variante aperta, sopra la tela «Con questi dati di prova la scheda mostrerebbe la variante N» o «… la schermata di serie»; se `renderPreview` dà `drawn` falso, «La scheda mostrerebbe la schermata di serie: c'è un elemento fuori dai limiti»;
  - `ui-warnings.js`: elenco di `liveWarnings` con contatore nella barra («N avvisi»), un clic seleziona l'elemento; misura «N / 4 096 byte» della schermata da `check.screens`.

- [ ] **Step 1: Prove che falliscono** (sezione 10, con timer finti):

```js
// 10. salvataggio automatico
A.commit(addRect); eq(st.get().save.state, 'pending', 'in sospeso');
timers.advance(300); await flush(); check(st.get().check, 'controllo dopo 300 ms');
timers.advance(1200); await flush(); eq(api.puts.length, 1, 'salvataggio dopo 1 500 ms'); eq(st.get().save.state, 'saved', 'salvato');
// tema non valido non si salva
api.nextCheck = { problems: [{ path: 'screens.home', msg: 'x' }] }; A.commit(addRect); timers.advance(1500); await flush();
eq(st.get().save.state, 'invalid', 'autosave: tema non valido non si salva'); eq(api.puts.length, 1, 'nessun put');
// 409 → conflict
api.nextPut = 'conflict'; A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'conflict', 'autosave: 409 → conflict');
await A.reload(); eq(st.get().save.state, 'saved', 'ricarica'); check(!Hi.canUndo(st.get().hist), 'ricarica azzera la cronologia del tema');
// errore di rete → error, poi riprova
api.nextPut = 'network'; A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'error', 'autosave: errore di rete → error');
await A.retrySave(); eq(st.get().save.state, 'saved', 'poi riprova');
// durante un gesto non si salva
A.moveSel(1, 0, 'move:x'); timers.advance(5000); await flush(); eq(st.get().save.state, 'pending', 'mai durante un trascinamento');
A.endGesture(); timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'dopo il gesto sì');
eq(A.exportTheme().filename, 'prova.theme.json', 'esporta');
eq(st.get().test.sessions, 1, 'dati di prova iniziali');
```

Nel browser: «Nuovo» con il nome «Notte fonda» crea il tema `notte-fonda` e lo apre; «Annulla»/«Ripeti» dai pulsanti; a 393 il menu «Altro» contiene «Importa» e nessun elemento della barra esce dallo schermo; il contatore degli avvisi cambia scegliendo «6 sessioni».
- [ ] **Step 2–4:** fallire, scrivere, passare.
- [ ] **Step 5: Commit** — `git commit -m "Editor: barra in alto con temi, varianti, dati di prova, animazione, avvisi e salvataggio automatico"`

---

### Task 14: «Invia alla scheda», prova completa nel browser, documentazione

**Files:**
- Create: `assets/js/editor/ui-send.js`
- Modify: `assets/js/api.js`, `assets/js/editor/actions.js`, `tools/webui-dev/test-editor.mjs` (sezione 11), `tools/webui-dev/smoke-editor.mjs`, `tools/webui-dev/shots.mjs`, `docs/DECISIONI.md` («Editor delle schermate»), `docs/TEMI.md` (§8, l'editor)

**Interfaces:**
- Consumes: `watchStatus` (`api.js`), `POST /api/displays/{board}/theme`, stato del salvataggio (Task 13).
- Produces:
  - `api.js`: `sendTheme(board, id) → ThemeJob` (`POST /api/displays/{board}/theme` `{id}`; 409/404 → `Error` col messaggio del servizio);
  - `actions.js`: `A.boards(status) → [{ board, label, ok, why }]` (puro sullo stato: schede con `w === 250 && h === 122`; `ok` falso con `why` «la scheda non ha ancora detto quali temi ha» se `themes.supported` è falso); `A.send(board)` — prima salva subito (niente attesa dei 1 500 ms), rifiuta con lo stato `invalid`/`conflict`, poi `sendTheme`, poi segue `status.displays[].themes.job` con lo stesso `id` dell'operazione; `send = { board, stage, error }`; testi: `attesa` «In attesa della scheda…», `offerta` «Offerto alla scheda…», `scaricato` «Scaricato, lo attivo…», `uso` «Attivazione…», `fatto` «Tema attivo sulla scheda», `errore` «Non riuscito: <error>»;
  - `ui-send.js`: «Invia alla scheda» nella barra; con una scheda manda subito, con più apre la scelta; nessuna scheda adatta → pulsante disattivato con «Nessuna scheda collegata».

- [ ] **Step 1: Prove che falliscono** (sezione 11): `A.boards` scarta una scheda 400×300 e segna quella senza `themes.supported`; `A.send` con modifiche in sospeso fa `putTheme` prima di `sendTheme` (ordine delle chiamate dell'api finta); con lo stato `invalid` non chiama `sendTheme` e dice «Correggi i problemi prima di inviare»; le fasi dello stato finto arrivano fino a «Tema attivo sulla scheda».
- [ ] **Step 2: Prova completa nel browser** in `smoke-editor.mjs`, a 1280 (Chromium) e a 393×852 (WebKit, tocco): nuovo tema; aggiungi un testo; nelle proprietà `{oggi.costo}` con Chakra Petch 44 · 700 → l'avviso «{oggi.costo} può mostrare «$», che Chakra Petch 44 non ha: si vedrà «?»» compare in `#ed-warnings`; sposta (mouse o tocco), annulla, ripeti; duplica, raggruppa, separa; «6 sessioni» e «Anima» (il CRC della tela cambia fra due fotogrammi per un tema con la mascotte); «Invia alla scheda» → «Tema attivo sulla scheda»; con `?fail=1` → «Non riuscito: download non riuscito (HTTP 410)». A 393: `scrollWidth <= innerWidth` dopo ogni passo; ogni `button`, `select`, `input` e `[role=button]` visibile misura almeno 44×44 (`getBoundingClientRect`); tutte le richieste dentro `phone-routes.json`; nessun errore in console. In `shots.mjs` aggiungere `editor-<w>` con un tema aperto e un elemento selezionato.
- [ ] **Step 3–5:** fallire, scrivere, passare: `cd tools/webui-dev && npm test 2>&1 | tail -n 10 && node smoke-editor.mjs 2>&1 | tail -n 20 && node shots.mjs 2>&1 | tail -n 10`; `cd service && go test ./... 2>&1 | tail -n 15`.
- [ ] **Step 6: Documentazione** — DECISIONI, sezione «Editor delle schermate», un punto per ognuna delle «Scelte di questo piano» (sotto) e i testi nuovi della webapp; TEMI §8 una riga sull'editor (salvataggio automatico, controllo, avvisi).
- [ ] **Step 7: Commit** — `git commit -m "Editor: Invia alla scheda con esito, prova completa nel browser, DECISIONI"`

---

## Scelte di questo piano (da riportare in DECISIONI nel Task 14)

- **Glifi**: avviso (mai errore) nel compilatore e nell'editor per testo fisso, valori (alfabeto chiuso o testo libero), filtri e `upper`; fonte comune `protocol/themes/catalog.json`, controllata da Go (contro i `.h`), JS (contro `fonts.js`) e native (`hasGlyph`). I caratteri non si rigenerano in questa fase.
- **Zoom ×1** in più rispetto a ×2–×6 della spec, perché a 393 pt lo schermo intero stia nella tela; interi soltanto (la griglia dei pixel resta esatta).
- **Anteprima della variante aperta**: la tela disegna sempre la variante che si modifica (con il suo `ref`), e una scritta dice quando coi dati di prova la scheda ne mostrerebbe un'altra o la schermata di serie. La parità con la scheda è provata sui casi del golden.
- **Salvataggio automatico**: `POST /api/themes/check` (nuova rotta, nell'elenco chiuso) per gli avvisi del servizio senza salvare; `PUT ?autosave=1&base=` con 409 se il tema è cambiato altrove e al più una versione della cronologia ogni 2 minuti di salvataggi automatici dello stesso processo. Un tema non valido non si salva (resta in memoria).
- **Rinomina** cambia solo il nome: l'id resta (la scheda riconosce i temi per id). **Duplica** passa da `import` (l'id lo sceglie il servizio). **Esporta** scarica il JSON corrente come Blob (nell'app diventa la condivisione).
- **Permessi** nel selettore delle schermate, disattivata; le altre schermate bloccate o non ancora personalizzabili non compaiono.
- **«Parti dalla schermata di serie»**: il meccanismo e il registro `theme/stock.js` arrivano ora, vuoti; le schermate di serie come dati (identiche al pixel) sono la fase 3: fino ad allora il pulsante è disattivato.
- **Tocco**: un dito su un elemento sposta (soglia 6 px), sul vuoto sposta la vista; due dita pizzicano; «Seleziona più» al posto di Maiusc; pulsanti ±1/±8 al posto delle frecce; maniglie con 44 pt di area.
- **Appunti** solo interni alla pagina (gli Appunti di sistema nello schema `calamaio-app://` non sono garantiti); cronologia di annulla per tema, per tutta la sessione della pagina.
- **Cronologia delle versioni** (`/history`, `/restore`) senza interfaccia in questa fase: la spec §6 non la mette nella barra.

## Prove dal vivo (con l'utente, Mac e iPhone 17 Pro)

Prerequisiti: prima ferma il servizio installato, perché `go run … serve` usa le stesse porte 8765/8766/8767 (`launchctl bootout gui/$(id -u)/com.calamaio.service`; alla fine lo si rimette con `launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.calamaio.service.plist` o `calamaio install`); poi servizio di questo ramo avviato sul Mac (`cd service && go run ./cmd/calamaio serve`), scheda con firmware 0.14.0 abbinata e collegata, iPhone abbinato con l'app «Calamaio». Nessun flash.

1. **Mac, Chrome**: Webapp › «Editor schermate». Nuovo tema «Prova editor»; nella Home aggiungi Mascotte, Testo, Orologio, Barra; sposta col mouse, ridimensiona con le maniglie, frecce e Maiusc+frecce; selezione a riquadro e con Maiusc; allinea e distribuisci; copia/incolla, duplica, raggruppa/separa; annulla e ripeti più volte. Atteso: tutto fluido, lo stato passa a «Salvato».
2. **Avvisi**: testo `{oggi.costo}` in Chakra Petch 44 · 700 → avviso del «$»; un testo lungo in una riga → «Il testo non ci sta…»; un elemento trascinato fuori → «Fuori dallo schermo»; un rettangolo pieno sopra un testo → «Coperto da…». Il servizio dà lo stesso avviso dei glifi: `curl -s http://127.0.0.1:8765/api/themes | grep -o 'non ha[^"]*'`.
3. **Dati di prova e animazione**: 0/1/6 sessioni, «Batteria scarica», «Notte», «Permesso in attesa», «PC scollegato»; «Anima» con una lista di 6 sessioni che sfoglia e la mascotte che si muove.
4. **Invia alla scheda**: «Invia alla scheda» → «Tema attivo sulla scheda»; sulla scheda la Home è quella dell'editor. Confronto a occhio pixel per pixel con l'anteprima a ×4 (stessi dati: «1 sessione» e una sessione vera aperta). Il «$» della prova 2, se lasciato, esce «?» sulla scheda come l'avviso diceva.
5. **Permessi**: nel selettore «Permessi — bloccata (regole di sicurezza)» non si sceglie; con il tema attivo un permesso vero arriva sulla schermata di serie e si risponde dalla scheda come sempre.
6. **iPhone, app «Calamaio»**: scheda «Scheda» › «Editor schermate». Niente scorrimento orizzontale; «Elementi» e «Proprietà» aprono i cassetti; pizzica per lo zoom, due dita per spostare la vista; tocca e trascina un elemento; «Seleziona più»; ±1/±8; maniglie comode col dito; «Altro › Importa» apre File; «Esporta» apre la condivisione; «Invia alla scheda» funziona dal telefono.
7. **Due editor insieme**: lo stesso tema aperto sul Mac e sull'iPhone; modifica su uno, poi sull'altro → «Cambiato altrove» con «Ricarica»/«Sovrascrivi»; nessuna modifica persa in silenzio. Poi «Sovrascrivi» entro 2 minuti dalla modifica dell'altro: `curl -s http://127.0.0.1:8765/api/themes/<id>/history` ha una versione in più, e la versione dell'altro dispositivo c'è ancora e si ripristina (`POST /api/themes/<id>/restore/<n>` o confronto del file in cronologia).
8. **Servizio fermo**: con l'editor aperto ferma il servizio, modifica → «Non salvato: …»; riavvia il servizio, «Riprova» → «Salvato».

Esiti, misure (tempo di «Invia alla scheda», fluidità sul telefono) e difetti in DECISIONI.
