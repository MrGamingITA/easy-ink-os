// Package setup: `calamaio setup claude` (brief 6.3). Un plugin non può impostare la status line,
// quindi la scrive questo comando in settings.json di Claude Code, dopo una conferma esplicita
// e un backup con data; `--undo` rimette quella di prima.
package setup

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"calamaio/internal/config"
)

// Command è la status line che si installa.
const Command = "calamaio statusline"

var ErrAborted = errors.New("annullato: nessun file modificato")

// Options: tutto ciò che tocca il sistema è sostituibile nei test.
type Options struct {
	Settings string // percorso di settings.json (vuoto = SettingsPath())
	In       io.Reader
	Out      io.Writer
	Now      func() time.Time
	Load     func() (config.Config, error)
	Save     func(config.Config) error
	LookPath func(string) (string, error)
}

func (o *Options) fill() error {
	if o.Settings == "" {
		p, err := SettingsPath()
		if err != nil {
			return err
		}
		o.Settings = p
	}
	if o.Now == nil {
		o.Now = time.Now
	}
	if o.Load == nil {
		o.Load = config.Load
	}
	if o.Save == nil {
		o.Save = config.Save
	}
	if o.LookPath == nil {
		o.LookPath = exec.LookPath
	}
	return nil
}

// SettingsPath: $CLAUDE_CONFIG_DIR/settings.json se impostata, altrimenti ~/.claude/settings.json.
func SettingsPath() (string, error) {
	if d := os.Getenv("CLAUDE_CONFIG_DIR"); d != "" {
		return filepath.Join(d, "settings.json"), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".claude", "settings.json"), nil
}

// settings: oggetto JSON di primo livello con l'ordine delle chiavi conservato.
type settings struct {
	keys []string
	vals map[string]json.RawMessage
	mode fs.FileMode
	had  bool // il file esisteva
}

func readSettings(path string) (*settings, error) {
	s := &settings{vals: map[string]json.RawMessage{}, mode: 0o600}
	b, err := os.ReadFile(path)
	if errors.Is(err, fs.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return nil, err
	}
	s.had = true
	if st, err := os.Stat(path); err == nil {
		s.mode = st.Mode().Perm()
	}
	if len(bytes.TrimSpace(b)) == 0 {
		return s, nil
	}
	dec := json.NewDecoder(bytes.NewReader(b))
	tok, err := dec.Token()
	if err != nil || tok != json.Delim('{') {
		return nil, fmt.Errorf("%s non è un oggetto JSON valido: non lo tocco", path)
	}
	for dec.More() {
		kt, err := dec.Token()
		if err != nil {
			return nil, fmt.Errorf("%s: %w", path, err)
		}
		k := kt.(string)
		var v json.RawMessage
		if err := dec.Decode(&v); err != nil {
			return nil, fmt.Errorf("%s: %w", path, err)
		}
		if _, dup := s.vals[k]; !dup {
			s.keys = append(s.keys, k)
		}
		s.vals[k] = v
	}
	if _, err := dec.Token(); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return s, nil
}

func (s *settings) set(k string, v json.RawMessage) {
	if _, ok := s.vals[k]; !ok {
		s.keys = append(s.keys, k)
	}
	s.vals[k] = v
}

func (s *settings) del(k string) {
	delete(s.vals, k)
	for i, x := range s.keys {
		if x == k {
			s.keys = append(s.keys[:i], s.keys[i+1:]...)
			return
		}
	}
}

func (s *settings) bytes() ([]byte, error) {
	var buf bytes.Buffer
	buf.WriteByte('{')
	for i, k := range s.keys {
		if i > 0 {
			buf.WriteByte(',')
		}
		kb, _ := json.Marshal(k)
		buf.Write(kb)
		buf.WriteByte(':')
		buf.Write(s.vals[k])
	}
	buf.WriteByte('}')
	var out bytes.Buffer
	if err := json.Indent(&out, buf.Bytes(), "", "  "); err != nil {
		return nil, err
	}
	out.WriteByte('\n')
	return out.Bytes(), nil
}

// write scrive in modo atomico (file temporaneo nella stessa cartella, poi rename).
func (s *settings) write(path string) error {
	b, err := s.bytes()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), ".settings-calamaio-*")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	if _, err := tmp.Write(b); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Chmod(s.mode); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	return os.Rename(tmp.Name(), path)
}

// backup copia settings.json accanto all'originale, con data e ora nel nome.
func backup(path string, now time.Time) (string, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	dst := path + ".calamaio-" + now.Format("20060102-150405") + ".bak"
	if _, err := os.Stat(dst); err == nil {
		dst = path + ".calamaio-" + now.Format("20060102-150405.000") + ".bak"
	}
	return dst, os.WriteFile(dst, b, 0o600)
}

func isCalamaio(raw json.RawMessage) bool {
	var sl config.StatusLine
	if json.Unmarshal(raw, &sl) != nil {
		return false
	}
	c := strings.Join(strings.Fields(sl.Command), " ")
	return c == Command || strings.HasSuffix(c, "/"+Command) || strings.HasSuffix(c, `\`+Command) ||
		strings.HasSuffix(c, "calamaio.exe statusline")
}

func describe(raw json.RawMessage) string {
	if raw == nil {
		return "nessuna"
	}
	var sl config.StatusLine
	if json.Unmarshal(raw, &sl) == nil && sl.Command != "" {
		return fmt.Sprintf("%q", sl.Command)
	}
	return string(raw)
}

func confirm(o Options, question string) bool {
	fmt.Fprintf(o.Out, "%s [y/N] ", question)
	line, _ := bufio.NewReader(o.In).ReadString('\n')
	switch strings.ToLower(strings.TrimSpace(line)) {
	case "y", "yes", "s", "si", "sì":
		return true
	}
	return false
}

// Install imposta la status line di Calamaio.
func Install(o Options) error {
	if err := o.fill(); err != nil {
		return err
	}
	st, err := readSettings(o.Settings)
	if err != nil {
		return err
	}
	cur, has := st.vals["statusLine"]
	if has && isCalamaio(cur) {
		fmt.Fprintf(o.Out, "La status line di %s è già %q: niente da fare.\n", o.Settings, Command)
		return nil
	}
	cfg, err := o.Load()
	if err != nil {
		return fmt.Errorf("config di Calamaio: %w", err)
	}
	fmt.Fprintf(o.Out, "Calamaio imposta la status line di Claude Code.\n\n")
	fmt.Fprintf(o.Out, "  File:          %s\n", o.Settings)
	if st.had {
		fmt.Fprintf(o.Out, "  Backup:        sì, accanto al file, con data e ora nel nome\n")
	} else {
		fmt.Fprintf(o.Out, "  Backup:        no, il file non esiste ancora (lo creo)\n")
	}
	fmt.Fprintf(o.Out, "  Adesso:        %s\n", describe(cur))
	fmt.Fprintf(o.Out, "  Dopo:          %q\n", Command)
	if has {
		fmt.Fprintf(o.Out, "  La precedente continua a funzionare: Calamaio la esegue e ne stampa l'output.\n")
	}
	fmt.Fprintf(o.Out, "  Per tornare indietro: calamaio setup claude --undo\n\n")
	if !confirm(o, "Procedo?") {
		fmt.Fprintln(o.Out, ErrAborted.Error())
		return ErrAborted
	}
	now := o.Now()
	rec := &config.ClaudeSetup{At: now.Format(time.RFC3339), Settings: o.Settings, Previous: json.RawMessage("null")}
	if st.had {
		if rec.Backup, err = backup(o.Settings, now); err != nil {
			return fmt.Errorf("backup: %w", err)
		}
	}
	if has {
		rec.Previous = cur
		var prev config.StatusLine
		if json.Unmarshal(cur, &prev) == nil && strings.TrimSpace(prev.Command) != "" {
			cfg.PreviousStatusLine = &prev
		} else {
			cfg.PreviousStatusLine = nil
		}
	} else {
		cfg.PreviousStatusLine = nil
	}
	cfg.ClaudeSetup = rec
	if err := o.Save(cfg); err != nil {
		return fmt.Errorf("config di Calamaio: %w", err)
	}
	st.set("statusLine", json.RawMessage(`{"type":"command","command":"`+Command+`"}`))
	if err := st.write(o.Settings); err != nil {
		return err
	}
	fmt.Fprintf(o.Out, "Fatto. ")
	if rec.Backup != "" {
		fmt.Fprintf(o.Out, "Backup: %s\n", rec.Backup)
	} else {
		fmt.Fprintln(o.Out)
	}
	if _, err := o.LookPath("calamaio"); err != nil {
		fmt.Fprintln(o.Out, "Attenzione: `calamaio` non è nel PATH. Claude Code non troverà la status line finché non ci sarà (per esempio ~/.local/bin).")
	}
	fmt.Fprintln(o.Out, "La status line nuova compare al prossimo aggiornamento di Claude Code (o riaprendo la sessione).")
	return nil
}

// Undo rimette la status line di prima (o la toglie, se non c'era).
func Undo(o Options) error {
	if err := o.fill(); err != nil {
		return err
	}
	cfg, err := o.Load()
	if err != nil {
		return fmt.Errorf("config di Calamaio: %w", err)
	}
	if cfg.ClaudeSetup != nil && cfg.ClaudeSetup.Settings != "" && o.Settings != cfg.ClaudeSetup.Settings {
		fmt.Fprintf(o.Out, "Nota: setup era stato fatto su %s; ora uso %s.\n", cfg.ClaudeSetup.Settings, o.Settings)
	}
	st, err := readSettings(o.Settings)
	if err != nil {
		return err
	}
	cur, has := st.vals["statusLine"]
	if !has || !isCalamaio(cur) {
		return fmt.Errorf("la status line in %s non è quella di Calamaio (%s): non la tocco", o.Settings, describe(cur))
	}
	var prev json.RawMessage
	switch {
	case cfg.ClaudeSetup != nil && len(cfg.ClaudeSetup.Previous) > 0 && string(cfg.ClaudeSetup.Previous) != "null":
		prev = cfg.ClaudeSetup.Previous
	case cfg.PreviousStatusLine != nil:
		prev, _ = json.Marshal(cfg.PreviousStatusLine)
	}
	fmt.Fprintf(o.Out, "Tolgo la status line di Calamaio da %s.\n", o.Settings)
	if prev != nil {
		fmt.Fprintf(o.Out, "  Torna:  %s\n", describe(prev))
	} else {
		fmt.Fprintf(o.Out, "  Prima non c'era una status line: tolgo la voce statusLine.\n")
	}
	fmt.Fprintf(o.Out, "  Backup: sì, accanto al file, con data e ora nel nome\n\n")
	if !confirm(o, "Procedo?") {
		fmt.Fprintln(o.Out, ErrAborted.Error())
		return ErrAborted
	}
	bk, err := backup(o.Settings, o.Now())
	if err != nil {
		return fmt.Errorf("backup: %w", err)
	}
	if prev != nil {
		st.set("statusLine", prev)
	} else {
		st.del("statusLine")
	}
	if err := st.write(o.Settings); err != nil {
		return err
	}
	cfg.PreviousStatusLine = nil
	cfg.ClaudeSetup = nil
	if err := o.Save(cfg); err != nil {
		return fmt.Errorf("config di Calamaio: %w", err)
	}
	fmt.Fprintf(o.Out, "Fatto. Backup: %s\n", bk)
	return nil
}
