// Package sessions è la macchina a stati di M4: costruisce lo stato delle sessioni dai payload
// veri di hook e status line di Claude Code (brief 5.4, corretto con le catture: vedi
// protocol/samples/fixtures/LEGGIMI.md e docs/DECISIONI.md).
package sessions

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"log"
	"math"
	"path"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"calamaio/internal/game"
	"calamaio/internal/state"
)

// Tempi della macchina a stati.
const (
	// DefaultGrace: dopo una status line arrivata mentre la sessione lavora o chiede un permesso,
	// quanto aspettare un hook prima di guardare il transcript. Nelle catture PostToolUse arriva
	// 5–31 ms dopo la status line della risposta al permesso.
	DefaultGrace = 1 * time.Second
	// DefaultEndDelay: dopo SessionEnd la riga resta (in pausa, «Sessione chiusa») per poco.
	DefaultEndDelay = 5 * time.Second
	// lateNotify: Notification permission_prompt arriva 6 s dopo PermissionRequest (misurato:
	// 5 casi su 5). Se nel frattempo il permesso è già stato risolto, la notifica è vecchia.
	lateNotify = 10 * time.Second
)

// Engine riceve i payload e scrive lo stato nello Store. Sicuro per l'uso concorrente.
type Engine struct {
	Store    *state.Store
	Now      func() time.Time
	TestRe   *regexp.Regexp
	Grace    time.Duration
	EndDelay time.Duration
	// ReadTail legge la coda del transcript (sostituibile nei test).
	ReadTail func(path string) ([]byte, error)
	// TodayFile: dove salvare le statistiche di Oggi (vuoto = non si salvano).
	TodayFile string
	Log       *log.Logger
	// OnHookClosed (M5): un permesso con un hook --wait in attesa è stato risolto da un altro segnale
	// (PostToolUse, nuovo prompt, Stop, diniego visto nel transcript…). Claude Code NON chiude il hook
	// quando rispondi nel terminale: va chiusa la richiesta (perm-close) e il hook esce vuoto.
	// reason: terminal oppure session (SessionEnd). Chiamata senza e.mu.
	OnHookClosed func(id, reason string)
	// OnGame (facoltativo): eventi del gioco della mascotte tradotti dagli hook (gamesvc.Service.Apply).
	// Chiamata senza e.mu, nell'ordine degli hook.
	OnGame func(game.Event)

	mu     sync.Mutex
	gameq  []game.Event // eventi del gioco da passare a OnGame dopo aver lasciato e.mu
	sess   map[string]*session
	hookq  []string // id delle richieste da chiudere (OnHookClosed) dopo aver lasciato e.mu
	seq    int      // ordine di creazione
	today  todayStats
	limits limitsState
	logq   []state.LogEntry // voci del log da aggiungere allo Store al prossimo sync
	dirty  bool             // Oggi da salvare
	saved  time.Time
}

type perm struct {
	agent  string // "" = agente principale
	tool   string // "" = da Notification permission_prompt, strumento sconosciuto
	useID  string
	detail string
	hook   bool   // un `calamaio hook --wait` aspetta la risposta (M5)
	hookID string // id della richiesta sul display (display.Hub)
}

type task struct {
	id     string
	title  string
	status int
}

type session struct {
	id, cwd, transcript, title string
	order                      int

	st          state.Session // campi del protocollo (Name calcolato in sync)
	prevState   string        // stato prima di compact
	prevDetail  string
	pending     []perm
	question    bool      // AskUserQuestion, ExitPlanMode, elicitation: risposta solo dal terminale
	lastResolve time.Time // ultimo permesso risolto (per le notifiche in ritardo)
	lastPre     map[string]preTool
	agents      map[string]bool
	tasks       []task
	testUseID   string
	lastTest    string // esito dell'ultimo test finito (ok, ko): da ko a ok è un test_green

	hasBase          bool
	baseCost         float64
	baseAdd, baseDel int
	events           int // hook ricevuti (le status line non contano)
	checkAt          time.Time
	checkEvents      int
	endAt            time.Time
	termDetail       string // permesso appena risposto nel terminale (hook chiuso): se era un no, «Negato: …»
}

type preTool struct{ useID, detail string }

// todayStats: Oggi, con la somma delle attese per la media.
type todayStats struct {
	Day     string      `json:"day"` // AAAA-MM-GG locale
	Today   state.Today `json:"today"`
	WaitSum int64       `json:"waitSumMs"`
	WaitN   int64       `json:"waitN"`
	// Seen: sessioni con almeno un evento oggi (per il riepilogo di `bye`), al massimo maxSeen.
	Seen map[string]bool `json:"seen,omitempty"`
}

// maxSeen: oltre, le sessioni nuove non si contano più (difesa da un file oggi.json enorme).
const maxSeen = 1000

type window struct {
	pct   int
	reset int64 // ms epoch
}

// Spend: rate_limits.spend_limit (gateway con limite di spesa). Il protocollo v1 non ha un campo
// per mandarlo ai display: si tiene e si mostra in /debug.
type Spend struct {
	UsedPct  *float64 `json:"used_percentage,omitempty"`
	ResetsAt *int64   `json:"resets_at,omitempty"` // ms epoch
	UsedUSD  *float64 `json:"used_usd,omitempty"`
	LimitUSD *float64 `json:"limit_usd,omitempty"`
	Period   string   `json:"period,omitempty"`
}

type limitsState struct {
	h5, d7 *window
	spend  *Spend
}

func New(store *state.Store) *Engine {
	return &Engine{
		Store:    store,
		Now:      time.Now,
		TestRe:   regexp.MustCompile(DefaultTestRegex),
		Grace:    DefaultGrace,
		EndDelay: DefaultEndDelay,
		ReadTail: ReadTail,
		Log:      log.New(discard{}, "", 0),
		sess:     map[string]*session{},
	}
}

type discard struct{}

func (discard) Write(p []byte) (int, error) { return len(p), nil }

// payload: i campi che servono, da tutti gli eventi (quelli assenti restano vuoti).
type payload struct {
	SessionID      string          `json:"session_id"`
	TranscriptPath string          `json:"transcript_path"`
	Cwd            string          `json:"cwd"`
	Event          string          `json:"hook_event_name"`
	AgentID        string          `json:"agent_id"`
	AgentType      string          `json:"agent_type"`
	Source         string          `json:"source"`
	Prompt         string          `json:"prompt"`
	ToolName       string          `json:"tool_name"`
	ToolInput      json.RawMessage `json:"tool_input"`
	ToolUseID      string          `json:"tool_use_id"`
	Error          string          `json:"error"`
	ErrorDetails   string          `json:"error_details"`
	IsInterrupt    bool            `json:"is_interrupt"`
	NotifType      string          `json:"notification_type"`
	LastMessage    string          `json:"last_assistant_message"`
	Background     *[]struct {
		ID     string `json:"id"`
		Type   string `json:"type"`
		Status string `json:"status"`
	} `json:"background_tasks"`
	TaskID      string `json:"task_id"`
	TaskSubject string `json:"task_subject"`
	PromptID    string `json:"prompt_id"`
}

// Hook applica un payload di hook. I payload non validi si ignorano.
func (e *Engine) Hook(body []byte) {
	var p payload
	if json.Unmarshal(body, &p) != nil || p.SessionID == "" || p.Event == "" {
		return
	}
	e.mu.Lock()
	now := e.Now()
	e.rollDay(now)
	s := e.ensure(p.SessionID, now)
	if p.Cwd != "" {
		s.cwd = p.Cwd
	}
	if p.TranscriptPath != "" {
		s.transcript = p.TranscriptPath
	}
	s.events++
	s.termDetail = "" // Claude è andato avanti: il permesso era concesso
	e.gameEvent(s, game.KindActivity, "", now)
	e.apply(s, &p, now)
	closed := e.takeHookq()
	gq := e.gameq
	e.gameq = nil
	e.mu.Unlock()
	if e.OnGame != nil {
		for _, ev := range gq {
			e.OnGame(ev)
		}
	}
	reason := "terminal"
	if p.Event == "SessionEnd" {
		reason = "session"
	}
	e.closeHooks(closed, reason)
	e.sync()
}

// gameEvent accoda un evento del gioco (con e.mu). key: identità dell'evento per non contarlo due
// volte, anche dopo un riavvio del servizio (vuota per activity).
func (e *Engine) gameEvent(s *session, kind, key string, now time.Time) {
	if e.OnGame == nil {
		return
	}
	e.gameq = append(e.gameq, game.Event{At: now, Session: s.id, Kind: kind, Key: key})
}

// noteDropped: un permesso tolto dalla coda. Se un hook lo aspettava, la richiesta va chiusa. Con e.mu.
func (e *Engine) noteDropped(pm perm) {
	if pm.hook && pm.hookID != "" {
		e.hookq = append(e.hookq, pm.hookID)
	}
}

func (e *Engine) takeHookq() []string {
	q := e.hookq
	e.hookq = nil
	return q
}

func (e *Engine) closeHooks(ids []string, reason string) {
	if e.OnHookClosed == nil {
		return
	}
	for _, id := range ids {
		e.OnHookClosed(id, reason)
	}
}

func (e *Engine) ensure(id string, now time.Time) *session {
	s := e.sess[id]
	if s == nil {
		e.seq++
		s = &session{id: id, order: e.seq, lastPre: map[string]preTool{}, agents: map[string]bool{}}
		s.st = state.Session{ID: id, State: state.Idle, Since: now.UnixMilli(), Tasks: []state.Task{}}
		e.sess[id] = s
	}
	e.seen(id)
	return s
}

// seen conta la sessione tra quelle di oggi (Hook e StatusLine hanno già chiamato rollDay).
func (e *Engine) seen(id string) {
	if e.today.Seen[id] || len(e.today.Seen) >= maxSeen {
		return
	}
	if e.today.Seen == nil {
		e.today.Seen = map[string]bool{}
	}
	e.today.Seen[id] = true
	e.dirty = true
}

// Day restituisce il giorno locale di Oggi (AAAA-MM-GG) e quante sessioni hanno avuto almeno un
// evento oggi. Per il riepilogo di `bye`.
func (e *Engine) Day() (string, int) {
	e.mu.Lock()
	defer e.mu.Unlock()
	// senza toccare Oggi: a cambiare giorno (e avvisare lo Store) ci pensa Tick
	if day := e.Now().Format("2006-01-02"); day != e.today.Day {
		return day, 0
	}
	return e.today.Day, len(e.today.Seen)
}

func (e *Engine) setState(s *session, st string, now time.Time) {
	if s.st.State != st {
		s.st.State = st
		s.st.Since = now.UnixMilli()
	}
	if st != state.Permission && st != state.Input {
		s.st.AttnAt = nil
	}
	if st != state.Error {
		s.st.Error = ""
	}
}

func (e *Engine) attention(s *session, now time.Time) {
	if s.st.AttnAt == nil {
		t := now.UnixMilli()
		s.st.AttnAt = &t
	}
}

func (e *Engine) logf(s *session, now time.Time, text string) {
	e.logq = append(e.logq, state.LogEntry{T: now.UnixMilli(), Name: e.nameOf(s), Text: text})
}

func (e *Engine) apply(s *session, p *payload, now time.Time) {
	main := p.AgentID == ""
	switch p.Event {
	case "SessionStart":
		s.endAt = time.Time{}
		switch p.Source {
		case "compact":
			// arriva tra PreCompact e PostCompact: non è una sessione nuova
		case "clear":
			for _, pm := range s.pending {
				e.noteDropped(pm)
			}
			s.tasks, s.st.Summary, s.st.Test, s.pending, s.question = nil, "", nil, nil, false
			s.st.Detail = ""
			e.setState(s, state.Idle, now)
		default:
			if s.events == 1 {
				e.logf(s, now, "sessione aperta")
			}
		}

	case "UserPromptSubmit":
		e.resolveAll(s, now, false)
		s.question = false
		s.st.Summary = ""
		if strings.HasPrefix(strings.TrimSpace(p.Prompt), "<task-notification>") {
			// prompt di sistema: un subagente in background ha finito
			s.st.Detail = "Risultato del subagente"
			e.logf(s, now, "legge il risultato del subagente")
		} else {
			s.st.Detail = "Prompt ricevuto"
			e.logf(s, now, "nuovo prompt")
		}
		e.setState(s, state.Working, now)

	case "PreToolUse":
		d := Detail(p.ToolName, p.ToolInput, s.cwd)
		s.lastPre[p.AgentID+"|"+p.ToolName] = preTool{useID: p.ToolUseID, detail: d}
		if p.ToolName == "Bash" && e.isTest(p.ToolInput) {
			s.testUseID = p.ToolUseID
			v := "run"
			s.st.Test = &v
		}
		// I permessi dello stesso agente sono in fila: se parte un altro strumento, quello in
		// attesa è stato risolto (anche «No, e di' a Claude cosa fare»: Claude prosegue).
		if e.dropAgent(s, p.AgentID) {
			e.countResolved(s, now)
			e.logf(s, now, "risposto dal terminale")
			if !main {
				e.afterResolve(s, now)
			}
		}
		if !main {
			return // i passi dei subagenti non cambiano lo stato della sessione
		}
		e.tasksFromTool(s, p, now)
		if isQuestion(p.ToolName) {
			e.ask(s, d, now)
			return
		}
		if len(s.pending) > 0 || s.question {
			return // un permesso o una domanda aspettano ancora: restano in primo piano
		}
		s.st.Detail = d
		e.setState(s, state.Working, now)

	case "PermissionRequest":
		pre := s.lastPre[p.AgentID+"|"+p.ToolName]
		d := pre.detail
		if d == "" {
			d = Detail(p.ToolName, p.ToolInput, s.cwd)
		}
		if isQuestion(p.ToolName) {
			// brief §7 regola 4: le domande non sono permessi, mai dal display
			e.ask(s, d, now)
			return
		}
		s.pending = append(s.pending, perm{agent: p.AgentID, tool: p.ToolName, useID: pre.useID, detail: d})
		s.st.Detail = d
		e.setState(s, state.Permission, now)
		e.attention(s, now)
		e.logf(s, now, "chiede permesso: "+shortCmd(d))

	case "PermissionDenied":
		// solo modalità auto: Claude prosegue da solo, non è una risposta dell'utente
		if e.drop(s, p.AgentID, p.ToolName, p.ToolUseID) {
			e.logf(s, now, "negato in automatico")
			e.afterResolve(s, now)
		}

	case "PostToolUse", "PostToolUseFailure":
		if e.drop(s, p.AgentID, p.ToolName, p.ToolUseID) {
			e.countResolved(s, now)
			e.logf(s, now, "risposto dal terminale")
		}
		if p.ToolUseID != "" && p.ToolUseID == s.testUseID {
			s.testUseID = ""
			v := "ok"
			if p.Event == "PostToolUseFailure" {
				v = "ko"
				if p.IsInterrupt {
					v = ""
				}
			}
			if v == "" {
				s.st.Test = nil
			} else {
				s.st.Test = &v
				e.logf(s, now, map[string]string{"ok": "test riusciti", "ko": "test falliti"}[v])
				kind := game.KindTestFail
				if v == "ok" {
					kind = game.KindTestOK
					if s.lastTest == "ko" {
						kind = game.KindTestGreen
					}
				}
				e.gameEvent(s, kind, "test|"+s.id+"|"+p.ToolUseID, now)
				s.lastTest = v
			}
		}
		if main && isQuestion(p.ToolName) && s.question {
			s.question = false
			e.countWait(now, s.st.AttnAt)
			e.logf(s, now, "hai risposto")
		}
		if main || len(s.pending) == 0 {
			e.afterResolve(s, now)
		}

	case "Notification":
		switch p.NotifType {
		case "permission_prompt":
			// Arriva anche per AskUserQuestion: se c'è già una domanda o un permesso, niente.
			if s.st.State == state.Permission || s.st.State == state.Input || s.question {
				return
			}
			if !s.lastResolve.IsZero() && now.Sub(s.lastResolve) < lateNotify {
				return // notifica di un permesso già risolto
			}
			s.pending = append(s.pending, perm{detail: s.st.Detail})
			e.setState(s, state.Permission, now)
			e.attention(s, now)
			e.logf(s, now, "chiede permesso: "+shortCmd(s.st.Detail))
		case "idle_prompt":
			// Arriva sempre ~60 s dopo la fine di ogni risposta: vuol dire solo «aspetta il prossimo
			// prompt». La sessione resta FINITA (o com'era): non diventa ATTENDE TE. Vedi DECISIONI.
			return
		case "elicitation_dialog":
			e.ask(s, "Domanda da un server MCP", now)
		}

	case "Elicitation":
		e.ask(s, "Domanda da un server MCP", now)

	case "ElicitationResult":
		if s.question {
			s.question = false
			e.countWait(now, s.st.AttnAt)
			e.afterResolve(s, now)
		}

	case "SubagentStart":
		if p.AgentID != "" && !s.agents[p.AgentID] {
			s.agents[p.AgentID] = true
			e.logf(s, now, "subagente avviato")
		}

	case "SubagentStop":
		// Ne arrivano anche senza SubagentStart (agenti interni di Claude Code, agent_type
		// vuoto): si ignorano.
		if s.agents[p.AgentID] {
			delete(s.agents, p.AgentID)
			e.logf(s, now, "subagente finito")
		}

	case "TaskCreated":
		e.upsertTask(s, p.TaskID, p.TaskSubject, state.TaskTodo)

	case "TaskCompleted":
		if t := e.upsertTask(s, p.TaskID, p.TaskSubject, -1); t != nil {
			e.completeTask(s, t, now)
			e.gameEvent(s, game.KindTaskDone, "task|"+s.id+"|"+p.TaskID, now)
		}

	case "PreCompact":
		if s.st.State != state.Compact {
			s.prevState, s.prevDetail = s.st.State, s.st.Detail
		}
		s.st.Detail = "Compatta il contesto"
		e.setState(s, state.Compact, now)
		e.logf(s, now, "riordina la memoria")

	case "PostCompact":
		if s.st.State == state.Compact {
			prev := s.prevState
			if prev == "" || prev == state.Compact {
				prev = state.Idle
			}
			s.st.Detail = s.prevDetail
			e.setState(s, prev, now)
			if prev == state.Permission || prev == state.Input {
				e.attention(s, now)
			}
		}

	case "Stop":
		if !main {
			return
		}
		e.resolveAll(s, now, false)
		s.question = false
		s.st.Summary = Summary(p.LastMessage)
		if s.st.Summary != "" {
			e.gameEvent(s, game.KindTurnDone, "turn|"+s.id+"|"+turnKey(p), now)
		}
		if p.Background != nil {
			// riallinea i subagenti con quelli ancora in esecuzione in background
			still := map[string]bool{}
			for _, b := range *p.Background {
				if b.Type == "subagent" && b.Status == "running" && s.agents[b.ID] {
					still[b.ID] = true
				}
			}
			s.agents = still
		}
		e.setState(s, state.Done, now)
		e.logf(s, now, "turno completato")

	case "StopFailure":
		e.resolveAll(s, now, false)
		s.question = false
		code := strings.TrimSpace(p.Error)
		if code == "" {
			code = "unknown"
		}
		s.st.Summary = ""
		s.st.Detail = clean("StopFailure: "+code, state.MaxDetail)
		e.setState(s, state.Error, now)
		s.st.Error = code
		e.logf(s, now, "errore "+code)

	case "SessionEnd":
		if s.st.State == state.Done {
			// sessione chiusa finita (la stessa chiave dell'archiviazione: conta una volta sola)
			e.gameEvent(s, game.KindSessionClosed, ClosedKey(s.id), now)
		}
		e.resolveAll(s, now, false)
		s.question = false
		s.agents = map[string]bool{}
		s.st.Detail = "Sessione chiusa"
		e.setState(s, state.Idle, now)
		s.endAt = now.Add(e.EndDelay)
		e.logf(s, now, "sessione chiusa")
	}
}

// ClosedKey: chiave dell'evento session_closed di una sessione (SessionEnd o archiviazione).
func ClosedKey(session string) string { return "closed|" + session }

// turnKey: identità di un turno: prompt_id (Claude Code ≥ 2.1) o, senza, l'impronta del messaggio finale.
func turnKey(p *payload) string {
	if p.PromptID != "" {
		return p.PromptID
	}
	return shortHash(p.LastMessage)
}

func shortHash(s string) string {
	h := sha256.Sum256([]byte(s))
	return hex.EncodeToString(h[:8])
}

func isQuestion(tool string) bool { return tool == "AskUserQuestion" || tool == "ExitPlanMode" }

// ask: Claude aspetta una risposta che si dà solo nel terminale (stato input).
func (e *Engine) ask(s *session, detail string, now time.Time) {
	if s.question && s.st.State == state.Input {
		return
	}
	s.question = true
	s.st.Detail = detail
	e.setState(s, state.Input, now)
	e.attention(s, now)
	e.logf(s, now, "ti ha fatto una domanda")
}

// drop toglie il permesso in attesa che corrisponde all'evento; dice se ce n'era uno.
func (e *Engine) drop(s *session, agent, tool, useID string) bool {
	for i, pm := range s.pending {
		if pm.agent != agent {
			continue
		}
		if (useID != "" && pm.useID == useID) || pm.tool == tool || pm.tool == "" {
			e.noteDropped(pm)
			s.pending = append(s.pending[:i], s.pending[i+1:]...)
			return true
		}
	}
	return false
}

// dropAgent toglie i permessi in attesa di un agente; dice se ce n'erano.
func (e *Engine) dropAgent(s *session, agent string) bool {
	kept := s.pending[:0]
	found := false
	for _, pm := range s.pending {
		if pm.agent == agent {
			found = true
			e.noteDropped(pm)
			continue
		}
		kept = append(kept, pm)
	}
	s.pending = kept
	return found
}

// countResolved: un permesso risolto nel terminale (in M4 non ci sono ancora risposte dal display).
func (e *Engine) countResolved(s *session, now time.Time) { e.countResolvedBy(s, now, false) }

func (e *Engine) countWait(now time.Time, attn *int64) {
	if attn == nil {
		return
	}
	if w := now.UnixMilli() - *attn; w >= 0 {
		e.today.WaitSum += w
		e.today.WaitN++
		e.today.Today.WaitAvgMs = e.today.WaitSum / e.today.WaitN
		e.dirty = true
	}
}

// resolveAll chiude tutti i permessi in attesa (nuovo prompt, fine turno): sono stati risolti
// nel terminale anche se non lo abbiamo visto.
func (e *Engine) resolveAll(s *session, now time.Time, _ bool) {
	if len(s.pending) == 0 {
		return
	}
	n := len(s.pending)
	for _, pm := range s.pending {
		e.noteDropped(pm)
	}
	s.pending = nil
	for i := 0; i < n; i++ {
		e.today.Today.PermTerminal++
	}
	s.lastResolve = now
	e.countWait(now, s.st.AttnAt)
	e.dirty = true
}

// afterResolve: senza più permessi né domande la sessione torna a lavorare.
func (e *Engine) afterResolve(s *session, now time.Time) {
	if len(s.pending) > 0 {
		s.st.Detail = s.pending[len(s.pending)-1].detail
		return
	}
	if s.question {
		return
	}
	switch s.st.State {
	case state.Permission, state.Input, state.Idle:
		e.setState(s, state.Working, now)
	}
}

func (e *Engine) isTest(input json.RawMessage) bool {
	var in struct {
		Command string `json:"command"`
	}
	if json.Unmarshal(input, &in) != nil || e.TestRe == nil {
		return false
	}
	return e.TestRe.MatchString(in.Command)
}

// tasksFromTool: TaskUpdate (stato) e TodoWrite (lista intera) da PreToolUse.
func (e *Engine) tasksFromTool(s *session, p *payload, now time.Time) {
	switch p.ToolName {
	case "TaskUpdate":
		var in struct {
			ID      string `json:"taskId"`
			Status  string `json:"status"`
			Subject string `json:"subject"`
		}
		if json.Unmarshal(p.ToolInput, &in) != nil || in.ID == "" {
			return
		}
		switch in.Status {
		case "in_progress":
			if t := e.upsertTask(s, in.ID, in.Subject, state.TaskDoing); t != nil && in.Subject != "" {
				t.title = in.Subject
			}
		case "deleted":
			for i, t := range s.tasks {
				if t.id == in.ID {
					s.tasks = append(s.tasks[:i], s.tasks[i+1:]...)
					break
				}
			}
		}
		// completed: lo conta TaskCompleted, che arriva subito dopo
	case "TodoWrite":
		var in struct {
			Todos []struct {
				Content string `json:"content"`
				Status  string `json:"status"`
			} `json:"todos"`
		}
		if json.Unmarshal(p.ToolInput, &in) != nil {
			return
		}
		old := map[string]int{}
		for _, t := range s.tasks {
			old[t.title] = t.status
		}
		s.tasks = s.tasks[:0]
		for i, td := range in.Todos {
			st := state.TaskTodo
			switch td.Status {
			case "in_progress":
				st = state.TaskDoing
			case "completed":
				st = state.TaskDoneSt
			}
			t := task{id: "todo-" + itoa(i), title: td.Content, status: st}
			s.tasks = append(s.tasks, t)
			if prev, ok := old[td.Content]; st == state.TaskDoneSt && (!ok || prev != state.TaskDoneSt) {
				e.today.Today.Tasks++
				e.dirty = true
				e.gameEvent(s, game.KindTaskDone, "todo|"+s.id+"|"+shortHash(td.Content), now)
			}
		}
	}
}

// upsertTask trova (o crea) un task per id; status < 0 lascia lo stato com'è.
func (e *Engine) upsertTask(s *session, id, title string, status int) *task {
	if id == "" {
		return nil
	}
	for i := range s.tasks {
		if s.tasks[i].id == id {
			if title != "" {
				s.tasks[i].title = title
			}
			if status >= 0 {
				s.tasks[i].status = status
			}
			return &s.tasks[i]
		}
	}
	if status < 0 {
		status = state.TaskTodo
	}
	s.tasks = append(s.tasks, task{id: id, title: title, status: status})
	return &s.tasks[len(s.tasks)-1]
}

func (e *Engine) completeTask(s *session, t *task, now time.Time) {
	if t.status == state.TaskDoneSt {
		return
	}
	t.status = state.TaskDoneSt
	e.today.Today.Tasks++
	e.dirty = true
	done := 0
	for _, x := range s.tasks {
		if x.status == state.TaskDoneSt {
			done++
		}
	}
	e.logf(s, now, "task "+itoa(done)+"/"+itoa(len(s.tasks))+" completato")
}

// statusPayload: i campi della status line che servono.
type statusPayload struct {
	SessionID   string `json:"session_id"`
	SessionName string `json:"session_name"`
	Transcript  string `json:"transcript_path"`
	Cwd         string `json:"cwd"`
	Workspace   struct {
		CurrentDir string `json:"current_dir"`
	} `json:"workspace"`
	Cost struct {
		USD     *float64 `json:"total_cost_usd"`
		Added   *int     `json:"total_lines_added"`
		Removed *int     `json:"total_lines_removed"`
	} `json:"cost"`
	Context struct {
		Used *float64 `json:"used_percentage"`
	} `json:"context_window"`
	Rate *struct {
		Five  *rateWindow `json:"five_hour"`
		Seven *rateWindow `json:"seven_day"`
		Spend *struct {
			Used     *float64 `json:"used_percentage"`
			ResetsAt *float64 `json:"resets_at"`
			UsedUSD  *float64 `json:"used_usd"`
			LimitUSD *float64 `json:"limit_usd"`
			Period   string   `json:"period"`
		} `json:"spend_limit"`
	} `json:"rate_limits"`
}

type rateWindow struct {
	Used     *float64 `json:"used_percentage"`
	ResetsAt *float64 `json:"resets_at"` // secondi epoch
}

// StatusLine applica un payload della status line.
func (e *Engine) StatusLine(body []byte) {
	var p statusPayload
	if json.Unmarshal(body, &p) != nil || p.SessionID == "" {
		return
	}
	e.mu.Lock()
	now := e.Now()
	e.rollDay(now)
	s := e.ensure(p.SessionID, now)
	if d := p.Workspace.CurrentDir; d != "" {
		s.cwd = d
	} else if p.Cwd != "" {
		s.cwd = p.Cwd
	}
	if p.Transcript != "" {
		s.transcript = p.Transcript
	}
	s.title = p.SessionName
	if p.Context.Used != nil {
		v := clampPct(*p.Context.Used, 100)
		s.st.Ctx = &v
	} else {
		s.st.Ctx = nil
	}
	e.costs(s, p)
	e.rate(p, now)
	// La status line si nasconde durante le richieste di permesso e torna quando si chiudono:
	// se nessun hook segue a breve, la risposta è stata data nel terminale (vedi check).
	if (s.st.State == state.Working || s.st.State == state.Permission) && s.endAt.IsZero() {
		s.checkAt = now.Add(e.Grace)
		s.checkEvents = s.events
	}
	e.mu.Unlock()
	e.sync()
}

// costs: Oggi somma i delta di ogni sessione, non i totali. Il primo valore visto fa da base
// (nelle catture la prima status line ha costo 0).
func (e *Engine) costs(s *session, p statusPayload) {
	c := p.Cost
	if c.USD == nil && c.Added == nil && c.Removed == nil {
		return
	}
	usd, add, del := deref(c.USD), derefI(c.Added), derefI(c.Removed)
	if !s.hasBase || usd < s.baseCost || add < s.baseAdd || del < s.baseDel {
		s.hasBase, s.baseCost, s.baseAdd, s.baseDel = true, usd, add, del
		return
	}
	t := &e.today.Today
	t.Cost += usd - s.baseCost
	t.LinesAdd += add - s.baseAdd
	t.LinesDel += del - s.baseDel
	t.Cost = math.Round(t.Cost*10000) / 10000
	if usd != s.baseCost || add != s.baseAdd || del != s.baseDel {
		e.dirty = true
	}
	s.baseCost, s.baseAdd, s.baseDel = usd, add, del
}

// rate: i limiti sono dell'account. Si aggiorna ogni finestra presente; quelle assenti restano
// finché non scade il loro resets_at (Claude Code le toglie proprio allora).
func (e *Engine) rate(p statusPayload, now time.Time) {
	if p.Rate == nil {
		return
	}
	set := func(dst **window, w *rateWindow) {
		if w == nil || w.Used == nil || w.ResetsAt == nil {
			return
		}
		*dst = &window{pct: clampPct(*w.Used, 1000), reset: int64(*w.ResetsAt * 1000)}
	}
	set(&e.limits.h5, p.Rate.Five)
	set(&e.limits.d7, p.Rate.Seven)
	if sp := p.Rate.Spend; sp != nil {
		out := &Spend{UsedPct: sp.Used, UsedUSD: sp.UsedUSD, LimitUSD: sp.LimitUSD, Period: sp.Period}
		if sp.ResetsAt != nil {
			v := int64(*sp.ResetsAt * 1000)
			out.ResetsAt = &v
		}
		e.limits.spend = out
	}
	e.expireLimits(now)
}

func (e *Engine) expireLimits(now time.Time) bool {
	ms := now.UnixMilli()
	changed := false
	if e.limits.h5 != nil && e.limits.h5.reset <= ms {
		e.limits.h5, changed = nil, true
	}
	if e.limits.d7 != nil && e.limits.d7.reset <= ms {
		e.limits.d7, changed = nil, true
	}
	if sp := e.limits.spend; sp != nil && sp.ResetsAt != nil && *sp.ResetsAt <= ms {
		e.limits.spend, changed = nil, true
	}
	return changed
}

// Tick fa scattare i controlli a tempo: risposta ai permessi vista nel transcript, rimozione
// dopo SessionEnd, mezzanotte, scadenza dei limiti, salvataggio di Oggi.
func (e *Engine) Tick() {
	e.mu.Lock()
	now := e.Now()
	changed := e.rollDay(now)
	if e.expireLimits(now) {
		changed = true
	}
	for id, s := range e.sess {
		if !s.endAt.IsZero() && !now.Before(s.endAt) {
			delete(e.sess, id)
			changed = true
			continue
		}
		if !s.checkAt.IsZero() && !now.Before(s.checkAt) {
			s.checkAt = time.Time{}
			if s.events == s.checkEvents && e.check(s, now) {
				changed = true
			}
		}
	}
	save := e.dirty && e.TodayFile != "" && now.Sub(e.saved) >= 10*time.Second
	var snap todayStats
	if save {
		snap, e.dirty, e.saved = e.today, false, now
	}
	closed := e.takeHookq()
	e.mu.Unlock()
	e.closeHooks(closed, "terminal")
	if save {
		if err := saveToday(e.TodayFile, snap); err != nil {
			e.Log.Printf("oggi: %v", err)
		}
	}
	if changed {
		e.sync()
	}
}

// check: una status line è arrivata mentre la sessione lavorava o chiedeva un permesso, e poi
// nessun hook. Se il transcript finisce con l'interruzione dell'utente il turno è finito
// (permesso negato o Esc); altrimenti un permesso dell'agente principale è stato concesso.
func (e *Engine) check(s *session, now time.Time) bool {
	mainOnly := len(s.pending) > 0
	for _, pm := range s.pending {
		if pm.agent != "" {
			mainOnly = false
		}
	}
	if s.st.State == state.Permission && !mainOnly {
		return false // un subagente aspetta: la status line può venire dall'agente principale
	}
	waiting := false
	for _, pm := range s.pending {
		waiting = waiting || pm.hook
	}
	var interrupted bool
	if s.transcript != "" && e.ReadTail != nil {
		if tail, err := e.ReadTail(s.transcript); err == nil {
			interrupted, _ = Interrupted(tail)
		}
	}
	if waiting && !interrupted {
		// Con un hook in attesa la concessione arriva da PostToolUse: una status line da sola non basta
		// per chiudere la richiesta sul display. Il diniego invece sì (transcript interrotto).
		return false
	}
	switch {
	case s.st.State == state.Permission && interrupted:
		d := s.st.Detail
		e.resolveAll(s, now, true)
		s.st.Detail = clean("Negato: "+d, state.MaxDetail)
		e.setState(s, state.Idle, now)
		e.logf(s, now, "permesso negato")
		return true
	case s.st.State == state.Permission:
		e.resolveAll(s, now, false)
		e.setState(s, state.Working, now)
		e.logf(s, now, "risposto dal terminale")
		return true
	case s.st.State == state.Working && interrupted && s.termDetail != "":
		s.st.Detail = clean("Negato: "+s.termDetail, state.MaxDetail)
		s.termDetail = ""
		e.setState(s, state.Idle, now)
		e.logf(s, now, "permesso negato")
		return true
	case s.st.State == state.Working && interrupted:
		s.st.Detail = "Interrotto"
		e.setState(s, state.Idle, now)
		e.logf(s, now, "interrotto")
		return true
	}
	return false
}

// rollDay azzera Oggi a mezzanotte (ora locale).
func (e *Engine) rollDay(now time.Time) bool {
	day := now.Format("2006-01-02")
	if e.today.Day == day {
		return false
	}
	first := e.today.Day == ""
	e.today = todayStats{Day: day}
	e.dirty = !first // al primo avvio non c'è niente da salvare
	return !first
}

// LoadToday riprende Oggi dal file, se è dello stesso giorno (riavvio del servizio).
func (e *Engine) LoadToday() {
	if e.TodayFile == "" {
		return
	}
	t, err := loadToday(e.TodayFile)
	if err != nil {
		return
	}
	e.mu.Lock()
	if t.Day == e.Now().Format("2006-01-02") {
		e.today = t
	}
	e.mu.Unlock()
	e.sync()
}

// Run chiama Tick ogni secondo finché ctx non si chiude, poi salva Oggi.
func (e *Engine) Run(ctx context.Context) {
	tk := time.NewTicker(time.Second)
	defer tk.Stop()
	for {
		select {
		case <-ctx.Done():
			e.mu.Lock()
			snap, dirty := e.today, e.dirty
			e.mu.Unlock()
			if dirty && e.TodayFile != "" {
				_ = saveToday(e.TodayFile, snap)
			}
			return
		case <-tk.C:
			e.Tick()
		}
	}
}

// Debug: ciò che non va ai display ma serve a capire (GET /debug).
func (e *Engine) Debug() map[string]any {
	e.mu.Lock()
	defer e.mu.Unlock()
	out := map[string]any{"sessions": len(e.sess), "today_day": e.today.Day}
	if e.limits.spend != nil {
		out["spend_limit"] = *e.limits.spend
	}
	pend := map[string]int{}
	for id, s := range e.sess {
		if len(s.pending) > 0 {
			pend[id] = len(s.pending)
		}
	}
	out["pending_permissions"] = pend
	return out
}

// sync scrive nello Store sessioni, limiti, Oggi e le nuove voci del log.
func (e *Engine) sync() {
	e.mu.Lock()
	list := make([]*session, 0, len(e.sess))
	for _, s := range e.sess {
		list = append(list, s)
	}
	sortByOrder(list)
	names := e.names(list)
	sessions := make([]state.Session, 0, len(list))
	for _, s := range list {
		ss := s.st
		ss.Name = names[s.id]
		ss.Cwd = s.cwd // per l'avatar del progetto; non va ai display
		ss.Subs = len(s.agents)
		ss.Tasks = make([]state.Task, 0, len(s.tasks))
		for _, t := range s.tasks {
			ss.Tasks = append(ss.Tasks, state.Task{Title: t.title, Status: t.status})
		}
		if len(ss.Tasks) > state.MaxTasks {
			ss.Tasks = keepTasks(ss.Tasks)
		}
		sessions = append(sessions, ss)
	}
	var lim *state.Limits
	if e.limits.h5 != nil || e.limits.d7 != nil {
		lim = &state.Limits{}
		if w := e.limits.h5; w != nil {
			lim.H5, lim.H5Reset = intp(w.pct), int64p(w.reset)
		}
		if w := e.limits.d7; w != nil {
			lim.D7, lim.D7Reset = intp(w.pct), int64p(w.reset)
		}
	}
	today := e.today.Today
	logq := e.logq
	e.logq = nil
	e.mu.Unlock()
	e.Store.Update(func(s *state.Snapshot) {
		s.Sessions = sessions
		s.Limits = lim
		s.Today = today
		for _, l := range logq {
			s.AddLog(l.T, l.Name, l.Text)
		}
	})
}

// keepTasks: oltre MaxTasks si tengono quelli in corso e da fare, poi gli ultimi fatti.
func keepTasks(ts []state.Task) []state.Task {
	open := []state.Task{}
	done := []state.Task{}
	for _, t := range ts {
		if t.Status == state.TaskDoneSt {
			done = append(done, t)
		} else {
			open = append(open, t)
		}
	}
	if len(open) >= state.MaxTasks {
		return open[:state.MaxTasks]
	}
	room := state.MaxTasks - len(open)
	if len(done) > room {
		done = done[len(done)-room:]
	}
	return append(done, open...)
}

// nameOf: il nome della sessione (con e.mu).
func (e *Engine) nameOf(s *session) string {
	list := make([]*session, 0, len(e.sess))
	for _, x := range e.sess {
		list = append(list, x)
	}
	return e.names(list)[s.id]
}

// names: session_name della status line, altrimenti la cartella di cwd; sui doppioni un suffisso
// breve dall'id (come il nome predefinito di Claude Code, «my-app-3f»).
func (e *Engine) names(list []*session) map[string]string {
	base := map[string]string{}
	count := map[string]int{}
	for _, s := range list {
		b := strings.TrimSpace(s.title)
		if b == "" {
			b = path.Base(strings.TrimRight(strings.ReplaceAll(s.cwd, "\\", "/"), "/"))
		}
		if b == "" || b == "." || b == "/" {
			b = "sessione"
		}
		b = clean(b, state.MaxName)
		base[s.id] = b
		count[b]++
	}
	out := map[string]string{}
	for _, s := range list {
		b := base[s.id]
		if count[b] < 2 {
			out[s.id] = b
			continue
		}
		suf := "-" + shortID(s.id, 2)
		for _, o := range list {
			if o.id != s.id && base[o.id] == b && shortID(o.id, 2) == shortID(s.id, 2) {
				suf = "-" + shortID(s.id, 4)
			}
		}
		out[s.id] = state.Truncate(b, state.MaxName-len(suf)) + suf
	}
	return out
}

func shortID(id string, n int) string {
	id = strings.ReplaceAll(id, "-", "")
	if len(id) > n {
		return id[:n]
	}
	return id
}

func sortByOrder(l []*session) {
	for i := 1; i < len(l); i++ {
		for j := i; j > 0 && l[j].order < l[j-1].order; j-- {
			l[j], l[j-1] = l[j-1], l[j]
		}
	}
}

func clampPct(v float64, max int) int {
	n := int(math.Round(v))
	if n < 0 {
		return 0
	}
	if n > max {
		return max
	}
	return n
}

func deref(p *float64) float64 {
	if p == nil {
		return 0
	}
	return *p
}

func derefI(p *int) int {
	if p == nil {
		return 0
	}
	return *p
}

func intp(n int) *int       { return &n }
func int64p(n int64) *int64 { return &n }
func itoa(n int) string     { return strconv.Itoa(n) }
