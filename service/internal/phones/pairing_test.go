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
