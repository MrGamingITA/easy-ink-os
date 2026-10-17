package backup

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

func newStore(t *testing.T) *Store {
	t.Helper()
	s, err := Open(filepath.Join(t.TempDir(), "backups"))
	if err != nil {
		t.Fatal(err)
	}
	return s
}

func shaHex(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) }

func perm(t *testing.T, p string) os.FileMode {
	t.Helper()
	st, err := os.Stat(p)
	if err != nil {
		t.Fatal(err)
	}
	return st.Mode().Perm()
}

// fakeFlash: inizia con l'intestazione di un bootloader ESP32-S3 (magic 0xE9, chip_id 9 al byte 12).
func fakeFlash(n int, seed byte) []byte {
	b := bytes.Repeat([]byte{0xff}, n)
	for i := 0; i < n; i += 4096 {
		b[i+1] = seed
	}
	b[0], b[12], b[13] = 0xE9, 9, 0
	return b
}

// leftovers: nessun file temporaneo rimasto nella cartella.
func leftovers(t *testing.T, s *Store) {
	t.Helper()
	ents, _ := os.ReadDir(s.Dir)
	for _, e := range ents {
		if strings.HasPrefix(e.Name(), ".tmp-") {
			t.Errorf("file temporaneo rimasto: %s", e.Name())
		}
	}
}

func TestCreateListOpenDelete(t *testing.T) {
	s := newStore(t)
	data := fakeFlash(64<<10, 1)
	b, err := s.Create(Meta{Name: " Solo programma ", Chip: "ESP32-S3", MAC: "AA-BB-CC-DD-48-E8", Board: "calamaio-48e8",
		FlashSize: 8 << 20, Offset: 0x10000, Note: "prova", SHA256: strings.ToUpper(shaHex(data))}, bytes.NewReader(data))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(b.ID, "u-") || b.Kind != KindUser || b.Protected || b.Name != "Solo programma" ||
		b.MAC != "aa:bb:cc:dd:48:e8" || b.Size != int64(len(data)) || b.Length != b.Size || b.SHA256 != shaHex(data) || b.Offset != 0x10000 {
		t.Fatalf("backup = %+v", b)
	}
	list, err := s.List()
	if err != nil || len(list) != 1 || list[0] != b {
		t.Fatalf("List = %+v, %v", list, err)
	}
	f, got, err := s.OpenData(b.ID)
	if err != nil {
		t.Fatal(err)
	}
	read, _ := io.ReadAll(f)
	f.Close()
	if !bytes.Equal(read, data) || got != b {
		t.Fatal("dati o metadati diversi")
	}
	if runtime.GOOS != "windows" {
		if p := perm(t, s.Dir); p != DirPerm {
			t.Errorf("cartella %o", p)
		}
		for _, p := range []string{s.binPath(b.ID), s.metaPath(b.ID)} {
			if m := perm(t, p); m != FilePerm {
				t.Errorf("%s: %o, atteso 600", p, m)
			}
		}
	}
	leftovers(t, s)
	if err := s.Delete(b.ID); err != nil {
		t.Fatal(err)
	}
	if err := s.Delete(b.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("seconda Delete: %v", err)
	}
	if ents, _ := os.ReadDir(s.Dir); len(ents) != 0 {
		t.Fatalf("restano %d file", len(ents))
	}
}

func TestOpenTightensPermissions(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("permessi POSIX")
	}
	dir := filepath.Join(t.TempDir(), "backups")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	if _, err := Open(dir); err != nil {
		t.Fatal(err)
	}
	if p := perm(t, dir); p != DirPerm {
		t.Fatalf("cartella %o", p)
	}
}

func TestCreateRejects(t *testing.T) {
	s := newStore(t)
	data := fakeFlash(4096, 2)
	ok := Meta{Name: "x", Chip: "ESP32-S3"}
	cases := []struct {
		name string
		m    Meta
		r    io.Reader
		want func(error) bool
	}{
		{"senza nome", Meta{Chip: "ESP32-S3"}, bytes.NewReader(data), isInvalid},
		{"senza chip", Meta{Name: "x"}, bytes.NewReader(data), isInvalid},
		{"chip strano", Meta{Name: "x", Chip: "../etc"}, bytes.NewReader(data), isInvalid},
		{"mac corto", Meta{Name: "x", Chip: "ESP32", MAC: "aa:bb"}, bytes.NewReader(data), isInvalid},
		{"board con barra", Meta{Name: "x", Chip: "ESP32", Board: "a/b"}, bytes.NewReader(data), isInvalid},
		{"nome con a capo", Meta{Name: "a\nb", Chip: "ESP32"}, bytes.NewReader(data), isInvalid},
		{"sha non esadecimale", Meta{Name: "x", Chip: "ESP32", SHA256: "zz"}, bytes.NewReader(data), isInvalid},
		{"fuori dalla flash", Meta{Name: "x", Chip: "ESP32", FlashSize: 4096, Offset: 1}, bytes.NewReader(data), isInvalid},
		{"vuoto", ok, bytes.NewReader(nil), func(e error) bool { return errors.Is(e, ErrEmpty) }},
		{"sha diverso", Meta{Name: "x", Chip: "ESP32", SHA256: shaHex([]byte("altro"))}, bytes.NewReader(data),
			func(e error) bool { var c *ChecksumError; return errors.As(e, &c) }},
		{"troppo grande", ok, io.LimitReader(zeros{}, MaxSize+1), func(e error) bool { return errors.Is(e, ErrTooLarge) }},
	}
	for _, c := range cases {
		if _, err := s.Create(c.m, c.r); !c.want(err) {
			t.Errorf("%s: errore %v", c.name, err)
		}
	}
	if ents, _ := os.ReadDir(s.Dir); len(ents) != 0 {
		t.Fatalf("dopo i rifiuti restano %d file", len(ents))
	}
	// Al limite esatto passa.
	b, err := s.Create(ok, io.LimitReader(zeros{}, MaxSize))
	if err != nil || b.Size != MaxSize {
		t.Fatalf("16 MiB esatti: %+v %v", b, err)
	}
}

type zeros struct{}

func (zeros) Read(p []byte) (int, error) { clear(p); return len(p), nil }

func isInvalid(err error) bool { var inv *InvalidError; return errors.As(err, &inv) }

func TestBadIDsNeverTouchFiles(t *testing.T) {
	s := newStore(t)
	// Un file fuori dall'archivio che un id con percorso proverebbe a raggiungere.
	outside := filepath.Join(filepath.Dir(s.Dir), "segreto")
	if err := os.WriteFile(outside+".json", []byte(`{"id":"x"}`), 0o600); err != nil {
		t.Fatal(err)
	}
	for _, id := range []string{"", ".", "..", "../segreto", "..%2Fsegreto", "a/b", `a\b`, "Factory-x", "x.json", strings.Repeat("a", 65)} {
		if _, err := s.Get(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("Get(%q): %v", id, err)
		}
		if err := s.Delete(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("Delete(%q): %v", id, err)
		}
	}
	if _, err := os.Stat(outside + ".json"); err != nil {
		t.Fatal("file esterno toccato")
	}
}

// factoryFixture: una flash finta con SHA256.txt accanto, come in backup-fabbrica/.
func factoryFixture(t *testing.T, sum string) (FactorySource, []byte) {
	t.Helper()
	dir := t.TempDir()
	data := fakeFlash(256<<10, 7)
	name := "heltec-wireless-paper_aabbccdd48e8_fabbrica.bin"
	file := filepath.Join(dir, name)
	if err := os.WriteFile(file, data, 0o644); err != nil {
		t.Fatal(err)
	}
	if sum == "" {
		sum = shaHex(data)
	}
	if err := os.WriteFile(filepath.Join(dir, "SHA256.txt"), []byte(sum+"  "+name+"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	mt := time.Date(2026, 10, 11, 13, 2, 0, 0, time.UTC)
	_ = os.Chtimes(file, mt, mt)
	return FactorySource{File: file, MAC: "aa:bb:cc:dd:48:e8", Name: "Originale di fabbrica · Heltec Wireless Paper"}, data
}

func TestFactoryImport(t *testing.T) {
	s := newStore(t)
	src, data := factoryFixture(t, "")
	res, err := s.ImportFactory(src)
	if err != nil {
		t.Fatal(err)
	}
	b := res.Backup
	if !res.Imported || b.ID != "factory-aabbccdd48e8" || b.Kind != KindFactory || !b.Protected || b.Chip != "ESP32-S3" ||
		b.Board != "calamaio-48e8" || b.FlashSize != int64(len(data)) || b.Offset != 0 || b.Length != int64(len(data)) ||
		b.SHA256 != shaHex(data) || b.Created != time.Date(2026, 10, 11, 13, 2, 0, 0, time.UTC).UnixMilli() {
		t.Fatalf("importato %+v", b)
	}
	// Copia, non spostamento: la sorgente resta intatta.
	if got, err := os.ReadFile(src.File); err != nil || !bytes.Equal(got, data) {
		t.Fatal("sorgente sparita o cambiata")
	}
	if runtime.GOOS != "windows" {
		for _, p := range []string{s.binPath(b.ID), s.metaPath(b.ID)} {
			if m := perm(t, p); m != ProtectedPerm {
				t.Errorf("%s: %o, atteso 400", p, m)
			}
		}
	}
	// Idempotente: la seconda volta non importa niente.
	res2, err := s.ImportFactory(src)
	if err != nil || res2.Imported || res2.Backup.ID != b.ID {
		t.Fatalf("seconda importazione: %+v %v", res2, err)
	}
	if list, _ := s.List(); len(list) != 1 {
		t.Fatalf("%d backup dopo due importazioni", len(list))
	}
	// Non si cancella, neanche con i metadati alterati (protected false, kind user).
	if err := s.Delete(b.ID); !errors.Is(err, ErrProtected) {
		t.Fatalf("Delete: %v", err)
	}
	if runtime.GOOS != "windows" {
		var m map[string]any
		raw, _ := os.ReadFile(s.metaPath(b.ID))
		_ = json.Unmarshal(raw, &m)
		m["protected"], m["kind"] = false, "user"
		raw, _ = json.Marshal(m)
		_ = os.Chmod(s.metaPath(b.ID), 0o600)
		if err := os.WriteFile(s.metaPath(b.ID), raw, 0o600); err != nil {
			t.Fatal(err)
		}
		if err := s.Delete(b.ID); !errors.Is(err, ErrProtected) {
			t.Fatalf("Delete con metadati alterati: %v", err)
		}
	}
	// Non si sovrascrive: create con lo stesso id fallisce.
	if _, err := s.create(b.ID, KindUser, false, 0, Meta{Name: "x", Chip: "ESP32"}, bytes.NewReader([]byte{1})); !errors.Is(err, ErrExists) {
		t.Fatalf("sovrascrittura: %v", err)
	}
	if got, _ := os.ReadFile(s.binPath(b.ID)); !bytes.Equal(got, data) {
		t.Fatal("dati del protetto cambiati")
	}
	leftovers(t, s)
}

func TestFactoryWrongSHARefused(t *testing.T) {
	s := newStore(t)
	src, _ := factoryFixture(t, strings.Repeat("0", 64))
	if _, err := s.ImportFactory(src); err == nil || !strings.Contains(err.Error(), "SHA-256 diverso") {
		t.Fatalf("errore = %v", err)
	}
	if ents, _ := os.ReadDir(s.Dir); len(ents) != 0 {
		t.Fatalf("restano %d file", len(ents))
	}
	// SHA nella config diverso da SHA256.txt: rifiutato prima di leggere.
	src2, data := factoryFixture(t, "")
	src2.SHA256 = shaHex(append(data, 0))
	if _, err := s.ImportFactory(src2); err == nil {
		t.Fatal("config e SHA256.txt in disaccordo: importato lo stesso")
	}
}

func TestFactoryNeedsSHAAndSize(t *testing.T) {
	s := newStore(t)
	src, _ := factoryFixture(t, "")
	_ = os.Remove(filepath.Join(filepath.Dir(src.File), "SHA256.txt"))
	if _, err := s.ImportFactory(src); err == nil || !strings.Contains(err.Error(), "manca lo SHA-256") {
		t.Fatalf("senza SHA: %v", err)
	}
	src2, data := factoryFixture(t, "")
	src2.FlashSize = int64(len(data)) * 2
	if _, err := s.ImportFactory(src2); err == nil {
		t.Fatal("dimensione diversa dalla flash: importato lo stesso")
	}
	src3, _ := factoryFixture(t, "")
	src3.MAC = "nonunmac"
	if _, err := s.ImportFactory(src3); err == nil {
		t.Fatal("MAC non valido: importato lo stesso")
	}
}

// Il default di sviluppo trova backup-fabbrica/ del repository (se c'è) e lo importa intatto.
func TestDevSourcesRealFile(t *testing.T) {
	srcs := DevSources()
	if len(srcs) == 0 {
		t.Skip("backup-fabbrica/ non presente (repository senza il dump)")
	}
	src := srcs[0]
	before, err := os.Stat(src.File)
	if err != nil {
		t.Fatal(err)
	}
	s := newStore(t)
	res, err := s.ImportFactory(src)
	if err != nil {
		t.Fatal(err)
	}
	b := res.Backup
	if b.Size != 8<<20 || b.MAC != src.MAC || b.Chip != "ESP32-S3" ||
		b.Name != "Originale di fabbrica · Heltec Wireless Paper" || !b.Protected {
		t.Fatalf("importato %+v", b)
	}
	after, err := os.Stat(src.File)
	if err != nil || after.Size() != before.Size() || after.Mode() != before.Mode() || !after.ModTime().Equal(before.ModTime()) {
		t.Fatal("il file del repository è cambiato")
	}
}

func TestFileName(t *testing.T) {
	cases := map[string]string{
		"Originale di fabbrica · Heltec Wireless Paper": "Originale-di-fabbrica-Heltec-Wireless-Paper.bin",
		"Solo programma (ota_0) · 11 ott 2026, 15:04":   "Solo-programma-ota_0-11-ott-2026-15-04.bin",
		"···":         "u-1.bin",
		"backup.bin":  "backup.bin",
		"../../etc/x": "etc-x.bin",
	}
	for in, want := range cases {
		if got := FileName(Backup{ID: "u-1", Name: in}); got != want {
			t.Errorf("FileName(%q) = %q, atteso %q", in, got, want)
		}
	}
}

// Il MAC del default di sviluppo viene dal nome del file, non dal codice.
func TestDevFactoryIn(t *testing.T) {
	dir := t.TempDir()
	if _, ok := devFactoryIn(dir); ok {
		t.Fatal("cartella vuota: trovato qualcosa")
	}
	for _, n := range []string{"altro.bin", "heltec-wireless-paper_AABBCCDD48E8_fabbrica.bin", "heltec-wireless-paper_0011223344556_fabbrica.bin"} {
		if err := os.WriteFile(filepath.Join(dir, n), []byte("x"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	if _, ok := devFactoryIn(dir); ok {
		t.Fatal("nomi non validi accettati")
	}
	name := "heltec-wireless-paper_001122334455_fabbrica.bin"
	if err := os.WriteFile(filepath.Join(dir, name), []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	src, ok := devFactoryIn(dir)
	if !ok || src.MAC != "00:11:22:33:44:55" || src.File != filepath.Join(dir, name) || src.SHA256 != "" || src.FlashSize != 8<<20 {
		t.Fatalf("trovato %+v %v", src, ok)
	}
}
