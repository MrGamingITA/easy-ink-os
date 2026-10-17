// Package config legge e scrive la configurazione del servizio Calamaio.
package config

import (
	"encoding/json"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
)

const (
	DefaultAPIPort = 8765
	DefaultWSPort  = 8766
	DefaultAppPort = 8767 // HTTPS per i telefoni abbinati (app iPhone)
)

// StatusLine è un comando di status line di Claude Code (come in settings.json).
type StatusLine struct {
	Type            string `json:"type,omitempty"`
	Command         string `json:"command"`
	Padding         *int   `json:"padding,omitempty"`
	RefreshInterval *int   `json:"refreshInterval,omitempty"`
}

type Config struct {
	APIPort int `json:"api_port"`
	WSPort  int `json:"ws_port"`
	// AppPort: porta HTTPS dei telefoni abbinati, su tutte le interfacce, accesa solo quando serve.
	AppPort int `json:"app_port"`
	// TZ sostituisce la stringa POSIX del fuso mandata ai display (vuota = calcolata dal fuso locale).
	TZ string `json:"tz,omitempty"`
	// FirmwareDir è la cartella del pacchetto firmware per la webapp (vuota = <DataDir>/firmware).
	// In sviluppo: firmware/.pio/build/heltec213 del repository.
	FirmwareDir string `json:"firmware_dir,omitempty"`
	// Status line che c'era prima di `calamaio setup claude`: viene eseguita a valle.
	PreviousStatusLine *StatusLine `json:"previous_statusline,omitempty"`
	// ClaudeSetup: cosa ha fatto `calamaio setup claude` (per --undo).
	ClaudeSetup *ClaudeSetup `json:"claude_setup,omitempty"`
	// FactoryBackups: originali di fabbrica da importare all'avvio, protetti (vuoto = default di
	// sviluppo: backup-fabbrica/ del repository, se c'è).
	FactoryBackups []FactoryBackup `json:"factory_backups,omitempty"`
	// TestRegex riconosce i comandi Bash di test e build (vuota = sessions.DefaultTestRegex).
	TestRegex string `json:"test_regex,omitempty"`
	// Paired: display abbinati e loro chiave K (docs/M5-PERMESSI.md §5). Il file è 0600. Lo scrive
	// il servizio (KeyStore); qui c'è perché Save (setup claude) non lo perda.
	Paired []Paired `json:"paired,omitempty"`
	// Avatars: avatar predefinito e assegnazioni per progetto e per sessione (docs/AVATAR.md §6).
	// Lo scrive il servizio (AvatarStore); qui c'è perché Save (setup claude) non lo perda.
	Avatars *AvatarAssignments `json:"avatars,omitempty"`
}

// Paired è un display abbinato: K di 32 byte in esadecimale, data dell'abbinamento in RFC 3339.
type Paired struct {
	Board   string `json:"board"`
	Key     string `json:"key"`
	Created string `json:"created"`
}

// FactoryBackup: un file con la flash completa di fabbrica di una scheda. sha256 vuoto = si cerca
// SHA256.txt (o <file>.sha256) accanto; senza nessuno dei due non si importa.
type FactoryBackup struct {
	File      string `json:"file"`
	MAC       string `json:"mac"`
	Chip      string `json:"chip,omitempty"`
	Board     string `json:"board,omitempty"`
	Name      string `json:"name,omitempty"`
	FlashSize int64  `json:"flashSize,omitempty"`
	SHA256    string `json:"sha256,omitempty"`
	Note      string `json:"note,omitempty"`
}

// ClaudeSetup ricorda l'impostazione della status line in settings.json di Claude Code.
type ClaudeSetup struct {
	At       string `json:"at"`       // RFC 3339
	Settings string `json:"settings"` // percorso di settings.json
	Backup   string `json:"backup,omitempty"`
	// Previous è l'oggetto statusLine di prima, identico (null se non c'era).
	Previous json.RawMessage `json:"previous"`
}

func Default() Config {
	return Config{APIPort: DefaultAPIPort, WSPort: DefaultWSPort, AppPort: DefaultAppPort}
}

// Dir restituisce la cartella di configurazione; CALAMAIO_CONFIG_DIR la sostituisce (test).
func Dir() (string, error) {
	if d := os.Getenv("CALAMAIO_CONFIG_DIR"); d != "" {
		return d, nil
	}
	base, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(base, "calamaio"), nil
}

// DataDir restituisce la cartella dei dati dell'utente; CALAMAIO_DATA_DIR la sostituisce.
// macOS: ~/Library/Application Support/calamaio · Windows: %LocalAppData%\calamaio ·
// Linux: $XDG_DATA_HOME/calamaio o ~/.local/share/calamaio.
func DataDir() (string, error) {
	if d := os.Getenv("CALAMAIO_DATA_DIR"); d != "" {
		return d, nil
	}
	switch runtime.GOOS {
	case "windows":
		if d := os.Getenv("LocalAppData"); d != "" {
			return filepath.Join(d, "calamaio"), nil
		}
		return Dir()
	case "darwin", "ios":
		return Dir() // ~/Library/Application Support/calamaio
	}
	if d := os.Getenv("XDG_DATA_HOME"); d != "" && filepath.IsAbs(d) {
		return filepath.Join(d, "calamaio"), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".local", "share", "calamaio"), nil
}

// AvatarDir: libreria degli avatar dell'utente, <dati utente>/calamaio/avatars
// (macOS ~/Library/Application Support/calamaio/avatars).
func AvatarDir() (string, error) {
	d, err := DataDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(d, "avatars"), nil
}

// FirmwarePath restituisce la cartella del pacchetto firmware (config o default).
func (c Config) FirmwarePath() string {
	if c.FirmwareDir != "" {
		return c.FirmwareDir
	}
	d, err := DataDir()
	if err != nil {
		return ""
	}
	return filepath.Join(d, "firmware")
}

func Path() (string, error) {
	d, err := Dir()
	if err != nil {
		return "", err
	}
	return filepath.Join(d, "config.json"), nil
}

// Load non fallisce mai per un file assente: restituisce i valori di default.
func Load() (Config, error) {
	c := Default()
	p, err := Path()
	if err != nil {
		return c, err
	}
	b, err := os.ReadFile(p)
	if errors.Is(err, fs.ErrNotExist) {
		return c.withEnv(), nil
	}
	if err != nil {
		return c, err
	}
	if err := json.Unmarshal(b, &c); err != nil {
		return Default().withEnv(), err
	}
	if c.APIPort == 0 {
		c.APIPort = DefaultAPIPort
	}
	if c.WSPort == 0 {
		c.WSPort = DefaultWSPort
	}
	if c.AppPort == 0 {
		c.AppPort = DefaultAppPort
	}
	return c.withEnv(), nil
}

// CALAMAIO_API_PORT, CALAMAIO_WS_PORT e CALAMAIO_APP_PORT sostituiscono le porte (test, più istanze).
func (c Config) withEnv() Config {
	if v := os.Getenv("CALAMAIO_API_PORT"); v != "" {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			c.APIPort = n
		}
	}
	if v := os.Getenv("CALAMAIO_WS_PORT"); v != "" {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			c.WSPort = n
		}
	}
	if v := os.Getenv("CALAMAIO_APP_PORT"); v != "" {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			c.AppPort = n
		}
	}
	return c
}

func Save(c Config) error {
	fileMu.Lock()
	defer fileMu.Unlock()
	p, err := Path()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o700); err != nil {
		return err
	}
	b, err := json.MarshalIndent(c, "", "  ")
	if err != nil {
		return err
	}
	tmp := p + ".tmp"
	if err := os.WriteFile(tmp, append(b, '\n'), 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, p)
}
