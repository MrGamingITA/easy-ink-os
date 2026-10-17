package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/santhosh-tekuri/jsonschema/v6"

	"calamaio/internal/client"
	"calamaio/internal/config"
	"calamaio/internal/display"
	"calamaio/internal/fakedisplay"
	"calamaio/internal/m5"
	"calamaio/internal/server"
)

// M5 dall'esterno: servizio vero su porte libere, display finto con la stessa crittografia
// (internal/fakedisplay), `calamaio hook --wait` vero lanciato come processo (questo stesso binario di
// test, vedi TestMain), config in una cartella temporanea.

func TestMain(m *testing.M) {
	if os.Getenv("CALAMAIO_TEST_MAIN") == "1" {
		main() // il processo figlio fa da `calamaio` (os.Args = binario, hook, --wait)
		os.Exit(0)
	}
	os.Exit(m.Run())
}

const (
	board   = "calamaio-3f2a"
	session = "8c1d4e2a-6b3f-4c5d-9e7f-0a1b2c3d4e5f"
	// PermissionRequest come lo manda Claude Code (campi delle catture in protocol/samples/fixtures).
	permPayload = `{"session_id":"` + session + `","transcript_path":"/nessuno/transcript.jsonl","cwd":"/home/utente/web-app",` +
		`"permission_mode":"default","hook_event_name":"PermissionRequest","tool_name":"Bash","tool_input":{"command":"npm run test","description":"test"}}`
	allowOut = `{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}` + "\n"
	denyOut  = `{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"deny","message":"Negato dal display Calamaio"}}}` + "\n"
)

type syncBuf struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (s *syncBuf) Write(p []byte) (int, error) { s.mu.Lock(); defer s.mu.Unlock(); return s.b.Write(p) }
func (s *syncBuf) String() string              { s.mu.Lock(); defer s.mu.Unlock(); return s.b.String() }

type fakeClock struct {
	mu  sync.Mutex
	off time.Duration
}

func (c *fakeClock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return time.Now().Add(c.off) }
func (c *fakeClock) Advance(d time.Duration) {
	c.mu.Lock()
	c.off += d
	c.mu.Unlock()
}

type svc struct {
	t      *testing.T
	run    server.Running
	cfg    config.Config
	cfgDir string
	keys   *config.KeyStore
	log    *syncBuf
	cancel context.CancelFunc
	done   chan error
	wsURL  string
	base   string
	clock  *fakeClock
}

func startSvc(t *testing.T, pairWindow time.Duration) *svc {
	t.Helper()
	return startSvcWith(t, pairWindow, nil)
}

// startSvcWith: come startSvc, con tune che cambia le opzioni del servizio prima dell'avvio.
func startSvcWith(t *testing.T, pairWindow time.Duration, tune func(dir string, o *server.Options)) *svc {
	t.Helper()
	apiLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	wsLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	cfg := config.Default()
	cfg.APIPort = apiLn.Addr().(*net.TCPAddr).Port
	cfg.WSPort = wsLn.Addr().(*net.TCPAddr).Port
	cfg.TZ = "CET-1CEST,M3.5.0,M10.5.0/3"
	dir := t.TempDir()
	s := &svc{t: t, cfg: cfg, cfgDir: dir, keys: &config.KeyStore{Path: filepath.Join(dir, "config.json")},
		log: &syncBuf{}, done: make(chan error, 1), clock: &fakeClock{},
		wsURL: fmt.Sprintf("ws://127.0.0.1:%d/ws", cfg.WSPort), base: fmt.Sprintf("http://127.0.0.1:%d", cfg.APIPort)}
	ready := make(chan server.Running, 1)
	ctx, cancel := context.WithCancel(context.Background())
	s.cancel = cancel
	o := server.Options{
		Config: cfg, Version: "test", Log: log.New(s.log, "", 0),
		APIListener: apiLn, WSListener: wsLn,
		TodayFile: filepath.Join(dir, "oggi.json"), BackupDir: filepath.Join(dir, "backups"), FirmwareDir: dir,
		Keys: s.keys,
		TuneHub: func(h *display.Hub) {
			h.MinInterval = 50 * time.Millisecond
			h.Now = s.clock.Now
			if pairWindow > 0 {
				h.PairWindow = pairWindow
			}
		},
		Ready: func(r server.Running) { ready <- r },
	}
	if tune != nil {
		tune(dir, &o)
	}
	go func() { s.done <- server.Run(ctx, o) }()
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
			t.Error("il servizio non si ferma")
		}
		if t.Failed() {
			t.Logf("log del servizio:\n%s", s.log)
		}
	})
	return s
}

func (s *svc) dial(K []byte, permOn bool) *fakedisplay.Display {
	s.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.Dial(ctx, s.wsURL, board, K, permOn)
	if err != nil {
		s.t.Fatal(err)
	}
	s.t.Cleanup(d.Close)
	if _, err := d.Await("welcome", 2*time.Second); err != nil {
		s.t.Fatal(err)
	}
	return d
}

func await(t *testing.T, d *fakedisplay.Display, typ string) fakedisplay.Event {
	t.Helper()
	e, err := d.Await(typ, 3*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	return e
}

// post: come la CLI (JSON, senza Origin).
func (s *svc) post(path string, body any) (int, map[string]any) {
	s.t.Helper()
	b, _ := json.Marshal(body)
	resp, err := http.Post(s.base+path, "application/json", bytes.NewReader(b))
	if err != nil {
		s.t.Fatal(err)
	}
	defer resp.Body.Close()
	var m map[string]any
	_ = json.NewDecoder(resp.Body).Decode(&m)
	return resp.StatusCode, m
}

func (s *svc) status() display.DisplayStatus {
	s.t.Helper()
	for _, d := range s.run.Hub.Status() {
		if d.Board == board {
			return d
		}
	}
	return display.DisplayStatus{}
}

func waitFor(t *testing.T, what string, ok func() bool) {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for !ok() {
		if time.Now().After(deadline) {
			t.Fatalf("non è successo: %s", what)
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// pair abbina d attraverso la API della webapp (codice letto dal display).
func (s *svc) pair(d *fakedisplay.Display) {
	s.t.Helper()
	waitFor(s.t, "display collegato", func() bool { return s.status().Board == board })
	if code, m := s.post("/api/displays/"+board+"/pair/start", map[string]any{}); code != 202 {
		s.t.Fatalf("pair/start: %d %v", code, m)
	}
	e := await(s.t, d, "pair-code")
	if code, m := s.post("/api/displays/"+board+"/pair/code", map[string]any{"code": e.Text}); code != 202 {
		s.t.Fatalf("pair/code: %d %v", code, m)
	}
	await(s.t, d, "pair-ready")
	if err := d.Confirm(); err != nil {
		s.t.Fatal(err)
	}
	await(s.t, d, "authed")
	waitFor(s.t, "abbinato e autenticato", func() bool { return s.status().Pair == "ok" })
}

// pairedDisplay: display abbinato, autenticato e con i permessi accesi.
func (s *svc) pairedDisplay() *fakedisplay.Display {
	s.t.Helper()
	d := s.dial(nil, true)
	s.pair(d)
	waitFor(s.t, "permessi dal display attivi", func() bool { return s.status().Perms })
	return d
}

type hookProc struct {
	cmd  *exec.Cmd
	out  *syncBuf
	errb *syncBuf
	done chan struct{}
	at   time.Time
	took time.Duration
}

// hook lancia `calamaio hook --wait` vero con payload su stdin.
func (s *svc) hook(payload string) *hookProc {
	s.t.Helper()
	cmd := exec.Command(os.Args[0], "hook", "--wait")
	// GORACE: con -race il processo figlio aspetterebbe 1 s all'uscita (atexit_sleep_ms) e i limiti di
	// tempo misurerebbero quello invece del servizio.
	cmd.Env = append(os.Environ(), "GORACE=atexit_sleep_ms=0", "CALAMAIO_TEST_MAIN=1", "CALAMAIO_CONFIG_DIR="+s.cfgDir,
		"CALAMAIO_DATA_DIR="+filepath.Join(s.cfgDir, "dati"), "CALAMAIO_API_PORT="+strconv.Itoa(s.cfg.APIPort))
	cmd.Stdin = strings.NewReader(payload)
	p := &hookProc{cmd: cmd, out: &syncBuf{}, errb: &syncBuf{}, done: make(chan struct{}), at: time.Now()}
	cmd.Stdout, cmd.Stderr = p.out, p.errb
	if err := cmd.Start(); err != nil {
		s.t.Fatal(err)
	}
	go func() {
		_ = cmd.Wait()
		p.took = time.Since(p.at)
		close(p.done)
	}()
	s.t.Cleanup(func() { _ = cmd.Process.Kill(); <-p.done })
	return p
}

func (p *hookProc) wait(t *testing.T, d time.Duration) string {
	t.Helper()
	select {
	case <-p.done:
	case <-time.After(d):
		t.Fatalf("il hook non è uscito entro %s", d)
	}
	if e := p.errb.String(); e != "" {
		t.Fatalf("il hook ha scritto su stderr: %q", e)
	}
	if code := p.cmd.ProcessState.ExitCode(); code != 0 && code != -1 {
		t.Fatalf("il hook è uscito con %d", code)
	}
	return p.out.String()
}

func (p *hookProc) running() bool {
	select {
	case <-p.done:
		return false
	default:
		return true
	}
}

// --- schema: ogni messaggio del servizio visto dal display finto deve rispettarlo ---

func compileSchema(t *testing.T) *jsonschema.Schema {
	t.Helper()
	f, err := os.Open(filepath.Join("..", "..", "..", "protocol", "schema", "message.schema.json"))
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
	if err := c.AddResource("message.schema.json", doc); err != nil {
		t.Fatal(err)
	}
	sch, err := c.Compile("message.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	return sch
}

var serviceTypes = map[string]bool{"welcome": true, "state": true, "set": true, "full-refresh": true, "ack": true,
	"pair-begin": true, "pair-reveal": true, "pair-confirm": true, "pair-abort": true, "auth-challenge": true,
	"auth-ok": true, "auth-fail": true, "perm-request": true, "perm-close": true,
	"ota-key": true, "ota-offer": true, "ota-cancel": true, "log-start": true, "log-stop": true, "avatar": true}

func checkSchema(t *testing.T, d *fakedisplay.Display) {
	t.Helper()
	sch := compileSchema(t)
	n := 0
	for _, e := range d.Log() {
		if e.Msg == nil || e.Type == "rejected" {
			continue
		}
		typ, _ := e.Msg["t"].(string)
		if !serviceTypes[typ] {
			continue
		}
		b, _ := json.Marshal(e.Msg)
		v, _ := jsonschema.UnmarshalJSON(bytes.NewReader(b))
		if err := sch.Validate(v); err != nil {
			t.Errorf("%s dal servizio non rispetta lo schema: %v\n%s", typ, err, b)
		}
		n++
	}
	if n == 0 {
		t.Error("nessun messaggio del servizio controllato")
	}
}

// ---------- abbinamento ----------

func TestPairCLI(t *testing.T) {
	s := startSvc(t, 0)
	d := s.dial(nil, true)
	waitFor(t, "display collegato", func() bool { return s.status().Board == board })
	if st := s.status(); st.Pair != "none" || st.Paired {
		t.Fatalf("prima dell'abbinamento: %+v", st)
	}

	pr, pw := io.Pipe()
	out := &syncBuf{}
	errc := make(chan error, 1)
	go func() { errc <- client.Pair(s.cfg, "", pr, out, make(chan os.Signal)) }()
	e := await(t, d, "pair-code")
	waitFor(t, "la domanda del codice", func() bool {
		return strings.Contains(out.String(), "Leggi il codice sul display e scrivilo qui:")
	})
	// Il codice non esce mai dal servizio: né nello stato né nel debug.
	b, _ := json.Marshal(s.run.Hub.Status())
	if bytes.Contains(b, []byte(`"code":`)) || bytes.Contains(b, []byte(`"sas":`)) || bytes.Contains(b, []byte(`"`+e.Text+`"`)) {
		t.Fatalf("il codice compare nello stato del servizio: %s", b)
	}
	fmt.Fprintln(pw, "12 34") // non sono 6 cifre: si richiede, niente annullato
	waitFor(t, "la richiesta di 6 cifre", func() bool { return strings.Contains(out.String(), "Servono le 6 cifre") })
	fmt.Fprintln(pw, e.Text[:3]+" "+e.Text[3:]) // con lo spazio vale lo stesso
	await(t, d, "pair-ready")
	waitFor(t, "la richiesta della conferma sul display", func() bool { return strings.Contains(out.String(), "conferma sul display") })
	if err := d.Confirm(); err != nil {
		t.Fatal(err)
	}
	await(t, d, "authed")
	select {
	case err := <-errc:
		if err != nil {
			t.Fatalf("Pair: %v\n%s", err, out)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("calamaio pair non finisce")
	}
	if !strings.Contains(out.String(), "Abbinato: "+board+". Connessione autenticata.") {
		t.Fatalf("output: %q", out.String())
	}
	// K salvata in config.json, 0600, uguale a quella del display.
	fi, err := os.Stat(s.keys.Path)
	if err != nil {
		t.Fatal(err)
	}
	if fi.Mode().Perm() != 0o600 {
		t.Fatalf("permessi di config.json: %v", fi.Mode().Perm())
	}
	list, _ := s.keys.List()
	if len(list) != 1 || list[0].Board != board || list[0].Key != m5.Hex(d.Key()) || list[0].Created == "" {
		t.Fatalf("paired = %+v", list)
	}
	if st := s.status(); st.Pair != "ok" || !st.Paired || !st.Authed || st.Pairing.Stage != display.StageDone {
		t.Fatalf("dopo l'abbinamento: %+v", st)
	}
	checkSchema(t, d)
}

func TestPairWrongCode(t *testing.T) {
	s := startSvc(t, 0)
	d := s.dial(nil, true)
	waitFor(t, "display collegato", func() bool { return s.status().Board == board })
	pr, pw := io.Pipe()
	out := &syncBuf{}
	errc := make(chan error, 1)
	go func() { errc <- client.Pair(s.cfg, board, pr, out, make(chan os.Signal)) }()
	e := await(t, d, "pair-code")
	n, _ := strconv.Atoi(e.Text)
	fmt.Fprintf(pw, "%06d\n", (n+1)%1000000)
	ab := await(t, d, "pair-abort")
	if ab.Text != "codice" {
		t.Fatalf("pair-abort %q", ab.Text)
	}
	if err := <-errc; err != client.ErrPairFailed {
		t.Fatalf("Pair: %v", err)
	}
	if !strings.Contains(out.String(), "codice sbagliato") || !strings.Contains(out.String(), "Nessuna chiave salvata") {
		t.Fatalf("output: %q", out.String())
	}
	if list, _ := s.keys.List(); len(list) != 0 {
		t.Fatalf("chiave salvata con il codice sbagliato: %+v", list)
	}
	if _, err := os.Stat(s.keys.Path); err == nil {
		t.Fatal("config.json scritto con il codice sbagliato")
	}
	if d.Key() != nil {
		t.Fatal("il display ha salvato una chiave")
	}
	if st := s.status(); st.Pair != "none" || st.Pairing.Stage != display.StageAborted || st.Pairing.Reason != "codice" {
		t.Fatalf("stato: %+v %+v", st, st.Pairing)
	}
	// Una seconda prova con lo stesso abbinamento non c'è: va ricominciato.
	if code, _ := s.post("/api/displays/"+board+"/pair/code", map[string]any{"code": e.Text}); code != http.StatusConflict {
		t.Fatalf("codice dopo l'annullamento: %d", code)
	}
}

func TestPairTimeoutAndCancel(t *testing.T) {
	s := startSvc(t, 300*time.Millisecond)
	d := s.dial(nil, true)
	waitFor(t, "display collegato", func() bool { return s.status().Board == board })

	// Tempo scaduto.
	if code, m := s.post("/api/displays/"+board+"/pair/start", map[string]any{}); code != 202 {
		t.Fatalf("start: %d %v", code, m)
	}
	if code, _ := s.post("/api/displays/"+board+"/pair/start", map[string]any{}); code != http.StatusConflict {
		t.Fatalf("un secondo abbinamento in corso: %d", code)
	}
	await(t, d, "pair-code")
	if ab := await(t, d, "pair-abort"); ab.Text != "tempo" {
		t.Fatalf("pair-abort %q", ab.Text)
	}
	if st := s.status(); st.Pairing.Stage != display.StageAborted || st.Pairing.Reason != "tempo" {
		t.Fatalf("stato: %+v", st.Pairing)
	}

	// Annullato dal PC.
	s.post("/api/displays/"+board+"/pair/start", map[string]any{})
	await(t, d, "pair-code")
	if code, _ := s.post("/api/displays/"+board+"/pair/cancel", map[string]any{}); code != 202 {
		t.Fatalf("cancel: %d", code)
	}
	if ab := await(t, d, "pair-abort"); ab.Text != "annullato" {
		t.Fatalf("pair-abort %q", ab.Text)
	}

	// Annullato dal display (pressione lunga) mentre `calamaio pair` aspetta il codice.
	pr, _ := io.Pipe()
	out := &syncBuf{}
	errc := make(chan error, 1)
	go func() { errc <- client.Pair(s.cfg, board, pr, out, make(chan os.Signal)) }()
	await(t, d, "pair-code")
	d.CancelPair("display")
	select {
	case err := <-errc:
		if err != client.ErrPairFailed || !strings.Contains(out.String(), "annullato sul display") {
			t.Fatalf("Pair: %v %q", err, out.String())
		}
	case <-time.After(3 * time.Second):
		t.Fatal("calamaio pair non si accorge dell'annullamento sul display")
	}

	// Ctrl-C su `calamaio pair`: pair-abort al display.
	sig := make(chan os.Signal, 1)
	go func() { errc <- client.Pair(s.cfg, board, pr, out, sig) }()
	await(t, d, "pair-code")
	sig <- os.Interrupt
	if err := <-errc; err != client.ErrPairFailed {
		t.Fatalf("Pair dopo Ctrl-C: %v", err)
	}
	if ab := await(t, d, "pair-abort"); ab.Text != "annullato" {
		t.Fatalf("pair-abort dopo Ctrl-C: %q", ab.Text)
	}
	if list, _ := s.keys.List(); len(list) != 0 {
		t.Fatalf("chiavi: %+v", list)
	}
}

// ---------- autenticazione ----------

func TestAuthEveryConnection(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	K := d.Key()
	d.Close()
	<-d.Done()

	// Nuova connessione: di nuovo la sfida, nuova SK.
	d2 := s.dial(K, true)
	if w := d2.Log()[0]; w.Msg["paired"] != true {
		t.Fatalf("welcome.paired = %v", w.Msg)
	}
	await(t, d2, "authed")
	if bytes.Equal(d2.SK(), d.SK()) || len(d2.SK()) != 32 {
		t.Fatal("la chiave di sessione deve cambiare a ogni connessione")
	}
	waitFor(t, "autenticato", func() bool { return s.status().Pair == "ok" })
	waitFor(t, "remote", func() bool { return s.status().Remote })
	if code, m := s.post("/api/displays/"+board+"/settings", map[string]any{"led": false}); code != 202 {
		t.Fatalf("set autenticato: %d %v", code, m)
	}
	await(t, d2, "set")
	d2.Close()
	<-d2.Done()

	// Chiave sbagliata: auth-fail, abbinamento da rifare, niente set.
	bad := append([]byte(nil), K...)
	bad[0] ^= 1
	d3 := s.dial(bad, true)
	await(t, d3, "auth-fail")
	waitFor(t, "da rifare", func() bool { return s.status().Pair == "redo" })
	if !strings.Contains(s.log.String(), "auth con un mac sbagliato") {
		t.Fatal("auth-fail non registrato nel log")
	}
	code, m := s.post("/api/displays/"+board+"/settings", map[string]any{"led": false})
	if code != http.StatusConflict || !strings.Contains(fmt.Sprint(m["error"]), "non abbinato o non autenticato") {
		t.Fatalf("set non autenticato: %d %v", code, m)
	}
	if code, _ := s.post("/api/displays/"+board+"/full-refresh", map[string]any{}); code != http.StatusConflict {
		t.Fatalf("full-refresh non autenticato: %d", code)
	}
	// …e nemmeno le richieste di permesso.
	p := s.hook(permPayload)
	if out := p.wait(t, 3*time.Second); out != "" {
		t.Fatalf("hook con display non autenticato: %q", out)
	}
	d3.Close()
	<-d3.Done()

	// Display senza chiave mentre il PC ce l'ha.
	d4 := s.dial(nil, true)
	await(t, d4, "redo")
	waitFor(t, "da rifare (il display non ha la chiave)", func() bool {
		st := s.status()
		return st.Pair == "redo" && strings.Contains(st.PairReason, "il display no")
	})
	d4.Close()
	<-d4.Done()

	// calamaio unpair: chiave tolta, connessione chiusa; al ritorno il display vede paired falso.
	d5 := s.dial(K, true)
	await(t, d5, "authed")
	out := &syncBuf{}
	if err := client.Unpair(s.cfg, board, out); err != nil || !strings.Contains(out.String(), "dimenticato") {
		t.Fatalf("unpair: %v %q", err, out.String())
	}
	select {
	case <-d5.Done():
	case <-time.After(3 * time.Second):
		t.Fatal("la connessione non si chiude dopo unpair")
	}
	if list, _ := s.keys.List(); len(list) != 0 {
		t.Fatalf("chiavi dopo unpair: %+v", list)
	}
	d6 := s.dial(K, true)
	if w := d6.Log()[0]; w.Msg["paired"] != false {
		t.Fatalf("welcome.paired dopo unpair = %v", w.Msg)
	}
	await(t, d6, "redo")
	waitFor(t, "da rifare (il PC non ha la chiave)", func() bool { return s.status().Pair == "redo" })
	checkSchema(t, d6)

	// Dimentica abbinamento dal display (unpair firmato).
	s.pair(d6)
	if list, _ := s.keys.List(); len(list) != 1 {
		t.Fatalf("chiavi dopo il nuovo abbinamento: %+v", list)
	}
	d6.Unpair()
	waitFor(t, "chiave cancellata dal display", func() bool { list, _ := s.keys.List(); return len(list) == 0 })
	if st := s.status(); st.Pair != "none" {
		t.Fatalf("dopo unpair dal display: %+v", st)
	}
}

// ---------- permessi ----------

func TestPermissionAllowDeny(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()

	p := s.hook(permPayload)
	e := await(t, d, "perm-request")
	if r := e.Req; r.Name != "web-app" || r.Tool != "Bash" || r.Arg != "npm run test" || r.Session != session || len(r.ID) != 32 {
		t.Fatalf("perm-request = %+v", r)
	}
	if !p.running() {
		t.Fatal("il hook doveva aspettare")
	}
	if ps := s.run.Hub.Permissions(); len(ps) != 1 || ps[0].ID != e.Req.ID || len(ps[0].SentTo) != 1 {
		t.Fatalf("richieste aperte: %+v", ps)
	}
	d.Reply(e.Req.ID, true)
	if out := p.wait(t, 3*time.Second); out != allowOut {
		t.Fatalf("hook consenti: %q", out)
	}
	if c := await(t, d, "perm-close"); c.Text != "display" {
		t.Fatalf("perm-close %q", c.Text)
	}

	p = s.hook(permPayload)
	e = await(t, d, "perm-request")
	d.Reply(e.Req.ID, false)
	if out := p.wait(t, 3*time.Second); out != denyOut {
		t.Fatalf("hook nega: %q", out)
	}
	await(t, d, "perm-close")

	waitFor(t, "statistiche di Oggi", func() bool { return s.run.Store.Snapshot().Today.PermDisplay == 2 })
	snap := s.run.Store.Snapshot()
	if snap.Today.PermTerminal != 0 {
		t.Fatalf("oggi = %+v", snap.Today)
	}
	var logText []string
	for _, l := range snap.Log {
		logText = append(logText, l.Text)
	}
	if j := strings.Join(logText, "|"); !strings.Contains(j, "consentito dal display") || !strings.Contains(j, "negato dal display") {
		t.Fatalf("log dello stato: %v", logText)
	}
	checkSchema(t, d)
}

func TestPermissionAnsweredInTerminal(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	p := s.hook(permPayload)
	e := await(t, d, "perm-request")
	// Claude Code chiude il hook quando rispondi nel terminale.
	if err := p.cmd.Process.Kill(); err != nil {
		t.Fatal(err)
	}
	c := await(t, d, "perm-close")
	if c.Text != "terminal" || c.Msg["id"] != e.Req.ID {
		t.Fatalf("perm-close = %v", c.Msg)
	}
	waitFor(t, "permesso risolto dal terminale", func() bool { return s.run.Store.Snapshot().Today.PermTerminal == 1 })
	if len(s.run.Hub.Permissions()) != 0 {
		t.Fatal("la richiesta è ancora aperta")
	}
	// Una risposta tardiva del display non vale più.
	d.Reply(e.Req.ID, true)
	waitFor(t, "risposta tardiva scartata", func() bool { return strings.Contains(s.log.String(), "id inesistente o non più in attesa") })
}

func TestPermissionNoDisplay(t *testing.T) {
	s := startSvc(t, 0)
	// Nessun display: subito, senza output.
	p := s.hook(permPayload)
	if out := p.wait(t, 3*time.Second); out != "" || p.took > 2*time.Second {
		t.Fatalf("senza display: %q in %s", out, p.took)
	}
	// Display abbinato ma con «Permessi dal display» spento: idem, e niente al display.
	d := s.pairedDisplay()
	d.SetPermOn(false)
	waitFor(t, "permessi spenti", func() bool { return !s.status().Perms })
	p = s.hook(permPayload)
	if out := p.wait(t, 3*time.Second); out != "" {
		t.Fatalf("permOn spento: %q", out)
	}
	if _, err := d.Await("perm-request", 300*time.Millisecond); err == nil {
		t.Fatal("perm-request a un display con i permessi spenti")
	}
	// Display spento a metà attesa: il hook resta in attesa; al ritorno il display riceve la richiesta.
	d.SetPermOn(true)
	waitFor(t, "permessi accesi", func() bool { return s.status().Perms })
	p = s.hook(permPayload)
	e := await(t, d, "perm-request")
	K := d.Key()
	d.Close()
	<-d.Done()
	time.Sleep(200 * time.Millisecond)
	if !p.running() {
		t.Fatal("il hook è uscito quando il display si è spento")
	}
	d2 := s.dial(K, true)
	await(t, d2, "authed")
	e2 := await(t, d2, "perm-request")
	if e2.Req.ID != e.Req.ID {
		t.Fatalf("richiesta diversa al ritorno: %s / %s", e2.Req.ID, e.Req.ID)
	}
	d2.Reply(e2.Req.ID, true)
	if out := p.wait(t, 3*time.Second); out != allowOut {
		t.Fatalf("hook: %q", out)
	}
}

func TestPermissionNoTimeout(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	p := s.hook(permPayload)
	e := await(t, d, "perm-request")
	// Tre giorni dopo (orologio finto del servizio) e qualche secondo vero: ancora aperta.
	s.clock.Advance(72 * time.Hour)
	time.Sleep(2500 * time.Millisecond) // più del vecchio timeout del client senza --wait
	if !p.running() || len(s.run.Hub.Permissions()) != 1 {
		t.Fatal("la richiesta è scaduta: non deve scadere mai")
	}
	d.Reply(e.Req.ID, true)
	if out := p.wait(t, 3*time.Second); out != allowOut {
		t.Fatalf("hook: %q", out)
	}
	if !strings.Contains(s.log.String(), "allow da "+board+" dopo 72h") {
		t.Fatalf("attesa nel log:\n%s", s.log)
	}
}

func TestPermissionSessionEndAndShutdown(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	p := s.hook(permPayload)
	await(t, d, "perm-request")
	end := `{"session_id":"` + session + `","cwd":"/home/utente/web-app","hook_event_name":"SessionEnd","reason":"other"}`
	resp, err := http.Post(s.base+"/hook", "application/json", strings.NewReader(end))
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if c := await(t, d, "perm-close"); c.Text != "session" {
		t.Fatalf("perm-close %q", c.Text)
	}
	if out := p.wait(t, 3*time.Second); out != "" {
		t.Fatalf("hook dopo la fine della sessione: %q", out)
	}

	p = s.hook(permPayload)
	await(t, d, "perm-request")
	s.cancel() // servizio in arresto
	if c := await(t, d, "perm-close"); c.Text != "service" {
		t.Fatalf("perm-close %q", c.Text)
	}
	if out := p.wait(t, 3*time.Second); out != "" {
		t.Fatalf("hook con il servizio in arresto: %q", out)
	}
	if strings.Contains(s.log.String(), "chiuso: terminal") {
		t.Fatalf("l'arresto non è una risposta nel terminale:\n%s", s.log)
	}
}

// ---------- i 7 attacchi di M5-PERMESSI.md §7 (test di accettazione 7) ----------

func TestAttacks(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	p := s.hook(permPayload)
	e := await(t, d, "perm-request")
	id := e.Req.ID
	logHas := func(what string) {
		t.Helper()
		waitFor(t, "nel log: "+what, func() bool { return strings.Contains(s.log.String(), what) })
		if !p.running() {
			t.Fatalf("dopo «%s» il hook è uscito: %q", what, p.out.String())
		}
	}

	// 1. Risposta da una connessione non autenticata (un altro client in rete).
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	evil, err := fakedisplay.Dial(ctx, s.wsURL, "calamaio-evil", nil, true)
	if err != nil {
		t.Fatal(err)
	}
	defer evil.Close()
	await(t, evil, "welcome")
	nonce := m5.Hex(m5.Random(16))
	evil.Send(`{"t":"perm-reply","v":1,"id":"` + id + `","decision":"allow","nonce":"` + nonce + `","mac":"` + m5.Hex(m5.RepMac(m5.Random(32), id, "allow", nonce)) + `"}`)
	logHas("perm-reply scartata (connessione non autenticata)")

	// 2. Mac sbagliato (dalla connessione autenticata, chiave falsa).
	nonce = m5.Hex(m5.Random(16))
	d.Send(`{"t":"perm-reply","v":1,"id":"` + id + `","decision":"allow","nonce":"` + nonce + `","mac":"` + m5.Hex(m5.RepMac(m5.Random(32), id, "allow", nonce)) + `"}`)
	logHas("perm-reply scartata (mac sbagliato)")

	// 3. Id inesistente (mac giusto).
	d.Reply(m5.Hex(m5.Random(16)), true)
	logHas("perm-reply scartata (id inesistente o non più in attesa)")

	// 5. perm-close falso verso il display: senza mac, con mac sbagliato, con il mac di un'altra richiesta.
	for _, fake := range []string{
		`{"t":"perm-close","v":1,"id":"` + id + `","reason":"terminal"}`,
		`{"t":"perm-close","v":1,"id":"` + id + `","reason":"terminal","mac":"228a46a330ce703d9fd212458d50c5ee"}`,
		`{"t":"perm-close","v":1,"id":"` + id + `","reason":"terminal","mac":"` + m5.Hex(m5.CloseMac(d.SK(), m5.Hex(m5.Random(16)), "terminal")) + `"}`,
	} {
		d.Deliver(fake)
		if r := await(t, d, "rejected"); !strings.HasPrefix(r.Text, "perm-close") {
			t.Fatalf("perm-close falso: %q", r.Text)
		}
	}
	for _, ev := range d.Log() {
		if ev.Type == "perm-close" {
			t.Fatal("il display ha accettato un perm-close falso")
		}
	}

	// 6. Name alterato (e troppo lungo): il nome è nel mac.
	for _, name := range []string{"altra-sessione", strings.Repeat("n", 32)} {
		forged := map[string]any{}
		for k, v := range e.Msg {
			forged[k] = v
		}
		forged["name"] = name
		forged["id"] = m5.Hex(m5.Random(16)) // id nuovo: non è una ripetizione
		b, _ := json.Marshal(forged)
		d.Deliver(string(b))
		if r := await(t, d, "rejected"); !strings.HasPrefix(r.Text, "perm-request") {
			t.Fatalf("perm-request alterata: %q", r.Text)
		}
	}

	// Dopo tutti gli attacchi la richiesta vera è ancora valida.
	frame := d.Reply(id, true)
	if out := p.wait(t, 3*time.Second); out != allowOut {
		t.Fatalf("hook: %q", out)
	}
	// 4. Nonce ripetuto: la stessa risposta registrata e rimandata.
	p2 := s.hook(permPayload)
	e2 := await(t, d, "perm-request")
	d.Send(frame)
	waitFor(t, "replica scartata", func() bool { return strings.Contains(s.log.String(), "perm-reply scartata (nonce già visto") })
	// I nonce si tengono per id (M5 §3): lo stesso nonce su un'altra richiesta, con il mac giusto per
	// quella, è una risposta nuova e vale. Solo chi ha SK può costruirla.
	var rep map[string]any
	_ = json.Unmarshal([]byte(frame), &rep)
	n := rep["nonce"].(string)
	d.Send(`{"t":"perm-reply","v":1,"id":"` + e2.Req.ID + `","decision":"allow","nonce":"` + n + `","mac":"` + m5.Hex(m5.RepMac(d.SK(), e2.Req.ID, "allow", n)) + `"}`)
	if out := p2.wait(t, 3*time.Second); out != allowOut {
		t.Fatalf("hook 2: %q", out)
	}

	// 7. AskUserQuestion ed ExitPlanMode non arrivano mai al display.
	d.Drain()
	for _, tool := range []string{"AskUserQuestion", "ExitPlanMode"} {
		q := strings.Replace(permPayload, `"tool_name":"Bash","tool_input":{"command":"npm run test","description":"test"}`,
			`"tool_name":"`+tool+`","tool_input":{"questions":[{"question":"Quale database?"}]}`, 1)
		if q == permPayload {
			t.Fatal("sostituzione fallita")
		}
		pq := s.hook(q)
		if out := pq.wait(t, 3*time.Second); out != "" || pq.took > 2*time.Second {
			t.Fatalf("%s: %q in %s", tool, out, pq.took)
		}
	}
	if _, err := d.Await("perm-request", 300*time.Millisecond); err == nil {
		t.Fatal("una domanda è arrivata al display come permesso")
	}
	if st := s.run.Store.Snapshot(); st.Sessions[0].State != "input" {
		t.Fatalf("la domanda deve lasciare la sessione in input: %+v", st.Sessions[0])
	}

	// Nessuna approvazione automatica: tutte le uscite «allow» sono quelle delle risposte firmate.
	if c := strings.Count(s.log.String(), "ha risposto allow"); c != 2 {
		t.Fatalf("risposte accettate: %d, attese 2\n%s", c, s.log)
	}
	checkSchema(t, d)
}

// Il hook --wait non si fa usare da una pagina web (Origin) né da un Host non locale.
func TestHookWaitRejectsBrowser(t *testing.T) {
	s := startSvc(t, 0)
	s.pairedDisplay()
	for name, hdr := range map[string][2]string{
		"Origin":         {"Origin", "http://evil.example"},
		"Sec-Fetch-Site": {"Sec-Fetch-Site", "cross-site"},
		"Host":           {"Host", "evil.example"},
	} {
		req, _ := http.NewRequest(http.MethodPost, s.base+"/hook?wait=1", strings.NewReader(permPayload))
		req.Header.Set("Content-Type", "application/json")
		if hdr[0] == "Host" {
			req.Host = hdr[1]
		} else {
			req.Header.Set(hdr[0], hdr[1])
		}
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		if resp.StatusCode != http.StatusForbidden {
			t.Errorf("%s: %d", name, resp.StatusCode)
		}
	}
	// text/plain (richiesta «semplice» di un form): rifiutata anche quella.
	resp, err := http.Post(s.base+"/hook?wait=1", "text/plain", strings.NewReader(permPayload))
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusForbidden {
		t.Errorf("text/plain: %d", resp.StatusCode)
	}
	if len(s.run.Hub.Permissions()) != 0 {
		t.Fatal("richiesta aperta da un browser")
	}
}

// ---------- risposta nel terminale senza chiusura del hook (Claude Code 2.1.296) ----------
//
// Provato dal vivo: rispondendo nel terminale Claude Code esegue lo strumento ma NON chiude il processo
// `calamaio hook --wait`. La richiesta la chiude la macchina a stati quando vede il permesso risolto.

func (s *svc) postEvent(path, body string) {
	s.t.Helper()
	resp, err := http.Post(s.base+path, "application/json", strings.NewReader(body))
	if err != nil {
		s.t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != 200 {
		s.t.Fatalf("%s: %d", path, resp.StatusCode)
	}
}

// ev costruisce un payload di hook della sessione di prova.
func ev(event string, extra string) string {
	b := `{"session_id":"` + session + `","transcript_path":"/nessuno/transcript.jsonl","cwd":"/home/utente/web-app","hook_event_name":"` + event + `"`
	if extra != "" {
		b += "," + extra
	}
	return b + "}"
}

// closedByEngine: il hook esce da solo, vuoto, entro limit dal segnale; il display riceve perm-close.
func closedByEngine(t *testing.T, s *svc, d *fakedisplay.Display, p *hookProc, id string, signal func(), limit time.Duration, reason string) {
	t.Helper()
	if !p.running() {
		t.Fatal("il hook doveva aspettare")
	}
	start := time.Now()
	signal()
	if out := p.wait(t, limit); out != "" {
		t.Fatalf("il hook deve uscire senza decisione, ha stampato %q", out)
	}
	if took := time.Since(start); took > limit {
		t.Fatalf("il hook è uscito dopo %s (limite %s)", took, limit)
	}
	c := await(t, d, "perm-close")
	if c.Text != reason || c.Msg["id"] != id {
		t.Fatalf("perm-close = %v, atteso %s per %s", c.Msg, reason, id)
	}
	if len(s.run.Hub.Permissions()) != 0 {
		t.Fatalf("richieste ancora aperte: %+v", s.run.Hub.Permissions())
	}
}

func TestTerminalAnswerPostToolUse(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	p := s.hook(permPayload)
	e := await(t, d, "perm-request")
	closedByEngine(t, s, d, p, e.Req.ID, func() {
		s.postEvent("/hook", ev("PostToolUse", `"tool_name":"Bash","tool_input":{"command":"npm run test"},"tool_use_id":"toolu_1","tool_response":{"stdout":"ok"}`))
	}, time.Second, "terminal")
	waitFor(t, "risolto dal terminale", func() bool { return s.run.Store.Snapshot().Today.PermTerminal == 1 })
	if st := s.run.Store.Snapshot(); st.Today.PermDisplay != 0 {
		t.Fatalf("oggi = %+v", st.Today)
	}
	// Il display risponde dopo la chiusura: rifiutata e registrata.
	d.Reply(e.Req.ID, true)
	waitFor(t, "risposta tardiva scartata", func() bool {
		return strings.Contains(s.log.String(), "perm-reply scartata (id inesistente o non più in attesa)")
	})
	if strings.Contains(s.log.String(), "ha risposto allow") {
		t.Fatal("risposta tardiva accettata")
	}
	// Stesso esito con PostToolUseFailure.
	p = s.hook(permPayload)
	e = await(t, d, "perm-request")
	closedByEngine(t, s, d, p, e.Req.ID, func() {
		s.postEvent("/hook", ev("PostToolUseFailure", `"tool_name":"Bash","tool_input":{"command":"npm run test"},"tool_use_id":"toolu_2","error":"exit 1"`))
	}, time.Second, "terminal")
}

func TestTerminalAnswerOtherEvents(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	for _, c := range []struct {
		name, path, body, reason string
	}{
		{"Stop", "/hook", ev("Stop", `"last_assistant_message":"Fatto."`), "terminal"},
		{"StopFailure", "/hook", ev("StopFailure", `"error":"rate_limit"`), "terminal"},
		{"UserPromptSubmit", "/hook", ev("UserPromptSubmit", `"prompt":"lascia stare"`), "terminal"},
		{"PreToolUse di un altro strumento", "/hook", ev("PreToolUse", `"tool_name":"Read","tool_input":{"file_path":"/home/utente/web-app/a.ts"},"tool_use_id":"toolu_9"`), "terminal"},
		{"SessionEnd", "/hook", ev("SessionEnd", `"reason":"other"`), "session"},
	} {
		p := s.hook(permPayload)
		e := await(t, d, "perm-request")
		closedByEngine(t, s, d, p, e.Req.ID, func() { s.postEvent(c.path, c.body) }, time.Second, c.reason)
		t.Logf("%s: hook uscito in %s", c.name, p.took)
	}
}

// Diniego nel terminale: nessun hook lo segnala; lo dicono la status line e il transcript (fixture vera).
func TestTerminalDenialFromTranscript(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	src, err := os.ReadFile(filepath.Join("..", "..", "..", "protocol", "samples", "fixtures", "transcript-negato.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	tr := filepath.Join(t.TempDir(), "transcript.jsonl")
	if err := os.WriteFile(tr, src, 0o600); err != nil {
		t.Fatal(err)
	}
	payload := strings.Replace(permPayload, "/nessuno/transcript.jsonl", tr, 1)
	p := s.hook(payload)
	e := await(t, d, "perm-request")
	// Una status line con il transcript che non finisce con il diniego non chiude niente (concessione:
	// arriva PostToolUse).
	ok := filepath.Join(t.TempDir(), "ok.jsonl")
	os.WriteFile(ok, []byte(`{"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"…"}]}}`+"\n"), 0o600)
	s.postEvent("/statusline", `{"session_id":"`+session+`","transcript_path":"`+ok+`","workspace":{"current_dir":"/home/utente/web-app"}}`)
	time.Sleep(2500 * time.Millisecond) // grazia di 1 s + un giro di Tick
	if !p.running() {
		t.Fatal("una status line senza diniego ha chiuso la richiesta")
	}
	closedByEngine(t, s, d, p, e.Req.ID, func() {
		s.postEvent("/statusline", `{"session_id":"`+session+`","transcript_path":"`+tr+`","workspace":{"current_dir":"/home/utente/web-app"}}`)
	}, 3*time.Second, "terminal")
	waitFor(t, "sessione in pausa con «Negato»", func() bool {
		st := s.run.Store.Snapshot()
		return len(st.Sessions) == 1 && st.Sessions[0].State == "idle" && strings.HasPrefix(st.Sessions[0].Detail, "Negato:")
	})
}

// Due richieste aperte nella stessa sessione (agente principale e subagente): si chiude quella giusta.
func TestTerminalAnswerRightRequest(t *testing.T) {
	s := startSvc(t, 0)
	d := s.pairedDisplay()
	s.postEvent("/hook", ev("PreToolUse", `"tool_name":"Bash","tool_input":{"command":"npm run test"},"tool_use_id":"toolu_main"`))
	pMain := s.hook(permPayload)
	eMain := await(t, d, "perm-request")
	sub := `"agent_id":"a1","agent_type":"Explore"`
	s.postEvent("/hook", ev("PreToolUse", sub+`,"tool_name":"Bash","tool_input":{"command":"ls"},"tool_use_id":"toolu_sub"`))
	pSub := s.hook(strings.Replace(permPayload, `"hook_event_name":"PermissionRequest"`, sub+`,"hook_event_name":"PermissionRequest"`, 1))
	eSub := await(t, d, "perm-request")
	if !pMain.running() {
		t.Fatal("il PreToolUse del subagente ha chiuso la richiesta dell'agente principale")
	}
	closedByEngine2 := func(p *hookProc, id, body string) {
		t.Helper()
		s.postEvent("/hook", body)
		if out := p.wait(t, time.Second); out != "" {
			t.Fatalf("hook: %q", out)
		}
		c := await(t, d, "perm-close")
		if c.Msg["id"] != id || c.Text != "terminal" {
			t.Fatalf("perm-close = %v, atteso %s", c.Msg, id)
		}
	}
	closedByEngine2(pSub, eSub.Req.ID, ev("PostToolUse", sub+`,"tool_name":"Bash","tool_input":{"command":"ls"},"tool_use_id":"toolu_sub"`))
	if !pMain.running() || len(s.run.Hub.Permissions()) != 1 || s.run.Hub.Permissions()[0].ID != eMain.Req.ID {
		t.Fatalf("chiusa la richiesta sbagliata: %+v", s.run.Hub.Permissions())
	}
	closedByEngine2(pMain, eMain.Req.ID, ev("PostToolUse", `"tool_name":"Bash","tool_input":{"command":"npm run test"},"tool_use_id":"toolu_main"`))
}
