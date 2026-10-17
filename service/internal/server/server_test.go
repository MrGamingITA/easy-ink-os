package server

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"log"
	"net"
	"net/http"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"

	"calamaio/internal/client"
	"calamaio/internal/config"
	"calamaio/internal/display"
	"calamaio/internal/fake"
)

type started struct {
	run    Running
	cfg    config.Config
	cancel context.CancelFunc
	done   chan error
	keys   *config.KeyStore // in memoria: mai il config.json vero
}

// start avvia `serve` vero su due porte libere.
func start(t *testing.T, fk bool, every time.Duration) *started {
	t.Helper()
	return startOpts(t, fk, every, nil)
}

// startOpts come start, con tune che può cambiare le opzioni prima dell'avvio.
func startOpts(t *testing.T, fk bool, every time.Duration, tune func(*Options)) *started {
	t.Helper()
	apiLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	// Nei test solo loopback: su tutte le interfacce il firewall di macOS potrebbe chiedere il permesso.
	wsLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	cfg := config.Default()
	cfg.APIPort = apiLn.Addr().(*net.TCPAddr).Port
	cfg.WSPort = wsLn.Addr().(*net.TCPAddr).Port
	cfg.TZ = "CET-1CEST,M3.5.0,M10.5.0/3"

	ready := make(chan Running, 1)
	ctx, cancel := context.WithCancel(context.Background())
	s := &started{cfg: cfg, cancel: cancel, done: make(chan error, 1), keys: &config.KeyStore{}}
	o := Options{
		Config: cfg, Fake: fk, FakeInterval: every, Version: "test",
		Log:         log.New(io.Discard, "", 0),
		APIListener: apiLn, WSListener: wsLn,
		TuneHub: func(h *display.Hub) { h.MinInterval = 50 * time.Millisecond },
		Ready:   func(r Running) { ready <- r },
		// mai il file vero dell'utente
		TodayFile: filepath.Join(t.TempDir(), "oggi.json"),
		Keys:      s.keys,
		AppListen: func() (net.Listener, error) { return net.Listen("tcp", "127.0.0.1:0") },
		AppHosts:  func() []string { return []string{"127.0.0.1"} },
	}
	if tune != nil {
		tune(&o)
	}
	go func() { s.done <- Run(ctx, o) }()
	select {
	case s.run = <-ready:
	case err := <-s.done:
		t.Fatalf("Run: %v", err)
	}
	t.Cleanup(func() {
		cancel()
		select {
		case <-s.done:
		case <-time.After(5 * time.Second):
			t.Error("Run non si ferma")
		}
	})
	return s
}

func dialHello(t *testing.T, port int) (*websocket.Conn, chan map[string]any) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	ws, _, err := websocket.Dial(ctx, "ws://127.0.0.1:"+itoa(port)+"/ws", nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = ws.CloseNow() })
	ch := make(chan map[string]any, 256)
	go func() {
		for {
			_, b, err := ws.Read(context.Background())
			if err != nil {
				return
			}
			var m map[string]any
			_ = json.Unmarshal(b, &m)
			ch <- m
		}
	}()
	err = ws.Write(ctx, websocket.MessageText, []byte(`{"t":"hello","v":1,"board":"calamaio-3f2a","device":"heltec213","class":"S","fw":"0.3.0","w":250,"h":122}`))
	if err != nil {
		t.Fatal(err)
	}
	return ws, ch
}

func next(t *testing.T, ch chan map[string]any) map[string]any {
	t.Helper()
	select {
	case m := <-ch:
		return m
	case <-time.After(3 * time.Second):
		t.Fatal("nessun messaggio")
		return nil
	}
}

func sessionState(m map[string]any, name string) (string, string) {
	ss, _ := m["sessions"].([]any)
	for _, x := range ss {
		s := x.(map[string]any)
		if s["name"] == name {
			return s["state"].(string), s["detail"].(string)
		}
	}
	return "", ""
}

func TestServeFakeEndToEnd(t *testing.T) {
	s := start(t, true, 100*time.Millisecond)
	_, ch := dialHello(t, s.cfg.WSPort)
	if w := next(t, ch); w["t"] != "welcome" || w["tz"] != s.cfg.TZ {
		t.Fatalf("welcome = %v", w)
	}
	first := next(t, ch)
	if first["t"] != "state" || len(first["sessions"].([]any)) != 4 {
		t.Fatalf("primo state = %v", first)
	}
	if st, d := sessionState(first, "web-app"); st != "permission" || d != "Bash: npm run test" {
		t.Fatalf("web-app = %s %q: deve partire come l'esempio", st, d)
	}
	// Il generatore lo fa evolvere.
	b1, _ := json.Marshal(first["sessions"])
	b2, _ := json.Marshal(next(t, ch)["sessions"])
	if bytes.Equal(b1, b2) {
		t.Fatal("il secondo state è uguale al primo")
	}

	// `calamaio fake` → POST /debug/fake → nuovo state sul WebSocket.
	var out bytes.Buffer
	d := "Bash: rm -rf build"
	if err := client.Fake(s.cfg, fake.ForceRequest{Session: "docs-site", State: "permission", Detail: &d}, &out); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "docs-site → permission") {
		t.Fatalf("uscita di fake: %q", out.String())
	}
	deadline := time.After(2 * time.Second)
	for {
		var m map[string]any
		select {
		case m = <-ch:
		case <-deadline:
			t.Fatal("lo stato forzato non arriva al display")
		}
		if st, dd := sessionState(m, "docs-site"); st == "permission" && dd == d {
			break
		}
	}

	// `calamaio status` vede il display e le sessioni.
	out.Reset()
	if err := client.Status(s.cfg, &out, time.Now()); err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"Display collegati (1)", "calamaio-3f2a", "heltec213", "127.0.0.1", "Sessioni (4)", "docs-site", "permission", "Ultimi eventi", "chiede permesso: rm -rf build", "stato finto"} {
		if !strings.Contains(out.String(), want) {
			t.Fatalf("status senza %q:\n%s", want, out.String())
		}
	}
	t.Logf("calamaio status:\n%s", out.String())
}

func TestServeWithoutFake(t *testing.T) {
	s := start(t, false, 0)
	_, ch := dialHello(t, s.cfg.WSPort)
	next(t, ch)
	st := next(t, ch)
	if len(st["sessions"].([]any)) != 0 || st["limits"] != nil {
		t.Fatalf("senza --fake lo stato deve essere vuoto: %v", st)
	}
	err := client.Fake(s.cfg, fake.ForceRequest{Session: "x", State: "idle"}, io.Discard)
	if err == nil || !strings.Contains(err.Error(), "--fake") {
		t.Fatalf("fake senza --fake: %v", err)
	}
}

func TestDebugOnlyLocal(t *testing.T) {
	s := start(t, true, 0)
	base := "http://" + s.run.APIAddr
	req, _ := http.NewRequest(http.MethodGet, base+"/debug", nil)
	req.Host = "attaccante.example:8765" // DNS rebinding
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusForbidden {
		t.Fatalf("Host estraneo: %d", resp.StatusCode)
	}
	// Un form da browser (text/plain, senza preflight) non può forzare lo stato.
	resp, err = http.Post(base+"/debug/fake", "text/plain", strings.NewReader(`{"session":"web-app","state":"idle"}`))
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusUnsupportedMediaType {
		t.Fatalf("text/plain: %d", resp.StatusCode)
	}
}

func itoa(n int) string {
	b, _ := json.Marshal(n)
	return string(b)
}
