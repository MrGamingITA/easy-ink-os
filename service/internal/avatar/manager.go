package avatar

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"calamaio/internal/config"
)

// Assigner legge e scrive le assegnazioni (config.AvatarStore).
type Assigner interface {
	Get() (config.AvatarAssignments, error)
	Update(fn func(*config.AvatarAssignments) error) error
}

// Origine dell'avatar di una sessione.
const (
	FromSession = "sessione"
	FromProject = "progetto"
	FromDefault = "predefinito"
)

// Resolution: l'avatar di una sessione e da dove viene.
type Resolution struct {
	Entry   *Entry
	From    string // sessione | progetto | predefinito
	Project string // cartella del progetto della sessione ("" se cwd è ignota)
}

// SessionInfo: ciò che serve di una sessione aperta per dire quali avatar sono in uso.
type SessionInfo struct {
	ID       string
	Name     string
	Cwd      string
	Archived bool
}

// Use: un avatar della libreria con il suo stato (docs/AVATAR.md §6.1 e §7, `list --json`).
type Use struct {
	ID          string   `json:"id"`
	Name        string   `json:"name"`
	Description string   `json:"description"`
	Author      string   `json:"author"`
	State       string   `json:"state"` // libero | in-uso
	UsedBy      []string `json:"usedBy"`
	Default     bool     `json:"default"`
	Source      string   `json:"source"` // predefinito | utente
	Frames      int      `json:"frames"`
	Poses       []string `json:"poses"`
	CRC         uint32   `json:"crc"`
	Bytes       int      `json:"bytes"`
	Warnings    []string `json:"warnings"`
}

// Manager mette insieme libreria, assegnazioni e cartelle dei progetti. Sicuro per l'uso concorrente.
type Manager struct {
	Lib    *Library
	Assign Assigner
	// Home: cartella dell'utente; una radice git uguale a Home non conta come progetto (chi tiene la
	// home in git avrebbe un solo progetto per tutto). Vuota = os.UserHomeDir.
	Home string
	// RootTTL: quanto ricordare la radice git di una cartella (0 = 30 s).
	RootTTL time.Duration

	mu    sync.Mutex
	roots map[string]rootEntry
}

type rootEntry struct {
	dir string
	at  time.Time
}

var (
	ErrUnknown   = errors.New("avatar sconosciuto")
	ErrBadTarget = errors.New("serve una sessione o una cartella di progetto")
)

// NewManager: libreria e assegnazioni.
func NewManager(lib *Library, as Assigner) *Manager {
	return &Manager{Lib: lib, Assign: as}
}

func (m *Manager) assignments() config.AvatarAssignments {
	a, _ := m.Assign.Get()
	return a
}

// DefaultEntry: l'avatar predefinito configurato, oppure la Goccia se manca o non è valido.
func (m *Manager) DefaultEntry() *Entry {
	return m.defaultIn(m.Lib.Catalog(), m.assignments())
}

func (m *Manager) defaultIn(cat Catalog, a config.AvatarAssignments) *Entry {
	if a.Default != "" {
		if e := cat.Get(a.Default); e != nil {
			return e
		}
	}
	return cat.Get(DefaultID)
}

// Resolve: avatar di una sessione, sessione → progetto → predefinito (docs/AVATAR.md §6).
// Un'assegnazione a un avatar che non c'è più vale come assente.
func (m *Manager) Resolve(sessionID, cwd string) Resolution {
	return m.resolveIn(m.Lib.Catalog(), m.assignments(), sessionID, cwd)
}

func (m *Manager) resolveIn(cat Catalog, a config.AvatarAssignments, sessionID, cwd string) Resolution {
	r := Resolution{}
	if cwd != "" {
		r.Project = m.ProjectDir(cwd)
	}
	if id := a.Sessions[sessionID]; sessionID != "" && id != "" {
		if e := cat.Get(id); e != nil {
			r.Entry, r.From = e, FromSession
			return r
		}
	}
	if id := a.Projects[r.Project]; r.Project != "" && id != "" {
		if e := cat.Get(id); e != nil {
			r.Entry, r.From = e, FromProject
			return r
		}
	}
	r.Entry, r.From = m.defaultIn(cat, a), FromDefault
	return r
}

// HasAssignment: la sessione o il suo progetto hanno un avatar scelto (anche se fosse la Goccia).
// Serve al hook SessionStart: senza assegnazione Claude propone una scelta.
func (m *Manager) HasAssignment(sessionID, cwd string) bool {
	a := m.assignments()
	if sessionID != "" && a.Sessions[sessionID] != "" {
		return true
	}
	return cwd != "" && a.Projects[m.ProjectDir(cwd)] != ""
}

// ProjectDir: la cartella del progetto di cwd = la radice git che la contiene (cartella con .git,
// anche come file nei worktree), altrimenti cwd stessa. Percorso assoluto, pulito, con i link
// simbolici risolti quando esiste.
func (m *Manager) ProjectDir(cwd string) string {
	dir := NormDir(cwd)
	if dir == "" {
		return ""
	}
	ttl := m.RootTTL
	if ttl == 0 {
		ttl = 30 * time.Second
	}
	m.mu.Lock()
	if r, ok := m.roots[dir]; ok && time.Since(r.at) < ttl {
		m.mu.Unlock()
		return r.dir
	}
	m.mu.Unlock()
	home := m.Home
	if home == "" {
		home, _ = os.UserHomeDir()
	}
	home = NormDir(home)
	root := dir
	for d := dir; ; {
		if _, err := os.Lstat(filepath.Join(d, ".git")); err == nil {
			if d != home {
				root = d
			}
			break
		}
		parent := filepath.Dir(d)
		if parent == d {
			break
		}
		d = parent
	}
	m.mu.Lock()
	if m.roots == nil {
		m.roots = map[string]rootEntry{}
	}
	if len(m.roots) > 1000 {
		m.roots = map[string]rootEntry{}
	}
	m.roots[dir] = rootEntry{dir: root, at: time.Now()}
	m.mu.Unlock()
	return root
}

// NormDir: percorso assoluto e pulito, con i link simbolici risolti se la cartella esiste.
func NormDir(p string) string {
	p = strings.TrimSpace(p)
	if p == "" {
		return ""
	}
	if a, err := filepath.Abs(p); err == nil {
		p = a
	}
	p = filepath.Clean(p)
	if r, err := filepath.EvalSymlinks(p); err == nil {
		p = r
	}
	return p
}

// AssignSession lega l'avatar id alla sessione; id vuoto toglie l'assegnazione.
func (m *Manager) AssignSession(sessionID, id string) error {
	if !validSessionID(sessionID) {
		return fmt.Errorf("%w: id di sessione non valido", ErrBadTarget)
	}
	if err := m.check(id); err != nil {
		return err
	}
	return m.Assign.Update(func(a *config.AvatarAssignments) error {
		if id == "" {
			delete(a.Sessions, sessionID)
		} else {
			a.Sessions[sessionID] = id
		}
		return nil
	})
}

// AssignProject lega l'avatar id alla cartella del progetto di dir (radice git se c'è); id vuoto
// toglie l'assegnazione. Restituisce la cartella usata.
func (m *Manager) AssignProject(dir, id string) (string, error) {
	if strings.TrimSpace(dir) == "" || !filepath.IsAbs(strings.TrimSpace(dir)) {
		return "", fmt.Errorf("%w: serve un percorso assoluto", ErrBadTarget)
	}
	if err := m.check(id); err != nil {
		return "", err
	}
	proj := m.ProjectDir(dir)
	err := m.Assign.Update(func(a *config.AvatarAssignments) error {
		if id == "" {
			delete(a.Projects, proj)
		} else {
			a.Projects[proj] = id
		}
		return nil
	})
	return proj, err
}

// SetDefault cambia l'avatar predefinito (vuoto o "goccia" = la Goccia).
func (m *Manager) SetDefault(id string) error {
	if err := m.check(id); err != nil {
		return err
	}
	return m.Assign.Update(func(a *config.AvatarAssignments) error {
		if id == DefaultID {
			id = ""
		}
		a.Default = id
		return nil
	})
}

// ForgetSession toglie l'assegnazione di una sessione finita (docs/AVATAR.md §6: «sparisce con la
// sessione»). Dice se c'era.
func (m *Manager) ForgetSession(sessionID string) bool {
	if sessionID == "" || m.assignments().Sessions[sessionID] == "" {
		return false
	}
	found := false
	_ = m.Assign.Update(func(a *config.AvatarAssignments) error {
		if _, ok := a.Sessions[sessionID]; ok {
			delete(a.Sessions, sessionID)
			found = true
		}
		return nil
	})
	return found
}

func (m *Manager) check(id string) error {
	if id == "" {
		return nil
	}
	if !ValidID(id) || m.Lib.Get(id) == nil {
		return fmt.Errorf("%w: %q (calamaio avatar list)", ErrUnknown, id)
	}
	return nil
}

func validSessionID(s string) bool {
	if s == "" || len(s) > 128 {
		return false
	}
	for _, c := range s {
		if !(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '-' || c == '_' || c == '.') {
			return false
		}
	}
	return true
}

// Usage: tutti gli avatar con «libero» o «in uso da …» rispetto alla sessione current (docs/AVATAR.md
// §6.1): in uso = mostrato da almeno una sessione aperta, non archiviata e diversa da current.
// free: i liberi da proporre, mai il predefinito (è il ripiego di tutte le sessioni senza avatar).
func (m *Manager) Usage(sessions []SessionInfo, current string) (list []Use, free []string, def string) {
	cat := m.Lib.Catalog()
	a := m.assignments()
	d := m.defaultIn(cat, a)
	if d != nil {
		def = d.ID
	}
	used := map[string][]string{}
	for _, s := range sessions {
		if s.Archived || s.ID == current {
			continue
		}
		r := m.resolveIn(cat, a, s.ID, s.Cwd)
		if r.Entry == nil {
			continue
		}
		used[r.Entry.ID] = append(used[r.Entry.ID], s.Name)
	}
	list = []Use{}
	free = []string{}
	for _, e := range cat.Entries {
		u := Use{ID: e.ID, Name: e.Name, Description: e.Description, Author: e.Author, State: "libero",
			UsedBy: []string{}, Default: e.ID == def, Source: e.Source, Frames: e.Frames, Poses: e.Poses,
			CRC: e.CRC, Bytes: len(e.Bin), Warnings: e.Warnings}
		if u.Warnings == nil {
			u.Warnings = []string{}
		}
		if names := used[e.ID]; len(names) > 0 {
			u.State, u.UsedBy = "in-uso", names
		}
		if u.State == "libero" && !u.Default {
			free = append(free, e.ID)
		}
		list = append(list, u)
	}
	return list, free, def
}

// Rev: impronta della libreria e del predefinito (cambia quando un avatar viene aggiunto, sostituito
// o tolto, o quando cambia il predefinito). La webapp la usa per sapere quando rileggere l'elenco.
func (m *Manager) Rev() string {
	cat := m.Lib.Catalog()
	h := sha256.New()
	for _, e := range cat.Entries {
		fmt.Fprintf(h, "%s:%08x:%s\n", e.ID, e.CRC, e.Source)
	}
	for _, p := range cat.Problems {
		fmt.Fprintf(h, "!%s:%s\n", p.File, strings.Join(p.Errors, "|"))
	}
	if d := m.DefaultEntry(); d != nil {
		fmt.Fprintf(h, "default:%s\n", d.ID)
	}
	return hex.EncodeToString(h.Sum(nil))[:16]
}

// AssignmentsRev: impronta delle assegnazioni (per accorgersi delle modifiche fatte a mano o da un
// altro processo a config.json).
func (m *Manager) AssignmentsRev() string {
	a := m.assignments()
	h := sha256.New()
	fmt.Fprintf(h, "d=%s\n", a.Default)
	for _, k := range sortedKeys(a.Projects) {
		fmt.Fprintf(h, "p=%s=%s\n", k, a.Projects[k])
	}
	for _, k := range sortedKeys(a.Sessions) {
		fmt.Fprintf(h, "s=%s=%s\n", k, a.Sessions[k])
	}
	return hex.EncodeToString(h.Sum(nil))[:16]
}

func sortedKeys(m map[string]string) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

// Assignments: le assegnazioni attuali (copia), per la webapp e la CLI.
func (m *Manager) Assignments() config.AvatarAssignments { return m.assignments() }
