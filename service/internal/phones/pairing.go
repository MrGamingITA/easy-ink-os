package phones

import (
	"crypto/subtle"
	"errors"
	"fmt"
	"sync"
	"time"
)

const (
	DefaultWindow = 5 * time.Minute
	MaxAttempts   = 5
	CodeBytes     = 16 // 128 bit, 32 cifre esadecimali
)

var ErrNoWindow = errors.New("nessun abbinamento aperto")

// CodeError: codice sbagliato; Left = tentativi rimasti (0 = finestra chiusa).
type CodeError struct{ Left int }

func (e *CodeError) Error() string {
	return fmt.Sprintf("codice sbagliato (%d tentativi rimasti)", e.Left)
}

// Pairing: la finestra di abbinamento. Un codice alla volta, monouso, al massimo 5 tentativi sbagliati.
type Pairing struct {
	Now    func() time.Time
	Window time.Duration // 0 = DefaultWindow

	mu      sync.Mutex
	code    string
	expires time.Time
	wrong   int
}

func (p *Pairing) now() time.Time {
	if p.Now != nil {
		return p.Now()
	}
	return time.Now()
}

// Open apre (o riapre) la finestra con un codice nuovo; il codice di prima non vale più.
func (p *Pairing) Open() (string, time.Time) {
	w := p.Window
	if w <= 0 {
		w = DefaultWindow
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	p.code, p.expires, p.wrong = randomHex(CodeBytes), p.now().Add(w), 0
	return p.code, p.expires
}

func (p *Pairing) Cancel() {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.code = ""
}

// Active: il codice della finestra aperta (per il QR); una finestra scaduta si chiude qui.
func (p *Pairing) Active() (string, time.Time, bool) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.code == "" || !p.now().Before(p.expires) {
		p.code = ""
		return "", time.Time{}, false
	}
	return p.code, p.expires, true
}

// Redeem: nil = codice giusto (e finestra chiusa); ErrNoWindow; *CodeError.
func (p *Pairing) Redeem(code string) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.code == "" || !p.now().Before(p.expires) {
		p.code = ""
		return ErrNoWindow
	}
	if subtle.ConstantTimeCompare([]byte(code), []byte(p.code)) == 1 {
		p.code = ""
		return nil
	}
	p.wrong++
	left := MaxAttempts - p.wrong
	if left <= 0 {
		p.code, left = "", 0
	}
	return &CodeError{Left: left}
}
