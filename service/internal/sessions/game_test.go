package sessions

import (
	"fmt"
	"strings"
	"testing"

	"calamaio/internal/game"
)

// Gli eventi del gioco ricavati dalla cattura vera: ogni hook è un'attività, poi task, turni con
// riepilogo, esiti dei test e sessioni chiuse finite, con chiavi stabili. Nessun evento per i permessi.
func TestGameEventsFromFixture(t *testing.T) {
	recs := loadFixture(t, "cattura-2026-10-11.jsonl")
	r := newRig(t, parseTS(t, recs[0].TS).Add(-1))
	r.denyTail(t)
	var got []game.Event
	r.e.OnGame = func(ev game.Event) { got = append(got, ev) }
	hooks := 0
	for _, x := range recs {
		if x.Source != "statusline" {
			hooks++
		}
		r.play(x)
	}
	count := map[string]int{}
	var keyed []string
	for _, ev := range got {
		count[ev.Kind]++
		if ev.At.IsZero() || ev.Session == "" {
			t.Fatalf("evento senza ora o sessione: %+v", ev)
		}
		if ev.Kind != game.KindActivity {
			keyed = append(keyed, ev.Kind+" "+ev.Key)
			if ev.Key == "" {
				t.Fatalf("evento senza chiave: %+v", ev)
			}
		}
	}
	t.Logf("eventi: %v\n%s", count, strings.Join(keyed, "\n"))
	if count[game.KindActivity] != hooks {
		t.Errorf("activity %d, hook %d", count[game.KindActivity], hooks)
	}
	want := map[string]int{game.KindTaskDone: 2, game.KindTurnDone: 3, game.KindTestFail: 1, game.KindSessionClosed: 2}
	for k, n := range want {
		if count[k] != n {
			t.Errorf("%s: %d, attesi %d", k, count[k], n)
		}
	}
	for _, k := range []string{"task|" + sidB + "|1", "task|" + sidB + "|2"} {
		if !contains(keyed, game.KindTaskDone+" "+k) {
			t.Errorf("manca %s in %v", k, keyed)
		}
	}
	// rigiocare la stessa cattura dà le stesse chiavi (un servizio riavviato non conta due volte)
	r2 := newRig(t, parseTS(t, recs[0].TS).Add(-1))
	r2.denyTail(t)
	var again []string
	r2.e.OnGame = func(ev game.Event) {
		if ev.Kind != game.KindActivity {
			again = append(again, ev.Kind+" "+ev.Key)
		}
	}
	for _, x := range recs {
		r2.play(x)
	}
	if fmt.Sprint(again) != fmt.Sprint(keyed) {
		t.Errorf("chiavi diverse rigiocando:\n%v\n%v", again, keyed)
	}
}

// Test: rosso poi verde nella stessa sessione = test_green; verde dopo verde = test_ok.
func TestGameTestGreen(t *testing.T) {
	r := newRig(t, parseTS(t, "2026-10-11T10:00:00+02:00"))
	var kinds []string
	r.e.OnGame = func(ev game.Event) {
		if ev.Kind != game.KindActivity {
			kinds = append(kinds, ev.Kind)
		}
	}
	run := func(id string, fail bool) {
		r.e.Hook([]byte(`{"session_id":"s","hook_event_name":"PreToolUse","tool_name":"Bash","tool_use_id":"` + id + `","tool_input":{"command":"go test ./..."}}`))
		ev := "PostToolUse"
		if fail {
			ev = "PostToolUseFailure"
		}
		r.e.Hook([]byte(`{"session_id":"s","hook_event_name":"` + ev + `","tool_name":"Bash","tool_use_id":"` + id + `","tool_input":{"command":"go test ./..."}}`))
	}
	run("t1", false)
	run("t2", true)
	run("t3", false)
	run("t4", false)
	// SessionEnd di una sessione finita: session_closed; di una che lavora: niente
	r.e.Hook([]byte(`{"session_id":"s","hook_event_name":"Stop","last_assistant_message":"Fatto: i test passano."}`))
	r.e.Hook([]byte(`{"session_id":"s","hook_event_name":"SessionEnd"}`))
	r.e.Hook([]byte(`{"session_id":"w","hook_event_name":"UserPromptSubmit","prompt":"vai"}`))
	r.e.Hook([]byte(`{"session_id":"w","hook_event_name":"SessionEnd"}`))
	want := []string{game.KindTestOK, game.KindTestFail, game.KindTestGreen, game.KindTestOK, game.KindTurnDone, game.KindSessionClosed}
	if fmt.Sprint(kinds) != fmt.Sprint(want) {
		t.Fatalf("eventi %v, attesi %v", kinds, want)
	}
}

func contains(l []string, s string) bool {
	for _, x := range l {
		if x == s {
			return true
		}
	}
	return false
}
