package api

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"io/fs"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
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
		Whoami: func(p PhoneInfo) any {
			return map[string]any{"phone": map[string]string{"id": p.ID, "name": p.Name}}
		}})
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
		"bearer " + tok, "Basic " + tok, "Bearer " + tok[:63]} {
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
		"hook":                 {"POST /hook", "POST /hook?wait=1", "GET /hook", "POST /hook/context", "GET /hook/context"},
		"status line":          {"POST /statusline", "GET /statusline"},
		"debug":                {"GET /debug", "POST /debug/fake"},
		"abbinamento scheda":   {"POST /api/displays/b/pair/start", "POST /api/displays/b/pair/code", "POST /api/displays/b/pair/cancel", "POST /api/displays/b/unpair"},
		"wifi della scheda":    {"POST /api/displays/b/wifi/add", "POST /api/displays/b/wifi/forget"},
		"OTA e firmware":       {"POST /api/displays/b/ota", "POST /api/displays/b/ota/image", "POST /api/displays/b/ota/cancel", "GET /api/firmware", "GET /api/firmware/parts/firmware.bin", "GET /firmware.html", "GET /vendor/esptool-js-0.7.0/bundle.js"},
		"backup":               {"GET /api/backups", "POST /api/backups", "GET /api/backups/x/data", "DELETE /api/backups/x"},
		"registro remoto":      {"POST /api/displays/b/log/start", "POST /api/displays/b/log/stop", "GET /api/displays/b/log", "GET /api/displays/b/log/file"},
		"telefoni":             {"GET /api/phones", "POST /api/phones/pair", "POST /api/phones/pair/cancel", "DELETE /api/phones/x", "GET /api/phones/pair/qr.svg"},
		"metodi e sconosciute": {"PUT /api/status", "DELETE /api/status", "POST /api/events", "PATCH /api/themes/x", "GET /api/nope", "GET /api/crashes/x/extra", "POST /api/displays/b/nope", "GET /api/gb/games", "OPTIONS /api/status"},
		"percorsi furbi": {"GET /api//backups", "GET /api/status/../backups", "GET /API/backups", "GET /api/backups/", "GET /assets/../api/backups", "GET /%2e%2e/api/backups",
			"GET /assets/%2e%2e/firmware.html", "GET /assets/..%2ffirmware.html", "GET /assets/%2e%2e%2ffirmware.html", "GET /assets/%2e%2e/vendor/esptool-js-0.7.0/bundle.js", "POST /app%2fpair"},
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
		"GET /api/themes", "POST /api/themes", "POST /api/themes/import", "POST /api/themes/check", "GET /api/themes/t1", "PUT /api/themes/t1", "DELETE /api/themes/t1",
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

// bearer: Go toglie lo spazio finale dei valori, quindi quel caso è un token valido; qui si prova l'helper.
func TestBearer(t *testing.T) {
	tok := strings.Repeat("a", 64)
	cases := map[string]bool{"Bearer " + tok: true, "bearer " + tok: false, "Bearer": false,
		"Bearer " + strings.ToUpper(tok): false, "Bearer " + tok[:63]: false, "Basic " + tok: false, "": false}
	for h, want := range cases {
		r, _ := http.NewRequest(http.MethodGet, "http://x/", nil)
		if h != "" {
			r.Header.Set("Authorization", h)
		}
		if _, ok := bearer(r); ok != want {
			t.Errorf("%q: %v, atteso %v", h, ok, want)
		}
	}
}

// Lo spazio finale lo toglie il parser HTTP di Go: arriva già pulito e il token vale.
func TestBearerTrailingSpaceTrimmed(t *testing.T) {
	tok := strings.Repeat("a", 64)
	raw := "GET / HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + tok + " \r\n\r\n"
	r, err := http.ReadRequest(bufio.NewReader(strings.NewReader(raw)))
	if err != nil {
		t.Fatal(err)
	}
	if got, ok := bearer(r); !ok || got != tok {
		t.Fatalf("%q %v", got, ok)
	}
}

// I percorsi furbi contro la mux vera con la webapp montata: il registratore non vede dove li risolve il gestore a valle.
func TestPhoneTrickyPathsRealMux(t *testing.T) {
	s := New(nil, log.New(io.Discard, "", 0))
	s.UI = webui.New(webui.Static())
	srv, _, tok := phoneSetup(t, s.Handler())
	for _, mp := range []string{"GET /assets/%2e%2e/firmware.html", "GET /assets/..%2ffirmware.html", "GET /assets/%2e%2e%2ffirmware.html",
		"GET /assets/%2e%2e/vendor/esptool-js-0.7.0/bundle.js", "GET /assets/../firmware.html", "GET /firmware.html", "POST /app%2fpair"} {
		method, path, _ := strings.Cut(mp, " ")
		code, body, _ := phoneReq(t, srv, method, path, "Bearer "+tok, "application/json", "")
		if code != 404 || strings.Contains(strings.ToLower(body), "esptool") {
			t.Errorf("%s → %d", mp, code)
		}
	}
}

// Ogni rotta dell'elenco chiuso esiste davvero nella mux della webapp (le due non devono divergere).
func TestPhoneRoutesRegistered(t *testing.T) {
	s := New(nil, log.New(io.Discard, "", 0))
	s.UI = webui.New(webui.Static())
	mux := s.Handler().(*http.ServeMux)
	for _, rt := range phoneRoutes {
		if rt == "GET /app/whoami" { // lo risponde il gestore dei telefoni
			continue
		}
		method, pat, _ := strings.Cut(rt, " ")
		path := strings.NewReplacer("{$}", "", "{board}", "b", "{id}", "x", "{n}", "1").Replace(pat)
		r := httptest.NewRequest(method, path, nil)
		_, got := mux.Handler(r)
		// GET /api/ e GET / sono i ripieghi: una rotta dell'elenco non deve cadere lì
		if got == "" || got == "GET /api/" || got == "POST /api/" || got == "PUT /api/" || got == "DELETE /api/" || (got == "GET /" && !strings.HasSuffix(pat, ".html") && path != "/" && !strings.HasPrefix(pat, "/assets/")) {
			t.Errorf("%s non è registrata nella mux (pattern: %q)", rt, got)
		}
	}
}

// Dopo il JSON non devono restare dati.
func TestPhonePairTrailingData(t *testing.T) {
	srv, m, _ := phoneSetup(t, &recorder{})
	code, _ := m.OpenWindow()
	c, _, _ := phoneReq(t, srv, http.MethodPost, "/app/pair", "", "application/json", `{"code":"`+code+`","name":"x"}garbage`)
	if c != 400 {
		t.Fatalf("dati dopo il JSON: %d", c)
	}
}

func TestPhoneInfoHasNoHash(t *testing.T) {
	b, _ := json.Marshal(infoOf(phones.Phone{ID: "i", Name: "n", TokenHash: "segretohash"}))
	if strings.Contains(string(b), "segretohash") || strings.Contains(strings.ToLower(string(b)), "sha") {
		t.Fatal(string(b))
	}
}

// Seconda difesa: anche se una rotta /api/phones passasse l'elenco, col segno del telefono risponde 404.
type phonesSpy struct{ calls int }

func (s *phonesSpy) PhonesView() PhonesView           { s.calls++; return PhonesView{} }
func (s *phonesSpy) OpenPairing() (PhonesView, error) { s.calls++; return PhonesView{}, nil }
func (s *phonesSpy) CancelPairing() PhonesView        { s.calls++; return PhonesView{} }
func (s *phonesSpy) RevokePhone(string) error         { s.calls++; return nil }
func (s *phonesSpy) PairingQR() ([]byte, bool)        { s.calls++; return nil, false }

func TestPhonesRoutesRefuseFromPhone(t *testing.T) {
	spy := &phonesSpy{}
	as := New(nil, log.New(io.Discard, "", 0))
	as.Phones = spy
	h := as.Handler()
	for _, tc := range [][2]string{{"GET", "/api/phones"}, {"POST", "/api/phones/pair"}, {"POST", "/api/phones/pair/cancel"}, {"DELETE", "/api/phones/x"}, {"GET", "/api/phones/pair/qr.svg"}} {
		r := httptest.NewRequest(tc[0], tc[1], nil)
		r.Header.Set("Content-Type", "application/json")
		r = r.WithContext(context.WithValue(r.Context(), phoneKey{}, phones.Phone{ID: "x"}))
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r) // senza passare dall'elenco chiuso
		if w.Code != 404 {
			t.Errorf("%s %s: %d", tc[0], tc[1], w.Code)
		}
	}
	if spy.calls != 0 {
		t.Fatalf("il telefono ha raggiunto il controllo: %d", spy.calls)
	}
}

// tools/webui-dev/phone-routes.json è la copia di phoneRoutes per il server finto e per la prova nel browser.
func TestPhoneRoutesFixture(t *testing.T) {
	path := filepath.Join("..", "..", "..", "tools", "webui-dev", "phone-routes.json")
	want, err := json.MarshalIndent(map[string]any{
		"comment": "Copia di phoneRoutes (service/internal/api/phone.go). Si rigenera con CALAMAIO_WRITE_FIXTURES=1 go test ./internal/api/ -run TestPhoneRoutesFixture",
		"routes":  phoneRoutes,
	}, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	want = append(want, '\n')
	if os.Getenv("CALAMAIO_WRITE_FIXTURES") == "1" {
		if err := os.WriteFile(path, want, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	got, err := os.ReadFile(path)
	if err != nil || string(got) != string(want) {
		t.Fatalf("phone-routes.json non è aggiornato: CALAMAIO_WRITE_FIXTURES=1 go test ./internal/api/ -run TestPhoneRoutesFixture (%v)", err)
	}
}
