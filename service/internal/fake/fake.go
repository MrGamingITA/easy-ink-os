// Package fake produce lo stato finto di M3: parte da protocol/examples/state.json (copiato in
// seed.json, un test controlla che siano uguali) con i tempi spostati ad adesso, e lo fa
// evolvere a ogni passo in modo plausibile.
package fake

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"sync"
	"time"

	"calamaio/internal/state"
)

//go:embed seed.json
var seedJSON []byte

// Seed restituisce lo stato dell'esempio con tutti i tempi spostati in modo che `time` = now.
func Seed(now time.Time) state.Snapshot {
	var m state.Message
	if err := json.Unmarshal(seedJSON, &m); err != nil {
		panic("seed.json non valido: " + err.Error())
	}
	d := now.UnixMilli() - m.Time
	s := m.Snapshot.Clone()
	for i := range s.Sessions {
		ss := &s.Sessions[i]
		ss.Since += d
		if ss.AttnAt != nil {
			v := *ss.AttnAt + d
			ss.AttnAt = &v
		}
	}
	if l := s.Limits; l != nil {
		if l.H5Reset != nil {
			v := *l.H5Reset + d
			l.H5Reset = &v
		}
		if l.D7Reset != nil {
			v := *l.D7Reset + d
			l.D7Reset = &v
		}
	}
	for i := range s.Log {
		s.Log[i].T += d
	}
	for i := range s.Agenda {
		s.Agenda[i].T += d
	}
	return s
}

// Generator fa evolvere lo stato nello Store.
type Generator struct {
	Store *state.Store
	Now   func() time.Time

	mu    sync.Mutex
	tick  int
	steps map[string]*run      // avanzamento del copione di ogni sessione
	pins  map[string]time.Time // sessioni forzate a mano: il copione non le tocca (zero = per sempre)
}

func New(store *state.Store) *Generator {
	return &Generator{Store: store, Now: time.Now, steps: map[string]*run{}, pins: map[string]time.Time{}}
}

// Reset rimette lo stato iniziale.
func (g *Generator) Reset() {
	g.mu.Lock()
	g.tick = 0
	g.steps = map[string]*run{}
	g.pins = map[string]time.Time{}
	g.mu.Unlock()
	g.Store.Replace(Seed(g.Now()))
}

// Run fa un passo ogni interval finché done non si chiude.
func (g *Generator) Run(done <-chan struct{}, interval time.Duration) {
	t := time.NewTicker(interval)
	defer t.Stop()
	for {
		select {
		case <-done:
			return
		case <-t.C:
			g.Step()
		}
	}
}

// Step fa evolvere lo stato di un passo (~4 s in `serve --fake`).
func (g *Generator) Step() {
	g.mu.Lock()
	defer g.mu.Unlock()
	now := g.Now()
	ms := now.UnixMilli()
	g.tick++
	tick := g.tick
	g.Store.Update(func(s *state.Snapshot) {
		for i := range s.Sessions {
			ss := &s.Sessions[i]
			if until, ok := g.pins[ss.ID]; ok {
				if until.IsZero() || now.Before(until) {
					continue
				}
				delete(g.pins, ss.ID)
			}
			script, ok := scripts[ss.ID]
			if !ok {
				continue
			}
			r := g.steps[ss.ID]
			if r == nil {
				r = &run{}
				g.steps[ss.ID] = r
			}
			c := &ctx{s: s, ss: ss, now: ms, k: r.k, run: r}
			r.k++
			if script(c) {
				// Fine del copione: la sessione riparte dall'esempio.
				for _, seed := range Seed(now).Sessions {
					if seed.ID == ss.ID {
						*ss = seed
					}
				}
				g.steps[ss.ID] = &run{}
				s.AddLog(ms, ss.Name, "nuovo prompt")
			}
		}
		global(s, tick, ms)
	})
}

// ForceRequest è il corpo di POST /debug/fake.
type ForceRequest struct {
	Session string  `json:"session"`
	State   string  `json:"state"`
	Detail  *string `json:"detail,omitempty"`
	Summary *string `json:"summary,omitempty"`
	Error   string  `json:"error,omitempty"`
	Ctx     *int    `json:"ctx,omitempty"`
	Test    *string `json:"test,omitempty"`
	// HoldS: per quanti secondi il copione lascia stare la sessione (default 60, 0 = finché non
	// arriva state "auto").
	HoldS *int `json:"hold_s,omitempty"`
}

// Force imposta a mano lo stato di una sessione (per id o nome); se non esiste la crea.
// Con State "auto" la sessione torna al copione.
func (g *Generator) Force(r ForceRequest) (state.Session, error) {
	if r.Session == "" {
		return state.Session{}, errors.New("manca la sessione")
	}
	if r.State != "auto" && !state.ValidState(r.State) {
		return state.Session{}, fmt.Errorf("stato %q non valido (working, permission, input, done, error, idle, compact, auto)", r.State)
	}
	if r.Test != nil && *r.Test != "" && *r.Test != "ok" && *r.Test != "ko" && *r.Test != "run" {
		return state.Session{}, fmt.Errorf("test %q non valido (ok, ko, run, vuoto = null)", *r.Test)
	}
	g.mu.Lock()
	defer g.mu.Unlock()
	now := g.Now()
	ms := now.UnixMilli()
	var out state.Session
	var err error
	g.Store.Update(func(s *state.Snapshot) {
		ss := s.Find(r.Session)
		if r.State == "auto" {
			if ss == nil {
				err = fmt.Errorf("sessione %q inesistente", r.Session)
				return
			}
			delete(g.pins, ss.ID)
			out = *ss
			return
		}
		if ss == nil {
			if len(s.Sessions) >= state.MaxSessions {
				err = fmt.Errorf("già %d sessioni: usa un nome esistente", state.MaxSessions)
				return
			}
			s.Sessions = append(s.Sessions, state.Session{ID: "fake-" + r.Session, Name: r.Session, Tasks: []state.Task{}})
			ss = &s.Sessions[len(s.Sessions)-1]
		}
		hold := 60
		if r.HoldS != nil {
			hold = *r.HoldS
		}
		if hold <= 0 {
			g.pins[ss.ID] = time.Time{}
		} else {
			g.pins[ss.ID] = now.Add(time.Duration(hold) * time.Second)
		}
		ss.State = r.State
		ss.Since = ms
		if r.Detail != nil {
			ss.Detail = *r.Detail
		}
		if r.Summary != nil {
			ss.Summary = *r.Summary
		} else if r.State != state.Done && r.State != state.Idle {
			ss.Summary = "" // un nuovo turno azzera il riepilogo (come UserPromptSubmit)
		}
		if r.Ctx != nil {
			v := *r.Ctx
			ss.Ctx = &v
		}
		if r.Test != nil {
			if *r.Test == "" {
				ss.Test = nil
			} else {
				ss.Test = strp(*r.Test)
			}
		}
		ss.AttnAt = nil
		if r.State == state.Permission || r.State == state.Input {
			ss.AttnAt = &ms
		}
		ss.Error = ""
		if r.State == state.Error {
			ss.Error = r.Error
			if ss.Error == "" {
				ss.Error = "rate_limit"
			}
		}
		s.AddLog(ms, ss.Name, logText(ss))
		out = *ss
	})
	return out, err
}

// logText: la riga dello storico per un cambio di stato.
func logText(ss *state.Session) string {
	switch ss.State {
	case state.Permission:
		return "chiede permesso: " + cmdOf(ss.Detail)
	case state.Input:
		return "aspetta una risposta"
	case state.Done:
		return "ha finito"
	case state.Error:
		return "errore: " + ss.Error
	case state.Compact:
		return "compatta il contesto"
	case state.Idle:
		return "inattiva"
	}
	return "al lavoro: " + ss.Detail
}

// cmdOf: "Bash: npm run test" → "npm run test".
func cmdOf(detail string) string {
	if len(detail) > 6 && detail[:6] == "Bash: " {
		return detail[6:]
	}
	return detail
}

func strp(s string) *string { return &s }
func intp(n int) *int       { return &n }
