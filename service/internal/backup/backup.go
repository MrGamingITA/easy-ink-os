// Package backup è l'archivio dei backup della flash delle schede: un file <id>.bin per backup e i
// metadati in <id>.json accanto, nella cartella <dati utente>/calamaio/backups (0700, file 0600).
//
// Regole: un backup non si sovrascrive mai (i file si pubblicano con un hard link, che fallisce se il
// nome esiste già); l'id è generato dal servizio e validato a ogni accesso (niente percorsi); i backup
// protetti (l'originale di fabbrica) non si cancellano da nessuna strada e hanno i file in sola lettura.
package backup

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode"
	"unicode/utf8"
)

// MaxSize: dimensione massima di un backup (16 MiB = la flash più grande che il frontend legge in un
// colpo solo sulle schede previste; la Heltec ne ha 8).
const MaxSize = 16 << 20

const (
	KindFactory = "factory"
	KindUser    = "user"
)

// Permessi: cartella e file privati; i file dei protetti in sola lettura anche per il proprietario.
const (
	DirPerm       fs.FileMode = 0o700
	FilePerm      fs.FileMode = 0o600
	ProtectedPerm fs.FileMode = 0o400
)

// Backup: i metadati, forma identica a quella della webapp (GET /api/backups).
type Backup struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	Kind      string `json:"kind"`
	Protected bool   `json:"protected"`
	Board     string `json:"board"`
	Chip      string `json:"chip"`
	MAC       string `json:"mac"`
	FlashSize int64  `json:"flashSize"`
	Offset    int64  `json:"offset"`
	Length    int64  `json:"length"`
	Size      int64  `json:"size"`
	SHA256    string `json:"sha256"`
	Created   int64  `json:"created"` // ms epoch, come gli altri tempi del servizio
	Note      string `json:"note"`
}

// Meta: quello che arriva da chi crea il backup. SHA256, se c'è, deve coincidere con quello calcolato.
type Meta struct {
	Name      string
	Chip      string
	MAC       string
	Board     string
	FlashSize int64
	Offset    int64
	Note      string
	SHA256    string
}

var (
	ErrNotFound  = errors.New("backup sconosciuto")
	ErrProtected = errors.New("backup protetto: non si elimina")
	ErrTooLarge  = fmt.Errorf("backup troppo grande (massimo %d MiB)", MaxSize>>20)
	ErrEmpty     = errors.New("backup vuoto")
	ErrExists    = errors.New("esiste già un backup con questo id")
)

// InvalidError: parametri non validi (400 nella API).
type InvalidError struct{ Msg string }

func (e *InvalidError) Error() string { return e.Msg }

func invalid(format string, a ...any) error { return &InvalidError{Msg: fmt.Sprintf(format, a...)} }

var (
	idRe    = regexp.MustCompile(`^[a-z0-9][a-z0-9-]{0,63}$`)
	chipRe  = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9 _-]{0,31}$`)
	boardRe = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9 ._-]{0,63}$`)
	hexRe   = regexp.MustCompile(`^[0-9a-f]{64}$`)
)

// ValidID: un id fatto solo di [a-z0-9-], quindi mai un percorso.
func ValidID(id string) bool { return idRe.MatchString(id) }

// FactoryID: id fisso dell'originale di fabbrica di una scheda (uno per MAC).
func FactoryID(mac string) string { return "factory-" + strings.ReplaceAll(mac, ":", "") }

// NormalizeMAC accetta aa:bb:cc:dd:48:e8, AC-A7-…, aabbccdd48e8; restituisce la forma con i due punti.
func NormalizeMAC(s string) (string, error) {
	h := strings.ToLower(strings.NewReplacer(":", "", "-", "", ".", "").Replace(strings.TrimSpace(s)))
	if len(h) != 12 {
		return "", invalid("MAC non valido: %q", s)
	}
	if _, err := hex.DecodeString(h); err != nil {
		return "", invalid("MAC non valido: %q", s)
	}
	parts := make([]string, 6)
	for i := range parts {
		parts[i] = h[2*i : 2*i+2]
	}
	return strings.Join(parts, ":"), nil
}

// BoardFromMAC: nome della scheda come in PROTOCOLLO.md (calamaio-xxxx, ultime 4 cifre del MAC).
func BoardFromMAC(mac string) string {
	h := strings.ReplaceAll(mac, ":", "")
	if len(h) < 4 {
		return ""
	}
	return "calamaio-" + h[len(h)-4:]
}

// ParseSize accetta un intero decimale o esadecimale (0x…), da 0 a 1 GiB.
func ParseSize(field, s string) (int64, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0, nil
	}
	n, err := strconv.ParseInt(s, 0, 64)
	if err != nil || n < 0 || n > 1<<30 {
		return 0, invalid("%s non valido: %q", field, s)
	}
	return n, nil
}

func cleanText(field, s string, max int, required bool) (string, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		if required {
			return "", invalid("manca %s", field)
		}
		return "", nil
	}
	if !utf8.ValidString(s) || utf8.RuneCountInString(s) > max {
		return "", invalid("%s non valido (massimo %d caratteri)", field, max)
	}
	for _, r := range s {
		if unicode.IsControl(r) {
			return "", invalid("%s contiene caratteri di controllo", field)
		}
	}
	return s, nil
}

// normalize valida e ripulisce i metadati. chip e name sono obbligatori.
func (m Meta) normalize() (Meta, error) {
	var err error
	if m.Name, err = cleanText("name", m.Name, 120, true); err != nil {
		return m, err
	}
	if m.Note, err = cleanText("note", m.Note, 500, false); err != nil {
		return m, err
	}
	m.Chip = strings.TrimSpace(m.Chip)
	if m.Chip == "" {
		return m, invalid("manca chip")
	}
	if !chipRe.MatchString(m.Chip) {
		return m, invalid("chip non valido: %q", m.Chip)
	}
	if strings.TrimSpace(m.MAC) != "" {
		if m.MAC, err = NormalizeMAC(m.MAC); err != nil {
			return m, err
		}
	} else {
		m.MAC = ""
	}
	m.Board = strings.TrimSpace(m.Board)
	if m.Board != "" && !boardRe.MatchString(m.Board) {
		return m, invalid("board non valido: %q", m.Board)
	}
	if m.FlashSize < 0 || m.FlashSize > 1<<30 || m.Offset < 0 || m.Offset > 1<<30 {
		return m, invalid("flashSize o offset fuori intervallo")
	}
	m.SHA256 = strings.ToLower(strings.TrimSpace(m.SHA256))
	if m.SHA256 != "" && !hexRe.MatchString(m.SHA256) {
		return m, invalid("sha256 non valido: servono 64 cifre esadecimali")
	}
	return m, nil
}

// Store: l'archivio su disco. Si rilegge dalla cartella a ogni elenco, così il servizio vede subito
// quello che fa la riga di comando (e viceversa).
type Store struct {
	Dir string
	Now func() time.Time

	mu sync.Mutex // serializza scritture e cancellazioni nello stesso processo
}

// Open crea la cartella (0700) se manca e ne stringe i permessi se sono più larghi.
func Open(dir string) (*Store, error) {
	if dir == "" {
		return nil, errors.New("cartella dei backup non indicata")
	}
	if err := os.MkdirAll(dir, DirPerm); err != nil {
		return nil, err
	}
	st, err := os.Stat(dir)
	if err != nil {
		return nil, err
	}
	if !st.IsDir() {
		return nil, fmt.Errorf("%s non è una cartella", dir)
	}
	if runtime.GOOS != "windows" && st.Mode().Perm() != DirPerm {
		if err := os.Chmod(dir, DirPerm); err != nil {
			return nil, err
		}
	}
	return &Store{Dir: dir, Now: time.Now}, nil
}

func (s *Store) binPath(id string) string  { return filepath.Join(s.Dir, id+".bin") }
func (s *Store) metaPath(id string) string { return filepath.Join(s.Dir, id+".json") }

// List: tutti i backup completi (metadati e dati presenti, dimensione coerente). Prima gli originali
// di fabbrica, poi i più recenti.
func (s *Store) List() ([]Backup, error) {
	ents, err := os.ReadDir(s.Dir)
	if err != nil {
		return nil, err
	}
	out := []Backup{}
	for _, e := range ents {
		name := e.Name()
		id, ok := strings.CutSuffix(name, ".json")
		if !ok || !ValidID(id) || !e.Type().IsRegular() {
			continue
		}
		b, err := s.Get(id)
		if err != nil {
			continue
		}
		out = append(out, b)
	}
	sort.SliceStable(out, func(i, j int) bool {
		fi, fj := out[i].Kind == KindFactory, out[j].Kind == KindFactory
		if fi != fj {
			return fi
		}
		if out[i].Created != out[j].Created {
			return out[i].Created > out[j].Created
		}
		return out[i].ID < out[j].ID
	})
	return out, nil
}

// Get legge i metadati e controlla che il file dei dati ci sia e abbia la dimensione registrata.
func (s *Store) Get(id string) (Backup, error) {
	if !ValidID(id) {
		return Backup{}, ErrNotFound
	}
	raw, err := os.ReadFile(s.metaPath(id))
	if errors.Is(err, fs.ErrNotExist) {
		return Backup{}, ErrNotFound
	}
	if err != nil {
		return Backup{}, err
	}
	var b Backup
	if err := json.Unmarshal(raw, &b); err != nil {
		return Backup{}, fmt.Errorf("metadati di %s illeggibili: %w", id, err)
	}
	if b.ID != id {
		return Backup{}, fmt.Errorf("metadati di %s con id %q", id, b.ID)
	}
	st, err := os.Lstat(s.binPath(id))
	if err != nil {
		return Backup{}, fmt.Errorf("dati di %s mancanti: %w", id, err)
	}
	if !st.Mode().IsRegular() || st.Size() != b.Size {
		return Backup{}, fmt.Errorf("dati di %s non coerenti (%d byte invece di %d)", id, st.Size(), b.Size)
	}
	return b, nil
}

// OpenData apre il file dei dati in sola lettura.
func (s *Store) OpenData(id string) (*os.File, Backup, error) {
	b, err := s.Get(id)
	if err != nil {
		return nil, Backup{}, err
	}
	f, err := os.Open(s.binPath(id))
	if err != nil {
		return nil, Backup{}, err
	}
	return f, b, nil
}

// isProtected: protetto se lo dicono i metadati, se è un originale di fabbrica o se l'id lo è.
// Tre segnali indipendenti: un file di metadati alterato non basta a renderlo cancellabile.
func isProtected(id string, b Backup) bool {
	return b.Protected || b.Kind == KindFactory || strings.HasPrefix(id, "factory-")
}

// Delete cancella un backup dell'utente. I protetti danno ErrProtected.
func (s *Store) Delete(id string) error {
	if !ValidID(id) {
		return ErrNotFound
	}
	if strings.HasPrefix(id, "factory-") {
		if _, err := os.Lstat(s.metaPath(id)); err != nil {
			return ErrNotFound
		}
		return ErrProtected
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	raw, err := os.ReadFile(s.metaPath(id))
	if errors.Is(err, fs.ErrNotExist) {
		return ErrNotFound
	}
	if err != nil {
		return err
	}
	var b Backup
	if err := json.Unmarshal(raw, &b); err != nil {
		// Metadati illeggibili: non si sa se è protetto, quindi non si tocca.
		return fmt.Errorf("metadati di %s illeggibili: non lo elimino", id)
	}
	if isProtected(id, b) {
		return ErrProtected
	}
	// Prima i metadati (il backup sparisce dall'elenco), poi i dati.
	if err := os.Remove(s.metaPath(id)); err != nil {
		return err
	}
	if err := os.Remove(s.binPath(id)); err != nil && !errors.Is(err, fs.ErrNotExist) {
		return err
	}
	syncDir(s.Dir)
	return nil
}

// Create salva un backup dell'utente con un id nuovo. r si legge fino a MaxSize byte.
func (s *Store) Create(m Meta, r io.Reader) (Backup, error) {
	id, err := newUserID()
	if err != nil {
		return Backup{}, err
	}
	return s.create(id, KindUser, false, s.Now().UnixMilli(), m, r)
}

func newUserID() (string, error) {
	var b [6]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return "u-" + hex.EncodeToString(b[:]), nil
}

func (s *Store) create(id, kind string, protected bool, created int64, m Meta, r io.Reader) (Backup, error) {
	if !ValidID(id) {
		return Backup{}, invalid("id non valido: %q", id)
	}
	m, err := m.normalize()
	if err != nil {
		return Backup{}, err
	}
	perm := FilePerm
	if protected {
		perm = ProtectedPerm
	}

	s.mu.Lock()
	defer s.mu.Unlock()
	if _, err := os.Lstat(s.metaPath(id)); err == nil {
		return Backup{}, ErrExists
	}

	// 1. dati in un file temporaneo, con SHA-256 calcolato mentre si scrive
	tmp, err := os.CreateTemp(s.Dir, ".tmp-*.bin")
	if err != nil {
		return Backup{}, err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // dopo il link resta solo il nome definitivo
	h := sha256.New()
	n, err := io.Copy(io.MultiWriter(tmp, h), io.LimitReader(r, MaxSize+1))
	if err == nil {
		err = tmp.Sync()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return Backup{}, err
	}
	switch {
	case n > MaxSize:
		return Backup{}, ErrTooLarge
	case n == 0:
		return Backup{}, ErrEmpty
	}
	sum := hex.EncodeToString(h.Sum(nil))
	if m.SHA256 != "" && m.SHA256 != sum {
		return Backup{}, &ChecksumError{Want: m.SHA256, Got: sum}
	}
	if m.FlashSize > 0 && m.Offset+n > m.FlashSize {
		return Backup{}, invalid("la regione (%d byte da 0x%x) esce dalla flash di %d byte", n, m.Offset, m.FlashSize)
	}

	b := Backup{ID: id, Name: m.Name, Kind: kind, Protected: protected, Board: m.Board, Chip: m.Chip, MAC: m.MAC,
		FlashSize: m.FlashSize, Offset: m.Offset, Length: n, Size: n, SHA256: sum, Created: created, Note: m.Note}

	// 2. pubblicazione dei dati: il link non sovrascrive mai un file esistente.
	binDone := false
	if err := os.Link(tmpName, s.binPath(id)); err != nil {
		if !errors.Is(err, fs.ErrExist) {
			return Backup{}, err
		}
		// Dati rimasti da una scrittura interrotta (metadati assenti): si riusano solo se identici.
		if got, herr := fileSHA256(s.binPath(id)); herr != nil || got != sum {
			return Backup{}, ErrExists
		}
	} else {
		binDone = true
		// Permessi definitivi dopo il link: su Windows un file in sola lettura non si potrebbe
		// più togliere dal nome temporaneo.
		_ = os.Remove(tmpName)
		if err := os.Chmod(s.binPath(id), perm); err != nil {
			_ = os.Remove(s.binPath(id))
			return Backup{}, err
		}
	}

	// 3. metadati, stessa tecnica: finché non ci sono, il backup non esiste.
	if err := s.writeMeta(b, perm); err != nil {
		if binDone {
			_ = os.Remove(s.binPath(id))
		}
		return Backup{}, err
	}
	syncDir(s.Dir)
	return b, nil
}

func (s *Store) writeMeta(b Backup, perm fs.FileMode) error {
	raw, err := json.MarshalIndent(b, "", "  ")
	if err != nil {
		return err
	}
	tmp, err := os.CreateTemp(s.Dir, ".tmp-*.json")
	if err != nil {
		return err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName)
	_, err = tmp.Write(append(raw, '\n'))
	if err == nil {
		err = tmp.Sync()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return err
	}
	if err := os.Link(tmpName, s.metaPath(b.ID)); err != nil {
		if errors.Is(err, fs.ErrExist) {
			return ErrExists
		}
		return err
	}
	_ = os.Remove(tmpName)
	return os.Chmod(s.metaPath(b.ID), perm)
}

// ChecksumError: lo SHA-256 dichiarato non coincide con quello dei dati.
type ChecksumError struct{ Want, Got string }

func (e *ChecksumError) Error() string {
	return fmt.Sprintf("SHA-256 diverso: atteso %s, calcolato %s", e.Want, e.Got)
}

func fileSHA256(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

// syncDir rende durevoli i rename/link (su Windows non serve e non si può).
func syncDir(dir string) {
	if runtime.GOOS == "windows" {
		return
	}
	if d, err := os.Open(dir); err == nil {
		_ = d.Sync()
		_ = d.Close()
	}
}

// FileName: nome sensato per lo scaricamento (solo ASCII, come lo calcola la webapp).
func FileName(b Backup) string {
	var sb strings.Builder
	dash := false
	for _, r := range b.Name {
		ok := r < 128 && (unicode.IsLetter(r) || unicode.IsDigit(r) || r == '_' || r == '.' || r == '-')
		if ok {
			sb.WriteRune(r)
			dash = r == '-'
		} else if !dash {
			sb.WriteByte('-')
			dash = true
		}
	}
	name := strings.Trim(sb.String(), "-.")
	if name == "" {
		name = b.ID
	}
	if len(name) > 100 {
		name = strings.TrimRight(name[:100], "-.")
	}
	if !strings.HasSuffix(strings.ToLower(name), ".bin") {
		name += ".bin"
	}
	return name
}
