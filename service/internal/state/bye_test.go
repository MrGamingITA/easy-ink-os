package state

import (
	"encoding/json"
	"strings"
	"testing"
	"time"
)

func i64(v int64) *int64 { return &v }

func TestByeSummary(t *testing.T) {
	off := time.Date(2026, 10, 11, 19, 0, 0, 0, time.UTC)
	snap := Empty()
	snap.Today = Today{Tasks: 14, PermDisplay: 2, PermTerminal: 3, WaitAvgMs: 132000, LinesAdd: 1240, LinesDel: 380, Cost: 18.4}
	snap.Sessions = []Session{
		{ID: "a", Name: "api-server", State: Done, Since: 300, Summary: "Endpoint aggiunto."},
		{ID: "b", Name: "web-app", State: Permission, Since: 200, AttnAt: i64(250)},
		{ID: "c", Name: "firmware", State: Error, Since: 100, Error: "overloaded", AttnAt: nil},
		{ID: "d", Name: "docs-site", State: Done, Since: 400, Summary: "Guida aggiornata."},
		{ID: "e", Name: "vecchia", State: Done, Since: 500, Summary: "Archiviata: non deve comparire."},
		{ID: "f", Name: "attende", State: Input, Since: 50, AttnAt: i64(60)},
		{ID: "g", Name: "lavora", State: Working, Since: 10, AttnAt: i64(11)},
	}
	st := NewStore(snap)
	if err := st.Archive("e"); err != nil {
		t.Fatal(err)
	}
	b := st.ByeSummary(off)
	if b.Off != off.UnixMilli() {
		t.Fatalf("off %d", b.Off)
	}
	if b.Open != 6 || b.Done != 2 || b.Waiting != 3 {
		t.Fatalf("conteggi open %d done %d waiting %d", b.Open, b.Done, b.Waiting)
	}
	if b.Today == nil || b.Today.Tasks != 14 || b.Today.Cost != 18.4 {
		t.Fatalf("today %+v", b.Today)
	}
	if len(b.Sessions) != 6 {
		t.Fatalf("sessioni %+v", b.Sessions)
	}
	for _, s := range b.Sessions {
		if s.Name == "vecchia" {
			t.Fatal("l'archiviata compare nel riepilogo")
		}
		want := s.State == Permission || s.State == Input
		if (s.AttnAt != nil) != want {
			t.Fatalf("attnAt di %s (%s): %v", s.Name, s.State, s.AttnAt)
		}
	}
	// ultimo messaggio: since più recente tra le non archiviate (docs-site, 400), non l'ultima in elenco
	if b.Last == nil || b.Last.Name != "docs-site" || b.Last.Summary != "Guida aggiornata." {
		t.Fatalf("last %+v", b.Last)
	}
	// oltre 6 sessioni: restano le più urgenti, i conteggi le contano tutte
	snap.Sessions = append(snap.Sessions, Session{ID: "h", Name: "pausa", State: Idle, Since: 1})
	b = NewStore(snap).ByeSummary(off)
	if b.Open != 8 || len(b.Sessions) != MaxSessions {
		t.Fatalf("open %d sessioni %d", b.Open, len(b.Sessions))
	}
	for _, s := range b.Sessions {
		if s.State == Idle {
			t.Fatal("con più di 6 sessioni la meno urgente (in pausa) doveva restare fuori")
		}
	}
	// senza sessioni
	b = NewStore(Empty()).ByeSummary(off)
	raw, _ := json.Marshal(Bye{T: "bye", V: 1, Summary: b})
	if !strings.Contains(string(raw), `"sessions":[]`) || strings.Contains(string(raw), `"last"`) {
		t.Fatalf("riepilogo vuoto: %s", raw)
	}
}

// Caso peggiore: 6 sessioni e un riepilogo con testi pieni, tutti caratteri che JSON scrive come
// < (6 byte l'uno). Deve stare sotto i 3 KB senza bisogno di Fit (PROTOCOLLO.md, «bye»).
func TestByeWorstCaseSize(t *testing.T) {
	snap := Empty()
	big := strings.Repeat("<", 400)
	for i := 0; i < 9; i++ {
		snap.Sessions = append(snap.Sessions, Session{ID: string(rune('a' + i)), Name: big, State: Permission,
			Since: 1 << 50, AttnAt: i64(1 << 50), Summary: big})
	}
	snap.Today = Today{Tasks: 1 << 30, PermDisplay: 1 << 30, PermTerminal: 1 << 30, WaitAvgMs: 1 << 50, LinesAdd: 1 << 30, LinesDel: 1 << 30, Cost: 123456.7891}
	b := NewStore(snap).ByeSummary(time.UnixMilli(1 << 50))
	n := 1 << 30
	b.SessionsToday, b.Day = &n, "2026-10-11"
	raw, _ := json.Marshal(Bye{T: "bye", V: 1, Summary: b})
	if len(raw) > 3*1024 {
		t.Fatalf("caso peggiore %d byte, oltre 3 KB", len(raw))
	}
	t.Logf("bye nel caso peggiore: %d byte", len(raw))
	if f := b.Fit(MaxByeBytes); len(f.Sessions) != len(b.Sessions) || f.Last.Summary != b.Last.Summary {
		t.Fatal("Fit non doveva cambiare niente sotto i 4 KB")
	}
	// Fit sotto un limite piccolo: accorcia, poi toglie sessioni, poi last
	f := b.Fit(1200)
	raw, _ = json.Marshal(Bye{T: "bye", V: 1, Summary: f})
	if len(raw) > 1200 || len(f.Sessions) >= len(b.Sessions) {
		t.Fatalf("Fit(1200): %d byte, %d sessioni", len(raw), len(f.Sessions))
	}
	if len(b.Last.Summary) != MaxSummary || len(b.Sessions[0].Name) != MaxName {
		t.Fatal("Fit ha cambiato l'originale")
	}
	f = b.Fit(400)
	raw, _ = json.Marshal(Bye{T: "bye", V: 1, Summary: f})
	if len(raw) > 400 || f.Last != nil || len(f.Sessions) != 0 {
		t.Fatalf("Fit(400): %d byte %s", len(raw), raw)
	}
}
