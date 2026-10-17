// mkfixtures rigenera le fixture ripulite in protocol/samples/fixtures/ dalla cattura grezza di M0
// (che non va nel repository). Ripetibile: stesso ingresso, stessi file.
//
//	cd service && go run ./cmd/mkfixtures \
//	  -capture ../protocol/samples/capture-2026-10-11.jsonl -lines 101 \
//	  -transcript ~/.claude/projects/<cartella del progetto di prova>/<session_id>.jsonl \
//	  -out ../protocol/samples/fixtures
//
// Pulizia: percorsi dell'utente → /home/utente/progetto-prova, transcript_path e simili finti,
// risultati di ToolSearch svuotati (nomi dei server MCP dell'utente), testi lunghi accorciati.
// Alla fine ogni file viene controllato contro un elenco di parole vietate: se ne resta una,
// il comando fallisce e non scrive niente.
package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

const (
	fakeCwd     = "/home/utente/progetto-prova"
	fakeDashCwd = "-home-utente-progetto-prova"
	fakeProj    = "/home/utente/.claude/projects/" + fakeDashCwd
	maxText     = 240 // oltre si accorcia a cutText caratteri
	cutText     = 200
)

// Parole che non devono mai comparire nelle fixture (controllo finale, senza distinzione di maiuscole);
// in più il nome dell'utente del Mac che le genera.
var forbidden = withLocalUser([]string{"/users/", "/private/", "claude-501", "downloads", "calamaios",
	"5e836d04", "gmail", "unreal", "mrgaming"})

func withLocalUser(w []string) []string {
	if u := strings.ToLower(os.Getenv("USER")); len(u) >= 3 {
		return append(w, u)
	}
	return w
}

type record struct {
	TS        string          `json:"ts"`
	Source    string          `json:"source"`
	Event     string          `json:"event"`
	Synthetic bool            `json:"synthetic,omitempty"`
	Note      string          `json:"note,omitempty"`
	Payload   json.RawMessage `json:"payload"`
}

func main() {
	capture := flag.String("capture", "", "cattura grezza JSONL di `calamaio serve --capture`")
	transcript := flag.String("transcript", "", "transcript della sessione di prova con il permesso negato nel terminale (facoltativo)")
	out := flag.String("out", "", "cartella delle fixture")
	maxLines := flag.Int("lines", 0, "usa solo le prime N righe della cattura (0 = tutte): il servizio in cattura continua ad aggiungerne")
	flag.Parse()
	if *capture == "" || *out == "" {
		flag.Usage()
		os.Exit(2)
	}
	files, err := build(*capture, *transcript, *maxLines)
	if err != nil {
		fmt.Fprintln(os.Stderr, "mkfixtures:", err)
		os.Exit(1)
	}
	for name, b := range files {
		if bad := check(b); bad != "" {
			fmt.Fprintf(os.Stderr, "mkfixtures: %s contiene ancora %q: niente scritto\n", name, bad)
			os.Exit(1)
		}
	}
	if err := os.MkdirAll(*out, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, "mkfixtures:", err)
		os.Exit(1)
	}
	names := make([]string, 0, len(files))
	for n := range files {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		if err := os.WriteFile(filepath.Join(*out, n), files[n], 0o644); err != nil {
			fmt.Fprintln(os.Stderr, "mkfixtures:", err)
			os.Exit(1)
		}
		fmt.Printf("%s: %d byte\n", n, len(files[n]))
	}
}

func check(b []byte) string {
	low := strings.ToLower(string(b))
	for _, f := range forbidden {
		if strings.Contains(low, f) {
			return f
		}
	}
	return ""
}

type cleaner struct {
	repl []string // coppie vecchio → nuovo, le più lunghe prima
}

var generic = []struct {
	re  *regexp.Regexp
	new string
}{
	{regexp.MustCompile(`/private/tmp/claude-\d+`), "/tmp/claude"},
	{regexp.MustCompile(`/private/var/[^\s"']*`), "/tmp/x"},
	{regexp.MustCompile(`/Users/[^/\s"']+`), "/home/utente"},
	{regexp.MustCompile(`-Users-[A-Za-z0-9._]+-`), "-home-utente-"},
	{regexp.MustCompile(`claude-\d{3,}`), "claude"},
	{regexp.MustCompile(`[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/scratchpad`), "scratch"},
}

func newCleaner(cwds []string) *cleaner {
	c := &cleaner{}
	seen := map[string]bool{}
	add := func(old, new string) {
		if old != "" && !seen[old] {
			seen[old] = true
			c.repl = append(c.repl, old, new)
		}
	}
	for _, d := range cwds {
		add(d, fakeCwd)
		add(strings.TrimPrefix(d, "/private"), fakeCwd)
		add(dash(d), fakeDashCwd)
		add(dash(strings.TrimPrefix(d, "/private")), fakeDashCwd)
	}
	// le più lunghe prima, così un prefisso non rovina una sostituzione più specifica
	type pair struct{ o, n string }
	ps := []pair{}
	for i := 0; i < len(c.repl); i += 2 {
		ps = append(ps, pair{c.repl[i], c.repl[i+1]})
	}
	sort.SliceStable(ps, func(i, j int) bool { return len(ps[i].o) > len(ps[j].o) })
	c.repl = c.repl[:0]
	for _, p := range ps {
		c.repl = append(c.repl, p.o, p.n)
	}
	return c
}

// dash: come Claude Code nomina la cartella del progetto in ~/.claude/projects.
func dash(p string) string {
	return regexp.MustCompile(`[^A-Za-z0-9]`).ReplaceAllString(p, "-")
}

func (c *cleaner) str(s string) string {
	s = strings.NewReplacer(c.repl...).Replace(s)
	for _, g := range generic {
		s = g.re.ReplaceAllString(s, g.new)
	}
	if r := []rune(s); len(r) > maxText {
		s = string(r[:cutText]) + "…[accorciato]"
	}
	return s
}

// walk ripulisce ricorsivamente ogni stringa (chiavi comprese: le risposte di AskUserQuestion
// usano la domanda come chiave).
func (c *cleaner) walk(v any) any {
	switch x := v.(type) {
	case string:
		return c.str(x)
	case []any:
		for i := range x {
			x[i] = c.walk(x[i])
		}
		return x
	case map[string]any:
		out := make(map[string]any, len(x))
		for k, val := range x {
			out[c.str(k)] = c.walk(val)
		}
		return out
	}
	return v
}

func readLines(path string) ([][]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var out [][]byte
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 0, 1<<20), 64<<20)
	for sc.Scan() {
		if l := bytes.TrimSpace(sc.Bytes()); len(l) > 0 {
			out = append(out, append([]byte(nil), l...))
		}
	}
	return out, sc.Err()
}

func build(capturePath, transcriptPath string, maxLines int) (map[string][]byte, error) {
	lines, err := readLines(capturePath)
	if err != nil {
		return nil, err
	}
	if maxLines > 0 && len(lines) > maxLines {
		lines = lines[:maxLines]
	}
	var recs []record
	var cwds []string
	for i, l := range lines {
		var r record
		if err := json.Unmarshal(l, &r); err != nil {
			return nil, fmt.Errorf("riga %d: %w", i+1, err)
		}
		var p map[string]any
		if err := json.Unmarshal(r.Payload, &p); err != nil {
			return nil, fmt.Errorf("riga %d: payload: %w", i+1, err)
		}
		for _, k := range []string{"cwd"} {
			if s, ok := p[k].(string); ok {
				cwds = append(cwds, s)
			}
		}
		if ws, ok := p["workspace"].(map[string]any); ok {
			for _, k := range []string{"current_dir", "project_dir"} {
				if s, ok := ws[k].(string); ok {
					cwds = append(cwds, s)
				}
			}
		}
		recs = append(recs, r)
	}
	c := newCleaner(cwds)

	var buf bytes.Buffer
	for i, r := range recs {
		var p map[string]any
		_ = json.Unmarshal(r.Payload, &p)
		fixFields(p)
		p = c.walk(p).(map[string]any)
		b, err := json.Marshal(p)
		if err != nil {
			return nil, fmt.Errorf("riga %d: %w", i+1, err)
		}
		r.Payload = b
		line, _ := json.Marshal(r)
		buf.Write(line)
		buf.WriteByte('\n')
	}
	files := map[string][]byte{"cattura-2026-10-11.jsonl": buf.Bytes()}
	files["stopfailure-sintetico.jsonl"] = stopFailure()
	if transcriptPath != "" {
		b, err := deniedTail(transcriptPath, c)
		if err != nil {
			return nil, err
		}
		files["transcript-negato.jsonl"] = b
	}
	return files, nil
}

// fixFields sostituisce i campi che contengono percorsi o dati dell'ambiente dell'utente.
func fixFields(p map[string]any) {
	sid, _ := p["session_id"].(string)
	if _, ok := p["transcript_path"]; ok {
		p["transcript_path"] = fakeProj + "/" + sid + ".jsonl"
	}
	if _, ok := p["agent_transcript_path"]; ok {
		aid, _ := p["agent_id"].(string)
		p["agent_transcript_path"] = fakeProj + "/" + sid + "/subagents/agent-" + aid + ".jsonl"
	}
	if _, ok := p["scratchpad_dir"]; ok {
		p["scratchpad_dir"] = "/tmp/claude/" + fakeDashCwd + "/" + sid + "/scratch"
	}
	if tr, ok := p["tool_response"].(map[string]any); ok {
		if _, ok := tr["outputFile"]; ok {
			id, _ := tr["agentId"].(string)
			tr["outputFile"] = "/tmp/claude/" + fakeDashCwd + "/" + sid + "/tasks/" + id + ".output"
		}
	}
	if p["tool_name"] == "ToolSearch" {
		// l'esito elenca strumenti e server MCP dell'utente: non serve al parser
		if _, ok := p["tool_response"]; ok {
			p["tool_response"] = map[string]any{"matches": []any{}, "ripulito": true}
		}
	}
}

// stopFailure: StopFailure costruito dall'esempio della documentazione degli hook (nessuna
// cattura vera: provocare un errore API apposta non si può). Marcato synthetic.
func stopFailure() []byte {
	const sid = "5f0c1a2b-0000-4000-8000-00000000e440"
	tp := fakeProj + "/" + sid + ".jsonl"
	note := "sintetico: dall'esempio StopFailure della documentazione degli hook (code.claude.com/docs/en/hooks)"
	recs := []record{
		{TS: "2026-10-11T16:00:00.000+02:00", Source: "hook", Event: "SessionStart", Synthetic: true, Note: note,
			Payload: mustJSON(map[string]any{"session_id": sid, "transcript_path": tp, "cwd": fakeCwd, "hook_event_name": "SessionStart", "source": "startup"})},
		{TS: "2026-10-11T16:00:05.000+02:00", Source: "hook", Event: "UserPromptSubmit", Synthetic: true, Note: note,
			Payload: mustJSON(map[string]any{"session_id": sid, "transcript_path": tp, "cwd": fakeCwd, "permission_mode": "default", "hook_event_name": "UserPromptSubmit", "prompt": "Rifai i test"})},
		{TS: "2026-10-11T16:00:09.000+02:00", Source: "hook", Event: "StopFailure", Synthetic: true, Note: note,
			Payload: mustJSON(map[string]any{"session_id": sid, "transcript_path": tp, "cwd": fakeCwd, "hook_event_name": "StopFailure",
				"error": "rate_limit", "error_details": "429 Too Many Requests", "last_assistant_message": "API Error: Rate limit reached"})},
		{TS: "2026-10-11T16:00:30.000+02:00", Source: "hook", Event: "UserPromptSubmit", Synthetic: true, Note: note,
			Payload: mustJSON(map[string]any{"session_id": sid, "transcript_path": tp, "cwd": fakeCwd, "permission_mode": "default", "hook_event_name": "UserPromptSubmit", "prompt": "Riprova"})},
	}
	var buf bytes.Buffer
	for _, r := range recs {
		b, _ := json.Marshal(r)
		buf.Write(b)
		buf.WriteByte('\n')
	}
	return buf.Bytes()
}

func mustJSON(v any) json.RawMessage {
	b, err := json.Marshal(v)
	if err != nil {
		panic(err)
	}
	return b
}

// deniedTail: la coda del transcript vero al momento del permesso negato nel terminale (dal
// tool_use negato alla fine del turno), ridotta ai campi che legge sessions.Interrupted: type,
// subtype, timestamp, toolDenialKind, message.role e i blocchi text, tool_use e tool_result.
func deniedTail(path string, c *cleaner) ([]byte, error) {
	lines, err := readLines(path)
	if err != nil {
		return nil, err
	}
	deny := -1
	for i, l := range lines {
		if bytes.Contains(l, []byte(`"toolDenialKind":"user-rejected"`)) {
			deny = i
			break
		}
	}
	if deny < 0 {
		return nil, fmt.Errorf("nessun permesso negato dall'utente in %s", path)
	}
	start := deny
	for start > 0 && !bytes.Contains(lines[start], []byte(`"type":"tool_use"`)) {
		start--
	}
	var buf bytes.Buffer
	for _, l := range lines[start:] {
		var e map[string]any
		if json.Unmarshal(l, &e) != nil {
			continue
		}
		t, _ := e["type"].(string)
		if t != "user" && t != "assistant" && t != "system" {
			continue
		}
		o := map[string]any{"type": t, "timestamp": e["timestamp"]}
		if st, ok := e["subtype"]; ok {
			o["subtype"] = st
		}
		if k, ok := e["toolDenialKind"]; ok {
			o["toolDenialKind"] = k
		}
		if m, ok := e["message"].(map[string]any); ok {
			o["message"] = map[string]any{"role": m["role"], "content": keepBlocks(m["content"])}
		}
		o = c.walk(o).(map[string]any)
		b, _ := json.Marshal(o)
		buf.Write(b)
		buf.WriteByte('\n')
		if t == "system" {
			break // fine del turno interrotto
		}
	}
	return buf.Bytes(), nil
}

// keepBlocks tiene solo i blocchi text, tool_use (id, name, input) e tool_result.
func keepBlocks(v any) any {
	list, ok := v.([]any)
	if !ok {
		return v
	}
	out := []any{}
	for _, x := range list {
		b, _ := x.(map[string]any)
		switch b["type"] {
		case "text":
			out = append(out, map[string]any{"type": "text", "text": b["text"]})
		case "tool_use":
			out = append(out, map[string]any{"type": "tool_use", "id": b["id"], "name": b["name"], "input": b["input"]})
		case "tool_result":
			out = append(out, map[string]any{"type": "tool_result", "tool_use_id": b["tool_use_id"], "is_error": b["is_error"], "content": b["content"]})
		}
	}
	return out
}
