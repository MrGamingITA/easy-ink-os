package config

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// AvatarAssignments: voce "avatars" di config.json (docs/AVATAR.md §6, decisione del proprietario
// sulla domanda 2). Default vuoto = "goccia". Projects: cartella del progetto (assoluta,
// normalizzata: radice git se c'è) → id. Sessions: session_id → id.
type AvatarAssignments struct {
	Default  string            `json:"default,omitempty"`
	Projects map[string]string `json:"projects,omitempty"`
	Sessions map[string]string `json:"sessions,omitempty"`
}

// Clone: copia profonda (mai nil le mappe).
func (a AvatarAssignments) Clone() AvatarAssignments {
	c := AvatarAssignments{Default: a.Default, Projects: map[string]string{}, Sessions: map[string]string{}}
	for k, v := range a.Projects {
		c.Projects[k] = v
	}
	for k, v := range a.Sessions {
		c.Sessions[k] = v
	}
	return c
}

func (a AvatarAssignments) empty() bool {
	return a.Default == "" && len(a.Projects) == 0 && len(a.Sessions) == 0
}

// fileMu serializza le riscritture di config.json fatte da questo processo (KeyStore e
// AvatarStore leggono e riscrivono ciascuno la sua voce: senza un lucchetto comune una delle due
// modifiche potrebbe andare persa).
var fileMu sync.Mutex

// AvatarStore custodisce la voce "avatars" di config.json, come KeyStore fa con "paired": le altre
// voci restano com'erano. Path vuoto = solo in memoria (test). Le letture riusano il valore finché
// il file non cambia (dimensione e data di modifica), così si può chiamare Get a ogni messaggio.
type AvatarStore struct {
	Path string

	mu    sync.Mutex
	mem   AvatarAssignments
	cache *AvatarAssignments
	mod   time.Time
	size  int64
}

// NewAvatarStore usa config.json della cartella di configurazione (CALAMAIO_CONFIG_DIR nei test).
func NewAvatarStore() (*AvatarStore, error) {
	p, err := Path()
	if err != nil {
		return nil, err
	}
	return &AvatarStore{Path: p}, nil
}

// Get restituisce le assegnazioni attuali (copia).
func (s *AvatarStore) Get() (AvatarAssignments, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.Path == "" {
		return s.mem.Clone(), nil
	}
	fi, err := os.Stat(s.Path)
	switch {
	case errors.Is(err, fs.ErrNotExist):
		s.cache = nil
		return AvatarAssignments{}.Clone(), nil
	case err != nil:
		return AvatarAssignments{}.Clone(), err
	}
	if s.cache != nil && fi.ModTime().Equal(s.mod) && fi.Size() == s.size {
		return s.cache.Clone(), nil
	}
	_, a, err := readAvatars(s.Path)
	if err != nil {
		return AvatarAssignments{}.Clone(), err
	}
	s.cache, s.mod, s.size = &a, fi.ModTime(), fi.Size()
	return a.Clone(), nil
}

// Update applica fn alle assegnazioni e riscrive config.json (atomico, 0600). Se fn restituisce
// un errore non si scrive niente.
func (s *AvatarStore) Update(fn func(*AvatarAssignments) error) error {
	fileMu.Lock()
	defer fileMu.Unlock()
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.Path == "" {
		a := s.mem.Clone()
		if err := fn(&a); err != nil {
			return err
		}
		s.mem = a
		return nil
	}
	raw, a, err := readAvatars(s.Path)
	if err != nil {
		return err
	}
	a = a.Clone()
	if err := fn(&a); err != nil {
		return err
	}
	if a.empty() {
		delete(raw, "avatars")
	} else {
		b, err := json.Marshal(a)
		if err != nil {
			return err
		}
		raw["avatars"] = b
	}
	s.cache = nil
	return writeRaw(s.Path, raw)
}

func readAvatars(path string) (map[string]json.RawMessage, AvatarAssignments, error) {
	raw := map[string]json.RawMessage{}
	var a AvatarAssignments
	b, err := os.ReadFile(path)
	switch {
	case errors.Is(err, fs.ErrNotExist):
		return raw, a, nil
	case err != nil:
		return nil, a, err
	}
	if err := json.Unmarshal(b, &raw); err != nil {
		return nil, a, fmt.Errorf("%s: %w", path, err)
	}
	if v, ok := raw["avatars"]; ok && string(v) != "null" {
		if err := json.Unmarshal(v, &a); err != nil {
			return nil, a, fmt.Errorf("%s, voce avatars: %w", path, err)
		}
	}
	return raw, a, nil
}

// writeRaw riscrive config.json con le voci grezze (file 0600, cartella 0700, rename atomico).
func writeRaw(path string, raw map[string]json.RawMessage) error {
	b, err := json.MarshalIndent(raw, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	if err := os.Chmod(tmp, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}
