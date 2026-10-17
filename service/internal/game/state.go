package game

import (
	"maps"
	"slices"
	"time"
)

// Event: un fatto delle sessioni già tradotto dal servizio.
type Event struct {
	At      time.Time `json:"at"`      // zero = adesso
	Session string    `json:"session"` // id della sessione (vuoto: nessuna)
	Kind    string    `json:"kind"`    // Kind*
	Key     string    `json:"key"`     // id univoco per deduplicare (ignorato per activity)
}

// Contatori interpretati dai traguardi (campo when.counter di achievements.json).
const (
	CounterSessions       = "sessions_total"    // sessioni viste
	CounterSessionsClosed = "sessions_closed"   // sessioni chiuse finite o archiviate
	CounterTasks          = "tasks_total"       // task completati
	CounterTurns          = "turns_total"       // turni finiti con riepilogo
	CounterGreens         = "greens_total"      // test da rosso a verde
	CounterGreenInRow     = "green_in_row"      // test verdi di fila (fila attuale)
	CounterConcurrentDone = "concurrent_done"   // record di sessioni contemporanee finite bene
	CounterWiseBreaks     = "wise_breaks_total" // pause sagge
	CounterWiseBreakDays  = "wise_break_days"   // giorni di lavoro di fila con almeno una pausa saggia
	CounterDaysWorked     = "days_worked"       // giorni con lavoro (non notturno)
	CounterStreak         = "streak"            // = State.Streak
	CounterLevel          = "level"             // = State.Level
)

var knownCounters = []string{
	CounterSessions, CounterSessionsClosed, CounterTasks, CounterTurns, CounterGreens,
	CounterGreenInRow, CounterConcurrentDone, CounterWiseBreaks, CounterWiseBreakDays,
	CounterDaysWorked, CounterStreak, CounterLevel,
}

// SessionSpan: quando una sessione è stata attiva (per le sessioni contemporanee).
type SessionSpan struct {
	First  time.Time `json:"first"`
	Last   time.Time `json:"last"`
	Closed time.Time `json:"closed,omitzero"` // chiusa finita o archiviata
}

// State: tutto ciò che si salva in game.json.
type State struct {
	On             bool                 `json:"on"`
	XP             int                  `json:"xp"`     // totali
	Level          int                  `json:"level"`  // ricavato da XP
	XPNext         int                  `json:"xpNext"` // XP totali per il livello successivo
	Energy         int                  `json:"energy"` // 0–100
	Streak         int                  `json:"streak"` // giorni di serie (0 = serie rotta)
	Today          string               `json:"today"`  // AAAA-MM-GG locale
	TodayXP        int                  `json:"todayXP"`
	LastWork       time.Time            `json:"lastWork"`  // ultimo evento di lavoro
	LastBreak      time.Time            `json:"lastBreak"` // fine dell'ultima pausa ≥ 10 min = inizio del blocco di lavoro
	Achievements   map[string]time.Time `json:"achievements"`
	Unlocked       []string             `json:"unlocked"`
	Wear           string               `json:"wear"`
	SpecialPending string               `json:"specialPending"` // traguardo che dà diritto a un accessorio speciale
	// Chiavi già contate con l'ora dell'evento, potate a 30 giorni.
	// (Il piano diceva map[string]bool: per potare serve l'ora.)
	Seen map[string]time.Time `json:"seen"`

	// Campi aggiunti al piano (servono alle regole).
	Tired       bool                   `json:"tired"`       // stanca: XP dimezzati fino alla pausa
	Counters    map[string]int         `json:"counters"`    // Counter*
	LastDay     string                 `json:"lastDay"`     // ultimo giorno con lavoro (serie)
	PrevDay     string                 `json:"prevDay"`     // giorno di lavoro precedente a LastDay
	BreakDay    string                 `json:"breakDay"`    // ultimo giorno con pausa saggia
	EnergyAt    time.Time              `json:"energyAt"`    // istante a cui si riferisce EnergyExact
	EnergyExact float64                `json:"energyExact"` // energia senza arrotondamento
	Sessions    map[string]SessionSpan `json:"sessions"`
}

func (s State) clone() State {
	c := s
	c.Achievements = maps.Clone(s.Achievements)
	c.Unlocked = slices.Clone(s.Unlocked)
	c.Seen = maps.Clone(s.Seen)
	c.Counters = maps.Clone(s.Counters)
	c.Sessions = maps.Clone(s.Sessions)
	return c
}

func (s *State) ensureMaps() {
	if s.Achievements == nil {
		s.Achievements = map[string]time.Time{}
	}
	if s.Seen == nil {
		s.Seen = map[string]time.Time{}
	}
	if s.Counters == nil {
		s.Counters = map[string]int{}
	}
	if s.Sessions == nil {
		s.Sessions = map[string]SessionSpan{}
	}
}
