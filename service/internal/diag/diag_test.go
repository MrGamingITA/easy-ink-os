package diag

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestCrashValidate(t *testing.T) {
	ok := CrashMsg{FW: "0.7.0", Reason: "LoadProhibited", PC: "0x42012a3c", Backtrace: []string{"0x42012a39:0x3fcebf00"}, Elf: "bf9fc49a"}
	if err := ok.Validate(); err != nil {
		t.Fatal(err)
	}
	for _, bad := range []CrashMsg{
		{Reason: "x"}, {FW: "1"}, {FW: "1", Reason: "x", PC: "42"}, {FW: "1", Reason: "x", Backtrace: []string{"0xzz"}},
		{FW: "1", Reason: "x", Elf: "BF9F"}, {FW: "1", Reason: strings.Repeat("x", 256)},
	} {
		if bad.Validate() == nil {
			t.Errorf("accettato: %+v", bad)
		}
	}
}

func TestCrashLastStringOrArray(t *testing.T) {
	var a, b CrashMsg
	if json.Unmarshal([]byte(`{"fw":"1","reason":"x","last":"una"}`), &a) != nil || len(a.Last) != 1 || a.Last[0] != "una" {
		t.Fatalf("%+v", a)
	}
	if json.Unmarshal([]byte(`{"fw":"1","reason":"x","last":["a","b"]}`), &b) != nil || len(b.Last) != 2 {
		t.Fatalf("%+v", b)
	}
	if json.Unmarshal([]byte(`{"fw":"1","reason":"x","last":5}`), &b) == nil {
		t.Fatal("last numerico accettato")
	}
}

func TestCrashSaveAndLast(t *testing.T) {
	dir := t.TempDir()
	now := time.Date(2026, 10, 11, 21, 0, 0, 0, time.Local)
	s := &CrashStore{Dir: dir, Addr2line: "/nessuno", Now: func() time.Time { return now }}
	c1, err := s.Save("calamaio-3f2a", CrashMsg{FW: "0.7.0", Reason: "ESP_RST_PANIC"})
	if err != nil || !strings.Contains(c1.DecodeNote, "Nessun indirizzo") {
		t.Fatalf("%v %+v", err, c1)
	}
	c2, _ := s.Save("calamaio-3f2a", CrashMsg{FW: "0.7.0", Reason: "LoadProhibited", PC: "0x42000000"}) // stesso secondo
	if c2.ID == c1.ID {
		t.Fatal("sovrascritto")
	}
	if st, _ := os.Stat(filepath.Join(dir, c2.ID+".json")); st.Mode().Perm() != 0o600 {
		t.Fatal("permessi")
	}
	// rilettura dal disco (servizio riavviato)
	s2 := &CrashStore{Dir: dir}
	if l := s2.Last("calamaio-3f2a"); l == nil || l.Reason == "" {
		t.Fatal("ultimo crash non riletto")
	}
	if len(s2.List(0)) != 2 {
		t.Fatal("elenco")
	}
	if _, ok := s2.File("../x", ".txt"); ok {
		t.Fatal("percorso accettato")
	}
}

func TestPickELF(t *testing.T) {
	el := []ElfCandidate{{Path: "/a.elf", Version: "0.6.0", SHA256: "aa11"}, {Path: "/b.elf", Version: "0.7.0", SHA256: "bb22"}}
	if p, note := pickELF(el, "0.6.0", "bb22"); p != "/b.elf" || note != "" {
		t.Fatalf("per impronta: %s %s", p, note)
	}
	if p, note := pickELF(el, "0.6.0", ""); p != "/a.elf" || !strings.Contains(note, "stessa build") {
		t.Fatalf("per versione: %s %s", p, note)
	}
	if p, _ := pickELF(el, "0.6.0", "cc33"); p != "" {
		t.Fatal("impronta sconosciuta tradotta lo stesso")
	}
}

// Traduzione vera con l'addr2line di PlatformIO e l'ELF della build del repository, se ci sono:
// l'indirizzo di setup() (o app_main) torna quella funzione.
func TestAddr2lineReal(t *testing.T) {
	tool := FindAddr2line()
	elf := "../../../firmware/.pio/build/heltec213/firmware.elf"
	if tool == "" {
		t.Skip("addr2line di PlatformIO assente")
	}
	if _, err := os.Stat(elf); err != nil {
		t.Skip("firmware.elf assente")
	}
	nm := strings.Replace(tool, "addr2line", "nm", 1)
	out, err := exec.Command(nm, elf).Output()
	if err != nil {
		t.Skip("nm assente")
	}
	addr, fn := "", ""
	for _, l := range strings.Split(string(out), "\n") {
		if f := strings.Fields(l); len(f) == 3 && (f[2] == "_Z5setupv" || f[2] == "app_main") {
			addr, fn = "0x"+f[0], strings.TrimPrefix(strings.TrimSuffix(f[2], "v"), "_Z5")
		}
	}
	if addr == "" {
		t.Skip("setup e app_main non trovati")
	}
	b, _ := os.ReadFile(elf)
	sum := sha256.Sum256(b)
	s := &CrashStore{Dir: t.TempDir(), Elves: func() []ElfCandidate {
		return []ElfCandidate{{Path: elf, Version: "0.6.0", SHA256: hex.EncodeToString(sum[:])}}
	}}
	c, err := s.Save("calamaio-3f2a", CrashMsg{FW: "0.6.0", Reason: "test", PC: addr, Backtrace: []string{addr + ":0x3fcebf00"},
		Elf: hex.EncodeToString(sum[:8])})
	if err != nil {
		t.Fatal(err)
	}
	if len(c.Decoded) != 2 || !strings.Contains(c.Decoded[0], fn) || c.DecodeNote != "" {
		t.Fatalf("%+v", c)
	}
	t.Logf("%s → %s", addr, c.Decoded[0])
}

func TestLogFiles(t *testing.T) {
	l := &LogFiles{Dir: t.TempDir()}
	p, err := l.Append("calamaio-3f2a", time.Now(), []string{"uno", "due"})
	if err != nil || !strings.HasSuffix(p, ".log") {
		t.Fatal(err)
	}
	if _, err := l.Path("../etc", "2026-10-11"); err == nil {
		t.Fatal("scheda non valida accettata")
	}
	if CleanLine("a\x1b[31mb\tc\r\n") != "a[31mb\tc" {
		t.Fatalf("%q", CleanLine("a\x1b[31mb\tc\r\n"))
	}
}
