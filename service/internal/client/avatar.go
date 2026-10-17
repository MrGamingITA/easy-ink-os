package client

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"calamaio/internal/api"
	"calamaio/internal/avatar"
	"calamaio/internal/config"
)

// Comandi `calamaio avatar …` (docs/AVATAR.md §7). Con il servizio acceso passano dalla API locale,
// così display e webapp si aggiornano subito; con il servizio spento (o troppo vecchio per gli
// avatar) lavorano direttamente su libreria e config.json, con le stesse regole (internal/avatar).

const avatarUsage = `uso:
  calamaio avatar list [--free] [--json] [--session ID]
  calamaio avatar add <file> [--replace]
  calamaio avatar assign <id> [--project DIR | --session ID]   (predefinito: il progetto della cartella corrente)
  calamaio avatar unassign [--project DIR | --session ID]
  calamaio avatar default [<id>]                                senza id: mostra il predefinito
  calamaio avatar show <id> [--pose POSA]                       anteprima nel terminale
`

// AvatarIO: dove leggono e scrivono i comandi (test).
type AvatarIO struct {
	Out, Err io.Writer
	// Getwd e Getenv sostituibili nei test.
	Getwd  func() (string, error)
	Getenv func(string) string
	// Local: libreria e assegnazioni da usare col servizio spento (nil = quelle dell'utente).
	Local *avatar.Manager
}

// errNoService: il servizio non risponde, o non conosce gli avatar (versione vecchia).
var errNoService = errors.New("servizio non disponibile")

type avatarArgs struct {
	pos   []string
	flags map[string]string
	bools map[string]bool
}

// parseAvatarArgs: flag in qualunque posizione (`add file --replace`).
func parseAvatarArgs(args []string, withValue, boolean []string) (avatarArgs, error) {
	a := avatarArgs{flags: map[string]string{}, bools: map[string]bool{}}
	isIn := func(l []string, s string) bool {
		for _, x := range l {
			if x == s {
				return true
			}
		}
		return false
	}
	for i := 0; i < len(args); i++ {
		x := args[i]
		if !strings.HasPrefix(x, "-") || x == "-" {
			a.pos = append(a.pos, x)
			continue
		}
		name := strings.TrimLeft(x, "-")
		val, hasVal := "", false
		if k, v, ok := strings.Cut(name, "="); ok {
			name, val, hasVal = k, v, true
		}
		switch {
		case isIn(boolean, name):
			a.bools[name] = true
		case isIn(withValue, name):
			if !hasVal {
				if i+1 >= len(args) {
					return a, fmt.Errorf("--%s vuole un valore", name)
				}
				i++
				val = args[i]
			}
			a.flags[name] = val
		default:
			return a, fmt.Errorf("opzione sconosciuta: %s", x)
		}
	}
	return a, nil
}

// Avatar esegue `calamaio avatar …` e restituisce il codice d'uscita.
func Avatar(c config.Config, args []string, o AvatarIO) int {
	if o.Getwd == nil {
		o.Getwd = os.Getwd
	}
	if o.Getenv == nil {
		o.Getenv = os.Getenv
	}
	if len(args) == 0 {
		fmt.Fprint(o.Err, avatarUsage)
		return 2
	}
	sub, rest := args[0], args[1:]
	var err error
	switch sub {
	case "list":
		err = avatarList(c, rest, o)
	case "add":
		err = avatarAdd(c, rest, o)
	case "assign":
		err = avatarAssign(c, rest, o, false)
	case "unassign":
		err = avatarAssign(c, rest, o, true)
	case "default":
		err = avatarDefault(c, rest, o)
	case "show":
		err = avatarShow(rest, o)
	case "help", "-h", "--help":
		fmt.Fprint(o.Out, avatarUsage)
		return 0
	default:
		fmt.Fprint(o.Err, avatarUsage)
		return 2
	}
	var ue usageError
	switch {
	case errors.As(err, &ue):
		fmt.Fprintln(o.Err, "calamaio:", err)
		fmt.Fprint(o.Err, avatarUsage)
		return 2
	case err != nil:
		fmt.Fprintln(o.Err, "calamaio:", err)
		return 1
	}
	return 0
}

type usageError struct{ msg string }

func (e usageError) Error() string { return e.msg }

func local(o AvatarIO) (*avatar.Manager, error) {
	if o.Local != nil {
		return o.Local, nil
	}
	dir, err := config.AvatarDir()
	if err != nil {
		return nil, err
	}
	st, err := config.NewAvatarStore()
	if err != nil {
		return nil, err
	}
	return avatar.NewManager(avatar.NewLibrary(dir), st), nil
}

// avatarAPI: richiesta alla API locale. errNoService se il servizio non risponde o risponde 404 alla
// rotta (servizio senza avatar). Gli altri errori HTTP diventano errori col testo del servizio.
func avatarAPI(c config.Config, method, path string, body []byte, out any) error {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	var rd io.Reader
	if body != nil {
		rd = bytes.NewReader(body)
	}
	req, err := http.NewRequestWithContext(ctx, method, baseURL(c)+path, rd)
	if err != nil {
		return err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := httpClient.Do(req)
	if err != nil {
		return errNoService
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(io.LimitReader(resp.Body, 4<<20))
	if resp.StatusCode == http.StatusNotFound &&
		(strings.Contains(string(b), "API sconosciuta") || strings.Contains(string(b), "avatar non disponibili")) {
		return errNoService
	}
	if resp.StatusCode/100 != 2 {
		var e struct {
			Error    string   `json:"error"`
			Errors   []string `json:"errors"`
			Warnings []string `json:"warnings"`
		}
		_ = json.Unmarshal(b, &e)
		if e.Error == "" {
			e.Error = fmt.Sprintf("stato %d", resp.StatusCode)
		}
		return &apiError{Status: resp.StatusCode, Msg: e.Error, Errors: e.Errors, Warnings: e.Warnings}
	}
	if out != nil {
		return json.Unmarshal(b, out)
	}
	return nil
}

type apiError struct {
	Status   int
	Msg      string
	Errors   []string
	Warnings []string
}

func (e *apiError) Error() string { return e.Msg }

func noteLocal(o AvatarIO, what string) {
	fmt.Fprintf(o.Err, "(servizio Calamaio spento o senza avatar: %s; i display lo vedranno al prossimo avvio del servizio)\n", what)
}

func currentSession(a avatarArgs, o AvatarIO) string {
	if s := a.flags["session"]; s != "" {
		return s
	}
	if s := o.Getenv("CALAMAIO_SESSION_ID"); s != "" {
		return s
	}
	return o.Getenv("CLAUDE_SESSION_ID")
}

func avatarList(c config.Config, args []string, o AvatarIO) error {
	a, err := parseAvatarArgs(args, []string{"session"}, []string{"free", "json"})
	if err != nil {
		return usageError{err.Error()}
	}
	if len(a.pos) > 0 {
		return usageError{"list non vuole argomenti"}
	}
	cur := currentSession(a, o)
	var l api.AvatarList
	q := ""
	if cur != "" {
		q = "?session=" + url.QueryEscape(cur)
	}
	err = avatarAPI(c, http.MethodGet, "/api/avatars"+q, nil, &l)
	if errors.Is(err, errNoService) {
		m, lerr := local(o)
		if lerr != nil {
			return lerr
		}
		list, free, def := m.Usage(nil, cur)
		l = api.AvatarList{Avatars: list, Free: free, Default: def, Problems: m.Lib.Catalog().Problems, UserDir: m.Lib.UserDir, Rev: m.Rev()}
		if cur != "" {
			l.Current = &cur
		}
		if !a.bools["json"] {
			fmt.Fprintln(o.Err, "(servizio Calamaio spento: nessuna sessione nota, tutti gli avatar risultano liberi)")
		}
	} else if err != nil {
		return err
	}
	if a.bools["free"] {
		keep := l.Avatars[:0]
		for _, u := range l.Avatars {
			if u.State == "libero" && !u.Default {
				keep = append(keep, u)
			}
		}
		l.Avatars = keep
	}
	if a.bools["json"] {
		if l.Problems == nil {
			l.Problems = []avatar.Problem{}
		}
		enc := json.NewEncoder(o.Out)
		enc.SetIndent("", "  ")
		return enc.Encode(l)
	}
	if len(l.Avatars) == 0 {
		fmt.Fprintln(o.Out, "nessun avatar libero: creane uno nuovo (skill avatar) o riusa uno in uso")
	}
	for _, u := range l.Avatars {
		st := "libero"
		if u.State != "libero" {
			st = "in uso da " + strings.Join(u.UsedBy, ", ")
		}
		mark := ""
		if u.Default {
			mark = "  (predefinito)"
		}
		if u.Source == avatar.SourceUser {
			mark += "  [utente]"
		}
		fmt.Fprintf(o.Out, "%-24s %-24s %s%s\n", u.ID, u.Name, st, mark)
	}
	for _, p := range l.Problems {
		fmt.Fprintf(o.Err, "non caricato: %s: %s\n", p.File, strings.Join(p.Errors, "; "))
	}
	return nil
}

func avatarAdd(c config.Config, args []string, o AvatarIO) error {
	a, err := parseAvatarArgs(args, nil, []string{"replace"})
	if err != nil {
		return usageError{err.Error()}
	}
	if len(a.pos) != 1 {
		return usageError{"add vuole un file"}
	}
	raw, err := os.ReadFile(a.pos[0])
	if err != nil {
		return err
	}
	if len(raw) > 1<<20 {
		return fmt.Errorf("%s: %d byte, il massimo è %d", a.pos[0], len(raw), avatar.MaxJSONBytes)
	}
	// Prima la validazione locale: stessi errori col servizio acceso o spento, e un file rotto non
	// arriva nemmeno al servizio.
	res := avatar.Validate(raw)
	if !res.OK() {
		fmt.Fprintf(o.Out, "✗ %s non valido:\n", a.pos[0])
		for _, e := range res.Errors {
			fmt.Fprintln(o.Out, "  errore: "+e)
		}
		for _, w := range res.Warnings {
			fmt.Fprintln(o.Out, "  avviso: "+w)
		}
		return errors.New("avatar non valido: niente è stato copiato")
	}
	path := "/api/avatars"
	if a.bools["replace"] {
		path += "?replace=1"
	}
	var out struct {
		Avatar   *avatar.Use `json:"avatar"`
		Warnings []string    `json:"warnings"`
		Replaced bool        `json:"replaced"`
		File     string      `json:"file"`
	}
	err = avatarAPI(c, http.MethodPost, path, raw, &out)
	if errors.Is(err, errNoService) {
		m, lerr := local(o)
		if lerr != nil {
			return lerr
		}
		existed := m.Lib.Get(res.Avatar.ID) != nil
		e, r2, aerr := m.Lib.Add(raw, a.bools["replace"])
		if aerr != nil {
			return aerr
		}
		noteLocal(o, "copiato nella libreria")
		out.Warnings, out.Replaced, out.File = r2.Warnings, existed && a.bools["replace"], e.File
		out.Avatar = &avatar.Use{ID: e.ID, Name: e.Name, CRC: e.CRC, Bytes: len(e.Bin)}
	} else if err != nil {
		return err
	}
	verb := "aggiunto"
	if out.Replaced {
		verb = "sostituito"
	}
	if out.Avatar != nil {
		fmt.Fprintf(o.Out, "✓ %s «%s» %s alla libreria (%s): binario %d byte, crc32 %08x\n", out.Avatar.ID, out.Avatar.Name, verb, out.File, out.Avatar.Bytes, out.Avatar.CRC)
	}
	for _, w := range out.Warnings {
		fmt.Fprintln(o.Out, "  avviso: "+w)
	}
	return nil
}

func avatarAssign(c config.Config, args []string, o AvatarIO, un bool) error {
	a, err := parseAvatarArgs(args, []string{"project", "session"}, nil)
	if err != nil {
		return usageError{err.Error()}
	}
	want := 1
	if un {
		want = 0
	}
	if len(a.pos) != want {
		if un {
			return usageError{"unassign non vuole un id"}
		}
		return usageError{"assign vuole l'id di un avatar"}
	}
	id := ""
	if !un {
		id = a.pos[0]
	}
	proj, sess := a.flags["project"], a.flags["session"]
	if proj != "" && sess != "" {
		return usageError{"--project e --session insieme: scegline uno"}
	}
	var body []byte
	var path string
	idv := any(nil)
	if id != "" {
		idv = id
	}
	if sess != "" {
		path = "/api/sessions/" + url.PathEscape(sess) + "/avatar"
		body, _ = json.Marshal(map[string]any{"id": idv})
	} else {
		if proj == "" {
			if proj, err = o.Getwd(); err != nil {
				return err
			}
		}
		if abs, err := filepath.Abs(proj); err == nil {
			proj = abs
		}
		path = "/api/projects/avatar"
		body, _ = json.Marshal(map[string]any{"dir": proj, "id": idv})
	}
	var out struct {
		Dir    string `json:"dir"`
		Avatar string `json:"avatar"`
		From   string `json:"from"`
	}
	err = avatarAPI(c, http.MethodPost, path, body, &out)
	if errors.Is(err, errNoService) {
		m, lerr := local(o)
		if lerr != nil {
			return lerr
		}
		if sess != "" {
			if err := m.AssignSession(sess, id); err != nil {
				return err
			}
			r := m.Resolve(sess, "")
			out.Avatar, out.From = r.Entry.ID, r.From
		} else {
			d, err := m.AssignProject(proj, id)
			if err != nil {
				return err
			}
			r := m.Resolve("", d)
			out.Dir, out.Avatar, out.From = d, r.Entry.ID, r.From
		}
		noteLocal(o, "assegnazione salvata in config.json")
	} else if err != nil {
		return err
	}
	target := "al progetto " + out.Dir
	if sess != "" {
		target = "alla sessione " + sess
	}
	if un {
		fmt.Fprintf(o.Out, "✓ assegnazione tolta %s: ora mostra %s (%s)\n", target, out.Avatar, out.From)
	} else {
		fmt.Fprintf(o.Out, "✓ %s assegnato %s\n", out.Avatar, target)
	}
	return nil
}

func avatarDefault(c config.Config, args []string, o AvatarIO) error {
	a, err := parseAvatarArgs(args, nil, nil)
	if err != nil {
		return usageError{err.Error()}
	}
	if len(a.pos) > 1 {
		return usageError{"default vuole al massimo un id"}
	}
	if len(a.pos) == 0 {
		var l api.AvatarList
		err := avatarAPI(c, http.MethodGet, "/api/avatars", nil, &l)
		if errors.Is(err, errNoService) {
			m, lerr := local(o)
			if lerr != nil {
				return lerr
			}
			if d := m.DefaultEntry(); d != nil {
				l.Default = d.ID
			}
		} else if err != nil {
			return err
		}
		fmt.Fprintln(o.Out, l.Default)
		return nil
	}
	body, _ := json.Marshal(map[string]string{"id": a.pos[0]})
	var out struct {
		Default string `json:"default"`
	}
	err = avatarAPI(c, http.MethodPost, "/api/avatars/default", body, &out)
	if errors.Is(err, errNoService) {
		m, lerr := local(o)
		if lerr != nil {
			return lerr
		}
		if err := m.SetDefault(a.pos[0]); err != nil {
			return err
		}
		out.Default = m.DefaultEntry().ID
		noteLocal(o, "predefinito salvato in config.json")
	} else if err != nil {
		return err
	}
	fmt.Fprintf(o.Out, "✓ avatar predefinito: %s (sessioni senza avatar e schermate senza sessione)\n", out.Default)
	return nil
}

func avatarShow(args []string, o AvatarIO) error {
	a, err := parseAvatarArgs(args, []string{"pose"}, nil)
	if err != nil {
		return usageError{err.Error()}
	}
	if len(a.pos) != 1 {
		return usageError{"show vuole l'id di un avatar (o un file .avatar.json)"}
	}
	var e *avatar.Entry
	if strings.HasSuffix(a.pos[0], ".json") {
		raw, err := os.ReadFile(a.pos[0])
		if err != nil {
			return err
		}
		r := avatar.Validate(raw)
		if !r.OK() {
			return fmt.Errorf("avatar non valido: %s", strings.Join(r.Errors, "; "))
		}
		bin, _ := avatar.Pack(r.Avatar)
		e = &avatar.Entry{ID: r.Avatar.ID, Name: r.Avatar.Name, Source: "file", Frames: r.Avatar.Frames, Avatar: r.Avatar, Bin: bin, CRC: avatar.CRC(bin)}
	} else {
		m, err := local(o)
		if err != nil {
			return err
		}
		if e = m.Lib.Get(a.pos[0]); e == nil {
			return fmt.Errorf("avatar sconosciuto: %q (calamaio avatar list)", a.pos[0])
		}
	}
	poses := avatar.Poses
	if p := a.flags["pose"]; p != "" {
		if !validPose(p) {
			return usageError{"posa sconosciuta: " + p + " (" + strings.Join(avatar.Poses, " ") + ")"}
		}
		poses = []string{p}
	}
	fmt.Fprintf(o.Out, "%s «%s» · %s · %d fotogrammi · binario %d byte, crc32 %08x\n\n", e.ID, e.Name, e.Source, e.Frames, len(e.Bin), e.CRC)
	fmt.Fprint(o.Out, AvatarText(e.Avatar, poses))
	return nil
}

func validPose(p string) bool {
	for _, x := range avatar.Poses {
		if x == p {
			return true
		}
	}
	return false
}

// AvatarText: anteprima nel terminale come text() di tools/avatar/lib/render.mjs (due caratteri per
// pixel: ██ inchiostro, ░░ carta, spazi trasparente; fotogrammi affiancati).
func AvatarText(a *avatar.Avatar, poses []string) string {
	var b strings.Builder
	for _, p := range poses {
		frs := a.PoseFrames(p)
		b.WriteString(p)
		if _, own := a.Poses[p]; !own {
			b.WriteString(" (ripiego: " + avatar.Fallback[p] + ")")
		}
		b.WriteByte('\n')
		for y := 0; y < a.H; y++ {
			for f, fr := range frs {
				if f > 0 {
					b.WriteString(" │ ")
				}
				for x := 0; x < len(fr[y]); x++ {
					switch fr[y][x] {
					case '#':
						b.WriteString("██")
					case '+':
						b.WriteString("░░")
					default:
						b.WriteString("  ")
					}
				}
			}
			b.WriteByte('\n')
		}
		b.WriteByte('\n')
	}
	return b.String()
}

var sessionIDRe = regexp.MustCompile(`^[A-Za-z0-9._-]{1,128}$`)

// HookContext: `calamaio hook --context`, nel SessionStart sincrono del plugin.
//   - annota CALAMAIO_SESSION_ID in $CLAUDE_ENV_FILE, così `calamaio avatar list` sa qual è la
//     sessione corrente nei comandi Bash di Claude;
//   - chiede al servizio (al massimo 1 s) se il progetto ha un avatar e, solo se non ce l'ha, stampa
//     il JSON con additionalContext per Claude.
//
// Servizio spento, lento o vecchio: nessun output. Mai un errore, mai un codice diverso da 0.
func HookContext(c config.Config, stdin io.Reader, out io.Writer, getenv func(string) string) {
	body, err := io.ReadAll(io.LimitReader(stdin, maxStdin))
	if err != nil || len(body) == 0 {
		return
	}
	var p struct {
		SessionID string `json:"session_id"`
		Event     string `json:"hook_event_name"`
	}
	_ = json.Unmarshal(body, &p)
	if f := getenv("CLAUDE_ENV_FILE"); f != "" && sessionIDRe.MatchString(p.SessionID) {
		if fh, err := os.OpenFile(f, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o600); err == nil {
			_, _ = fmt.Fprintf(fh, "export CALAMAIO_SESSION_ID=%s\n", p.SessionID)
			_ = fh.Close()
		}
	}
	if p.Event != "SessionStart" && p.Event != "UserPromptSubmit" {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	resp, err := post(ctx, baseURL(c)+"/hook/context", body)
	if err != nil || len(bytes.TrimSpace(resp)) == 0 {
		return
	}
	var r struct {
		AdditionalContext string `json:"additionalContext"`
	}
	if json.Unmarshal(resp, &r) != nil || strings.TrimSpace(r.AdditionalContext) == "" {
		return
	}
	var o struct {
		HookSpecificOutput struct {
			HookEventName     string `json:"hookEventName"`
			AdditionalContext string `json:"additionalContext"`
		} `json:"hookSpecificOutput"`
	}
	o.HookSpecificOutput.HookEventName = p.Event
	o.HookSpecificOutput.AdditionalContext = r.AdditionalContext
	b, _ := json.Marshal(o)
	_, _ = out.Write(append(b, '\n'))
}
