package ota

import (
	"crypto/ed25519"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/m5"
)

func TestKeygenRefusesOverwrite(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "sub", KeyFile)
	k1, err := Generate(path, false, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	st, _ := os.Stat(path)
	if st.Mode().Perm() != 0o600 {
		t.Fatalf("permessi %o, attesi 0600", st.Mode().Perm())
	}
	if dst, _ := os.Stat(filepath.Dir(path)); dst.Mode().Perm() != 0o700 {
		t.Fatalf("cartella %o, attesa 0700", dst.Mode().Perm())
	}
	if _, err := Generate(path, false, time.Now()); !errors.Is(err, ErrKeyExists) {
		t.Fatalf("seconda keygen senza --force: %v", err)
	}
	got, err := LoadKey(path)
	if err != nil || got.PubHex() != k1.PubHex() {
		t.Fatalf("chiave riletta %v %v", got, err)
	}
	k2, err := Generate(path, true, time.Now())
	if err != nil || k2.PubHex() == k1.PubHex() {
		t.Fatalf("--force: %v", err)
	}
	if st, _ := os.Stat(path); st.Mode().Perm() != 0o600 {
		t.Fatalf("dopo --force permessi %o", st.Mode().Perm())
	}
	os.Chmod(path, 0o644)
	if _, err := LoadKey(path); err == nil || !strings.Contains(err.Error(), "chmod 600") {
		t.Fatalf("chiave leggibile da tutti accettata: %v", err)
	}
	c := &KeyCache{Path: path}
	if _, err := c.Get(); err == nil {
		t.Fatal("cache: chiave 0644 accettata")
	}
	os.Chmod(path, 0o600)
	if k, err := c.Get(); err != nil || k.PubHex() != k2.PubHex() {
		t.Fatalf("cache dopo chmod: %v", err)
	}
	if _, err := LoadKey(filepath.Join(dir, "niente")); !errors.Is(err, ErrNoKey) {
		t.Fatalf("chiave assente: %v", err)
	}
}

// Vettore fisso: seme 32 × 0x01. Lo stesso calcolo lo fa il firmware (TweetNaCl crypto_sign); il
// confronto con protocol/vectors/ota-vectors.json è in vectors_test.go.
func TestManifestCanonicalAndSignature(t *testing.T) {
	seed := make([]byte, 32)
	for i := range seed {
		seed[i] = 1
	}
	priv := ed25519.NewKeyFromSeed(seed)
	m := Manifest{Board: "calamaio-3f2a", Device: "heltec213", Version: "0.7.0", Size: 1556288,
		SHA256: strings.Repeat("ab", 32), Built: 1791738000000}
	want := "calamaio-3f2a\x1fheltec213\x1f0.7.0\x1f1556288\x1f" + strings.Repeat("ab", 32) + "\x1f1791738000000"
	if string(m.Canonical()) != want {
		t.Fatalf("forma canonica %q", m.Canonical())
	}
	sig, err := m.Sign(priv)
	if err != nil {
		t.Fatal(err)
	}
	if len(sig) != 128 || !m.Verify(priv.Public().(ed25519.PublicKey), sig) {
		t.Fatalf("firma %s", sig)
	}
	// Ed25519 è deterministico: la firma è un vettore stabile.
	t.Logf("pub %s sig %s", hex.EncodeToString(priv.Public().(ed25519.PublicKey)), sig)
	for _, bad := range []Manifest{
		{Board: m.Board, Device: m.Device, Version: "0.7.1", Size: m.Size, SHA256: m.SHA256, Built: m.Built},
		{Board: "calamaio-0000", Device: m.Device, Version: m.Version, Size: m.Size, SHA256: m.SHA256, Built: m.Built},
		{Board: m.Board, Device: m.Device, Version: m.Version, Size: m.Size + 1, SHA256: m.SHA256, Built: m.Built},
	} {
		if bad.Verify(priv.Public().(ed25519.PublicKey), sig) {
			t.Fatalf("firma valida per un manifesto diverso: %+v", bad)
		}
	}
	// Un separatore dentro un campo renderebbe ambigua la forma canonica: rifiutato.
	if _, err := (Manifest{Board: "calamaio-3f2a", Device: "x\x1fy", Version: "1", Size: 1, SHA256: m.SHA256}).Sign(priv); err == nil {
		t.Fatal("device con 0x1F accettato")
	}
	// mac dei messaggi
	sk := make([]byte, 32)
	if got := hex.EncodeToString(m5.OtaOfferMac(sk, "o1", m.SHA256)); len(got) != 32 {
		t.Fatal(got)
	}
	if hex.EncodeToString(m5.OtaKeyMac(sk, "aa")) == hex.EncodeToString(m5.OtaCancelMac(sk, "aa")) {
		t.Fatal("etichette uguali")
	}
}

func writeBuild(t *testing.T, dir string, img []byte, files map[string]string) {
	t.Helper()
	os.MkdirAll(dir, 0o755)
	if err := os.WriteFile(filepath.Join(dir, "firmware.bin"), img, 0o644); err != nil {
		t.Fatal(err)
	}
	for n, s := range files {
		os.WriteFile(filepath.Join(dir, n), []byte(s), 0o644)
	}
}

func TestResolveVersionAndDevice(t *testing.T) {
	root := t.TempDir()
	elf := []byte("\x7fELF finto")

	// build del repository: versione da model.h, profilo dal nome dell'ambiente, ELF accanto
	repo := filepath.Join(root, "firmware")
	os.MkdirAll(filepath.Join(repo, "src", "core"), 0o755)
	os.WriteFile(filepath.Join(repo, "src", "core", "model.h"), []byte("#define CALAMAIO_FW_VERSION \"0.7.0\"\n"), 0o644)
	bdir := filepath.Join(repo, ".pio", "build", "heltec213")
	writeBuild(t, bdir, FakeImage("0.7.0", "heltec213", 4096, elf), map[string]string{"firmware.elf": string(elf)})
	b, err := Resolve(ResolveOptions{Path: bdir})
	if err != nil {
		t.Fatal(err)
	}
	if b.Version != "0.7.0" || b.Device != "heltec213" || b.ELF == "" || b.Info.Chip != "ESP32-S3" {
		t.Fatalf("%+v", b)
	}
	// model.h cambiato dopo la build: la versione nuova non è nell'immagine → rifiuto
	os.WriteFile(filepath.Join(repo, "src", "core", "model.h"), []byte("#define CALAMAIO_FW_VERSION \"0.8.0\"\n"), 0o644)
	if _, err := Resolve(ResolveOptions{Path: bdir}); err == nil || !strings.Contains(err.Error(), "non compare") {
		t.Fatalf("versione stantia accettata: %v", err)
	}
	// il file version accanto vince su model.h
	os.WriteFile(filepath.Join(bdir, "version"), []byte("0.7.0\n"), 0o644)
	if b, err := Resolve(ResolveOptions{Path: filepath.Join(bdir, "firmware.bin")}); err != nil || b.VersionSrc != "file version" {
		t.Fatalf("%v %+v", err, b)
	}
	// ELF di un'altra build: non usato
	os.WriteFile(filepath.Join(bdir, "firmware.elf"), []byte("altro"), 0o644)
	if b, _ := Resolve(ResolveOptions{Path: bdir}); b.ELF != "" || b.ELFNote == "" {
		t.Fatalf("ELF sbagliato accettato: %+v", b)
	}

	// file sciolto senza indizi: serve --version; il profilo dalla scheda solo se è nell'immagine
	loose := filepath.Join(root, "sciolto")
	writeBuild(t, loose, FakeImage("1.2.3", "heltec213", 2048, nil), nil)
	if _, err := Resolve(ResolveOptions{Path: loose}); err == nil {
		t.Fatal("senza versione accettato")
	}
	if b, err := Resolve(ResolveOptions{Path: loose, Version: "1.2.3", DeviceHint: "heltec213"}); err != nil || b.Device != "heltec213" {
		t.Fatalf("%v", err)
	}
	if _, err := Resolve(ResolveOptions{Path: loose, Version: "1.2.3", DeviceHint: "altra290"}); err == nil {
		t.Fatal("profilo assente dall'immagine accettato")
	}

	// stringa calamaio-fw: vale da sola
	mk := FakeImage("calamaio-fw 2.0.0 heltec213", "", 2048, nil)
	writeBuild(t, filepath.Join(root, "marker"), mk, nil)
	if b, err := Resolve(ResolveOptions{Path: filepath.Join(root, "marker")}); err != nil || b.Version != "2.0.0" || b.Device != "heltec213" {
		t.Fatalf("%v %+v", err, b)
	}

	// non un'app: bootloader o immagine completa
	bad := FakeImage("1", "x", 1024, nil)
	bad[appDescOff] = 0
	writeBuild(t, filepath.Join(root, "bad"), bad, map[string]string{"version": "1"})
	if _, err := Resolve(ResolveOptions{Path: filepath.Join(root, "bad")}); err == nil {
		t.Fatal("immagine senza descrittore accettata")
	}
	// troppo grande per la partizione
	big := filepath.Join(root, "big")
	writeBuild(t, big, FakeImage("1.0.0", "heltec213", DefaultMaxApp+1, nil), map[string]string{"version": "1.0.0"})
	if _, err := Resolve(ResolveOptions{Path: big}); err == nil || !strings.Contains(err.Error(), "partizione") {
		t.Fatalf("immagine troppo grande: %v", err)
	}
}

// CheckImage (calamaio ota push): il limite è la partizione app della tabella del firmware, 0x240000.
func TestCheckImageMaxSize(t *testing.T) {
	if DefaultMaxApp != 0x240000 {
		t.Fatalf("DefaultMaxApp = %#x, la tabella del firmware dà 0x240000", DefaultMaxApp)
	}
	if _, err := CheckImage(FakeImage("1.0.0", "heltec213", 0x240001, nil), "1.0.0", "heltec213"); err == nil || !strings.Contains(err.Error(), "partizione") {
		t.Fatalf("immagine di 0x240001 byte: %v", err)
	}
	if _, err := CheckImage(FakeImage("1.0.0", "heltec213", 0x240000, nil), "1.0.0", "heltec213"); err != nil {
		t.Fatalf("immagine di 0x240000 byte rifiutata: %v", err)
	}
}

// La build vera del repository, se c'è: versione da model.h presente nell'immagine, impronta dell'ELF
// uguale a SHA-256 di firmware.elf.
func TestResolveRealBuild(t *testing.T) {
	dir := "../../../firmware/.pio/build/heltec213"
	if _, err := os.Stat(filepath.Join(dir, "firmware.bin")); err != nil {
		t.Skip("build del firmware assente")
	}
	b, err := Resolve(ResolveOptions{Path: dir})
	if err != nil {
		t.Fatalf("build vera: %v", err)
	}
	t.Logf("versione %s (%s), profilo %s (%s), %d byte, chip %s, elf %q %s, massimo %d", b.Version, b.VersionSrc,
		b.Device, b.DeviceSrc, len(b.Bin), b.Info.Chip, b.ELF, b.ELFNote, b.MaxSize)
	if b.Device != "heltec213" || b.Info.Chip != "ESP32-S3" || b.MaxSize != DefaultMaxApp {
		t.Fatalf("%+v", b)
	}
}

func TestArchiveELF(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "firmware.elf")
	os.WriteFile(src, []byte("elf"), 0o644)
	sum, _ := fileSHA256(src)
	if _, err := ArchiveELF(filepath.Join(dir, "a"), src, "0.7.0", strings.Repeat("0", 64)); err == nil {
		t.Fatal("impronta sbagliata accettata")
	}
	p, err := ArchiveELF(filepath.Join(dir, "a"), src, "0.7.0", sum)
	if err != nil {
		t.Fatal(err)
	}
	l := ListELF(filepath.Join(dir, "a"))
	if len(l) != 1 || l[0].Version != "0.7.0" || l[0].SHA256 != sum || l[0].Path != p {
		t.Fatalf("%+v", l)
	}
}
