package server

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"calamaio/internal/avatar"
	"calamaio/internal/client"
	"calamaio/internal/config"
)

// Avatar end-to-end: API della webapp, hook SessionStart, display, CLI. Libreria dell'utente in una
// cartella temporanea, assegnazioni in memoria: mai i file veri.

func startAvatars(t *testing.T) (*started, *web, string) {
	t.Helper()
	dir := filepath.Join(t.TempDir(), "avatars")
	s := startOpts(t, false, 0, func(o *Options) {
		o.AvatarDir = dir
		o.AvatarPoll = 50 * time.Millisecond
	})
	return s, newWeb(t, s), dir
}

func gattoBlu(t *testing.T, name string) []byte {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join("..", "..", "..", "avatars", "gatto.avatar.json"))
	if err != nil {
		t.Fatal(err)
	}
	var m map[string]any
	_ = json.Unmarshal(raw, &m)
	m["id"], m["name"] = "gatto-blu", name
	b, _ := json.MarshalIndent(m, "", " ")
	return b
}

func (w *web) postRaw(path string, body []byte) (int, map[string]any) {
	w.t.Helper()
	code, m, _ := w.do(http.MethodPost, path, "application/json", string(body),
		"Origin", "http://"+w.host, "Sec-Fetch-Site", "same-origin")
	return code, m
}

func ids(list []any, key string) string {
	var out []string
	for _, x := range list {
		if m, ok := x.(map[string]any); ok {
			out = append(out, fmt.Sprint(m[key]))
		} else {
			out = append(out, fmt.Sprint(x))
		}
	}
	return strings.Join(out, ",")
}

func TestAvatarsWebAPI(t *testing.T) {
	s, w, dir := startAvatars(t)

	code, l, _ := w.do(http.MethodGet, "/api/avatars", "", "")
	if code != 200 || ids(l["avatars"].([]any), "id") != "goccia,calamaro,demone,gatto" || ids(l["free"].([]any), "") != "calamaro,demone,gatto" || l["default"] != "goccia" {
		t.Fatalf("elenco: %d %v", code, l)
	}
	st := w.status()
	av0 := st["avatars"].(map[string]any)
	if av0["default"] != "goccia" || av0["rev"] == "" {
		t.Fatalf("status.avatars: %v", av0)
	}

	// caricamento
	if c, m := w.postRaw("/api/avatars", gattoBlu(t, "Gatto blu")); c != 201 || m["avatar"].(map[string]any)["id"] != "gatto-blu" {
		t.Fatalf("upload: %d %v", c, m)
	}
	if _, err := os.Stat(filepath.Join(dir, "gatto-blu.avatar.json")); err != nil {
		t.Fatal(err)
	}
	if c, m := w.postRaw("/api/avatars", gattoBlu(t, "Gatto blu 2")); c != 409 || !strings.Contains(m["error"].(string), "replace") {
		t.Fatalf("doppione: %d %v", c, m)
	}
	if c, m := w.postRaw("/api/avatars?replace=1", gattoBlu(t, "Gatto blu 2")); c != 201 || m["replaced"] != true {
		t.Fatalf("replace: %d %v", c, m)
	}
	gatto, _ := os.ReadFile(filepath.Join("..", "..", "..", "avatars", "gatto.avatar.json"))
	if c, m := w.postRaw("/api/avatars?replace=1", gatto); c != 409 || !strings.Contains(m["error"].(string), "predefinit") {
		t.Fatalf("predefinito: %d %v", c, m)
	}
	if c, m := w.postRaw("/api/avatars", []byte(`{"format":"calamaio-avatar"}`)); c != 400 || len(m["errors"].([]any)) == 0 {
		t.Fatalf("non valido: %d %v", c, m)
	}
	if c, _ := w.postRaw("/api/avatars", bytes.Repeat([]byte(" "), 80<<10)); c != 413 {
		t.Fatalf("troppo grande: %d", c)
	}
	if st2 := w.status(); st2["avatars"].(map[string]any)["rev"] == av0["rev"] {
		t.Fatal("rev non cambiata dopo il caricamento")
	}

	// file dell'avatar
	code, m, hdr := w.do(http.MethodGet, "/api/avatars/gatto-blu", "", "")
	if code != 200 || m["name"] != "Gatto blu 2" || hdr.Get("X-Calamaio-Crc") == "" {
		t.Fatalf("file: %d %v", code, hdr)
	}
	if code, _, _ := w.do(http.MethodGet, "/api/avatars/gatto-blu", "", "", "If-None-Match", hdr.Get("ETag")); code != 304 {
		t.Fatalf("ETag: %d", code)
	}
	if code, _, _ := w.do(http.MethodGet, "/api/avatars/nessuno", "", ""); code != 404 {
		t.Fatalf("sconosciuto: %d", code)
	}

	// predefinito
	if c, m := w.post("/api/avatars/default", `{"id":"demone"}`); c != 200 || m["default"] != "demone" {
		t.Fatalf("default: %d %v", c, m)
	}
	if c, _ := w.post("/api/avatars/default", `{"id":"boh"}`); c != 400 {
		t.Fatalf("default sconosciuto: %d", c)
	}
	_, l, _ = w.do(http.MethodGet, "/api/avatars", "", "")
	if l["default"] != "demone" || strings.Contains(ids(l["free"].([]any), ""), "demone") || !strings.Contains(ids(l["free"].([]any), ""), "goccia") {
		t.Fatalf("liberi col demone predefinito: %v", l["free"])
	}

	// sessione e progetto
	repo := t.TempDir()
	_ = os.Mkdir(filepath.Join(repo, ".git"), 0o755)
	sub := filepath.Join(repo, "src")
	_ = os.Mkdir(sub, 0o755)
	hook(t, s, map[string]any{"session_id": "s-uno", "hook_event_name": "SessionStart", "cwd": sub, "source": "startup"})
	hook(t, s, map[string]any{"session_id": "s-due", "hook_event_name": "SessionStart", "cwd": t.TempDir(), "source": "startup"})
	if c, m := w.post("/api/projects/avatar", fmt.Sprintf(`{"dir":%q,"id":"gatto"}`, sub)); c != 200 || m["dir"] != avatar.NormDir(repo) {
		t.Fatalf("progetto: %d %v", c, m)
	}
	waitStatus(t, w, func(st map[string]any) bool {
		for _, x := range st["state"].(map[string]any)["sessions"].([]any) {
			ss := x.(map[string]any)
			if ss["id"] == "s-uno" {
				return ss["avatar"] == "gatto" && ss["avatarFrom"] == "progetto" && ss["project"] == avatar.NormDir(repo)
			}
		}
		return false
	})
	if c, m := w.post("/api/sessions/s-due/avatar", `{"id":"calamaro"}`); c != 200 || m["avatar"] != "calamaro" || m["known"] != true {
		t.Fatalf("sessione: %d %v", c, m)
	}
	// in uso: rispetto a s-uno il calamaro è in uso da s-due; rispetto a s-due no
	_, l, _ = w.do(http.MethodGet, "/api/avatars?session=s-uno", "", "")
	if strings.Contains(ids(l["free"].([]any), ""), "calamaro") {
		t.Fatalf("calamaro libero per s-uno: %v", l["free"])
	}
	_, l, _ = w.do(http.MethodGet, "/api/avatars?session=s-due", "", "")
	if !strings.Contains(ids(l["free"].([]any), ""), "calamaro") || l["current"] != "s-due" {
		t.Fatalf("calamaro in uso per s-due: %v", l)
	}
	if c, m := w.post("/api/sessions/s-due/avatar", `{"id":null}`); c != 200 || m["avatar"] != "demone" || m["from"] != "predefinito" {
		t.Fatalf("unassign: %d %v", c, m)
	}
	if c, _ := w.post("/api/projects/avatar", `{"dir":"relativa","id":"gatto"}`); c != 400 {
		t.Fatalf("dir relativa: %d", c)
	}
	if c, _ := w.post("/api/sessions/s-due/avatar", `{"id":"gatto","extra":1}`); c != 400 {
		t.Fatalf("campo in più: %d", c)
	}

	// guardie (come le altre POST)
	for name, hdrs := range map[string][]string{
		"altro sito": {"Origin", "http://evil.example", "Sec-Fetch-Site", "cross-site"},
		"host":       {"Host", "evil.example:8765"},
	} {
		code, _, _ := w.do(http.MethodPost, "/api/avatars/default", "application/json", `{"id":"gatto"}`, hdrs...)
		if code != 403 {
			t.Errorf("%s: %d", name, code)
		}
	}
	if code, _, _ := w.do(http.MethodPost, "/api/projects/avatar", "text/plain", `{"dir":"/x","id":"gatto"}`, "Origin", "http://"+w.host); code != 415 {
		t.Errorf("text/plain: %d", code)
	}
	if code, _, hdr := w.do(http.MethodGet, "/api/avatars", "", ""); code != 200 || !strings.Contains(hdr.Get("Content-Security-Policy"), "default-src 'self'") {
		t.Errorf("CSP: %v", hdr)
	}
}

func hook(t *testing.T, s *started, p map[string]any) {
	t.Helper()
	b, _ := json.Marshal(p)
	resp, err := http.Post(fmt.Sprintf("http://127.0.0.1:%d/hook", s.cfg.APIPort), "application/json", bytes.NewReader(b))
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
}

func hookContext(t *testing.T, s *started, p map[string]any, hdr ...string) (int, string) {
	t.Helper()
	b, _ := json.Marshal(p)
	req, _ := http.NewRequest(http.MethodPost, fmt.Sprintf("http://127.0.0.1:%d/hook/context", s.cfg.APIPort), bytes.NewReader(b))
	req.Header.Set("Content-Type", "application/json")
	for i := 0; i+1 < len(hdr); i += 2 {
		req.Header.Set(hdr[i], hdr[i+1])
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	out, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, string(out)
}

// SessionStart: contesto per Claude solo se il progetto non ha un avatar; il comando `hook --context`
// stampa la forma giusta per Claude Code e annota la sessione in CLAUDE_ENV_FILE.
func TestAvatarSessionStartContext(t *testing.T) {
	s, w, _ := startAvatars(t)
	proj := t.TempDir()
	start := map[string]any{"session_id": "s-ctx", "hook_event_name": "SessionStart", "cwd": proj, "source": "startup"}
	code, body := hookContext(t, s, start)
	if code != 200 || !strings.Contains(body, "questo progetto non ha un avatar; usa la skill avatar") || !strings.Contains(body, avatar.NormDir(proj)) {
		t.Fatalf("senza avatar: %d %s", code, body)
	}
	for _, src := range []string{"compact", "clear"} {
		p := map[string]any{"session_id": "s-ctx", "hook_event_name": "SessionStart", "cwd": proj, "source": src}
		if code, _ := hookContext(t, s, p); code != 204 {
			t.Errorf("source %s: %d", src, code)
		}
	}
	if code, _ := hookContext(t, s, start, "Origin", "http://127.0.0.1"); code != 403 {
		t.Errorf("da un browser: %d", code)
	}
	if code, _ := hookContext(t, s, start, "Sec-Fetch-Mode", "cors"); code != 403 {
		t.Errorf("Sec-Fetch: %d", code)
	}

	// il comando vero, come lo chiama il plugin
	envFile := filepath.Join(t.TempDir(), "env")
	getenv := func(k string) string {
		if k == "CLAUDE_ENV_FILE" {
			return envFile
		}
		return ""
	}
	in, _ := json.Marshal(start)
	var out bytes.Buffer
	client.HookContext(s.cfg, bytes.NewReader(in), &out, getenv)
	var o struct {
		H struct {
			Event string `json:"hookEventName"`
			Ctx   string `json:"additionalContext"`
		} `json:"hookSpecificOutput"`
	}
	if err := json.Unmarshal(out.Bytes(), &o); err != nil || o.H.Event != "SessionStart" || !strings.HasPrefix(o.H.Ctx, "Calamaio: questo progetto non ha un avatar") {
		t.Fatalf("hook --context: %q %v", out.String(), err)
	}
	if env, _ := os.ReadFile(envFile); string(env) != "export CALAMAIO_SESSION_ID=s-ctx\n" {
		t.Fatalf("CLAUDE_ENV_FILE: %q", env)
	}

	// con l'avatar del progetto: niente contesto
	if c, _ := w.post("/api/projects/avatar", fmt.Sprintf(`{"dir":%q,"id":"demone"}`, proj)); c != 200 {
		t.Fatal(c)
	}
	out.Reset()
	client.HookContext(s.cfg, bytes.NewReader(in), &out, getenv)
	if out.Len() != 0 {
		t.Fatalf("con avatar: %q", out.String())
	}

	// servizio spento: nessun output, subito
	dead := s.cfg
	dead.APIPort = freePort(t)
	t0 := time.Now()
	out.Reset()
	client.HookContext(dead, bytes.NewReader(in), &out, func(string) string { return "" })
	if out.Len() != 0 || time.Since(t0) > 1500*time.Millisecond {
		t.Fatalf("servizio spento: %q dopo %s", out.String(), time.Since(t0))
	}

	// SessionEnd toglie l'assegnazione per sessione
	if c, _ := w.post("/api/sessions/s-ctx/avatar", `{"id":"gatto"}`); c != 200 {
		t.Fatal(c)
	}
	hook(t, s, map[string]any{"session_id": "s-ctx", "hook_event_name": "SessionEnd", "cwd": proj, "reason": "other"})
	if a := s.run.Avatars.Assignments(); a.Sessions["s-ctx"] != "" {
		t.Fatalf("assegnazione rimasta dopo SessionEnd: %+v", a)
	}
}

func freePort(t *testing.T) int {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	p := ln.Addr().(*net.TCPAddr).Port
	ln.Close()
	return p
}

// Il display riceve l'avatar nuovo appena lo si assegna, e i binari con avatar-need.
func TestAvatarReachesDisplay(t *testing.T) {
	s, w, _ := startAvatars(t)
	proj := t.TempDir()
	hook(t, s, map[string]any{"session_id": "s-d", "hook_event_name": "SessionStart", "cwd": proj, "source": "startup"})
	ws, ch := dialHello(t, s.cfg.WSPort)
	d := &fakeDisplay{t: t, ws: ws, ch: ch}
	wel := d.expect("welcome")
	if wel["defaultAvatar"] != "goccia" {
		t.Fatalf("welcome: %v", wel)
	}
	if c, _ := w.post("/api/sessions/s-d/avatar", `{"id":"calamaro"}`); c != 200 {
		t.Fatal(c)
	}
	deadline := time.After(3 * time.Second)
	for got := false; !got; {
		select {
		case m := <-ch:
			if m["t"] != "state" {
				continue
			}
			for _, x := range m["sessions"].([]any) {
				ss := x.(map[string]any)
				if ss["id"] == "s-d" && ss["avatar"] == "calamaro" {
					got = true
				}
				if _, leak := ss["cwd"]; leak {
					t.Fatal("cwd al display")
				}
			}
		case <-deadline:
			t.Fatal("state con il calamaro non arrivato")
		}
	}
	d.send(`{"t":"avatar-need","v":1,"ids":["calamaro"]}`)
	a := d.expect("avatar")
	if a["id"] != "calamaro" || len(a["data"].(string)) != 8496 {
		t.Fatalf("avatar: %v", a["id"])
	}
	// cambio del predefinito: arriva con lo state
	if c, _ := w.post("/api/avatars/default", `{"id":"gatto"}`); c != 200 {
		t.Fatal(c)
	}
	deadline = time.After(3 * time.Second)
	for {
		select {
		case m := <-ch:
			if m["t"] == "state" && m["defaultAvatar"] == "gatto" {
				return
			}
		case <-deadline:
			t.Fatal("predefinito non arrivato")
		}
	}
}

// Un file copiato a mano nella libreria arriva ai display senza riavviare (controllo periodico).
func TestAvatarLibraryWatch(t *testing.T) {
	s, _, dir := startAvatars(t)
	_ = os.MkdirAll(dir, 0o700)
	ws, ch := dialHello(t, s.cfg.WSPort)
	d := &fakeDisplay{t: t, ws: ws, ch: ch}
	d.expect("welcome")
	if err := os.WriteFile(filepath.Join(dir, "gatto-blu.avatar.json"), gattoBlu(t, "Blu"), 0o644); err != nil {
		t.Fatal(err)
	}
	deadline := time.After(4 * time.Second)
	for s.run.Avatars.Lib.Get("gatto-blu") == nil {
		select {
		case <-deadline:
			t.Fatal("file non visto")
		case <-time.After(50 * time.Millisecond):
		}
	}
	d.send(`{"t":"avatar-need","v":1,"ids":["gatto-blu"]}`)
	if a := d.expect("avatar"); a["id"] != "gatto-blu" {
		t.Fatal(a)
	}
}

// CLI `calamaio avatar …` contro il servizio vero, e col servizio spento (libreria e config locali).
func TestAvatarCLI(t *testing.T) {
	s, _, dir := startAvatars(t)
	repo := t.TempDir()
	_ = os.Mkdir(filepath.Join(repo, ".git"), 0o755)
	hook(t, s, map[string]any{"session_id": "s-cli", "hook_event_name": "SessionStart", "cwd": repo, "source": "startup"})
	run := func(c config.Config, local *avatar.Manager, args ...string) (int, string, string) {
		var out, errb bytes.Buffer
		code := client.Avatar(c, args, client.AvatarIO{Out: &out, Err: &errb, Local: local,
			Getwd: func() (string, error) { return filepath.Join(repo), nil },
			Getenv: func(k string) string {
				if k == "CALAMAIO_SESSION_ID" {
					return "s-cli"
				}
				return ""
			}})
		return code, out.String(), errb.String()
	}
	file := filepath.Join(t.TempDir(), "mio.json")
	_ = os.WriteFile(file, gattoBlu(t, "Gatto blu"), 0o644)

	if code, out, e := run(s.cfg, nil, "add", file); code != 0 || !strings.Contains(out, "gatto-blu «Gatto blu» aggiunto") {
		t.Fatalf("add: %d %q %q", code, out, e)
	}
	if _, err := os.Stat(filepath.Join(dir, "gatto-blu.avatar.json")); err != nil {
		t.Fatal("add non è passato dal servizio")
	}
	if code, _, e := run(s.cfg, nil, "add", file); code != 1 || !strings.Contains(e, "--replace") {
		t.Fatalf("add doppio: %d %q", code, e)
	}
	if code, out, _ := run(s.cfg, nil, "add", file, "--replace"); code != 0 || !strings.Contains(out, "sostituito") {
		t.Fatalf("add --replace: %d %q", code, out)
	}
	if code, out, _ := run(s.cfg, nil, "assign", "demone"); code != 0 || !strings.Contains(out, "demone assegnato al progetto "+avatar.NormDir(repo)) {
		t.Fatalf("assign: %d %q", code, out)
	}
	code, out, _ := run(s.cfg, nil, "list")
	if code != 0 || !strings.Contains(out, "goccia") || !strings.Contains(out, "(predefinito)") || !strings.Contains(out, "[utente]") {
		t.Fatalf("list: %q", out)
	}
	// la sessione corrente (CALAMAIO_SESSION_ID) non tiene occupato il suo demone
	if !regexpLine(out, `^demone\s+Demone\s+libero`) {
		t.Fatalf("list, demone della sessione corrente: %q", out)
	}
	code, out, _ = run(s.cfg, nil, "list", "--free", "--json")
	var l struct {
		Avatars []struct{ ID, State string }
		Free    []string
	}
	if code != 0 || json.Unmarshal([]byte(out), &l) != nil || strings.Contains(strings.Join(l.Free, ","), "goccia") || len(l.Avatars) != len(l.Free) {
		t.Fatalf("list --free --json: %q", out)
	}
	code, out, _ = run(s.cfg, nil, "list", "--session", "altra")
	if !regexpLine(out, `^demone\s+Demone\s+in uso da `) {
		t.Fatalf("list --session altra: %q", out)
	}
	if code, out, _ := run(s.cfg, nil, "assign", "calamaro", "--session", "s-cli"); code != 0 || !strings.Contains(out, "alla sessione s-cli") {
		t.Fatalf("assign --session: %d %q", code, out)
	}
	if code, out, _ := run(s.cfg, nil, "unassign", "--session", "s-cli"); code != 0 || !strings.Contains(out, "ora mostra demone (progetto)") {
		t.Fatalf("unassign --session: %d %q", code, out)
	}
	if code, out, _ := run(s.cfg, nil, "default", "gatto-blu"); code != 0 || !strings.Contains(out, "gatto-blu") {
		t.Fatalf("default: %d %q", code, out)
	}
	if code, out, _ := run(s.cfg, nil, "default"); code != 0 || out != "gatto-blu\n" {
		t.Fatalf("default (lettura): %q", out)
	}
	if code, _, e := run(s.cfg, nil, "assign", "inesistente"); code != 1 || !strings.Contains(e, "avatar sconosciuto") {
		t.Fatalf("assign sconosciuto: %d %q", code, e)
	}
	if code, _, _ := run(s.cfg, nil, "assign"); code != 2 {
		t.Fatalf("assign senza id: %d", code)
	}
	if code, out, _ := run(s.cfg, nil, "show", "gatto", "--pose", "done"); code != 0 || !strings.Contains(out, "██") || !strings.HasPrefix(out, "gatto «Gatto»") || strings.Contains(out, "working") {
		t.Fatalf("show: %d %.200q", code, out)
	}
	if code, _, _ := run(s.cfg, nil, "show", "gatto", "--pose", "balla"); code != 2 {
		t.Fatal("posa sconosciuta")
	}

	// servizio spento: stesse operazioni su libreria e assegnazioni locali
	dead := s.cfg
	dead.APIPort = freePort(t)
	localDir := filepath.Join(t.TempDir(), "avatars")
	loc := avatar.NewManager(avatar.NewLibrary(localDir), &config.AvatarStore{})
	if code, out, e := run(dead, loc, "add", file); code != 0 || !strings.Contains(out, "aggiunto") || !strings.Contains(e, "spento") {
		t.Fatalf("add locale: %d %q %q", code, out, e)
	}
	if code, _, _ := run(dead, loc, "assign", "gatto-blu"); code != 0 || loc.Resolve("", repo).Entry.ID != "gatto-blu" {
		t.Fatal("assign locale")
	}
	if code, out, e := run(dead, loc, "list"); code != 0 || !strings.Contains(e, "nessuna sessione nota") || !strings.Contains(out, "gatto-blu") {
		t.Fatalf("list locale: %q %q", out, e)
	}
	if code, _, _ := run(dead, loc, "default", "demone"); code != 0 || loc.DefaultEntry().ID != "demone" {
		t.Fatal("default locale")
	}
	bad := filepath.Join(t.TempDir(), "rotto.json")
	_ = os.WriteFile(bad, []byte(`{"format":"calamaio-avatar"}`), 0o644)
	if code, out, _ := run(dead, loc, "add", bad); code != 1 || !strings.Contains(out, "errore:") {
		t.Fatalf("add non valido: %d %q", code, out)
	}
}

func regexpLine(out, re string) bool {
	for _, l := range strings.Split(out, "\n") {
		if ok, _ := regexpMatch(re, l); ok {
			return true
		}
	}
	return false
}

func regexpMatch(re, s string) (bool, error) { return regexp.MatchString(re, s) }
