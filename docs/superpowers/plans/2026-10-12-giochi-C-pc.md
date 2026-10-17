# Giochi Game Boy — piano C (PC) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** il servizio Go tiene la cartella delle cartucce del Game Boy dell'utente, le offre alla scheda (elenco, avvio, download della ROM con ripresa, salvataggi a 3 copie con ripristino) e le mostra in una pagina «Giochi» della webapp e nei comandi `calamaio gb …`.

**Architecture:** un pacchetto nuovo `service/internal/gbgames` (intestazione della cartuccia, cartella con `index.json`, salvataggi) senza dipendenze dal resto; `service/internal/display/gb.go` aggiunge all'hub i messaggi `gb-*` e tre rotte HTTP sulla porta dei display (stesso modello di sicurezza di `GET /ota/{token}`: token casuale, scadenza, solo l'IP della scheda, consegna unica); `service/internal/api/gb.go` espone la API locale su 127.0.0.1; `service/internal/client/gb.go` fa la CLI; la pagina `gb.html` usa la stessa grafica e la Goccia animata delle altre pagine.

**Tech Stack:** Go 1.24 (stdlib, `github.com/coder/websocket`, `jsonschema/v6` già presenti), JavaScript ES module senza build (webapp incorporata con `go:embed` in `service/internal/webui`), Node 20 + Playwright 1.64 solo per gli strumenti di prova in `tools/webui-dev`.

**Spec:** `docs/superpowers/specs/2026-10-12-giochi-gameboy-design.md`; contratto vincolante tra i piani A/B/C: `docs/superpowers/plans/2026-10-12-giochi-contratti.md` (questo piano implementa la parte C e il lato servizio di «B ↔ C»; le proposte di modifica al contratto sono in fondo, sezione «Proposte di modifica al contratto», da approvare prima di eseguire i task 5–8).

**Verifica del piano (2026-10-12):** il codice dei task 1–12 è stato applicato così com'è a una copia del repo fuori dall'albero di lavoro: `go vet ./...`, `go test ./...`, `go test -race ./internal/display/`, `npm test` e `node shots.mjs --giochi` passano, anche negli stati intermedi dopo i task 6 e 7 (scheletri compresi). Dopo l'incolla serve solo `gofmt -w` (allineamenti dei commenti e delle mappe).

## Global Constraints

- Prefisso **`gb`** ovunque: pacchetto Go `gbgames`, messaggi `gb-*`, file `gb*`; testi a schermo «Giochi» («game» è già del gioco della mascotte).
- Testi a schermo e messaggi d'errore **in italiano**; identificatori in inglese; commenti brevi in italiano (CLAUDE.md).
- Cartella: `<dati>/calamaio/games/` con `<id>.gb` e `index.json`; salvataggi in `<dati>/calamaio/games/saves/<id>/`, **ultime 3 copie** per gioco.
- `id` = **primi 16 esadecimali (minuscoli) dello SHA-256 della ROM**.
- ROM accettata: intestazione valida (logo di avvio, checksum dell'intestazione), tipo di cartuccia emulabile (ROM, MBC1, MBC2, MBC3, MBC5), dimensione del file uguale a quella dichiarata, **≤ 2 MiB (2 097 152 byte)**; **rifiuta CGB-only (0x143 = 0xC0)**, **accetta DMG e CGB-compatibili (0x80)**.
- **Solo file forniti dall'utente**: il servizio non scarica mai ROM; nel repo nessuna ROM vera, le prove costruiscono immagini sintetiche (`gbgames.FakeROM`).
- Messaggi `gb-*`: **tutti solo con connessione autenticata**, facoltativi e compatibili (un firmware che non li conosce li scarta come tipi sconosciuti). Messaggi servizio → display con `mac` = HMAC-SHA256(SK, etichetta ␟ campi) troncato a 16 byte, come `ota-*` e `wifi-forget`; i messaggi display → servizio senza mac (come `ota-status`, `crash`, `log`: vale l'autenticazione della connessione).
- Rotte della scheda sulla **porta dei display (8766)**, come `/ota/{token}`, mai sulla 8765: token di 16 byte casuali (32 cifre esadecimali minuscole), **10 minuti** (`Hub.GbTokenTTL`; il `Hub.TokenTTL` dell'OTA resta 5 minuti), **solo l'IP della scheda**; la ROM si serve con `Range` sullo stesso token fino alla consegna completa, il caricamento di un salvataggio si accetta finché un `PUT` completo non riesce; 404 sconosciuto, 410 usato o scaduto, 403 IP diverso.
- Contratto (versione del 2026-10-12): mac obbligatori su `gb-list`, `gb-start`, `gb-save-ok`; formati e limiti (id, size 0x150–0x200000, token `[A-Za-z0-9_-]{1,64}`, titoli ASCII 1–16, ≤ 64 giochi, salvataggi 1–131 136 byte, `.gb`/`.gbc`); OTA rifiutato dalla scheda durante la partita e rifatto dal servizio dopo `gb-state {playing: null}` (task 7); avvio rifiutato (409) durante un OTA.
- API locale solo su `127.0.0.1:8765` con le guardie esistenti (`guard`, `postGuard`, `bodyGuard`, `deleteGuard`); errori `{"error": "testo in italiano"}`.
- Prima di ogni commit `gofmt -w` sui file Go toccati (i blocchi del piano possono avere allineamenti da sistemare).
- Test: `cd service && go test ./...` (tutto verde a ogni commit); webapp: `cd tools/webui-dev && npm test`; prova a schermo (facoltativa, serve Playwright): `cd tools/webui-dev && node shots.mjs`.
- Commit: messaggi in italiano, **senza** riga `Co-Authored-By`.
- Mai toccare `reference/`, `~/.claude/settings.json`, la flash o la NVS: questo piano è solo PC.

## Review Focus

1. **Download della ROM interrotto e ripreso** (Wi-Fi che cade a metà dei 2 MB): la scheda riprende con `Range: bytes=N-` sullo stesso token e riceve il resto; il token si consuma solo quando l'ultimo byte è stato consegnato, poi 410. Test: `TestGbROMRangeResume` (task 7).
2. **Copia da ripristinare cancellata dalla rotazione**: l'utente sceglie «Ripristina» sulla copia 3, poi al collegamento la scheda carica un salvataggio nuovo prima di scaricare il ripristino; la copia scelta non deve sparire (si tiene una quarta copia finché il ripristino non è consegnato). Test: `TestRestoreSurvivesPrune` (task 3).
3. **Id malformati o percorsi** (`../x`, maiuscole, 15 cifre) in API, rotte della scheda e `gb-save`: sempre 404 o messaggio ignorato, mai un accesso a file fuori dalla cartella. Test: `TestBadIDs` (task 2) e `TestGbAPIBadIDs` (task 9).
4. **Stessa cartuccia caricata due volte con nomi diversi, o ROM cambiata sul disco dopo il caricamento**: un solo gioco (`existed: true`); una ROM alterata non parte mai (409 con il motivo). Test: `TestAddDuplicate`, `TestReadROMTampered` (task 2), `TestStartGbTampered` (task 7).
5. **Salvataggio falso o ripetuto** (connessione non autenticata, token riusato, CRC o dimensione diversi da `gb-save`): niente scritto su disco, nessun `gb-save-ok`. Test: `TestGbSaveRejects` (task 8).

---

## File toccati

| File | Responsabilità |
|---|---|
| `service/internal/gbgames/header.go` (nuovo) | intestazione della cartuccia: controlli, titolo, tipo, dimensioni |
| `service/internal/gbgames/fakerom.go` (nuovo) | ROM sintetiche per le prove (anche di altri pacchetti) |
| `service/internal/gbgames/library.go` (nuovo) | cartella dei giochi, `index.json`, aggiunta, rimozione, lettura verificata |
| `service/internal/gbgames/saves.go` (nuovo) | salvataggi: 3 copie, ripristino in attesa |
| `service/internal/m5/gb.go` (nuovo) | mac di `gb-list`, `gb-start`, `gb-save-ok` |
| `protocol/schema/message.schema.json`, `protocol/examples/gb-*.json` | schema ed esempi veri dei messaggi |
| `docs/PROTOCOLLO.md` | sezione «Giochi» e rotte API |
| `service/internal/display/gb.go` (nuovo) | messaggi `gb-*` della scheda, `gb-list`, stato per la webapp |
| `service/internal/display/gb_start.go` (nuovo) | `gb-start`, `gb-pick`, `GET /gb/rom/{token}` con Range |
| `service/internal/display/gb_save.go` (nuovo) | `gb-save`, `PUT`/`GET /gb/save/{token}`, `gb-save-ok`, ripristino |
| `service/internal/display/hub.go`, `remote.go`, `m5.go` | aggancio dei messaggi, stato per la webapp, `gb-list` dopo `auth-ok` |
| `service/internal/api/gb.go` (nuovo), `api/server.go`, `api/web.go` | API locale `/api/gb/…` |
| `service/internal/server/server.go`, `service/cmd/calamaio/main.go` | apertura della cartella, sorveglianza, opzione `GbDir` |
| `service/internal/client/gb.go` (nuovo) | `calamaio gb list\|add\|rm\|start\|saves` |
| `service/internal/webui/static/gb.html`, `assets/js/gb.js`, `assets/js/gb-page.js` (nuovi), `assets/js/api.js`, `assets/app.css`, le altre 6 pagine | pagina «Giochi» |
| `tools/webui-dev/test-units.mjs`, `mock-server.mjs`, `shots.mjs` | prove della webapp |
| `docs/DECISIONI.md` | scelte prese |

---

### Task 1: intestazione della cartuccia e ROM sintetiche

**Files:**
- Create: `service/internal/gbgames/header.go`
- Create: `service/internal/gbgames/fakerom.go`
- Test: `service/internal/gbgames/header_test.go`

**Interfaces:**
- Consumes: niente.
- Produces:
  - `const MinROM = 32 << 10`, `const MaxROM = 2 << 20`, `const MaxTitle = 16`
  - `type Header struct { Title string; CartType byte; Mapper string; Battery bool; ROMSize int; RAMSize int; CGB bool }` (tag JSON `title, cartType, mapper, battery, romSize, ramSize, cgb`)
  - `type InvalidError struct{ Msg string }` (implementa `error`); `var ErrTooLarge, ErrCGBOnly *InvalidError`
  - `func ParseHeader(rom []byte) (Header, error)`
  - `func TitleFromFile(name string) string`
  - `type FakeROMOptions struct { Title string; CartType, SizeCode, RAMCode, CGB, Fill byte }`, `func FakeROM(o FakeROMOptions) []byte`

- [ ] **Step 1: Write the failing test**

`service/internal/gbgames/header_test.go`:

```go
package gbgames

import (
	"errors"
	"strings"
	"testing"
)

// Il checksum dell'intestazione è quello della ROM di avvio: valore calcolato a parte (Python) per
// titolo «TEST», licenziatario «00», destinazione 1, vecchio licenziatario 0x33, resto a zero.
func TestHeaderChecksumKnown(t *testing.T) {
	rom := FakeROM(FakeROMOptions{Title: "TEST"})
	if rom[0x14D] != 0x13 {
		t.Fatalf("checksum 0x%02x, atteso 0x13", rom[0x14D])
	}
	if len(rom) != MinROM {
		t.Fatalf("lunghezza %d", len(rom))
	}
}

func TestParseHeaderAccepts(t *testing.T) {
	cases := []struct {
		name string
		o    FakeROMOptions
		want Header
	}{
		{"ROM semplice", FakeROMOptions{Title: "HOMEBREW DEMO"},
			Header{Title: "HOMEBREW DEMO", CartType: 0x00, Mapper: "ROM", ROMSize: 32 << 10}},
		{"MBC1 con batteria", FakeROMOptions{Title: "PUZZLE", CartType: 0x03, SizeCode: 1, RAMCode: 2},
			Header{Title: "PUZZLE", CartType: 0x03, Mapper: "MBC1", Battery: true, ROMSize: 64 << 10, RAMSize: 8 << 10}},
		{"MBC2 ha 512 mezzi byte", FakeROMOptions{Title: "MBC2", CartType: 0x06, SizeCode: 2},
			Header{Title: "MBC2", CartType: 0x06, Mapper: "MBC2", Battery: true, ROMSize: 128 << 10, RAMSize: 512}},
		{"compatibile Color, titolo di 15", FakeROMOptions{Title: "ABCDEFGHIJKLMNO", CartType: 0x1B, SizeCode: 5, RAMCode: 3, CGB: 0x80},
			Header{Title: "ABCDEFGHIJKLMNO", CartType: 0x1B, Mapper: "MBC5", Battery: true, ROMSize: 1 << 20, RAMSize: 32 << 10, CGB: true}},
		{"2 MB, il massimo", FakeROMOptions{Title: "GRANDE", CartType: 0x13, SizeCode: 6, RAMCode: 3},
			Header{Title: "GRANDE", CartType: 0x13, Mapper: "MBC3", Battery: true, ROMSize: 2 << 20, RAMSize: 32 << 10}},
		{"titolo di 16 senza Color", FakeROMOptions{Title: "ABCDEFGHIJKLMNOP"},
			Header{Title: "ABCDEFGHIJKLMNOP", Mapper: "ROM", ROMSize: 32 << 10}},
	}
	for _, c := range cases {
		h, err := ParseHeader(FakeROM(c.o))
		if err != nil {
			t.Errorf("%s: %v", c.name, err)
			continue
		}
		if h != c.want {
			t.Errorf("%s: %+v, atteso %+v", c.name, h, c.want)
		}
	}
}

func TestParseHeaderRejects(t *testing.T) {
	good := func() []byte { return FakeROM(FakeROMOptions{Title: "OK", CartType: 0x01, SizeCode: 1}) }
	badLogo := good()
	badLogo[0x110] ^= 0xFF
	badSum := good()
	badSum[0x14D]++
	cases := []struct {
		name string
		rom  []byte
		want string
	}{
		{"troppo corto", make([]byte, 0x150), "troppo corto"},
		{"oltre 2 MB", FakeROM(FakeROMOptions{Title: "X", SizeCode: 7}), "più grande di 2 MB"},
		{"senza logo", badLogo, "manca il logo di avvio"},
		{"checksum sbagliato", badSum, "checksum"},
		{"solo Color", FakeROM(FakeROMOptions{Title: "COLOR", CGB: 0xC0}), "solo per Game Boy Color"},
		{"MBC7 non supportata", FakeROM(FakeROMOptions{Title: "MBC7", CartType: 0x22, SizeCode: 1}), "tipo 0x22"},
		{"file tagliato", good()[:MinROM], "l'intestazione dice 65536"},
		{"codice dimensione strano", FakeROM(FakeROMOptions{Title: "X", SizeCode: 0x52})[:MinROM], "codice 0x52"},
		{"RAM non valida", FakeROM(FakeROMOptions{Title: "X", RAMCode: 9}), "RAM della cartuccia"},
	}
	for _, c := range cases {
		_, err := ParseHeader(c.rom)
		var inv *InvalidError
		if !errors.As(err, &inv) || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s: %v (atteso un InvalidError con %q)", c.name, err, c.want)
		}
	}
	if _, err := ParseHeader(FakeROM(FakeROMOptions{Title: "C", CGB: 0xC0})); !errors.Is(err, ErrCGBOnly) {
		t.Errorf("CGB-only: %v", err)
	}
	if _, err := ParseHeader(FakeROM(FakeROMOptions{Title: "X", SizeCode: 7})); !errors.Is(err, ErrTooLarge) {
		t.Errorf("4 MB: %v", err)
	}
}

func TestTitles(t *testing.T) {
	// byte non stampabili: il titolo si ferma lì; spazi in coda tolti
	rom := FakeROM(FakeROMOptions{Title: "AB  "})
	rom[0x134+5] = 'Z' // dopo lo zero: ignorato
	rom[0x14D] = headerChecksum(rom)
	if h, err := ParseHeader(rom); err != nil || h.Title != "AB" {
		t.Fatalf("%q %v", h.Title, err)
	}
	for in, want := range map[string]string{
		"Tetris DX (homebrew).gb": "Tetris DX (homeb",
		"/percorso/al/gioco.GB":   "gioco",
		"città perduta.gbc":       "citt perduta",
		"àèì.gb":                  "Senza titolo",
		"a\x1fb.gb":               "ab",
	} {
		if got := TitleFromFile(in); got != want {
			t.Errorf("TitleFromFile(%q) = %q, atteso %q", in, got, want)
		}
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/gbgames/ -run 'TestHeader|TestParse|TestTitles' -v`
Expected: FAIL (`undefined: FakeROM`, `undefined: ParseHeader` …)

- [ ] **Step 3: Write minimal implementation**

`service/internal/gbgames/header.go`:

```go
// Package gbgames tiene le cartucce del Game Boy dell'utente: controllo dell'intestazione, cartella
// dei giochi, salvataggi. Solo file forniti dall'utente (cartucce proprie, homebrew): il servizio non
// scarica mai ROM da internet.
package gbgames

import (
	"fmt"
	"path/filepath"
	"strings"
)

const (
	MinROM   = 32 << 10 // la ROM più piccola: 2 banchi da 16 KB
	MaxROM   = 2 << 20  // area gbrom della scheda (contratto, «B: memoria»)
	MaxTitle = 16
)

// nintendoLogo: i 48 byte a 0x104–0x133 che la ROM di avvio confronta (Pan Docs, «The Cartridge
// Header»). Senza, la console vera non avvia la cartuccia: qui serve a riconoscere una ROM del Game Boy.
var nintendoLogo = [48]byte{
	0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
	0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
	0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
}

type cartInfo struct {
	mapper  string
	battery bool
}

// carts: tipi di cartuccia (0x147) che l'emulatore della scheda (Peanut-GB) sa gestire.
var carts = map[byte]cartInfo{
	0x00: {"ROM", false}, 0x01: {"MBC1", false}, 0x02: {"MBC1", false}, 0x03: {"MBC1", true},
	0x05: {"MBC2", false}, 0x06: {"MBC2", true}, 0x08: {"ROM", false}, 0x09: {"ROM", true},
	0x0F: {"MBC3", true}, 0x10: {"MBC3", true}, 0x11: {"MBC3", false}, 0x12: {"MBC3", false}, 0x13: {"MBC3", true},
	0x19: {"MBC5", false}, 0x1A: {"MBC5", false}, 0x1B: {"MBC5", true},
	0x1C: {"MBC5", false}, 0x1D: {"MBC5", false}, 0x1E: {"MBC5", true},
}

// ramSizes: RAM della cartuccia (0x149) in byte.
var ramSizes = map[byte]int{0: 0, 1: 2 << 10, 2: 8 << 10, 3: 32 << 10, 4: 128 << 10, 5: 64 << 10}

// Header: quello che serve sapere di una cartuccia.
type Header struct {
	Title    string `json:"title"`    // dall'intestazione, ASCII stampabile, ≤ 16 ("" se vuoto)
	CartType byte   `json:"cartType"` // 0x147
	Mapper   string `json:"mapper"`   // ROM, MBC1, MBC2, MBC3, MBC5
	Battery  bool   `json:"battery"`  // ha RAM con batteria: salvataggi
	ROMSize  int    `json:"romSize"`
	RAMSize  int    `json:"ramSize"`
	CGB      bool   `json:"cgb"` // compatibile Game Boy Color (0x80): si gioca in modalità classica
}

// InvalidError: file rifiutato, con il motivo in italiano.
type InvalidError struct{ Msg string }

func (e *InvalidError) Error() string { return e.Msg }

func invalid(format string, a ...any) error { return &InvalidError{Msg: fmt.Sprintf(format, a...)} }

var (
	ErrTooLarge = &InvalidError{Msg: "ROM più grande di 2 MB: non entra nella scheda"}
	ErrCGBOnly  = &InvalidError{Msg: "gioco solo per Game Boy Color: non supportato (si gioca solo il Game Boy classico)"}
)

// ParseHeader controlla l'intestazione (0x100–0x14F) e la dimensione del file.
func ParseHeader(rom []byte) (Header, error) {
	switch {
	case len(rom) > MaxROM:
		return Header{}, ErrTooLarge
	case len(rom) < MinROM:
		return Header{}, invalid("file troppo corto per una cartuccia del Game Boy (%d byte)", len(rom))
	case [48]byte(rom[0x104:0x134]) != nintendoLogo:
		return Header{}, invalid("non è una ROM del Game Boy: manca il logo di avvio nell'intestazione")
	}
	if sum := headerChecksum(rom); sum != rom[0x14D] {
		return Header{}, invalid("intestazione rovinata: checksum 0x%02x invece di 0x%02x", rom[0x14D], sum)
	}
	cgb := rom[0x143]
	if cgb == 0xC0 {
		return Header{}, ErrCGBOnly
	}
	ci, ok := carts[rom[0x147]]
	if !ok {
		return Header{}, invalid("cartuccia non supportata (tipo 0x%02x): vanno ROM semplici, MBC1, MBC2, MBC3 e MBC5", rom[0x147])
	}
	code := rom[0x148]
	if code > 6 {
		return Header{}, invalid("dimensione della ROM nell'intestazione non supportata (codice 0x%02x)", code)
	}
	if want := MinROM << code; len(rom) != want {
		return Header{}, invalid("il file è di %d byte ma l'intestazione dice %d: ROM incompleta o modificata", len(rom), want)
	}
	ram, ok := ramSizes[rom[0x149]]
	if !ok {
		return Header{}, invalid("RAM della cartuccia non valida nell'intestazione (codice 0x%02x)", rom[0x149])
	}
	if ci.mapper == "MBC2" {
		ram = 512 // 512 mezzi byte dentro il chip
	}
	end := 0x144
	if cgb == 0x80 {
		end = 0x143 // con il byte Color il titolo è di 15
	}
	return Header{Title: cleanTitle(rom[0x134:end]), CartType: rom[0x147], Mapper: ci.mapper, Battery: ci.battery,
		ROMSize: len(rom), RAMSize: ram, CGB: cgb == 0x80}, nil
}

// headerChecksum: x = x − rom[i] − 1 per 0x134–0x14C (come la ROM di avvio).
func headerChecksum(rom []byte) byte {
	var x byte
	for i := 0x134; i <= 0x14C; i++ {
		x = x - rom[i] - 1
	}
	return x
}

// globalChecksum: somma di tutti i byte tranne 0x14E–0x14F (la console non la controlla).
func globalChecksum(rom []byte) uint16 {
	var s uint16
	for i, b := range rom {
		if i != 0x14E && i != 0x14F {
			s += uint16(b)
		}
	}
	return s
}

// cleanTitle: fino al primo byte non stampabile, spazi in coda tolti.
func cleanTitle(b []byte) string {
	var sb strings.Builder
	for _, c := range b {
		if c < 0x20 || c > 0x7E {
			break
		}
		sb.WriteByte(c)
	}
	return strings.TrimRight(sb.String(), " ")
}

// TitleFromFile: titolo di riserva dal nome del file (solo ASCII stampabile, ≤ 16).
func TitleFromFile(name string) string {
	base := filepath.Base(strings.ReplaceAll(name, "\\", "/"))
	base = strings.TrimSuffix(base, filepath.Ext(base))
	var sb strings.Builder
	for _, r := range base {
		if r >= 0x20 && r <= 0x7E && sb.Len() < MaxTitle {
			sb.WriteRune(r)
		}
	}
	t := strings.TrimSpace(sb.String())
	if t == "" {
		return "Senza titolo"
	}
	return t
}
```

`service/internal/gbgames/fakerom.go`:

```go
package gbgames

// FakeROMOptions: ROM sintetica per le prove. Nessun codice di gioco: intestazione valida e byte di
// riempimento (Fill diverso = SHA-256 diverso).
type FakeROMOptions struct {
	Title    string // ≤ 16 (≤ 15 con CGB ≠ 0)
	CartType byte   // 0x147
	SizeCode byte   // 0x148: dimensione = 32 KB << SizeCode
	RAMCode  byte   // 0x149
	CGB      byte   // 0x143: 0x00, 0x80 o 0xC0
	Fill     byte
}

// FakeROM costruisce la ROM con il checksum dell'intestazione giusto (e quello globale, per realismo).
// Con SizeCode > 6 la ROM è più grande di MaxROM: serve a provare il rifiuto.
func FakeROM(o FakeROMOptions) []byte {
	n := MinROM << min(o.SizeCode, 7)
	rom := make([]byte, n)
	for i := range rom {
		rom[i] = o.Fill
	}
	copy(rom[0x100:], []byte{0x00, 0xC3, 0x50, 0x01}) // nop; jp 0x0150
	copy(rom[0x104:], nintendoLogo[:])
	for i := 0x134; i < 0x150; i++ {
		rom[i] = 0
	}
	end := 0x144
	if o.CGB != 0 {
		end = 0x143
	}
	copy(rom[0x134:end], o.Title)
	if o.CGB != 0 {
		rom[0x143] = o.CGB
	}
	rom[0x144], rom[0x145] = '0', '0'
	rom[0x147], rom[0x148], rom[0x149] = o.CartType, o.SizeCode, o.RAMCode
	rom[0x14A], rom[0x14B] = 0x01, 0x33
	rom[0x14D] = headerChecksum(rom)
	g := globalChecksum(rom)
	rom[0x14E], rom[0x14F] = byte(g>>8), byte(g)
	return rom
}
```

Nota: con `SizeCode: 0x52` (caso «codice dimensione strano») `min(o.SizeCode, 7)` dà 4 MB e il test la taglia a `MinROM`; il controllo del codice viene prima di quello della lunghezza.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/gbgames/ -v`
Expected: PASS (4 test)

- [ ] **Step 5: Commit**

```bash
git add service/internal/gbgames/header.go service/internal/gbgames/fakerom.go service/internal/gbgames/header_test.go
git commit -m "gbgames: intestazione della cartuccia e ROM sintetiche per le prove"
```

---

### Task 2: cartella dei giochi e `index.json`

**Files:**
- Create: `service/internal/gbgames/library.go`
- Test: `service/internal/gbgames/library_test.go`

**Interfaces:**
- Consumes: `ParseHeader`, `TitleFromFile`, `InvalidError`, `FakeROM` (task 1).
- Produces:
  - `const IndexFile = "index.json"`, `const IndexFormat = "calamaio-gb-games"`
  - `var ErrNotFound = errors.New("gioco sconosciuto")`
  - `type Game struct { ID, Title string; Size int64; SHA256, File string; Added int64; Header Header }` (tag JSON `id, title, size, sha256, file, added, header`)
  - `type Library struct { Dir string; Now func() time.Time; Problems []string; … }`
  - `func Open(dir string) (*Library, error)`
  - `func (l *Library) List() []Game` (ordine: titolo senza maiuscole, poi id)
  - `func (l *Library) Get(id string) (Game, bool)`
  - `func (l *Library) Add(name string, rom []byte) (g Game, existed bool, err error)`
  - `func (l *Library) Remove(id string) error`
  - `func (l *Library) ReadROM(id string) ([]byte, error)`
  - `func (l *Library) Refresh() bool`, `func (l *Library) Rev() int64`
  - `func ValidID(s string) bool`, `func IDOf(rom []byte) (id, sha string)`
  - `func writeAtomic(path string, data []byte, perm os.FileMode) error` (non esportata, la usa il task 3)

- [ ] **Step 1: Write the failing test**

`service/internal/gbgames/library_test.go`:

```go
package gbgames

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func openTemp(t *testing.T) *Library {
	t.Helper()
	l, err := Open(filepath.Join(t.TempDir(), "games"))
	if err != nil {
		t.Fatal(err)
	}
	l.Now = func() time.Time { return time.UnixMilli(1791748800000) }
	return l
}

func TestAddListRemove(t *testing.T) {
	l := openTemp(t)
	rom := FakeROM(FakeROMOptions{Title: "PUZZLE", CartType: 0x03, SizeCode: 1, RAMCode: 2})
	g, existed, err := l.Add("Puzzle.gb", rom)
	if err != nil || existed {
		t.Fatalf("%v %v", err, existed)
	}
	id, sha := IDOf(rom)
	if g.ID != id || len(id) != 16 || g.SHA256 != sha || g.Title != "PUZZLE" || g.Size != 64<<10 ||
		g.File != "Puzzle.gb" || g.Added != 1791748800000 || !g.Header.Battery {
		t.Fatalf("gioco %+v", g)
	}
	st, err := os.Stat(filepath.Join(l.Dir, id+".gb"))
	if err != nil || st.Mode().Perm() != 0o600 {
		t.Fatalf("file della ROM: %v %v", st, err)
	}
	b, err := l.ReadROM(id)
	if err != nil || string(b) != string(rom) {
		t.Fatalf("ReadROM: %v", err)
	}
	// senza titolo nell'intestazione: dal nome del file
	g2, _, err := l.Add("aaa homebrew.gbc", FakeROM(FakeROMOptions{Fill: 7}))
	if err != nil || g2.Title != "aaa homebrew" {
		t.Fatalf("%+v %v", g2, err)
	}
	if list := l.List(); len(list) != 2 || list[0].ID != g2.ID || list[1].ID != id {
		t.Fatalf("ordine per titolo: %+v", list)
	}
	// riaperta: stesso elenco
	l2, err := Open(l.Dir)
	if err != nil || len(l2.List()) != 2 {
		t.Fatalf("riapertura: %v %d", err, len(l2.List()))
	}
	if err := l.Remove(id); err != nil {
		t.Fatal(err)
	}
	if _, ok := l.Get(id); ok {
		t.Fatal("ancora nell'elenco")
	}
	if _, err := os.Stat(filepath.Join(l.Dir, id+".gb")); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("file ancora lì: %v", err)
	}
	if err := l.Remove(id); !errors.Is(err, ErrNotFound) {
		t.Fatalf("seconda rimozione: %v", err)
	}
}

func TestAddRejects(t *testing.T) {
	l := openTemp(t)
	if _, _, err := l.Add("gioco.zip", FakeROM(FakeROMOptions{Title: "X"})); err == nil || !strings.Contains(err.Error(), ".gb") {
		t.Fatalf("estensione: %v", err)
	}
	if _, _, err := l.Add("color.gbc", FakeROM(FakeROMOptions{Title: "C", CGB: 0xC0})); !errors.Is(err, ErrCGBOnly) {
		t.Fatalf("solo Color: %v", err)
	}
	if len(l.List()) != 0 {
		t.Fatal("aggiunto un file rifiutato")
	}
	if roms, _ := filepath.Glob(filepath.Join(l.Dir, "*.gb")); len(roms) != 0 {
		t.Fatalf("ROM rimaste: %v", roms)
	}
}

// Review Focus 4: stessa cartuccia con un altro nome → un gioco solo.
func TestAddDuplicate(t *testing.T) {
	l := openTemp(t)
	rom := FakeROM(FakeROMOptions{Title: "DOPPIO"})
	a, _, _ := l.Add("uno.gb", rom)
	b, existed, err := l.Add("due.gb", rom)
	if err != nil || !existed || b.ID != a.ID || b.File != "uno.gb" || len(l.List()) != 1 {
		t.Fatalf("%+v %v %v", b, existed, err)
	}
}

// Review Focus 4: ROM cambiata sul disco dopo il caricamento → non si legge.
func TestReadROMTampered(t *testing.T) {
	l := openTemp(t)
	g, _, _ := l.Add("x.gb", FakeROM(FakeROMOptions{Title: "X"}))
	p := filepath.Join(l.Dir, g.ID+".gb")
	b, _ := os.ReadFile(p)
	b[0x200] ^= 1
	os.WriteFile(p, b, 0o600)
	if _, err := l.ReadROM(g.ID); err == nil || !strings.Contains(err.Error(), "cambiata sul disco") {
		t.Fatalf("ROM alterata letta: %v", err)
	}
}

// Review Focus 3: id malformati → mai un file.
func TestBadIDs(t *testing.T) {
	l := openTemp(t)
	for _, id := range []string{"", "../index.json00", "ABCDEF0123456789", "0123456789abcde", "0123456789abcdef0", "0123456789abcdeg"} {
		if ValidID(id) {
			t.Errorf("ValidID(%q)", id)
		}
		if _, err := l.ReadROM(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("ReadROM(%q): %v", id, err)
		}
		if err := l.Remove(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("Remove(%q): %v", id, err)
		}
	}
	if !ValidID("0123456789abcdef") {
		t.Error("id buono rifiutato")
	}
}

// index.json rovinato: messo da parte, elenco ricostruito dai file <id>.gb.
func TestIndexRebuilt(t *testing.T) {
	l := openTemp(t)
	g, _, _ := l.Add("x.gb", FakeROM(FakeROMOptions{Title: "SALVO"}))
	os.WriteFile(filepath.Join(l.Dir, IndexFile), []byte("{rotto"), 0o600)
	os.WriteFile(filepath.Join(l.Dir, "0000000000000000.gb"), []byte("non una rom"), 0o600)
	l2, err := Open(l.Dir)
	if err != nil {
		t.Fatal(err)
	}
	if got, ok := l2.Get(g.ID); !ok || got.Title != "SALVO" {
		t.Fatalf("ricostruito: %+v %v", got, ok)
	}
	if len(l2.List()) != 1 || len(l2.Problems) != 2 {
		t.Fatalf("problemi: %v", l2.Problems)
	}
	if m, _ := filepath.Glob(filepath.Join(l.Dir, IndexFile+".rotto-*")); len(m) != 1 {
		t.Fatalf("copia del file rotto: %v", m)
	}
}

// Una modifica fatta da un altro processo (CLI col servizio spento) si vede con Refresh.
func TestRefresh(t *testing.T) {
	l := openTemp(t)
	if l.Refresh() {
		t.Fatal("niente è cambiato")
	}
	other, _ := Open(l.Dir)
	other.Add("y.gb", FakeROM(FakeROMOptions{Title: "NUOVO"}))
	rev := l.Rev()
	if !l.Refresh() || len(l.List()) != 1 || l.Rev() == rev {
		t.Fatalf("Refresh non vede il gioco nuovo: %d", len(l.List()))
	}
	if l.Refresh() {
		t.Fatal("seconda Refresh senza cambi")
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/gbgames/ -run 'TestAdd|TestRead|TestBad|TestIndex|TestRefresh' -v`
Expected: FAIL (`undefined: Open`, `undefined: IDOf` …)

- [ ] **Step 3: Write minimal implementation**

`service/internal/gbgames/library.go`:

```go
package gbgames

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode/utf8"
)

const (
	IndexFile   = "index.json"
	IndexFormat = "calamaio-gb-games"
)

var ErrNotFound = errors.New("gioco sconosciuto")

// Game: una cartuccia della cartella.
type Game struct {
	ID     string `json:"id"`     // primi 16 esadecimali dello SHA-256
	Title  string `json:"title"`  // intestazione, o nome del file
	Size   int64  `json:"size"`   // byte
	SHA256 string `json:"sha256"` // 64 cifre
	File   string `json:"file"`   // nome del file caricato
	Added  int64  `json:"added"`  // ms epoch
	Header Header `json:"header"`
}

type indexFile struct {
	Format  string `json:"format"`
	Version int    `json:"version"`
	Games   []Game `json:"games"`
}

// Library: <dir>/<id>.gb, <dir>/index.json, <dir>/saves/<id>/ (saves.go).
type Library struct {
	Dir      string
	Now      func() time.Time
	Problems []string // file messi da parte o scartati all'ultima lettura

	mu    sync.Mutex
	games map[string]Game
	sum   [32]byte // impronta dell'ultimo index.json letto o scritto
	rev   int64
}

// ValidID: 16 cifre esadecimali minuscole (niente percorsi).
func ValidID(s string) bool {
	if len(s) != 16 {
		return false
	}
	for _, c := range s {
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f') {
			return false
		}
	}
	return true
}

// IDOf: id e SHA-256 completo della ROM.
func IDOf(rom []byte) (id, sha string) {
	s := sha256.Sum256(rom)
	sha = hex.EncodeToString(s[:])
	return sha[:16], sha
}

// Open apre (e crea) la cartella.
func Open(dir string) (*Library, error) {
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return nil, fmt.Errorf("cartella dei giochi: %w", err)
	}
	l := &Library{Dir: dir, Now: time.Now}
	l.mu.Lock()
	defer l.mu.Unlock()
	if err := l.load(); err != nil {
		return nil, err
	}
	return l, nil
}

func (l *Library) romPath(id string) string { return filepath.Join(l.Dir, id+".gb") }

// load legge index.json; se manca o è illeggibile lo ricostruisce dai file <id>.gb. Con l.mu.
func (l *Library) load() error {
	l.Problems = nil
	p := filepath.Join(l.Dir, IndexFile)
	b, err := os.ReadFile(p)
	var idx indexFile
	switch {
	case errors.Is(err, os.ErrNotExist):
		return l.rebuild()
	case err != nil:
		return fmt.Errorf("indice dei giochi: %w", err)
	case json.Unmarshal(b, &idx) != nil || idx.Format != IndexFormat:
		bad := p + ".rotto-" + l.Now().Format("20060102-150405")
		_ = os.Rename(p, bad)
		l.Problems = append(l.Problems, "index.json illeggibile: messo da parte come "+filepath.Base(bad))
		return l.rebuild()
	}
	l.games = map[string]Game{}
	for _, g := range idx.Games {
		if !ValidID(g.ID) {
			l.Problems = append(l.Problems, fmt.Sprintf("id non valido nell'indice: %q", g.ID))
			continue
		}
		if _, err := os.Stat(l.romPath(g.ID)); err != nil {
			l.Problems = append(l.Problems, g.ID+".gb mancante: tolto dall'elenco")
			continue
		}
		l.games[g.ID] = g
	}
	l.sum = sha256.Sum256(b)
	l.rev++
	return nil
}

// rebuild: elenco dai file <id>.gb validi, poi index.json nuovo. Con l.mu.
func (l *Library) rebuild() error {
	l.games = map[string]Game{}
	ents, err := os.ReadDir(l.Dir)
	if err != nil {
		return fmt.Errorf("cartella dei giochi: %w", err)
	}
	for _, e := range ents {
		name := e.Name()
		id := strings.TrimSuffix(name, ".gb")
		if e.IsDir() || !strings.HasSuffix(name, ".gb") || !ValidID(id) {
			continue
		}
		rom, err := os.ReadFile(filepath.Join(l.Dir, name))
		if err != nil {
			l.Problems = append(l.Problems, name+": "+err.Error())
			continue
		}
		h, err := ParseHeader(rom)
		got, sha := IDOf(rom)
		if err != nil || got != id {
			l.Problems = append(l.Problems, name+": non è una cartuccia valida o il nome non è il suo id")
			continue
		}
		title := h.Title
		if title == "" {
			title = id
		}
		var added int64
		if st, err := e.Info(); err == nil {
			added = st.ModTime().UnixMilli()
		}
		l.games[id] = Game{ID: id, Title: title, Size: int64(len(rom)), SHA256: sha, File: name, Added: added, Header: h}
	}
	return l.writeIndex()
}

// writeIndex scrive index.json (ordinato per id) in modo atomico. Con l.mu.
func (l *Library) writeIndex() error {
	idx := indexFile{Format: IndexFormat, Version: 1, Games: make([]Game, 0, len(l.games))}
	for _, g := range l.games {
		idx.Games = append(idx.Games, g)
	}
	sort.Slice(idx.Games, func(i, j int) bool { return idx.Games[i].ID < idx.Games[j].ID })
	b, err := json.MarshalIndent(idx, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')
	if err := writeAtomic(filepath.Join(l.Dir, IndexFile), b, 0o600); err != nil {
		return fmt.Errorf("indice dei giochi: %w", err)
	}
	l.sum = sha256.Sum256(b)
	l.rev++
	return nil
}

// writeAtomic: file temporaneo nella stessa cartella, sync, rename.
func writeAtomic(path string, data []byte, perm os.FileMode) error {
	f, err := os.CreateTemp(filepath.Dir(path), ".tmp-*")
	if err != nil {
		return err
	}
	tmp := f.Name()
	defer os.Remove(tmp) // dopo il rename non c'è più: nessun effetto
	if _, err := f.Write(data); err != nil {
		f.Close()
		return err
	}
	if err := f.Sync(); err != nil {
		f.Close()
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	if err := os.Chmod(tmp, perm); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// clip taglia s a max byte senza spezzare un carattere UTF-8.
func clip(s string, max int) string {
	s = strings.ToValidUTF8(s, "")
	for len(s) > max {
		_, n := utf8.DecodeLastRuneInString(s)
		s = s[:len(s)-n]
	}
	return s
}

// List: tutti i giochi, per titolo (senza maiuscole) e poi per id.
func (l *Library) List() []Game {
	l.mu.Lock()
	out := make([]Game, 0, len(l.games))
	for _, g := range l.games {
		out = append(out, g)
	}
	l.mu.Unlock()
	sort.Slice(out, func(i, j int) bool {
		a, b := strings.ToLower(out[i].Title), strings.ToLower(out[j].Title)
		if a != b {
			return a < b
		}
		return out[i].ID < out[j].ID
	})
	return out
}

func (l *Library) Get(id string) (Game, bool) {
	l.mu.Lock()
	defer l.mu.Unlock()
	g, ok := l.games[id]
	return g, ok
}

// Add controlla e aggiunge una cartuccia. existed: c'era già (stessa ROM, anche con un altro nome).
func (l *Library) Add(name string, rom []byte) (Game, bool, error) {
	if ext := strings.ToLower(filepath.Ext(name)); ext != ".gb" && ext != ".gbc" {
		return Game{}, false, invalid("serve il file di una cartuccia del Game Boy (.gb)")
	}
	h, err := ParseHeader(rom)
	if err != nil {
		return Game{}, false, err
	}
	id, sha := IDOf(rom)
	l.mu.Lock()
	defer l.mu.Unlock()
	if g, ok := l.games[id]; ok {
		return g, true, nil
	}
	title := h.Title
	if title == "" {
		title = TitleFromFile(name)
	}
	g := Game{ID: id, Title: title, Size: int64(len(rom)), SHA256: sha,
		File: clip(filepath.Base(strings.ReplaceAll(name, "\\", "/")), 128), Added: l.Now().UnixMilli(), Header: h}
	if err := writeAtomic(l.romPath(id), rom, 0o600); err != nil {
		return Game{}, false, fmt.Errorf("ROM non salvata: %w", err)
	}
	l.games[id] = g
	if err := l.writeIndex(); err != nil {
		delete(l.games, id)
		_ = os.Remove(l.romPath(id))
		return Game{}, false, err
	}
	return g, false, nil
}

// Remove toglie la cartuccia (i salvataggi restano: si ritrovano ricaricando la stessa ROM).
func (l *Library) Remove(id string) error {
	if !ValidID(id) {
		return ErrNotFound
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	g, ok := l.games[id]
	if !ok {
		return ErrNotFound
	}
	delete(l.games, id)
	if err := l.writeIndex(); err != nil {
		l.games[id] = g
		return err
	}
	if err := os.Remove(l.romPath(id)); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	return nil
}

// ReadROM legge la ROM e controlla che sia ancora quella caricata.
func (l *Library) ReadROM(id string) ([]byte, error) {
	if !ValidID(id) {
		return nil, ErrNotFound
	}
	g, ok := l.Get(id)
	if !ok {
		return nil, ErrNotFound
	}
	b, err := os.ReadFile(l.romPath(id))
	if err != nil {
		return nil, err
	}
	if _, sha := IDOf(b); sha != g.SHA256 {
		return nil, fmt.Errorf("la ROM %s è cambiata sul disco: toglila e ricaricala", id)
	}
	return b, nil
}

// Refresh rilegge index.json se un altro processo l'ha cambiato; true = elenco cambiato.
func (l *Library) Refresh() bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	b, err := os.ReadFile(filepath.Join(l.Dir, IndexFile))
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return false
	}
	if err == nil && sha256.Sum256(b) == l.sum {
		return false
	}
	if l.load() != nil {
		return false
	}
	return true
}

// Rev cambia a ogni lettura o scrittura dell'elenco (per la webapp).
func (l *Library) Rev() int64 {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.rev
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/gbgames/ -v`
Expected: PASS (tutti, anche quelli del task 1)

- [ ] **Step 5: Commit**

```bash
git add service/internal/gbgames/library.go service/internal/gbgames/library_test.go
git commit -m "gbgames: cartella dei giochi con index.json, aggiunta, rimozione e lettura verificata"
```

---

### Task 3: salvataggi a 3 copie e ripristino

**Files:**
- Create: `service/internal/gbgames/saves.go`
- Test: `service/internal/gbgames/saves_test.go`

**Interfaces:**
- Consumes: `Library`, `ValidID`, `ErrNotFound`, `writeAtomic`, `invalid` (task 1–2).
- Produces:
  - `const SavesKept = 3`, `const MaxSave = 128<<10 + 64`
  - `var ErrNoSave = errors.New("salvataggio sconosciuto")`
  - `type Save struct { N int; Name string; Size int64; CRC32 uint32; At int64 }` (tag JSON `n, name, size, crc32, at`)
  - `func (l *Library) PutSave(id string, data []byte) (s Save, fresh bool, err error)`
  - `func (l *Library) HasSave(id string, size int64, crc uint32) bool`
  - `func (l *Library) Saves(id string) ([]Save, error)` (la più recente per prima, `N` = 1…)
  - `func (l *Library) ReadSave(id string, n int) (Save, []byte, error)`
  - `func (l *Library) SetRestore(id string, n int) (Save, error)`
  - `func (l *Library) PendingRestore(id string) (Save, []byte, bool)`
  - `func (l *Library) ClearRestore(id string, crc uint32)`

- [ ] **Step 1: Write the failing test**

`service/internal/gbgames/saves_test.go`:

```go
package gbgames

import (
	"bytes"
	"errors"
	"hash/crc32"
	"os"
	"path/filepath"
	"testing"
	"time"
)

const saveID = "0123456789abcdef"

func ticking(l *Library) {
	t := int64(1791748800000)
	l.Now = func() time.Time { t += 1000; return time.UnixMilli(t) }
}

func TestSavesKeepThree(t *testing.T) {
	l := openTemp(t)
	ticking(l)
	for i := byte(1); i <= 5; i++ {
		data := bytes.Repeat([]byte{i}, 8192)
		s, fresh, err := l.PutSave(saveID, data)
		if err != nil || !fresh || s.N != 1 || s.CRC32 != crc32.ChecksumIEEE(data) || s.Size != 8192 {
			t.Fatalf("copia %d: %+v %v %v", i, s, fresh, err)
		}
	}
	list, err := l.Saves(saveID)
	if err != nil || len(list) != SavesKept {
		t.Fatalf("copie: %+v %v", list, err)
	}
	for i, s := range list {
		_, data, err := l.ReadSave(saveID, s.N)
		if err != nil || s.N != i+1 || data[0] != byte(5-i) {
			t.Fatalf("copia %d: %+v %v", i+1, s, err)
		}
	}
	// lo stesso salvataggio di prima non fa una copia nuova
	s, fresh, err := l.PutSave(saveID, bytes.Repeat([]byte{5}, 8192))
	if err != nil || fresh || s.N != 1 {
		t.Fatalf("doppione: %+v %v %v", s, fresh, err)
	}
	if !l.HasSave(saveID, 8192, crc32.ChecksumIEEE(bytes.Repeat([]byte{3}, 8192))) || l.HasSave(saveID, 8192, 1) {
		t.Fatal("HasSave")
	}
	if fi, _ := os.Stat(filepath.Join(l.Dir, "saves", saveID)); fi.Mode().Perm() != 0o700 {
		t.Fatalf("permessi della cartella: %v", fi.Mode())
	}
}

func TestSaveRejects(t *testing.T) {
	l := openTemp(t)
	if _, _, err := l.PutSave("../../etc", []byte{1}); !errors.Is(err, ErrNotFound) {
		t.Fatalf("id: %v", err)
	}
	if _, _, err := l.PutSave(saveID, nil); err == nil {
		t.Fatal("vuoto accettato")
	}
	if _, _, err := l.PutSave(saveID, make([]byte, MaxSave+1)); err == nil {
		t.Fatal("troppo grande accettato")
	}
	if _, _, err := l.ReadSave(saveID, 1); !errors.Is(err, ErrNoSave) {
		t.Fatalf("nessuna copia: %v", err)
	}
	if list, err := l.Saves(saveID); err != nil || len(list) != 0 {
		t.Fatalf("elenco vuoto: %v %v", list, err)
	}
}

func TestRestore(t *testing.T) {
	l := openTemp(t)
	ticking(l)
	l.PutSave(saveID, []byte("vecchio"))
	l.PutSave(saveID, []byte("nuovo"))
	if _, _, ok := l.PendingRestore(saveID); ok {
		t.Fatal("ripristino senza richiesta")
	}
	if _, err := l.SetRestore(saveID, 7); !errors.Is(err, ErrNoSave) {
		t.Fatalf("copia inesistente: %v", err)
	}
	s, err := l.SetRestore(saveID, 2)
	if err != nil || s.N != 2 {
		t.Fatal(err)
	}
	ps, data, ok := l.PendingRestore(saveID)
	if !ok || string(data) != "vecchio" || ps.CRC32 != s.CRC32 {
		t.Fatalf("in attesa: %+v %q %v", ps, data, ok)
	}
	l.ClearRestore(saveID, s.CRC32+1) // crc di un altro: resta
	if _, _, ok := l.PendingRestore(saveID); !ok {
		t.Fatal("tolto con il crc sbagliato")
	}
	l.ClearRestore(saveID, s.CRC32)
	if _, _, ok := l.PendingRestore(saveID); ok {
		t.Fatal("ancora in attesa")
	}
}

// Review Focus 2: la copia scelta per il ripristino non sparisce con la rotazione.
func TestRestoreSurvivesPrune(t *testing.T) {
	l := openTemp(t)
	ticking(l)
	for _, s := range []string{"a", "b", "c"} {
		l.PutSave(saveID, []byte(s))
	}
	chosen, _ := l.SetRestore(saveID, 3) // «a»
	l.PutSave(saveID, []byte("d"))
	l.PutSave(saveID, []byte("e"))
	_, data, ok := l.PendingRestore(saveID)
	if !ok || string(data) != "a" {
		t.Fatalf("copia scelta persa: %q %v", data, ok)
	}
	if list, _ := l.Saves(saveID); len(list) != SavesKept+1 {
		t.Fatalf("attese 4 copie finché il ripristino non è consegnato: %d", len(list))
	}
	l.ClearRestore(saveID, chosen.CRC32)
	if list, _ := l.Saves(saveID); len(list) != SavesKept {
		t.Fatalf("dopo la consegna: %d copie", len(list))
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/gbgames/ -run 'TestSave|TestRestore' -v`
Expected: FAIL (`l.PutSave undefined` …)

- [ ] **Step 3: Write minimal implementation**

`service/internal/gbgames/saves.go`:

```go
package gbgames

import (
	"errors"
	"fmt"
	"hash/crc32"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

const (
	SavesKept   = 3
	MaxSave     = 128<<10 + 64 // RAM della cartuccia più grande (128 KB) + orologio MBC3
	restoreFile = "ripristino" // nome della copia scelta per il prossimo avvio
)

var ErrNoSave = errors.New("salvataggio sconosciuto")

// Save: una copia della RAM della cartuccia. N = 1 è la più recente.
type Save struct {
	N     int    `json:"n"`
	Name  string `json:"name"`
	Size  int64  `json:"size"`
	CRC32 uint32 `json:"crc32"`
	At    int64  `json:"at"` // ms epoch dell'arrivo al PC
}

func (l *Library) saveDir(id string) string { return filepath.Join(l.Dir, "saves", id) }

// saveName: <ms a 13 cifre>-<crc a 8>.sav, così l'ordine dei nomi è l'ordine di arrivo.
func saveName(at int64, crc uint32) string { return fmt.Sprintf("%013d-%08x.sav", at, crc) }

func parseSaveName(n string) (at int64, crc uint32, ok bool) {
	if len(n) != 26 || n[13] != '-' || !strings.HasSuffix(n, ".sav") {
		return 0, 0, false
	}
	a, err1 := strconv.ParseInt(n[:13], 10, 64)
	c, err2 := strconv.ParseUint(n[14:22], 16, 32)
	if err1 != nil || err2 != nil {
		return 0, 0, false
	}
	return a, uint32(c), true
}

// listSaves: copie, la più recente per prima. Con l.mu.
func (l *Library) listSaves(id string) ([]Save, error) {
	ents, err := os.ReadDir(l.saveDir(id))
	if errors.Is(err, os.ErrNotExist) {
		return []Save{}, nil
	}
	if err != nil {
		return nil, err
	}
	out := []Save{}
	for _, e := range ents {
		at, crc, ok := parseSaveName(e.Name())
		if !ok || e.IsDir() {
			continue
		}
		fi, err := e.Info()
		if err != nil {
			continue
		}
		out = append(out, Save{Name: e.Name(), Size: fi.Size(), CRC32: crc, At: at})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name > out[j].Name })
	for i := range out {
		out[i].N = i + 1
	}
	return out, nil
}

// restoreName: la copia scelta per il ripristino ("" se nessuna). Con l.mu.
func (l *Library) restoreName(id string) string {
	b, err := os.ReadFile(filepath.Join(l.saveDir(id), restoreFile))
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(b))
}

// prune lascia le ultime SavesKept copie più quella scelta per il ripristino. Con l.mu.
func (l *Library) prune(id string) {
	list, err := l.listSaves(id)
	if err != nil {
		return
	}
	keep := l.restoreName(id)
	for _, s := range list[min(len(list), SavesKept):] {
		if s.Name != keep {
			_ = os.Remove(filepath.Join(l.saveDir(id), s.Name))
		}
	}
}

// PutSave tiene una copia nuova; fresh = false se uguale alla più recente.
func (l *Library) PutSave(id string, data []byte) (Save, bool, error) {
	if !ValidID(id) {
		return Save{}, false, ErrNotFound
	}
	if len(data) == 0 || len(data) > MaxSave {
		return Save{}, false, invalid("salvataggio di %d byte: atteso tra 1 e %d", len(data), MaxSave)
	}
	crc := crc32.ChecksumIEEE(data)
	l.mu.Lock()
	defer l.mu.Unlock()
	list, err := l.listSaves(id)
	if err != nil {
		return Save{}, false, err
	}
	if len(list) > 0 && list[0].CRC32 == crc && list[0].Size == int64(len(data)) {
		return list[0], false, nil
	}
	if err := os.MkdirAll(l.saveDir(id), 0o700); err != nil {
		return Save{}, false, err
	}
	at := l.Now().UnixMilli()
	if len(list) > 0 && at <= list[0].At {
		at = list[0].At + 1 // nomi sempre crescenti anche con l'orologio indietro
	}
	if err := writeAtomic(filepath.Join(l.saveDir(id), saveName(at, crc)), data, 0o600); err != nil {
		return Save{}, false, err
	}
	l.prune(id)
	list, err = l.listSaves(id)
	if err != nil || len(list) == 0 {
		return Save{}, false, fmt.Errorf("salvataggio non riletto: %v", err)
	}
	return list[0], true, nil
}

// HasSave: c'è già una copia con questa dimensione e questo CRC.
func (l *Library) HasSave(id string, size int64, crc uint32) bool {
	if !ValidID(id) {
		return false
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	list, _ := l.listSaves(id)
	for _, s := range list {
		if s.Size == size && s.CRC32 == crc {
			return true
		}
	}
	return false
}

// Saves: le copie di id (anche se la cartuccia è stata tolta).
func (l *Library) Saves(id string) ([]Save, error) {
	if !ValidID(id) {
		return nil, ErrNotFound
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.listSaves(id)
}

// readSave: copia n e i suoi byte (CRC controllato). Con l.mu.
func (l *Library) readSave(id string, match func(Save) bool) (Save, []byte, error) {
	list, err := l.listSaves(id)
	if err != nil {
		return Save{}, nil, err
	}
	for _, s := range list {
		if !match(s) {
			continue
		}
		b, err := os.ReadFile(filepath.Join(l.saveDir(id), s.Name))
		if err != nil {
			return Save{}, nil, err
		}
		if crc32.ChecksumIEEE(b) != s.CRC32 {
			return Save{}, nil, fmt.Errorf("copia %s rovinata sul disco", s.Name)
		}
		return s, b, nil
	}
	return Save{}, nil, ErrNoSave
}

func (l *Library) ReadSave(id string, n int) (Save, []byte, error) {
	if !ValidID(id) {
		return Save{}, nil, ErrNotFound
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.readSave(id, func(s Save) bool { return s.N == n })
}

// SetRestore sceglie la copia n per il prossimo avvio di id (gb-start con saveToken).
func (l *Library) SetRestore(id string, n int) (Save, error) {
	if !ValidID(id) {
		return Save{}, ErrNotFound
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	s, _, err := l.readSave(id, func(s Save) bool { return s.N == n })
	if err != nil {
		return Save{}, err
	}
	if err := writeAtomic(filepath.Join(l.saveDir(id), restoreFile), []byte(s.Name+"\n"), 0o600); err != nil {
		return Save{}, err
	}
	return s, nil
}

// PendingRestore: la copia scelta e i suoi byte, se c'è ancora.
func (l *Library) PendingRestore(id string) (Save, []byte, bool) {
	if !ValidID(id) {
		return Save{}, nil, false
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	name := l.restoreName(id)
	if name == "" {
		return Save{}, nil, false
	}
	s, b, err := l.readSave(id, func(s Save) bool { return s.Name == name })
	if err != nil {
		return Save{}, nil, false
	}
	return s, b, true
}

// ClearRestore: la scheda ha scaricato la copia con questo CRC; la scelta si chiude e la rotazione
// riprende.
func (l *Library) ClearRestore(id string, crc uint32) {
	if !ValidID(id) {
		return
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	name := l.restoreName(id)
	if _, c, ok := parseSaveName(name); !ok || c != crc {
		return
	}
	_ = os.Remove(filepath.Join(l.saveDir(id), restoreFile))
	l.prune(id)
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/gbgames/ -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add service/internal/gbgames/saves.go service/internal/gbgames/saves_test.go
git commit -m "gbgames: salvataggi con le ultime 3 copie e ripristino al prossimo avvio"
```

---

### Task 4: mac dei messaggi `gb-*`

**Files:**
- Create: `service/internal/m5/gb.go`
- Test: `service/internal/m5/gb_test.go`

**Interfaces:**
- Consumes: `Mac16`, `joined` (esistenti in `service/internal/m5/m5.go`).
- Produces:
  - `type GbListItem struct { ID, Title string; Size int64 }`
  - `func GbListMac(SK []byte, games []GbListItem) []byte` — `HMAC(SK, "gb-list" ␟ id₁ ␟ size₁ ␟ title₁ ␟ id₂ …)`
  - `func GbStartMac(SK []byte, id string, size int64, sha256Hex, token, saveToken string) []byte` — `HMAC(SK, "gb-start" ␟ id ␟ size ␟ sha256 ␟ token ␟ saveToken)` (`saveToken` vuoto se assente)
  - `func GbSaveOkMac(SK []byte, id string, crc uint32) []byte` — `HMAC(SK, "gb-save-ok" ␟ id ␟ crc32)`
  - Interi in decimale senza zeri iniziali; SHA-256 e token in esadecimale minuscolo.

- [ ] **Step 1: Write the failing test**

`service/internal/m5/gb_test.go` (vettori calcolati a parte con Python `hmac`, SK = 32 × 0x02, gli stessi degli esempi del task 5):

```go
package m5

import (
	"bytes"
	"testing"
)

func TestGbMacVectors(t *testing.T) {
	sk := bytes.Repeat([]byte{2}, 32)
	const sha = "3b5f0c9a1d2e4f607c1e9d22a8b04f3165e0d7a9c2b18f4e5d6a7b8c9d0e1f20"
	const tok, stok = "5d2c8e1f0a9b4c3d6e7f8091a2b3c4d5", "9e8d7c6b5a4f3e2d1c0b0a9988776655"
	cases := map[string][2]string{
		"lista": {Hex(GbListMac(sk, []GbListItem{{"3b5f0c9a1d2e4f60", "HOMEBREW DEMO", 32768}, {"a1b2c3d4e5f60718", "Puzzle", 65536}})),
			"f6d53e39660c529c836fafb597a847e8"},
		"lista vuota": {Hex(GbListMac(sk, nil)), "c41604f236b545be2f894c2bb645b6e1"},
		"avvio":       {Hex(GbStartMac(sk, "3b5f0c9a1d2e4f60", 32768, sha, tok, "")), "79de567f8a40b0efdf73f04badd19aa6"},
		"avvio con ripristino": {Hex(GbStartMac(sk, "3b5f0c9a1d2e4f60", 32768, sha, tok, stok)),
			"d57133ff50a083ee56d6005253591fff"},
		"salvataggio": {Hex(GbSaveOkMac(sk, "3b5f0c9a1d2e4f60", 305419896)), "7f690a4c01c28ed6e0f773b54866f6ef"},
	}
	for name, c := range cases {
		if c[0] != c[1] {
			t.Errorf("%s: %s, atteso %s", name, c[0], c[1])
		}
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/m5/ -run TestGbMacVectors -v`
Expected: FAIL (`undefined: GbListMac`)

- [ ] **Step 3: Write minimal implementation**

`service/internal/m5/gb.go`:

```go
package m5

import "strconv"

// Mac dei messaggi dei giochi Game Boy (docs/PROTOCOLLO.md «Giochi»): stessa forma di quelli dell'OTA,
// etichetta e campi di testo separati da 0x1F, HMAC-SHA256 con la chiave di sessione SK troncato a
// 16 byte. Interi in decimale; titoli già ripuliti (ASCII stampabile, mai 0x1F).

// GbListItem: una voce di gb-list, nell'ordine del messaggio.
type GbListItem struct {
	ID    string
	Title string
	Size  int64
}

// GbListMac = HMAC(SK, "gb-list" ␟ id₁ ␟ size₁ ␟ title₁ ␟ id₂ ␟ …).
func GbListMac(SK []byte, games []GbListItem) []byte {
	f := make([]string, 0, 3*len(games))
	for _, g := range games {
		f = append(f, g.ID, strconv.FormatInt(g.Size, 10), g.Title)
	}
	return Mac16(SK, joined("gb-list", f...))
}

// GbStartMac = HMAC(SK, "gb-start" ␟ id ␟ size ␟ sha256 ␟ token ␟ saveToken); saveToken "" se assente.
func GbStartMac(SK []byte, id string, size int64, sha256Hex, token, saveToken string) []byte {
	return Mac16(SK, joined("gb-start", id, strconv.FormatInt(size, 10), sha256Hex, token, saveToken))
}

// GbSaveOkMac = HMAC(SK, "gb-save-ok" ␟ id ␟ crc32), crc32 in decimale.
func GbSaveOkMac(SK []byte, id string, crc uint32) []byte {
	return Mac16(SK, joined("gb-save-ok", id, strconv.FormatUint(uint64(crc), 10)))
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/m5/ -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add service/internal/m5/gb.go service/internal/m5/gb_test.go
git commit -m "m5: mac di gb-list, gb-start e gb-save-ok con vettori fissi"
```

---
### Task 5: protocollo «Giochi» — schema, esempi veri, PROTOCOLLO.md

> Le «Proposte di modifica al contratto» di questo piano sono nel contratto aggiornato il 2026-10-12 (porta 8766, `mac` obbligatori, token di caricamento scelto dalla scheda, Range sullo stesso token, token di 10 minuti): questo task le scrive in PROTOCOLLO.md.

**Files:**
- Modify: `protocol/schema/message.schema.json` (enum `t`, `allOf`, `$defs`)
- Create: `protocol/examples/gb-list.json`, `gb-pick.json`, `gb-start.json`, `gb-start-ripristino.json`, `gb-save.json`, `gb-save-ok.json`, `gb-state.json`, `gb-state-nessuno.json`, `gb-pad.json`
- Modify: `service/internal/display/schema_test.go` (elenco dei tipi in `TestExamplesMatchSchema`, casi in `TestSchemaRejects`)
- Modify: `docs/PROTOCOLLO.md` (sezione nuova «Giochi» prima di «API locale della webapp»)

**Interfaces:**
- Consumes: formule dei mac del task 4.
- Produces: i nomi dei tipi `gb-list`, `gb-pick`, `gb-start`, `gb-save`, `gb-save-ok`, `gb-state`, `gb-pad` nello schema; gli esempi che il task 6 rilegge per controllare i mac.

- [ ] **Step 1: Write the failing test**

In `service/internal/display/schema_test.go`, `TestExamplesMatchSchema`, aggiungi i tipi nuovi all'elenco (l'ultima riga diventa):

```go
		"game", "accessory", "accessory-need", "anchors", "anchors-need", "accessory-wear",
		"gb-list", "gb-pick", "gb-start", "gb-save", "gb-save-ok", "gb-state", "gb-pad"} {
```

In `TestSchemaRejects`, nella mappa dei casi, dopo il blocco `// Giornata di lavoro.` aggiungi:

```go
		// Giochi Game Boy.
		"gb-list senza mac":         `{"t":"gb-list","v":1,"games":[]}`,
		"gb-list id corto":          `{"t":"gb-list","v":1,"games":[{"id":"3b5f0c9a","title":"X","size":32768}],"mac":"` + strings.Repeat("a", 32) + `"}`,
		"gb-list titolo lungo":      `{"t":"gb-list","v":1,"games":[{"id":"3b5f0c9a1d2e4f60","title":"` + strings.Repeat("t", 17) + `","size":32768}],"mac":"` + strings.Repeat("a", 32) + `"}`,
		"gb-list rom da 4 MB":       `{"t":"gb-list","v":1,"games":[{"id":"3b5f0c9a1d2e4f60","title":"X","size":4194304}],"mac":"` + strings.Repeat("a", 32) + `"}`,
		"gb-pick id maiuscolo":      `{"t":"gb-pick","v":1,"id":"3B5F0C9A1D2E4F60"}`,
		"gb-start senza token":      `{"t":"gb-start","v":1,"id":"3b5f0c9a1d2e4f60","size":32768,"sha256":"` + strings.Repeat("a", 64) + `","mac":"` + strings.Repeat("a", 32) + `"}`,
		"gb-start sha corto":        `{"t":"gb-start","v":1,"id":"3b5f0c9a1d2e4f60","size":32768,"sha256":"abc","token":"` + strings.Repeat("a", 32) + `","mac":"` + strings.Repeat("a", 32) + `"}`,
		"gb-start rom troppo corta": `{"t":"gb-start","v":1,"id":"3b5f0c9a1d2e4f60","size":335,"sha256":"` + strings.Repeat("a", 64) + `","token":"` + strings.Repeat("a", 32) + `","mac":"` + strings.Repeat("a", 32) + `"}`,
		"gb-start token con /":      `{"t":"gb-start","v":1,"id":"3b5f0c9a1d2e4f60","size":32768,"sha256":"` + strings.Repeat("a", 64) + `","token":"a/b","mac":"` + strings.Repeat("a", 32) + `"}`,
		"gb-save vuoto":             `{"t":"gb-save","v":1,"id":"3b5f0c9a1d2e4f60","size":0,"crc32":1}`,
		"gb-save troppo grande":     `{"t":"gb-save","v":1,"id":"3b5f0c9a1d2e4f60","size":131137,"crc32":1}`,
		"gb-save-ok senza mac":      `{"t":"gb-save-ok","v":1,"id":"3b5f0c9a1d2e4f60","crc32":1}`,
		"gb-state senza playing":    `{"t":"gb-state","v":1}`,
		"gb-pad stato inventato":    `{"t":"gb-pad","v":1,"link":"bluetooth"}`,
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/display/ -run 'TestExamplesMatchSchema|TestSchemaRejects' -v`
Expected: FAIL («nessun esempio per "gb-list"», e i casi `gb-*` accettati perché lo schema non conosce i tipi)

- [ ] **Step 3: Write minimal implementation**

`protocol/schema/message.schema.json` — enum di `t`: dopo la riga

```json
                    "wifi-list", "wifi-add", "wifi-forget", "workday",
```

aggiungi la riga

```json
                    "gb-list", "gb-pick", "gb-start", "gb-save", "gb-save-ok", "gb-state", "gb-pad",
```

`allOf`: l'ultima voce

```json
    { "if": { "properties": { "t": { "const": "accessory-wear" } } }, "then": { "$ref": "#/$defs/accessoryWear" } }
  ],
```

diventa

```json
    { "if": { "properties": { "t": { "const": "accessory-wear" } } }, "then": { "$ref": "#/$defs/accessoryWear" } },
    { "if": { "properties": { "t": { "const": "gb-list" } } }, "then": { "$ref": "#/$defs/gbList" } },
    { "if": { "properties": { "t": { "const": "gb-pick" } } }, "then": { "$ref": "#/$defs/gbPick" } },
    { "if": { "properties": { "t": { "const": "gb-start" } } }, "then": { "$ref": "#/$defs/gbStart" } },
    { "if": { "properties": { "t": { "const": "gb-save" } } }, "then": { "$ref": "#/$defs/gbSave" } },
    { "if": { "properties": { "t": { "const": "gb-save-ok" } } }, "then": { "$ref": "#/$defs/gbSaveOk" } },
    { "if": { "properties": { "t": { "const": "gb-state" } } }, "then": { "$ref": "#/$defs/gbState" } },
    { "if": { "properties": { "t": { "const": "gb-pad" } } }, "then": { "$ref": "#/$defs/gbPad" } }
  ],
```

`$defs`: la fine del file

```json
        "action": { "enum": ["start", "stop"] }
      }
    }
  }
}
```

diventa

```json
        "action": { "enum": ["start", "stop"] }
      }
    },

    "gbId": { "type": "string", "pattern": "^[0-9a-f]{16}$", "$comment": "primi 16 esadecimali dello SHA-256 della ROM" },
    "gbToken": { "type": "string", "pattern": "^[A-Za-z0-9_-]{1,64}$", "$comment": "token di gb-start (il servizio ne genera 32 cifre esadecimali minuscole); finisce in un URL" },
    "gbList": {
      "$comment": "servizio → display, solo autenticato: cartucce nella cartella dei giochi del PC, dopo auth-ok e a ogni cambio. mac = HMAC(SK, gb-list␟id␟size␟title… nell'ordine dell'elenco)",
      "type": "object", "required": ["t", "v", "games", "mac"], "additionalProperties": false,
      "properties": {
        "t": true, "v": true,
        "games": { "type": "array", "maxItems": 64, "items": {
          "type": "object", "required": ["id", "title", "size"], "additionalProperties": false,
          "properties": {
            "id": { "$ref": "#/$defs/gbId" },
            "title": { "type": "string", "minLength": 1, "maxLength": 16, "pattern": "^[ -~]+$" },
            "size": { "type": "integer", "minimum": 336, "maximum": 2097152 }
          } } },
        "mac": { "$ref": "#/$defs/hex16" }
      }
    },
    "gbPick": {
      "$comment": "display → servizio, solo autenticato: gioco scelto dalla scheda. Risposta gb-start, oppure gb-list se il gioco non c'è più",
      "type": "object", "required": ["t", "v", "id"], "additionalProperties": false,
      "properties": { "t": true, "v": true, "id": { "$ref": "#/$defs/gbId" } }
    },
    "gbStart": {
      "$comment": "servizio → display, solo autenticato: avvia il gioco. Se la scheda non ha la ROM con questo sha256 la scarica da GET /gb/rom/<token> (porta dei display, Range per la ripresa); con saveToken scarica prima il salvataggio da GET /gb/save/<saveToken>. mac = HMAC(SK, gb-start␟id␟size␟sha256␟token␟saveToken), saveToken vuoto se assente",
      "type": "object", "required": ["t", "v", "id", "size", "sha256", "token", "mac"], "additionalProperties": false,
      "properties": {
        "t": true, "v": true,
        "id": { "$ref": "#/$defs/gbId" },
        "size": { "type": "integer", "minimum": 336, "maximum": 2097152 },
        "sha256": { "type": "string", "pattern": "^[0-9a-f]{64}$" },
        "token": { "$ref": "#/$defs/gbToken" },
        "saveToken": { "$ref": "#/$defs/gbToken" },
        "mac": { "$ref": "#/$defs/hex16" }
      }
    },
    "gbSave": {
      "$comment": "display → servizio, solo autenticato: salvataggio (RAM della cartuccia) da copiare sul PC, seguito 300 ms dopo da PUT /gb/save/<token> sulla porta dei display (token scelto dalla scheda: 16 byte casuali in 32 cifre minuscole, mai riusato; due nuovi tentativi su 404). Senza token: solo annuncio, il servizio risponde gb-save-ok se ha già una copia uguale",
      "type": "object", "required": ["t", "v", "id", "size", "crc32"], "additionalProperties": false,
      "properties": {
        "t": true, "v": true,
        "id": { "$ref": "#/$defs/gbId" },
        "size": { "type": "integer", "minimum": 1, "maximum": 131136 },
        "crc32": { "$ref": "#/$defs/crc32" },
        "token": { "$ref": "#/$defs/hex16" }
      }
    },
    "gbSaveOk": {
      "$comment": "servizio → display: il salvataggio con questo crc32 è al sicuro sul PC. mac = HMAC(SK, gb-save-ok␟id␟crc32), crc32 in decimale",
      "type": "object", "required": ["t", "v", "id", "crc32", "mac"], "additionalProperties": false,
      "properties": { "t": true, "v": true, "id": { "$ref": "#/$defs/gbId" }, "crc32": { "$ref": "#/$defs/crc32" }, "mac": { "$ref": "#/$defs/hex16" } }
    },
    "gbState": {
      "$comment": "display → servizio, solo autenticato: gioco in corso (null = nessuno), per la webapp",
      "type": "object", "required": ["t", "v", "playing"], "additionalProperties": false,
      "properties": { "t": true, "v": true, "playing": { "anyOf": [{ "type": "null" }, { "$ref": "#/$defs/gbId" }] } }
    },
    "gbPad": {
      "$comment": "display → servizio, solo autenticato: stato del controller Bluetooth, per la webapp",
      "type": "object", "required": ["t", "v", "link"], "additionalProperties": false,
      "properties": { "t": true, "v": true, "link": { "enum": ["off", "pairing", "connecting", "ready"] } }
    }
  }
}
```

Esempi (mac veri con SK = 32 × 0x02, gli stessi vettori del task 4):

`protocol/examples/gb-list.json`
```json
{"t":"gb-list","v":1,"games":[{"id":"3b5f0c9a1d2e4f60","title":"HOMEBREW DEMO","size":32768},{"id":"a1b2c3d4e5f60718","title":"Puzzle","size":65536}],"mac":"f6d53e39660c529c836fafb597a847e8"}
```
`protocol/examples/gb-pick.json`
```json
{"t":"gb-pick","v":1,"id":"3b5f0c9a1d2e4f60"}
```
`protocol/examples/gb-start.json`
```json
{"t":"gb-start","v":1,"id":"3b5f0c9a1d2e4f60","size":32768,"sha256":"3b5f0c9a1d2e4f607c1e9d22a8b04f3165e0d7a9c2b18f4e5d6a7b8c9d0e1f20","token":"5d2c8e1f0a9b4c3d6e7f8091a2b3c4d5","mac":"79de567f8a40b0efdf73f04badd19aa6"}
```
`protocol/examples/gb-start-ripristino.json`
```json
{"t":"gb-start","v":1,"id":"3b5f0c9a1d2e4f60","size":32768,"sha256":"3b5f0c9a1d2e4f607c1e9d22a8b04f3165e0d7a9c2b18f4e5d6a7b8c9d0e1f20","token":"5d2c8e1f0a9b4c3d6e7f8091a2b3c4d5","saveToken":"9e8d7c6b5a4f3e2d1c0b0a9988776655","mac":"d57133ff50a083ee56d6005253591fff"}
```
`protocol/examples/gb-save.json`
```json
{"t":"gb-save","v":1,"id":"3b5f0c9a1d2e4f60","size":8192,"crc32":305419896,"token":"4f3e2d1c0b0a99887766554433221100"}
```
`protocol/examples/gb-save-ok.json`
```json
{"t":"gb-save-ok","v":1,"id":"3b5f0c9a1d2e4f60","crc32":305419896,"mac":"7f690a4c01c28ed6e0f773b54866f6ef"}
```
`protocol/examples/gb-state.json`
```json
{"t":"gb-state","v":1,"playing":"3b5f0c9a1d2e4f60"}
```
`protocol/examples/gb-state-nessuno.json`
```json
{"t":"gb-state","v":1,"playing":null}
```
`protocol/examples/gb-pad.json`
```json
{"t":"gb-pad","v":1,"link":"ready"}
```

`docs/PROTOCOLLO.md`: subito prima di `## API locale della webapp` inserisci:

````markdown
## Giochi

Giochi del Game Boy classico sulla scheda (spec `docs/superpowers/specs/2026-10-12-giochi-gameboy-design.md`, contratto tra i piani `docs/superpowers/plans/2026-10-12-giochi-contratti.md`). Le cartucce stanno sul PC in `<dati utente>/calamaio/games/` (`<id>.gb` + `index.json`), i salvataggi in `games/saves/<id>/` (ultime 3 copie). Si caricano **solo file dell'utente** (copie delle proprie cartucce, homebrew): il servizio non scarica mai ROM. `id` = primi 16 esadecimali dello SHA-256 della ROM. Aggiunta compatibile alla v1: tutti i messaggi sono facoltativi e **solo con connessione autenticata** (un messaggio `gb-*` da una connessione non autenticata si ignora e si registra nel log); un firmware che non li conosce li scarta come tipi sconosciuti. Messaggi del servizio con `mac` = HMAC-SHA256(SK, …) troncato a 16 byte, `␟` = 0x1F come nei permessi, **obbligatorio**: la scheda lo verifica e scarta, registrandolo, un messaggio senza `mac` o con `mac` sbagliato. I messaggi della scheda non hanno mac (vale l'autenticazione della connessione, come `ota-status`). Interi nei mac in decimale senza zeri iniziali, stringhe tali e quali.

Formati: `id` = 16 cifre esadecimali minuscole (primi 16 caratteri dello SHA-256 della ROM); `size` della ROM 0x150–0x200000 (336–2 097 152) byte; token `[A-Za-z0-9_-]{1,64}`; `crc32` = CRC-32 IEEE, intero u32; titoli ASCII stampabili, 1–16 caratteri; al massimo 64 giochi in `gb-list`; salvataggi 1–131 136 byte sul servizio (la scheda gestisce cartucce con RAM fino a 32 KB: oltre, «Gioco non supportato»). Il servizio accetta file `.gb` e `.gbc` e decide l'intestazione (rifiuta i giochi solo Color, 0x143 = 0xC0). Esempi veri (SK = 32 × 0x02): `examples/gb-*.json`; vettori in `service/internal/m5/gb_test.go`.

### `gb-list` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `games` | array ≤ 64 | per titolo, poi per id; oltre 64 cartucce si mandano le prime 64 |
| `games[].id` | 16 cifre esadecimali | |
| `games[].title` | string 1–16, ASCII stampabile | dall'intestazione della cartuccia, altrimenti dal nome del file |
| `games[].size` | int 336–2097152 | byte della ROM (il servizio, che controlla l'intestazione, ne manda da 32768) |
| `mac` | hex 16 byte | `HMAC(SK, "gb-list" ␟ id₁ ␟ size₁ ␟ title₁ ␟ id₂ ␟ …)`; con l'elenco vuoto `HMAC(SK, "gb-list")` |

Subito dopo `auth-ok` e a ogni cambio della cartella (caricamento o rimozione dalla webapp o dalla CLI; un cambio fatto con il servizio spento si vede entro 5 s dal riavvio).

### `gb-pick` (display → servizio)
`{id}`: gioco scelto dalla scheda. Il servizio risponde con `gb-start`; se il gioco non c'è più rimanda `gb-list`.

### `gb-start` (servizio → display)
| Campo | Tipo | Note |
|---|---|---|
| `id` | 16 cifre esadecimali | |
| `size` | int 336–2097152 | byte della ROM |
| `sha256` | hex 32 byte | SHA-256 della ROM intera |
| `token` | string `[A-Za-z0-9_-]{1,64}` | per `GET /gb/rom/<token>`; il servizio ne genera 32 cifre esadecimali minuscole |
| `saveToken` | come `token`, facoltativo | c'è un ripristino scelto dalla webapp: `GET /gb/save/<saveToken>` prima di partire, e il salvataggio scaricato sostituisce quello della scheda |
| `mac` | hex 16 byte | `HMAC(SK, "gb-start" ␟ id ␟ size ␟ sha256 ␟ token ␟ saveToken)`, `saveToken` vuoto se assente |

Dalla webapp, dalla CLI o in risposta a `gb-pick`. Se nell'area `gbrom` c'è già la ROM con lo stesso `sha256` la scheda parte subito; altrimenti la scarica, verifica SHA-256 e dimensione e **solo allora** parte. Un avvio nuovo per la stessa scheda revoca i token del precedente. Il servizio rifiuta l'avvio durante un aggiornamento remoto della scheda (409).

**Aggiornamento remoto e partita.** Con una partita aperta (caricamento, gioco o pausa) la scheda rifiuta `ota-offer` con `ota-status {phase: "rifiutato", error: "partita in corso: riprova a fine partita"}` (nella webapp: «rifiutato: partita in corso: riprova a fine partita») e la partita continua. Il servizio tiene l'immagine e rifà l'offerta **una volta** quando la stessa scheda manda `gb-state {playing: null}`, se intanto non è partito un altro aggiornamento.

### `gb-save` (display → servizio) e `gb-save-ok` (servizio → display)
| Campo di `gb-save` | Tipo | Note |
|---|---|---|
| `id` | 16 cifre esadecimali | anche di una cartuccia tolta dal PC (i salvataggi restano) |
| `size` | int 1–131136 | byte della RAM della cartuccia (128 KB + 64 byte dell'orologio MBC3) |
| `crc32` | int | CRC-32 IEEE del salvataggio |
| `token` | hex 16 byte, facoltativo | scelto dalla scheda (16 byte casuali in 32 cifre minuscole, mai riusati): annuncia il caricamento `PUT /gb/save/<token>`, che la scheda fa 300 ms dopo il `gb-save` e ritenta 2 volte su 404 |

`gb-save-ok {id, crc32, mac}` con `mac = HMAC(SK, "gb-save-ok" ␟ id ␟ crc32)` dice che quel salvataggio è al sicuro sul PC. La scheda manda `gb-save` a ogni connessione e all'uscita dal gioco se la RAM della cartuccia è cambiata dall'ultimo `gb-save-ok`. **Senza `token`** è solo un annuncio: se il servizio ha già una copia con la stessa dimensione e lo stesso CRC risponde subito `gb-save-ok`, altrimenti lo ignora (la scheda riprova con un token). Con il token il servizio risponde `gb-save-ok` dopo aver salvato il `PUT` (anche se la copia era già uguale alla più recente: in quel caso non ne fa una nuova). Ultime 3 copie per gioco; la copia scelta per un ripristino non ancora consegnato non si cancella.

### `gb-state`, `gb-pad` (display → servizio)
`gb-state {playing: id | null}`: gioco in corso; `gb-pad {link: "off" | "pairing" | "connecting" | "ready"}`: controller Bluetooth. Solo per la webapp e `calamaio status`; il servizio li tiene finché la connessione resta aperta. La scheda li manda dopo `auth-ok` e a ogni cambio.

### Download e caricamento (porta dei display, 8766)
Stesso server e stesso modello di sicurezza di `GET /ota/<token>`: l'host e la porta sono quelli della connessione WebSocket della scheda (porta dei display, **8766**; mai la 8765 della API locale, che ascolta solo su 127.0.0.1), i token valgono **10 minuti** e solo per l'**IP della scheda** che ha ricevuto `gb-start` (o mandato `gb-save`). Token sconosciuto 404, usato o scaduto 410, IP diverso 403 (il token resta valido). Su 403, 404 o 410 durante il download della ROM la scheda manda di nuovo `gb-pick {id}`, **una volta per gioco**, e il servizio risponde con un `gb-start` nuovo: la scheda riprende dal punto salvato.

| Rotta | Risposta |
|---|---|
| `GET /gb/rom/<token>` | la ROM, `application/octet-stream`, `X-Calamaio-Sha256`; **`Range` ammesso** (206 con `Content-Range`): dopo un'interruzione la scheda riprende con `Range: bytes=N-` sullo stesso token. Il token si consuma quando l'ultimo byte è stato consegnato (una risposta 200 completa, o una 206 completa che arriva alla fine); dopo, 410. Oltre i 10 minuti (o su 403/404/410) la scheda manda di nuovo `gb-pick`, una volta per gioco, e riceve un token nuovo. |
| `PUT /gb/save/<token>` | corpo = il salvataggio, `Content-Length` uguale a `size` di `gb-save`, CRC-32 uguale a `crc32`: 204 e poi `gb-save-ok` sul WebSocket; 400 dimensione o CRC diversi (niente salvato, il token resta buono fino alla scadenza); 409 se un altro `PUT` con lo stesso token è in corso; dopo il primo caricamento riuscito, 410 |
| `GET /gb/save/<saveToken>` | il salvataggio scelto per il ripristino (`X-Calamaio-Crc32` in decimale), una volta sola; consegnato tutto, il ripristino si chiude |

Avvio dalla API locale: `POST /api/displays/{board}/gb/start {id}` (solo 127.0.0.1:8765) risponde 409 se la scheda non è collegata o autenticata o se c'è un aggiornamento remoto in corso.

Esempi: `examples/gb-list.json`, `gb-pick.json`, `gb-start.json`, `gb-start-ripristino.json`, `gb-save.json`, `gb-save-ok.json`, `gb-state.json`, `gb-state-nessuno.json`, `gb-pad.json`.
````

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/display/ -run 'TestExamplesMatchSchema|TestSchemaRejects' -v && go test ./...`
Expected: PASS (anche `cmd/calamaio`, che controlla con lo stesso schema ogni frame ricevuto dalle schede finte)

- [ ] **Step 5: Commit**

```bash
git add protocol/schema/message.schema.json protocol/examples/gb-*.json service/internal/display/schema_test.go docs/PROTOCOLLO.md
git commit -m "Protocollo: messaggi gb-* dei giochi Game Boy, schema ed esempi veri"
```

---

### Task 6: hub — `gb-list`, `gb-state`, `gb-pad`, stato per la webapp

**Files:**
- Create: `service/internal/display/gb.go`
- Modify: `service/internal/display/hub.go` (campi `Gb` e `GbTokenTTL` dell'Hub, `NewHub`, campi `gb*` di `conn`, `readLoop`, `run`)
- Modify: `service/internal/display/remote.go` (campo `gb` di `inbound`, `Gb` in `DisplayStatus` e in `Status()`)
- Modify: `service/internal/display/m5.go` (`onAuth`: `gb-list` dopo `auth-ok`)
- Test: `service/internal/display/gb_test.go`

**Interfaces:**
- Consumes: `gbgames.Game`, `gbgames.Save`, `gbgames.ValidID`, `gbgames.MaxSave` (task 1–3); `m5.GbListMac`, `m5.GbListItem` (task 4); schema (task 5).
- Produces:
  - `type GbLibrary interface { List() []gbgames.Game; Get(id string) (gbgames.Game, bool); ReadROM(id string) ([]byte, error); PutSave(id string, data []byte) (gbgames.Save, bool, error); HasSave(id string, size int64, crc uint32) bool; PendingRestore(id string) (gbgames.Save, []byte, bool); ClearRestore(id string, crc uint32) }` (lo soddisfa `*gbgames.Library`)
  - campo `Hub.Gb GbLibrary` (nil = giochi spenti: niente messaggi né rotte attive); campo `Hub.GbTokenTTL time.Duration` (10 minuti in `NewHub`: validità dei token dei giochi, contratto)
  - `const MaxGbList = 64`
  - `type GbListMsg struct { T string; V int; Games []GbListGame; Mac string }`, `type GbListGame struct { ID, Title string; Size int64 }`
  - `type GbView struct { Known bool; Playing, Title, Pad string; Start *GbStartView }` (JSON `known, playing, title, pad, start`); `DisplayStatus.Gb *GbView` (JSON `gb`, null se il servizio non ha i giochi)
  - `type GbStartView struct { ID, Title string; Size, At, Served int64; Done, Restore bool }` (JSON `id, title, size, at, served, done, restore`) — riempito dal task 7
  - `func (h *Hub) GbListChanged()`
  - non esportate per i task 7–8: `type gbIn`, `func parseGb(t string, data []byte) (*gbIn, bool)`, `func (h *Hub) onGb(c *conn, t string, m *gbIn)`, `func (h *Hub) queueGbList(c *conn)`, `type gbHub`, `func (h *Hub) gbInit()`

- [ ] **Step 1: Write the failing test**

`service/internal/display/gb_test.go`:

```go
package display

import (
	"bytes"
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/gbgames"
	"calamaio/internal/m5"
)

// Giochi Game Boy con un display finto (internal/fakedisplay) che fa la parte della scheda: elenco dopo
// auth-ok con il mac giusto, stato del gioco e del controller, avvio, download con ripresa, salvataggi.

const gbBoard = "calamaio-3f2a"

type gbRig struct {
	*rig
	lib  *gbgames.Library
	game gbgames.Game
	rom  []byte
}

func newGbRig(t *testing.T, tune func(*Hub)) *gbRig {
	t.Helper()
	lib, err := gbgames.Open(filepath.Join(t.TempDir(), "games"))
	if err != nil {
		t.Fatal(err)
	}
	rom := gbgames.FakeROM(gbgames.FakeROMOptions{Title: "HOMEBREW DEMO", CartType: 0x03, SizeCode: 1, RAMCode: 2})
	g, _, err := lib.Add("demo.gb", rom)
	if err != nil {
		t.Fatal(err)
	}
	r := newRig(t, func(h *Hub) {
		h.Gb = lib
		if tune != nil {
			tune(h)
		}
	})
	r.pairKey(gbBoard, testKey)
	return &gbRig{rig: r, lib: lib, game: g, rom: rom}
}

// gbDisplay: scheda finta abbinata e autenticata, con il primo gb-list già arrivato.
func (r *gbRig) gbDisplay(t *testing.T) (*fakedisplay.Display, fakedisplay.Event) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.Dial(ctx, r.url, gbBoard, testKey, false)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	e, err := d.Await("gb-list", 3*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	return d, e
}

// gbListOf ricava l'elenco e controlla il mac con la SK della scheda.
func gbListOf(t *testing.T, d *fakedisplay.Display, e fakedisplay.Event) []m5.GbListItem {
	t.Helper()
	b, _ := json.Marshal(e.Msg)
	var m GbListMsg
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatal(err)
	}
	items := make([]m5.GbListItem, len(m.Games))
	for i, g := range m.Games {
		items[i] = m5.GbListItem{ID: g.ID, Title: g.Title, Size: g.Size}
	}
	if m.Mac != m5.Hex(m5.GbListMac(d.SK(), items)) {
		t.Fatalf("mac di gb-list sbagliato: %s", b)
	}
	validate(t, compileSchema(t), "gb-list", b)
	return items
}

func TestGbListAfterAuth(t *testing.T) {
	r := newGbRig(t, nil)
	d, e := r.gbDisplay(t)
	items := gbListOf(t, d, e)
	if len(items) != 1 || items[0].ID != r.game.ID || items[0].Title != "HOMEBREW DEMO" || items[0].Size != 64<<10 {
		t.Fatalf("elenco: %+v", items)
	}
	// cartella cambiata: elenco nuovo a tutte le schede autenticate
	g2, _, _ := r.lib.Add("b.gb", gbgames.FakeROM(gbgames.FakeROMOptions{Title: "AAA", Fill: 9}))
	r.hub.GbListChanged()
	e2, err := d.Await("gb-list", 3*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if items := gbListOf(t, d, e2); len(items) != 2 || items[0].ID != g2.ID {
		t.Fatalf("elenco dopo l'aggiunta: %+v", items)
	}
}

// Senza autenticazione: niente gb-list, gb-state e gb-pad ignorati.
func TestGbNeedsAuth(t *testing.T) {
	r := newGbRig(t, nil)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.Dial(ctx, r.url, "calamaio-0000", nil, false)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	if _, err := d.Await("welcome", 2*time.Second); err != nil {
		t.Fatal(err)
	}
	before := r.hub.Ignored()
	d.Send(`{"t":"gb-state","v":1,"playing":"` + r.game.ID + `"}`)
	d.Send(`{"t":"gb-pad","v":1,"link":"ready"}`)
	waitFor(t, 2*time.Second, func() bool { return r.hub.Ignored() >= before+2 })
	st := r.hub.Status()
	if len(st) != 1 || st[0].Gb == nil || st[0].Gb.Known || st[0].Gb.Pad != "" {
		t.Fatalf("stato: %+v", st[0].Gb)
	}
	for _, f := range d.Frames() {
		if strings.Contains(f, `"gb-list"`) {
			t.Fatalf("gb-list a una scheda non autenticata: %s", f)
		}
	}
}

func TestGbStateAndPad(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	d.Send(`{"t":"gb-pad","v":1,"link":"pairing"}`)
	d.Send(`{"t":"gb-state","v":1,"playing":"` + r.game.ID + `"}`)
	waitFor(t, 2*time.Second, func() bool { v := r.hub.Status()[0].Gb; return v.Playing != "" && v.Pad == "pairing" })
	v := r.hub.Status()[0].Gb
	if !v.Known || v.Title != "HOMEBREW DEMO" {
		t.Fatalf("vista: %+v", v)
	}
	d.Send(`{"t":"gb-state","v":1,"playing":null}`)
	waitFor(t, 2*time.Second, func() bool { return r.hub.Status()[0].Gb.Playing == "" })
	before := r.hub.Ignored()
	for _, bad := range []string{
		`{"t":"gb-state","v":1}`,
		`{"t":"gb-state","v":1,"playing":"XYZ"}`,
		`{"t":"gb-pad","v":1,"link":"blu"}`,
		`{"t":"gb-pick","v":1,"id":"../../etc/passwd"}`,
		`{"t":"gb-save","v":1,"id":"` + r.game.ID + `","size":0,"crc32":1}`,
		`{"t":"gb-save","v":1,"id":"` + r.game.ID + `","size":10,"crc32":-1}`,
		`{"t":"gb-save","v":1,"id":"` + r.game.ID + `","size":10,"crc32":1,"token":"corto"}`,
	} {
		d.Send(bad)
	}
	waitFor(t, 2*time.Second, func() bool { return r.hub.Ignored() >= before+7 })
	if r.hub.Status()[0].Gb.Pad != "pairing" {
		t.Fatal("un messaggio non valido ha cambiato lo stato")
	}
}

// Gli esempi del protocollo sono veri: mac con SK = 32 × 0x02 e parser del servizio.
func TestGbExamplesAreReal(t *testing.T) {
	sk := bytes.Repeat([]byte{2}, 32)
	read := func(name string) []byte {
		b, err := os.ReadFile(filepath.Join(protocolDir, "examples", name))
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	var l GbListMsg
	json.Unmarshal(read("gb-list.json"), &l)
	items := []m5.GbListItem{}
	for _, g := range l.Games {
		items = append(items, m5.GbListItem{ID: g.ID, Title: g.Title, Size: g.Size})
	}
	if l.Mac != m5.Hex(m5.GbListMac(sk, items)) {
		t.Error("gb-list.json: mac")
	}
	for _, f := range []string{"gb-start.json", "gb-start-ripristino.json"} {
		var s GbStartMsg
		json.Unmarshal(read(f), &s)
		if s.Mac != m5.Hex(m5.GbStartMac(sk, s.ID, s.Size, s.SHA256, s.Token, s.SaveToken)) {
			t.Errorf("%s: mac", f)
		}
	}
	var ok gbSaveOkMsg
	json.Unmarshal(read("gb-save-ok.json"), &ok)
	if ok.Mac != m5.Hex(m5.GbSaveOkMac(sk, ok.ID, ok.CRC32)) {
		t.Error("gb-save-ok.json: mac")
	}
	for _, f := range []string{"gb-pick.json", "gb-save.json", "gb-state.json", "gb-state-nessuno.json", "gb-pad.json"} {
		b := read(f)
		var head struct{ T string }
		json.Unmarshal(b, &head)
		if _, okp := parseGb(head.T, b); !okp {
			t.Errorf("%s rifiutato da parseGb", f)
		}
	}
}
```

Nota: `GbStartMsg` e `gbSaveOkMsg` si definiscono in questo task (in `gb.go`), anche se li manda il task 7–8: così l'esempio si controlla subito.

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/display/ -run 'TestGb' -v`
Expected: FAIL (`h.Gb undefined`, `undefined: GbListMsg` …)

- [ ] **Step 3: Write minimal implementation**

`service/internal/display/gb.go`:

```go
package display

import (
	"encoding/json"
	"time"

	"calamaio/internal/gbgames"
	"calamaio/internal/m5"
)

// Giochi Game Boy (docs/PROTOCOLLO.md «Giochi»): elenco delle cartucce (gb-list) dopo auth-ok e a ogni
// cambio della cartella, stato del gioco e del controller dalla scheda (gb-state, gb-pad), avvio con
// download della ROM e salvataggi (gb_start.go, gb_save.go). Tutto solo con connessione autenticata.

// GbLibrary: la cartella dei giochi (gbgames.Library).
type GbLibrary interface {
	List() []gbgames.Game
	Get(id string) (gbgames.Game, bool)
	ReadROM(id string) ([]byte, error)
	PutSave(id string, data []byte) (gbgames.Save, bool, error)
	HasSave(id string, size int64, crc uint32) bool
	PendingRestore(id string) (gbgames.Save, []byte, bool)
	ClearRestore(id string, crc uint32)
}

// MaxGbList: cartucce al massimo in un gb-list.
const MaxGbList = 64

type (
	GbListGame struct {
		ID    string `json:"id"`
		Title string `json:"title"`
		Size  int64  `json:"size"`
	}
	// GbListMsg (servizio → display).
	GbListMsg struct {
		T     string       `json:"t"`
		V     int          `json:"v"`
		Games []GbListGame `json:"games"`
		Mac   string       `json:"mac"`
	}
	// GbStartMsg (servizio → display), gb_start.go.
	GbStartMsg struct {
		T         string `json:"t"`
		V         int    `json:"v"`
		ID        string `json:"id"`
		Size      int64  `json:"size"`
		SHA256    string `json:"sha256"`
		Token     string `json:"token"`
		SaveToken string `json:"saveToken,omitempty"`
		Mac       string `json:"mac"`
	}
	// gbSaveOkMsg (servizio → display), gb_save.go.
	gbSaveOkMsg struct {
		T     string `json:"t"`
		V     int    `json:"v"`
		ID    string `json:"id"`
		CRC32 uint32 `json:"crc32"`
		Mac   string `json:"mac"`
	}
)

// gbIn: i campi dei messaggi gb-* della scheda.
type gbIn struct {
	ID      string
	Playing string // gb-state: "" = nessun gioco
	Link    string
	Size    int64
	CRC32   uint32
	Token   string
}

var gbLinks = map[string]bool{"off": true, "pairing": true, "connecting": true, "ready": true}

// parseGb controlla un messaggio gb-* della scheda (forme esatte dello schema).
func parseGb(t string, data []byte) (*gbIn, bool) {
	var raw map[string]json.RawMessage
	if json.Unmarshal(data, &raw) != nil {
		return nil, false
	}
	str := func(k string) (string, bool) {
		var s string
		return s, raw[k] != nil && json.Unmarshal(raw[k], &s) == nil
	}
	m := &gbIn{}
	switch t {
	case "gb-pick":
		id, ok := str("id")
		if !ok || !gbgames.ValidID(id) {
			return nil, false
		}
		m.ID = id
	case "gb-state":
		p, ok := raw["playing"]
		if !ok {
			return nil, false
		}
		if string(p) != "null" {
			var id string
			if json.Unmarshal(p, &id) != nil || !gbgames.ValidID(id) {
				return nil, false
			}
			m.Playing = id
		}
	case "gb-pad":
		l, ok := str("link")
		if !ok || !gbLinks[l] {
			return nil, false
		}
		m.Link = l
	case "gb-save":
		var s struct {
			ID    string  `json:"id"`
			Size  *int64  `json:"size"`
			CRC32 *uint32 `json:"crc32"`
			Token *string `json:"token"`
		}
		if json.Unmarshal(data, &s) != nil || !gbgames.ValidID(s.ID) || s.Size == nil || s.CRC32 == nil ||
			*s.Size < 1 || *s.Size > gbgames.MaxSave {
			return nil, false
		}
		m.ID, m.Size, m.CRC32 = s.ID, *s.Size, *s.CRC32
		if s.Token != nil {
			if len(*s.Token) != 32 || !m5.IsLowerHex(*s.Token) {
				return nil, false
			}
			m.Token = *s.Token
		}
	default:
		return nil, false
	}
	return m, true
}

// gbHub: stato dei giochi dell'hub (protetto da Hub.mu).
type gbHub struct {
	roms   map[string]*gbRomTok  // token → download della ROM (gb_start.go)
	downs  map[string]*gbDownTok // token → ripristino di un salvataggio (gb_save.go)
	ups    map[string]*gbUpTok   // token della scheda → caricamento di un salvataggio (gb_save.go)
	seen   map[string]time.Time  // token di caricamento già visti: mai riusati
	starts map[string]*gbStart   // scheda → ultimo avvio
	// Aggiornamenti rifiutati per una partita in corso, da rifare a gb-state {playing: null} (gb_start.go, task 7).
	otaRetry   map[string]*gbOtaRetry
	restartOTA func(board string, img OTAImage) (*OtaView, error) // nil = Hub.StartOTA; le prove lo sostituiscono
}

func (h *Hub) gbInit() {
	if h.gb.roms == nil {
		h.gb = gbHub{roms: map[string]*gbRomTok{}, downs: map[string]*gbDownTok{}, ups: map[string]*gbUpTok{},
			seen: map[string]time.Time{}, starts: map[string]*gbStart{}, otaRetry: map[string]*gbOtaRetry{}}
	}
}

// GbView: giochi della scheda in GET /api/status (displays[].gb).
type GbView struct {
	Known   bool         `json:"known"`   // la scheda ha mandato gb-state o gb-pad su questa connessione
	Playing string       `json:"playing"` // id del gioco in corso ("" = nessuno)
	Title   string       `json:"title,omitempty"`
	Pad     string       `json:"pad"`   // off · pairing · connecting · ready ("" = mai riferito)
	Start   *GbStartView `json:"start"` // ultimo avvio mandato a questa scheda
}

// GbStartView: un avvio (gb_start.go).
type GbStartView struct {
	ID      string `json:"id"`
	Title   string `json:"title"`
	Size    int64  `json:"size"`
	At      int64  `json:"at"`      // ms epoch
	Served  int64  `json:"served"`  // byte della ROM serviti (con la ripresa possono superare size)
	Done    bool   `json:"done"`    // ROM consegnata tutta
	Restore bool   `json:"restore"` // con un salvataggio da ripristinare
}

// gbView: con h.mu.
func (h *Hub) gbView(c *conn) *GbView {
	v := &GbView{Known: c.gbKnown, Playing: c.gbPlaying, Pad: c.gbPad}
	if v.Playing != "" {
		if g, ok := h.Gb.Get(v.Playing); ok {
			v.Title = g.Title
		}
	}
	if st := h.gb.starts[c.info.Board]; st != nil {
		v.Start = st.view()
	}
	return v
}

// queueGbList mette in coda gb-list (costruito all'invio, con la SK di quel momento). Con h.mu.
func (h *Hub) queueGbList(c *conn) {
	if h.Gb == nil || !c.authed {
		return
	}
	h.enqueueOut(c, lazyMsg(func() any {
		games := h.Gb.List()
		if len(games) > MaxGbList {
			h.Log.Printf("giochi: %d cartucce, a %s ne mando %d", len(games), c.info.Board, MaxGbList)
			games = games[:MaxGbList]
		}
		msg := GbListMsg{T: "gb-list", V: ProtocolVersion, Games: make([]GbListGame, len(games))}
		items := make([]m5.GbListItem, len(games))
		for i, g := range games {
			msg.Games[i] = GbListGame{ID: g.ID, Title: g.Title, Size: g.Size}
			items[i] = m5.GbListItem{ID: g.ID, Title: g.Title, Size: g.Size}
		}
		h.mu.Lock()
		defer h.mu.Unlock()
		if !c.authed || c.sk == nil {
			return nil
		}
		msg.Mac = m5.Hex(m5.GbListMac(c.sk, items))
		return msg
	}))
}

// GbListChanged: la cartella è cambiata (webapp, CLI, sorveglianza): gb-list a tutte le schede
// autenticate, e la webapp si aggiorna.
func (h *Hub) GbListChanged() {
	h.mu.Lock()
	for _, c := range h.conns {
		h.queueGbList(c)
	}
	h.mu.Unlock()
	go h.changed()
}

// onGb applica un messaggio gb-* della scheda. Dal goroutine della connessione.
func (h *Hub) onGb(c *conn, t string, m *gbIn) {
	h.mu.Lock()
	if h.Gb == nil {
		h.ignoreLocked(c.info.ID, "%s: giochi non disponibili in questo servizio", t)
		h.mu.Unlock()
		return
	}
	if !c.authed {
		h.ignoreLocked(c.info.ID, "%s da una connessione non autenticata", t)
		h.mu.Unlock()
		return
	}
	board := c.info.Board
	switch t {
	case "gb-state":
		c.gbKnown, c.gbPlaying = true, m.Playing
		h.mu.Unlock()
		go h.changed()
	case "gb-pad":
		c.gbKnown, c.gbPad = true, m.Link
		h.mu.Unlock()
		go h.changed()
	case "gb-pick":
		c.gbKnown = true
		h.mu.Unlock()
		if _, err := h.StartGb(board, m.ID); err != nil {
			h.Log.Printf("giochi: %s ha scelto %s ma non parte: %v", board, m.ID, err)
			h.mu.Lock()
			h.queueGbList(c) // l'elenco giusto, se il gioco non c'è più
			h.mu.Unlock()
		}
	case "gb-save":
		c.gbKnown = true
		h.mu.Unlock()
		h.onGbSave(c, m)
	default:
		h.mu.Unlock()
	}
}
```

Nota per l'implementatore: `StartGb`, `onGbSave`, `gbRomTok`, `gbDownTok`, `gbUpTok`, `gbStart` arrivano nei task 7–8. Per far compilare questo task aggiungi **adesso** il file `service/internal/display/gb_start.go` con questo scheletro (il task 7 lo sostituisce tutto):

```go
package display

import "errors"

type gbStart struct{}
type gbRomTok struct{}
type gbOtaRetry struct{}

func (st *gbStart) view() *GbStartView { return nil }

// StartGb: task 7.
func (h *Hub) StartGb(board, id string) (*GbStartView, error) {
	return nil, errors.New("avvio dei giochi non ancora disponibile")
}
```

e `service/internal/display/gb_save.go` (il task 8 lo sostituisce tutto):

```go
package display

type gbDownTok struct{}
type gbUpTok struct{}

// onGbSave: task 8.
func (h *Hub) onGbSave(c *conn, m *gbIn) {}
```

Modifiche agli altri file:

`service/internal/display/hub.go`, struct `Hub`, dopo il blocco `wifi map[string]*boardWifi`:

```go
	// Giochi Game Boy (gb.go): cartella delle cartucce (nil = giochi spenti) e token, avvii.
	Gb         GbLibrary
	GbTokenTTL time.Duration // 10 minuti: token dei giochi (ROM, salvataggi, ripristino); TokenTTL resta dell'OTA
	gb         gbHub
```

in `NewHub`, subito dopo `TokenTTL:     5 * time.Minute,`:

```go
		GbTokenTTL:   10 * time.Minute, // contratto dei piani giochi
```

struct `conn`, dopo il blocco della giornata di lavoro:

```go
	// Giochi Game Boy (gb.go, protetti da Hub.mu).
	gbKnown   bool   // la scheda ha mandato un messaggio gb-*
	gbPlaying string // gb-state.playing
	gbPad     string // gb-pad.link
```

`readLoop`, prima di `case "pair-key", "pair-done", …`:

```go
		case "gb-pick", "gb-state", "gb-pad", "gb-save":
			m, ok := parseGb(head.T, data)
			if !ok {
				h.ignore(id, "%s non valido: %q", head.T, clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, gb: m}); err != nil {
				return err
			}
```

`run`, nel primo `switch m.t` (quello con `case "ota-status":`), aggiungi:

```go
				case "gb-pick", "gb-state", "gb-pad", "gb-save":
					h.onGb(c, m.t, m.gb)
					continue
```

`service/internal/display/remote.go`, struct `inbound`, dopo `wifi []WifiNetwork`:

```go
	gb        *gbIn         // gb-pick, gb-state, gb-pad, gb-save (gb.go)
```

struct `DisplayStatus`, dopo `Workday *WorkdayView`:

```go
	// Giochi Game Boy: gioco in corso, controller, ultimo avvio (null se il servizio non ha i giochi).
	Gb *GbView `json:"gb"`
```

in `Status()`, dopo `st.Workday = workdayView(c)`:

```go
		if h.Gb != nil {
			h.gbInit()
			st.Gb = h.gbView(c)
		}
```

`service/internal/display/m5.go`, in `onAuth`, subito dopo `h.afterAuth(c)`:

```go
	h.mu.Lock()
	h.queueGbList(c) // giochi: elenco delle cartucce (docs/PROTOCOLLO.md «Giochi»)
	h.mu.Unlock()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/display/ -v -run 'TestGb' && go test ./...`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add service/internal/display/gb.go service/internal/display/gb_start.go service/internal/display/gb_save.go service/internal/display/gb_test.go service/internal/display/hub.go service/internal/display/remote.go service/internal/display/m5.go
git commit -m "Hub: gb-list dopo auth-ok e a ogni cambio, gb-state e gb-pad per la webapp"
```

---
### Task 7: avvio (`gb-start`, `gb-pick`) e download della ROM con ripresa

**Files:**
- Modify (sostituisci tutto lo scheletro del task 6): `service/internal/display/gb_start.go`
- Modify (sostituisci lo scheletro del task 6 con quello qui sotto): `service/internal/display/gb_save.go`
- Modify: `service/internal/display/hub.go` (`Handler`: rotta `GET /gb/rom/{token}`)
- Modify (Step 6–8, OTA a fine partita): `service/internal/display/ota.go` (`otaJob.img`, `StartOTA`, `otaEnd`, `onOtaStatus`), `service/internal/display/gb.go` (`onGb`, caso `gb-state`)
- Test: `service/internal/display/gb_start_test.go`

**Interfaces:**
- Consumes: `GbLibrary`, `GbStartMsg`, `GbStartView`, `gbHub`, `gbInit`, `queueGbList`, `Hub.GbTokenTTL` (task 6); `m5.GbStartMac` (task 4); `BlockedError`, `ErrNotConnected`, `ErrNotAuthed`, `ErrBusy`, `ErrStopping`, `otaFinal`, `lazyMsg`, `remoteIP` (esistenti).
- Produces:
  - `var ErrGbOff, ErrGbUnknown, ErrGbOTA error`
  - `func (h *Hub) StartGb(board, id string) (*GbStartView, error)`
  - `func (h *Hub) ServeGbROM(w http.ResponseWriter, r *http.Request)` (rotta `GET /gb/rom/{token}` sulla porta dei display)
  - `const OtaPlayingErr = "partita in corso: riprova a fine partita"`; `type gbOtaRetry struct { job *otaJob; img OTAImage; start func(string, OTAImage) (*OtaView, error) }`, `func (h *Hub) gbOtaRetryLater(j *otaJob)`, `func (h *Hub) gbTakeOtaRetry(board string) *gbOtaRetry` (Step 6–8: OTA rifiutato durante la partita e rifatto a `gb-state {playing: null}`, contratto «OTA e partita»)
  - non esportate per il task 8: `type gbStart struct { board, id, title, sha string; size int64; at time.Time; rom []byte; served int64; done, restore bool; romTok, saveTok string }`, `func (h *Hub) gbSweep(now time.Time)`, `type gbCountWriter`
  - contratto con `gb_save.go` (scheletro qui, vero nel task 8): `func (h *Hub) gbAddDown(c *conn, id string, crc uint32, data []byte, now time.Time) string` (token del ripristino, "" se nessuno), `func (h *Hub) gbSweepSaves(now time.Time)`

- [ ] **Step 1: Write the failing test**

`service/internal/display/gb_start_test.go`:

```go
package display

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/m5"
)

// awaitStart: il prossimo gb-start, con mac e schema controllati.
func awaitStart(t *testing.T, d *fakedisplay.Display) GbStartMsg {
	t.Helper()
	e, err := d.Await("gb-start", 3*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	b, _ := json.Marshal(e.Msg)
	var m GbStartMsg
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatal(err)
	}
	if m.Mac != m5.Hex(m5.GbStartMac(d.SK(), m.ID, m.Size, m.SHA256, m.Token, m.SaveToken)) {
		t.Fatalf("mac di gb-start sbagliato: %s", b)
	}
	validate(t, compileSchema(t), "gb-start", b)
	return m
}

// get: richiesta alla porta dei display; hdr = coppie nome, valore.
func (r *gbRig) get(t *testing.T, path string, hdr ...string) (int, []byte, http.Header) {
	t.Helper()
	req, _ := http.NewRequest(http.MethodGet, r.srv.URL+path, nil)
	for i := 0; i+1 < len(hdr); i += 2 {
		req.Header.Set(hdr[i], hdr[i+1])
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, b, resp.Header
}

func TestStartGb(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	v, err := r.hub.StartGb(gbBoard, r.game.ID)
	if err != nil {
		t.Fatal(err)
	}
	if v.ID != r.game.ID || v.Title != "HOMEBREW DEMO" || v.Size != 64<<10 || v.Done || v.Restore {
		t.Fatalf("vista: %+v", v)
	}
	m := awaitStart(t, d)
	if m.ID != r.game.ID || m.Size != 64<<10 || m.SHA256 != r.game.SHA256 || len(m.Token) != 32 || m.SaveToken != "" {
		t.Fatalf("gb-start: %+v", m)
	}
	code, body, hd := r.get(t, "/gb/rom/"+m.Token)
	sum := sha256.Sum256(body)
	if code != 200 || hex.EncodeToString(sum[:]) != m.SHA256 || hd.Get("X-Calamaio-Sha256") != m.SHA256 {
		t.Fatalf("download: %d, %d byte", code, len(body))
	}
	// il servizio segna la consegna appena finita la risposta: si aspetta
	waitFor(t, 2*time.Second, func() bool { st := r.hub.Status()[0].Gb.Start; return st != nil && st.Done })
	if st := r.hub.Status()[0].Gb.Start; st.Served != 64<<10 {
		t.Fatalf("stato dopo il download: %+v", st)
	}
	if code, _, _ := r.get(t, "/gb/rom/"+m.Token); code != http.StatusGone {
		t.Fatalf("secondo download: %d", code)
	}
}

// Review Focus 1: download interrotto e ripreso con Range sullo stesso token.
func TestGbROMRangeResume(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	r.hub.StartGb(gbBoard, r.game.ID)
	m := awaitStart(t, d)
	code, a, hd := r.get(t, "/gb/rom/"+m.Token, "Range", "bytes=0-9999")
	if code != http.StatusPartialContent || len(a) != 10000 || hd.Get("Content-Range") != "bytes 0-9999/65536" {
		t.Fatalf("prima parte: %d %d %q", code, len(a), hd.Get("Content-Range"))
	}
	waitFor(t, 2*time.Second, func() bool { return r.hub.Status()[0].Gb.Start.Served == 10000 })
	if r.hub.Status()[0].Gb.Start.Done {
		t.Fatal("segnata consegnata dopo la prima parte")
	}
	code, b, _ := r.get(t, "/gb/rom/"+m.Token, "Range", "bytes=10000-")
	if code != http.StatusPartialContent || len(b) != 65536-10000 {
		t.Fatalf("resto: %d %d", code, len(b))
	}
	sum := sha256.Sum256(append(a, b...))
	if hex.EncodeToString(sum[:]) != m.SHA256 {
		t.Fatal("ROM ricomposta diversa")
	}
	waitFor(t, 2*time.Second, func() bool { return r.hub.Status()[0].Gb.Start.Done })
	if code, _, _ := r.get(t, "/gb/rom/"+m.Token, "Range", "bytes=0-9"); code != http.StatusGone {
		t.Fatalf("dopo la consegna: %d", code)
	}
}

func TestGbROMTokens(t *testing.T) {
	r := newGbRig(t, func(h *Hub) { h.GbTokenTTL = time.Second })
	d, _ := r.gbDisplay(t)
	for _, p := range []string{"/gb/rom/" + strings.Repeat("a", 32), "/gb/rom/corto", "/gb/rom/" + strings.Repeat("A", 32)} {
		if code, _, _ := r.get(t, p); code != http.StatusNotFound {
			t.Errorf("%s: %d", p, code)
		}
	}
	r.hub.StartGb(gbBoard, r.game.ID)
	old := awaitStart(t, d)
	// IP diverso: 403, il token resta buono
	r.hub.mu.Lock()
	r.hub.gb.roms[old.Token].ip = "10.0.0.9"
	r.hub.mu.Unlock()
	if code, _, _ := r.get(t, "/gb/rom/"+old.Token); code != http.StatusForbidden {
		t.Fatalf("altro IP: %d", code)
	}
	// un avvio nuovo revoca il token del precedente
	r.hub.StartGb(gbBoard, r.game.ID)
	m := awaitStart(t, d)
	if code, _, _ := r.get(t, "/gb/rom/"+old.Token); code != http.StatusNotFound {
		t.Fatalf("token revocato: %d", code)
	}
	time.Sleep(1200 * time.Millisecond)
	if code, _, _ := r.get(t, "/gb/rom/"+m.Token); code != http.StatusGone {
		t.Fatalf("token scaduto: %d", code)
	}
}

func TestGbPick(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	d.Send(`{"t":"gb-pick","v":1,"id":"` + r.game.ID + `"}`)
	if m := awaitStart(t, d); m.ID != r.game.ID {
		t.Fatalf("gb-start per %s", m.ID)
	}
	// gioco che non c'è più: di nuovo l'elenco
	d.Send(`{"t":"gb-pick","v":1,"id":"0000000000000000"}`)
	if _, err := d.Await("gb-list", 3*time.Second); err != nil {
		t.Fatal(err)
	}
}

func TestStartGbErrors(t *testing.T) {
	r := newGbRig(t, nil)
	if _, err := r.hub.StartGb(gbBoard, r.game.ID); !errors.Is(err, ErrNotConnected) {
		t.Fatalf("scheda assente: %v", err)
	}
	r.gbDisplay(t)
	if _, err := r.hub.StartGb(gbBoard, "0000000000000000"); !errors.Is(err, ErrGbUnknown) {
		t.Fatalf("gioco sconosciuto: %v", err)
	}
	r.hub.mu.Lock()
	r.hub.otaInit()
	r.hub.otaJobs[gbBoard] = &otaJob{id: "o1", board: gbBoard, stage: OtaReceive}
	r.hub.mu.Unlock()
	if _, err := r.hub.StartGb(gbBoard, r.game.ID); !errors.Is(err, ErrGbOTA) {
		t.Fatalf("durante l'OTA: %v", err)
	}
	// scheda non autenticata
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d2, err := fakedisplay.Dial(ctx, r.url, "calamaio-0000", nil, false)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d2.Close)
	d2.Await("welcome", 2*time.Second)
	if _, err := r.hub.StartGb("calamaio-0000", r.game.ID); !errors.Is(err, ErrNotAuthed) {
		t.Fatalf("non autenticata: %v", err)
	}
	off := newRig(t, nil)
	if _, err := off.hub.StartGb(gbBoard, r.game.ID); !errors.Is(err, ErrGbOff) {
		t.Fatalf("senza giochi: %v", err)
	}
}

// Review Focus 4: una ROM cambiata sul disco non parte mai.
func TestStartGbTampered(t *testing.T) {
	r := newGbRig(t, nil)
	r.gbDisplay(t)
	p := filepath.Join(r.lib.Dir, r.game.ID+".gb")
	b, _ := os.ReadFile(p)
	b[0x300] ^= 1
	os.WriteFile(p, b, 0o600)
	_, err := r.hub.StartGb(gbBoard, r.game.ID)
	var blk *BlockedError
	if !errors.As(err, &blk) || !strings.Contains(err.Error(), "cambiata sul disco") {
		t.Fatalf("ROM alterata: %v", err)
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/display/ -run 'TestStartGb|TestGbROM|TestGbPick' -v`
Expected: FAIL (`undefined: ErrGbUnknown`, `r.hub.gb.roms[...].ip undefined` …)

- [ ] **Step 3: Write minimal implementation**

`service/internal/display/gb_start.go` (tutto il file):

```go
package display

import (
	"bytes"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"calamaio/internal/m5"
)

// Avvio di un gioco (gb-start) e download della ROM: GET /gb/rom/<token> sulla porta dei display,
// token casuale valido GbTokenTTL (10 minuti) e solo per l'IP della scheda (come /ota/<token>). Range ammesso per la
// ripresa: il token si consuma quando l'ultimo byte è stato consegnato.

var (
	ErrGbOff     = errors.New("giochi non disponibili in questo servizio")
	ErrGbUnknown = errors.New("gioco sconosciuto: non è nella cartella dei giochi")
	ErrGbOTA     = errors.New("c'è un aggiornamento remoto in corso su questa scheda: aspetta che finisca")
)

type gbStart struct {
	board, id, title, sha string
	size                  int64
	at                    time.Time
	rom                   []byte // liberata quando la ROM è consegnata tutta o l'avvio è sostituito
	served                int64
	done, restore         bool
	romTok, saveTok       string
}

func (st *gbStart) view() *GbStartView {
	if st == nil {
		return nil
	}
	return &GbStartView{ID: st.id, Title: st.title, Size: st.size, At: st.at.UnixMilli(), Served: st.served,
		Done: st.done, Restore: st.restore}
}

type gbRomTok struct {
	st   *gbStart
	ip   string
	exp  time.Time
	used bool
}

// gbOtaRetry: offerta rifiutata per una partita in corso, da rifare una volta a gb-state {playing: null}
// (Step 6–8 di questo task; campo gbHub.otaRetry del task 6).
type gbOtaRetry struct {
	job   *otaJob
	img   OTAImage
	start func(board string, img OTAImage) (*OtaView, error)
}

// gbRevoke toglie i token dell'avvio precedente di board. Con h.mu.
func (h *Hub) gbRevoke(board string) {
	if old := h.gb.starts[board]; old != nil {
		delete(h.gb.roms, old.romTok)
		delete(h.gb.downs, old.saveTok)
		old.rom = nil
	}
}

// gbSweep toglie i token scaduti da più di un GbTokenTTL (fino ad allora rispondono 410). Con h.mu.
func (h *Hub) gbSweep(now time.Time) {
	for k, t := range h.gb.roms {
		if now.Sub(t.exp) > h.GbTokenTTL {
			delete(h.gb.roms, k)
		}
	}
	h.gbSweepSaves(now)
}

// StartGb manda gb-start alla scheda: dalla webapp, dalla CLI o in risposta a gb-pick.
func (h *Hub) StartGb(board, id string) (*GbStartView, error) {
	if h.Gb == nil {
		return nil, ErrGbOff
	}
	g, ok := h.Gb.Get(id)
	if !ok {
		return nil, ErrGbUnknown
	}
	rom, err := h.Gb.ReadROM(id)
	if err != nil {
		return nil, &BlockedError{Msg: "la ROM di " + g.Title + " non si può mandare: " + err.Error()}
	}
	sv, saveData, restore := h.Gb.PendingRestore(id)

	h.mu.Lock()
	defer h.mu.Unlock()
	h.gbInit()
	if h.stopping || h.closed || h.farewell {
		return nil, ErrStopping
	}
	c := h.target(board)
	if c == nil {
		return nil, ErrNotConnected
	}
	if !c.authed || c.sk == nil {
		return nil, ErrNotAuthed
	}
	if j := h.otaJobs[board]; j != nil && !otaFinal(j.stage) {
		return nil, ErrGbOTA
	}
	now := h.Now()
	h.gbSweep(now)
	h.gbRevoke(board)
	st := &gbStart{board: board, id: id, title: g.Title, sha: g.SHA256, size: int64(len(rom)), at: now, rom: rom,
		romTok: m5.Hex(m5.Random(16))}
	if restore {
		st.saveTok = h.gbAddDown(c, id, sv.CRC32, saveData, now)
		st.restore = st.saveTok != ""
	}
	if !h.enqueueOut(c, lazyMsg(func() any {
		h.mu.Lock()
		defer h.mu.Unlock()
		if !c.authed || c.sk == nil || h.gb.starts[board] != st {
			return nil
		}
		return GbStartMsg{T: "gb-start", V: ProtocolVersion, ID: st.id, Size: st.size, SHA256: st.sha, Token: st.romTok,
			SaveToken: st.saveTok, Mac: m5.Hex(m5.GbStartMac(c.sk, st.id, st.size, st.sha, st.romTok, st.saveTok))}
	})) {
		delete(h.gb.downs, st.saveTok)
		return nil, ErrBusy
	}
	h.gb.roms[st.romTok] = &gbRomTok{st: st, ip: c.info.IP, exp: now.Add(h.GbTokenTTL)}
	h.gb.starts[board] = st
	h.Log.Printf("giochi: avvio di %s (%s, %d byte, sha256 %s…) su %s%s", g.Title, id, st.size, st.sha[:12], board,
		map[bool]string{true: ", con ripristino del salvataggio", false: ""}[st.restore])
	go h.changed()
	return st.view(), nil
}

// gbCountWriter conta i byte scritti e ricorda il codice di risposta.
type gbCountWriter struct {
	http.ResponseWriter
	code int
	n    int64
}

func (c *gbCountWriter) WriteHeader(code int) {
	if c.code == 0 {
		c.code = code
	}
	c.ResponseWriter.WriteHeader(code)
}

func (c *gbCountWriter) Write(b []byte) (int, error) {
	if c.code == 0 {
		c.code = http.StatusOK
	}
	n, err := c.ResponseWriter.Write(b)
	c.n += int64(n)
	return n, err
}

// ServeGbROM: GET /gb/rom/{token}. 404 token sconosciuto, 410 consegnato o scaduto, 403 IP diverso da
// quello della scheda (il token resta valido). Range ammesso (http.ServeContent).
func (h *Hub) ServeGbROM(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	h.gbInit()
	t := h.gb.roms[tok]
	now := h.Now()
	switch {
	case len(tok) != 32 || !m5.IsLowerHex(tok) || t == nil:
		h.mu.Unlock()
		h.Log.Printf("giochi: download della ROM con un token sconosciuto da %s", ip)
		http.Error(w, "token sconosciuto", http.StatusNotFound)
		return
	case t.used:
		h.mu.Unlock()
		http.Error(w, "ROM già consegnata", http.StatusGone)
		return
	case now.After(t.exp):
		h.mu.Unlock()
		h.Log.Printf("giochi: download della ROM da %s con il token scaduto: la scheda deve chiedere di nuovo il gioco", ip)
		http.Error(w, "token scaduto", http.StatusGone)
		return
	case ip != t.ip:
		h.mu.Unlock()
		h.Log.Printf("giochi: download della ROM rifiutato a %s: il token vale solo per %s", ip, t.ip)
		http.Error(w, "non per questo indirizzo", http.StatusForbidden)
		return
	}
	st := t.st
	rom := st.rom
	h.mu.Unlock()
	if rom == nil {
		http.Error(w, "avvio chiuso", http.StatusGone)
		return
	}
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Sha256", st.sha)
	cw := &gbCountWriter{ResponseWriter: w}
	http.ServeContent(cw, r, "", time.Time{}, bytes.NewReader(rom))
	// consegnata tutta: risposta completa che arriva all'ultimo byte
	whole := r.Method == http.MethodGet && cw.n > 0 && strconv.FormatInt(cw.n, 10) == hd.Get("Content-Length") &&
		(cw.code == http.StatusOK || cw.code == http.StatusPartialContent &&
			strings.HasSuffix(hd.Get("Content-Range"), fmt.Sprintf("-%d/%d", len(rom)-1, len(rom))))
	h.mu.Lock()
	st.served += cw.n
	if whole {
		t.used, st.done, st.rom = true, true, nil
	}
	h.mu.Unlock()
	rg := r.Header.Get("Range")
	if rg == "" {
		rg = "intera"
	}
	h.Log.Printf("giochi: %d byte della ROM %s verso %s (%s)%s", cw.n, st.id, ip, rg,
		map[bool]string{true: ": consegnata tutta", false: ""}[whole])
	go h.changed()
}
```

`service/internal/display/gb_save.go` (scheletro più largo, sostituito dal task 8):

```go
package display

import "time"

type gbDownTok struct{}
type gbUpTok struct{}

// onGbSave: task 8.
func (h *Hub) onGbSave(c *conn, m *gbIn) {}

// gbAddDown: task 8 (ripristino non ancora disponibile).
func (h *Hub) gbAddDown(c *conn, id string, crc uint32, data []byte, now time.Time) string { return "" }

// gbSweepSaves: task 8.
func (h *Hub) gbSweepSaves(now time.Time) {}
```

`service/internal/display/hub.go`, `Handler()`, dopo la rotta `/ota/{token}`:

```go
	mux.HandleFunc("GET /gb/rom/{token}", h.ServeGbROM) // ROM di un gioco Game Boy (gb_start.go)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/display/ -v -run 'TestGb|TestStartGb' && go test ./...`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add service/internal/display/gb_start.go service/internal/display/gb_save.go service/internal/display/gb_start_test.go service/internal/display/hub.go
git commit -m "Hub: gb-start con token monouso e download della ROM con ripresa (Range)"
```

- [ ] **Step 6: Write the failing test (OTA a fine partita)**

Contratto, «OTA e partita»: durante la partita la scheda rifiuta `ota-offer` con `ota-status {phase: "rifiutato", error: "partita in corso: riprova a fine partita"}` (piano B, Task 10); il servizio tiene l'immagine e rifà l'offerta una volta a `gb-state {playing: null}`. In fondo a `service/internal/display/gb_start_test.go`:

```go
// Contratto «OTA e partita»: offerta rifiutata per la partita, rifatta una volta a gb-state {playing: null}.
func TestGbOtaRetryAfterGame(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	img := OTAImage{Bin: []byte("firmware nuovo"), Version: "0.12.0", Device: "heltec213", Source: "prova"}
	got := make(chan OTAImage, 4)
	r.hub.mu.Lock()
	r.hub.otaInit()
	r.hub.gbInit()
	r.hub.gb.restartOTA = func(board string, im OTAImage) (*OtaView, error) {
		if board != gbBoard {
			t.Errorf("scheda %s", board)
		}
		got <- im
		return nil, nil
	}
	r.hub.otaJobs[gbBoard] = &otaJob{id: "o1", board: gbBoard, stage: OtaOffer, img: img}
	r.hub.mu.Unlock()
	d.Send(`{"t":"gb-state","v":1,"playing":"` + r.game.ID + `"}`)
	d.Send(`{"t":"ota-status","v":1,"id":"o1","phase":"rifiutato","error":"` + OtaPlayingErr + `"}`)
	waitFor(t, 2*time.Second, func() bool {
		r.hub.mu.Lock()
		defer r.hub.mu.Unlock()
		return r.hub.otaJobs[gbBoard].stage == OtaRejected
	})
	select {
	case <-got:
		t.Fatal("offerta rifatta durante la partita")
	case <-time.After(200 * time.Millisecond):
	}
	d.Send(`{"t":"gb-state","v":1,"playing":null}`)
	select {
	case im := <-got:
		if im.Version != "0.12.0" || string(im.Bin) != "firmware nuovo" {
			t.Fatalf("immagine: %+v", im)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("offerta non rifatta a fine partita")
	}
	d.Send(`{"t":"gb-state","v":1,"playing":null}`) // una volta sola
	select {
	case <-got:
		t.Fatal("offerta rifatta due volte")
	case <-time.After(300 * time.Millisecond):
	}
	// un rifiuto con un altro motivo non si riprova
	r.hub.mu.Lock()
	r.hub.otaJobs[gbBoard] = &otaJob{id: "o2", board: gbBoard, stage: OtaOffer, img: img}
	r.hub.mu.Unlock()
	d.Send(`{"t":"ota-status","v":1,"id":"o2","phase":"rifiutato","error":"batteria sotto il 50%"}`)
	d.Send(`{"t":"gb-state","v":1,"playing":null}`)
	select {
	case <-got:
		t.Fatal("rifatta un'offerta rifiutata per un altro motivo")
	case <-time.After(300 * time.Millisecond):
	}
}
```

Run: `cd service && go test ./internal/display/ -run TestGbOtaRetryAfterGame -v`
Expected: FAIL (`r.hub.gb.restartOTA undefined` o `unknown field img in struct literal of type otaJob`)

- [ ] **Step 7: Write minimal implementation**

`service/internal/display/ota.go`:
1. in `type otaJob struct`, dopo `bin []byte`: `img OTAImage // immagine offerta, per rifarla dopo una partita (gb_start.go); liberata da otaEnd`;
2. in `StartOTA`, nel letterale `j := &otaJob{…}`, dopo `bin: img.Bin`: `img: img,`;
3. in `otaEnd`, dopo `j.bin = nil`: `j.img = OTAImage{}`;
4. in `onOtaStatus`, nel `case OtaRejected, OtaError:`, subito prima di `h.otaEnd(j, m.Phase, why)`:

```go
		if m.Phase == OtaRejected && m.Error == OtaPlayingErr {
			h.gbOtaRetryLater(j) // giochi: l'offerta si rifà a fine partita (gb_start.go)
		}
```

In fondo a `service/internal/display/gb_start.go` (il tipo `gbOtaRetry` c'è già dallo Step 3):

```go
// OtaPlayingErr: motivo con cui la scheda rifiuta ota-offer durante una partita (firmware, core/ota kPlaying).
const OtaPlayingErr = "partita in corso: riprova a fine partita"

// gbOtaRetryLater ricorda l'offerta rifiutata (prima di otaEnd, che libera l'immagine). Con h.mu.
func (h *Hub) gbOtaRetryLater(j *otaJob) {
	h.gbInit()
	if j.img.Bin == nil {
		return
	}
	h.gb.otaRetry[j.board] = &gbOtaRetry{job: j, img: j.img}
	h.Log.Printf("ota %s: %s sta giocando, l'offerta si rifà a fine partita", j.id, j.board)
}

// gbTakeOtaRetry: l'offerta da rifare per board, una volta sola e solo se intanto non è partito un altro
// aggiornamento. Con h.mu.
func (h *Hub) gbTakeOtaRetry(board string) *gbOtaRetry {
	h.gbInit()
	rt := h.gb.otaRetry[board]
	delete(h.gb.otaRetry, board)
	if rt == nil || h.otaJobs[board] != rt.job {
		return nil
	}
	rt.start = h.gb.restartOTA
	if rt.start == nil {
		rt.start = h.StartOTA
	}
	return rt
}
```

`service/internal/display/gb.go`, in `onGb`, il caso `"gb-state"` diventa:

```go
	case "gb-state":
		c.gbKnown, c.gbPlaying = true, m.Playing
		var rt *gbOtaRetry
		if m.Playing == "" {
			rt = h.gbTakeOtaRetry(board) // contratto «OTA e partita»
		}
		h.mu.Unlock()
		go h.changed()
		if rt != nil {
			go func() {
				if _, err := rt.start(board, rt.img); err != nil {
					h.Log.Printf("giochi: partita finita su %s, l'aggiornamento non riparte: %v", board, err)
					return
				}
				h.Log.Printf("giochi: partita finita su %s, aggiornamento %s offerto di nuovo", board, rt.img.Version)
			}()
		}
```

- [ ] **Step 8: Run test to verify it passes, commit**

Run: `cd service && go test ./internal/display/ -v -run 'TestGb|TestStartGb|TestOta' && go test -race ./internal/display/ && go test ./...`
Expected: PASS (anche i test dell'OTA esistenti: `img` in più nel job non cambia niente fuori dai giochi)

```bash
git add service/internal/display/ota.go service/internal/display/gb.go service/internal/display/gb_start.go service/internal/display/gb_start_test.go
git commit -m "Hub: aggiornamento rifiutato per una partita in corso, rifatto a fine partita"
```

---
### Task 8: salvataggi — `gb-save`, `PUT /gb/save`, `gb-save-ok`, ripristino

**Files:**
- Modify (sostituisci tutto lo scheletro): `service/internal/display/gb_save.go`
- Modify: `service/internal/display/hub.go` (`Handler`: rotte `GET` e `PUT /gb/save/{token}`)
- Test: `service/internal/display/gb_save_test.go`

**Interfaces:**
- Consumes: `GbLibrary.HasSave/PutSave/ClearRestore` (task 6), `gbHub`, `gbIn`, `gbSaveOkMsg` (task 6), `gbStart`, `gbSweep`, `gbRomTok` (task 7), `m5.GbSaveOkMac` (task 4).
- Produces:
  - `func (h *Hub) ServeGbSaveUpload(w http.ResponseWriter, r *http.Request)` (`PUT /gb/save/{token}`)
  - `func (h *Hub) ServeGbSave(w http.ResponseWriter, r *http.Request)` (`GET /gb/save/{token}`)
  - le funzioni dello scheletro (`onGbSave`, `gbAddDown`, `gbSweepSaves`) con la stessa firma, ora vere

- [ ] **Step 1: Write the failing test**

`service/internal/display/gb_save_test.go`:

```go
package display

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"hash/crc32"
	"net/http"
	"strconv"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/m5"
)

func (r *gbRig) put(t *testing.T, tok string, body []byte) int {
	t.Helper()
	req, _ := http.NewRequest(http.MethodPut, r.srv.URL+"/gb/save/"+tok, bytes.NewReader(body))
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	return resp.StatusCode
}

// announce manda gb-save e aspetta che il servizio l'abbia registrato (con token) o elaborato.
func (r *gbRig) announce(t *testing.T, d *fakedisplay.Display, id string, size int, crc uint32, tok string) {
	t.Helper()
	msg := fmt.Sprintf(`{"t":"gb-save","v":1,"id":"%s","size":%d,"crc32":%d`, id, size, crc)
	if tok != "" {
		msg += `,"token":"` + tok + `"`
	}
	before := r.hub.Ignored()
	d.Send(msg + "}")
	if tok == "" {
		time.Sleep(200 * time.Millisecond)
		return
	}
	waitFor(t, 2*time.Second, func() bool {
		r.hub.mu.Lock()
		defer r.hub.mu.Unlock()
		_, seen := r.hub.gb.seen[tok]
		return seen || r.hub.Ignored() > before
	})
}

func saveOks(d *fakedisplay.Display) int {
	n := 0
	for _, f := range d.Frames() {
		if strings.Contains(f, `"t":"gb-save-ok"`) {
			n++
		}
	}
	return n
}

func TestGbSaveUpload(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	data := bytes.Repeat([]byte("salvataggio"), 745)[:8192]
	crc := crc32.ChecksumIEEE(data)
	tok := m5.Hex(m5.Random(16))
	r.announce(t, d, r.game.ID, len(data), crc, tok)
	if code := r.put(t, tok, data); code != http.StatusNoContent {
		t.Fatalf("PUT: %d", code)
	}
	if code := r.put(t, tok, data); code != http.StatusGone {
		t.Fatalf("PUT dopo un caricamento riuscito: %d", code)
	}
	e, err := d.Await("gb-save-ok", 3*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	b, _ := json.Marshal(e.Msg)
	var ok gbSaveOkMsg
	json.Unmarshal(b, &ok)
	if ok.ID != r.game.ID || ok.CRC32 != crc || ok.Mac != m5.Hex(m5.GbSaveOkMac(d.SK(), ok.ID, ok.CRC32)) {
		t.Fatalf("gb-save-ok: %s", b)
	}
	validate(t, compileSchema(t), "gb-save-ok", b)
	if list, _ := r.lib.Saves(r.game.ID); len(list) != 1 || list[0].CRC32 != crc {
		t.Fatalf("copie: %+v", list)
	}
	// alla connessione dopo: solo annuncio, già copiato → gb-save-ok senza caricamento
	n := saveOks(d)
	r.announce(t, d, r.game.ID, len(data), crc, "")
	waitFor(t, 2*time.Second, func() bool { return saveOks(d) == n+1 })
}

// Review Focus 5: salvataggi falsi o ripetuti → niente su disco, nessun gb-save-ok.
func TestGbSaveRejects(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	data := bytes.Repeat([]byte{7}, 4096)
	crc := crc32.ChecksumIEEE(data)
	if code := r.put(t, strings.Repeat("b", 32), data); code != http.StatusNotFound {
		t.Fatalf("token sconosciuto: %d", code)
	}
	// CRC diverso: 400, e il token resta buono (contratto: fino al primo caricamento riuscito)
	tok := m5.Hex(m5.Random(16))
	r.announce(t, d, r.game.ID, len(data), crc+1, tok)
	if code := r.put(t, tok, data); code != http.StatusBadRequest {
		t.Fatalf("crc sbagliato: %d", code)
	}
	if code := r.put(t, tok, data); code != http.StatusBadRequest {
		t.Fatalf("secondo tentativo con lo stesso token: %d", code)
	}
	// stesso token in un altro gb-save: ignorato
	before := r.hub.Ignored()
	d.Send(fmt.Sprintf(`{"t":"gb-save","v":1,"id":"%s","size":%d,"crc32":%d,"token":"%s"}`, r.game.ID, len(data), crc, tok))
	waitFor(t, 2*time.Second, func() bool { return r.hub.Ignored() == before+1 })
	// dimensione diversa da quella annunciata
	tok2 := m5.Hex(m5.Random(16))
	r.announce(t, d, r.game.ID, len(data)+1, crc, tok2)
	if code := r.put(t, tok2, data); code != http.StatusBadRequest {
		t.Fatalf("dimensione: %d", code)
	}
	// IP diverso: 403 e il token resta buono
	tok3 := m5.Hex(m5.Random(16))
	r.announce(t, d, r.game.ID, len(data), crc, tok3)
	r.hub.mu.Lock()
	r.hub.gb.ups[tok3].ip = "10.0.0.9"
	r.hub.mu.Unlock()
	if code := r.put(t, tok3, data); code != http.StatusForbidden {
		t.Fatalf("altro IP: %d", code)
	}
	// annuncio senza token di un salvataggio che il PC non ha: niente risposta
	r.announce(t, d, r.game.ID, len(data), crc, "")
	// scheda non autenticata
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d2, err := fakedisplay.Dial(ctx, r.url, "calamaio-0000", nil, false)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d2.Close)
	d2.Await("welcome", 2*time.Second)
	tok4 := m5.Hex(m5.Random(16))
	d2.Send(fmt.Sprintf(`{"t":"gb-save","v":1,"id":"%s","size":%d,"crc32":%d,"token":"%s"}`, r.game.ID, len(data), crc, tok4))
	time.Sleep(200 * time.Millisecond)
	if code := r.put(t, tok4, data); code != http.StatusNotFound {
		t.Fatalf("token da una scheda non autenticata: %d", code)
	}
	if list, _ := r.lib.Saves(r.game.ID); len(list) != 0 {
		t.Fatalf("salvato qualcosa: %+v", list)
	}
	if saveOks(d) != 0 || saveOks(d2) != 0 {
		t.Fatal("gb-save-ok senza un salvataggio vero")
	}
}

func TestGbRestore(t *testing.T) {
	r := newGbRig(t, nil)
	d, _ := r.gbDisplay(t)
	r.lib.PutSave(r.game.ID, []byte("vecchio"))
	time.Sleep(5 * time.Millisecond)
	r.lib.PutSave(r.game.ID, []byte("nuovo"))
	chosen, err := r.lib.SetRestore(r.game.ID, 2)
	if err != nil {
		t.Fatal(err)
	}
	v, err := r.hub.StartGb(gbBoard, r.game.ID)
	if err != nil || !v.Restore {
		t.Fatalf("%+v %v", v, err)
	}
	m := awaitStart(t, d)
	if len(m.SaveToken) != 32 {
		t.Fatalf("saveToken: %+v", m)
	}
	code, body, hd := r.get(t, "/gb/save/"+m.SaveToken)
	if code != 200 || string(body) != "vecchio" || hd.Get("X-Calamaio-Crc32") != strconv.FormatUint(uint64(chosen.CRC32), 10) {
		t.Fatalf("ripristino: %d %q %v", code, body, hd)
	}
	// il servizio chiude il ripristino subito dopo aver scritto l'ultimo byte
	waitFor(t, 2*time.Second, func() bool { _, _, ok := r.lib.PendingRestore(r.game.ID); return !ok })
	if code, _, _ := r.get(t, "/gb/save/"+m.SaveToken); code != http.StatusGone {
		t.Fatalf("secondo download: %d", code)
	}
	// avvio dopo: niente più ripristino
	r.hub.StartGb(gbBoard, r.game.ID)
	if m := awaitStart(t, d); m.SaveToken != "" {
		t.Fatalf("ripristino ripetuto: %+v", m)
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/display/ -run 'TestGbSave|TestGbRestore' -v`
Expected: FAIL (`r.hub.gb.ups[tok3].ip undefined (type *gbUpTok has no field ip)` …)

- [ ] **Step 3: Write minimal implementation**

`service/internal/display/gb_save.go` (tutto il file):

```go
package display

import (
	"hash/crc32"
	"io"
	"net/http"
	"strconv"
	"time"

	"calamaio/internal/m5"
)

// Salvataggi dei giochi: gb-save annuncia un salvataggio (con il token scelto dalla scheda), PUT
// /gb/save/<token> lo carica, gb-save-ok conferma. Il ripristino scelto dalla webapp viaggia con il
// saveToken di gb-start e GET /gb/save/<token>. Stesse regole dei token della ROM: GbTokenTTL, solo l'IP
// della scheda; il caricamento si accetta finché un PUT completo non riesce, il ripristino si consegna una volta.

// gbUpTok: caricamento annunciato da gb-save. Buono finché un PUT completo non riesce (used), entro exp.
type gbUpTok struct {
	board, id, ip string
	size          int64
	crc           uint32
	exp           time.Time
	used          bool // un caricamento è riuscito: 410
	busy          bool // un PUT è in corso: un secondo PUT con lo stesso token riceve 409
}

// gbDownTok: salvataggio da ripristinare (gb-start.saveToken).
type gbDownTok struct {
	board, id, ip string
	data          []byte
	crc           uint32
	exp           time.Time
	used          bool
}

// gbTokStatus: controlli comuni dei token (0 = buono).
func gbTokStatus(tok string, missing, used, expired, otherIP bool) (int, string) {
	switch {
	case len(tok) != 32 || !m5.IsLowerHex(tok) || missing:
		return http.StatusNotFound, "token sconosciuto"
	case used:
		return http.StatusGone, "token già usato"
	case expired:
		return http.StatusGone, "token scaduto"
	case otherIP:
		return http.StatusForbidden, "non per questo indirizzo"
	}
	return 0, ""
}

// gbAddDown registra il token del ripristino per la scheda di c. Con h.mu.
func (h *Hub) gbAddDown(c *conn, id string, crc uint32, data []byte, now time.Time) string {
	tok := m5.Hex(m5.Random(16))
	h.gb.downs[tok] = &gbDownTok{board: c.info.Board, id: id, ip: c.info.IP, data: data, crc: crc, exp: now.Add(h.GbTokenTTL)}
	return tok
}

// gbSweepSaves: token scaduti da più di un GbTokenTTL; token di caricamento visti da più di un giorno. Con h.mu.
func (h *Hub) gbSweepSaves(now time.Time) {
	for k, t := range h.gb.downs {
		if now.Sub(t.exp) > h.GbTokenTTL {
			delete(h.gb.downs, k)
		}
	}
	for k, t := range h.gb.ups {
		if now.Sub(t.exp) > h.GbTokenTTL {
			delete(h.gb.ups, k)
		}
	}
	for k, at := range h.gb.seen {
		if now.Sub(at) > 24*time.Hour {
			delete(h.gb.seen, k)
		}
	}
}

// queueGbSaveOk: gb-save-ok con la SK del momento dell'invio. Con h.mu.
func (h *Hub) queueGbSaveOk(c *conn, id string, crc uint32) {
	h.enqueueOut(c, lazyMsg(func() any {
		h.mu.Lock()
		defer h.mu.Unlock()
		if !c.authed || c.sk == nil {
			return nil
		}
		return gbSaveOkMsg{T: "gb-save-ok", V: ProtocolVersion, ID: id, CRC32: crc, Mac: m5.Hex(m5.GbSaveOkMac(c.sk, id, crc))}
	}))
}

// onGbSave: la scheda (autenticata: lo controlla onGb) annuncia un salvataggio. Senza token è solo un
// annuncio: gb-save-ok se il PC ha già una copia uguale. Con il token si aspetta il PUT.
func (h *Hub) onGbSave(c *conn, m *gbIn) {
	if m.Token == "" {
		if h.Gb.HasSave(m.ID, m.Size, m.CRC32) {
			h.mu.Lock()
			h.queueGbSaveOk(c, m.ID, m.CRC32)
			h.mu.Unlock()
			h.Log.Printf("giochi: il salvataggio di %s su %s (crc %08x) è già sul PC", m.ID, c.info.Board, m.CRC32)
		} else {
			h.Log.Printf("giochi: %s annuncia un salvataggio nuovo di %s senza token: non caricato", c.info.Board, m.ID)
		}
		return
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	h.gbInit()
	now := h.Now()
	h.gbSweep(now)
	if _, seen := h.gb.seen[m.Token]; seen || h.gb.roms[m.Token] != nil || h.gb.downs[m.Token] != nil {
		h.ignoreLocked(c.info.ID, "gb-save con un token già usato")
		return
	}
	h.gb.seen[m.Token] = now
	h.gb.ups[m.Token] = &gbUpTok{board: c.info.Board, id: m.ID, ip: c.info.IP, size: m.Size, crc: m.CRC32,
		exp: now.Add(h.GbTokenTTL)}
	h.Log.Printf("giochi: %s carica il salvataggio di %s (%d byte, crc %08x)", c.info.Board, m.ID, m.Size, m.CRC32)
}

// ServeGbSaveUpload: PUT /gb/save/{token}. 204 e poi gb-save-ok; 400 dimensione o CRC diversi da
// gb-save (niente salvato, il token resta buono fino alla scadenza: contratto); 409 un altro PUT con lo stesso
// token è in corso; 404, 410, 403 come la ROM. Dopo il primo caricamento riuscito il token è morto (410).
func (h *Hub) ServeGbSaveUpload(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	h.gbInit()
	t := h.gb.ups[tok]
	now := h.Now()
	if code, why := gbTokStatus(tok, t == nil, t != nil && t.used, t != nil && now.After(t.exp), t != nil && ip != t.ip); code != 0 {
		h.mu.Unlock()
		h.Log.Printf("giochi: caricamento di un salvataggio da %s rifiutato: %s", ip, why)
		http.Error(w, why, code)
		return
	}
	if t.busy {
		h.mu.Unlock()
		http.Error(w, "caricamento già in corso", http.StatusConflict)
		return
	}
	t.busy = true
	h.mu.Unlock()
	fail := func(why string) {
		h.mu.Lock()
		t.busy = false // il token resta buono: la scheda può riprovare (contratto)
		h.mu.Unlock()
		h.Log.Printf("giochi: salvataggio di %s da %s scartato: %s", t.id, t.board, why)
		http.Error(w, why, http.StatusBadRequest)
	}
	if r.ContentLength != t.size {
		fail("dimensione diversa da quella di gb-save")
		return
	}
	data, err := io.ReadAll(http.MaxBytesReader(w, r.Body, t.size))
	if err != nil || int64(len(data)) != t.size {
		fail("salvataggio incompleto")
		return
	}
	if crc32.ChecksumIEEE(data) != t.crc {
		fail("CRC diverso da quello di gb-save")
		return
	}
	sv, fresh, err := h.Gb.PutSave(t.id, data)
	if err != nil {
		h.mu.Lock()
		t.busy = false
		h.mu.Unlock()
		h.Log.Printf("giochi: salvataggio di %s non scritto: %v", t.id, err)
		http.Error(w, "salvataggio non scritto sul PC", http.StatusInternalServerError)
		return
	}
	w.WriteHeader(http.StatusNoContent)
	h.mu.Lock()
	t.used, t.busy = true, false // caricamento completo riuscito: da qui 410
	if c := h.target(t.board); c != nil && c.authed {
		h.queueGbSaveOk(c, t.id, t.crc)
	}
	h.mu.Unlock()
	h.Log.Printf("giochi: salvataggio di %s da %s al sicuro (%s, copia nuova: %v)", t.id, t.board, sv.Name, fresh)
	go h.changed()
}

// ServeGbSave: GET /gb/save/{token}, il salvataggio scelto per il ripristino. Consegnato tutto, il
// ripristino si chiude; se la scrittura si interrompe il token resta buono fino alla scadenza.
func (h *Hub) ServeGbSave(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	h.gbInit()
	t := h.gb.downs[tok]
	now := h.Now()
	if code, why := gbTokStatus(tok, t == nil, t != nil && t.used, t != nil && now.After(t.exp), t != nil && ip != t.ip); code != 0 {
		h.mu.Unlock()
		h.Log.Printf("giochi: ripristino di un salvataggio verso %s rifiutato: %s", ip, why)
		http.Error(w, why, code)
		return
	}
	head := r.Method == http.MethodHead
	if !head {
		t.used = true
	}
	data := t.data
	h.mu.Unlock()
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Content-Length", strconv.Itoa(len(data)))
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Crc32", strconv.FormatUint(uint64(t.crc), 10))
	w.WriteHeader(http.StatusOK)
	if head {
		return
	}
	if n, err := w.Write(data); err != nil || n != len(data) {
		h.mu.Lock()
		t.used = false
		h.mu.Unlock()
		h.Log.Printf("giochi: ripristino di %s verso %s interrotto dopo %d byte", t.id, ip, n)
		return
	}
	h.Gb.ClearRestore(t.id, t.crc)
	h.Log.Printf("giochi: salvataggio di %s ripristinato su %s (crc %08x)", t.id, t.board, t.crc)
	go h.changed()
}
```

`service/internal/display/hub.go`, `Handler()`, dopo la rotta `/gb/rom/{token}`:

```go
	mux.HandleFunc("GET /gb/save/{token}", h.ServeGbSave)       // salvataggio da ripristinare (gb_save.go)
	mux.HandleFunc("PUT /gb/save/{token}", h.ServeGbSaveUpload) // salvataggio dalla scheda (gb_save.go)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/display/ -v -run 'TestGb|TestStartGb' && go test -race ./internal/display/ && go test ./...`
Expected: PASS (anche con `-race`)

- [ ] **Step 5: Commit**

```bash
git add service/internal/display/gb_save.go service/internal/display/gb_save_test.go service/internal/display/hub.go
git commit -m "Hub: salvataggi dei giochi (gb-save, PUT, gb-save-ok) e ripristino con saveToken"
```

---
### Task 9: API locale `/api/gb/…` e avvio nel servizio

**Files:**
- Create: `service/internal/api/gb.go`
- Modify: `service/internal/api/server.go` (campi `Gb`, `GbHub`; rotte)
- Modify: `service/internal/api/web.go` (`statusBody.Gb`, `status()`)
- Modify: `service/internal/server/server.go` (`Options.GbDir`, apertura, `watchGb`, `Running.Gb`)
- Modify: `service/cmd/calamaio/main.go` (`serve`: `o.GbDir`; uso)
- Modify: `docs/PROTOCOLLO.md` (righe della tabella «API locale della webapp»)
- Test: `service/internal/server/gb_test.go`

**Interfaces:**
- Consumes: `gbgames.Library` e i suoi metodi (task 2–3), `display.Hub.StartGb`, `GbListChanged`, `ErrGbUnknown`, `ErrGbOTA`, `ErrGbOff`, `BlockedError` (task 6–7); `guard`, `postGuard`, `bodyGuard`, `deleteGuard`, `decodeStrict`, `writeJSON`, `errBody`, `commandError` (esistenti).
- Produces:
  - `type api.GbControl interface { StartGb(board, id string) (*display.GbStartView, error); GbListChanged() }`
  - `type api.GbStatus struct { Rev int64; Count int }` (JSON `rev, count`), in `GET /api/status` come `gb` (null senza giochi)
  - campi `api.Server.Gb *gbgames.Library`, `api.Server.GbHub GbControl`
  - `server.Options.GbDir string` (vuoto = giochi spenti), `server.Running.Gb *gbgames.Library`
  - Rotte (forme che usano la CLI del task 10 e la webapp del task 11):

| Rotta | Risposta |
|---|---|
| `GET /api/gb/games` | 200 `{games: [Game], dir, maxSize, problems}` |
| `POST /api/gb/games?name=<file>` (`application/octet-stream`) | 201 `{game, existed: false}` · 200 `{game, existed: true}` · 400 · 413 · 415 |
| `DELETE /api/gb/games/{id}` | 204 · 404 |
| `POST /api/displays/{board}/gb/start` `{id}` | 202 `{start: GbStartView}` · 400 · 404 gioco · 409 scheda (non collegata, non autenticata, OTA in corso, ROM illeggibile) |
| `GET /api/gb/saves/{id}` | 200 `{saves: [Save], restore: Save \| null, game: Game \| null}` · 404 id non valido |
| `POST /api/gb/saves/{id}/restore` `{n, board?}` | 200 `{restore: Save, start: GbStartView \| null, warning?}` · 400 · 404 |

- [ ] **Step 1: Write the failing test**

`service/internal/server/gb_test.go`:

```go
package server

import (
	"net/http"
	"net/url"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/gbgames"
)

// Giochi Game Boy dalla API locale: servizio vero, cartella temporanea, richieste come la webapp.

func startGb(t *testing.T) (*started, *web, string) {
	t.Helper()
	dir := filepath.Join(t.TempDir(), "games")
	s := startOpts(t, false, 0, func(o *Options) {
		o.GbDir = dir
		o.AvatarPoll = 50 * time.Millisecond
	})
	return s, newWeb(t, s), dir
}

// upload come la webapp: octet-stream, stessa origine.
func (w *web) upload(name string, rom []byte, hdr ...string) (int, map[string]any) {
	w.t.Helper()
	h := append([]string{"Origin", w.base, "Sec-Fetch-Site", "same-origin"}, hdr...)
	code, m, _ := w.do(http.MethodPost, "/api/gb/games?name="+url.QueryEscape(name), "application/octet-stream", string(rom), h...)
	return code, m
}

func (w *web) del(path string, sameOrigin bool) int {
	w.t.Helper()
	var h []string
	if sameOrigin {
		h = []string{"Origin", w.base, "Sec-Fetch-Site", "same-origin"}
	}
	code, _, _ := w.do(http.MethodDelete, path, "", "", h...)
	return code
}

func TestGbAPI(t *testing.T) {
	s, w, _ := startGb(t)
	code, m, _ := w.do(http.MethodGet, "/api/gb/games", "", "")
	if code != 200 || len(m["games"].([]any)) != 0 || m["maxSize"].(float64) != gbgames.MaxROM {
		t.Fatalf("elenco vuoto: %d %v", code, m)
	}
	rom := gbgames.FakeROM(gbgames.FakeROMOptions{Title: "HOMEBREW DEMO", CartType: 0x03, SizeCode: 1, RAMCode: 2})
	code, m = w.upload("demo.gb", rom)
	if code != http.StatusCreated || m["existed"] != false {
		t.Fatalf("caricamento: %d %v", code, m)
	}
	id := m["game"].(map[string]any)["id"].(string)
	if g, ok := s.run.Gb.Get(id); !ok || g.Title != "HOMEBREW DEMO" {
		t.Fatalf("nella cartella: %+v", g)
	}
	if code, m = w.upload("copia.gb", rom); code != 200 || m["existed"] != true {
		t.Fatalf("doppione: %d %v", code, m)
	}
	cases := []struct {
		name  string
		code  int
		file  string
		rom   []byte
		ctype string
		hdr   []string
		want  string
	}{
		{"solo Color", 400, "c.gbc", gbgames.FakeROM(gbgames.FakeROMOptions{Title: "C", CGB: 0xC0}), "", nil, "Game Boy Color"},
		{"4 MB", 413, "big.gb", gbgames.FakeROM(gbgames.FakeROMOptions{Title: "B", SizeCode: 7}), "", nil, "2 MB"},
		{"non una ROM", 400, "x.gb", make([]byte, 40000), "", nil, "logo"},
		{"estensione", 400, "x.zip", rom, "", nil, ".gb"},
		{"JSON", 415, "x.gb", rom, "application/json", nil, "octet-stream"},
		{"altro sito", 403, "x.gb", rom, "", []string{"Origin", "http://cattivo.example"}, "origine"},
	}
	for _, c := range cases {
		var got int
		var body map[string]any
		if c.ctype != "" {
			got, body, _ = w.do(http.MethodPost, "/api/gb/games?name="+c.file, c.ctype, string(c.rom), "Origin", w.base)
		} else {
			got, body = w.upload(c.file, c.rom, c.hdr...)
		}
		if got != c.code || !strings.Contains(body["error"].(string), c.want) {
			t.Errorf("%s: %d %v", c.name, got, body)
		}
	}
	if code, m, _ := w.do(http.MethodPost, "/api/gb/games", "application/octet-stream", string(rom), "Origin", w.base); code != 400 {
		t.Fatalf("senza name: %d %v", code, m)
	}
	gb := w.status()["gb"].(map[string]any)
	if gb["count"].(float64) != 1 || gb["rev"].(float64) < 1 {
		t.Fatalf("status.gb: %v", gb)
	}
	// avvio senza scheda, gioco sconosciuto, corpo sbagliato
	if code, m := w.post("/api/displays/calamaio-3f2a/gb/start", `{"id":"`+id+`"}`); code != 409 || !strings.Contains(m["error"].(string), "non collegato") {
		t.Fatalf("senza scheda: %d %v", code, m)
	}
	if code, _ := w.post("/api/displays/calamaio-3f2a/gb/start", `{"id":"0000000000000000"}`); code != 404 {
		t.Fatalf("gioco sconosciuto: %d", code)
	}
	if code, _ := w.post("/api/displays/calamaio-3f2a/gb/start", `{"gioco":"x"}`); code != 400 {
		t.Fatalf("corpo sbagliato: %d", code)
	}
	// salvataggi
	code, m, _ = w.do(http.MethodGet, "/api/gb/saves/"+id, "", "")
	if code != 200 || len(m["saves"].([]any)) != 0 || m["restore"] != nil || m["game"] == nil {
		t.Fatalf("salvataggi: %d %v", code, m)
	}
	if code, _ := w.post("/api/gb/saves/"+id+"/restore", `{"n":1}`); code != 404 {
		t.Fatalf("ripristino senza copie: %d", code)
	}
	s.run.Gb.PutSave(id, []byte("partita"))
	code, m = w.post("/api/gb/saves/"+id+"/restore", `{"n":1,"board":"calamaio-3f2a"}`)
	if code != 200 || m["restore"] == nil || m["start"] != nil || !strings.Contains(m["warning"].(string), "non è partito") {
		t.Fatalf("ripristino: %d %v", code, m)
	}
	if _, _, ok := s.run.Gb.PendingRestore(id); !ok {
		t.Fatal("ripristino non in attesa")
	}
	// rimozione: serve la stessa origine
	if code := w.del("/api/gb/games/"+id, false); code != 403 {
		t.Fatalf("DELETE senza origine: %d", code)
	}
	if code := w.del("/api/gb/games/"+id, true); code != 204 {
		t.Fatalf("DELETE: %d", code)
	}
	if code := w.del("/api/gb/games/"+id, true); code != 404 {
		t.Fatalf("seconda DELETE: %d", code)
	}
}

// Review Focus 3: id malformati → 404, mai un file.
func TestGbAPIBadIDs(t *testing.T) {
	_, w, _ := startGb(t)
	for _, id := range []string{"ABCDEF0123456789", "123", "..%2F..%2Fetc%2Fpasswd", "0123456789abcdef0"} {
		if code, _, _ := w.do(http.MethodGet, "/api/gb/saves/"+id, "", ""); code != 404 {
			t.Errorf("GET saves %s: %d", id, code)
		}
		if code, _ := w.post("/api/gb/saves/"+id+"/restore", `{"n":1}`); code != 404 {
			t.Errorf("restore %s: %d", id, code)
		}
		if code := w.del("/api/gb/games/"+id, true); code != 404 {
			t.Errorf("DELETE %s: %d", id, code)
		}
	}
}

// Una cartuccia aggiunta da un altro processo (CLI col servizio spento) arriva alla webapp da sola.
func TestGbWatch(t *testing.T) {
	_, w, dir := startGb(t)
	other, err := gbgames.Open(dir)
	if err != nil {
		t.Fatal(err)
	}
	other.Add("altro.gb", gbgames.FakeROM(gbgames.FakeROMOptions{Title: "ALTRO"}))
	waitStatus(t, w, func(st map[string]any) bool {
		gb, _ := st["gb"].(map[string]any)
		return gb != nil && gb["count"].(float64) == 1
	})
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/server/ -run 'TestGb' -v`
Expected: FAIL (`unknown field GbDir in struct literal of type Options`)

- [ ] **Step 3: Write minimal implementation**

`service/internal/api/gb.go`:

```go
package api

import (
	"errors"
	"io"
	"net/http"
	"net/url"

	"calamaio/internal/display"
	"calamaio/internal/gbgames"
)

// Giochi Game Boy per la webapp e per `calamaio gb` (docs/PROTOCOLLO.md, «API locale» e «Giochi»).
// Solo 127.0.0.1 con le guardie di sempre; il caricamento è application/octet-stream come i backup.

// GbControl: avvio dei giochi e avvisi alle schede (display.Hub).
type GbControl interface {
	StartGb(board, id string) (*display.GbStartView, error)
	GbListChanged()
}

// GbStatus: parte `gb` di GET /api/status (la webapp ricarica l'elenco quando rev cambia).
type GbStatus struct {
	Rev   int64 `json:"rev"`
	Count int   `json:"count"`
}

func (s *Server) gbStatus() *GbStatus {
	if s.Gb == nil {
		return nil
	}
	return &GbStatus{Rev: s.Gb.Rev(), Count: len(s.Gb.List())}
}

func (s *Server) noGb(w http.ResponseWriter) bool {
	if s.Gb == nil {
		writeJSON(w, http.StatusNotFound, errBody("giochi non disponibili"))
		return true
	}
	return false
}

func (s *Server) gbChanged() {
	if s.GbHub != nil {
		s.GbHub.GbListChanged()
	}
}

// GET /api/gb/games
func (s *Server) apiGbGames(w http.ResponseWriter, _ *http.Request) {
	if s.noGb(w) {
		return
	}
	problems := append([]string{}, s.Gb.Problems...)
	writeJSON(w, http.StatusOK, map[string]any{"games": s.Gb.List(), "dir": s.Gb.Dir, "maxSize": gbgames.MaxROM, "problems": problems})
}

// POST /api/gb/games?name=<file>, corpo = la ROM.
func (s *Server) apiGbAdd(w http.ResponseWriter, r *http.Request) {
	if s.noGb(w) {
		return
	}
	q, err := url.ParseQuery(r.URL.RawQuery)
	if err != nil || len(q) != 1 || len(q["name"]) != 1 || q.Get("name") == "" {
		writeJSON(w, http.StatusBadRequest, errBody("serve ?name=<nome del file> e nient'altro"))
		return
	}
	if r.ContentLength > gbgames.MaxROM {
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody(gbgames.ErrTooLarge.Error()))
		return
	}
	rom, err := io.ReadAll(http.MaxBytesReader(w, r.Body, gbgames.MaxROM+1))
	var mbe *http.MaxBytesError
	switch {
	case errors.As(err, &mbe):
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody(gbgames.ErrTooLarge.Error()))
		return
	case err != nil:
		writeJSON(w, http.StatusBadRequest, errBody("lettura del file non riuscita"))
		return
	}
	g, existed, err := s.Gb.Add(q.Get("name"), rom)
	var inv *gbgames.InvalidError
	switch {
	case errors.Is(err, gbgames.ErrTooLarge):
		writeJSON(w, http.StatusRequestEntityTooLarge, errBody(err.Error()))
	case errors.As(err, &inv):
		writeJSON(w, http.StatusBadRequest, errBody(inv.Msg))
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	case existed:
		writeJSON(w, http.StatusOK, map[string]any{"game": g, "existed": true})
	default:
		if s.Log != nil {
			s.Log.Printf("giochi: aggiunta la cartuccia %s «%s» (%d byte, da %s)", g.ID, g.Title, g.Size, g.File)
		}
		w.Header().Set("Location", "/api/gb/games/"+g.ID)
		writeJSON(w, http.StatusCreated, map[string]any{"game": g, "existed": false})
		s.gbChanged()
	}
}

// DELETE /api/gb/games/{id}: i salvataggi restano.
func (s *Server) apiGbRemove(w http.ResponseWriter, r *http.Request) {
	if s.noGb(w) {
		return
	}
	id := r.PathValue("id")
	switch err := s.Gb.Remove(id); {
	case errors.Is(err, gbgames.ErrNotFound):
		writeJSON(w, http.StatusNotFound, errBody("Gioco sconosciuto."))
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	default:
		if s.Log != nil {
			s.Log.Printf("giochi: tolta la cartuccia %s (i salvataggi restano)", id)
		}
		w.Header().Set("Cache-Control", "no-store")
		w.WriteHeader(http.StatusNoContent)
		s.gbChanged()
	}
}

func gbError(w http.ResponseWriter, err error) {
	var blk *display.BlockedError
	switch {
	case errors.Is(err, display.ErrGbUnknown):
		writeJSON(w, http.StatusNotFound, errBody(err.Error()))
	case errors.Is(err, display.ErrGbOTA), errors.Is(err, display.ErrGbOff), errors.As(err, &blk):
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
	default:
		commandError(w, err)
	}
}

// POST /api/displays/{board}/gb/start {"id": "<16 cifre>"}
func (s *Server) apiGbStart(w http.ResponseWriter, r *http.Request) {
	if s.noGb(w) {
		return
	}
	var req struct {
		ID string `json:"id"`
	}
	if err := decodeStrict(r, &req); err != nil || !gbgames.ValidID(req.ID) {
		writeJSON(w, http.StatusBadRequest, errBody(`serve {"id": "<id del gioco, 16 cifre esadecimali>"}`))
		return
	}
	if s.GbHub == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrGbOff.Error()))
		return
	}
	v, err := s.GbHub.StartGb(r.PathValue("board"), req.ID)
	if err != nil {
		gbError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"start": v})
}

// GET /api/gb/saves/{id}
func (s *Server) apiGbSaves(w http.ResponseWriter, r *http.Request) {
	if s.noGb(w) {
		return
	}
	id := r.PathValue("id")
	list, err := s.Gb.Saves(id)
	if errors.Is(err, gbgames.ErrNotFound) {
		writeJSON(w, http.StatusNotFound, errBody("Gioco sconosciuto."))
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	out := map[string]any{"saves": list, "restore": nil, "game": nil}
	if sv, _, ok := s.Gb.PendingRestore(id); ok {
		out["restore"] = sv
	}
	if g, ok := s.Gb.Get(id); ok {
		out["game"] = g
	}
	writeJSON(w, http.StatusOK, out)
}

// POST /api/gb/saves/{id}/restore {"n": 2, "board": "calamaio-3f2a"}: la copia n parte con il prossimo
// avvio del gioco; con board il gioco parte subito su quella scheda.
func (s *Server) apiGbRestore(w http.ResponseWriter, r *http.Request) {
	if s.noGb(w) {
		return
	}
	id := r.PathValue("id")
	var req struct {
		N     int    `json:"n"`
		Board string `json:"board"`
	}
	if !gbgames.ValidID(id) {
		writeJSON(w, http.StatusNotFound, errBody("Gioco sconosciuto."))
		return
	}
	if err := decodeStrict(r, &req); err != nil || req.N < 1 {
		writeJSON(w, http.StatusBadRequest, errBody(`serve {"n": <copia, 1 = la più recente>, "board": "<scheda, facoltativa>"}`))
		return
	}
	sv, err := s.Gb.SetRestore(id, req.N)
	switch {
	case errors.Is(err, gbgames.ErrNoSave), errors.Is(err, gbgames.ErrNotFound):
		writeJSON(w, http.StatusNotFound, errBody("Salvataggio sconosciuto."))
		return
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	out := map[string]any{"restore": sv, "start": nil}
	if req.Board != "" {
		if _, ok := s.Gb.Get(id); !ok {
			out["warning"] = "ripristino pronto, ma la cartuccia non è più sul PC: ricaricala per giocare"
		} else if s.GbHub == nil {
			out["warning"] = "ripristino pronto, ma il gioco non è partito: " + display.ErrGbOff.Error()
		} else if v, err := s.GbHub.StartGb(req.Board, id); err != nil {
			out["warning"] = "ripristino pronto, ma il gioco non è partito: " + err.Error()
		} else {
			out["start"] = v
		}
	}
	if s.Log != nil {
		s.Log.Printf("giochi: ripristino della copia %d di %s (%s) al prossimo avvio", sv.N, id, sv.Name)
	}
	writeJSON(w, http.StatusOK, out)
}
```

`service/internal/api/server.go`, struct `Server`, dopo `Game *gamesvc.Service`:

```go
	// Giochi Game Boy (facoltativi, gb.go): cartella delle cartucce (nil = 404) e avvio sulle schede.
	Gb    *gbgames.Library
	GbHub GbControl
```

(import `"calamaio/internal/gbgames"`), e in `Handler()` dopo `mux.HandleFunc("DELETE /api/backups/{id}", …)`:

```go
	mux.HandleFunc("GET /api/gb/games", guard(s.apiGbGames))
	mux.HandleFunc("POST /api/gb/games", bodyGuard("application/octet-stream", s.apiGbAdd))
	mux.HandleFunc("DELETE /api/gb/games/{id}", deleteGuard(s.apiGbRemove))
	mux.HandleFunc("POST /api/displays/{board}/gb/start", postGuard(s.apiGbStart))
	mux.HandleFunc("GET /api/gb/saves/{id}", guard(s.apiGbSaves))
	mux.HandleFunc("POST /api/gb/saves/{id}/restore", postGuard(s.apiGbRestore))
```

`service/internal/api/web.go`, struct `statusBody`, dopo `LastCrash *diag.Crash`:

```go
	// Giochi Game Boy: impronta della cartella (null se il servizio non ha i giochi).
	Gb *GbStatus `json:"gb"`
```

e in `status()`, dopo `b.Avatars = s.avatarStatus()`:

```go
	b.Gb = s.gbStatus()
```

`service/internal/server/server.go`:
- import `"calamaio/internal/gbgames"`;
- in `Options`, dopo `GameClock`:

```go
	// GbDir: cartella dei giochi Game Boy (vuota = giochi spenti, test); `calamaio serve` usa
	// <dati utente>/calamaio/games.
	GbDir string
```

- in `Running`, dopo `Game *gamesvc.Service`: `Gb *gbgames.Library`
- in `Run`, subito prima di `hub := display.NewHub(store, lg)`:

```go
	// Giochi Game Boy: cartella delle cartucce dell'utente.
	var gbLib *gbgames.Library
	if o.GbDir != "" {
		if l, err := gbgames.Open(o.GbDir); err != nil {
			lg.Printf("giochi: %v (giochi spenti)", err)
		} else {
			gbLib = l
			for _, p := range l.Problems {
				lg.Printf("giochi: %s", p)
			}
			lg.Printf("giochi: %d cartucce in %s", len(l.List()), o.GbDir)
		}
	}
```

- dopo `hub.Game = gsvc`:

```go
	if gbLib != nil {
		hub.Gb = gbLib // mai un *Library nil dentro l'interfaccia
	}
```

- dopo `as.Game = gsvc`:

```go
	if gbLib != nil {
		as.Gb, as.GbHub = gbLib, hub
	}
```

- dopo la goroutine di `watchAvatars`:

```go
	if gbLib != nil {
		wg.Add(1)
		go func() { defer wg.Done(); watchGb(runCtx, gbLib, hub, o.AvatarPoll) }()
	}
```

- in `o.Ready(Running{…})` aggiungi `Gb: gbLib`;
- in fondo al file:

```go
// watchGb: cartucce aggiunte o tolte da un altro processo (CLI con il servizio spento) arrivano a
// schede e webapp entro un intervallo. Quelle fatte con la API avvisano subito (GbListChanged).
func watchGb(ctx context.Context, l *gbgames.Library, hub *display.Hub, every time.Duration) {
	if every <= 0 {
		every = 5 * time.Second
	}
	tk := time.NewTicker(every)
	defer tk.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-tk.C:
			if l.Refresh() {
				hub.GbListChanged()
			}
		}
	}
}
```

`service/cmd/calamaio/main.go`, in `serve`, nel blocco `if d, err := config.DataDir(); err == nil {`, dopo la riga di `o.GameFile`:

```go
		o.GbDir = filepath.Join(d, "games")
```

e nell'uso (`usage`), dopo le righe di `calamaio accessory`:

```
  calamaio gb list [--json] | add FILE | rm ID | start ID [--board B] | saves ID [--restore N] [--board B]
                                    giochi Game Boy: le tue cartucce (o homebrew) sulla scheda
```

(il comando arriva nel task 10).

`docs/PROTOCOLLO.md`, sezione «API locale della webapp»: in fondo alla tabella delle rotte (dopo `GET /api/firmware/parts/{nome}`) aggiungi

```markdown
| `GET /api/gb/games` | **Giochi**: `{games: [Gioco], dir, maxSize, problems}`, per titolo |
| `POST /api/gb/games?name=<file>` | corpo = la ROM (`application/octet-stream`, stesse guardie, ≤ 2 MiB, `.gb` o `.gbc`) → 201 `{game, existed: false}` · 200 `{game, existed: true}` (stessa ROM già caricata, anche con un altro nome) · 400 intestazione non valida, gioco solo per Color, cartuccia non supportata · 413 · 415 |
| `DELETE /api/gb/games/{id}` | → 204 (i salvataggi restano) · 404 |
| `POST /api/displays/{board}/gb/start` | `{id}` → 202 `{start: {id, title, size, at, served, done, restore}}` · 404 gioco sconosciuto · 409 scheda non collegata o non autenticata, aggiornamento remoto in corso, ROM cambiata sul disco |
| `GET /api/gb/saves/{id}` | `{saves: [{n, name, size, crc32, at}], restore: Save \| null, game: Gioco \| null}`, la copia più recente per prima (`n` = 1) · 404 id non valido |
| `POST /api/gb/saves/{id}/restore` | `{n, board?}` → 200 `{restore, start, warning?}`: la copia `n` sostituisce il salvataggio della scheda al prossimo avvio del gioco (subito, con `board`) · 400 · 404 |
```

e dopo il paragrafo **Display** in `/api/status` aggiungi:

```markdown
**Giochi** in `/api/status`: `gb {rev, count}` (null se il servizio non ha i giochi; `rev` cambia a ogni modifica della cartella) e in ogni display `gb {known, playing, title, pad, start}` (`known`: la scheda ha mandato un messaggio `gb-*` su questa connessione; `playing` = id o `""`; `pad` = `off` · `pairing` · `connecting` · `ready` o `""`; `start` = ultimo avvio o null). **Gioco** in `/api/gb/games`: `{id, title, size, sha256, file, added, header: {title, cartType, mapper, battery, romSize, ramSize, cgb}}`.
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/server/ -run 'TestGb' -v && go test ./...`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add service/internal/api/gb.go service/internal/api/server.go service/internal/api/web.go service/internal/server/server.go service/internal/server/gb_test.go service/cmd/calamaio/main.go docs/PROTOCOLLO.md
git commit -m "API locale dei giochi: caricamento, elenco, rimozione, avvio, salvataggi e ripristino"
```

---
### Task 10: `calamaio gb list|add|rm|start|saves` e prova di tutto il giro con una scheda finta

**Files:**
- Create: `service/internal/client/gb.go`
- Modify: `service/cmd/calamaio/main.go` (`case "gb"`)
- Test: `service/cmd/calamaio/gb_test.go`

**Interfaces:**
- Consumes: rotte del task 9; `gbgames` (task 1–3) per il servizio spento; `display.GbStartView`; helper esistenti del pacchetto `client`: `baseURL`, `httpClient`, `errNoService`, `apiError`, `usageError`, `runCmd`, `GameIO`, `parseAvatarArgs` (flag in qualunque posizione: `parseAvatarArgs(args, withValue, boolean []string) (avatarArgs{pos, flags, bools}, error)`), `getAPIStatus`, `wifiBoard`; nei test di `cmd/calamaio`: `startSvcWith`, `svc.cli`, `svc.otaDisplay`, `await`, `checkSchema`, `waitFor`, `freePort`, `cliResult`, costante `board`.
- Produces:
  - `type client.GbIO struct { Out, Err io.Writer; Local *gbgames.Library }`
  - `func client.Gb(c config.Config, args []string, o GbIO) int` (0 ok, 1 errore, 2 uso)
  - `func client.GbText(games []gbgames.Game, dir string) string`, `func client.GbSavesText(id string, g *gbgames.Game, saves []gbgames.Save, restore *gbgames.Save) string`

- [ ] **Step 1: Write the failing test**

`service/cmd/calamaio/gb_test.go`:

```go
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"hash/crc32"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/gbgames"
	"calamaio/internal/m5"
	"calamaio/internal/server"
)

// Giochi Game Boy dall'esterno: servizio vero, scheda finta abbinata attraverso la API, `calamaio gb`
// vero lanciato come processo, download e caricamenti HTTP come li fa la scheda.

func gbSvc(t *testing.T) *svc {
	t.Helper()
	return startSvcWith(t, 0, func(dir string, o *server.Options) {
		o.GbDir = filepath.Join(dir, "dati", "games") // la stessa che la CLI usa col servizio spento
	})
}

// boardHTTP: base delle rotte della scheda (porta dei display).
func (s *svc) boardHTTP() string {
	return strings.TrimSuffix(strings.Replace(s.wsURL, "ws://", "http://", 1), "/ws")
}

func httpDo(t *testing.T, method, url string, body []byte) (int, []byte) {
	t.Helper()
	var rd io.Reader
	if body != nil {
		rd = bytes.NewReader(body)
	}
	req, _ := http.NewRequest(method, url, rd)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, b
}

func TestCLIGb(t *testing.T) {
	s := gbSvc(t)
	rom := gbgames.FakeROM(gbgames.FakeROMOptions{Title: "HOMEBREW DEMO", CartType: 0x03, SizeCode: 1, RAMCode: 2})
	id, sha := gbgames.IDOf(rom)
	romPath := filepath.Join(t.TempDir(), "Demo Homebrew.gb")
	os.WriteFile(romPath, rom, 0o644)

	for _, args := range [][]string{{"gb"}, {"gb", "boh"}, {"gb", "add"}, {"gb", "saves", id, "--restore", "zero"}, {"gb", "saves", id, "--board", board}} {
		if r := s.cli("", args...); r.code != 2 || !strings.Contains(r.err, "uso:") {
			t.Errorf("%v: %+v", args, r)
		}
	}
	if r := s.cli("", "gb", "add", romPath); r.code != 0 || !strings.Contains(r.out, "Aggiunto: HOMEBREW DEMO ("+id) {
		t.Fatalf("add: %+v", r)
	}
	if r := s.cli("", "gb", "add", romPath); r.code != 0 || !strings.Contains(r.out, "Già presente: HOMEBREW DEMO") {
		t.Fatalf("add ripetuto: %+v", r)
	}
	bad := filepath.Join(t.TempDir(), "color.gbc")
	os.WriteFile(bad, gbgames.FakeROM(gbgames.FakeROMOptions{Title: "C", CGB: 0xC0}), 0o644)
	if r := s.cli("", "gb", "add", bad); r.code != 1 || !strings.Contains(r.err, "solo per Game Boy Color") {
		t.Fatalf("add solo Color: %+v", r)
	}
	r := s.cli("", "gb", "list")
	if r.code != 0 || !strings.Contains(r.out, id+"  HOMEBREW DEMO") || !strings.Contains(r.out, "64 KB  MBC1 · salvataggi") {
		t.Fatalf("list: %+v", r)
	}
	var games []gbgames.Game
	if r := s.cli("", "gb", "list", "--json"); r.code != 0 || json.Unmarshal([]byte(r.out), &games) != nil || len(games) != 1 {
		t.Fatalf("list --json: %+v", r)
	}
	if r := s.cli("", "gb", "start", id); r.code != 1 || !strings.Contains(r.err, "nessuna scheda collegata") {
		t.Fatalf("start senza scheda: %+v", r)
	}

	d := s.otaDisplay(fakedisplay.Options{}, nil) // abbinata attraverso la API, autenticata
	await(t, d, "gb-list")
	if r := s.cli("", "gb", "start", id); r.code != 0 || !strings.Contains(r.out, "Avvio di HOMEBREW DEMO su "+board) {
		t.Fatalf("start: %+v", r)
	}
	e := await(t, d, "gb-start")
	tok := e.Msg["token"].(string)
	if e.Msg["mac"] != m5.Hex(m5.GbStartMac(d.SK(), id, int64(len(rom)), sha, tok, "")) {
		t.Fatalf("mac di gb-start: %v", e.Msg)
	}
	if code, b := httpDo(t, http.MethodGet, s.boardHTTP()+"/gb/rom/"+tok, nil); code != 200 || !bytes.Equal(b, rom) {
		t.Fatalf("download della ROM: %d, %d byte", code, len(b))
	}

	// la scheda copia il suo salvataggio sul PC
	save := bytes.Repeat([]byte{0x5A}, 8192)
	stok := m5.Hex(m5.Random(16))
	d.Send(fmt.Sprintf(`{"t":"gb-save","v":1,"id":"%s","size":8192,"crc32":%d,"token":"%s"}`, id, crc32.ChecksumIEEE(save), stok))
	var code int
	waitFor(t, "caricamento accettato", func() bool {
		code, _ = httpDo(t, http.MethodPut, s.boardHTTP()+"/gb/save/"+stok, save)
		return code != http.StatusNotFound // 404 finché il servizio non ha letto gb-save
	})
	if code != http.StatusNoContent {
		t.Fatalf("PUT del salvataggio: %d", code)
	}
	await(t, d, "gb-save-ok")
	if r := s.cli("", "gb", "saves", id); r.code != 0 || !strings.Contains(r.out, "Salvataggi di HOMEBREW DEMO ("+id+")") ||
		!strings.Contains(r.out, "  1  ") {
		t.Fatalf("saves: %+v", r)
	}
	r = s.cli("", "gb", "saves", id, "--restore", "1", "--board", board)
	if r.code != 0 || !strings.Contains(r.out, "Ripristino pronto: la copia 1") || !strings.Contains(r.out, "Gioco avviato su "+board) {
		t.Fatalf("restore: %+v", r)
	}
	e = await(t, d, "gb-start")
	saveTok, _ := e.Msg["saveToken"].(string)
	if code, b := httpDo(t, http.MethodGet, s.boardHTTP()+"/gb/save/"+saveTok, nil); code != 200 || !bytes.Equal(b, save) {
		t.Fatalf("download del ripristino: %d", code)
	}

	if r := s.cli("", "gb", "rm", id); r.code != 0 || !strings.Contains(r.out, "I salvataggi restano") {
		t.Fatalf("rm: %+v", r)
	}
	e = await(t, d, "gb-list")
	if g, _ := e.Msg["games"].([]any); len(g) != 0 {
		t.Fatalf("gb-list dopo rm: %v", e.Msg)
	}
	if r := s.cli("", "gb", "list"); r.code != 0 || !strings.Contains(r.out, "Nessun gioco") {
		t.Fatalf("list vuota: %+v", r)
	}
	checkSchema(t, d) // ogni frame ricevuto dalla scheda rispetta lo schema
}

// Servizio spento: la CLI lavora sulla cartella e lo dice.
func TestCLIGbOffline(t *testing.T) {
	data, cfg := t.TempDir(), t.TempDir()
	port := freePort(t)
	run := func(args ...string) cliResult {
		ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
		defer cancel()
		cmd := exec.CommandContext(ctx, os.Args[0], args...)
		cmd.Env = append(os.Environ(), "CALAMAIO_TEST_MAIN=1", "CALAMAIO_CONFIG_DIR="+cfg, "CALAMAIO_DATA_DIR="+data,
			"CALAMAIO_API_PORT="+strconv.Itoa(port))
		var out, errb bytes.Buffer
		cmd.Stdout, cmd.Stderr = &out, &errb
		_ = cmd.Run()
		return cliResult{out.String(), errb.String(), cmd.ProcessState.ExitCode()}
	}
	rom := gbgames.FakeROM(gbgames.FakeROMOptions{Title: "SPENTO"})
	id, _ := gbgames.IDOf(rom)
	p := filepath.Join(t.TempDir(), "spento.gb")
	os.WriteFile(p, rom, 0o644)
	if r := run("gb", "add", p); r.code != 0 || !strings.Contains(r.out, "Aggiunto: SPENTO") || !strings.Contains(r.err, "servizio Calamaio spento") {
		t.Fatalf("add: %+v", r)
	}
	if _, err := os.Stat(filepath.Join(data, "games", id+".gb")); err != nil {
		t.Fatalf("ROM nella cartella dei dati: %v", err)
	}
	if r := run("gb", "list"); r.code != 0 || !strings.Contains(r.out, "SPENTO") {
		t.Fatalf("list: %+v", r)
	}
	if r := run("gb", "saves", id); r.code != 0 || !strings.Contains(r.out, "Nessun salvataggio") {
		t.Fatalf("saves: %+v", r)
	}
	if r := run("gb", "start", id); r.code != 1 {
		t.Fatalf("start a servizio spento: %+v", r)
	}
	if r := run("gb", "rm", id); r.code != 0 {
		t.Fatalf("rm: %+v", r)
	}
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./cmd/calamaio/ -run 'TestCLIGb' -v`
Expected: FAIL (`gb` non è ancora un comando: `calamaio` stampa l'uso generale ed esce con 2, quindi `gb add` non dice «Aggiunto»)

- [ ] **Step 3: Write minimal implementation**

`service/internal/client/gb.go`:

```go
package client

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"calamaio/internal/config"
	"calamaio/internal/display"
	"calamaio/internal/gbgames"
)

// Comandi `calamaio gb …` (giochi Game Boy). Con il servizio acceso passano dalla API locale (schede e
// webapp si aggiornano subito); con il servizio spento lavorano su <dati>/calamaio/games con le stesse
// regole (gbgames). Avviare un gioco vuole il servizio.

const gbUsage = `uso:
  calamaio gb list [--json]                  cartucce nella cartella dei giochi del PC
  calamaio gb add <file.gb>                  aggiunge una cartuccia: solo file tuoi (copie delle tue
                                             cartucce o homebrew); Calamaio non scarica giochi
  calamaio gb rm <id>                        toglie una cartuccia (i salvataggi restano)
  calamaio gb start <id> [--board B]         avvia il gioco sulla scheda (la ROM viaggia solo se la
                                             scheda non ce l'ha già)
  calamaio gb saves <id> [--json]            salvataggi copiati dalla scheda (ultime 3 copie)
  calamaio gb saves <id> --restore N [--board B]
                                             rimette la copia N al prossimo avvio (subito, con --board)
`

// GbIO: dove leggono e scrivono i comandi (test).
type GbIO struct {
	Out, Err io.Writer
	// Local: cartella da usare col servizio spento (nil = <dati>/calamaio/games).
	Local *gbgames.Library
}

func (o GbIO) local() (*gbgames.Library, error) {
	if o.Local != nil {
		return o.Local, nil
	}
	d, err := config.DataDir()
	if err != nil {
		return nil, err
	}
	return gbgames.Open(filepath.Join(d, "games"))
}

func noteGbLocal(o GbIO) {
	fmt.Fprintln(o.Err, "(servizio Calamaio spento: cartella dei giochi letta e scritta direttamente; la scheda vedrà le modifiche quando il servizio riparte)")
}

// gbAPI: come gameAPI, con Origin (le DELETE vogliono la stessa origine; qui il client è la CLI locale).
func gbAPI(c config.Config, method, path, ctype string, body []byte, out any) error {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second) // 2 MB in loopback: abbondante
	defer cancel()
	var rd io.Reader
	if body != nil {
		rd = bytes.NewReader(body)
	}
	req, err := http.NewRequestWithContext(ctx, method, baseURL(c)+path, rd)
	if err != nil {
		return err
	}
	req.Header.Set("Origin", baseURL(c))
	if body != nil {
		req.Header.Set("Content-Type", ctype)
	}
	resp, err := httpClient.Do(req)
	if err != nil {
		return errNoService
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(io.LimitReader(resp.Body, 4<<20))
	if resp.StatusCode == http.StatusNotFound &&
		(strings.Contains(string(b), "API sconosciuta") || strings.Contains(string(b), "giochi non disponibili")) {
		return errNoService
	}
	if resp.StatusCode/100 != 2 {
		var e struct {
			Error string `json:"error"`
		}
		_ = json.Unmarshal(b, &e)
		if e.Error == "" {
			e.Error = fmt.Sprintf("stato %d", resp.StatusCode)
		}
		return &apiError{Status: resp.StatusCode, Msg: e.Error}
	}
	if out != nil && len(b) > 0 {
		return json.Unmarshal(b, out)
	}
	return nil
}

// Gb esegue `calamaio gb …`.
func Gb(c config.Config, args []string, o GbIO) int {
	if len(args) == 0 {
		fmt.Fprint(o.Err, gbUsage)
		return 2
	}
	sub, rest := args[0], args[1:]
	var err error
	switch sub {
	case "list":
		err = gbList(c, rest, o)
	case "add":
		err = gbAdd(c, rest, o)
	case "rm", "remove":
		err = gbRemove(c, rest, o)
	case "start":
		err = gbStart(c, rest, o)
	case "saves":
		err = gbSaves(c, rest, o)
	default:
		err = usageError{"comando sconosciuto: gb " + sub}
	}
	return runCmd(err, GameIO{Out: o.Out, Err: o.Err}, gbUsage)
}

func fmtKB(n int64) string {
	if n >= 1<<20 {
		return strings.Replace(strconv.FormatFloat(float64(n)/(1<<20), 'f', 1, 64), ".", ",", 1) + " MB"
	}
	return strconv.FormatInt((n+1023)/1024, 10) + " KB"
}

// GbText: elenco leggibile delle cartucce.
func GbText(games []gbgames.Game, dir string) string {
	var b strings.Builder
	if len(games) == 0 {
		fmt.Fprintf(&b, "Nessun gioco nella cartella %s.\nAggiungine uno con: calamaio gb add <file.gb> (solo cartucce tue o homebrew).\n", dir)
		return b.String()
	}
	fmt.Fprintf(&b, "Giochi (%d) in %s:\n", len(games), dir)
	for _, g := range games {
		extra := g.Header.Mapper
		if g.Header.Battery {
			extra += " · salvataggi"
		}
		fmt.Fprintf(&b, "  %s  %-16s  %7s  %s\n", g.ID, g.Title, fmtKB(g.Size), extra)
	}
	return b.String()
}

// GbSavesText: copie di un gioco, la più recente per prima.
func GbSavesText(id string, g *gbgames.Game, saves []gbgames.Save, restore *gbgames.Save) string {
	title := id
	if g != nil {
		title = g.Title + " (" + id + ")"
	}
	var b strings.Builder
	if len(saves) == 0 {
		fmt.Fprintf(&b, "Nessun salvataggio di %s sul PC: arrivano dalla scheda a ogni collegamento.\n", title)
		return b.String()
	}
	fmt.Fprintf(&b, "Salvataggi di %s (si tengono le ultime %d copie):\n", title, gbgames.SavesKept)
	for _, s := range saves {
		mark := ""
		if restore != nil && restore.Name == s.Name {
			mark = "  ← al prossimo avvio"
		}
		fmt.Fprintf(&b, "  %d  %s  %s  crc %08x%s\n", s.N, time.UnixMilli(s.At).Local().Format("02/01/2006 15:04"), fmtKB(s.Size), s.CRC32, mark)
	}
	return b.String()
}

func gbList(c config.Config, args []string, o GbIO) error {
	a, err := parseAvatarArgs(args, nil, []string{"json"})
	if err != nil || len(a.pos) > 0 {
		return usageError{"list vuole solo --json"}
	}
	var res struct {
		Games []gbgames.Game `json:"games"`
		Dir   string         `json:"dir"`
	}
	err = gbAPI(c, http.MethodGet, "/api/gb/games", "", nil, &res)
	if errors.Is(err, errNoService) {
		l, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		res.Games, res.Dir, err = l.List(), l.Dir, nil
		noteGbLocal(o)
	}
	if err != nil {
		return err
	}
	if a.bools["json"] {
		enc := json.NewEncoder(o.Out)
		enc.SetIndent("", "  ")
		return enc.Encode(res.Games)
	}
	fmt.Fprint(o.Out, GbText(res.Games, res.Dir))
	return nil
}

func gbAdd(c config.Config, args []string, o GbIO) error {
	a, err := parseAvatarArgs(args, nil, nil)
	if err != nil || len(a.pos) != 1 {
		return usageError{"add vuole il percorso di un file .gb"}
	}
	path := a.pos[0]
	st, err := os.Stat(path)
	if err != nil {
		return err
	}
	if st.Size() > gbgames.MaxROM {
		return gbgames.ErrTooLarge
	}
	rom, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	name := filepath.Base(path)
	var res struct {
		Game    gbgames.Game `json:"game"`
		Existed bool         `json:"existed"`
	}
	err = gbAPI(c, http.MethodPost, "/api/gb/games?name="+url.QueryEscape(name), "application/octet-stream", rom, &res)
	if errors.Is(err, errNoService) {
		l, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		res.Game, res.Existed, err = l.Add(name, rom)
		if err == nil {
			noteGbLocal(o)
		}
	}
	if err != nil {
		return err
	}
	if res.Existed {
		fmt.Fprintf(o.Out, "Già presente: %s (%s, %s).\n", res.Game.Title, res.Game.ID, fmtKB(res.Game.Size))
		return nil
	}
	fmt.Fprintf(o.Out, "Aggiunto: %s (%s, %s, %s).\n", res.Game.Title, res.Game.ID, fmtKB(res.Game.Size), res.Game.Header.Mapper)
	return nil
}

func gbRemove(c config.Config, args []string, o GbIO) error {
	a, err := parseAvatarArgs(args, nil, nil)
	if err != nil || len(a.pos) != 1 {
		return usageError{"rm vuole l'id del gioco (calamaio gb list)"}
	}
	id := a.pos[0]
	err = gbAPI(c, http.MethodDelete, "/api/gb/games/"+url.PathEscape(id), "", nil, nil)
	if errors.Is(err, errNoService) {
		l, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		if err = l.Remove(id); err == nil {
			noteGbLocal(o)
		}
	}
	if errors.Is(err, gbgames.ErrNotFound) {
		return fmt.Errorf("gioco sconosciuto: %s (calamaio gb list)", id)
	}
	if err != nil {
		return err
	}
	fmt.Fprintf(o.Out, "Tolto %s. I salvataggi restano: ricaricando la stessa cartuccia li ritrovi.\n", id)
	return nil
}

func gbStart(c config.Config, args []string, o GbIO) error {
	a, err := parseAvatarArgs(args, []string{"board"}, nil)
	if err != nil || len(a.pos) != 1 {
		return usageError{"start vuole l'id del gioco (calamaio gb list)"}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	st, err := getAPIStatus(ctx, c)
	if err != nil {
		return fmt.Errorf("per avviare un gioco serve il servizio Calamaio acceso (calamaio serve): %w", err)
	}
	d, err := wifiBoard(st.Displays, a.flags["board"])
	if err != nil {
		return err
	}
	body, _ := json.Marshal(map[string]string{"id": a.pos[0]})
	var res struct {
		Start display.GbStartView `json:"start"`
	}
	err = gbAPI(c, http.MethodPost, "/api/displays/"+url.PathEscape(d.Board)+"/gb/start", "application/json", body, &res)
	if errors.Is(err, errNoService) {
		return errors.New("questo servizio non ha i giochi: aggiornalo")
	}
	if err != nil {
		return err
	}
	fmt.Fprintf(o.Out, "Avvio di %s su %s: la scheda scarica la ROM (%s) solo se non ce l'ha già.\n", res.Start.Title, d.Board, fmtKB(res.Start.Size))
	return nil
}

func gbSaves(c config.Config, args []string, o GbIO) error {
	a, err := parseAvatarArgs(args, []string{"restore", "board"}, []string{"json"})
	if err != nil || len(a.pos) != 1 {
		return usageError{"saves vuole l'id del gioco"}
	}
	id := a.pos[0]
	if r, ok := a.flags["restore"]; ok {
		n, err := strconv.Atoi(r)
		if err != nil || n < 1 {
			return usageError{"--restore vuole il numero della copia (1 = la più recente)"}
		}
		return gbRestore(c, id, n, a.flags["board"], o)
	}
	if a.flags["board"] != "" {
		return usageError{"--board serve solo con --restore"}
	}
	var res struct {
		Saves   []gbgames.Save `json:"saves"`
		Restore *gbgames.Save  `json:"restore"`
		Game    *gbgames.Game  `json:"game"`
	}
	err = gbAPI(c, http.MethodGet, "/api/gb/saves/"+url.PathEscape(id), "", nil, &res)
	if errors.Is(err, errNoService) {
		l, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		if res.Saves, err = l.Saves(id); err == nil {
			if sv, _, ok := l.PendingRestore(id); ok {
				res.Restore = &sv
			}
			if g, ok := l.Get(id); ok {
				res.Game = &g
			}
			noteGbLocal(o)
		}
	}
	if errors.Is(err, gbgames.ErrNotFound) {
		return fmt.Errorf("id non valido: %s (calamaio gb list)", id)
	}
	if err != nil {
		return err
	}
	if a.bools["json"] {
		enc := json.NewEncoder(o.Out)
		enc.SetIndent("", "  ")
		return enc.Encode(res)
	}
	fmt.Fprint(o.Out, GbSavesText(id, res.Game, res.Saves, res.Restore))
	return nil
}

func gbRestore(c config.Config, id string, n int, board string, o GbIO) error {
	var res struct {
		Restore gbgames.Save         `json:"restore"`
		Start   *display.GbStartView `json:"start"`
		Warning string               `json:"warning"`
	}
	body, _ := json.Marshal(map[string]any{"n": n, "board": board})
	err := gbAPI(c, http.MethodPost, "/api/gb/saves/"+url.PathEscape(id)+"/restore", "application/json", body, &res)
	if errors.Is(err, errNoService) {
		if board != "" {
			return errors.New("per avviare il gioco serve il servizio Calamaio acceso: senza --board si prepara solo il ripristino")
		}
		l, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		if res.Restore, err = l.SetRestore(id, n); err == nil {
			noteGbLocal(o)
		}
	}
	var ae *apiError
	if errors.Is(err, gbgames.ErrNoSave) || errors.Is(err, gbgames.ErrNotFound) || errors.As(err, &ae) && ae.Status == http.StatusNotFound {
		return fmt.Errorf("salvataggio sconosciuto: copia %d di %s (calamaio gb saves %s)", n, id, id)
	}
	if err != nil {
		return err
	}
	fmt.Fprintf(o.Out, "Ripristino pronto: la copia %d (%s) sostituisce il salvataggio della scheda al prossimo avvio del gioco.\n",
		res.Restore.N, time.UnixMilli(res.Restore.At).Local().Format("02/01/2006 15:04"))
	if res.Start != nil {
		fmt.Fprintf(o.Out, "Gioco avviato su %s.\n", board)
	}
	if res.Warning != "" {
		fmt.Fprintln(o.Err, "!", res.Warning)
	}
	return nil
}
```

`service/cmd/calamaio/main.go`, nello `switch cmd`, dopo `case "accessory":`:

```go
	case "gb":
		c, _ := config.Load()
		os.Exit(client.Gb(c, args, client.GbIO{Out: os.Stdout, Err: os.Stderr}))
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./cmd/calamaio/ -run 'TestCLIGb' -v && go test ./...`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add service/internal/client/gb.go service/cmd/calamaio/main.go service/cmd/calamaio/gb_test.go
git commit -m "CLI: calamaio gb list, add, rm, start e saves (anche a servizio spento)"
```

---
### Task 11: pagina «Giochi» della webapp

**Files:**
- Create: `service/internal/webui/static/gb.html`
- Create: `service/internal/webui/static/assets/js/gb.js` (testi e controlli puri, senza DOM)
- Create: `service/internal/webui/static/assets/js/gb-page.js`
- Modify: `service/internal/webui/static/assets/js/api.js` (in fondo al file)
- Modify: `service/internal/webui/static/assets/app.css` (in fondo al file)
- Modify: `service/internal/webui/static/{index,avatars,game,device,firmware,editor}.html` (voce «Giochi» nella barra)
- Modify: `service/internal/webui/webui_test.go`
- Modify: `tools/webui-dev/test-units.mjs`

**Interfaces:**
- Consumes: rotte del task 9 (forme della tabella del task 9), `displays[].gb` di `/api/status` (`GbView` del task 6: `known, playing, title, pad, start{id,title,size,at,served,done,restore}`), `status.gb.rev` (task 9); `watchStatus`, `readJSON`, `postJSON`, `boardPath` (in `api.js`), `h`, `heroGoccia`, `setConn`, `fmtBytes` (in `ui.js`).
- Produces:
  - `gb.js`: `export const MAX_ROM`, `export function checkRomFile(name, size)` ('' = va bene, altrimenti il motivo), `export function padText(link)`, `export function playingText(gb)`, `export function startText(st)`, `export function fmtSaveDate(ms)`
  - `api.js`: `listGbGames()`, `uploadGbGame(name, data)`, `deleteGbGame(id)` → `{status, body}`; `startGb(board, id)`, `listGbSaves(id)`, `restoreGbSave(id, n, board)` → `{status, body}`
  - id e classi usati dalla prova del task 12: `#drop`, `#rom-file`, `#games .gb-game`, `[data-delete="<id>"]`, `#dlg-delete` (bottoni `value="cancel"` e `value="delete"`), `.gb-save`, `#boards .note`, `#err`, `#msg`

- [ ] **Step 1: Write the failing test**

`service/internal/webui/webui_test.go`, in `TestNoInlineInPages`, dopo il controllo di `game.html`:

```go
		if !strings.Contains(string(b), `href="gb.html"`) {
			t.Errorf("%s: manca il collegamento alla pagina Giochi", p)
		}
```

e in fondo al file (aggiungi `"bytes"` agli import):

```go
// Pagina Giochi: diritti spiegati, caricamento, conferma della rimozione, azioni principali.
func TestGbPage(t *testing.T) {
	page, err := fs.ReadFile(Static(), "gb.html")
	if err != nil {
		t.Fatal(err)
	}
	js, err := fs.ReadFile(Static(), "assets/js/gb-page.js")
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"Carica solo giochi tuoi", "non scarica giochi da internet", `id="drop"`, `id="rom-file"`,
		`id="dlg-delete"`, `aria-current="page">Giochi`, `<canvas>`} {
		if !bytes.Contains(page, []byte(want)) {
			t.Errorf("gb.html: manca %q", want)
		}
	}
	for _, want := range []string{"Gioca sulla scheda", "Ripristina", "Salvataggi", "Togli", "dragover", "drop"} {
		if !bytes.Contains(js, []byte(want)) {
			t.Errorf("gb-page.js: manca %q", want)
		}
	}
}
```

`tools/webui-dev/test-units.mjs`: con gli altri import

```js
import { checkRomFile, padText, playingText, startText, MAX_ROM } from '../../service/internal/webui/static/assets/js/gb.js';
```

e subito prima di `console.log(fail ? fail + ' prove fallite' : 'tutto ok');`:

```js
// 4. Giochi: controlli prima del caricamento e testi della pagina.
check(checkRomFile('demo.gb', 32768) === '', 'gb ok');
check(checkRomFile('DEMO.GBC', 65536) === '', 'gbc maiuscolo ok');
check(/\.gb/.test(checkRomFile('demo.zip', 32768)), 'estensione');
check(/corto/.test(checkRomFile('x.gb', 100)), 'troppo corto');
check(/2 MB/.test(checkRomFile('x.gb', MAX_ROM + 1)), 'troppo grande');
check(checkRomFile('x.gb', MAX_ROM) === '', '2 MB esatti');
check(padText('ready') === 'Controller collegato', 'pad pronto');
check(/sconosciuto/.test(padText(undefined)), 'pad mai riferito');
check(/non ha ancora i giochi/.test(playingText(null)), 'firmware senza giochi');
check(playingText({ known: true, playing: 'abc', title: 'TETRIS' }) === 'Sta giocando a TETRIS.', 'in gioco');
check(playingText({ known: true, playing: '' }) === 'Nessuna partita in corso.', 'fermo');
check(/consegnata/.test(startText({ title: 'X', size: 65536, served: 65536, done: true })), 'avvio consegnato');
check(/10 KB di 64 KB/.test(startText({ title: 'X', size: 65536, served: 10240, done: false })), 'avvio a metà');
console.log('giochi: 13 prove');
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd service && go test ./internal/webui/ -v` e `cd tools/webui-dev && npm test`
Expected: FAIL (`gb.html` non esiste, nessuna pagina collega `gb.html`; Node: `Cannot find module …/gb.js`)

- [ ] **Step 3: Write minimal implementation**

`service/internal/webui/static/gb.html`:

```html
<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Giochi · Calamaio OS</title>
<link rel="icon" href="assets/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="assets/fonts/fonts.css">
<link rel="stylesheet" href="assets/app.css">
<script type="module" src="assets/js/gb-page.js"></script>
</head>
<body>
<a class="skip" href="#main">Vai al contenuto</a>
<nav class="topbar" aria-label="Pagine">
  <a class="brand" href="./">Calamaio OS</a>
  <a href="./">Panoramica</a>
  <a href="avatars.html">Avatar</a>
  <a href="game.html">Gioco</a>
  <a href="gb.html" aria-current="page">Giochi</a>
  <a href="device.html">Dispositivo</a>
  <a href="firmware.html">Firmware</a>
  <a href="editor.html">Editor schermate</a>
  <span class="conn" id="conn" role="status"><span class="conn-text">collegamento…</span></span>
</nav>
<main class="home" id="main">
  <header class="hero">
    <div class="hero-text">
      <p class="kicker">Game Boy classico</p>
      <h1>Giochi</h1>
      <p class="lead">Le cartucce del Game Boy stanno sul PC e partono sulla scheda: la ROM viaggia una volta sola, i salvataggi tornano qui a ogni collegamento (si tengono le ultime 3 copie). Si gioca con il controller Xbox associato alla scheda.</p>
      <p class="set-note">Carica solo giochi tuoi: copie delle cartucce che possiedi o giochi homebrew liberi. Calamaio non scarica giochi da internet e non ne contiene nessuno.</p>
    </div>
    <figure class="goccia" aria-live="polite">
      <canvas></canvas>
      <figcaption><span class="goccia-label"></span><span class="goccia-text"></span></figcaption>
    </figure>
  </header>

  <p class="alert error" id="err" role="alert"></p>
  <p class="alert ok" id="msg" role="status" aria-live="polite"></p>

  <section class="section" aria-labelledby="h-boards">
    <div class="section-head">
      <p class="kicker">Schede</p>
      <h2 id="h-boards">Controller e partita in corso</h2>
    </div>
    <div class="grid" id="boards"></div>
  </section>

  <section class="section" aria-labelledby="h-add">
    <div class="section-head">
      <p class="kicker">Aggiungi</p>
      <h2 id="h-add">Carica una cartuccia</h2>
      <p>File <code>.gb</code> fino a 2 MB del Game Boy classico (vanno anche i giochi compatibili con il Color, non quelli solo per Color). Il servizio controlla l’intestazione prima di tenerlo.</p>
    </div>
    <label class="drop" id="drop" for="rom-file">
      <span class="drop-title">Trascina qui i file .gb</span>
      <span class="muted">oppure fai clic per sceglierli</span>
      <input type="file" id="rom-file" class="sr-only" accept=".gb,.gbc" multiple>
    </label>
  </section>

  <section class="section" aria-labelledby="h-lib">
    <div class="section-head">
      <p class="kicker" id="lib-count">Cartucce</p>
      <h2 id="h-lib">Sul PC</h2>
      <p class="muted" id="lib-dir"></p>
    </div>
    <div class="gb-list" id="games"></div>
  </section>
</main>

<dialog class="modal" id="dlg-delete" aria-labelledby="dd-title">
  <form method="dialog">
    <h2 id="dd-title">Togliere la cartuccia?</h2>
    <p id="dd-text"></p>
    <div class="card-actions end">
      <button type="submit" class="btn secondary" value="cancel">Annulla</button>
      <button type="submit" class="btn danger" value="delete">Togli</button>
    </div>
  </form>
</dialog>
</body>
</html>
```

`service/internal/webui/static/assets/js/gb.js`:

```js
// Giochi Game Boy: controlli e testi puri della pagina (provati in tools/webui-dev/test-units.mjs).
export const MAX_ROM = 2 * 1024 * 1024;
const MIN_ROM = 32 * 1024;

const PAD_TEXT = {
  off: 'Controller spento o non associato',
  pairing: 'Associazione del controller in corso',
  connecting: 'Controller in collegamento',
  ready: 'Controller collegato'
};

const kb = (n) => Math.round(n / 1024) + ' KB';

// checkRomFile: '' se il file si può mandare al servizio (che ricontrolla l'intestazione), altrimenti il motivo.
export function checkRomFile(name, size) {
  const ext = ((/\.[^.]*$/.exec(name || '') || [''])[0]).toLowerCase();
  if (ext !== '.gb' && ext !== '.gbc') return 'serve il file di una cartuccia del Game Boy (.gb).';
  if (size < MIN_ROM) return 'file troppo corto per una cartuccia del Game Boy.';
  if (size > MAX_ROM) return 'ROM più grande di 2 MB: non entra nella scheda.';
  return '';
}

export function padText(link) { return PAD_TEXT[link] || 'Controller: stato sconosciuto'; }

// playingText: cosa fa la scheda (gb = displays[].gb di /api/status).
export function playingText(gb) {
  if (!gb || !gb.known) return 'Il firmware di questa scheda non ha ancora i giochi.';
  if (gb.playing) return 'Sta giocando a ' + (gb.title || gb.playing) + '.';
  return 'Nessuna partita in corso.';
}

// startText: l'ultimo avvio mandato alla scheda.
export function startText(st) {
  if (!st) return '';
  if (st.done) return 'Ultimo avvio: ' + st.title + ', ROM consegnata' + (st.restore ? ' con il salvataggio ripristinato' : '') + '.';
  if (st.served > 0) return 'Invio di ' + st.title + ': ' + kb(Math.min(st.served, st.size)) + ' di ' + kb(st.size) + '.';
  return 'Avvio di ' + st.title + ' mandato alla scheda (la ROM viaggia solo se non ce l’ha già).';
}

export function fmtSaveDate(ms) {
  return new Date(ms).toLocaleString('it-IT', { day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' });
}
```

`service/internal/webui/static/assets/js/gb-page.js`:

```js
// Pagina Giochi: cartucce del Game Boy sul PC (caricamento con trascina e rilascia, elenco, rimozione
// con conferma), «Gioca sulla scheda», controller e partita in corso, salvataggi con ripristino.
// Dati da /api/gb/… e da /api/status (SSE o polling): l'elenco si ricarica quando status.gb.rev cambia.
import { watchStatus, listGbGames, uploadGbGame, deleteGbGame, startGb, listGbSaves, restoreGbSave } from './api.js';
import { h, heroGoccia, setConn, fmtBytes } from './ui.js';
import { checkRomFile, padText, playingText, startText, fmtSaveDate } from './gb.js';

const $ = (id) => document.getElementById(id);
const hero = heroGoccia('idle', 'Caricamento dei giochi');
let games = [], displays = [], rev, boardSig, busy = false;
const openSaves = new Set(); // giochi con i salvataggi aperti

function say(kind, text) {
  $('err').textContent = kind === 'err' ? text : '';
  $('msg').textContent = kind === 'ok' ? text : '';
}

function ask(dlg) {
  return new Promise((resolve) => {
    dlg.returnValue = '';
    dlg.addEventListener('close', () => resolve(dlg.returnValue), { once: true });
    dlg.showModal();
  });
}

// schede a cui si può mandare un gioco: abbinate e autenticate, con un servizio che ha i giochi
const ready = () => displays.filter((d) => d.authed && d.gb);

/* ---------- schede ---------- */

function renderBoards() {
  const box = $('boards');
  if (!displays.length) {
    box.replaceChildren(h('p', { class: 'empty' }, 'Nessuna scheda collegata: accendi il display e aspetta che si colleghi al servizio.'));
    return;
  }
  box.replaceChildren(...displays.map((d) => h('article', { class: 'note' },
    h('p', { class: 'kicker' }, d.board),
    d.authed
      ? [h('p', null, h('strong', null, padText(d.gb && d.gb.pad))), h('p', null, playingText(d.gb)),
        d.gb && d.gb.start ? h('p', { class: 'muted' }, startText(d.gb.start)) : null]
      : h('p', null, 'Scheda non abbinata: abbinala dalla pagina Dispositivo per giocare.'))));
}

/* ---------- cartucce ---------- */

function gameCard(g) {
  const boards = ready();
  const play = boards.length
    ? boards.map((d) => h('button', { class: 'btn', type: 'button', onclick: (e) => playOn(g, d.board, e.currentTarget) },
      boards.length > 1 ? 'Gioca su ' + d.board : 'Gioca sulla scheda'))
    : h('button', { class: 'btn', type: 'button', disabled: true, title: 'Nessuna scheda abbinata collegata' }, 'Gioca sulla scheda');
  const info = [g.header.mapper, g.header.battery ? 'con salvataggi' : 'senza salvataggi', g.header.cgb ? 'compatibile Color' : null];
  return h('article', { class: 'note gb-game', 'data-id': g.id },
    h('div', { class: 'card-head' },
      h('div', null, h('h3', { class: 'gb-title' }, g.title), h('p', { class: 'mono muted' }, g.id)),
      h('span', { class: 'badge' }, fmtBytes(g.size))),
    h('p', null, info.filter(Boolean).join(' · ')),
    h('div', { class: 'card-actions' }, play,
      h('button', { class: 'btn secondary', type: 'button', 'aria-expanded': String(openSaves.has(g.id)), 'aria-controls': 'saves-' + g.id,
        onclick: (e) => toggleSaves(g, e.currentTarget) }, 'Salvataggi'),
      h('button', { class: 'btn secondary', type: 'button', 'data-delete': g.id, onclick: () => remove(g) }, 'Togli')),
    h('div', { class: 'gb-saves', id: 'saves-' + g.id, hidden: !openSaves.has(g.id) }));
}

function renderGames() {
  $('lib-count').textContent = games.length === 1 ? '1 cartuccia' : games.length + ' cartucce';
  const box = $('games');
  if (!games.length) {
    box.replaceChildren(h('p', { class: 'empty' }, 'Nessuna cartuccia sul PC. Trascina qui sopra un file .gb: una copia di una cartuccia tua o un gioco homebrew.'));
    return;
  }
  box.replaceChildren(...games.map(gameCard));
  for (const id of openSaves) loadSaves(id);
}

async function loadGames() {
  const r = await listGbGames();
  if (r.status === 404) { games = []; renderGames(); say('err', 'Questo servizio non ha ancora i giochi: aggiornalo.'); return; }
  if (r.status !== 200) { say('err', r.body.error || 'Errore ' + r.status); return; }
  games = r.body.games || [];
  $('lib-dir').textContent = 'Cartella: ' + r.body.dir;
  renderGames();
}

/* ---------- caricamento ---------- */

async function upload(files) {
  if (busy || !files.length) return;
  busy = true;
  hero.set('working', 'Controllo le cartucce');
  const done = [];
  try {
    for (const f of files) {
      const why = checkRomFile(f.name, f.size);
      if (why) { say('err', f.name + ': ' + why); hero.set('error', 'File rifiutato'); return; }
      const r = await uploadGbGame(f.name, new Uint8Array(await f.arrayBuffer()));
      if (r.status === 201) done.push('Aggiunta: ' + r.body.game.title + '.');
      else if (r.status === 200) done.push('Già presente: ' + r.body.game.title + '.');
      else { say('err', f.name + ': ' + (r.body.error || 'errore ' + r.status)); hero.set('error', 'Cartuccia rifiutata'); return; }
    }
    say('ok', done.join(' '));
    hero.set('done', 'Cartucce pronte');
    await loadGames();
  } finally {
    busy = false;
    $('rom-file').value = '';
  }
}

const drop = $('drop');
drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
drop.addEventListener('dragleave', () => drop.classList.remove('over'));
drop.addEventListener('drop', (e) => { e.preventDefault(); drop.classList.remove('over'); upload([...e.dataTransfer.files]); });
$('rom-file').addEventListener('change', (e) => upload([...e.target.files]));

/* ---------- gioca, togli ---------- */

async function playOn(g, board, btn) {
  btn.disabled = true;
  const r = await startGb(board, g.id);
  btn.disabled = false;
  if (r.status !== 202) { say('err', r.body.error || 'Errore ' + r.status); return; }
  say('ok', 'Avvio di ' + g.title + ' su ' + board + ': la scheda scarica la ROM (' + fmtBytes(g.size) + ') solo se non ce l’ha già.');
  hero.set('working', 'Si parte: ' + g.title);
}

async function remove(g) {
  $('dd-text').textContent = '«' + g.title + '» (' + fmtBytes(g.size) + ') sparisce dal PC e dall’elenco della scheda. I salvataggi restano: ricaricando la stessa cartuccia li ritrovi.';
  if (await ask($('dlg-delete')) !== 'delete') return;
  const r = await deleteGbGame(g.id);
  if (r.status !== 204) { say('err', r.body.error || 'Errore ' + r.status); return; }
  openSaves.delete(g.id);
  say('ok', 'Tolta: ' + g.title + '.');
  await loadGames();
}

/* ---------- salvataggi ---------- */

async function toggleSaves(g, btn) {
  const box = $('saves-' + g.id);
  if (openSaves.has(g.id)) openSaves.delete(g.id); else openSaves.add(g.id);
  box.hidden = !openSaves.has(g.id);
  btn.setAttribute('aria-expanded', String(!box.hidden));
  if (!box.hidden) await loadSaves(g.id);
}

async function loadSaves(id) {
  const box = $('saves-' + id);
  if (!box) return;
  const r = await listGbSaves(id);
  if (r.status !== 200) { box.replaceChildren(h('p', { class: 'muted' }, r.body.error || 'Errore ' + r.status)); return; }
  const { saves, restore } = r.body;
  if (!saves.length) {
    box.replaceChildren(h('p', { class: 'muted' }, 'Nessun salvataggio sul PC: arrivano dalla scheda a ogni collegamento e all’uscita dal gioco.'));
    return;
  }
  box.replaceChildren(h('ul', { class: 'gb-save-list' }, saves.map((s) => h('li', { class: 'gb-save' },
    h('span', null, h('strong', null, s.n === 1 ? 'Più recente' : 'Copia ' + s.n), ' · ' + fmtSaveDate(s.at) + ' · ' + fmtBytes(s.size)),
    restore && restore.name === s.name
      ? h('span', { class: 'badge' }, 'al prossimo avvio')
      : h('button', { class: 'btn secondary', type: 'button', onclick: () => restoreSave(id, s) }, 'Ripristina')))));
}

async function restoreSave(id, s) {
  const r = await restoreGbSave(id, s.n);
  if (r.status !== 200) { say('err', r.body.error || 'Errore ' + r.status); return; }
  say('ok', 'Ripristino pronto: la copia ' + s.n + ' sostituisce il salvataggio della scheda al prossimo «Gioca sulla scheda».');
  await loadSaves(id);
}

/* ---------- stato in diretta ---------- */

watchStatus((st) => {
  displays = st.displays || [];
  renderBoards();
  const sig = ready().map((d) => d.board).join(',');
  const r = st.gb ? st.gb.rev : null;
  if (r !== rev || sig !== boardSig) { rev = r; boardSig = sig; loadGames(); }
  if (busy) return;
  const playing = displays.find((d) => d.gb && d.gb.playing);
  hero.set(playing ? 'working' : 'idle', playing ? d2text(playing) : (games.length ? 'Pronta a giocare' : 'Aspetto una cartuccia'));
}, setConn);

function d2text(d) { return d.board + ': ' + playingText(d.gb); }
```

`service/internal/webui/static/assets/js/api.js`, in fondo:

```js
/* ---------- giochi Game Boy ---------- */

// → { status, body: { games, dir, maxSize, problems } | { error } }; 404 = servizio senza giochi
export async function listGbGames() {
  const res = await fetch('/api/gb/games', { cache: 'no-store' });
  return { status: res.status, body: await readJSON(res) };
}

// data: Uint8Array. → { status: 201 | 200 (già presente) | 400 | 413, body: { game, existed } | { error } }
export async function uploadGbGame(name, data) {
  const res = await fetch('/api/gb/games?name=' + encodeURIComponent(name), {
    method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: data
  });
  return { status: res.status, body: await readJSON(res) };
}

export async function deleteGbGame(id) {
  const res = await fetch('/api/gb/games/' + encodeURIComponent(id), { method: 'DELETE' });
  return { status: res.status, body: await readJSON(res) };
}

// → { status: 202 | 404 | 409, body: { start } | { error } }
export const startGb = (board, id) => postJSON(boardPath(board) + '/gb/start', { id });

// → { status, body: { saves, restore, game } | { error } }
export async function listGbSaves(id) {
  const res = await fetch('/api/gb/saves/' + encodeURIComponent(id), { cache: 'no-store' });
  return { status: res.status, body: await readJSON(res) };
}

// board facoltativa: con la scheda il gioco parte subito
export const restoreGbSave = (id, n, board) =>
  postJSON('/api/gb/saves/' + encodeURIComponent(id) + '/restore', board ? { n, board } : { n });
```

`service/internal/webui/static/assets/app.css`, in fondo:

```css
/* Giochi Game Boy */
.drop { display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 6px; min-height: 120px; max-width: 640px; padding: 24px 16px; box-sizing: border-box; border: 2px dashed var(--ink-3); border-radius: 10px; background: var(--card); cursor: pointer; text-align: center; }
.drop:hover, .drop:focus-within, .drop.over { border-color: var(--accent); }
.drop.over { background: var(--paper); }
.drop-title { font: 600 16px/1.3 'Chakra Petch', sans-serif; color: var(--ink); }
.gb-list { display: grid; gap: 16px; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); align-items: start; }
.gb-title { margin: 0; font: 600 18px/1.25 'Chakra Petch', sans-serif; color: var(--ink); overflow-wrap: anywhere; }
.gb-save-list { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; }
.gb-save { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 8px; padding: 8px 0; border-top: 1px solid var(--line); }
@media (max-width: 420px) { .gb-list { grid-template-columns: minmax(0, 1fr); } }
```

Barra delle pagine: in `index.html`, `avatars.html`, `device.html`, `firmware.html`, `editor.html` la riga

```html
  <a href="game.html">Gioco</a>
```

diventa

```html
  <a href="game.html">Gioco</a>
  <a href="gb.html">Giochi</a>
```

e in `game.html` la riga `  <a href="game.html" aria-current="page">Gioco</a>` è seguita da `  <a href="gb.html">Giochi</a>`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd service && go test ./internal/webui/ -v && go test ./...` e `cd tools/webui-dev && npm test`
Expected: PASS (Go) e `giochi: 13 prove` … `tutto ok` (Node)

Poi una prova a mano, breve: `cd service && go run ./cmd/calamaio serve --fake --no-mdns` e apri `http://127.0.0.1:8765/gb.html`; con `gbgames.FakeROM` non c'è un file pronto, quindi trascina un `.gb` homebrew tuo (o salta la prova a mano: la fa il task 12 con il server finto).

- [ ] **Step 5: Commit**

```bash
git add service/internal/webui/static/gb.html service/internal/webui/static/assets/js/gb.js service/internal/webui/static/assets/js/gb-page.js service/internal/webui/static/assets/js/api.js service/internal/webui/static/assets/app.css service/internal/webui/static/index.html service/internal/webui/static/avatars.html service/internal/webui/static/game.html service/internal/webui/static/device.html service/internal/webui/static/firmware.html service/internal/webui/static/editor.html service/internal/webui/webui_test.go tools/webui-dev/test-units.mjs
git commit -m "Webapp: pagina Giochi con caricamento, avvio sulla scheda, controller e salvataggi"
```

---

### Task 12: prova a schermo della pagina «Giochi» (server finto + Playwright)

**Files:**
- Modify: `tools/webui-dev/mock-server.mjs` (rotte `/api/gb/…`, `status.gb`, `displays[0].gb`)
- Modify: `tools/webui-dev/shots.mjs` (funzione `gbFlows`, chiamata in `run`)

**Interfaces:**
- Consumes: id e classi del task 11; forme delle risposte del task 9.
- Produces: `node shots.mjs --giochi` (e il giro completo `node shots.mjs`, quando tornerà a passare) fotografa `out/giochi-1280.png`, `out/giochi-390.png`, `out/giochi-togli-*.png` e fallisce se la pagina scrive errori in console o se il giro carica → salvataggi → togli non va.

- [ ] **Step 1: Write the failing test**

`tools/webui-dev/shots.mjs`, dopo la funzione `backupFlows`:

```js
// G. Pagina Giochi: elenco, file rifiutato, cartuccia nuova, «Gioca sulla scheda», salvataggi, rimozione con conferma.
// Un server finto per larghezza: l'elenco parte sempre da una cartuccia.
async function gbFlows() {
  const chrome = await chromium.launch();
  const mocks = [];
  try {
    for (const [i, w] of WIDTHS.entries()) {
      const port = 8780 + i;
      mocks.push(await startMock(port));
      const ctx = await chrome.newContext({ viewport: { width: w, height: 900 } });
      const page = await ctx.newPage();
      watch(page, 'G' + w, [/Failed to load resource/]);
      await page.goto('http://127.0.0.1:' + port + '/gb.html');
      await page.waitForSelector('#games .gb-game');
      await page.waitForSelector('#boards .note');
      await shot(page, `giochi-${w}`);
      await page.setInputFiles('#rom-file', { name: 'gioco.zip', mimeType: 'application/zip', buffer: Buffer.alloc(40000) });
      await page.waitForSelector('#err:not(:empty)');
      await page.setInputFiles('#rom-file', { name: 'nuovo.gb', mimeType: 'application/octet-stream', buffer: Buffer.alloc(32768, 7) });
      await page.waitForFunction(() => document.querySelectorAll('#games .gb-game').length === 2);
      if (!(await page.textContent('#msg')).includes('Aggiunta: nuovo')) problems.push('G' + w + ': manca «Aggiunta: nuovo»');
      await page.click('[data-id="3b5f0c9a1d2e4f60"] button:has-text("Gioca sulla scheda")');
      await page.waitForFunction(() => document.getElementById('msg').textContent.includes('Avvio di HOMEBREW DEMO su calamaio-3f2a'));
      await page.waitForSelector('#boards >> text=mandato alla scheda');
      await page.click('button[aria-controls="saves-3b5f0c9a1d2e4f60"]'); // il bottone, non la riga «con salvataggi»
      await page.waitForSelector('[data-id="3b5f0c9a1d2e4f60"] .gb-save');
      await page.click('[data-delete="3b5f0c9a1d2e4f60"]');
      await page.waitForSelector('#dlg-delete[open]');
      await shot(page, `giochi-togli-${w}`, true);
      await page.click('#dlg-delete button[value="delete"]');
      await page.waitForFunction(() => document.querySelectorAll('#games .gb-game').length === 1);
      await ctx.close();
    }
  } finally {
    await chrome.close();
    mocks.forEach((m) => m.kill());
  }
}
```

in `run()`, subito dopo `await backupFlows();`:

```js
  await gbFlows();
```

e l'ultima riga del file (`run().catch((e) => { console.error(e); process.exit(2); });`) diventa:

```js
// node shots.mjs --giochi: solo la prova della pagina Giochi.
async function gbOnly() {
  await gbFlows();
  console.log(problems.length ? '\nPROBLEMI: ' + problems.length : '\nnessun errore in console, prove superate');
  for (const p of problems) console.log('  ' + p);
  process.exit(problems.length ? 1 : 0);
}

(process.argv.includes('--giochi') ? gbOnly : run)().catch((e) => { console.error(e); process.exit(2); });
```

Nota: oggi (prima di questo piano) `node shots.mjs` completo si ferma già alla pagina Dispositivo (`shots.mjs:91`, clic su un'impostazione disabilitata: il server finto non dice `authed` e la webapp tiene le impostazioni in sola lettura). Non è di questo piano: per questo la prova dei giochi si lancia da sola con `--giochi`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools/webui-dev && node shots.mjs --giochi`
Expected: FAIL in `gbFlows` (timeout su `#games .gb-game`: il server finto risponde 404 a `/api/gb/games`)

- [ ] **Step 3: Write minimal implementation**

`tools/webui-dev/mock-server.mjs`, dopo la definizione di `VALID`:

```js
/* ---------- giochi Game Boy ---------- */
const GB_HEADER = { title: 'HOMEBREW DEMO', cartType: 3, mapper: 'MBC1', battery: true, romSize: 65536, ramSize: 8192, cgb: false };
const gbGames = [{ id: '3b5f0c9a1d2e4f60', title: 'HOMEBREW DEMO', size: 65536, sha256: '3b5f0c9a1d2e4f60' + '0'.repeat(48),
  file: 'demo.gb', added: now - 3600000, header: GB_HEADER }];
const gbSaves = { '3b5f0c9a1d2e4f60': [
  { n: 1, name: '1791748200000-12345678.sav', size: 8192, crc32: 305419896, at: now - 600000 },
  { n: 2, name: '1791661800000-00000001.sav', size: 8192, crc32: 1, at: now - 86400000 }] };
let gbRev = 1;
const gbView = { known: true, playing: '3b5f0c9a1d2e4f60', title: 'HOMEBREW DEMO', pad: 'ready',
  start: { id: '3b5f0c9a1d2e4f60', title: 'HOMEBREW DEMO', size: 65536, at: now - 900000, served: 65536, done: true, restore: false } };
```

in `status()`:

```js
function status() {
  return {
    service: { version: '0.1.0-mock', host: 'mac-sviluppo', tz: 'CET-1CEST,M3.5.0,M10.5.0/3', wsPort: 8766 },
    displays: DISPLAYS.map((d, i) => (i === 0 ? { ...d, authed: true, gb: gbView } : d)),
    gb: { rev: gbRev, count: gbGames.length },
    state: { ...state, time: Date.now() }
  };
}
```

in `api()`, subito prima dell'ultima riga `return json(res, 404, { error: 'not found' });`:

```js
  if (p === '/api/gb/games' && req.method === 'GET') {
    return json(res, 200, { games: gbGames, dir: '/Users/prova/Library/Application Support/calamaio/games', maxSize: 2097152, problems: [] });
  }
  if (p === '/api/gb/games' && req.method === 'POST') {
    const name = url.searchParams.get('name') || '';
    const data = await readBinary(req, 2097152);
    if (data === null) return json(res, 413, { error: 'ROM più grande di 2 MB: non entra nella scheda' });
    if (!/\.gbc?$/i.test(name)) return json(res, 400, { error: 'serve il file di una cartuccia del Game Boy (.gb)' });
    if (data.length < 32768) return json(res, 400, { error: 'file troppo corto per una cartuccia del Game Boy (' + data.length + ' byte)' });
    const id = crypto.createHash('sha256').update(data).digest('hex').slice(0, 16);
    const old = gbGames.find((g) => g.id === id);
    if (old) return json(res, 200, { game: old, existed: true });
    const title = name.replace(/\.[^.]*$/, '').slice(0, 16);
    const g = { id, title, size: data.length, sha256: id + '0'.repeat(48), file: name, added: Date.now(),
      header: { ...GB_HEADER, title, romSize: data.length } };
    gbGames.push(g); gbRev++; broadcast();
    return json(res, 201, { game: g, existed: false });
  }
  const gm = /^\/api\/gb\/games\/([0-9a-f]{16})$/.exec(p);
  if (gm && req.method === 'DELETE') {
    const i = gbGames.findIndex((g) => g.id === gm[1]);
    if (i < 0) return json(res, 404, { error: 'Gioco sconosciuto.' });
    gbGames.splice(i, 1); gbRev++; broadcast();
    res.writeHead(204); return res.end();
  }
  const sm = /^\/api\/gb\/saves\/([0-9a-f]{16})(\/restore)?$/.exec(p);
  if (sm && req.method === 'GET' && !sm[2]) {
    return json(res, 200, { saves: gbSaves[sm[1]] || [], restore: null, game: gbGames.find((g) => g.id === sm[1]) || null });
  }
  if (sm && req.method === 'POST' && sm[2]) {
    let body = {};
    try { body = JSON.parse((await readBody(req)) || '{}'); } catch (e) { return json(res, 400, { error: 'JSON non valido.' }); }
    const s = (gbSaves[sm[1]] || []).find((x) => x.n === body.n);
    if (!s) return json(res, 404, { error: 'Salvataggio sconosciuto.' });
    return json(res, 200, { restore: s, start: null });
  }
  if (/^\/api\/displays\/[^/]+\/gb\/start$/.test(p) && req.method === 'POST') {
    let body = {};
    try { body = JSON.parse((await readBody(req)) || '{}'); } catch (e) { return json(res, 400, { error: 'JSON non valido.' }); }
    const g = gbGames.find((x) => x.id === body.id);
    if (!g) return json(res, 404, { error: 'gioco sconosciuto: non è nella cartella dei giochi' });
    gbView.start = { id: g.id, title: g.title, size: g.size, at: Date.now(), served: 0, done: false, restore: false };
    broadcast();
    return json(res, 202, { start: gbView.start });
  }
```

(`now`, `crypto`, `readBinary`, `readBody` e `broadcast` ci sono già nel file.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tools/webui-dev && npm test && node shots.mjs --giochi`
Expected: `nessun errore in console, prove superate`; guarda `out/giochi-1280.png` e `out/giochi-390.png` (niente scorrimento orizzontale a 390 px, Goccia in alto a destra come nelle altre pagine).

- [ ] **Step 5: Commit**

```bash
git add tools/webui-dev/mock-server.mjs tools/webui-dev/shots.mjs
git commit -m "Webapp: prova a schermo della pagina Giochi con il server finto"
```

---

### Task 13: DECISIONI, controllo finale

**Files:**
- Modify: `docs/DECISIONI.md` (in fondo alla sezione più recente)

**Interfaces:**
- Consumes: tutto il piano.
- Produces: la traccia delle scelte per i piani A e B.

- [ ] **Step 1: Scrivi la voce**

In fondo a `docs/DECISIONI.md`:

```markdown
- **Giochi Game Boy, lato PC (piano C)**: pacchetto `service/internal/gbgames` (intestazione: logo di avvio, checksum, tipi ROM/MBC1/MBC2/MBC3/MBC5, dimensione uguale a quella dichiarata, ≤ 2 MiB; rifiuta i giochi solo Color 0xC0, accetta 0x80), cartella `<dati>/calamaio/games/` con `<id>.gb` e `index.json` atomico (illeggibile → messo da parte e ricostruito dai file), salvataggi in `games/saves/<id>/` con le ultime 3 copie più quella scelta per un ripristino non ancora consegnato. Solo file dell'utente: nessuno scaricamento, nessuna ROM nel repo (prove con `gbgames.FakeROM`). Messaggi `gb-*` solo autenticati; `gb-list`, `gb-start`, `gb-save-ok` con mac obbligatorio come `ota-*`. Rotte della scheda sulla porta 8766 come `/ota`: `GET /gb/rom/<token>` con Range sullo stesso token fino alla consegna dell'ultimo byte, `PUT /gb/save/<token>` con il token scelto dalla scheda in `gb-save`, `GET /gb/save/<token>` per il ripristino; 10 minuti (`GbTokenTTL`), solo l'IP della scheda; il caricamento di un salvataggio si accetta finché un `PUT` completo non riesce. Avvio rifiutato durante un OTA (409); un OTA rifiutato dalla scheda per la partita in corso si rifà una volta a `gb-state {playing: null}`. API `/api/gb/…`, `calamaio gb list|add|rm|start|saves` (a servizio spento sulla cartella), pagina «Giochi». Accettati anche i file `.gbc` compatibili (decide l'intestazione). La cartella si ricontrolla ogni 5 s per le modifiche fatte a servizio spento.
```

- [ ] **Step 2: Controllo finale**

Run: `cd service && gofmt -l . && go vet ./... && go test ./... && go test -race ./internal/display/ ./internal/gbgames/`
Expected: nessun file da `gofmt -l`, nessun avviso di `go vet`, tutti i test PASS.

Run: `cd tools/webui-dev && npm test`
Expected: `tutto ok`

Run: `claude plugin validate plugin` (dalla radice del repo; il plugin non cambia, è il controllo di sempre)
Expected: valido

- [ ] **Step 3: Commit**

```bash
git add docs/DECISIONI.md
git commit -m "DECISIONI: giochi Game Boy, lato PC"
```

---

## Proposte di modifica al contratto (accolte il 2026-10-12)

Le dieci proposte di questo piano sono nel contratto (`docs/superpowers/plans/2026-10-12-giochi-contratti.md`, versione del 2026-10-12), con tre differenze decise dal coordinatore e già applicate nei task qui sopra:

1. **Token di 10 minuti**, non 5 (proposta 4): `Hub.GbTokenTTL` separato dal `TokenTTL` dell'OTA (task 6–8).
2. **Caricamento del salvataggio**: il token si accetta finché un `PUT` completo non riesce (un 400 non lo consuma; due `PUT` insieme: 409), sempre entro i 10 minuti e solo dall'IP della scheda (task 8).
3. **OTA durante la partita**: oltre al 409 sull'avvio durante un OTA (proposta 10), il servizio rifà una volta l'offerta rifiutata con «partita in corso: riprova a fine partita» quando arriva `gb-state {playing: null}` (task 7, Step 6–8).

Il resto come proposto: porta 8766, `mac` obbligatori su `gb-list`/`gb-start`/`gb-save-ok` con le forme del task 4, token del salvataggio scelto dalla scheda, Range sullo stesso token fino alla consegna completa, `.gb` e `.gbc`, ripristino con `{n, board?}`, `gb-pick` di un gioco sparito → `gb-list`, limiti (64 cartucce, titoli 1–16 ASCII, salvataggi 1–131 136 byte), stato per la webapp.

## Rischi

- **Byte del logo di avvio nel sorgente** (48 byte, `nintendoLogo`): servono a riconoscere una ROM e a costruire le ROM sintetiche delle prove; sono un dato funzionale pubblicato ovunque (Pan Docs) e ogni emulatore li contiene, ma restano un marchio. Alternativa se l'utente preferisce: tenere solo lo SHA-256 dei 48 byte nel codice e far leggere il logo alle prove da un file fuori dal repo (le prove allora si saltano senza quel file).
- **Memoria**: ogni avvio tiene la ROM in RAM (≤ 2 MiB) finché non è consegnata o sostituita, una per scheda: trascurabile su un PC.
- **Token di 10 minuti**: 2 MB a ~115 KB/s (misura dell'OTA) sono ~18 s; con un Wi-Fi pessimo la scheda rimanda `gb-pick` una volta per gioco (piano B, Task 13).
- **`gb-list` a schede senza firmware giochi**: lo scartano come tipo sconosciuto e lo registrano nel loro log; nessun effetto, ma riga in più nel registro remoto (CrowPanel compresa).
- **CLI e `DELETE`**: la CLI manda `Origin: http://127.0.0.1:<porta>` per passare `deleteGuard`; è un processo locale come quelli che possono già scrivere i file: nessun allargamento reale.
- **Due processi sulla stessa cartella** (CLI a servizio spento mentre il servizio parte): `index.json` è atomico, ma due scritture contemporanee possono perdere una delle due aggiunte; la sorveglianza ogni 5 s rilegge l'indice, non lo unisce. Basso: la CLI usa la API appena il servizio risponde.
- **Ordine dei piani**: i task 5–8 cambiano il protocollo condiviso con il piano B; seguono il contratto del 2026-10-12, lo stesso che segue il piano B (mac, porta, token): i vettori del task 4 sono anche nei test della scheda (`test_gb_proto`).
