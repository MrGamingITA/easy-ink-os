// Package diag conserva sul PC quello che le schede mandano per la diagnosi (docs/OTA.md §5-§6): i
// rapporti di crash, con gli indirizzi tradotti da addr2line se c'è l'ELF della stessa build, e il
// registro da remoto.
package diag

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"
)

// CrashMsg è il messaggio `crash` del display (docs/PROTOCOLLO.md, «Crash»).
type CrashMsg struct {
	FW        string   `json:"fw"`
	Reason    string   `json:"reason"`
	PC        string   `json:"pc,omitempty"`
	Backtrace []string `json:"backtrace,omitempty"`
	Last      Lines    `json:"last,omitempty"` // ultime righe di registro salvate in RTC (stringa o array)
	Elf       string   `json:"elf,omitempty"`  // impronta dell'ELF (esp_ota_get_app_elf_sha256), anche solo l'inizio
	Task      string   `json:"task,omitempty"`
	Exc       string   `json:"exc,omitempty"` // causa dell'eccezione (EXCCAUSE), se c'è
}

var (
	addrRe = regexp.MustCompile(`^0x[0-9a-fA-F]{1,8}(:0x[0-9a-fA-F]{1,8})?$`)
	elfRe  = regexp.MustCompile(`^[0-9a-f]{8,64}$`)
)

// Limiti del messaggio: oltre si scarta (non si tronca: un rapporto a metà inganna).
const (
	MaxBacktrace = 64
	MaxText      = 255
	MaxLast      = 16 // righe di last
)

// Lines: una stringa o un array di stringhe in JSON (il firmware può mandare `last` in tutti e due i
// modi); si scrive sempre come array.
type Lines []string

func (l *Lines) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err == nil {
		if s == "" {
			*l = nil
		} else {
			*l = Lines{s}
		}
		return nil
	}
	var a []string
	if err := json.Unmarshal(b, &a); err != nil {
		return errors.New("last: serve una stringa o un array di stringhe")
	}
	*l = a
	return nil
}

// Validate controlla il messaggio.
func (m CrashMsg) Validate() error {
	switch {
	case m.FW == "" || len(m.FW) > 31:
		return errors.New("fw mancante o troppo lungo")
	case m.Reason == "" || len(m.Reason) > MaxText:
		return errors.New("reason mancante o troppo lungo")
	case m.PC != "" && !addrRe.MatchString(m.PC):
		return fmt.Errorf("pc %q non valido", m.PC)
	case len(m.Backtrace) > MaxBacktrace:
		return errors.New("backtrace troppo lungo")
	case len(m.Last) > MaxLast || len(m.Task) > 31 || len(m.Exc) > 63:
		return errors.New("testo troppo lungo")
	case m.Elf != "" && !elfRe.MatchString(m.Elf):
		return fmt.Errorf("elf %q non valido", m.Elf)
	}
	for _, l := range m.Last {
		if len(l) > MaxText {
			return errors.New("riga di last troppo lunga")
		}
	}
	for _, a := range m.Backtrace {
		if !addrRe.MatchString(a) {
			return fmt.Errorf("indirizzo %q non valido nel backtrace", a)
		}
	}
	return nil
}

// Crash è un rapporto salvato.
type Crash struct {
	ID       string `json:"id"` // nome del file senza estensione
	Board    string `json:"board"`
	Received int64  `json:"received"` // ms epoch
	CrashMsg
	// Traduzione degli indirizzi (addr2line): una riga per indirizzo, con le funzioni inline.
	Decoded     []string `json:"decoded,omitempty"`
	DecodedWith string   `json:"decodedWith,omitempty"` // ELF usato
	DecodeNote  string   `json:"decodeNote,omitempty"`  // perché non c'è la traduzione, o con che riserva
}

// ElfCandidate è un firmware.elf che può tradurre gli indirizzi.
type ElfCandidate struct {
	Path    string
	Version string // versione della build ("" se non si sa)
	SHA256  string // impronta del file ("" se non calcolata)
}

// CrashStore salva i rapporti in Dir (<dati>/calamaio/crash).
type CrashStore struct {
	Dir string
	// Elves: dove cercare l'ELF (pacchetto firmware del servizio, archivio degli ELF delle build mandate).
	Elves func() []ElfCandidate
	// Addr2line: percorso di xtensa-esp32s3-elf-addr2line ("" = cercato in PlatformIO e nel PATH).
	Addr2line string
	Now       func() time.Time

	mu   sync.Mutex
	last map[string]*Crash // board → ultimo (anche letto dal disco all'avvio)
	scan bool
}

func (s *CrashStore) now() time.Time {
	if s.Now != nil {
		return s.Now()
	}
	return time.Now()
}

// Save salva il rapporto, prova a tradurre gli indirizzi e restituisce il rapporto salvato.
func (s *CrashStore) Save(board string, m CrashMsg) (*Crash, error) {
	if err := m.Validate(); err != nil {
		return nil, err
	}
	t := s.now()
	c := &Crash{Board: board, Received: t.UnixMilli(), CrashMsg: m}
	c.ID = board + "-" + t.Format("20060102-150405")
	s.decode(c)
	if err := os.MkdirAll(s.Dir, 0o700); err != nil {
		return nil, err
	}
	base := filepath.Join(s.Dir, c.ID)
	for i := 2; ; i++ { // due crash nello stesso secondo: non si sovrascrive
		if _, err := os.Stat(base + ".json"); err != nil {
			break
		}
		c.ID = fmt.Sprintf("%s-%s-%d", board, t.Format("20060102-150405"), i)
		base = filepath.Join(s.Dir, c.ID)
	}
	b, _ := json.MarshalIndent(c, "", "  ")
	if err := os.WriteFile(base+".json", append(b, '\n'), 0o600); err != nil {
		return nil, err
	}
	_ = os.WriteFile(base+".txt", []byte(c.Text()), 0o600)
	s.mu.Lock()
	s.loadLocked()
	s.last[board] = c
	s.mu.Unlock()
	return c, nil
}

// Text è il rapporto leggibile (file .txt e `calamaio status`).
func (c *Crash) Text() string {
	var b strings.Builder
	fmt.Fprintf(&b, "Crash di %s, firmware %s, ricevuto %s\n", c.Board, c.FW, time.UnixMilli(c.Received).Local().Format("2006-01-02 15:04:05"))
	fmt.Fprintf(&b, "Causa: %s\n", c.Reason)
	if c.Exc != "" {
		fmt.Fprintf(&b, "Eccezione: %s\n", c.Exc)
	}
	if c.Task != "" {
		fmt.Fprintf(&b, "Task: %s\n", c.Task)
	}
	if c.PC != "" {
		fmt.Fprintf(&b, "PC: %s\n", c.PC)
	}
	if len(c.Backtrace) > 0 {
		fmt.Fprintf(&b, "Backtrace: %s\n", strings.Join(c.Backtrace, " "))
	}
	for _, l := range c.Last {
		fmt.Fprintf(&b, "Registro: %s\n", l)
	}
	if len(c.Decoded) > 0 {
		fmt.Fprintf(&b, "\nTraduzione (%s):\n", c.DecodedWith)
		for _, l := range c.Decoded {
			fmt.Fprintf(&b, "  %s\n", l)
		}
	}
	if c.DecodeNote != "" {
		fmt.Fprintf(&b, "\n%s\n", c.DecodeNote)
	}
	return b.String()
}

// Last restituisce l'ultimo crash di board ("" = di qualunque scheda), nil se non ce ne sono.
func (s *CrashStore) Last(board string) *Crash {
	if s == nil {
		return nil
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.loadLocked()
	if board != "" {
		return s.last[board]
	}
	var best *Crash
	for _, c := range s.last {
		if best == nil || c.Received > best.Received {
			best = c
		}
	}
	return best
}

// List restituisce i rapporti salvati, i più recenti prima (al massimo n).
func (s *CrashStore) List(n int) []*Crash {
	files, _ := filepath.Glob(filepath.Join(s.Dir, "*.json"))
	var out []*Crash
	for _, f := range files {
		if c := readCrash(f); c != nil {
			out = append(out, c)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Received > out[j].Received })
	if n > 0 && len(out) > n {
		out = out[:n]
	}
	return out
}

// File restituisce il percorso del rapporto id (solo nomi semplici).
func (s *CrashStore) File(id, ext string) (string, bool) {
	if !regexp.MustCompile(`^[a-z0-9-]{1,64}$`).MatchString(id) {
		return "", false
	}
	p := filepath.Join(s.Dir, id+ext)
	if _, err := os.Stat(p); err != nil {
		return "", false
	}
	return p, true
}

func readCrash(path string) *Crash {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	var c Crash
	if json.Unmarshal(b, &c) != nil || c.Board == "" {
		return nil
	}
	return &c
}

// loadLocked legge dal disco l'ultimo crash di ogni scheda (una volta, all'avvio). Con s.mu.
func (s *CrashStore) loadLocked() {
	if s.scan {
		return
	}
	s.scan = true
	if s.last == nil {
		s.last = map[string]*Crash{}
	}
	files, _ := filepath.Glob(filepath.Join(s.Dir, "*.json"))
	for _, f := range files {
		c := readCrash(f)
		if c == nil {
			continue
		}
		if o := s.last[c.Board]; o == nil || c.Received > o.Received {
			s.last[c.Board] = c
		}
	}
}

// ---------- traduzione degli indirizzi ----------

func (s *CrashStore) decode(c *Crash) {
	addrs := c.addresses()
	if len(addrs) == 0 {
		c.DecodeNote = "Nessun indirizzo da tradurre (senza coredump la scheda manda solo la causa del riavvio)."
		return
	}
	tool := s.Addr2line
	if tool == "" {
		tool = FindAddr2line()
	}
	if tool == "" {
		c.DecodeNote = "Indirizzi non tradotti: manca xtensa-esp32s3-elf-addr2line (PlatformIO, ~/.platformio/packages/toolchain-xtensa-esp32s3)."
		return
	}
	var elves []ElfCandidate
	if s.Elves != nil {
		elves = s.Elves()
	}
	elf, note := pickELF(elves, c.FW, c.Elf)
	if elf == "" {
		c.DecodeNote = note
		return
	}
	lines, err := Addr2line(tool, elf, addrs)
	if err != nil {
		c.DecodeNote = "Indirizzi non tradotti: " + err.Error()
		return
	}
	c.Decoded, c.DecodedWith, c.DecodeNote = lines, elf, note
}

// addresses: PC e indirizzi del backtrace (la parte prima di «:», lo stack pointer non serve).
func (c *Crash) addresses() []string {
	var out []string
	add := func(a string) {
		if i := strings.IndexByte(a, ':'); i >= 0 {
			a = a[:i]
		}
		a = strings.ToLower(a)
		if a == "" || a == "0x0" || a == "0x00000000" {
			return
		}
		out = append(out, a)
	}
	if c.PC != "" {
		add(c.PC)
	}
	for _, a := range c.Backtrace {
		add(a)
	}
	return out
}

// pickELF sceglie l'ELF: con l'impronta della scheda quello con la stessa impronta (esatto); senza,
// quello della stessa versione (con riserva: due build della stessa versione hanno indirizzi diversi).
func pickELF(elves []ElfCandidate, fw, elfPrefix string) (string, string) {
	if elfPrefix != "" {
		for _, e := range elves {
			if e.SHA256 != "" && strings.HasPrefix(e.SHA256, elfPrefix) {
				return e.Path, ""
			}
		}
		return "", "Indirizzi non tradotti: nessun firmware.elf con l'impronta " + elfPrefix + " (pacchetto firmware del servizio o build mandate con `calamaio ota push`)."
	}
	for _, e := range elves {
		if e.Version != "" && e.Version == fw {
			return e.Path, "Tradotti con l'ELF della versione " + fw + ": attendibili solo se è la stessa build."
		}
	}
	return "", "Indirizzi non tradotti: nessun firmware.elf della versione " + fw + "."
}

// FindAddr2line cerca xtensa-esp32s3-elf-addr2line nel toolchain di PlatformIO, poi nel PATH.
func FindAddr2line() string {
	name := "xtensa-esp32s3-elf-addr2line"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	core := os.Getenv("PLATFORMIO_CORE_DIR")
	if core == "" {
		if home, err := os.UserHomeDir(); err == nil {
			core = filepath.Join(home, ".platformio")
		}
	}
	if core != "" {
		p := filepath.Join(core, "packages", "toolchain-xtensa-esp32s3", "bin", name)
		if st, err := os.Stat(p); err == nil && st.Mode().IsRegular() {
			return p
		}
	}
	if p, err := exec.LookPath(name); err == nil {
		return p
	}
	return ""
}

// Addr2line traduce gli indirizzi con `addr2line -pfiaC -e elf`: una riga per indirizzo, con le
// funzioni inline sulla stessa riga dopo « ← ».
func Addr2line(tool, elf string, addrs []string) ([]string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	args := append([]string{"-pfiaC", "-e", elf}, addrs...)
	cmd := exec.CommandContext(ctx, tool, args...)
	var out, errb bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &errb
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(errb.String())
		if msg == "" {
			msg = err.Error()
		}
		return nil, fmt.Errorf("addr2line: %s", msg)
	}
	var lines []string
	sc := bufio.NewScanner(&out)
	for sc.Scan() {
		l := strings.TrimRight(sc.Text(), " ")
		if strings.HasPrefix(strings.TrimSpace(l), "(inlined by)") && len(lines) > 0 {
			lines[len(lines)-1] += " ← " + strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(l), "(inlined by)"))
			continue
		}
		if l != "" {
			lines = append(lines, l)
		}
	}
	return lines, nil
}
