package server

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
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
	tok, id := m["token"].(string), m["id"].(string)
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
		{"GET", "/api/phones", 404}, {"POST", "/api/phones/pair", 404}, {"POST", "/api/phones/pair/cancel", 404},
		{"DELETE", "/api/phones/" + id, 404}, {"GET", "/firmware.html", 404}, {"GET", "/api/firmware", 404},
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
		Phone struct{ Name string }    `json:"phone"`
		PC    struct{ Version string } `json:"pc"`
		Hosts []string                 `json:"hosts"`
		Port  int                      `json:"port"`
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
	if strings.Contains(w.text("/api/phones"), tok) {
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

func TestAppNoHostsNoWindow(t *testing.T) {
	s := startOpts(t, true, 0, func(o *Options) { o.AppHosts = func() []string { return nil } })
	w := newWeb(t, s)
	if c, m := w.post("/api/phones/pair", `{}`); c != 409 || !strings.Contains(m["error"].(string), "rete") {
		t.Fatalf("senza rete: %d %v", c, m)
	}
	if s.run.AppAddr() != "" || phonesView(t, w)["pairing"] != nil {
		t.Fatal("finestra aperta senza rete")
	}
}

func TestAppSyncAfterCloseDoesNotListen(t *testing.T) {
	s := start(t, true, 0)
	w := newWeb(t, s)
	openPairing(t, w)
	s.cancel()
	err := <-s.done
	s.done <- err
	s.run.Phones.OpenWindow() // finisce durante lo spegnimento: non deve riaccendere
	if s.run.AppAddr() != "" {
		t.Fatal("porta riaccesa dopo close")
	}
}

// QR: SVG del link finché la finestra è aperta, 404 altrimenti (anche in HEAD, come la webapp lo prova).
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
	if c, _, _ := w.do(http.MethodHead, "/api/phones/pair/qr.svg", "", ""); c != 200 {
		t.Fatalf("HEAD a finestra aperta: %d", c)
	}
	w.post("/api/phones/pair/cancel", `{}`)
	if c, _, _ := w.do(http.MethodGet, "/api/phones/pair/qr.svg", "", ""); c != 404 {
		t.Fatalf("dopo l'annullamento: %d", c)
	}
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

// annunci finti dei telefoni: porta e prefisso dell'impronta di ogni annuncio, ritiri contati.
type fakeAnnounce struct {
	mu    sync.Mutex
	ports []int
	fps   []string
	stops int
}

func (f *fakeAnnounce) announce(port int, fp string) func() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.ports, f.fps = append(f.ports, port), append(f.fps, fp)
	return func() { f.mu.Lock(); f.stops++; f.mu.Unlock() }
}

func (f *fakeAnnounce) state() (n, stops int) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return len(f.ports), f.stops
}

func TestAppAnnounceLifecycle(t *testing.T) {
	f := &fakeAnnounce{}
	s := startOpts(t, true, 0, func(o *Options) { o.MDNS, o.AnnounceApp = true, f.announce })
	w := newWeb(t, s)
	if n, _ := f.state(); n != 0 {
		t.Fatalf("annuncio a porta spenta: %d", n)
	}
	// finestra aperta: un annuncio con la porta e l'impronta del QR, poi ritirato all'annullamento
	_, fp, _ := openPairing(t, w)
	if n, st := f.state(); n != 1 || st != 0 || f.fps[0] != fp || f.ports[0] != tcpPortOf(t, s.run.AppAddr()) {
		t.Fatalf("annunci %d ritiri %d: %v %v (porta %s)", n, st, f.ports, f.fps, s.run.AppAddr())
	}
	w.post("/api/phones/pair/cancel", `{}`)
	eventually(t, "annuncio ritirato all'annullamento", func() bool { _, st := f.state(); return st == 1 })
	// telefono abbinato: annuncio di nuovo; la revoca dell'ultimo lo ritira
	_, id := pairedPhone(t, w)
	eventually(t, "secondo annuncio", func() bool { n, _ := f.state(); return n == 2 })
	if c := revoke(t, w, id); c != 204 {
		t.Fatalf("revoca: %d", c)
	}
	eventually(t, "annuncio ritirato alla revoca", func() bool { _, st := f.state(); return st == 2 })
	// chiusura con la porta accesa: ritirato anche lì
	openPairing(t, w)
	eventually(t, "terzo annuncio", func() bool { n, _ := f.state(); return n == 3 })
	s.cancel()
	select {
	case err := <-s.done:
		s.done <- err // la pulizia di startOpts lo aspetta ancora
	case <-time.After(5 * time.Second):
		t.Fatal("Run non si ferma")
	}
	if _, st := f.state(); st != 3 {
		t.Fatalf("ritiri alla chiusura: %d", st)
	}
}

// --loopback spegne mDNS (display e telefoni): nessun annuncio e una riga nel registro.
func TestLoopbackNoMDNS(t *testing.T) {
	f := &fakeAnnounce{}
	var buf syncBuf
	s := startOpts(t, true, 0, func(o *Options) {
		o.MDNS, o.Loopback, o.AnnounceApp = true, true, f.announce
		o.Log = log.New(&buf, "", 0)
	})
	openPairing(t, newWeb(t, s))
	if n, _ := f.state(); n != 0 {
		t.Fatalf("annunci con --loopback: %d", n)
	}
	if !strings.Contains(buf.String(), "mdns: spento con --loopback") {
		t.Fatalf("registro: %q", buf.String())
	}
}

type syncBuf struct {
	mu sync.Mutex
	b  strings.Builder
}

func (s *syncBuf) Write(p []byte) (int, error) { s.mu.Lock(); defer s.mu.Unlock(); return s.b.Write(p) }
func (s *syncBuf) String() string              { s.mu.Lock(); defer s.mu.Unlock(); return s.b.String() }

func tcpPortOf(t *testing.T, addr string) int {
	t.Helper()
	_, p, err := net.SplitHostPort(addr)
	if err != nil {
		t.Fatal(err)
	}
	n, _ := strconv.Atoi(p)
	return n
}

func TestAppRetryDelay(t *testing.T) {
	want := []time.Duration{time.Second, 2 * time.Second, 5 * time.Second, 10 * time.Second, 10 * time.Second, 10 * time.Second}
	for n, w := range want {
		if d := appRetryDelay(n); d != w {
			t.Fatalf("tentativo %d: %s, atteso %s", n, d, w)
		}
	}
}

// busyListen: le prime fail chiamate rispondono «porta occupata», poi apre davvero (fail < 0: sempre occupata).
func busyListen(fail int, calls *atomic.Int32) func() (net.Listener, error) {
	return func() (net.Listener, error) {
		if n := calls.Add(1); fail < 0 || int(n) <= fail {
			return nil, errors.New("listen tcp :8767: bind: address already in use")
		}
		return net.Listen("tcp", "127.0.0.1:0")
	}
}

// R2: al riavvio la porta è ancora del servizio di prima. Con un telefono abbinato si riprova da sola.
func TestAppPortBusyAtStartRetries(t *testing.T) {
	dir := t.TempDir()
	files := func(o *Options) {
		o.PhonesFile = filepath.Join(dir, "phones.json")
		o.AppTLSDir = filepath.Join(dir, "app-tls")
	}
	s := startOpts(t, true, 0, files)
	pairedPhone(t, newWeb(t, s))
	s.cancel()
	err := <-s.done
	s.done <- err
	var calls atomic.Int32
	s2 := startOpts(t, true, 0, func(o *Options) {
		files(o)
		o.AppListen = busyListen(2, &calls)
		o.AppRetry = func(int) time.Duration { return 20 * time.Millisecond }
	})
	eventually(t, "porta riaperta dopo due tentativi falliti", func() bool { return s2.run.AppAddr() != "" })
	if n := calls.Load(); n != 3 {
		t.Fatalf("tentativi: %d, attesi 3", n)
	}
	if e, ok := phonesView(t, newWeb(t, s2))["app"].(map[string]any)["error"]; ok {
		t.Fatalf("errore rimasto nella vista: %v", e)
	}
}

// Nessun telefono e finestra chiusa: niente tentativi in sottofondo.
func TestAppRetryStopsWhenNotWanted(t *testing.T) {
	var calls atomic.Int32
	s := startOpts(t, true, 0, func(o *Options) {
		o.AppListen = busyListen(-1, &calls)
		o.AppRetry = func(int) time.Duration { return 20 * time.Millisecond }
	})
	w := newWeb(t, s)
	if c, _ := w.post("/api/phones/pair", `{}`); c != 409 {
		t.Fatalf("porta occupata: %d", c)
	}
	time.Sleep(50 * time.Millisecond)
	n := calls.Load()
	time.Sleep(200 * time.Millisecond)
	if m := calls.Load(); m != n || n > 2 {
		t.Fatalf("tentativi senza finestra né telefoni: %d poi %d", n, m)
	}
}

// Allo spegnimento i tentativi si fermano.
func TestAppRetryStopsOnClose(t *testing.T) {
	dir := t.TempDir()
	files := func(o *Options) {
		o.PhonesFile = filepath.Join(dir, "phones.json")
		o.AppTLSDir = filepath.Join(dir, "app-tls")
	}
	s := startOpts(t, true, 0, files)
	pairedPhone(t, newWeb(t, s))
	s.cancel()
	err := <-s.done
	s.done <- err
	var calls atomic.Int32
	s2 := startOpts(t, true, 0, func(o *Options) {
		files(o)
		o.AppListen = busyListen(-1, &calls)
		o.AppRetry = func(int) time.Duration { return 20 * time.Millisecond }
	})
	eventually(t, "almeno due tentativi", func() bool { return calls.Load() >= 2 })
	s2.cancel()
	err = <-s2.done
	s2.done <- err
	n := calls.Load()
	time.Sleep(200 * time.Millisecond)
	if m := calls.Load(); m != n {
		t.Fatalf("tentativi dopo lo spegnimento: %d poi %d", n, m)
	}
}
