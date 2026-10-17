package config

import (
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// KeyStore custodisce le chiavi di abbinamento in config.json, sotto "paired" (M5 §5).
//
// Legge e riscrive solo la voce "paired": le altre voci del file restano com'erano, byte per byte
// nel valore (così le porte sostituite da variabili d'ambiente non finiscono nel file). File 0600,
// cartella 0700, scrittura atomica (file temporaneo e rename). Path vuoto = solo in memoria (test).
type KeyStore struct {
	Path string

	mu  sync.Mutex
	mem []Paired // usato solo con Path vuoto
}

// NewKeyStore usa config.json della cartella di configurazione (CALAMAIO_CONFIG_DIR nei test).
func NewKeyStore() (*KeyStore, error) {
	p, err := Path()
	if err != nil {
		return nil, err
	}
	return &KeyStore{Path: p}, nil
}

// List restituisce i display abbinati validi (chiave di 32 byte in esadecimale).
func (k *KeyStore) List() ([]Paired, error) {
	k.mu.Lock()
	defer k.mu.Unlock()
	_, list, err := k.read()
	return list, err
}

// Put salva (o sostituisce) la chiave di board.
func (k *KeyStore) Put(board string, key []byte, created time.Time) error {
	if board == "" || len(key) != 32 {
		return errors.New("chiave di abbinamento non valida")
	}
	fileMu.Lock()
	defer fileMu.Unlock()
	k.mu.Lock()
	defer k.mu.Unlock()
	raw, list, err := k.read()
	if err != nil {
		return err
	}
	out := make([]Paired, 0, len(list)+1)
	for _, p := range list {
		if p.Board != board {
			out = append(out, p)
		}
	}
	out = append(out, Paired{Board: board, Key: hex.EncodeToString(key), Created: created.UTC().Format(time.RFC3339)})
	return k.write(raw, out)
}

// Delete toglie la chiave di board; dice se c'era.
func (k *KeyStore) Delete(board string) (bool, error) {
	fileMu.Lock()
	defer fileMu.Unlock()
	k.mu.Lock()
	defer k.mu.Unlock()
	raw, list, err := k.read()
	if err != nil {
		return false, err
	}
	out := make([]Paired, 0, len(list))
	found := false
	for _, p := range list {
		if p.Board == board {
			found = true
			continue
		}
		out = append(out, p)
	}
	if !found {
		return false, nil
	}
	return true, k.write(raw, out)
}

// read: il file come mappa di voci grezze e la lista "paired" (solo le voci valide).
func (k *KeyStore) read() (map[string]json.RawMessage, []Paired, error) {
	if k.Path == "" {
		return nil, append([]Paired(nil), k.mem...), nil
	}
	raw := map[string]json.RawMessage{}
	b, err := os.ReadFile(k.Path)
	switch {
	case errors.Is(err, fs.ErrNotExist):
		return raw, nil, nil
	case err != nil:
		return nil, nil, err
	}
	if err := json.Unmarshal(b, &raw); err != nil {
		return nil, nil, fmt.Errorf("%s: %w", k.Path, err)
	}
	var list []Paired
	if p, ok := raw["paired"]; ok && string(p) != "null" {
		if err := json.Unmarshal(p, &list); err != nil {
			return nil, nil, fmt.Errorf("%s, voce paired: %w", k.Path, err)
		}
	}
	valid := list[:0]
	for _, p := range list {
		if kb, err := hex.DecodeString(p.Key); err == nil && len(kb) == 32 && p.Board != "" {
			valid = append(valid, p)
		}
	}
	return raw, valid, nil
}

func (k *KeyStore) write(raw map[string]json.RawMessage, list []Paired) error {
	if k.Path == "" {
		k.mem = list
		return nil
	}
	if len(list) == 0 {
		delete(raw, "paired")
	} else {
		b, err := json.Marshal(list)
		if err != nil {
			return err
		}
		raw["paired"] = b
	}
	b, err := json.MarshalIndent(raw, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(k.Path), 0o700); err != nil {
		return err
	}
	tmp := k.Path + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	if err := os.Chmod(tmp, 0o600); err != nil { // WriteFile non cambia i permessi di un file già esistente
		return err
	}
	return os.Rename(tmp, k.Path)
}

// KeyBytes decodifica la chiave di un abbinamento (nil se non valida).
func (p Paired) KeyBytes() []byte {
	b, err := hex.DecodeString(p.Key)
	if err != nil || len(b) != 32 {
		return nil
	}
	return b
}
