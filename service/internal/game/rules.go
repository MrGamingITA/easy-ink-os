// Package game: regole del gioco della mascotte (XP, livelli, energia, serie,
// traguardi). Puro: niente rete, niente file (solo achievements.json incorporato),
// il tempo arriva dagli eventi o da chi chiama.
package game

import (
	"math"
	"time"
)

// Tipi di evento. Nessun Kind per permessi, costi, token, notte o ore filate:
// un Kind sconosciuto non tocca lo stato.
const (
	KindTaskDone      = "task_done"      // task completato
	KindTurnDone      = "turn_done"      // turno finito con riepilogo
	KindTestGreen     = "test_green"     // test da rosso a verde
	KindSessionClosed = "session_closed" // sessione chiusa finita o archiviata
	KindActivity      = "activity"       // qualunque hook: conta per tempo ed energia, non per XP
	KindTestOK        = "test_ok"        // test verde che non era rosso: 0 XP, allunga la fila di verdi
	KindTestFail      = "test_fail"      // test rosso: 0 XP, azzera la fila di verdi
)

// Coefficienti della specifica (sezione 2).
const (
	XPTaskDone      = 10
	XPTurnDone      = 20
	XPTestGreen     = 15
	XPSessionClosed = 10
	XPWiseBreak     = 15
	XPStreakStep    = 5  // primo lavoro del giorno: 5 × giorni di serie…
	XPStreakMax     = 35 // …al massimo 35
	DailyCap        = 300

	WiseBreakWork = 90 * time.Minute // lavoro minimo prima di una pausa saggia
	BreakMin      = 10 * time.Minute // inattività minima che conta come pausa
	TiredAfter    = 3 * time.Hour    // senza pausa: stanca, XP dimezzati
)

// Scelte del motore non scritte nella specifica (da riportare nella spec).
const (
	// Una pausa più lunga è fine lavoro, non pausa saggia (altrimenti ogni notte lo sarebbe).
	WiseBreakMax = 3 * time.Hour
	// La serie regge fino a 2 giorni di riposo (il fine settimana): niente incentivo a lavorare 7 giorni su 7.
	StreakGraceDays = 2
	// Lavoro notturno [NightFromHour, NightToHour) ora locale: zero XP, niente contatori, niente serie.
	NightFromHour = 0
	NightToHour   = 6
	// Energia: −25 per ora di lavoro (100 → 25 in 3 h), +120 per ora di pausa (10 min → +20).
	EnergyDrainPerHour   = 25.0
	EnergyRecoverPerHour = 120.0
	// Chiavi degli eventi ricordate per 30 giorni; eventi più vecchi ignorati.
	SeenRetention = 30 * 24 * time.Hour
	// Le sessioni si ricordano 2 giorni (per le sessioni contemporanee).
	SessionRetention = 48 * time.Hour
)

// baseXP: XP di un tipo di evento; ok=false per i Kind che non esistono.
func baseXP(kind string) (int, bool) {
	switch kind {
	case KindTaskDone:
		return XPTaskDone, true
	case KindTurnDone:
		return XPTurnDone, true
	case KindTestGreen:
		return XPTestGreen, true
	case KindSessionClosed:
		return XPSessionClosed, true
	case KindActivity, KindTestOK, KindTestFail:
		return 0, true
	}
	return 0, false
}

// LevelCost: XP per passare dal livello n a n+1 = round(100 × n^1.3).
func LevelCost(n int) int {
	return int(math.Round(100 * math.Pow(float64(n), 1.3)))
}

// LevelStart: XP totali con cui si entra nel livello (livello 1 = 0).
func LevelStart(level int) int {
	sum := 0
	for n := 1; n < level; n++ {
		sum += LevelCost(n)
	}
	return sum
}

// LevelFor: livello raggiunto con xp totali.
func LevelFor(xp int) int {
	level, next := 1, LevelCost(1)
	for xp >= next {
		level++
		next += LevelCost(level)
	}
	return level
}

// half: XP dimezzati da stanca, arrotondati per eccesso (15 → 8).
func half(xp int) int { return (xp + 1) / 2 }

// streakBonus: bonus del primo lavoro del giorno.
func streakBonus(streak int) int { return min(XPStreakStep*streak, XPStreakMax) }

func isNight(t time.Time) bool {
	h := t.Hour()
	return h >= NightFromHour && h < NightToHour
}

func dayKey(t time.Time) string { return t.Format(time.DateOnly) }

// daysBetween: giorni di calendario tra due date "AAAA-MM-GG" (b − a); -1 se non valide.
func daysBetween(a, b string) int {
	ta, err1 := time.Parse(time.DateOnly, a)
	tb, err2 := time.Parse(time.DateOnly, b)
	if err1 != nil || err2 != nil {
		return -1
	}
	return int(math.Round(tb.Sub(ta).Hours() / 24))
}
