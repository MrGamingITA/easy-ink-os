package game

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"
)

// Fixture vere delle sessioni catturate (sola lettura).
const fixturePath = "../../../protocol/samples/fixtures/cattura-2026-10-11.jsonl"

type hookLine struct {
	TS      time.Time `json:"ts"`
	Source  string    `json:"source"`
	Event   string    `json:"event"`
	Payload struct {
		SessionID   string `json:"session_id"`
		TaskID      string `json:"task_id"`
		LastMessage string `json:"last_assistant_message"`
		ToolName    string `json:"tool_name"`
		ToolInput   struct {
			Command string `json:"command"`
		} `json:"tool_input"`
	} `json:"payload"`
}

func loadFixture(t *testing.T) []hookLine {
	t.Helper()
	f, err := os.Open(fixturePath)
	if err != nil {
		t.Fatalf("fixture: %v", err)
	}
	defer f.Close()
	var out []hookLine
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	for sc.Scan() {
		var h hookLine
		if err := json.Unmarshal(sc.Bytes(), &h); err != nil {
			t.Fatal(err)
		}
		if h.Source == "hook" {
			out = append(out, h)
		}
	}
	if err := sc.Err(); err != nil {
		t.Fatal(err)
	}
	return out
}

// toEvents: mappatura provvisoria hook → eventi del gioco, come quella prevista
// in fase 2 (Task 6): ogni hook è activity; più l'evento che vale XP.
func toEvents(h hookLine, at time.Time, suffix string) []Event {
	sid := h.Payload.SessionID + suffix
	out := []Event{{At: at, Session: sid, Kind: KindActivity}}
	key := func(k string) string { return sid + "/" + k }
	switch h.Event {
	case "TaskCompleted":
		out = append(out, Event{at, sid, KindTaskDone, key("task/" + h.Payload.TaskID)})
	case "Stop":
		if h.Payload.LastMessage != "" {
			out = append(out, Event{at, sid, KindTurnDone, key("stop/" + h.TS.Format(time.RFC3339Nano))})
		}
	case "PostToolUseFailure":
		if h.Payload.ToolName == "Bash" && strings.Contains(h.Payload.ToolInput.Command, "test") {
			out = append(out, Event{at, sid, KindTestFail, key("fail/" + h.TS.Format(time.RFC3339Nano))})
		}
	case "SessionEnd":
		out = append(out, Event{at, sid, KindSessionClosed, key("end")})
	}
	return out
}

func TestFixtureReplayExactXPAndNoDoubles(t *testing.T) {
	hooks := loadFixture(t)
	e := newOn(t, State{}, catalog8)
	byKind := map[string]int{}
	permXP := 0
	for _, h := range hooks {
		for _, x := range toEvents(h, h.TS, "") {
			g, _, _ := e.Apply(x)
			byKind[x.Kind] += g
			if strings.HasPrefix(h.Event, "Permission") {
				permXP += g
			}
		}
	}
	st := e.State()
	// 5 (primo lavoro, serie 1) + 2 task × 10 + 3 turni × 20 + 2 sessioni chiuse × 10
	if st.XP != 105 || byKind[KindTaskDone] != 20 || byKind[KindTurnDone] != 60 || byKind[KindSessionClosed] != 20 {
		t.Errorf("XP=%d per tipo=%v, voglio 105 (5+20+60+20)", st.XP, byKind)
	}
	if permXP != 0 {
		t.Errorf("i permessi hanno dato %d XP", permXP)
	}
	// rigiocate dopo un riavvio: nessun XP in più
	e2 := New(st, catalog8, func() time.Time { return hooks[len(hooks)-1].TS })
	for _, h := range hooks {
		for _, x := range toEvents(h, h.TS, "") {
			e2.Apply(x)
		}
	}
	if xp := e2.State().XP; xp != 105 {
		t.Errorf("fixture rigiocate: XP %d, voglio 105", xp)
	}
}

// Una giornata simulata: blocchi di lavoro riempiti di repliche della fixture,
// ognuna stirata a 30 minuti.
type block struct{ fromH, fromM, toH, toM int }

var (
	dayFull   = []block{{9, 0, 10, 45}, {11, 0, 13, 0}, {14, 0, 16, 0}, {16, 15, 18, 0}} // giornata 09–18 (DECISIONI)
	dayHalf   = []block{{9, 0, 10, 45}, {14, 0, 16, 0}}
	dayLight  = []block{{9, 0, 10, 0}}
	replayLen = 30 * time.Minute
)

func simulate(t *testing.T, hooks []hookLine, dailyCap int, week [5][]block, days int) (levels []int, xpDay []int, capAt time.Time) {
	t.Helper()
	ts0, ts1 := hooks[0].TS, hooks[len(hooks)-1].TS
	stretch := float64(replayLen) / float64(ts1.Sub(ts0))
	e := newOn(t, State{}, catalog8)
	e.dailyCap = dailyCap
	date := time.Date(2026, 10, 12, 0, 0, 0, 0, zone) // lunedì
	for worked := 0; worked < days; date = date.AddDate(0, 0, 1) {
		wd := date.Weekday()
		if wd == time.Saturday || wd == time.Sunday {
			e.Tick(date.Add(12 * time.Hour))
			continue
		}
		before := e.State().XP
		for bi, b := range week[wd-time.Monday] {
			from := date.Add(time.Duration(b.fromH)*time.Hour + time.Duration(b.fromM)*time.Minute)
			to := date.Add(time.Duration(b.toH)*time.Hour + time.Duration(b.toM)*time.Minute)
			for r := 0; from.Add(time.Duration(r+1)*replayLen).Compare(to) <= 0; r++ {
				base := from.Add(time.Duration(r) * replayLen)
				suffix := fmt.Sprintf("/%d/%d/%d", worked, bi, r)
				for _, h := range hooks {
					at := base.Add(time.Duration(float64(h.TS.Sub(ts0)) * stretch))
					for _, x := range toEvents(h, at, suffix) {
						e.Apply(x)
						if worked == 0 && capAt.IsZero() && e.st.TodayXP >= dailyCap {
							capAt = at
						}
					}
					e.Tick(at)
				}
			}
		}
		worked++
		st := e.State()
		levels = append(levels, st.Level)
		xpDay = append(xpDay, st.XP-before)
	}
	return levels, xpDay, capAt
}

func TestCalibration(t *testing.T) {
	hooks := loadFixture(t)
	typical := [5][]block{dayFull, dayHalf, dayFull, dayHalf, dayLight}
	scenarios := []struct {
		name string
		week [5][]block
	}{
		{"tipico (pieno, metà, pieno, metà, leggero)", typical},
		{"sempre pieno", [5][]block{dayFull, dayFull, dayFull, dayFull, dayFull}},
		{"sempre leggero", [5][]block{dayLight, dayLight, dayLight, dayLight, dayLight}},
	}
	for _, cap := range []int{DailyCap, 300, 1 << 30} {
		for _, sc := range scenarios {
			levels, xp, capAt := simulate(t, hooks, cap, sc.week, 30)
			capName := fmt.Sprint(cap)
			if cap == 1<<30 {
				capName = "nessuno"
			}
			t.Logf("tetto %s, %s: livello giorno 1 = %d, giorno 30 = %d; XP giorno 1 = %d, XP lunedì 2ª settimana = %d, venerdì = %d; tetto raggiunto il giorno 1: %s",
				capName, sc.name, levels[0], levels[29], xp[0], xp[5], xp[4], capTime(capAt))
		}
	}
	// verifica del piano sul giorno tipico con il tetto della specifica
	levels, _, _ := simulate(t, hooks, DailyCap, typical, 30)
	if levels[0] < 2 {
		t.Errorf("livello al giorno 1 = %d, voglio almeno 2", levels[0])
	}
	if levels[29] < 8 || levels[29] > 12 {
		t.Errorf("livello al giorno 30 = %d, voglio 8–12", levels[29])
	}
}

func capTime(t time.Time) string {
	if t.IsZero() {
		return "mai"
	}
	return t.Format("15:04")
}
