package display

import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/santhosh-tekuri/jsonschema/v6"

	"calamaio/internal/sessions"
	"calamaio/internal/state"
)

func sessionIDs(m map[string]any) []string {
	out := []string{}
	ss, _ := m["sessions"].([]any)
	for _, s := range ss {
		out = append(out, s.(map[string]any)["id"].(string))
	}
	return out
}

// Archivia dal display: dismiss → ack (stesso id) → subito uno state senza la sessione.
func TestDismissFlow(t *testing.T) {
	sch := compileSchema(t)
	r := newRig(t, func(h *Hub) { h.MinInterval = time.Hour }) // lo state dopo l'ack non deve aspettare
	r.hub.Archiver = r.store
	r.store.Update(func(s *state.Snapshot) {
		s.Sessions = []state.Session{
			{ID: "s-done", Name: "finita", State: state.Done, Since: 1},
			{ID: "s-work", Name: "lavora", State: state.Working, Since: 1},
		}
	})
	c := r.dial(nil)
	st := c.greet("calamaio-3f2a")
	if len(sessionIDs(st)) != 2 {
		t.Fatalf("state iniziale: %v", sessionIDs(st))
	}

	send := func(msg string) (ack, next map[string]any) {
		t.Helper()
		var v any
		_ = json.Unmarshal([]byte(msg), &v)
		validate(t, sch, "dismiss del display", []byte(msg))
		c.send(msg)
		ack = c.next(2 * time.Second)
		b, _ := json.Marshal(ack)
		validate(t, sch, "ack del servizio", b)
		if ack["t"] != "ack" {
			t.Fatalf("atteso ack: %s", b)
		}
		return ack, nil
	}

	// rifiutato: la sessione lavora
	ack, _ := send(`{"t":"dismiss","v":1,"id":"d1","session":"s-work"}`)
	if ack["id"] != "d1" || ack["ok"] != false || !strings.Contains(ack["error"].(string), "finite") {
		t.Fatalf("dismiss su working: %v", ack)
	}
	c.none(200 * time.Millisecond)

	// sconosciuta
	ack, _ = send(`{"t":"dismiss","v":1,"id":"d2","session":"boh"}`)
	if ack["ok"] != false || ack["error"] != "Sessione sconosciuta" {
		t.Fatalf("dismiss sconosciuta: %v", ack)
	}

	// accettato: ack e poi subito state senza la sessione
	ack, _ = send(`{"t":"dismiss","v":1,"id":"d3","session":"s-done"}`)
	if ack["ok"] != true || ack["error"] != nil {
		t.Fatalf("dismiss: %v", ack)
	}
	st = c.next(2 * time.Second)
	if st["t"] != "state" || strings.Join(sessionIDs(st), ",") != "s-work" {
		t.Fatalf("state dopo l'ack: %v", st)
	}

	// id senza «d» o malformato: ignorato e registrato, nessuna risposta
	c.send(`{"t":"dismiss","v":1,"id":"c9","session":"s-work"}`)
	c.send(`{"t":"dismiss","v":1,"id":"d9"}`)
	c.none(300 * time.Millisecond)
	if r.hub.Ignored() < 2 {
		t.Fatalf("dismiss malformati non contati: %d", r.hub.Ignored())
	}

	// senza Archiver (per esempio un servizio vecchio): rifiuto esplicito
	r.hub.Archiver = nil
	ack, _ = send(`{"t":"dismiss","v":1,"id":"d4","session":"s-work"}`)
	if ack["ok"] != false {
		t.Fatalf("senza archiviazione: %v", ack)
	}
}

func TestDismissSchemaRejects(t *testing.T) {
	sch := compileSchema(t)
	for name, s := range map[string]string{
		"senza session":   `{"t":"dismiss","v":1,"id":"d1"}`,
		"id del servizio": `{"t":"dismiss","v":1,"id":"c1","session":"x"}`,
		"campo inventato": `{"t":"dismiss","v":1,"id":"d1","session":"x","all":true}`,
		"session vuota":   `{"t":"dismiss","v":1,"id":"d1","session":""}`,
		"id troppo lungo": `{"t":"dismiss","v":1,"id":"d` + strings.Repeat("1", 32) + `","session":"x"}`,
	} {
		v, _ := jsonschema.UnmarshalJSON(strings.NewReader(s))
		if sch.Validate(v) == nil {
			t.Errorf("%s: accettato, doveva essere rifiutato", name)
		}
	}
}

// Gli state costruiti dai dati veri (fixture rigiocate nella macchina a stati) rispettano lo schema
// a ogni passo, anche con una sessione archiviata.
func TestRealStatesMatchSchema(t *testing.T) {
	sch := compileSchema(t)
	for _, name := range []string{"cattura-2026-10-11.jsonl", "stopfailure-sintetico.jsonl"} {
		f, err := os.Open(filepath.Join(protocolDir, "samples", "fixtures", name))
		if err != nil {
			t.Fatal(err)
		}
		var clock time.Time
		store := state.NewStore(state.Empty())
		store.Now = func() time.Time { return clock }
		eng := sessions.New(store)
		eng.Now = func() time.Time { return clock }
		eng.ReadTail = func(string) ([]byte, error) { return nil, os.ErrNotExist }
		sc := bufio.NewScanner(f)
		sc.Buffer(make([]byte, 0, 1<<20), 16<<20)
		n := 0
		for sc.Scan() {
			var rec struct {
				TS      string          `json:"ts"`
				Source  string          `json:"source"`
				Payload json.RawMessage `json:"payload"`
			}
			if err := json.Unmarshal(sc.Bytes(), &rec); err != nil {
				t.Fatal(err)
			}
			clock, _ = time.Parse(time.RFC3339Nano, rec.TS)
			eng.Tick()
			if rec.Source == "statusline" {
				eng.StatusLine(rec.Payload)
			} else {
				eng.Hook(rec.Payload)
			}
			if n == 40 {
				_ = store.Archive("dcbb543b-f2ad-479f-90df-ddfbd6fd58cd")
			}
			b, _ := json.Marshal(store.Message())
			validate(t, sch, name, b)
			n++
		}
		f.Close()
		if n == 0 {
			t.Fatalf("%s vuoto", name)
		}
	}
}
