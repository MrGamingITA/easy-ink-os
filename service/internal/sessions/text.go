package sessions

import (
	"encoding/json"
	"net/url"
	"path"
	"regexp"
	"strings"
	"unicode/utf8"

	"calamaio/internal/state"
)

// DefaultTestRegex riconosce i comandi Bash di test e build (sostituibile con "test_regex" nella
// config). Brief 5.4 più node/bun/deno/npx, go build, pio test e i comandi più comuni di
// altri ecosistemi. Si cerca in tutto il comando, quindi `cd web && npm test` vale.
const DefaultTestRegex = `(?:^|[\s;&|(])(?:` +
	`(?:npm|pnpm|yarn|bun)\s+(?:run\s+)?(?:test|build)|` +
	`npx\s+(?:jest|vitest|tsc|playwright\s+test)|` +
	`pytest|python3?\s+-m\s+(?:pytest|unittest)|` +
	`cargo\s+(?:test|build)|go\s+(?:test|build|vet)|` +
	`vitest|jest|tsc|make\s+(?:test|build|check)|` +
	`node\s+(?:--test|\S*test\S*\.[cm]?[jt]s)|deno\s+test|` +
	`mvn\s+(?:test|package|verify)|gradlew?\s+(?:test|build)|\./gradlew\s+(?:test|build)|dotnet\s+(?:test|build)|` +
	`swift\s+(?:test|build)|pio\s+(?:test|run)|ctest|rspec|phpunit` +
	`)(?:$|[\s;&|)])`

// Detail costruisce il dettaglio «strumento + argomento corto»: `Edit src/app.ts`,
// `Bash: npm run test`. I percorsi sono relativi a cwd quando ci stanno dentro.
func Detail(tool string, input json.RawMessage, cwd string) string {
	var in map[string]any
	_ = json.Unmarshal(input, &in)
	str := func(k string) string {
		v, _ := in[k].(string)
		return v
	}
	var d string
	switch tool {
	case "Bash", "PowerShell":
		d = tool + ": " + firstLine(str("command"))
	case "Edit", "MultiEdit", "Write", "Read", "NotebookEdit", "NotebookRead":
		p := str("file_path")
		if p == "" {
			p = str("notebook_path")
		}
		d = tool + " " + relPath(p, cwd)
	case "Glob", "Grep":
		d = tool + " " + str("pattern")
	case "WebFetch":
		if u, err := url.Parse(str("url")); err == nil && u.Host != "" {
			d = "WebFetch " + u.Host
		} else {
			d = "WebFetch"
		}
	case "WebSearch":
		d = "WebSearch " + str("query")
	case "Agent", "Task":
		d = "Agent: " + str("description")
	case "TaskCreate":
		d = "Task: " + str("subject")
	case "TaskUpdate":
		d = "Task #" + str("taskId")
	case "TodoWrite":
		d = "Aggiorna i task"
	case "Skill":
		d = "Skill " + str("skill")
	case "AskUserQuestion":
		d = "Domanda: " + question(in)
	case "ExitPlanMode":
		d = "Piano da approvare"
	default:
		if strings.HasPrefix(tool, "mcp__") {
			// mcp__server__tool → server: tool
			parts := strings.SplitN(strings.TrimPrefix(tool, "mcp__"), "__", 2)
			if len(parts) == 2 {
				d = parts[0] + ": " + parts[1]
				break
			}
		}
		d = tool
	}
	return clean(strings.TrimSpace(d), state.MaxDetail)
}

func question(in map[string]any) string {
	qs, _ := in["questions"].([]any)
	if len(qs) == 0 {
		return ""
	}
	q, _ := qs[0].(map[string]any)
	s, _ := q["question"].(string)
	return firstLine(s)
}

func firstLine(s string) string {
	s = strings.TrimSpace(s)
	if i := strings.IndexAny(s, "\r\n"); i >= 0 {
		return strings.TrimSpace(s[:i]) + " …"
	}
	return s
}

// relPath: relativo a cwd se ci sta dentro, altrimenti le ultime due parti del percorso.
func relPath(p, cwd string) string {
	if p == "" {
		return ""
	}
	p = strings.ReplaceAll(p, "\\", "/")
	c := strings.TrimRight(strings.ReplaceAll(cwd, "\\", "/"), "/")
	if c != "" && strings.HasPrefix(p, c+"/") {
		return strings.TrimPrefix(p, c+"/")
	}
	dir, file := path.Split(path.Clean(p))
	if parent := path.Base(dir); parent != "/" && parent != "." && parent != "" {
		return "…/" + parent + "/" + file
	}
	return file
}

// clean toglie i caratteri di controllo, comprime gli spazi e taglia a max byte con «…».
func clean(s string, max int) string {
	var b strings.Builder
	space := false
	for _, r := range s {
		if r < 0x20 || r == 0x7f || r == ' ' || r == ' ' {
			r = ' '
		}
		if r == ' ' {
			if space {
				continue
			}
			space = true
		} else {
			space = false
		}
		b.WriteRune(r)
	}
	return ellipsis(strings.TrimSpace(b.String()), max)
}

// ellipsis taglia a max byte UTF-8, su un confine di parola se possibile, aggiungendo «…».
func ellipsis(s string, max int) string { return limitText(s, 1<<30, max) }

var (
	reFence    = regexp.MustCompile("(?s)```.*?(```|$)")
	reImage    = regexp.MustCompile(`!\[([^\]]*)\]\([^)]*\)`)
	reLink     = regexp.MustCompile(`\[([^\]]+)\]\([^)]*\)`)
	reEmph     = regexp.MustCompile(`(\*\*|__|~~)(.+?)(\*\*|__|~~)`)
	reItalic   = regexp.MustCompile(`(^|[\s(])[*_]([^*_\s][^*_]*?)[*_]([\s).,;:!?]|$)`)
	reCode     = regexp.MustCompile("`([^`]*)`")
	reLinePref = regexp.MustCompile(`(?m)^\s{0,3}(#{1,6}\s+|>\s?|[-*+]\s+(\[[ xX]\]\s+)?|\d+[.)]\s+)`)
	reSentence = regexp.MustCompile(`[.!?…](\s|$)`)
)

// SummaryMaxRunes: «al massimo circa 120 caratteri» (brief 5.4); in byte resta comunque entro
// state.MaxSummary.
const SummaryMaxRunes = 120

// Summary: prima frase di last_assistant_message, senza markdown, al massimo ~120 caratteri.
func Summary(msg string) string {
	s := strings.ReplaceAll(msg, "\r\n", "\n")
	s = reFence.ReplaceAllString(s, " ")
	s = reImage.ReplaceAllString(s, "$1")
	s = reLink.ReplaceAllString(s, "$1")
	s = reLinePref.ReplaceAllString(s, "")
	for i := 0; i < 2; i++ { // enfasi annidate
		s = reEmph.ReplaceAllString(s, "$2")
		s = reItalic.ReplaceAllString(s, "$1$2$3")
	}
	s = reCode.ReplaceAllString(s, "$1")
	// primo paragrafo non vuoto
	var para string
	for _, p := range strings.Split(s, "\n\n") {
		if p = strings.TrimSpace(p); p != "" {
			para = p
			break
		}
	}
	// una riga che finisce con «:» introduce un elenco: basta lei
	if i := strings.IndexByte(para, '\n'); i >= 0 {
		first := strings.TrimSpace(para[:i])
		if strings.HasSuffix(first, ":") || reSentence.MatchString(first) {
			para = first
		}
	}
	if loc := reSentence.FindStringIndex(para); loc != nil {
		para = para[:loc[0]+utf8.RuneLen([]rune(para[loc[0]:])[0])]
	}
	para = strings.TrimSuffix(strings.TrimSpace(para), ":")
	return limitText(clean(para, 1<<20), SummaryMaxRunes, state.MaxSummary)
}

// limitText taglia s a maxRunes caratteri e maxBytes byte, su un confine di parola se possibile,
// aggiungendo «…».
func limitText(s string, maxRunes, maxBytes int) string {
	if utf8.RuneCountInString(s) <= maxRunes && len(s) <= maxBytes {
		return s
	}
	const dots = "…" // 1 carattere, 3 byte
	var b strings.Builder
	n := 0
	for _, r := range s {
		if n+1 > maxRunes-1 || b.Len()+utf8.RuneLen(r) > maxBytes-len(dots) {
			break
		}
		b.WriteRune(r)
		n++
	}
	cut := b.String()
	if i := strings.LastIndexByte(cut, ' '); i > len(cut)*2/3 {
		cut = cut[:i]
	}
	return strings.TrimRight(cut, " ,;:") + dots
}

// shortCmd: il dettaglio senza «Bash: », per il log («chiede permesso: npm test»).
func shortCmd(detail string) string {
	return strings.TrimPrefix(strings.TrimPrefix(detail, "Bash: "), "PowerShell: ")
}
