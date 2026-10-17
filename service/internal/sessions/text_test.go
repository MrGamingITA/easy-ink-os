package sessions

import (
	"encoding/json"
	"regexp"
	"strings"
	"testing"
	"unicode/utf8"

	"calamaio/internal/state"
)

func TestDetail(t *testing.T) {
	cwd := "/home/utente/web"
	cases := []struct {
		tool, input, want string
	}{
		{"Edit", `{"file_path":"/home/utente/web/src/app.ts"}`, "Edit src/app.ts"},
		{"Write", `{"file_path":"/etc/hosts"}`, "Write …/etc/hosts"},
		{"Read", `{"file_path":"C:\\Users\\u\\web\\a.go"}`, "Read …/web/a.go"},
		{"Bash", `{"command":"npm run test","description":"x"}`, "Bash: npm run test"},
		{"Bash", `{"command":"echo a\necho b"}`, "Bash: echo a …"},
		{"Grep", `{"pattern":"TODO"}`, "Grep TODO"},
		{"WebFetch", `{"url":"https://code.claude.com/docs/en/hooks"}`, "WebFetch code.claude.com"},
		{"Agent", `{"description":"Elenca i file"}`, "Agent: Elenca i file"},
		{"AskUserQuestion", `{"questions":[{"question":"Che tema?"}]}`, "Domanda: Che tema?"},
		{"mcp__github__create_issue", `{}`, "github: create_issue"},
		{"Strano", `{}`, "Strano"},
	}
	for _, c := range cases {
		if got := Detail(c.tool, json.RawMessage(c.input), cwd); got != c.want {
			t.Errorf("Detail(%s, %s) = %q, atteso %q", c.tool, c.input, got, c.want)
		}
	}
	long := Detail("Bash", json.RawMessage(`{"command":"`+strings.Repeat("àbc ", 60)+`"}`), cwd)
	if len(long) > state.MaxDetail || !strings.HasSuffix(long, "…") || !utf8.ValidString(long) {
		t.Fatalf("dettaglio lungo: %d byte %q", len(long), long)
	}
}

func TestSummary(t *testing.T) {
	cases := []struct{ in, want string }{
		{"Ho fatto tutte e due le cose. Il test fallisce come volevi.\n\n**1. File**", "Ho fatto tutte e due le cose."},
		{"**Fatto!** Ho aggiornato `index.html`.", "Fatto!"},
		{"## Riepilogo\n\nHo corretto [due link](http://x) rotti e il file `a.go`", "Riepilogo"},
		{"Ecco cosa ho fatto:\n- uno\n- due", "Ecco cosa ho fatto"},
		{"- [x] Primo punto completato. Altro", "Primo punto completato."},
		{"Versione 1.2 pronta. Fine", "Versione 1.2 pronta."},
		{"```go\ncodice\n```\nTutto ok.", "Tutto ok."},
		{"", ""},
	}
	for _, c := range cases {
		if got := Summary(c.in); got != c.want {
			t.Errorf("Summary(%q) = %q, atteso %q", c.in, got, c.want)
		}
	}
	long := Summary(strings.Repeat("parola è ", 40))
	if utf8.RuneCountInString(long) > SummaryMaxRunes || len(long) > state.MaxSummary || !strings.HasSuffix(long, "…") {
		t.Fatalf("riepilogo lungo: %d caratteri, %d byte: %q", utf8.RuneCountInString(long), len(long), long)
	}
}

func TestDefaultTestRegex(t *testing.T) {
	re := regexp.MustCompile(DefaultTestRegex)
	yes := []string{"npm run test", "npm test", "pnpm build", "yarn run build", "cd web && npm test", "pytest -q",
		"cargo test", "go test ./...", "vitest run", "npx jest", "make test", "tsc --noEmit", "node test.js", "node --test",
		"pio test -e native", "python -m pytest"}
	no := []string{"ls -a", "echo test", "cat jest.config.js", "git commit -m 'npm-test'", "node server.js", "mytsc"}
	for _, c := range yes {
		if !re.MatchString(c) {
			t.Errorf("non riconosciuto: %q", c)
		}
	}
	for _, c := range no {
		if re.MatchString(c) {
			t.Errorf("riconosciuto per sbaglio: %q", c)
		}
	}
}

func TestInterruptedTail(t *testing.T) {
	esc := `{"type":"assistant","message":{"content":[{"type":"text","text":"Sto"}]}}
{"type":"user","message":{"content":[{"type":"text","text":"[Request interrupted by user]"}]}}
{"type":"system","subtype":"turn_duration"}`
	if i, f := Interrupted([]byte(esc)); !i || f {
		t.Fatalf("Esc: %v %v", i, f)
	}
	if i, _ := Interrupted([]byte(plainEnd)); i {
		t.Fatal("messaggio normale preso per interruzione")
	}
	prompt := `{"type":"user","message":{"content":"[Request interrupted by user]"}}
{"type":"user","message":{"content":"nuovo prompt"}}`
	if i, _ := Interrupted([]byte(prompt)); i {
		t.Fatal("un prompt successivo chiude l'interruzione")
	}
	if _, err := ReadTail("relativo.jsonl"); err == nil {
		t.Fatal("percorso relativo accettato")
	}
	if _, err := ReadTail("/etc/passwd"); err == nil {
		t.Fatal("file non .jsonl accettato")
	}
}
