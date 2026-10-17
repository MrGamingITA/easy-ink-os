package accessory

import (
	"embed"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"sort"
	"strings"
	"sync"
	"time"
)

//go:generate go run ../../cmd/mkavatars ../../../avatars/accessori builtin *.accessory.json

// Copia di avatars/accessori/ del repository (cmd/mkavatars), inclusa nel binario.
//
//go:embed builtin/*.accessory.json
var builtinFS embed.FS

// Builtin: gli accessori predefiniti inclusi nel binario.
func Builtin() fs.FS {
	sub, err := fs.Sub(builtinFS, "builtin")
	if err != nil {
		panic(err)
	}
	return sub
}

// CatalogOrder: ordine di sblocco del catalogo iniziale (specifica del gioco §1.5: cappellino,
// occhiali, sciarpa, papillon, corona, cuffie, fiocco, berretto da mago). I predefiniti che non
// sono qui vengono dopo, per id.
var CatalogOrder = []string{"cappellino", "occhiali", "sciarpa", "papillon", "corona", "cuffie", "fiocco", "mago"}

const (
	SourceBuiltin = "predefinito"
	SourceUser    = "utente"
	maxReadBytes  = 256 << 10
)

// Entry: un accessorio valido della libreria, già impacchettato.
type Entry struct {
	ID          string
	Name        string
	Author      string
	Description string
	Special     bool
	Source      string // predefinito | utente
	File        string
	JSON        []byte
	Accessory   *Accessory
	Bin         []byte // CACC
	CRC         uint32
	Warnings    []string
}

// Problem: un file della libreria dell'utente che non si carica.
type Problem struct {
	File   string   `json:"file"`
	Errors []string `json:"errors"`
}

// Catalog: la libreria in un istante. Entries: i predefiniti nell'ordine di CatalogOrder, poi
// quelli dell'utente per id.
type Catalog struct {
	Entries  []*Entry
	Problems []Problem
}

func (c Catalog) Get(id string) *Entry {
	for _, e := range c.Entries {
		if e.ID == id {
			return e
		}
	}
	return nil
}

// UnlockOrder: gli accessori che si sbloccano salendo di livello, in ordine: i predefiniti, poi
// quelli dell'utente non speciali. Gli speciali si sbloccano solo come premio (AddSpecial).
func (c Catalog) UnlockOrder() []string {
	var out []string
	for _, e := range c.Entries {
		if !e.Special {
			out = append(out, e.ID)
		}
	}
	return out
}

// Library: predefiniti + cartella dell'utente (<dati utente>/calamaio/accessories). Gli id dei
// predefiniti sono riservati. La cartella si rilegge al massimo una volta al secondo.
type Library struct {
	BuiltinFS fs.FS
	UserDir   string
	TTL       time.Duration

	mu      sync.Mutex
	builtin []*Entry
	bprob   []Problem
	loaded  bool
	cat     Catalog
	scanned time.Time
	files   map[string]fileEntry
	rev     uint64
	sig     string
}

type fileEntry struct {
	mod  time.Time
	size int64
	e    *Entry
	prob *Problem
}

func NewLibrary(userDir string) *Library { return &Library{BuiltinFS: Builtin(), UserDir: userDir} }

var (
	ErrInvalid  = errors.New("accessorio non valido")
	ErrExists   = errors.New("esiste già un accessorio con questo id: usa --replace per sostituirlo")
	ErrReserved = errors.New("id riservato a un accessorio predefinito: scegli un altro id")
	ErrNoUser   = errors.New("cartella degli accessori dell'utente non disponibile")
)

// Load: l'Entry di un file valido (senza controllare il nome del file).
func Load(raw []byte, source, file string) (*Entry, Result) {
	r := Validate(raw)
	if !r.OK() {
		return nil, r
	}
	bin, err := Pack(r.Accessory)
	if err != nil {
		r.Errors = append(r.Errors, err.Error())
		return nil, r
	}
	a := r.Accessory
	return &Entry{ID: a.ID, Name: a.Name, Author: a.Author, Description: a.Description, Special: a.Special,
		Source: source, File: file, JSON: raw, Accessory: a, Bin: bin, CRC: CRC(bin), Warnings: r.Warnings}, r
}

func (l *Library) loadBuiltin() {
	if l.loaded {
		return
	}
	l.loaded = true
	if l.BuiltinFS == nil {
		return
	}
	names, _ := fs.Glob(l.BuiltinFS, "*.accessory.json")
	for _, n := range names {
		raw, err := fs.ReadFile(l.BuiltinFS, n)
		if err != nil {
			l.bprob = append(l.bprob, Problem{File: "builtin/" + n, Errors: []string{err.Error()}})
			continue
		}
		e, r := Load(raw, SourceBuiltin, "builtin/"+n)
		if e == nil {
			l.bprob = append(l.bprob, Problem{File: "builtin/" + n, Errors: r.Errors})
			continue
		}
		l.builtin = append(l.builtin, e)
	}
	sort.SliceStable(l.builtin, func(i, j int) bool {
		a, b := slices.Index(CatalogOrder, l.builtin[i].ID), slices.Index(CatalogOrder, l.builtin[j].ID)
		if a < 0 {
			a = len(CatalogOrder)
		}
		if b < 0 {
			b = len(CatalogOrder)
		}
		if a != b {
			return a < b
		}
		return l.builtin[i].ID < l.builtin[j].ID
	})
}

// Invalidate fa rileggere la cartella dell'utente alla prossima richiesta.
func (l *Library) Invalidate() {
	l.mu.Lock()
	l.scanned = time.Time{}
	l.mu.Unlock()
}

// Rev cambia quando cambia il contenuto della libreria (id e CRC).
func (l *Library) Rev() uint64 {
	l.Catalog()
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.rev
}

func (l *Library) Catalog() Catalog {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.loadBuiltin()
	ttl := l.TTL
	if ttl == 0 {
		ttl = time.Second
	}
	if !l.scanned.IsZero() && time.Since(l.scanned) < ttl {
		return l.cat
	}
	l.scanned = time.Now()
	cat := Catalog{Entries: append([]*Entry(nil), l.builtin...), Problems: append([]Problem(nil), l.bprob...)}
	reserved := map[string]bool{}
	for _, e := range l.builtin {
		reserved[e.ID] = true
	}
	if l.files == nil {
		l.files = map[string]fileEntry{}
	}
	seen := map[string]bool{}
	var user []*Entry
	if l.UserDir != "" {
		ents, _ := os.ReadDir(l.UserDir)
		for _, de := range ents {
			name := de.Name()
			if de.IsDir() || !strings.HasSuffix(name, ".accessory.json") || strings.HasPrefix(name, ".") {
				continue
			}
			path := filepath.Join(l.UserDir, name)
			seen[path] = true
			fi, err := os.Stat(path)
			if err != nil || !fi.Mode().IsRegular() {
				continue
			}
			fe, ok := l.files[path]
			if !ok || !fe.mod.Equal(fi.ModTime()) || fe.size != fi.Size() {
				fe = readUser(path, fi)
				l.files[path] = fe
			}
			switch {
			case fe.prob != nil:
				cat.Problems = append(cat.Problems, *fe.prob)
			case reserved[fe.e.ID]:
				cat.Problems = append(cat.Problems, Problem{File: path, Errors: []string{
					fmt.Sprintf("id %q riservato a un accessorio predefinito: il file è ignorato", fe.e.ID)}})
			default:
				user = append(user, fe.e)
			}
		}
	}
	for p := range l.files {
		if !seen[p] {
			delete(l.files, p)
		}
	}
	sort.Slice(user, func(i, j int) bool { return user[i].ID < user[j].ID })
	cat.Entries = append(cat.Entries, user...)
	var sig strings.Builder
	for _, e := range cat.Entries {
		fmt.Fprintf(&sig, "%s:%08x;", e.ID, e.CRC)
	}
	if s := sig.String(); s != l.sig {
		l.sig = s
		l.rev++
	}
	l.cat = cat
	return cat
}

func readUser(path string, fi os.FileInfo) fileEntry {
	fe := fileEntry{mod: fi.ModTime(), size: fi.Size()}
	if fi.Size() > maxReadBytes {
		fe.prob = &Problem{File: path, Errors: []string{fmt.Sprintf("file di %d byte: troppo grande", fi.Size())}}
		return fe
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		fe.prob = &Problem{File: path, Errors: []string{err.Error()}}
		return fe
	}
	e, r := Load(raw, SourceUser, path)
	if e == nil {
		fe.prob = &Problem{File: path, Errors: r.Errors}
		return fe
	}
	if want := strings.TrimSuffix(filepath.Base(path), ".accessory.json"); want != e.ID {
		fe.prob = &Problem{File: path, Errors: []string{fmt.Sprintf(`id "%s" diverso dal nome del file: deve chiamarsi %s.accessory.json`, e.ID, e.ID)}}
		return fe
	}
	fe.e = e
	return fe
}

func (l *Library) Get(id string) *Entry { return l.Catalog().Get(id) }

// Add valida raw e lo salva nella cartella dell'utente come <id>.accessory.json, byte per byte
// (scrittura atomica). Rifiuta gli id dei predefiniti e quelli già presenti senza replace.
func (l *Library) Add(raw []byte, replace bool) (*Entry, Result, error) {
	e, r := Load(raw, SourceUser, "")
	if e == nil {
		return nil, r, ErrInvalid
	}
	if l.UserDir == "" {
		return nil, r, ErrNoUser
	}
	if old := l.Catalog().Get(e.ID); old != nil && old.Source == SourceBuiltin {
		return nil, r, ErrReserved
	}
	path := filepath.Join(l.UserDir, e.ID+".accessory.json")
	if _, err := os.Stat(path); err == nil && !replace {
		return nil, r, ErrExists
	}
	if err := writeAtomic(l.UserDir, path, raw, 0o644); err != nil {
		return nil, r, err
	}
	e.File = path
	l.Invalidate()
	return e, r, nil
}

// writeAtomic: file temporaneo nella stessa cartella, poi rename (mai un file a metà).
func writeAtomic(dir, path string, data []byte, mode os.FileMode) error {
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(dir, ".tmp-*")
	if err != nil {
		return err
	}
	_, werr := tmp.Write(data)
	if werr == nil {
		werr = tmp.Sync()
	}
	if cerr := tmp.Close(); werr == nil {
		werr = cerr
	}
	if werr == nil {
		werr = os.Chmod(tmp.Name(), mode)
	}
	if werr == nil {
		werr = os.Rename(tmp.Name(), path)
	}
	if werr != nil {
		_ = os.Remove(tmp.Name())
	}
	return werr
}
