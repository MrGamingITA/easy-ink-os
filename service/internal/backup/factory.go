package backup

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"

	"calamaio/internal/firmware"
)

// FactorySource: da dove importare l'originale di fabbrica di una scheda (flag --factory-backup,
// config factory_backups, oppure il default di sviluppo nel repository).
type FactorySource struct {
	File      string `json:"file"`
	MAC       string `json:"mac"`
	Chip      string `json:"chip,omitempty"`  // vuoto = letto dall'intestazione del bootloader
	Board     string `json:"board,omitempty"` // vuoto = calamaio-xxxx dal MAC
	Name      string `json:"name,omitempty"`
	FlashSize int64  `json:"flashSize,omitempty"` // vuoto = dimensione del file (flash completa)
	SHA256    string `json:"sha256,omitempty"`    // vuoto = SHA256.txt o <file>.sha256 accanto al file
	Note      string `json:"note,omitempty"`
}

// ImportResult: esito dell'importazione di una sorgente.
type ImportResult struct {
	Backup   Backup
	Imported bool // false = c'era già un originale di fabbrica per quel MAC
}

// ImportFactory copia (non sposta) la flash di fabbrica nell'archivio, come backup protetto, se per
// quel MAC non ce n'è già uno. Prima controlla lo SHA-256 atteso: senza, non importa.
func (s *Store) ImportFactory(src FactorySource) (ImportResult, error) {
	if src.File == "" {
		return ImportResult{}, errors.New("file dell'originale di fabbrica non indicato")
	}
	mac, err := NormalizeMAC(src.MAC)
	if err != nil {
		return ImportResult{}, fmt.Errorf("originale di fabbrica %s: %w", src.File, err)
	}
	list, err := s.List()
	if err != nil {
		return ImportResult{}, err
	}
	for _, b := range list {
		if b.Kind == KindFactory && b.MAC == mac {
			return ImportResult{Backup: b}, nil
		}
	}
	if b, err := s.Get(FactoryID(mac)); err == nil {
		return ImportResult{Backup: b}, nil
	}

	want, from, err := expectedSHA(src)
	if err != nil {
		return ImportResult{}, err
	}
	f, err := os.Open(src.File)
	if err != nil {
		return ImportResult{}, err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return ImportResult{}, err
	}
	if !st.Mode().IsRegular() {
		return ImportResult{}, fmt.Errorf("%s non è un file", src.File)
	}
	size := st.Size()
	flash := src.FlashSize
	if flash == 0 {
		flash = size
	}
	if size != flash {
		return ImportResult{}, fmt.Errorf("%s: %d byte, attesa la flash completa di %d", src.File, size, flash)
	}
	chip := src.Chip
	if chip == "" {
		var hdr [24]byte
		if _, err := io.ReadFull(f, hdr[:]); err == nil {
			chip, _ = firmware.ChipOfImage(hdr[:])
		}
		if _, err := f.Seek(0, io.SeekStart); err != nil {
			return ImportResult{}, err
		}
		if chip == "" || strings.HasPrefix(chip, "chip ") {
			return ImportResult{}, fmt.Errorf("%s: chip non riconosciuto, indicalo nella config", src.File)
		}
	}
	board := src.Board
	if board == "" {
		board = BoardFromMAC(mac)
	}
	name := src.Name
	if name == "" {
		name = "Originale di fabbrica · " + board
	}
	note := src.Note
	if note == "" {
		note = "Flash completa com’era alla consegna. Copia di " + filepath.Base(src.File) + ", SHA-256 verificato con " + from + "."
	}
	m := Meta{Name: name, Chip: chip, MAC: mac, Board: board, FlashSize: flash, Offset: 0, Note: note, SHA256: want}
	// Data del backup = quando è stata letta la flash (data del file), non quella dell'importazione.
	b, err := s.create(FactoryID(mac), KindFactory, true, st.ModTime().UnixMilli(), m, f)
	if err != nil {
		var ce *ChecksumError
		if errors.As(err, &ce) {
			return ImportResult{}, fmt.Errorf("%s: SHA-256 diverso da %s (atteso %s, calcolato %s): non importato", src.File, from, ce.Want, ce.Got)
		}
		return ImportResult{}, fmt.Errorf("%s: %w", src.File, err)
	}
	return ImportResult{Backup: b, Imported: true}, nil
}

// expectedSHA: lo SHA-256 atteso e da dove viene (config, SHA256.txt o <file>.sha256 accanto).
func expectedSHA(src FactorySource) (sum, from string, err error) {
	if src.SHA256 != "" {
		sum = strings.ToLower(strings.TrimSpace(src.SHA256))
		if !hexRe.MatchString(sum) {
			return "", "", fmt.Errorf("%s: sha256 non valido nella config", src.File)
		}
		from = "lo SHA-256 atteso"
	}
	base := filepath.Base(src.File)
	for _, cand := range []string{filepath.Join(filepath.Dir(src.File), "SHA256.txt"), src.File + ".sha256"} {
		got, ok, rerr := shaFromList(cand, base)
		if rerr != nil {
			return "", "", rerr
		}
		if !ok {
			continue
		}
		if sum != "" && got != sum {
			return "", "", fmt.Errorf("%s: lo SHA-256 della config e quello di %s non coincidono", src.File, filepath.Base(cand))
		}
		if sum == "" {
			sum, from = got, filepath.Base(cand)
		} else {
			from += " e con " + filepath.Base(cand)
		}
	}
	if sum == "" {
		return "", "", fmt.Errorf("%s: manca lo SHA-256 atteso (sha256 nella config, SHA256.txt o %s.sha256 accanto): non importato", src.File, base)
	}
	return sum, from, nil
}

// shaFromList legge un file nel formato di sha256sum («<hex>  <nome>»); una riga col solo hash vale
// per il file indicato quando il file delle somme è <file>.sha256.
func shaFromList(path, base string) (string, bool, error) {
	f, err := os.Open(path)
	if errors.Is(err, os.ErrNotExist) {
		return "", false, nil
	}
	if err != nil {
		return "", false, err
	}
	defer f.Close()
	sc := bufio.NewScanner(io.LimitReader(f, 1<<20))
	for sc.Scan() {
		fields := strings.Fields(sc.Text())
		if len(fields) == 0 {
			continue
		}
		sum := strings.ToLower(fields[0])
		if !hexRe.MatchString(sum) {
			continue
		}
		if len(fields) == 1 && strings.HasSuffix(path, ".sha256") {
			return sum, true, nil
		}
		if len(fields) >= 2 && strings.TrimPrefix(fields[len(fields)-1], "*") == base {
			return sum, true, nil
		}
	}
	return "", false, sc.Err()
}

// Originale di fabbrica della Heltec Wireless Paper: nome del file <scheda>_<mac senza due punti>_fabbrica.bin
// in backup-fabbrica/ (escluso da git). MAC e SHA-256 non stanno nel codice: il MAC viene dal nome del
// file, lo SHA-256 da SHA256.txt accanto (senza, ImportFactory non importa).
var devFactory = FactorySource{
	Chip:      "ESP32-S3",
	Name:      "Originale di fabbrica · Heltec Wireless Paper",
	FlashSize: 8 << 20,
}

var devFactoryRe = regexp.MustCompile(`^heltec-wireless-paper_([0-9a-f]{12})_fabbrica\.bin$`)

// devFactoryIn: il primo originale di fabbrica in dir (ordine alfabetico), se c'è.
func devFactoryIn(dir string) (FactorySource, bool) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return FactorySource{}, false
	}
	for _, e := range entries {
		m := devFactoryRe.FindStringSubmatch(e.Name())
		if m == nil || !e.Type().IsRegular() {
			continue
		}
		h := m[1]
		src := devFactory
		src.File = filepath.Join(dir, e.Name())
		src.MAC = strings.Join([]string{h[0:2], h[2:4], h[4:6], h[6:8], h[8:10], h[10:12]}, ":")
		return src, true
	}
	return FactorySource{}, false
}

// DevSources: il default di sviluppo. Cerca backup-fabbrica/ risalendo dalla cartella corrente e
// dalla cartella dei sorgenti con cui è stato compilato il binario (che non esiste sulle macchine
// degli altri, o con -trimpath: allora non trova niente).
func DevSources() []FactorySource {
	var roots []string
	if wd, err := os.Getwd(); err == nil {
		roots = append(roots, wd)
	}
	if _, file, _, ok := runtime.Caller(0); ok && filepath.IsAbs(file) {
		roots = append(roots, filepath.Dir(file))
	}
	seen := map[string]bool{}
	var out []FactorySource
	for _, r := range roots {
		for d := r; ; d = filepath.Dir(d) {
			if src, ok := devFactoryIn(filepath.Join(d, "backup-fabbrica")); ok && !seen[src.File] {
				seen[src.File] = true
				out = append(out, src)
				break
			}
			if filepath.Dir(d) == d {
				break
			}
		}
	}
	if len(out) > 1 {
		out = out[:1] // un MAC, un originale: basta la prima copia trovata
	}
	return out
}
