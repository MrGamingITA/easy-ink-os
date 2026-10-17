package state

import (
	"encoding/json"
	"strings"
	"testing"
	"unicode/utf8"
)

func TestTruncateUTF8(t *testing.T) {
	s := strings.Repeat("è", 20) // 2 byte ciascuna
	got := Truncate(s, 31)
	if len(got) != 30 || !utf8.ValidString(got) {
		t.Fatalf("Truncate = %q (%d byte)", got, len(got))
	}
	if Truncate("corto", 31) != "corto" {
		t.Fatal("stringa corta modificata")
	}
	if g := Truncate("ab€", 4); g != "ab" { // € = 3 byte
		t.Fatalf("Truncate = %q", g)
	}
}

func TestNormalizeLimits(t *testing.T) {
	var s Snapshot
	for i := 0; i < 8; i++ {
		s.Sessions = append(s.Sessions, Session{ID: "x", Name: strings.Repeat("n", 40), State: Working,
			Detail: strings.Repeat("d", 100), Summary: strings.Repeat("s", 200), Error: "rate_limit"})
	}
	for i := 0; i < 15; i++ {
		s.Sessions[0].Tasks = append(s.Sessions[0].Tasks, Task{"t", 0})
	}
	s.Sessions[1].State = Error
	s.Sessions[1].Error = ""
	for i := 0; i < 25; i++ {
		s.AddLog(int64(i), "n", "x")
	}
	n := s.Normalize()
	if len(n.Sessions) != MaxSessions || len(n.Sessions[0].Tasks) != MaxTasks || len(n.Log) != MaxLog {
		t.Fatalf("conteggi: %d sessioni, %d task, %d log", len(n.Sessions), len(n.Sessions[0].Tasks), len(n.Log))
	}
	ss := n.Sessions[0]
	if len(ss.Name) != MaxName || len(ss.Detail) != MaxDetail || len(ss.Summary) != MaxSummary {
		t.Fatal("testi non troncati")
	}
	if ss.Error != "" || n.Sessions[1].Error == "" {
		t.Fatal("error deve esserci solo con state=error")
	}
	if n.Log[0].T != 24 {
		t.Fatal("il log deve avere il più recente per primo")
	}
	if len(s.Sessions) != 8 {
		t.Fatal("Normalize ha modificato l'originale")
	}
}

func TestEmptyMarshalsAsArrays(t *testing.T) {
	st := NewStore(Empty())
	b, _ := json.Marshal(st.Message())
	for _, k := range []string{`"sessions":[]`, `"limits":null`, `"log":[]`, `"services":[]`, `"prs":[]`, `"agenda":[]`} {
		if !strings.Contains(string(b), k) {
			t.Fatalf("manca %s in %s", k, b)
		}
	}
	st.Update(func(s *Snapshot) { s.Sessions = append(s.Sessions, Session{ID: "a", Name: "a", State: Idle}) })
	b, _ = json.Marshal(st.Message())
	if !strings.Contains(string(b), `"tasks":[]`) || !strings.Contains(string(b), `"attnAt":null`) || strings.Contains(string(b), `"error"`) {
		t.Fatalf("sessione: %s", b)
	}
}

func TestTaskJSON(t *testing.T) {
	b, _ := json.Marshal([]Task{{"Leggi", 2}})
	if string(b) != `[["Leggi",2]]` {
		t.Fatalf("%s", b)
	}
	var ts []Task
	if err := json.Unmarshal([]byte(`[["A",1],["B",0]]`), &ts); err != nil || ts[0] != (Task{"A", 1}) {
		t.Fatalf("%v %v", ts, err)
	}
	if err := json.Unmarshal([]byte(`[["A"]]`), &ts); err == nil {
		t.Fatal("task a un elemento accettato")
	}
}

func TestSubscribeCoalesces(t *testing.T) {
	st := NewStore(Empty())
	ch, cancel := st.Subscribe()
	for i := 0; i < 10; i++ {
		st.Update(func(*Snapshot) {})
	}
	<-ch
	select {
	case <-ch:
		t.Fatal("dieci cambiamenti devono dare un solo avviso in attesa")
	default:
	}
	cancel()
	st.Update(func(*Snapshot) {})
	select {
	case <-ch:
		t.Fatal("avviso dopo la disiscrizione")
	default:
	}
	if st.Rev() != 11 {
		t.Fatalf("rev = %d", st.Rev())
	}
}
