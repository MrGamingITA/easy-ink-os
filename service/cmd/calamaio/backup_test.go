package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"testing"

	"calamaio/internal/backup"
	"calamaio/internal/config"
)

func run(t *testing.T, stdin string, args ...string) (int, string, string) {
	t.Helper()
	var out, errOut bytes.Buffer
	code := backupCmd(args, strings.NewReader(stdin), &out, &errOut)
	return code, out.String(), errOut.String()
}

func TestBackupCLI(t *testing.T) {
	data := t.TempDir()
	t.Setenv("CALAMAIO_DATA_DIR", data)
	t.Setenv("CALAMAIO_CONFIG_DIR", t.TempDir())

	if code, out, _ := run(t, "", "list"); code != 0 || !strings.Contains(out, "Nessun backup") {
		t.Fatalf("list vuota: %d %q", code, out)
	}

	// Un originale di fabbrica, importato come farebbe il servizio all'avvio.
	bs, err := backup.Open(filepath.Join(data, "backups"))
	if err != nil {
		t.Fatal(err)
	}
	img := bytes.Repeat([]byte{0xff}, 8192)
	img[0], img[12], img[13] = 0xE9, 9, 0
	src := filepath.Join(t.TempDir(), "flash_aabbccdd48e8_fabbrica.bin")
	_ = os.WriteFile(src, img, 0o644)
	h := sha256.Sum256(img)
	sum := hex.EncodeToString(h[:])
	_ = os.WriteFile(src+".sha256", []byte(sum+"\n"), 0o644)
	if _, err := bs.ImportFactory(backup.FactorySource{File: src, MAC: "aabbccdd48e8"}); err != nil {
		t.Fatal(err)
	}

	// import: flag dopo il file, chip letto dall'intestazione.
	code, out, errOut := run(t, "", "import", src, "--mac", "AA:BB:CC:DD:48:E8", "--name", "Copia a mano")
	if code != 0 {
		t.Fatalf("import: %d %s %s", code, out, errOut)
	}
	id := regexp.MustCompile(`u-[0-9a-f]{12}`).FindString(out)
	if id == "" {
		t.Fatalf("id non stampato: %q", out)
	}
	b, err := bs.Get(id)
	if err != nil || b.Kind != "user" || b.Protected || b.Chip != "ESP32-S3" || b.MAC != "aa:bb:cc:dd:48:e8" ||
		b.Board != "calamaio-48e8" || b.Name != "Copia a mano" || b.SHA256 != sum {
		t.Fatalf("importato %+v %v", b, err)
	}
	if code, _, _ := run(t, "", "import", src); code != 2 {
		t.Fatalf("import senza --mac: %d", code)
	}
	if code, _, _ := run(t, "", "import", src, "--mac", "aa:bb", "--chip", "ESP32"); code != 2 {
		t.Fatalf("import con MAC sbagliato: %d", code)
	}

	code, out, _ = run(t, "", "list")
	if code != 0 || !strings.Contains(out, "factory-aabbccdd48e8") || !strings.Contains(out, "fabbrica, protetto") ||
		!strings.Contains(out, id) || !strings.Contains(out, "2 backup") {
		t.Fatalf("list: %q", out)
	}

	// rm del protetto: rifiutato anche con --yes.
	code, _, errOut = run(t, "", "rm", "--yes", "factory-aabbccdd48e8")
	if code != 1 || !strings.Contains(errOut, "protetto") {
		t.Fatalf("rm protetto: %d %q", code, errOut)
	}
	if code, _, _ := run(t, "", "rm", "--yes", "../backups/factory-aabbccdd48e8"); code != 1 {
		t.Fatalf("rm con percorso: %d", code)
	}
	// rm senza conferma: non elimina.
	if code, out, _ := run(t, "no\n", "rm", id); code != 1 || !strings.Contains(out, "Non eliminato") {
		t.Fatalf("rm senza conferma: %d %q", code, out)
	}
	if code, out, errOut := run(t, "sì\n", "rm", id); code != 0 {
		t.Fatalf("rm: %d %q %q", code, out, errOut)
	}
	if l, _ := bs.List(); len(l) != 1 || l[0].Kind != "factory" {
		t.Fatalf("dopo rm: %+v", l)
	}
	if runtime.GOOS != "windows" {
		st, _ := os.Stat(filepath.Join(data, "backups", "factory-aabbccdd48e8.bin"))
		if st.Mode().Perm() != 0o400 {
			t.Fatalf("permessi fabbrica %o", st.Mode().Perm())
		}
	}
}

func TestFactorySources(t *testing.T) {
	t.Setenv("CALAMAIO_CONFIG_DIR", "/cfg")
	c := config.Default()
	// Flag: MAC dal nome del file.
	got := factorySources(c, backup.FactorySource{File: "/x/heltec_aabbccdd48e8_fabbrica.bin"})
	if len(got) != 1 || got[0].MAC != "aabbccdd48e8" || got[0].File != "/x/heltec_aabbccdd48e8_fabbrica.bin" {
		t.Fatalf("flag: %+v", got)
	}
	// Config: percorso relativo alla cartella della config.
	c.FactoryBackups = []config.FactoryBackup{{File: "f.bin", MAC: "f4:12:fa:9c:41:10", Name: "Seconda"}}
	got = factorySources(c, backup.FactorySource{})
	if len(got) != 1 || got[0].File != filepath.Join("/cfg", "f.bin") || got[0].Name != "Seconda" {
		t.Fatalf("config: %+v", got)
	}
}
