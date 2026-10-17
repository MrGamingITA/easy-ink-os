// Package phones: telefoni abbinati al servizio (app iPhone, spec 2026-10-16 §3): token, finestra di
// abbinamento, certificato e link del QR. Il token non si salva mai: in phones.json c'è solo il suo SHA-256.
package phones

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode"
)

const (
	TokenBytes = 32 // 256 bit, 64 cifre esadecimali
	MaxName    = 40 // caratteri del nome del telefono
	// TouchEvery: l'ultimo uso va su disco al massimo così spesso (in memoria è sempre aggiornato).
	TouchEvery = 10 * time.Minute
)

var ErrNotFound = errors.New("telefono sconosciuto")

// Phone è un telefono abbinato. TokenHash = SHA-256 del token (le 64 cifre come testo), in esadecimale.
type Phone struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	TokenHash string `json:"tokenSha256"`
	Paired    int64  `json:"paired"`   // ms
	LastUsed  int64  `json:"lastUsed"` // ms, 0 = mai
}

// Store: l'elenco dei telefoni, in <dati>/phones.json (0600). Path vuoto = solo in memoria (test).
type Store struct {
	Path string
	Now  func() time.Time

	mu     sync.Mutex
	phones []Phone
	saved  map[string]int64 // LastUsed già scritto su disco, per telefono
}

type storeFile struct {
	V      int     `json:"v"`
	Phones []Phone `json:"phones"`
}

// Open legge l'elenco. Un file illeggibile è un errore e non si tocca: chi chiama spegne l'abbinamento.
func Open(path string) (*Store, error) {
	s := &Store{Path: path, saved: map[string]int64{}}
	if path == "" {
		return s, nil
	}
	b, err := os.ReadFile(path)
	if errors.Is(err, fs.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return nil, err
	}
	var f storeFile
	if err := json.Unmarshal(b, &f); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	for _, p := range f.Phones {
		if p.ID != "" && isLowerHex(p.TokenHash, 64) {
			s.phones = append(s.phones, p)
			s.saved[p.ID] = p.LastUsed
		}
	}
	return s, nil
}

func (s *Store) now() time.Time {
	if s.Now != nil {
		return s.Now()
	}
	return time.Now()
}

// ValidToken: 64 cifre esadecimali minuscole.
func ValidToken(t string) bool { return isLowerHex(t, 2*TokenBytes) }

func isLowerHex(s string, n int) bool {
	if len(s) != n {
		return false
	}
	for _, c := range s {
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f') {
			return false
		}
	}
	return true
}

func hashToken(t string) string {
	sum := sha256.Sum256([]byte(t))
	return hex.EncodeToString(sum[:])
}

func randomHex(n int) string {
	b := make([]byte, n)
	_, _ = rand.Read(b) // dalla 1.24 non fallisce mai
	return hex.EncodeToString(b)
}

// cleanName: niente caratteri di controllo, al massimo MaxName caratteri; vuoto = «iPhone».
func cleanName(name string) string {
	var b strings.Builder
	n := 0
	for _, r := range strings.TrimSpace(name) {
		if unicode.IsControl(r) {
			continue
		}
		if n == MaxName {
			break
		}
		b.WriteRune(r)
		n++
	}
	if out := strings.TrimSpace(b.String()); out != "" {
		return out
	}
	return "iPhone"
}

// Add abbina un telefono nuovo e restituisce il suo token, che da qui in poi esiste solo nel telefono.
func (s *Store) Add(name string) (string, Phone, error) {
	tok := randomHex(TokenBytes)
	p := Phone{ID: randomHex(6), Name: cleanName(name), TokenHash: hashToken(tok), Paired: s.now().UnixMilli()}
	s.mu.Lock()
	defer s.mu.Unlock()
	next := append(append([]Phone(nil), s.phones...), p)
	if err := s.write(next); err != nil {
		return "", Phone{}, err
	}
	s.phones = next
	s.saved[p.ID] = 0
	return tok, p, nil
}

// Lookup: il telefono del token. Confronta tutti gli hash a tempo costante.
func (s *Store) Lookup(token string) (Phone, bool) {
	if !ValidToken(token) {
		return Phone{}, false
	}
	want := []byte(hashToken(token))
	s.mu.Lock()
	defer s.mu.Unlock()
	var found Phone
	ok := false
	for _, p := range s.phones {
		if subtle.ConstantTimeCompare([]byte(p.TokenHash), want) == 1 {
			found, ok = p, true
		}
	}
	return found, ok
}

func (s *Store) Has(id string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, p := range s.phones {
		if p.ID == id {
			return true
		}
	}
	return false
}

// Touch segna l'ultimo uso; su disco al massimo ogni TouchEvery (il primo uso subito).
func (s *Store) Touch(id string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := s.now().UnixMilli()
	for i := range s.phones {
		if s.phones[i].ID != id {
			continue
		}
		s.phones[i].LastUsed = now
		if now-s.saved[id] >= TouchEvery.Milliseconds() || s.saved[id] == 0 {
			if s.write(s.phones) == nil {
				s.saved[id] = now
			}
		}
		return
	}
}

// Flush scrive gli ultimi usi rimasti in memoria (allo spegnimento).
func (s *Store) Flush() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	dirty := false
	for _, p := range s.phones {
		if p.LastUsed != s.saved[p.ID] {
			dirty = true
		}
	}
	if !dirty {
		return nil
	}
	if err := s.write(s.phones); err != nil {
		return err
	}
	for _, p := range s.phones {
		s.saved[p.ID] = p.LastUsed
	}
	return nil
}

// Revoke toglie il telefono. Vale subito in memoria anche se phones.json non si scrive (l'errore torna a chi
// chiama, che lo mostra): un telefono revocato non deve restare valido per un disco pieno.
func (s *Store) Revoke(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	next := make([]Phone, 0, len(s.phones))
	for _, p := range s.phones {
		if p.ID != id {
			next = append(next, p)
		}
	}
	if len(next) == len(s.phones) {
		return ErrNotFound
	}
	s.phones = next
	delete(s.saved, id)
	return s.write(next)
}

// List: copia dell'elenco, dal più vecchio.
func (s *Store) List() []Phone {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := append([]Phone(nil), s.phones...)
	sort.SliceStable(out, func(i, j int) bool { return out[i].Paired < out[j].Paired })
	return out
}

func (s *Store) Len() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.phones)
}

// write: file temporaneo 0600 e rinomina (mai un phones.json a metà). Chiamata col lucchetto preso.
func (s *Store) write(list []Phone) error {
	if s.Path == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(s.Path), 0o700); err != nil {
		return err
	}
	if list == nil {
		list = []Phone{}
	}
	b, err := json.MarshalIndent(storeFile{V: 1, Phones: list}, "", "  ")
	if err != nil {
		return err
	}
	tmp := s.Path + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	if err := os.Chmod(tmp, 0o600); err != nil { // WriteFile non cambia i permessi di un file già esistente
		return err
	}
	return os.Rename(tmp, s.Path)
}
