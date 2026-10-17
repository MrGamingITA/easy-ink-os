package display

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"calamaio/internal/diag"
	"calamaio/internal/m5"
)

// Alimentazione (`status`), registro da remoto (`log-start`, `log`, `log-stop`) e rapporti di crash
// (`crash`): docs/OTA.md §1.5, §5, §6 e docs/PROTOCOLLO.md.

// ---------- status {batt, usb, cal} ----------

type powerMsg struct {
	Batt *int  `json:"batt"`
	USB  *bool `json:"usb"`
	Cal  *bool `json:"cal"`
	// Giornata di lavoro (workday.go): campo facoltativo; assente o null = stato invariato.
	WorkdayRaw json.RawMessage `json:"workday"`
	hasWorkday bool
	workday    *Workday
}

// Power: ultimo `status` della scheda. Batt/USB nil = la scheda non lo sa.
type Power struct {
	Batt *int      `json:"batt"`
	USB  *bool     `json:"usb"`
	Cal  bool      `json:"cal"`
	At   time.Time `json:"-"`
}

func parsePower(data []byte) (*powerMsg, bool) {
	var m powerMsg
	if err := json.Unmarshal(data, &m); err != nil {
		return nil, false
	}
	if m.Batt != nil && (*m.Batt < 0 || *m.Batt > 100) {
		return nil, false
	}
	w, ok := parseWorkday(m.WorkdayRaw)
	if !ok {
		return nil, false
	}
	m.hasWorkday, m.workday = m.WorkdayRaw != nil, w
	return &m, true
}

func (h *Hub) onPower(c *conn, m *powerMsg) {
	h.mu.Lock()
	c.power = &Power{Batt: m.Batt, USB: m.USB, Cal: m.Cal != nil && *m.Cal, At: h.Now()}
	if m.hasWorkday {
		c.workdaySeen = true
	}
	if m.workday != nil {
		w := *m.workday
		w.At = h.Now().UnixMilli()
		c.workday = &w
	}
	h.mu.Unlock()
	h.changed()
}

// ---------- registro da remoto ----------

// Limiti del registro.
const (
	LogRing      = 4000 // righe tenute in memoria per scheda (oltre a quelle salvate su file)
	MaxLogLine   = 512  // byte per riga (si tronca)
	MaxLogLines  = 400  // righe per messaggio `log`
	MaxLogMinute = 60   // minuti al massimo di log-start
)

// LogSink salva le righe (diag.LogFiles).
type LogSink interface {
	Append(board string, t time.Time, lines []string) (string, error)
}

// LogLine è una riga del registro di una scheda.
type LogLine struct {
	Seq  int64  `json:"seq"`
	At   int64  `json:"at"` // ms epoch dell'arrivo al servizio
	Text string `json:"text"`
}

// LogView è la sessione di registro di una scheda in GET /api/status e nella API del registro.
type LogView struct {
	ID      string `json:"id"`
	Follow  bool   `json:"follow"`
	Active  bool   `json:"active"`
	Started int64  `json:"started"`
	Until   int64  `json:"until,omitempty"`  // con follow: spegnimento automatico
	Lines   int    `json:"lines"`            // righe ricevute in questa sessione
	Reason  string `json:"reason,omitempty"` // perché è finita
	File    string `json:"file,omitempty"`   // file del giorno
}

type logSess struct {
	id      string
	follow  bool
	started time.Time
	until   time.Time
	connID  int
	lines   int
	live    bool // contenuto dell'anello finito (`end`), ora righe in diretta
	ended   bool
	reason  string
	timer   *time.Timer
	gen     int
}

type boardLog struct {
	ring []LogLine
	seq  int64
	sess *logSess
	wake chan struct{}
	file string
}

type logMsg struct {
	ID    string   `json:"id"`
	Lines []string `json:"lines"`
	End   bool     `json:"end"`
}

func parseLog(data []byte) (*logMsg, bool) {
	var m logMsg
	if err := json.Unmarshal(data, &m); err != nil || m.ID == "" || len(m.ID) > 32 || len(m.Lines) > MaxLogLines {
		return nil, false
	}
	return &m, true
}

type logStartMsg struct {
	T       string `json:"t"`
	V       int    `json:"v"`
	ID      string `json:"id"`
	Follow  bool   `json:"follow"`
	Minutes int    `json:"minutes"`
}

type logStopMsg struct {
	T  string `json:"t"`
	V  int    `json:"v"`
	ID string `json:"id"`
}

func (h *Hub) boardLogLocked(board string) *boardLog {
	if h.logs == nil {
		h.logs = map[string]*boardLog{}
	}
	bl := h.logs[board]
	if bl == nil {
		bl = &boardLog{wake: make(chan struct{})}
		h.logs[board] = bl
	}
	return bl
}

// wakeLocked sveglia chi aspetta righe nuove (lunga attesa della API). Con h.mu.
func (bl *boardLog) wakeLocked() {
	close(bl.wake)
	bl.wake = make(chan struct{})
}

func (s *logSess) view(file string) *LogView {
	if s == nil {
		return nil
	}
	v := &LogView{ID: s.id, Follow: s.follow, Active: !s.ended, Started: s.started.UnixMilli(), Lines: s.lines, Reason: s.reason, File: file}
	if s.follow {
		v.Until = s.until.UnixMilli()
	}
	return v
}

// LogStart chiede alla scheda il suo registro (log-start): il contenuto dell'anello e, con follow, le
// righe nuove per minutes minuti (1–60). Una sessione già aperta per la stessa scheda si chiude.
func (h *Hub) LogStart(board string, follow bool, minutes int) (*LogView, error) {
	if minutes == 0 {
		minutes = 10
	}
	if minutes < 1 || minutes > MaxLogMinute {
		return nil, &InvalidError{Msg: fmt.Sprintf("minuti da 1 a %d", MaxLogMinute)}
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	c := h.target(board)
	if c == nil {
		return nil, ErrNotConnected
	}
	if !c.authed {
		return nil, ErrNotAuthed
	}
	bl := h.boardLogLocked(board)
	if s := bl.sess; s != nil && !s.ended {
		h.logEndLocked(board, bl, "sostituita da una nuova richiesta", true)
	}
	now := h.Now()
	s := &logSess{id: "l" + m5.Hex(m5.Random(4)), follow: follow, started: now, connID: c.info.ID}
	if follow {
		s.until = now.Add(time.Duration(minutes) * time.Minute)
	}
	if !h.enqueueOut(c, logStartMsg{T: "log-start", V: ProtocolVersion, ID: s.id, Follow: follow, Minutes: minutes}) {
		return nil, ErrBusy
	}
	bl.sess = s
	what := "contenuto dell'anello"
	if follow {
		what += fmt.Sprintf(", poi in diretta per %d min", minutes)
	}
	h.logWriteLocked(board, bl, []string{fmt.Sprintf("--- %s · registro di %s (fw %s): %s ---", now.Local().Format("2006-01-02 15:04:05"), board, c.info.FW, what)})
	if follow {
		h.logArmLocked(board, bl, s, time.Duration(minutes)*time.Minute, "tempo scaduto")
	} else {
		h.logArmLocked(board, bl, s, h.LogQuiet*5, "nessuna risposta dalla scheda")
	}
	h.Log.Printf("registro %s: chiesto a %s (follow %v, %d min)", s.id, board, follow, minutes)
	go h.changed()
	return s.view(bl.file), nil
}

// LogStop chiude la sessione di registro di board (log-stop).
func (h *Hub) LogStop(board string) (*LogView, error) {
	h.mu.Lock()
	defer h.mu.Unlock()
	bl := h.boardLogLocked(board)
	if bl.sess == nil || bl.sess.ended {
		return nil, &BlockedError{Msg: "nessun registro in corso per questa scheda"}
	}
	h.logEndLocked(board, bl, "fermato dal PC", true)
	return bl.sess.view(bl.file), nil
}

// logArmLocked: la sessione finisce dopo d senza novità (o alla scadenza di follow). Con h.mu.
func (h *Hub) logArmLocked(board string, bl *boardLog, s *logSess, d time.Duration, why string) {
	s.gen++
	gen := s.gen
	if s.timer != nil {
		s.timer.Stop()
	}
	s.timer = time.AfterFunc(d, func() {
		h.mu.Lock()
		defer h.mu.Unlock()
		if bl.sess != s || s.ended || s.gen != gen {
			return
		}
		h.logEndLocked(board, bl, why, s.follow)
	})
}

// logEndLocked chiude la sessione; con stop manda anche log-stop alla scheda. Con h.mu.
func (h *Hub) logEndLocked(board string, bl *boardLog, why string, stop bool) {
	s := bl.sess
	if s == nil || s.ended {
		return
	}
	s.ended, s.reason = true, why
	s.gen++
	if s.timer != nil {
		s.timer.Stop()
	}
	if stop {
		if c := h.conns[s.connID]; c != nil {
			h.enqueueOut(c, logStopMsg{T: "log-stop", V: ProtocolVersion, ID: s.id})
		}
	}
	h.logWriteLocked(board, bl, []string{fmt.Sprintf("--- %s · fine del registro: %s (%d righe) ---", h.Now().Local().Format("15:04:05"), why, s.lines)})
	bl.wakeLocked()
	h.Log.Printf("registro %s di %s finito: %s (%d righe)", s.id, board, why, s.lines)
	go h.changed()
}

// logWriteLocked aggiunge righe all'anello in memoria e al file. Con h.mu.
func (h *Hub) logWriteLocked(board string, bl *boardLog, lines []string) {
	now := h.Now()
	for _, l := range lines {
		bl.seq++
		bl.ring = append(bl.ring, LogLine{Seq: bl.seq, At: now.UnixMilli(), Text: l})
	}
	if over := len(bl.ring) - LogRing; over > 0 {
		bl.ring = append([]LogLine(nil), bl.ring[over:]...)
	}
	if h.LogFiles != nil {
		if f, err := h.LogFiles.Append(board, now, lines); err != nil {
			h.Log.Printf("registro di %s: non salvato: %v", board, err)
		} else {
			bl.file = f
		}
	}
	bl.wakeLocked()
}

func (h *Hub) onLog(c *conn, m *logMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	board := c.info.Board
	if !c.authed {
		h.ignoreLocked(c.info.ID, "log da una connessione non autenticata")
		return
	}
	bl := h.boardLogLocked(board)
	s := bl.sess
	if s == nil || s.ended || s.id != m.ID {
		h.ignoreLocked(c.info.ID, "log per un registro non richiesto (%q)", clipStr(m.ID))
		return
	}
	lines := make([]string, 0, len(m.Lines)+1)
	for _, l := range m.Lines {
		lines = append(lines, m5.TruncUTF8(diag.CleanLine(l), MaxLogLine))
	}
	s.lines += len(m.Lines)
	if len(m.Lines) == 0 {
		m.End = true // un `log` vuoto chiude il contenuto dell'anello, come end
	}
	if m.End && s.follow && !s.live {
		lines = append(lines, "--- in diretta ---")
	}
	if len(lines) > 0 {
		h.logWriteLocked(board, bl, lines)
	}
	switch {
	case m.End && !s.follow:
		h.logEndLocked(board, bl, "contenuto dell'anello ricevuto", false)
	case m.End:
		s.live = true
	case !s.follow:
		h.logArmLocked(board, bl, s, h.LogQuiet, "contenuto dell'anello ricevuto")
	}
	go h.changed()
}

// LogRead restituisce le righe di board dopo after, la sessione e un canale che si chiude alla prossima
// novità (per la lunga attesa).
func (h *Hub) LogRead(board string, after int64) ([]LogLine, int64, *LogView, <-chan struct{}) {
	h.mu.Lock()
	defer h.mu.Unlock()
	bl := h.boardLogLocked(board)
	out := []LogLine{}
	for _, l := range bl.ring {
		if l.Seq > after {
			out = append(out, l)
		}
	}
	return out, bl.seq, bl.sess.view(bl.file), bl.wake
}

// LogOf: la sessione di registro di board (nil se non ce n'è mai stata una).
func (h *Hub) LogOf(board string) *LogView {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.logs == nil || h.logs[board] == nil {
		return nil
	}
	bl := h.logs[board]
	return bl.sess.view(bl.file)
}

// logConnClosed: la scheda si è scollegata, la sessione di registro finisce. Con h.mu.
func (h *Hub) logConnClosed(c *conn) {
	if h.logs == nil {
		return
	}
	bl := h.logs[c.info.Board]
	if bl != nil && bl.sess != nil && !bl.sess.ended && bl.sess.connID == c.info.ID {
		h.logEndLocked(c.info.Board, bl, "scheda scollegata", false)
	}
}

// ---------- crash ----------

// CrashSaver salva i rapporti di crash (diag.CrashStore).
type CrashSaver interface {
	Save(board string, m diag.CrashMsg) (*diag.Crash, error)
}

func parseCrash(data []byte) (*diag.CrashMsg, bool) {
	var m diag.CrashMsg
	if err := json.Unmarshal(data, &m); err != nil || m.Validate() != nil {
		return nil, false
	}
	return &m, true
}

func (h *Hub) onCrash(c *conn, m *diag.CrashMsg) {
	h.mu.Lock()
	authed, board := c.authed, c.info.Board
	h.mu.Unlock()
	if !authed {
		h.ignore(c.info.ID, "crash da una connessione non autenticata")
		return
	}
	h.Log.Printf("crash: %s riferisce un crash del firmware %s: %s (pc %s)", board, m.FW, m.Reason, m.PC)
	if h.Crashes == nil {
		return
	}
	go func() { // addr2line può metterci qualche secondo
		cr, err := h.Crashes.Save(board, *m)
		if err != nil {
			h.Log.Printf("crash di %s non salvato: %v", board, err)
			return
		}
		note := ""
		if len(cr.Decoded) > 0 {
			note = ": " + strings.Join(cr.Decoded[:min(2, len(cr.Decoded))], " · ")
		} else if cr.DecodeNote != "" {
			note = " (" + cr.DecodeNote + ")"
		}
		h.Log.Printf("crash di %s salvato come %s%s", board, cr.ID, note)
		h.changed()
	}()
}
