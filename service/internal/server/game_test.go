package server

import (
	"bytes"
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/accessory"
	"calamaio/internal/client"
	"calamaio/internal/config"
	"calamaio/internal/game"
	"calamaio/internal/gamesvc"
)

// Gioco della mascotte end-to-end: hook veri → XP, API della webapp, proposta a Claude, CLI.
// Sempre file temporanei: mai game.json né la libreria veri dell'utente.

type gameDirs struct{ file, acc string }

func newGameDirs(t *testing.T, st *game.State) gameDirs {
	t.Helper()
	d := t.TempDir()
	g := gameDirs{file: filepath.Join(d, "game.json"), acc: filepath.Join(d, "accessories")}
	if st != nil {
		b, _ := json.Marshal(map[string]any{"format": gamesvc.FileFormat, "version": 1, "state": st})
		if err := os.WriteFile(g.file, b, 0o600); err != nil {
			t.Fatal(err)
		}
	}
	return g
}

func startGame(t *testing.T, g gameDirs) (*started, *web) {
	t.Helper()
	s := startOpts(t, false, 0, func(o *Options) {
		o.GameFile, o.AccessoryDir = g.file, g.acc
		o.GameClock = gameNoon // ore 12 di oggi: di notte (00–06) il lavoro non conta
	})
	return s, newWeb(t, s)
}

func (w *web) game() map[string]any {
	w.t.Helper()
	code, m, _ := w.do(http.MethodGet, "/api/game", "", "")
	if code != 200 {
		w.t.Fatalf("GET /api/game: %d %v", code, m)
	}
	return m
}

func specialFile(t *testing.T, id string) []byte {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join("..", "..", "..", "avatars", "accessori", "corona.accessory.json"))
	if err != nil {
		t.Fatal(err)
	}
	var m map[string]any
	_ = json.Unmarshal(raw, &m)
	m["id"], m["name"], m["special"], m["author"] = id, "Corona di stelle", true, "Claude"
	b, _ := json.MarshalIndent(m, "", " ")
	return b
}

// Hook veri → XP; riavvio del servizio e stessi hook rigiocati → nessun XP in più (Review Focus).
func TestGameHooksAndRestart(t *testing.T) {
	g := newGameDirs(t, &game.State{On: true})
	s, w := startGame(t, g)
	sid := "game-s1"
	hooks := []map[string]any{
		{"session_id": sid, "hook_event_name": "SessionStart", "cwd": "/tmp", "source": "startup"},
		{"session_id": sid, "hook_event_name": "UserPromptSubmit", "prompt": "fai i task", "prompt_id": "p1"},
		{"session_id": sid, "hook_event_name": "TaskCreated", "task_id": "1", "task_subject": "Uno"},
		{"session_id": sid, "hook_event_name": "TaskCompleted", "task_id": "1", "task_subject": "Uno", "prompt_id": "p1"},
		{"session_id": sid, "hook_event_name": "PermissionRequest", "tool_name": "Bash", "tool_input": map[string]any{"command": "ls"}},
		{"session_id": sid, "hook_event_name": "Stop", "last_assistant_message": "Fatto il task uno.", "prompt_id": "p1"},
	}
	for _, h := range hooks {
		hook(t, s, h)
	}
	waitFor(t, 2*time.Second, func() bool { return w.game()["xp"].(float64) > 0 })
	st := w.game()
	// bonus del primo lavoro del giorno 5 + task 10 + turno 20; niente per il permesso
	if st["xp"] != 35.0 {
		t.Fatalf("XP %v, attesi 35", st["xp"])
	}
	day := time.Now().Format(time.DateOnly)
	hist := st["history"].([]any)
	if last := hist[len(hist)-1].(map[string]any); last["day"] != day || last["xp"] != 35.0 {
		t.Fatalf("storico: %v", last)
	}

	// riavvio: un altro servizio sullo stesso game.json
	s.cancel()
	err := <-s.done
	s.done <- err // per il Cleanup di startOpts
	s2, w2 := startGame(t, g)
	for _, h := range hooks {
		hook(t, s2, h)
	}
	time.Sleep(200 * time.Millisecond)
	if xp := w2.game()["xp"]; xp != 35.0 {
		t.Fatalf("dopo il riavvio XP %v: eventi contati due volte", xp)
	}
	// archiviare la sessione finita la chiude: +10, una volta sola anche con SessionEnd dopo
	if code, _ := w2.post("/api/sessions/"+sid+"/archive", ""); code != 202 {
		// dopo il riavvio la sessione è ricreata dagli hook rigiocati: è done
		t.Fatalf("archivia: %d", code)
	}
	waitFor(t, 2*time.Second, func() bool { return w2.game()["xp"] == 45.0 })
	hook(t, s2, map[string]any{"session_id": sid, "hook_event_name": "SessionEnd", "reason": "other"})
	time.Sleep(150 * time.Millisecond)
	if xp := w2.game()["xp"]; xp != 45.0 {
		t.Fatalf("SessionEnd dopo l'archiviazione: XP %v", xp)
	}
}

// Gioco spento: niente XP, niente proposta, progressi conservati (Review Focus).
func TestGameOffNoXPNoProposal(t *testing.T) {
	all := append([]string{}, accessory.CatalogOrder...)
	g := newGameDirs(t, &game.State{On: false, XP: 120, Unlocked: all, Wear: "corona", SpecialPending: "task-10"})
	s, w := startGame(t, g)
	hook(t, s, map[string]any{"session_id": "x", "hook_event_name": "TaskCompleted", "task_id": "9"})
	time.Sleep(150 * time.Millisecond)
	if xp := w.game()["xp"]; xp != 120.0 {
		t.Fatalf("XP a gioco spento: %v", xp)
	}
	prompt := map[string]any{"session_id": "x", "hook_event_name": "UserPromptSubmit", "prompt": "ciao"}
	if code, body := hookContext(t, s, prompt); code != 204 {
		t.Fatalf("proposta a gioco spento: %d %s", code, body)
	}
	if code, _ := w.post("/api/game", `{"on":true}`); code != 200 {
		t.Fatal(code)
	}
	st := w.game()
	if st["on"] != true || st["wear"] != "corona" || len(st["unlocked"].([]any)) != 8 {
		t.Fatalf("riacceso: %v", st)
	}
	code, body := hookContext(t, s, prompt)
	if code != 200 || !strings.Contains(body, "AskUserQuestion") || !strings.Contains(body, "calamaio:accessorio") {
		t.Fatalf("proposta: %d %s", code, body)
	}
	if code, _ := hookContext(t, s, prompt); code != 204 {
		t.Fatalf("seconda proposta lo stesso giorno: %d", code)
	}
	// SessionStart per un progetto con avatar: il gioco non c'entra
	in, _ := json.Marshal(prompt)
	var out bytes.Buffer
	client.HookContext(s.cfg, bytes.NewReader(in), &out, func(string) string { return "" })
	if out.Len() != 0 {
		t.Fatalf("hook --context dopo la proposta: %q", out.String())
	}
}

func TestGameProposalThroughCommand(t *testing.T) {
	all := append([]string{}, accessory.CatalogOrder...)
	g := newGameDirs(t, &game.State{On: true, Unlocked: all, SpecialPending: "primo-task"})
	s, _ := startGame(t, g)
	in, _ := json.Marshal(map[string]any{"session_id": "x", "hook_event_name": "UserPromptSubmit", "prompt": "ciao"})
	var out bytes.Buffer
	client.HookContext(s.cfg, bytes.NewReader(in), &out, func(string) string { return "" })
	var o struct {
		H struct {
			Event string `json:"hookEventName"`
			Ctx   string `json:"additionalContext"`
		} `json:"hookSpecificOutput"`
	}
	if err := json.Unmarshal(out.Bytes(), &o); err != nil || o.H.Event != "UserPromptSubmit" || !strings.Contains(o.H.Ctx, "«Primo task»") {
		t.Fatalf("hook --context: %q %v", out.String(), err)
	}
}

func TestGameWebAPIAndCLI(t *testing.T) {
	all := append([]string{}, accessory.CatalogOrder...)
	g := newGameDirs(t, &game.State{On: true, XP: 400, Unlocked: all[:3], SpecialPending: "task-10"})
	s, w := startGame(t, g)
	st := w.game()
	if st["level"] != 3.0 || st["xpLevelStart"] != 346.0 || st["xpNext"] != 763.0 || len(st["accessories"].([]any)) != 8 ||
		len(st["achievements"].([]any)) != 21 || len(st["history"].([]any)) != 30 {
		t.Fatalf("status: %v", st)
	}
	if sp := st["specialPending"].(map[string]any); sp["title"] != "10 task" {
		t.Fatalf("specialPending: %v", sp)
	}
	// wear
	if code, m := w.post("/api/game/wear", `{"id":"corona"}`); code != 409 {
		t.Fatalf("wear bloccato: %d %v", code, m)
	}
	if code, m := w.post("/api/game/wear", `{"id":"boh"}`); code != 404 {
		t.Fatalf("wear sconosciuto: %d %v", code, m)
	}
	if code, m := w.post("/api/game/wear", `{"id":"occhiali"}`); code != 200 || m["wear"] != "occhiali" {
		t.Fatalf("wear: %d %v", code, m)
	}
	if code, _ := w.post("/api/game", `{"acceso":true}`); code != 400 {
		t.Fatal("campo sconosciuto accettato")
	}
	// file dell'accessorio e ancore per l'anteprima
	code, acc, _ := w.do(http.MethodGet, "/api/accessories/occhiali", "", "")
	if code != 200 || acc["anchor"] != "eyes" {
		t.Fatalf("accessorio: %d %v", code, acc)
	}
	code, anc, _ := w.do(http.MethodGet, "/api/avatars/gatto/anchors", "", "")
	if code != 200 || anc["from"] != "ancore/gatto.anchors.json" || len(anc["anchors"].(map[string]any)["done"].([]any)) != 4 {
		t.Fatalf("ancore: %d %v", code, anc)
	}
	if code, _, _ := w.do(http.MethodGet, "/api/avatars/boh/anchors", "", ""); code != 404 {
		t.Fatalf("ancore di un avatar sconosciuto: %d", code)
	}
	frame := anc["anchors"].(map[string]any)["working"].([]any)[0].(map[string]any)
	if _, ok := frame["eyes"].([]any); !ok {
		t.Fatalf("ancora: %v", frame)
	}
	// sicurezza: POST senza JSON o da un altro sito
	if code, _, _ := w.do(http.MethodPost, "/api/game", "text/plain", `{"on":false}`); code != 415 {
		t.Fatalf("text/plain: %d", code)
	}
	if code, _, _ := w.do(http.MethodPost, "/api/game", "application/json", `{"on":false}`, "Origin", "http://evil.example"); code != 403 {
		t.Fatalf("altro sito: %d", code)
	}

	run := func(f func(c config.Config, args []string, o client.GameIO) int, args ...string) (int, string, string) {
		var out, errb bytes.Buffer
		code := f(s.cfg, args, client.GameIO{Out: &out, Err: &errb})
		return code, out.String(), errb.String()
	}
	if code, out, e := run(client.Game, "status"); code != 0 || !strings.Contains(out, "Livello 3 · 400 XP (54/417 verso il livello 4)") ||
		!strings.Contains(out, "Accessorio speciale disponibile (traguardo «10 task»)") || !strings.Contains(out, "Occhiali (occhiali)") {
		t.Fatalf("game status: %d %q %q", code, out, e)
	}
	if code, out, _ := run(client.Accessory, "list"); code != 0 || !regexpLine(out, `^occhiali\s+Occhiali\s+indossato`) || !regexpLine(out, `^corona\s+Corona\s+bloccato`) {
		t.Fatalf("accessory list: %q", out)
	}
	if code, out, _ := run(client.Accessory, "show", "sciarpa"); code != 0 || !strings.Contains(out, "2 fotogrammi") || !strings.Contains(out, "██") {
		t.Fatalf("accessory show: %q", out)
	}
	// add --special: file senza special:true rifiutato; con special sbloccato, indossato, proposta chiusa
	file := filepath.Join(t.TempDir(), "corona-di-stelle.accessory.json")
	plain := bytes.Replace(specialFile(t, "corona-di-stelle"), []byte(`"special": true`), []byte(`"special": false`), 1)
	_ = os.WriteFile(file, plain, 0o644)
	if code, _, e := run(client.Accessory, "add", file, "--special"); code != 1 || !strings.Contains(e, `"special": true`) {
		t.Fatalf("add --special senza special: %d %q", code, e)
	}
	_ = os.WriteFile(file, specialFile(t, "corona-di-stelle"), 0o644)
	if code, out, e := run(client.Accessory, "add", file, "--special"); code != 0 || !strings.Contains(out, "sbloccato e indossato") {
		t.Fatalf("add --special: %d %q %q", code, out, e)
	}
	if _, err := os.Stat(filepath.Join(g.acc, "corona-di-stelle.accessory.json")); err != nil {
		t.Fatal("add non è passato dal servizio")
	}
	st = w.game()
	if st["wear"] != "corona-di-stelle" || st["specialPending"] != nil {
		t.Fatalf("dopo add --special: wear %v pending %v", st["wear"], st["specialPending"])
	}
	if code, _, e := run(client.Accessory, "add", file, "--special"); code != 1 || !strings.Contains(e, "--replace") {
		t.Fatalf("add doppio: %d %q", code, e)
	}
	if code, out, _ := run(client.Accessory, "wear", "none"); code != 0 || !strings.Contains(out, "non indossa più") || w.game()["wear"] != "" {
		t.Fatalf("wear none: %q", out)
	}
	if code, _, e := run(client.Game, "special", "later"); code != 1 || !strings.Contains(e, "nessun accessorio speciale") {
		t.Fatalf("special senza proposta: %d %q", code, e)
	}
	if code, out, _ := run(client.Game, "off"); code != 0 || !strings.Contains(out, "spento") || w.game()["on"] != false {
		t.Fatalf("game off: %q", out)
	}

	// servizio spento: gli stessi comandi lavorano sui file
	dead := s.cfg
	dead.APIPort = freePort(t)
	local := gamesvc.Open(g.file, accessory.NewLibrary(g.acc), nil, nil)
	var out, errb bytes.Buffer
	if code := client.Game(dead, []string{"on"}, client.GameIO{Out: &out, Err: &errb, Local: local}); code != 0 || !strings.Contains(errb.String(), "servizio Calamaio spento") {
		t.Fatalf("game on a servizio spento: %d %q", code, errb.String())
	}
	if !gamesvc.Open(g.file, accessory.NewLibrary(g.acc), nil, nil).State().On {
		t.Fatal("game on a servizio spento non salvato")
	}
}

func waitFor(t *testing.T, d time.Duration, ok func() bool) {
	t.Helper()
	deadline := time.Now().Add(d)
	for !ok() {
		if time.Now().After(deadline) {
			t.Fatal("condizione non raggiunta entro", d)
		}
		time.Sleep(20 * time.Millisecond)
	}
}

// gameNoon: mezzogiorno di oggi, ora locale (i test del gioco non dipendono dall'ora in cui girano).
func gameNoon() time.Time {
	n := time.Now()
	return time.Date(n.Year(), n.Month(), n.Day(), 12, 0, 0, 0, time.Local)
}
