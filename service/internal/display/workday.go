package display

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"
)

// Giornata di lavoro (docs/PROTOCOLLO.md «Giornata di lavoro»): conto alla rovescia fino all'orario di
// fine, tenuto dalla scheda (funziona anche senza PC). Il servizio non calcola niente: riferisce quello
// che dice la scheda e le passa i comandi.
//
//	impostazioni          workStart, workEnd   "HH:MM", workStart < workEnd (in settings e set)
//	servizio → display    workday {id, action: "start" | "stop"}   solo autenticato, risposta ack
//	display → servizio    status {…, workday: {on, end}}   end = ms epoch della fine, null se spenta

// Azioni del comando workday.
const (
	WorkdayStart = "start"
	WorkdayStop  = "stop"
)

// Orari predefiniti della scheda (li decide il firmware: qui servono solo per gli esempi nei messaggi).
const (
	WorkStartDefault = "09:00"
	WorkEndDefault   = "18:00"
)

// Errori del comando workday prima dell'invio (HTTP 409).
var (
	ErrNoWorkday   = errors.New("il firmware di questo display non gestisce la giornata di lavoro: aggiornalo")
	ErrWorkdayBusy = errors.New("c'è già un comando della giornata di lavoro in attesa di conferma: aspetta che finisca")
)

// ValidClock: "HH:MM" con due cifre per parte, 00:00–23:59.
func ValidClock(s string) bool {
	if len(s) != 5 || s[2] != ':' {
		return false
	}
	for _, i := range []int{0, 1, 3, 4} {
		if s[i] < '0' || s[i] > '9' {
			return false
		}
	}
	h := int(s[0]-'0')*10 + int(s[1]-'0')
	m := int(s[3]-'0')*10 + int(s[4]-'0')
	return h <= 23 && m <= 59
}

// NormalizeClock accetta anche "9:00", "9.00" e "0900" (per la CLI) e restituisce "HH:MM".
func NormalizeClock(s string) (string, bool) {
	s = strings.TrimSpace(strings.ReplaceAll(s, ".", ":"))
	if !strings.Contains(s, ":") && len(s) == 4 {
		s = s[:2] + ":" + s[2:]
	}
	if len(s) == 4 && s[1] == ':' {
		s = "0" + s
	}
	return s, ValidClock(s)
}

func defaultClock(key string) string {
	if key == "workEnd" {
		return WorkEndDefault
	}
	return WorkStartDefault
}

// CheckWorkHours: l'inizio viene prima della fine, nello stesso giorno (gli orari "HH:MM" a due cifre
// si confrontano come stringhe).
func CheckWorkHours(start, end string) error {
	if start >= end {
		return invalid("l'orario di inizio (%s) deve venire prima di quello di fine (%s), nello stesso giorno", start, end)
	}
	return nil
}

// Workday è lo stato riferito dalla scheda con l'ultimo `status` che conteneva `workday`.
type Workday struct {
	On  bool   `json:"on"`
	End *int64 `json:"end"` // ms epoch della fine; null se spenta
	At  int64  `json:"at"`  // ms epoch dell'arrivo al servizio
}

// WorkdayPending è il comando in attesa di ack.
type WorkdayPending struct {
	ID     string `json:"id"`
	Action string `json:"action"` // start · stop
	Until  int64  `json:"until"`  // ms epoch: oltre, senza ack il comando è fallito
}

// WorkdayResult è l'esito dell'ultimo comando.
type WorkdayResult struct {
	ID      string `json:"id"`
	Action  string `json:"action"`
	OK      bool   `json:"ok"`
	Error   string `json:"error,omitempty"`   // frase in italiano
	Reason  string `json:"reason,omitempty"`  // motivo dell'ack negativo, come l'ha scritto la scheda
	Timeout bool   `json:"timeout,omitempty"` // nessun ack in tempo
	At      int64  `json:"at"`
}

// WorkdayView è la parte `workday` di un display in GET /api/status.
type WorkdayView struct {
	// Supported: il firmware gestisce la giornata (ha mandato workday in status, o workStart/workEnd
	// nelle impostazioni). Known: la scheda ha già riferito lo stato su questa connessione.
	Supported bool            `json:"supported"`
	Known     bool            `json:"known"`
	On        bool            `json:"on"`
	End       *int64          `json:"end"`
	At        int64           `json:"at,omitempty"`
	Pending   *WorkdayPending `json:"pending"`
	Last      *WorkdayResult  `json:"last"`
}

// WorkdayMsg (servizio → display).
type WorkdayMsg struct {
	T      string `json:"t"`
	V      int    `json:"v"`
	ID     string `json:"id"`
	Action string `json:"action"`
}

type workdayOp struct {
	id, action string
	until      time.Time
}

// parseWorkday legge `status.workday`. nil, true = campo assente o null (stato invariato).
// on è obbligatorio; end intero ≥ 0 o null; con on falso end si ignora.
func parseWorkday(raw json.RawMessage) (*Workday, bool) {
	raw = bytes.TrimSpace(raw)
	if len(raw) == 0 || string(raw) == "null" {
		return nil, true
	}
	var m struct {
		On  *bool            `json:"on"`
		End *json.RawMessage `json:"end"`
	}
	if raw[0] != '{' || json.Unmarshal(raw, &m) != nil || m.On == nil {
		return nil, false
	}
	w := &Workday{On: *m.On}
	if m.End != nil && string(bytes.TrimSpace(*m.End)) != "null" {
		var end int64
		if json.Unmarshal(*m.End, &end) != nil || end < 0 {
			return nil, false
		}
		if w.On {
			w.End = &end
		}
	}
	return w, true
}

// workdayCapable: il firmware conosce la giornata di lavoro. Con h.mu.
func workdayCapable(c *conn) bool {
	if c.workdaySeen {
		return true
	}
	_, a := c.settings["workStart"]
	_, b := c.settings["workEnd"]
	return a && b
}

// workdayView: la vista per /api/status. Con h.mu.
func workdayView(c *conn) *WorkdayView {
	v := &WorkdayView{Supported: workdayCapable(c)}
	if w := c.workday; w != nil {
		v.Known, v.On, v.At = true, w.On, w.At
		if w.End != nil {
			e := *w.End
			v.End = &e
		}
	}
	if o := c.wdOp; o != nil {
		v.Pending = &WorkdayPending{ID: o.id, Action: o.action, Until: o.until.UnixMilli()}
	}
	if c.wdLast != nil {
		l := *c.wdLast
		v.Last = &l
	}
	return v
}

// checkWorkHoursFor controlla workStart/workEnd chiesti in un set contro i valori già in attesa o
// riferiti dalla scheda. Con h.mu.
func checkWorkHoursFor(c *conn, vals map[string]any) error {
	_, s := vals["workStart"]
	_, e := vals["workEnd"]
	if !s && !e {
		return nil
	}
	if !workdayCapable(c) {
		return ErrNoWorkday
	}
	pick := func(k string) string {
		if v, ok := vals[k].(string); ok {
			return v
		}
		if p, ok := c.pending[k]; ok {
			if v, ok := p.value.(string); ok {
				return v
			}
		}
		v, _ := c.settings[k].(string)
		return v
	}
	start, end := pick("workStart"), pick("workEnd")
	if start == "" || end == "" {
		return nil // la scheda non ha riferito l'altro orario: decide lei (ack negativo se non va)
	}
	return CheckWorkHours(start, end)
}

// Workday manda `workday {id, action}` alla scheda. Il comando resta in attesa fino all'ack (al
// massimo AckTimeout); l'esito va in WorkdayView.Last. Errori: *InvalidError (400), ErrNotConnected,
// ErrNotAuthed, ErrNoWorkday, ErrWorkdayBusy, ErrBusy (409), ErrStopping (503).
func (h *Hub) Workday(board, action string) (string, *WorkdayPending, error) {
	if action != WorkdayStart && action != WorkdayStop {
		return "", nil, invalid("action: serve %q o %q", WorkdayStart, WorkdayStop)
	}
	h.mu.Lock()
	if h.stopping || h.closed || h.farewell {
		h.mu.Unlock()
		return "", nil, ErrStopping
	}
	c := h.target(board)
	if c == nil {
		h.mu.Unlock()
		return "", nil, ErrNotConnected
	}
	if !c.authed {
		h.mu.Unlock()
		return "", nil, ErrNotAuthed
	}
	if !workdayCapable(c) {
		h.mu.Unlock()
		return "", nil, ErrNoWorkday
	}
	if c.wdOp != nil {
		h.mu.Unlock()
		return "", nil, ErrWorkdayBusy
	}
	h.nextCmd++
	id := fmt.Sprintf("c%d", h.nextCmd)
	select {
	case c.out <- WorkdayMsg{T: "workday", V: ProtocolVersion, ID: id, Action: action}:
	default:
		h.mu.Unlock()
		return "", nil, ErrBusy
	}
	// Niente c.lastErr = nil: l'errore di un `set` resta finché non arriva un altro set.
	c.cmds[id] = &command{kind: "workday", timer: time.AfterFunc(h.AckTimeout, func() { h.expire(c, id) })}
	c.wdOp = &workdayOp{id: id, action: action, until: h.Now().Add(h.AckTimeout)}
	p := &WorkdayPending{ID: id, Action: action, Until: c.wdOp.until.UnixMilli()}
	h.mu.Unlock()
	h.Log.Printf("ws: #%d workday %s %s", c.info.ID, id, action)
	h.changed()
	return id, p, nil
}

func workdayVerb(action string) string {
	if action == WorkdayStop {
		return "fermato"
	}
	return "avviato"
}

// workdayAcked: ack della scheda per un comando workday. Con h.mu.
func (h *Hub) workdayAcked(c *conn, id string, ok bool, why string) {
	o := c.wdOp
	if o == nil || o.id != id {
		return
	}
	res := WorkdayResult{ID: id, Action: o.action, OK: ok, At: h.Now().UnixMilli()}
	if !ok {
		if why = strings.TrimSpace(why); why == "" {
			why = "senza motivo"
		}
		res.Reason = why
		res.Error = fmt.Sprintf("la scheda non ha %s la giornata di lavoro: %s", workdayVerb(o.action), why)
		h.Log.Printf("ws: #%d workday %s %s rifiutato dalla scheda: %s", c.info.ID, id, o.action, why)
	} else {
		h.Log.Printf("ws: #%d workday %s %s: confermato dalla scheda", c.info.ID, id, o.action)
	}
	c.wdOp, c.wdLast = nil, &res
}

// workdayExpired: nessun ack entro AckTimeout. Con h.mu.
func (h *Hub) workdayExpired(c *conn, id string) {
	o := c.wdOp
	if o == nil || o.id != id {
		return
	}
	h.Log.Printf("ws: #%d nessun ack per workday %s %s entro %s", c.info.ID, id, o.action, h.AckTimeout)
	c.wdOp = nil
	c.wdLast = &WorkdayResult{ID: id, Action: o.action, Timeout: true, At: h.Now().UnixMilli(),
		Error: fmt.Sprintf("la scheda non ha confermato entro %s: la giornata potrebbe non essere stata %s",
			h.AckTimeout.Round(time.Second), strings.TrimSuffix(workdayVerb(o.action), "o")+"a")}
}
