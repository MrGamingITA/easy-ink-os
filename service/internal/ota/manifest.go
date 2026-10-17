package ota

import (
	"crypto/ed25519"
	"encoding/hex"
	"errors"
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

// Manifest è il manifesto firmato di docs/OTA.md §2. L'ordine dei campi JSON è quello della forma
// canonica.
type Manifest struct {
	Board   string `json:"board"`   // scheda di destinazione (calamaio-xxxx): il manifesto vale per una sola scheda
	Device  string `json:"device"`  // profilo (hello.device), es. heltec213
	Version string `json:"version"` // versione del firmware nuovo
	Size    int64  `json:"size"`    // byte dell'immagine dell'app (firmware.bin)
	SHA256  string `json:"sha256"`  // dell'immagine, esadecimale minuscolo
	Built   int64  `json:"built"`   // ms epoch UTC della build (data di firmware.bin)
}

// Limiti dei campi di testo (il firmware li tiene in buffer fissi).
const (
	MaxBoard   = 31
	MaxDevice  = 31
	MaxVersion = 31
	SigLen     = ed25519.SignatureSize // 64 byte, 128 cifre esadecimali
)

var (
	boardRe   = regexp.MustCompile(`^[a-z0-9][a-z0-9-]{0,30}$`)
	deviceRe  = regexp.MustCompile(`^[a-z0-9][a-z0-9_-]{0,30}$`)
	versionRe = regexp.MustCompile(`^[0-9A-Za-z][0-9A-Za-z.+_-]{0,30}$`)
	sha256Re  = regexp.MustCompile(`^[0-9a-f]{64}$`)
)

// Validate controlla i campi: niente caratteri che renderebbero ambigua la forma canonica.
func (m Manifest) Validate() error {
	switch {
	case !boardRe.MatchString(m.Board):
		return fmt.Errorf("manifesto: board %q non valida", m.Board)
	case !deviceRe.MatchString(m.Device):
		return fmt.Errorf("manifesto: device %q non valido", m.Device)
	case !versionRe.MatchString(m.Version):
		return fmt.Errorf("manifesto: versione %q non valida (lettere, cifre, . + _ -, al massimo 31)", m.Version)
	case m.Size <= 0:
		return errors.New("manifesto: dimensione non valida")
	case !sha256Re.MatchString(m.SHA256):
		return errors.New("manifesto: sha256 non valido")
	case m.Built < 0:
		return errors.New("manifesto: data di build non valida")
	}
	return nil
}

// Canonical è la forma firmata: board ␟ device ␟ version ␟ size ␟ sha256 ␟ built (␟ = 0x1F, interi in
// decimale senza zeri iniziali, sha256 in esadecimale minuscolo, nessun separatore finale).
func (m Manifest) Canonical() []byte {
	return []byte(strings.Join([]string{m.Board, m.Device, m.Version, strconv.FormatInt(m.Size, 10), m.SHA256,
		strconv.FormatInt(m.Built, 10)}, "\x1f"))
}

// Sign firma la forma canonica: firma Ed25519 in esadecimale minuscolo (128 cifre).
func (m Manifest) Sign(priv ed25519.PrivateKey) (string, error) {
	if err := m.Validate(); err != nil {
		return "", err
	}
	return hex.EncodeToString(ed25519.Sign(priv, m.Canonical())), nil
}

// Verify controlla la firma (quello che fa la scheda, docs/OTA.md §3.1).
func (m Manifest) Verify(pub ed25519.PublicKey, sigHex string) bool {
	sig, err := hex.DecodeString(sigHex)
	if err != nil || len(sig) != SigLen || len(pub) != ed25519.PublicKeySize || m.Validate() != nil {
		return false
	}
	return ed25519.Verify(pub, m.Canonical(), sig)
}

// ValidBoard: nome di scheda accettabile in un percorso (file di registro e di crash).
func ValidBoard(b string) bool { return boardRe.MatchString(b) }
