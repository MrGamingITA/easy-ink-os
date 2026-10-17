package display

import (
	"testing"
	"time"

	"calamaio/internal/m5"
)

// testKey: chiave di abbinamento fissa dei test (K del flusso dei vettori).
var testKey = mustHex("3d9c852a2bcba1e6337f46470998e3bd093d70c3a6fcaa006c4f7875a15e6ba0")

func mustHex(s string) []byte {
	b, ok := m5.ParseHex(s, len(s)/2)
	if !ok {
		panic(s)
	}
	return b
}

// pairKey dà al servizio la chiave di board (come dopo un abbinamento).
func (r *rig) pairKey(board string, K []byte) {
	r.hub.mu.Lock()
	r.hub.keys[board] = append([]byte(nil), K...)
	r.hub.mu.Unlock()
}

// nextOf restituisce il primo messaggio di tipo typ, saltando gli state.
func (c *client) nextOf(typ string, d time.Duration) map[string]any {
	c.t.Helper()
	deadline := time.Now().Add(d)
	for {
		m := c.next(time.Until(deadline))
		if m["t"] == typ {
			return m
		}
		if m["t"] != "state" && m["t"] != "game" {
			c.t.Fatalf("atteso %s, arrivato %v", typ, m)
		}
	}
}

// authedDisplay: display abbinato con testKey, hello e sfida-risposta fino ad auth-ok. Restituisce
// anche SK nella mappa skOf.
func authedDisplay(t *testing.T, r *rig, board string) *client {
	t.Helper()
	r.pairKey(board, testKey)
	c := r.dial(nil)
	c.greet(board)
	ch := c.nextOf("auth-challenge", 2*time.Second)
	ns, ok := m5.ParseHex(ch["ns"].(string), 16)
	if !ok {
		t.Fatalf("auth-challenge = %v", ch)
	}
	nd := m5.Random(16)
	c.send(`{"t":"auth","v":1,"nd":"` + m5.Hex(nd) + `","mac":"` + m5.Hex(m5.AuthMac(testKey, board, ns, nd)) + `"}`)
	okm := c.nextOf("auth-ok", 2*time.Second)
	if okm["mac"] != m5.Hex(m5.AuthOkMac(testKey, board, ns, nd)) {
		t.Fatalf("auth-ok con un mac sbagliato: %v", okm)
	}
	c.sk = m5.SessionKey(testKey, ns, nd)
	return c
}
