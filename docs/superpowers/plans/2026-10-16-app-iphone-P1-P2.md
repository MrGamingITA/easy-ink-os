# App iPhone, fasi P1 e P2: porta dei telefoni nel servizio e guscio dell'app — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un iPhone abbinato con un QR apre tutta la webapp del servizio (tranne Firmware) dentro l'app «Calamaio», attraverso una porta HTTPS nuova del PC (8767) che accetta solo telefoni col token e solo un elenco chiuso di API; la porta 8765 resta com'è.

**Architecture:** Servizio (P1): pacchetto nuovo `service/internal/phones` (elenco dei telefoni con il solo SHA-256 del token, finestra di abbinamento con codice monouso, certificato ECDSA autofirmato, link del QR); in `service/internal/api` un gestore per la porta 8767 (token → elenco chiuso → la stessa mux di 8765 con un segno nel contesto che vale come «stessa origine») e le rotte `/api/phones…` solo su 8765; in `service/internal/server` l'ascoltatore HTTPS acceso solo se c'è un telefono o una finestra aperta, con annuncio Bonjour `_calamaio-app._tcp`; sezione «Telefoni» nella pagina Dispositivo. App (P2): cartella `ios/` con progetto generato da XcodeGen, logica nel pacchetto Swift locale `ios/CalamaioKit` (link del QR, impronta fissata, client del PC, Portachiavi, ricerca del PC, gestore dello schema `calamaio-app://`), app SwiftUI con le schede «Scheda» e «Impostazioni». Il primo compito di P2 è una prova automatica nel simulatore che decide se serve il ponte JavaScript per i corpi delle richieste.

**Tech Stack:** Go 1.24 (`go.mod`; sul Mac c'è Go 1.27), `github.com/brutella/dnssd` v1.2.14, `rsc.io/qr` v0.2.0 (nuova, solo col sì dell'utente); JavaScript della webapp (moduli ES, Node 22 per i test); Swift 6.4 in modo di linguaggio 5, SwiftUI, WebKit, Network, CryptoKit, Security, VisionKit, XCTest; Xcode 27, simulatore iOS 27.0, XcodeGen 2.45.4 (già installato in `/opt/homebrew/bin/xcodegen`).

**Spec:** `docs/superpowers/specs/2026-10-16-app-iphone-design.md` (§1, §3, §5 per lo user agent, §7 «Go» e «Swift», §8 punti 2 e 3). **Prerequisito:** albero pulito sul ramo `app-iphone` nel worktree `~/CalamaiOS-iphone`; `cd service && go test ./... 2>&1 | tail -5` passa prima di cominciare. Le prove dal vivo della fase 1b dell'editor (spec §8 punto 1) non bloccano questo piano: nessun compito tocca la scheda.

## Global Constraints

- **Regole dei permessi (brief §7) invariate**: il telefono non riceve né risponde a `perm-reply`, non ha `/hook*`; nessuna approvazione automatica, nessuna scorciatoia. Il telefono non darà mai CONSENTI (spec §2.3, fase P3, fuori da questo piano).
- **Porta 8765 invariata**: solo `127.0.0.1`, `guard`/`postGuard`/`bodyGuard`/`deleteGuard`/`localOnly`/`hookCaller`/`fromLoopback` con lo stesso comportamento di oggi per ogni richiesta che non arriva dal gestore dei telefoni. Un `Authorization: Bearer` valido su 8765 non dà niente.
- **Porta dei telefoni**: **8767**, chiave `app_port` in `config.json`, variabile `CALAMAIO_APP_PORT`; tutte le interfacce (con `serve --loopback` solo `127.0.0.1`); accesa solo se c'è almeno un telefono abbinato o una finestra di abbinamento aperta; HTTPS TLS 1.3, solo `http/1.1`.
- **Certificato**: ECDSA P-256, validità **10 anni**, in `<dati>/app-tls/` (`cert.pem` e `key.pem` 0600, cartella 0700), creato solo se la cartella non ha né l'uno né l'altro; uno rovinato o a metà **non si rifà mai da solo**. Impronta = SHA-256 del DER del certificato, **64 cifre esadecimali minuscole**.
- **Token**: 32 byte casuali = **64 cifre esadecimali minuscole**; nel servizio solo `SHA-256(token)` in `<dati>/phones.json` (0600) con id, nome, data di abbinamento, ultimo uso. Token assente, malformato, sconosciuto o revocato: **401** `{"error":"non autorizzato"}` con `WWW-Authenticate: Bearer`, identico in tutti i casi, prima di qualsiasi controllo di rotta.
- **Abbinamento**: finestra di **5 minuti**, codice di 16 byte = **32 cifre esadecimali minuscole**, monouso, **5 tentativi sbagliati chiudono la finestra**. `POST /app/pair {code, name}` è l'unica richiesta di 8767 senza token.
- **QR**: `calamaio://pair?v=1&h=<ip1,ip2>&p=8767&fp=<impronta completa>&c=<codice>&n=<nome PC>` (nome con `%20` per gli spazi, mai `+`).
- **Bonjour**: `_calamaio-app._tcp` sulla porta dei telefoni, TXT `fp=<prime 16 cifre dell'impronta>`, solo mentre l'ascoltatore è acceso.
- **Elenco chiuso** (`phoneRoutes` in `service/internal/api/phone.go`): tutto il resto **404** `{"error":"non trovato"}`. Mai dal telefono: `/hook*`, `/statusline`, `/debug*`, abbinamento e disabbinamento della scheda, Wi-Fi della scheda, OTA e immagini firmware (`/api/firmware*`, `firmware.html`, `/vendor/`), backup, registro remoto (`/api/displays/{board}/log*`), `/api/phones*`.
- **App**: iOS **27.0**, Xcode 27, SwiftUI, modo di linguaggio Swift 5 (`SWIFT_VERSION 5.0`, `swiftLanguageModes: [.v5]`), progetto generato da `ios/project.yml` con XcodeGen (`ios/Calamaio.xcodeproj` non si committa), bundle id `com.mrgamingita.calamaio` (sostituibile in `ios/Config/Local.xcconfig`, non committato, dove va anche `DEVELOPMENT_TEAM` dell'Apple ID gratuito scelto dall'utente).
- **Info.plist**: `NSLocalNetworkUsageDescription`, `NSBonjourServices = [_calamaio-app._tcp]`, `NSCameraUsageDescription`, `NSBluetoothAlwaysUsageDescription`, `NSAppTransportSecurity.NSAllowsLocalNetworking = YES`.
- **Webapp nell'app**: `calamaio-app://ui/<percorso>`; user agent con `CalamaioApp/<versione>` (`applicationNameForUserAgent`). Il token sta **solo** nel Portachiavi (`kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`) e nelle richieste native: mai nel JavaScript, negli URL, nelle risposte, nei registri.
- **Simulatore**: `DEST='platform=iOS Simulator,name=iPhone 18 Pro,OS=27.0'` (ce ne sono due con lo stesso nome: xcodebuild prende il primo e lo dice, va bene). Il simulatore non ha Bluetooth né fotocamera: niente controller (P3), abbinamento col link incollato.
- **Lingua**: testi a schermo e messaggi d'errore in italiano; identificatori in inglese; commenti brevi in italiano. I testi dell'app sono nuovi (il simulatore della scheda non ha schermate dell'app): elenco in DECISIONI.
- **Uscite**: mai stampare uscite enormi: ogni comando di prova finisce con `2>&1 | tail -n 40` (o `| grep …`).
- **Commit**: piccoli, messaggio in italiano, **senza** riga `Co-Authored-By`; niente push, niente merge.
- **Consenso dell'utente prima di**: aggiungere la dipendenza `rsc.io/qr` (Task 5), `brew install xcodegen` (solo se `xcodegen --version` fallisce), qualunque cosa fuori dal repository. Nessun flash, nessuna NVS, nessuna modifica a `~/.claude/settings.json` in questo piano.

## Review Focus

- **Il PC cambia indirizzo IP** (DHCP, altra rete, Mac a 5 GHz e telefono a 2,4 GHz senza multicast): l'app prova l'ultimo indirizzo buono, poi Bonjour con l'impronta giusta (mai un altro PC Calamaio), poi gli indirizzi più recenti dati da `/app/whoami`, poi quelli del QR; se nessuno risponde dice «PC non trovato: telefono e PC devono essere sulla stessa rete»; riabbinare col QR nuovo sostituisce il token vecchio senza lasciare doppioni sul PC — `testCandidatesOrderAndFilter`, `testConnectFallsBackAndRemembersHosts` (Task 9), `TestAppPairReplacesOldToken` (Task 4).
- **Certificato rifatto, rovinato o cartella a metà**: il servizio non lo rigenera (tutti i telefoni lo hanno fissato), spegne la porta e dice perché in `/api/phones`; dopo un riavvio del servizio l'impronta è la stessa; l'app che trova un'impronta diversa dice «Il PC ha un certificato diverso da quello abbinato: abbina di nuovo il telefono» e non manda il token — `TestCertBrokenNotRegenerated` (Task 2), `TestAppBrokenCertNotRegenerated`, `TestAppFilesAndRestart` (Task 4), `testConnectReportsCertChanged` (Task 9), `testPairAndPinAgainstRealService` (Task 8, e2e).
- **Il token finisce nel JavaScript o in una risposta**: il JavaScript della pagina non lo vede mai (lo aggiunge il gestore nativo, che toglie anche un `Authorization` messo dalla pagina), nessuna risposta di 8767 lo ripete, la WKWebView non apre mai `https://` direttamente — `TestPhoneTokenNeverEchoed` (Task 3), `testUpstreamDropsPageHeaders`, `testNavigationPolicy` (Task 10).
- **SSE attraverso il proxy**: ogni pezzo di `/api/events` arriva subito alla pagina (non a fine flusso), lo stop della pagina annulla la richiesta e dopo non arriva più niente, la revoca chiude lo stream aperto entro un secondo — prova dello schema (Task 7), `testStreamsChunksBeforeFinish`, `testStopCancelsAndSilences` (Task 10), `TestAppSSEClosedOnRevoke` (Task 4), `TestPhoneRevokeClosesOpenRequest` (Task 3).
- **Caricamenti grandi** (ROM da 2 MiB del piano C, `FormData` con file, `Blob`): il corpo arriva intero e identico (SHA-256) fino a 16 MiB, oltre la pagina riceve un 413 locale e niente parte verso il PC, mai un corpo troncato in silenzio — prova dello schema (Task 7), `testLargeStreamBodyForwarded`, `testBodyOverLimitIs413` (Task 10).

---

## Mappa dei file

| File | Responsabilità | Task |
|---|---|---|
| `service/internal/phones/store.go`, `pairing.go`, `manager.go` | telefoni abbinati (`phones.json`), finestra e codice monouso, revoca che chiude gli stream | 1 |
| `service/internal/phones/tls.go`, `link.go` | certificato, impronta, link del QR, indirizzi IPv4 del PC | 2 |
| `protocol/vectors/app-pairing.json` | vettori condivisi Go ↔ Swift (certificato DER, impronta, link) | 2 |
| `service/internal/api/phone.go`, `service/internal/api/web.go` | gestore della porta 8767: token, elenco chiuso, segno nel contesto, `/app/pair`, `/app/whoami` | 3 |
| `service/internal/api/phones.go`, `service/internal/api/server.go` | rotte `/api/phones…` (solo 8765) | 4, 5 |
| `service/internal/config/config.go` | `app_port`, `CALAMAIO_APP_PORT` | 4 |
| `service/internal/server/app.go`, `service/internal/server/server.go` | ascoltatore HTTPS a richiesta, collegamenti, Bonjour | 4, 5 |
| `service/internal/phones/qr.go` | QR del link in SVG | 5 |
| `service/internal/mdns/mdns.go` | un solo responder, annunci aggiunti e tolti | 5 |
| `service/cmd/calamaio/main.go` | percorsi di `phones.json` e `app-tls`, `serve --loopback` | 5 |
| `docs/PROTOCOLLO.md`, `docs/DECISIONI.md` | documentazione | 5, 7, 11 |
| `service/internal/webui/static/device.html`, `assets/js/device.js`, `assets/js/api.js`, `assets/js/phones.js`, `assets/app.css`, `tools/webui-dev/test-units.mjs`, `tools/webui-dev/mock-server.mjs` | sezione «Telefoni» | 6 |
| `ios/project.yml`, `ios/Config/App.xcconfig`, `ios/.gitignore`, `ios/App/*`, `ios/AppTests/*` | progetto, app, prove nel simulatore | 7, 9, 10, 11 |
| `ios/CalamaioKit/Package.swift`, `Sources/CalamaioKit/*`, `Tests/CalamaioKitTests/*` | logica dell'app provata con `swift test` | 7–11 |
| `ios/scripts/e2e.sh` | servizio vero in locale per le prove end-to-end | 8 |
| `ios/AppUITests/*` | prova UI: abbinamento col link e webapp caricata | 11 |

---

### Task 1: Telefoni abbinati e finestra di abbinamento

**Files:**
- Create: `service/internal/phones/store.go`, `service/internal/phones/pairing.go`, `service/internal/phones/manager.go`
- Test: `service/internal/phones/store_test.go`, `service/internal/phones/pairing_test.go`, `service/internal/phones/manager_test.go`

**Interfaces:**
- Consumes: niente.
- Produces:
  - `type Phone struct { ID, Name, TokenHash string; Paired, LastUsed int64 }` (JSON `id, name, tokenSha256, paired, lastUsed`; tempi in ms);
  - `const TokenBytes = 32`, `MaxName = 40`, `TouchEvery = 10 * time.Minute`; `var ErrNotFound`, `var ErrNoWindow`; `type CodeError struct{ Left int }`;
  - `func Open(path string) (*Store, error)` (`""` = solo in memoria); `(*Store).Add(name string) (token string, p Phone, err error)`, `Lookup(token string) (Phone, bool)`, `Has(id string) bool`, `Touch(id string)`, `Revoke(id string) error`, `List() []Phone`, `Len() int`, `Flush() error`; campi `Path string`, `Now func() time.Time`;
  - `func ValidToken(t string) bool`;
  - `type Pairing struct { Now func() time.Time; Window time.Duration }` con `Open() (code string, expires time.Time)`, `Cancel()`, `Active() (code string, expires time.Time, ok bool)`, `Redeem(code string) error`; `const DefaultWindow = 5 * time.Minute`, `MaxAttempts = 5`;
  - `type Manager struct { Store *Store; Pairing *Pairing; OnChange func() }`, `func NewManager(st *Store) *Manager`, metodi `Wanted() bool`, `OpenWindow() (string, time.Time)`, `CancelWindow()`, `Lookup(token string) (Phone, bool)`, `Touch(id string)`, `Redeem(code, name, oldToken string) (string, Phone, error)`, `Revoke(id string) error`, `Track(ctx context.Context, id string) (context.Context, func())`, `Close()`.

- [ ] **Step 1: Scrivere i test che falliscono**

`service/internal/phones/store_test.go`:

```go
package phones

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestStoreAddLookupRevoke(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sub", "phones.json")
	s, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	tok, p, err := s.Add("  iPhone di Prova\n ")
	if err != nil {
		t.Fatal(err)
	}
	if !ValidToken(tok) || p.Name != "iPhone di Prova" || p.ID == "" || p.Paired == 0 || len(p.TokenHash) != 64 {
		t.Fatalf("%q %+v", tok, p)
	}
	if got, ok := s.Lookup(tok); !ok || got.ID != p.ID {
		t.Fatalf("lookup: %v %+v", ok, got)
	}
	for _, bad := range []string{"", strings.ToUpper(tok), tok[:63], tok + "0", strings.Repeat("0", 64), p.TokenHash} {
		if _, ok := s.Lookup(bad); ok {
			t.Fatalf("token falso accettato: %q", bad)
		}
	}
	// sul disco solo l'hash, file 0600 in una cartella 0700
	b, _ := os.ReadFile(path)
	if strings.Contains(string(b), tok) || !strings.Contains(string(b), p.TokenHash) {
		t.Fatalf("phones.json: %s", b)
	}
	if st, _ := os.Stat(path); st.Mode().Perm() != 0o600 {
		t.Fatalf("permessi del file %v", st.Mode().Perm())
	}
	if st, _ := os.Stat(filepath.Dir(path)); st.Mode().Perm() != 0o700 {
		t.Fatalf("permessi della cartella %v", st.Mode().Perm())
	}
	s2, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := s2.Lookup(tok); !ok || !s2.Has(p.ID) {
		t.Fatal("telefono perso al riavvio")
	}
	if err := s2.Revoke(p.ID); err != nil {
		t.Fatal(err)
	}
	if _, ok := s2.Lookup(tok); ok || s2.Has(p.ID) {
		t.Fatal("revocato ma ancora valido")
	}
	if err := s2.Revoke(p.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("seconda revoca: %v", err)
	}
	if s3, _ := Open(path); s3.Len() != 0 {
		t.Fatal("revoca non salvata")
	}
}

func TestStoreTouchThrottled(t *testing.T) {
	path := filepath.Join(t.TempDir(), "phones.json")
	now := time.UnixMilli(1_791_000_000_000)
	s, _ := Open(path)
	s.Now = func() time.Time { return now }
	tok, p, _ := s.Add("x")
	disk := func() int64 { d, _ := Open(path); return d.List()[0].LastUsed }

	now = now.Add(time.Minute)
	s.Touch(p.ID)
	if got, _ := s.Lookup(tok); got.LastUsed != now.UnixMilli() {
		t.Fatal("ultimo uso non aggiornato in memoria")
	}
	if disk() != now.UnixMilli() {
		t.Fatal("il primo uso va scritto subito")
	}
	first := now
	now = now.Add(5 * time.Minute)
	s.Touch(p.ID)
	if disk() != first.UnixMilli() {
		t.Fatal("scritto prima di 10 minuti")
	}
	now = now.Add(6 * time.Minute)
	s.Touch(p.ID)
	if disk() != now.UnixMilli() {
		t.Fatal("non scritto dopo 10 minuti")
	}
	now = now.Add(time.Minute)
	s.Touch(p.ID)
	if err := s.Flush(); err != nil || disk() != now.UnixMilli() {
		t.Fatalf("Flush: %v", err)
	}
}

func TestStoreBrokenFileNotOverwritten(t *testing.T) {
	path := filepath.Join(t.TempDir(), "phones.json")
	os.WriteFile(path, []byte("{rotto"), 0o600)
	if _, err := Open(path); err == nil {
		t.Fatal("file rotto accettato")
	}
	if b, _ := os.ReadFile(path); string(b) != "{rotto" {
		t.Fatal("file rotto toccato")
	}
}

func TestCleanName(t *testing.T) {
	cases := map[string]string{
		"":                       "iPhone",
		"   ":                    "iPhone",
		"iPhone\x00 di\tProva\n": "iPhone diProva",
		strings.Repeat("è", 50):  strings.Repeat("è", 40),
	}
	for in, want := range cases {
		if got := cleanName(in); got != want {
			t.Errorf("cleanName(%q) = %q, atteso %q", in, got, want)
		}
	}
}
```

`service/internal/phones/pairing_test.go`:

```go
package phones

import (
	"errors"
	"strings"
	"testing"
	"time"
)

func TestPairingOneTimeCode(t *testing.T) {
	now := time.UnixMilli(1_791_000_000_000)
	p := &Pairing{Now: func() time.Time { return now }}
	if err := p.Redeem(strings.Repeat("0", 32)); !errors.Is(err, ErrNoWindow) {
		t.Fatalf("senza finestra: %v", err)
	}
	code, exp := p.Open()
	if !isLowerHex(code, 32) || exp.Sub(now) != 5*time.Minute {
		t.Fatalf("%q %v", code, exp)
	}
	if c, _, ok := p.Active(); !ok || c != code {
		t.Fatal("finestra non attiva")
	}
	if err := p.Redeem(code); err != nil {
		t.Fatal(err)
	}
	if err := p.Redeem(code); !errors.Is(err, ErrNoWindow) {
		t.Fatalf("codice riusato: %v", err)
	}
}

func TestPairingFiveWrongAttemptsClose(t *testing.T) {
	p := &Pairing{}
	code, _ := p.Open()
	wrong := strings.Repeat("a", 32)
	if wrong == code {
		wrong = strings.Repeat("b", 32)
	}
	for i := 1; i <= MaxAttempts; i++ {
		var ce *CodeError
		if err := p.Redeem(wrong); !errors.As(err, &ce) || ce.Left != MaxAttempts-i {
			t.Fatalf("tentativo %d: %v", i, err)
		}
	}
	if err := p.Redeem(code); !errors.Is(err, ErrNoWindow) {
		t.Fatalf("dopo 5 errori il codice giusto vale ancora: %v", err)
	}
	if _, _, ok := p.Active(); ok {
		t.Fatal("finestra ancora aperta")
	}
}

func TestPairingExpiresAndReopenResets(t *testing.T) {
	now := time.UnixMilli(1_791_000_000_000)
	p := &Pairing{Now: func() time.Time { return now }}
	code, _ := p.Open()
	now = now.Add(5 * time.Minute)
	if _, _, ok := p.Active(); ok {
		t.Fatal("scaduta ma attiva")
	}
	if err := p.Redeem(code); !errors.Is(err, ErrNoWindow) {
		t.Fatalf("codice scaduto: %v", err)
	}
	c1, _ := p.Open()
	for i := 0; i < 3; i++ {
		p.Redeem(strings.Repeat("c", 32))
	}
	c2, _ := p.Open()
	var ce *CodeError
	if c2 == c1 || !errors.As(p.Redeem(strings.Repeat("d", 32)), &ce) || ce.Left != 4 {
		t.Fatalf("riaprire deve dare un codice nuovo e 5 tentativi: %v", ce)
	}
}
```

`service/internal/phones/manager_test.go`:

```go
package phones

import (
	"context"
	"strings"
	"testing"
	"time"
)

func TestManagerRedeemRevokeAndStreams(t *testing.T) {
	st, _ := Open("")
	m := NewManager(st)
	t.Cleanup(m.Close)
	changes := 0
	m.OnChange = func() { changes++; _ = m.Wanted() } // OnChange può richiamare il Manager: niente lucchetti presi
	if m.Wanted() {
		t.Fatal("acceso senza telefoni né finestra")
	}
	code, _ := m.OpenWindow()
	if !m.Wanted() || changes != 1 {
		t.Fatalf("finestra: wanted=%v changes=%d", m.Wanted(), changes)
	}
	tok, p, err := m.Redeem(code, "Telefono", "")
	if err != nil || !m.Wanted() || changes != 2 {
		t.Fatalf("redeem: %v %d", err, changes)
	}
	ctx, done := m.Track(context.Background(), p.ID)
	defer done()
	if err := m.Revoke(p.ID); err != nil {
		t.Fatal(err)
	}
	select {
	case <-ctx.Done():
	case <-time.After(time.Second):
		t.Fatal("la revoca non chiude lo stream aperto")
	}
	if _, ok := m.Lookup(tok); ok || m.Wanted() || changes != 3 {
		t.Fatalf("dopo la revoca: wanted=%v changes=%d", m.Wanted(), changes)
	}
	ctx2, done2 := m.Track(context.Background(), p.ID)
	defer done2()
	if ctx2.Err() == nil {
		t.Fatal("contesto vivo per un telefono revocato")
	}
}

func TestManagerRedeemReplacesOldToken(t *testing.T) {
	st, _ := Open("")
	m := NewManager(st)
	t.Cleanup(m.Close)
	c1, _ := m.OpenWindow()
	tok1, p1, _ := m.Redeem(c1, "A", "")
	c2, _ := m.OpenWindow()
	tok2, p2, err := m.Redeem(c2, "A", tok1)
	if err != nil || p2.ID == p1.ID {
		t.Fatalf("%v %+v", err, p2)
	}
	if _, ok := m.Lookup(tok1); ok {
		t.Fatal("il token vecchio vale ancora")
	}
	if _, ok := m.Lookup(tok2); !ok || st.Len() != 1 {
		t.Fatalf("telefoni: %d", st.Len())
	}
	c3, _ := m.OpenWindow()
	if _, _, err := m.Redeem(c3, "B", strings.Repeat("f", 64)); err != nil || st.Len() != 2 {
		t.Fatalf("un token vecchio sconosciuto si ignora: %v %d", err, st.Len())
	}
}

func TestManagerWrongCodeNotifies(t *testing.T) {
	st, _ := Open("")
	m := NewManager(st)
	t.Cleanup(m.Close)
	changes := 0
	m.OnChange = func() { changes++ }
	m.OpenWindow()
	for i := 0; i < MaxAttempts; i++ {
		m.Redeem(strings.Repeat("e", 32), "x", "")
	}
	if m.Wanted() || changes != 1+MaxAttempts {
		t.Fatalf("la quinta prova sbagliata deve chiudere la finestra e avvisare: %v %d", m.Wanted(), changes)
	}
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/phones/ 2>&1 | tail -n 20`
Expected: FAIL (`undefined: Open`, `undefined: Pairing`, `undefined: NewManager`).

- [ ] **Step 3: Scrivere `store.go`**

`service/internal/phones/store.go`:

```go
// Package phones: telefoni abbinati al servizio (app iPhone, spec 2026-10-16 §3): token, finestra di
// abbinamento, certificato e link del QR. Il token non si salva mai: in phones.json c'è solo il suo SHA-256.
package phones

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode"
)

const (
	TokenBytes = 32 // 256 bit, 64 cifre esadecimali
	MaxName    = 40 // caratteri del nome del telefono
	// TouchEvery: l'ultimo uso va su disco al massimo così spesso (in memoria è sempre aggiornato).
	TouchEvery = 10 * time.Minute
)

var ErrNotFound = errors.New("telefono sconosciuto")

// Phone è un telefono abbinato. TokenHash = SHA-256 del token (le 64 cifre come testo), in esadecimale.
type Phone struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	TokenHash string `json:"tokenSha256"`
	Paired    int64  `json:"paired"`   // ms
	LastUsed  int64  `json:"lastUsed"` // ms, 0 = mai
}

// Store: l'elenco dei telefoni, in <dati>/phones.json (0600). Path vuoto = solo in memoria (test).
type Store struct {
	Path string
	Now  func() time.Time

	mu     sync.Mutex
	phones []Phone
	saved  map[string]int64 // LastUsed già scritto su disco, per telefono
}

type storeFile struct {
	V      int     `json:"v"`
	Phones []Phone `json:"phones"`
}

// Open legge l'elenco. Un file illeggibile è un errore e non si tocca: chi chiama spegne l'abbinamento.
func Open(path string) (*Store, error) {
	s := &Store{Path: path, saved: map[string]int64{}}
	if path == "" {
		return s, nil
	}
	b, err := os.ReadFile(path)
	if errors.Is(err, fs.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return nil, err
	}
	var f storeFile
	if err := json.Unmarshal(b, &f); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	for _, p := range f.Phones {
		if p.ID != "" && isLowerHex(p.TokenHash, 64) {
			s.phones = append(s.phones, p)
			s.saved[p.ID] = p.LastUsed
		}
	}
	return s, nil
}

func (s *Store) now() time.Time {
	if s.Now != nil {
		return s.Now()
	}
	return time.Now()
}

// ValidToken: 64 cifre esadecimali minuscole.
func ValidToken(t string) bool { return isLowerHex(t, 2*TokenBytes) }

func isLowerHex(s string, n int) bool {
	if len(s) != n {
		return false
	}
	for _, c := range s {
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f') {
			return false
		}
	}
	return true
}

func hashToken(t string) string {
	sum := sha256.Sum256([]byte(t))
	return hex.EncodeToString(sum[:])
}

func randomHex(n int) string {
	b := make([]byte, n)
	_, _ = rand.Read(b) // dalla 1.24 non fallisce mai
	return hex.EncodeToString(b)
}

// cleanName: niente caratteri di controllo, al massimo MaxName caratteri; vuoto = «iPhone».
func cleanName(name string) string {
	var b strings.Builder
	n := 0
	for _, r := range strings.TrimSpace(name) {
		if unicode.IsControl(r) {
			continue
		}
		if n == MaxName {
			break
		}
		b.WriteRune(r)
		n++
	}
	if out := strings.TrimSpace(b.String()); out != "" {
		return out
	}
	return "iPhone"
}

// Add abbina un telefono nuovo e restituisce il suo token, che da qui in poi esiste solo nel telefono.
func (s *Store) Add(name string) (string, Phone, error) {
	tok := randomHex(TokenBytes)
	p := Phone{ID: randomHex(6), Name: cleanName(name), TokenHash: hashToken(tok), Paired: s.now().UnixMilli()}
	s.mu.Lock()
	defer s.mu.Unlock()
	next := append(append([]Phone(nil), s.phones...), p)
	if err := s.write(next); err != nil {
		return "", Phone{}, err
	}
	s.phones = next
	s.saved[p.ID] = 0
	return tok, p, nil
}

// Lookup: il telefono del token. Confronta tutti gli hash a tempo costante.
func (s *Store) Lookup(token string) (Phone, bool) {
	if !ValidToken(token) {
		return Phone{}, false
	}
	want := []byte(hashToken(token))
	s.mu.Lock()
	defer s.mu.Unlock()
	var found Phone
	ok := false
	for _, p := range s.phones {
		if subtle.ConstantTimeCompare([]byte(p.TokenHash), want) == 1 {
			found, ok = p, true
		}
	}
	return found, ok
}

func (s *Store) Has(id string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, p := range s.phones {
		if p.ID == id {
			return true
		}
	}
	return false
}

// Touch segna l'ultimo uso; su disco al massimo ogni TouchEvery (il primo uso subito).
func (s *Store) Touch(id string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := s.now().UnixMilli()
	for i := range s.phones {
		if s.phones[i].ID != id {
			continue
		}
		s.phones[i].LastUsed = now
		if now-s.saved[id] >= TouchEvery.Milliseconds() || s.saved[id] == 0 {
			if s.write(s.phones) == nil {
				s.saved[id] = now
			}
		}
		return
	}
}

// Flush scrive gli ultimi usi rimasti in memoria (allo spegnimento).
func (s *Store) Flush() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	dirty := false
	for _, p := range s.phones {
		if p.LastUsed != s.saved[p.ID] {
			dirty = true
		}
	}
	if !dirty {
		return nil
	}
	if err := s.write(s.phones); err != nil {
		return err
	}
	for _, p := range s.phones {
		s.saved[p.ID] = p.LastUsed
	}
	return nil
}

// Revoke toglie il telefono. Vale subito in memoria anche se phones.json non si scrive (l'errore torna a chi
// chiama, che lo mostra): un telefono revocato non deve restare valido per un disco pieno.
func (s *Store) Revoke(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	next := make([]Phone, 0, len(s.phones))
	for _, p := range s.phones {
		if p.ID != id {
			next = append(next, p)
		}
	}
	if len(next) == len(s.phones) {
		return ErrNotFound
	}
	s.phones = next
	delete(s.saved, id)
	return s.write(next)
}

// List: copia dell'elenco, dal più vecchio.
func (s *Store) List() []Phone {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := append([]Phone(nil), s.phones...)
	sort.SliceStable(out, func(i, j int) bool { return out[i].Paired < out[j].Paired })
	return out
}

func (s *Store) Len() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.phones)
}

// write: file temporaneo 0600 e rinomina (mai un phones.json a metà). Chiamata col lucchetto preso.
func (s *Store) write(list []Phone) error {
	if s.Path == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(s.Path), 0o700); err != nil {
		return err
	}
	if list == nil {
		list = []Phone{}
	}
	b, err := json.MarshalIndent(storeFile{V: 1, Phones: list}, "", "  ")
	if err != nil {
		return err
	}
	tmp := s.Path + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	if err := os.Chmod(tmp, 0o600); err != nil { // WriteFile non cambia i permessi di un file già esistente
		return err
	}
	return os.Rename(tmp, s.Path)
}
```

- [ ] **Step 4: Scrivere `pairing.go`**

`service/internal/phones/pairing.go`:

```go
package phones

import (
	"crypto/subtle"
	"errors"
	"fmt"
	"sync"
	"time"
)

const (
	DefaultWindow = 5 * time.Minute
	MaxAttempts   = 5
	CodeBytes     = 16 // 128 bit, 32 cifre esadecimali
)

var ErrNoWindow = errors.New("nessun abbinamento aperto")

// CodeError: codice sbagliato; Left = tentativi rimasti (0 = finestra chiusa).
type CodeError struct{ Left int }

func (e *CodeError) Error() string { return fmt.Sprintf("codice sbagliato (%d tentativi rimasti)", e.Left) }

// Pairing: la finestra di abbinamento. Un codice alla volta, monouso, al massimo 5 tentativi sbagliati.
type Pairing struct {
	Now    func() time.Time
	Window time.Duration // 0 = DefaultWindow

	mu      sync.Mutex
	code    string
	expires time.Time
	wrong   int
}

func (p *Pairing) now() time.Time {
	if p.Now != nil {
		return p.Now()
	}
	return time.Now()
}

// Open apre (o riapre) la finestra con un codice nuovo; il codice di prima non vale più.
func (p *Pairing) Open() (string, time.Time) {
	w := p.Window
	if w <= 0 {
		w = DefaultWindow
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	p.code, p.expires, p.wrong = randomHex(CodeBytes), p.now().Add(w), 0
	return p.code, p.expires
}

func (p *Pairing) Cancel() {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.code = ""
}

// Active: il codice della finestra aperta (per il QR); una finestra scaduta si chiude qui.
func (p *Pairing) Active() (string, time.Time, bool) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.code == "" || !p.now().Before(p.expires) {
		p.code = ""
		return "", time.Time{}, false
	}
	return p.code, p.expires, true
}

// Redeem: nil = codice giusto (e finestra chiusa); ErrNoWindow; *CodeError.
func (p *Pairing) Redeem(code string) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.code == "" || !p.now().Before(p.expires) {
		p.code = ""
		return ErrNoWindow
	}
	if subtle.ConstantTimeCompare([]byte(code), []byte(p.code)) == 1 {
		p.code = ""
		return nil
	}
	p.wrong++
	left := MaxAttempts - p.wrong
	if left <= 0 {
		p.code, left = "", 0
	}
	return &CodeError{Left: left}
}
```

- [ ] **Step 5: Scrivere `manager.go`**

`service/internal/phones/manager.go`:

```go
package phones

import (
	"context"
	"errors"
	"sync"
	"time"
)

// Manager mette insieme elenco e finestra, chiude gli stream di un telefono revocato e avvisa (OnChange)
// quando l'ascoltatore va acceso o spento. OnChange non viene mai chiamata col lucchetto preso.
type Manager struct {
	Store    *Store
	Pairing  *Pairing
	OnChange func()

	mu      sync.Mutex
	streams map[string]map[int]context.CancelFunc
	next    int
	timer   *time.Timer
}

func NewManager(st *Store) *Manager {
	return &Manager{Store: st, Pairing: &Pairing{}, streams: map[string]map[int]context.CancelFunc{}}
}

func (m *Manager) changed() {
	if m.OnChange != nil {
		m.OnChange()
	}
}

// Wanted: l'ascoltatore dei telefoni serve (almeno un telefono o una finestra aperta).
func (m *Manager) Wanted() bool {
	if m.Store.Len() > 0 {
		return true
	}
	_, _, ok := m.Pairing.Active()
	return ok
}

// OpenWindow apre la finestra e programma l'avviso della scadenza (l'ascoltatore si spegne se non serve più).
func (m *Manager) OpenWindow() (string, time.Time) {
	code, exp := m.Pairing.Open()
	m.mu.Lock()
	if m.timer != nil {
		m.timer.Stop()
	}
	m.timer = time.AfterFunc(time.Until(exp)+50*time.Millisecond, m.changed)
	m.mu.Unlock()
	m.changed()
	return code, exp
}

func (m *Manager) CancelWindow() {
	m.Pairing.Cancel()
	m.stopTimer()
	m.changed()
}

func (m *Manager) stopTimer() {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.timer != nil {
		m.timer.Stop()
		m.timer = nil
	}
}

func (m *Manager) Lookup(token string) (Phone, bool) { return m.Store.Lookup(token) }
func (m *Manager) Touch(id string)                   { m.Store.Touch(id) }

// Redeem: codice giusto → telefono nuovo. oldToken, se valido, è lo stesso telefono abbinato prima (QR nuovo
// dopo un cambio di indirizzo): il telefono vecchio si revoca, così sul PC non restano doppioni.
func (m *Manager) Redeem(code, name, oldToken string) (string, Phone, error) {
	if err := m.Pairing.Redeem(code); err != nil {
		m.changed() // la quinta prova sbagliata chiude la finestra
		return "", Phone{}, err
	}
	m.stopTimer()
	tok, p, err := m.Store.Add(name)
	if err != nil {
		m.changed()
		return "", Phone{}, err
	}
	if oldToken != "" {
		if op, ok := m.Store.Lookup(oldToken); ok && op.ID != p.ID {
			_ = m.revoke(op.ID)
		}
	}
	m.changed()
	return tok, p, nil
}

// Revoke toglie il telefono e chiude subito le sue richieste aperte (SSE compresi).
func (m *Manager) Revoke(id string) error {
	err := m.revoke(id)
	if !errors.Is(err, ErrNotFound) {
		m.changed()
	}
	return err
}

func (m *Manager) revoke(id string) error {
	err := m.Store.Revoke(id)
	if errors.Is(err, ErrNotFound) {
		return err
	}
	m.mu.Lock()
	cs := m.streams[id]
	delete(m.streams, id)
	m.mu.Unlock()
	for _, c := range cs {
		c()
	}
	return err
}

// Track: un contesto che si chiude alla revoca del telefono; done va chiamata a fine richiesta. Per un
// telefono già revocato il contesto è chiuso subito.
func (m *Manager) Track(ctx context.Context, id string) (context.Context, func()) {
	ctx, cancel := context.WithCancel(ctx)
	m.mu.Lock()
	m.next++
	n := m.next
	if m.streams[id] == nil {
		m.streams[id] = map[int]context.CancelFunc{}
	}
	m.streams[id][n] = cancel
	m.mu.Unlock()
	if !m.Store.Has(id) {
		cancel()
	}
	return ctx, func() {
		m.mu.Lock()
		delete(m.streams[id], n)
		if len(m.streams[id]) == 0 {
			delete(m.streams, id)
		}
		m.mu.Unlock()
		cancel()
	}
}

// Close ferma il timer della finestra (allo spegnimento del servizio).
func (m *Manager) Close() { m.stopTimer() }
```

- [ ] **Step 6: Eseguire e vedere passare**

Run: `cd service && go test ./internal/phones/ -v 2>&1 | tail -n 25`
Expected: PASS (`TestStoreAddLookupRevoke`, `TestStoreTouchThrottled`, `TestStoreBrokenFileNotOverwritten`, `TestCleanName`, `TestPairing…`, `TestManager…`).

Poi `cd service && go vet ./internal/phones/ && go test ./... 2>&1 | tail -n 5`
Expected: tutto `ok`.

- [ ] **Step 7: Commit**

```bash
git add service/internal/phones
git commit -m "Telefoni: elenco con il solo hash del token, finestra di abbinamento con codice monouso e 5 tentativi, revoca che chiude gli stream"
```

---

### Task 2: Certificato, impronta, link del QR e vettori condivisi

**Files:**
- Create: `service/internal/phones/tls.go`, `service/internal/phones/link.go`, `protocol/vectors/app-pairing.json` (generato dal test)
- Test: `service/internal/phones/tls_test.go`, `service/internal/phones/link_test.go`

**Interfaces:**
- Consumes: `isLowerHex` (Task 1).
- Produces:
  - `func Fingerprint(der []byte) string` (SHA-256, 64 cifre minuscole);
  - `func LoadOrCreateCert(dir, host string, now time.Time) (tls.Certificate, string, error)` (`dir ""` = certificato nuovo solo in memoria; il secondo valore è l'impronta; `Leaf` sempre impostato); `const CertYears = 10`;
  - `func Link(hosts []string, port int, fp, code, name string) string`;
  - `func LocalIPv4s() []string` (al massimo 4, private prima, niente loopback né 169.254/16);
  - file `protocol/vectors/app-pairing.json` con `cert.der_b64`, `cert.sha256`, `link.{hosts,port,code,name,uri}`, letto anche dai test Swift (Task 8).

- [ ] **Step 1: Scrivere i test che falliscono**

`service/internal/phones/tls_test.go`:

```go
package phones

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/x509"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

var t0 = time.Date(2026, 10, 16, 12, 0, 0, 0, time.UTC)

func TestCertCreateReuseAndPerms(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "app-tls")
	c1, fp1, err := LoadOrCreateCert(dir, "MacBook", t0)
	if err != nil {
		t.Fatal(err)
	}
	pub, ok := c1.Leaf.PublicKey.(*ecdsa.PublicKey)
	if !ok || pub.Curve != elliptic.P256() || c1.Leaf.PublicKeyAlgorithm != x509.ECDSA {
		t.Fatalf("chiave: %T", c1.Leaf.PublicKey)
	}
	if !c1.Leaf.NotAfter.Equal(t0.AddDate(CertYears, 0, 0)) {
		t.Fatalf("scadenza %v", c1.Leaf.NotAfter)
	}
	if fp1 != Fingerprint(c1.Certificate[0]) || !isLowerHex(fp1, 64) {
		t.Fatalf("impronta %q", fp1)
	}
	for name, want := range map[string]os.FileMode{"": 0o700, "key.pem": 0o600, "cert.pem": 0o600} {
		st, err := os.Stat(filepath.Join(dir, name))
		if err != nil || st.Mode().Perm() != want {
			t.Fatalf("%s: %v %v", name, err, st.Mode().Perm())
		}
	}
	os.Chmod(filepath.Join(dir, "key.pem"), 0o644)
	_, fp2, err := LoadOrCreateCert(dir, "MacBook", t0.Add(time.Hour))
	if err != nil || fp2 != fp1 {
		t.Fatalf("riletto: %v %s %s", err, fp1, fp2)
	}
	if st, _ := os.Stat(filepath.Join(dir, "key.pem")); st.Mode().Perm() != 0o600 {
		t.Fatal("permessi della chiave non riparati")
	}
	// nessuna cartella temporanea lasciata accanto
	if m, _ := filepath.Glob(filepath.Join(filepath.Dir(dir), ".app-tls-*")); len(m) != 0 {
		t.Fatalf("avanzi: %v", m)
	}
}

func TestCertBrokenNotRegenerated(t *testing.T) {
	dir := t.TempDir() // cartella già esistente e vuota: il certificato si crea dentro
	if _, _, err := LoadOrCreateCert(dir, "pc", t0); err != nil {
		t.Fatal(err)
	}
	kp, cp := filepath.Join(dir, "key.pem"), filepath.Join(dir, "cert.pem")
	cert, _ := os.ReadFile(cp)
	os.WriteFile(kp, []byte("rotto"), 0o600)
	if _, _, err := LoadOrCreateCert(dir, "pc", t0); err == nil || !strings.Contains(err.Error(), "illeggibile") {
		t.Fatalf("chiave rotta: %v", err)
	}
	if b, _ := os.ReadFile(kp); string(b) != "rotto" {
		t.Fatal("chiave rotta riscritta")
	}
	if b, _ := os.ReadFile(cp); string(b) != string(cert) {
		t.Fatal("certificato riscritto")
	}
	os.Remove(kp) // solo il certificato: anche questo è un errore, mai un certificato nuovo
	if _, _, err := LoadOrCreateCert(dir, "pc", t0); err == nil {
		t.Fatal("certificato senza chiave accettato")
	}
	if _, err := os.Stat(kp); !os.IsNotExist(err) {
		t.Fatal("chiave ricreata")
	}
}

func TestCertExpired(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "app-tls")
	LoadOrCreateCert(dir, "pc", t0)
	if _, _, err := LoadOrCreateCert(dir, "pc", t0.AddDate(CertYears, 0, 1)); err == nil || !strings.Contains(err.Error(), "scaduto") {
		t.Fatalf("scaduto: %v", err)
	}
}

func TestCertInMemory(t *testing.T) {
	c, fp, err := LoadOrCreateCert("", "pc", t0)
	if err != nil || c.Leaf == nil || fp != Fingerprint(c.Certificate[0]) {
		t.Fatalf("%v %q", err, fp)
	}
}
```

`service/internal/phones/link_test.go`:

```go
package phones

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"flag"
	"net"
	"os"
	"testing"
)

var updateVectors = flag.Bool("update", false, "riscrive protocol/vectors/app-pairing.json")

const vectorsPath = "../../../protocol/vectors/app-pairing.json"

type pairingVectors struct {
	Note string `json:"note"`
	Cert struct {
		DER    string `json:"der_b64"`
		SHA256 string `json:"sha256"`
	} `json:"cert"`
	Link struct {
		Hosts []string `json:"hosts"`
		Port  int      `json:"port"`
		Code  string   `json:"code"`
		Name  string   `json:"name"`
		URI   string   `json:"uri"`
	} `json:"link"`
}

func TestLink(t *testing.T) {
	got := Link([]string{"192.168.1.20", "10.0.0.7"}, 8767, "ab", "cd", "Mac di Prova & C.")
	want := "calamaio://pair?v=1&h=192.168.1.20,10.0.0.7&p=8767&fp=ab&c=cd&n=Mac%20di%20Prova%20%26%20C."
	if got != want {
		t.Fatalf("\n%s\n%s", got, want)
	}
}

// Vettori condivisi con ios/CalamaioKit (Task 8): stessa impronta e stesso link dalle due parti.
func TestAppPairingVectors(t *testing.T) {
	if *updateVectors {
		c, fp, err := LoadOrCreateCert("", "vettori", t0)
		if err != nil {
			t.Fatal(err)
		}
		var v pairingVectors
		v.Note = "Vettori condivisi fra service/internal/phones e ios/CalamaioKit: impronta del certificato e link del QR. " +
			"Rigenerare con: cd service && go test ./internal/phones -run TestAppPairingVectors -update"
		v.Cert.DER = base64.StdEncoding.EncodeToString(c.Certificate[0])
		v.Cert.SHA256 = fp
		v.Link.Hosts = []string{"192.168.1.20", "10.0.0.7"}
		v.Link.Port = 8767
		v.Link.Code = "00112233445566778899aabbccddeeff"
		v.Link.Name = "Mac di Prova & C."
		v.Link.URI = Link(v.Link.Hosts, v.Link.Port, fp, v.Link.Code, v.Link.Name)
		var b bytes.Buffer
		enc := json.NewEncoder(&b)
		enc.SetEscapeHTML(false) // «&» leggibile nell'URI
		enc.SetIndent("", "  ")
		if err := enc.Encode(v); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(vectorsPath, b.Bytes(), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	b, err := os.ReadFile(vectorsPath)
	if err != nil {
		t.Fatalf("%v (generali con -update)", err)
	}
	var v pairingVectors
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	der, err := base64.StdEncoding.DecodeString(v.Cert.DER)
	if err != nil || Fingerprint(der) != v.Cert.SHA256 {
		t.Fatalf("impronta: %v %s", err, v.Cert.SHA256)
	}
	if got := Link(v.Link.Hosts, v.Link.Port, v.Cert.SHA256, v.Link.Code, v.Link.Name); got != v.Link.URI {
		t.Fatalf("link\n%s\n%s", got, v.Link.URI)
	}
}

func TestLocalIPv4s(t *testing.T) {
	ips := LocalIPv4s()
	if len(ips) > 4 {
		t.Fatalf("troppi: %v", ips)
	}
	for _, s := range ips {
		ip := net.ParseIP(s)
		if ip == nil || ip.To4() == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
			t.Fatalf("indirizzo non ammesso: %q", s)
		}
	}
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/phones/ -run 'TestCert|TestLink|TestAppPairing|TestLocal' 2>&1 | tail -n 20`
Expected: FAIL (`undefined: LoadOrCreateCert`, `undefined: Link`).

- [ ] **Step 3: Scrivere `tls.go`**

`service/internal/phones/tls.go`:

```go
package phones

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"io/fs"
	"math/big"
	"os"
	"path/filepath"
	"time"
)

const CertYears = 10

// Fingerprint: SHA-256 del certificato (DER) in esadecimale minuscolo. È ciò che l'app fissa dal QR.
func Fingerprint(der []byte) string {
	sum := sha256.Sum256(der)
	return hex.EncodeToString(sum[:])
}

// LoadOrCreateCert: il certificato della porta dei telefoni in dir (cert.pem e key.pem 0600, cartella 0700).
// Si crea solo se la cartella non ha né l'uno né l'altro. Uno rovinato, a metà o scaduto è un errore e NON si
// rifà: ogni telefono abbinato ne ha fissato l'impronta. Per rifarlo si cancella la cartella e si riabbinano
// i telefoni. dir "" = certificato nuovo solo in memoria (test).
func LoadOrCreateCert(dir, host string, now time.Time) (tls.Certificate, string, error) {
	if dir == "" {
		cp, kp, err := newCert(host, now)
		if err != nil {
			return tls.Certificate{}, "", err
		}
		return keyPair(cp, kp)
	}
	cpath, kpath := filepath.Join(dir, "cert.pem"), filepath.Join(dir, "key.pem")
	_, cerr := os.Stat(cpath)
	_, kerr := os.Stat(kpath)
	if errors.Is(cerr, fs.ErrNotExist) && errors.Is(kerr, fs.ErrNotExist) {
		if err := create(dir, host, now); err != nil {
			return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s: %w", dir, err)
		}
	}
	cp, err1 := os.ReadFile(cpath)
	kp, err2 := os.ReadFile(kpath)
	if err := errors.Join(err1, err2); err != nil {
		return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s illeggibile: %w", dir, err)
	}
	c, fp, err := keyPair(cp, kp)
	if err != nil {
		return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s illeggibile: %w", dir, err)
	}
	if now.After(c.Leaf.NotAfter) {
		return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s scaduto il %s", dir, c.Leaf.NotAfter.Format("2006-01-02"))
	}
	_ = os.Chmod(kpath, 0o600) // ripara i permessi se qualcuno li ha allargati
	return c, fp, nil
}

func keyPair(certPEM, keyPEM []byte) (tls.Certificate, string, error) {
	c, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return tls.Certificate{}, "", err
	}
	if c.Leaf == nil {
		if c.Leaf, err = x509.ParseCertificate(c.Certificate[0]); err != nil {
			return tls.Certificate{}, "", err
		}
	}
	return c, Fingerprint(c.Certificate[0]), nil
}

// create: cartella nuova preparata accanto e rinominata (mai una cartella a metà); cartella già esistente e
// vuota: file temporanei e rinomina, prima la chiave.
func create(dir, host string, now time.Time) error {
	cp, kp, err := newCert(host, now)
	if err != nil {
		return err
	}
	target, fresh := dir, false
	if _, err := os.Stat(dir); errors.Is(err, fs.ErrNotExist) {
		if err := os.MkdirAll(filepath.Dir(dir), 0o700); err != nil {
			return err
		}
		tmp, err := os.MkdirTemp(filepath.Dir(dir), ".app-tls-*")
		if err != nil {
			return err
		}
		defer os.RemoveAll(tmp) // dopo la rinomina non c'è più: non fa niente
		target, fresh = tmp, true
	}
	if err := os.Chmod(target, 0o700); err != nil {
		return err
	}
	if err := writeSecret(filepath.Join(target, "key.pem"), kp); err != nil {
		return err
	}
	if err := writeSecret(filepath.Join(target, "cert.pem"), cp); err != nil {
		return err
	}
	if fresh {
		return os.Rename(target, dir)
	}
	return nil
}

func writeSecret(path string, b []byte) error {
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// newCert: ECDSA P-256 autofirmato, 10 anni. Nessun nome da verificare: l'app controlla solo l'impronta.
func newCert(host string, now time.Time) (certPEM, keyPEM []byte, err error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 127))
	if err != nil {
		return nil, nil, err
	}
	tpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "Calamaio " + host, Organization: []string{"Calamaio"}},
		NotBefore:             now.Add(-time.Hour),
		NotAfter:              now.AddDate(CertYears, 0, 0),
		KeyUsage:              x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		DNSNames:              []string{"calamaio.local"},
	}
	der, err := x509.CreateCertificate(rand.Reader, tpl, tpl, &key.PublicKey, key)
	if err != nil {
		return nil, nil, err
	}
	kder, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		return nil, nil, err
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}),
		pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: kder}), nil
}
```

Attenzione: `NotAfter` del test è `t0.AddDate(10,0,0)` esatto; `x509` tronca ai secondi, `t0` è già a secondi interi.

- [ ] **Step 4: Scrivere `link.go`**

`service/internal/phones/link.go`:

```go
package phones

import (
	"fmt"
	"net"
	"net/url"
	"sort"
	"strings"
)

// Link: il contenuto del QR di abbinamento (spec §3.2). Gli spazi del nome sono %20: URLComponents di iOS
// non legge «+» come spazio.
func Link(hosts []string, port int, fp, code, name string) string {
	n := strings.ReplaceAll(url.QueryEscape(name), "+", "%20")
	return fmt.Sprintf("calamaio://pair?v=1&h=%s&p=%d&fp=%s&c=%s&n=%s", strings.Join(hosts, ","), port, fp, code, n)
}

// LocalIPv4s: gli indirizzi IPv4 del PC per il QR (interfacce accese, niente loopback né 169.254/16),
// prima quelli privati, al massimo 4.
func LocalIPv4s() []string {
	ifs, _ := net.Interfaces()
	var out []string
	for _, ifc := range ifs {
		if ifc.Flags&net.FlagUp == 0 || ifc.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, _ := ifc.Addrs()
		for _, a := range addrs {
			ipn, ok := a.(*net.IPNet)
			if !ok {
				continue
			}
			ip := ipn.IP.To4()
			if ip == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
				continue
			}
			out = append(out, ip.String())
		}
	}
	sort.SliceStable(out, func(i, j int) bool {
		return net.ParseIP(out[i]).IsPrivate() && !net.ParseIP(out[j]).IsPrivate()
	})
	if len(out) > 4 {
		out = out[:4]
	}
	return out
}
```

- [ ] **Step 5: Generare i vettori e vedere passare**

Run: `cd service && go test ./internal/phones/ -run TestAppPairingVectors -update 2>&1 | tail -n 5 && go test ./internal/phones/ -v 2>&1 | tail -n 30`
Expected: PASS; `protocol/vectors/app-pairing.json` esiste, contiene `"uri": "calamaio://pair?v=1&h=192.168.1.20,10.0.0.7&p=8767&fp=<64 cifre>&c=00112233445566778899aabbccddeeff&n=Mac%20di%20Prova%20%26%20C."`. Un secondo `go test` senza `-update` passa senza riscrivere il file (`git status --short protocol/` lo mostra solo come nuovo).

- [ ] **Step 6: Commit**

```bash
git add service/internal/phones protocol/vectors/app-pairing.json
git commit -m "Telefoni: certificato ECDSA autofirmato di 10 anni mai rifatto da solo, impronta SHA-256, link del QR e vettori condivisi con l'app"
```

---
### Task 3: Gestore della porta dei telefoni: token, elenco chiuso, `/app/pair`, `/app/whoami`

**Files:**
- Create: `service/internal/api/phone.go`
- Modify: `service/internal/api/web.go:63-92` (`guard` e `sameOrigin`)
- Test: `service/internal/api/phone_test.go`

**Interfaces:**
- Consumes: `phones.Phone`, `phones.ValidToken`, `phones.ErrNoWindow`, `*phones.CodeError`, `phones.NewManager`, `phones.Open` (Task 1).
- Produces:
  - `type PhoneAuth interface { Lookup(token string) (phones.Phone, bool); Touch(id string); Redeem(code, name, oldToken string) (string, phones.Phone, error); Track(ctx context.Context, id string) (context.Context, func()) }` (lo soddisfa `*phones.Manager`);
  - `type PhoneOptions struct { Auth PhoneAuth; Whoami func(phones.Phone) any; Next http.Handler; Log *log.Logger }`;
  - `func NewPhoneHandler(o PhoneOptions) http.Handler`;
  - `var PhoneHiddenPages = []string{"firmware.html"}`;
  - risposte: 401 `{"error":"non autorizzato"}` + `WWW-Authenticate: Bearer`; 404 `{"error":"non trovato"}`; `POST /app/pair` → 200 `{token, id, name}` · 403 `{error:"codice sbagliato", left}` · 409 `{error}` · 400 · 405 · 415; `GET /app/whoami` → 200 con ciò che restituisce `Whoami`.
  - comportamento: `guard` non controlla l'Host e `sameOrigin` risponde «stessa origine» **solo** per richieste passate da `NewPhoneHandler` (segno nel contesto, chiave non esportata).

- [ ] **Step 1: Scrivere i test che falliscono**

`service/internal/api/phone_test.go`:

```go
package api

import (
	"context"
	"encoding/json"
	"io"
	"io/fs"
	"log"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"calamaio/internal/phones"
	"calamaio/internal/webui"
)

// recorder è la mux finta dietro il gestore: conta chi arriva, se ha il segno del telefono e se l'Authorization
// è stata tolta.
type recorder struct {
	mu    sync.Mutex
	hits  []string
	phone bool
	auth  string
}

func (rc *recorder) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	rc.mu.Lock()
	rc.hits = append(rc.hits, r.Method+" "+r.URL.Path)
	_, rc.phone = phoneOf(r)
	rc.auth = r.Header.Get("Authorization")
	rc.mu.Unlock()
	w.WriteHeader(http.StatusOK)
}

func (rc *recorder) count() int { rc.mu.Lock(); defer rc.mu.Unlock(); return len(rc.hits) }

// phoneSetup: gestore dei telefoni con un telefono già abbinato; restituisce server, Manager e token.
func phoneSetup(t *testing.T, next http.Handler) (*httptest.Server, *phones.Manager, string) {
	t.Helper()
	st, _ := phones.Open("")
	m := phones.NewManager(st)
	t.Cleanup(m.Close)
	code, _ := m.OpenWindow()
	tok, _, err := m.Redeem(code, "Prova", "")
	if err != nil {
		t.Fatal(err)
	}
	h := NewPhoneHandler(PhoneOptions{Auth: m, Next: next, Log: log.New(io.Discard, "", 0),
		Whoami: func(p phones.Phone) any { return map[string]any{"phone": map[string]string{"id": p.ID, "name": p.Name}} }})
	srv := httptest.NewServer(h)
	t.Cleanup(srv.Close)
	return srv, m, tok
}

func phoneReq(t *testing.T, srv *httptest.Server, method, path, auth, ctype, body string, hdr ...string) (int, string, http.Header) {
	t.Helper()
	var rd io.Reader
	if body != "" {
		rd = strings.NewReader(body)
	}
	req, _ := http.NewRequest(method, srv.URL+path, rd)
	if auth != "" {
		req.Header.Set("Authorization", auth)
	}
	if ctype != "" {
		req.Header.Set("Content-Type", ctype)
	}
	for i := 0; i+1 < len(hdr); i += 2 {
		if hdr[i] == "Host" {
			req.Host = hdr[i+1]
			continue
		}
		req.Header.Set(hdr[i], hdr[i+1])
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, string(b), resp.Header
}

func TestPhoneNeedsToken(t *testing.T) {
	rc := &recorder{}
	srv, _, tok := phoneSetup(t, rc)
	var first string
	for _, auth := range []string{"", "Bearer", "Bearer " + strings.Repeat("0", 64), "Bearer " + strings.ToUpper(tok),
		"bearer " + tok, "Basic " + tok, "Bearer " + tok + " ", "Bearer " + tok[:63]} {
		for _, path := range []string{"/", "/api/status", "/api/backups", "/hook", "/app/whoami", "/nope"} {
			code, body, hd := phoneReq(t, srv, http.MethodGet, path, auth, "", "")
			if code != 401 || hd.Get("WWW-Authenticate") != "Bearer" {
				t.Fatalf("%q %s: %d %q", auth, path, code, body)
			}
			if first == "" {
				first = body
			} else if body != first {
				t.Fatalf("401 diversi (dicono troppo): %q / %q", first, body)
			}
		}
	}
	if rc.count() != 0 {
		t.Fatalf("arrivate alla webapp senza token: %v", rc.hits)
	}
}

// Ogni classe di rotte vietata dalla spec §3.1, con un token valido: 404 e la webapp non la vede mai.
func TestPhoneForbiddenRoutes(t *testing.T) {
	rc := &recorder{}
	srv, _, tok := phoneSetup(t, rc)
	forbidden := map[string][]string{
		"hook":                {"POST /hook", "POST /hook?wait=1", "GET /hook", "POST /hook/context", "GET /hook/context"},
		"status line":         {"POST /statusline", "GET /statusline"},
		"debug":               {"GET /debug", "POST /debug/fake"},
		"abbinamento scheda":  {"POST /api/displays/b/pair/start", "POST /api/displays/b/pair/code", "POST /api/displays/b/pair/cancel", "POST /api/displays/b/unpair"},
		"wifi della scheda":   {"POST /api/displays/b/wifi/add", "POST /api/displays/b/wifi/forget"},
		"OTA e firmware":      {"POST /api/displays/b/ota", "POST /api/displays/b/ota/image", "POST /api/displays/b/ota/cancel", "GET /api/firmware", "GET /api/firmware/parts/firmware.bin", "GET /firmware.html", "GET /vendor/esptool-js-0.7.0/bundle.js"},
		"backup":              {"GET /api/backups", "POST /api/backups", "GET /api/backups/x/data", "DELETE /api/backups/x"},
		"registro remoto":     {"POST /api/displays/b/log/start", "POST /api/displays/b/log/stop", "GET /api/displays/b/log", "GET /api/displays/b/log/file"},
		"telefoni":            {"GET /api/phones", "POST /api/phones/pair", "POST /api/phones/pair/cancel", "DELETE /api/phones/x", "GET /api/phones/pair/qr.svg"},
		"metodi e sconosciute": {"PUT /api/status", "DELETE /api/status", "POST /api/events", "PATCH /api/themes/x", "GET /api/nope", "GET /api/crashes/x/extra", "POST /api/displays/b/nope", "GET /api/gb/games", "OPTIONS /api/status"},
		"percorsi furbi":      {"GET /api//backups", "GET /api/status/../backups", "GET /API/backups", "GET /api/backups/", "GET /assets/../api/backups", "GET /%2e%2e/api/backups"},
	}
	for class, list := range forbidden {
		for _, mp := range list {
			method, path, _ := strings.Cut(mp, " ")
			code, body, _ := phoneReq(t, srv, method, path, "Bearer "+tok, "application/json", "")
			if code != 404 && code != 301 {
				t.Errorf("%s: %s → %d %q (atteso 404)", class, mp, code, body)
			}
		}
	}
	if rc.count() != 0 {
		t.Fatalf("rotte vietate arrivate alla webapp: %v", rc.hits)
	}
}

func TestPhoneAllowedRoutes(t *testing.T) {
	rc := &recorder{}
	srv, _, tok := phoneSetup(t, rc)
	allowed := []string{
		"GET /", "GET /index.html", "GET /avatars.html", "GET /game.html", "GET /device.html", "GET /editor.html",
		"GET /assets/app.css", "GET /assets/js/api.js", "HEAD /api/status",
		"GET /api/status", "GET /api/events",
		"POST /api/displays/calamaio-3f2a/settings", "POST /api/displays/b/full-refresh", "POST /api/displays/b/workday", "POST /api/displays/b/theme",
		"GET /api/crashes", "GET /api/crashes/c1",
		"POST /api/sessions/s1/archive", "POST /api/sessions/s1/unarchive", "POST /api/sessions/s1/avatar",
		"GET /api/avatars", "POST /api/avatars", "GET /api/avatars/goccia", "POST /api/avatars/default", "GET /api/avatars/goccia/anchors", "POST /api/projects/avatar",
		"GET /api/game", "POST /api/game", "POST /api/game/wear", "POST /api/game/special", "GET /api/accessories/a1", "POST /api/accessories",
		"GET /api/themes", "POST /api/themes", "POST /api/themes/import", "GET /api/themes/t1", "PUT /api/themes/t1", "DELETE /api/themes/t1",
		"GET /api/themes/t1/history", "POST /api/themes/t1/restore/3", "POST /api/themes/t1/export",
	}
	for _, mp := range allowed {
		method, path, _ := strings.Cut(mp, " ")
		n := rc.count()
		code, body, _ := phoneReq(t, srv, method, path, "Bearer "+tok, "application/json", "")
		if code != 200 || rc.count() != n+1 {
			t.Fatalf("%s → %d %q: doveva arrivare alla webapp", mp, code, body)
		}
		if !rc.phone || rc.auth != "" {
			t.Fatalf("%s: segno=%v Authorization=%q (va tolta)", mp, rc.phone, rc.auth)
		}
	}
}

// Ogni pagina della webapp è o nell'elenco o tra quelle nascoste: una pagina nuova obbliga a decidere.
func TestPhoneAllowlistCoversPages(t *testing.T) {
	rc := &recorder{}
	srv, _, tok := phoneSetup(t, rc)
	pages, _ := fs.Glob(webui.Static(), "*.html")
	if len(pages) < 6 {
		t.Fatalf("pagine: %v", pages)
	}
	hidden := map[string]bool{}
	for _, p := range PhoneHiddenPages {
		hidden[p] = true
	}
	for _, p := range pages {
		n := rc.count()
		code, _, _ := phoneReq(t, srv, http.MethodGet, "/"+p, "Bearer "+tok, "", "")
		reached := rc.count() == n+1
		switch {
		case hidden[p] && (code != 404 || reached):
			t.Errorf("%s è nascosta ma risponde %d", p, code)
		case !hidden[p] && !reached:
			t.Errorf("%s non è né nell'elenco (phoneRoutes) né in PhoneHiddenPages", p)
		}
	}
}

func TestPhoneWhoami(t *testing.T) {
	srv, _, tok := phoneSetup(t, &recorder{})
	code, body, _ := phoneReq(t, srv, http.MethodGet, "/app/whoami", "Bearer "+tok, "", "")
	var m map[string]map[string]string
	if code != 200 || json.Unmarshal([]byte(body), &m) != nil || m["phone"]["name"] != "Prova" {
		t.Fatalf("%d %s", code, body)
	}
	if code, _, _ := phoneReq(t, srv, http.MethodPost, "/app/whoami", "Bearer "+tok, "application/json", "{}"); code != 404 {
		t.Fatalf("POST whoami: %d", code)
	}
}

func TestPhonePair(t *testing.T) {
	rc := &recorder{}
	srv, m, _ := phoneSetup(t, rc)
	pair := func(body string, hdr ...string) (int, map[string]any) {
		code, b, _ := phoneReq(t, srv, http.MethodPost, "/app/pair", "", "application/json", body, hdr...)
		var out map[string]any
		_ = json.Unmarshal([]byte(b), &out)
		return code, out
	}
	if code, _, _ := phoneReq(t, srv, http.MethodGet, "/app/pair", "", "", ""); code != 405 {
		t.Fatalf("GET: %d", code)
	}
	if code, _, _ := phoneReq(t, srv, http.MethodPost, "/app/pair", "", "text/plain", `{"code":"x"}`); code != 415 {
		t.Fatalf("text/plain: %d", code)
	}
	if code, _ := pair(`{"code":"` + strings.Repeat("a", 5000) + `"}`); code != 400 {
		t.Fatalf("corpo enorme: %d", code)
	}
	if code, _ := pair(`{"code":"x","name":"y","extra":1}`); code != 400 {
		t.Fatalf("campo in più: %d", code)
	}
	if code, out := pair(`{"code":"` + strings.Repeat("a", 32) + `","name":"x"}`); code != 409 {
		t.Fatalf("senza finestra: %d %v", code, out)
	}
	code0, _ := m.OpenWindow()
	for i := 1; i <= phones.MaxAttempts; i++ {
		code, out := pair(`{"code":"` + strings.Repeat("b", 32) + `","name":"x"}`)
		if code != 403 || out["left"] != float64(phones.MaxAttempts-i) {
			t.Fatalf("tentativo %d: %d %v", i, code, out)
		}
	}
	if code, _ := pair(`{"code":"` + code0 + `","name":"x"}`); code != 409 {
		t.Fatalf("dopo 5 errori il codice giusto non vale: %d", code)
	}
	code1, _ := m.OpenWindow()
	code, out := pair(`{"code":"` + code1 + `","name":"iPhone di Prova"}`)
	tok, _ := out["token"].(string)
	if code != 200 || !phones.ValidToken(tok) || out["name"] != "iPhone di Prova" || out["id"] == "" {
		t.Fatalf("abbinamento: %d %v", code, out)
	}
	if c, _, _ := phoneReq(t, srv, http.MethodGet, "/app/whoami", "Bearer "+tok, "", ""); c != 200 {
		t.Fatalf("token nuovo: %d", c)
	}
	if code, _ := pair(`{"code":"` + code1 + `","name":"x"}`); code != 409 {
		t.Fatalf("codice riusato: %d", code)
	}
	if rc.count() != 0 {
		t.Fatal("l'abbinamento non passa dalla webapp")
	}
}

// Nessuna risposta della porta dei telefoni ripete il token (corpo o intestazioni).
func TestPhoneTokenNeverEchoed(t *testing.T) {
	srv, m, tok := phoneSetup(t, &recorder{})
	code1, _ := m.OpenWindow()
	reqs := [][3]string{
		{"GET", "/app/whoami", "Bearer " + tok}, {"GET", "/api/backups", "Bearer " + tok}, {"GET", "/hook", "Bearer " + tok},
		{"GET", "/", "Bearer " + strings.ToUpper(tok)}, {"POST", "/app/pair", "Bearer " + tok},
	}
	for _, r := range reqs {
		body := ""
		if r[1] == "/app/pair" {
			body = `{"code":"` + code1 + `","name":"x"}`
		}
		_, b, hd := phoneReq(t, srv, r[0], r[1], r[2], "application/json", body)
		all := b
		for k, v := range hd {
			all += k + strings.Join(v, ",")
		}
		if strings.Contains(strings.ToLower(all), tok) {
			t.Fatalf("%s %s: il token è nella risposta", r[0], r[1])
		}
	}
}

// Una richiesta lunga (SSE) di un telefono si chiude appena il telefono è revocato.
func TestPhoneRevokeClosesOpenRequest(t *testing.T) {
	started := make(chan struct{})
	next := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(200)
		w.(http.Flusher).Flush()
		close(started)
		<-r.Context().Done()
	})
	srv, m, tok := phoneSetup(t, next)
	done := make(chan struct{})
	go func() {
		req, _ := http.NewRequest(http.MethodGet, srv.URL+"/api/events", nil)
		req.Header.Set("Authorization", "Bearer "+tok)
		if resp, err := http.DefaultClient.Do(req); err == nil {
			io.Copy(io.Discard, resp.Body)
			resp.Body.Close()
		}
		close(done)
	}()
	<-started
	p, _ := m.Lookup(tok)
	if err := m.Revoke(p.ID); err != nil {
		t.Fatal(err)
	}
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("lo stream resta aperto dopo la revoca")
	}
}

// Con la mux vera: il token vale come stessa origine su 8767 (Host del PC, Origin dello schema dell'app),
// mentre su 8765 lo stesso token non dà niente.
func TestPhoneGuardsAndPort8765Unchanged(t *testing.T) {
	s := New(nil, log.New(io.Discard, "", 0))
	srv, _, tok := phoneSetup(t, s.Handler())
	local := httptest.NewServer(s.Handler())
	defer local.Close()
	bear := "Bearer " + tok
	host := []string{"Host", "192.168.1.20:8767"}
	origin := []string{"Host", "192.168.1.20:8767", "Origin", "calamaio-app://ui"}

	// 8767: niente 403/415 delle guardie (le risposte vengono dai gestori: 409 archiviazione, 404 temi spenti)
	if code, body, _ := phoneReq(t, srv, http.MethodGet, "/api/status", bear, "", "", host...); code != 200 {
		t.Fatalf("status da 8767: %d %s", code, body)
	}
	if code, body, _ := phoneReq(t, srv, http.MethodPost, "/api/sessions/s1/archive", bear, "application/json", "{}", origin...); code != 409 || !strings.Contains(body, "non disponibile") {
		t.Fatalf("POST da 8767: %d %s", code, body)
	}
	if code, body, _ := phoneReq(t, srv, http.MethodPost, "/api/sessions/s1/archive", bear, "", "", host...); code != 409 {
		t.Fatalf("POST senza corpo da 8767: %d %s", code, body)
	}
	if code, body, _ := phoneReq(t, srv, http.MethodDelete, "/api/themes/t1", bear, "", "", host...); code != 404 || !strings.Contains(body, "non disponibili") {
		t.Fatalf("DELETE da 8767: %d %s", code, body)
	}
	// il Content-Type resta obbligatorio anche dal telefono
	if code, _, _ := phoneReq(t, srv, http.MethodPost, "/api/sessions/s1/archive", bear, "text/plain", "x", host...); code != 415 {
		t.Fatalf("text/plain da 8767: %d", code)
	}

	// 8765: lo stesso token non cambia niente
	if code, _, _ := phoneReq(t, local, http.MethodGet, "/api/status", bear, "", "", host...); code != 403 {
		t.Fatalf("8765 con Host esterno e token: %d", code)
	}
	if code, _, _ := phoneReq(t, local, http.MethodPost, "/api/sessions/s1/archive", bear, "application/json", "{}", "Origin", "http://evil.example"); code != 403 {
		t.Fatalf("8765 con origine esterna e token: %d", code)
	}
	if code, _, _ := phoneReq(t, local, http.MethodDelete, "/api/themes/t1", bear, "", ""); code != 403 {
		t.Fatalf("8765 DELETE senza origine, con token: %d", code)
	}
	if code, _, _ := phoneReq(t, local, http.MethodGet, "/app/whoami", bear, "", ""); code == 200 {
		t.Fatal("/app/whoami esiste anche su 8765")
	}
}
```

- [ ] **Step 2: Eseguire e vedere fallire**

Run: `cd service && go test ./internal/api/ -run TestPhone 2>&1 | tail -n 20`
Expected: FAIL (`undefined: NewPhoneHandler`, `undefined: phoneOf`).

- [ ] **Step 3: Scrivere `phone.go`**

`service/internal/api/phone.go`:

```go
package api

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"mime"
	"net/http"
	"strings"

	"calamaio/internal/phones"
)

// Porta dei telefoni (8767, spec app iPhone §3.1): prima il token, poi l'elenco chiuso, poi la stessa mux
// della webapp con un segno nel contesto. Il segno lo mette solo NewPhoneHandler: su 8765 non c'è mai.

type phoneKey struct{}

func phoneOf(r *http.Request) (phones.Phone, bool) {
	p, ok := r.Context().Value(phoneKey{}).(phones.Phone)
	return p, ok
}

func fromPhone(r *http.Request) bool {
	_, ok := phoneOf(r)
	return ok
}

// PhoneAuth: token e abbinamento dei telefoni (phones.Manager).
type PhoneAuth interface {
	Lookup(token string) (phones.Phone, bool)
	Touch(id string)
	Redeem(code, name, oldToken string) (string, phones.Phone, error)
	Track(ctx context.Context, id string) (context.Context, func())
}

type PhoneOptions struct {
	Auth   PhoneAuth
	Whoami func(phones.Phone) any // corpo di GET /app/whoami
	Next   http.Handler           // la mux della webapp (Server.Handler)
	Log    *log.Logger
}

// phoneRoutes: l'elenco chiuso. Una rotta nuova della webapp NON si raggiunge dal telefono finché non si
// aggiunge qui (e in TestPhoneAllowedRoutes). Le rotte dei Giochi (piano C) vanno aggiunte da quel piano.
var phoneRoutes = []string{
	"GET /app/whoami",
	"GET /api/status",
	"GET /api/events",
	"POST /api/displays/{board}/settings",
	"POST /api/displays/{board}/full-refresh",
	"POST /api/displays/{board}/workday",
	"POST /api/displays/{board}/theme",
	"GET /api/crashes",
	"GET /api/crashes/{id}",
	"POST /api/sessions/{id}/archive",
	"POST /api/sessions/{id}/unarchive",
	"POST /api/sessions/{id}/avatar",
	"GET /api/avatars",
	"POST /api/avatars",
	"GET /api/avatars/{id}",
	"POST /api/avatars/default",
	"GET /api/avatars/{id}/anchors",
	"POST /api/projects/avatar",
	"GET /api/game",
	"POST /api/game",
	"POST /api/game/wear",
	"POST /api/game/special",
	"GET /api/accessories/{id}",
	"POST /api/accessories",
	"GET /api/themes",
	"POST /api/themes",
	"POST /api/themes/import",
	"GET /api/themes/{id}",
	"PUT /api/themes/{id}",
	"DELETE /api/themes/{id}",
	"GET /api/themes/{id}/history",
	"POST /api/themes/{id}/restore/{n}",
	"POST /api/themes/{id}/export",
	// pagine e file della webapp; Firmware no (Web Serial non c'è su iOS)
	"GET /{$}",
	"GET /index.html",
	"GET /avatars.html",
	"GET /game.html",
	"GET /device.html",
	"GET /editor.html",
	"GET /assets/",
}

// PhoneHiddenPages: pagine della webapp che dal telefono rispondono 404.
var PhoneHiddenPages = []string{"firmware.html"}

func NewPhoneHandler(o PhoneOptions) http.Handler {
	allow := http.NewServeMux()
	ok := http.HandlerFunc(func(http.ResponseWriter, *http.Request) {})
	for _, p := range phoneRoutes {
		allow.Handle(p, ok)
	}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		securityHeaders(w.Header())
		if r.URL.Path == "/app/pair" {
			phonePair(o, w, r)
			return
		}
		p, found := phones.Phone{}, false
		if tok, ok := bearer(r); ok {
			p, found = o.Auth.Lookup(tok)
		}
		if !found {
			w.Header().Set("WWW-Authenticate", "Bearer")
			writeJSON(w, http.StatusUnauthorized, errBody("non autorizzato"))
			return
		}
		// pattern vuoto = nessuna rotta dell'elenco (anche per metodo sbagliato o percorso da ripulire verso una
		// rotta vietata). Un percorso da ripulire verso una rotta ammessa passa e la mux vera risponde 301.
		if _, pat := allow.Handler(r); pat == "" {
			writeJSON(w, http.StatusNotFound, errBody("non trovato"))
			return
		}
		o.Auth.Touch(p.ID)
		ctx, done := o.Auth.Track(context.WithValue(r.Context(), phoneKey{}, p), p.ID)
		defer done()
		r = r.WithContext(ctx)
		r.Header.Del("Authorization")
		if r.URL.Path == "/app/whoami" {
			writeJSON(w, http.StatusOK, o.Whoami(p))
			return
		}
		o.Next.ServeHTTP(w, r)
	})
}

// bearer: «Bearer <64 cifre esadecimali minuscole>», nient'altro.
func bearer(r *http.Request) (string, bool) {
	t, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
	return t, ok && phones.ValidToken(t)
}

// phonePair: POST /app/pair {code, name}. L'unica richiesta senza token; con «Authorization: Bearer <token di
// prima>» il telefono abbinato prima (stesso telefono, QR nuovo) si revoca.
func phonePair(o PhoneOptions, w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeJSON(w, http.StatusMethodNotAllowed, errBody("solo POST"))
		return
	}
	if mt, _, _ := mime.ParseMediaType(r.Header.Get("Content-Type")); mt != "application/json" {
		writeJSON(w, http.StatusUnsupportedMediaType, errBody("serve Content-Type: application/json"))
		return
	}
	var body struct {
		Code string `json:"code"`
		Name string `json:"name"`
	}
	dec := json.NewDecoder(io.LimitReader(r.Body, 4<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&body); err != nil {
		writeJSON(w, http.StatusBadRequest, errBody("JSON non valido"))
		return
	}
	old, _ := bearer(r)
	tok, p, err := o.Auth.Redeem(body.Code, body.Name, old)
	var ce *phones.CodeError
	switch {
	case err == nil:
		o.Log.Printf("telefoni: abbinato «%s» (%s)", p.Name, p.ID)
		writeJSON(w, http.StatusOK, map[string]any{"token": tok, "id": p.ID, "name": p.Name})
	case errors.As(err, &ce):
		o.Log.Printf("telefoni: codice di abbinamento sbagliato (%d tentativi rimasti)", ce.Left)
		writeJSON(w, http.StatusForbidden, map[string]any{"error": "codice sbagliato", "left": ce.Left})
	case errors.Is(err, phones.ErrNoWindow):
		writeJSON(w, http.StatusConflict, errBody("nessun abbinamento aperto: aprilo dalla webapp sul PC"))
	default:
		o.Log.Printf("telefoni: abbinamento non salvato: %v", err)
		writeJSON(w, http.StatusInternalServerError, errBody("abbinamento non salvato sul PC"))
	}
}
```

- [ ] **Step 4: Il segno del telefono nelle guardie**

In `service/internal/api/web.go` sostituire `guard` (righe 63-73) con:

```go
// guard: Host locale e header di sicurezza su ogni risposta della webapp e di /api. Dalla porta dei telefoni
// l'Host è l'indirizzo del PC: lì protegge il token, e il segno nel contesto c'è solo dopo il token.
func guard(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		securityHeaders(w.Header())
		if !loopbackHost(r) && !fromPhone(r) {
			http.Error(w, "solo da 127.0.0.1", http.StatusForbidden)
			return
		}
		next(w, r)
	}
}
```

e all'inizio di `sameOrigin` (riga 77) aggiungere il caso del telefono:

```go
func sameOrigin(r *http.Request) (browser bool, err error) {
	if fromPhone(r) {
		return true, nil // richiesta col token di un telefono abbinato (porta 8767): vale come stessa origine
	}
	if sfs := r.Header.Get("Sec-Fetch-Site"); sfs != "" {
```

(il resto della funzione invariato). Nessun'altra guardia cambia: `localOnly`, `hookCaller` e `fromLoopback` restano come sono (le loro rotte non sono nell'elenco).

- [ ] **Step 5: Eseguire e vedere passare**

Run: `cd service && go test ./internal/api/ -run TestPhone -v 2>&1 | tail -n 30`
Expected: PASS (8 test). Se `TestPhoneForbiddenRoutes` mostra un 301 per `GET /api/status/../backups` o `/%2e%2e/…`, va bene solo se `rc.count()` resta 0 (la mux dell'elenco rimanda a un percorso pulito che poi è vietato): il test lo ammette.

Poi tutto il servizio: `cd service && go vet ./... && go test ./... 2>&1 | tail -n 8`
Expected: tutto `ok` (le guardie di 8765 non sono cambiate: `TestWebSecurity` in `internal/server` passa).

- [ ] **Step 6: Commit**

```bash
git add service/internal/api/phone.go service/internal/api/phone_test.go service/internal/api/web.go
git commit -m "Telefoni: gestore della porta 8767 con token, elenco chiuso (404 per tutto il resto), /app/pair monouso e /app/whoami; il token vale come stessa origine solo da lì"
```

---

### Task 4: Porta 8767 accesa a richiesta e rotte `/api/phones` sulla 8765

**Files:**
- Create: `service/internal/server/app.go`, `service/internal/api/phones.go`, `service/internal/config/config_test.go`
- Modify: `service/internal/config/config.go:14-17,28-30,81-83,165-190` (porta `app_port`), `service/internal/api/server.go:64-101,173` (campo `Phones`, rotte), `service/internal/server/server.go` (opzioni, `Running`, avvio e spegnimento), `service/internal/server/server_test.go:58-67` (ascoltatore dei telefoni solo su loopback nei test)
- Test: `service/internal/server/app_test.go`, `service/internal/config/config_test.go`

**Interfaces:**
- Consumes: `phones.*` (Task 1-2), `api.NewPhoneHandler`, `api.PhoneOptions` (Task 3).
- Produces:
  - `config.DefaultAppPort = 8767`, `Config.AppPort int` (JSON `app_port`), variabile `CALAMAIO_APP_PORT`;
  - in `api`: `type PhonesControl interface { PhonesView() PhonesView; OpenPairing() (PhonesView, error); CancelPairing() PhonesView; RevokePhone(id string) error }`; `type PhonesView struct { Phones []PhoneView; Pairing *PairingView; App AppView }` (JSON `phones, pairing, app`); `PhoneView{ID, Name string; Paired, LastUsed int64}` (`id, name, paired, lastUsed`); `PairingView{Link string; Expires int64}` (`link, expires`); `AppView{Port int; Listening bool; Fingerprint string; Hosts []string; Error string}` (`port, listening, fingerprint, hosts, error` omesso se vuoto); campo `Server.Phones PhonesControl`;
  - rotte su 8765: `GET /api/phones` → 200 vista · `POST /api/phones/pair` (`postGuard`) → 200 vista · 409 `{error}` · `POST /api/phones/pair/cancel` → 200 vista · `DELETE /api/phones/{id}` (`deleteGuard`) → 204 · 404 · 500 `{error}`; senza `Phones` → 404;
  - in `server`: `Options.PhonesFile string`, `Options.AppTLSDir string`, `Options.AppListen func() (net.Listener, error)`, `Options.AppHosts func() []string`, `Options.TuneApp func(*phones.Manager)`; `Running.AppAddr func() string` (`""` = spento), `Running.Phones *phones.Manager`;
  - `type appService` con `sync()`, `close()`, `addr() string`, `port() int`, `whoami(phones.Phone) any`, e campo `announce func(port int, fp string) (stop func())` (riempito nel Task 5).

- [ ] **Step 1: Test della configurazione (fallisce)**

`service/internal/config/config_test.go`:

```go
package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestAppPortDefaultFileAndEnv(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("CALAMAIO_CONFIG_DIR", dir)
	t.Setenv("CALAMAIO_APP_PORT", "")
	c, err := Load()
	if err != nil || c.AppPort != DefaultAppPort || DefaultAppPort != 8767 {
		t.Fatalf("predefinita: %v %d", err, c.AppPort)
	}
	os.WriteFile(filepath.Join(dir, "config.json"), []byte(`{"api_port":8765,"app_port":9767}`), 0o600)
	if c, _ = Load(); c.AppPort != 9767 || c.APIPort != 8765 {
		t.Fatalf("dal file: %+v", c)
	}
	os.WriteFile(filepath.Join(dir, "config.json"), []byte(`{"api_port":8765}`), 0o600)
	if c, _ = Load(); c.AppPort != DefaultAppPort {
		t.Fatalf("config vecchia senza app_port: %d", c.AppPort)
	}
	t.Setenv("CALAMAIO_APP_PORT", "18767")
	if c, _ = Load(); c.AppPort != 18767 {
		t.Fatalf("dalla variabile: %d", c.AppPort)
	}
}
```

Run: `cd service && go test ./internal/config/ -run TestAppPort 2>&1 | tail -n 5`
Expected: FAIL (`undefined: DefaultAppPort`).

- [ ] **Step 2: `app_port` nella configurazione**

In `service/internal/config/config.go`:

```go
const (
	DefaultAPIPort = 8765
	DefaultWSPort  = 8766
	DefaultAppPort = 8767 // HTTPS per i telefoni abbinati (app iPhone)
)
```

nel tipo `Config`, subito dopo `WSPort`:

```go
	// AppPort: porta HTTPS dei telefoni abbinati, su tutte le interfacce, accesa solo quando serve.
	AppPort int `json:"app_port"`
```

`Default()`:

```go
func Default() Config {
	return Config{APIPort: DefaultAPIPort, WSPort: DefaultWSPort, AppPort: DefaultAppPort}
}
```

in `Load`, dopo il blocco `if c.WSPort == 0 {…}`:

```go
	if c.AppPort == 0 {
		c.AppPort = DefaultAppPort
	}
```

e in `withEnv`, dopo `CALAMAIO_WS_PORT`, con il commento aggiornato a «CALAMAIO_API_PORT, CALAMAIO_WS_PORT e CALAMAIO_APP_PORT sostituiscono le porte (test, più istanze).»:

```go
	if v := os.Getenv("CALAMAIO_APP_PORT"); v != "" {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			c.AppPort = n
		}
	}
```

Run: `cd service && go test ./internal/config/ 2>&1 | tail -n 5`
Expected: PASS.

- [ ] **Step 3: Scrivere i test d'integrazione (falliscono)**

In `service/internal/server/server_test.go`, in `startOpts`, dentro il letterale `o := Options{…}` subito dopo `Keys: s.keys,` aggiungere (nei test la porta dei telefoni non apre mai tutte le interfacce):

```go
		AppListen: func() (net.Listener, error) { return net.Listen("tcp", "127.0.0.1:0") },
		AppHosts:  func() []string { return []string{"127.0.0.1"} },
```

`service/internal/server/app_test.go`:

```go
package server

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/phones"
)

// phone: un client HTTPS come l'app: nessuna autorità di certificazione, solo l'impronta del QR.
type phone struct {
	t     *testing.T
	base  string
	c     *http.Client
	token string
}

func pinned(fp string) *http.Transport {
	return &http.Transport{TLSClientConfig: &tls.Config{
		InsecureSkipVerify: true, // la verifica è l'impronta qui sotto, come nell'app
		VerifyPeerCertificate: func(raw [][]byte, _ [][]*x509.Certificate) error {
			if len(raw) == 0 || phones.Fingerprint(raw[0]) != fp {
				return errors.New("impronta diversa")
			}
			return nil
		},
	}}
}

func newPhone(t *testing.T, base, fp string) *phone {
	return &phone{t: t, base: base, c: &http.Client{Timeout: 5 * time.Second, Transport: pinned(fp)}}
}

func (p *phone) do(method, path, ctype, body string) (int, string) {
	p.t.Helper()
	var rd io.Reader
	if body != "" {
		rd = strings.NewReader(body)
	}
	req, _ := http.NewRequest(method, p.base+path, rd)
	if ctype != "" {
		req.Header.Set("Content-Type", ctype)
	}
	if p.token != "" {
		req.Header.Set("Authorization", "Bearer "+p.token)
	}
	resp, err := p.c.Do(req)
	if err != nil {
		p.t.Fatalf("%s %s: %v", method, path, err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, string(b)
}

// pair: POST /app/pair; con p.token impostato manda anche il token di prima.
func (p *phone) pair(code, name string) (int, map[string]any) {
	p.t.Helper()
	c, b := p.do(http.MethodPost, "/app/pair", "application/json", `{"code":"`+code+`","name":"`+name+`"}`)
	var m map[string]any
	_ = json.Unmarshal([]byte(b), &m)
	return c, m
}

// openPairing apre la finestra dalla webapp e legge il link del QR: base HTTPS, impronta, codice.
func openPairing(t *testing.T, w *web) (base, fp, code string) {
	t.Helper()
	c, m := w.post("/api/phones/pair", `{}`)
	if c != 200 {
		t.Fatalf("pair: %d %v", c, m)
	}
	pr, _ := m["pairing"].(map[string]any)
	link, _ := pr["link"].(string)
	u, err := url.Parse(link)
	if err != nil || u.Scheme != "calamaio" || u.Host != "pair" {
		t.Fatalf("link %q", link)
	}
	q := u.Query()
	if q.Get("v") != "1" || q.Get("h") != "127.0.0.1" || len(q.Get("fp")) != 64 || len(q.Get("c")) != 32 || q.Get("n") == "" {
		t.Fatalf("link %q", link)
	}
	return "https://127.0.0.1:" + q.Get("p"), q.Get("fp"), q.Get("c")
}

func pairedPhone(t *testing.T, w *web) (*phone, string) {
	t.Helper()
	base, fp, code := openPairing(t, w)
	p := newPhone(t, base, fp)
	c, m := p.pair(code, "iPhone di prova")
	if c != 200 {
		t.Fatalf("abbinamento: %d %v", c, m)
	}
	p.token = m["token"].(string)
	return p, m["id"].(string)
}

func phonesView(t *testing.T, w *web) map[string]any {
	t.Helper()
	c, m, _ := w.do(http.MethodGet, "/api/phones", "", "")
	if c != 200 {
		t.Fatalf("GET /api/phones: %d %v", c, m)
	}
	return m
}

func eventually(t *testing.T, what string, ok func() bool) {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for !ok() {
		if time.Now().After(deadline) {
			t.Fatalf("non succede: %s", what)
		}
		time.Sleep(20 * time.Millisecond)
	}
}

func revoke(t *testing.T, w *web, id string) int {
	t.Helper()
	c, _, _ := w.do(http.MethodDelete, "/api/phones/"+id, "", "", "Origin", "http://"+w.host, "Sec-Fetch-Site", "same-origin")
	return c
}

func TestAppListenerOnlyWhenNeeded(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	if s.run.AppAddr() != "" {
		t.Fatal("porta dei telefoni accesa senza telefoni")
	}
	v := phonesView(t, w)
	if v["pairing"] != nil || v["app"].(map[string]any)["listening"] != false || len(v["phones"].([]any)) != 0 {
		t.Fatalf("vista iniziale: %v", v)
	}
	openPairing(t, w)
	if s.run.AppAddr() == "" {
		t.Fatal("finestra aperta ma porta spenta")
	}
	if c, _ := w.post("/api/phones/pair/cancel", `{}`); c != 200 {
		t.Fatalf("cancel: %d", c)
	}
	eventually(t, "porta spenta dopo l'annullamento", func() bool { return s.run.AppAddr() == "" })
}

func TestAppPairAndAllowlistOverTLS(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	base, fp, code := openPairing(t, w)
	// il client con l'impronta sbagliata non arriva nemmeno all'HTTP
	if _, err := newPhone(t, base, strings.Repeat("0", 64)).c.Get(base + "/app/whoami"); err == nil {
		t.Fatal("TLS accettato con l'impronta sbagliata")
	}
	p := newPhone(t, base, fp)
	c, m := p.pair(code, "iPhone di prova")
	if c != 200 {
		t.Fatalf("pair: %d %v", c, m)
	}
	tok := m["token"].(string)
	if c, _ := p.pair(code, "doppione"); c != 409 {
		t.Fatalf("codice riusato: %d", c)
	}
	if c, _ := p.do(http.MethodGet, "/api/status", "", ""); c != 401 {
		t.Fatalf("senza token: %d", c)
	}
	p.token = tok
	bodies := ""
	for _, tc := range []struct {
		method, path string
		want         int
	}{
		{"GET", "/api/status", 200}, {"GET", "/", 200}, {"GET", "/device.html", 200}, {"GET", "/assets/app.css", 200},
		{"GET", "/app/whoami", 200}, {"GET", "/api/backups", 404}, {"POST", "/hook", 404}, {"GET", "/debug", 404},
		{"GET", "/api/phones", 404}, {"GET", "/firmware.html", 404}, {"GET", "/api/firmware", 404},
	} {
		c, b := p.do(tc.method, tc.path, "application/json", "")
		if c != tc.want {
			t.Errorf("%s %s: %d (atteso %d) %.80s", tc.method, tc.path, c, tc.want, b)
		}
		bodies += b
	}
	if strings.Contains(bodies, tok) {
		t.Fatal("il token compare in una risposta")
	}
	_, who := p.do(http.MethodGet, "/app/whoami", "", "")
	var me struct {
		Phone struct{ Name string } `json:"phone"`
		PC    struct{ Version string } `json:"pc"`
		Hosts []string `json:"hosts"`
		Port  int      `json:"port"`
	}
	if json.Unmarshal([]byte(who), &me) != nil || me.Phone.Name != "iPhone di prova" || me.PC.Version != "test" ||
		len(me.Hosts) != 1 || me.Hosts[0] != "127.0.0.1" || !strings.HasSuffix(base, ":"+itoa(me.Port)) {
		t.Fatalf("whoami: %s", who)
	}
	v := phonesView(t, w)
	list := v["phones"].([]any)
	if len(list) != 1 || list[0].(map[string]any)["name"] != "iPhone di prova" || v["pairing"] != nil {
		t.Fatalf("vista: %v", v)
	}
	if strings.Contains(w.raw("/api/phones"), tok) {
		t.Fatal("il token compare in /api/phones")
	}
}

func TestAppPairFiveWrongCodes(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	base, fp, code := openPairing(t, w)
	p := newPhone(t, base, fp)
	for i := 1; i <= 5; i++ {
		c, m := p.pair(strings.Repeat("9", 32), "x")
		if c != 403 || m["left"] != float64(5-i) {
			t.Fatalf("tentativo %d: %d %v", i, c, m)
		}
	}
	eventually(t, "porta spenta dopo 5 errori senza telefoni", func() bool { return s.run.AppAddr() == "" })
	if v := phonesView(t, w); v["pairing"] != nil {
		t.Fatalf("finestra ancora aperta: %v", v)
	}
	_ = code
}

func TestAppSSEClosedOnRevoke(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	p, id := pairedPhone(t, w)
	req, _ := http.NewRequest(http.MethodGet, p.base+"/api/events", nil)
	req.Header.Set("Authorization", "Bearer "+p.token)
	stream := &http.Client{Transport: p.c.Transport} // senza Timeout: è uno stream
	resp, err := stream.Do(req)
	if err != nil || resp.StatusCode != 200 {
		t.Fatalf("SSE: %v %v", err, resp)
	}
	lines := make(chan string, 64)
	go func() {
		defer close(lines)
		sc := bufio.NewScanner(resp.Body)
		sc.Buffer(make([]byte, 1<<20), 1<<20)
		for sc.Scan() {
			lines <- sc.Text()
		}
	}()
	deadline := time.After(3 * time.Second)
	for got := false; !got; {
		select {
		case l := <-lines:
			got = l == "event: status"
		case <-deadline:
			t.Fatal("nessun evento status attraverso la porta dei telefoni")
		}
	}
	if c := revoke(t, w, id); c != 204 {
		t.Fatalf("revoca: %d", c)
	}
	closed := time.After(2 * time.Second)
	for {
		select {
		case _, ok := <-lines:
			if !ok {
				eventually(t, "porta spenta dopo l'ultima revoca", func() bool { return s.run.AppAddr() == "" })
				if revoke(t, w, id) != 404 {
					t.Fatal("seconda revoca")
				}
				return
			}
		case <-closed:
			t.Fatal("lo stream resta aperto dopo la revoca")
		}
	}
}

func TestAppPairReplacesOldToken(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	p, _ := pairedPhone(t, w)
	old := p.token
	base, fp, code := openPairing(t, w)
	p2 := newPhone(t, base, fp)
	p2.token = old // l'app manda il token di prima insieme al codice nuovo
	c, m := p2.pair(code, "iPhone di prova")
	if c != 200 {
		t.Fatalf("riabbinamento: %d %v", c, m)
	}
	if n := len(phonesView(t, w)["phones"].([]any)); n != 1 {
		t.Fatalf("doppioni sul PC: %d", n)
	}
	p2.token = m["token"].(string)
	if c, _ := p2.do(http.MethodGet, "/api/status", "", ""); c != 200 {
		t.Fatalf("token nuovo: %d", c)
	}
	p2.token = old
	if c, _ := p2.do(http.MethodGet, "/api/status", "", ""); c != 401 {
		t.Fatalf("token vecchio: %d", c)
	}
}

func TestAppFilesAndRestart(t *testing.T) {
	dir := t.TempDir()
	tune := func(o *Options) {
		o.PhonesFile = filepath.Join(dir, "phones.json")
		o.AppTLSDir = filepath.Join(dir, "app-tls")
	}
	s := startOpts(t, true, 0, tune)
	w := newWeb(t, s)
	p, _ := pairedPhone(t, w)
	fp := phonesView(t, w)["app"].(map[string]any)["fingerprint"].(string)
	for name, want := range map[string]os.FileMode{"phones.json": 0o600, "app-tls": 0o700, "app-tls/key.pem": 0o600, "app-tls/cert.pem": 0o600} {
		st, err := os.Stat(filepath.Join(dir, name))
		if err != nil || st.Mode().Perm() != want {
			t.Fatalf("%s: %v %v", name, err, st.Mode().Perm())
		}
	}
	if b, _ := os.ReadFile(filepath.Join(dir, "phones.json")); strings.Contains(string(b), p.token) {
		t.Fatal("il token è in phones.json")
	}
	// spegnimento e riavvio con le stesse cartelle
	s.cancel()
	err := <-s.done
	s.done <- err // la pulizia di startOpts lo rilegge
	s2 := startOpts(t, true, 0, tune)
	w2 := newWeb(t, s2)
	if s2.run.AppAddr() == "" {
		t.Fatal("con un telefono abbinato la porta deve accendersi all'avvio")
	}
	if fp2 := phonesView(t, w2)["app"].(map[string]any)["fingerprint"]; fp2 != fp {
		t.Fatalf("impronta cambiata al riavvio: %v → %v", fp, fp2)
	}
	p2 := newPhone(t, "https://"+s2.run.AppAddr(), fp)
	p2.token = p.token
	if c, _ := p2.do(http.MethodGet, "/app/whoami", "", ""); c != 200 {
		t.Fatalf("token dopo il riavvio: %d", c)
	}
}

func TestAppBrokenCertNotRegenerated(t *testing.T) {
	dir := t.TempDir()
	tlsDir := filepath.Join(dir, "app-tls")
	os.MkdirAll(tlsDir, 0o700)
	os.WriteFile(filepath.Join(tlsDir, "cert.pem"), []byte("rotto"), 0o600)
	os.WriteFile(filepath.Join(tlsDir, "key.pem"), []byte("rotta"), 0o600)
	s := startOpts(t, true, 0, func(o *Options) { o.AppTLSDir = tlsDir })
	w := newWeb(t, s)
	app := phonesView(t, w)["app"].(map[string]any)
	if e, _ := app["error"].(string); !strings.Contains(e, "illeggibile") || app["listening"] != false {
		t.Fatalf("errore del certificato non mostrato: %v", app)
	}
	if c, m := w.post("/api/phones/pair", `{}`); c != 409 || !strings.Contains(m["error"].(string), "illeggibile") {
		t.Fatalf("abbinamento con certificato rotto: %d %v", c, m)
	}
	if b, _ := os.ReadFile(filepath.Join(tlsDir, "key.pem")); string(b) != "rotta" {
		t.Fatal("chiave riscritta")
	}
	if s.run.AppAddr() != "" {
		t.Fatal("porta accesa senza certificato")
	}
}

func TestAppPortBusy(t *testing.T) {
	s := startOpts(t, true, 0, func(o *Options) {
		o.AppListen = func() (net.Listener, error) { return nil, errors.New("porta 8767 occupata (prova)") }
	})
	w := newWeb(t, s)
	if c, m := w.post("/api/phones/pair", `{}`); c != 409 || !strings.Contains(m["error"].(string), "occupata") {
		t.Fatalf("porta occupata: %d %v", c, m)
	}
	v := phonesView(t, w)
	if v["pairing"] != nil || !strings.Contains(v["app"].(map[string]any)["error"].(string), "occupata") {
		t.Fatalf("vista: %v", v)
	}
}

func TestAppPairingWindowExpires(t *testing.T) {
	s := startOpts(t, true, 0, func(o *Options) {
		o.TuneApp = func(m *phones.Manager) { m.Pairing.Window = 300 * time.Millisecond }
	})
	w := newWeb(t, s)
	openPairing(t, w)
	eventually(t, "porta spenta alla scadenza della finestra", func() bool { return s.run.AppAddr() == "" })
	if v := phonesView(t, w); v["pairing"] != nil {
		t.Fatalf("finestra scaduta ancora nella vista: %v", v)
	}
}

func TestPhonesRoutesOnlyLocal(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	if c, _, _ := w.do(http.MethodGet, "/api/phones", "", "", "Host", "evil.example:8765"); c != 403 {
		t.Fatalf("Host esterno: %d", c)
	}
	if c, _, _ := w.do(http.MethodPost, "/api/phones/pair", "application/json", `{}`, "Origin", "http://evil.example"); c != 403 {
		t.Fatalf("origine esterna: %d", c)
	}
	if c, _, _ := w.do(http.MethodDelete, "/api/phones/x", "", ""); c != 403 {
		t.Fatalf("DELETE senza origine: %d", c)
	}
	if c := revoke(t, w, "sconosciuto"); c != 404 {
		t.Fatalf("revoca di un telefono sconosciuto: %d", c)
	}
}
```

In `service/internal/server/webapi_test.go`, dopo `func (w *web) post(…)`, aggiungere l'aiuto `raw`:

```go
// raw: il corpo di una GET come testo.
func (w *web) raw(path string) string {
	w.t.Helper()
	resp, err := http.Get(w.base + path)
	if err != nil {
		w.t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return string(b)
}
```

Run: `cd service && go test ./internal/server/ -run 'TestApp|TestPhonesRoutes' 2>&1 | tail -n 20`
Expected: FAIL (`unknown field AppListen in struct literal`, `s.run.AppAddr undefined`).

- [ ] **Step 4: Rotte `/api/phones` nella API**

`service/internal/api/phones.go`:

```go
package api

import (
	"errors"
	"net/http"

	"calamaio/internal/phones"
)

// Telefoni abbinati e finestra di abbinamento, solo dalla webapp sul PC (porta 8765): dalla porta dei telefoni
// queste rotte non sono nell'elenco (spec §3.1, «abbinamento di altri telefoni»).

// PhonesControl: server.appService.
type PhonesControl interface {
	PhonesView() PhonesView
	OpenPairing() (PhonesView, error)
	CancelPairing() PhonesView
	RevokePhone(id string) error
}

type PhoneView struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Paired   int64  `json:"paired"`
	LastUsed int64  `json:"lastUsed"`
}

// PairingView: la finestra aperta. Link = contenuto del QR (con il codice monouso).
type PairingView struct {
	Link    string `json:"link"`
	Expires int64  `json:"expires"`
}

type AppView struct {
	Port        int      `json:"port"`
	Listening   bool     `json:"listening"`
	Fingerprint string   `json:"fingerprint"`
	Hosts       []string `json:"hosts"`
	Error       string   `json:"error,omitempty"`
}

type PhonesView struct {
	Phones  []PhoneView  `json:"phones"`
	Pairing *PairingView `json:"pairing"`
	App     AppView      `json:"app"`
}

func (s *Server) phonesOff(w http.ResponseWriter, r *http.Request) bool {
	if s.Phones == nil {
		apiNotFound(w, r)
		return true
	}
	return false
}

func (s *Server) apiPhones(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	writeJSON(w, http.StatusOK, s.Phones.PhonesView())
}

func (s *Server) apiPhonesPair(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	v, err := s.Phones.OpenPairing()
	if err != nil {
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
		return
	}
	s.Log.Printf("telefoni: finestra di abbinamento aperta (5 minuti)")
	writeJSON(w, http.StatusOK, v)
}

func (s *Server) apiPhonesCancel(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	writeJSON(w, http.StatusOK, s.Phones.CancelPairing())
}

func (s *Server) apiPhoneRevoke(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	id := r.PathValue("id")
	switch err := s.Phones.RevokePhone(id); {
	case errors.Is(err, phones.ErrNotFound):
		writeJSON(w, http.StatusNotFound, errBody("telefono sconosciuto"))
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody("revocato fino al riavvio del servizio, ma phones.json non è stato scritto: "+err.Error()))
	default:
		s.Log.Printf("telefoni: revocato %s", id)
		w.WriteHeader(http.StatusNoContent)
	}
}
```

In `service/internal/api/server.go`, nel tipo `Server` dopo il campo `ThemeSend ThemeSender`:

```go
	// Phones (facoltativo): telefoni abbinati e abbinamento (server.appService). nil = 404.
	Phones PhonesControl
```

e in `Handler()` subito prima di `mux.HandleFunc("POST /api/", postGuard(apiNotFound))`:

```go
	mux.HandleFunc("GET /api/phones", guard(s.apiPhones))
	mux.HandleFunc("POST /api/phones/pair", postGuard(s.apiPhonesPair))
	mux.HandleFunc("POST /api/phones/pair/cancel", postGuard(s.apiPhonesCancel))
	mux.HandleFunc("DELETE /api/phones/{id}", deleteGuard(s.apiPhoneRevoke))
```

- [ ] **Step 5: Scrivere `app.go`**

`service/internal/server/app.go`:

```go
package server

import (
	"context"
	"crypto/tls"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"sync"
	"time"

	"calamaio/internal/api"
	"calamaio/internal/config"
	"calamaio/internal/mdns"
	"calamaio/internal/phones"
)

// appService: telefoni abbinati, finestra di abbinamento e porta HTTPS dei telefoni (spec app iPhone §3).
// La porta è accesa solo se c'è un telefono o una finestra aperta (phones.Manager.Wanted).
type appService struct {
	mgr      *phones.Manager
	cert     *tls.Certificate
	fp       string
	broken   error // elenco o certificato illeggibili: niente porta, niente abbinamento
	cfgPort  int
	listen   func() (net.Listener, error)
	hosts    func() []string
	name     string
	version  string
	handler  http.Handler
	base     func(net.Listener) context.Context
	lg       *log.Logger
	announce func(port int, fp string) (stop func()) // Bonjour (nil = spento), Task 5

	mu      sync.Mutex
	srv     *http.Server
	ln      net.Listener
	unannounce func()
	lastErr error
}

// openApp prepara i telefoni; la porta si accende con sync().
func openApp(o Options, c config.Config, lg *log.Logger, as *api.Server, base func(net.Listener) context.Context) *appService {
	a := &appService{lg: lg, name: mdns.ShortHost(), version: o.Version, base: base, cfgPort: c.AppPort}
	st, err := phones.Open(o.PhonesFile)
	if err != nil {
		lg.Printf("telefoni: %v: abbinamento spento, il file non si tocca", err)
		a.broken = fmt.Errorf("elenco dei telefoni illeggibile: %w", err)
		st, _ = phones.Open("")
	}
	a.mgr = phones.NewManager(st)
	if o.TuneApp != nil {
		o.TuneApp(a.mgr)
	}
	if a.broken == nil {
		cert, fp, err := phones.LoadOrCreateCert(o.AppTLSDir, a.name, time.Now())
		if err != nil {
			lg.Printf("telefoni: %v (porta spenta; per rifarlo cancella la cartella e riabbina i telefoni)", err)
			a.broken = err
		} else {
			a.cert, a.fp = &cert, fp
		}
	}
	a.listen = o.AppListen
	if a.listen == nil {
		addr := fmt.Sprintf(":%d", c.AppPort)
		a.listen = func() (net.Listener, error) { return net.Listen("tcp", addr) }
	}
	a.hosts = o.AppHosts
	if a.hosts == nil {
		a.hosts = phones.LocalIPv4s
	}
	a.handler = api.NewPhoneHandler(api.PhoneOptions{Auth: a.mgr, Whoami: a.whoami, Next: as.Handler(), Log: lg})
	a.mgr.OnChange = a.sync
	if n := st.Len(); n > 0 {
		lg.Printf("telefoni: %d abbinati", n)
	}
	return a
}

// sync accende o spegne la porta. Spegnere: il listener si chiude subito (la porta torna libera e nessuno si
// collega più), le richieste in corso hanno 1 s per finire (la risposta che ha chiuso la finestra arriva).
func (a *appService) sync() {
	a.mu.Lock()
	defer a.mu.Unlock()
	want := a.broken == nil && a.mgr.Wanted()
	switch {
	case want && a.srv == nil:
		ln, err := a.listen()
		if err != nil {
			a.lastErr = fmt.Errorf("porta dei telefoni: %w", err)
			a.lg.Printf("telefoni: %v", a.lastErr)
			return
		}
		a.lastErr = nil
		tl := tls.NewListener(ln, &tls.Config{Certificates: []tls.Certificate{*a.cert}, MinVersion: tls.VersionTLS13, NextProtos: []string{"http/1.1"}})
		srv := &http.Server{Handler: a.handler, ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 2 * time.Minute,
			BaseContext: a.base, ErrorLog: log.New(io.Discard, "", 0)} // i TLS interrotti (sonde Bonjour) non vanno nel registro
		a.srv, a.ln = srv, ln
		go func() { _ = srv.Serve(tl) }()
		if a.announce != nil {
			a.unannounce = a.announce(ln.Addr().(*net.TCPAddr).Port, a.fp)
		}
		a.lg.Printf("telefoni: HTTPS su %s (impronta %s…)", ln.Addr(), a.fp[:16])
	case !want && a.srv != nil:
		srv := a.srv
		_ = a.ln.Close()
		a.srv, a.ln = nil, nil
		if a.unannounce != nil {
			a.unannounce()
			a.unannounce = nil
		}
		go func() {
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			if srv.Shutdown(ctx) != nil {
				_ = srv.Close()
			}
		}()
		a.lg.Printf("telefoni: porta spenta (nessun telefono e nessuna finestra aperta)")
	}
}

func (a *appService) close() {
	a.mgr.Close()
	if err := a.mgr.Store.Flush(); err != nil {
		a.lg.Printf("telefoni: ultimo uso non salvato: %v", err)
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.srv != nil {
		_ = a.srv.Close()
		a.srv, a.ln = nil, nil
	}
	if a.unannounce != nil {
		a.unannounce()
		a.unannounce = nil
	}
}

func (a *appService) addr() string {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.ln == nil {
		return ""
	}
	return a.ln.Addr().String()
}

// port: la porta in ascolto (nei test casuale), altrimenti quella della configurazione.
func (a *appService) port() int {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.ln != nil {
		return a.ln.Addr().(*net.TCPAddr).Port
	}
	return a.cfgPort
}

// whoami: GET /app/whoami. Gli indirizzi di adesso: l'app li tiene per il prossimo cambio di rete.
func (a *appService) whoami(p phones.Phone) any {
	return map[string]any{
		"phone": map[string]string{"id": p.ID, "name": p.Name},
		"pc":    map[string]string{"name": a.name, "version": a.version},
		"hosts": a.hosts(),
		"port":  a.port(),
	}
}

func (a *appService) PhonesView() api.PhonesView {
	v := api.PhonesView{Phones: []api.PhoneView{}}
	for _, p := range a.mgr.Store.List() {
		v.Phones = append(v.Phones, api.PhoneView{ID: p.ID, Name: p.Name, Paired: p.Paired, LastUsed: p.LastUsed})
	}
	a.mu.Lock()
	listening, lastErr := a.srv != nil, a.lastErr
	a.mu.Unlock()
	v.App = api.AppView{Port: a.port(), Listening: listening, Fingerprint: a.fp, Hosts: a.hosts()}
	if v.App.Hosts == nil {
		v.App.Hosts = []string{}
	}
	switch {
	case a.broken != nil:
		v.App.Error = a.broken.Error()
	case lastErr != nil:
		v.App.Error = lastErr.Error()
	}
	if code, exp, ok := a.mgr.Pairing.Active(); ok && listening {
		v.Pairing = &api.PairingView{Link: phones.Link(v.App.Hosts, v.App.Port, a.fp, code, a.name), Expires: exp.UnixMilli()}
	}
	return v
}

func (a *appService) OpenPairing() (api.PhonesView, error) {
	if a.broken != nil {
		return api.PhonesView{}, a.broken
	}
	a.mgr.OpenWindow() // → sync: la porta si accende qui
	if a.addr() == "" {
		a.mu.Lock()
		err := a.lastErr
		a.mu.Unlock()
		a.mgr.CancelWindow()
		if err == nil {
			err = fmt.Errorf("porta dei telefoni spenta")
		}
		return api.PhonesView{}, err
	}
	return a.PhonesView(), nil
}

func (a *appService) CancelPairing() api.PhonesView {
	a.mgr.CancelWindow()
	return a.PhonesView()
}

func (a *appService) RevokePhone(id string) error { return a.mgr.Revoke(id) }
```

- [ ] **Step 6: Collegare `appService` in `server.go`**

In `service/internal/server/server.go`:

1. import: aggiungere `"calamaio/internal/phones"`.
2. In `Options`, dopo il campo `WSListener net.Listener`:

```go
	// Telefoni (app iPhone): elenco (vuoto = solo in memoria, test; calamaio serve: <dati>/phones.json) e
	// cartella del certificato (vuota = certificato nuovo in memoria a ogni avvio, test; <dati>/app-tls).
	PhonesFile string
	AppTLSDir  string
	// AppListen apre la porta dei telefoni (nil = :AppPort); AppHosts dà gli indirizzi del QR (nil =
	// phones.LocalIPv4s). Nei test: 127.0.0.1.
	AppListen func() (net.Listener, error)
	AppHosts  func() []string
	// TuneApp, se non nil, configura i telefoni prima di partire (test: finestra più corta).
	TuneApp func(*phones.Manager)
```

3. In `Running`, dopo `Game *gamesvc.Service`:

```go
	AppAddr func() string // porta dei telefoni ("" = spenta)
	Phones  *phones.Manager
```

4. In `Run`, subito dopo `base := func(net.Listener) context.Context { return runCtx }`:

```go
	app := openApp(o, c, lg, as, base)
	as.Phones = app
	defer app.close()
```

5. Subito prima di `if o.Ready != nil {` aggiungere `app.sync() // con un telefono già abbinato la porta si accende all'avvio` e nel letterale `Running{…}` passato a `o.Ready` aggiungere `AppAddr: app.addr, Phones: app.mgr`.

- [ ] **Step 7: Eseguire e vedere passare**

Run: `cd service && go test ./internal/server/ -run 'TestApp|TestPhonesRoutes' -v 2>&1 | tail -n 30`
Expected: PASS (`TestAppListenerOnlyWhenNeeded`, `TestAppPairAndAllowlistOverTLS`, `TestAppPairFiveWrongCodes`, `TestAppSSEClosedOnRevoke`, `TestAppPairReplacesOldToken`, `TestAppFilesAndRestart`, `TestAppBrokenCertNotRegenerated`, `TestAppPortBusy`, `TestAppPairingWindowExpires`, `TestPhonesRoutesOnlyLocal`).

Poi `cd service && go vet ./... && go test ./... 2>&1 | tail -n 8`
Expected: tutto `ok`.

- [ ] **Step 8: Commit**

```bash
git add service/internal/config service/internal/api/phones.go service/internal/api/server.go service/internal/server
git commit -m "Telefoni: porta HTTPS 8767 accesa solo con un telefono o una finestra aperta, app_port, /api/phones sulla 8765 (abbina, annulla, revoca), prove su TLS con l'impronta"
```

---

### Task 5: QR in SVG, Bonjour per i telefoni, `serve --loopback` e documentazione del servizio

**Files:**
- Create: `service/internal/phones/qr.go`, `service/internal/mdns/mdns_test.go`
- Modify: `service/go.mod`, `service/go.sum` (solo col sì dell'utente), `service/internal/mdns/mdns.go` (riscritto), `service/internal/api/phones.go` (`PairingQR`, rotta), `service/internal/api/server.go` (rotta), `service/internal/server/app.go` (`PairingQR`, `Loopback`), `service/internal/server/server.go` (WebSocket con `Loopback`, responder mDNS unico), `service/cmd/calamaio/main.go` (percorsi, `--loopback`), `docs/PROTOCOLLO.md` (sezione nuova in fondo), `docs/DECISIONI.md` (sezione nuova in fondo)
- Test: `service/internal/phones/qr_test.go`, `service/internal/mdns/mdns_test.go`, `service/internal/server/app_test.go` (due test in più)

**Interfaces:**
- Consumes: `phones.Link` (Task 2), `appService`, `api.PhonesControl` (Task 4).
- Produces:
  - `func phones.QRSVG(text string) ([]byte, error)` (livello M, margine 4 moduli);
  - `api.PhonesControl` con in più `PairingQR() ([]byte, bool)`; rotta `GET /api/phones/pair/qr.svg` (`guard`, `image/svg+xml`, `Cache-Control: no-store`, 404 senza finestra);
  - `mdns.AppServiceType = "_calamaio-app._tcp"`, `type mdns.Responder`, `mdns.NewResponder(lg) (*Responder, error)`, `(*Responder).Run(ctx) error`, `(*Responder).Announce(cfg dnssd.Config) (stop func())`, `mdns.DisplayConfig(port int, path string) dnssd.Config`, `mdns.AppConfig(port int, fp string) dnssd.Config`;
  - `server.Options.Loopback bool` (WebSocket e porta dei telefoni solo su `127.0.0.1`, QR con `h=127.0.0.1`); comando `calamaio serve --loopback`.

- [ ] **Step 1: Chiedere all'utente la dipendenza del QR**

Scrivere all'utente (e aspettare la risposta): «Per disegnare il QR di abbinamento nella webapp vorrei aggiungere al servizio la libreria `rsc.io/qr` v0.2.0 (Russ Cox, BSD, Go puro, nessuna dipendenza). Il motore QR della webapp (`theme/qr.js`) arriva solo alla versione 5 (84 byte) e il link è di circa 200 byte. Senza la libreria la webapp mostra il link da copiare e l'app lo accetta incollato: funziona lo stesso, senza fotocamera. Posso aggiungerla?»

- Se **sì**: `cd service && go get rsc.io/qr@v0.2.0 2>&1 | tail -n 3 && go mod tidy 2>&1 | tail -n 3`; poi gli Step 2–4.
- Se **no**: saltare gli Step 2 e 3; allo Step 4 `appService.PairingQR` è `func (a *appService) PairingQR() ([]byte, bool) { return nil, false }` e `TestPhonesQR` si scrive con l'attesa di 404 anche a finestra aperta; in DECISIONI (Step 9) la riga «QR» dice «senza libreria: solo link da copiare».

- [ ] **Step 2: Test del QR (fallisce)**

`service/internal/phones/qr_test.go`:

```go
package phones

import (
	"bytes"
	"fmt"
	"strings"
	"testing"
)

func TestQRSVG(t *testing.T) {
	link := Link([]string{"192.168.100.200", "10.0.0.7", "172.16.5.4"}, 8767, strings.Repeat("ab", 32), strings.Repeat("cd", 16), "mac-sviluppo")
	svg, err := QRSVG(link)
	if err != nil {
		t.Fatal(err)
	}
	var n int
	if _, err := fmt.Sscanf(string(svg), `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d`, &n); err != nil {
		t.Fatalf("intestazione: %v %.80s", err, svg)
	}
	size := n - 8
	if size < 21 || (size-17)%4 != 0 || size > 4*12+17 {
		t.Fatalf("lato %d moduli: versione oltre la 12, troppo fitto per 240 px", size)
	}
	if !bytes.Contains(svg, []byte("M4 4h1v1h-1z")) || !bytes.HasSuffix(svg, []byte(`"/></svg>`)) {
		t.Fatalf("motivo di posizione in alto a sinistra mancante: %.200s", svg)
	}
	if _, err := QRSVG(strings.Repeat("x", 5000)); err == nil {
		t.Fatal("testo troppo lungo accettato")
	}
}
```

Run: `cd service && go test ./internal/phones/ -run TestQRSVG 2>&1 | tail -n 5`
Expected: FAIL (`undefined: QRSVG`).

- [ ] **Step 3: Scrivere `qr.go`**

`service/internal/phones/qr.go`:

```go
package phones

import (
	"bytes"
	"fmt"

	"rsc.io/qr"
)

// QRSVG: il link come QR in SVG (livello M, margine di 4 moduli), mostrato dalla webapp come immagine
// (img-src 'self': nessuno script, nessuno stile in linea).
func QRSVG(text string) ([]byte, error) {
	c, err := qr.Encode(text, qr.M)
	if err != nil {
		return nil, err
	}
	n := c.Size + 8
	var b bytes.Buffer
	fmt.Fprintf(&b, `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" shape-rendering="crispEdges">`, n, n)
	fmt.Fprintf(&b, `<rect width="%d" height="%d" fill="#fff"/><path fill="#000" d="`, n, n)
	for y := 0; y < c.Size; y++ {
		for x := 0; x < c.Size; x++ {
			if c.Black(x, y) {
				fmt.Fprintf(&b, "M%d %dh1v1h-1z", x+4, y+4)
			}
		}
	}
	b.WriteString(`"/></svg>`)
	return b.Bytes(), nil
}
```

Run: `cd service && go test ./internal/phones/ -run TestQRSVG -v 2>&1 | tail -n 5`
Expected: PASS.

- [ ] **Step 4: Rotta del QR**

In `service/internal/api/phones.go`, nell'interfaccia `PhonesControl` aggiungere `PairingQR() ([]byte, bool) // SVG del link della finestra aperta` e in fondo al file:

```go
func (s *Server) apiPhonesQR(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	svg, ok := s.Phones.PairingQR()
	if !ok {
		writeJSON(w, http.StatusNotFound, errBody("nessun abbinamento aperto"))
		return
	}
	w.Header().Set("Content-Type", "image/svg+xml")
	w.Header().Set("Cache-Control", "no-store")
	_, _ = w.Write(svg)
}
```

In `service/internal/api/server.go`, dopo `mux.HandleFunc("POST /api/phones/pair/cancel", …)`:

```go
	mux.HandleFunc("GET /api/phones/pair/qr.svg", guard(s.apiPhonesQR))
```

In `service/internal/server/app.go`, in fondo:

```go
// PairingQR: il QR della finestra aperta (nil, false se chiusa o se il QR non si disegna).
func (a *appService) PairingQR() ([]byte, bool) {
	v := a.PhonesView()
	if v.Pairing == nil {
		return nil, false
	}
	svg, err := phones.QRSVG(v.Pairing.Link)
	if err != nil {
		a.lg.Printf("telefoni: QR non disegnato: %v", err)
		return nil, false
	}
	return svg, true
}
```

In `service/internal/server/app_test.go` aggiungere:

```go
func TestPhonesQR(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	if c, _, _ := w.do(http.MethodGet, "/api/phones/pair/qr.svg", "", ""); c != 404 {
		t.Fatalf("senza finestra: %d", c)
	}
	openPairing(t, w)
	c, m, hd := w.do(http.MethodGet, "/api/phones/pair/qr.svg", "", "")
	if body, _ := m["_raw"].(string); c != 200 || hd.Get("Content-Type") != "image/svg+xml" || hd.Get("Cache-Control") != "no-store" || !strings.HasPrefix(body, "<svg") {
		t.Fatalf("QR: %d %v %.60v", c, hd, m)
	}
	w.post("/api/phones/pair/cancel", `{}`)
	if c, _, _ := w.do(http.MethodGet, "/api/phones/pair/qr.svg", "", ""); c != 404 {
		t.Fatalf("dopo l'annullamento: %d", c)
	}
}
```

Run: `cd service && go test ./internal/server/ -run TestPhonesQR -v 2>&1 | tail -n 5`
Expected: PASS.

- [ ] **Step 5: Un solo responder mDNS (test che fallisce, poi riscrittura)**

`service/internal/mdns/mdns_test.go`:

```go
package mdns

import (
	"strings"
	"testing"
)

func TestConfigs(t *testing.T) {
	d := DisplayConfig(8766, "/ws")
	if d.Type != ServiceType || d.Port != 8766 || d.Host != HostAlias || d.Text["proto"] != "1" || d.Text["path"] != "/ws" {
		t.Fatalf("display: %+v", d)
	}
	fp := strings.Repeat("ab", 32)
	a := AppConfig(8767, fp)
	if a.Type != "_calamaio-app._tcp" || a.Port != 8767 || a.Host != HostAlias || len(a.Text) != 1 || a.Text["fp"] != fp[:16] {
		t.Fatalf("telefoni: %+v", a)
	}
	if a.Name != d.Name || !strings.HasPrefix(a.Name, "Calamaio su ") {
		t.Fatalf("nomi: %q %q", a.Name, d.Name)
	}
}
```

Run: `cd service && go test ./internal/mdns/ 2>&1 | tail -n 5`
Expected: FAIL (`undefined: DisplayConfig`).

Riscrivere `service/internal/mdns/mdns.go`:

```go
// Package mdns annuncia il servizio: ai display `_calamaio._tcp` (TXT proto=1, path=/ws), ai telefoni
// abbinati `_calamaio-app._tcp` (TXT fp = prime 16 cifre dell'impronta), con un solo responder.
//
// Libreria: github.com/brutella/dnssd (responder mDNS/DNS-SD in Go puro, con probing e
// gestione dei conflitti di nome come da RFC 6762). Su macOS convive con mDNSResponder perché
// apre la porta 5353 con SO_REUSEPORT (lo fa la libreria standard di Go per gli indirizzi
// multicast).
package mdns

import (
	"context"
	"fmt"
	"log"
	"os"
	"strings"
	"sync"

	"github.com/brutella/dnssd"
	dnssdlog "github.com/brutella/dnssd/log"
)

const (
	ServiceType    = "_calamaio._tcp"
	AppServiceType = "_calamaio-app._tcp"
	// HostAlias è il nome host annunciato insieme al servizio: dà anche l'alias calamaio.local
	// citato nel brief (5.3). Se è già occupato la libreria passa a calamaio-2.local.
	HostAlias = "calamaio"
)

func init() {
	// La libreria scrive su stdout per default: i nostri log bastano.
	dnssdlog.Info.Disable()
}

// Responder: un solo responder per tutti gli annunci del servizio (due responder si contenderebbero
// calamaio.local).
type Responder struct {
	rp dnssd.Responder
	lg *log.Logger
}

func NewResponder(lg *log.Logger) (*Responder, error) {
	rp, err := dnssd.NewResponder()
	if err != nil {
		return nil, fmt.Errorf("mdns: %w", err)
	}
	return &Responder{rp: rp, lg: lg}, nil
}

// Run risponde finché ctx non viene chiuso. Blocca: va chiamato in una goroutine.
func (r *Responder) Run(ctx context.Context) error {
	err := r.rp.Respond(ctx)
	if ctx.Err() != nil {
		return nil
	}
	if err != nil {
		return fmt.Errorf("mdns: %w", err)
	}
	return nil
}

// Announce pubblica cfg, anche col responder già acceso (la libreria fa il probing, ~1 s: per questo in una
// goroutine), e restituisce la funzione che lo ritira. Non blocca.
func (r *Responder) Announce(cfg dnssd.Config) (stop func()) {
	var (
		mu      sync.Mutex
		h       dnssd.ServiceHandle
		stopped bool
	)
	go func() {
		sv, err := dnssd.NewService(cfg)
		if err == nil {
			var hh dnssd.ServiceHandle
			if hh, err = r.rp.Add(sv); err == nil {
				mu.Lock()
				defer mu.Unlock()
				if stopped {
					r.rp.Remove(hh)
					return
				}
				h = hh
				r.lg.Printf("mdns: annuncio %q %s porta %d (host %s.local)", cfg.Name, cfg.Type, cfg.Port, HostAlias)
				return
			}
		}
		r.lg.Printf("mdns: %s non annunciato: %v", cfg.Type, err)
	}()
	return func() {
		mu.Lock()
		defer mu.Unlock()
		stopped = true
		if h != nil {
			r.rp.Remove(h)
			h = nil
		}
	}
}

// DisplayConfig: l'annuncio per le schede.
func DisplayConfig(port int, path string) dnssd.Config {
	return dnssd.Config{Name: InstanceName(), Type: ServiceType, Host: HostAlias, Port: port,
		Text: map[string]string{"proto": "1", "path": path}}
}

// AppConfig: l'annuncio per l'app iPhone; nel TXT le prime 16 cifre dell'impronta (l'app riconosce il suo PC).
func AppConfig(port int, fp string) dnssd.Config {
	if len(fp) > 16 {
		fp = fp[:16]
	}
	return dnssd.Config{Name: InstanceName(), Type: AppServiceType, Host: HostAlias, Port: port,
		Text: map[string]string{"fp": fp}}
}

// InstanceName: «Calamaio su <nome del PC>», leggibile in un browser DNS-SD.
func InstanceName() string {
	return "Calamaio su " + ShortHost()
}

// ShortHost restituisce il nome del PC senza dominio (mac-sviluppo.local → mac-sviluppo).
func ShortHost() string {
	h, err := os.Hostname()
	if err != nil || h == "" {
		return "PC"
	}
	if i := strings.IndexByte(h, '.'); i > 0 {
		h = h[:i]
	}
	return h
}
```

In `service/internal/server/server.go` sostituire il blocco `if o.MDNS { … mdns.Announce(runCtx, wsPort, "/ws", lg) … }` con:

```go
	if o.MDNS {
		rp, err := mdns.NewResponder(lg)
		if err != nil {
			lg.Printf("%v (i display dovranno usare l'indirizzo IP)", err)
		} else {
			rp.Announce(mdns.DisplayConfig(wsPort, "/ws"))
			app.mu.Lock()
			app.announce = func(port int, fp string) func() { return rp.Announce(mdns.AppConfig(port, fp)) }
			app.mu.Unlock()
			wg.Add(1)
			go func() {
				defer wg.Done()
				if err := rp.Run(runCtx); err != nil {
					lg.Printf("%v (i display dovranno usare l'indirizzo IP)", err)
				}
			}()
		}
	}
```

(il blocco resta prima di `app.sync()` aggiunto nel Task 4, così una porta accesa all'avvio viene annunciata).

Run: `cd service && go test ./internal/mdns/ ./internal/server/ 2>&1 | tail -n 5`
Expected: PASS.

- [ ] **Step 6: `--loopback` (test che fallisce, poi codice)**

In `service/internal/server/app_test.go`:

```go
func freePort(t *testing.T) int {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	return ln.Addr().(*net.TCPAddr).Port
}

// --loopback: porta dei telefoni solo su 127.0.0.1 e QR con h=127.0.0.1 (prove dell'app nel simulatore).
func TestAppLoopback(t *testing.T) {
	port := freePort(t)
	s := startOpts(t, true, 0, func(o *Options) {
		o.Loopback = true
		o.AppListen, o.AppHosts = nil, nil
		o.Config.AppPort = port
	})
	w := newWeb(t, s)
	base, _, _ := openPairing(t, w) // controlla anche h=127.0.0.1
	if base != "https://127.0.0.1:"+itoa(port) || s.run.AppAddr() != "127.0.0.1:"+itoa(port) {
		t.Fatalf("base %s, ascolto %s", base, s.run.AppAddr())
	}
}
```

Run: `cd service && go test ./internal/server/ -run TestAppLoopback 2>&1 | tail -n 5`
Expected: FAIL (`unknown field Loopback`).

In `server.go`, in `Options` dopo `MDNS bool`:

```go
	// Loopback: WebSocket dei display e porta dei telefoni solo su 127.0.0.1, QR con h=127.0.0.1
	// (prove dell'app nel simulatore, che vede il loopback del Mac). La 8765 è comunque solo locale.
	Loopback bool
```

e in `Run`, nel ramo `if wsLn == nil {`:

```go
		// Tutte le interfacce: i display arrivano dalla rete locale (con Loopback solo 127.0.0.1).
		addr := fmt.Sprintf(":%d", c.WSPort)
		if o.Loopback {
			addr = fmt.Sprintf("127.0.0.1:%d", c.WSPort)
		}
```

In `app.go`, in `openApp`, sostituire i due blocchi `if a.listen == nil {…}` e `if a.hosts == nil {…}` con:

```go
	a.listen = o.AppListen
	if a.listen == nil {
		host := ""
		if o.Loopback {
			host = "127.0.0.1"
		}
		addr := net.JoinHostPort(host, strconv.Itoa(c.AppPort))
		a.listen = func() (net.Listener, error) { return net.Listen("tcp", addr) }
	}
	a.hosts = o.AppHosts
	if a.hosts == nil {
		a.hosts = phones.LocalIPv4s
		if o.Loopback {
			a.hosts = func() []string { return []string{"127.0.0.1"} }
		}
	}
```

(import `"strconv"`).

In `service/cmd/calamaio/main.go`, in `serve`, dopo `noMDNS := fs.Bool(…)`:

```go
	loopback := fs.Bool("loopback", false, "WebSocket dei display e porta dei telefoni solo su 127.0.0.1 (prove dell'app nel simulatore)")
```

nel letterale `o := server.Options{…}` aggiungere `Loopback: *loopback,` e nel blocco `if d, err := config.DataDir(); err == nil {` aggiungere:

```go
		o.PhonesFile, o.AppTLSDir = filepath.Join(d, "phones.json"), filepath.Join(d, "app-tls")
```

Run: `cd service && go vet ./... && go test ./... 2>&1 | tail -n 8`
Expected: tutto `ok`.

- [ ] **Step 7: Prova a mano senza scheda (servizio vero, dati temporanei)**

```bash
TMPD=$(mktemp -d) && (cd service && go build -o $TMPD/calamaio ./cmd/calamaio) && \
CALAMAIO_CONFIG_DIR=$TMPD/c CALAMAIO_DATA_DIR=$TMPD/d CALAMAIO_API_PORT=28765 CALAMAIO_WS_PORT=28766 CALAMAIO_APP_PORT=28767 \
  $TMPD/calamaio serve --fake --loopback > $TMPD/log 2>&1 &
SPID=$!; sleep 1
curl -s -X POST http://127.0.0.1:28765/api/phones/pair -H 'Content-Type: application/json' \
  -H 'Origin: http://127.0.0.1:28765' -H 'Sec-Fetch-Site: same-origin' -d '{}' | head -c 400; echo
dns-sd -B _calamaio-app._tcp local. > $TMPD/dnssd 2>&1 & DPID=$!; sleep 4; kill $DPID; tail -n 3 $TMPD/dnssd
kill $SPID; sleep 1; grep -E 'telefoni|mdns' $TMPD/log | tail -n 6; ls -la $TMPD/d/app-tls
```

Expected: JSON con `"pairing":{"link":"calamaio://pair?v=1&h=127.0.0.1&p=28767…` (il `&` è `&` scritto da `encoding/json`), una riga `Add … Calamaio su <PC>` da `dns-sd` (se il firewall di macOS chiede il permesso per il binario, rispondere «Rifiuta»: l'annuncio si vede lo stesso dal Mac), righe `telefoni: HTTPS su 127.0.0.1:28767` e `mdns: annuncio … _calamaio-app._tcp` nel registro, `app-tls` con `cert.pem` e `key.pem` `-rw-------`. 

- [ ] **Step 8: Documentazione del protocollo**

In fondo a `docs/PROTOCOLLO.md` aggiungere:

```markdown
## Telefoni (app iPhone)

Spec: `docs/superpowers/specs/2026-10-16-app-iphone-design.md` §3. Codice: `service/internal/phones`, `service/internal/api/phone.go`, `service/internal/api/phones.go`, `service/internal/server/app.go`.

**Porta**: HTTPS su tutte le interfacce, porta `app_port` (predefinita **8767**, variabile `CALAMAIO_APP_PORT`; con `calamaio serve --loopback` solo `127.0.0.1`). Accesa solo se c'è almeno un telefono abbinato o una finestra di abbinamento aperta. TLS 1.3, solo `http/1.1`. Certificato ECDSA P-256 autofirmato di 10 anni in `<dati>/app-tls/` (`cert.pem`, `key.pem`, 0600, cartella 0700), creato solo se manca del tutto: uno rovinato non si rifà (si cancella la cartella e si riabbinano i telefoni). L'app fissa l'**impronta** = SHA-256 del DER del certificato, 64 cifre esadecimali minuscole. La 8765 non cambia.

**Bonjour**: `_calamaio-app._tcp` sulla porta dei telefoni, TXT `fp=<prime 16 cifre dell'impronta>`, solo mentre la porta è accesa.

**Token**: `Authorization: Bearer <64 cifre esadecimali minuscole>` su ogni richiesta tranne `POST /app/pair`. Il servizio tiene solo lo SHA-256 del token in `<dati>/phones.json` (0600): `{"v":1,"phones":[{"id","name","tokenSha256","paired","lastUsed"}]}` (ms; l'ultimo uso si scrive al massimo ogni 10 minuti). Token assente, malformato, sconosciuto o revocato: **401** `{"error":"non autorizzato"}` con `WWW-Authenticate: Bearer`, sempre uguale, prima di guardare la rotta. Con un token valido, ciò che non è nell'elenco chiuso risponde **404** `{"error":"non trovato"}`.

**Elenco chiuso** (`phoneRoutes`): `GET /`, `/index.html`, `/avatars.html`, `/game.html`, `/device.html`, `/editor.html`, `/assets/…`; `GET /app/whoami`; `GET /api/status`, `GET /api/events`; `POST /api/displays/{board}/settings|full-refresh|workday|theme`; `GET /api/crashes`, `GET /api/crashes/{id}`; `POST /api/sessions/{id}/archive|unarchive|avatar`; avatar (`GET /api/avatars`, `POST /api/avatars`, `GET /api/avatars/{id}`, `POST /api/avatars/default`, `GET /api/avatars/{id}/anchors`, `POST /api/projects/avatar`); gioco (`GET|POST /api/game`, `POST /api/game/wear|special`, `GET /api/accessories/{id}`, `POST /api/accessories`); temi (tutte le rotte `/api/themes…`). Le rotte dei Giochi (`/api/gb/…`) le aggiunge il piano C. **Mai dal telefono**: `/hook*`, `/statusline`, `/debug*`, `firmware.html`, `/vendor/`, `/api/firmware*`, abbinamento e disabbinamento della scheda, Wi-Fi della scheda, OTA, backup, registro remoto, `/api/phones*`. Dentro l'elenco la richiesta passa alla stessa mux della 8765 con un segno nel contesto: l'Host non si controlla e vale come «stessa origine» per `postGuard`/`bodyGuard`/`deleteGuard` (il Content-Type resta obbligatorio); l'`Authorization` si toglie prima.

**`POST /app/pair`** `{"code":"<32 cifre>","name":"<nome del telefono>"}` (`application/json`, ≤ 4 KiB, niente campi in più) → 200 `{"token","id","name"}` · 403 `{"error":"codice sbagliato","left":n}` · 409 nessuna finestra (mai aperta, scaduta, già usata o chiusa dopo 5 errori) · 400 · 405 · 415. Con `Authorization: Bearer <token di prima>` valido il telefono abbinato prima si revoca (QR nuovo dopo un cambio di rete: nessun doppione).

**`GET /app/whoami`** → `{"phone":{"id","name"},"pc":{"name","version"},"hosts":["ip",…],"port":8767}`: l'app ci prova il collegamento e tiene gli indirizzi più recenti.

**QR** (`phones.Link`): `calamaio://pair?v=1&h=<ip1,ip2>&p=<porta>&fp=<impronta completa>&c=<codice>&n=<nome PC>`; nome con `%20` per gli spazi.

**Dalla webapp (solo 8765)**: `GET /api/phones` → `{"phones":[{"id","name","paired","lastUsed"}],"pairing":{"link","expires"}|null,"app":{"port","listening","fingerprint","hosts","error"?}}` · `POST /api/phones/pair` `{}` → 200 la stessa vista con la finestra (5 minuti, codice nuovo) · 409 `{error}` (certificato o elenco illeggibili, porta occupata) · `POST /api/phones/pair/cancel` → 200 · `GET /api/phones/pair/qr.svg` → `image/svg+xml` (404 senza finestra) · `DELETE /api/phones/{id}` → 204 · 404 · 500 (revocato in memoria, `phones.json` non scritto). La revoca chiude subito le richieste aperte di quel telefono (SSE compresi); la porta si spegne quando non ci sono più telefoni né finestre.
```

- [ ] **Step 9: DECISIONI**

In fondo a `docs/DECISIONI.md` aggiungere:

```markdown
## App iPhone

- **Fase P1, servizio** (spec `docs/superpowers/specs/2026-10-16-app-iphone-design.md` §3, piano `docs/superpowers/plans/2026-10-16-app-iphone-P1-P2.md`): pacchetto `service/internal/phones`, porta HTTPS 8767 a richiesta, `/api/phones` sulla 8765; forme in PROTOCOLLO, «Telefoni».
- **Ordine dei controlli sulla 8767**: prima il token (401 uguale per tutti i casi), poi l'elenco chiuso (404): chi non ha il token non scopre quali rotte esistono. `POST /app/pair` è l'unica eccezione al token (spec: «ogni richiesta»; senza token non si potrebbe abbinare).
- **`GET /app/whoami`** (non nella spec): prova del collegamento per l'app e indirizzi aggiornati del PC, per ritrovarlo dopo un cambio di IP senza riabbinare.
- **Riabbinare sostituisce**: `POST /app/pair` con il token di prima revoca il telefono vecchio (niente doppioni nell'elenco dopo un QR nuovo).
- **Giochi fuori dall'elenco per ora**: la spec mette `/api/gb/*` «quando esisterà»; per tenere l'elenco chiuso le rotte le aggiunge il piano C, una per una (con `TestPhoneAllowedRoutes`).
- **Certificato mai rifatto da solo**: rovinato, a metà o scaduto → porta spenta e motivo in `/api/phones` (`app.error`); per rifarlo si cancella `<dati>/app-tls` e si riabbinano i telefoni. Una cartella nuova si prepara accanto e si rinomina.
- **Revoca**: vale subito in memoria anche se `phones.json` non si scrive (500 con il motivo): un telefono revocato non resta valido per un disco pieno.
- **Spegnimento della porta**: il listener si chiude subito, le richieste in corso hanno 1 s (la risposta che chiude la finestra arriva al telefono).
- **QR**: libreria `rsc.io/qr` v0.2.0 (sì dell'utente del <data>), SVG da `GET /api/phones/pair/qr.svg`; `theme/qr.js` resta com'è (versioni 1–5, parità col firmware). Il link si può anche copiare e incollare nell'app (simulatore senza fotocamera).
- **`serve --loopback`**: WebSocket e porta dei telefoni solo su 127.0.0.1 e QR con `h=127.0.0.1`, per le prove dell'app nel simulatore senza aprire porte in rete.
- **mDNS**: un solo responder per display e telefoni (`mdns.Responder`); l'annuncio dei telefoni si aggiunge e si toglie con la porta.
```

(sostituire `<data>` con la data del sì; se l'utente ha detto no, la riga «QR» diventa «QR: senza libreria (decisione dell'utente del <data>): la webapp mostra solo il link da copiare; l'app lo accetta incollato».)

- [ ] **Step 10: Commit**

```bash
git add service docs/PROTOCOLLO.md docs/DECISIONI.md
git commit -m "Telefoni: QR del link in SVG, annuncio Bonjour _calamaio-app._tcp con un solo responder, serve --loopback, phones.json e app-tls nella cartella dei dati, PROTOCOLLO e DECISIONI"
```

---

### Task 6: Sezione «Telefoni» nella pagina Dispositivo

**Files:**
- Create: `service/internal/webui/static/assets/js/phones.js`
- Modify: `service/internal/webui/static/device.html:42-46,94` (contenitore e finestra di conferma), `service/internal/webui/static/assets/js/device.js:5-7` (import) e in fondo, `service/internal/webui/static/assets/js/api.js` (in fondo), `service/internal/webui/static/assets/app.css` (in fondo), `tools/webui-dev/test-units.mjs` (in fondo, prima del riepilogo), `tools/webui-dev/mock-server.mjs:184` (rotte finte)

**Interfaces:**
- Consumes: rotte `/api/phones…` (Task 4-5), `h`, `showErr`, `showMsg`, `$` di `device.js`.
- Produces: `phones.js` con `countdown(expires, now) → "m:ss"`, `lastUsedText(p, now)`, `pairedText(p)`, `appStateText(app)`; in `api.js` `listPhones()`, `pairPhone()`, `cancelPhonePairing()`, `revokePhone(id)`, `phoneQrUrl(expires)`. La sezione resta vuota quando `GET /api/phones` non risponde 200 (servizio vecchio, o pagina aperta dall'app sulla 8767 dove è 404).

- [ ] **Step 1: Test dei testi (fallisce)**

In `tools/webui-dev/test-units.mjs`, tra gli import in cima:

```js
import { countdown, lastUsedText, pairedText, appStateText } from '../../service/internal/webui/static/assets/js/phones.js';
```

e prima del riepilogo finale (la riga che stampa i FAIL o esce con `process.exit`):

```js
// Telefoni: testi della sezione in Dispositivo.
{
  const now = Date.UTC(2026, 9, 16, 10, 0, 0);
  check(countdown(now + 300000, now) === '5:00', 'countdown 5:00');
  check(countdown(now + 299001, now) === '5:00', 'countdown arrotonda per eccesso');
  check(countdown(now + 61000, now) === '1:01', 'countdown 1:01');
  check(countdown(now - 5000, now) === '0:00', 'countdown scaduto');
  check(lastUsedText({ lastUsed: 0 }, now) === 'mai usato', 'mai usato');
  check(lastUsedText({ lastUsed: now - 20000 }, now) === 'usato adesso', 'usato adesso');
  check(lastUsedText({ lastUsed: now - 5 * 60000 }, now) === 'usato 5 min fa', 'usato 5 min fa');
  check(lastUsedText({ lastUsed: now - 3 * 3600000 }, now) === 'usato 3 h fa', 'usato 3 h fa');
  check(lastUsedText({ lastUsed: now - 3 * 86400000 }, now) === 'usato 3 giorni fa', 'usato 3 giorni fa');
  check(/^abbinato il \d\d\/\d\d\/2026$/.test(pairedText({ paired: now })), 'abbinato il gg/mm/aaaa');
  check(appStateText({ listening: true, port: 8767, fingerprint: 'ab'.repeat(32) }) === 'Accesa sulla porta 8767 (HTTPS, impronta abababababababab…)', 'porta accesa');
  check(appStateText({ listening: false, port: 8767, fingerprint: '' }) === 'Spenta: si accende quando abbini un telefono', 'porta spenta');
  check(appStateText({ listening: false, error: 'porta occupata' }) === 'Spenta: porta occupata', 'porta con errore');
  console.log('telefoni: 13 testi controllati');
}
```

Run: `cd tools/webui-dev && node test-units.mjs 2>&1 | tail -n 5`
Expected: errore `Cannot find module …/phones.js`.

- [ ] **Step 2: `phones.js`**

`service/internal/webui/static/assets/js/phones.js`:

```js
// Telefoni abbinati (app iPhone): testi della sezione in Dispositivo, senza DOM (provati in
// tools/webui-dev/test-units.mjs).

const pad2 = (n) => String(n).padStart(2, '0');

// Tempo che resta alla finestra di abbinamento, «m:ss», per eccesso.
export function countdown(expires, now) {
  const s = Math.max(0, Math.ceil((expires - now) / 1000));
  return Math.floor(s / 60) + ':' + pad2(s % 60);
}

export function lastUsedText(p, now) {
  if (!p.lastUsed) return 'mai usato';
  const m = Math.max(0, Math.round((now - p.lastUsed) / 60000));
  if (m < 1) return 'usato adesso';
  if (m < 60) return 'usato ' + m + ' min fa';
  if (m < 48 * 60) return 'usato ' + Math.floor(m / 60) + ' h fa';
  return 'usato ' + Math.floor(m / 1440) + ' giorni fa';
}

export function pairedText(p) {
  const d = new Date(p.paired);
  return 'abbinato il ' + pad2(d.getDate()) + '/' + pad2(d.getMonth() + 1) + '/' + d.getFullYear();
}

export function appStateText(app) {
  if (app.error) return 'Spenta: ' + app.error;
  if (app.listening) return 'Accesa sulla porta ' + app.port + ' (HTTPS, impronta ' + String(app.fingerprint).slice(0, 16) + '…)';
  return 'Spenta: si accende quando abbini un telefono';
}
```

Run: `cd tools/webui-dev && node test-units.mjs 2>&1 | tail -n 5`
Expected: `telefoni: 13 testi controllati` e nessun `FAIL`.

- [ ] **Step 3: Chiamate in `api.js`**

In fondo a `service/internal/webui/static/assets/js/api.js`:

```js
/* ---------- telefoni abbinati (app iPhone; solo dalla webapp sul PC: dalla porta dei telefoni sono 404) ---------- */

// { status, body }: 200 { phones, pairing, app } · 404 servizio senza telefoni (o pagina aperta dall'app).
export async function listPhones() {
  const res = await fetch('/api/phones', { cache: 'no-store' });
  return { status: res.status, body: await readJSON(res) };
}

export const pairPhone = () => postJSON('/api/phones/pair', {});
export const cancelPhonePairing = () => postJSON('/api/phones/pair/cancel', {});

export async function revokePhone(id) {
  const res = await fetch('/api/phones/' + encodeURIComponent(id), { method: 'DELETE' });
  return { status: res.status, body: await readJSON(res) };
}

// expires nell'indirizzo: un QR nuovo per ogni finestra (niente cache del browser).
export const phoneQrUrl = (expires) => '/api/phones/pair/qr.svg?e=' + encodeURIComponent(expires);
```

- [ ] **Step 4: HTML e CSS**

In `service/internal/webui/static/device.html`, subito dopo `<div class="settings" id="groups"></div>`:

```html

  <div id="phones"></div>
```

e prima di `</body>`:

```html
<dialog class="modal" id="dlg-phone-revoke" aria-labelledby="phone-revoke-title">
  <form method="dialog">
    <h2 id="phone-revoke-title">Revocare il telefono?</h2>
    <p>Il telefono non potrà più usare il servizio finché non lo abbini di nuovo. Se è collegato adesso, si scollega subito.</p>
    <div class="card-actions end">
      <button type="submit" class="btn secondary" value="cancel">Annulla</button>
      <button type="submit" class="btn danger" value="ok">Revoca</button>
    </div>
  </form>
</dialog>
```

In fondo a `service/internal/webui/static/assets/app.css`:

```css
/* Telefoni abbinati (Dispositivo) */
#phones { margin-top: 18px; }
.phone-list { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 8px; }
.phone-list li { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; }
.phone-name { font-weight: 600; color: var(--ink); }
.phone-pair { display: flex; flex-direction: column; gap: 8px; align-items: flex-start; }
.phone-qr { width: 240px; height: 240px; max-width: 100%; background: #FFFFFF; border: 1px solid var(--line); border-radius: 6px; }
.phone-link { word-break: break-all; font-size: 12px; }
```

- [ ] **Step 5: La sezione in `device.js`**

In `service/internal/webui/static/assets/js/device.js`, alla riga 5 aggiungere all'import di `./api.js` i nomi `listPhones, pairPhone, cancelPhonePairing, revokePhone, phoneQrUrl`, e dopo la riga 7:

```js
import { countdown, lastUsedText, pairedText, appStateText } from './phones.js';
```

Prima dell'ultima riga (`watchStatus(onStatus, …)`):

```js
/* ---------- telefoni abbinati (app iPhone, docs/PROTOCOLLO.md «Telefoni») ---------- */

// Vale per il PC, non per una scheda: si vede anche senza display collegati. Dall'app (porta 8767)
// GET /api/phones è 404 e la sezione resta vuota.
let phones = null;      // ultima risposta 200 di GET /api/phones, null = sezione nascosta
let phonesSig = '';
let phonesTimer = 0;
let phonesTick = 0;

async function loadPhones() {
  try {
    const r = await listPhones();
    const before = phones ? phones.phones.length : -1;
    phones = r.status === 200 ? r.body : null;
    if (phones && before >= 0 && phones.phones.length > before) {
      const p = phones.phones[phones.phones.length - 1];
      showMsg('Telefono abbinato: «' + p.name + '».');
    }
  } catch (e) { phones = null; }
  renderPhones();
}

// Finestra aperta: si guarda ogni secondo per vedere il telefono comparire; altrimenti ogni 15 s.
function schedulePhones() {
  clearTimeout(phonesTimer);
  phonesTimer = setTimeout(() => { loadPhones().finally(schedulePhones); }, phones && phones.pairing ? 1000 : 15000);
}

async function phonesPair() {
  const r = await pairPhone();
  if (r.status !== 200) { showErr(r.body.error || 'Abbinamento non aperto.'); return; }
  phones = r.body;
  renderPhones();
  schedulePhones();
}

async function phonesCancel() {
  const r = await cancelPhonePairing();
  if (r.status === 200) { phones = r.body; renderPhones(); }
}

function phonesRevoke(p) {
  const dlg = $('dlg-phone-revoke');
  $('phone-revoke-title').textContent = 'Revocare «' + p.name + '»?';
  dlg.returnValue = '';
  dlg.addEventListener('close', async () => {
    if (dlg.returnValue !== 'ok') return;
    const r = await revokePhone(p.id);
    if (r.status === 204) showMsg('«' + p.name + '» revocato: non può più usare il servizio.');
    else showErr(r.body.error || 'Revoca non riuscita.');
    loadPhones();
  }, { once: true });
  dlg.showModal();
}

function copyLink() {
  navigator.clipboard.writeText(phones.pairing.link)
    .then(() => showMsg('Link copiato: incollalo nell’app Calamaio.'), () => showErr('Copia non riuscita: seleziona il link a mano.'));
}

function renderPhones() {
  const box = $('phones');
  clearInterval(phonesTick);
  if (!phones) { box.replaceChildren(); phonesSig = ''; return; }
  const now = Date.now();
  const sig = JSON.stringify([phones.phones.map((p) => [p.id, p.name, Math.round((now - (p.lastUsed || now)) / 60000)]), phones.pairing && phones.pairing.expires, phones.app]);
  if (sig !== phonesSig) {
    phonesSig = sig;
    const list = phones.phones.length
      ? h('ul', { class: 'phone-list' }, phones.phones.map((p) => h('li', null,
        h('span', { class: 'phone-name' }, p.name),
        h('span', { class: 'set-note' }, pairedText(p) + ' · ' + lastUsedText(p, now)),
        h('button', { type: 'button', class: 'btn small danger', onclick: () => phonesRevoke(p) }, 'Revoca'))))
      : h('p', null, 'Nessun telefono abbinato.');
    let pair;
    if (phones.pairing) {
      pair = h('div', { class: 'phone-pair' },
        h('img', { class: 'phone-qr', src: phoneQrUrl(phones.pairing.expires), alt: 'Codice QR per l’app Calamaio', width: 240, height: 240, onerror: (e) => { e.target.hidden = true; } }),
        h('p', null, 'Inquadra il codice con l’app Calamaio sull’iPhone (Impostazioni › Abbina un PC). Scade tra ', h('strong', { id: 'phone-left' }, countdown(phones.pairing.expires, now)), '.'),
        h('p', { class: 'set-help' }, 'Senza fotocamera: copia il link e incollalo nell’app.'),
        h('code', { class: 'phone-link' }, phones.pairing.link),
        h('div', { class: 'card-actions' },
          h('button', { type: 'button', class: 'btn small secondary', onclick: copyLink }, 'Copia il link'),
          h('button', { type: 'button', class: 'btn small secondary', onclick: phonesCancel }, 'Annulla')));
    } else {
      pair = h('div', { class: 'card-actions' },
        h('button', { type: 'button', class: 'btn small', disabled: !!phones.app.error, onclick: phonesPair }, 'Abbina un telefono'));
    }
    box.replaceChildren(h('article', { class: 'note', 'aria-labelledby': 'phones-h' },
      h('p', { class: 'kicker' }, 'Questo PC'),
      h('h3', { id: 'phones-h' }, 'Telefoni'),
      h('p', { class: 'set-note' }, appStateText(phones.app)),
      list, pair,
      h('p', { class: 'set-help' }, 'I telefoni abbinati usano la webapp dall’app Calamaio, nella rete locale (HTTPS con certificato fissato). Non possono abbinare schede, cambiare il Wi-Fi della scheda, aggiornare il firmware né leggere i backup, e non danno mai CONSENTI a un permesso.')));
  }
  if (phones.pairing) {
    const exp = phones.pairing.expires;
    phonesTick = setInterval(() => { const el = $('phone-left'); if (el) el.textContent = countdown(exp, Date.now()); }, 1000);
  }
}

loadPhones().finally(schedulePhones);
```

- [ ] **Step 6: Rotte finte per le catture**

In `tools/webui-dev/mock-server.mjs`, nella funzione `api`, subito prima della riga `if (p === '/api/backups' || p.startsWith('/api/backups/')) {`:

```js
  // Telefoni abbinati (un telefono finto; la finestra dura 5 minuti come nel servizio).
  if (p === '/api/phones' || p.startsWith('/api/phones/')) {
    const view = () => ({ phones: mockPhones, pairing: mockPairing && mockPairing.expires > Date.now() ? mockPairing : null,
      app: { port: 8767, listening: true, fingerprint: 'ab'.repeat(32), hosts: ['192.168.1.20'] } });
    if (req.method === 'GET' && p === '/api/phones') return json(res, 200, view());
    if (req.method === 'POST' && p === '/api/phones/pair') {
      mockPairing = { link: 'calamaio://pair?v=1&h=192.168.1.20&p=8767&fp=' + 'ab'.repeat(32) + '&c=' + 'cd'.repeat(16) + '&n=PC%20finto', expires: Date.now() + 300000 };
      return json(res, 200, view());
    }
    if (req.method === 'POST' && p === '/api/phones/pair/cancel') { mockPairing = null; return json(res, 200, view()); }
    if (req.method === 'GET' && p === '/api/phones/pair/qr.svg') {
      res.writeHead(200, { 'Content-Type': 'image/svg+xml', 'Cache-Control': 'no-store' });
      return res.end('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 29 29"><rect width="29" height="29" fill="#fff"/><path d="M4 4h7v7h-7z"/></svg>');
    }
    const m = /^\/api\/phones\/([^/]+)$/.exec(p);
    if (req.method === 'DELETE' && m) {
      const n = mockPhones.length;
      mockPhones = mockPhones.filter((x) => x.id !== decodeURIComponent(m[1]));
      if (mockPhones.length === n) return json(res, 404, { error: 'telefono sconosciuto' });
      res.writeHead(204); return res.end();
    }
    return json(res, 404, { error: 'non trovato' });
  }
```

e accanto alle altre variabili di stato in cima al file (dove sono dichiarati `backups` e `clients`):

```js
let mockPhones = [{ id: 'a1b2c3d4e5f6', name: 'iPhone di Prova', paired: Date.now() - 3 * 86400000, lastUsed: Date.now() - 5 * 60000 }];
let mockPairing = null;
```

- [ ] **Step 7: Eseguire le prove della webapp e del servizio**

Run: `cd tools/webui-dev && npm test 2>&1 | tail -n 8`
Expected: nessun `FAIL`, riga `telefoni: 13 testi controllati`.

Run: `cd service && go test ./internal/webui/ ./internal/server/ 2>&1 | tail -n 5`
Expected: `ok` (`TestNoInlineInPages` conferma che nella pagina non ci sono script o stili in linea).

Facoltativo, se Playwright è installato (`ls tools/webui-dev/node_modules/playwright`): `cd tools/webui-dev && node shots.mjs 2>&1 | tail -n 10` → nessun problema nuovo nella console di `device.html`; guardare `out/device-*.png` (sezione «Telefoni» in fondo, a 390 px senza scorrimento orizzontale).

- [ ] **Step 8: Commit**

```bash
git add service/internal/webui tools/webui-dev
git commit -m "Webapp: sezione Telefoni in Dispositivo (elenco, Abbina un telefono con QR e link da copiare, conto alla rovescia, Revoca con conferma), nascosta quando /api/phones non c'è"
```

---

## Fase P2: l'app

### Task 7: Progetto iOS e prova dei corpi nello schema proprio

Primo compito di P2 (spec §3.3): una prova automatica nel simulatore iOS 27 misura che cosa riceve un `WKURLSchemeHandler` per `fetch` POST con corpo JSON, `ArrayBuffer` e `Blob` da 2 MiB, `FormData` con un file, e se un `EventSource` sullo schema proprio riceve gli eventi mentre arrivano; la pagina di prova ha la stessa CSP della webapp. L'esito, scritto in `SpikeOutcome`, decide i rami dei Task 10 e 11:

| Esito | Ramo | Cosa cambia |
|---|---|---|
| `bodiesReachHandler = true` | **A1** | nessun ponte: il gestore legge `httpBody` o `httpBodyStream` |
| `bodiesReachHandler = false` | **B1** | `FetchBridge` + `fetch-shim.js` iniettato: i `fetch` con corpo passano da `messageHandlers` (Task 10, Step 7) |
| `sseStreams = true` | **A2** | `EventSource` resta |
| `sseStreams = false` | **B2** | script iniettato `delete window.EventSource;`: la webapp passa da sola al polling ogni 2 s (`watchStatus` in `api.js`) (Task 11, Step 4) |
| violazioni CSP nel riepilogo | **C** | `Proxy.downstream` aggiunge `calamaio-app://ui` alle direttive bloccate (Task 10, Step 8) |

**Files:**
- Create: `ios/.gitignore`, `ios/project.yml`, `ios/Config/App.xcconfig`, `ios/App/CalamaioApp.swift`, `ios/CalamaioKit/Package.swift`, `ios/CalamaioKit/Sources/CalamaioKit/SpikeOutcome.swift`, `ios/AppTests/SchemeBodySpikeTests.swift`, `ios/AppTests/SpikeHandler.swift`
- Modify: `docs/DECISIONI.md` (sezione «App iPhone»)
- Test: `ios/AppTests/SchemeBodySpikeTests.swift` (nel simulatore)

**Interfaces:**
- Consumes: niente.
- Produces: progetto `ios/Calamaio.xcodeproj` generato (schema `Calamaio`, bersagli `Calamaio` e `CalamaioTests`), pacchetto `CalamaioKit` (prodotto `CalamaioKit`, iOS 27 e macOS 27), `public enum SpikeOutcome { public static let bodiesReachHandler: Bool; public static let sseStreams: Bool }`.

- [ ] **Step 1: Controllare gli strumenti**

Run: `xcodegen --version; xcodebuild -version | head -n 2; xcrun simctl list devices available | grep -c 'iPhone 18 Pro ('`
Expected: `Version: 2.45.4`, `Xcode 27.0`, almeno `1`. Se `xcodegen` manca: **chiedere all'utente** «XcodeGen non c'è: posso installarlo con `brew install xcodegen`?» e aspettare il sì prima di installarlo.

- [ ] **Step 2: Scrivere progetto, configurazione e pacchetto**

`ios/.gitignore`:

```gitignore
# Generato da project.yml con `xcodegen generate`
Calamaio.xcodeproj/
# Squadra e bundle id personali (Apple ID gratuito di chi compila)
Config/Local.xcconfig
CalamaioKit/.build/
CalamaioKit/.swiftpm/
build/
DerivedData/
*.xcresult
```

`ios/Config/App.xcconfig`:

```
// Impostazioni comuni dell'app Calamaio. Squadra e bundle id personali vanno in Config/Local.xcconfig
// (non nel repository), per esempio:
//   DEVELOPMENT_TEAM = ABCDE12345
//   CALAMAIO_BUNDLE_ID = com.mrgamingita.calamaio
CALAMAIO_BUNDLE_ID = com.mrgamingita.calamaio
DEVELOPMENT_TEAM =
#include? "Local.xcconfig"
```

`ios/project.yml`:

```yaml
# Progetto dell'app Calamaio per iPhone. Si genera con: cd ios && xcodegen generate
name: Calamaio
options:
  deploymentTarget:
    iOS: "27.0"
  createIntermediateGroups: true
  developmentLanguage: it
configFiles:
  Debug: Config/App.xcconfig
  Release: Config/App.xcconfig
settings:
  base:
    SWIFT_VERSION: "5.0"
    IPHONEOS_DEPLOYMENT_TARGET: "27.0"
    CODE_SIGN_STYLE: Automatic
packages:
  CalamaioKit:
    path: CalamaioKit
targets:
  Calamaio:
    type: application
    platform: iOS
    sources:
      - App
    dependencies:
      - package: CalamaioKit
        product: CalamaioKit
    settings:
      base:
        PRODUCT_BUNDLE_IDENTIFIER: $(CALAMAIO_BUNDLE_ID)
        PRODUCT_NAME: Calamaio
        MARKETING_VERSION: "0.1.0"
        CURRENT_PROJECT_VERSION: "1"
        TARGETED_DEVICE_FAMILY: "1"
    info:
      path: App/Info.plist
      properties:
        CFBundleDisplayName: Calamaio
        CFBundleShortVersionString: $(MARKETING_VERSION)
        CFBundleVersion: $(CURRENT_PROJECT_VERSION)
        UILaunchScreen: {}
        UIApplicationSceneManifest:
          UIApplicationSupportsMultipleScenes: false
        UISupportedInterfaceOrientations:
          - UIInterfaceOrientationPortrait
          - UIInterfaceOrientationLandscapeLeft
          - UIInterfaceOrientationLandscapeRight
        ITSAppUsesNonExemptEncryption: false
        NSLocalNetworkUsageDescription: "Calamaio cerca il servizio sul tuo PC nella rete locale per mostrarti la webapp."
        NSBonjourServices:
          - _calamaio-app._tcp
        NSCameraUsageDescription: "La fotocamera serve a inquadrare il codice QR di abbinamento mostrato dalla webapp sul PC."
        NSBluetoothAlwaysUsageDescription: "Il Bluetooth serve a usare il telefono come controller della scheda Calamaio."
        NSAppTransportSecurity:
          NSAllowsLocalNetworking: true
  CalamaioTests:
    type: bundle.unit-test
    platform: iOS
    sources:
      - AppTests
    dependencies:
      - target: Calamaio
    settings:
      base:
        GENERATE_INFOPLIST_FILE: YES
        PRODUCT_BUNDLE_IDENTIFIER: $(CALAMAIO_BUNDLE_ID).tests
schemes:
  Calamaio:
    build:
      targets:
        Calamaio: all
        CalamaioTests: [test]
    test:
      targets:
        - CalamaioTests
```

`ios/App/CalamaioApp.swift` (guscio provvisorio: il Task 11 lo sostituisce):

```swift
import SwiftUI

// App Calamaio. In questo passo solo il guscio che ospita le prove; schede, abbinamento e webapp nel Task 11.
@main
struct CalamaioApp: App {
    var body: some Scene {
        WindowGroup { Text("Calamaio") }
    }
}
```

`ios/CalamaioKit/Package.swift`:

```swift
// swift-tools-version: 6.0
// CalamaioKit: la logica dell'app iPhone, provata con `swift test` sul Mac e nel simulatore.
import PackageDescription

let package = Package(
    name: "CalamaioKit",
    platforms: [.iOS("27.0"), .macOS("27.0")],
    products: [.library(name: "CalamaioKit", targets: ["CalamaioKit"])],
    targets: [
        .target(name: "CalamaioKit"),
    ],
    swiftLanguageModes: [.v5]
)
```

`ios/CalamaioKit/Sources/CalamaioKit/SpikeOutcome.swift` (valori provvisori, li fissa lo Step 6):

```swift
// Esito della prova del Task 7 (piano app iPhone P1-P2) su iOS 27: cosa riceve un WKURLSchemeHandler.
// Lo controllano i test della prova (se iOS cambia comportamento il test fallisce) e lo leggono
// SchemeHandler/WebContainer per scegliere i rami.
public enum SpikeOutcome {
    /// fetch con corpo (JSON, ArrayBuffer, Blob, FormData) arriva intero al gestore dello schema.
    public static let bodiesReachHandler = false
    /// un EventSource sullo schema proprio riceve gli eventi mentre arrivano (non a fine flusso).
    public static let sseStreams = false
}
```

- [ ] **Step 3: Scrivere la prova**

`ios/AppTests/SpikeHandler.swift`:

```swift
import WebKit
import CryptoKit

// Gestore dello schema «spike» per la prova: serve la pagina, rimanda indietro cosa ha ricevuto
// (corpo in linea, corpo a stream, SHA-256) e manda un SSE con un evento al secondo.
final class SpikeHandler: NSObject, WKURLSchemeHandler {
    // la stessa CSP della webapp (service/internal/api/web.go)
    static let csp = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; " +
        "font-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'"
    private var stopped = Set<ObjectIdentifier>()

    static func hex(_ d: Data) -> String { SHA256.hash(data: d).map { String(format: "%02x", $0) }.joined() }

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        let path = task.request.url?.path ?? ""
        switch path {
        case "/harness.html": reply(task, "text/html; charset=utf-8", Data(SpikePage.html.utf8))
        case "/harness.js": reply(task, "text/javascript; charset=utf-8", Data(SpikePage.js.utf8))
        case "/sse": sse(task)
        default:
            if path.hasPrefix("/echo/") { echo(task) } else { reply(task, "text/plain", Data(), status: 404) }
        }
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) { stopped.insert(ObjectIdentifier(task)) }

    private func reply(_ t: WKURLSchemeTask, _ ctype: String, _ body: Data, status: Int = 200) {
        let r = HTTPURLResponse(url: t.request.url!, statusCode: status, httpVersion: "HTTP/1.1",
                                headerFields: ["Content-Type": ctype, "Content-Security-Policy": Self.csp, "Cache-Control": "no-store"])!
        t.didReceive(r)
        t.didReceive(body)
        t.didFinish()
    }

    private func echo(_ t: WKURLSchemeTask) {
        let rq = t.request
        let inline = rq.httpBody
        var streamed: Data?
        if let s = rq.httpBodyStream {
            s.open()
            var d = Data()
            var buf = [UInt8](repeating: 0, count: 64 << 10)
            while true {
                let n = s.read(&buf, maxLength: buf.count)
                if n <= 0 { break }
                d.append(buf, count: n)
            }
            s.close()
            streamed = d
        }
        let got = inline ?? streamed ?? Data()
        let info: [String: Any] = [
            "path": rq.url?.path ?? "", "method": rq.httpMethod ?? "",
            "ctype": rq.value(forHTTPHeaderField: "Content-Type") ?? "",
            "origin": rq.value(forHTTPHeaderField: "Origin") ?? "",
            "inline": inline?.count ?? -1, "stream": streamed?.count ?? -1,
            "sha": Self.hex(got),
            "multipart": got.range(of: Data("filename=\"x.gb\"".utf8)) != nil,
        ]
        reply(t, "application/json", try! JSONSerialization.data(withJSONObject: info))
    }

    // tre eventi a 0, 1 e 2 s, fine del flusso a 3 s
    private func sse(_ t: WKURLSchemeTask) {
        let id = ObjectIdentifier(t)
        t.didReceive(HTTPURLResponse(url: t.request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                                     headerFields: ["Content-Type": "text/event-stream; charset=utf-8", "Cache-Control": "no-store"])!)
        for i in 0..<4 {
            DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(1000 * i)) { [weak self] in
                guard let self, !self.stopped.contains(id) else { return }
                if i < 3 { t.didReceive(Data("event: tick\ndata: \(i)\n\n".utf8)) } else { t.didFinish() }
            }
        }
    }
}

enum SpikePage {
    static let html = """
    <!doctype html><meta charset="utf-8"><title>prova</title><script type="module" src="/harness.js"></script>
    """

    static let js = """
    const big = new Uint8Array(2 * 1024 * 1024);
    for (let i = 0; i < big.length; i++) big[i] = i & 255;
    const out = { csp: [] };
    document.addEventListener('securitypolicyviolation', (e) => out.csp.push(e.violatedDirective + ' ' + e.blockedURI));
    const post = async (path, init) => {
      try { const r = await fetch(path, Object.assign({ method: 'POST' }, init)); return await r.json(); }
      catch (e) { return { error: String(e) }; }
    };
    async function run() {
      out.json = await post('/echo/json', { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ a: 1, s: 'è' }) });
      out.buffer = await post('/echo/buffer', { headers: { 'Content-Type': 'application/octet-stream' }, body: big });
      out.blob = await post('/echo/blob', { headers: { 'Content-Type': 'application/octet-stream' }, body: new Blob([big]) });
      const fd = new FormData();
      fd.append('name', 'x');
      fd.append('rom', new File([big.subarray(0, 32768)], 'x.gb', { type: 'application/octet-stream' }));
      out.form = await post('/echo/form', { body: fd });
      out.sse = await new Promise((res) => {
        if (!('EventSource' in window)) return res({ error: 'EventSource assente' });
        const t0 = performance.now(), got = [];
        const es = new EventSource('/sse');
        const end = (x) => { es.close(); res(x); };
        es.addEventListener('tick', (e) => { got.push([Number(e.data), Math.round(performance.now() - t0)]); if (got.length === 3) end({ got }); });
        es.addEventListener('error', () => end({ error: 'error', got }));
        setTimeout(() => end({ timeout: true, got }), 8000);
      });
      webkit.messageHandlers.spike.postMessage(JSON.stringify(out));
    }
    run().catch((e) => webkit.messageHandlers.spike.postMessage(JSON.stringify({ fatal: String(e) })));
    """
}

// Riceve il risultato della pagina.
final class SpikeSink: NSObject, WKScriptMessageHandler {
    var result: String?
    var done: (() -> Void)?
    func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage) {
        result = message.body as? String
        done?()
        done = nil
    }
}
```

`ios/AppTests/SchemeBodySpikeTests.swift`:

```swift
import XCTest
import WebKit
import CalamaioKit

// Prova del Task 7: misura, non giudica. Le ultime due asserzioni confrontano la misura con SpikeOutcome.
@MainActor
final class SchemeBodySpikeTests: XCTestCase {
    func testWhatTheSchemeHandlerReceives() async throws {
        let handler = SpikeHandler()
        let sink = SpikeSink()
        let cfg = WKWebViewConfiguration()
        cfg.setURLSchemeHandler(handler, forURLScheme: "spike")
        cfg.userContentController.add(sink, name: "spike")
        cfg.websiteDataStore = .nonPersistent()
        // la WKWebView sta in una finestra visibile: fuori schermo i timer della pagina rallentano
        let scene = try XCTUnwrap(UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first)
        let window = UIWindow(windowScene: scene)
        window.frame = CGRect(x: 0, y: 0, width: 393, height: 852)
        let vc = UIViewController()
        let wv = WKWebView(frame: window.bounds, configuration: cfg)
        vc.view.addSubview(wv)
        window.rootViewController = vc
        window.makeKeyAndVisible()
        defer { window.isHidden = true }

        let exp = expectation(description: "pagina di prova")
        sink.done = { exp.fulfill() }
        wv.load(URLRequest(url: URL(string: "spike://t/harness.html")!))
        await fulfillment(of: [exp], timeout: 60)

        let raw = try XCTUnwrap(sink.result)
        print("SPIKE-RESULT " + raw)
        let att = XCTAttachment(string: raw)
        att.name = "spike.json"
        att.lifetime = .keepAlways
        add(att)
        let r = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(raw.utf8)) as? [String: Any])
        XCTAssertNil(r["fatal"], "la pagina di prova si è fermata")

        let big = Data((0..<(2 << 20)).map { UInt8($0 & 255) })
        let json = Data(#"{"a":1,"s":"è"}"#.utf8)
        func full(_ k: String, _ want: Data) -> Bool {
            guard let e = r[k] as? [String: Any] else { return false }
            let n = max(e["inline"] as? Int ?? -1, e["stream"] as? Int ?? -1)
            return n == want.count && (e["sha"] as? String) == SpikeHandler.hex(want)
        }
        let form = (r["form"] as? [String: Any]) ?? [:]
        let formOK = max(form["inline"] as? Int ?? -1, form["stream"] as? Int ?? -1) > 32768 && (form["multipart"] as? Bool ?? false)
        let bodies = full("json", json) && full("buffer", big) && full("blob", big) && formOK
        let ticks = ((r["sse"] as? [String: Any])?["got"] as? [[Int]]) ?? []
        let streams = ticks.count == 3 && ticks[0][1] < 800 && ticks[2][1] < 2800
        let csp = (r["csp"] as? [String]) ?? []
        print("SPIKE-SUMMARY corpi=\(bodies) (json \(full("json", json)) buffer \(full("buffer", big)) blob \(full("blob", big)) form \(formOK)) sse=\(streams) \(ticks) csp=\(csp)")

        XCTAssertEqual(bodies, SpikeOutcome.bodiesReachHandler, "i corpi nello schema proprio non sono come nella prova: rivedere il ponte (Task 10)")
        XCTAssertEqual(streams, SpikeOutcome.sseStreams, "l'SSE nello schema proprio non è come nella prova: rivedere EventSource (Task 11)")
    }
}
```

- [ ] **Step 4: Generare e compilare**

Run: `cd ios && xcodegen generate 2>&1 | tail -n 3 && (cd CalamaioKit && swift build 2>&1 | tail -n 3)`
Expected: `Created project at …/ios/Calamaio.xcodeproj`, `Build complete!`. `git status --short ios` mostra `App/Info.plist` (lo scrive XcodeGen dalle `properties`: si committa, così si vede cosa chiede l'app) e non mostra `Calamaio.xcodeproj` (ignorato).

Se la compilazione del bersaglio di test dice `No such module 'CalamaioKit'`, aggiungere in `project.yml` a `CalamaioTests.dependencies` la voce `- package: CalamaioKit` con `product: CalamaioKit` e `link: false` (il modulo è già dentro l'app ospite: collegarlo due volte darebbe classi doppie) e rigenerare.

- [ ] **Step 5: Eseguire la prova nel simulatore**

Run:

```bash
cd ios && xcodebuild test -project Calamaio.xcodeproj -scheme Calamaio -destination 'platform=iOS Simulator,name=iPhone 18 Pro,OS=27.0' \
  -only-testing:CalamaioTests/SchemeBodySpikeTests 2>&1 | grep -E 'SPIKE-(SUMMARY|RESULT)|error:|Test Case|TEST (SUCCEEDED|FAILED)' | cut -c1-600 | tail -n 12
```

Expected: una riga `SPIKE-SUMMARY corpi=<true|false> (…) sse=<true|false> [[0, …], [1, …], [2, …]] csp=[…]` e la riga `SPIKE-RESULT {…}` con, per ogni prova, `inline`, `stream`, `origin`. Il test **può fallire** sulle ultime due asserzioni: è la misura. Se manca `SPIKE-SUMMARY`, guardare `error:` (firma: col simulatore basta la firma locale automatica; se Xcode chiede una squadra, aggiungere `CODE_SIGNING_ALLOWED=NO` solo a questo comando).

- [ ] **Step 6: Fissare l'esito**

Copiare i due valori di `SPIKE-SUMMARY` in `SpikeOutcome.swift` (`bodiesReachHandler = <corpi>`, `sseStreams = <sse>`) e rieseguire il comando dello Step 5.
Expected: `** TEST SUCCEEDED **`.

In `docs/DECISIONI.md`, sezione «App iPhone», aggiungere:

```markdown
- **Prova dei corpi nello schema proprio** (Task 7, simulatore iOS 27.0, <data>): `fetch` POST JSON <arriva/non arriva> (`httpBody` <n>/`httpBodyStream` <n>), `ArrayBuffer` 2 MiB <…>, `Blob` 2 MiB <…>, `FormData` con file <…>; `Origin` mandato: «<valore>»; `EventSource`: <eventi a 0/1/2 s | tutti a fine flusso | errore>; CSP: <nessuna violazione | elenco>. Ramo scelto: **<A1|B1>** per i corpi, **<A2|B2>** per l'SSE<, C per la CSP>. `SchemeBodySpikeTests` resta come prova di regressione (fallisce se iOS cambia comportamento).
```

(riempire ogni `<…>` con i numeri della riga `SPIKE-RESULT`).

- [ ] **Step 7: Commit**

```bash
git add ios docs/DECISIONI.md
git commit -m "App iPhone: progetto XcodeGen (iOS 27, chiavi di rete locale, Bonjour, fotocamera, Bluetooth), pacchetto CalamaioKit e prova dei corpi e dell'SSE nello schema proprio"
```

---

### Task 8: Link del QR, impronta fissata e client del PC

**Files:**
- Create: `ios/CalamaioKit/Sources/CalamaioKit/PairingLink.swift`, `Pinning.swift`, `PCClient.swift`, `Texts.swift`; `ios/CalamaioKit/Tests/CalamaioKitTests/Vectors.swift`, `PairingLinkTests.swift`, `PinTests.swift`, `E2ETests.swift`; `ios/scripts/e2e.sh`
- Modify: `ios/CalamaioKit/Package.swift` (bersaglio di test)
- Test: `cd ios/CalamaioKit && swift test`; `ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E`

**Interfaces:**
- Consumes: `protocol/vectors/app-pairing.json` (Task 2), `POST /app/pair`, `GET /app/whoami`, `serve --loopback` (Task 3-5).
- Produces:
  - `public struct PairingLink: Equatable, Sendable { hosts: [String]; port: Int; fingerprint: String; code: String; name: String }`, `static func parse(_ text: String) throws -> PairingLink`, `enum ParseError { notCalamaio, version, hosts, port, fingerprint, code }`, `static func isLowerHex(_ s: String, _ n: Int) -> Bool`;
  - `public enum Pin { static func fingerprint(der: Data) -> String; static func matches(der: Data, pin: String) -> Bool; static func leafDER(_ trust: SecTrust) -> Data? }`;
  - `public class PinnedDelegate: NSObject, URLSessionDelegate` (`init(pin:)`, `mismatch: Bool`, `reset()`), sottoclassabile (Task 10);
  - `public struct WhoAmI: Decodable { phone: {id, name}; pc: {name, version}; hosts: [String]; port: Int }`;
  - `public enum PCError: Error, Equatable { pinMismatch, unauthorized, pairingRejected(String), unreachable, badResponse(Int) }`;
  - `public final class PCClient { init(pin: String, timeout: TimeInterval = 4); func pair(host:port:code:name:oldToken:) async throws -> String; func whoami(host:port:token:) async throws -> WhoAmI }`;
  - `public enum Texts` (testi dell'app);
  - `ios/scripts/e2e.sh <comando…>`: servizio vero in locale (porte 18765/18766/18767, `--fake --no-mdns --loopback`, dati temporanei), finestra aperta, `CALAMAIO_E2E_LINK` e `TEST_RUNNER_CALAMAIO_E2E_LINK` nell'ambiente del comando.

- [ ] **Step 1: Bersaglio di test e test che falliscono**

In `ios/CalamaioKit/Package.swift` sostituire `targets:` con:

```swift
    targets: [
        .target(name: "CalamaioKit"),
        .testTarget(name: "CalamaioKitTests", dependencies: ["CalamaioKit"]),
    ],
```

`ios/CalamaioKit/Tests/CalamaioKitTests/Vectors.swift`:

```swift
import Foundation
import XCTest

// protocol/vectors/app-pairing.json, scritti dal servizio (service/internal/phones, TestAppPairingVectors).
struct Vectors: Decodable {
    struct Cert: Decodable { let der_b64: String; let sha256: String }
    struct Link: Decodable { let hosts: [String]; let port: Int; let code: String; let name: String; let uri: String }
    let cert: Cert
    let link: Link

    static func load() throws -> Vectors {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() } // …/ios/CalamaioKit/Tests/CalamaioKitTests/Vectors.swift → radice
        let data = try Data(contentsOf: url.appendingPathComponent("protocol/vectors/app-pairing.json"))
        return try JSONDecoder().decode(Vectors.self, from: data)
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/PairingLinkTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

final class PairingLinkTests: XCTestCase {
    func testParsesTheServiceLink() throws {
        let v = try Vectors.load()
        let l = try PairingLink.parse("  " + v.link.uri + "\n")
        XCTAssertEqual(l.hosts, v.link.hosts)
        XCTAssertEqual(l.port, v.link.port)
        XCTAssertEqual(l.fingerprint, v.cert.sha256)
        XCTAssertEqual(l.code, v.link.code)
        XCTAssertEqual(l.name, v.link.name) // «Mac di Prova & C.»: %20 e %26 decodificati
    }

    func testRejectsBadLinks() throws {
        let v = try Vectors.load()
        let good = v.link.uri
        let fp = v.cert.sha256
        let bad: [(String, PairingLink.ParseError)] = [
            (good.replacingOccurrences(of: "calamaio://", with: "https://"), .notCalamaio),
            ("calamaio://other?v=1", .notCalamaio),
            ("", .notCalamaio),
            ("non un link", .notCalamaio),
            (good.replacingOccurrences(of: "v=1", with: "v=2"), .version),
            (good.replacingOccurrences(of: "h=192.168.1.20,10.0.0.7", with: "h="), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=evil.example/x"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=[::1]"), .hosts),
            (good.replacingOccurrences(of: "h=192.168.1.20", with: "h=1.1.1.1,2.2.2.2,3.3.3.3,4.4.4.4,5.5.5.5"), .hosts),
            (good.replacingOccurrences(of: "p=8767", with: "p=0"), .port),
            (good.replacingOccurrences(of: "p=8767", with: "p=70000"), .port),
            (good.replacingOccurrences(of: fp, with: fp.uppercased()), .fingerprint),
            (good.replacingOccurrences(of: fp, with: String(fp.dropLast())), .fingerprint),
            (good.replacingOccurrences(of: "&c=", with: "&x="), .code),
        ]
        for (text, want) in bad {
            XCTAssertThrowsError(try PairingLink.parse(text), text) { err in
                XCTAssertEqual(err as? PairingLink.ParseError, want, text)
            }
        }
    }

    func testEmptyNameBecomesPC() throws {
        let v = try Vectors.load()
        let l = try PairingLink.parse(v.link.uri.components(separatedBy: "&n=")[0])
        XCTAssertEqual(l.name, "PC")
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/PinTests.swift`:

```swift
import XCTest
import Security
@testable import CalamaioKit

final class PinTests: XCTestCase {
    func testFingerprintMatchesService() throws {
        let v = try Vectors.load()
        let der = try XCTUnwrap(Data(base64Encoded: v.cert.der_b64))
        XCTAssertEqual(Pin.fingerprint(der: der), v.cert.sha256)
        XCTAssertTrue(Pin.matches(der: der, pin: v.cert.sha256))
        XCTAssertFalse(Pin.matches(der: der, pin: String(repeating: "0", count: 64)))
        XCTAssertFalse(Pin.matches(der: der, pin: ""))
        var other = der
        other[other.count - 1] ^= 1
        XCTAssertFalse(Pin.matches(der: other, pin: v.cert.sha256))
    }

    func testLeafFromTrust() throws {
        let v = try Vectors.load()
        let der = try XCTUnwrap(Data(base64Encoded: v.cert.der_b64))
        let cert = try XCTUnwrap(SecCertificateCreateWithData(nil, der as CFData))
        var trust: SecTrust?
        XCTAssertEqual(SecTrustCreateWithCertificates(cert, SecPolicyCreateBasicX509(), &trust), errSecSuccess)
        XCTAssertEqual(Pin.leafDER(try XCTUnwrap(trust)), der)
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/E2ETests.swift`:

```swift
import XCTest
@testable import CalamaioKit

// Col servizio vero: ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E
// (senza lo script questi test si saltano).
enum E2E {
    static var cached: (link: PairingLink, token: String)?

    // Un abbinamento per esecuzione (il codice è monouso): i test lo condividono.
    static func session() async throws -> (link: PairingLink, token: String) {
        if let c = cached { return c }
        guard let raw = ProcessInfo.processInfo.environment["CALAMAIO_E2E_LINK"] else {
            throw XCTSkip("prova col servizio vero: ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E")
        }
        let l = try PairingLink.parse(raw)
        let t = try await PCClient(pin: l.fingerprint).pair(host: l.hosts[0], port: l.port, code: l.code, name: "Prova e2e")
        cached = (l, t)
        return (l, t)
    }
}

final class E2ETests: XCTestCase {
    func testPairAndPinAgainstRealService() async throws {
        let (l, token) = try await E2E.session()
        let host = l.hosts[0]
        let client = PCClient(pin: l.fingerprint)
        let me = try await client.whoami(host: host, port: l.port, token: token)
        XCTAssertEqual(me.phone.name, "Prova e2e")
        XCTAssertEqual(me.port, l.port)
        XCTAssertEqual(me.hosts, ["127.0.0.1"])
        do { _ = try await client.whoami(host: host, port: l.port, token: String(repeating: "0", count: 64)); XCTFail("token falso accettato") }
        catch PCError.unauthorized {}
        do { _ = try await PCClient(pin: String(repeating: "0", count: 64)).whoami(host: host, port: l.port, token: token); XCTFail("impronta sbagliata accettata") }
        catch PCError.pinMismatch {}
        do { _ = try await client.pair(host: host, port: l.port, code: l.code, name: "doppione"); XCTFail("codice riusato accettato") }
        catch PCError.pairingRejected {}
        do { _ = try await client.whoami(host: "127.0.0.1", port: 1, token: token); XCTFail("porta chiusa") }
        catch PCError.unreachable {}
    }
}
```

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 15`
Expected: FAIL di compilazione (`cannot find 'PairingLink' in scope`).

- [ ] **Step 2: Scrivere `PairingLink.swift`, `Pinning.swift`, `Texts.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/PairingLink.swift`:

```swift
import Foundation

// Link del QR di abbinamento (spec §3.2, phones.Link nel servizio):
// calamaio://pair?v=1&h=<ip1,ip2>&p=8767&fp=<64 cifre>&c=<32 cifre>&n=<nome PC>
public struct PairingLink: Equatable, Sendable {
    public let hosts: [String]
    public let port: Int
    public let fingerprint: String // SHA-256 del certificato del PC, 64 cifre esadecimali minuscole
    public let code: String        // codice monouso, 32 cifre esadecimali minuscole
    public let name: String

    public enum ParseError: Error, Equatable { case notCalamaio, version, hosts, port, fingerprint, code }

    public static func parse(_ text: String) throws -> PairingLink {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let c = URLComponents(string: t), c.scheme == "calamaio", c.host == "pair" else { throw ParseError.notCalamaio }
        var q: [String: String] = [:]
        for it in c.queryItems ?? [] where q[it.name] == nil { q[it.name] = it.value ?? "" }
        guard q["v"] == "1" else { throw ParseError.version }
        let hosts = (q["h"] ?? "").split(separator: ",").map(String.init)
        guard (1...4).contains(hosts.count), hosts.allSatisfy(isHost) else { throw ParseError.hosts }
        guard let p = Int(q["p"] ?? ""), (1...65535).contains(p) else { throw ParseError.port }
        guard let fp = q["fp"], isLowerHex(fp, 64) else { throw ParseError.fingerprint }
        guard let code = q["c"], isLowerHex(code, 32) else { throw ParseError.code }
        let name = String((q["n"] ?? "").trimmingCharacters(in: .whitespacesAndNewlines).prefix(64))
        return PairingLink(hosts: hosts, port: p, fingerprint: fp, code: code, name: name.isEmpty ? "PC" : name)
    }

    public static func isLowerHex(_ s: String, _ n: Int) -> Bool {
        s.utf8.count == n && s.utf8.allSatisfy { ($0 >= 48 && $0 <= 57) || ($0 >= 97 && $0 <= 102) }
    }

    // IPv4 o nome semplice (lettere, cifre, «-» e «.»): niente porte, niente IPv6, niente percorsi
    static func isHost(_ s: String) -> Bool {
        !s.isEmpty && s.utf8.count <= 253 && s.utf8.allSatisfy {
            ($0 >= 48 && $0 <= 57) || ($0 >= 65 && $0 <= 90) || ($0 >= 97 && $0 <= 122) || $0 == 45 || $0 == 46
        }
    }
}
```

Il limite di 4 indirizzi è lo stesso di `phones.LocalIPv4s` nel servizio.

`ios/CalamaioKit/Sources/CalamaioKit/Pinning.swift`:

```swift
import Foundation
import CryptoKit
import Security

public enum Pin {
    // SHA-256 del certificato (DER) in esadecimale minuscolo, come phones.Fingerprint nel servizio.
    public static func fingerprint(der: Data) -> String {
        SHA256.hash(data: der).map { String(format: "%02x", $0) }.joined()
    }

    // Confronto senza uscite anticipate.
    public static func matches(der: Data, pin: String) -> Bool {
        let a = Array(fingerprint(der: der).utf8), b = Array(pin.lowercased().utf8)
        guard a.count == b.count else { return false }
        var d: UInt8 = 0
        for i in 0..<a.count { d |= a[i] ^ b[i] }
        return d == 0
    }

    // Il certificato foglia del server.
    public static func leafDER(_ trust: SecTrust) -> Data? {
        guard let chain = SecTrustCopyCertificateChain(trust) as? [SecCertificate], let leaf = chain.first else { return nil }
        return SecCertificateCopyData(leaf) as Data
    }
}

// Delegato delle sessioni verso il PC: accetta solo il certificato con l'impronta abbinata. Nessuna autorità di
// certificazione, nessuna eccezione TLS generica. `mismatch` dice se l'ultimo errore era l'impronta.
public class PinnedDelegate: NSObject, URLSessionDelegate, @unchecked Sendable {
    public let pin: String
    private let lock = NSLock()
    private var sawMismatch = false

    public init(pin: String) { self.pin = pin.lowercased() }

    public var mismatch: Bool { lock.lock(); defer { lock.unlock() }; return sawMismatch }
    public func reset() { lock.lock(); sawMismatch = false; lock.unlock() }

    public func urlSession(_ session: URLSession, didReceive challenge: URLAuthenticationChallenge,
                           completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        guard challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust,
              let trust = challenge.protectionSpace.serverTrust else {
            completionHandler(.performDefaultHandling, nil)
            return
        }
        if let der = Pin.leafDER(trust), Pin.matches(der: der, pin: pin) {
            completionHandler(.useCredential, URLCredential(trust: trust))
        } else {
            lock.lock(); sawMismatch = true; lock.unlock()
            completionHandler(.cancelAuthenticationChallenge, nil)
        }
    }
}
```

`ios/CalamaioKit/Sources/CalamaioKit/Texts.swift`:

```swift
// Testi dell'app, in italiano. Nuovi rispetto al simulatore della scheda (che non ha schermate dell'app):
// elenco in DECISIONI, «App iPhone».
public enum Texts {
    public static let notFound = "PC non trovato: telefono e PC devono essere sulla stessa rete"
    public static let revoked = "Il PC ha revocato questo telefono: abbinalo di nuovo da Impostazioni."
    public static let certChanged = "Il PC ha un certificato diverso da quello abbinato: abbina di nuovo il telefono."
    public static let badLink = "Questo codice non è un abbinamento di Calamaio."
    public static let pairPinMismatch = "Il certificato del PC non corrisponde al codice: apri un abbinamento nuovo dalla webapp e riprova."
    public static let pairRejected = "Abbinamento rifiutato dal PC: "
    public static let keychain = "Il token non si è salvato nel Portachiavi: riprova."
    public static let notPaired = "Nessun PC abbinato. Sul PC apri la webapp, Dispositivo › Telefoni › «Abbina un telefono», e inquadra il codice."
    public static let searching = "Cerco il PC…"
    public static let pcUnreachable = "PC non raggiungibile"
    public static let tooLarge = "File troppo grande per l'app (al massimo 16 MiB)."
}
```

- [ ] **Step 3: Scrivere `PCClient.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/PCClient.swift`:

```swift
import Foundation

public struct WhoAmI: Decodable, Equatable, Sendable {
    public struct Phone: Decodable, Equatable, Sendable { public let id: String; public let name: String }
    public struct PC: Decodable, Equatable, Sendable { public let name: String; public let version: String }
    public let phone: Phone
    public let pc: PC
    public let hosts: [String]
    public let port: Int
}

public enum PCError: Error, Equatable {
    case pinMismatch              // il PC risponde con un certificato diverso da quello abbinato
    case unauthorized             // 401: token revocato o sconosciuto
    case pairingRejected(String)  // /app/pair rifiutato (codice sbagliato, finestra chiusa)
    case unreachable              // nessuna risposta (rete, porta chiusa, tempo scaduto)
    case badResponse(Int)
}

// Richieste native al PC (abbinamento e prova del collegamento), con l'impronta fissata. Il token passa solo da qui
// e dal gestore dello schema: mai dal JavaScript.
public final class PCClient: @unchecked Sendable {
    let delegate: PinnedDelegate
    let session: URLSession

    public init(pin: String, timeout: TimeInterval = 4) {
        delegate = PinnedDelegate(pin: pin)
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = timeout
        cfg.timeoutIntervalForResource = timeout * 2
        cfg.waitsForConnectivity = false
        cfg.urlCache = nil
        cfg.httpCookieStorage = nil
        session = URLSession(configuration: cfg, delegate: delegate, delegateQueue: nil)
    }

    deinit { session.invalidateAndCancel() }

    static func url(_ host: String, _ port: Int, _ path: String) -> URL {
        var c = URLComponents()
        c.scheme = "https"
        c.host = host
        c.port = port
        c.path = path
        return c.url!
    }

    // POST /app/pair → token. oldToken: quello di prima per lo stesso PC (il servizio revoca il telefono vecchio).
    public func pair(host: String, port: Int, code: String, name: String, oldToken: String? = nil) async throws -> String {
        var rq = URLRequest(url: Self.url(host, port, "/app/pair"))
        rq.httpMethod = "POST"
        rq.setValue("application/json", forHTTPHeaderField: "Content-Type")
        if let o = oldToken { rq.setValue("Bearer " + o, forHTTPHeaderField: "Authorization") }
        rq.httpBody = try JSONSerialization.data(withJSONObject: ["code": code, "name": name])
        let (data, status) = try await send(rq)
        guard status == 200 else { throw PCError.pairingRejected(Self.errorText(data) ?? "HTTP \(status)") }
        struct R: Decodable { let token: String }
        guard let r = try? JSONDecoder().decode(R.self, from: data), PairingLink.isLowerHex(r.token, 64) else {
            throw PCError.badResponse(status)
        }
        return r.token
    }

    public func whoami(host: String, port: Int, token: String) async throws -> WhoAmI {
        var rq = URLRequest(url: Self.url(host, port, "/app/whoami"))
        rq.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
        let (data, status) = try await send(rq)
        guard status == 200, let me = try? JSONDecoder().decode(WhoAmI.self, from: data) else { throw PCError.badResponse(status) }
        return me
    }

    func send(_ rq: URLRequest) async throws -> (Data, Int) {
        delegate.reset()
        let data: Data
        let resp: URLResponse
        do {
            (data, resp) = try await session.data(for: rq)
        } catch {
            throw delegate.mismatch ? PCError.pinMismatch : PCError.unreachable
        }
        guard let h = resp as? HTTPURLResponse else { throw PCError.badResponse(0) }
        if h.statusCode == 401 { throw PCError.unauthorized }
        return (data, h.statusCode)
    }

    static func errorText(_ d: Data) -> String? {
        (try? JSONSerialization.jsonObject(with: d) as? [String: Any])?["error"] as? String
    }
}
```

- [ ] **Step 4: Eseguire i test senza servizio**

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 15`
Expected: `PairingLinkTests` e `PinTests` passano; `E2ETests.testPairAndPinAgainstRealService` saltato (`skipped`). Nessun fallimento.

- [ ] **Step 5: Script del servizio vero**

`ios/scripts/e2e.sh` (eseguibile: `chmod +x ios/scripts/e2e.sh`):

```bash
#!/bin/bash
# Servizio vero in locale per le prove dell'app (nessuna scheda): dati e config in una cartella temporanea,
# porte 18765/18766/18767, solo 127.0.0.1 (--loopback), stato finto, niente mDNS. Apre una finestra di
# abbinamento e lancia il comando con CALAMAIO_E2E_LINK (e TEST_RUNNER_CALAMAIO_E2E_LINK per xcodebuild).
#   ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/calamaio-e2e.XXXXXX")
export CALAMAIO_CONFIG_DIR="$TMP/config" CALAMAIO_DATA_DIR="$TMP/data"
export CALAMAIO_API_PORT=18765 CALAMAIO_WS_PORT=18766 CALAMAIO_APP_PORT=18767
(cd "$ROOT/service" && go build -o "$TMP/calamaio" ./cmd/calamaio)
"$TMP/calamaio" serve --fake --no-mdns --loopback >"$TMP/serve.log" 2>&1 &
PID=$!
cleanup() { kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -rf "$TMP"; }
trap cleanup EXIT
for _ in $(seq 1 100); do
  curl -fsS "http://127.0.0.1:18765/api/status" >/dev/null 2>&1 && break
  sleep 0.1
done
LINK=$(curl -fsS -X POST "http://127.0.0.1:18765/api/phones/pair" -H 'Content-Type: application/json' \
  -H 'Origin: http://127.0.0.1:18765' -H 'Sec-Fetch-Site: same-origin' -d '{}' |
  python3 -I -c 'import json,sys; print(json.load(sys.stdin)["pairing"]["link"])')
echo "e2e: servizio su 127.0.0.1:18765, finestra aperta (${LINK:0:40}…)" >&2
export CALAMAIO_E2E_LINK="$LINK" TEST_RUNNER_CALAMAIO_E2E_LINK="$LINK"
set +e
"$@"
STATUS=$?
set -e
if [ "$STATUS" -ne 0 ]; then echo "e2e: registro del servizio:" >&2; tail -n 20 "$TMP/serve.log" >&2; fi
exit "$STATUS"
```

- [ ] **Step 6: Eseguire la prova end-to-end**

Run: `ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E 2>&1 | tail -n 12`
Expected: `testPairAndPinAgainstRealService` **passed** (non saltato). Se fallisce con `unreachable` sul primo `pair`, guardare il registro stampato dallo script (porta 18767 occupata da un'altra prova?).

- [ ] **Step 7: Commit**

```bash
git add ios/CalamaioKit ios/scripts/e2e.sh
git commit -m "App iPhone: lettura del link del QR, impronta fissata sui vettori del servizio, client del PC (abbinamento, whoami) e prova end-to-end col servizio vero"
```

---

### Task 9: Portachiavi, PC abbinato, ricerca del PC e controllore dell'app

**Files:**
- Create: `ios/CalamaioKit/Sources/CalamaioKit/SecretStore.swift`, `PairingStore.swift`, `Locator.swift`, `BonjourScanner.swift`, `AppController.swift`; `ios/CalamaioKit/Tests/CalamaioKitTests/LocatorTests.swift`, `PairingStoreTests.swift`, `AppControllerTests.swift`; `ios/AppTests/KeychainStoreTests.swift`
- Test: `cd ios/CalamaioKit && swift test`; Portachiavi vero nel simulatore con `xcodebuild test … -only-testing:CalamaioTests/KeychainStoreTests`

**Interfaces:**
- Consumes: `PairingLink`, `PCClient`, `PCError`, `WhoAmI`, `Texts` (Task 8).
- Produces:
  - `public protocol SecretStore { func read(_ key: String) throws -> String?; func write(_ key: String, _ value: String) throws; func delete(_ key: String) throws }`, `MemorySecretStore`, `KeychainStore(service: String)` (`kSecClassGenericPassword`, `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`, `kSecUseDataProtectionKeychain`), `KeychainError(status: OSStatus)`;
  - `public struct PairedPC: Codable, Equatable { name; fingerprint; hosts: [String]; port: Int; lastHost: String? }`;
  - `public final class PairingStore { init(defaults: UserDefaults, secrets: SecretStore); load() -> (PairedPC, String)?; save(_ pc: PairedPC, token: String) throws; update(_ pc: PairedPC); forget() }`;
  - `public struct Endpoint: Hashable { host: String; port: Int }`, `public struct BonjourHit: Equatable { host; port; fpPrefix }`, `public enum Locator { static func candidates(pc: PairedPC, bonjour: [BonjourHit]) -> [Endpoint] }`;
  - `public enum Reach: Equatable { ready(Endpoint), notFound, revoked, certChanged }`, `public enum Connector { static func connect(pc:token:hits:probe:) async -> (Reach, PairedPC) }`;
  - `public final class BonjourScanner { func scan(timeout: TimeInterval = 1.5) async -> [BonjourHit] }`;
  - `@MainActor public final class AppController: ObservableObject` con `phase: Phase` (`unpaired`, `connecting`, `ready(Endpoint)`, `failed(String)`), `pc: PairedPC?`, `token: String?`, `upstream: Upstream?`, `connect() async`, `pair(with text: String) async -> String?` (messaggio d'errore, `nil` = fatto), `forget()`; `public struct Upstream: Equatable { endpoint: Endpoint; token: String }`; `AppController.Deps` (iniettabile) e `Deps.live(deviceName:)`.

- [ ] **Step 1: Scrivere i test che falliscono**

`ios/CalamaioKit/Tests/CalamaioKitTests/LocatorTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

let fpA = String(repeating: "a", count: 64)
let fpB = String(repeating: "b", count: 64)

func pcA(lastHost: String? = "192.168.1.20") -> PairedPC {
    PairedPC(name: "MacBook", fingerprint: fpA, hosts: ["192.168.1.20", "10.0.0.7"], port: 8767, lastHost: lastHost)
}

func who(_ hosts: [String]) -> WhoAmI {
    WhoAmI(phone: .init(id: "p1", name: "iPhone"), pc: .init(name: "MacBook", version: "1.0"), hosts: hosts, port: 8767)
}

final class LocatorTests: XCTestCase {
    func testCandidatesOrderAndFilter() {
        let hits = [
            BonjourHit(host: "192.168.1.99", port: 8767, fpPrefix: String(fpB.prefix(16))), // un altro PC Calamaio
            BonjourHit(host: "192.168.1.50", port: 9767, fpPrefix: String(fpA.prefix(16))), // il nostro, IP nuovo
            BonjourHit(host: "192.168.1.20", port: 8767, fpPrefix: String(fpA.prefix(16))), // doppione
        ]
        let c = Locator.candidates(pc: pcA(), bonjour: hits)
        XCTAssertEqual(c, [
            Endpoint(host: "192.168.1.20", port: 8767), // l'ultimo buono
            Endpoint(host: "192.168.1.50", port: 9767), // Bonjour con la nostra impronta
            Endpoint(host: "10.0.0.7", port: 8767),     // gli indirizzi conosciuti
        ])
        XCTAssertEqual(Locator.candidates(pc: pcA(lastHost: nil), bonjour: []).map(\.host), ["192.168.1.20", "10.0.0.7"])
    }

    func testConnectFallsBackAndRemembersHosts() async {
        var tried: [String] = []
        let (reach, updated) = await Connector.connect(pc: pcA(), token: "t", hits: []) { ep in
            tried.append(ep.host)
            if ep.host == "10.0.0.7" { return who(["10.0.0.7", "172.20.0.3"]) }
            throw PCError.unreachable
        }
        XCTAssertEqual(tried, ["192.168.1.20", "10.0.0.7"])
        XCTAssertEqual(reach, .ready(Endpoint(host: "10.0.0.7", port: 8767)))
        XCTAssertEqual(updated.lastHost, "10.0.0.7")
        XCTAssertEqual(updated.hosts, ["10.0.0.7", "172.20.0.3"]) // gli indirizzi di adesso, per il prossimo cambio di rete
    }

    func testConnectReportsCertChanged() async {
        let (reach, updated) = await Connector.connect(pc: pcA(), token: "t", hits: []) { ep in
            if ep.host == "192.168.1.20" { throw PCError.pinMismatch }
            throw PCError.unreachable
        }
        XCTAssertEqual(reach, .certChanged)
        XCTAssertEqual(updated, pcA())
    }

    func testConnectStopsOnRevoked() async {
        var tried = 0
        let (reach, _) = await Connector.connect(pc: pcA(), token: "t", hits: []) { _ in tried += 1; throw PCError.unauthorized }
        XCTAssertEqual(reach, .revoked)
        XCTAssertEqual(tried, 1)
    }

    func testConnectNotFound() async {
        let (reach, _) = await Connector.connect(pc: pcA(), token: "t", hits: []) { _ in throw PCError.unreachable }
        XCTAssertEqual(reach, .notFound)
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/PairingStoreTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

final class PairingStoreTests: XCTestCase {
    func testSaveLoadUpdateForget() throws {
        let defaults = try XCTUnwrap(UserDefaults(suiteName: "calamaio-test-\(UUID().uuidString)"))
        let secrets = MemorySecretStore()
        let st = PairingStore(defaults: defaults, secrets: secrets)
        XCTAssertNil(st.load())
        let tok = String(repeating: "c", count: 64)
        try st.save(pcA(), token: tok)
        let (pc, t) = try XCTUnwrap(st.load())
        XCTAssertEqual(pc, pcA())
        XCTAssertEqual(t, tok)
        // il token non sta nelle preferenze, solo nel segreto
        let plist = defaults.dictionaryRepresentation().description
        XCTAssertFalse(plist.contains(tok))
        var moved = pcA()
        moved.lastHost = "10.0.0.7"
        st.update(moved)
        XCTAssertEqual(st.load()?.0.lastHost, "10.0.0.7")
        st.forget()
        XCTAssertNil(st.load())
        XCTAssertNil(try secrets.read(PairingStore.tokenKey))
    }

    func testMissingTokenMeansUnpaired() throws {
        let defaults = try XCTUnwrap(UserDefaults(suiteName: "calamaio-test-\(UUID().uuidString)"))
        let secrets = MemorySecretStore()
        let st = PairingStore(defaults: defaults, secrets: secrets)
        try st.save(pcA(), token: String(repeating: "c", count: 64))
        try secrets.delete(PairingStore.tokenKey) // per esempio un ripristino del telefono senza Portachiavi
        XCTAssertNil(st.load())
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/AppControllerTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

@MainActor
final class AppControllerTests: XCTestCase {
    var pairCalls: [(host: String, pin: String, code: String, old: String?)] = []
    var probe: (Endpoint) throws -> WhoAmI = { _ in who(["127.0.0.1"]) }
    var pairResult: (String) throws -> String = { _ in String(repeating: "d", count: 64) }

    func makeController(defaults: UserDefaults? = nil, secrets: SecretStore = MemorySecretStore()) -> AppController {
        let d = defaults ?? UserDefaults(suiteName: "calamaio-test-\(UUID().uuidString)")!
        let deps = AppController.Deps(
            pair: { [unowned self] host, _, pin, code, _, old in
                self.pairCalls.append((host, pin, code, old))
                return try self.pairResult(host)
            },
            whoami: { [unowned self] ep, _, _ in try self.probe(ep) },
            scan: { [] },
            deviceName: { "iPhone di prova" })
        return AppController(store: PairingStore(defaults: d, secrets: secrets), deps: deps)
    }

    func link(hosts: String = "127.0.0.1", fp: String = fpA) -> String {
        "calamaio://pair?v=1&h=\(hosts)&p=8767&fp=\(fp)&c=\(String(repeating: "1", count: 32))&n=MacBook"
    }

    func testPairFromLinkThenReady() async {
        let c = makeController()
        XCTAssertEqual(c.phase, .unpaired)
        let err = await c.pair(with: link())
        XCTAssertNil(err)
        XCTAssertEqual(c.phase, .ready(Endpoint(host: "127.0.0.1", port: 8767)))
        XCTAssertEqual(c.upstream, Upstream(endpoint: Endpoint(host: "127.0.0.1", port: 8767), token: String(repeating: "d", count: 64)))
        XCTAssertEqual(c.pc?.name, "MacBook")
        XCTAssertNil(pairCalls.first?.old)
    }

    func testBadLinkAndUnreachable() async {
        let c = makeController()
        let bad = await c.pair(with: "https://example.com")
        XCTAssertEqual(bad, Texts.badLink)
        pairResult = { _ in throw PCError.unreachable }
        let unreachable = await c.pair(with: link(hosts: "192.168.1.20,10.0.0.7"))
        XCTAssertEqual(unreachable, Texts.notFound)
        XCTAssertEqual(pairCalls.map(\.host), ["192.168.1.20", "10.0.0.7"]) // prova ogni indirizzo del QR
        XCTAssertEqual(c.phase, .unpaired)
    }

    func testPairPinMismatchAndRejected() async {
        let c = makeController()
        pairResult = { _ in throw PCError.pinMismatch }
        let mismatch = await c.pair(with: link())
        XCTAssertEqual(mismatch, Texts.pairPinMismatch)
        pairResult = { _ in throw PCError.pairingRejected("codice sbagliato") }
        let rejected = await c.pair(with: link())
        XCTAssertEqual(rejected, Texts.pairRejected + "codice sbagliato")
    }

    func testRepairSamePCSendsOldToken() async {
        let c = makeController()
        _ = await c.pair(with: link())
        pairResult = { _ in String(repeating: "e", count: 64) }
        _ = await c.pair(with: link())
        XCTAssertEqual(pairCalls.last?.old, String(repeating: "d", count: 64))
        _ = await c.pair(with: link(fp: fpB)) // un altro PC: niente token vecchio
        XCTAssertNil(pairCalls.last?.old)
    }

    func testStartupStatesAndForget() async throws {
        let d = try XCTUnwrap(UserDefaults(suiteName: "calamaio-test-\(UUID().uuidString)"))
        let secrets = MemorySecretStore()
        _ = await makeController(defaults: d, secrets: secrets).pair(with: link())
        let c = makeController(defaults: d, secrets: secrets) // riavvio dell'app
        XCTAssertEqual(c.phase, .connecting)
        probe = { _ in throw PCError.unauthorized }
        await c.connect()
        XCTAssertEqual(c.phase, .failed(Texts.revoked))
        XCTAssertNil(c.upstream)
        probe = { _ in throw PCError.pinMismatch }
        await c.connect()
        XCTAssertEqual(c.phase, .failed(Texts.certChanged))
        c.forget()
        XCTAssertEqual(c.phase, .unpaired)
        XCTAssertNil(makeController(defaults: d, secrets: secrets).pc)
    }
}
```

`ios/AppTests/KeychainStoreTests.swift` (Portachiavi vero, solo nel simulatore):

```swift
import XCTest
import CalamaioKit

final class KeychainStoreTests: XCTestCase {
    func testWriteReadOverwriteDelete() throws {
        let ks = KeychainStore(service: "com.mrgamingita.calamaio.tests.\(UUID().uuidString)")
        XCTAssertNil(try ks.read("token"))
        try ks.write("token", "uno")
        XCTAssertEqual(try ks.read("token"), "uno")
        try ks.write("token", "due") // sovrascrive, niente doppioni
        XCTAssertEqual(try ks.read("token"), "due")
        try ks.delete("token")
        XCTAssertNil(try ks.read("token"))
        try ks.delete("token") // cancellare ciò che non c'è non è un errore
    }
}
```

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 10`
Expected: FAIL di compilazione (`cannot find 'PairedPC' in scope`).

- [ ] **Step 2: Scrivere `SecretStore.swift` e `PairingStore.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/SecretStore.swift`:

```swift
import Foundation
import Security

// Segreti dell'app (il token del PC). Nell'app il Portachiavi; nei test un dizionario.
public protocol SecretStore: Sendable {
    func read(_ key: String) throws -> String?
    func write(_ key: String, _ value: String) throws
    func delete(_ key: String) throws
}

public final class MemorySecretStore: SecretStore, @unchecked Sendable {
    private let lock = NSLock()
    private var items: [String: String] = [:]
    public init() {}
    public func read(_ key: String) throws -> String? { lock.lock(); defer { lock.unlock() }; return items[key] }
    public func write(_ key: String, _ value: String) throws { lock.lock(); items[key] = value; lock.unlock() }
    public func delete(_ key: String) throws { lock.lock(); items[key] = nil; lock.unlock() }
}

public struct KeychainError: Error, Equatable { public let status: OSStatus }

// Portachiavi: solo questo dispositivo, leggibile dopo il primo sblocco (spec §3.2 punto 4).
public struct KeychainStore: SecretStore {
    public let service: String
    public init(service: String) { self.service = service }

    private func base(_ key: String) -> [CFString: Any] {
        [kSecClass: kSecClassGenericPassword, kSecAttrService: service, kSecAttrAccount: key,
         kSecUseDataProtectionKeychain: true]
    }

    public func read(_ key: String) throws -> String? {
        var q = base(key)
        q[kSecReturnData] = true
        q[kSecMatchLimit] = kSecMatchLimitOne
        var out: CFTypeRef?
        let st = SecItemCopyMatching(q as CFDictionary, &out)
        if st == errSecItemNotFound { return nil }
        guard st == errSecSuccess, let d = out as? Data else { throw KeychainError(status: st) }
        return String(data: d, encoding: .utf8)
    }

    public func write(_ key: String, _ value: String) throws {
        try delete(key)
        var q = base(key)
        q[kSecValueData] = Data(value.utf8)
        q[kSecAttrAccessible] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        let st = SecItemAdd(q as CFDictionary, nil)
        guard st == errSecSuccess else { throw KeychainError(status: st) }
    }

    public func delete(_ key: String) throws {
        let st = SecItemDelete(base(key) as CFDictionary)
        guard st == errSecSuccess || st == errSecItemNotFound else { throw KeychainError(status: st) }
    }
}
```

`ios/CalamaioKit/Sources/CalamaioKit/PairingStore.swift`:

```swift
import Foundation

// Il PC abbinato: dati non segreti nelle preferenze, il token solo nel SecretStore.
public struct PairedPC: Codable, Equatable, Sendable {
    public var name: String
    public var fingerprint: String
    public var hosts: [String]   // indirizzi conosciuti (QR, poi /app/whoami)
    public var port: Int
    public var lastHost: String? // l'ultimo che ha risposto

    public init(name: String, fingerprint: String, hosts: [String], port: Int, lastHost: String?) {
        self.name = name; self.fingerprint = fingerprint; self.hosts = hosts; self.port = port; self.lastHost = lastHost
    }
}

public final class PairingStore: @unchecked Sendable {
    static let pcKey = "calamaio.pairedPC"
    static let tokenKey = "pc-token"
    let defaults: UserDefaults
    let secrets: SecretStore

    public init(defaults: UserDefaults, secrets: SecretStore) {
        self.defaults = defaults
        self.secrets = secrets
    }

    // nil se manca il PC o il token (per esempio Portachiavi non ripristinato): l'app torna «non abbinata».
    public func load() -> (PairedPC, String)? {
        guard let d = defaults.data(forKey: Self.pcKey), let pc = try? JSONDecoder().decode(PairedPC.self, from: d),
              let t = try? secrets.read(Self.tokenKey), PairingLink.isLowerHex(t, 64) else { return nil }
        return (pc, t)
    }

    // Prima il token: se il Portachiavi rifiuta, niente PC a metà nelle preferenze.
    public func save(_ pc: PairedPC, token: String) throws {
        try secrets.write(Self.tokenKey, token)
        update(pc)
    }

    public func update(_ pc: PairedPC) {
        if let d = try? JSONEncoder().encode(pc) { defaults.set(d, forKey: Self.pcKey) }
    }

    public func forget() {
        defaults.removeObject(forKey: Self.pcKey)
        try? secrets.delete(Self.tokenKey)
    }
}
```

- [ ] **Step 3: Scrivere `Locator.swift` e `BonjourScanner.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/Locator.swift`:

```swift
import Foundation

public struct Endpoint: Hashable, Sendable {
    public let host: String
    public let port: Int
    public init(host: String, port: Int) { self.host = host; self.port = port }
}

// Un servizio «_calamaio-app._tcp» trovato con Bonjour: indirizzo IPv4, porta, TXT fp (16 cifre).
public struct BonjourHit: Equatable, Sendable {
    public let host: String
    public let port: Int
    public let fpPrefix: String
    public init(host: String, port: Int, fpPrefix: String) { self.host = host; self.port = port; self.fpPrefix = fpPrefix }
}

public enum Locator {
    // Ordine dei tentativi: l'ultimo indirizzo buono, Bonjour con la nostra impronta (mai un altro PC), poi gli
    // indirizzi conosciuti. Senza doppioni.
    public static func candidates(pc: PairedPC, bonjour: [BonjourHit]) -> [Endpoint] {
        var out: [Endpoint] = []
        func add(_ e: Endpoint) { if !out.contains(e) { out.append(e) } }
        if let h = pc.lastHost { add(Endpoint(host: h, port: pc.port)) }
        let prefix = String(pc.fingerprint.prefix(16))
        for b in bonjour where b.fpPrefix == prefix { add(Endpoint(host: b.host, port: b.port)) }
        for h in pc.hosts { add(Endpoint(host: h, port: pc.port)) }
        return out
    }
}

public enum Reach: Equatable, Sendable {
    case ready(Endpoint)
    case notFound
    case revoked
    case certChanged
}

public enum Connector {
    // Prova i candidati con /app/whoami. 401 ferma subito (revocato); un'impronta diversa si ricorda (se nessun
    // altro indirizzo risponde il PC ha un certificato nuovo). Il PC aggiornato tiene l'indirizzo buono e quelli
    // che il PC dice di avere adesso.
    public static func connect(pc: PairedPC, token: String, hits: [BonjourHit],
                               probe: (Endpoint) async throws -> WhoAmI) async -> (Reach, PairedPC) {
        var mismatch = false
        for ep in Locator.candidates(pc: pc, bonjour: hits) {
            do {
                let me = try await probe(ep)
                var up = pc
                up.lastHost = ep.host
                up.port = ep.port
                if !me.hosts.isEmpty { up.hosts = me.hosts }
                return (.ready(ep), up)
            } catch PCError.unauthorized {
                return (.revoked, pc)
            } catch PCError.pinMismatch {
                mismatch = true
            } catch {
                continue
            }
        }
        return (mismatch ? .certChanged : .notFound, pc)
    }
}
```

`ios/CalamaioKit/Sources/CalamaioKit/BonjourScanner.swift`:

```swift
import Foundation
import Network

// Cerca «_calamaio-app._tcp» per un attimo e risolve ogni servizio in IPv4 e porta (una connessione TCP che si
// chiude appena pronta). Nel simulatore usa la rete del Mac; sul telefono la prima volta iOS chiede il permesso
// della rete locale.
public final class BonjourScanner: @unchecked Sendable {
    public init() {}

    public func scan(timeout: TimeInterval = 1.5) async -> [BonjourHit] {
        let found: [(NWEndpoint, String)] = await withCheckedContinuation { cont in
            let browser = NWBrowser(for: .bonjourWithTXTRecord(type: "_calamaio-app._tcp", domain: nil), using: .tcp)
            let q = DispatchQueue(label: "calamaio.bonjour")
            var items: [(NWEndpoint, String)] = []
            browser.browseResultsChangedHandler = { results, _ in
                items = results.compactMap { r in
                    guard case let .bonjour(txt) = r.metadata else { return nil }
                    return (r.endpoint, txt["fp"] ?? "")
                }
            }
            browser.start(queue: q)
            q.asyncAfter(deadline: .now() + timeout) {
                browser.cancel()
                cont.resume(returning: items)
            }
        }
        var hits: [BonjourHit] = []
        for (ep, fp) in found {
            if let h = await Self.resolve(ep, fp: fp) { hits.append(h) }
        }
        return hits
    }

    static func resolve(_ ep: NWEndpoint, fp: String) async -> BonjourHit? {
        await withCheckedContinuation { cont in
            let params = NWParameters.tcp
            if let ip = params.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options { ip.version = .v4 }
            let c = NWConnection(to: ep, using: params)
            let q = DispatchQueue(label: "calamaio.resolve")
            var done = false
            func finish(_ h: BonjourHit?) {
                if done { return }
                done = true
                c.cancel()
                cont.resume(returning: h)
            }
            c.stateUpdateHandler = { st in
                switch st {
                case .ready:
                    if case let .hostPort(host, port)? = c.currentPath?.remoteEndpoint {
                        var s = "\(host)"
                        if let i = s.firstIndex(of: "%") { s = String(s[..<i]) } // senza «%en0»
                        finish(BonjourHit(host: s, port: Int(port.rawValue), fpPrefix: fp))
                    } else {
                        finish(nil)
                    }
                case .failed, .cancelled:
                    finish(nil)
                default:
                    break
                }
            }
            c.start(queue: q)
            q.asyncAfter(deadline: .now() + 2) { finish(nil) }
        }
    }
}
```

- [ ] **Step 4: Scrivere `AppController.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/AppController.swift`:

```swift
import Foundation
import Combine

// Dove mandare le richieste della webapp: PC raggiunto e token.
public struct Upstream: Equatable, Sendable {
    public let endpoint: Endpoint
    public let token: String
    public init(endpoint: Endpoint, token: String) { self.endpoint = endpoint; self.token = token }
}

// Stato dell'app: abbinamento, ricerca del PC, errori da mostrare. Le dipendenze di rete si iniettano (prove).
@MainActor
public final class AppController: ObservableObject {
    public enum Phase: Equatable, Sendable {
        case unpaired
        case connecting
        case ready(Endpoint)
        case failed(String)
    }

    public struct Deps {
        public var pair: (_ host: String, _ port: Int, _ pin: String, _ code: String, _ name: String, _ old: String?) async throws -> String
        public var whoami: (_ ep: Endpoint, _ pin: String, _ token: String) async throws -> WhoAmI
        public var scan: () async -> [BonjourHit]
        public var deviceName: () -> String

        public init(pair: @escaping (String, Int, String, String, String, String?) async throws -> String,
                    whoami: @escaping (Endpoint, String, String) async throws -> WhoAmI,
                    scan: @escaping () async -> [BonjourHit],
                    deviceName: @escaping () -> String) {
            self.pair = pair; self.whoami = whoami; self.scan = scan; self.deviceName = deviceName
        }

        // Rete vera: PCClient con l'impronta fissata, Bonjour.
        public static func live(deviceName: @escaping () -> String) -> Deps {
            Deps(pair: { host, port, pin, code, name, old in
                     try await PCClient(pin: pin).pair(host: host, port: port, code: code, name: name, oldToken: old)
                 },
                 whoami: { ep, pin, token in
                     try await PCClient(pin: pin, timeout: 2.5).whoami(host: ep.host, port: ep.port, token: token)
                 },
                 scan: { await BonjourScanner().scan() },
                 deviceName: deviceName)
        }
    }

    @Published public private(set) var phase: Phase = .unpaired
    @Published public private(set) var pc: PairedPC?
    public private(set) var token: String?
    let store: PairingStore
    let deps: Deps

    public init(store: PairingStore, deps: Deps) {
        self.store = store
        self.deps = deps
        if let (pc, t) = store.load() {
            self.pc = pc
            self.token = t
            phase = .connecting
        }
    }

    public var upstream: Upstream? {
        guard case let .ready(ep) = phase, let t = token else { return nil }
        return Upstream(endpoint: ep, token: t)
    }

    public func connect() async {
        guard let pc, let token else { phase = .unpaired; return }
        phase = .connecting
        let hits = await deps.scan()
        let whoami = deps.whoami
        let (reach, updated) = await Connector.connect(pc: pc, token: token, hits: hits) { ep in
            try await whoami(ep, pc.fingerprint, token)
        }
        if updated != pc {
            self.pc = updated
            store.update(updated)
        }
        switch reach {
        case let .ready(ep): phase = .ready(ep)
        case .notFound: phase = .failed(Texts.notFound)
        case .revoked: phase = .failed(Texts.revoked)
        case .certChanged: phase = .failed(Texts.certChanged)
        }
    }

    // Abbinamento da QR o da link incollato: prova gli indirizzi del QR uno alla volta. nil = fatto.
    public func pair(with text: String) async -> String? {
        let link: PairingLink
        do { link = try PairingLink.parse(text) } catch { return Texts.badLink }
        let old = pc?.fingerprint == link.fingerprint ? token : nil // stesso PC: il servizio revoca il telefono vecchio
        var mismatch = false
        for host in link.hosts {
            do {
                let t = try await deps.pair(host, link.port, link.fingerprint, link.code, deps.deviceName(), old)
                let p = PairedPC(name: link.name, fingerprint: link.fingerprint, hosts: link.hosts, port: link.port, lastHost: host)
                do { try store.save(p, token: t) } catch { return Texts.keychain }
                pc = p
                token = t
                await connect()
                return nil
            } catch PCError.pinMismatch {
                mismatch = true
            } catch let PCError.pairingRejected(msg) {
                return Texts.pairRejected + msg
            } catch {
                continue
            }
        }
        return mismatch ? Texts.pairPinMismatch : Texts.notFound
    }

    // Dimentica il PC sul telefono (il token resta valido sul PC finché non si revoca dalla webapp).
    public func forget() {
        store.forget()
        pc = nil
        token = nil
        phase = .unpaired
    }
}
```

- [ ] **Step 5: Eseguire e vedere passare**

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 12`
Expected: tutti i test passano (`LocatorTests` 5, `PairingStoreTests` 2, `AppControllerTests` 5, quelli del Task 8), `E2ETests` saltato.

Run (Portachiavi vero):

```bash
cd ios && xcodegen generate >/dev/null && xcodebuild test -project Calamaio.xcodeproj -scheme Calamaio \
  -destination 'platform=iOS Simulator,name=iPhone 18 Pro,OS=27.0' -only-testing:CalamaioTests/KeychainStoreTests 2>&1 | grep -E 'error:|Test Case|TEST (SUCCEEDED|FAILED)' | tail -n 6
```

Expected: `** TEST SUCCEEDED **`. Un `KeychainError(status: -34018)` vuol dire app non firmata: non aggiungere `CODE_SIGNING_ALLOWED=NO` a questo comando (la firma locale automatica del simulatore basta).

- [ ] **Step 6: Commit**

```bash
git add ios
git commit -m "App iPhone: token nel Portachiavi (solo questo dispositivo), PC abbinato, ricerca con Bonjour e indirizzi di riserva, stati revocato e certificato cambiato, controllore dell'app"
```

---

### Task 10: Gestore dello schema `calamaio-app://` (e ponte per i corpi, ramo B1)

**Files:**
- Create: `ios/CalamaioKit/Sources/CalamaioKit/Proxy.swift`, `Transport.swift`, `SchemeHandler.swift`, `NavigationPolicy.swift`; `ios/CalamaioKit/Tests/CalamaioKitTests/ProxyTests.swift`, `SchemeHandlerTests.swift`, `NavigationPolicyTests.swift`, `TransportE2ETests.swift`
- Create solo nel ramo B1: `ios/CalamaioKit/Sources/CalamaioKit/FetchBridge.swift`, `ios/CalamaioKit/Sources/CalamaioKit/Resources/fetch-shim.js`, `ios/CalamaioKit/Tests/CalamaioKitTests/FetchBridgeTests.swift`; Modify `ios/CalamaioKit/Package.swift` (risorse)
- Test: `cd ios/CalamaioKit && swift test`; `ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E`

**Interfaces:**
- Consumes: `Endpoint`, `Upstream` (Task 9), `PinnedDelegate` (Task 8), `SpikeOutcome` (Task 7).
- Produces:
  - `public enum Proxy { scheme = "calamaio-app"; host = "ui"; maxBody = 16 << 20; startURL: URL; upstream(_:body:to:token:) throws -> URLRequest; downstream(_:for:) -> HTTPURLResponse; readBody(_:) -> Data?; local(status:url:message:) -> (HTTPURLResponse, Data); enum ProxyError { notOurURL, bodyTooLarge } }`;
  - `public protocol Transport: AnyObject { func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void, onDone: @escaping (Error?) -> Void) -> () -> Void }` (callback sul main thread; restituisce la funzione che annulla); `public final class URLSessionTransport: PinnedDelegate, URLSessionDataDelegate, Transport` (`init(pin:)`);
  - `public protocol SchemeTask: AnyObject { var request: URLRequest { get }; func didReceive(_ response: URLResponse); func didReceive(_ data: Data); func didFinish(); func didFailWithError(_ error: Error) }`; `@MainActor public final class ProxyCore { init(transport:upstream:); start(_ task: SchemeTask, onEnd: @escaping () -> Void = {}); stop(_ task: SchemeTask) }`; `@MainActor public final class SchemeHandler: NSObject, WKURLSchemeHandler` (`init(core: ProxyCore)`);
  - `public enum NavDecision { allow, download, cancel }`, `public enum NavigationPolicy { decide(url:shouldDownload:) ; decideResponse(disposition:canShow:) }`, `public enum DownloadName { static func safe(_ s: String) -> String }`;
  - ramo B1: `@MainActor public final class FetchBridge: NSObject, WKScriptMessageHandlerWithReply` (`init(transport:upstream:)`, `static var shimSource: String`, `static func request(from:upstream:) throws -> URLRequest`).

- [ ] **Step 1: Scrivere i test che falliscono**

`ios/CalamaioKit/Tests/CalamaioKitTests/ProxyTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

let ep = Endpoint(host: "192.168.1.20", port: 8767)
let token = String(repeating: "f", count: 64)

final class ProxyTests: XCTestCase {
    func testUpstreamDropsPageHeaders() throws {
        var page = URLRequest(url: URL(string: "calamaio-app://ui/api/displays/calamaio-3f2a/settings?x=1&y=%20z")!)
        page.httpMethod = "POST"
        for (k, v) in ["Content-Type": "application/json", "Accept": "*/*", "Authorization": "Bearer pagina",
                       "Origin": "calamaio-app://ui", "Cookie": "a=b", "Referer": "calamaio-app://ui/", "Sec-Fetch-Site": "same-origin",
                       "Last-Event-ID": "7", "X-Altro": "1"] {
            page.setValue(v, forHTTPHeaderField: k)
        }
        let up = try Proxy.upstream(page, body: Data("{}".utf8), to: ep, token: token)
        XCTAssertEqual(up.url?.absoluteString, "https://192.168.1.20:8767/api/displays/calamaio-3f2a/settings?x=1&y=%20z")
        XCTAssertEqual(up.httpMethod, "POST")
        XCTAssertEqual(up.httpBody, Data("{}".utf8))
        XCTAssertEqual(up.value(forHTTPHeaderField: "Authorization"), "Bearer " + token) // mai quello della pagina
        XCTAssertEqual(up.value(forHTTPHeaderField: "Content-Type"), "application/json")
        XCTAssertEqual(up.value(forHTTPHeaderField: "Last-Event-ID"), "7")
        for h in ["Origin", "Cookie", "Referer", "Sec-Fetch-Site", "X-Altro"] {
            XCTAssertNil(up.value(forHTTPHeaderField: h), h)
        }
    }

    func testRootPathAndForeignURLs() throws {
        let root = try Proxy.upstream(URLRequest(url: URL(string: "calamaio-app://ui")!), body: nil, to: ep, token: token)
        XCTAssertEqual(root.url?.absoluteString, "https://192.168.1.20:8767/")
        for bad in ["calamaio-app://evil/api/status", "https://192.168.1.20:8767/api/status", "calamaio-app:///api/status"] {
            XCTAssertThrowsError(try Proxy.upstream(URLRequest(url: URL(string: bad)!), body: nil, to: ep, token: token), bad)
        }
    }

    func testBodyLimit() {
        let page = URLRequest(url: URL(string: "calamaio-app://ui/api/avatars")!)
        XCTAssertNoThrow(try Proxy.upstream(page, body: Data(count: Proxy.maxBody), to: ep, token: token))
        XCTAssertThrowsError(try Proxy.upstream(page, body: Data(count: Proxy.maxBody + 1), to: ep, token: token)) {
            XCTAssertEqual($0 as? Proxy.ProxyError, .bodyTooLarge)
        }
    }

    func testDownstreamHeadersAndLocation() throws {
        let r = HTTPURLResponse(url: URL(string: "https://192.168.1.20:8767/x")!, statusCode: 301, httpVersion: "HTTP/1.1", headerFields: [
            "Content-Type": "text/html", "Content-Length": "10", "Content-Encoding": "gzip", "Set-Cookie": "a=b",
            "Strict-Transport-Security": "max-age=1", "Content-Security-Policy": "default-src 'self'",
            "Location": "https://192.168.1.20:8767/device.html?a=1"])!
        let page = URL(string: "calamaio-app://ui/x")!
        let d = Proxy.downstream(r, for: page)
        XCTAssertEqual(d.url, page)
        XCTAssertEqual(d.statusCode, 301)
        XCTAssertEqual(d.value(forHTTPHeaderField: "Content-Type"), "text/html")
        XCTAssertEqual(d.value(forHTTPHeaderField: "Content-Security-Policy"), "default-src 'self'")
        XCTAssertEqual(d.value(forHTTPHeaderField: "Location"), "calamaio-app://ui/device.html?a=1")
        for h in ["Content-Length", "Content-Encoding", "Set-Cookie", "Strict-Transport-Security"] {
            XCTAssertNil(d.value(forHTTPHeaderField: h), h)
        }
    }

    func testReadBodyFromStream() {
        var rq = URLRequest(url: URL(string: "calamaio-app://ui/x")!)
        let big = Data((0..<(3 << 20)).map { UInt8($0 & 255) })
        rq.httpBodyStream = InputStream(data: big)
        XCTAssertEqual(Proxy.readBody(rq), big)
        rq.httpBodyStream = InputStream(data: Data(count: Proxy.maxBody + 10))
        XCTAssertEqual(Proxy.readBody(rq)?.count, Proxy.maxBody + 1) // si ferma appena oltre il limite
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/SchemeHandlerTests.swift`:

```swift
import XCTest
import CryptoKit
@testable import CalamaioKit

final class FakeTask: SchemeTask {
    let request: URLRequest
    var response: HTTPURLResponse?
    var data = Data()
    var calls: [String] = []
    var error: Error?
    init(_ r: URLRequest) { request = r }
    func didReceive(_ response: URLResponse) { calls.append("response"); self.response = response as? HTTPURLResponse }
    func didReceive(_ data: Data) { calls.append("data"); self.data.append(data) }
    func didFinish() { calls.append("finish") }
    func didFailWithError(_ error: Error) { calls.append("fail"); self.error = error }
}

final class FakeTransport: Transport {
    var requests: [URLRequest] = []
    var cancelled = 0
    var onResponse: ((HTTPURLResponse) -> Void)?
    var onData: ((Data) -> Void)?
    var onDone: ((Error?) -> Void)?
    func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
               onDone: @escaping (Error?) -> Void) -> () -> Void {
        requests.append(rq)
        self.onResponse = onResponse; self.onData = onData; self.onDone = onDone
        return { [weak self] in self?.cancelled += 1 }
    }
    func respond(_ status: Int, _ ctype: String) {
        onResponse?(HTTPURLResponse(url: requests.last!.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: ["Content-Type": ctype])!)
    }
}

@MainActor
final class SchemeHandlerTests: XCTestCase {
    var transport = FakeTransport()
    var up: Upstream? = Upstream(endpoint: ep, token: token)
    lazy var core = ProxyCore(transport: transport, upstream: { [unowned self] in self.up })

    func task(_ path: String, method: String = "GET") -> FakeTask {
        var r = URLRequest(url: URL(string: "calamaio-app://ui" + path)!)
        r.httpMethod = method
        return FakeTask(r)
    }

    func testStreamsChunksBeforeFinish() {
        let t = task("/api/events")
        core.start(t)
        XCTAssertEqual(transport.requests.first?.value(forHTTPHeaderField: "Authorization"), "Bearer " + token)
        transport.respond(200, "text/event-stream")
        transport.onData?(Data("event: status\n".utf8))
        XCTAssertEqual(t.calls, ["response", "data"]) // il primo pezzo è già nella pagina
        XCTAssertEqual(t.response?.url?.absoluteString, "calamaio-app://ui/api/events")
        transport.onData?(Data("data: {}\n\n".utf8))
        transport.onDone?(nil)
        XCTAssertEqual(t.calls, ["response", "data", "data", "finish"])
        XCTAssertEqual(String(data: t.data, encoding: .utf8), "event: status\ndata: {}\n\n")
    }

    func testStopCancelsAndSilences() {
        let t = task("/api/events")
        core.start(t)
        transport.respond(200, "text/event-stream")
        core.stop(t)
        XCTAssertEqual(transport.cancelled, 1)
        transport.onData?(Data("tardi".utf8))
        transport.onDone?(URLError(.cancelled))
        XCTAssertEqual(t.calls, ["response"]) // dopo lo stop niente più chiamate al compito (WebKit si fermerebbe)
        core.stop(t) // un secondo stop non fa niente
        XCTAssertEqual(transport.cancelled, 1)
    }

    func testFailureReachesPageOnce() {
        let t = task("/api/status")
        core.start(t)
        transport.onDone?(URLError(.networkConnectionLost))
        transport.onDone?(URLError(.networkConnectionLost))
        XCTAssertEqual(t.calls, ["fail"])
    }

    func testNotReachableGives503() {
        up = nil
        let t = task("/api/status")
        core.start(t)
        XCTAssertTrue(transport.requests.isEmpty)
        XCTAssertEqual(t.response?.statusCode, 503)
        XCTAssertEqual(t.calls.last, "finish")
        XCTAssertTrue(String(data: t.data, encoding: .utf8)!.contains(Texts.pcUnreachable))
    }

    func testLargeStreamBodyForwarded() {
        let big = Data((0..<(2 << 20)).map { UInt8(($0 * 7) & 255) }) // una ROM da 2 MiB
        let t = task("/api/gb/games?name=x.gb", method: "POST")
        var r = t.request
        r.httpBodyStream = InputStream(data: big)
        let t2 = FakeTask(r)
        core.start(t2)
        XCTAssertEqual(transport.requests.count, 1)
        XCTAssertEqual(transport.requests[0].httpBody.map { SHA256.hash(data: $0).description }, SHA256.hash(data: big).description)
    }

    func testBodyOverLimitIs413() {
        var r = task("/api/avatars", method: "POST").request
        r.httpBodyStream = InputStream(data: Data(count: Proxy.maxBody + 1))
        let t = FakeTask(r)
        core.start(t)
        XCTAssertTrue(transport.requests.isEmpty) // niente parte verso il PC
        XCTAssertEqual(t.response?.statusCode, 413)
        XCTAssertTrue(String(data: t.data, encoding: .utf8)!.contains("16 MiB"))
    }

    func testForeignHostFails() {
        let t = FakeTask(URLRequest(url: URL(string: "calamaio-app://evil/api/status")!))
        core.start(t)
        XCTAssertTrue(transport.requests.isEmpty)
        XCTAssertEqual(t.calls, ["fail"])
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/NavigationPolicyTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

final class NavigationPolicyTests: XCTestCase {
    func testNavigationPolicy() {
        let u = { (s: String) in URL(string: s) }
        XCTAssertEqual(NavigationPolicy.decide(url: u("calamaio-app://ui/device.html"), shouldDownload: false), .allow)
        XCTAssertEqual(NavigationPolicy.decide(url: u("about:blank"), shouldDownload: false), .allow)
        XCTAssertEqual(NavigationPolicy.decide(url: u("blob:calamaio-app://ui/1234"), shouldDownload: true), .download)
        XCTAssertEqual(NavigationPolicy.decide(url: u("calamaio-app://ui/api/themes/t1/export"), shouldDownload: true), .download)
        // mai il PC direttamente (né col suo certificato né senza token), mai siti esterni
        for s in ["https://192.168.1.20:8767/", "http://127.0.0.1:8765/", "https://example.com/", "calamaio-app://evil/",
                  "data:text/html,ciao", "javascript:alert(1)", "file:///etc/hosts"] {
            XCTAssertEqual(NavigationPolicy.decide(url: u(s), shouldDownload: false), .cancel, s)
        }
        XCTAssertEqual(NavigationPolicy.decide(url: nil, shouldDownload: false), .cancel)
    }

    func testResponsePolicy() {
        XCTAssertEqual(NavigationPolicy.decideResponse(disposition: #"attachment; filename="t1.theme.json""#, canShow: true), .download)
        XCTAssertEqual(NavigationPolicy.decideResponse(disposition: nil, canShow: true), .allow)
        XCTAssertEqual(NavigationPolicy.decideResponse(disposition: nil, canShow: false), .download)
    }

    func testDownloadName() {
        XCTAssertEqual(DownloadName.safe("t1.theme.json"), "t1.theme.json")
        XCTAssertEqual(DownloadName.safe("../../etc/passwd"), "passwd")
        XCTAssertEqual(DownloadName.safe(""), "file")
        XCTAssertEqual(DownloadName.safe(".."), "file")
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/TransportE2ETests.swift`:

```swift
import XCTest
@testable import CalamaioKit

// Col servizio vero (ios/scripts/e2e.sh …): SSE attraverso URLSessionTransport, elenco chiuso.
@MainActor
final class TransportE2ETests: XCTestCase {
    func testSSEAndForbiddenThroughTransport() async throws {
        let (l, token) = try await E2E.session()
        let up = Upstream(endpoint: Endpoint(host: l.hosts[0], port: l.port), token: token)
        let transport = URLSessionTransport(pin: l.fingerprint)
        let core = ProxyCore(transport: transport, upstream: { up })

        let events = FakeTask(URLRequest(url: URL(string: "calamaio-app://ui/api/events")!))
        core.start(events)
        let deadline = Date().addingTimeInterval(5)
        while !String(decoding: events.data, as: UTF8.self).contains("event: status"), Date() < deadline {
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        XCTAssertEqual(events.response?.statusCode, 200)
        XCTAssertTrue(String(decoding: events.data, as: UTF8.self).contains("event: status"), "primo evento SSE non arrivato")
        XCTAssertFalse(events.calls.contains("finish"), "lo stream non deve finire da solo")
        core.stop(events)

        for (path, want) in [("/api/backups", 404), ("/api/status", 200), ("/", 200), ("/firmware.html", 404)] {
            let t = FakeTask(URLRequest(url: URL(string: "calamaio-app://ui" + path)!))
            core.start(t)
            let end = Date().addingTimeInterval(5)
            while !t.calls.contains("finish") && !t.calls.contains("fail"), Date() < end {
                try await Task.sleep(nanoseconds: 50_000_000)
            }
            XCTAssertEqual(t.response?.statusCode, want, path)
            XCTAssertFalse(String(decoding: t.data, as: UTF8.self).contains(token), "token nella risposta di \(path)")
        }
    }
}
```

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 10`
Expected: FAIL di compilazione (`cannot find 'Proxy' in scope`).

- [ ] **Step 2: Scrivere `Proxy.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/Proxy.swift`:

```swift
import Foundation

// Regole del passaggio pagina → PC (spec §3.3): solo calamaio-app://ui, intestazioni in elenco, token aggiunto qui.
public enum Proxy {
    public static let scheme = "calamaio-app"
    public static let host = "ui"
    public static let maxBody = 16 << 20 // come maxBody del servizio
    public static let startURL = URL(string: "calamaio-app://ui/")!

    public enum ProxyError: Error, Equatable { case notOurURL, bodyTooLarge }

    // Le sole intestazioni della pagina che arrivano al PC: Origin, Cookie, Referer e un Authorization messo dal
    // JavaScript non passano mai.
    static let forwarded: Set<String> = ["accept", "content-type", "range", "last-event-id", "if-none-match"]
    // Intestazioni di trasporto che la pagina non deve vedere (il corpo è già decompresso e a pezzi).
    static let dropped: Set<String> = ["content-length", "content-encoding", "transfer-encoding", "connection",
                                       "set-cookie", "strict-transport-security"]

    public static func upstream(_ rq: URLRequest, body: Data?, to ep: Endpoint, token: String) throws -> URLRequest {
        guard let u = rq.url, u.scheme == scheme, u.host == host,
              var c = URLComponents(url: u, resolvingAgainstBaseURL: false) else { throw ProxyError.notOurURL }
        if let b = body, b.count > maxBody { throw ProxyError.bodyTooLarge }
        c.scheme = "https"
        c.host = ep.host
        c.port = ep.port
        if c.percentEncodedPath.isEmpty { c.percentEncodedPath = "/" }
        guard let target = c.url else { throw ProxyError.notOurURL }
        var out = URLRequest(url: target, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 60)
        out.httpMethod = rq.httpMethod ?? "GET"
        for (k, v) in rq.allHTTPHeaderFields ?? [:] where forwarded.contains(k.lowercased()) {
            out.setValue(v, forHTTPHeaderField: k)
        }
        out.setValue("Bearer " + token, forHTTPHeaderField: "Authorization")
        out.httpShouldHandleCookies = false
        out.httpBody = body
        return out
    }

    // La risposta del PC vista dalla pagina: URL dello schema proprio, Location riportata sullo schema.
    public static func downstream(_ r: HTTPURLResponse, for url: URL) -> HTTPURLResponse {
        var h: [String: String] = [:]
        for (k, v) in r.allHeaderFields {
            guard let ks = k as? String, let vs = v as? String, !dropped.contains(ks.lowercased()) else { continue }
            h[ks] = ks.lowercased() == "location" ? onScheme(vs) : vs
        }
        return HTTPURLResponse(url: url, statusCode: r.statusCode, httpVersion: "HTTP/1.1", headerFields: h)!
    }

    static func onScheme(_ location: String) -> String {
        guard var c = URLComponents(string: location), c.scheme == "https" else { return location }
        c.scheme = scheme
        c.host = host
        c.port = nil
        return c.string ?? location
    }

    // Il corpo della richiesta della pagina: httpBody, o lo stream (Blob, FormData) fino a maxBody+1 byte.
    public static func readBody(_ rq: URLRequest) -> Data? {
        if let b = rq.httpBody { return b }
        guard let s = rq.httpBodyStream else { return nil }
        s.open()
        defer { s.close() }
        var out = Data()
        var buf = [UInt8](repeating: 0, count: 64 << 10)
        while out.count <= maxBody {
            let n = s.read(&buf, maxLength: min(buf.count, maxBody + 1 - out.count))
            if n <= 0 { break }
            out.append(buf, count: n)
        }
        return out
    }

    // Una risposta fatta dall'app (PC non raggiungibile, file troppo grande), JSON come quelle del servizio.
    public static func local(status: Int, url: URL, message: String) -> (HTTPURLResponse, Data) {
        let body = (try? JSONSerialization.data(withJSONObject: ["error": message])) ?? Data()
        let r = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1",
                                headerFields: ["Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store"])!
        return (r, body)
    }
}
```

- [ ] **Step 3: Scrivere `Transport.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/Transport.swift`:

```swift
import Foundation

// Una richiesta verso il PC con la risposta a pezzi. Callback sul main thread; il valore restituito annulla.
public protocol Transport: AnyObject {
    func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
               onDone: @escaping (Error?) -> Void) -> () -> Void
}

// URLSession con l'impronta fissata. I redirect non si seguono qui: la pagina riceve il 30x con Location
// riportata sullo schema (Proxy.downstream) e lo segue passando di nuovo dal gestore.
public final class URLSessionTransport: PinnedDelegate, URLSessionDataDelegate, Transport, @unchecked Sendable {
    private struct Sink {
        let onResponse: (HTTPURLResponse) -> Void
        let onData: (Data) -> Void
        let onDone: (Error?) -> Void
    }
    private var session: URLSession!
    private var sinks: [Int: Sink] = [:] // solo sul main thread

    public override init(pin: String) {
        super.init(pin: pin)
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = 60 // tra un pezzo e l'altro: l'SSE manda un commento ogni 15 s
        cfg.waitsForConnectivity = false
        cfg.urlCache = nil
        cfg.httpCookieStorage = nil
        cfg.httpShouldSetCookies = false
        session = URLSession(configuration: cfg, delegate: self, delegateQueue: .main)
    }

    deinit { session.invalidateAndCancel() }

    public func start(_ rq: URLRequest, onResponse: @escaping (HTTPURLResponse) -> Void, onData: @escaping (Data) -> Void,
                      onDone: @escaping (Error?) -> Void) -> () -> Void {
        let t = session.dataTask(with: rq)
        sinks[t.taskIdentifier] = Sink(onResponse: onResponse, onData: onData, onDone: onDone)
        t.resume()
        return { [weak self] in
            self?.sinks[t.taskIdentifier] = nil
            t.cancel()
        }
    }

    public func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                           completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        if let h = response as? HTTPURLResponse { sinks[dataTask.taskIdentifier]?.onResponse(h) }
        completionHandler(.allow)
    }

    public func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        sinks[dataTask.taskIdentifier]?.onData(data)
    }

    public func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        sinks.removeValue(forKey: task.taskIdentifier)?.onDone(error)
    }

    public func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                           newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil)
    }
}
```

- [ ] **Step 4: Scrivere `SchemeHandler.swift` e `NavigationPolicy.swift`**

`ios/CalamaioKit/Sources/CalamaioKit/SchemeHandler.swift`:

```swift
import Foundation
import WebKit

// Il compito di WebKit visto dal gestore (permette le prove senza WKWebView).
public protocol SchemeTask: AnyObject {
    var request: URLRequest { get }
    func didReceive(_ response: URLResponse)
    func didReceive(_ data: Data)
    func didFinish()
    func didFailWithError(_ error: Error)
}

// Ogni richiesta della pagina (pagine, JS, API, SSE) va al PC con l'impronta fissata e il token. Dopo lo stop di
// WebKit nessuna chiamata al compito (WebKit lo considera un errore grave).
@MainActor
public final class ProxyCore {
    let transport: Transport
    public var upstream: () -> Upstream?
    private var live: [ObjectIdentifier: () -> Void] = [:]

    public init(transport: Transport, upstream: @escaping () -> Upstream?) {
        self.transport = transport
        self.upstream = upstream
    }

    // onEnd: il compito è finito (risposta completa, errore o risposta locale); non viene chiamata dopo uno stop.
    public func start(_ task: SchemeTask, onEnd: @escaping () -> Void = {}) {
        let id = ObjectIdentifier(task)
        guard let url = task.request.url else { task.didFailWithError(URLError(.badURL)); onEnd(); return }
        guard let up = upstream() else { return reply(task, Proxy.local(status: 503, url: url, message: Texts.pcUnreachable), onEnd) }
        let rq: URLRequest
        do {
            rq = try Proxy.upstream(task.request, body: Proxy.readBody(task.request), to: up.endpoint, token: up.token)
        } catch Proxy.ProxyError.bodyTooLarge {
            return reply(task, Proxy.local(status: 413, url: url, message: Texts.tooLarge), onEnd)
        } catch {
            task.didFailWithError(URLError(.unsupportedURL))
            onEnd()
            return
        }
        live[id] = {}
        let cancel = transport.start(rq, onResponse: { [weak self] r in
            guard self?.live[id] != nil else { return }
            task.didReceive(Proxy.downstream(r, for: url))
        }, onData: { [weak self] d in
            guard self?.live[id] != nil else { return }
            task.didReceive(d)
        }, onDone: { [weak self] err in
            guard let self, self.live.removeValue(forKey: id) != nil else { return }
            if let err { task.didFailWithError(err) } else { task.didFinish() }
            onEnd()
        })
        if live[id] != nil { live[id] = cancel } // un trasporto che finisce subito ha già tolto il segnaposto
    }

    public func stop(_ task: SchemeTask) {
        live.removeValue(forKey: ObjectIdentifier(task))?()
    }

    private func reply(_ task: SchemeTask, _ r: (HTTPURLResponse, Data), _ onEnd: () -> Void) {
        task.didReceive(r.0)
        task.didReceive(r.1)
        task.didFinish()
        onEnd()
    }
}

// WKURLSchemeTask → SchemeTask (stessa identità: il box si ritrova dallo stesso compito).
final class WKTaskBox: SchemeTask {
    let t: WKURLSchemeTask
    init(_ t: WKURLSchemeTask) { self.t = t }
    var request: URLRequest { t.request }
    func didReceive(_ response: URLResponse) { t.didReceive(response) }
    func didReceive(_ data: Data) { t.didReceive(data) }
    func didFinish() { t.didFinish() }
    func didFailWithError(_ error: Error) { t.didFailWithError(error) }
}

@MainActor
public final class SchemeHandler: NSObject, WKURLSchemeHandler {
    public let core: ProxyCore
    private var boxes: [ObjectIdentifier: WKTaskBox] = [:]

    public init(core: ProxyCore) { self.core = core }

    public func webView(_ webView: WKWebView, start urlSchemeTask: WKURLSchemeTask) {
        let id = ObjectIdentifier(urlSchemeTask)
        let box = WKTaskBox(urlSchemeTask)
        boxes[id] = box // il box vive finché il compito finisce o WebKit lo ferma
        core.start(box, onEnd: { [weak self] in self?.boxes[id] = nil })
    }

    public func webView(_ webView: WKWebView, stop urlSchemeTask: WKURLSchemeTask) {
        if let box = boxes.removeValue(forKey: ObjectIdentifier(urlSchemeTask)) { core.stop(box) }
    }
}
```

`ios/CalamaioKit/Sources/CalamaioKit/NavigationPolicy.swift`:

```swift
import Foundation

public enum NavDecision: Equatable { case allow, download, cancel }

public enum NavigationPolicy {
    // Solo le pagine dello schema proprio; blob: e le risposte da scaricare diventano scaricamenti. Mai https://
    // verso il PC (la WKWebView non ha né l'impronta né il token) e mai siti esterni.
    public static func decide(url: URL?, shouldDownload: Bool) -> NavDecision {
        guard let u = url, let s = u.scheme?.lowercased() else { return .cancel }
        if shouldDownload && (s == Proxy.scheme || s == "blob") { return .download }
        if s == Proxy.scheme && u.host == Proxy.host { return .allow }
        if u.absoluteString == "about:blank" { return .allow }
        return .cancel
    }

    public static func decideResponse(disposition: String?, canShow: Bool) -> NavDecision {
        if let d = disposition?.lowercased(), d.hasPrefix("attachment") { return .download }
        return canShow ? .allow : .download
    }
}

public enum DownloadName {
    // Solo l'ultimo pezzo del nome proposto; vuoto o «..» → «file».
    public static func safe(_ s: String) -> String {
        let last = s.split(whereSeparator: { $0 == "/" || $0 == "\\" }).last.map(String.init) ?? ""
        return last.isEmpty || last == "." || last == ".." ? "file" : last
    }
}
```

- [ ] **Step 5: Eseguire e vedere passare**

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 12`
Expected: passano `ProxyTests` (5), `SchemeHandlerTests` (7), `NavigationPolicyTests` (3) e i precedenti; gli E2E saltati.

Run: `ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E 2>&1 | tail -n 10`
Expected: `testPairAndPinAgainstRealService` e `testSSEAndForbiddenThroughTransport` passano (il primo evento SSE arriva prima della fine del flusso; `/api/backups` e `/firmware.html` 404).

- [ ] **Step 6: Commit (ramo A1, o prima parte del B1)**

```bash
git add ios/CalamaioKit
git commit -m "App iPhone: gestore dello schema calamaio-app:// verso il PC (token aggiunto dall'app, intestazioni in elenco, SSE a pezzi, stop che silenzia, 413 oltre 16 MiB), navigazione solo sullo schema, nomi dei file scaricati"
```

- [ ] **Step 7: Solo ramo B1 — ponte per i corpi**

Se `SpikeOutcome.bodiesReachHandler == true` saltare questo step.

In `ios/CalamaioKit/Package.swift` il bersaglio diventa `.target(name: "CalamaioKit", resources: [.process("Resources")]),`.

`ios/CalamaioKit/Sources/CalamaioKit/Resources/fetch-shim.js`:

```js
// App Calamaio: lo schema proprio di iOS non riceve i corpi delle richieste, quindi i fetch con corpo passano
// dall'app (messageHandlers.calamaioFetch). Qui non c'è nessun token: lo aggiunge l'app.
(() => {
  const h = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.calamaioFetch;
  if (!h) return;
  const toB64 = (buf) => {
    const b = new Uint8Array(buf); let s = '';
    for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode.apply(null, b.subarray(i, i + 0x8000));
    return btoa(s);
  };
  const fromB64 = (s) => { const bin = atob(s); const b = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) b[i] = bin.charCodeAt(i); return b; };
  const orig = window.fetch.bind(window);
  window.fetch = async (input, init) => {
    const rq = new Request(input, init);
    if (rq.method === 'GET' || rq.method === 'HEAD') return orig(input, init);
    const body = await rq.arrayBuffer();
    const headers = {};
    rq.headers.forEach((v, k) => { headers[k] = v; });
    const r = await h.postMessage({ method: rq.method, url: rq.url, headers, body: toB64(body) });
    const empty = r.status === 204 || r.status === 304;
    return new Response(empty ? null : fromB64(r.body), { status: r.status, headers: r.headers });
  };
})();
```

`ios/CalamaioKit/Sources/CalamaioKit/FetchBridge.swift`:

```swift
import Foundation
import WebKit

// Ramo B1 della prova del Task 7: i fetch con corpo arrivano da fetch-shim.js e partono da qui, con le stesse
// regole del gestore dello schema (Proxy.upstream). Solo dalla pagina principale dello schema proprio.
@MainActor
public final class FetchBridge: NSObject, WKScriptMessageHandlerWithReply {
    let transport: Transport
    let upstream: () -> Upstream?

    public init(transport: Transport, upstream: @escaping () -> Upstream?) {
        self.transport = transport
        self.upstream = upstream
    }

    public static var shimSource: String {
        guard let u = Bundle.module.url(forResource: "fetch-shim", withExtension: "js"),
              let s = try? String(contentsOf: u, encoding: .utf8) else { return "" }
        return s
    }

    // {method, url, headers, body (base64)} dalla pagina → richiesta verso il PC.
    public static func request(from body: Any, upstream up: Upstream) throws -> URLRequest {
        guard let d = body as? [String: Any], let m = d["method"] as? String, let s = d["url"] as? String,
              let url = URL(string: s) else { throw Proxy.ProxyError.notOurURL }
        var page = URLRequest(url: url)
        page.httpMethod = m
        for (k, v) in (d["headers"] as? [String: String]) ?? [:] { page.setValue(v, forHTTPHeaderField: k) }
        let data = Data(base64Encoded: (d["body"] as? String) ?? "") ?? Data()
        return try Proxy.upstream(page, body: data, to: up.endpoint, token: up.token)
    }

    public func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage,
                                      replyHandler: @escaping (Any?, String?) -> Void) {
        guard message.frameInfo.isMainFrame, message.frameInfo.securityOrigin.protocol == Proxy.scheme else {
            return replyHandler(nil, "origine non ammessa")
        }
        guard let up = upstream() else { return replyHandler(nil, Texts.pcUnreachable) }
        let rq: URLRequest
        do { rq = try Self.request(from: message.body, upstream: up) } catch Proxy.ProxyError.bodyTooLarge {
            return replyHandler(nil, Texts.tooLarge)
        } catch { return replyHandler(nil, "richiesta non valida") }
        let pageURL = URL(string: (message.body as? [String: Any])?["url"] as? String ?? "") ?? Proxy.startURL
        var status = 0
        var headers: [String: String] = [:]
        var data = Data()
        _ = transport.start(rq, onResponse: { r in
            status = r.statusCode
            headers = (Proxy.downstream(r, for: pageURL).allHeaderFields as? [String: String]) ?? [:]
        }, onData: { data.append($0) }, onDone: { err in
            if let err { replyHandler(nil, err.localizedDescription) }
            else { replyHandler(["status": status, "headers": headers, "body": data.base64EncodedString()], nil) }
        })
    }
}
```

`ios/CalamaioKit/Tests/CalamaioKitTests/FetchBridgeTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

final class FetchBridgeTests: XCTestCase {
    func testRequestFromPageMessage() throws {
        let up = Upstream(endpoint: ep, token: token)
        let msg: [String: Any] = ["method": "POST", "url": "calamaio-app://ui/api/avatars",
                                  "headers": ["content-type": "application/json", "authorization": "Bearer pagina", "origin": "x"],
                                  "body": Data("{\"a\":1}".utf8).base64EncodedString()]
        let rq = try FetchBridge.request(from: msg, upstream: up)
        XCTAssertEqual(rq.url?.absoluteString, "https://192.168.1.20:8767/api/avatars")
        XCTAssertEqual(rq.httpBody, Data("{\"a\":1}".utf8))
        XCTAssertEqual(rq.value(forHTTPHeaderField: "Authorization"), "Bearer " + token)
        XCTAssertNil(rq.value(forHTTPHeaderField: "Origin"))
        XCTAssertThrowsError(try FetchBridge.request(from: ["method": "POST", "url": "https://evil.example/"], upstream: up))
        XCTAssertFalse(FetchBridge.shimSource.isEmpty)
        XCTAssertFalse(FetchBridge.shimSource.lowercased().contains("bearer")) // il token non passa mai dal JS
    }
}
```

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 8`
Expected: anche `FetchBridgeTests` passa.

```bash
git add ios/CalamaioKit
git commit -m "App iPhone: ponte per i corpi dei fetch (ramo B1 della prova): fetch-shim.js senza token, richiesta dall'app con le regole dello schema"
```

- [ ] **Step 8: Solo ramo C — CSP sullo schema proprio**

Se il riepilogo della prova (`csp=[]`) non aveva violazioni, saltare. Altrimenti in `Proxy.downstream`, per l'intestazione `Content-Security-Policy`, aggiungere `calamaio-app://ui` dopo `'self'` nelle direttive elencate nel riepilogo (per esempio `connect-src 'self'` → `connect-src 'self' calamaio-app://ui`), con un test in `ProxyTests.testDownstreamHeadersAndLocation` che controlla la stringa risultante; la CSP del servizio non cambia. Commit: «App iPhone: CSP della webapp estesa allo schema proprio (ramo C della prova)».

---

### Task 11: App SwiftUI: schede «Scheda» e «Impostazioni», abbinamento, webapp, scaricamenti

**Files:**
- Create: `ios/CalamaioKit/Sources/CalamaioKit/WebScripts.swift`, `ios/CalamaioKit/Tests/CalamaioKitTests/WebScriptsTests.swift`; `ios/App/RootView.swift`, `ios/App/WebTab.swift`, `ios/App/WebContainer.swift`, `ios/App/SettingsTab.swift`, `ios/App/PairSheet.swift`, `ios/App/QRScanner.swift`, `ios/App/ShareSheet.swift`; `ios/AppUITests/PairAndLoadUITests.swift`
- Modify: `ios/App/CalamaioApp.swift` (riscritto), `ios/project.yml` (bersaglio `CalamaioUITests`), `docs/DECISIONI.md` (sezione «App iPhone»)
- Test: `swift test` (Kit), `ios/scripts/e2e.sh xcodebuild test … -only-testing:CalamaioUITests` (simulatore, servizio vero)

**Interfaces:**
- Consumes: `AppController`, `Upstream`, `PairedPC`, `PairingStore`, `KeychainStore` (Task 9); `Proxy`, `ProxyCore`, `SchemeHandler`, `URLSessionTransport`, `NavigationPolicy`, `DownloadName` (Task 10); `SpikeOutcome` (Task 7); ramo B1: `FetchBridge` (Task 10).
- Produces: `public enum WebScripts { static func userAgentName(version: String) -> String; static func sources(sseStreams: Bool) -> [String] }`; identificatori di accessibilità `open-pair`, `pair-link`, `pair-go`, `pair-error`, `settings-pair`, `webapp`; argomento di avvio `--reset-for-tests`.

Testi nuovi a schermo (italiano, tutti qui o in `Texts`): schede «Scheda» e «Impostazioni»; «Nessun PC abbinato», «Abbina un PC», «PC non raggiungibile», «Riprova», «Abbina di nuovo», «Cerco il PC…»; foglio «Abbina un PC» con «Link di abbinamento», «Incolla», «Abbina», «Annulla», «Abbino il telefono…», le due note sotto la fotocamera e sotto il link; Impostazioni: sezioni «PC abbinato» e «App», righe «Nome», «Indirizzo», «Impronta», «Stato», «Versione», stati «Collegato» e «Non abbinato», «Dimentica questo PC» con la conferma «Dimenticare questo PC?» / «Dimentica» / «Annulla» e il messaggio «Il telefono smette di usarlo. Revocalo anche dalla webapp sul PC: Dispositivo › Telefoni › Revoca.».

La scheda «Controller» arriva con la fase P3 (fuori da questo piano): qui solo «Scheda» e «Impostazioni».

- [ ] **Step 1: Test di `WebScripts` (fallisce), poi il codice**

`ios/CalamaioKit/Tests/CalamaioKitTests/WebScriptsTests.swift`:

```swift
import XCTest
@testable import CalamaioKit

final class WebScriptsTests: XCTestCase {
    func testUserAgentAndScripts() {
        XCTAssertEqual(WebScripts.userAgentName(version: "0.1.0"), "CalamaioApp/0.1.0") // spec §5: la webapp riconosce l'app
        XCTAssertEqual(WebScripts.sources(sseStreams: true), [])
        XCTAssertEqual(WebScripts.sources(sseStreams: false), ["delete window.EventSource;"])
        for s in WebScripts.sources(sseStreams: false) { XCTAssertFalse(s.lowercased().contains("bearer")) }
    }
}
```

Run: `cd ios/CalamaioKit && swift test --filter WebScripts 2>&1 | tail -n 5` → FAIL (`cannot find 'WebScripts'`).

`ios/CalamaioKit/Sources/CalamaioKit/WebScripts.swift`:

```swift
// Cosa l'app aggiunge alla webapp nella WKWebView.
public enum WebScripts {
    // In coda allo user agent: la webapp riconosce l'app da «CalamaioApp/» (spec §5, pagina Firmware nascosta in P4).
    public static func userAgentName(version: String) -> String { "CalamaioApp/" + version }

    // Script iniettati all'inizio di ogni pagina secondo la prova del Task 7. Ramo B2 (SSE che non arriva a pezzi):
    // senza EventSource la webapp passa da sola al polling ogni 2 s (api.js, watchStatus).
    public static func sources(sseStreams: Bool) -> [String] {
        sseStreams ? [] : ["delete window.EventSource;"]
    }
}
```

Run: `cd ios/CalamaioKit && swift test 2>&1 | tail -n 5` → PASS.

- [ ] **Step 2: L'app: ingresso, schede, webapp**

`ios/App/CalamaioApp.swift` (riscritto):

```swift
import SwiftUI
import CalamaioKit

@main
struct CalamaioApp: App {
    @StateObject private var model = CalamaioApp.makeModel()

    // «--reset-for-tests» (prove UI): si parte senza PC abbinato.
    @MainActor static func makeModel() -> AppController {
        let store = PairingStore(defaults: .standard, secrets: KeychainStore(service: "com.mrgamingita.calamaio.pc"))
        if ProcessInfo.processInfo.arguments.contains("--reset-for-tests") { store.forget() }
        return AppController(store: store, deps: .live(deviceName: { UIDevice.current.name }))
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .task { if model.pc != nil { await model.connect() } }
        }
    }
}
```

`ios/App/RootView.swift`:

```swift
import SwiftUI
import CalamaioKit

struct RootView: View {
    @EnvironmentObject var model: AppController
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        TabView {
            Tab("Scheda", systemImage: "rectangle.on.rectangle") { WebTab() }
            Tab("Impostazioni", systemImage: "gearshape") { SettingsTab() }
        }
        .onChange(of: scenePhase) { _, phase in
            // di ritorno in primo piano si riprova il PC (rete cambiata, servizio riavviato, telefono revocato)
            if phase == .active, model.pc != nil, case .failed = model.phase {
                Task { await model.connect() }
            }
        }
    }
}
```

`ios/App/WebTab.swift`:

```swift
import SwiftUI
import CalamaioKit

struct ShareItem: Identifiable {
    let url: URL
    var id: URL { url }
}

struct WebTab: View {
    @EnvironmentObject var model: AppController
    @State private var pairing = false
    @State private var share: ShareItem?

    var body: some View {
        Group {
            switch model.phase {
            case .unpaired:
                ContentUnavailableView {
                    Label("Nessun PC abbinato", systemImage: "link")
                } description: {
                    Text(Texts.notPaired)
                } actions: {
                    Button("Abbina un PC") { pairing = true }
                        .buttonStyle(.borderedProminent)
                        .accessibilityIdentifier("open-pair")
                }
            case .connecting:
                ProgressView(Texts.searching)
            case let .failed(msg):
                ContentUnavailableView {
                    Label(Texts.pcUnreachable, systemImage: "wifi.exclamationmark")
                } description: {
                    Text(msg)
                } actions: {
                    Button("Riprova") { Task { await model.connect() } }
                        .buttonStyle(.borderedProminent)
                    Button("Abbina di nuovo") { pairing = true }
                }
            case .ready:
                WebContainer(model: model, onShare: { share = ShareItem(url: $0) })
                    .ignoresSafeArea(edges: .bottom)
            }
        }
        .sheet(isPresented: $pairing) { PairSheet().environmentObject(model) }
        .sheet(item: $share) { ShareSheet(items: [$0.url]) }
    }
}
```

`ios/App/WebContainer.swift`:

```swift
import SwiftUI
import WebKit
import CalamaioKit

// La webapp del PC in una WKWebView, tutta attraverso calamaio-app:// (il token resta nell'app, spec §3.3).
struct WebContainer: UIViewRepresentable {
    let model: AppController
    let onShare: (URL) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(model: model, onShare: onShare) }

    func makeUIView(context: Context) -> WKWebView {
        let c = context.coordinator
        let cfg = WKWebViewConfiguration()
        cfg.websiteDataStore = .nonPersistent() // niente cookie né cache su disco
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0"
        cfg.applicationNameForUserAgent = WebScripts.userAgentName(version: version)
        cfg.setURLSchemeHandler(c.scheme, forURLScheme: Proxy.scheme)
        for src in WebScripts.sources(sseStreams: SpikeOutcome.sseStreams) {
            cfg.userContentController.addUserScript(WKUserScript(source: src, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        }
        let wv = WKWebView(frame: .zero, configuration: cfg)
        wv.navigationDelegate = c
        wv.allowsBackForwardNavigationGestures = true
        wv.isInspectable = true // Safari › Sviluppo sul Mac, per le prove
        wv.accessibilityIdentifier = "webapp"
        wv.load(URLRequest(url: Proxy.startURL))
        return wv
    }

    func updateUIView(_ wv: WKWebView, context: Context) {}

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate, WKDownloadDelegate {
        let model: AppController
        let onShare: (URL) -> Void
        let transport: URLSessionTransport
        let scheme: SchemeHandler
        private var downloads: [ObjectIdentifier: URL] = [:]

        init(model: AppController, onShare: @escaping (URL) -> Void) {
            self.model = model
            self.onShare = onShare
            transport = URLSessionTransport(pin: model.pc?.fingerprint ?? "")
            let up: () -> Upstream? = { [weak model] in model?.upstream }
            scheme = SchemeHandler(core: ProxyCore(transport: transport, upstream: up))
        }

        func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction) async -> WKNavigationActionPolicy {
            switch NavigationPolicy.decide(url: navigationAction.request.url, shouldDownload: navigationAction.shouldPerformDownload) {
            case .allow: return .allow
            case .download: return .download
            case .cancel: return .cancel
            }
        }

        func webView(_ webView: WKWebView, decidePolicyFor navigationResponse: WKNavigationResponse) async -> WKNavigationResponsePolicy {
            let disp = (navigationResponse.response as? HTTPURLResponse)?.value(forHTTPHeaderField: "Content-Disposition")
            return NavigationPolicy.decideResponse(disposition: disp, canShow: navigationResponse.canShowMIMEType) == .download ? .download : .allow
        }

        func webView(_ webView: WKWebView, navigationAction: WKNavigationAction, didBecome download: WKDownload) { download.delegate = self }
        func webView(_ webView: WKWebView, navigationResponse: WKNavigationResponse, didBecome download: WKDownload) { download.delegate = self }

        // Scaricamenti (export di temi e salvataggi): in una cartella temporanea, poi il foglio di condivisione.
        func download(_ download: WKDownload, decideDestinationUsing response: URLResponse, suggestedFilename: String) async -> URL? {
            let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            let url = dir.appendingPathComponent(DownloadName.safe(suggestedFilename))
            downloads[ObjectIdentifier(download)] = url
            return url
        }

        func downloadDidFinish(_ download: WKDownload) {
            if let u = downloads.removeValue(forKey: ObjectIdentifier(download)) { onShare(u) }
        }

        func download(_ download: WKDownload, didFailWithError error: Error, resumeData: Data?) {
            downloads.removeValue(forKey: ObjectIdentifier(download))
        }

        // La pagina non arriva (PC sparito, impronta cambiata, telefono revocato): si ricontrolla il PC e lo stato
        // dice perché. Le navigazioni fermate da noi (link esterni, scaricamenti) non contano.
        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
            let e = error as NSError
            if e.domain == "WebKitErrorDomain" || (e.domain == NSURLErrorDomain && e.code == NSURLErrorCancelled) { return }
            Task { await model.connect() }
        }

        // Mai accettare un certificato nella WKWebView: l'impronta la controlla solo URLSessionTransport.
        func webView(_ webView: WKWebView, respondTo challenge: URLAuthenticationChallenge) async -> (URLSession.AuthChallengeDisposition, URLCredential?) {
            (.performDefaultHandling, nil)
        }
    }
}
```

`ios/App/ShareSheet.swift`:

```swift
import SwiftUI
import UIKit

struct ShareSheet: UIViewControllerRepresentable {
    let items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }
    func updateUIViewController(_ vc: UIActivityViewController, context: Context) {}
}
```

- [ ] **Step 3: Abbinamento e Impostazioni**

`ios/App/QRScanner.swift`:

```swift
import SwiftUI
import VisionKit

// Fotocamera (VisionKit): il primo QR «calamaio://» va a onCode, una volta sola. Nel simulatore non c'è.
struct QRScanner: UIViewControllerRepresentable {
    let onCode: (String) -> Void

    static var isAvailable: Bool { DataScannerViewController.isSupported }

    func makeCoordinator() -> Coordinator { Coordinator(onCode: onCode) }

    func makeUIViewController(context: Context) -> DataScannerViewController {
        let vc = DataScannerViewController(recognizedDataTypes: [.barcode(symbologies: [.qr])], qualityLevel: .balanced,
                                           recognizesMultipleItems: false, isHighlightingEnabled: true)
        vc.delegate = context.coordinator
        try? vc.startScanning() // senza permesso della fotocamera resta nero: c'è il link da incollare
        return vc
    }

    func updateUIViewController(_ vc: DataScannerViewController, context: Context) {}

    static func dismantleUIViewController(_ vc: DataScannerViewController, coordinator: Coordinator) { vc.stopScanning() }

    final class Coordinator: NSObject, DataScannerViewControllerDelegate {
        let onCode: (String) -> Void
        private var done = false
        init(onCode: @escaping (String) -> Void) { self.onCode = onCode }

        func dataScanner(_ scanner: DataScannerViewController, didAdd items: [RecognizedItem], allItems: [RecognizedItem]) {
            guard !done else { return }
            for case let .barcode(b) in items {
                if let s = b.payloadStringValue, s.hasPrefix("calamaio://") {
                    done = true
                    onCode(s)
                    return
                }
            }
        }
    }
}
```

`ios/App/PairSheet.swift`:

```swift
import SwiftUI
import CalamaioKit

struct PairSheet: View {
    @EnvironmentObject var model: AppController
    @Environment(\.dismiss) private var dismiss
    @State private var link = ""
    @State private var error: String?
    @State private var busy = false
    @State private var attempt = 0 // dopo un errore la fotocamera riparte da capo

    var body: some View {
        NavigationStack {
            Form {
                if QRScanner.isAvailable {
                    Section {
                        QRScanner { text in Task { await submit(text) } }
                            .id(attempt)
                            .frame(height: 300)
                            .listRowInsets(EdgeInsets())
                    } footer: {
                        Text("Inquadra il codice mostrato dalla webapp sul PC: Dispositivo › Telefoni › «Abbina un telefono».")
                    }
                }
                Section {
                    TextField("calamaio://pair?…", text: $link)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .font(.system(.footnote, design: .monospaced))
                        .accessibilityIdentifier("pair-link")
                    PasteButton(payloadType: String.self) { strings in link = strings.first ?? "" }
                    Button("Abbina") { Task { await submit(link) } }
                        .disabled(busy || link.isEmpty)
                        .accessibilityIdentifier("pair-go")
                } header: {
                    Text("Link di abbinamento")
                } footer: {
                    Text("Senza fotocamera: nella webapp premi «Copia il link» e incollalo qui.")
                }
                if let error {
                    Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("pair-error") }
                }
                if busy {
                    Section { ProgressView("Abbino il telefono…") }
                }
            }
            .navigationTitle("Abbina un PC")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Annulla") { dismiss() } }
            }
        }
    }

    private func submit(_ text: String) async {
        guard !busy else { return }
        busy = true
        error = nil
        let err = await model.pair(with: text)
        busy = false
        if let err {
            error = err
            attempt += 1
        } else {
            dismiss()
        }
    }
}
```

`PasteButton` mostra «Incolla» nella lingua del telefono e non fa comparire l'avviso di iOS sugli appunti.

`ios/App/SettingsTab.swift`:

```swift
import SwiftUI
import CalamaioKit

struct SettingsTab: View {
    @EnvironmentObject var model: AppController
    @State private var pairing = false
    @State private var confirmForget = false

    private var version: String {
        let v = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
        let b = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"
        return "\(v) (\(b))"
    }

    private var address: String {
        if case let .ready(ep) = model.phase { return "\(ep.host):\(ep.port)" }
        return model.pc?.lastHost.map { "\($0) (l'ultimo)" } ?? "—"
    }

    private var stateText: String {
        switch model.phase {
        case .ready: return "Collegato"
        case .connecting: return Texts.searching
        case let .failed(msg): return msg
        case .unpaired: return "Non abbinato"
        }
    }

    // «abcd ef01 …»: le prime 16 cifre dell'impronta, come nel TXT Bonjour e nella webapp
    static func grouped(_ s: Substring) -> String {
        stride(from: 0, to: s.count, by: 4).map { i in
            let a = s.index(s.startIndex, offsetBy: i)
            return String(s[a..<s.index(a, offsetBy: min(4, s.count - i))])
        }.joined(separator: " ")
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("PC abbinato") {
                    if let pc = model.pc {
                        LabeledContent("Nome", value: pc.name)
                        LabeledContent("Indirizzo", value: address)
                        LabeledContent("Impronta", value: Self.grouped(pc.fingerprint.prefix(16)))
                        LabeledContent("Stato", value: stateText)
                        Button("Abbina di nuovo") { pairing = true }
                        Button("Dimentica questo PC", role: .destructive) { confirmForget = true }
                    } else {
                        Text(Texts.notPaired).foregroundStyle(.secondary)
                        Button("Abbina un PC") { pairing = true }.accessibilityIdentifier("settings-pair")
                    }
                }
                Section("App") {
                    LabeledContent("Versione", value: version)
                }
            }
            .navigationTitle("Impostazioni")
            .sheet(isPresented: $pairing) { PairSheet().environmentObject(model) }
            .confirmationDialog("Dimenticare questo PC?", isPresented: $confirmForget, titleVisibility: .visible) {
                Button("Dimentica", role: .destructive) { model.forget() }
                Button("Annulla", role: .cancel) {}
            } message: {
                Text("Il telefono smette di usarlo. Revocalo anche dalla webapp sul PC: Dispositivo › Telefoni › Revoca.")
            }
        }
    }
}
```

- [ ] **Step 4: Solo ramo B1 — collegare il ponte**

Se `SpikeOutcome.bodiesReachHandler == true` saltare. Altrimenti in `WebContainer.Coordinator` aggiungere la proprietà `let bridge: FetchBridge` e nell'`init`, dopo `scheme = …`, la riga `bridge = FetchBridge(transport: transport, upstream: up)`; in `makeUIView`, dopo il ciclo degli script:

```swift
        // ramo B1 della prova: i fetch con corpo passano dal ponte (nessun token nel JavaScript)
        cfg.userContentController.addUserScript(WKUserScript(source: FetchBridge.shimSource, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        cfg.userContentController.addScriptMessageHandler(c.bridge, contentWorld: .page, name: "calamaioFetch")
```

(Il ramo B2 non chiede codice qui: `WebScripts.sources(sseStreams:)` legge già `SpikeOutcome`.)

- [ ] **Step 5: Prova UI col servizio vero (fallisce senza il bersaglio)**

`ios/AppUITests/PairAndLoadUITests.swift`:

```swift
import XCTest

// Abbinamento col link incollato e webapp caricata, col servizio vero:
//   ios/scripts/e2e.sh xcodebuild test -project ios/Calamaio.xcodeproj -scheme Calamaio -destination '…' -only-testing:CalamaioUITests
final class PairAndLoadUITests: XCTestCase {
    func testPairWithLinkAndSeeTheWebapp() throws {
        guard let link = ProcessInfo.processInfo.environment["CALAMAIO_E2E_LINK"] else {
            throw XCTSkip("prova col servizio vero: ios/scripts/e2e.sh xcodebuild test …")
        }
        let app = XCUIApplication()
        app.launchArguments = ["--reset-for-tests"]
        app.launch()
        app.buttons["open-pair"].tap()
        let field = app.textFields["pair-link"]
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        field.tap()
        field.typeText(link)
        app.buttons["pair-go"].tap()

        let web = app.webViews["webapp"]
        XCTAssertTrue(web.waitForExistence(timeout: 20), "webapp assente: " + app.staticTexts["pair-error"].label)
        XCTAssertTrue(web.links["Panoramica"].waitForExistence(timeout: 20), "la Panoramica della webapp non si vede")
        web.links["Dispositivo"].tap()
        XCTAssertTrue(web.staticTexts["Dispositivo"].waitForExistence(timeout: 10), "pagina Dispositivo non caricata")
        sleep(2) // GET /api/phones ha avuto il tempo di rispondere (404 dalla porta dei telefoni)
        XCTAssertFalse(web.staticTexts["Telefoni"].exists, "dall'app la sezione Telefoni non deve esserci")

        app.tabBars.buttons["Impostazioni"].tap()
        XCTAssertTrue(app.staticTexts["Collegato"].waitForExistence(timeout: 5))
    }
}
```

In `ios/project.yml`, tra i `targets`, dopo `CalamaioTests`:

```yaml
  CalamaioUITests:
    type: bundle.ui-testing
    platform: iOS
    sources:
      - AppUITests
    dependencies:
      - target: Calamaio
    settings:
      base:
        GENERATE_INFOPLIST_FILE: YES
        PRODUCT_BUNDLE_IDENTIFIER: $(CALAMAIO_BUNDLE_ID).uitests
        TEST_TARGET_NAME: Calamaio
```

e nello schema: `CalamaioUITests: [test]` sotto `build.targets` e `- CalamaioUITests` sotto `test.targets`.

- [ ] **Step 6: Compilare e provare tutto**

Run: `cd ios && xcodegen generate 2>&1 | tail -n 1 && (cd CalamaioKit && swift test 2>&1 | tail -n 4)`
Expected: progetto rigenerato; tutti i test del Kit passano.

Run (simulatore, senza servizio: prova dello schema, Portachiavi, UI saltata):

```bash
cd ios && xcodebuild test -project Calamaio.xcodeproj -scheme Calamaio -destination 'platform=iOS Simulator,name=iPhone 18 Pro,OS=27.0' \
  2>&1 | grep -E 'error:|warning: .*CalamaioKit|Test Case .*(passed|failed|skipped)|TEST (SUCCEEDED|FAILED)' | tail -n 12
```

Expected: `SchemeBodySpikeTests` e `KeychainStoreTests` passano, `PairAndLoadUITests` saltato, `** TEST SUCCEEDED **`.

Run (simulatore col servizio vero):

```bash
ios/scripts/e2e.sh xcodebuild test -project ios/Calamaio.xcodeproj -scheme Calamaio \
  -destination 'platform=iOS Simulator,name=iPhone 18 Pro,OS=27.0' -only-testing:CalamaioUITests 2>&1 | \
  grep -E 'error:|Test Case|TEST (SUCCEEDED|FAILED)|e2e:' | tail -n 10
```

Expected: `testPairWithLinkAndSeeTheWebapp` **passed**: abbinamento, Panoramica, Dispositivo senza «Telefoni», Impostazioni «Collegato». Se fallisce su `Panoramica`, aprire Safari › Sviluppo › Simulatore › Calamaio sul Mac e guardare la console della pagina (CSP, richieste fallite): è il caso del ramo C del Task 10.

- [ ] **Step 7: DECISIONI**

In `docs/DECISIONI.md`, sezione «App iPhone», aggiungere:

```markdown
- **Fase P2, app** (`ios/`): progetto generato da `ios/project.yml` con XcodeGen 2.45.4 (`ios/Calamaio.xcodeproj` non si committa; `App/Info.plist` sì, lo scrive XcodeGen); squadra dell'Apple ID gratuito e bundle id personali in `ios/Config/Local.xcconfig` (ignorato da git, incluso da `App.xcconfig` con `#include?`: rigenerare il progetto non li perde). Logica nel pacchetto locale `ios/CalamaioKit` (iOS 27 e macOS 27: `swift test` sul Mac), app SwiftUI sottile. Modo di linguaggio Swift 5 (delegati di WebKit e URLSession senza la concorrenza stretta di Swift 6).
- **Schede in P2**: «Scheda» (webapp) e «Impostazioni»; la scheda «Controller» arriva con P3.
- **Abbinamento anche col link incollato** (non nella spec): il simulatore non ha fotocamera, e sul telefono è il ripiego se la fotocamera è negata; la webapp ha «Copia il link» (Appunti condivisi tra Mac e iPhone).
- **Nessuno schema `calamaio://` registrato nell'app**: un link da un sito o da un'altra app non può far partire un abbinamento verso un PC qualsiasi; il QR si legge solo dentro l'app.
- **Token e richieste**: token nel Portachiavi (`com.mrgamingita.calamaio.pc`, solo questo dispositivo, dopo il primo sblocco); tutte le richieste della pagina passano da `calamaio-app://ui/` → `ProxyCore` → `URLSessionTransport` con l'impronta fissata. La pagina non vede mai il token; le sue intestazioni passano solo se in elenco (Accept, Content-Type, Range, Last-Event-ID, If-None-Match); corpi fino a 16 MiB, oltre 413 dall'app; redirect riportati sullo schema; WKWebView con dati non persistenti, che non accetta certificati e non apre `https://`.
- **«Dimentica questo PC»** cancella solo sul telefono: per togliere il token dal PC si revoca dalla webapp (detto nella conferma).
- **Ricerca del PC**: ultimo indirizzo buono, poi Bonjour `_calamaio-app._tcp` con la stessa impronta (solo IPv4), poi gli indirizzi di `/app/whoami` (aggiornati a ogni collegamento) e del QR; 401 = «revocato», impronta diversa ovunque = «certificato diverso».
- **Testi nuovi dell'app** (non esistono nel simulatore della scheda): quelli di `CalamaioKit/Texts.swift` e delle viste del Task 11 del piano.
```

- [ ] **Step 8: Commit**

```bash
git add ios docs/DECISIONI.md
git commit -m "App iPhone: schede Scheda e Impostazioni, abbinamento da QR o da link, webapp nello schema proprio con scaricamenti e foglio di condivisione, prova UI end-to-end col servizio vero"
```

---

## Prove dal vivo (con l'utente, iPhone 17 Pro)

Non sono compiti del piano: le fa il coordinatore **con l'utente**, dopo che i Task 1–11 sono verdi. Nessun flash, nessuna scheda necessaria (una scheda accesa rende le prove più ricche ma non serve). Ogni prova ha l'esito atteso; gli esiti veri vanno in DECISIONI, «App iPhone».

**Preparazione** (chiedere all'utente prima di ogni punto):
1. Firma: `cd ios && xcodegen generate && open Calamaio.xcodeproj`; in Xcode › Calamaio › Signing & Capabilities scegliere la squadra personale dell'Apple ID; copiare il Team ID in `ios/Config/Local.xcconfig` (`DEVELOPMENT_TEAM = …`; se il bundle id è già preso da un altro account, anche `CALAMAIO_BUNDLE_ID = …`) e rigenerare.
2. iPhone: Modalità sviluppatore attiva (Impostazioni › Privacy e sicurezza), cavo o rete; dopo la prima installazione fidarsi dello sviluppatore (Impostazioni › Generali › VPN e gestione dispositivi).
3. Servizio: **prova A** con un'istanza di sviluppo separata (build nuova: `cd service && go build -o <tmp>/calamaio ./cmd/calamaio`), senza toccare il servizio installato: `CALAMAIO_CONFIG_DIR=<tmp>/c CALAMAIO_DATA_DIR=<tmp>/d CALAMAIO_API_PORT=28765 CALAMAIO_WS_PORT=28766 <tmp>/calamaio serve --fake --no-mdns` (porta dei telefoni 8767 su tutte le interfacce; `--no-mdns` per non far trovare alla scheda due servizi). macOS chiederà se accettare connessioni in entrata per il binario: decide l'utente. **Prova B** (Bonjour, servizio vero con i dati veri): solo se l'utente accetta di fermare il servizio installato e avviare la build nuova al suo posto, dopo un **backup** della cartella dei dati (`~/Library/Application Support/calamaio`) e di `config.json`.

**Prove:**
1. *Abbinamento col QR*: webapp sul Mac › Dispositivo › Telefoni › «Abbina un telefono» → QR e conto alla rovescia; nell'app «Abbina un PC» → iOS chiede la fotocamera, poi la rete locale → l'app mostra la Panoramica; nella webapp compare il telefono («usato adesso»). Atteso: entro 10 s dall'inquadratura.
2. *Webapp completa*: Panoramica, Avatar, Gioco, Dispositivo, Editor schermate si aprono; stato in diretta (con `calamaio fake <sessione> working` sull'istanza A lo stato cambia nell'app entro 1–2 s); Firmware risponde 404 (si nasconde nella fase P4).
3. *Caricamenti*: un avatar `.json` dall'app File (`<input type="file">`), un tema importato; atteso: caricati come dal Mac.
4. *Scaricamenti*: oggi **nessuno scaricamento è raggiungibile dal telefono**. «Export di un tema» non ha un comando nella webapp (`POST /api/themes/{id}/export` esiste nel servizio ma nessun JS lo chiama), e i soli `<a download>` della webapp sono il registro della scheda (Dispositivo), i backup e la copia letta del firmware (Firmware): registro, backup e firmware sono fuori dalla porta 8767 apposta (elenco chiuso di `service/internal/api/phone.go`), quindi dal telefono il link «Scarica» del registro scarica la risposta 404 e la pagina Firmware è 404. Prova possibile oggi: nessuna dal vivo; il foglio di condivisione si prova quando la webapp avrà uno scaricamento ammesso sulla 8767 (per esempio un comando per l'export dei temi).
5. *Revoca*: dalla webapp «Revoca». Due casi (decisione del coordinatore dopo la revisione finale, I1):
   - *con altri telefoni ancora abbinati* (la porta 8767 resta accesa): l'app, alla prossima richiesta della pagina (un 401) o al ritorno in primo piano, dice «Il PC ha revocato questo telefono…»;
   - *se era l'unico telefono*: la porta 8767 si spegne (`lsof -iTCP:8767 -sTCP:LISTEN` vuoto), il 401 non arriva mai e l'app, al ritorno in primo piano o alla prossima pagina che non arriva, dice «PC non trovato: telefono e PC devono essere sulla stessa rete».
6. *Riabbinamento*: QR nuovo con l'app già abbinata → nella webapp resta un solo telefono.
7. *Cambio di rete del PC*: con la prova B, cambiare rete Wi-Fi al Mac (o rinnovare il DHCP) → l'app ritrova il PC con Bonjour; con la prova A (senza Bonjour) → «PC non trovato…», poi un QR nuovo lo sistema.
8. *Certificato*: riavviare il servizio → l'app si ricollega senza riabbinare (stessa impronta). Facoltativo, solo sull'istanza A: cancellare `<tmp>/d/app-tls` e riavviare → l'app dice «Il PC ha un certificato diverso…».
9. *Rete del telefono*: Wi-Fi spento sul telefono → «PC non trovato…»; riacceso → «Riprova» funziona.
10. *Sette giorni*: annotare la data d'installazione; dopo la scadenza dell'Apple ID gratuito reinstallare da Xcode e controllare che il token sia ancora nel Portachiavi (l'app si collega senza riabbinare).
