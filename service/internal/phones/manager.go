package phones

import (
	"context"
	"errors"
	"sync"
	"time"
)

// Manager mette insieme elenco e finestra, chiude gli stream di un telefono revocato e avvisa (OnChange)
// quando l'ascoltatore va acceso o spento. OnChange non viene mai chiamata col lucchetto preso.
type Manager struct {
	Store    *Store
	Pairing  *Pairing
	OnChange func()

	mu      sync.Mutex
	streams map[string]map[int]context.CancelFunc
	next    int
	timer   *time.Timer
}

func NewManager(st *Store) *Manager {
	return &Manager{Store: st, Pairing: &Pairing{}, streams: map[string]map[int]context.CancelFunc{}}
}

func (m *Manager) changed() {
	if m.OnChange != nil {
		m.OnChange()
	}
}

// Wanted: l'ascoltatore dei telefoni serve (almeno un telefono o una finestra aperta).
func (m *Manager) Wanted() bool {
	if m.Store.Len() > 0 {
		return true
	}
	_, _, ok := m.Pairing.Active()
	return ok
}

// OpenWindow apre la finestra e programma l'avviso della scadenza (l'ascoltatore si spegne se non serve più).
func (m *Manager) OpenWindow() (string, time.Time) {
	code, exp := m.Pairing.Open()
	m.mu.Lock()
	if m.timer != nil {
		m.timer.Stop()
	}
	m.timer = time.AfterFunc(time.Until(exp)+50*time.Millisecond, m.changed)
	m.mu.Unlock()
	m.changed()
	return code, exp
}

func (m *Manager) CancelWindow() {
	m.Pairing.Cancel()
	m.stopTimer()
	m.changed()
}

func (m *Manager) stopTimer() {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.timer != nil {
		m.timer.Stop()
		m.timer = nil
	}
}

func (m *Manager) Lookup(token string) (Phone, bool) { return m.Store.Lookup(token) }
func (m *Manager) Touch(id string)                   { m.Store.Touch(id) }

// Redeem: codice giusto → telefono nuovo. oldToken, se valido, è lo stesso telefono abbinato prima (QR nuovo
// dopo un cambio di indirizzo): il telefono vecchio si revoca, così sul PC non restano doppioni.
func (m *Manager) Redeem(code, name, oldToken string) (string, Phone, error) {
	if err := m.Pairing.Redeem(code); err != nil {
		m.changed() // la quinta prova sbagliata chiude la finestra
		return "", Phone{}, err
	}
	m.stopTimer()
	tok, p, err := m.Store.Add(name)
	if err != nil {
		m.changed()
		return "", Phone{}, err
	}
	if oldToken != "" {
		if op, ok := m.Store.Lookup(oldToken); ok && op.ID != p.ID {
			_ = m.revoke(op.ID)
		}
	}
	m.changed()
	return tok, p, nil
}

// Revoke toglie il telefono e chiude subito le sue richieste aperte (SSE compresi).
func (m *Manager) Revoke(id string) error {
	err := m.revoke(id)
	if !errors.Is(err, ErrNotFound) {
		m.changed()
	}
	return err
}

func (m *Manager) revoke(id string) error {
	err := m.Store.Revoke(id)
	if errors.Is(err, ErrNotFound) {
		return err
	}
	m.mu.Lock()
	cs := m.streams[id]
	delete(m.streams, id)
	m.mu.Unlock()
	for _, c := range cs {
		c()
	}
	return err
}

// Track: un contesto che si chiude alla revoca del telefono; done va chiamata a fine richiesta. Per un
// telefono già revocato il contesto è chiuso subito.
func (m *Manager) Track(ctx context.Context, id string) (context.Context, func()) {
	ctx, cancel := context.WithCancel(ctx)
	m.mu.Lock()
	m.next++
	n := m.next
	if m.streams[id] == nil {
		m.streams[id] = map[int]context.CancelFunc{}
	}
	m.streams[id][n] = cancel
	m.mu.Unlock()
	if !m.Store.Has(id) {
		cancel()
	}
	return ctx, func() {
		m.mu.Lock()
		delete(m.streams[id], n)
		if len(m.streams[id]) == 0 {
			delete(m.streams, id)
		}
		m.mu.Unlock()
		cancel()
	}
}

// Close ferma il timer della finestra (allo spegnimento del servizio).
func (m *Manager) Close() { m.stopTimer() }
