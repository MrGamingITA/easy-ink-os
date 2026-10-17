package game

import (
	"errors"
	"math"
	"slices"
	"sync"
	"time"
)

// Engine applica le regole a uno State. Sicuro per l'uso da più goroutine.
type Engine struct {
	mu       sync.Mutex
	st       State
	catalog  []string // accessori del catalogo, nell'ordine di sblocco
	now      func() time.Time
	loc      *time.Location // fuso dei giorni (mezzanotte, notte): quello di now()
	dailyCap int            // tetto giornaliero (DailyCap; i test di taratura lo variano)
	prunedAt time.Time      // ultima potatura (al massimo una all'ora)
}

// New: motore sopra uno stato salvato (anche vuoto). catalog: id degli accessori
// del catalogo nell'ordine di sblocco; now: orologio (nil = time.Now).
func New(st State, catalog []string, now func() time.Time) *Engine {
	if now == nil {
		now = time.Now
	}
	st = st.clone()
	st.ensureMaps()
	if st.EnergyAt.IsZero() {
		st.EnergyExact, st.Energy = 100, 100
	}
	e := &Engine{st: st, catalog: slices.Clone(catalog), now: now, loc: now().Location(), dailyCap: DailyCap}
	e.refreshLevel()
	return e
}

// State: copia dello stato (da salvare o da mostrare).
func (e *Engine) State() State {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.st.clone()
}

// SetOn accende o spegne il gioco. Spento: niente XP né traguardi, ma i progressi
// restano e le chiavi degli eventi si ricordano (riaccendendo non si rigiocano).
func (e *Engine) SetOn(on bool) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.st.On = on
	if !on {
		e.st.Tired = false
	}
}

// Wear: indossa un accessorio sbloccato, "" per nessuno.
func (e *Engine) Wear(id string) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	if id != "" && !slices.Contains(e.st.Unlocked, id) {
		return errors.New("accessorio non sbloccato: " + id)
	}
	e.st.Wear = id
	return nil
}

// AddSpecial: sblocca un accessorio speciale creato da Claude e chiude la proposta.
func (e *Engine) AddSpecial(id string) error {
	if id == "" {
		return errors.New("id vuoto")
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if !slices.Contains(e.st.Unlocked, id) {
		e.st.Unlocked = append(e.st.Unlocked, id)
	}
	e.st.SpecialPending = ""
	return nil
}

// DeclineSpecial: l'utente ha detto «No» all'accessorio speciale: la proposta per quel traguardo si
// chiude e il prossimo traguardo (col catalogo ancora esaurito) ne apre una nuova.
func (e *Engine) DeclineSpecial() {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.st.SpecialPending = ""
}

// SetCatalog: accessori sbloccabili salendo di livello, in ordine (la libreria è cambiata). Non
// sblocca niente da solo: vale dal prossimo livello pari.
func (e *Engine) SetCatalog(catalog []string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.catalog = slices.Clone(catalog)
}

// Tick: energia, stanchezza, mezzanotte, serie rotta, potatura. Da chiamare spesso
// (es. ogni minuto); non dà mai XP.
func (e *Engine) Tick(now time.Time) {
	e.mu.Lock()
	defer e.mu.Unlock()
	s := &e.st
	now = now.In(e.loc)
	e.prune(now)
	e.rollDay(now)
	if !s.LastWork.IsZero() {
		e.advanceEnergy(now, now.Sub(s.LastWork) < BreakMin)
	}
	s.Tired = s.On && !s.LastWork.IsZero() && now.Sub(s.LastWork) < BreakMin &&
		s.LastWork.Sub(s.LastBreak) >= TiredAfter
	if s.LastDay != "" && daysBetween(s.LastDay, dayKey(now)) > 1+StreakGraceDays {
		s.Streak = 0
	}
}

// Apply: un evento. Ritorna gli XP guadagnati (pausa saggia e bonus del giorno
// compresi), i traguardi nuovi e se il livello è salito.
func (e *Engine) Apply(ev Event) (gained int, newAchievements []string, levelUp bool) {
	base, ok := baseXP(ev.Kind)
	if !ok {
		return 0, nil, false
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	s := &e.st

	at := ev.At
	if at.IsZero() {
		at = e.now()
	}
	at = at.In(e.loc)
	// più vecchio della memoria delle chiavi: potrebbe essere già contato
	if !s.LastWork.IsZero() && at.Before(s.LastWork.Add(-SeenRetention)) {
		return 0, nil, false
	}
	keyed := ev.Key != "" && ev.Kind != KindActivity
	if keyed {
		if _, dup := s.Seen[ev.Key]; dup {
			return 0, nil, false
		}
	}
	// il tempo non torna indietro: un evento in ritardo vale come adesso
	t := at
	if t.Before(s.LastWork) {
		t = s.LastWork
	}
	if keyed {
		s.Seen[ev.Key] = t
	}
	e.prune(t)
	e.rollDay(t)
	counting := s.On && !isNight(t)

	// blocco di lavoro, energia e pausa saggia
	if s.LastWork.IsZero() {
		s.LastBreak = t
		if s.EnergyAt.IsZero() || s.EnergyAt.Before(t) {
			s.EnergyAt = t
		}
	} else {
		gap := t.Sub(s.LastWork)
		e.advanceEnergy(t, gap < BreakMin)
		if gap >= BreakMin {
			wise := s.LastWork.Sub(s.LastBreak) >= WiseBreakWork && gap <= WiseBreakMax
			s.LastBreak = t // blocco nuovo: non più stanca
			if wise && counting {
				gained += e.addXP(XPWiseBreak, t)
				s.Counters[CounterWiseBreaks]++
				e.creditBreakDay(dayKey(s.LastWork))
			}
		}
	}
	s.LastWork = t

	if counting {
		// primo lavoro del giorno: serie e bonus
		if d := dayKey(t); d != s.LastDay {
			if gap := daysBetween(s.LastDay, d); s.LastDay != "" && gap >= 1 && gap <= 1+StreakGraceDays {
				s.Streak++
			} else {
				s.Streak = 1
			}
			s.PrevDay, s.LastDay = s.LastDay, d
			s.Counters[CounterDaysWorked]++
			gained += e.addXP(streakBonus(s.Streak), t)
		}
		gained += e.addXP(base, t)
		e.count(ev, t)
	}
	s.Tired = s.On && s.LastWork.Sub(s.LastBreak) >= TiredAfter

	if !counting {
		return gained, nil, false
	}
	old := s.Level
	e.refreshLevel()
	levelUp = s.Level > old
	for l := old + 1; l <= s.Level; l++ {
		if l%2 == 0 {
			e.unlockNext()
		}
	}
	newAchievements = e.checkAchievements(t)
	return gained, newAchievements, levelUp
}

// addXP: dimezzati da stanca, poi tagliati al tetto del giorno.
func (e *Engine) addXP(xp int, t time.Time) int {
	s := &e.st
	if xp <= 0 {
		return 0
	}
	if t.Sub(s.LastBreak) >= TiredAfter {
		xp = half(xp)
	}
	xp = max(0, min(xp, e.dailyCap-s.TodayXP))
	s.TodayXP += xp
	s.XP += xp
	return xp
}

func (e *Engine) count(ev Event, t time.Time) {
	c := e.st.Counters
	switch ev.Kind {
	case KindTaskDone:
		c[CounterTasks]++
	case KindTurnDone:
		c[CounterTurns]++
	case KindTestGreen:
		c[CounterGreens]++
		c[CounterGreenInRow]++
	case KindTestOK:
		c[CounterGreenInRow]++
	case KindTestFail:
		c[CounterGreenInRow] = 0
	}
	if ev.Session == "" {
		return
	}
	span, known := e.st.Sessions[ev.Session]
	if !known {
		span.First = t
		c[CounterSessions]++
	}
	span.Last = t
	if ev.Kind == KindSessionClosed && span.Closed.IsZero() {
		span.Closed = t
		c[CounterSessionsClosed]++
	}
	e.st.Sessions[ev.Session] = span
	if ev.Kind == KindSessionClosed {
		c[CounterConcurrentDone] = max(c[CounterConcurrentDone], e.concurrentDone(span))
	}
}

// concurrentDone: quante sessioni chiuse bene erano attive tutte insieme con me.
// In una dimensione, intervalli che si sovrappongono a due a due hanno un punto
// comune: basta provare gli inizi che cadono nel mio intervallo.
func (e *Engine) concurrentDone(me SessionSpan) int {
	best := 1
	for _, p := range e.st.Sessions {
		if p.Closed.IsZero() || p.First.Before(me.First) || p.First.After(me.Closed) {
			continue
		}
		n := 0
		for _, q := range e.st.Sessions {
			if !q.Closed.IsZero() && !q.First.After(p.First) && !q.Closed.Before(p.First) {
				n++
			}
		}
		best = max(best, n)
	}
	return best
}

// creditBreakDay: giorno di lavoro con pausa saggia; allunga la fila se anche il
// giorno di lavoro precedente l'aveva (con al massimo 2 giorni di riposo in mezzo).
func (e *Engine) creditBreakDay(day string) {
	s := &e.st
	if day != s.LastDay || s.BreakDay == day {
		return
	}
	if s.BreakDay != "" && s.BreakDay == s.PrevDay && daysBetween(s.PrevDay, s.LastDay) <= 1+StreakGraceDays {
		s.Counters[CounterWiseBreakDays]++
	} else {
		s.Counters[CounterWiseBreakDays] = 1
	}
	s.BreakDay = day
}

func (e *Engine) counter(name string) int {
	switch name {
	case CounterLevel:
		return e.st.Level
	case CounterStreak:
		return e.st.Streak
	}
	return e.st.Counters[name]
}

func (e *Engine) checkAchievements(t time.Time) []string {
	s := &e.st
	var got []string
	for _, a := range achievements {
		if _, done := s.Achievements[a.ID]; done || e.counter(a.When.Counter) < a.When.Min {
			continue
		}
		s.Achievements[a.ID] = t
		got = append(got, a.ID)
	}
	// catalogo esaurito: il traguardo dà diritto a un accessorio speciale (una proposta alla volta)
	if len(got) > 0 && s.SpecialPending == "" && e.catalogExhausted() {
		s.SpecialPending = got[0]
	}
	return got
}

func (e *Engine) catalogExhausted() bool {
	for _, id := range e.catalog {
		if !slices.Contains(e.st.Unlocked, id) {
			return false
		}
	}
	return true
}

// unlockNext: il primo accessorio del catalogo non ancora sbloccato.
func (e *Engine) unlockNext() {
	for _, id := range e.catalog {
		if !slices.Contains(e.st.Unlocked, id) {
			e.st.Unlocked = append(e.st.Unlocked, id)
			return
		}
	}
}

func (e *Engine) refreshLevel() {
	e.st.Level = LevelFor(e.st.XP)
	e.st.XPNext = LevelStart(e.st.Level + 1)
}

// rollDay: a mezzanotte (o al primo evento del giorno dopo) azzera il tetto.
// Mai all'indietro: un evento in ritardo di ieri conta su oggi.
func (e *Engine) rollDay(t time.Time) {
	if d := dayKey(t); d > e.st.Today {
		e.st.Today, e.st.TodayXP = d, 0
	}
}

func (e *Engine) advanceEnergy(t time.Time, working bool) {
	s := &e.st
	if s.EnergyAt.IsZero() {
		s.EnergyAt = t
		return
	}
	if !t.After(s.EnergyAt) {
		return
	}
	h := t.Sub(s.EnergyAt).Hours()
	if working {
		s.EnergyExact -= h * EnergyDrainPerHour
	} else {
		s.EnergyExact += h * EnergyRecoverPerHour
	}
	s.EnergyExact = math.Max(0, math.Min(100, s.EnergyExact))
	s.Energy = int(math.Round(s.EnergyExact))
	s.EnergyAt = t
}

func (e *Engine) prune(ref time.Time) {
	if !e.prunedAt.IsZero() && ref.Sub(e.prunedAt) < time.Hour && !ref.Before(e.prunedAt) {
		return
	}
	e.prunedAt = ref
	s := &e.st
	for k, t := range s.Seen {
		if t.Before(ref.Add(-SeenRetention)) {
			delete(s.Seen, k)
		}
	}
	for id, sp := range s.Sessions {
		if sp.Last.Before(ref.Add(-SessionRetention)) {
			delete(s.Sessions, id)
		}
	}
}
