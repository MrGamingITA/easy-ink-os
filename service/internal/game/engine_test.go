package game

import (
	"encoding/json"
	"fmt"
	"slices"
	"sync"
	"testing"
	"time"
)

// Fuso fisso: i test non dipendono dal fuso della macchina.
var zone = time.FixedZone("CEST", 2*3600)

var catalog8 = []string{"cappellino", "occhiali", "sciarpa", "papillon", "corona", "cuffie", "fiocco", "berretto-mago"}

// at: ottobre 2026 (il 12 è lunedì).
func at(day, hh, mm int) time.Time { return time.Date(2026, 10, day, hh, mm, 0, 0, zone) }

func newOn(t *testing.T, st State, catalog []string) *Engine {
	t.Helper()
	st.On = true
	return New(st, catalog, func() time.Time { return at(12, 9, 0) })
}

func ev(kind string, when time.Time, key string) Event {
	return Event{At: when, Session: "s1", Kind: kind, Key: key}
}

// work: attività ogni 5 minuti da from a to compresi; somma degli XP.
func work(e *Engine, from, to time.Time) int {
	sum := 0
	for t := from; !t.After(to); t = t.Add(5 * time.Minute) {
		g, _, _ := e.Apply(ev(KindActivity, t, ""))
		sum += g
	}
	return sum
}

func TestEachXPSource(t *testing.T) {
	cases := map[string]int{KindTaskDone: 10, KindTurnDone: 20, KindTestGreen: 15, KindSessionClosed: 10}
	for kind, want := range cases {
		e := newOn(t, State{}, catalog8)
		if g, _, _ := e.Apply(ev(KindActivity, at(12, 9, 0), "")); g != 5 {
			t.Fatalf("%s: bonus del primo lavoro = %d, voglio 5", kind, g)
		}
		if g, _, _ := e.Apply(ev(kind, at(12, 9, 1), "k1")); g != want {
			t.Errorf("%s: XP = %d, voglio %d", kind, g, want)
		}
	}
}

func TestActivityAndTestRunsGiveNoXP(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindActivity, at(12, 9, 0), ""))
	for i, k := range []string{KindActivity, KindTestOK, KindTestFail} {
		if g, _, _ := e.Apply(ev(k, at(12, 9, 1+i), fmt.Sprint("k", i))); g != 0 {
			t.Errorf("%s: XP = %d, voglio 0", k, g)
		}
	}
}

func TestNoPointsForPermissionsCostsTokens(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	for i, k := range []string{"permission", "permission_approved", "cost", "tokens", "night", "hours", ""} {
		g, ach, up := e.Apply(ev(k, at(12, 9, i), fmt.Sprint("p", i)))
		if g != 0 || len(ach) != 0 || up {
			t.Errorf("%q: gained=%d ach=%v up=%v, voglio niente", k, g, ach, up)
		}
	}
	st := e.State()
	if st.XP != 0 || !st.LastWork.IsZero() || len(st.Counters) != 0 || len(st.Seen) != 0 {
		t.Errorf("un Kind sconosciuto ha toccato lo stato: %+v", st)
	}
}

func TestFirstWorkBonusGrowsWithStreakUpTo35(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	want := []int{5, 10, 15, 20, 25, 30, 35, 35, 35}
	for i, w := range want {
		if g, _, _ := e.Apply(ev(KindActivity, at(12+i, 9, 0), "")); g != w {
			t.Errorf("giorno %d: bonus = %d, voglio %d", i+1, g, w)
		}
		// il secondo evento dello stesso giorno non ridà il bonus
		if g, _, _ := e.Apply(ev(KindActivity, at(12+i, 9, 5), "")); g != 0 {
			t.Errorf("giorno %d: secondo evento = %d, voglio 0", i+1, g)
		}
	}
	if s := e.State().Streak; s != 9 {
		t.Errorf("Streak = %d, voglio 9", s)
	}
}

func TestStreakSurvivesWeekend(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindActivity, at(16, 9, 0), ""))            // venerdì
	g, _, _ := e.Apply(ev(KindActivity, at(19, 9, 0), "")) // lunedì: 2 giorni di riposo
	if s := e.State().Streak; s != 2 || g != 10 {
		t.Errorf("dopo il fine settimana Streak=%d bonus=%d, voglio 2 e 10", s, g)
	}
}

func TestStreakResetsAfterThreeRestDays(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindActivity, at(12, 9, 0), ""))
	e.Apply(ev(KindActivity, at(13, 9, 0), ""))
	e.Tick(at(17, 9, 0)) // 13 → 17: tre giorni di riposo
	if s := e.State().Streak; s != 0 {
		t.Errorf("Tick dopo la serie rotta: Streak=%d, voglio 0", s)
	}
	g, _, _ := e.Apply(ev(KindActivity, at(17, 9, 0), ""))
	if s := e.State().Streak; s != 1 || g != 5 {
		t.Errorf("serie ripartita: Streak=%d bonus=%d, voglio 1 e 5", s, g)
	}
}

func TestWiseBreak(t *testing.T) {
	cases := []struct {
		name      string
		workUntil time.Time // lavoro dalle 9:00
		resume    time.Time
		want      int
	}{
		{"90 min poi 10 min", at(12, 10, 30), at(12, 10, 40), 15},
		{"85 min poi 10 min", at(12, 10, 25), at(12, 10, 35), 0},
		{"90 min poi 9 min", at(12, 10, 30), at(12, 10, 39), 0},
		{"90 min poi pranzo di 3 h", at(12, 10, 30), at(12, 13, 30), 15},
		{"90 min poi oltre 3 h: fine lavoro, non pausa", at(12, 10, 30), at(12, 13, 31), 0},
	}
	for _, c := range cases {
		e := newOn(t, State{}, catalog8)
		work(e, at(12, 9, 0), c.workUntil)
		g, _, _ := e.Apply(ev(KindActivity, c.resume, ""))
		if g != c.want {
			t.Errorf("%s: XP = %d, voglio %d", c.name, g, c.want)
		}
		if got := e.State().Counters[CounterWiseBreaks]; (got == 1) != (c.want > 0) {
			t.Errorf("%s: %s = %d", c.name, CounterWiseBreaks, got)
		}
	}
}

func TestTiredAfterThreeHoursHalvesXPUntilBreak(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	work(e, at(12, 9, 0), at(12, 11, 55))
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 11, 58), "a")); g != 10 {
		t.Errorf("prima delle 3 h: %d, voglio 10", g)
	}
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 12, 0), "b")); g != 5 {
		t.Errorf("a 3 h: %d, voglio 5", g)
	}
	if g, _, _ := e.Apply(ev(KindTestGreen, at(12, 12, 5), "c")); g != 8 {
		t.Errorf("verde da stanca: %d, voglio 8", g)
	}
	e.Tick(at(12, 12, 6))
	if !e.State().Tired {
		t.Error("dopo 3 h senza pausa la mascotte deve essere stanca")
	}
	e.Tick(at(12, 12, 16)) // 11 minuti di pausa
	if e.State().Tired {
		t.Error("in pausa la mascotte non è più stanca")
	}
	// ripresa: pausa saggia (15) + task pieno (10)
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 12, 16), "d")); g != 25 {
		t.Errorf("dopo la pausa: %d, voglio 25", g)
	}
}

func TestDailyCap(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindActivity, at(12, 9, 0), "")) // 5
	var gains []int
	for i := 0; i < 50; i++ {
		g, _, _ := e.Apply(ev(KindTaskDone, at(12, 9, 1+i), fmt.Sprint("t", i)))
		gains = append(gains, g)
	}
	// 5 XP del primo lavoro del giorno, poi task da 10 fino al tetto: l'ultimo è parziale.
	last := (DailyCap - 5) / 10 // indice del task che tocca il tetto
	if gains[last-1] != 10 || gains[last] != (DailyCap-5)%10+5*boolInt((DailyCap-5)%10 == 0) || gains[last+1] != 0 {
		t.Errorf("tetto %d: gains[%d..%d] = %v", DailyCap, last-1, last+1, gains[last-1:last+2])
	}
	st := e.State()
	if st.TodayXP != DailyCap || st.XP != DailyCap {
		t.Errorf("TodayXP=%d XP=%d, voglio %d", st.TodayXP, st.XP, DailyCap)
	}
	if st.Counters[CounterTasks] != 50 {
		t.Errorf("i task oltre il tetto si contano lo stesso per i traguardi: %d", st.Counters[CounterTasks])
	}
}

// Review Focus: giorno che cambia a mezzanotte durante una sessione lunga.
func TestMidnightDuringLongSession(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindActivity, at(12, 21, 0), ""))
	for m := 5; m <= 175; m += 5 { // 21:05 … 23:55, turni da 20: tetto raggiunto
		e.Apply(ev(KindTurnDone, at(12, 21, 0).Add(time.Duration(m)*time.Minute), fmt.Sprint("turn", m)))
	}
	st := e.State()
	if st.TodayXP != DailyCap || st.Today != "2026-10-12" || st.Streak != 1 {
		t.Fatalf("prima di mezzanotte: TodayXP=%d Today=%s Streak=%d", st.TodayXP, st.Today, st.Streak)
	}
	e.Tick(at(13, 0, 0).Add(30 * time.Second))
	st = e.State()
	if st.Today != "2026-10-13" || st.TodayXP != 0 || st.XP != DailyCap {
		t.Errorf("Tick a mezzanotte: Today=%s TodayXP=%d XP=%d", st.Today, st.TodayXP, st.XP)
	}
	// la sessione continua dopo mezzanotte: notte, niente punti, niente serie
	for i, m := range []int{5, 10, 15} {
		g, _, _ := e.Apply(ev(KindTurnDone, at(13, 0, m), fmt.Sprint("night", i)))
		if g != 0 {
			t.Errorf("turno alle 00:%02d: %d XP, voglio 0", m, g)
		}
	}
	if st = e.State(); st.Streak != 1 || st.TodayXP != 0 {
		t.Errorf("dopo mezzanotte: Streak=%d TodayXP=%d, voglio 1 e 0", st.Streak, st.TodayXP)
	}
	// il mattino dopo: bonus della serie 2 una volta sola, tetto nuovo
	if g, _, _ := e.Apply(ev(KindTurnDone, at(13, 9, 0), "morning")); g != 10+20 {
		t.Errorf("primo turno del mattino: %d, voglio 30", g)
	}
	if g, _, _ := e.Apply(ev(KindTurnDone, at(13, 9, 5), "morning2")); g != 20 {
		t.Errorf("secondo turno del mattino: %d, voglio 20", g)
	}
	if st = e.State(); st.Streak != 2 || st.TodayXP != 50 {
		t.Errorf("mattino: Streak=%d TodayXP=%d, voglio 2 e 50", st.Streak, st.TodayXP)
	}
}

func TestMidnightWithoutTick(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindActivity, at(12, 23, 50), ""))
	e.Apply(ev(KindTaskDone, at(12, 23, 55), "a"))
	g, _, _ := e.Apply(ev(KindTaskDone, at(13, 7, 0), "b"))
	st := e.State()
	if g != 10+10 || st.Today != "2026-10-13" || st.TodayXP != 20 || st.Streak != 2 {
		t.Errorf("g=%d Today=%s TodayXP=%d Streak=%d", g, st.Today, st.TodayXP, st.Streak)
	}
}

func TestNightGivesNothing(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	g1, a1, _ := e.Apply(ev(KindActivity, at(12, 2, 0), ""))
	g2, a2, _ := e.Apply(ev(KindTaskDone, at(12, 2, 5), "a"))
	st := e.State()
	if g1+g2 != 0 || len(a1)+len(a2) != 0 || st.Streak != 0 || st.Counters[CounterTasks] != 0 {
		t.Errorf("notte: XP=%d traguardi=%v Streak=%d task=%d", g1+g2, append(a1, a2...), st.Streak, st.Counters[CounterTasks])
	}
	if st.LastWork.IsZero() {
		t.Error("il lavoro notturno conta per l'energia")
	}
}

// Review Focus: eventi duplicati o rigiocati dopo un riavvio.
func TestDuplicateKeyIgnored(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindActivity, at(12, 9, 0), ""))
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 9, 1), "s1/task/1")); g != 10 {
		t.Fatalf("primo: %d", g)
	}
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 9, 1), "s1/task/1")); g != 0 {
		t.Errorf("duplicato: %d XP, voglio 0", g)
	}
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 9, 9), "s1/task/1")); g != 0 {
		t.Errorf("stessa chiave con un'altra ora: %d XP, voglio 0", g)
	}
	if n := e.State().Counters[CounterTasks]; n != 1 {
		t.Errorf("tasks_total = %d, voglio 1", n)
	}
}

func TestReplayAfterRestartNoDoubleXP(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	events := []Event{
		ev(KindActivity, at(12, 9, 0), ""),
		ev(KindTaskDone, at(12, 9, 1), "s1/task/1"),
		ev(KindTurnDone, at(12, 9, 2), "s1/stop/1"),
		ev(KindSessionClosed, at(12, 9, 3), "s1/end"),
	}
	for _, x := range events {
		e.Apply(x)
	}
	before := e.State()
	// riavvio: lo stato passa da JSON (come game.json) e gli eventi arrivano di nuovo
	raw, err := json.Marshal(before)
	if err != nil {
		t.Fatal(err)
	}
	var restored State
	if err := json.Unmarshal(raw, &restored); err != nil {
		t.Fatal(err)
	}
	e2 := New(restored, catalog8, func() time.Time { return at(12, 9, 10) })
	for _, x := range events {
		if g, ach, _ := e2.Apply(x); g != 0 || len(ach) != 0 {
			t.Errorf("rigiocato %s: %d XP %v", x.Kind, g, ach)
		}
	}
	after := e2.State()
	if after.XP != before.XP || after.Counters[CounterTasks] != 1 || after.Counters[CounterSessionsClosed] != 1 {
		t.Errorf("XP %d→%d task=%d chiuse=%d", before.XP, after.XP, after.Counters[CounterTasks], after.Counters[CounterSessionsClosed])
	}
}

func TestSeenPrunedAfter30DaysAndOldEventsIgnored(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(ev(KindTaskDone, at(1, 9, 0), "old"))
	e.Tick(at(31, 9, 1)) // 30 giorni e un minuto dopo
	if _, ok := e.State().Seen["old"]; ok {
		t.Error("chiave più vecchia di 30 giorni non potata")
	}
	e.Apply(ev(KindActivity, at(31, 9, 2), ""))
	tasks := e.State().Counters[CounterTasks]
	// lo stesso evento rigiocato dopo la potatura: è più vecchio della finestra, si ignora
	if g, _, _ := e.Apply(ev(KindTaskDone, at(1, 9, 0), "old")); g != 0 {
		t.Errorf("evento oltre la finestra: %d XP", g)
	}
	if n := e.State().Counters[CounterTasks]; n != tasks {
		t.Errorf("evento oltre la finestra contato: %d → %d", tasks, n)
	}
}

func TestLevelUpAndEvenLevelsUnlockInCatalogOrder(t *testing.T) {
	e := newOn(t, State{XP: 90}, catalog8)
	if st := e.State(); st.Level != 1 || st.XPNext != 100 {
		t.Fatalf("New: Level=%d XPNext=%d", st.Level, st.XPNext)
	}
	_, _, up := e.Apply(ev(KindTaskDone, at(12, 9, 0), "a")) // 5 + 10 → 105
	st := e.State()
	if !up || st.Level != 2 || st.XPNext != 346 || !slices.Equal(st.Unlocked, []string{"cappellino"}) {
		t.Errorf("livello 2: up=%v Level=%d XPNext=%d Unlocked=%v", up, st.Level, st.XPNext, st.Unlocked)
	}
	e = newOn(t, State{XP: 330, Unlocked: []string{"cappellino"}}, catalog8)
	e.Apply(ev(KindTurnDone, at(12, 9, 0), "b")) // → 355: livello 3, dispari
	if st = e.State(); st.Level != 3 || len(st.Unlocked) != 1 {
		t.Errorf("livello 3: Level=%d Unlocked=%v", st.Level, st.Unlocked)
	}
	e = newOn(t, State{XP: 750, Unlocked: []string{"cappellino"}}, catalog8)
	e.Apply(ev(KindTurnDone, at(12, 9, 0), "c")) // → 775: livello 4
	if st = e.State(); st.Level != 4 || !slices.Equal(st.Unlocked, []string{"cappellino", "occhiali"}) {
		t.Errorf("livello 4: Level=%d Unlocked=%v", st.Level, st.Unlocked)
	}
	// un accessorio già sbloccato (es. scelto dall'utente) si salta
	e = newOn(t, State{XP: 750, Unlocked: []string{"sciarpa"}}, catalog8)
	e.Apply(ev(KindTurnDone, at(12, 9, 0), "d"))
	if st = e.State(); !slices.Equal(st.Unlocked, []string{"sciarpa", "cappellino"}) {
		t.Errorf("sblocco in ordine saltando i già sbloccati: %v", st.Unlocked)
	}
}

func TestSpecialPendingOnlyWhenCatalogExhausted(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	_, ach, _ := e.Apply(ev(KindActivity, at(12, 9, 0), ""))
	if len(ach) == 0 || e.State().SpecialPending != "" {
		t.Errorf("catalogo non esaurito: ach=%v pending=%q", ach, e.State().SpecialPending)
	}
	e = newOn(t, State{Unlocked: []string{"cappellino"}}, []string{"cappellino"})
	_, ach, _ = e.Apply(ev(KindActivity, at(12, 9, 0), ""))
	if !slices.Contains(ach, "prima-sessione") || e.State().SpecialPending != "prima-sessione" {
		t.Errorf("catalogo esaurito: ach=%v pending=%q", ach, e.State().SpecialPending)
	}
	// una proposta alla volta: il traguardo successivo non la sovrascrive
	_, ach, _ = e.Apply(ev(KindTaskDone, at(12, 9, 1), "a"))
	if !slices.Contains(ach, "primo-task") || e.State().SpecialPending != "prima-sessione" {
		t.Errorf("seconda: ach=%v pending=%q", ach, e.State().SpecialPending)
	}
	e.AddSpecial("stella")
	st := e.State()
	if st.SpecialPending != "" || !slices.Contains(st.Unlocked, "stella") {
		t.Errorf("AddSpecial: pending=%q Unlocked=%v", st.SpecialPending, st.Unlocked)
	}
}

func TestWearOnlyUnlocked(t *testing.T) {
	e := newOn(t, State{Unlocked: []string{"cappellino"}}, catalog8)
	if err := e.Wear("corona"); err == nil {
		t.Error("Wear di un accessorio bloccato deve fallire")
	}
	if err := e.Wear("cappellino"); err != nil || e.State().Wear != "cappellino" {
		t.Errorf("Wear sbloccato: %v %q", err, e.State().Wear)
	}
	if err := e.Wear(""); err != nil || e.State().Wear != "" {
		t.Errorf("Wear vuoto: %v %q", err, e.State().Wear)
	}
}

func TestOffGivesNothingButKeepsProgress(t *testing.T) {
	e := New(State{XP: 500, Unlocked: []string{"cappellino"}}, catalog8, func() time.Time { return at(12, 9, 0) })
	if g, ach, _ := e.Apply(ev(KindTaskDone, at(12, 9, 0), "a")); g != 0 || len(ach) != 0 {
		t.Errorf("spento: %d XP %v", g, ach)
	}
	if st := e.State(); st.XP != 500 || st.Level != LevelFor(500) || len(st.Unlocked) != 1 {
		t.Errorf("progressi persi: %+v", st)
	}
	e.SetOn(true)
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 9, 1), "a")); g != 0 {
		t.Errorf("evento visto da spento e rigiocato da acceso: %d XP", g)
	}
	if g, _, _ := e.Apply(ev(KindTaskDone, at(12, 9, 2), "b")); g != 15 {
		t.Errorf("riacceso: %d XP, voglio 15 (bonus 5 + task 10)", g)
	}
}

func TestEnergy(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	if en := e.State().Energy; en != 100 {
		t.Fatalf("energia iniziale %d", en)
	}
	work(e, at(12, 9, 0), at(12, 12, 0))
	if en := e.State().Energy; en < 24 || en > 26 {
		t.Errorf("dopo 3 h di lavoro energia = %d, voglio 25±1", en)
	}
	e.Tick(at(12, 12, 30))
	if en := e.State().Energy; en < 84 || en > 86 {
		t.Errorf("dopo 30 min di pausa energia = %d, voglio 85±1", en)
	}
	e.Tick(at(12, 13, 0))
	if en := e.State().Energy; en != 100 {
		t.Errorf("energia oltre 100: %d", en)
	}
	work(e, at(12, 13, 0), at(12, 19, 0))
	if en := e.State().Energy; en != 0 {
		t.Errorf("energia sotto 0 o non scesa: %d", en)
	}
}

func TestConcurrentSessionsDone(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	for i, s := range []string{"a", "b", "c"} {
		e.Apply(Event{At: at(12, 9, i), Session: s, Kind: KindActivity})
	}
	var got []string
	for i, s := range []string{"a", "b", "c"} {
		_, ach, _ := e.Apply(Event{At: at(12, 9, 30+i), Session: s, Kind: KindSessionClosed, Key: s + "/end"})
		got = append(got, ach...)
	}
	if !slices.Contains(got, "tre-insieme") {
		t.Errorf("tre sessioni contemporanee finite: %v", got)
	}
	e = newOn(t, State{}, catalog8)
	got = nil
	for i, s := range []string{"a", "b", "c"} {
		e.Apply(Event{At: at(12, 10, 20*i), Session: s, Kind: KindActivity})
		_, ach, _ := e.Apply(Event{At: at(12, 10, 20*i+10), Session: s, Kind: KindSessionClosed, Key: s + "/end"})
		got = append(got, ach...)
	}
	if slices.Contains(got, "tre-insieme") || e.State().Counters[CounterConcurrentDone] != 1 {
		t.Errorf("sessioni in fila non sono contemporanee: %v %d", got, e.State().Counters[CounterConcurrentDone])
	}
}

func TestGreenInRow(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	n := 0
	apply := func(kind string) []string {
		n++
		_, ach, _ := e.Apply(ev(kind, at(12, 9, 0).Add(time.Duration(n)*time.Minute), fmt.Sprint("t", n)))
		return ach
	}
	for i := 0; i < 9; i++ {
		apply(KindTestOK)
	}
	apply(KindTestFail)
	if e.State().Counters[CounterGreenInRow] != 0 {
		t.Error("un test rosso deve azzerare la fila")
	}
	var got []string
	for i := 0; i < 9; i++ {
		got = append(got, apply(KindTestOK)...)
	}
	if slices.Contains(got, "verdi-10") {
		t.Fatal("traguardo a 9")
	}
	if ach := apply(KindTestGreen); !slices.Contains(ach, "verdi-10") {
		t.Errorf("10 verdi di fila: %v", ach)
	}
}

func TestWiseBreakDaysAchievement(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	var got []string
	for _, d := range []int{15, 16, 19, 20, 21} { // gio, ven, (fine settimana), lun, mar, mer
		work(e, at(d, 9, 0), at(d, 10, 30))
		_, ach, _ := e.Apply(ev(KindActivity, at(d, 10, 45), ""))
		got = append(got, ach...)
	}
	if !slices.Contains(got, "pause-settimana") {
		t.Errorf("5 giorni di lavoro con pausa saggia: %v (fila %d)", got, e.State().Counters[CounterWiseBreakDays])
	}
	// un giorno di lavoro senza pausa saggia rompe la fila
	e = newOn(t, State{}, catalog8)
	for _, d := range []int{12, 13} {
		work(e, at(d, 9, 0), at(d, 10, 30))
		e.Apply(ev(KindActivity, at(d, 10, 45), ""))
	}
	work(e, at(14, 9, 0), at(14, 9, 30))
	work(e, at(15, 9, 0), at(15, 10, 30))
	e.Apply(ev(KindActivity, at(15, 10, 45), ""))
	if n := e.State().Counters[CounterWiseBreakDays]; n != 1 {
		t.Errorf("fila dopo un giorno senza pausa = %d, voglio 1", n)
	}
}

func TestZeroAtUsesNow(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	e.Apply(Event{Session: "s1", Kind: KindActivity})
	if st := e.State(); !st.LastWork.Equal(at(12, 9, 0)) {
		t.Errorf("LastWork = %v", st.LastWork)
	}
}

func TestStateIsACopy(t *testing.T) {
	e := newOn(t, State{Unlocked: []string{"cappellino"}}, catalog8)
	e.Apply(ev(KindTaskDone, at(12, 9, 0), "a"))
	st := e.State()
	st.Unlocked[0] = "x"
	st.Seen["zzz"] = time.Time{}
	st.Counters[CounterTasks] = 99
	for k := range st.Achievements {
		delete(st.Achievements, k)
	}
	again := e.State()
	if again.Unlocked[0] != "cappellino" || len(again.Seen) != 1 || again.Counters[CounterTasks] != 1 || len(again.Achievements) == 0 {
		t.Errorf("State() espone lo stato interno: %+v", again)
	}
}

func TestConcurrentUse(t *testing.T) {
	e := newOn(t, State{}, catalog8)
	var wg sync.WaitGroup
	for g := 0; g < 4; g++ {
		wg.Add(1)
		go func(g int) {
			defer wg.Done()
			for i := 0; i < 100; i++ {
				when := at(12, 9, 0).Add(time.Duration(i) * time.Second)
				e.Apply(Event{At: when, Session: fmt.Sprint("s", g), Kind: KindTaskDone, Key: fmt.Sprint(g, "/", i)})
				e.Tick(when)
				_ = e.State()
			}
		}(g)
	}
	wg.Wait()
	if n := e.State().Counters[CounterTasks]; n != 400 {
		t.Errorf("tasks_total = %d, voglio 400", n)
	}
}

func boolInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

func TestDeclineSpecialOpensTheNextOne(t *testing.T) {
	e := newOn(t, State{Unlocked: []string{"cappellino"}}, []string{"cappellino"})
	e.Apply(ev(KindActivity, at(12, 9, 0), ""))
	if e.State().SpecialPending != "prima-sessione" {
		t.Fatalf("pending %q", e.State().SpecialPending)
	}
	e.DeclineSpecial()
	if p := e.State().SpecialPending; p != "" {
		t.Fatalf("dopo il no: %q", p)
	}
	_, ach, _ := e.Apply(ev(KindTaskDone, at(12, 9, 1), "a"))
	if !slices.Contains(ach, "primo-task") || e.State().SpecialPending != "primo-task" {
		t.Errorf("traguardo dopo il no: ach=%v pending=%q", ach, e.State().SpecialPending)
	}
}

func TestSetCatalogUnlocksFromNextEvenLevel(t *testing.T) {
	e := newOn(t, State{Unlocked: []string{"cappellino"}}, []string{"cappellino"})
	e.SetCatalog([]string{"cappellino", "berretto"})
	e.Apply(ev(KindActivity, at(12, 9, 0), ""))
	if p := e.State().SpecialPending; p != "" {
		t.Errorf("catalogo non più esaurito: pending %q", p)
	}
	e2 := newOn(t, State{XP: LevelStart(2) - 10}, []string{"cappellino"})
	e2.SetCatalog([]string{"berretto"})
	e2.Apply(ev(KindTaskDone, at(12, 9, 0), "a"))
	if u := e2.State().Unlocked; !slices.Equal(u, []string{"berretto"}) {
		t.Errorf("livello 2 con il catalogo nuovo: %v", u)
	}
}
