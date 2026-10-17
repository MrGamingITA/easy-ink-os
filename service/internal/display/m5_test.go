package display

import (
	"errors"
	"strings"
	"testing"
	"time"

	"calamaio/internal/m5"
)

// Abbinamento a livello di hub con un display scritto a mano: messaggi fuori sequenza, pair-done
// con un mac sbagliato, auth senza sfida. I flussi completi (CLI, hook vero, attacchi) sono in
// cmd/calamaio/m5_test.go.
func TestPairSequenceAndBadDoneMac(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.greet("calamaio-3f2a")

	before := r.hub.Ignored()
	c.send(`{"t":"pair-key","v":1,"id":"p1","pkD":"` + strings.Repeat("a", 64) + `"}`) // nessun abbinamento
	c.send(`{"t":"auth","v":1,"nd":"` + strings.Repeat("a", 32) + `","mac":"` + strings.Repeat("b", 32) + `"}`)
	waitFor(t, time.Second, func() bool { return r.hub.Ignored() == before+2 })

	if _, err := r.hub.PairCode("calamaio-3f2a", "123456"); !errors.Is(err, ErrNoPairing) {
		t.Fatalf("codice senza abbinamento: %v", err)
	}
	st, err := r.hub.PairStart("calamaio-3f2a")
	if err != nil {
		t.Fatal(err)
	}
	begin := c.nextOf("pair-begin", time.Second)
	if begin["id"] != st.ID {
		t.Fatalf("pair-begin = %v", begin)
	}
	if _, err := r.hub.PairCode("calamaio-3f2a", "123456"); !errors.Is(err, ErrPairNotReady) {
		t.Fatalf("codice prima di pair-key: %v", err)
	}
	skD, pkD, _ := m5.NewKeyPair(nil)
	c.send(`{"t":"pair-key","v":1,"id":"` + st.ID + `","pkD":"` + m5.Hex(pkD) + `"}`)
	reveal := c.nextOf("pair-reveal", time.Second)
	pkS, _ := m5.ParseHex(reveal["pkS"].(string), 32)
	commit, _ := m5.ParseHex(begin["commit"].(string), 32)
	if !m5.Equal(m5.Commit(pkS), commit) {
		t.Fatal("pkS non corrisponde all'impegno")
	}
	Z, _ := m5.X25519(skD, pkS)
	T := m5.Transcript("calamaio-3f2a", commit, pkD, pkS)
	K := m5.PairKey(Z, T)
	if _, err := r.hub.PairCode("calamaio-3f2a", "12a456"); err == nil {
		t.Fatal("codice non numerico accettato")
	}
	if _, err := r.hub.PairCode("calamaio-3f2a", m5.SAS(Z, T)); err != nil {
		t.Fatal(err)
	}
	conf := c.nextOf("pair-confirm", time.Second)
	if conf["mac"] != m5.Hex(m5.PairConfirmMac(K, T)) {
		t.Fatalf("pair-confirm = %v", conf)
	}
	c.send(`{"t":"pair-done","v":1,"id":"` + st.ID + `","mac":"` + m5.Hex(m5.PairConfirmMac(K, T)) + `"}`) // mac di pair-s, non di pair-d
	if ab := c.nextOf("pair-abort", time.Second); ab["reason"] != "mac" {
		t.Fatalf("pair-abort = %v", ab)
	}
	if d := statusOf(r.hub, "calamaio-3f2a"); d.Paired || d.Pairing.Stage != StageAborted || d.Pairing.Reason != "mac" {
		t.Fatalf("stato = %+v %+v", d, d.Pairing)
	}
	if len(r.hub.PairedBoards()) != 0 {
		t.Fatal("chiave salvata con un pair-done falso")
	}
	c.none(150 * time.Millisecond) // e niente sfida
}
