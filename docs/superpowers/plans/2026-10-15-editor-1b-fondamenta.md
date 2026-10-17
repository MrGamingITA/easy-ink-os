# Editor delle schermate, piano 1b: archivio, protocollo «Temi», scheda e menu — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un tema scritto a mano arriva sulla scheda vera e si vede: il servizio lo salva (con cronologia) e lo espone nell'API; lo offre a una scheda autenticata col protocollo nuovo; la scheda lo scarica via HTTP con un token monouso, lo controlla e lo salva in LittleFS; si sceglie da Menu › Display › Tema o dalla webapp; PRG tenuto durante l'avvio dà il tema di serie. Firmware **0.13.0-ed**.

**Architecture:** Servizio: `theme.Store` (file `<dati>/calamaio/themes/<id>.json` + cronologia), rotte `/api/themes…` e `POST /api/displays/{board}/theme`, messaggi nuovi nell'hub (`themes`, `theme-offer`, `theme-ack`, `theme-use`) e `GET /themes/{token}` sulla porta 8766 con le stesse regole dell'OTA (monouso, 5 minuti, IP della scheda). Scheda: installatore puro (`core/theme_store`) sopra l'interfaccia `Storage` del piano 1a, codifica dei messaggi (`core/theme_proto`), LittleFS in `hw/theme_fs` (`/th/<id>.cthm`, `.tmp` + rinomina, tema attivo in NVS `calamaio-th/cur`), download nel task di rete (`net.cpp`), menu e loop (`core/menu.cpp`, `core/ui.cpp`, `main.cpp`).

**Tech Stack:** Go 1.24, `github.com/coder/websocket`; C++ (scheda `-std=gnu++11`, test C++17, Unity, ArduinoJson 7.4.3); Arduino-ESP32 2.0.17 (`LittleFS`, `Preferences`, `HTTPClient`); Node 22 e Playwright 1.64.0 (solo per ricatturare il simulatore).

**Spec:** `docs/superpowers/specs/2026-10-15-editor-schermate-design.md` (§3, §4, §8 punto 1, §9). **Prerequisito:** il piano 1a (`docs/superpowers/plans/2026-10-15-editor-1a-fondamenta.md`) è finito e verde: `git log --oneline | grep "Temi: renderer JS e parità"` trova il commit, e `cd firmware && pio test -e native`, `cd service && go test ./...`, `cd tools/webui-dev && npm test` passano.

## Global Constraints

- Tutti i vincoli del piano 1a valgono anche qui (limiti 4 096 / 65 536 / 8 temi, `-std=gnu++11`, testi in italiano, commit senza `Co-Authored-By`, Permessi e schermate di sistema sempre di serie).
- Versione del firmware: **`0.13.0-ed`** (`CALAMAIO_FW_VERSION` in `firmware/src/core/model.h`, assert in `firmware/test/test_net/test_main.cpp`).
- Messaggi nuovi, facoltativi, versione del protocollo invariata (`v: 1`): un firmware che non li conosce li ignora; il servizio offre temi solo a una scheda **autenticata** che ha mandato `themes`. Nessun `mac` (come `set` e `workday`: la sicurezza è la connessione autenticata).
- Download: `GET http://<pc>:8766/themes/<token>`, token di **16 byte (32 cifre esadecimali minuscole)**, monouso, **5 minuti**, solo per l'**IP della scheda** che ha ricevuto l'offerta: sconosciuto 404, usato o scaduto 410, IP diverso 403.
- Scheda: LittleFS **`/th/<id>.cthm`**, scrittura in **`/th/tmp`**, controllo completo (CRC + ogni blocco con `checkBlock`), poi rinomina; ordine d'installazione in **`/th/order`**; tema attivo in NVS **`calamaio-th`**, chiave **`cur`** (stringa, vuota = di serie). Mai più di 8 temi: il nono prima si salva (rinomina), poi toglie il più vecchio **non attivo** (se il salvataggio non riesce non si toglie niente). La rinomina è quella diretta di LittleFS (sostituisce un file esistente), mai `remove` e poi `rename`. Le modifiche ai file dei temi e a `/th/order` dal task di rete e dal loop passano per un lucchetto (`themefs::lock`/`unlock`).
- In modalità gioco (`net::suspended()`) niente temi: il servizio ripropone quando la scheda torna (`themes` dopo `auth-ok`). Mentre il buffer del tema è prestato alla partita (`Runtime::lent()`) `themeSync`/`themeEvent` non fanno niente (un `activate()` falso lì non è un tema rotto); all'uscita dalla partita il tema attivo si riattiva dall'archivio e il catalogo si rifà (gli eventi `ThemeStored` scartati da `pumpNet` in partita non lasciano l'indice vecchio).
- Mai cancellare un tema buono: prima di togliere un tema «rotto» la scheda lo ricontrolla con `verifyStored`.
- Il servizio ignora un `themes` da una connessione non autenticata (come l'offerta e `theme-use`, che partono solo verso una scheda autenticata).
- Testi nuovi a schermo, esattamente: voce «Tema» (Menu › Display, ultima voce) con valore = nome del tema attivo o «Calamaio»; titolo della lista «Tema»; voce «Calamaio»; valore «✓» sul tema attivo; toast «Tema rovinato: uso quello di serie» e «Tema di serie»; azione «Tema: <nome>». Aiuti: «Tema delle schermate. Calamaio è quello di serie; gli altri arrivano dalla webapp.» (Display › Tema), «Il tema di serie: sempre presente.» (Calamaio), «Tema dalla webapp. Permessi, abbinamento e schermate di sistema restano quelle di serie.» (gli altri).
- Allineamento del simulatore (`reference/calamaio-os-browser/`, annotato in DECISIONI): voce «Tema» in Display e lista «tema» con la sola voce «Calamaio».
- Nessun flash durante il piano. Le prove dal vivo (Task B8) le fa il coordinatore **dopo un sì esplicito dell'utente**, dopo il backup di NVS e otadata, a 115200 baud, scrivendo solo otadata e app0, oppure con l'aggiornamento remoto del servizio.

## Review Focus

- **La scheda si scollega (o parte una partita) a metà download**: il temporaneo si scarta, nessun tema a metà, il servizio torna in «attesa» e ripropone con un token nuovo al ritorno — `TestThemeResumeAfterReconnect` (Task B3) e `test_install_abort_keeps_old` (Task B4).
- **Token riusato, scaduto o da un altro IP**: 410/410/403, il download non parte, il token resta valido per la scheda giusta (403) — `TestThemeTokenRules` (Task B3).
- **Nono tema sulla scheda**: si salva prima, poi si toglie il più vecchio non attivo, mai quello attivo, e l'ordine resta giusto dopo un riavvio; se il salvataggio fallisce restano tutti e otto — `test_ninth_theme_evicts_oldest_not_active` e `test_ninth_commit_fails_keeps_all` (Task B4).
- **Scheda collegata ma non autenticata**: nessuna offerta, nessun `theme-use`, il suo `themes` si ignora — `TestThemeNotAuthedWaits` (Task B3) e `test_offer_only_when_authed` (Task B4).
- **Tema attivo rovinato in flash all'accensione**: di serie, toast «Tema rovinato: uso quello di serie», file tolto, NVS a vuoto, `themes` aggiornato — `test_boot_plan_broken_active` (Task B7, logica pura `themeBootPlan`).
- **`theme-use` per un tema che la scheda non ha** (tolto perché rovinato, o id sbagliato): la scheda resta col tema di prima e risponde `themes` invariato; il servizio, visto che il tema manca, lo riofferta (senza `themes` entro 15 s: errore «la scheda non ha attivato il tema») — `test_use_unknown_theme` (Task B4) e `TestThemeUseUnknownReoffers` (Task B3).

---

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `service/internal/theme/store.go` | archivio dei temi sul PC, cronologia (20), import/export | B1 |
| `service/internal/api/themes.go`, `service/internal/api/server.go` | rotte `/api/themes…` e `/api/displays/{board}/theme` | B2, B3 |
| `service/internal/server/server.go`, `service/cmd/calamaio/main.go` | cartella dei temi (`Options.ThemeDir`), collegamento di store, hub e API | B2 |
| `service/internal/display/themes.go`, `hub.go`, `remote.go` | messaggi `themes`/`theme-ack`, offerte, token, `GET /themes/{token}`, stato per la webapp | B3 |
| `service/internal/fakedisplay/themes.go` | la scheda finta che scarica e attiva i temi | B3 |
| `protocol/schema/message.schema.json`, `protocol/examples/theme*.json` | schema ed esempi dei messaggi nuovi | B3 |
| `firmware/src/core/theme_store.{h,cpp}` | installazione controllata, ordine, nono tema | B4 |
| `firmware/src/core/theme_proto.{h,cpp}`, `core/apply_state.{h,cpp}` | messaggi dei temi lato scheda | B4 |
| `firmware/src/hw/theme_fs.{h,cpp}` | LittleFS `/th`, NVS `calamaio-th/cur` | B5 |
| `firmware/src/net/net.{h,cpp}` | download nel task di rete, `theme-ack`, `themes`, eventi al loop | B5 |
| `firmware/src/core/{model,menu,ui}.{h,cpp}` | catalogo nel modello, Menu › Display › Tema | B6 |
| `reference/calamaio-os-browser/assets/calamaio-test.js`, `LEGGIMI.txt`, `tools/golden/core-golden.js`, `firmware/test/test_core_golden/golden_core.h`, `tools/sim-capture/{scenarios.mjs,out/heltec213}` | allineamento del simulatore e golden | B6 |
| `firmware/src/main.cpp`, `firmware/src/core/theme_runtime.{h,cpp}` (`themeBootPlan`) | avvio, eventi, uscita di emergenza, Riposo in sonno profondo, versione | B7 |
| `docs/PROTOCOLLO.md`, `docs/DECISIONI.md`, `docs/TEMI.md` | documentazione | B8 |

---
### Task B1: Archivio dei temi sul PC

**Files:**
- Create: `service/internal/theme/store.go`
- Test: `service/internal/theme/store_test.go`

**Interfaces:**
- Consumes: `CompileJSON`, `Compiled`, `InvalidError`, `Problem`, `ReservedID`, `ScreenS` (piano 1a).
- Produces:
  - `type Store struct { Dir string; Now func() time.Time; Keep int }` (`Keep` = versioni nella cronologia, 0 = 20), `func NewStore(dir string) *Store`;
  - `type Info struct { ID, Name, Screen string; CRC uint32; Bytes int; Screens map[string]int; Updated int64; Warnings []string; Problems []Problem }` (JSON in camelCase: `id, name, screen, crc, bytes, screens, updated, warnings, problems`; `problems` non vuoto = file non valido sul disco);
  - `type Version struct { N int; At int64; Bytes int }` (JSON `n, at, bytes`);
  - errori `ErrNotFound`, `ErrExists`, `ErrReserved` (sentinelle), `*InvalidError` dal piano 1a;
  - metodi: `List() ([]Info, error)` (per id), `Get(id) ([]byte, *Info, error)`, `Compiled(id) (*Compiled, error)`, `Create(data) (*Info, error)`, `Put(id string, data []byte) (*Info, error)` (l'id nel corpo deve essere `id`), `Delete(id) error`, `History(id) ([]Version, error)` (la più recente per prima), `Restore(id string, n int) (*Info, error)`, `Import(data) (*Info, error)` (id già usato → `<id>-2` … `<id>-99`).

- [ ] **Step 1: Scrivere il test che fallisce**

`service/internal/theme/store_test.go`:

```go
package theme

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func newStore(t *testing.T) *Store {
	s := NewStore(t.TempDir())
	clock := time.UnixMilli(1791707400000)
	s.Now = func() time.Time { clock = clock.Add(time.Second); return clock }
	return s
}

func renamed(t *testing.T, data []byte, name string) []byte {
	var m map[string]any
	if err := json.Unmarshal(data, &m); err != nil {
		t.Fatal(err)
	}
	m["name"] = name
	b, _ := json.Marshal(m)
	return b
}

func TestStoreCreateGetList(t *testing.T) {
	s := newStore(t)
	in, err := s.Create(read(t, "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	if in.ID != "prova" || in.Bytes == 0 || in.Screens["home"] == 0 || in.CRC == 0 {
		t.Fatalf("%+v", in)
	}
	if _, err := s.Create(read(t, "prova.theme.json")); !errors.Is(err, ErrExists) {
		t.Fatalf("doppione: %v", err)
	}
	data, info, err := s.Get("prova")
	if err != nil || info.Name != "Mascotte grande" || !strings.Contains(string(data), `"prova"`) {
		t.Fatalf("get: %v %+v", err, info)
	}
	if _, _, err := s.Get("manca"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("manca: %v", err)
	}
	// un file rovinato nella cartella compare con i problemi, non blocca gli altri
	os.WriteFile(filepath.Join(s.Dir, "rotto.json"), []byte(`{"format":"x"}`), 0o644)
	list, err := s.List()
	if err != nil || len(list) != 2 || list[0].ID != "prova" || list[1].ID != "rotto" || len(list[1].Problems) == 0 {
		t.Fatalf("list: %v %+v", err, list)
	}
}

func TestStoreRejects(t *testing.T) {
	s := newStore(t)
	_, err := s.Create([]byte(`{"format":"calamaio-theme"}`))
	var ie *InvalidError
	if !errors.As(err, &ie) {
		t.Fatalf("non valido: %v", err)
	}
	if _, err := s.Put("altro", read(t, "prova.theme.json")); err == nil || !strings.Contains(err.Error(), "id") {
		t.Fatalf("id diverso dal percorso: %v", err)
	}
	if err := s.Delete(ReservedID); !errors.Is(err, ErrReserved) {
		t.Fatalf("tema di serie: %v", err)
	}
}

func TestStoreHistoryAndRestore(t *testing.T) {
	s := newStore(t)
	s.Keep = 3
	base := read(t, "prova.theme.json")
	if _, err := s.Create(base); err != nil {
		t.Fatal(err)
	}
	for i := 1; i <= 4; i++ {
		if _, err := s.Put("prova", renamed(t, base, "Versione "+string(rune('0'+i)))); err != nil {
			t.Fatal(err)
		}
	}
	h, err := s.History("prova")
	if err != nil || len(h) != 3 || h[0].N != 5 || h[2].N != 3 {
		t.Fatalf("cronologia: %v %+v", err, h)
	}
	in, err := s.Restore("prova", 3)
	if err != nil || in.Name != "Versione 2" {
		t.Fatalf("ripristino: %v %+v", err, in)
	}
	if h, _ := s.History("prova"); h[0].N != 6 { // il ripristino è una versione nuova
		t.Fatalf("dopo il ripristino: %+v", h)
	}
	if _, err := s.Restore("prova", 1); !errors.Is(err, ErrNotFound) {
		t.Fatalf("versione uscita dalla cronologia: %v", err)
	}
	if err := s.Delete("prova"); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(s.Dir, "prova")); !os.IsNotExist(err) {
		t.Fatal("cronologia rimasta")
	}
}

func TestStoreImportRenames(t *testing.T) {
	s := newStore(t)
	s.Create(read(t, "prova.theme.json"))
	in, err := s.Import(read(t, "prova.theme.json"))
	if err != nil || in.ID != "prova-2" {
		t.Fatalf("import: %v %+v", err, in)
	}
	data, _, _ := s.Get("prova-2")
	if !strings.Contains(string(data), `"id": "prova-2"`) {
		t.Fatalf("id nel file: %s", data[:80])
	}
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/theme/ -run TestStore -v`
Expected: FAIL (`undefined: NewStore`).

- [ ] **Step 3: Scrivere `store.go`**

```go
package theme

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

var (
	ErrNotFound = errors.New("tema sconosciuto")
	ErrExists   = errors.New("c'è già un tema con questo id")
	ErrReserved = errors.New("«calamaio» è il tema di serie: non si cambia")
)

// Store: temi dell'utente in Dir (<dati utente>/calamaio/themes), un file <id>.json, e la cronologia <id>/<n>.json
// (ogni salvataggio è una versione; si tengono le ultime Keep).
type Store struct {
	Dir  string
	Now  func() time.Time
	Keep int
	mu   sync.Mutex
}

type Info struct {
	ID       string         `json:"id"`
	Name     string         `json:"name"`
	Screen   string         `json:"screen"`
	CRC      uint32         `json:"crc"`
	Bytes    int            `json:"bytes"`
	Screens  map[string]int `json:"screens"`
	Updated  int64          `json:"updated"`
	Warnings []string       `json:"warnings"`
	Problems []Problem      `json:"problems,omitempty"`
}

type Version struct {
	N     int   `json:"n"`
	At    int64 `json:"at"`
	Bytes int   `json:"bytes"`
}

func NewStore(dir string) *Store { return &Store{Dir: dir, Now: time.Now} }

func (s *Store) keep() int {
	if s.Keep > 0 {
		return s.Keep
	}
	return 20
}

func (s *Store) file(id string) string { return filepath.Join(s.Dir, id+".json") }

func validStoreID(id string) bool { return len(id) >= 1 && len(id) <= 24 && idRe.MatchString(id) }

func info(data []byte, mod time.Time) *Info {
	t, c, err := CompileJSON(data)
	in := &Info{Updated: mod.UnixMilli(), Screens: map[string]int{}, Warnings: []string{}}
	if t != nil {
		in.ID, in.Name, in.Screen = t.ID, t.Name, t.Screen
	}
	if c != nil {
		in.Screens, in.Warnings = c.Screens, append(in.Warnings, c.Warnings...)
		in.CRC, in.Bytes = c.CRC, len(c.Bin)
	}
	var ie *InvalidError
	if errors.As(err, &ie) {
		in.Problems = ie.Problems
	} else if err != nil {
		in.Problems = []Problem{{Msg: err.Error()}}
	}
	return in
}

// pretty: il file si salva indentato (si legge e si confronta con git).
func pretty(data []byte) []byte {
	var b bytes.Buffer
	if json.Indent(&b, data, "", "  ") != nil {
		return data
	}
	b.WriteByte('\n')
	return b.Bytes()
}

func writeAtomic(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func (s *Store) List() ([]Info, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	ents, err := os.ReadDir(s.Dir)
	if errors.Is(err, os.ErrNotExist) {
		return []Info{}, nil
	}
	if err != nil {
		return nil, err
	}
	out := []Info{}
	for _, e := range ents {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		data, err := os.ReadFile(filepath.Join(s.Dir, e.Name()))
		fi, _ := e.Info()
		if err != nil || fi == nil {
			continue
		}
		in := info(data, fi.ModTime())
		if in.ID == "" {
			in.ID = strings.TrimSuffix(e.Name(), ".json")
		}
		out = append(out, *in)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out, nil
}

func (s *Store) Get(id string) ([]byte, *Info, error) {
	if !validStoreID(id) {
		return nil, nil, ErrNotFound
	}
	data, err := os.ReadFile(s.file(id))
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil, ErrNotFound
	}
	if err != nil {
		return nil, nil, err
	}
	fi, _ := os.Stat(s.file(id))
	return data, info(data, fi.ModTime()), nil
}

// Compiled: il CTHM del tema salvato (per mandarlo alla scheda).
func (s *Store) Compiled(id string) (*Compiled, error) {
	data, _, err := s.Get(id)
	if err != nil {
		return nil, err
	}
	_, c, err := CompileJSON(data)
	return c, err
}

// check: il tema deve essere valido; id = quello atteso ("" = qualunque).
func check(data []byte, id string) (*Theme, error) {
	t, _, err := CompileJSON(data)
	if err != nil {
		return t, err
	}
	if id != "" && t.ID != id {
		return t, &InvalidError{[]Problem{{Path: "id", Msg: fmt.Sprintf("l'id nel file (%q) non è quello del percorso (%q)", t.ID, id)}}}
	}
	return t, nil
}

func (s *Store) save(id string, data []byte) (*Info, error) {
	data = pretty(data)
	if err := writeAtomic(s.file(id), data); err != nil {
		return nil, err
	}
	vs, _ := s.versions(id)
	n := 1
	if len(vs) > 0 {
		n = vs[0].N + 1
	}
	if err := writeAtomic(filepath.Join(s.Dir, id, strconv.Itoa(n)+".json"), data); err != nil {
		return nil, err
	}
	for _, v := range append([]Version{{N: n}}, vs...)[min(len(vs)+1, s.keep()):] {
		os.Remove(filepath.Join(s.Dir, id, strconv.Itoa(v.N)+".json"))
	}
	now := s.Now()
	os.Chtimes(s.file(id), now, now)
	return info(data, now), nil
}

func (s *Store) Create(data []byte) (*Info, error) {
	t, err := check(data, "")
	if err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, err := os.Stat(s.file(t.ID)); err == nil {
		return nil, ErrExists
	}
	return s.save(t.ID, data)
}

func (s *Store) Put(id string, data []byte) (*Info, error) {
	if id == ReservedID {
		return nil, ErrReserved
	}
	if _, err := check(data, id); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.save(id, data)
}

func (s *Store) Delete(id string) error {
	if id == ReservedID {
		return ErrReserved
	}
	if !validStoreID(id) {
		return ErrNotFound
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if err := os.Remove(s.file(id)); errors.Is(err, os.ErrNotExist) {
		return ErrNotFound
	} else if err != nil {
		return err
	}
	return os.RemoveAll(filepath.Join(s.Dir, id))
}

func (s *Store) versions(id string) ([]Version, error) {
	ents, err := os.ReadDir(filepath.Join(s.Dir, id))
	if err != nil {
		return nil, err
	}
	var out []Version
	for _, e := range ents {
		n, err := strconv.Atoi(strings.TrimSuffix(e.Name(), ".json"))
		if err != nil || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		fi, _ := e.Info()
		out = append(out, Version{N: n, At: fi.ModTime().UnixMilli(), Bytes: int(fi.Size())})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].N > out[j].N })
	return out, nil
}

func (s *Store) History(id string) ([]Version, error) {
	if _, _, err := s.Get(id); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	vs, err := s.versions(id)
	if err != nil {
		return []Version{}, nil
	}
	return vs, nil
}

func (s *Store) Restore(id string, n int) (*Info, error) {
	if !validStoreID(id) {
		return nil, ErrNotFound
	}
	data, err := os.ReadFile(filepath.Join(s.Dir, id, strconv.Itoa(n)+".json"))
	if errors.Is(err, os.ErrNotExist) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, err
	}
	return s.Put(id, data)
}

// Import: come Create, ma un id già usato diventa <id>-2, <id>-3… (il nome resta).
func (s *Store) Import(data []byte) (*Info, error) {
	t, err := check(data, "")
	if err != nil {
		return nil, err
	}
	var raw map[string]any
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	id := t.ID
	for k := 2; ; k++ {
		if _, err := os.Stat(s.file(id)); errors.Is(err, os.ErrNotExist) {
			break
		}
		if k > 99 {
			return nil, ErrExists
		}
		suffix := "-" + strconv.Itoa(k)
		base := t.ID
		if len(base)+len(suffix) > 24 {
			base = strings.TrimRight(base[:24-len(suffix)], "-")
		}
		id = base + suffix
	}
	raw["id"] = id
	b, _ := json.Marshal(raw)
	return s.save(id, b)
}
```

Nota: `json.Marshal` di una mappa riordina le chiavi; `pretty()` indenta. Il file importato quindi non mantiene l'ordine dei campi: va bene (il contenuto è uguale).

- [ ] **Step 4: Eseguire i test**

Run: `cd service && go test ./internal/theme/ -v`
Expected: PASS (i test del piano 1a più i 4 nuovi).

- [ ] **Step 5: Commit**

```bash
git add service/internal/theme/store.go service/internal/theme/store_test.go
git commit -m "Temi: archivio sul PC con cronologia, import ed export"
```

---
### Task B2: API dei temi nella webapp e cartella del servizio

**Files:**
- Create: `service/internal/api/themes.go`
- Modify: `service/internal/api/server.go` (campo `Themes`, rotte), `service/internal/server/server.go` (`Options.ThemeDir`, `as.Themes`), `service/cmd/calamaio/main.go` (`o.ThemeDir`)
- Modify: `service/internal/webui/theme_parity_test.go` (`skipTestCache`: `t.Logf` invece di `t.Fatalf`, portato dal piano 1a)
- Test: `service/internal/server/themes_test.go`

**Interfaces:**
- Consumes: `theme.Store` e i suoi errori (Task B1); `guard`, `postGuard`, `bodyGuard`, `deleteGuard`, `writeJSON`, `errBody`, `apiNotFound` di `api/web.go`.
- Produces:
  - `api.Server.Themes *theme.Store` (nil = le rotte dei temi rispondono 404 `{"error":"temi non disponibili"}`);
  - `server.Options.ThemeDir string` (vuoto = nessun archivio; `calamaio serve` usa `<dati utente>/calamaio/themes`);
  - rotte (stesse guardie di oggi; corpi ≤ 256 KB):

| Rotta | Risposta |
|---|---|
| `GET /api/themes` | 200 `{themes: [Info], dir}` |
| `POST /api/themes` | corpo = tema JSON → 201 `{theme: Info}` · 400 `{error, errors: [Problem]}` (anche per l'id `calamaio`: `Parse` lo segnala come problema) · 409 id già usato |
| `GET /api/themes/{id}` | il file com'è (`application/json`), `ETag` e `X-Calamaio-Crc` = CRC del CTHM in esadecimale · 404 |
| `PUT /api/themes/{id}` | corpo = tema JSON con lo stesso `id` → 200 `{theme}` · 400 · 409 `calamaio` |
| `DELETE /api/themes/{id}` | 204 · 404 · 409 `calamaio` |
| `GET /api/themes/{id}/history` | 200 `{versions: [Version]}` (la più recente per prima) · 404 |
| `POST /api/themes/{id}/restore/{n}` | corpo vuoto o `{}` → 200 `{theme}` · 404 |
| `POST /api/themes/{id}/export` | corpo vuoto o `{}` → 200 il file come allegato (`Content-Disposition: attachment; filename="<id>.theme.json"`) · 404 |
| `POST /api/themes/import` | corpo = tema JSON → 201 `{theme}` (id già usato → `<id>-2`…) · 400 |

- [ ] **Step 1: Scrivere il test che fallisce**

`service/internal/server/themes_test.go`:

```go
package server

import (
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func startThemes(t *testing.T) (*started, *web, string) {
	t.Helper()
	dir := filepath.Join(t.TempDir(), "themes")
	s := startOpts(t, false, 0, func(o *Options) { o.ThemeDir = dir })
	return s, newWeb(t, s), dir
}

func provaTheme(t *testing.T) []byte {
	t.Helper()
	b, err := os.ReadFile(filepath.Join("..", "..", "..", "protocol", "themes", "fixtures", "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func (w *web) same() []string { return []string{"Origin", "http://" + w.host, "Sec-Fetch-Site", "same-origin"} }

func TestWebThemesCRUD(t *testing.T) {
	_, w, dir := startThemes(t)
	if code, m := w.postRaw("/api/themes", provaTheme(t)); code != 201 || m["theme"].(map[string]any)["id"] != "prova" {
		t.Fatalf("crea: %d %v", code, m)
	}
	if _, err := os.Stat(filepath.Join(dir, "prova.json")); err != nil {
		t.Fatal(err)
	}
	if code, _ := w.postRaw("/api/themes", provaTheme(t)); code != 409 {
		t.Fatalf("doppione: %d", code)
	}
	code, m, _ := w.do(http.MethodGet, "/api/themes", "", "")
	if code != 200 || len(m["themes"].([]any)) != 1 {
		t.Fatalf("elenco: %d %v", code, m)
	}
	code, _, h := w.do(http.MethodGet, "/api/themes/prova", "", "")
	if code != 200 || h.Get("X-Calamaio-Crc") == "" || h.Get("ETag") == "" {
		t.Fatalf("file: %d %v", code, h)
	}
	body := strings.Replace(string(provaTheme(t)), "Mascotte grande", "Mascotte enorme", 1)
	code, m, _ = w.do(http.MethodPut, "/api/themes/prova", "application/json", body, w.same()...)
	if code != 200 || m["theme"].(map[string]any)["name"] != "Mascotte enorme" {
		t.Fatalf("modifica: %d %v", code, m)
	}
	code, m, _ = w.do(http.MethodGet, "/api/themes/prova/history", "", "")
	if code != 200 || len(m["versions"].([]any)) != 2 {
		t.Fatalf("cronologia: %d %v", code, m)
	}
	code, m, _ = w.do(http.MethodPost, "/api/themes/prova/restore/1", "", "", w.same()...)
	if code != 200 || m["theme"].(map[string]any)["name"] != "Mascotte grande" {
		t.Fatalf("ripristino: %d %v", code, m)
	}
	code, _, h = w.do(http.MethodPost, "/api/themes/prova/export", "", "", w.same()...)
	if code != 200 || !strings.Contains(h.Get("Content-Disposition"), `prova.theme.json`) {
		t.Fatalf("export: %d %v", code, h)
	}
	if code, m := w.postRaw("/api/themes/import", provaTheme(t)); code != 201 || m["theme"].(map[string]any)["id"] != "prova-2" {
		t.Fatalf("import: %d %v", code, m)
	}
	code, _, _ = w.do(http.MethodDelete, "/api/themes/prova-2", "", "", w.same()...)
	if code != 204 {
		t.Fatalf("cancella: %d", code)
	}
	code, _, _ = w.do(http.MethodDelete, "/api/themes/calamaio", "", "", w.same()...)
	if code != 409 {
		t.Fatalf("tema di serie: %d", code)
	}
}

func TestWebThemesInvalidAndGuards(t *testing.T) {
	_, w, _ := startThemes(t)
	bad := strings.Replace(string(provaTheme(t)), `"scale": 4 }`, `"scale": 9 }`, 1)
	code, m := w.postRaw("/api/themes", []byte(bad))
	if code != 400 || len(m["errors"].([]any)) == 0 || !strings.Contains(m["error"].(string), "scale") {
		t.Fatalf("non valido: %d %v", code, m)
	}
	// l'id del tema di serie non passa Parse: 400 (la PUT e la DELETE di /api/themes/calamaio danno 409)
	reserved := strings.Replace(string(provaTheme(t)), `"id": "prova"`, `"id": "calamaio"`, 1)
	if code, _ := w.postRaw("/api/themes", []byte(reserved)); code != 400 {
		t.Fatalf("id riservato: %d", code)
	}
	// altro sito: 403; senza JSON: 415
	code, _, _ = w.do(http.MethodPost, "/api/themes", "application/json", string(provaTheme(t)), "Origin", "http://evil.example", "Sec-Fetch-Site", "cross-site")
	if code != 403 {
		t.Fatalf("altro sito: %d", code)
	}
	code, _, _ = w.do(http.MethodPut, "/api/themes/prova", "text/plain", "x", w.same()...)
	if code != 415 {
		t.Fatalf("Content-Type: %d", code)
	}
}

func TestWebThemesDisabled(t *testing.T) {
	s := startOpts(t, false, 0, nil)
	w := newWeb(t, s)
	code, m, _ := w.do(http.MethodGet, "/api/themes", "", "")
	if code != 404 || m["error"] != "temi non disponibili" {
		t.Fatalf("%d %v", code, m)
	}
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/server/ -run TestWebThemes -v`
Expected: FAIL (`o.ThemeDir undefined`).

- [ ] **Step 3: Scrivere `api/themes.go`**

```go
package api

import (
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"

	"calamaio/internal/theme"
)

const maxThemeBody = 256 << 10

func (s *Server) themesOff(w http.ResponseWriter) bool {
	if s.Themes == nil {
		writeJSON(w, http.StatusNotFound, errBody("temi non disponibili"))
		return true
	}
	return false
}

// themeError: errori dell'archivio → risposte della API (docs/PROTOCOLLO.md «Temi»).
func themeError(w http.ResponseWriter, err error) {
	var ie *theme.InvalidError
	switch {
	case errors.As(err, &ie):
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": ie.Error(), "errors": ie.Problems})
	case errors.Is(err, theme.ErrNotFound):
		writeJSON(w, http.StatusNotFound, errBody(err.Error()))
	case errors.Is(err, theme.ErrExists), errors.Is(err, theme.ErrReserved):
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
	default:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	}
}

func readTheme(w http.ResponseWriter, r *http.Request) ([]byte, bool) {
	b, err := io.ReadAll(io.LimitReader(r.Body, maxThemeBody+1))
	if err != nil || len(b) > maxThemeBody {
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody("tema oltre 256 KB"))
		return nil, false
	}
	return b, true
}

func (s *Server) apiThemes(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	list, err := s.Themes.List()
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"themes": list, "dir": s.Themes.Dir})
}

func (s *Server) apiThemeCreate(import_ bool) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if s.themesOff(w) {
			return
		}
		b, ok := readTheme(w, r)
		if !ok {
			return
		}
		var in *theme.Info
		var err error
		if import_ {
			in, err = s.Themes.Import(b)
		} else {
			in, err = s.Themes.Create(b)
		}
		if err != nil {
			themeError(w, err)
			return
		}
		writeJSON(w, http.StatusCreated, map[string]any{"theme": in})
	}
}

func (s *Server) apiThemeFile(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	data, in, err := s.Themes.Get(r.PathValue("id"))
	if err != nil {
		themeError(w, err)
		return
	}
	crc := fmt.Sprintf("%08x", in.CRC)
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("ETag", `"`+crc+`"`)
	w.Header().Set("X-Calamaio-Crc", crc)
	w.Header().Set("Cache-Control", "no-cache")
	_, _ = w.Write(data)
}

func (s *Server) apiThemePut(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	b, ok := readTheme(w, r)
	if !ok {
		return
	}
	in, err := s.Themes.Put(r.PathValue("id"), b)
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"theme": in})
}

func (s *Server) apiThemeDelete(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	if err := s.Themes.Delete(r.PathValue("id")); err != nil {
		themeError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (s *Server) apiThemeHistory(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	vs, err := s.Themes.History(r.PathValue("id"))
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"versions": vs})
}

func (s *Server) apiThemeRestore(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	n, err := strconv.Atoi(r.PathValue("n"))
	if err != nil {
		writeJSON(w, http.StatusNotFound, errBody("versione sconosciuta"))
		return
	}
	in, err := s.Themes.Restore(r.PathValue("id"), n)
	if err != nil {
		themeError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"theme": in})
}

func (s *Server) apiThemeExport(w http.ResponseWriter, r *http.Request) {
	if s.themesOff(w) {
		return
	}
	id := r.PathValue("id")
	data, _, err := s.Themes.Get(id)
	if err != nil {
		themeError(w, err)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Disposition", `attachment; filename="`+id+`.theme.json"`)
	_, _ = w.Write(data)
}
```

- [ ] **Step 4: Rotte e collegamenti**

In `api/server.go`: nel `Server` (dopo `Game`) il campo `// Temi delle schermate (archivio sul PC; nil = 404).` `Themes *theme.Store` (import `calamaio/internal/theme`). In `Handler()`, prima di `GET /api/`:

```go
	mux.HandleFunc("GET /api/themes", guard(s.apiThemes))
	mux.HandleFunc("POST /api/themes", postGuard(s.apiThemeCreate(false)))
	mux.HandleFunc("POST /api/themes/import", postGuard(s.apiThemeCreate(true)))
	mux.HandleFunc("GET /api/themes/{id}", guard(s.apiThemeFile))
	mux.HandleFunc("PUT /api/themes/{id}", postGuard(s.apiThemePut))
	mux.HandleFunc("DELETE /api/themes/{id}", deleteGuard(s.apiThemeDelete))
	mux.HandleFunc("GET /api/themes/{id}/history", guard(s.apiThemeHistory))
	mux.HandleFunc("POST /api/themes/{id}/restore/{n}", postGuard(s.apiThemeRestore))
	mux.HandleFunc("POST /api/themes/{id}/export", postGuard(s.apiThemeExport))
```

e dopo `DELETE /api/`: `mux.HandleFunc("PUT /api/", postGuard(apiNotFound))`. (`postGuard` controlla Content-Type e origine anche per le PUT: è `bodyGuard("application/json", …)`, che non guarda il metodo.)

In `server/server.go`: nelle `Options` (vicino a `GameFile`) `ThemeDir string // temi delle schermate (vuoto = nessun archivio; calamaio serve: <dati utente>/calamaio/themes)`; dopo `as.Game = gsvc`:

```go
	if o.ThemeDir != "" {
		as.Themes = theme.NewStore(o.ThemeDir)
		lg.Printf("temi: %s", o.ThemeDir)
	}
```

In `cmd/calamaio/main.go`, nel blocco di `config.DataDir()`: `o.ThemeDir = filepath.Join(d, "themes")`.

- [ ] **Step 5: `theme_parity_test.go`: lo stamp che non si scrive non fa fallire il test**

Portato dal piano 1a: in un checkout in sola lettura `skipTestCache` (`service/internal/webui/theme_parity_test.go`)
non riesce a scrivere `.theme-parity-nocache` e oggi fa fallire `go test ./...` con `t.Fatalf`. Si perde solo il salto
della cache, non il controllo: diventa `t.Logf`.

```go
func skipTestCache(t *testing.T) {
	// Checkout in sola lettura: niente stamp, il controllo resta; si perde solo il salto della cache (risultato vecchio possibile).
	if err := os.WriteFile(noCacheStamp, []byte(time.Now().Format(time.RFC3339Nano)+"\n"), 0o644); err != nil {
		t.Logf("file per saltare la cache non scritto (il risultato può venire dalla cache): %v", err)
		return
	}
	if _, err := os.ReadFile(noCacheStamp); err != nil {
		t.Logf("file per saltare la cache non riletto (il risultato può venire dalla cache): %v", err)
	}
}
```

- [ ] **Step 6: Eseguire i test**

Run: `cd service && go test ./internal/server/ -run TestWebThemes -v && go test ./...`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add service/internal/api/themes.go service/internal/api/server.go service/internal/server/server.go service/cmd/calamaio/main.go service/internal/server/themes_test.go service/internal/webui/theme_parity_test.go
git commit -m "Temi: API della webapp (elenco, crea, modifica, cronologia, export, import)"
```

---
### Task B3: Protocollo «Temi» nel servizio (offerta, download, attivazione)

**Files:**
- Create: `service/internal/display/themes.go`, `service/internal/fakedisplay/themes.go`
- Create: `protocol/examples/themes.json`, `themes-vuoto.json`, `theme-offer.json`, `theme-ack.json`, `theme-ack-errore.json`, `theme-use.json`, `theme-use-serie.json`
- Modify: `service/internal/display/hub.go` (campi, `Handler`, `readLoop`, `run`, chiusura della connessione), `service/internal/display/remote.go` (`inbound`, `DisplayStatus.Themes`), `service/internal/fakedisplay/fakedisplay.go` (opzioni, url, smistamento), `protocol/schema/message.schema.json`, `service/internal/api/themes.go` + `server.go` (rotta della scheda), `service/internal/server/server.go` (`as.ThemeSend = hub`)
- Test: `service/internal/display/themes_test.go`, `service/internal/display/schema_test.go` (i 4 tipi nuovi in `seen`, 2 casi in `TestSchemaRejects`), `service/internal/server/themes_test.go` (end-to-end)

**Interfaces:**
- Consumes: `theme.Store.Compiled` (B1), `theme.Compiled` (piano 1a), `Hub.target`, `Hub.enqueueOut`, `Hub.changed`, `Hub.ignore`, `Hub.Status` (`[]DisplayStatus`, `remote.go`), `conn.authed`, `remoteIP`, `m5.Hex`, `m5.Random`, `m5.IsLowerHex`, `BlockedError`, `ErrNotConnected`, `commandError` (`api/web.go`: non conosce `*display.BlockedError`, che darebbe 500).
- Produces (package `display`):
  - `type ThemeHave struct { ID string; CRC uint32 }` (JSON `id`, `crc`);
  - `type ThemePayload struct { ID, Name, Screen string; CRC uint32; Bin []byte }` (nil = tema di serie);
  - `func (h *Hub) SendTheme(board string, p *ThemePayload) (*ThemeJobView, error)`; `func (h *Hub) ThemeOf(board string) *ThemeJobView`; `func (h *Hub) ServeTheme(w, r)`;
  - `type ThemeJobView struct { ID, Board string; Theme *string; Name string; CRC uint32; Stage, Error string; Started, Updated int64 }` (JSON `id, board, theme, name, crc, stage, error?, started, updated`); fasi `ThemeWaiting = "attesa"`, `ThemeOffered = "offerta"`, `ThemeDownloaded = "scaricato"`, `ThemeUsing = "uso"`, `ThemeDone = "fatto"`, `ThemeFailed = "errore"`;
  - `type ThemesView struct { Supported bool; Have []ThemeHave; Cur *string; Job *ThemeJobView }` (JSON `supported, have, cur, job`) in `DisplayStatus.Themes`;
  - campi dell'`Hub`: `ThemeAckTimeout` (60 s), `ThemeUseTimeout` (15 s);
  - regole: `themes` da una connessione non autenticata si ignora (`ws: #N ignorato: themes da una connessione non autenticata`); `theme-use` parte **una volta sola** per operazione (in fase `uso` un `themes` che ha già il tema ma non ancora come attivo si aspetta e basta);
  - API: `api.ThemeSender` interfaccia `{ SendTheme(board string, p *display.ThemePayload) (*display.ThemeJobView, error) }`, campo `Server.ThemeSend`; rotta `POST /api/displays/{board}/theme` corpo `{"id": "x" | null}` → 202 `{theme: ThemeJobView}` · 400 corpo · 404 tema sconosciuto · 409 scheda sconosciuta (`ErrNotConnected`, da `commandError`), tema non valido o schermo diverso (`*display.BlockedError`, gestito in `apiDisplayTheme`).
- Produces (package `fakedisplay`): `Options.Themes ThemeOptions{ Enabled bool; Reject string; NoAck bool }`, `func (d *Display) Themes() (have []ThemeHave, cur string)`, eventi `theme-offer`, `theme-use`, `themes-sent`.

**Messaggi** (testo per `docs/PROTOCOLLO.md`, Task B8):

```
display → servizio   themes       {have: [{id, crc}] (0..8), cur: id | null}     dopo auth-ok e a ogni cambio
servizio → display   theme-offer  {id, crc, size, token}                          solo autenticato
display → servizio   theme-ack    {id, ok, error?}                                dopo il download e i controlli
servizio → display   theme-use    {id | null}                                     solo autenticato
```

- [ ] **Step 1: Esempi e schema**

`protocol/examples/themes.json`: `{"t":"themes","v":1,"have":[{"id":"prova","crc":2864434397}],"cur":"prova"}` · `themes-vuoto.json`: `{"t":"themes","v":1,"have":[],"cur":null}` · `theme-offer.json`: `{"t":"theme-offer","v":1,"id":"prova","crc":2864434397,"size":3120,"token":"0123456789abcdef0123456789abcdef"}` · `theme-ack.json`: `{"t":"theme-ack","v":1,"id":"prova","ok":true}` · `theme-ack-errore.json`: `{"t":"theme-ack","v":1,"id":"prova","ok":false,"error":"CRC sbagliato"}` · `theme-use.json`: `{"t":"theme-use","v":1,"id":"prova"}` · `theme-use-serie.json`: `{"t":"theme-use","v":1,"id":null}`.

In `protocol/schema/message.schema.json`: i quattro tipi nell'`enum` di `t`, quattro `if/then` in `allOf` e in `$defs`:

```json
"themeId": { "type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 24 },
"themes": { "type": "object", "required": ["have", "cur"], "properties": {
  "t": {}, "v": {}, "have": { "type": "array", "maxItems": 8, "items": { "type": "object", "required": ["id", "crc"],
    "additionalProperties": false, "properties": { "id": { "$ref": "#/$defs/themeId" }, "crc": { "type": "integer", "minimum": 0, "maximum": 4294967295 } } } },
  "cur": { "oneOf": [ { "type": "null" }, { "$ref": "#/$defs/themeId" } ] } }, "additionalProperties": false },
"themeOffer": { "type": "object", "required": ["id", "crc", "size", "token"], "properties": { "t": {}, "v": {},
  "id": { "$ref": "#/$defs/themeId" }, "crc": { "type": "integer", "minimum": 0, "maximum": 4294967295 },
  "size": { "type": "integer", "minimum": 72, "maximum": 65536 }, "token": { "type": "string", "pattern": "^[0-9a-f]{32}$" } }, "additionalProperties": false },
"themeAck": { "type": "object", "required": ["id", "ok"], "properties": { "t": {}, "v": {}, "id": { "$ref": "#/$defs/themeId" },
  "ok": { "type": "boolean" }, "error": { "type": "string", "maxLength": 127 } }, "additionalProperties": false },
"themeUse": { "type": "object", "required": ["id"], "properties": { "t": {}, "v": {},
  "id": { "oneOf": [ { "type": "null" }, { "$ref": "#/$defs/themeId" } ] } }, "additionalProperties": false }
```

`TestExamplesMatchSchema` (in `display/schema_test.go`) valida già tutti gli esempi della cartella, ma l'elenco dei tipi
obbligatori (`seen`) non ha quelli nuovi: in fondo all'elenco, dopo `"accessory-wear"`, aggiungere
`"themes", "theme-offer", "theme-ack", "theme-use"`. In `TestSchemaRejects`, in fondo alla mappa `bad` (dopo i casi della
giornata):

```go
		// Temi delle schermate.
		"theme-offer token a 31 cifre": `{"t":"theme-offer","v":1,"id":"prova","crc":1,"size":3120,"token":"` + strings.Repeat("a", 31) + `"}`,
		"themes con 9 temi":            `{"t":"themes","v":1,"have":[` + strings.TrimSuffix(strings.Repeat(`{"id":"a","crc":1},`, 9), ",") + `],"cur":null}`,
```

Run `cd service && go test ./internal/display/ -run 'TestExamplesMatchSchema|TestSchemaRejects'` → PASS.

- [ ] **Step 2: Scrivere i test dell'hub che falliscono**

`service/internal/display/themes_test.go`:

```go
package display

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/theme"
)

const themeBoard = "calamaio-7e11"

func provaPayload(t *testing.T) *ThemePayload {
	t.Helper()
	b, err := os.ReadFile(filepath.Join("..", "..", "..", "protocol", "themes", "fixtures", "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	th, c, err := theme.CompileJSON(b)
	if err != nil {
		t.Fatal(err)
	}
	return &ThemePayload{ID: th.ID, Name: th.Name, Screen: th.Screen, CRC: c.CRC, Bin: c.Bin}
}

func themeRig(t *testing.T, tune func(*Hub)) *rig {
	r := newRig(t, func(h *Hub) {
		h.ThemeAckTimeout = 2 * time.Second
		h.ThemeUseTimeout = time.Second
		if tune != nil {
			tune(h)
		}
	})
	r.pairKey(themeBoard, testKey)
	return r
}

func themeDisplay(t *testing.T, r *rig, opt fakedisplay.ThemeOptions) *fakedisplay.Display {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	opt.Enabled = true
	d, err := fakedisplay.DialWith(ctx, r.url, themeBoard, testKey, false, fakedisplay.Options{Themes: opt})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	awaitEv(t, d, "authed")
	awaitEv(t, d, "themes-sent")
	return d
}

func waitTheme(t *testing.T, r *rig, stage string) *ThemeJobView {
	t.Helper()
	deadline := time.Now().Add(5 * time.Second)
	for {
		v := r.hub.ThemeOf(themeBoard)
		if v != nil && v.Stage == stage {
			return v
		}
		if time.Now().After(deadline) {
			t.Fatalf("fase %q non raggiunta: %+v\n%s", stage, v, r.log.String())
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func TestThemeFullFlow(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	p := provaPayload(t)
	if _, err := r.hub.SendTheme(themeBoard, p); err != nil {
		t.Fatal(err)
	}
	off := awaitEv(t, d, "theme-offer")
	if off.Msg["id"] != "prova" || len(off.Msg["token"].(string)) != 32 {
		t.Fatalf("offerta: %v", off.Msg)
	}
	awaitEv(t, d, "theme-use")
	waitTheme(t, r, ThemeDone)
	// il themes che segue il theme-ack non rimanda theme-use: uno solo per operazione
	if _, err := d.Await("theme-use", 300*time.Millisecond); err == nil {
		t.Fatal("theme-use doppio")
	}
	have, cur := d.Themes()
	if cur != "prova" || len(have) != 1 || have[0].CRC != p.CRC {
		t.Fatalf("scheda: %v %q", have, cur)
	}
	// stesso tema ancora: niente download, solo theme-use (già attivo: subito fatto)
	if _, err := r.hub.SendTheme(themeBoard, p); err != nil {
		t.Fatal(err)
	}
	waitTheme(t, r, ThemeDone)
	// tema di serie
	if _, err := r.hub.SendTheme(themeBoard, nil); err != nil {
		t.Fatal(err)
	}
	awaitEv(t, d, "theme-use")
	waitTheme(t, r, ThemeDone)
	if _, cur := d.Themes(); cur != "" {
		t.Fatalf("di serie: %q", cur)
	}
	st := r.hub.Status()
	if len(st) != 1 || st[0].Themes == nil || !st[0].Themes.Supported || st[0].Themes.Cur != nil {
		t.Fatalf("stato: %+v", st[0].Themes)
	}
}

func TestThemeTokenRules(t *testing.T) {
	r := themeRig(t, func(h *Hub) { h.TokenTTL = time.Second }) // margine largo anche con -race
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{NoAck: true})
	if _, err := r.hub.SendTheme(themeBoard, provaPayload(t)); err != nil {
		t.Fatal(err)
	}
	off := awaitEv(t, d, "theme-offer")
	tok := off.Msg["token"].(string)
	base := strings.Replace(r.url, "ws://", "http://", 1)
	base = base[:strings.LastIndex(base, "/")]
	// IP sbagliato: 403, il token resta valido
	req := httptest.NewRequest(http.MethodGet, "/themes/"+tok, nil)
	req.RemoteAddr = "10.0.0.99:4321"
	req.SetPathValue("token", tok)
	rec := httptest.NewRecorder()
	r.hub.ServeTheme(rec, req)
	if rec.Code != http.StatusForbidden {
		t.Fatalf("IP sbagliato: %d", rec.Code)
	}
	if res, _ := http.Get(base + "/themes/" + strings.Repeat("b", 32)); res.StatusCode != http.StatusNotFound {
		t.Fatalf("sconosciuto: %d", res.StatusCode)
	}
	res, err := http.Get(base + "/themes/" + tok)
	if err != nil || res.StatusCode != 200 || res.ContentLength != int64(len(provaPayload(t).Bin)) {
		t.Fatalf("download: %v %d", err, res.StatusCode)
	}
	res.Body.Close()
	waitTheme(t, r, ThemeDownloaded)
	if res, _ := http.Get(base + "/themes/" + tok); res.StatusCode != http.StatusGone {
		t.Fatalf("riusato: %d", res.StatusCode)
	}
	// scaduto
	r.hub.SendTheme(themeBoard, provaPayload(t))
	off = awaitEv(t, d, "theme-offer")
	time.Sleep(1200 * time.Millisecond)
	if res, _ := http.Get(base + "/themes/" + off.Msg["token"].(string)); res.StatusCode != http.StatusGone {
		t.Fatalf("scaduto: %d", res.StatusCode)
	}
}

func TestThemeRejectedAndTimeout(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{Reject: "CRC sbagliato"})
	r.hub.SendTheme(themeBoard, provaPayload(t))
	awaitEv(t, d, "theme-offer")
	if v := waitTheme(t, r, ThemeFailed); !strings.Contains(v.Error, "CRC sbagliato") {
		t.Fatalf("%+v", v)
	}
	r2 := themeRig(t, nil)
	d2 := themeDisplay(t, r2, fakedisplay.ThemeOptions{NoAck: true})
	r2.hub.SendTheme(themeBoard, provaPayload(t))
	awaitEv(t, d2, "theme-offer")
	if v := waitTheme(t, r2, ThemeFailed); !strings.Contains(v.Error, "non ha risposto") {
		t.Fatalf("%+v", v)
	}
}

// La scheda non ha più il tema che il servizio crede di averle dato (tolto perché rovinato): al theme-use risponde
// themes senza di lui, e il servizio, visto che manca, lo riofferta invece di aspettare a vuoto.
func TestThemeUseUnknownReoffers(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	p := provaPayload(t)
	r.hub.SendTheme(themeBoard, p)
	waitTheme(t, r, ThemeDone)
	awaitEv(t, d, "theme-offer")
	d.ForgetTheme("prova")
	r.hub.SendTheme(themeBoard, nil)
	waitTheme(t, r, ThemeDone)
	r.hub.mu.Lock()
	r.hub.themes[themeBoard].have = []ThemeHave{{"prova", p.CRC}} // il servizio crede che ci sia ancora
	r.hub.mu.Unlock()
	r.hub.SendTheme(themeBoard, p)
	awaitEv(t, d, "theme-use")   // prima prova: la scheda non lo trova e risponde themes invariato
	awaitEv(t, d, "theme-offer") // allora si riofferta
	waitTheme(t, r, ThemeDone)
	if _, cur := d.Themes(); cur != "prova" {
		t.Fatalf("cur %q", cur)
	}
}

func TestThemeResumeAfterReconnect(t *testing.T) {
	r := themeRig(t, nil)
	if _, err := r.hub.SendTheme(themeBoard, provaPayload(t)); err != nil { // scheda abbinata ma non collegata
		t.Fatal(err)
	}
	waitTheme(t, r, ThemeWaiting)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{NoAck: true})
	awaitEv(t, d, "theme-offer")
	d.Close() // scollegata a metà (partita del Game Boy, Wi-Fi spento)
	waitTheme(t, r, ThemeWaiting)
	d2 := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	off := awaitEv(t, d2, "theme-offer")
	if len(off.Msg["token"].(string)) != 32 {
		t.Fatal(off.Msg)
	}
	waitTheme(t, r, ThemeDone)
}

// §7: offerta e theme-use solo a una scheda autenticata. Con la chiave sbagliata la scheda resta collegata ma non
// autenticata (auth-fail): il suo themes si ignora, l'operazione resta in «attesa» e non parte nessuna offerta.
func TestThemeNotAuthedWaits(t *testing.T) {
	r := themeRig(t, nil)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	wrong := append([]byte(nil), testKey...)
	wrong[0] ^= 1
	d, err := fakedisplay.DialWith(ctx, r.url, themeBoard, wrong, false, fakedisplay.Options{Themes: fakedisplay.ThemeOptions{Enabled: true}})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	awaitEv(t, d, "auth-fail")
	// una scheda che manda themes lo stesso (il firmware non lo fa): ignorato
	if err := d.Send(`{"t":"themes","v":1,"have":[{"id":"prova","crc":1}],"cur":"prova"}`); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(3 * time.Second)
	for !strings.Contains(r.log.String(), "themes da una connessione non autenticata") {
		if time.Now().After(deadline) {
			t.Fatalf("themes non ignorato:\n%s", r.log.String())
		}
		time.Sleep(10 * time.Millisecond)
	}
	if _, err := r.hub.SendTheme(themeBoard, provaPayload(t)); err != nil {
		t.Fatal(err)
	}
	waitTheme(t, r, ThemeWaiting)
	if _, err := d.Await("theme-offer", 500*time.Millisecond); err == nil {
		t.Fatal("offerta a una scheda non autenticata")
	}
	if v := r.hub.ThemeOf(themeBoard); v == nil || v.Stage != ThemeWaiting {
		t.Fatalf("fase: %+v", v)
	}
	st := r.hub.Status()
	if len(st) != 1 || st[0].Themes == nil || st[0].Themes.Supported || st[0].Themes.Cur != nil || len(st[0].Themes.Have) != 0 {
		t.Fatalf("stato: %+v", st[0].Themes)
	}
}

func TestThemeScreenMismatch(t *testing.T) {
	r := themeRig(t, nil)
	themeDisplay(t, r, fakedisplay.ThemeOptions{})
	p := provaPayload(t)
	p.Screen = "800x480"
	if _, err := r.hub.SendTheme(themeBoard, p); err == nil || !strings.Contains(err.Error(), "250×122") {
		t.Fatalf("schermo: %v", err)
	}
	if _, err := r.hub.SendTheme("calamaio-0000", provaPayload(t)); err != ErrNotConnected {
		t.Fatalf("scheda sconosciuta: %v", err)
	}
}
```

(`r.hub.Status()` è la funzione di `remote.go` che costruisce `[]DisplayStatus`, `remote.go:385`. In `TestThemeNotAuthedWaits` la chiave sbagliata fa rispondere all'hub `auth-fail` senza chiudere la connessione, `m5.go` `onAuth`.)

- [ ] **Step 3: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/display/ -run TestTheme -v`
Expected: FAIL (`undefined: ThemePayload`).

- [ ] **Step 4: Scrivere `display/themes.go`**

```go
package display

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"time"

	"calamaio/internal/m5"
)

// Temi delle schermate (docs/PROTOCOLLO.md «Temi», docs/TEMI.md). Il servizio offre un tema a una scheda autenticata
// che ha mandato `themes`; la scheda lo scarica con GET /themes/<token> su questa porta (token monouso, TokenTTL,
// solo per l'IP della scheda, come l'OTA), lo controlla, risponde theme-ack; poi theme-use lo attiva.

const (
	ThemeWaiting    = "attesa"
	ThemeOffered    = "offerta"
	ThemeDownloaded = "scaricato"
	ThemeUsing      = "uso"
	ThemeDone       = "fatto"
	ThemeFailed     = "errore"
)

type ThemeHave struct {
	ID  string `json:"id"`
	CRC uint32 `json:"crc"`
}

// ThemePayload: il tema da mandare (CTHM già compilato); nil = tema di serie.
type ThemePayload struct {
	ID, Name, Screen string
	CRC              uint32
	Bin              []byte
}

type ThemeJobView struct {
	ID      string  `json:"id"`
	Board   string  `json:"board"`
	Theme   *string `json:"theme"`
	Name    string  `json:"name"`
	CRC     uint32  `json:"crc"`
	Stage   string  `json:"stage"`
	Error   string  `json:"error,omitempty"`
	Started int64   `json:"started"`
	Updated int64   `json:"updated"`
}

type ThemesView struct {
	Supported bool          `json:"supported"`
	Have      []ThemeHave   `json:"have"`
	Cur       *string       `json:"cur"`
	Job       *ThemeJobView `json:"job"`
}

type themeJob struct {
	id, board, stage, err string
	want                  *ThemePayload
	token                 string
	started, updated      time.Time
	timer                 *time.Timer
	gen                   int
}

type boardThemes struct {
	seen bool
	have []ThemeHave
	cur  *string
	job  *themeJob
}

type themeToken struct {
	job  *themeJob
	ip   string
	exp  time.Time
	used bool
}

type themesMsg struct {
	Have []ThemeHave `json:"have"`
	Cur  *string     `json:"cur"`
}

type themeAckMsg struct {
	ID    string `json:"id"`
	OK    *bool  `json:"ok"`
	Error string `json:"error"`
}

type themeOfferOut struct {
	T     string `json:"t"`
	V     int    `json:"v"`
	ID    string `json:"id"`
	CRC   uint32 `json:"crc"`
	Size  int    `json:"size"`
	Token string `json:"token"`
}

type themeUseOut struct {
	T  string  `json:"t"`
	V  int     `json:"v"`
	ID *string `json:"id"`
}

func parseThemes(data []byte) (*themesMsg, bool) {
	var m themesMsg
	if json.Unmarshal(data, &m) != nil || len(m.Have) > 8 {
		return nil, false
	}
	for _, h := range m.Have {
		if !themeIDOK(h.ID) {
			return nil, false
		}
	}
	if m.Cur != nil && !themeIDOK(*m.Cur) {
		return nil, false
	}
	return &m, true
}

func parseThemeAck(data []byte) (*themeAckMsg, bool) {
	var m themeAckMsg
	if json.Unmarshal(data, &m) != nil || !themeIDOK(m.ID) || m.OK == nil {
		return nil, false
	}
	m.Error = cleanField(m.Error, 127)
	return &m, true
}

func themeIDOK(id string) bool {
	if len(id) < 1 || len(id) > 24 {
		return false
	}
	for i, c := range id {
		if !(c >= 'a' && c <= 'z' || c >= '0' && c <= '9' || c == '-' && i > 0 && i < len(id)-1) {
			return false
		}
	}
	return true
}

func (h *Hub) themeInit() {
	if h.themes == nil {
		h.themes = map[string]*boardThemes{}
		h.themeTokens = map[string]*themeToken{}
	}
}

func (h *Hub) boardThemes(board string) *boardThemes {
	h.themeInit()
	bt := h.themes[board]
	if bt == nil {
		bt = &boardThemes{}
		h.themes[board] = bt
	}
	return bt
}

func themeFinal(stage string) bool { return stage == ThemeDone || stage == ThemeFailed }

func (j *themeJob) view() *ThemeJobView {
	if j == nil {
		return nil
	}
	v := &ThemeJobView{ID: j.id, Board: j.board, Stage: j.stage, Error: j.err, Started: j.started.UnixMilli(), Updated: j.updated.UnixMilli()}
	if j.want != nil {
		id := j.want.ID
		v.Theme, v.Name, v.CRC = &id, j.want.Name, j.want.CRC
	}
	return v
}

// SendTheme: manda il tema p (nil = di serie) alla scheda e lo attiva. La scheda può essere scollegata (partita,
// riavvio): l'operazione aspetta in «attesa» e riparte quando la scheda torna. Sostituisce un'operazione in corso.
func (h *Hub) SendTheme(board string, p *ThemePayload) (*ThemeJobView, error) {
	h.mu.Lock()
	defer h.mu.Unlock()
	c := h.target(board)
	if c == nil && h.keys[board] == nil {
		return nil, ErrNotConnected
	}
	if p != nil && p.Screen != "250x122" {
		return nil, &BlockedError{Msg: "il tema è per uno schermo " + p.Screen + ": questa scheda è 250×122"}
	}
	if c != nil && p != nil && (c.info.W != 250 || c.info.H != 122) {
		return nil, &BlockedError{Msg: fmt.Sprintf("il tema è per uno schermo 250×122, la scheda %s è %d×%d", board, c.info.W, c.info.H)}
	}
	bt := h.boardThemes(board)
	if bt.job != nil {
		h.themeEnd(bt.job, "", "")
	}
	now := h.Now()
	bt.job = &themeJob{id: "t" + m5.Hex(m5.Random(6)), board: board, want: p, stage: ThemeWaiting, started: now, updated: now}
	h.themeStep(c, bt)
	go h.changed()
	return bt.job.view(), nil
}

func (h *Hub) ThemeOf(board string) *ThemeJobView {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.themeInit()
	if bt := h.themes[board]; bt != nil {
		return bt.job.view()
	}
	return nil
}

func (bt *boardThemes) has(p *ThemePayload) bool {
	for _, x := range bt.have {
		if x.ID == p.ID && x.CRC == p.CRC {
			return true
		}
	}
	return false
}

// themeStep fa il passo dovuto dell'operazione in corso. Con h.mu; c può essere nil (scheda scollegata).
func (h *Hub) themeStep(c *conn, bt *boardThemes) {
	j := bt.job
	if j == nil || themeFinal(j.stage) {
		return
	}
	if c == nil || !c.authed || !bt.seen {
		h.themeStage(j, ThemeWaiting)
		return
	}
	switch {
	case j.want == nil && bt.cur == nil, j.want != nil && bt.cur != nil && *bt.cur == j.want.ID && bt.has(j.want):
		h.themeEnd(j, ThemeDone, "")
	case j.stage == ThemeUsing && (j.want == nil || bt.has(j.want)):
		// theme-use già mandato: il themes che segue il theme-ack ha il tema ma non ancora come attivo. Si aspetta il
		// themes della risposta (o la scadenza): rimandarlo farebbe due attivazioni e riarmerebbe il timer.
	case j.want == nil || bt.has(j.want):
		var id *string
		if j.want != nil {
			s := j.want.ID
			id = &s
		}
		if h.enqueueOut(c, themeUseOut{T: "theme-use", V: ProtocolVersion, ID: id}) {
			h.themeStage(j, ThemeUsing)
			h.themeArm(j, h.ThemeUseTimeout, "la scheda non ha attivato il tema")
		}
	case j.stage == ThemeOffered || j.stage == ThemeDownloaded:
		// offerta già partita: si aspetta theme-ack
	default:
		tok := m5.Hex(m5.Random(16))
		if !h.enqueueOut(c, themeOfferOut{T: "theme-offer", V: ProtocolVersion, ID: j.want.ID, CRC: j.want.CRC, Size: len(j.want.Bin), Token: tok}) {
			return
		}
		j.token = tok
		h.themeTokens[tok] = &themeToken{job: j, ip: c.info.IP, exp: h.Now().Add(h.TokenTTL)}
		h.themeStage(j, ThemeOffered)
		h.themeArm(j, h.ThemeAckTimeout, "la scheda non ha risposto all'offerta del tema")
		h.Log.Printf("temi %s: offerta di %s (%d byte, crc %08x) a %s", j.id, j.want.ID, len(j.want.Bin), j.want.CRC, j.board)
	}
}

func (h *Hub) themeStage(j *themeJob, stage string) {
	if j.stage != stage {
		j.stage, j.updated = stage, h.Now()
		go h.changed()
	}
}

func (h *Hub) themeArm(j *themeJob, d time.Duration, why string) {
	j.gen++
	gen := j.gen
	if j.timer != nil {
		j.timer.Stop()
	}
	j.timer = time.AfterFunc(d, func() {
		h.mu.Lock()
		defer h.mu.Unlock()
		if j.gen == gen && !themeFinal(j.stage) {
			h.themeEnd(j, ThemeFailed, why)
			h.Log.Printf("temi %s: %s", j.id, why)
		}
	})
}

// themeEnd chiude l'operazione (stage vuoto = sostituita: resta com'è ma non fa più niente). Con h.mu.
func (h *Hub) themeEnd(j *themeJob, stage, why string) {
	j.gen++
	if j.timer != nil {
		j.timer.Stop()
	}
	if j.token != "" {
		delete(h.themeTokens, j.token)
		j.token = ""
	}
	if stage != "" {
		j.stage, j.err, j.updated = stage, why, h.Now()
		go h.changed()
	}
}

func (h *Hub) onThemes(c *conn, m *themesMsg) {
	h.mu.Lock()
	if !c.authed { // come l'offerta: have/cur falsi non devono finire in /api/status né nella scelta «già sulla scheda»
		h.mu.Unlock()
		h.ignore(c.info.ID, "themes da una connessione non autenticata")
		return
	}
	bt := h.boardThemes(c.info.Board)
	bt.seen, bt.have, bt.cur = true, append([]ThemeHave{}, m.Have...), m.Cur
	h.themeStep(c, bt)
	h.mu.Unlock()
	h.changed()
}

func (h *Hub) onThemeAck(c *conn, m *themeAckMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	bt := h.boardThemes(c.info.Board)
	j := bt.job
	if j == nil || j.want == nil || j.want.ID != m.ID || themeFinal(j.stage) {
		h.Log.Printf("temi: theme-ack di %s per %s senza un'offerta in corso: ignorato", c.info.Board, m.ID)
		return
	}
	if !*m.OK {
		h.themeEnd(j, ThemeFailed, "la scheda ha rifiutato il tema: "+m.Error)
		return
	}
	if !bt.has(j.want) {
		bt.have = append(bt.have, ThemeHave{j.want.ID, j.want.CRC})
	}
	if j.token != "" {
		delete(h.themeTokens, j.token)
		j.token = ""
	}
	j.stage = ThemeDownloaded // il passo dopo è theme-use
	h.themeStep(c, bt)
}

// themeConnClosed: la scheda se n'è andata (anche a metà download): l'operazione torna in attesa. Con h.mu.
func (h *Hub) themeConnClosed(c *conn) {
	if h.themes == nil || c.info.Board == "" {
		return
	}
	bt := h.themes[c.info.Board]
	if bt == nil || h.target(c.info.Board) != nil {
		return
	}
	bt.seen = false
	if j := bt.job; j != nil && !themeFinal(j.stage) {
		h.themeEnd(j, "", "")
		j.stage, j.updated = ThemeWaiting, h.Now()
	}
}

func (h *Hub) themesView(board string) *ThemesView {
	bt := h.themes[board]
	if bt == nil {
		return &ThemesView{Have: []ThemeHave{}}
	}
	return &ThemesView{Supported: bt.seen, Have: append([]ThemeHave{}, bt.have...), Cur: bt.cur, Job: bt.job.view()}
}

// ServeTheme: GET /themes/{token}. Stesse regole di ServeOTA: 404 sconosciuto, 410 usato o scaduto, 403 IP diverso.
func (h *Hub) ServeTheme(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	h.themeInit()
	t := h.themeTokens[tok]
	switch {
	case len(tok) != 32 || !m5.IsLowerHex(tok) || t == nil:
		h.mu.Unlock()
		http.Error(w, "token sconosciuto", http.StatusNotFound)
		return
	case t.used:
		h.mu.Unlock()
		http.Error(w, "token già usato", http.StatusGone)
		return
	case h.Now().After(t.exp):
		delete(h.themeTokens, tok)
		h.mu.Unlock()
		http.Error(w, "token scaduto", http.StatusGone)
		return
	case ip != t.ip:
		h.mu.Unlock()
		h.Log.Printf("temi %s: download rifiutato a %s: il token vale solo per %s", t.job.id, ip, t.ip)
		http.Error(w, "non per questo indirizzo", http.StatusForbidden)
		return
	}
	t.used = true
	j := t.job
	bin := j.want.Bin
	h.themeStage(j, ThemeDownloaded)
	h.mu.Unlock()
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Content-Length", strconv.Itoa(len(bin)))
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Crc", fmt.Sprintf("%08x", j.want.CRC))
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write(bin)
	h.Log.Printf("temi %s: %s scaricato da %s (%d byte)", j.id, j.want.ID, ip, len(bin))
}

var _ = context.Background
```

(togliere l'ultima riga se `context` non serve.)

- [ ] **Step 5: Agganciare l'hub**

`hub.go`:
- campi dell'`Hub`, dopo `wifi`: `// Temi delle schermate (themes.go).` `ThemeAckTimeout, ThemeUseTimeout time.Duration`, `themes map[string]*boardThemes`, `themeTokens map[string]*themeToken`; in `NewHub`: `ThemeAckTimeout: 60 * time.Second, ThemeUseTimeout: 15 * time.Second`;
- `Handler()`: `mux.HandleFunc("GET /themes/{token}", h.ServeTheme) // temi delle schermate (themes.go)`;
- `readLoop`, due casi nuovi accanto a `wifi-list`:

```go
		case "themes":
			m, ok := parseThemes(data)
			if !ok {
				h.ignore(id, "themes non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, themes: m}); err != nil {
				return err
			}
		case "theme-ack":
			m, ok := parseThemeAck(data)
			if !ok {
				h.ignore(id, "theme-ack non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, themeAck: m}); err != nil {
				return err
			}
```

- `run`, nel primo `switch m.t` dopo `hello`: `case "themes": h.onThemes(c, m.themes); continue` e `case "theme-ack": h.onThemeAck(c, m.themeAck); continue`;
- `ServeHTTP`, alla chiusura (dopo `h.wifiConnClosed(c)`, ma **dopo** `delete(h.conns, c.info.ID)`, già così): `h.themeConnClosed(c)`.

`remote.go`: in `inbound` i campi `themes *themesMsg` e `themeAck *themeAckMsg`; in `DisplayStatus` `// Temi delle schermate: ultimo themes della scheda e operazione in corso o ultima.` `Themes *ThemesView \`json:"themes"\``; dove si riempie `st.Workday = workdayView(c)` aggiungere `h.themeInit(); st.Themes = h.themesView(c.info.Board)`.

- [ ] **Step 6: Scheda finta (`fakedisplay/themes.go`)**

```go
package fakedisplay

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"io"
	"net/http"
	"strings"
	"sync"
)

// La parte della scheda dei temi (docs/PROTOCOLLO.md «Temi»), come la fa il firmware: themes dopo auth-ok e a ogni
// cambio; theme-offer → GET http://<pc>/themes/<token>, controllo di lunghezza e CRC, theme-ack, themes; theme-use →
// attiva se ce l'ha, poi themes (invariato se non ce l'ha).
type ThemeOptions struct {
	Enabled bool   // il firmware conosce i temi
	Reject  string // risponde theme-ack ok:false con questo motivo
	NoAck   bool   // non risponde all'offerta (per la scadenza)
}

type ThemeHave struct {
	ID  string `json:"id"`
	CRC uint32 `json:"crc"`
}

type themeState struct {
	mu   sync.Mutex
	opt  ThemeOptions
	have []ThemeHave
	cur  string
}

func (d *Display) Themes() ([]ThemeHave, string) {
	d.th.mu.Lock()
	defer d.th.mu.Unlock()
	return append([]ThemeHave{}, d.th.have...), d.th.cur
}

// ForgetTheme toglie un tema dalla scheda finta senza dirlo al servizio.
func (d *Display) ForgetTheme(id string) {
	d.th.mu.Lock()
	defer d.th.mu.Unlock()
	for i, h := range d.th.have {
		if h.ID == id {
			d.th.have = append(d.th.have[:i], d.th.have[i+1:]...)
			break
		}
	}
	if d.th.cur == id {
		d.th.cur = ""
	}
}

func (d *Display) sendThemes() {
	d.th.mu.Lock()
	var cur any
	if d.th.cur != "" {
		cur = d.th.cur
	}
	m := map[string]any{"t": "themes", "have": append([]ThemeHave{}, d.th.have...), "cur": cur}
	d.th.mu.Unlock()
	d.sendJSON(m)
	d.emit(Event{Type: "themes-sent"})
}

func (d *Display) handleThemes(t string, m map[string]any) bool {
	if !d.th.opt.Enabled {
		return false
	}
	switch t {
	case "theme-offer":
		d.emit(Event{Type: "theme-offer", Msg: m})
		if d.th.opt.NoAck {
			return true
		}
		go d.themeDownload(m)
		return true
	case "theme-use":
		d.emit(Event{Type: "theme-use", Msg: m})
		id, _ := m["id"].(string)
		d.th.mu.Lock()
		ok := id == ""
		for _, h := range d.th.have {
			ok = ok || h.ID == id
		}
		if ok {
			d.th.cur = id
		}
		d.th.mu.Unlock()
		d.sendThemes()
		return true
	}
	return false
}

func (d *Display) themeDownload(m map[string]any) {
	id, _ := m["id"].(string)
	tok, _ := m["token"].(string)
	crc := uint32(m["crc"].(float64))
	ack := func(ok bool, why string) {
		msg := map[string]any{"t": "theme-ack", "id": id, "ok": ok}
		if why != "" {
			msg["error"] = why
		}
		d.sendJSON(msg)
	}
	if d.th.opt.Reject != "" {
		ack(false, d.th.opt.Reject)
		return
	}
	base := strings.Replace(d.wsURL, "ws://", "http://", 1)
	base = base[:strings.LastIndex(base, "/")]
	res, err := http.Get(base + "/themes/" + tok)
	if err != nil {
		ack(false, "download non riuscito")
		return
	}
	defer res.Body.Close()
	bin, _ := io.ReadAll(res.Body)
	switch {
	case res.StatusCode != 200:
		ack(false, fmt.Sprintf("download non riuscito (HTTP %d)", res.StatusCode))
		return
	case len(bin) < 72 || string(bin[:4]) != "CTHM" || binary.LittleEndian.Uint32(bin[8:]) != crc || crc32.ChecksumIEEE(bin[12:]) != crc:
		ack(false, "CRC sbagliato")
		return
	}
	d.th.mu.Lock()
	keep := d.th.have[:0]
	for _, h := range d.th.have {
		if h.ID != id {
			keep = append(keep, h)
		}
	}
	d.th.have = append(keep, ThemeHave{id, crc})
	d.th.mu.Unlock()
	ack(true, "")
	d.sendThemes()
}
```

In `fakedisplay.go`: `Options.Themes ThemeOptions`; nella struct `Display` i campi `th themeState` e `wsURL string`; in `DialWith` `d.wsURL = url` e `d.th.opt = o.Themes`; nel caso `"auth-ok"`, dopo il `settingsMsg(true)`: `if d.th.opt.Enabled { d.sendThemes() }`; in `handle`, prima dello `switch t` (accanto a `handleWorkday`): `if d.handleThemes(t, m) { return }`.

- [ ] **Step 7: Rotta della scheda nell'API**

In `api/themes.go`:

```go
// ThemeSender: mandare un tema a una scheda (display.Hub).
type ThemeSender interface {
	SendTheme(board string, p *display.ThemePayload) (*display.ThemeJobView, error)
}

func (s *Server) apiDisplayTheme(w http.ResponseWriter, r *http.Request) {
	if s.ThemeSend == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	var body struct {
		ID json.RawMessage `json:"id"`
	}
	dec := json.NewDecoder(io.LimitReader(r.Body, 1<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&body); err != nil || body.ID == nil {
		writeJSON(w, http.StatusBadRequest, errBody(`serve un oggetto JSON {"id": "tema" | null}`))
		return
	}
	var p *display.ThemePayload
	if string(body.ID) != "null" {
		var id string
		if json.Unmarshal(body.ID, &id) != nil {
			writeJSON(w, http.StatusBadRequest, errBody("id: una stringa o null"))
			return
		}
		if s.themesOff(w) {
			return
		}
		_, in, err := s.Themes.Get(id)
		if err != nil {
			themeError(w, err)
			return
		}
		c, err := s.Themes.Compiled(id)
		if err != nil {
			writeJSON(w, http.StatusConflict, errBody("tema non valido: "+err.Error()))
			return
		}
		p = &display.ThemePayload{ID: in.ID, Name: in.Name, Screen: in.Screen, CRC: c.CRC, Bin: c.Bin}
	}
	v, err := s.ThemeSend.SendTheme(r.PathValue("board"), p)
	var blk *display.BlockedError
	switch {
	case errors.As(err, &blk): // schermo diverso: commandError non conosce BlockedError (darebbe 500), come otaError
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
		return
	case err != nil:
		commandError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"theme": v})
}
```

(import `encoding/json`, `calamaio/internal/display`.) In `api/server.go`: campo `ThemeSend ThemeSender` accanto a `Themes`, rotta `mux.HandleFunc("POST /api/displays/{board}/theme", postGuard(s.apiDisplayTheme))`. `commandError` (`api/web.go`) oggi non ha il caso `*display.BlockedError` (l'OTA lo gestisce a parte in `otaError`, `api/ota.go`): per questo `apiDisplayTheme` lo traduce in 409 da sé e `commandError` resta com'è (darebbe 500, e cambiarlo toccherebbe anche le altre rotte). In `server/server.go`: `as.ThemeSend = hub`.

- [ ] **Step 8: Prova end-to-end in `server/themes_test.go`**

```go
func TestWebThemeToDisplay(t *testing.T) {
	s, w, _ := startThemes(t)
	if code, _ := w.postRaw("/api/themes", provaTheme(t)); code != 201 {
		t.Fatal(code)
	}
	d := themeBoard(t, s) // scheda finta abbinata e autenticata con i temi (fakedisplay.Options{Themes: {Enabled: true}})
	code, m := w.post("/api/displays/"+d.Board+"/theme", `{"id":"prova"}`)
	if code != 202 || m["theme"].(map[string]any)["theme"] != "prova" {
		t.Fatalf("%d %v", code, m)
	}
	st := waitStatus(t, w, func(st map[string]any) bool {
		th, _ := firstDisplay(st)["themes"].(map[string]any)
		job, _ := th["job"].(map[string]any)
		return job != nil && job["stage"] == "fatto"
	})
	if firstDisplay(st)["themes"].(map[string]any)["cur"] != "prova" {
		t.Fatalf("%v", firstDisplay(st)["themes"])
	}
	if code, _ := w.post("/api/displays/"+d.Board+"/theme", `{"id":"manca"}`); code != 404 {
		t.Fatalf("tema sconosciuto: %d", code)
	}
	if code, _ := w.post("/api/displays/"+d.Board+"/theme", `{"tema":"prova"}`); code != 400 {
		t.Fatalf("corpo: %d", code)
	}
}
```

`themeBoard(t, s)` va scritto in questo file sul modello dei test end-to-end della giornata (`server/workday_test.go`, `TestWebWorkdayEndToEnd`: abbinamento della scheda finta con la chiave del `KeyStore` del test e `fakedisplay.DialWith`), passando `fakedisplay.Options{Themes: fakedisplay.ThemeOptions{Enabled: true}}` e aspettando l'evento `themes-sent`.

- [ ] **Step 9: Eseguire i test**

Run: `cd service && go test ./internal/display/ -run 'TestTheme|TestExamples|TestSchemaRejects' -v -race && go test ./...`
Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add service/internal/display service/internal/fakedisplay service/internal/api service/internal/server protocol/schema/message.schema.json protocol/examples/theme*.json
git commit -m "Temi: protocollo nel servizio (themes, theme-offer, download col token, theme-ack, theme-use)"
```

---
### Task B4: Firmware — installatore e messaggi dei temi (logica pura)

**Files:**
- Create: `firmware/src/core/theme_store.h`, `firmware/src/core/theme_store.cpp`, `firmware/src/core/theme_proto.h`, `firmware/src/core/theme_proto.cpp`
- Modify: `firmware/src/core/apply_state.h` (`MsgType::ThemeOffer`, `MsgType::ThemeUse` in fondo all'enum), `firmware/src/core/apply_state.cpp` (`msgType`, filtro: `size`, `token`)
- Modify: `firmware/src/core/theme_runtime.h`, `firmware/src/core/theme_runtime.cpp` (`Runtime::lend`, `Runtime::giveBack`: il buffer in prestito alla partita)
- Test: `firmware/test/test_theme_store/test_main.cpp`, `firmware/test/test_theme_runtime/test_main.cpp` (`test_lend_give_back`)

**Interfaces:**
- Consumes: `Storage`, `MemStorage` (9 posti, `failWrites`, `reads`, `raw`), `verifyAt`, `catalog`, `CatalogEntry`, `Header`, `IndexEntry`, `errorText`, `kMaxThemes`, `theme::kIdLen` (= 25, `core/theme_format.h`), `kMaxBlock` (piano 1a); `msgType`, `parseMessage` (`core/apply_state`).
- Nota sui nomi: `cal::kIdLen` (= 40, `core/model.h`, id di sessione) e `cal::theme::kIdLen` (= 25) esistono tutti e due. Nei test, con `using namespace cal; using namespace cal::theme;`, un `kIdLen` nudo è ambiguo e non compila: si scrive sempre `theme::kIdLen` (come fanno già i test del piano 1a); dentro `namespace cal { namespace theme {` il nome nudo è quello del tema.
- Produces (namespace `cal::theme`):
  - `class Installer`: `bool begin(Storage &st, const char *id, uint32_t crc, uint32_t size, char *why, size_t cap)`, `bool write(const uint8_t *p, size_t n, char *why, size_t cap)`, `bool finish(const char *activeId, uint8_t *scratch, char *why, size_t cap)` (controllo completo, rinomina, **poi** con 9 temi toglie il più vecchio non attivo, ordine; rinomina fallita → «salvataggio non riuscito» e non si toglie niente), `void abort()`, `bool busy() const`, `const char *id() const`;
  - `int orderList(Storage &st, char (*ids)[kIdLen], int cap)` (qui e sotto `kIdLen` = `theme::kIdLen`) (dal più vecchio; solo i temi presenti; quelli presenti ma non nell'ordine vanno in testa), `void orderDrop(Storage &st, const char *id)`;
  - `bool canUse(Storage &st, const char *id)` ('' sempre vero);
  - `uint8_t *Runtime::lend(size_t n)`, `void Runtime::giveBack()`, `bool Runtime::lent() const` (buffer da 4 096 byte del `Runtime` in prestito alla partita, come `LogRing::lend`/`giveBack`);
  - `struct Offer { char id[kIdLen]; uint32_t crc; uint32_t size; char token[33]; }`, `bool decodeOffer(JsonVariantConst root, Offer &out)`, `bool decodeUse(JsonVariantConst root, char *id, size_t cap)`, `size_t encodeThemes(const CatalogEntry *e, int n, const char *cur, char *out, size_t cap)`, `size_t encodeAck(const char *id, bool ok, const char *error, char *out, size_t cap)`, `bool downloadUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap)` (`http://<host>:<port>/themes/<token>`);
  - `enum class OfferVerdict : uint8_t { Accept, NotAuthed, Busy }`, `OfferVerdict acceptOffer(bool authed, bool busy)` (§7: prima l'autenticazione, poi «un altro tema in arrivo»), `bool acceptUse(bool authed)`: le decisioni della scheda su `theme-offer` e `theme-use`, pure e provate (in `net.cpp` restano solo i testi e gli effetti).

- [ ] **Step 1: Scrivere il test che fallisce**

`firmware/test/test_theme_store/test_main.cpp`:

```cpp
// Temi sulla scheda: installazione tutto o niente, ordine e nono tema, messaggi (PROTOCOLLO.md «Temi»).
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/theme_proto.h"
#include "core/theme_runtime.h"
#include "core/theme_store.h"

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
static uint32_t crcOf(const std::vector<uint8_t> &f) { return f[8] | f[9] << 8 | f[10] << 16 | uint32_t(f[11]) << 24; }
// Lo stesso tema con un altro id (intestazione e CRC rifatti): per riempire la scheda.
static std::vector<uint8_t> withId(const char *id) {
  std::vector<uint8_t> f = load("prova.cthm");
  memset(f.data() + 20, 0, 24);
  memcpy(f.data() + 20, id, strlen(id));
  const uint32_t c = crc32Update(0, f.data() + 12, f.size() - 12);
  f[8] = c; f[9] = c >> 8; f[10] = c >> 16; f[11] = c >> 24;
  return f;
}

static MemStorage gSt;
static uint8_t gScratch[kMaxBlock];

static bool install(const char *id, const std::vector<uint8_t> &f, const char *active = "", char *why = nullptr) {
  char w[96] = "";
  Installer in;
  bool ok = in.begin(gSt, id, crcOf(f), uint32_t(f.size()), w, sizeof w);
  for (size_t off = 0; ok && off < f.size(); off += 500)  // a pezzi, come dalla rete
    ok = in.write(f.data() + off, f.size() - off < 500 ? f.size() - off : 500, w, sizeof w);
  ok = ok && in.finish(active, gScratch, w, sizeof w);
  if (!ok) in.abort();
  if (why) strcpy(why, w);
  return ok;
}

static void test_install_ok_and_catalog() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  CatalogEntry e[kMaxThemes];
  TEST_ASSERT_EQUAL(1, catalog(gSt, e, kMaxThemes));
  TEST_ASSERT_EQUAL_STRING("Mascotte grande", e[0].name);
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());  // il temporaneo non resta
}

static void test_install_rejects() {
  gSt.eraseAll();
  char why[96];
  std::vector<uint8_t> f = load("prova.cthm");
  // offerta con un altro id: il file non è quello offerto
  TEST_ASSERT_FALSE(install("altro", f, "", why));
  TEST_ASSERT_EQUAL_STRING("il tema non è quello offerto", why);
  // un byte rovinato
  std::vector<uint8_t> bad = f;
  bad[bad.size() / 2] ^= 0x40;
  Installer in;
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(bad.data(), bad.size(), why, sizeof why));
  TEST_ASSERT_FALSE(in.finish("", gScratch, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("CRC sbagliato", why);
  in.abort();
  // troppo lungo per l'offerta
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), 100, why, sizeof why));
  TEST_ASSERT_FALSE(in.write(f.data(), 200, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("più byte di quelli offerti", why);
  in.abort();
  // offerta oltre 64 KB o id non valido: rifiutata subito
  TEST_ASSERT_FALSE(in.begin(gSt, "prova", 1, 70000, why, sizeof why));
  TEST_ASSERT_FALSE(in.begin(gSt, "Prova!", 1, 100, why, sizeof why));
  CatalogEntry none[kMaxThemes];
  TEST_ASSERT_EQUAL(0, catalog(gSt, none, kMaxThemes));
}

static void test_install_abort_keeps_old() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  Installer in;
  char why[96];
  std::vector<uint8_t> f = load("prova.cthm");
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), 300, why, sizeof why));  // la rete cade a metà
  in.abort();
  Header h;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h)));
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
  // un download che si ferma prima della fine non si installa
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size() - 1, why, sizeof why));
  TEST_ASSERT_FALSE(in.finish("", gScratch, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("download incompleto", why);
  in.abort();
}

static void test_ninth_theme_evicts_oldest_not_active() {
  gSt.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"};
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  CatalogEntry all[kMaxThemes];
  TEST_ASSERT_EQUAL(8, catalog(gSt, all, kMaxThemes));
  TEST_ASSERT_TRUE(install("t9", withId("t9"), "t1"));  // t1 attivo: esce t2, il più vecchio non attivo
  char got[kMaxThemes + 1][theme::kIdLen];
  const int n = orderList(gSt, got, kMaxThemes + 1);
  TEST_ASSERT_EQUAL(8, n);
  TEST_ASSERT_EQUAL_STRING("t1", got[0]);
  TEST_ASSERT_EQUAL_STRING("t3", got[1]);
  TEST_ASSERT_EQUAL_STRING("t9", got[7]);
  TEST_ASSERT_EQUAL(-1, gSt.size("t2"));
  // reinstallare un tema che c'è già non toglie niente e lo porta in fondo
  TEST_ASSERT_TRUE(install("t3", withId("t3"), "t1"));
  TEST_ASSERT_EQUAL(8, orderList(gSt, got, kMaxThemes + 1));
  TEST_ASSERT_EQUAL_STRING("t3", got[7]);
}

// Tutto o niente anche col nono tema: si salva prima e si toglie dopo, quindi una rinomina che fallisce lascia gli
// otto temi com'erano (col vecchio ordine «togli, poi salva» t2 sparirebbe e t9 non ci sarebbe).
static void test_ninth_commit_fails_keeps_all() {
  gSt.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"};
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  std::vector<uint8_t> f = withId("t9");
  char why[96];
  Installer in;
  TEST_ASSERT_TRUE(in.begin(gSt, "t9", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size(), why, sizeof why));
  gSt.failWrites = true;  // la flash rifiuta la rinomina (tmpCommit) e l'ordine
  TEST_ASSERT_FALSE(in.finish("t1", gScratch, why, sizeof why));
  gSt.failWrites = false;
  TEST_ASSERT_EQUAL_STRING("salvataggio non riuscito", why);
  in.abort();
  char got[kMaxThemes + 1][theme::kIdLen];
  TEST_ASSERT_EQUAL(8, orderList(gSt, got, kMaxThemes + 1));
  TEST_ASSERT_EQUAL_STRING("t1", got[0]);
  TEST_ASSERT_EQUAL_STRING("t2", got[1]);  // il più vecchio non attivo è ancora lì
  TEST_ASSERT_EQUAL_STRING("t8", got[7]);
  TEST_ASSERT_EQUAL(-1, gSt.size("t9"));
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
}

static void test_use_unknown_theme() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  TEST_ASSERT_TRUE(canUse(gSt, ""));
  TEST_ASSERT_TRUE(canUse(gSt, "prova"));
  TEST_ASSERT_FALSE(canUse(gSt, "manca"));
}

static void test_messages() {
  JsonDocument doc;
  const char *offer = R"({"t":"theme-offer","v":1,"id":"prova","crc":2864434397,"size":3120,"token":"0123456789abcdef0123456789abcdef"})";
  TEST_ASSERT_TRUE(parseMessage(offer, strlen(offer), doc));
  TEST_ASSERT_EQUAL(int(MsgType::ThemeOffer), int(msgType(doc.as<JsonVariantConst>())));
  Offer o;
  TEST_ASSERT_TRUE(decodeOffer(doc.as<JsonVariantConst>(), o));
  TEST_ASSERT_EQUAL_STRING("prova", o.id);
  TEST_ASSERT_EQUAL_UINT32(2864434397u, o.crc);
  TEST_ASSERT_EQUAL_UINT32(3120, o.size);
  const char *badTok = R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":3120,"token":"0123"})";
  parseMessage(badTok, strlen(badTok), doc);
  TEST_ASSERT_FALSE(decodeOffer(doc.as<JsonVariantConst>(), o));
  char id[theme::kIdLen];
  const char *use = R"({"t":"theme-use","v":1,"id":null})";
  parseMessage(use, strlen(use), doc);
  TEST_ASSERT_EQUAL(int(MsgType::ThemeUse), int(msgType(doc.as<JsonVariantConst>())));
  TEST_ASSERT_TRUE(decodeUse(doc.as<JsonVariantConst>(), id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("", id);
  char out[512];
  CatalogEntry e[1];
  memset(e, 0, sizeof e);
  strcpy(e[0].id, "prova");
  e[0].crc = 2864434397u;
  TEST_ASSERT_TRUE(encodeThemes(e, 1, "prova", out, sizeof out) > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"themes","v":1,"have":[{"id":"prova","crc":2864434397}],"cur":"prova"})", out);
  encodeThemes(e, 0, "", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"themes","v":1,"have":[],"cur":null})", out);
  encodeAck("prova", false, "CRC sbagliato", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"theme-ack","v":1,"id":"prova","ok":false,"error":"CRC sbagliato"})", out);
  TEST_ASSERT_TRUE(downloadUrl("192.168.1.20", 8766, o.token, out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:8766/themes/0123456789abcdef0123456789abcdef", out);
}

// §7 lato scheda: theme-offer e theme-use valgono solo da una connessione autenticata; l'autenticazione si guarda prima
// dell'«occupato» (una connessione non autenticata non deve sapere se c'è un download in corso).
static void test_offer_only_when_authed() {
  TEST_ASSERT_EQUAL(int(OfferVerdict::NotAuthed), int(acceptOffer(false, false)));
  TEST_ASSERT_EQUAL(int(OfferVerdict::NotAuthed), int(acceptOffer(false, true)));
  TEST_ASSERT_EQUAL(int(OfferVerdict::Busy), int(acceptOffer(true, true)));
  TEST_ASSERT_EQUAL(int(OfferVerdict::Accept), int(acceptOffer(true, false)));
  TEST_ASSERT_FALSE(acceptUse(false));
  TEST_ASSERT_TRUE(acceptUse(true));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_install_ok_and_catalog);
  RUN_TEST(test_install_rejects);
  RUN_TEST(test_install_abort_keeps_old);
  RUN_TEST(test_ninth_theme_evicts_oldest_not_active);
  RUN_TEST(test_ninth_commit_fails_keeps_all);
  RUN_TEST(test_use_unknown_theme);
  RUN_TEST(test_messages);
  RUN_TEST(test_offer_only_when_authed);
  return UNITY_END();
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd firmware && pio test -e native -f test_theme_store`
Expected: FAIL (manca `core/theme_store.h`).

- [ ] **Step 3: Scrivere `core/theme_store.{h,cpp}`**

```cpp
// theme_store.h — Temi sulla scheda (PROTOCOLLO.md «Temi»): installazione tutto o niente su Storage (LittleFS in
// hw/theme_fs, MemStorage nei test). Il file arriva a pezzi nel temporaneo, si controlla tutto (CRC, intestazione,
// ogni blocco con l'interprete in sola lettura), poi si rinomina sopra <id>. Al massimo 8 temi: esce il più vecchio
// non attivo (ordine d'installazione nel file «order»).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "core/theme_runtime.h"

namespace cal {
namespace theme {

class Installer {
 public:
  Installer() : st_(nullptr), active_(false), got_(0), size_(0), crc_(0) { id_[0] = 0; }
  bool begin(Storage &st, const char *id, uint32_t crc, uint32_t size, char *why, size_t cap);
  bool write(const uint8_t *p, size_t n, char *why, size_t cap);
  bool finish(const char *activeId, uint8_t *scratch, char *why, size_t cap);
  void abort();
  bool busy() const { return active_; }
  const char *id() const { return id_; }

 private:
  Storage *st_;
  bool active_;
  uint32_t got_, size_, crc_;
  char id_[kIdLen];
};

int orderList(Storage &st, char (*ids)[kIdLen], int cap);
void orderDrop(Storage &st, const char *id);
bool canUse(Storage &st, const char *id);
bool validThemeId(const char *id);

}  // namespace theme
}  // namespace cal
```

```cpp
// theme_store.cpp
#include "core/theme_store.h"

#include <string.h>

#include "core/strbuf.h"

namespace cal {
namespace theme {

namespace {

const int kOrderCap = kMaxThemes * kIdLen + 16;

void say(char *why, size_t cap, const char *s) { scopy(why, cap, s); }

// Ordine salvato («id\n» ripetuto, dal più vecchio), senza doppioni.
int readOrder(Storage &st, char (*ids)[kIdLen], int cap) {
  char buf[kOrderCap + 1];
  const int n = st.readOrder(buf, kOrderCap);
  if (n <= 0) return 0;
  buf[n] = 0;
  int k = 0;
  for (char *p = buf; *p && k < cap;) {
    char *e = strchr(p, '\n');
    if (e) *e = 0;
    if (validThemeId(p)) {
      bool dup = false;
      for (int i = 0; i < k; i++) dup = dup || !strcmp(ids[i], p);
      if (!dup) scopy(ids[k++], kIdLen, p);
    }
    if (!e) break;
    p = e + 1;
  }
  return k;
}

void writeOrder(Storage &st, char (*ids)[kIdLen], int n) {
  char buf[kOrderCap];
  Sb b(buf, sizeof buf);
  for (int i = 0; i < n; i++) b.s(ids[i]).ch('\n');
  st.writeOrder(buf, int(b.len()));
}

}  // namespace

bool validThemeId(const char *id) {
  const size_t n = id ? strlen(id) : 0;
  if (n < 1 || n > 24) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = id[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && i > 0 && i + 1 < n && id[i - 1] != '-');
    if (!ok) return false;
  }
  return true;
}

int orderList(Storage &st, char (*ids)[kIdLen], int cap) {
  char present[kMaxThemes + 1][kIdLen];
  const int np = st.list(present, kMaxThemes + 1);
  char saved[kMaxThemes + 1][kIdLen];
  const int ns = readOrder(st, saved, kMaxThemes + 1);
  int k = 0;
  // prima quelli presenti che l'ordine non conosce (i più vecchi per sicurezza), poi l'ordine salvato
  for (int i = 0; i < np && k < cap; i++) {
    bool known = false;
    for (int j = 0; j < ns; j++) known = known || !strcmp(saved[j], present[i]);
    if (!known) scopy(ids[k++], kIdLen, present[i]);
  }
  for (int j = 0; j < ns && k < cap; j++)
    if (st.size(saved[j]) >= 0) scopy(ids[k++], kIdLen, saved[j]);
  return k;
}

void orderDrop(Storage &st, const char *id) {
  char ids[kMaxThemes + 1][kIdLen];
  int n = orderList(st, ids, kMaxThemes + 1), k = 0;
  for (int i = 0; i < n; i++)
    if (strcmp(ids[i], id)) memcpy(ids[k++], ids[i], kIdLen);
  writeOrder(st, ids, k);
}

bool canUse(Storage &st, const char *id) { return !id || !id[0] || st.size(id) >= 0; }

bool Installer::begin(Storage &st, const char *id, uint32_t crc, uint32_t size, char *why, size_t cap) {
  abort();
  if (!validThemeId(id)) { say(why, cap, "id del tema non valido"); return false; }
  if (size < uint32_t(kHeaderLen) || size > kMaxFile) { say(why, cap, "dimensione del tema non valida"); return false; }
  if (!st.tmpBegin()) { say(why, cap, "spazio per i temi non disponibile"); return false; }
  st_ = &st;
  active_ = true;
  got_ = 0;
  size_ = size;
  crc_ = crc;
  scopy(id_, kIdLen, id);
  return true;
}

bool Installer::write(const uint8_t *p, size_t n, char *why, size_t cap) {
  if (!active_) { say(why, cap, "nessuna installazione in corso"); return false; }
  if (got_ + n > size_) { say(why, cap, "più byte di quelli offerti"); return false; }
  if (!st_->tmpAppend(p, uint32_t(n))) { say(why, cap, "scrittura non riuscita"); return false; }
  got_ += uint32_t(n);
  return true;
}

bool Installer::finish(const char *activeId, uint8_t *scratch, char *why, size_t cap) {
  if (!active_) { say(why, cap, "nessuna installazione in corso"); return false; }
  if (got_ != size_) { say(why, cap, "download incompleto"); return false; }
  Header h;
  IndexEntry idx[kMaxScreens];
  const Error e = verifyAt(*st_, nullptr, int32_t(size_), scratch, &h, idx);
  if (e != Error::Ok) { say(why, cap, errorText(e)); return false; }
  if (strcmp(h.id, id_)) { say(why, cap, "il tema non è quello offerto"); return false; }
  if (h.crc != crc_) { say(why, cap, "CRC diverso da quello offerto"); return false; }
  // Tutto o niente: prima si salva, poi si fa posto. L'ordine si legge prima della rinomina (il nuovo non c'è ancora,
  // o c'è la sua versione vecchia). Se la rinomina fallisce i temi sulla scheda restano quelli. Il nono file ci sta:
  // MemStorage ha 9 posti apposta, in LittleFS 9 × 64 KB stanno nella partizione.
  char ids[kMaxThemes + 1][kIdLen];
  const int n = orderList(*st_, ids, kMaxThemes + 1);
  if (!st_->tmpCommit(id_)) { say(why, cap, "salvataggio non riuscito"); return false; }
  active_ = false;
  int others = 0;
  for (int i = 0; i < n; i++) others += strcmp(ids[i], id_) != 0;
  // con 9 temi esce il più vecchio che non è attivo (e non è questo); l'ordine: senza questo e senza i tolti, poi questo
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!strcmp(ids[i], id_)) continue;
    if (others >= kMaxThemes && !(activeId && !strcmp(ids[i], activeId))) {
      st_->remove(ids[i]);
      others--;
      continue;
    }
    memcpy(ids[k++], ids[i], kIdLen);
  }
  scopy(ids[k++], kIdLen, id_);
  writeOrder(*st_, ids, k);
  return true;
}

void Installer::abort() {
  if (active_ && st_) st_->tmpDrop();
  active_ = false;
}

}  // namespace theme
}  // namespace cal
```

`finish` legge l'ordine **prima** della rinomina: un tema presente ma sconosciuto all'ordine (`orderList` lo mette in testa) è il più vecchio, e il nuovo va sempre in fondo. Con un `writeOrder` che fallisce dopo la rinomina il tema è installato lo stesso (l'ordine è solo un'indicazione: `orderList` rimette in testa i presenti che non conosce). I test del nono tema provano i due casi (riuscito e rinomina fallita).

- [ ] **Step 4: Scrivere `core/theme_proto.{h,cpp}` e i tipi nuovi in `apply_state`**

`theme_proto.h` dichiara quanto in «Produces» (include `ArduinoJson.h`, `core/theme_runtime.h`). `theme_proto.cpp`:

```cpp
#include "core/theme_proto.h"

#include <stdio.h>
#include <string.h>

#include "core/apply_state.h"
#include "core/strbuf.h"
#include "core/theme_store.h"

namespace cal {
namespace theme {

namespace {
bool lowerHex(const char *s, size_t n) {
  if (!s || strlen(s) != n) return false;
  for (size_t i = 0; i < n; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return true;
}
}  // namespace

bool decodeOffer(JsonVariantConst root, Offer &out) {
  memset(&out, 0, sizeof out);
  if (msgType(root) != MsgType::ThemeOffer) return false;
  const char *id = root["id"].as<const char *>(), *tok = root["token"].as<const char *>();
  if (!validThemeId(id) || !lowerHex(tok, 32) || !root["crc"].is<uint32_t>() || !root["size"].is<uint32_t>()) return false;
  out.size = root["size"].as<uint32_t>();
  if (out.size < uint32_t(kHeaderLen) || out.size > kMaxFile) return false;
  out.crc = root["crc"].as<uint32_t>();
  scopy(out.id, kIdLen, id);
  scopy(out.token, sizeof out.token, tok);
  return true;
}

bool decodeUse(JsonVariantConst root, char *id, size_t cap) {
  if (msgType(root) != MsgType::ThemeUse) return false;
  if (root["id"].isNull()) { if (cap) id[0] = 0; return true; }
  const char *s = root["id"].as<const char *>();
  if (!validThemeId(s)) return false;
  scopy(id, cap, s);
  return true;
}

size_t encodeThemes(const CatalogEntry *e, int n, const char *cur, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "themes";
  d["v"] = 1;
  JsonArray have = d["have"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = have.add<JsonObject>();
    o["id"] = e[i].id;
    o["crc"] = e[i].crc;
  }
  if (cur && cur[0]) d["cur"] = cur;
  else d["cur"] = nullptr;
  const size_t len = measureJson(d);
  if (len + 1 > cap) return 0;
  return serializeJson(d, out, cap);
}

size_t encodeAck(const char *id, bool ok, const char *error, char *out, size_t cap) {
  JsonDocument d;
  d["t"] = "theme-ack";
  d["v"] = 1;
  d["id"] = id;
  d["ok"] = ok;
  if (!ok && error && error[0]) d["error"] = error;
  const size_t len = measureJson(d);
  if (len + 1 > cap) return 0;
  return serializeJson(d, out, cap);
}

bool downloadUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap) {
  const int n = snprintf(out, cap, "http://%s:%u/themes/%s", host, unsigned(port), token);
  return n > 0 && size_t(n) < cap;
}

// §7: solo da una connessione autenticata (prima di tutto), un tema alla volta.
OfferVerdict acceptOffer(bool authed, bool busy) {
  if (!authed) return OfferVerdict::NotAuthed;
  return busy ? OfferVerdict::Busy : OfferVerdict::Accept;
}

bool acceptUse(bool authed) { return authed; }

}  // namespace theme
}  // namespace cal
```

In `apply_state.h`: in fondo a `enum class MsgType` `// 0.13.0, temi delle schermate (core/theme_proto): servizio → display, solo da connessione autenticata` `ThemeOffer, ThemeUse,`. In `apply_state.cpp`: in `msgType` `if (streq(t, "theme-offer")) return MsgType::ThemeOffer; if (streq(t, "theme-use")) return MsgType::ThemeUse;`; nel filtro `f["size"] = true; f["token"] = true;` (con un commento «0.13.0: theme-offer {id, crc, size, token}, theme-use {id}»).

- [ ] **Step 5: `Runtime::lend()`/`giveBack()`: il buffer del tema in prestito alla partita**

In modalità gioco lo heap è già al limite: Pokémon Red (MBC3 + 32 KB) ci sta solo con l'anello del registro in
prestito (0.12.2-gb: `LogRing::lend`/`giveBack`, `logtee::lendRing`/`returnRing`, usati da `lendRing`/`returnRing` in
`hw/gb_run.cpp`), e la 0.12.4-gb dopo il Wi-Fi spento non aveva un blocco libero da 8 KB. Il `Runtime` statico del
Task B7 (`buf_` da 4 096 byte più indice e intestazione, circa 4,3 KB) in partita non serve (rete spenta, nessuna
schermata dei temi): si presta come l'anello (spec §3: «come l'anello del registro, se servirà»). Il collegamento a
`gb_run` è nel Task B7, Step 7.

Test in fondo a `firmware/test/test_theme_runtime/test_main.cpp` (e `RUN_TEST(test_lend_give_back);` in `main`):

```cpp
// Prestito alla partita: niente blocchi né attivazioni finché è prestato, il tema resta quello; dopo giveBack il
// blocco si rilegge (la partita ci ha scritto sopra).
static void test_lend_give_back() {
  reset();
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_TRUE(gRt.block(Screen::Home, b, n));
  TEST_ASSERT_NULL(gRt.lend(kMaxBlock + 1));
  uint8_t *p = gRt.lend(kMaxBlock);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_TRUE(gRt.lent());
  TEST_ASSERT_NULL(gRt.lend(16));  // già prestato
  memset(p, 0xA5, kMaxBlock);      // la RAM della cartuccia
  TEST_ASSERT_FALSE(gRt.block(Screen::Home, b, n));
  TEST_ASSERT_FALSE(gRt.activate("lista"));
  char bad[theme::kIdLen];
  TEST_ASSERT_FALSE(gRt.takeBroken(bad, sizeof bad));  // in prestito non è un tema rotto
  TEST_ASSERT_EQUAL_STRING("prova", gRt.active());
  gRt.giveBack();
  TEST_ASSERT_FALSE(gRt.lent());
  const int before = gSt.reads;
  TEST_ASSERT_TRUE(gRt.block(Screen::Home, b, n));  // riletto dall'archivio, non i byte della partita
  TEST_ASSERT_EQUAL(before + 1, gSt.reads);
  TEST_ASSERT_EQUAL(int(Error::Ok), int(checkBlock(b, n)));
}
```

In `core/theme_runtime.h`, nella classe `Runtime` (campo privato `bool lent_;`, `false` nel costruttore):

```cpp
  // Modalità gioco (net::suspended(), 0.13.0): buf_ si presta alla partita come RAM della cartuccia, come l'anello del
  // registro (LogRing::lend). lend: buf_ se n ≤ kMaxBlock e non già prestato, altrimenti nullptr; il blocco caricato
  // si dimentica. In prestito block() e activate() rispondono false senza segnare il tema rotto (id, intestazione e
  // indice restano). giveBack: buf_ torna al Runtime, il blocco si rilegge al prossimo disegno.
  uint8_t *lend(size_t n);
  void giveBack();
  bool lent() const { return lent_; }
```

In `core/theme_runtime.cpp`:

```cpp
uint8_t *Runtime::lend(size_t n) {
  if (lent_ || n > sizeof buf_) return nullptr;
  lent_ = true;
  loaded_ = Screen::None;
  return buf_;
}

void Runtime::giveBack() {
  lent_ = false;
  loaded_ = Screen::None;
}
```

e in testa a `activate` `if (lent_) return false;` (prima di toccare `loaded_` e `id_`), in testa a `block`
`if (lent_) return false;`.

- [ ] **Step 6: Eseguire i test**

Run: `cd firmware && pio test -e native -f test_theme_store -f test_theme_runtime && pio test -e native && pio test -e native_asan`
Expected: PASS, nient'altro cambiato.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/core/theme_store.h firmware/src/core/theme_store.cpp firmware/src/core/theme_proto.h firmware/src/core/theme_proto.cpp firmware/src/core/apply_state.h firmware/src/core/apply_state.cpp firmware/src/core/theme_runtime.h firmware/src/core/theme_runtime.cpp firmware/test/test_theme_store firmware/test/test_theme_runtime
git commit -m "Temi: installazione tutto o niente sulla scheda, nono tema e messaggi"
```

---
### Task B5: Firmware — LittleFS dei temi e download nel task di rete

**Files:**
- Create: `firmware/src/hw/theme_fs.h`, `firmware/src/hw/theme_fs.cpp`
- Modify: `firmware/src/net/net.h` (`EvType::ThemeStored`, `EvType::ThemeUse`), `firmware/src/core/ui.h` (`NetCmd::ThemeCur`, `netCmdKey`), `firmware/src/core/ui.cpp` (testo di `netCmdKey`), `firmware/src/net/net.cpp`
- Modify: `firmware/src/core/theme_runtime.{h,cpp}` (`verifyAt`: CRC a pezzi da 4 096 byte nello `scratch`, non da 256)
- Test: `firmware/test/test_theme_runtime/test_main.cpp` (`test_verify_reads_in_big_chunks`); build della scheda (`pio run -e heltec213`); il resto della logica è già provato in `test_theme_store` (Task B4) e la prova vera è dal vivo (Task B8)

**Interfaces:**
- Consumes: `Storage`, `Installer`, `catalog`, `Offer`, `decodeOffer`, `decodeUse`, `encodeThemes`, `encodeAck`, `downloadUrl`, `acceptOffer`, `acceptUse` (B4 e piano 1a); in `net.cpp` `ws`, `wsConn`, `pcIp`, `pcPort`, `gLink.authed()`, `kNotAuthed`, `post()`, `onText` (lo `switch (cal::msgType(root))`), le dichiarazioni anticipate accanto a `void sendWifiList();`, `handle()`, `request()`, il caso `LinkEvType::Authed` in `NetOut::event` (sta **prima** di `onText`), `suspendNow()` (task di rete, accanto a `dropOffer`).
- Produces:
  - `hw/theme_fs.h` (namespace `themefs`): `bool begin()` (LittleFS già montato da `avfs::begin`: crea `/th`, toglie `/th/tmp` rimasto, crea il lucchetto), `bool mountNoFormat()` (risveglio dell'ora: monta la partizione «spiffs» **senza mai formattare** e senza scrivere niente; false = risveglio pieno), `cal::theme::Storage &storage()`, `void lock()`/`void unlock()` (attorno a ogni modifica dei file dei temi e di `/th/order`: `Installer::finish` nel task di rete, `themeBroken` e `forget` nel loop), `bool loadCur(char *id, size_t cap)`, `void saveCur(const char *id)` (scrive solo se cambia), `void forget()` (Ripristino di fabbrica: tutti i temi, l'ordine e la chiave `cur`);
  - `FsStorage::tmpCommit` e `writeOrder`: `LittleFS.rename` diretto sopra il file esistente (littlefs lo sostituisce in modo atomico), **mai** `remove` prima; se la rinomina fallisse l'installazione si annulla (sicuro), prova dal vivo in B8;
  - eventi `net::EvType::ThemeStored` (`e.id` = tema installato) e `net::EvType::ThemeUse` (`e.id` = tema da attivare, '' = di serie);
  - richiesta `cal::NetCmd::ThemeCur` (`r.id` = tema attivo, '' = di serie): la rete la tiene e manda `themes` subito (se autenticata) e dopo ogni `auth-ok`.

- [ ] **Step 1: `hw/theme_fs.{h,cpp}`**

```cpp
// theme_fs.h — Temi sulla scheda (PROTOCOLLO.md «Temi»): /th/<id>.cthm in LittleFS (la partizione «spiffs» già montata
// da avfs::begin), scrittura in /th/tmp e rinomina, ordine d'installazione in /th/order; tema attivo nella NVS
// «calamaio-th», chiave «cur». LittleFS ha il suo lucchetto (VFS dell'IDF) per ogni chiamata; le modifiche fatte di più
// chiamate (installazione, tema rotto tolto, ordine riscritto) dal task di rete e dal loop passano per lock()/unlock().
#pragma once
#include <stddef.h>

#include "core/theme_runtime.h"

namespace themefs {

bool begin();
// Risveglio dell'ora: monta senza mai formattare (avfs::begin formatta una partizione che non monta: lì
// cancellerebbe temi e avatar) e senza scrivere niente. false = risveglio pieno.
bool mountNoFormat();
cal::theme::Storage &storage();
void lock();
void unlock();
bool loadCur(char *id, size_t cap);
void saveCur(const char *id);
void forget();

}  // namespace themefs
```

```cpp
// theme_fs.cpp
#include "hw/theme_fs.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "core/strbuf.h"
#include "hw/logtee.h"

namespace themefs {

namespace {

const char *const kDir = "/th";
const char *const kTmp = "/th/tmp";
const char *const kOrder = "/th/order";

void pathOf(const char *id, char *out, size_t cap) { snprintf(out, cap, "/th/%s.cthm", id); }

bool readAt(const char *path, uint32_t off, uint8_t *dst, uint32_t n) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  const bool ok = f.seek(off) && f.read(dst, n) == n;
  f.close();
  return ok;
}

class FsStorage : public cal::theme::Storage {
 public:
  int list(char (*ids)[cal::theme::kIdLen], int cap) override {
    File d = LittleFS.open(kDir);
    if (!d || !d.isDirectory()) return 0;
    int n = 0;
    for (File f = d.openNextFile(); f && n < cap; f = d.openNextFile()) {
      const char *name = f.name();
      const char *slash = strrchr(name, '/');
      if (slash) name = slash + 1;
      const size_t len = strlen(name);
      if (len > 5 && len - 5 < size_t(cal::theme::kIdLen) && !strcmp(name + len - 5, ".cthm")) {
        memcpy(ids[n], name, len - 5);
        ids[n][len - 5] = 0;
        n++;
      }
      f.close();
    }
    d.close();
    return n;
  }
  int32_t size(const char *id) override {
    char p[48];
    pathOf(id, p, sizeof p);
    if (!LittleFS.exists(p)) return -1;
    File f = LittleFS.open(p, "r");
    if (!f) return -1;
    const int32_t n = int32_t(f.size());
    f.close();
    return n;
  }
  bool read(const char *id, uint32_t off, uint8_t *dst, uint32_t n) override {
    char p[48];
    pathOf(id, p, sizeof p);
    return readAt(p, off, dst, n);
  }
  void remove(const char *id) override {
    char p[48];
    pathOf(id, p, sizeof p);
    if (LittleFS.exists(p)) LittleFS.remove(p);
  }
  bool tmpBegin() override {
    if (tmp_) tmp_.close();
    tmp_ = LittleFS.open(kTmp, "w");
    return bool(tmp_);
  }
  bool tmpAppend(const uint8_t *p, uint32_t n) override { return tmp_ && tmp_.write(p, n) == n; }
  int32_t tmpSize() override {
    if (tmp_) tmp_.flush();
    if (!LittleFS.exists(kTmp)) return -1;
    File f = LittleFS.open(kTmp, "r");
    const int32_t n = f ? int32_t(f.size()) : -1;
    if (f) f.close();
    return n;
  }
  bool tmpRead(uint32_t off, uint8_t *dst, uint32_t n) override {
    if (tmp_) tmp_.flush();
    return readAt(kTmp, off, dst, n);
  }
  bool tmpCommit(const char *id) override {
    if (tmp_) tmp_.close();
    char p[48];
    pathOf(id, p, sizeof p);
    // rename diretto: littlefs sostituisce il file esistente in modo atomico. Con remove prima, fra le due chiamate il
    // tema attivo non ci sarebbe (corrente che manca lì → «Tema rovinato» e tema perso). Se fallisce: tutto o niente.
    return LittleFS.rename(kTmp, p);
  }
  void tmpDrop() override {
    if (tmp_) tmp_.close();
    if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);
  }
  int readOrder(char *dst, int cap) override {
    if (!LittleFS.exists(kOrder)) return -1;
    File f = LittleFS.open(kOrder, "r");
    if (!f) return -1;
    const int n = int(f.read(reinterpret_cast<uint8_t *>(dst), cap));
    f.close();
    return n;
  }
  bool writeOrder(const char *src, int n) override {
    File f = LittleFS.open("/th/order.tmp", "w");
    if (!f) return false;
    const bool ok = f.write(reinterpret_cast<const uint8_t *>(src), n) == size_t(n);
    f.close();
    if (ok && LittleFS.rename("/th/order.tmp", kOrder)) return true;  // rename diretto, come tmpCommit
    LittleFS.remove("/th/order.tmp");
    return false;
  }
  void eraseAll() override {
    char ids[cal::theme::kMaxThemes + 1][cal::theme::kIdLen];
    const int n = list(ids, cal::theme::kMaxThemes + 1);
    for (int i = 0; i < n; i++) remove(ids[i]);
    tmpDrop();
    if (LittleFS.exists(kOrder)) LittleFS.remove(kOrder);
  }

 private:
  File tmp_;
};

FsStorage gSt;
Preferences gPrefs;
bool gOpen = false;
SemaphoreHandle_t gLock = nullptr;  // creato in begin(), nel setup, prima che parta il task di rete
bool prefs() {
  if (!gOpen) gOpen = gPrefs.begin("calamaio-th", false);
  return gOpen;
}

}  // namespace

bool begin() {
  if (!gLock) gLock = xSemaphoreCreateMutex();
  if (!LittleFS.exists(kDir) && !LittleFS.mkdir(kDir)) {
    LogSerial.println("[temi] /th non creata: solo il tema di serie");
    return false;
  }
  if (LittleFS.exists(kTmp)) LittleFS.remove(kTmp);  // download interrotto da un riavvio
  return true;
}

bool mountNoFormat() { return LittleFS.begin(false, "/littlefs", 4, "spiffs"); }  // stessi argomenti di avfs::begin, senza formattare

cal::theme::Storage &storage() { return gSt; }

void lock() {
  if (gLock) xSemaphoreTake(gLock, portMAX_DELAY);
}
void unlock() {
  if (gLock) xSemaphoreGive(gLock);
}

bool loadCur(char *id, size_t cap) {
  if (!cap) return false;
  id[0] = 0;
  if (!prefs() || !gPrefs.isKey("cur")) return false;
  gPrefs.getString("cur", id, cap);
  return id[0] != 0;
}

void saveCur(const char *id) {
  char old[cal::theme::kIdLen];
  loadCur(old, sizeof old);
  if (!strcmp(old, id ? id : "") || !prefs()) return;  // la NVS si scrive solo se cambia
  gPrefs.putString("cur", id ? id : "");
}

void forget() {
  lock();
  gSt.eraseAll();
  unlock();
  if (prefs()) gPrefs.remove("cur");
}

}  // namespace themefs
```

- [ ] **Step 2: Richiesta ed eventi nuovi**

`core/ui.h`: in fondo a `enum class NetCmd` `ThemeCur,  // 0.13.0: tema attivo cambiato (id = tema, '' = di serie): la rete manda themes`; `ui.cpp`: aggiungere `"themecur"` in fondo alla tabella di `netCmdKey` (stesso ordine dell'enum). `net/net.h`: in fondo a `enum class EvType` `ThemeStored,  // 0.13.0: tema installato (id)` e `ThemeUse,  // 0.13.0: theme-use da una connessione autenticata (id, '' = di serie)`.

- [ ] **Step 3: `net.cpp`**

In testa: `#include <HTTPClient.h>` (oggi manca in `net.cpp`), `#include "core/theme_proto.h"`, `#include "core/theme_store.h"`, `#include "hw/theme_fs.h"`. Accanto alle dichiarazioni anticipate (`void sendSettings();`, `void sendWifiList();`, ~riga 192): `void sendThemes();` (la usa il caso `LinkEvType::Authed` di `NetOut::event`, che sta prima di `onText`). Variabili del task (vicino a quelle dell'OTA):

```cpp
// 0.13.0, temi delle schermate (PROTOCOLLO.md «Temi»): l'offerta verificata aspetta il giro del task (themeLoop),
// così il download non avviene dentro la callback del WebSocket.
char gThemeCur[cal::theme::kIdLen] = "";
cal::theme::Offer gThemeOffer;
bool gThemePending = false;
cal::theme::Installer gThemeInst;
```

Funzioni (prima di `onText`):

```cpp
void sendThemes() {
  if (!wsConn || !gLink.authed()) return;
  cal::theme::CatalogEntry e[cal::theme::kMaxThemes];
  const int n = cal::theme::catalog(themefs::storage(), e, cal::theme::kMaxThemes);
  char buf[640];
  const size_t len = cal::theme::encodeThemes(e, n, gThemeCur, buf, sizeof buf);
  if (len) ws.sendTXT(buf, len);
  LogSerial.printf("[temi] themes: %d sulla scheda, attivo «%s»\n", n, gThemeCur[0] ? gThemeCur : "Calamaio");
}

void sendThemeAck(const char *id, bool ok, const char *why) {
  char buf[200];
  const size_t n = cal::theme::encodeAck(id, ok, why, buf, sizeof buf);
  if (n && wsConn) ws.sendTXT(buf, n);
  LogSerial.printf("[temi] theme-ack %s: %s%s%s\n", id, ok ? "ok" : "rifiutato", ok ? "" : " · ", ok ? "" : why);
}

// Download e installazione (nel task di rete, fuori dalla callback). HTTP/1.0 come l'OTA, al massimo 15 s.
void themeLoop() {
  if (!gThemePending) return;
  gThemePending = false;
  const cal::theme::Offer &o = gThemeOffer;
  char url[160], why[96] = "";
  bool ok = cal::theme::downloadUrl(pcIp, pcPort, o.token, url, sizeof url) &&
            gThemeInst.begin(themefs::storage(), o.id, o.crc, o.size, why, sizeof why);
  if (!why[0] && !ok) cal::scopy(why, "indirizzo del download non valido");
  WiFiClient client;
  HTTPClient http;
  if (ok) {
    http.useHTTP10(true);
    http.setReuse(false);
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    ok = http.begin(client, url);
    const int code = ok ? http.GET() : -1;
    if (code != 200) {
      ok = false;
      snprintf(why, sizeof why, "download non riuscito (HTTP %d)", code);
    }
  }
  uint8_t chunk[512];
  const uint32_t t0 = millis();
  uint32_t got = 0;
  WiFiClient *s = ok ? http.getStreamPtr() : nullptr;
  while (ok && got < o.size) {
    if (millis() - t0 > 15000) { ok = false; cal::scopy(why, "download troppo lento"); break; }
    const int av = s->available();
    if (av <= 0) {
      if (!s->connected()) { ok = false; cal::scopy(why, "download interrotto"); break; }
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    const int n = s->read(chunk, av < int(sizeof chunk) ? av : int(sizeof chunk));
    if (n <= 0) continue;
    ok = gThemeInst.write(chunk, size_t(n), why, sizeof why);
    got += uint32_t(n);
  }
  http.end();
  if (ok) {
    uint8_t *scratch = static_cast<uint8_t *>(malloc(cal::theme::kMaxBlock));  // solo durante il controllo
    themefs::lock();  // il loop (themeBroken) può togliere un tema e riscrivere /th/order nello stesso momento
    ok = scratch && gThemeInst.finish(gThemeCur, scratch, why, sizeof why);
    themefs::unlock();
    if (!scratch) cal::scopy(why, "memoria insufficiente");
    free(scratch);
  }
  if (!ok) gThemeInst.abort();
  sendThemeAck(o.id, ok, why);
  if (ok) {
    LogSerial.printf("[temi] %s installato (%lu byte, crc %08lx) in %lu ms\n", o.id, (unsigned long)o.size, (unsigned long)o.crc,
                     (unsigned long)(millis() - t0));
    post(EvType::ThemeStored, "", "", 0, 0, o.id);
    sendThemes();
  }
}
```

In `onText`, nello `switch (cal::msgType(root))` (accanto a `MsgType::Workday`):

```cpp
    case cal::MsgType::ThemeOffer: {  // 0.13.0 (PROTOCOLLO.md «Temi»): solo autenticato (acceptOffer, provata in B4)
      cal::theme::Offer o;
      if (!cal::theme::decodeOffer(root, o)) { LogSerial.println("[temi] theme-offer non valido: ignorato"); break; }
      const cal::theme::OfferVerdict v = cal::theme::acceptOffer(gLink.authed(), gThemePending || gThemeInst.busy());
      if (v == cal::theme::OfferVerdict::NotAuthed) { sendThemeAck(o.id, false, kNotAuthed); break; }
      if (v == cal::theme::OfferVerdict::Busy) { sendThemeAck(o.id, false, "un altro tema in arrivo, riprova"); break; }
      gThemeOffer = o;
      gThemePending = true;
      LogSerial.printf("[temi] theme-offer %s: %lu byte, scarico al prossimo giro\n", o.id, (unsigned long)o.size);
      break;
    }
    case cal::MsgType::ThemeUse: {
      char id[cal::theme::kIdLen];
      if (!cal::theme::acceptUse(gLink.authed()) || !cal::theme::decodeUse(root, id, sizeof id)) { LogSerial.println("[temi] theme-use ignorato"); break; }
      post(EvType::ThemeUse, "", "", 0, 0, id);
      break;
    }
```

(`kNotAuthed` è il motivo già usato per `set` e `workday`: «display non abbinato: cambia le impostazioni dal menu del display», `net.cpp:146`.) Nel caso `LinkEvType::Authed` di `NetOut::event`, dopo `sendWifiList();`: `sendThemes();  // 0.13.0: temi sulla scheda, solo autenticato`. In `handle()`:

```cpp
  if (r.cmd == cal::NetCmd::ThemeCur) {
    cal::scopy(gThemeCur, r.id);
    sendThemes();
    return;
  }
```

In `request()`, nel ramo della modalità gioco: `else if (r.cmd == cal::NetCmd::ThemeCur) cal::scopy(gThemeCur, r.id);` (l'ultimo tema attivo si dice dopo il prossimo `auth-ok`). Nel ciclo del task, dopo `otaLoop();`: `themeLoop();`. In `suspendNow()` (spegnimento per la partita, nel task di rete: è lui che chiude il WebSocket con `wsDrop()`), subito dopo il controllo `if (gUpd.active()) { … return; }` (una partita rifiutata lascia stare il tema): `if (gThemeInst.busy()) gThemeInst.abort(); gThemePending = false;  // 0.13.0: offerta del tema scartata, il servizio ripropone al ritorno`. Non in `net::suspend()`: gira nel loop, sull'altro core, e farebbe una corsa con `themeLoop`.

Nota: `pcIp` e `pcPort` sono l'indirizzo del WebSocket collegato (lo stesso che usa `ws.begin`): il download va al PC che ha mandato l'offerta. `HTTPClient` e `WiFiClient` sono già inclusi da `hw/ota_hw.cpp`, non da `net.cpp` (vedi l'include in testa). Il download blocca il task di rete fino a 5 s + 15 s (niente `ws.loop()`, quindi niente risposta ai ping, e un `net::suspend(8000)` in quel momento fallisce): su LAN 64 KB arrivano in meno di 1 s; il tempo vero si misura in B8 (prova 1) e, se supera 1 s, il download va a pezzi per giro come `otaLoop` (annotare in DECISIONI).

- [ ] **Step 4: `verifyAt`: CRC a pezzi da 4 096 byte**

Con `theme_fs` ogni `Storage::read` apre, cerca e chiude il file in LittleFS. Il `verifyAt` del piano 1a calcola il CRC
in pezzi da 256 byte (`chunk[256]`, `theme_runtime.cpp`): un tema da 64 KB costa circa 256 aperture. Lo `scratch` da
`kMaxBlock` byte c'è già (tutti i chiamanti lo passano, e serve solo dopo, per indice e blocchi): il CRC si calcola lì,
16 volte meno letture. Test in fondo a `test_theme_runtime/test_main.cpp` (e `RUN_TEST`):

```cpp
// verifyAt legge il CRC a pezzi da kMaxBlock nello scratch: con theme_fs ogni read è un'apertura del file.
static void test_verify_reads_in_big_chunks() {
  reset();
  const int32_t len = gSt.size("lista");
  Header h;
  const int before = gSt.reads;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "lista", gScratch, &h)));
  const int crcReads = int((uint32_t(len) - 12 + kMaxBlock - 1) / kMaxBlock);
  // intestazione + CRC + indice + un blocco per schermata
  TEST_ASSERT_EQUAL(1 + crcReads + 1 + h.nScreens, gSt.reads - before);
}
```

In `verifyAt` togliere `uint8_t chunk[256];` e leggere in `scratch` a pezzi da `kMaxBlock` (commento dell'intestazione:
«CRC a pezzi da kMaxBlock nello scratch»). Run: `cd firmware && pio test -e native -f test_theme_runtime && pio test -e native_asan` → PASS.
Se per qualche motivo restasse a 256 byte, scriverlo in DECISIONI (costo misurato del controllo all'avvio, Task B8).

- [ ] **Step 5: Build**

Run: `cd firmware && pio run -e heltec213 2>&1 | grep -E "RAM:|Flash:|SUCCESS|error" ; pio test -e native`
Expected: SUCCESS e test verdi. La RAM statica cresce di meno di 1 KB (variabili dei temi nel task di rete; l'`Offer` e l'`Installer` sono piccoli; il buffer da 4 096 byte arriva col `Runtime` nel Task B7).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/hw/theme_fs.h firmware/src/hw/theme_fs.cpp firmware/src/net/net.h firmware/src/net/net.cpp firmware/src/core/ui.h firmware/src/core/ui.cpp firmware/src/core/theme_runtime.h firmware/src/core/theme_runtime.cpp firmware/test/test_theme_runtime
git commit -m "Temi: LittleFS dei temi e download col token nel task di rete"
```

---
### Task B6: Firmware — Menu › Display › Tema e allineamento del simulatore

**Files:**
- Modify: `firmware/src/core/model.h`, `firmware/src/core/model.cpp` (`Themes` nel modello, `menuKey`), `firmware/src/core/menu.h` (`ActId::Theme`), `firmware/src/core/menu.cpp` (voce e lista), `firmware/src/core/ui.h`, `firmware/src/core/ui.cpp` (`themePick`)
- Modify (allineamento, annotato in DECISIONI nel Task B8): `reference/calamaio-os-browser/assets/calamaio-test.js`, `reference/calamaio-os-browser/LEGGIMI.txt`
- Modify (rigenerati): `firmware/test/test_core_golden/golden_core.h`, `tools/sim-capture/scenarios.mjs` (scenari `menu-tema`), `tools/sim-capture/out/heltec213/*`
- Test: `firmware/test/test_theme_menu/test_main.cpp`, `test_core_golden`, `test_ui`

**Interfaces:**
- Consumes: `Builder` di `menu.cpp`, `Ui::activate`, `setAction`, `redraw`.
- Produces:
  - in `core/model.h`: `const int kThemeCap = 8; struct ThemeItem { char id[kAvatarRefLen]; char name[kAvatarRefLen]; }; struct Themes { ThemeItem items[kThemeCap]; int n; int cur; };` (`cur` = indice in `items`, −1 = Calamaio), `Model::themes`; `newModel` lo azzera con `cur = -1`;
  - `MenuId::Theme` (in fondo, prima di `Count`; `menuKey` = `"tema"`), `ActId::Theme` (in fondo; `actKey` = `"theme"`; `MenuItem::net` = indice del tema, −1 = Calamaio);
  - `Ui::themePick(int k, Ms now)` (privata): `m.themes.cur = k`, azione «Tema: <nome>», ridisegno parziale. Il loop (Task B7) si accorge del cambio e attiva il tema.

- [ ] **Step 1: Scrivere il test che fallisce**

`firmware/test/test_theme_menu/test_main.cpp`:

```cpp
// Menu › Display › Tema (0.13.0): voce in fondo a Display, lista dei temi con ✓ sull'attivo, 2× sceglie.
#include <string.h>
#include <unity.h>

#include "core/gesture.h"
#include "core/menu.h"
#include "core/strbuf.h"
#include "core/ui.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static const Ms kNow = 1791707400000LL;
static const WallClock kWc = {kNow};

static void test_display_has_theme_last() {
  static Ui ui(kHeltec213);
  ui.begin(kNow, kWc);
  static MenuList L;
  menuList(MenuId::Display, kHeltec213, ui.model(), kNow, kWc, L);
  TEST_ASSERT_FALSE(L.truncated);
  const MenuItem &it = L.items[L.n - 1];
  TEST_ASSERT_EQUAL_STRING("Tema", it.label);
  TEST_ASSERT_EQUAL_STRING("Calamaio", it.val);
  TEST_ASSERT_EQUAL(int(ItemKind::Go), int(it.kind));
  TEST_ASSERT_EQUAL(int(MenuId::Theme), int(it.go));
  TEST_ASSERT_EQUAL_STRING("tema", menuKey(MenuId::Theme));
}

static void test_theme_list_and_pick() {
  static Ui ui(kHeltec213);
  ui.begin(kNow, kWc);
  Model &m = ui.model();
  m.themes.n = 2;
  scopy(m.themes.items[0].id, "prova");
  scopy(m.themes.items[0].name, "Mascotte grande");
  scopy(m.themes.items[1].id, "lista");
  scopy(m.themes.items[1].name, "Righe a pagine");
  m.themes.cur = 1;
  static MenuList L;
  menuList(MenuId::Theme, kHeltec213, m, kNow, kWc, L);
  TEST_ASSERT_EQUAL_STRING("Tema", L.title);
  TEST_ASSERT_EQUAL(3, L.n);
  TEST_ASSERT_EQUAL_STRING("Calamaio", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("", L.items[0].val);
  TEST_ASSERT_EQUAL_STRING("\xe2\x9c\x93", L.items[2].val);  // ✓ sull'attivo
  menuList(MenuId::Display, kHeltec213, m, kNow, kWc, L);
  TEST_ASSERT_EQUAL_STRING("Righe a pagine", L.items[L.n - 1].val);
  // dalla lista: seconda voce («Mascotte grande»), 2× sceglie
  resetStack(m, makeScreen(ScreenId::Home));
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = MenuId::Theme;
  e.sel = 1;
  pushScreen(m, e);
  ui.gesture(Gesture::Press2, kNow, kWc);
  TEST_ASSERT_EQUAL(0, m.themes.cur);
  TEST_ASSERT_EQUAL_STRING("Tema: Mascotte grande", ui.lastAction());
  top(m).sel = 0;  // «Calamaio»
  ui.gesture(Gesture::Press2, kNow, kWc);
  TEST_ASSERT_EQUAL(-1, m.themes.cur);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_display_has_theme_last);
  RUN_TEST(test_theme_list_and_pick);
  return UNITY_END();
}
```

(Se `Ui::gesture` vuole un altro nome per «2 pressioni», usare quello di `core/gesture.h`, `Gesture::Press2`, già usato da `test_core_golden`.)

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd firmware && pio test -e native -f test_theme_menu`
Expected: FAIL (`MenuId::Theme` non esiste).

- [ ] **Step 3: Modello, menu, Ui**

`model.h`, dopo `struct Pet`:

```cpp
// ---------- 0.13.0: temi delle schermate (core/theme_runtime, docs/TEMI.md) ----------
// Catalogo dei temi sulla scheda per Menu › Display › Tema; lo aggiorna il loop dall'archivio (hw/theme_fs).
const int kThemeCap = 8;
struct ThemeItem { char id[kAvatarRefLen]; char name[kAvatarRefLen]; };
struct Themes {
  ThemeItem items[kThemeCap];
  int n;
  int cur;  // indice in items, -1 = Calamaio (di serie)
};
```

e in `Model` dopo `GbView gb;`: `Themes themes;  // 0.13.0`. `MenuId`: `…, GbPause, Theme, Count` e in `menuKey` `"tema"` in fondo. In `newModel` (`model.cpp`): `memset(&m.themes, 0, sizeof m.themes); m.themes.cur = -1;`.

`menu.h`: in fondo a `ActId` `Theme,  // 0.13.0: Display › Tema › un tema (net = indice, -1 = Calamaio)`; `actKey` (`menu.cpp`): `"theme"` in fondo alla tabella.

`menu.cpp`, ultima voce di `MenuId::Display` (dopo «Gioco della mascotte»):

```cpp
    B.go("Tema", m.themes.cur >= 0 && m.themes.cur < m.themes.n ? m.themes.items[m.themes.cur].name : "Calamaio", MenuId::Theme,
         "Tema delle schermate. Calamaio è quello di serie; gli altri arrivano dalla webapp.");
    return;
```

e la lista nuova (dopo il blocco di `MenuId::PetAcc`):

```cpp
  // 0.13.0: Display › Tema. 2× attiva subito (il loop salva la scelta e la dice al PC).
  if (id == MenuId::Theme) {
    out.title = "Tema";
    MenuItem &c = B.act("Calamaio", m.themes.cur < 0 ? "\xe2\x9c\x93" : "", ActId::Theme, "Il tema di serie: sempre presente.");
    c.net = -1;
    for (int k = 0; k < m.themes.n && k < kThemeCap; k++) {
      MenuItem &it = B.act(m.themes.items[k].name, m.themes.cur == k ? "\xe2\x9c\x93" : "", ActId::Theme,
                           "Tema dalla webapp. Permessi, abbinamento e schermate di sistema restano quelle di serie.");
      it.net = k;
    }
    return;
  }
```

`ui.h`: tra i privati accanto a `petWear` `void themePick(int k, Ms now);  // Display › Tema › 2×: k = indice, -1 = Calamaio`. `ui.cpp`, in `Ui::activate` accanto a `ActId::Wear`: `if (it.kind == ItemKind::Act && it.act == ActId::Theme) { themePick(it.net, now); return; }` e:

```cpp
void Ui::themePick(int k, Ms now) {
  (void)now;
  Themes &t = m_.themes;
  t.cur = k >= 0 && k < t.n ? k : -1;
  char msg[64];
  Sb(msg, sizeof msg).s("Tema: ").s(t.cur >= 0 ? t.items[t.cur].name : "Calamaio");
  redraw(RefreshKind::Partial);
  setAction(msg);
}
```

- [ ] **Step 4: Eseguire il test nuovo**

Run: `cd firmware && pio test -e native -f test_theme_menu`
Expected: PASS. Poi `pio test -e native -f test_core_golden` → FAIL atteso (il Display del firmware ha una voce in più del simulatore): si allinea il simulatore.

- [ ] **Step 5: Allineamento del simulatore**

`reference/calamaio-os-browser/assets/calamaio-test.js`, voce in fondo alla lista `display` (dopo «Gioco della mascotte», con la virgola):

```js
          { label: 'Tema', val: 'Calamaio', go: 'tema', help: 'Tema delle schermate. Calamaio è quello di serie; gli altri arrivano dalla webapp.' }
```

lista nuova, subito dopo il blocco `if (id === 'petacc') { … }`:

```js
      // Temi delle schermate (firmware 0.13.0): nel simulatore c'è solo quello di serie.
      if (id === 'tema') return { title: 'Tema', items: [{ label: 'Calamaio', val: '✓', act: 'theme', help: 'Il tema di serie: sempre presente.' }] };
```

e in `doAct`, accanto a `night`: `else if (id === 'theme') { msg = 'Tema: Calamaio'; }`. In `LEGGIMI.txt`, dopo la riga del gioco della mascotte: «- Temi delle schermate (firmware 0.13.0): Menu > Display > Tema (ultima voce). Nel simulatore c'è solo il tema di serie «Calamaio»; gli altri temi si fanno con l'editor della webapp e si vedono solo sulla scheda.»

- [ ] **Step 6: Rigenerare golden e catture**

Run: `node tools/golden/core-golden.js && cd firmware && pio test -e native -f test_core_golden`
Expected: PASS (le voci di Display e il `go:tema` coincidono).

In `tools/sim-capture/scenarios.mjs`, dopo `gioco-acceso-menu`:

```js
  { name: 'menu-tema', steps: [BOOT, ...TO_DISPLAY, ...times(9, '1x')], note: 'Menu › Display, ultima voce: Tema «Calamaio» (0.13.0).' },
  { name: 'menu-tema-lista', steps: [BOOT, ...TO_DISPLAY, ...times(9, '1x'), '2x'], note: 'Display › Tema: solo «Calamaio» con ✓.' },
```

Run (coordinatore: ~25 minuti, Playwright con Chromium 1248 già in cache): `cd tools/sim-capture && node capture.mjs && node verify-determinism.mjs`, poi `cd ../../firmware && pio test -e native -f test_ui`.
Expected: catture nuove per gli scenari del Display (barra di scorrimento con una voce in più) e per i due `menu-tema`; `test_ui` PASS (testi uguali e pixel sotto lo 0,3%). Prima di lanciare la cattura completa si può provare con `node capture.mjs --only menu-display,menu-tema,menu-tema-lista --out /tmp/prova`.

- [ ] **Step 7: Tutti i test**

Run: `cd firmware && pio test -e native`
Expected: tutto verde.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/core firmware/test/test_theme_menu firmware/test/test_core_golden reference/calamaio-os-browser/assets/calamaio-test.js reference/calamaio-os-browser/LEGGIMI.txt tools/sim-capture/scenarios.mjs tools/sim-capture/out
git commit -m "Temi: Menu › Display › Tema, allineamento del simulatore, golden e catture rigenerati"
```

---
### Task B7: Firmware — tema attivo nel loop, uscita di emergenza, Riposo in sonno profondo, 0.13.0-ed

**Files:**
- Modify: `firmware/src/core/theme_runtime.h`, `firmware/src/core/theme_runtime.cpp` (`themeBootPlan`)
- Modify: `firmware/src/core/wake.h` (tema del Riposo in `RestKeep`, `kRestKeepMagic`: sta in `wake.h`, `wake.cpp` non cambia)
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/src/hw/gb_run.cpp` (il buffer del tema come RAM della cartuccia, dopo l'anello del registro)
- Modify: `firmware/src/core/model.h` (`CALAMAIO_FW_VERSION "0.13.0-ed"`), `firmware/test/test_net/test_main.cpp` (assert della versione)
- Test: `firmware/test/test_theme_store/test_main.cpp` (`test_boot_plan_*`, `test_activate_known`), `firmware/test/test_clock/test_main.cpp` (`test_rest_keep` con il tema)

**Interfaces:**
- Consumes: `Runtime` (`lend`/`giveBack`/`lent` da B4), `SafeBoot`, `catalog`, `orderDrop`, `canUse`, `verifyStored` (piano 1a, B4); `themefs::begin`, `themefs::mountNoFormat`, `themefs::storage`, `themefs::lock`/`unlock`, `themefs::loadCur`/`saveCur`/`forget` (B5); `net::EvType::ThemeStored`, `net::EvType::ThemeUse`, `NetCmd::ThemeCur` (B5); `Model::themes` (B6); `ui::setThemeRuntime`, `ui::themeRuntime`, `ui::lastThemeDraw` (`ThemeDrawInfo{drawn, nextChange, …, screen}`, piano 1a); in `hw/gb_run.cpp` `gLent`, `gLentBytes`, `lendRing`, `returnRing`, `lentBank`, `needBytes(info, lentBytes)`, `cal::gb::kRamBank`.
- Produces:
  - `struct BootPlan { bool use; bool broken; char id[kIdLen]; }`, `BootPlan themeBootPlan(Storage &st, const char *nvsCur, uint8_t *scratch)` (in `core/theme_runtime`): NVS vuota → di serie; file presente e controllato → `use`; mancante o rovinato → `broken` (di serie, toast, file tolto, NVS vuota);
  - `bool Runtime::activateKnown(const char *id, uint32_t crc)` (risveglio dell'ora: intestazione e indice, CRC dell'intestazione confrontato con quello di `RestKeep`, senza rileggere tutto il file);
  - `RestKeep::themeId[25]`, `RestKeep::themeCrc` (tema che ha disegnato davvero il Riposo: `lastThemeDraw().drawn && screen == Sleep`, altrimenti '' e 0; `kRestKeepMagic` in `wake.h` cambia, così un `RestKeep` vecchio non vale);
  - in `main.cpp`: `themesBegin(Ms now)`, `themesToModel()`, `themeBroken(const char *id, Ms now, Ms toastMs = kToastMs)` (ricontrolla con `verifyStored` prima di togliere), `themeCheckBroken(Ms now)` (`takeBroken` → `themeBroken`), `themeSync(Ms now)` e `themeEvent(const net::Event &e, Ms now)` (niente mentre `themeRt.lent()`; all'uscita dalla partita riattivano il tema).

- [ ] **Step 1: Test di `themeBootPlan` e di `RestKeep`**

In fondo a `firmware/test/test_theme_store/test_main.cpp` (e i `RUN_TEST` in `main`):

```cpp
static void test_boot_plan_ok_and_empty() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  BootPlan p = themeBootPlan(gSt, "", gScratch);
  TEST_ASSERT_FALSE(p.use);
  TEST_ASSERT_FALSE(p.broken);
  p = themeBootPlan(gSt, "prova", gScratch);
  TEST_ASSERT_TRUE(p.use);
  TEST_ASSERT_EQUAL_STRING("prova", p.id);
}

static void test_boot_plan_broken_active() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  gSt.raw("prova")[90] ^= 0x10;  // flash che cede
  BootPlan p = themeBootPlan(gSt, "prova", gScratch);
  TEST_ASSERT_FALSE(p.use);
  TEST_ASSERT_TRUE(p.broken);
  TEST_ASSERT_EQUAL_STRING("prova", p.id);
  p = themeBootPlan(gSt, "manca", gScratch);  // tolto mentre la NVS lo ricordava
  TEST_ASSERT_TRUE(p.broken);
}
```

```cpp
// Risveglio dell'ora (ogni minuto in Riposo): niente CRC completo, solo intestazione e indice contro RestKeep.
static void test_activate_known() {
  gSt.eraseAll();
  std::vector<uint8_t> f = load("prova.cthm");
  TEST_ASSERT_TRUE(install("prova", f));
  Runtime rt;
  rt.begin(&gSt);
  const int before = gSt.reads;
  TEST_ASSERT_TRUE(rt.activateKnown("prova", crcOf(f)));
  TEST_ASSERT_EQUAL(2, gSt.reads - before);  // intestazione + indice
  TEST_ASSERT_EQUAL_UINT32(crcOf(f), rt.activeCrc());
  TEST_ASSERT_FALSE(rt.activateKnown("prova", crcOf(f) ^ 1));  // tema cambiato: risveglio pieno
  TEST_ASSERT_FALSE(rt.activateKnown("manca", crcOf(f)));
  char bad[theme::kIdLen];
  TEST_ASSERT_FALSE(rt.takeBroken(bad, sizeof bad));  // non è un tema rotto: decide il risveglio pieno
  // un blocco rovinato passa activateKnown, ma block() lo ricontrolla (checkBlock) e il tema risulta rotto
  TEST_ASSERT_TRUE(rt.activateKnown("prova", crcOf(f)));
  IndexEntry idx[kMaxScreens];
  Header h;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(parseHeader(gSt.raw("prova"), kHeaderLen, h)));
  TEST_ASSERT_EQUAL(int(Error::Ok), int(parseIndex(gSt.raw("prova") + kHeaderLen, 8u * h.nScreens, h, idx, kMaxScreens)));
  gSt.raw("prova")[idx[0].off] = 0xFF;  // primo byte del primo blocco
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_FALSE(rt.block(idx[0].screen, b, n));
  TEST_ASSERT_TRUE(rt.takeBroken(bad, sizeof bad));
}
```

(Adattare l'offset del byte rovinato se `0xFF` in testa al blocco non basta a far fallire `checkBlock`: il test deve
provare che il controllo del blocco resta.)

In `test_clock/test_main.cpp`, dentro `test_rest_keep`: scrivere `strcpy(k.themeId, "prova"); k.themeCrc = 0xAABBCCDD;`, `restSeal(k)`, controllare `restValid(k)` e che cambiare `themeCrc` senza `restSeal` lo renda non valido.

Run: `cd firmware && pio test -e native -f test_theme_store -f test_clock` → FAIL (manca `themeBootPlan`, `activateKnown`, `themeId`).

- [ ] **Step 2: `themeBootPlan` e `RestKeep`**

In `core/theme_runtime.h`:

```cpp
// Avvio (0.13.0): il tema della NVS si usa solo se il file c'è ed è integro; altrimenti di serie e broken (il loop
// mostra «Tema rovinato: uso quello di serie», toglie il file e svuota la NVS).
struct BootPlan { bool use; bool broken; char id[kIdLen]; };
BootPlan themeBootPlan(Storage &st, const char *nvsCur, uint8_t *scratch);
```

In `theme_runtime.cpp`:

```cpp
BootPlan themeBootPlan(Storage &st, const char *nvsCur, uint8_t *scratch) {
  BootPlan p;
  memset(&p, 0, sizeof p);
  if (!nvsCur || !nvsCur[0]) return p;
  strncpy(p.id, nvsCur, kIdLen - 1);
  Header h;
  if (verifyStored(st, p.id, scratch, &h) == Error::Ok && !strcmp(h.id, p.id)) p.use = true;
  else p.broken = true;
  return p;
}
```

`Runtime::activateKnown` (`core/theme_runtime.h`, accanto ad `activate`):

```cpp
  // Risveglio dell'ora (0.13.0): il tema era già stato controllato tutto quando si è disegnato il Riposo. Si leggono
  // solo intestazione e indice e si confronta il CRC dell'intestazione con quello salvato in RestKeep; ogni blocco
  // passa comunque checkBlock in block() e nel disegno. false (diverso, mancante, illeggibile, in prestito): niente
  // tema rotto, il chiamante fa il risveglio pieno e lì activate() controlla tutto.
  bool activateKnown(const char *id, uint32_t crc);
```

In `theme_runtime.cpp`: `if (lent_ || !st_ || !id || !id[0] || strlen(id) >= kIdLen) return false;`, poi `size`,
intestazione (`parseHeader`), `h.length == size`, `h.crc == crc`, `!strcmp(h.id, id)`, indice (`parseIndex` in
`buf_`); se tutto va, `id_`, `h_`, `idx_` come `activate` e `loaded_ = Screen::None`; altrimenti `id_[0] = 0` senza
`broke()`.

In `core/wake.h`, in `RestKeep` prima di `uint8_t img[…]`: `char themeId[25];  // 0.13.0: tema che ha disegnato il Riposo ('' = di serie)` · `uint8_t pad1[3];` · `uint32_t themeCrc;`; sempre in `core/wake.h` cambiare il valore di `kRestKeepMagic` (oggi `0xCA1A0C11u`: per esempio `0xCA1A0C12u`, con un commento «0.13.0: tema del Riposo»; `restSeal`/`restValid` in `wake.cpp` la usano già). Controllare che `RestKeep` stia ancora nella memoria RTC: la build stampa l'uso di `.rtc.data`/`.rtc_noinit`; 32 byte in più.

Run: `cd firmware && pio test -e native -f test_theme_store -f test_clock` → PASS.

- [ ] **Step 3: `main.cpp` — avvio e catalogo**

In testa: `#include "core/theme_runtime.h"`, `#include "core/theme_store.h"`, `#include "hw/theme_fs.h"`, `#include "ui/theme_draw.h"`. Variabili globali:

```cpp
// ---------- 0.13.0: temi delle schermate (docs/TEMI.md) ----------
static cal::theme::Runtime themeRt;   // buffer statico da 4 096 byte del blocco della schermata corrente
static cal::theme::SafeBoot safeBoot; // PRG tenuto 1,5 s durante l'avvio: tema di serie per questo avvio
static bool themeFs = false;
static Ms themeWakeAt = 0;            // ridisegno a tempo chiesto dal tema (orologio coi secondi, pagine della lista)
```

Funzioni (dopo `avatarsBegin`):

```cpp
// Catalogo dell'archivio → Menu › Display › Tema; cur = il tema attivo del runtime.
static void themesToModel() {
  Themes &t = gUi.model().themes;
  memset(&t, 0, sizeof t);
  t.cur = -1;
  if (!themeFs) return;
  cal::theme::CatalogEntry e[cal::theme::kMaxThemes];
  t.n = cal::theme::catalog(themefs::storage(), e, cal::theme::kMaxThemes);
  for (int k = 0; k < t.n; k++) {
    scopy(t.items[k].id, e[k].id);
    scopy(t.items[k].name, e[k].name);
    if (!themeRt.suspended() && !strcmp(e[k].id, themeRt.active())) t.cur = k;  // in emergenza il menu mostra Calamaio
  }
}

static void themeReport() {
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = NetCmd::ThemeCur;
  scopy(r.id, themeRt.active());
  net::request(r);
}

// Un tema si è rivelato rovinato (all'avvio, all'attivazione, al caricamento di un blocco). Mai cancellare un tema
// buono: prima si ricontrolla il file com'è adesso (un errore da indice vecchio, per un file appena sostituito dal task
// di rete o un ThemeStored scartato in partita, non è un tema rotto). Integro → si riattiva; rovinato → di serie, toast,
// file tolto. Il lucchetto copre controllo e cancellazione: il task di rete non installa in mezzo.
static void themeBroken(const char *id, Ms now, Ms toastMs = kToastMs) {
  char bad[cal::theme::kIdLen];
  scopy(bad, id);  // id può essere themeRt.active(), che activate() cambia
  uint8_t *scratch = static_cast<uint8_t *>(malloc(cal::theme::kMaxBlock));
  if (!scratch) {  // non si può ricontrollare: di serie per adesso, il file resta (si riprova al prossimo avvio)
    LogSerial.printf("[temi] %s non attivato e memoria insufficiente per ricontrollarlo: di serie, file lasciato\n", bad);
    themesToModel();
    return;
  }
  themefs::lock();
  cal::theme::Header h;
  const bool good = cal::theme::verifyStored(themefs::storage(), bad, scratch, &h) == cal::theme::Error::Ok && !strcmp(h.id, bad);
  if (!good) {
    themefs::storage().remove(bad);
    cal::theme::orderDrop(themefs::storage(), bad);
  }
  themefs::unlock();
  free(scratch);
  if (good) {
    char again[cal::theme::kIdLen];
    const bool ok = themeRt.activate(bad);
    if (!ok) themeRt.takeBroken(again, sizeof again);  // si è rotto fra il controllo e l'attivazione: al prossimo avvio
    LogSerial.printf("[temi] %s: il file è integro (indice vecchio), %s\n", bad, ok ? "riattivato" : "di serie per adesso");
    themesToModel();
    pending = mergeRefresh(pending, RefreshKind::Quick);
    return;
  }
  LogSerial.printf("[temi] %s rovinato: tolto, si usa il tema di serie\n", bad);
  themefs::saveCur("");
  toast(gUi.model(), "Tema rovinato: uso quello di serie", now, toastMs);
  themesToModel();
  themeReport();
  pending = mergeRefresh(pending, RefreshKind::Quick);
}

// Il Runtime ha segnato un tema rotto (activate() o block() falso): si decide in themeBroken.
static void themeCheckBroken(Ms now) {
  char bad[cal::theme::kIdLen];
  if (themeRt.takeBroken(bad, sizeof bad)) themeBroken(bad, now);
}

static void themesBegin(Ms now) {
  themeFs = avFs && themefs::begin();
  if (!themeFs) return;
  themeRt.begin(&themefs::storage());
  ui::setThemeRuntime(&themeRt);
  char cur[cal::theme::kIdLen];
  themefs::loadCur(cur, sizeof cur);
  uint8_t *scratch = static_cast<uint8_t *>(malloc(cal::theme::kMaxBlock));  // solo per il controllo dell'avvio
  cal::theme::BootPlan p;
  memset(&p, 0, sizeof p);
  if (scratch) p = cal::theme::themeBootPlan(themefs::storage(), cur, scratch);
  free(scratch);
  if (p.use && !themeRt.activate(p.id)) themeCheckBroken(now);
  themesToModel();
  LogSerial.printf("[temi] %d sulla scheda, attivo «%s»\n", gUi.model().themes.n, themeRt.active()[0] ? themeRt.active() : "Calamaio");
  // 5 s come «Tema di serie» (Step 5): l'avvio (2,6 s) non disegna i toast, si vede sulla prima Home
  if (p.broken) themeBroken(p.id, now, 5000);
}
```

(Con lo `scratch` dallo heap la RAM statica cresce solo del buffer del `Runtime`. Se `malloc` fallisce all'avvio il tema resta di serie senza toast; al giro dopo l'utente può sceglierlo dal menu.)

In `setup()`, dopo `avatarsBegin();`: `themesBegin(now);` e dopo `net::begin(boardId);`: `themeReport();`. Nel Ripristino di fabbrica (accanto a `accStore.clear();`): `themefs::forget();`.

- [ ] **Step 4: `main.cpp` — cambi dal menu, eventi, ridisegni a tempo**

```cpp
static bool themeWasLent = false;  // al giro prima il buffer del tema era prestato alla partita

// Il menu (Ui::themePick) cambia m.themes.cur: qui si attiva, si salva nella NVS e si dice al PC.
static void themeSync(Ms now) {
  if (!themeFs) return;
  // In partita il buffer è della cartuccia (Runtime::lend, Step 7): activate() e block() rispondono false senza che il
  // tema sia rotto. Niente da fare finché non torna.
  if (themeRt.lent()) {
    themeWasLent = true;
    return;
  }
  if (themeWasLent) {
    // Fine partita: pumpNet ha scartato gli eventi della rete, ThemeStored compreso. Il file del tema attivo può essere
    // cambiato sotto il Runtime (intestazione e indice vecchi): si riattiva dall'archivio e si rifà il catalogo.
    themeWasLent = false;
    char cur[cal::theme::kIdLen];
    scopy(cur, themeRt.active());
    if (cur[0] && !themeRt.activate(cur)) themeCheckBroken(now);
    themesToModel();
    pending = mergeRefresh(pending, RefreshKind::Quick);
  }
  themeCheckBroken(now);  // prima di activate: un blocco rotto per un indice vecchio non si riattiva per poi cancellarlo
  Themes &t = gUi.model().themes;
  const char *want = t.cur >= 0 && t.cur < t.n ? t.items[t.cur].id : "";
  const char *shown = themeRt.suspended() ? "" : themeRt.active();  // quello che si vede adesso
  if (strcmp(want, shown)) {
    char id[cal::theme::kIdLen];
    scopy(id, want);
    themeRt.suspend(false);
    if (!themeRt.activate(id)) {
      themeCheckBroken(now);
    } else {
      themefs::saveCur(id);
      themeReport();
      LogSerial.printf("[temi] attivo «%s»\n", id[0] ? id : "Calamaio");
      pending = mergeRefresh(pending, RefreshKind::Quick);
    }
  }
  if (themeWakeAt && now >= themeWakeAt) {
    themeWakeAt = 0;
    pending = mergeRefresh(pending, RefreshKind::Partial);
  }
}

static RefreshKind themeEvent(const net::Event &e, Ms now) {
  // In partita pumpNet scarta già questi eventi; comunque niente col buffer prestato: all'uscita ci pensa themeSync.
  if (!themeFs || themeRt.lent()) return RefreshKind::None;
  if (e.type == net::EvType::ThemeStored) {
    // un tema nuovo o aggiornato: se è quello attivo si ricarica (CRC nuovo)
    if (!strcmp(e.id, themeRt.active()) && !themeRt.activate(e.id)) themeCheckBroken(now);
    themesToModel();
    return RefreshKind::Quick;
  }
  // ThemeUse dal PC
  if (!cal::theme::canUse(themefs::storage(), e.id)) {
    LogSerial.printf("[temi] theme-use %s: tema che la scheda non ha, resta «%s»\n", e.id, themeRt.active());
    themeReport();  // themes invariato: il servizio vede che manca e lo riofferta
    return RefreshKind::None;
  }
  Themes &t = gUi.model().themes;
  t.cur = -1;
  for (int k = 0; k < t.n; k++)
    if (!strcmp(t.items[k].id, e.id)) t.cur = k;
  themeSync(now);
  return RefreshKind::None;  // il ridisegno lo chiede themeSync
}
```

Nel `switch` degli eventi di rete (accanto a `EvType::Workday`): `case net::EvType::ThemeStored: case net::EvType::ThemeUse: r = themeEvent(e, now); break;`. In `loop()`, dopo `workSync();`: `themeSync(now);`. Nella funzione che disegna (dove c'è `ui::compose(canvas, kHeltec213, m, f, now, wc);`, riga ~282), subito dopo: `{ const ui::ThemeDrawInfo &ti = ui::lastThemeDraw(); themeWakeAt = ti.drawn ? ti.nextChange : 0; }`.

- [ ] **Step 5: `main.cpp` — uscita di emergenza**

In `loop()`, subito dopo il ciclo dei fronti del tasto:

```cpp
  // Uscita di emergenza (spec §3): PRG tenuto 1,5 s mentre c'è la schermata di avvio → tema di serie per questo avvio.
  // Non all'accensione col tasto già giù: GPIO0 è il pin di boot (la ROM entrerebbe nel caricatore seriale).
  if (safeBoot.step(gestures.pressed(), top(gUi.model()).id == ScreenId::Boot, nowMs()) && themeRt.active()[0]) {
    themeRt.suspend(true);
    toast(gUi.model(), "Tema di serie", nowMs(), 5000);
    LogSerial.println("[temi] PRG all'avvio: tema di serie per questo avvio");
  }
```

e in `onGesture`, in testa: `if (safeBoot.swallow(gestures.pressed())) { LogSerial.printf("[tasto] %s scartato (uscita di emergenza)\n", gestureName(g)); return; }`. Il toast dura 5 s perché l'avvio (2,6 s) non disegna i toast: si vede sulla prima Home.

- [ ] **Step 6: `main.cpp` — Riposo in sonno profondo**

In `keepPanel` (dopo ogni disegno, dove si salva il riquadro della mascotte), prima di `restSeal(rtcRest);`:

```cpp
  // 0.13.0: il tema va in RestKeep solo se ha disegnato davvero il Riposo. Un tema senza «sleep», senza varianti vere o
  // in emergenza lascia il Riposo di serie: con themeId vuoto il risveglio dell'ora resta leggero come oggi (altrimenti
  // drawn falso → risveglio pieno a ogni minuto).
  const ui::ThemeDrawInfo &ti = ui::lastThemeDraw();
  const bool themedRest = ti.drawn && ti.screen == cal::theme::Screen::Sleep;
  scopy(rtcRest.themeId, themedRest ? themeRt.active() : "");
  rtcRest.themeCrc = themedRest ? themeRt.activeCrc() : 0;
```

In `clockWake()`, prima di `ui::compose(…)`:

```cpp
  // 0.13.0: Riposo di un tema. Il blocco serve per ridisegnarlo: LittleFS si monta senza mai formattare (avfs::begin
  // formatterebbe una partizione che non monta, cancellando temi e avatar) e senza scrivere; niente NVS.
  if (rtcRest.themeId[0]) {
    if (!themefs::mountNoFormat()) {
      LogSerial.println("[orologio] LittleFS non montato: risveglio pieno");
      return false;
    }
    themeRt.begin(&themefs::storage());
    // Ogni minuto: niente CRC completo (con theme_fs ogni lettura apre il file; un tema da 64 KB costerebbe circa 16
    // letture da 4 KB e il CRC a 80 MHz a ogni risveglio). Intestazione e indice contro RestKeep bastano: il blocco del
    // Riposo passa checkBlock quando si carica.
    if (!themeRt.activateKnown(rtcRest.themeId, rtcRest.themeCrc)) {
      LogSerial.println("[orologio] il tema del Riposo è cambiato o non si legge: risveglio pieno");
      return false;
    }
    ui::setThemeRuntime(&themeRt);
  }
```

Dopo `ui::compose(…)`, prima di mandare l'immagine al pannello: `if (rtcRest.themeId[0] && !ui::lastThemeDraw().drawn) return false;`
(un blocco che non passa `checkBlock` disegna il Riposo di serie: il riquadro salvato non vale più, decide il
risveglio pieno, dove `themeBootPlan` e `themeBroken` fanno il resto). Misurare in B8 (prova 7) il tempo da svegli.

Così `sleepMascotRect` torna uguale a quello salvato e la mascotte si riprende dall'immagine come oggi: non chiede niente all'interprete, legge `ui::lastThemeDraw()` (con `top == Sleep`, `drawn` e `screen == Sleep`, `ui/screens.cpp`), e in `clockWake` `ui::compose` lo precede già, quindi vede il disegno appena fatto. `themefs::begin()` (crea `/th`, toglie `/th/tmp`) nel risveglio dell'ora non si chiama: scrive. Limite noto (in DECISIONI, Task B8): nel risveglio dell'ora il modello è quello ricostruito da `restModel` (sessioni, giornata, Inverti, Ruota): i valori che non ci sono (limiti, gioco, Oggi) escono vuoti nel Riposo personalizzato.

- [ ] **Step 7: `hw/gb_run.cpp` — il buffer del tema in prestito alla partita**

In testa `#include "core/theme_runtime.h"` e `#include "ui/theme_draw.h"`. Accanto a `gLent` e `gLentBytes`: `uint8_t *gThemeLent = nullptr; size_t gThemeLentBytes = 0;` (il buffer del `Runtime`,
Task B4 Step 5). In `lendRing(ramBytes)`, dopo l'anello (anche se `logtee::lendRing` non l'ha dato: allora `gLentBytes` = 0 e il tema copre i primi banchi) e solo con la rete spenta (`net::suspended()`, la guardia in testa di oggi):

```cpp
  // 0.13.0: dopo i 16 KB dell'anello, i 4 KB del tema (Runtime::lend): due banchi da 2 KB in meno dallo heap.
  cal::theme::Runtime *rt = ui::themeRuntime();
  if (rt && ramBytes > gLentBytes) {
    const size_t want = ramBytes - gLentBytes < cal::theme::kMaxBlock ? ramBytes - gLentBytes : cal::theme::kMaxBlock;
    const size_t m = want / cal::gb::kRamBank * cal::gb::kRamBank;  // solo banchi interi
    gThemeLent = m ? rt->lend(m) : nullptr;
    gThemeLentBytes = gThemeLent ? m : 0;
  }
```

`lentBank(from, len)`: prima l'anello (`gLent && from + len <= gLentBytes`), poi il tema (`gThemeLent && from >= gLentBytes && from + len <=
gLentBytes + gThemeLentBytes` → `gThemeLent + (from - gLentBytes)`); `lentMask` copre anche questi banchi (niente
`free`). `returnRing()` oggi esce subito con `if (!gLent) return;`: il tema si restituisce **prima** di quella riga,
altrimenti con l'anello non prestato il buffer del tema non tornerebbe più (niente tema fino al riavvio):

```cpp
void returnRing() {
  if (gThemeLent) {  // 0.13.0: prima il tema, anche se l'anello non è stato prestato
    gThemeLent = nullptr;
    gThemeLentBytes = 0;
    if (cal::theme::Runtime *rt = ui::themeRuntime()) rt->giveBack();
  }
  if (!gLent) return;
  gLent = nullptr;
  gLentBytes = 0;
  logtee::returnRing();
}
```

(tutte le uscite passano già da lì, `freeCtx` compreso). In `begin`, il controllo di memoria conta anche i byte del
tema (prestati da `lendRing`, che lo precede): `needBytes(info, gLentBytes + gThemeLentBytes)` al posto di
`needBytes(info, gLentBytes)`, altrimenti la partita si rifiuta come se il prestito non ci fosse (proprio il caso di
Pokémon Red). Nel ciclo dei banchi, accanto a `fromRing += len;`, un contatore a parte per la riga del registro:
`if (size_t(i) * cal::gb::kRamBank >= gLentBytes) fromTheme += len;` (`size_t fromTheme = 0;` accanto a `fromRing`);
`fromRing` resta il totale prestato, così `if (!fromRing) returnRing();` regge. La riga `[gb] RAM cartuccia` diventa
`%u byte dall'anello del registro, %u dal tema, %u dallo heap` con `fromRing - fromTheme`, `fromTheme`,
`g->ramSize - fromRing`. Il ritorno dalla partita ridisegna: `block()` rilegge il blocco, e `themeSync` (Step 4)
riattiva il tema dall'archivio. La logica del prestito è in `test_lend_give_back` (B4); qui la prova è la build e B8
(prova 6b).

- [ ] **Step 8: Versione**

`firmware/src/core/model.h`: `#define CALAMAIO_FW_VERSION "0.13.0-ed"`. `firmware/test/test_net/test_main.cpp`: `TEST_ASSERT_EQUAL_STRING("0.13.0-ed", kFwVersion);  // 0.13.0: temi delle schermate (editor, fase 1)`.

- [ ] **Step 9: Test e build**

Run: `cd firmware && pio test -e native && pio test -e native_asan && pio run -e heltec213 2>&1 | grep -E "RAM:|Flash:|SUCCESS"`
Expected: tutto verde; immagine sotto 2 359 296 byte; RAM statica circa +4,6 KB rispetto a oggi (buffer del `Runtime`, catalogo nel modello); in partita il buffer torna alla cartuccia (Step 7). Annotare i due numeri per DECISIONI.

- [ ] **Step 10: Commit**

```bash
git add firmware/src/core/theme_runtime.h firmware/src/core/theme_runtime.cpp firmware/src/core/wake.h firmware/src/main.cpp firmware/src/hw/gb_run.cpp firmware/src/core/model.h firmware/test/test_net/test_main.cpp firmware/test/test_theme_store firmware/test/test_clock
git commit -m "Temi: tema attivo sulla scheda, uscita di emergenza col PRG, Riposo in sonno profondo, firmware 0.13.0-ed"
```

---
### Task B8: Documentazione e prove dal vivo

**Files:**
- Modify: `docs/PROTOCOLLO.md` (sezione «Temi», righe della tabella dell'API, campo `themes` del Display, sequenza), `docs/DECISIONI.md` (sezione «Editor delle schermate»), `docs/TEMI.md` (§8 «Sulla scheda e nel servizio»)
- Test: `cd service && go test ./...` (gli esempi del protocollo), lettura

**Interfaces:**
- Consumes: tutto quello che c'è sopra.
- Produces: la documentazione che i piani 2–4 dell'editor danno per scontata.

- [ ] **Step 1: `docs/PROTOCOLLO.md`**

Nell'intestazione, in fondo all'elenco delle aggiunte compatibili: «; poi i temi delle schermate, sezione «Temi»». Nella sequenza:

```
display → servizio   themes       (temi sulla scheda e attivo: dopo auth-ok e a ogni cambio)
servizio → display   theme-offer → display: GET /themes/<token> → theme-ack     (solo autenticato)
servizio → display   theme-use    (solo autenticato) → themes
```

Sezione nuova prima di «API locale della webapp»:

```markdown
## Temi

Formato del tema (JSON e binario CTHM v1) e regole di disegno: `docs/TEMI.md` (fonte unica). Qui solo il filo.
Aggiunta compatibile alla v1: tutti i messaggi sono nuovi e facoltativi; un firmware che non li conosce li ignora e il
servizio, che non riceve `themes`, non gli offre temi. Niente `mac`: come `set`, valgono solo su una connessione
autenticata (M5). Permessi, abbinamento e schermate di sistema restano sempre di serie.

### `themes` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `have` | array di `{id, crc}` | i temi sulla scheda (0–8), `crc` = byte 8–11 del CTHM |
| `cur` | string o `null` | tema attivo; `null` = Calamaio (di serie) |

Dopo `auth-ok` e a ogni cambio (tema installato, scelto dal menu o con `theme-use`, rovinato e tolto). In modalità gioco
(Wi-Fi spento) non parte: la scheda lo manda al ritorno. Il servizio ignora un `themes` da una connessione non
autenticata.

### `theme-offer` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | id del tema (kebab-case, ≤ 24) |
| `crc` | u32 | CRC del CTHM |
| `size` | int | byte del CTHM (72–65 536) |
| `token` | hex 16 byte | `GET http://<pc>:8766/themes/<token>`, host e porta = quelli del WebSocket della scheda |

**Download.** `GET /themes/<token>` sulla porta dei display: 200 `application/octet-stream`, `Content-Length` = `size`,
`X-Calamaio-Crc`. Token monouso, valido 5 minuti, solo per l'IP della scheda che ha ricevuto l'offerta: sconosciuto 404,
già usato o scaduto 410, IP diverso 403 (resta valido per la scheda). La scheda scrive in `/th/tmp`, controlla
lunghezza, CRC, intestazione (stesso `id`) e ogni blocco con l'interprete in sola lettura, poi rinomina in
`/th/<id>.cthm` (rinomina diretta sopra il file vecchio); solo dopo, se i temi sono diventati 9, toglie il più vecchio
non attivo (rinomina fallita: «salvataggio non riuscito», nessun tema tolto). Un'offerta mentre un'altra è in corso: `theme-ack` con
`ok: false`, «un altro tema in arrivo, riprova».

### `theme-ack` (display → servizio)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string | quello dell'offerta |
| `ok` | bool | |
| `error` | string, facoltativo | con `ok: false`, ≤ 127 byte: «display non abbinato: cambia le impostazioni dal menu del display» (connessione non autenticata, lo stesso testo di `set` e `workday`), «CRC sbagliato», «download non riuscito (HTTP 410)», «download incompleto», «il tema non è quello offerto», «salvataggio non riuscito», «un altro tema in arrivo, riprova», … |

### `theme-use` (servizio → display, solo autenticato)
| Campo | Tipo | Note |
|---|---|---|
| `id` | string o `null` | tema da attivare; `null` = Calamaio |

La scheda attiva il tema se ce l'ha (lo salva nella NVS `calamaio-th/cur`) e risponde `themes`; se non ce l'ha resta
com'è e risponde `themes` invariato (il servizio allora lo riofferta).

**Servizio.** `POST /api/displays/{board}/theme` avvia un'operazione per scheda (una alla volta: la nuova sostituisce
la vecchia): `attesa` (scheda scollegata, non autenticata o senza `themes` ancora) → `offerta` → `scaricato` → `uso` →
`fatto`, oppure `errore` (theme-ack negativo; nessun theme-ack entro 60 s: «la scheda non ha risposto all'offerta del
tema»; nessun `themes` col tema attivo entro 15 s dal theme-use: «la scheda non ha attivato il tema»). `theme-use` parte
una volta sola per operazione. Un tema per uno schermo diverso: 409. Se la scheda si
scollega a metà l'operazione torna in `attesa` e riparte al ritorno con un token nuovo. Un tema già sulla scheda con lo
stesso CRC non si riscarica. Esempi: `protocol/examples/theme*.json`.
```

Nella tabella dell'API le righe di B2 e quella di `POST /api/displays/{board}/theme` (B3), con le risposte esatte. In «Display in `/api/status`»: «`themes` = `{supported, have, cur, job}`: `supported` (la scheda ha mandato `themes` su questa connessione), `have` e `cur` dall'ultimo `themes`, `job` (`null` oppure **ThemeJob** `{id, board, theme, name, crc, stage, error?, started, updated}`)».

- [ ] **Step 2: `docs/TEMI.md` §8**

In fondo, dopo «## 7. Parità C++/JS» (del piano 1a):

```markdown
## 8. Sulla scheda e nel servizio

- **PC**: `<dati utente>/calamaio/themes/<id>.json` (indentato) e la cronologia `<id>/<n>.json` (ultime 20 versioni:
  ogni salvataggio, ripristino compreso, è una versione nuova). API in `docs/PROTOCOLLO.md` «Temi» e «API locale».
- **Scheda**: `/th/<id>.cthm` (al massimo 8, `/th/order` = ordine d'installazione), tema attivo nella NVS
  `calamaio-th/cur`. All'avvio il tema attivo si controlla tutto (CRC e blocchi); rovinato o mancante → di serie,
  toast «Tema rovinato: uso quello di serie», file tolto. Il blocco della schermata corrente sta in un buffer statico
  da 4 096 byte, ricaricato al cambio di schermata; un blocco che non passa i controlli → di serie, mai a metà. In
  partita il buffer si presta alla cartuccia (come l'anello del registro) e torna all'uscita.
- **Scelta**: Menu › Display › Tema (✓ sull'attivo, cambio immediato) o dalla webapp. **Uscita di emergenza**: PRG
  tenuto 1,5 s durante la schermata di avvio → tema di serie per questo avvio (toast «Tema di serie»), la scelta salvata
  resta. Non col tasto già giù all'accensione: GPIO0 è il pin di boot.
- **Riposo in sonno profondo**: se il Riposo l'ha disegnato il tema, il risveglio dell'ora monta LittleFS senza mai
  formattare e rilegge intestazione, indice e blocco del Riposo
  (niente NVS né rete, niente CRC di tutto il file: basta il CRC dell'intestazione uguale a quello salvato col Riposo;
  il blocco passa comunque i controlli) e disegna col modello ricostruito dal sonno: valgono ora, data, giorno, sessioni, batteria e giornata; gli altri valori
  escono vuoti fino al risveglio pieno.
```

- [ ] **Step 3: `docs/DECISIONI.md`**

Sezione nuova in fondo, `## Editor delle schermate`, con una voce per ciascuna scelta (data 2026-10-15, firmware 0.13.0-ed):

- **Temi, fase 1 (Fondamenta)**: formato JSON v1 (`docs/TEMI.md`) e binario CTHM v1; nomi del JSON in inglese, valori e filtri dei testi in italiano come nella spec; condizioni e varianti con nomi propri (`when: always/none/one/many`, `ref: selected/urgent/row/nobody`).
- **Schermate personalizzabili nella fase 1**: home, detail, sleep, offline, riepilogo, oggi, batteria, stato, limiti, focus, ambiente, pet. Bloccate (sicurezza, spec §3): perm, pair, confirm, boot, ota, battlow, calib, gbload, gbplay. Non ancora (mancano gli elementi per i loro dati): list, wifiScan, qr, connecting, storico, guest, servizi, agenda, game.
- **Valori**: formati uguali alle schermate di serie (percentuali col «%», «—» senza dati, `oggi.costo` come `oggi()`); `giorno` = nome del giorno, `data` = `dateStr`; `test` = ✓/✗; `sub` = numero; `inattesa` = sessioni in Permesso (anche senza rete).
- **Stringhe nel blocco**: ogni schermata ha la sua tabella (blocco autosufficiente da 4 096 byte nel buffer statico).
- **Interprete in due file**: la logica pura in `core/theme_*`, il disegno in `ui/theme_draw.cpp` (le funzioni `T()` e `avatarS()` stanno in `ui/screens.cpp`: si usano tramite `ui/kit.h`).
- **Home «Una» = `sIdx`** (piano 1a, T4): nello stile «Una» la sessione «selezionata» (`ref: selected`) è `sIdx`, come la Home di serie, non `ui.page`.
- **Regola di `extends` negli scenari** (piano 1a, C6): C++ e JS calcolano `since` con il `now` finale dello scenario esteso (una sola regola, in `protocol/themes/scenarios.json` e TEMI); i due modi non sono equivalenti e un caso del golden la fissa.
- **Parità transitiva** (piano 1a, C9; la spec §7 diceva «gira in `go test`»): JS scrive il golden, `go test` (`TestThemeRendererParity`, mai dalla cache: il test riscrive e riapre un file del modulo, perché la cache di `go test` non vede i file fuori da `service/`) controlla che sia fresco, `pio test -e native -f test_theme_parity` confronta il C++. `go test` da solo non vede una deriva del C++: la vede `pio test`. Le fixture C++ (testi, QR, valori) si ricontrollano con `gen-fixture.mjs --check` (clang).
- **`fonts.js`**: 467 KB nell'embed senza gzip (piano 1a, T7); nella fase 1 non lo importa nessuna pagina, va compresso o caricato a parte quando l'editor lo usa.
- **Anteprima e blocchi non validi** (piano 1a, T10): l'anteprima dà `drawn: false` (schermata di serie) come la scheda quando un elemento non passa i controlli; invece `lowerTheme` toglie in silenzio le condizioni non valide e sostituisce i valori sconosciuti (dove il servizio rifiuta il tema). L'editor della fase 2 deve mostrare i problemi del servizio accanto all'anteprima.
- **RAM statica del piano 1a**: +1 760 byte (`t[256]`, `ls[8][96]` del disegno, pezzi dei valori, `qr::Bits`, ultimo disegno), più il `Runtime` di questo piano (in partita prestato alla cartuccia).
- **Disegno di `rect`/`circle` pixel per pixel**: nel caso peggiore (64 rettangoli pieni a tutto schermo) centinaia di ms a 240 MHz e circa il triplo a 80 MHz nel risveglio dell'ora, non 10–20 ms; i temi veri sono lontani. Accettabile per l'e-ink; se servirà, riempire per righe (`[xa, xb)` per ogni `py`) in C++ e JS insieme, senza perdere la parità.
- **Renderer JS sul JSON** (non sul CTHM): lo abbassa `lower.js` con le stesse regole del compilatore; la parità C++/JS si prova col golden scritto da `tools/golden/theme-golden.mjs` e ricontrollato da `go test` (`TestThemeRendererParity`). Caratteri esportati dai `.h` del firmware (`tools/fonts/export-js.mjs`), senza rasterizzare di nuovo.
- **`invert`**: testo in carta con le varianti chiaro su scuro dei caratteri; gli altri elementi con inchiostro e carta scambiati.
- **Protocollo senza `mac`**: come `set`/`workday`, solo su connessione autenticata; `theme-offer` porta il token, l'URL lo costruisce la scheda dall'indirizzo del WebSocket.
- **Nono tema**: esce il più vecchio non attivo (non c'è ancora un modo di cancellare dalla scheda), e solo dopo che il nuovo è salvato (tutto o niente). Rinomina diretta di LittleFS sopra il file esistente, senza `remove` prima (prova dal vivo 9: se esp_littlefs non sostituisse il file, l'aggiornamento si annullerebbe, sicuro). Un lucchetto (`themefs::lock`) fra il task di rete e il loop per i file dei temi e `/th/order`.
- **`themes` dopo `auth-ok`** (la spec §4 dice «dopo `hello`»): prima dell'autenticazione il servizio non offre niente e ignora un `themes`, quindi mandarlo prima sarebbe inutile.
- **Tema rotto ricontrollato prima di toglierlo**: `themeBroken` rilegge il file con `verifyStored`; integro (indice vecchio, `ThemeStored` scartato in partita) → si riattiva, mai cancellato. All'uscita dalla partita il tema attivo si riattiva dall'archivio; mentre il buffer è prestato (`Runtime::lent()`) i temi nel loop non fanno niente.
- **`POST /api/displays/{board}/theme` con la scheda scollegata ma abbinata**: accettato, l'operazione aspetta («attesa») e riparte al ritorno (spec §4, modalità gioco). In memoria del servizio: dopo un riavvio del servizio si rimanda dalla webapp.
- **Uscita di emergenza**: PRG tenuto 1,5 s durante l'avvio (la spec dice «all'accensione»: col tasto già giù la ROM dell'ESP32-S3 entra nel caricatore seriale perché PRG è GPIO0).
- **Display › Tema**: ultima voce di Display (meno spostamenti nelle catture). **Allineamento del simulatore**: voce «Tema» e lista «tema» con la sola «Calamaio»; rigenerati golden del core e catture (`menu-tema`, `menu-tema-lista`, scenari del Display).
- **Riposo in sonno profondo con un tema**: LittleFS montato anche nel risveglio dell'ora; tema e CRC in `RestKeep` (magia cambiata); valori non conservati nel sonno vuoti.
- **Limiti noti**: `oggi.costo` usa `%.2f` in C e `printf2` in JS (piano 1a, T9), che arrotonda i pari merito esatti in binario (per esempio 0,125) come `%.2f` della libc di macOS (quella dei test nativi del C++); resta da vedere se la newlib della scheda fa lo stesso (prova dal vivo 8, Step 6). Il JS dei dati di prova non modella la calibrazione della batteria oltre a `battRate`.
- **Misure**: immagine `N` byte (limite 2 359 296), RAM statica `M` byte (da Task B7, Step 9) — scrivere i numeri veri.

- [ ] **Step 4: Controlli finali**

Run: `cd service && go test ./... && cd ../firmware && pio test -e native && pio run -e heltec213 | tail -3 && cd ../tools/webui-dev && npm test && cd ../.. && node tools/golden/theme-golden.mjs --check && node tools/fonts/export-js.mjs --check`
Expected: tutto verde.

- [ ] **Step 5: Commit**

```bash
git add docs/PROTOCOLLO.md docs/DECISIONI.md docs/TEMI.md
git commit -m "Temi: documentazione del protocollo, del formato e delle scelte della fase 1"
```

- [ ] **Step 6: Prove dal vivo (le fa il coordinatore, dopo un sì esplicito dell'utente)**

Prima: backup di NVS e otadata della scheda (`backup-fabbrica/`, mai nel repository); poi il firmware 0.13.0-ed con l'aggiornamento remoto del servizio (`calamaio ota push --build firmware/.pio/build/heltec213`) oppure via cavo a 115200 scrivendo solo otadata e app0. Servizio di sviluppo avviato con `calamaio serve` (cartella dei temi: `~/Library/Application Support/calamaio/themes`).

1. **Tema inviato e attivato**: `curl -s -X POST -H 'Content-Type: application/json' -H 'Origin: http://127.0.0.1:8765' --data-binary @protocol/themes/fixtures/prova.theme.json http://127.0.0.1:8765/api/themes`, poi `curl -s -X POST -H 'Content-Type: application/json' -H 'Origin: http://127.0.0.1:8765' -d '{"id":"prova"}' http://127.0.0.1:8765/api/displays/<board>/theme`. Atteso: nel registro della scheda `[temi] prova installato (… byte) in … ms` (annotare il tempo in DECISIONI: oltre 1 s il download va a pezzi per giro come `otaLoop`, Task B5), Home con la mascotte grande, `/api/status` con `themes.cur = "prova"` e `job.stage = "fatto"`.
2. **Cambio dal Menu della scheda**: Menu › Display › Tema › Calamaio (2×) e di nuovo «Mascotte grande». Atteso: cambio immediato, ✓ che si sposta, `themes` nel registro del servizio.
3. **PRG all'avvio**: riavvio (Info › Riavvia), PRG tenuto durante la schermata di avvio. Atteso: Home di serie e toast «Tema di serie»; dopo un riavvio normale torna «Mascotte grande».
4. **Permesso in arrivo col tema attivo**: una richiesta di permesso da Claude Code. Atteso: schermata Permessi di serie (NEGA preselezionato, tenuto 3 s), identica a quella senza tema.
5. **Memoria**: righe `[memoria]` con il tema attivo e il Bluetooth acceso (controller associato): heap libero e minimo da confrontare con la 0.12.5-gb (annotare in DECISIONI).
6. **Tema durante e dopo una partita**: con il tema attivo avviare una partita (Menu › Gioco › ROM), mandare un tema dalla webapp durante la partita (atteso: `job.stage = "attesa"`), uscire. Atteso: al ritorno della rete offerta, download e attivazione da soli.
6b. **Cartuccia grande col tema attivo**: con «Mascotte grande» attivo avviare Pokémon Red (MBC3 + 32 KB di RAM). Atteso: la partita parte, la riga `[gb] RAM cartuccia: 16384 byte dall'anello del registro, 4096 dal tema, … dallo heap` e le righe `[memoria]`, da confrontare con la 0.12.5-gb (annotare in DECISIONI); all'uscita la Home del tema torna uguale.
7. **Riposo in sonno profondo**: con il tema attivo (che ha la schermata `sleep`) lasciare la scheda nel Riposo senza cavo per 10 minuti. Atteso: l'ora si aggiorna a ogni minuto senza risvegli pieni (registro `[orologio]`), tempo da svegli per risveglio annotato in DECISIONI (confrontarlo col Riposo di serie: `activateKnown` non rilegge tutto il file).
7b. **Tema senza Riposo**: con un tema che non ha la schermata `sleep` lasciare la scheda nel Riposo 5 minuti. Atteso: risvegli dell'ora leggeri come col tema di serie (nessun «risveglio pieno» a ogni minuto nel registro `[orologio]`).
8. **Pari merito di `oggi.costo` sulla newlib**: un tema con `{oggi.costo}` sulla schermata Oggi e un costo del giorno a pari merito esatto (0,125 $, dai dati di prova del servizio). Atteso: la scheda scrive lo stesso testo dell'anteprima della webapp; se no, annotare in DECISIONI (Limiti noti) e allineare `printf2` alla newlib.
9. **Aggiornamento di un tema già sulla scheda** (rinomina diretta sopra un file esistente): cambiare il nome di «prova» con `PUT /api/themes/prova` e rimandarlo. Atteso: `[temi] prova installato`, il tema nuovo attivo, `job.stage = "fatto"`; se il registro dice «salvataggio non riuscito» esp_littlefs non sostituisce il file: annotare in DECISIONI e decidere con l'utente (il tema vecchio resta, nessuna perdita).

---

## Fine del piano 1b

Esito: un tema scritto a mano si vede sulla scheda (spec §8, fase 1). Fase 2 (l'editor nella webapp) parte da qui: usa `GET/POST/PUT /api/themes`, `POST /api/displays/{board}/theme`, `lowerTheme`/`renderScreen`/`makeModel` del renderer e le avvertenze di `Info.warnings` e `Info.screens` (byte per schermata).
