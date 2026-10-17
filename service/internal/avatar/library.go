package avatar

import (
	"embed"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

//go:generate go run ../../cmd/mkavatars ../../../avatars builtin
//go:generate go run ../../cmd/mkavatars ../../../avatars/ancore builtin/ancore *.anchors.json

// Copia di avatars/ del repository (cmd/mkavatars), con le ancore dei predefiniti (avatars/ancore/,
// docs/AVATAR.md §13.3). Inclusa nel binario: il servizio installato in ~/.local/bin non dipende
// dalla cartella del repository.
//
//go:embed builtin/*.avatar.json builtin/ancore/*.anchors.json
var builtinFS embed.FS

// Builtin restituisce gli avatar predefiniti inclusi nel binario.
func Builtin() fs.FS {
	sub, err := fs.Sub(builtinFS, "builtin")
	if err != nil {
		panic(err)
	}
	return sub
}

// Sorgenti di un avatar.
const (
	SourceBuiltin = "predefinito"
	SourceUser    = "utente"
)

// maxReadBytes: un file più grande non si legge nemmeno (il limite vero, 48 KB, lo dà Validate).
const maxReadBytes = 1 << 20

// Entry è un avatar valido della libreria, già impacchettato.
type Entry struct {
	ID          string
	Name        string
	Author      string
	Description string
	Source      string // predefinito | utente
	File        string // percorso del file (per i predefiniti: builtin/<id>.avatar.json)
	Frames      int
	Poses       []string // pose presenti, nell'ordine fisso
	JSON        []byte   // file com'è
	Avatar      *Avatar
	Bin         []byte // CAVT
	CRC         uint32
	// BinLegacy e CRCLegacy: il CAVT senza la posa run, per il firmware ≤ 0.6.0 (uguali a Bin e CRC
	// se l'avatar non ha run).
	BinLegacy []byte
	CRCLegacy uint32
	// Anchors: blocco CANC delle ancore (docs/AVATAR.md §13.4), legato a CRC; nil se l'avatar non ha
	// ancore (niente accessori su di lui). AnchorsFrom: "json" o "ancore/<id>.anchors.json".
	Anchors     []byte
	AnchorsFrom string
	Warnings    []string
	Stats       Stats
}

// Problem: un file della libreria dell'utente che non si carica.
type Problem struct {
	File   string   `json:"file"`
	Errors []string `json:"errors"`
}

// Catalog: la libreria in un istante.
type Catalog struct {
	Entries  []*Entry // predefiniti (Goccia per prima) poi quelli dell'utente, per id
	Problems []Problem
}

// Get cerca un avatar per id.
func (c Catalog) Get(id string) *Entry {
	for _, e := range c.Entries {
		if e.ID == id {
			return e
		}
	}
	return nil
}

// Library unisce gli avatar predefiniti e quelli dell'utente (<dati utente>/calamaio/avatars).
//
// Gli id dei predefiniti sono riservati: un file dell'utente con lo stesso id non li sostituisce
// (finisce tra i Problem). La cartella dell'utente si rilegge al massimo una volta al secondo
// (Invalidate la fa rileggere subito); i file già letti si riusano finché non cambiano.
type Library struct {
	BuiltinFS fs.FS
	UserDir   string
	// TTL: ogni quanto rileggere la cartella dell'utente (0 = 1 s).
	TTL time.Duration

	mu      sync.Mutex
	builtin []*Entry
	bprob   []Problem
	loaded  bool
	files   map[string]fileEntry
	cat     Catalog
	scanned time.Time
}

type fileEntry struct {
	mod  time.Time
	size int64
	e    *Entry
	prob *Problem
}

// NewLibrary: predefiniti inclusi nel binario più la cartella dell'utente (può non esistere).
func NewLibrary(userDir string) *Library {
	return &Library{BuiltinFS: Builtin(), UserDir: userDir}
}

var (
	ErrInvalid  = errors.New("avatar non valido")
	ErrExists   = errors.New("esiste già un avatar con questo id: usa --replace per sostituirlo")
	ErrReserved = errors.New("id riservato a un avatar predefinito: i predefiniti non si sostituiscono, scegli un altro id")
	ErrNoUser   = errors.New("cartella della libreria dell'utente non disponibile")
)

// load costruisce l'Entry di un file valido.
func load(raw []byte, source, file string) (*Entry, Result) { return loadWith(raw, source, file, nil) }

// loadWith: come load, con le ancore dal file accanto in sidecars (solo i predefiniti).
func loadWith(raw []byte, source, file string, sidecars fs.FS) (*Entry, Result) {
	r := Validate(raw)
	if !r.OK() {
		return nil, r
	}
	bin, err := Pack(r.Avatar)
	if err != nil {
		r.Errors = append(r.Errors, err.Error())
		return nil, r
	}
	if len(bin) > MaxBinBytes {
		r.Errors = append(r.Errors, fmt.Sprintf("binario di %d byte: il massimo è %d", len(bin), MaxBinBytes))
		return nil, r
	}
	a := r.Avatar
	legacy, err := PackLegacy(a)
	if err != nil {
		r.Errors = append(r.Errors, err.Error())
		return nil, r
	}
	from := ""
	if a.Anchors != nil {
		from = "json"
	} else if anc, errs := loadSidecar(sidecars, a, CRC(bin)); len(errs) > 0 {
		r.Errors = append(r.Errors, errs...)
		return nil, r
	} else if anc != nil {
		a.Anchors, from = anc, "ancore/"+a.ID+".anchors.json"
	}
	var canc []byte
	if a.Anchors != nil {
		if canc, err = PackAnchors(a, CRC(bin)); err != nil {
			r.Errors = append(r.Errors, err.Error())
			return nil, r
		}
	}
	return &Entry{BinLegacy: legacy, CRCLegacy: CRC(legacy), ID: a.ID, Name: a.Name, Author: a.Author, Description: a.Description, Source: source, File: file,
		Frames: a.Frames, Poses: a.Present(), JSON: raw, Avatar: a, Bin: bin, CRC: CRC(bin),
		Anchors: canc, AnchorsFrom: from, Warnings: r.Warnings, Stats: r.Stats}, r
}

func (l *Library) loadBuiltin() {
	if l.loaded {
		return
	}
	l.loaded = true
	if l.BuiltinFS == nil {
		return
	}
	names, _ := fs.Glob(l.BuiltinFS, "*.avatar.json")
	for _, n := range names {
		raw, err := fs.ReadFile(l.BuiltinFS, n)
		if err != nil {
			l.bprob = append(l.bprob, Problem{File: "builtin/" + n, Errors: []string{err.Error()}})
			continue
		}
		e, r := loadWith(raw, SourceBuiltin, "builtin/"+n, l.BuiltinFS)
		if e == nil {
			l.bprob = append(l.bprob, Problem{File: "builtin/" + n, Errors: r.Errors})
			continue
		}
		l.builtin = append(l.builtin, e)
	}
	sortEntries(l.builtin)
}

func sortEntries(list []*Entry) {
	sort.SliceStable(list, func(i, j int) bool {
		a, b := list[i].ID, list[j].ID
		if (a == DefaultID) != (b == DefaultID) {
			return a == DefaultID
		}
		return a < b
	})
}

// Invalidate fa rileggere la cartella dell'utente alla prossima richiesta.
func (l *Library) Invalidate() {
	l.mu.Lock()
	l.scanned = time.Time{}
	l.mu.Unlock()
}

// Catalog restituisce la libreria attuale.
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
	reserved := map[string]bool{}
	cat := Catalog{Entries: append([]*Entry(nil), l.builtin...), Problems: append([]Problem(nil), l.bprob...)}
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
			if de.IsDir() || !strings.HasSuffix(name, ".avatar.json") || strings.HasPrefix(name, ".") {
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
				fe = l.readUser(path, fi)
				l.files[path] = fe
			}
			switch {
			case fe.prob != nil:
				cat.Problems = append(cat.Problems, *fe.prob)
			case reserved[fe.e.ID]:
				cat.Problems = append(cat.Problems, Problem{File: path, Errors: []string{
					fmt.Sprintf("id %q riservato a un avatar predefinito: il file è ignorato", fe.e.ID)}})
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
	sortEntries(user)
	cat.Entries = append(cat.Entries, user...)
	l.cat = cat
	return cat
}

func (l *Library) readUser(path string, fi os.FileInfo) fileEntry {
	fe := fileEntry{mod: fi.ModTime(), size: fi.Size()}
	if fi.Size() > maxReadBytes {
		fe.prob = &Problem{File: path, Errors: []string{fmt.Sprintf("file di %d byte: il massimo è %d", fi.Size(), MaxJSONBytes)}}
		return fe
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		fe.prob = &Problem{File: path, Errors: []string{err.Error()}}
		return fe
	}
	e, r := load(raw, SourceUser, path)
	if e == nil {
		fe.prob = &Problem{File: path, Errors: r.Errors}
		return fe
	}
	if want := strings.TrimSuffix(filepath.Base(path), ".avatar.json"); want != e.ID {
		fe.prob = &Problem{File: path, Errors: []string{fmt.Sprintf("id %q diverso dal nome del file: il file deve chiamarsi %s.avatar.json", e.ID, e.ID)}}
		return fe
	}
	fe.e = e
	return fe
}

// Get cerca un avatar per id (nil se non c'è o non è valido).
func (l *Library) Get(id string) *Entry { return l.Catalog().Get(id) }

// Add valida raw e lo salva nella libreria dell'utente come <id>.avatar.json, byte per byte.
// Rifiuta gli id dei predefiniti sempre e quelli già presenti salvo replace. Restituisce anche
// l'esito della validazione (avvisi compresi).
func (l *Library) Add(raw []byte, replace bool) (*Entry, Result, error) {
	e, r := load(raw, SourceUser, "")
	if e == nil {
		return nil, r, ErrInvalid
	}
	if l.UserDir == "" {
		return nil, r, ErrNoUser
	}
	cat := l.Catalog()
	if old := cat.Get(e.ID); old != nil && old.Source == SourceBuiltin {
		return nil, r, ErrReserved
	}
	path := filepath.Join(l.UserDir, e.ID+".avatar.json")
	if _, err := os.Stat(path); err == nil && !replace {
		return nil, r, ErrExists
	}
	if err := os.MkdirAll(l.UserDir, 0o700); err != nil {
		return nil, r, err
	}
	tmp, err := os.CreateTemp(l.UserDir, ".add-*.tmp")
	if err != nil {
		return nil, r, err
	}
	_, werr := tmp.Write(raw)
	cerr := tmp.Close()
	if werr == nil {
		werr = cerr
	}
	if werr == nil {
		werr = os.Chmod(tmp.Name(), 0o644)
	}
	if werr == nil {
		werr = os.Rename(tmp.Name(), path)
	}
	if werr != nil {
		_ = os.Remove(tmp.Name())
		return nil, r, werr
	}
	e.File = path
	l.Invalidate()
	return e, r, nil
}
