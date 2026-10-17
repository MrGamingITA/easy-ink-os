package display

import (
	"io"
	"net/http"
	"testing"
	"time"

	"github.com/coder/websocket"
)

// Bye: arriva a chi ha fatto hello (non a chi non l'ha fatto), dopo non parte più nessuno state,
// le connessioni nuove ricevono 503; Close poi chiude con 1001.
func TestByeToGreetedDisplays(t *testing.T) {
	r := newRig(t, func(h *Hub) { h.MinInterval = 10 * time.Millisecond })
	a := r.dial(nil)
	a.greet("calamaio-0001")
	b := r.dial(nil)
	b.greet("calamaio-0002")
	silent := r.dial(nil) // collegato senza hello
	waitFor(t, 2*time.Second, func() bool { return len(r.hub.Displays()) == 3 })

	msg := map[string]any{"t": "bye", "v": 1, "summary": map[string]any{"off": 1}}
	sent, total := r.hub.Bye(msg, 2*time.Second)
	if sent != 2 || total != 2 {
		t.Fatalf("bye mandato a %d su %d", sent, total)
	}
	for _, c := range []*client{a, b} {
		if m := c.next(time.Second); m["t"] != "bye" {
			t.Fatalf("atteso bye, arrivato %v", m)
		}
	}
	silent.none(100 * time.Millisecond)

	// dopo bye nessuno state, anche se lo stato cambia
	setDetail(r.store, "dopo il bye")
	a.none(200 * time.Millisecond)

	// connessioni nuove: 503
	resp, err := http.Get(r.srv.URL + "/ws")
	if err != nil {
		t.Fatal(err)
	}
	io.Copy(io.Discard, resp.Body)
	resp.Body.Close()
	if resp.StatusCode != http.StatusServiceUnavailable {
		t.Fatalf("dopo bye: %d", resp.StatusCode)
	}

	r.hub.Close()
	select {
	case <-a.done:
	case <-time.After(2 * time.Second):
		t.Fatal("Close non ha chiuso il display")
	}
	if s := websocket.CloseStatus(a.err); s != websocket.StatusGoingAway {
		t.Fatalf("chiusura dopo bye = %v", a.err)
	}
}

// Una connessione bloccata non blocca lo spegnimento: Bye torna entro il tempo dato.
func TestByeDoesNotHang(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.greet("calamaio-0003")
	waitFor(t, 2*time.Second, func() bool { return len(r.hub.Displays()) == 1 })
	// il goroutine della connessione resta occupato 1,5 s prima di arrivare al bye
	r.hub.mu.Lock()
	for _, cn := range r.hub.conns {
		cn.out <- lazyMsg(func() any { time.Sleep(1500 * time.Millisecond); return nil })
	}
	r.hub.mu.Unlock()
	start := time.Now()
	sent, total := r.hub.Bye(map[string]any{"t": "bye", "v": 1, "summary": map[string]any{"off": 1}}, 300*time.Millisecond)
	if took := time.Since(start); took > time.Second {
		t.Fatalf("Bye ha impiegato %s", took)
	}
	if sent != 0 || total != 1 {
		t.Fatalf("bye %d su %d", sent, total)
	}
}
