package phones

import (
	"context"
	"strings"
	"testing"
	"time"
)

func TestManagerRedeemRevokeAndStreams(t *testing.T) {
	st, _ := Open("")
	m := NewManager(st)
	t.Cleanup(m.Close)
	changes := 0
	m.OnChange = func() { changes++; _ = m.Wanted() } // OnChange può richiamare il Manager: niente lucchetti presi
	if m.Wanted() {
		t.Fatal("acceso senza telefoni né finestra")
	}
	code, _ := m.OpenWindow()
	if !m.Wanted() || changes != 1 {
		t.Fatalf("finestra: wanted=%v changes=%d", m.Wanted(), changes)
	}
	tok, p, err := m.Redeem(code, "Telefono", "")
	if err != nil || !m.Wanted() || changes != 2 {
		t.Fatalf("redeem: %v %d", err, changes)
	}
	ctx, done := m.Track(context.Background(), p.ID)
	defer done()
	if err := m.Revoke(p.ID); err != nil {
		t.Fatal(err)
	}
	select {
	case <-ctx.Done():
	case <-time.After(time.Second):
		t.Fatal("la revoca non chiude lo stream aperto")
	}
	if _, ok := m.Lookup(tok); ok || m.Wanted() || changes != 3 {
		t.Fatalf("dopo la revoca: wanted=%v changes=%d", m.Wanted(), changes)
	}
	ctx2, done2 := m.Track(context.Background(), p.ID)
	defer done2()
	if ctx2.Err() == nil {
		t.Fatal("contesto vivo per un telefono revocato")
	}
}

func TestManagerRedeemReplacesOldToken(t *testing.T) {
	st, _ := Open("")
	m := NewManager(st)
	t.Cleanup(m.Close)
	c1, _ := m.OpenWindow()
	tok1, p1, _ := m.Redeem(c1, "A", "")
	c2, _ := m.OpenWindow()
	tok2, p2, err := m.Redeem(c2, "A", tok1)
	if err != nil || p2.ID == p1.ID {
		t.Fatalf("%v %+v", err, p2)
	}
	if _, ok := m.Lookup(tok1); ok {
		t.Fatal("il token vecchio vale ancora")
	}
	if _, ok := m.Lookup(tok2); !ok || st.Len() != 1 {
		t.Fatalf("telefoni: %d", st.Len())
	}
	c3, _ := m.OpenWindow()
	if _, _, err := m.Redeem(c3, "B", strings.Repeat("f", 64)); err != nil || st.Len() != 2 {
		t.Fatalf("un token vecchio sconosciuto si ignora: %v %d", err, st.Len())
	}
}

func TestManagerWrongCodeNotifies(t *testing.T) {
	st, _ := Open("")
	m := NewManager(st)
	t.Cleanup(m.Close)
	changes := 0
	m.OnChange = func() { changes++ }
	m.OpenWindow()
	for i := 0; i < MaxAttempts; i++ {
		m.Redeem(strings.Repeat("e", 32), "x", "")
	}
	if m.Wanted() || changes != 1+MaxAttempts {
		t.Fatalf("la quinta prova sbagliata deve chiudere la finestra e avvisare: %v %d", m.Wanted(), changes)
	}
}
