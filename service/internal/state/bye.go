package state

import (
	"encoding/json"
	"time"
)

// MaxByeBytes: il messaggio `bye` serializzato non supera 4 KB (docs/PROTOCOLLO.md, «bye»).
// Con i limiti per campo il caso peggiore sta sotto i 3 KB (TestByeWorstCaseSize).
const MaxByeBytes = 4 * 1024

// ByeSession è una sessione ancora aperta allo spegnimento, come la mostra il riepilogo «PC spento».
type ByeSession struct {
	Name  string `json:"name"`
	State string `json:"state"`
	// AttnAt: da quando aspetta l'utente (solo permission e input, come nello `state`).
	AttnAt *int64 `json:"attnAt,omitempty"`
}

// ByeLast è l'ultimo riepilogo di fine turno («ULTIMO MESSAGGIO · nome»).
type ByeLast struct {
	Name    string `json:"name"`
	Summary string `json:"summary"`
}

// ByeSummary è il campo `summary` di `bye`. Solo `off` è obbligatorio.
type ByeSummary struct {
	Off int64 `json:"off"` // ms epoch dello spegnimento
	// Day: giorno locale (AAAA-MM-GG) delle statistiche di Today e SessionsToday.
	Day string `json:"day,omitempty"`
	// SessionsToday: sessioni con almeno un evento oggi (nil con lo stato finto).
	SessionsToday *int   `json:"sessionsToday,omitempty"`
	Today         *Today `json:"today,omitempty"`
	// Conteggi sulle sessioni ancora aperte e non archiviate (anche oltre le 6 elencate).
	Open    int `json:"open"`
	Done    int `json:"done"`
	Waiting int `json:"waiting"` // permission, input o error, come isAttn del simulatore
	// Sessions: al massimo MaxSessions, scelte e ordinate come nello `state`.
	Sessions []ByeSession `json:"sessions"`
	Last     *ByeLast     `json:"last,omitempty"`
}

// Bye è il messaggio completo.
type Bye struct {
	T       string     `json:"t"`
	V       int        `json:"v"`
	Summary ByeSummary `json:"summary"`
}

// Waiting: la sessione aspetta l'utente (isAttn del simulatore: permission, input, error).
func Waiting(st string) bool { return st == Permission || st == Input || st == Error }

// ByeSummary costruisce il riepilogo dallo stato attuale: sessioni non archiviate, Oggi, ultimo
// riepilogo di fine turno. Day e SessionsToday li aggiunge chi conosce la giornata (sessions.Engine).
func (s *Store) ByeSummary(off time.Time) ByeSummary {
	s.mu.Lock()
	snap := s.snap.Clone()
	arch := make(map[string]bool, len(s.archived))
	for k := range s.archived {
		arch[k] = true
	}
	s.mu.Unlock()
	visible := []Session{}
	for _, ss := range snap.Sessions {
		if !arch[ss.ID] {
			visible = append(visible, ss)
		}
	}
	today := snap.Today
	b := ByeSummary{Off: off.UnixMilli(), Today: &today, Open: len(visible), Sessions: []ByeSession{}}
	var last *Session
	for i := range visible {
		ss := &visible[i]
		switch {
		case ss.State == Done:
			b.Done++
		case Waiting(ss.State):
			b.Waiting++
		}
		// Ultimo messaggio: il riepilogo più recente (since della sessione), non l'ultima in elenco.
		if ss.Summary != "" && (last == nil || ss.Since >= last.Since) {
			last = ss
		}
	}
	for _, ss := range mostUrgent(visible, MaxSessions) {
		one := ByeSession{Name: Truncate(ss.Name, MaxName), State: ss.State}
		if (ss.State == Permission || ss.State == Input) && ss.AttnAt != nil {
			at := *ss.AttnAt
			one.AttnAt = &at
		}
		b.Sessions = append(b.Sessions, one)
	}
	if last != nil {
		b.Last = &ByeLast{Name: Truncate(last.Name, MaxName), Summary: Truncate(last.Summary, MaxSummary)}
	}
	return b
}

// Fit riduce il riepilogo finché il messaggio sta in max byte: prima accorcia l'ultimo riepilogo,
// poi toglie le sessioni elencate dalla fine, poi l'ultimo riepilogo. Rete di sicurezza: con i
// limiti per campo non serve.
func (b ByeSummary) Fit(max int) ByeSummary {
	size := func() int {
		raw, _ := json.Marshal(Bye{T: "bye", V: 1, Summary: b})
		return len(raw)
	}
	if size() <= max {
		return b
	}
	b.Sessions = append([]ByeSession(nil), b.Sessions...)
	if b.Last != nil {
		l := *b.Last
		l.Summary = Truncate(l.Summary, 40)
		b.Last = &l
	}
	for size() > max && len(b.Sessions) > 0 {
		b.Sessions = b.Sessions[:len(b.Sessions)-1]
	}
	if size() > max {
		b.Last = nil
	}
	return b
}
