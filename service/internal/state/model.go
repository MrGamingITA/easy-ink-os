// Package state contiene lo stato mostrato dai display (messaggio `state` di docs/PROTOCOLLO.md)
// e il componente che lo custodisce. In M3 lo riempie il generatore finto, in M4 la macchina
// a stati degli hook.
package state

import (
	"encoding/json"
	"fmt"
	"unicode/utf8"
)

// Limiti del protocollo v1 (docs/PROTOCOLLO.md).
const (
	MaxSessions = 6
	MaxLog      = 20
	MaxTasks    = 12
	MaxName     = 31  // byte UTF-8
	MaxDetail   = 95  // byte UTF-8
	MaxSummary  = 127 // byte UTF-8
	MaxTaskText = 47  // byte UTF-8 (firmware: kTaskLen 48 con il terminatore)
	MaxLogText  = 63  // byte UTF-8 (firmware: kLogTextLen 64 con il terminatore)
	// MaxStateBytes: il messaggio `state` serializzato non supera 12 KB (il firmware accetta frame
	// fino a 15 KB: 3 KB di margine).
	MaxStateBytes = 12 * 1024
)

// Stati di una sessione.
const (
	Working    = "working"
	Permission = "permission"
	Input      = "input"
	Done       = "done"
	Error      = "error"
	Idle       = "idle"
	Compact    = "compact"
)

// ValidState dice se s è uno stato di sessione previsto dal protocollo.
func ValidState(s string) bool {
	switch s {
	case Working, Permission, Input, Done, Error, Idle, Compact:
		return true
	}
	return false
}

// Priority: priorità per scegliere la sessione più urgente, come STATES del simulatore.
func Priority(s string) int {
	switch s {
	case Permission:
		return 6
	case Input:
		return 5
	case Error:
		return 4
	case Working, Compact:
		return 3
	case Done:
		return 2
	case Idle:
		return 1
	}
	return 0
}

// Archivable: solo le sessioni ferme (finita, in pausa, in errore) si possono archiviare.
func Archivable(s string) bool { return s == Done || s == Idle || s == Error }

// Stati di un task.
const (
	TaskTodo   = 0
	TaskDoing  = 1
	TaskDoneSt = 2
)

// Snapshot è il contenuto di un messaggio `state` (senza `t`, `v` e `time`).
type Snapshot struct {
	Sessions []Session    `json:"sessions"`
	Limits   *Limits      `json:"limits"`
	Today    Today        `json:"today"`
	Log      []LogEntry   `json:"log"`
	Services []Service    `json:"services"`
	PRs      []PR         `json:"prs"`
	Agenda   []AgendaItem `json:"agenda"`
}

type Session struct {
	ID      string  `json:"id"`
	Name    string  `json:"name"`
	State   string  `json:"state"`
	Detail  string  `json:"detail"`
	Since   int64   `json:"since"`
	AttnAt  *int64  `json:"attnAt"`
	Ctx     *int    `json:"ctx"`
	Subs    int     `json:"subs"`
	Test    *string `json:"test"`
	Summary string  `json:"summary"`
	Tasks   []Task  `json:"tasks"`
	Error   string  `json:"error,omitempty"`
	// Avatar e AvatarCrc (docs/AVATAR.md §10): avatar della sessione (sessione → progetto →
	// predefinito) e CRC-32 del suo binario CAVT. Facoltativi: un firmware che non li conosce li ignora.
	Avatar    string  `json:"avatar,omitempty"`
	AvatarCrc *uint32 `json:"avatarCrc,omitempty"`
	// Cwd: cartella di lavoro della sessione. Serve a risolvere l'avatar del progetto e alla webapp;
	// non va mai ai display (Store.Message la toglie).
	Cwd string `json:"cwd,omitempty"`
}

// Task si serializza come coppia [titolo, stato].
type Task struct {
	Title  string
	Status int
}

func (t Task) MarshalJSON() ([]byte, error) {
	return json.Marshal([2]any{t.Title, t.Status})
}

func (t *Task) UnmarshalJSON(b []byte) error {
	var raw []json.RawMessage
	if err := json.Unmarshal(b, &raw); err != nil {
		return err
	}
	if len(raw) != 2 {
		return fmt.Errorf("task: attesi 2 elementi, trovati %d", len(raw))
	}
	if err := json.Unmarshal(raw[0], &t.Title); err != nil {
		return err
	}
	return json.Unmarshal(raw[1], &t.Status)
}

// Limits: ogni coppia può mancare.
type Limits struct {
	H5      *int   `json:"h5,omitempty"`
	H5Reset *int64 `json:"h5Reset,omitempty"`
	D7      *int   `json:"d7,omitempty"`
	D7Reset *int64 `json:"d7Reset,omitempty"`
}

type Today struct {
	Tasks        int     `json:"tasks"`
	PermDisplay  int     `json:"permDisplay"`
	PermTerminal int     `json:"permTerminal"`
	WaitAvgMs    int64   `json:"waitAvgMs"`
	LinesAdd     int     `json:"linesAdd"`
	LinesDel     int     `json:"linesDel"`
	Cost         float64 `json:"cost"` // stimato, USD
}

type LogEntry struct {
	T    int64  `json:"t"`
	Name string `json:"name"`
	Text string `json:"text"`
}

type Service struct {
	Name string `json:"name"`
	OK   bool   `json:"ok"`
	Ms   int    `json:"ms"`
}

type PR struct {
	Repo   string `json:"repo"`
	N      int    `json:"n"`
	Title  string `json:"title"`
	Checks string `json:"checks"` // ok, ko, run
}

type AgendaItem struct {
	T     int64  `json:"t"`
	Title string `json:"title"`
}

// Message è il messaggio `state` completo, pronto da serializzare.
type Message struct {
	T    string `json:"t"`
	V    int    `json:"v"`
	Time int64  `json:"time"`
	Snapshot
	// DefaultAvatar e DefaultAvatarCrc: avatar predefinito, per le schermate senza sessione
	// (facoltativi, come in welcome).
	DefaultAvatar    string  `json:"defaultAvatar,omitempty"`
	DefaultAvatarCrc *uint32 `json:"defaultAvatarCrc,omitempty"`
}

// AvatarRef: un avatar risolto. ID vuoto = nessun avatar (servizio senza libreria).
type AvatarRef struct {
	ID      string
	CRC     uint32
	From    string // sessione | progetto | predefinito
	Project string // cartella del progetto della sessione
}

// AvatarResolver risolve gli avatar (avatar.Manager, tramite un adattatore).
type AvatarResolver interface {
	SessionAvatar(id, cwd string) AvatarRef
	DefaultAvatar() AvatarRef
}

// Clone restituisce una copia profonda.
func (s Snapshot) Clone() Snapshot {
	b, err := json.Marshal(s)
	if err != nil {
		panic(err) // i tipi sono tutti serializzabili
	}
	var c Snapshot
	if err := json.Unmarshal(b, &c); err != nil {
		panic(err)
	}
	return c.nonNil()
}

// nonNil sostituisce le slice nil con slice vuote: il protocollo vuole array, mai null.
func (s Snapshot) nonNil() Snapshot {
	if s.Sessions == nil {
		s.Sessions = []Session{}
	}
	for i := range s.Sessions {
		if s.Sessions[i].Tasks == nil {
			s.Sessions[i].Tasks = []Task{}
		}
	}
	if s.Log == nil {
		s.Log = []LogEntry{}
	}
	if s.Services == nil {
		s.Services = []Service{}
	}
	if s.PRs == nil {
		s.PRs = []PR{}
	}
	if s.Agenda == nil {
		s.Agenda = []AgendaItem{}
	}
	return s
}

// Normalize applica i limiti del protocollo (conteggi e byte) su una copia.
func (s Snapshot) Normalize() Snapshot {
	s = s.Clone()
	if len(s.Sessions) > MaxSessions {
		s.Sessions = s.Sessions[:MaxSessions]
	}
	for i := range s.Sessions {
		ss := &s.Sessions[i]
		ss.Name = Truncate(ss.Name, MaxName)
		ss.Detail = Truncate(ss.Detail, MaxDetail)
		ss.Summary = Truncate(ss.Summary, MaxSummary)
		if len(ss.Tasks) > MaxTasks {
			ss.Tasks = ss.Tasks[:MaxTasks]
		}
		for j := range ss.Tasks {
			ss.Tasks[j].Title = Truncate(ss.Tasks[j].Title, MaxTaskText)
		}
		if ss.State != Error {
			ss.Error = ""
		} else if ss.Error == "" {
			ss.Error = "unknown"
		}
	}
	if len(s.Log) > MaxLog {
		s.Log = s.Log[:MaxLog]
	}
	for i := range s.Log {
		s.Log[i].Name = Truncate(s.Log[i].Name, MaxName)
		s.Log[i].Text = Truncate(s.Log[i].Text, MaxLogText)
	}
	return s
}

// FitSize riduce un'istantanea già normalizzata finché il messaggio `state` serializzato sta in
// max byte: prima accorcia titoli dei task e testi del log, poi toglie voci del log, poi task,
// infine riepiloghi e dettagli. Con i limiti normali non serve: è una rete di sicurezza per i
// testi pieni di caratteri che JSON scrive come \uXXXX (6 byte l'uno).
func (s Snapshot) FitSize(t int64, max int) Snapshot {
	size := func() int {
		b, _ := json.Marshal(Message{T: "state", V: 1, Time: t, Snapshot: s})
		return len(b)
	}
	if size() <= max {
		return s
	}
	s = s.Clone()
	steps := []func() bool{
		func() bool { return s.shrinkTexts(24) },
		func() bool { return s.shrinkTexts(12) },
		func() bool {
			if len(s.Log) == 0 {
				return false
			}
			s.Log = s.Log[:len(s.Log)-1]
			return true
		},
		func() bool {
			// toglie un task dalla sessione che ne ha di più
			best := -1
			for i := range s.Sessions {
				if n := len(s.Sessions[i].Tasks); n > 0 && (best < 0 || n > len(s.Sessions[best].Tasks)) {
					best = i
				}
			}
			if best < 0 {
				return false
			}
			ss := &s.Sessions[best]
			ss.Tasks = ss.Tasks[:len(ss.Tasks)-1]
			return true
		},
		func() bool {
			cut := false
			for i := range s.Sessions {
				ss := &s.Sessions[i]
				if len(ss.Summary) > 40 || len(ss.Detail) > 40 {
					ss.Summary = Truncate(ss.Summary, 40)
					ss.Detail = Truncate(ss.Detail, 40)
					cut = true
				}
			}
			return cut
		},
	}
	for _, step := range steps {
		for size() > max {
			if !step() {
				break
			}
		}
	}
	return s
}

func (s *Snapshot) shrinkTexts(n int) bool {
	cut := false
	for i := range s.Sessions {
		for j := range s.Sessions[i].Tasks {
			if tt := &s.Sessions[i].Tasks[j].Title; len(*tt) > n {
				*tt = Truncate(*tt, n)
				cut = true
			}
		}
	}
	for i := range s.Log {
		if len(s.Log[i].Text) > n {
			s.Log[i].Text = Truncate(s.Log[i].Text, n)
			cut = true
		}
	}
	return cut
}

// Truncate taglia s a max byte senza spezzare un carattere UTF-8.
func Truncate(s string, max int) string {
	if len(s) <= max {
		return s
	}
	cut := max
	for cut > 0 && !utf8.RuneStart(s[cut]) {
		cut--
	}
	return s[:cut]
}
