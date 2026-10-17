package phones

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/x509"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

var t0 = time.Date(2026, 10, 16, 12, 0, 0, 0, time.UTC)

func TestCertCreateReuseAndPerms(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "app-tls")
	c1, fp1, err := LoadOrCreateCert(dir, "MacBook", t0)
	if err != nil {
		t.Fatal(err)
	}
	pub, ok := c1.Leaf.PublicKey.(*ecdsa.PublicKey)
	if !ok || pub.Curve != elliptic.P256() || c1.Leaf.PublicKeyAlgorithm != x509.ECDSA {
		t.Fatalf("chiave: %T", c1.Leaf.PublicKey)
	}
	if !c1.Leaf.NotAfter.Equal(t0.AddDate(CertYears, 0, 0)) {
		t.Fatalf("scadenza %v", c1.Leaf.NotAfter)
	}
	if fp1 != Fingerprint(c1.Certificate[0]) || !isLowerHex(fp1, 64) {
		t.Fatalf("impronta %q", fp1)
	}
	for name, want := range map[string]os.FileMode{"": 0o700, "key.pem": 0o600, "cert.pem": 0o600} {
		st, err := os.Stat(filepath.Join(dir, name))
		if err != nil || st.Mode().Perm() != want {
			t.Fatalf("%s: %v %v", name, err, st.Mode().Perm())
		}
	}
	os.Chmod(filepath.Join(dir, "key.pem"), 0o644)
	_, fp2, err := LoadOrCreateCert(dir, "MacBook", t0.Add(time.Hour))
	if err != nil || fp2 != fp1 {
		t.Fatalf("riletto: %v %s %s", err, fp1, fp2)
	}
	if st, _ := os.Stat(filepath.Join(dir, "key.pem")); st.Mode().Perm() != 0o600 {
		t.Fatal("permessi della chiave non riparati")
	}
	// nessuna cartella temporanea lasciata accanto
	if m, _ := filepath.Glob(filepath.Join(filepath.Dir(dir), ".app-tls-*")); len(m) != 0 {
		t.Fatalf("avanzi: %v", m)
	}
}

func TestCertBrokenNotRegenerated(t *testing.T) {
	dir := t.TempDir() // cartella già esistente e vuota: il certificato si crea dentro
	if _, _, err := LoadOrCreateCert(dir, "pc", t0); err != nil {
		t.Fatal(err)
	}
	kp, cp := filepath.Join(dir, "key.pem"), filepath.Join(dir, "cert.pem")
	cert, _ := os.ReadFile(cp)
	os.WriteFile(kp, []byte("rotto"), 0o600)
	if _, _, err := LoadOrCreateCert(dir, "pc", t0); err == nil || !strings.Contains(err.Error(), "illeggibile") {
		t.Fatalf("chiave rotta: %v", err)
	}
	if b, _ := os.ReadFile(kp); string(b) != "rotto" {
		t.Fatal("chiave rotta riscritta")
	}
	if b, _ := os.ReadFile(cp); string(b) != string(cert) {
		t.Fatal("certificato riscritto")
	}
	os.Remove(kp) // solo il certificato: anche questo è un errore, mai un certificato nuovo
	if _, _, err := LoadOrCreateCert(dir, "pc", t0); err == nil {
		t.Fatal("certificato senza chiave accettato")
	}
	if _, err := os.Stat(kp); !os.IsNotExist(err) {
		t.Fatal("chiave ricreata")
	}
}

func TestCertExpired(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "app-tls")
	LoadOrCreateCert(dir, "pc", t0)
	if _, _, err := LoadOrCreateCert(dir, "pc", t0.AddDate(CertYears, 0, 1)); err == nil || !strings.Contains(err.Error(), "scaduto") {
		t.Fatalf("scaduto: %v", err)
	}
}

func TestCertInMemory(t *testing.T) {
	c, fp, err := LoadOrCreateCert("", "pc", t0)
	if err != nil || c.Leaf == nil || fp != Fingerprint(c.Certificate[0]) {
		t.Fatalf("%v %q", err, fp)
	}
}
