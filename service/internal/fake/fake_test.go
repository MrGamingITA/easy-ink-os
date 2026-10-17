package fake

import (
	"bytes"
	"encoding/json"
	"os"
	"reflect"
	"strings"
	"testing"
	"time"

	"calamaio/internal/state"
)

// seed.json è una copia di protocol/examples/state.json: devono restare uguali.
func TestSeedMatchesExample(t *testing.T) {
	ex, err := os.ReadFile("../../../protocol/examples/state.json")
	if err != nil {
		t.Fatal(err)
	}
	var a, b any
	if err := json.Unmarshal(ex, &a); err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(seedJSON, &b); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(a, b) {
		t.Fatal("internal/fake/seed.json diverso da protocol/examples/state.json: ricopialo")
	}
}

// Con now = time dell'esempio, Seed riproduce l'esempio esatto (stesso JSON).
func TestSeedAtExampleTimeIsExample(t *testing.T) {
	var ex state.Message
	if err := json.Unmarshal(seedJSON, &ex); err != nil {
		t.Fatal(err)
	}
	now := time.UnixMilli(ex.Time)
	st := state.NewStore(Seed(now))
	st.Now = func() time.Time { return now }
	got, _ := json.Marshal(st.Message())
	var a, b any
	_ = json.Unmarshal(got, &a)
	_ = json.Unmarshal(seedJSON, &b)
	if !reflect.DeepEqual(a, b) {
		t.Fatalf("Seed ≠ esempio:\n%s", got)
	}
}

func TestSeedRelativeToNow(t *testing.T) {
	now := time.Date(2026, 10, 11, 15, 0, 0, 0, time.UTC)
	s := Seed(now)
	// Nell'esempio web-app chiede il permesso da 60 s e api-server lavora da 12 minuti.
	web := s.Find("web-app")
	if web == nil || web.AttnAt == nil || now.UnixMilli()-*web.AttnAt != 60_000 {
		t.Fatalf("web-app = %+v", web)
	}
	if api := s.Find("api-server"); now.UnixMilli()-api.Since != 12*60_000 {
		t.Fatalf("api-server.since = %d", api.Since)
	}
	if s.Log[0].T != now.UnixMilli()-60_000 {
		t.Fatalf("log[0].t = %d", s.Log[0].T)
	}
}

type rig struct {
	clock time.Time
	store *state.Store
	gen   *Generator
}

func newRig() *rig {
	r := &rig{clock: time.Date(2026, 10, 11, 9, 0, 0, 0, time.UTC)}
	r.store = state.NewStore(Seed(r.clock))
	r.store.Now = func() time.Time { return r.clock }
	r.gen = New(r.store)
	r.gen.Now = func() time.Time { return r.clock }
	return r
}

func (r *rig) step() *state.Snapshot {
	r.clock = r.clock.Add(4 * time.Second)
	r.gen.Step()
	s := r.store.Snapshot()
	return &s
}

// La storia di web-app: permesso → lavoro → test → nuovo permesso → fine con riepilogo → riparte.
func TestWebAppStory(t *testing.T) {
	r := newRig()
	seedToday := r.store.Snapshot().Today
	var states []string
	var summary string
	for i := 0; i < 40; i++ {
		s := r.step()
		w := s.Find("web-app")
		if len(states) == 0 || states[len(states)-1] != w.State {
			states = append(states, w.State)
		}
		if w.State == state.Done {
			summary = w.Summary
		}
		if (w.State == state.Permission) != (w.AttnAt != nil) {
			t.Fatalf("passo %d: attnAt incoerente con lo stato %s", i, w.State)
		}
	}
	got := strings.Join(states, " ")
	if !strings.HasPrefix(got, "permission working permission working done permission") {
		t.Fatalf("sequenza di web-app: %s", got)
	}
	if summary == "" {
		t.Fatal("web-app finisce senza riepilogo")
	}
	td := r.store.Snapshot().Today
	// Fino a M5 il display non risponde ai permessi: il contatore del display resta quello
	// dell'esempio e tutte le risposte arrivano dal terminale.
	if td.PermDisplay != seedToday.PermDisplay || td.PermTerminal < seedToday.PermTerminal+2 || td.WaitAvgMs <= 0 {
		t.Fatalf("Oggi non aggiornato: %+v (esempio %+v)", td, seedToday)
	}
	for _, l := range r.store.Snapshot().Log {
		if strings.Contains(l.Text, "dal display") {
			t.Fatalf("evento impossibile prima di M5: %q", l.Text)
		}
	}
}

// Ogni passo cambia qualcosa e lo Store avvisa gli iscritti.
func TestEveryStepChangesSomething(t *testing.T) {
	r := newRig()
	ch, cancel := r.store.Subscribe()
	defer cancel()
	prev, _ := json.Marshal(r.store.Snapshot())
	for i := 0; i < 50; i++ {
		s := r.step()
		select {
		case <-ch:
		default:
			t.Fatalf("passo %d senza avviso", i)
		}
		cur, _ := json.Marshal(s)
		if bytes.Equal(cur, prev) {
			t.Fatalf("passo %d non cambia niente", i)
		}
		prev = cur
	}
}

func TestForceAndRelease(t *testing.T) {
	r := newRig()
	d := "Bash: rm -rf build"
	ss, err := r.gen.Force(ForceRequest{Session: "web-app", State: "permission", Detail: &d})
	if err != nil {
		t.Fatal(err)
	}
	if ss.State != "permission" || ss.Detail != d || ss.AttnAt == nil || *ss.AttnAt != r.clock.UnixMilli() {
		t.Fatalf("forzata = %+v", ss)
	}
	if l := r.store.Snapshot().Log[0]; l.Name != "web-app" || l.Text != "chiede permesso: rm -rf build" {
		t.Fatalf("log = %+v", l)
	}
	// Per 60 s il copione non la tocca.
	for i := 0; i < 10; i++ {
		if w := r.step().Find("web-app"); w.State != "permission" || w.Detail != d {
			t.Fatalf("passo %d: il copione ha toccato la sessione forzata: %+v", i, w)
		}
	}
	if _, err := r.gen.Force(ForceRequest{Session: "web-app", State: "auto"}); err != nil {
		t.Fatal(err)
	}
	moved := false
	for i := 0; i < 20 && !moved; i++ {
		moved = r.step().Find("web-app").Detail != d
	}
	if !moved {
		t.Fatal("dopo auto il copione non riprende")
	}
}

func TestForceHoldExpires(t *testing.T) {
	r := newRig()
	hold := 8
	if _, err := r.gen.Force(ForceRequest{Session: "b2", State: "idle", HoldS: &hold}); err != nil {
		t.Fatal(err)
	}
	r.step() // +4 s: ancora ferma
	if s := r.store.Snapshot().Find("b2"); s.State != "idle" {
		t.Fatalf("stato = %s", s.State)
	}
	r.step()
	r.step() // +12 s: il copione riprende
	if s := r.store.Snapshot().Find("b2"); s.State == "idle" {
		t.Fatal("la sospensione non scade")
	}
}

func TestForceNewSessionAndErrors(t *testing.T) {
	r := newRig()
	ss, err := r.gen.Force(ForceRequest{Session: "nuova", State: "error"})
	if err != nil || ss.ID != "fake-nuova" || ss.Error != "rate_limit" {
		t.Fatalf("nuova = %+v, %v", ss, err)
	}
	if n := len(r.store.Snapshot().Sessions); n != 5 {
		t.Fatalf("sessioni = %d", n)
	}
	if _, err := r.gen.Force(ForceRequest{Session: "x", State: "dorme"}); err == nil {
		t.Fatal("stato non valido accettato")
	}
	if _, err := r.gen.Force(ForceRequest{Session: "", State: "idle"}); err == nil {
		t.Fatal("sessione vuota accettata")
	}
	if _, err := r.gen.Force(ForceRequest{Session: "sesta", State: "idle"}); err != nil {
		t.Fatal(err)
	}
	if _, err := r.gen.Force(ForceRequest{Session: "settima", State: "idle"}); err == nil {
		t.Fatal("settima sessione accettata: il protocollo ne vuole al massimo 6")
	}
	if _, err := r.gen.Force(ForceRequest{Session: "fantasma", State: "auto"}); err == nil {
		t.Fatal("auto su sessione inesistente accettato")
	}
}
