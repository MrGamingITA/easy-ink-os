# tools/fonts · font bitmap del firmware

Genera i font bitmap che `firmware/src/gfx/text.cpp` usa per replicare `T()`, `fit()`, `wrapLines()` e `wrapT()` del simulatore (`reference/calamaio-os-browser/assets/calamaio-test.js`).

## Comando

Dalla radice del repo:

```sh
uv run tools/fonts/gen.py           # apre Chrome, rasterizza, scrive i .h
uv run tools/fonts/gen.py --cache   # riusa tools/fonts/.cache/extract.json (niente browser)
```

Serve Google Chrome installato: Playwright lo apre con `channel="chrome"`. Le dipendenze Python (playwright, fonttools, brotli) le installa `uv` dai metadati in testa a `gen.py`.

Poi: `cd firmware && pio test -e native -f test_text -v` stampa la tabella di fedeltà.

File scritti:

- `firmware/src/gfx/fonts/f_*.h`: un file per font, più `f_kern.h` (kerning) e `f_registry.h` (registro);
- `firmware/test/test_text/fidelity.h`: le stringhe di riferimento disegnate da Chrome;
- `tools/fonts/.cache/`: dati grezzi e `summary.json` (byte, fallback, confronti). Non va nel repo.

`font_types.h` e `text_internal.h` sono scritti a mano.

## Export per la webapp

`node tools/fonts/export-js.mjs` legge i `.h` appena generati e scrive
`service/internal/webui/static/assets/js/theme/fonts.js` (renderer dei temi, docs/TEMI.md §6). Va rifatto dopo ogni
`gen.py`: `go test ./internal/webui/` fallisce se `fonts.js` è vecchio (`--check`).

Parità con `text.cpp`: `node tools/fonts/parity/gen-fixture.mjs` (serve `clang++`) compila `parity/harness.cpp` con il
vero `text.cpp`, confronta `text.js` su 765 casi più 5000 casuali e riscrive `parity/text-cases.json`. È il fixture che
`tools/webui-dev/test-theme.mjs` riconfronta senza clang. Con `--check` confronta e basta. Va rifatto dopo ogni `gen.py`
o modifica di `text.cpp`.

## Come rasterizza

1. Una pagina carica i woff2 di `assets/fonts/` con le stesse regole `@font-face` di `fonts.css`, cioè le stesse `unicode-range` (i file sono passati come data URL).
2. Per ogni combinazione si usa lo stesso font string del simulatore (`peso + ' ' + size + 'px ' + famiglia`, con la catena di fallback di `F_UI`/`F_PIX`), `textBaseline 'top'`, su un canvas CPU (`willReadFrequently`, come `Screen.src`).
3. Ogni glifo viene disegnato a 4 fasi orizzontali (x + 0, ¼, ½, ¾). Chrome posiziona i glifi al quarto di pixel, e la soglia rende diverse le 4 bitmap.
4. Soglia come `Screen.compose`: luminanza < 0,5 = inchiostro. Le bitmap vengono ritagliate alla bbox (offset rispetto all'origine e alla y 'top').
5. Si misurano anche:
   - l'avanzamento (`measureText`);
   - il kerning a coppie, misurato a 1000 px: `w(ab) − w(a) − w(b)`, in unità del font (UPM 1000).
6. Le varianti «chiaro su scuro» si rasterizzano a parte, bianco su nero: Chrome copre il testo chiaro in modo diverso, e senza varianti dedicate quelle stringhe avevano 32–73 pixel diversi l'una.

`text.cpp` ricompone le stringhe come fa Chrome:

- legature `fi`/`fl` di Chakra Petch (sono `liga`, attive di default nel canvas);
- kerning GPOS;
- avanzamenti in 16.16 troncati, come li passa Chrome a HarfBuzz;
- kerning scalato con `em_mult`;
- fase = `floor(4·x + ½)`, come Skia.

## Combinazioni (classe S, Heltec 250×122)

Le dimensioni vengono da `MET(dev)` per S (f1 20, f2 12, f3 10) e dai letterali nelle funzioni del ramo S.

| Famiglia | Peso | Dimensioni | Dove (esempi) |
|---|---|---|---|
| Chakra Petch | 700 | 10, 12, 13, 14, 15, 16, 18, 20, 22, 26, 40, 44, 46 | toast 10, detailS/permS/listScreen 12, homeRighe 13, CONSENTI 14, homeUna 15 e 22, detailS 16, boot e offline 18, homeGoccia 20, ambiente 26, focus 40, sleep 44 (`round(122·0,36)`), homeNumeri 46 |
| Chakra Petch | 600 | 10, 11, 12, 13 | drawVal/meta 10, permS e sleep (`max(10, round(44·0,24))`) 11, listScreen 12, homeGoccia 13 |
| Chakra Petch | 500 | 10, 11, 12 | stateText/wrapT 10, homeUna/guest/ambiente 11, agenda 12 |
| Silkscreen | 400 | 8 | orologi, istruzioni in basso |
| Silkscreen | 700 | 8 | stripS, etichette; anche il peso 600 di `guest` («QR DI ESEMPIO»), perché il matching CSS sceglie la faccia 700 |

`hints()` e `statusBar()` non disegnano nella classe S (`hint: 0`, `sb: 0`). `banner()` è escluso da `!S`.

## Caratteri

- Tutto l'ASCII stampabile, `àèéìíòóùúÀÈÉÌÒÙ`, `° · › ‹ ″ ′ − – — … ✓ ✗ ▲ ▼ ◀ ▶ ● ← → ↑ ↓ € × ’`, più le legature `fi` (U+E000) e `fl` (U+E001).
- Le dimensioni da 26 in su (che in S mostrano solo numeri, orari, «min» e «ppm») hanno un sottoinsieme: cifre, spazio, `:.,%+-/?…` e minuscole.
- Un glifo assente viene disegnato come `?`.
- Glifi che non sono nel font e che Chrome prende dal fallback di sistema (le bitmap salvate sono quelle del fallback, cioè ciò che il simulatore mostra su questa macchina):
  - Chakra Petch: `✓ ✗ ▲ ▼ ◀ ▶ ● ← →`;
  - Silkscreen: `″ ′ − ✓ ✗ ▲ ▼ ◀ ▶ ● ← → ↑ ↓`.

## Formato

Vedi `firmware/src/gfx/fonts/font_types.h`:

- `GlyphEntry` (cp, avanzamento in millesimi di em, indice della prima bitmap), ordinati per codepoint per la ricerca binaria;
- `Glyph` (offset, w, h, x, y): 4 fasi, più 4 varianti chiare dove servono;
- bitmap a 1 bit impacchettate, senza padding di riga, con le bitmap identiche condivise;
- il kerning è in `f_kern.h`, con chiavi `(a<<16)|b` condivise dai tre pesi.

Deviazione dal brief: l'avanzamento non è in 1/16 px ma in millesimi di em, scalato in 16.16. Con 1/16 px, sulle 31 stringhe a riga singola del test di fedeltà, i pixel diversi erano il 5,14%; con la posizione esatta sono lo 0,02% (emulazione Python, `--cache`).
