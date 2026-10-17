package display

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/coder/websocket"

	"calamaio/internal/state"
)

// logBuf raccoglie il log dell'hub per controllarlo nei test.
type logBuf struct {
	mu sync.Mutex
	b  strings.Builder
}

func (l *logBuf) Write(p []byte) (int, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.b.Write(p)
}

func (l *logBuf) String() string {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.b.String()
}

type rig struct {
	t     *testing.T
	store *state.Store
	hub   *Hub
	srv   *httptest.Server
	url   string
	log   *logBuf
}

// newRig avvia un hub vero su una porta libera.
func newRig(t *testing.T, tune func(*Hub)) *rig {
	t.Helper()
	store := state.NewStore(state.Empty())
	lb := &logBuf{}
	hub := NewHub(store, log.New(lb, "", 0))
	hub.TZ = "CET-1CEST,M3.5.0,M10.5.0/3"
	hub.Host = "pc-di-prova"
	hub.Version = "test"
	if tune != nil {
		tune(hub)
	}
	srv := httptest.NewServer(hub.Handler())
	r := &rig{t: t, store: store, hub: hub, srv: srv, url: "ws" + strings.TrimPrefix(srv.URL, "http") + "/ws", log: lb}
	t.Cleanup(func() {
		hub.Close()
		srv.Close()
	})
	return r
}

type client struct {
	t     *testing.T
	ws    *websocket.Conn
	msgs  chan map[string]any
	times chan time.Time // istante di arrivo di ogni messaggio, nello stesso ordine
	pings atomic.Int32
	done  chan struct{}
	err   error
	sk    []byte // chiave di sessione dopo authedDisplay
}

func (r *rig) dial(hdr http.Header) *client {
	r.t.Helper()
	c := &client{t: r.t, msgs: make(chan map[string]any, 256), times: make(chan time.Time, 256), done: make(chan struct{})}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	ws, _, err := websocket.Dial(ctx, r.url, &websocket.DialOptions{
		HTTPHeader: hdr,
		OnPingReceived: func(context.Context, []byte) bool {
			c.pings.Add(1)
			return true
		},
	})
	if err != nil {
		r.t.Fatalf("dial: %v", err)
	}
	c.ws = ws
	go func() {
		defer close(c.done)
		for {
			_, b, err := ws.Read(context.Background())
			if err != nil {
				c.err = err
				return
			}
			var m map[string]any
			if err := json.Unmarshal(b, &m); err != nil {
				c.t.Errorf("messaggio del servizio non JSON: %s", b)
				continue
			}
			c.times <- time.Now()
			c.msgs <- m
		}
	}()
	r.t.Cleanup(func() { _ = ws.CloseNow() })
	return c
}

func (c *client) send(s string) {
	c.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := c.ws.Write(ctx, websocket.MessageText, []byte(s)); err != nil {
		c.t.Fatalf("write: %v", err)
	}
}

func (c *client) next(d time.Duration) map[string]any {
	c.t.Helper()
	select {
	case m := <-c.msgs:
		<-c.times
		return m
	case <-time.After(d):
		c.t.Fatalf("nessun messaggio entro %s", d)
		return nil
	}
}

func (c *client) none(d time.Duration) {
	c.t.Helper()
	select {
	case m := <-c.msgs:
		c.t.Fatalf("messaggio inatteso: %v", m)
	case <-time.After(d):
	}
}

func hello(board string) string {
	return fmt.Sprintf(`{"t":"hello","v":1,"board":%q,"device":"heltec213","class":"S","fw":"0.3.0","w":250,"h":122}`, board)
}

// greet manda hello e controlla welcome + state.
func (c *client) greet(board string) map[string]any {
	c.t.Helper()
	c.send(hello(board))
	w := c.next(2 * time.Second)
	if w["t"] != "welcome" {
		c.t.Fatalf("atteso welcome, arrivato %v", w)
	}
	s := c.next(2 * time.Second)
	if s["t"] != "state" {
		c.t.Fatalf("atteso state, arrivato %v", s)
	}
	return s
}

func setDetail(st *state.Store, detail string) {
	st.Update(func(s *state.Snapshot) {
		if len(s.Sessions) == 0 {
			s.Sessions = append(s.Sessions, state.Session{ID: "s1", Name: "prova", State: state.Working, Since: 1})
		}
		s.Sessions[0].Detail = detail
	})
}

func detailOf(m map[string]any) string {
	ss, _ := m["sessions"].([]any)
	if len(ss) == 0 {
		return ""
	}
	return ss[0].(map[string]any)["detail"].(string)
}

func TestHelloWelcomeState(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.none(150 * time.Millisecond) // prima di hello non arriva niente

	before := time.Now().UnixMilli()
	c.send(hello("calamaio-3f2a"))
	w := c.next(2 * time.Second)
	if w["t"] != "welcome" || w["v"] != 1.0 || w["tz"] != "CET-1CEST,M3.5.0,M10.5.0/3" || w["host"] != "pc-di-prova" || w["service"] != "test" {
		t.Fatalf("welcome = %v", w)
	}
	if tm := int64(w["time"].(float64)); tm < before || tm > time.Now().UnixMilli() {
		t.Fatalf("welcome.time %d fuori da [%d, adesso]", tm, before)
	}
	s := c.next(2 * time.Second)
	if s["t"] != "state" || s["v"] != 1.0 {
		t.Fatalf("state = %v", s)
	}
	for _, k := range []string{"time", "sessions", "limits", "today", "log", "services", "prs", "agenda"} {
		if _, ok := s[k]; !ok {
			t.Errorf("state senza %q", k)
		}
	}
	ds := r.hub.Displays()
	if len(ds) != 1 || ds[0].Board != "calamaio-3f2a" || ds[0].Device != "heltec213" || ds[0].IP != "127.0.0.1" || !ds[0].Hello || ds[0].States != 1 {
		t.Fatalf("Displays = %+v", ds)
	}
	if !strings.Contains(r.log.String(), "display collegato #1 calamaio-3f2a") {
		t.Fatalf("log senza la connessione:\n%s", r.log)
	}
}

func TestChangeProducesState(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.greet("calamaio-0001")
	time.Sleep(300 * time.Millisecond) // fuori dalla finestra dei 250 ms
	start := time.Now()
	setDetail(r.store, "Edit a.go")
	m := c.next(time.Second)
	if detailOf(m) != "Edit a.go" {
		t.Fatalf("state senza il cambiamento: %v", m)
	}
	if el := time.Since(start); el > 100*time.Millisecond {
		t.Fatalf("state arrivato dopo %s: fuori dalla finestra deve partire subito", el)
	}
}

func TestCoalescing(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.greet("calamaio-0002")

	// 60 cambiamenti in ~600 ms: con al massimo uno ogni 250 ms arrivano pochi state,
	// e l'ultimo deve contenere l'ultimo cambiamento.
	start := time.Now()
	for i := 0; i < 60; i++ {
		setDetail(r.store, fmt.Sprintf("passo %d", i))
		time.Sleep(10 * time.Millisecond)
	}
	var got []map[string]any
	var times []time.Time
	deadline := time.After(time.Second)
loop:
	for {
		select {
		case m := <-c.msgs:
			got = append(got, m)
		case <-deadline:
			break loop
		}
	}
	elapsed := time.Since(start)
	for range got {
		times = append(times, <-c.times)
	}
	maxStates := int(elapsed/(250*time.Millisecond)) + 1
	if len(got) == 0 || len(got) > maxStates {
		t.Fatalf("%d state in %s (massimo %d)", len(got), elapsed, maxStates)
	}
	if d := detailOf(got[len(got)-1]); d != "passo 59" {
		t.Fatalf("ultimo state = %q, atteso il cambiamento più recente", d)
	}
	for i := 1; i < len(times); i++ {
		// Tolleranza per i tempi di consegna: il servizio non manda mai a meno di 250 ms.
		if gap := times[i].Sub(times[i-1]); gap < 200*time.Millisecond {
			t.Fatalf("due state a %s di distanza", gap)
		}
	}
	t.Logf("60 cambiamenti → %d state in %s", len(got), elapsed)
}

func TestPing(t *testing.T) {
	if NewHub(nil, nil).PingInterval != 15*time.Second {
		t.Fatal("il protocollo vuole un ping ogni 15 s")
	}
	r := newRig(t, func(h *Hub) { h.PingInterval = 40 * time.Millisecond })
	c := r.dial(nil)
	c.greet("calamaio-0003")
	time.Sleep(300 * time.Millisecond)
	if n := c.pings.Load(); n < 3 {
		t.Fatalf("ping ricevuti = %d in 300 ms (atteso ≥ 3)", n)
	}
	if len(r.hub.Displays()) != 1 {
		t.Fatal("con i pong la connessione deve restare aperta")
	}
}

// Un display che non risponde ai ping viene chiuso.
func TestPingTimeoutCloses(t *testing.T) {
	r := newRig(t, func(h *Hub) {
		h.PingInterval = 30 * time.Millisecond
		h.PingTimeout = 50 * time.Millisecond
	})
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	ws, _, err := websocket.Dial(ctx, r.url, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer ws.CloseNow()
	_ = ws.Write(ctx, websocket.MessageText, []byte(hello("calamaio-muto")))
	// Il client non legge: i ping non hanno risposta.
	waitFor(t, 2*time.Second, func() bool { return len(r.hub.Displays()) == 0 })
	if !strings.Contains(r.log.String(), "ping senza risposta") {
		t.Fatalf("log:\n%s", r.log)
	}
}

func TestMultipleDisplays(t *testing.T) {
	r := newRig(t, nil)
	var cs []*client
	for i := 0; i < 3; i++ {
		c := r.dial(nil)
		c.greet(fmt.Sprintf("calamaio-%04x", i))
		cs = append(cs, c)
	}
	if n := len(r.hub.Displays()); n != 3 {
		t.Fatalf("display = %d", n)
	}
	time.Sleep(300 * time.Millisecond)
	setDetail(r.store, "per tutti")
	for i, c := range cs {
		if d := detailOf(c.next(time.Second)); d != "per tutti" {
			t.Fatalf("display %d: %q", i, d)
		}
	}
	// Uno se ne va: gli altri restano.
	_ = cs[0].ws.Close(websocket.StatusNormalClosure, "ciao")
	waitFor(t, 2*time.Second, func() bool { return len(r.hub.Displays()) == 2 })
	// la riga del log arriva subito dopo aver tolto la connessione: si aspetta anche quella
	waitFor(t, 2*time.Second, func() bool { return strings.Contains(r.log.String(), "display scollegato #1 calamaio-0000") })
	if !strings.Contains(r.log.String(), "display scollegato #1 calamaio-0000") {
		t.Fatalf("log senza la disconnessione:\n%s", r.log)
	}
}

func TestMalformedIgnored(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	bad := []string{
		`non è json`,
		`{"t":"boh","v":1}`,
		`{"t":"hello"}`,
		`{"t":"hello","v":2,"board":"x"}`,
		`{"t":"hello","v":1}`,
		`{"v":1}`,
		`[1,2,3]`,
		`{"t":"perm-reply","v":1,"id":"x","decision":"allow","nonce":"n","hmac":"h"}`,
		`{"t":"hello","v":1,"board":"x","w":"largo"}`,
	}
	for _, b := range bad {
		c.send(b)
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if err := c.ws.Write(ctx, websocket.MessageBinary, []byte{0, 1, 2}); err != nil {
		t.Fatal(err)
	}
	c.none(200 * time.Millisecond)
	waitFor(t, time.Second, func() bool { return r.hub.Ignored() == int64(len(bad)+1) })
	// La connessione è ancora viva e un hello valido funziona.
	c.greet("calamaio-0004")
	if !strings.Contains(r.log.String(), "JSON malformato") || !strings.Contains(r.log.String(), `tipo sconosciuto "boh"`) {
		t.Fatalf("log:\n%s", r.log)
	}
	// Dopo hello, i messaggi strani continuano a essere ignorati senza chiudere.
	c.send(`{{{`)
	time.Sleep(300 * time.Millisecond)
	setDetail(r.store, "ancora viva")
	if d := detailOf(c.next(time.Second)); d != "ancora viva" {
		t.Fatalf("detail = %q", d)
	}
}

func TestHelloTimeout(t *testing.T) {
	r := newRig(t, func(h *Hub) { h.HelloTimeout = 100 * time.Millisecond })
	c := r.dial(nil)
	select {
	case <-c.done:
	case <-time.After(2 * time.Second):
		t.Fatal("connessione senza hello non chiusa")
	}
	if s := websocket.CloseStatus(c.err); s != websocket.StatusPolicyViolation {
		t.Fatalf("chiusura = %v", c.err)
	}
}

func TestOrigin(t *testing.T) {
	r := newRig(t, nil)
	// arduinoWebSockets manda "Origin: file://".
	c := r.dial(http.Header{"Origin": {"file://"}})
	c.greet("calamaio-0005")

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	_, resp, err := websocket.Dial(ctx, r.url, &websocket.DialOptions{HTTPHeader: http.Header{"Origin": {"http://sito-malevolo.example"}}})
	if err == nil || resp == nil || resp.StatusCode != http.StatusForbidden {
		t.Fatalf("origine estranea accettata: err=%v resp=%v", err, resp)
	}
}

func TestSameBoardReplacesOld(t *testing.T) {
	r := newRig(t, nil)
	old := r.dial(nil)
	old.greet("calamaio-3f2a")
	nw := r.dial(nil)
	nw.greet("calamaio-3f2a")
	select {
	case <-old.done:
	case <-time.After(2 * time.Second):
		t.Fatal("la connessione vecchia non è stata chiusa")
	}
	waitFor(t, time.Second, func() bool { return len(r.hub.Displays()) == 1 })
}

func TestCloseSendsGoingAway(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.greet("calamaio-0006")
	r.hub.Close()
	select {
	case <-c.done:
	case <-time.After(2 * time.Second):
		t.Fatal("Close non ha chiuso il display")
	}
	if s := websocket.CloseStatus(c.err); s != websocket.StatusGoingAway {
		t.Fatalf("chiusura = %v", c.err)
	}
	// Dopo Close non si accettano nuove connessioni.
	resp, err := http.Get(r.srv.URL + "/ws")
	if err != nil {
		t.Fatal(err)
	}
	io.Copy(io.Discard, resp.Body)
	resp.Body.Close()
	if resp.StatusCode != http.StatusServiceUnavailable {
		t.Fatalf("dopo Close: %d", resp.StatusCode)
	}
}

func waitFor(t *testing.T, d time.Duration, ok func() bool) {
	t.Helper()
	end := time.Now().Add(d)
	for time.Now().Before(end) {
		if ok() {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("condizione non raggiunta in tempo")
}
