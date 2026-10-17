// Package firmware legge il pacchetto firmware da scrivere sulla scheda dalla webapp (Web Serial).
//
// Un pacchetto è una cartella con i quattro file di una build PlatformIO per ESP32-S3:
//
//	bootloader.bin  0x0
//	partitions.bin  0x8000
//	boot_app0.bin   0xe000
//	firmware.bin    0x10000
//
// È la stessa struttura di firmware/.pio/build/<env>/, dove però boot_app0.bin manca: in quel
// caso si prende dal pacchetto Arduino di PlatformIO,
// <PLATFORMIO_CORE_DIR o ~/.platformio>/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin.
//
// manifest.json (facoltativo) sostituisce versione, chip e offset:
//
//	{"version":"0.3.0","chip":"ESP32-S3","parts":[{"name":"firmware.bin","offset":"0x10000"}, …]}
//
// Senza manifest il chip si legge dall'intestazione di firmware.bin (esp_image_header_t.chip_id) e la
// versione da un file «version» accanto o, se la cartella è una build del repository, da
// firmware/src/core/model.h (CALAMAIO_FW_VERSION).
package firmware

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Part è un file da scrivere in flash.
type Part struct {
	Name   string `json:"name"`
	Offset int64  `json:"offset"`
	URL    string `json:"url"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
	path   string
}

// Info è la risposta di GET /api/firmware.
type Info struct {
	Available bool   `json:"available"`
	Version   string `json:"version"`
	Chip      string `json:"chip"`
	Parts     []Part `json:"parts"`
	Dir       string `json:"dir"`
	Built     int64  `json:"built,omitempty"` // ms epoch di firmware.bin
	Problem   string `json:"problem,omitempty"`
}

// Layout predefinito (ESP32-S3, Arduino, tabella partizioni di default).
var defaultParts = []struct {
	name   string
	offset int64
}{
	{"bootloader.bin", 0x0},
	{"partitions.bin", 0x8000},
	{"boot_app0.bin", 0xe000},
	{"firmware.bin", 0x10000},
}

// URLPrefix è il percorso da cui la API serve le parti.
const URLPrefix = "/api/firmware/parts/"

// Package legge una cartella e tiene in cache gli hash (per file, dimensione e data).
type Package struct {
	Dir string

	mu    sync.Mutex
	cache map[string]hashed
}

type hashed struct {
	size  int64
	mtime time.Time
	sum   string
}

func New(dir string) *Package { return &Package{Dir: dir, cache: map[string]hashed{}} }

type manifest struct {
	Version string `json:"version"`
	Chip    string `json:"chip"`
	Parts   []struct {
		Name   string          `json:"name"`
		Offset json.RawMessage `json:"offset"`
	} `json:"parts"`
}

var safeName = regexp.MustCompile(`^[A-Za-z0-9_.-]+\.bin$`)

// Read legge il pacchetto. Non restituisce mai errore: i problemi finiscono in Info.Problem.
func (p *Package) Read() Info {
	info := Info{Dir: p.Dir, Parts: []Part{}}
	if p.Dir == "" {
		info.Problem = "nessuna cartella del firmware configurata"
		return info
	}
	type want struct {
		name   string
		offset int64
	}
	var wants []want
	var man manifest
	if b, err := os.ReadFile(filepath.Join(p.Dir, "manifest.json")); err == nil {
		if err := json.Unmarshal(b, &man); err != nil {
			info.Problem = "manifest.json non valido: " + err.Error()
			return info
		}
		for _, mp := range man.Parts {
			off, err := parseOffset(mp.Offset)
			if err != nil || !safeName.MatchString(mp.Name) {
				info.Problem = fmt.Sprintf("manifest.json: parte %q non valida", mp.Name)
				return info
			}
			wants = append(wants, want{mp.Name, off})
		}
	}
	if len(wants) == 0 {
		for _, d := range defaultParts {
			wants = append(wants, want{d.name, d.offset})
		}
	}

	var missing []string
	for _, w := range wants {
		path := p.locate(w.name)
		st, err := os.Stat(path)
		if err != nil || !st.Mode().IsRegular() || st.Size() == 0 {
			missing = append(missing, w.name)
			continue
		}
		sum, err := p.hash(path, st)
		if err != nil {
			missing = append(missing, w.name)
			continue
		}
		info.Parts = append(info.Parts, Part{Name: w.name, Offset: w.offset, URL: URLPrefix + w.name,
			Size: st.Size(), SHA256: sum, path: path})
		if w.name == "firmware.bin" {
			info.Built = st.ModTime().UnixMilli()
		}
	}
	if len(missing) > 0 {
		info.Problem = "mancano " + strings.Join(missing, ", ")
		info.Parts = []Part{}
		return info
	}

	info.Chip = man.Chip
	app := p.find(info.Parts, "firmware.bin")
	if app != nil {
		chip, err := chipOf(app.path)
		if err != nil {
			info.Problem = "firmware.bin: " + err.Error()
			info.Parts = []Part{}
			return info
		}
		if info.Chip == "" {
			info.Chip = chip
		}
	}
	info.Version = man.Version
	if info.Version == "" {
		if vb, err := os.ReadFile(filepath.Join(p.Dir, "version")); err == nil {
			info.Version = strings.TrimSpace(string(vb))
		}
	}
	if info.Version == "" {
		info.Version = DevVersion(p.Dir)
	}
	info.Available = true
	return info
}

// Open apre una parte per nome (solo i nomi del pacchetto: niente percorsi).
func (p *Package) Open(name string) (*os.File, Part, error) {
	if !safeName.MatchString(name) {
		return nil, Part{}, os.ErrNotExist
	}
	info := p.Read()
	if !info.Available {
		return nil, Part{}, os.ErrNotExist
	}
	pt := p.find(info.Parts, name)
	if pt == nil {
		return nil, Part{}, os.ErrNotExist
	}
	f, err := os.Open(pt.path)
	return f, *pt, err
}

func (p *Package) find(parts []Part, name string) *Part {
	for i := range parts {
		if parts[i].Name == name {
			return &parts[i]
		}
	}
	return nil
}

// locate: la cartella del pacchetto, e per boot_app0.bin anche il pacchetto Arduino di PlatformIO.
func (p *Package) locate(name string) string {
	path := filepath.Join(p.Dir, name)
	if name != "boot_app0.bin" {
		return path
	}
	if _, err := os.Stat(path); err == nil {
		return path
	}
	if alt := PlatformIOBootApp0(); alt != "" {
		return alt
	}
	return path
}

// PlatformIOBootApp0 restituisce boot_app0.bin del framework Arduino di PlatformIO, se c'è.
func PlatformIOBootApp0() string {
	core := os.Getenv("PLATFORMIO_CORE_DIR")
	if core == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		core = filepath.Join(home, ".platformio")
	}
	path := filepath.Join(core, "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
	if st, err := os.Stat(path); err == nil && st.Mode().IsRegular() {
		return path
	}
	return ""
}

func (p *Package) hash(path string, st os.FileInfo) (string, error) {
	p.mu.Lock()
	h, ok := p.cache[path]
	p.mu.Unlock()
	if ok && h.size == st.Size() && h.mtime.Equal(st.ModTime()) {
		return h.sum, nil
	}
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	sh := sha256.New()
	if _, err := io.Copy(sh, f); err != nil {
		return "", err
	}
	sum := hex.EncodeToString(sh.Sum(nil))
	p.mu.Lock()
	p.cache[path] = hashed{size: st.Size(), mtime: st.ModTime(), sum: sum}
	p.mu.Unlock()
	return sum, nil
}

func parseOffset(raw json.RawMessage) (int64, error) {
	var n int64
	if err := json.Unmarshal(raw, &n); err == nil {
		if n < 0 {
			return 0, errors.New("offset negativo")
		}
		return n, nil
	}
	var s string
	if err := json.Unmarshal(raw, &s); err != nil {
		return 0, err
	}
	return strconv.ParseInt(s, 0, 64) // "0x10000" o "65536"
}

// Chip dall'intestazione dell'immagine ESP-IDF: magic 0xE9 al byte 0, chip_id (uint16 LE) al byte 12.
var chipNames = map[uint16]string{
	0x0000: "ESP32", 0x0002: "ESP32-S2", 0x0005: "ESP32-C3", 0x0009: "ESP32-S3",
	0x000C: "ESP32-C2", 0x000D: "ESP32-C6", 0x0010: "ESP32-H2", 0x0012: "ESP32-P4",
}

func chipOf(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	var hdr [24]byte
	if _, err := io.ReadFull(f, hdr[:]); err != nil {
		return "", errors.New("intestazione troncata")
	}
	return ChipOfImage(hdr[:])
}

// ChipOfImage legge il chip dai primi 24 byte di un'immagine ESP-IDF (anche l'inizio di una flash
// completa, dove a 0x0 c'è il bootloader dell'ESP32-S3).
func ChipOfImage(hdr []byte) (string, error) {
	if len(hdr) < 14 {
		return "", errors.New("intestazione troncata")
	}
	if hdr[0] != 0xE9 {
		return "", fmt.Errorf("non è un'immagine ESP (primo byte 0x%02x, atteso 0xe9)", hdr[0])
	}
	id := binary.LittleEndian.Uint16(hdr[12:14])
	if name, ok := chipNames[id]; ok {
		return name, nil
	}
	return fmt.Sprintf("chip 0x%04x", id), nil
}

var fwVersionRe = regexp.MustCompile(`#define\s+CALAMAIO_FW_VERSION\s+"([^"]+)"`)

// DevVersion: per una build del repository (firmware/.pio/build/<env>) legge la versione dai sorgenti.
func DevVersion(dir string) string {
	abs, err := filepath.Abs(dir)
	if err != nil {
		return ""
	}
	if filepath.Base(filepath.Dir(abs)) != "build" || filepath.Base(filepath.Dir(filepath.Dir(abs))) != ".pio" {
		return ""
	}
	root := filepath.Dir(filepath.Dir(filepath.Dir(abs)))
	b, err := os.ReadFile(filepath.Join(root, "src", "core", "model.h"))
	if err != nil {
		return ""
	}
	if m := fwVersionRe.FindSubmatch(b); m != nil {
		return string(m[1])
	}
	return ""
}
