package sessions

import (
	"encoding/json"
	"strings"
	"time"
)

// M5: i permessi che passano dal hook `calamaio hook --wait`. La risposta dal display arriva dal hub.
// Quella nel terminale no: provato dal vivo (Claude Code 2.1.296), rispondendo nel terminale Claude Code
// esegue lo strumento ma NON chiude il processo del hook. Quindi la macchina a stati resta la fonte:
// quando risolve un permesso con un hook in attesa (PostToolUse, PreToolUse di un altro strumento,
// nuovo prompt, Stop, diniego nel transcript, SessionEnd) chiama OnHookClosed e la richiesta si chiude.

// Arg è l'argomento corto di una richiesta di permesso: il dettaglio senza il nome dello strumento
// (`Bash: npm run test` → `npm run test`, `Edit src/app.ts` → `src/app.ts`).
func Arg(tool string, input json.RawMessage, cwd string) string {
	d := Detail(tool, input, cwd)
	for _, p := range []string{tool + ": ", tool + " "} {
		if strings.HasPrefix(d, p) {
			return strings.TrimSpace(strings.TrimPrefix(d, p))
		}
	}
	if d == tool || strings.HasPrefix(tool, "mcp__") {
		return ""
	}
	return d
}

// SessionName restituisce il nome della sessione come nello state ("" se non c'è).
func (e *Engine) SessionName(id string) string {
	e.mu.Lock()
	defer e.mu.Unlock()
	s := e.sess[id]
	if s == nil {
		return ""
	}
	return e.nameOf(s)
}

// PermissionWaiting lega la richiesta id (sul display) al permesso in attesa della sessione. false se
// il permesso non c'è più (già risolto nel frattempo): la richiesta va chiusa subito.
func (e *Engine) PermissionWaiting(session, agent, tool, id string) bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	s := e.sess[session]
	if s == nil {
		return false
	}
	for i := len(s.pending) - 1; i >= 0; i-- {
		pm := &s.pending[i]
		if pm.agent == agent && pm.tool == tool && !pm.hook {
			pm.hook, pm.hookID = true, id
			return true
		}
	}
	return false
}

// PermissionResolved: il hook in attesa ha un esito. how: allow / deny (dal display), terminal (hook
// chiuso da Claude Code: risposta nel terminale), session, service (niente da contare).
func (e *Engine) PermissionResolved(session, id, how string) {
	e.mu.Lock()
	s := e.sess[session]
	if s == nil {
		e.mu.Unlock()
		return
	}
	now := e.Now()
	idx := -1
	for i, pm := range s.pending {
		if pm.hook && pm.hookID == id {
			idx = i
			break
		}
	}
	if idx < 0 {
		// già risolto da un altro evento (PostToolUse, nuovo prompt…): niente da contare due volte
		e.mu.Unlock()
		return
	}
	detail := s.pending[idx].detail
	switch how {
	case "allow", "deny":
		s.pending = append(s.pending[:idx], s.pending[idx+1:]...)
		e.countResolvedBy(s, now, true)
		if how == "allow" {
			e.logf(s, now, "consentito dal display")
		} else {
			e.logf(s, now, "negato dal display")
		}
		e.afterResolve(s, now)
	case "terminal":
		s.pending = append(s.pending[:idx], s.pending[idx+1:]...)
		e.countResolvedBy(s, now, false)
		e.logf(s, now, "risposto dal terminale")
		e.afterResolve(s, now)
		// Concesso o negato? Lo dice il transcript: se finisce con l'interruzione, era un no.
		s.termDetail = detail
		s.checkAt = now.Add(e.Grace)
		s.checkEvents = s.events
	default: // session, service: il permesso resta com'è (lo chiudono gli altri eventi)
		s.pending[idx].hook, s.pending[idx].hookID = false, ""
		e.mu.Unlock()
		return
	}
	e.mu.Unlock()
	e.sync()
}

// countResolvedBy conta un permesso risolto dal display o dal terminale e l'attesa media.
func (e *Engine) countResolvedBy(s *session, now time.Time, display bool) {
	if display {
		e.today.Today.PermDisplay++
	} else {
		e.today.Today.PermTerminal++
	}
	s.lastResolve = now
	if len(s.pending) == 0 {
		e.countWait(now, s.st.AttnAt)
	}
	e.dirty = true
}
