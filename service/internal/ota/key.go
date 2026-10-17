// Package ota contiene la parte del servizio dell'aggiornamento remoto (docs/OTA.md): la chiave di
// firma Ed25519 del PC, il manifesto canonico firmato, la lettura di un'immagine dell'app e la scelta
// di versione e scheda da una build.
package ota

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"time"

	"calamaio/internal/config"
)

// KeyFile è il nome del file della chiave nella cartella di configurazione (<config>/calamaio/ota-key).
const KeyFile = "ota-key"

// ErrNoKey: la chiave di firma non c'è ancora.
var ErrNoKey = errors.New("manca la chiave di firma degli aggiornamenti: creala con `calamaio ota keygen`")

// ErrKeyExists: keygen senza --force su una chiave già presente.
var ErrKeyExists = errors.New("la chiave di firma c'è già: per sostituirla usa --force (le schede la riconosceranno solo dopo un nuovo abbinamento)")

// Key è la coppia Ed25519 del PC.
type Key struct {
	Priv    ed25519.PrivateKey
	Pub     ed25519.PublicKey
	Created time.Time
}

// PubHex: chiave pubblica in esadecimale minuscolo (64 cifre), come in `ota-key`.
func (k *Key) PubHex() string { return hex.EncodeToString(k.Pub) }

// keyJSON è il formato del file: seme di 32 byte (RFC 8032) e chiave pubblica, entrambi in esadecimale.
type keyJSON struct {
	Alg     string `json:"alg"`
	Seed    string `json:"seed"`
	Pub     string `json:"pub"`
	Created string `json:"created"`
}

// DefaultKeyPath: <config>/calamaio/ota-key (CALAMAIO_CONFIG_DIR la sostituisce, come per config.json).
func DefaultKeyPath() (string, error) {
	d, err := config.Dir()
	if err != nil {
		return "", err
	}
	return filepath.Join(d, KeyFile), nil
}

// Generate crea una chiave nuova in path (0600, cartella 0700). Senza force rifiuta di sovrascrivere.
func Generate(path string, force bool, now time.Time) (*Key, error) {
	seed := make([]byte, ed25519.SeedSize)
	if _, err := rand.Read(seed); err != nil {
		return nil, err
	}
	return writeKey(path, seed, force, now)
}

func writeKey(path string, seed []byte, force bool, now time.Time) (*Key, error) {
	priv := ed25519.NewKeyFromSeed(seed)
	k := &Key{Priv: priv, Pub: priv.Public().(ed25519.PublicKey), Created: now.UTC().Truncate(time.Second)}
	b, _ := json.MarshalIndent(keyJSON{Alg: "ed25519", Seed: hex.EncodeToString(seed), Pub: k.PubHex(),
		Created: k.Created.Format(time.RFC3339)}, "", "  ")
	b = append(b, '\n')
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return nil, err
	}
	if !force {
		// O_EXCL: due keygen in parallelo non si sovrascrivono a vicenda.
		f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
		if errors.Is(err, fs.ErrExist) {
			return nil, ErrKeyExists
		}
		if err != nil {
			return nil, err
		}
		if _, err := f.Write(b); err != nil {
			f.Close()
			os.Remove(path)
			return nil, err
		}
		if err := f.Close(); err != nil {
			os.Remove(path)
			return nil, err
		}
		return k, nil
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o600); err != nil {
		return nil, err
	}
	if err := os.Chmod(tmp, 0o600); err != nil { // umask non conta, ma un .tmp rimasto da prima sì
		os.Remove(tmp)
		return nil, err
	}
	if err := os.Rename(tmp, path); err != nil {
		os.Remove(tmp)
		return nil, err
	}
	return k, nil
}

// LoadKey legge la chiave. ErrNoKey se il file non c'è; errore se è leggibile da altri utenti (come ssh).
func LoadKey(path string) (*Key, error) {
	st, err := os.Stat(path)
	if errors.Is(err, fs.ErrNotExist) {
		return nil, ErrNoKey
	}
	if err != nil {
		return nil, err
	}
	if runtime.GOOS != "windows" && st.Mode().Perm()&0o077 != 0 {
		return nil, fmt.Errorf("la chiave di firma %s è leggibile da altri utenti (permessi %04o): chmod 600 %s", path, st.Mode().Perm(), path)
	}
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var kj keyJSON
	if err := json.Unmarshal(b, &kj); err != nil {
		return nil, fmt.Errorf("chiave di firma %s non valida: %v", path, err)
	}
	seed, err := hex.DecodeString(kj.Seed)
	if kj.Alg != "ed25519" || err != nil || len(seed) != ed25519.SeedSize {
		return nil, fmt.Errorf("chiave di firma %s non valida (alg %q, seme di %d byte)", path, kj.Alg, len(seed))
	}
	priv := ed25519.NewKeyFromSeed(seed)
	k := &Key{Priv: priv, Pub: priv.Public().(ed25519.PublicKey)}
	if kj.Pub != "" && kj.Pub != k.PubHex() {
		return nil, fmt.Errorf("chiave di firma %s incoerente: la chiave pubblica non corrisponde al seme", path)
	}
	k.Created, _ = time.Parse(time.RFC3339, kj.Created)
	return k, nil
}

// KeyCache rilegge il file solo quando cambia (keygen fatto a servizio acceso vale subito).
type KeyCache struct {
	Path string

	mu    sync.Mutex
	key   *Key
	mtime time.Time
	size  int64
	mode  fs.FileMode
	err   error
	seen  bool
}

// Get restituisce la chiave attuale (ErrNoKey se manca).
func (c *KeyCache) Get() (*Key, error) {
	if c == nil || c.Path == "" {
		return nil, ErrNoKey
	}
	st, err := os.Stat(c.Path)
	c.mu.Lock()
	defer c.mu.Unlock()
	if errors.Is(err, fs.ErrNotExist) {
		c.key, c.err, c.seen = nil, ErrNoKey, false
		return nil, ErrNoKey
	}
	if err != nil {
		return nil, err
	}
	if c.seen && st.ModTime().Equal(c.mtime) && st.Size() == c.size && st.Mode() == c.mode {
		return c.key, c.err
	}
	c.key, c.err = LoadKey(c.Path)
	c.mtime, c.size, c.mode, c.seen = st.ModTime(), st.Size(), st.Mode(), true
	return c.key, c.err
}
