package phones

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"io/fs"
	"math/big"
	"os"
	"path/filepath"
	"time"
)

const CertYears = 10

// Fingerprint: SHA-256 del certificato (DER) in esadecimale minuscolo. È ciò che l'app fissa dal QR.
func Fingerprint(der []byte) string {
	sum := sha256.Sum256(der)
	return hex.EncodeToString(sum[:])
}

// LoadOrCreateCert: il certificato della porta dei telefoni in dir (cert.pem e key.pem 0600, cartella 0700).
// Si crea solo se la cartella non ha né l'uno né l'altro. Uno rovinato, a metà o scaduto è un errore e NON si
// rifà: ogni telefono abbinato ne ha fissato l'impronta. Per rifarlo si cancella la cartella e si riabbinano
// i telefoni. dir "" = certificato nuovo solo in memoria (test).
func LoadOrCreateCert(dir, host string, now time.Time) (tls.Certificate, string, error) {
	if dir == "" {
		cp, kp, err := newCert(host, now)
		if err != nil {
			return tls.Certificate{}, "", err
		}
		return keyPair(cp, kp)
	}
	cpath, kpath := filepath.Join(dir, "cert.pem"), filepath.Join(dir, "key.pem")
	_, cerr := os.Stat(cpath)
	_, kerr := os.Stat(kpath)
	if errors.Is(cerr, fs.ErrNotExist) && errors.Is(kerr, fs.ErrNotExist) {
		if err := create(dir, host, now); err != nil {
			return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s: %w", dir, err)
		}
	}
	cp, err1 := os.ReadFile(cpath)
	kp, err2 := os.ReadFile(kpath)
	if err := errors.Join(err1, err2); err != nil {
		return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s illeggibile: %w", dir, err)
	}
	c, fp, err := keyPair(cp, kp)
	if err != nil {
		return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s illeggibile: %w", dir, err)
	}
	if now.After(c.Leaf.NotAfter) {
		return tls.Certificate{}, "", fmt.Errorf("certificato dei telefoni in %s scaduto il %s", dir, c.Leaf.NotAfter.Format("2006-01-02"))
	}
	_ = os.Chmod(kpath, 0o600) // ripara i permessi se qualcuno li ha allargati
	return c, fp, nil
}

func keyPair(certPEM, keyPEM []byte) (tls.Certificate, string, error) {
	c, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return tls.Certificate{}, "", err
	}
	if c.Leaf == nil {
		if c.Leaf, err = x509.ParseCertificate(c.Certificate[0]); err != nil {
			return tls.Certificate{}, "", err
		}
	}
	return c, Fingerprint(c.Certificate[0]), nil
}

// create: cartella nuova preparata accanto e rinominata (mai una cartella a metà); cartella già esistente e
// vuota: file temporanei e rinomina, prima la chiave.
func create(dir, host string, now time.Time) error {
	cp, kp, err := newCert(host, now)
	if err != nil {
		return err
	}
	target, fresh := dir, false
	if _, err := os.Stat(dir); errors.Is(err, fs.ErrNotExist) {
		if err := os.MkdirAll(filepath.Dir(dir), 0o700); err != nil {
			return err
		}
		tmp, err := os.MkdirTemp(filepath.Dir(dir), ".app-tls-*")
		if err != nil {
			return err
		}
		defer os.RemoveAll(tmp) // dopo la rinomina non c'è più: non fa niente
		target, fresh = tmp, true
	}
	if err := os.Chmod(target, 0o700); err != nil {
		return err
	}
	if err := writeSecret(filepath.Join(target, "key.pem"), kp); err != nil {
		return err
	}
	if err := writeSecret(filepath.Join(target, "cert.pem"), cp); err != nil {
		return err
	}
	if fresh {
		return os.Rename(target, dir)
	}
	return nil
}

func writeSecret(path string, b []byte) error {
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// newCert: ECDSA P-256 autofirmato, 10 anni. Nessun nome da verificare: l'app controlla solo l'impronta.
func newCert(host string, now time.Time) (certPEM, keyPEM []byte, err error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 127))
	if err != nil {
		return nil, nil, err
	}
	tpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "Calamaio " + host, Organization: []string{"Calamaio"}},
		NotBefore:             now.Add(-time.Hour),
		NotAfter:              now.AddDate(CertYears, 0, 0),
		KeyUsage:              x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		DNSNames:              []string{"calamaio.local"},
	}
	der, err := x509.CreateCertificate(rand.Reader, tpl, tpl, &key.PublicKey, key)
	if err != nil {
		return nil, nil, err
	}
	kder, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		return nil, nil, err
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}),
		pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: kder}), nil
}
