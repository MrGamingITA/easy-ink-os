package firmware

import (
	"crypto/sha256"
	"encoding/hex"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// image crea un finto binario ESP con intestazione valida per il chip indicato.
func image(chip uint16, n int) []byte {
	b := make([]byte, n)
	b[0] = 0xE9
	b[12], b[13] = byte(chip), byte(chip>>8)
	for i := 24; i < n; i++ {
		b[i] = byte(i)
	}
	return b
}

func write(t *testing.T, dir, name string, b []byte) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, name), b, 0o644); err != nil {
		t.Fatal(err)
	}
}

func sum(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) }

func TestPackageDefaultLayout(t *testing.T) {
	t.Setenv("PLATFORMIO_CORE_DIR", t.TempDir()) // niente boot_app0 di riserva
	dir := t.TempDir()
	app := image(9, 4096)
	write(t, dir, "bootloader.bin", image(9, 512))
	write(t, dir, "partitions.bin", []byte{0xAA, 0x50, 1, 2})
	write(t, dir, "firmware.bin", app)

	p := New(dir)
	info := p.Read()
	if info.Available || !strings.Contains(info.Problem, "boot_app0.bin") || len(info.Parts) != 0 {
		t.Fatalf("senza boot_app0 = %+v", info)
	}
	write(t, dir, "boot_app0.bin", []byte{1, 2, 3})
	info = p.Read()
	if !info.Available || info.Chip != "ESP32-S3" || len(info.Parts) != 4 {
		t.Fatalf("info = %+v", info)
	}
	want := map[string]int64{"bootloader.bin": 0, "partitions.bin": 0x8000, "boot_app0.bin": 0xe000, "firmware.bin": 0x10000}
	for _, pt := range info.Parts {
		if want[pt.Name] != pt.Offset || pt.URL != URLPrefix+pt.Name || pt.Size == 0 || len(pt.SHA256) != 64 {
			t.Errorf("parte %+v", pt)
		}
	}
	if fw := p.find(info.Parts, "firmware.bin"); fw.SHA256 != sum(app) || fw.Size != 4096 {
		t.Fatalf("firmware.bin %+v", fw)
	}

	// Il file cambia: l'hash in cache non deve restare vecchio.
	app2 := image(9, 5000)
	write(t, dir, "firmware.bin", app2)
	if fw := p.find(p.Read().Parts, "firmware.bin"); fw.SHA256 != sum(app2) {
		t.Fatal("hash vecchio dopo la modifica")
	}

	f, pt, err := p.Open("firmware.bin")
	if err != nil {
		t.Fatal(err)
	}
	got, _ := io.ReadAll(f)
	f.Close()
	if sum(got) != pt.SHA256 {
		t.Fatal("contenuto servito diverso dall'hash dichiarato")
	}
	for _, bad := range []string{"../firmware.bin", "manifest.json", "firmware.elf", "/etc/passwd", "altro.bin"} {
		if _, _, err := p.Open(bad); err == nil {
			t.Errorf("Open(%q) doveva fallire", bad)
		}
	}
}

func TestPackageManifestAndBadImage(t *testing.T) {
	t.Setenv("PLATFORMIO_CORE_DIR", t.TempDir())
	dir := t.TempDir()
	write(t, dir, "app.bin", image(5, 100))
	write(t, dir, "manifest.json", []byte(`{"version":"0.4.0","parts":[{"name":"app.bin","offset":"0x10000"}]}`))
	info := New(dir).Read()
	if !info.Available || info.Version != "0.4.0" || len(info.Parts) != 1 || info.Parts[0].Offset != 0x10000 {
		t.Fatalf("manifest = %+v", info)
	}

	dir2 := t.TempDir()
	for _, n := range []string{"bootloader.bin", "partitions.bin", "boot_app0.bin"} {
		write(t, dir2, n, []byte{1})
	}
	write(t, dir2, "firmware.bin", []byte("non è un firmware, ma un testo qualsiasi"))
	if info := New(dir2).Read(); info.Available || !strings.Contains(info.Problem, "0xe9") {
		t.Fatalf("immagine non ESP accettata: %+v", info)
	}
	if info := New(filepath.Join(dir2, "manca")).Read(); info.Available || info.Problem == "" {
		t.Fatalf("cartella assente: %+v", info)
	}
}

// boot_app0.bin dal pacchetto Arduino di PlatformIO e versione dai sorgenti, come in
// firmware/.pio/build/heltec213 del repository.
func TestPackageDevBuild(t *testing.T) {
	core := t.TempDir()
	pio := filepath.Join(core, "packages", "framework-arduinoespressif32", "tools", "partitions")
	if err := os.MkdirAll(pio, 0o755); err != nil {
		t.Fatal(err)
	}
	write(t, pio, "boot_app0.bin", []byte{7, 7})
	t.Setenv("PLATFORMIO_CORE_DIR", core)

	root := t.TempDir()
	build := filepath.Join(root, ".pio", "build", "heltec213")
	_ = os.MkdirAll(build, 0o755)
	_ = os.MkdirAll(filepath.Join(root, "src", "core"), 0o755)
	write(t, filepath.Join(root, "src", "core"), "model.h", []byte("#define CALAMAIO_FW_VERSION \"9.8.7\"\n"))
	write(t, build, "bootloader.bin", image(9, 64))
	write(t, build, "partitions.bin", []byte{1})
	write(t, build, "firmware.bin", image(9, 64))
	info := New(build).Read()
	if !info.Available || info.Version != "9.8.7" || info.Chip != "ESP32-S3" {
		t.Fatalf("build di sviluppo = %+v", info)
	}
	if bp := New(build).find(info.Parts, "boot_app0.bin"); bp == nil || bp.path != filepath.Join(pio, "boot_app0.bin") {
		t.Fatalf("boot_app0 = %+v", bp)
	}
}

// La build vera del repository, se c'è (non fallisce se manca).
func TestRealRepositoryBuild(t *testing.T) {
	dir := "../../../firmware/.pio/build/heltec213"
	if _, err := os.Stat(filepath.Join(dir, "firmware.bin")); err != nil {
		t.Skip("nessuna build del firmware nel repository")
	}
	if PlatformIOBootApp0() == "" {
		t.Skip("boot_app0.bin di PlatformIO assente")
	}
	info := New(dir).Read()
	if !info.Available || info.Chip != "ESP32-S3" || info.Version == "" {
		t.Fatalf("build del repository = %+v", info)
	}
	t.Logf("firmware %s %s: %d parti", info.Version, info.Chip, len(info.Parts))
	for _, p := range info.Parts {
		t.Logf("  0x%05x %-15s %8d B sha256 %s", p.Offset, p.Name, p.Size, p.SHA256[:16])
	}
}
