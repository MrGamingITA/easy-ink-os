package sessions

import (
	"encoding/json"
	"path/filepath"
	"testing"
	"time"

	"calamaio/internal/state"
)

// casi non presenti nella cattura, costruiti a mano con la stessa forma dei payload veri

func (r *rig) hook(sid string, m map[string]any) {
	m["session_id"] = sid
	if _, ok := m["cwd"]; !ok {
		m["cwd"] = "/home/utente/progetto-prova"
	}
	m["transcript_path"] = "/t/" + sid + ".jsonl"
	b, _ := json.Marshal(m)
	r.e.Hook(b)
}

func (r *rig) status(sid string, m map[string]any) {
	m["session_id"] = sid
	b, _ := json.Marshal(m)
	r.e.StatusLine(b)
}

func (r *rig) wait(d time.Duration) { r.at(r.now.Add(d)) }

func TestNamesDuplicates(t *testing.T) {
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	r.hook("3f000000-1", map[string]any{"hook_event_name": "SessionStart", "cwd": "/a/web"})
	r.hook("a1000000-2", map[string]any{"hook_event_name": "SessionStart", "cwd": "/b/web"})
	r.hook("c0000000-3", map[string]any{"hook_event_name": "SessionStart", "cwd": "/b/api"})
	got := map[string]string{}
	for _, s := range r.store.Snapshot().Sessions {
		got[s.ID] = s.Name
	}
	if got["3f000000-1"] != "web-3f" || got["a1000000-2"] != "web-a1" || got["c0000000-3"] != "api" {
		t.Fatalf("nomi: %v", got)
	}
	r.status("c0000000-3", map[string]any{"session_name": "Refactoring API"})
	if r.session("c0000000-3").Name != "Refactoring API" {
		t.Fatal("session_name ha la precedenza")
	}
}

func TestTodayMidnightAndCostDeltas(t *testing.T) {
	r := newRig(t, time.Date(2026, 10, 11, 23, 59, 0, 0, time.Local))
	cost := func(usd float64, add, del int) map[string]any {
		return map[string]any{"cost": map[string]any{"total_cost_usd": usd, "total_lines_added": add, "total_lines_removed": del}}
	}
	// sessione già aperta prima dell'avvio del servizio: il primo totale fa solo da base
	r.status("s1", cost(5, 100, 10))
	r.status("s1", cost(5.5, 120, 10))
	if td := r.store.Snapshot().Today; td.Cost != 0.5 || td.LinesAdd != 20 {
		t.Fatalf("delta: %+v", td)
	}
	r.wait(2 * time.Minute) // mezzanotte
	if td := r.store.Snapshot().Today; td.Cost != 0 || td.LinesAdd != 0 {
		t.Fatalf("Oggi non azzerato a mezzanotte: %+v", td)
	}
	r.status("s1", cost(6, 121, 12))
	if td := r.store.Snapshot().Today; td.Cost != 0.5 || td.LinesAdd != 1 || td.LinesDel != 2 {
		t.Fatalf("dopo mezzanotte conta solo il nuovo delta: %+v", td)
	}
	// /clear o nuova conversazione con totali più bassi: nuova base, niente negativi
	r.status("s1", cost(0.1, 0, 0))
	r.status("s1", cost(0.3, 5, 0))
	if td := r.store.Snapshot().Today; td.Cost != 0.7 || td.LinesAdd != 6 {
		t.Fatalf("nuova base: %+v", td)
	}
}

func TestTodayPersisted(t *testing.T) {
	dir := t.TempDir()
	start := time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local)
	r := newRig(t, start)
	r.e.TodayFile = filepath.Join(dir, "oggi.json")
	r.hook("s", map[string]any{"hook_event_name": "TaskCreated", "task_id": "1", "task_subject": "A"})
	r.hook("s", map[string]any{"hook_event_name": "TaskCompleted", "task_id": "1", "task_subject": "A"})
	r.wait(11 * time.Second)
	r2 := newRig(t, start.Add(time.Hour))
	r2.e.TodayFile = r.e.TodayFile
	r2.e.LoadToday()
	if r2.store.Snapshot().Today.Tasks != 1 {
		t.Fatal("Oggi non ripreso dopo il riavvio")
	}
	r3 := newRig(t, start.Add(24*time.Hour))
	r3.e.TodayFile = r.e.TodayFile
	r3.e.LoadToday()
	if r3.store.Snapshot().Today.Tasks != 0 {
		t.Fatal("Oggi di ieri ripreso")
	}
}

func TestLimitsAbsentAndExpired(t *testing.T) {
	now := time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local)
	r := newRig(t, now)
	r.status("s", map[string]any{}) // piano senza limiti, o prima risposta non ancora arrivata
	if r.store.Snapshot().Limits != nil {
		t.Fatal("limiti senza dati")
	}
	reset := float64(now.Add(30 * time.Minute).Unix())
	r.status("s", map[string]any{"rate_limits": map[string]any{"five_hour": map[string]any{"used_percentage": 12.6, "resets_at": reset},
		"spend_limit": map[string]any{"used_percentage": 40, "resets_at": reset, "used_usd": 40.5, "limit_usd": 100, "period": "monthly"}}})
	l := r.store.Snapshot().Limits
	if l == nil || *l.H5 != 13 || l.D7 != nil || *l.H5Reset != int64(reset)*1000 {
		t.Fatalf("limiti: %+v", l)
	}
	if sp, ok := r.e.Debug()["spend_limit"].(Spend); !ok || *sp.UsedUSD != 40.5 || sp.Period != "monthly" {
		t.Fatalf("spend_limit: %+v", r.e.Debug())
	}
	// un'altra sessione senza rate_limits non cancella i limiti dell'account
	r.status("altra", map[string]any{})
	if r.store.Snapshot().Limits == nil {
		t.Fatal("limiti persi")
	}
	r.wait(31 * time.Minute)
	if r.store.Snapshot().Limits != nil {
		t.Fatal("finestra scaduta ancora presente")
	}
}

func TestTodoWriteTasks(t *testing.T) {
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	todo := func(st1, st2 string) {
		r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "TodoWrite", "tool_use_id": "u",
			"tool_input": map[string]any{"todos": []any{
				map[string]any{"content": "Uno", "status": st1, "activeForm": "Uno"},
				map[string]any{"content": "Due", "status": st2, "activeForm": "Due"}}}})
	}
	todo("in_progress", "pending")
	todo("completed", "in_progress")
	todo("completed", "in_progress") // ripetuto: non conta due volte
	s := r.session("s")
	if len(s.Tasks) != 2 || s.Tasks[0].Status != 2 || s.Tasks[1].Status != 1 {
		t.Fatalf("task: %+v", s.Tasks)
	}
	if r.store.Snapshot().Today.Tasks != 1 {
		t.Fatalf("task completati oggi: %d", r.store.Snapshot().Today.Tasks)
	}
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "TaskUpdate", "tool_use_id": "v",
		"tool_input": map[string]any{"taskId": "9", "status": "in_progress", "subject": "Nove"}})
	if s := r.session("s"); len(s.Tasks) != 3 || s.Tasks[2].Status != 1 {
		t.Fatalf("TaskUpdate in_progress: %+v", s.Tasks)
	}
}

func TestEscInterruptWhileWorking(t *testing.T) {
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	r.tails["/t/s.jsonl"] = []byte(`{"type":"user","message":{"content":[{"type":"text","text":"[Request interrupted by user]"}]}}`)
	r.hook("s", map[string]any{"hook_event_name": "UserPromptSubmit", "prompt": "vai"})
	r.status("s", map[string]any{})
	r.wait(2 * time.Second)
	if s := r.session("s"); s.State != state.Idle || s.Detail != "Interrotto" {
		t.Fatalf("Esc: %s %q", s.State, s.Detail)
	}
}

func TestGrantedLongCommand(t *testing.T) {
	// concesso nel terminale, comando lungo: la status line arriva subito, PostToolUse molto dopo
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	r.tails["/t/s.jsonl"] = []byte(plainEnd)
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_use_id": "u1", "tool_input": map[string]any{"command": "npm test"}})
	r.hook("s", map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash", "tool_input": map[string]any{"command": "npm test"}})
	r.wait(8 * time.Second)
	r.status("s", map[string]any{})
	r.wait(2 * time.Second)
	s := r.session("s")
	if s.State != state.Working || *s.Test != "run" || r.store.Snapshot().Today.PermTerminal != 1 {
		t.Fatalf("concesso: %s test=%v", s.State, s.Test)
	}
	if w := r.store.Snapshot().Today.WaitAvgMs; w < 8000 || w > 9500 {
		t.Fatalf("attesa: %d ms", w)
	}
	r.hook("s", map[string]any{"hook_event_name": "PostToolUse", "tool_name": "Bash", "tool_use_id": "u1", "tool_input": map[string]any{"command": "npm test"}})
	if s := r.session("s"); *s.Test != "ok" || r.store.Snapshot().Today.PermTerminal != 1 {
		t.Fatal("PostToolUse dopo il permesso già contato")
	}
}

func TestDenyWithFeedbackContinues(t *testing.T) {
	// «No, e di' a Claude cosa fare»: nessuna interruzione, Claude prova un altro strumento
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Write", "tool_use_id": "u1", "tool_input": map[string]any{"file_path": "/home/utente/progetto-prova/a"}})
	r.hook("s", map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Write", "tool_input": map[string]any{"file_path": "/home/utente/progetto-prova/a"}})
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Read", "tool_use_id": "u2", "tool_input": map[string]any{"file_path": "/home/utente/progetto-prova/b"}})
	s := r.session("s")
	if s.State != state.Working || s.Detail != "Read b" || r.store.Snapshot().Today.PermTerminal != 1 {
		t.Fatalf("%s %q", s.State, s.Detail)
	}
}

func TestNotificationOnlyAndLate(t *testing.T) {
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	// senza hook PermissionRequest (per esempio hook configurati a mano): basta la notifica
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_use_id": "u1", "tool_input": map[string]any{"command": "rm -rf build"}})
	r.hook("s", map[string]any{"hook_event_name": "Notification", "notification_type": "permission_prompt", "message": "Claude needs your permission"})
	if s := r.session("s"); s.State != state.Permission || s.Detail != "Bash: rm -rf build" {
		t.Fatalf("notifica: %s %q", s.State, s.Detail)
	}
	r.hook("s", map[string]any{"hook_event_name": "PostToolUse", "tool_name": "Bash", "tool_use_id": "u1"})
	if r.session("s").State != state.Working {
		t.Fatal("PostToolUse deve chiudere il permesso della notifica")
	}
	// permesso risolto in meno di 6 s, poi notifica in ritardo: ignorata
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Edit", "tool_use_id": "u2", "tool_input": map[string]any{}})
	r.hook("s", map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Edit", "tool_input": map[string]any{}})
	r.wait(time.Second)
	r.hook("s", map[string]any{"hook_event_name": "PostToolUse", "tool_name": "Edit", "tool_use_id": "u2"})
	r.wait(5 * time.Second)
	r.hook("s", map[string]any{"hook_event_name": "Notification", "notification_type": "permission_prompt"})
	if r.session("s").State != state.Working {
		t.Fatal("notifica in ritardo presa per un nuovo permesso")
	}
}

func TestAutoModeDeniedAndSubagentPermission(t *testing.T) {
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	r.hook("s", map[string]any{"hook_event_name": "SubagentStart", "agent_id": "a1", "agent_type": "Explore"})
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "agent_id": "a1", "tool_name": "Bash", "tool_use_id": "u1", "tool_input": map[string]any{"command": "make test"}})
	r.hook("s", map[string]any{"hook_event_name": "PermissionRequest", "agent_id": "a1", "tool_name": "Bash", "tool_input": map[string]any{"command": "make test"}})
	if s := r.session("s"); s.State != state.Permission || s.Detail != "Bash: make test" || *s.Test != "run" {
		t.Fatalf("permesso del subagente: %s %q", s.State, s.Detail)
	}
	// l'agente principale va avanti e la status line arriva: il permesso del subagente resta
	r.status("s", map[string]any{})
	r.wait(2 * time.Second)
	if r.session("s").State != state.Permission {
		t.Fatal("status line dell'agente principale presa per la risposta al subagente")
	}
	r.hook("s", map[string]any{"hook_event_name": "PostToolUseFailure", "agent_id": "a1", "tool_name": "Bash", "tool_use_id": "u1", "error": "Exit code 2\nmake: *** [test] Error 1", "is_interrupt": false})
	if s := r.session("s"); s.State != state.Working || *s.Test != "ko" {
		t.Fatalf("dopo il fallimento: %s", s.State)
	}
	// modalità auto: PermissionDenied non è una risposta dell'utente
	r.hook("s", map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_use_id": "u2", "tool_input": map[string]any{"command": "rm -rf /tmp/build"}})
	r.hook("s", map[string]any{"hook_event_name": "PermissionDenied", "tool_name": "Bash", "tool_use_id": "u2", "reason": "x"})
	if s := r.session("s"); s.State != state.Working || r.store.Snapshot().Today.PermTerminal != 1 {
		t.Fatalf("PermissionDenied: %s, terminale %d", s.State, r.store.Snapshot().Today.PermTerminal)
	}
}

func TestClearAndUnknownEvents(t *testing.T) {
	r := newRig(t, time.Date(2026, 10, 11, 10, 0, 0, 0, time.Local))
	r.hook("s", map[string]any{"hook_event_name": "TaskCreated", "task_id": "1", "task_subject": "A"})
	r.hook("s", map[string]any{"hook_event_name": "Stop", "last_assistant_message": "Fatto."})
	r.hook("s", map[string]any{"hook_event_name": "SessionStart", "source": "clear"})
	if s := r.session("s"); s.State != state.Idle || len(s.Tasks) != 0 || s.Summary != "" {
		t.Fatalf("clear: %+v", *s)
	}
	r.hook("s", map[string]any{"hook_event_name": "EventoFuturo"})
	r.e.Hook([]byte(`non json`))
	r.e.StatusLine([]byte(`{}`))
	if len(r.store.Snapshot().Sessions) != 1 {
		t.Fatal("eventi sconosciuti o non validi hanno creato sessioni")
	}
}

// Sessioni di oggi (riepilogo di `bye`): distinte, anche dopo SessionEnd; a mezzanotte si riparte e
// conta chi ha un evento nel giorno nuovo; si riprendono dopo un riavvio.
func TestSessionsToday(t *testing.T) {
	dir := t.TempDir()
	r := newRig(t, time.Date(2026, 10, 11, 23, 50, 0, 0, time.Local))
	r.e.TodayFile = filepath.Join(dir, "oggi.json")
	if day, n := r.e.Day(); day != "2026-10-11" || n != 0 {
		t.Fatalf("all'avvio: %s %d", day, n)
	}
	r.hook("a", map[string]any{"hook_event_name": "SessionStart"})
	r.hook("b", map[string]any{"hook_event_name": "SessionStart"})
	r.hook("a", map[string]any{"hook_event_name": "UserPromptSubmit", "prompt": "ciao"})
	r.status("c", map[string]any{"session_name": "solo status line"})
	r.hook("b", map[string]any{"hook_event_name": "SessionEnd", "reason": "other"})
	r.wait(11 * time.Second) // SessionEnd toglie b; Oggi salvato
	if _, n := r.e.Day(); n != 3 {
		t.Fatalf("sessioni di oggi %d, attese 3", n)
	}
	r2 := newRig(t, r.now.Add(time.Minute))
	r2.e.TodayFile = r.e.TodayFile
	r2.e.LoadToday()
	if _, n := r2.e.Day(); n != 3 {
		t.Fatalf("dopo il riavvio: %d", n)
	}
	r.wait(10 * time.Minute) // mezzanotte
	if day, n := r.e.Day(); day != "2026-10-12" || n != 0 {
		t.Fatalf("dopo mezzanotte: %s %d", day, n)
	}
	r.hook("a", map[string]any{"hook_event_name": "UserPromptSubmit", "prompt": "di nuovo"})
	if _, n := r.e.Day(); n != 1 {
		t.Fatalf("dopo mezzanotte con un evento: %d", n)
	}
}
