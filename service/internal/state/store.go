package state

import (
	"encoding/json"
	"errors"
	"sort"
	"sync"
	"time"
)

// Source è ciò che i display leggono: un'istantanea completa e un avviso a ogni cambiamento.
// In M3 dietro c'è lo Store riempito dal generatore finto; in M4 lo stesso Store riempito
// dalla macchina a stati degli hook (internal/sessions).
type Source interface {
	// Message restituisce il messaggio `state` completo, con `time` = adesso.
	Message() Message
	// Subscribe restituisce un canale che riceve un segnale (coalescente) a ogni cambiamento.
	Subscribe() (<-chan struct{}, func())
}

// Writer è ciò che chi produce lo stato (generatore finto, hook) usa per cambiarlo.
type Writer interface {
	Update(fn func(*Snapshot))
}

// Archiver archivia e ripristina le sessioni ferme (display: `dismiss`; webapp: /api/sessions/…).
type Archiver interface {
	Archive(id string) error
	Unarchive(id string) error
}

var (
	ErrNoSession     = errors.New("sessione sconosciuta")
	ErrNotArchivable = errors.New("si archiviano solo le sessioni finite, in pausa o in errore")
)

// Store custodisce lo stato. Sicuro per l'uso concorrente.
//
// Archiviate: l'insieme vive solo qui (effimero, si perde al riavvio). Una sessione archiviata non
// compare nel messaggio `state` dei display; torna visibile da sola appena il suo stato diventa
// working, permission, input o compact (qualunque scrittore lo cambi).
type Store struct {
	Now func() time.Time
	// Avatars (facoltativo): risolve l'avatar di ogni sessione e il predefinito. Va impostato prima
	// di usare lo Store.
	Avatars AvatarResolver
	// OnArchive (facoltativo): una sessione appena archiviata (gioco della mascotte: archiviare una
	// sessione finita la chiude). Chiamata senza s.mu.
	OnArchive func(Session)

	mu       sync.Mutex
	snap     Snapshot
	rev      uint64
	subs     map[int]chan struct{}
	next     int
	archived map[string]bool
}

func NewStore(initial Snapshot) *Store {
	return &Store{Now: time.Now, snap: initial.Clone(), subs: map[int]chan struct{}{}, archived: map[string]bool{}}
}

// Snapshot restituisce una copia normalizzata dello stato attuale, archiviate comprese.
func (s *Store) Snapshot() Snapshot {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.snap.Normalize()
}

// Rev conta i cambiamenti (utile nei test e in /debug).
func (s *Store) Rev() uint64 {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.rev
}

// Message è il messaggio `state` per i display: senza le archiviate, al massimo MaxSessions
// (le più urgenti, nell'ordine dello stato), testi nei limiti e al massimo MaxStateBytes.
func (s *Store) Message() Message {
	s.mu.Lock()
	snap := s.snap.Clone()
	arch := make(map[string]bool, len(s.archived))
	for k := range s.archived {
		arch[k] = true
	}
	s.mu.Unlock()
	visible := snap.Sessions[:0]
	for _, ss := range snap.Sessions {
		if !arch[ss.ID] {
			visible = append(visible, ss)
		}
	}
	snap.Sessions = mostUrgent(visible, MaxSessions)
	t := s.Now().UnixMilli()
	n := snap.Normalize()
	m := Message{T: "state", V: 1, Time: t}
	reserve := 0
	for i := range n.Sessions {
		ss := &n.Sessions[i]
		if s.Avatars != nil {
			if r := s.Avatars.SessionAvatar(ss.ID, ss.Cwd); r.ID != "" {
				crc := r.CRC
				ss.Avatar, ss.AvatarCrc = r.ID, &crc
			}
		}
		ss.Cwd = ""
	}
	if s.Avatars != nil {
		if d := s.Avatars.DefaultAvatar(); d.ID != "" {
			crc := d.CRC
			m.DefaultAvatar, m.DefaultAvatarCrc = d.ID, &crc
			b, _ := json.Marshal(struct {
				A string `json:"defaultAvatar"`
				C uint32 `json:"defaultAvatarCrc"`
			}{d.ID, crc})
			reserve = len(b) // i campi in radice non li conta FitSize
		}
	}
	m.Snapshot = n.FitSize(t, MaxStateBytes-reserve)
	return m
}

// Touch avvisa gli iscritti senza cambiare lo stato: per esempio è cambiato l'avatar di una
// sessione (assegnazione o libreria), che si risolve all'invio.
func (s *Store) Touch() {
	s.mu.Lock()
	s.rev++
	s.mu.Unlock()
	s.notify()
}

// mostUrgent tiene le n sessioni a priorità più alta senza cambiarne l'ordine.
func mostUrgent(ss []Session, n int) []Session {
	if len(ss) <= n {
		return ss
	}
	idx := make([]int, len(ss))
	for i := range idx {
		idx[i] = i
	}
	sort.SliceStable(idx, func(a, b int) bool { return Priority(ss[idx[a]].State) > Priority(ss[idx[b]].State) })
	keep := map[int]bool{}
	for _, i := range idx[:n] {
		keep[i] = true
	}
	out := make([]Session, 0, n)
	for i, x := range ss {
		if keep[i] {
			out = append(out, x)
		}
	}
	return out
}

// SessionView è una sessione per la webapp: come nel protocollo, più il flag `archived`.
type SessionView struct {
	Session
	Archived bool `json:"archived"`
	// Avatar per la webapp: da dove viene (sessione | progetto | predefinito) e cartella del progetto.
	AvatarFrom string `json:"avatarFrom,omitempty"`
	Project    string `json:"project,omitempty"`
}

// View è lo stato per la webapp (GET /api/status): tutte le sessioni, archiviate comprese.
type View struct {
	Time     int64         `json:"time"`
	Sessions []SessionView `json:"sessions"`
	Limits   *Limits       `json:"limits"`
	Today    Today         `json:"today"`
	Log      []LogEntry    `json:"log"`
	Services []Service     `json:"services"`
	PRs      []PR          `json:"prs"`
	Agenda   []AgendaItem  `json:"agenda"`
	// DefaultAvatar: avatar predefinito (vuoto senza libreria).
	DefaultAvatar    string  `json:"defaultAvatar,omitempty"`
	DefaultAvatarCrc *uint32 `json:"defaultAvatarCrc,omitempty"`
}

func (s *Store) View() View {
	s.mu.Lock()
	snap := s.snap.Clone()
	arch := make(map[string]bool, len(s.archived))
	for k := range s.archived {
		arch[k] = true
	}
	s.mu.Unlock()
	all := snap.Sessions
	snap.Sessions = nil
	n := snap.Normalize()
	v := View{Time: s.Now().UnixMilli(), Sessions: make([]SessionView, 0, len(all)), Limits: n.Limits,
		Today: n.Today, Log: n.Log, Services: n.Services, PRs: n.PRs, Agenda: n.Agenda}
	for _, ss := range all {
		one := Snapshot{Sessions: []Session{ss}}.Normalize().Sessions[0]
		sv := SessionView{Session: one, Archived: arch[ss.ID]}
		if s.Avatars != nil {
			if r := s.Avatars.SessionAvatar(ss.ID, ss.Cwd); r.ID != "" {
				crc := r.CRC
				sv.Avatar, sv.AvatarCrc, sv.AvatarFrom, sv.Project = r.ID, &crc, r.From, r.Project
			}
		}
		v.Sessions = append(v.Sessions, sv)
	}
	if s.Avatars != nil {
		if d := s.Avatars.DefaultAvatar(); d.ID != "" {
			crc := d.CRC
			v.DefaultAvatar, v.DefaultAvatarCrc = d.ID, &crc
		}
	}
	return v
}

// Update applica fn allo stato e avvisa gli iscritti.
func (s *Store) Update(fn func(*Snapshot)) {
	s.mu.Lock()
	fn(&s.snap)
	s.snap = s.snap.nonNil()
	s.reviveLocked()
	s.rev++
	s.mu.Unlock()
	s.notify()
}

// reviveLocked: le archiviate che si sono rimesse in moto tornano visibili; quelle sparite si
// dimenticano. Con s.mu.
func (s *Store) reviveLocked() {
	if len(s.archived) == 0 {
		return
	}
	present := map[string]bool{}
	for i := range s.snap.Sessions {
		ss := &s.snap.Sessions[i]
		present[ss.ID] = true
		if s.archived[ss.ID] && !Archivable(ss.State) {
			delete(s.archived, ss.ID)
			s.snap.AddLog(s.Now().UnixMilli(), ss.Name, "riprende")
		}
	}
	for id := range s.archived {
		if !present[id] {
			delete(s.archived, id)
		}
	}
}

func (s *Store) notify() {
	s.mu.Lock()
	subs := make([]chan struct{}, 0, len(s.subs))
	for _, ch := range s.subs {
		subs = append(subs, ch)
	}
	s.mu.Unlock()
	for _, ch := range subs {
		select {
		case ch <- struct{}{}:
		default: // c'è già un avviso in attesa: basta quello
		}
	}
}

// Replace sostituisce tutto lo stato.
func (s *Store) Replace(snap Snapshot) {
	c := snap.Clone()
	s.Update(func(p *Snapshot) { *p = c })
}

// Archive nasconde una sessione ferma ai display. Archiviarla di nuovo non è un errore.
func (s *Store) Archive(id string) error {
	s.mu.Lock()
	ss := findID(&s.snap, id)
	switch {
	case ss == nil:
		s.mu.Unlock()
		return ErrNoSession
	case !Archivable(ss.State):
		s.mu.Unlock()
		return ErrNotArchivable
	case s.archived[id]:
		s.mu.Unlock()
		return nil
	}
	s.archived[id] = true
	s.snap.AddLog(s.Now().UnixMilli(), ss.Name, "archiviata")
	s.rev++
	arch := *ss
	s.mu.Unlock()
	s.notify()
	if s.OnArchive != nil {
		s.OnArchive(arch)
	}
	return nil
}

// Unarchive rimette una sessione tra quelle visibili (webapp).
func (s *Store) Unarchive(id string) error {
	s.mu.Lock()
	ss := findID(&s.snap, id)
	if ss == nil {
		s.mu.Unlock()
		return ErrNoSession
	}
	if !s.archived[id] {
		s.mu.Unlock()
		return nil
	}
	delete(s.archived, id)
	s.snap.AddLog(s.Now().UnixMilli(), ss.Name, "riprende")
	s.rev++
	s.mu.Unlock()
	s.notify()
	return nil
}

// IsArchived dice se la sessione è archiviata.
func (s *Store) IsArchived(id string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.archived[id]
}

func findID(s *Snapshot, id string) *Session {
	for i := range s.Sessions {
		if s.Sessions[i].ID == id {
			return &s.Sessions[i]
		}
	}
	return nil
}

func (s *Store) Subscribe() (<-chan struct{}, func()) {
	ch := make(chan struct{}, 1)
	s.mu.Lock()
	id := s.next
	s.next++
	s.subs[id] = ch
	s.mu.Unlock()
	return ch, func() {
		s.mu.Lock()
		delete(s.subs, id)
		s.mu.Unlock()
	}
}

// Find cerca una sessione per id o per nome (il puntatore punta nella slice condivisa).
func (s Snapshot) Find(key string) *Session {
	for i := range s.Sessions {
		if s.Sessions[i].ID == key {
			return &s.Sessions[i]
		}
	}
	for i := range s.Sessions {
		if s.Sessions[i].Name == key {
			return &s.Sessions[i]
		}
	}
	return nil
}

// AddLog aggiunge un evento in testa (il più recente per primo) e tiene gli ultimi MaxLog.
func (s *Snapshot) AddLog(t int64, name, text string) {
	s.Log = append([]LogEntry{{T: t, Name: name, Text: text}}, s.Log...)
	if len(s.Log) > MaxLog {
		s.Log = s.Log[:MaxLog]
	}
}

// Empty è lo stato senza sessioni (protocol/examples/state-empty.json).
func Empty() Snapshot {
	return Snapshot{}.nonNil()
}
