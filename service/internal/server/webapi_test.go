package server

import (
	"bufio"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"

	"calamaio/internal/api"
	"calamaio/internal/display"
	"calamaio/internal/m5"
)

// Test d'integrazione della webapp: servizio vero, display finto via WebSocket, richieste HTTP
// come le fa il browser (stessa origine).

const fakeSettings = `{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true,"mute":"off","save":false}`

type fakeDisplay struct {
	t  *testing.T
	ws *websocket.Conn
	ch chan map[string]any
}

func (d *fakeDisplay) send(s string) {
	d.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := d.ws.Write(ctx, websocket.MessageText, []byte(s)); err != nil {
		d.t.Fatal(err)
	}
}

// expect salta gli `state` e restituisce il primo messaggio di tipo typ.
func (d *fakeDisplay) expect(typ string) map[string]any {
	d.t.Helper()
	deadline := time.After(3 * time.Second)
	for {
		select {
		case m := <-d.ch:
			if m["t"] == typ {
				return m
			}
			if m["t"] != "state" && m["t"] != "welcome" && m["t"] != "game" {
				d.t.Fatalf("atteso %s, arrivato %v", typ, m)
			}
		case <-deadline:
			d.t.Fatalf("%s non arrivato", typ)
			return nil
		}
	}
}

// testKey: chiave di abbinamento dei test (K del flusso in protocol/vectors/crypto-vectors.json).
var testKey, _ = m5.ParseHex("3d9c852a2bcba1e6337f46470998e3bd093d70c3a6fcaa006c4f7875a15e6ba0", 32)

// connectDisplay collega un display abbinato e autenticato (M5: set e full-refresh solo così).
func connectDisplay(t *testing.T, s *started, withSettings bool) *fakeDisplay {
	t.Helper()
	if err := s.keys.Put("calamaio-3f2a", testKey, time.Now()); err != nil {
		t.Fatal(err)
	}
	if err := s.run.Hub.LoadKeys(); err != nil {
		t.Fatal(err)
	}
	ws, ch := dialHello(t, s.cfg.WSPort)
	d := &fakeDisplay{t: t, ws: ws, ch: ch}
	d.expect("welcome")
	chal := d.expect("auth-challenge")
	ns, _ := m5.ParseHex(chal["ns"].(string), 16)
	nd := m5.Random(16)
	d.send(`{"t":"auth","v":1,"nd":"` + m5.Hex(nd) + `","mac":"` + m5.Hex(m5.AuthMac(testKey, "calamaio-3f2a", ns, nd)) + `"}`)
	d.expect("auth-ok")
	if withSettings {
		d.send(`{"t":"settings","v":1,"settings":` + fakeSettings + `,"remote":true}`)
	}
	return d
}

type web struct {
	t    *testing.T
	base string
	host string
}

func newWeb(t *testing.T, s *started) *web {
	return &web{t: t, base: "http://" + s.run.APIAddr, host: s.run.APIAddr}
}

// do manda una richiesta; hdr: coppie nome, valore. Restituisce codice, corpo e header.
func (w *web) do(method, path, ctype, body string, hdr ...string) (int, map[string]any, http.Header) {
	w.t.Helper()
	var rd io.Reader
	if body != "" {
		rd = strings.NewReader(body)
	}
	req, _ := http.NewRequest(method, w.base+path, rd)
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
		w.t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	var m map[string]any
	_ = json.Unmarshal(b, &m)
	if m == nil {
		m = map[string]any{"_raw": string(b)}
	}
	return resp.StatusCode, m, resp.Header
}

// post come la fa la webapp: JSON, Origin e Sec-Fetch-Site della stessa origine.
func (w *web) post(path, body string) (int, map[string]any) {
	w.t.Helper()
	code, m, _ := w.do(http.MethodPost, path, "application/json", body,
		"Origin", "http://"+w.host, "Sec-Fetch-Site", "same-origin")
	return code, m
}

func (w *web) status() map[string]any {
	w.t.Helper()
	code, m, _ := w.do(http.MethodGet, "/api/status", "", "")
	if code != 200 {
		w.t.Fatalf("GET /api/status: %d %v", code, m)
	}
	return m
}

func firstDisplay(st map[string]any) map[string]any {
	ds, _ := st["displays"].([]any)
	if len(ds) == 0 {
		return nil
	}
	return ds[0].(map[string]any)
}

func waitStatus(t *testing.T, w *web, ok func(map[string]any) bool) map[string]any {
	t.Helper()
	end := time.Now().Add(3 * time.Second)
	for time.Now().Before(end) {
		st := w.status()
		if ok(st) {
			return st
		}
		time.Sleep(20 * time.Millisecond)
	}
	b, _ := json.Marshal(w.status())
	t.Fatalf("condizione non raggiunta; status = %s", b)
	return nil
}

// sse apre /api/events e consegna i dati di ogni evento `status`.
func sse(t *testing.T, w *web) chan map[string]any {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, w.base+"/api/events", nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != 200 || !strings.HasPrefix(resp.Header.Get("Content-Type"), "text/event-stream") {
		t.Fatalf("SSE: %d %s", resp.StatusCode, resp.Header.Get("Content-Type"))
	}
	ch := make(chan map[string]any, 64)
	go func() {
		defer resp.Body.Close()
		sc := bufio.NewScanner(resp.Body)
		sc.Buffer(make([]byte, 1<<20), 1<<20)
		event := ""
		for sc.Scan() {
			line := sc.Text()
			switch {
			case strings.HasPrefix(line, "event: "):
				event = strings.TrimPrefix(line, "event: ")
			case strings.HasPrefix(line, "data: ") && event == "status":
				var m map[string]any
				if json.Unmarshal([]byte(strings.TrimPrefix(line, "data: ")), &m) == nil {
					ch <- m
				}
			case line == "":
				event = ""
			}
		}
	}()
	return ch
}

func TestWebSettingsEndToEnd(t *testing.T) {
	s := startOpts(t, false, 0, func(o *Options) { o.FirmwareDir = t.TempDir() })
	w := newWeb(t, s)
	events := sse(t, w)
	select {
	case first := <-events:
		if first["service"] == nil || first["state"] == nil {
			t.Fatalf("primo evento SSE = %v", first)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("SSE: nessun evento iniziale")
	}

	d := connectDisplay(t, s, true)
	st := waitStatus(t, w, func(st map[string]any) bool {
		fd := firstDisplay(st)
		return fd != nil && fd["settings"] != nil
	})
	svc := st["service"].(map[string]any)
	if svc["version"] != "test" || svc["tz"] != s.cfg.TZ || int(svc["wsPort"].(float64)) != s.cfg.WSPort || svc["host"] == "" {
		t.Fatalf("service = %v", svc)
	}
	state := st["state"].(map[string]any)
	if _, ok := state["time"]; !ok || state["t"] != nil || state["v"] != nil || state["sessions"] == nil {
		t.Fatalf("state = %v", state)
	}
	fd := firstDisplay(st)
	for _, k := range []string{"board", "device", "class", "fw", "w", "h", "ip", "since", "settings", "settingsPending"} {
		if _, ok := fd[k]; !ok {
			t.Errorf("display senza %q: %v", k, fd)
		}
	}
	if fd["board"] != "calamaio-3f2a" || fd["remote"] != true || len(fd["settingsPending"].(map[string]any)) != 0 {
		t.Fatalf("display = %v", fd)
	}

	// La UI cambia lo stile: 202 con pending, il display riceve set.
	code, body := w.post("/api/displays/calamaio-3f2a/settings", `{"homeStyle":"righe"}`)
	if code != http.StatusAccepted || body["pending"].(map[string]any)["homeStyle"] != "righe" {
		t.Fatalf("POST settings: %d %v", code, body)
	}
	set := d.expect("set")
	if set["settings"].(map[string]any)["homeStyle"] != "righe" || set["id"] != body["id"] {
		t.Fatalf("set = %v", set)
	}
	st = w.status()
	if firstDisplay(st)["settingsPending"].(map[string]any)["homeStyle"] != "righe" {
		t.Fatalf("pending non visibile in status: %v", firstDisplay(st))
	}
	d.send(`{"t":"settings","v":1,"settings":` + strings.Replace(fakeSettings, `"goccia"`, `"righe"`, 1) + `,"remote":true}`)
	d.send(`{"t":"ack","v":1,"id":"` + set["id"].(string) + `","ok":true}`)
	waitStatus(t, w, func(st map[string]any) bool {
		fd := firstDisplay(st)
		return fd["settings"].(map[string]any)["homeStyle"] == "righe" && len(fd["settingsPending"].(map[string]any)) == 0
	})

	// L'SSE ha visto il cambiamento.
	deadline := time.After(3 * time.Second)
	for seen := false; !seen; {
		select {
		case ev := <-events:
			if fd := firstDisplay(ev); fd != nil && fd["settings"] != nil {
				seen = fd["settings"].(map[string]any)["homeStyle"] == "righe" && len(fd["settingsPending"].(map[string]any)) == 0
			}
		case <-deadline:
			t.Fatal("SSE: il cambiamento non arriva")
		}
	}

	// Refresh completo come lo manda la webapp: POST senza corpo né Content-Type, stessa origine.
	code, body, _ = w.do(http.MethodPost, "/api/displays/calamaio-3f2a/full-refresh", "", "",
		"Origin", "http://"+w.host, "Sec-Fetch-Site", "same-origin")
	if code != http.StatusAccepted || body["id"] == nil {
		t.Fatalf("full-refresh: %d %v", code, body)
	}
	fr := d.expect("full-refresh")
	d.send(`{"t":"ack","v":1,"id":"` + fr["id"].(string) + `","ok":true}`)

	// permOn: da remoto solo false.
	code, body = w.post("/api/displays/calamaio-3f2a/settings", `{"permOn":true}`)
	if code != http.StatusBadRequest || body["error"] != display.MsgPermOnRemote {
		t.Fatalf("permOn true: %d %v", code, body)
	}
	for _, bad := range []string{`{"volume":3}`, `{"led":"sì"}`, `[1]`, `nonjson`, `{}`, `{"led":false} {"led":true}`} {
		if code, body = w.post("/api/displays/calamaio-3f2a/settings", bad); code != http.StatusBadRequest || body["error"] == nil {
			t.Errorf("%s: %d %v", bad, code, body)
		}
	}
	code, _ = w.post("/api/displays/calamaio-3f2a/settings", `{"permOn":false}`)
	if code != http.StatusAccepted {
		t.Fatalf("permOn false: %d", code)
	}
	if set := d.expect("set"); set["settings"].(map[string]any)["permOn"] != false {
		t.Fatalf("set permOn = %v", set)
	}
}

func TestWebConflicts(t *testing.T) {
	s := startOpts(t, false, 0, func(o *Options) { o.FirmwareDir = t.TempDir() })
	w := newWeb(t, s)
	connectDisplay(t, s, false) // firmware M3: hello senza settings
	waitStatus(t, w, func(st map[string]any) bool { return firstDisplay(st) != nil })
	fd := firstDisplay(w.status())
	if fd["settings"] != nil || fd["remote"] != false {
		t.Fatalf("display M3 = %v", fd)
	}
	for _, p := range []string{"/api/displays/calamaio-3f2a/settings", "/api/displays/calamaio-3f2a/full-refresh", "/api/displays/calamaio-zzzz/settings"} {
		code, body := w.post(p, `{"led":false}`)
		if code != http.StatusConflict || body["error"] == nil {
			t.Errorf("%s: %d %v", p, code, body)
		}
	}
}

func TestWebAckTimeoutVisible(t *testing.T) {
	s := startOpts(t, false, 0, func(o *Options) {
		o.FirmwareDir = t.TempDir()
		o.TuneHub = func(h *display.Hub) { h.MinInterval = 50 * time.Millisecond; h.AckTimeout = 200 * time.Millisecond }
		o.TuneAPI = func(a *api.Server) { a.EventInterval = 20 * time.Millisecond }
	})
	w := newWeb(t, s)
	d := connectDisplay(t, s, true)
	waitStatus(t, w, func(st map[string]any) bool { fd := firstDisplay(st); return fd != nil && fd["remote"] == true })
	if code, body := w.post("/api/displays/calamaio-3f2a/settings", `{"mute":"1h"}`); code != http.StatusAccepted {
		t.Fatalf("%d %v", code, body)
	}
	d.expect("set") // nessun ack
	st := waitStatus(t, w, func(st map[string]any) bool {
		fd := firstDisplay(st)
		return fd["error"] != nil && len(fd["settingsPending"].(map[string]any)) == 0
	})
	if e := firstDisplay(st)["error"].(map[string]any); !strings.Contains(e["text"].(string), "non ha confermato") {
		t.Fatalf("errore = %v", e)
	}
}

func TestWebSecurity(t *testing.T) {
	s := startOpts(t, false, 0, func(o *Options) { o.FirmwareDir = t.TempDir() })
	w := newWeb(t, s)
	connectDisplay(t, s, true)
	waitStatus(t, w, func(st map[string]any) bool { fd := firstDisplay(st); return fd != nil && fd["remote"] == true })
	p := "/api/displays/calamaio-3f2a/settings"

	// CSRF: un'altra pagina aperta nel browser.
	cases := []struct {
		name  string
		ctype string
		body  string
		hdr   []string
		want  int
	}{
		{"Origin diverso", "application/json", `{"led":false}`, []string{"Origin", "http://attaccante.example"}, 403},
		{"Origin null", "application/json", `{"led":false}`, []string{"Origin", "null"}, 403},
		{"Origin altra porta", "application/json", `{"led":false}`, []string{"Origin", "http://127.0.0.1:1"}, 403},
		{"Sec-Fetch-Site cross-site", "application/json", `{"led":false}`, []string{"Sec-Fetch-Site", "cross-site"}, 403},
		{"Sec-Fetch-Site same-site", "application/json", `{"led":false}`, []string{"Sec-Fetch-Site", "same-site"}, 403},
		{"form text/plain", "text/plain", `{"led":false}`, []string{"Origin", "http://" + w.host}, 415},
		{"form urlencoded", "application/x-www-form-urlencoded", `led=false`, nil, 415},
		{"senza Content-Type né origine", "", "", nil, 415},
		{"DNS rebinding", "application/json", `{"led":false}`, []string{"Host", "attaccante.example:8765"}, 403},
	}
	for _, c := range cases {
		code, body, _ := w.do(http.MethodPost, p, c.ctype, c.body, c.hdr...)
		if code != c.want {
			t.Errorf("%s: %d %v, atteso %d", c.name, code, body, c.want)
		}
	}
	// Un client locale senza browser (curl) con JSON passa.
	if code, _, _ := w.do(http.MethodPost, p, "application/json", `{"led":false}`); code != http.StatusAccepted {
		t.Fatalf("curl locale: %d", code)
	}

	// Host non locale su tutte le rotte della UI e di /api.
	for _, path := range []string{"/", "/index.html", "/api/status", "/api/events", "/api/firmware", "/api/firmware/parts/firmware.bin", "/api/boh"} {
		for _, host := range []string{"attaccante.example", "192.168.1.10:8765", "localhost.attaccante.example"} {
			if code, _, _ := w.do(http.MethodGet, path, "", "", "Host", host); code != http.StatusForbidden {
				t.Errorf("GET %s Host %s: %d", path, host, code)
			}
		}
	}
	for _, host := range []string{"localhost:8765", "127.0.0.1", "[::1]:8765"} {
		if code, _, _ := w.do(http.MethodGet, "/api/status", "", "", "Host", host); code != 200 {
			t.Errorf("Host %s: %d", host, code)
		}
	}

	// Header di sicurezza.
	for _, path := range []string{"/api/status", "/", "/api/boh"} {
		_, _, h := w.do(http.MethodGet, path, "", "")
		csp := h.Get("Content-Security-Policy")
		if !strings.Contains(csp, "default-src 'self'") || !strings.Contains(csp, "frame-ancestors 'none'") || strings.Contains(csp, "unsafe-inline") {
			t.Errorf("%s: CSP %q", path, csp)
		}
		if h.Get("X-Content-Type-Options") != "nosniff" || h.Get("X-Frame-Options") != "DENY" {
			t.Errorf("%s: header %v", path, h)
		}
	}
	if code, body, _ := w.do(http.MethodGet, "/api/boh", "", ""); code != 404 || body["error"] == nil {
		t.Errorf("/api/boh: %d %v", code, body)
	}
	if code, _, _ := w.do(http.MethodGet, "/hook", "", ""); code != http.StatusMethodNotAllowed {
		t.Errorf("GET /hook: %d", code)
	}

	// La webapp (se l'altro lavoro ha già scritto index.html).
	code, m, h := w.do(http.MethodGet, "/", "", "")
	if _, err := os.Stat("../webui/static/index.html"); err == nil {
		if code != 200 || h.Get("Content-Type") != "text/html; charset=utf-8" {
			t.Errorf("GET /: %d %s %v", code, h.Get("Content-Type"), m)
		}
	} else {
		t.Logf("index.html assente: GET / = %d", code)
	}
}

func TestWebFirmware(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("PLATFORMIO_CORE_DIR", t.TempDir())
	app := make([]byte, 3000)
	app[0], app[12] = 0xE9, 9
	files := map[string][]byte{"bootloader.bin": {0xE9, 1}, "partitions.bin": {0xAA, 0x50}, "boot_app0.bin": {1}, "firmware.bin": app}
	for n, b := range files {
		if err := os.WriteFile(filepath.Join(dir, n), b, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(dir, "manifest.json"), []byte(`{"version":"0.3.0"}`), 0o644); err != nil {
		t.Fatal(err)
	}
	s := startOpts(t, false, 0, func(o *Options) { o.FirmwareDir = dir })
	w := newWeb(t, s)
	code, fw, _ := w.do(http.MethodGet, "/api/firmware", "", "")
	if code != 200 || fw["available"] != true || fw["version"] != "0.3.0" || fw["chip"] != "ESP32-S3" {
		t.Fatalf("firmware: %d %v", code, fw)
	}
	parts := fw["parts"].([]any)
	if len(parts) != 4 {
		t.Fatalf("parti = %v", parts)
	}
	for _, x := range parts {
		p := x.(map[string]any)
		resp, err := http.Get(w.base + p["url"].(string))
		if err != nil {
			t.Fatal(err)
		}
		b, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		sum := sha256.Sum256(b)
		if resp.StatusCode != 200 || hex.EncodeToString(sum[:]) != p["sha256"] || int(p["size"].(float64)) != len(b) ||
			resp.Header.Get("Content-Type") != "application/octet-stream" {
			t.Errorf("parte %v: %d, %d byte", p, resp.StatusCode, len(b))
		}
	}
	if code, _, _ := w.do(http.MethodGet, "/api/firmware/parts/..%2fmanifest.json", "", ""); code != 404 {
		t.Errorf("percorso fuori pacchetto: %d", code)
	}

	// Cartella vuota: available false, 200.
	s2 := startOpts(t, false, 0, func(o *Options) { o.FirmwareDir = t.TempDir() })
	code, fw, _ = newWeb(t, s2).do(http.MethodGet, "/api/firmware", "", "")
	if code != 200 || fw["available"] != false || fw["problem"] == nil {
		t.Fatalf("senza pacchetto: %d %v", code, fw)
	}
}

// Lo spegnimento non resta appeso a un /api/events aperto.
func TestShutdownWithOpenSSE(t *testing.T) {
	s := startOpts(t, false, 0, func(o *Options) { o.FirmwareDir = t.TempDir() })
	events := sse(t, newWeb(t, s))
	<-events
	begin := time.Now()
	s.cancel()
	select {
	case <-s.done:
	case <-time.After(2500 * time.Millisecond):
		t.Fatal("Run non si ferma con un SSE aperto")
	}
	if d := time.Since(begin); d > time.Second {
		t.Fatalf("spegnimento in %s", d)
	}
	s.done <- nil // per il Cleanup di start
}

// /api/status porta lo stato dell'avvio automatico (M6), null se il servizio non lo sa.
func TestStatusAutostart(t *testing.T) {
	s := start(t, true, 0)
	if v, ok := newWeb(t, s).status()["autostart"]; !ok || v != nil {
		t.Fatalf("senza Autostart: %v %v", v, ok)
	}
	s = startOpts(t, true, 0, func(o *Options) {
		o.Autostart = func() any { return map[string]any{"supported": true, "installed": false} }
	})
	a, _ := newWeb(t, s).status()["autostart"].(map[string]any)
	if a == nil || a["supported"] != true || a["installed"] != false {
		t.Fatalf("autostart: %v", a)
	}
}

// text: il corpo di una GET come testo.
func (w *web) text(path string) string {
	w.t.Helper()
	resp, err := http.Get(w.base + path)
	if err != nil {
		w.t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return string(b)
}
