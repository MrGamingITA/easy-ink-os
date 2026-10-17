package sessions

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"
	"time"

	"calamaio/internal/state"
)

// Stato atteso della sessione dell'evento dopo ogni riga di cattura-2026-10-11.jsonl
// (sessione A: righe 0–65, sessione B: 66–100). Vedi fixtures/LEGGIMI.md per la storia.
var wantStates = strings.Fields(`
idle idle working working working working working working working working
permission permission permission working working working permission permission permission working
working working working working working working permission permission permission permission
permission permission working working permission permission working working done done
done working input input input working working working working permission
permission permission working working permission permission working done done done
compact compact compact done done idle idle idle working working
working working working working working working working working working working
working working working working working working working working working working
working working working working working working done done done done
idle`)

func parseTS(t *testing.T, s string) time.Time {
	t.Helper()
	ts, err := time.Parse(time.RFC3339Nano, s)
	if err != nil {
		t.Fatal(err)
	}
	return ts
}

func sidOf(p json.RawMessage) string {
	var x struct {
		SessionID string `json:"session_id"`
	}
	_ = json.Unmarshal(p, &x)
	return x.SessionID
}

func TestReplayCapture(t *testing.T) {
	recs := loadFixture(t, "cattura-2026-10-11.jsonl")
	if len(recs) != 101 || len(wantStates) != len(recs) {
		t.Fatalf("righe %d, attese %d", len(recs), len(wantStates))
	}
	r := newRig(t, parseTS(t, recs[0].TS).Add(-time.Second))
	r.denyTail(t)
	for i, x := range recs {
		if i == 19 {
			// prima del prompt successivo: il diniego nel terminale è stato visto nel transcript
			r.at(parseTS(t, x.TS))
			s := r.session(sidA)
			if s.State != state.Idle || s.Detail != "Negato: Write style.css" || s.AttnAt != nil {
				t.Fatalf("dopo il diniego: %s %q attn=%v", s.State, s.Detail, s.AttnAt)
			}
		}
		r.play(x)
		s := r.session(sidOf(x.Payload))
		if s == nil {
			t.Fatalf("riga %d %s: sessione assente", i, x.Event)
		}
		if s.State != wantStates[i] {
			t.Fatalf("riga %d %s: stato %s, atteso %s (detail %q)", i, x.Event, s.State, wantStates[i], s.Detail)
		}
		if (s.State == state.Permission || s.State == state.Input) != (s.AttnAt != nil) {
			t.Fatalf("riga %d: attnAt %v con stato %s", i, s.AttnAt, s.State)
		}
		check := func(cond bool, what string) {
			t.Helper()
			if !cond {
				t.Fatalf("riga %d %s: %s; sessione %+v", i, x.Event, what, *s)
			}
		}
		switch i {
		case 1:
			check(s.Name == "progetto-prova" && s.Ctx == nil, "nome dalla cartella, ctx null")
		case 5:
			check(s.Name == "Pagina index.html e style.css" && *s.Ctx == 5, "session_name e ctx dalla status line")
		case 9:
			check(s.Detail == "Write index.html", "percorso relativo a cwd")
		case 10, 16:
			check(s.State == state.Permission, "PermissionRequest → permission")
		case 21:
			check(s.Subs == 1, "SubagentStart → subs 1")
		case 27, 28:
			check(s.State == state.Permission && s.Detail == "Write test.js", "i passi del subagente non toccano il permesso")
		case 29:
			check(s.Subs == 0, "SubagentStop → subs 0")
		case 33:
			check(s.Detail == "Bash: node test.js" && *s.Test == "run", "test in corso")
		case 36:
			check(*s.Test == "ko", "PostToolUseFailure con Exit code 1 → ko")
		case 37:
			check(s.Detail == "Risultato del subagente", "task-notification")
		case 38:
			check(s.Summary == "Ho fatto tutte e due le cose.", "riepilogo = prima frase")
		case 40:
			check(s.Subs == 0, "SubagentStop senza SubagentStart ignorato")
		case 42, 43, 44:
			check(s.State == state.Input && s.Detail == "Domanda: Che tema vuoi per la pagina?", "AskUserQuestion → input, mai permission")
		case 60:
			check(s.Detail == "Compatta il contesto", "PreCompact")
		case 62:
			check(s.State == state.Compact, "SessionStart source=compact non azzera")
		case 63:
			check(s.Summary == "Ho completato i tre task, con il tema scuro che hai scelto." && s.Detail == "Edit index.html", "PostCompact torna allo stato precedente")
		case 64:
			check(*s.Ctx == 0, "ctx dopo la compattazione")
		case 65:
			check(s.Detail == "Sessione chiusa", "SessionEnd")
		case 77:
			check(len(s.Tasks) == 2 && s.Tasks[1].Title == "Conta le righe di index.html" && s.Tasks[1].Status == 0, "TaskCreated")
		case 80:
			check(s.Test == nil, "ls non è un test")
		case 93:
			check(s.Tasks[0].Status == 2 && s.Tasks[1].Status == 2, "TaskCompleted")
		case 96:
			check(s.Summary == "Ho creato le due task e le ho segnate entrambe come completate.", "riepilogo B")
		case 99:
			check(s.Summary != "" && s.State == state.Done, "idle_prompt lascia la sessione FINITA con il riepilogo")
		}
		if i == 66 && r.session(sidA) != nil {
			t.Fatal("la sessione A doveva sparire 5 s dopo SessionEnd")
		}
	}
	r.at(r.now.Add(6 * time.Second))
	snap := r.store.Snapshot()
	if len(snap.Sessions) != 0 {
		t.Fatalf("sessioni rimaste: %d", len(snap.Sessions))
	}
	td := snap.Today
	// 6 permessi: 5 concessi e 1 negato, tutti nel terminale. AskUserQuestion non conta.
	if td.PermTerminal != 6 || td.PermDisplay != 0 || td.Tasks != 2 {
		t.Fatalf("oggi: %+v", td)
	}
	// costo = totale finale di A (0.7621351) + B (0.2644761): la prima status line vale 0.
	if fmt.Sprintf("%.4f", td.Cost) != "1.0266" || td.LinesAdd != 29 || td.LinesDel != 0 {
		t.Fatalf("oggi: %+v", td)
	}
	if td.WaitAvgMs <= 0 {
		t.Fatalf("attesa media: %d", td.WaitAvgMs)
	}
	l := snap.Limits
	if l == nil || l.H5 == nil || l.D7 == nil || *l.H5Reset != 1791739200000 || *l.D7Reset != 1792062000000 {
		t.Fatalf("limiti: %+v", l)
	}
}

func TestStopFailureSynthetic(t *testing.T) {
	recs := loadFixture(t, "stopfailure-sintetico.jsonl")
	r := newRig(t, parseTS(t, recs[0].TS).Add(-time.Second))
	want := []string{state.Idle, state.Working, state.Error, state.Working}
	for i, x := range recs {
		if !x.Synthetic {
			t.Fatalf("riga %d non marcata synthetic", i)
		}
		r.play(x)
		s := r.session(sidOf(x.Payload))
		if s.State != want[i] {
			t.Fatalf("riga %d: %s, atteso %s", i, s.State, want[i])
		}
		if i == 2 && (s.Error != "rate_limit" || s.Detail != "StopFailure: rate_limit") {
			t.Fatalf("errore: %+v", *s)
		}
		if i == 3 && s.Error != "" {
			t.Fatal("error deve sparire fuori dallo stato error")
		}
	}
	// il messaggio per i display rispetta lo schema: error solo con state=error
	m := r.store.Message()
	b, _ := json.Marshal(m)
	if strings.Contains(string(b), `"error"`) {
		t.Fatalf("error fuori posto: %s", b)
	}
}

// Archivia (richiesta dell'utente): Stop → done → archivia → assente dallo state →
// UserPromptSubmit → di nuovo presente; rifiutato su working; idle_prompt non la riaccende.
func TestArchiveWithFixtures(t *testing.T) {
	recs := loadFixture(t, "cattura-2026-10-11.jsonl")
	r := newRig(t, parseTS(t, recs[0].TS).Add(-time.Second))
	r.denyTail(t)
	visible := func(id string) bool {
		for _, s := range r.store.Message().Sessions {
			if s.ID == id {
				return true
			}
		}
		return false
	}
	for i := 0; i <= 37; i++ { // fino al prompt di sistema (working)
		r.play(recs[i])
	}
	if err := r.store.Archive(sidA); err != state.ErrNotArchivable {
		t.Fatalf("archiviare una sessione working: %v", err)
	}
	r.play(recs[38]) // Stop → done
	if err := r.store.Archive(sidA); err != nil {
		t.Fatal(err)
	}
	if visible(sidA) {
		t.Fatal("archiviata ma ancora nello state")
	}
	v := r.store.View()
	if len(v.Sessions) != 1 || !v.Sessions[0].Archived {
		t.Fatalf("la webapp deve vederla con archived=true: %+v", v.Sessions)
	}
	if r.store.Snapshot().Log[0].Text != "archiviata" {
		t.Fatal("manca «archiviata» nel log")
	}
	r.play(recs[39]) // status line
	r.play(recs[40]) // SubagentStop orfano
	if visible(sidA) {
		t.Fatal("status line e SubagentStop non devono riportarla in vista")
	}
	r.play(recs[41]) // UserPromptSubmit
	if !visible(sidA) || r.session(sidA).State != state.Working {
		t.Fatal("dopo UserPromptSubmit deve tornare visibile e working")
	}
	if r.store.IsArchived(sidA) {
		t.Fatal("ancora archiviata")
	}
	found := false
	for _, l := range r.store.Snapshot().Log {
		if l.Text == "riprende" {
			found = true
		}
	}
	if !found {
		t.Fatal("manca «riprende» nel log")
	}

	// sessione B: Stop, archivia, poi idle_prompt (60 s dopo): resta archiviata e ferma
	for i := 66; i <= 98; i++ {
		r.play(recs[i])
	}
	if err := r.store.Archive(sidB); err != nil {
		t.Fatal(err)
	}
	r.play(recs[99])
	if visible(sidB) || r.session(sidB).State != state.Done {
		t.Fatalf("idle_prompt ha riacceso l'archiviata: %s", r.session(sidB).State)
	}
	if err := r.store.Archive("nessuna"); err != state.ErrNoSession {
		t.Fatalf("sessione sconosciuta: %v", err)
	}
	if err := r.store.Unarchive(sidB); err != nil || !visible(sidB) {
		t.Fatal("unarchive")
	}
}

// Lo state completo con 6 sessioni piene resta sotto 12 KB (il firmware accetta frame fino a 15 KB).
func TestStateSizeSixFullSessions(t *testing.T) {
	// testo normale (accentato): bastano i limiti per campo, senza FitSize
	plain := fullSessions(t, strings.Repeat("Aggiorna la pagina è ", 20))
	raw, _ := json.Marshal(state.Message{T: "state", V: 1, Time: 1, Snapshot: plain.Normalize()})
	if len(raw) > state.MaxStateBytes {
		t.Fatalf("con i soli limiti per campo: %d byte", len(raw))
	}
	if len(plain.Log) != state.MaxLog {
		t.Fatalf("log: %d voci", len(plain.Log))
	}
	t.Logf("6 sessioni piene, testo normale, senza FitSize: %d byte", len(raw))
	// caratteri che JSON scrive con \uXXXX (6 byte l'uno): interviene FitSize
	fullSessions(t, strings.Repeat("<&>\"é", 60))
}

func fullSessions(t *testing.T, long string) state.Snapshot {
	now := time.Date(2026, 10, 11, 12, 0, 0, 0, time.Local)
	r := newRig(t, now)
	for k := 0; k < 8; k++ {
		sid := fmt.Sprintf("%08d-aaaa-4bbb-8ccc-%012d", k, k)
		hook := func(m map[string]any) {
			m["session_id"] = sid
			m["cwd"] = "/home/utente/" + long
			m["transcript_path"] = "/x/" + sid + ".jsonl"
			b, _ := json.Marshal(m)
			r.e.Hook(b)
		}
		hook(map[string]any{"hook_event_name": "SessionStart", "source": "startup"})
		hook(map[string]any{"hook_event_name": "UserPromptSubmit", "prompt": long})
		for j := 0; j < 20; j++ {
			hook(map[string]any{"hook_event_name": "TaskCreated", "task_id": fmt.Sprint(j), "task_subject": long})
		}
		hook(map[string]any{"hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_use_id": "u", "tool_input": map[string]any{"command": long}})
		hook(map[string]any{"hook_event_name": "PermissionRequest", "tool_name": "Bash", "tool_input": map[string]any{"command": long}})
		hook(map[string]any{"hook_event_name": "Stop", "last_assistant_message": long})
		sl, _ := json.Marshal(map[string]any{"session_id": sid, "session_name": long,
			"context_window": map[string]any{"used_percentage": 99},
			"rate_limits": map[string]any{"five_hour": map[string]any{"used_percentage": 50, "resets_at": 1791739200},
				"seven_day": map[string]any{"used_percentage": 60, "resets_at": 1792062000}}})
		r.e.StatusLine(sl)
	}
	m := r.store.Message()
	b, _ := json.Marshal(m)
	if len(m.Sessions) != 6 {
		t.Fatalf("sessioni: %d", len(m.Sessions))
	}
	if len(b) > state.MaxStateBytes {
		t.Fatalf("state di %d byte, massimo %d", len(b), state.MaxStateBytes)
	}
	for _, s := range m.Sessions {
		if len(s.Tasks) != state.MaxTasks || len(s.Name) > state.MaxName || len(s.Summary) > state.MaxSummary || len(s.Detail) > state.MaxDetail {
			t.Fatalf("sessione oltre i limiti: %d task, %d/%d/%d byte", len(s.Tasks), len(s.Name), len(s.Summary), len(s.Detail))
		}
		for _, tk := range s.Tasks {
			if len(tk.Title) > state.MaxTaskText {
				t.Fatalf("titolo di %d byte", len(tk.Title))
			}
		}
	}
	t.Logf("6 sessioni piene: %d byte dopo FitSize", len(b))
	snap := r.store.Snapshot()
	snap.Sessions = m.Sessions
	return snap
}
