package theme

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

var (
	ErrNotFound = errors.New("tema sconosciuto")
	ErrExists   = errors.New("c'è già un tema con questo id")
	ErrReserved = errors.New("«calamaio» è il tema di serie: non si cambia")
	ErrChanged  = errors.New("il tema è stato cambiato altrove")
)

// AutosaveMerge: i salvataggi automatici più vicini di così alla creazione di una versione la riscrivono.
const AutosaveMerge = 2 * time.Minute

// autoMark: l'ultima versione scritta da un Autosave di questo processo e quando è stata creata.
type autoMark struct {
	n   int
	at  time.Time // creazione della versione: la finestra parte da qui
	mod int64     // mtime (ms) del file della versione come l'ha lasciato l'ultimo Autosave
}

// Store: temi dell'utente in Dir (<dati utente>/calamaio/themes), un file <id>.json, e la cronologia <id>/<n>.json
// (ogni salvataggio è una versione; si tengono le ultime Keep).
type Store struct {
	Dir  string
	Now  func() time.Time
	Keep int
	mu   sync.Mutex
	auto map[string]autoMark // protetto da mu
}

type Info struct {
	ID       string         `json:"id"`
	Name     string         `json:"name"`
	Screen   string         `json:"screen"`
	CRC      uint32         `json:"crc"`
	Bytes    int            `json:"bytes"`
	Screens  map[string]int `json:"screens"`
	Updated  int64          `json:"updated"`
	Warnings []string       `json:"warnings"`
	Problems []Problem      `json:"problems,omitempty"`
}

type HistoryEntry struct {
	N     int   `json:"n"`
	At    int64 `json:"at"`
	Bytes int   `json:"bytes"`
}

func NewStore(dir string) *Store { return &Store{Dir: dir, Now: time.Now} }

func (s *Store) keep() int {
	if s.Keep > 0 {
		return s.Keep
	}
	return 20
}

func (s *Store) file(id string) string { return filepath.Join(s.Dir, id+".json") }

func validStoreID(id string) bool { return len(id) >= 1 && len(id) <= 24 && idRe.MatchString(id) }

func info(data []byte, mod time.Time) *Info {
	t, c, err := CompileJSON(data)
	in := &Info{Updated: mod.UnixMilli(), Screens: map[string]int{}, Warnings: []string{}}
	if t != nil {
		in.ID, in.Name, in.Screen = t.ID, t.Name, t.Screen
	}
	if c != nil {
		in.Screens, in.Warnings = c.Screens, append(in.Warnings, c.Warnings...)
		in.CRC, in.Bytes = c.CRC, len(c.Bin)
	}
	var ie *InvalidError
	if errors.As(err, &ie) {
		in.Problems = ie.Problems
	} else if err != nil {
		in.Problems = []Problem{{Msg: err.Error()}}
	}
	return in
}

// pretty: il file si salva indentato (si legge e si confronta con git).
//
// Si salva il JSON riletto e riscritto (chiavi ordinate, una sola copia di ogni chiave: vale l'ultima, come per
// Parse e per JSON.parse), mai i byte arrivati: una chiave doppia non fa arrivare su disco (né in /export) una
// schermata che il compilatore ha scartato.
func pretty(data []byte) []byte {
	var v any
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	if dec.Decode(&v) != nil {
		return data
	}
	var b bytes.Buffer
	enc := json.NewEncoder(&b)
	enc.SetEscapeHTML(false)
	enc.SetIndent("", "  ")
	if enc.Encode(v) != nil {
		return data
	}
	return b.Bytes()
}

func writeAtomic(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	tmp := path + ".tmp"
	f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return err
	}
	_, err = f.Write(data)
	if err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		os.Remove(tmp)
		return err
	}
	return nil
}

func (s *Store) List() ([]Info, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	ents, err := os.ReadDir(s.Dir)
	if errors.Is(err, os.ErrNotExist) {
		return []Info{}, nil
	}
	if err != nil {
		return nil, err
	}
	out := []Info{}
	for _, e := range ents {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		data, err := os.ReadFile(filepath.Join(s.Dir, e.Name()))
		fi, _ := e.Info()
		if err != nil || fi == nil {
			continue
		}
		in := info(data, fi.ModTime())
		if in.ID == "" {
			in.ID = strings.TrimSuffix(e.Name(), ".json")
		}
		out = append(out, *in)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out, nil
}

func (s *Store) Get(id string) ([]byte, *Info, error) {
	if !validStoreID(id) {
		return nil, nil, ErrNotFound
	}
	data, err := os.ReadFile(s.file(id))
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil, ErrNotFound
	}
	if err != nil {
		return nil, nil, err
	}
	mod := s.Now()
	if fi, err := os.Stat(s.file(id)); err == nil {
		mod = fi.ModTime()
	}
	return data, info(data, mod), nil
}

// Compiled: il CTHM del tema salvato (per mandarlo alla scheda).
func (s *Store) Compiled(id string) (*Compiled, error) {
	data, _, err := s.Get(id)
	if err != nil {
		return nil, err
	}
	_, c, err := CompileJSON(data)
	return c, err
}

// check: il tema deve essere valido; id = quello atteso ("" = qualunque).
func check(data []byte, id string) (*Theme, error) {
	t, _, err := CompileJSON(data)
	if err != nil {
		return t, err
	}
	if id != "" && t.ID != id {
		return t, &InvalidError{[]Problem{{Path: "id", Msg: fmt.Sprintf("l'id nel file (%q) non è quello del percorso (%q)", t.ID, id)}}}
	}
	return t, nil
}

func (s *Store) save(id string, data []byte) (*Info, error) {
	data = pretty(data)
	vs, err := s.versions(id)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	n := 1
	if len(vs) > 0 {
		n = vs[0].N + 1
	}
	// prima la cronologia, poi il file vivo: se fallisce, il tema resta com'era
	if err := writeAtomic(filepath.Join(s.Dir, id, strconv.Itoa(n)+".json"), data); err != nil {
		return nil, err
	}
	if err := writeAtomic(s.file(id), data); err != nil {
		os.Remove(filepath.Join(s.Dir, id, strconv.Itoa(n)+".json"))
		return nil, err
	}
	for _, v := range append([]HistoryEntry{{N: n}}, vs...)[min(len(vs)+1, s.keep()):] {
		os.Remove(filepath.Join(s.Dir, id, strconv.Itoa(v.N)+".json"))
	}
	now := s.Now()
	os.Chtimes(s.file(id), now, now)
	return info(data, now), nil
}

func (s *Store) Create(data []byte) (*Info, error) {
	t, err := check(data, "")
	if err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.auto, t.ID)
	if _, err := os.Stat(s.file(t.ID)); err == nil {
		return nil, ErrExists
	}
	return s.save(t.ID, data)
}

func (s *Store) Put(id string, data []byte) (*Info, error) {
	if id == ReservedID {
		return nil, ErrReserved
	}
	if !validStoreID(id) {
		return nil, ErrNotFound
	}
	if _, err := check(data, id); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.auto, id)
	return s.save(id, data)
}

func (s *Store) Delete(id string) error {
	if id == ReservedID {
		return ErrReserved
	}
	if !validStoreID(id) {
		return ErrNotFound
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.auto, id)
	if err := os.Remove(s.file(id)); errors.Is(err, os.ErrNotExist) {
		return ErrNotFound
	} else if err != nil {
		return err
	}
	return os.RemoveAll(filepath.Join(s.Dir, id))
}

func (s *Store) versions(id string) ([]HistoryEntry, error) {
	ents, err := os.ReadDir(filepath.Join(s.Dir, id))
	if err != nil {
		return nil, err
	}
	var out []HistoryEntry
	for _, e := range ents {
		n, err := strconv.Atoi(strings.TrimSuffix(e.Name(), ".json"))
		if err != nil || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		fi, err := e.Info()
		if err != nil {
			continue
		}
		out = append(out, HistoryEntry{N: n, At: fi.ModTime().UnixMilli(), Bytes: int(fi.Size())})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].N > out[j].N })
	return out, nil
}

func (s *Store) History(id string) ([]HistoryEntry, error) {
	if _, _, err := s.Get(id); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	vs, err := s.versions(id)
	if err != nil {
		return []HistoryEntry{}, nil
	}
	return vs, nil
}

func (s *Store) Restore(id string, n int) (*Info, error) {
	if !validStoreID(id) {
		return nil, ErrNotFound
	}
	data, err := os.ReadFile(filepath.Join(s.Dir, id, strconv.Itoa(n)+".json"))
	if errors.Is(err, os.ErrNotExist) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, err
	}
	return s.Put(id, data)
}

// Import: come Create, ma un id già usato diventa <id>-2, <id>-3… (il nome resta).
func (s *Store) Import(data []byte) (*Info, error) {
	t, err := check(data, "")
	if err != nil {
		return nil, err
	}
	var raw map[string]any
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	id := t.ID
	for k := 2; ; k++ {
		if _, err := os.Stat(s.file(id)); errors.Is(err, os.ErrNotExist) {
			break
		}
		if k > 99 {
			return nil, ErrExists
		}
		suffix := "-" + strconv.Itoa(k)
		base := t.ID
		if len(base)+len(suffix) > 24 {
			base = strings.TrimRight(base[:24-len(suffix)], "-")
		}
		id = base + suffix
	}
	delete(s.auto, id)
	raw["id"] = id
	b, _ := json.Marshal(raw)
	return s.save(id, b)
}

// Check: come info, senza scrivere niente (problemi e avvisi dentro).
func (s *Store) Check(data []byte) *Info { return info(data, s.Now()) }

// Autosave: come Put, più il controllo del conflitto (base > 0 e il file vivo è cambiato → ErrChanged, letto
// dentro il lucchetto) e l'unione: la versione creata da un Autosave meno di AutosaveMerge fa si riscrive.
func (s *Store) Autosave(id string, data []byte, base int64) (*Info, error) {
	return s.autosave(id, data, base, false)
}

// AutosaveFresh: come Autosave, ma senza unione («Sovrascrivi»): nasce sempre una versione nuova, così quella
// dell'altro dispositivo resta in cronologia. Il controllo della base resta.
func (s *Store) AutosaveFresh(id string, data []byte, base int64) (*Info, error) {
	return s.autosave(id, data, base, true)
}

func (s *Store) autosave(id string, data []byte, base int64, fresh bool) (*Info, error) {
	if id == ReservedID {
		return nil, ErrReserved
	}
	if !validStoreID(id) {
		return nil, ErrNotFound
	}
	if _, err := check(data, id); err != nil {
		return nil, err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if base > 0 {
		fi, err := os.Stat(s.file(id))
		if errors.Is(err, os.ErrNotExist) {
			return nil, ErrNotFound
		}
		if err != nil {
			return nil, err
		}
		if fi.ModTime().UnixMilli() != base {
			return nil, ErrChanged
		}
	}
	if fresh {
		delete(s.auto, id)
	}
	now := s.Now()
	vs, _ := s.versions(id)
	if mk, ok := s.auto[id]; ok && len(vs) > 0 && vs[0].N == mk.n && vs[0].At == mk.mod && now.Sub(mk.at) < AutosaveMerge {
		data = pretty(data)
		hist := filepath.Join(s.Dir, id, strconv.Itoa(mk.n)+".json")
		// prima il file vivo: se fallisce la cronologia resta com'era
		if err := writeAtomic(s.file(id), data); err != nil {
			return nil, err
		}
		if err := writeAtomic(hist, data); err != nil {
			return nil, err
		}
		os.Chtimes(hist, now, now)
		os.Chtimes(s.file(id), now, now)
		mk.mod = now.UnixMilli()
		s.auto[id] = mk
		return info(data, now), nil
	}
	in, err := s.save(id, data)
	if err != nil {
		return nil, err
	}
	if vs, _ = s.versions(id); len(vs) > 0 {
		if s.auto == nil {
			s.auto = map[string]autoMark{}
		}
		s.auto[id] = autoMark{vs[0].N, now, vs[0].At} // l'istante è quello della versione: non si sposta a ogni unione
	}
	return in, nil
}
