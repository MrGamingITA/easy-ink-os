package sessions

import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"

	"calamaio/internal/state"
)

const fixturesDir = "../../../protocol/samples/fixtures"

type rec struct {
	TS        string          `json:"ts"`
	Source    string          `json:"source"`
	Event     string          `json:"event"`
	Synthetic bool            `json:"synthetic"`
	Payload   json.RawMessage `json:"payload"`
}

func loadFixture(t *testing.T, name string) []rec {
	t.Helper()
	f, err := os.Open(filepath.Join(fixturesDir, name))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	var out []rec
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 0, 1<<20), 16<<20)
	for sc.Scan() {
		var r rec
		if err := json.Unmarshal(sc.Bytes(), &r); err != nil {
			t.Fatal(err)
		}
		out = append(out, r)
	}
	return out
}

// rig: Engine con orologio finto e transcript finti.
type rig struct {
	t     *testing.T
	now   time.Time
	store *state.Store
	e     *Engine
	tails map[string][]byte
	// tailAt, se c'è, sceglie la coda del transcript in base all'ora (sostituisce tails).
	tailAt func(path string, now time.Time) []byte
}

const (
	sidA     = "dcbb543b-f2ad-479f-90df-ddfbd6fd58cd"
	sidB     = "89b92428-b19d-4433-892d-3780c01dd012"
	tPathA   = "/home/utente/.claude/projects/-home-utente-progetto-prova/" + sidA + ".jsonl"
	plainEnd = `{"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"Fatto."}]}}` + "\n"
)

// denyTail: il transcript di A finisce con il permesso negato solo tra il diniego vero
// (15:10:49) e il prompt successivo (15:11:00); prima e dopo finisce con un messaggio normale.
func (r *rig) denyTail(t *testing.T) {
	tail, err := os.ReadFile(filepath.Join(fixturesDir, "transcript-negato.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	from, _ := time.Parse(time.RFC3339Nano, "2026-10-11T15:10:49.169+02:00")
	to, _ := time.Parse(time.RFC3339Nano, "2026-10-11T15:11:00.094+02:00")
	r.tailAt = func(p string, now time.Time) []byte {
		if p == tPathA && !now.Before(from) && now.Before(to) {
			return tail
		}
		return []byte(plainEnd)
	}
}

func newRig(t *testing.T, start time.Time) *rig {
	r := &rig{t: t, now: start, tails: map[string][]byte{}}
	r.store = state.NewStore(state.Empty())
	r.store.Now = func() time.Time { return r.now }
	r.e = New(r.store)
	r.e.Now = func() time.Time { return r.now }
	r.e.ReadTail = func(p string) ([]byte, error) {
		if r.tailAt != nil {
			return r.tailAt(p, r.now), nil
		}
		if b, ok := r.tails[p]; ok {
			return b, nil
		}
		return nil, os.ErrNotExist
	}
	return r
}

// at porta l'orologio a ts facendo scattare i controlli a tempo lungo la strada.
func (r *rig) at(ts time.Time) {
	for r.now.Before(ts) {
		step := ts.Sub(r.now)
		if step > 250*time.Millisecond {
			step = 250 * time.Millisecond
		}
		r.now = r.now.Add(step)
		r.e.Tick()
	}
}

func (r *rig) play(x rec) {
	ts, err := time.Parse(time.RFC3339Nano, x.TS)
	if err != nil {
		r.t.Fatal(err)
	}
	r.at(ts)
	if x.Source == "statusline" {
		r.e.StatusLine(x.Payload)
	} else {
		r.e.Hook(x.Payload)
	}
}

func (r *rig) session(id string) *state.Session {
	for _, s := range r.store.Snapshot().Sessions {
		if s.ID == id {
			c := s
			return &c
		}
	}
	return nil
}
