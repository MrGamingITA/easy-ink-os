package api

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"path"
	"strings"

	"calamaio/internal/display"
	"calamaio/internal/sessions"
)

// M5: abbinamento dalla webapp e da `calamaio pair`, richieste di permesso dal hook `--wait`.

// PairingAPI: abbinamento (display.Hub).
type PairingAPI interface {
	PairStart(board string) (*display.PairingStatus, error)
	PairCode(board, code string) (*display.PairingStatus, error)
	PairCancel(board string) (*display.PairingStatus, error)
	Unpair(board string) (bool, error)
	PairedBoards() []string
}

// Permissions: richieste di permesso verso i display (display.Hub).
type Permissions interface {
	OpenPermission(q display.PermQuestion) (string, bool)
	WaitPermission(ctx context.Context, id string) display.PermOutcome
	ClosePermissions(session string)
	ClosePermission(id, how string)
	Permissions() []display.PermStatus
}

// PermTracker: la macchina a stati delle sessioni (sessions.Engine).
type PermTracker interface {
	SessionName(id string) string
	PermissionWaiting(session, agent, tool, id string) bool
	PermissionResolved(session, id, how string)
}

// DenyMessage è il motivo che Claude riceve quando il display nega.
const DenyMessage = "Negato dal display Calamaio"

type hookDecision struct {
	HookSpecificOutput struct {
		HookEventName string `json:"hookEventName"`
		Decision      struct {
			Behavior string `json:"behavior"`
			Message  string `json:"message,omitempty"`
		} `json:"decision"`
	} `json:"hookSpecificOutput"`
}

// Decision è l'output di `calamaio hook --wait` per una risposta del display.
func Decision(allow bool) []byte {
	var d hookDecision
	d.HookSpecificOutput.HookEventName = "PermissionRequest"
	if allow {
		d.HookSpecificOutput.Decision.Behavior = "allow"
	} else {
		d.HookSpecificOutput.Decision.Behavior = "deny"
		d.HookSpecificOutput.Decision.Message = DenyMessage
	}
	b, _ := json.Marshal(d)
	return b
}

// hookCaller: POST /hook?wait=1 solo dal hook locale: Host di loopback, JSON, nessun segno di browser.
func hookCaller(r *http.Request) error {
	switch {
	case !loopbackHost(r):
		return errors.New("Host non locale")
	case r.Header.Get("Origin") != "" || r.Header.Get("Sec-Fetch-Site") != "" || r.Header.Get("Sec-Fetch-Mode") != "":
		return errors.New("richiesta da un browser")
	case !strings.HasPrefix(r.Header.Get("Content-Type"), "application/json"):
		return errors.New("serve Content-Type: application/json")
	}
	return nil
}

// isQuestion: le domande di Claude non sono permessi e non vanno mai al display (brief §7 regola 4).
func isQuestion(tool string) bool { return tool == "AskUserQuestion" || tool == "ExitPlanMode" }

// permission gestisce un PermissionRequest arrivato con --wait. nil = nessuna decisione (terminale).
// Aspetta senza limite: finisce con la risposta del display, con la chiusura del hook (ctx, risposta
// nel terminale), con la fine della sessione o con l'arresto del servizio.
func (s *Server) permission(ctx context.Context, body []byte) []byte {
	var p struct {
		Event     string          `json:"hook_event_name"`
		SessionID string          `json:"session_id"`
		AgentID   string          `json:"agent_id"`
		Tool      string          `json:"tool_name"`
		Input     json.RawMessage `json:"tool_input"`
		Cwd       string          `json:"cwd"`
	}
	if json.Unmarshal(body, &p) != nil || p.Event != "PermissionRequest" || p.SessionID == "" || p.Tool == "" {
		return nil
	}
	if isQuestion(p.Tool) {
		return nil // nessuna approvazione automatica e nessun inoltro: si risponde nel terminale
	}
	name := ""
	if s.Tracker != nil {
		name = s.Tracker.SessionName(p.SessionID)
	}
	if name == "" {
		name = path.Base(strings.TrimRight(strings.ReplaceAll(p.Cwd, "\\", "/"), "/"))
		if name == "." || name == "/" || name == "" {
			name = "sessione"
		}
	}
	q := display.PermQuestion{Session: p.SessionID, Name: name, Tool: p.Tool, Arg: sessions.Arg(p.Tool, p.Input, p.Cwd)}
	id, ok := s.Perms.OpenPermission(q)
	if !ok {
		return nil // nessun display autenticato con i permessi accesi: subito al terminale
	}
	if s.Tracker != nil && !s.Tracker.PermissionWaiting(p.SessionID, p.AgentID, p.Tool, id) {
		s.Perms.ClosePermission(id, "terminal") // risolto mentre si apriva: niente da aspettare
	}
	out := s.Perms.WaitPermission(ctx, id)
	if s.Tracker != nil {
		s.Tracker.PermissionResolved(p.SessionID, id, out.How)
	}
	switch out.How {
	case "allow":
		return Decision(true)
	case "deny":
		return Decision(false)
	}
	return nil
}

// apiPair: POST /api/displays/{board}/pair/start | code {code} | cancel → 202 {pairing}.
func (s *Server) apiPair(op string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if s.Pairing == nil {
			writeJSON(w, http.StatusConflict, errBody("abbinamento non disponibile"))
			return
		}
		board := r.PathValue("board")
		var (
			st  *display.PairingStatus
			err error
		)
		switch op {
		case "start":
			st, err = s.Pairing.PairStart(board)
		case "cancel":
			st, err = s.Pairing.PairCancel(board)
		case "code":
			var req struct {
				Code string `json:"code"`
			}
			dec := json.NewDecoder(io.LimitReader(r.Body, 1<<10))
			if dec.Decode(&req) != nil {
				writeJSON(w, http.StatusBadRequest, errBody("serve {\"code\": \"123456\"}"))
				return
			}
			st, err = s.Pairing.PairCode(board, req.Code)
		}
		if err != nil {
			commandError(w, err)
			return
		}
		writeJSON(w, http.StatusAccepted, map[string]any{"board": board, "pairing": st})
	}
}

// apiUnpair: POST /api/displays/{board}/unpair → 200 {board, unpaired}. La scheda può anche non essere
// collegata.
func (s *Server) apiUnpair(w http.ResponseWriter, r *http.Request) {
	if s.Pairing == nil {
		writeJSON(w, http.StatusConflict, errBody("abbinamento non disponibile"))
		return
	}
	board := r.PathValue("board")
	found, err := s.Pairing.Unpair(board)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"board": board, "unpaired": found})
}
