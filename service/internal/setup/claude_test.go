package setup

import (
	"bytes"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/config"
)

// Tutto in cartelle temporanee: HOME, cartella di Claude e config di Calamaio. Mai i file veri.
type env struct {
	t        *testing.T
	settings string
	out      bytes.Buffer
	clock    time.Time
}

func newEnv(t *testing.T) *env {
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("USERPROFILE", home)
	t.Setenv("CLAUDE_CONFIG_DIR", "")
	t.Setenv("CALAMAIO_CONFIG_DIR", filepath.Join(home, "cfg"))
	p, err := SettingsPath()
	if err != nil {
		t.Fatal(err)
	}
	if p != filepath.Join(home, ".claude", "settings.json") {
		t.Fatalf("settings.json fuori dalla HOME di prova: %s", p)
	}
	return &env{t: t, settings: p, clock: time.Date(2026, 10, 11, 15, 30, 0, 0, time.Local)}
}

func (e *env) opts(answer string) Options {
	e.clock = e.clock.Add(time.Second)
	c := e.clock
	return Options{In: strings.NewReader(answer + "\n"), Out: &e.out, Now: func() time.Time { return c },
		LookPath: func(string) (string, error) { return "/x/calamaio", nil }}
}

func (e *env) write(s string) {
	e.t.Helper()
	if err := os.MkdirAll(filepath.Dir(e.settings), 0o700); err != nil {
		e.t.Fatal(err)
	}
	if err := os.WriteFile(e.settings, []byte(s), 0o600); err != nil {
		e.t.Fatal(err)
	}
}

func (e *env) read() string {
	e.t.Helper()
	b, err := os.ReadFile(e.settings)
	if err != nil {
		e.t.Fatal(err)
	}
	return string(b)
}

func backups(t *testing.T, settings string) []string {
	m, _ := filepath.Glob(settings + ".calamaio-*.bak")
	return m
}

const original = `{
  "model": "opus",
  "statusLine": {"type": "command", "command": "~/.claude/mia-riga.sh", "padding": 1},
  "hooks": {"Stop": []},
  "zeta": true
}`

func TestInstallAndUndo(t *testing.T) {
	e := newEnv(t)
	e.write(original)

	// senza conferma non cambia niente
	if err := Install(e.opts("")); !errors.Is(err, ErrAborted) {
		t.Fatalf("senza sì: %v", err)
	}
	if err := Install(e.opts("n")); !errors.Is(err, ErrAborted) {
		t.Fatalf("con no: %v", err)
	}
	if e.read() != original || len(backups(t, e.settings)) != 0 {
		t.Fatal("file toccato senza conferma")
	}

	if err := Install(e.opts("y")); err != nil {
		t.Fatal(err)
	}
	got := e.read()
	var m map[string]any
	if err := json.Unmarshal([]byte(got), &m); err != nil {
		t.Fatal(err)
	}
	sl := m["statusLine"].(map[string]any)
	if sl["type"] != "command" || sl["command"] != "calamaio statusline" {
		t.Fatalf("statusLine: %v", sl)
	}
	// le altre impostazioni restano, nello stesso ordine
	if m["model"] != "opus" || m["zeta"] != true || m["hooks"] == nil ||
		!(strings.Index(got, `"model"`) < strings.Index(got, `"statusLine"`) && strings.Index(got, `"statusLine"`) < strings.Index(got, `"zeta"`)) {
		t.Fatalf("altre chiavi perse o riordinate:\n%s", got)
	}
	bk := backups(t, e.settings)
	if len(bk) != 1 {
		t.Fatalf("backup: %v", bk)
	}
	if b, _ := os.ReadFile(bk[0]); string(b) != original {
		t.Fatal("il backup non è il file originale")
	}
	cfg, _ := config.Load()
	if cfg.PreviousStatusLine == nil || cfg.PreviousStatusLine.Command != "~/.claude/mia-riga.sh" || cfg.ClaudeSetup == nil {
		t.Fatalf("precedente non salvata in config: %+v", cfg)
	}

	// già impostata: non chiede e non sovrascrive la precedente
	if err := Install(e.opts("y")); err != nil {
		t.Fatal(err)
	}
	cfg2, _ := config.Load()
	if cfg2.PreviousStatusLine == nil || cfg2.PreviousStatusLine.Command != "~/.claude/mia-riga.sh" {
		t.Fatal("la precedente è stata sovrascritta con quella di Calamaio")
	}
	if len(backups(t, e.settings)) != 1 {
		t.Fatal("backup inutile quando non cambia niente")
	}

	// undo: torna l'oggetto di prima, identico (padding compreso)
	if err := Undo(e.opts("y")); err != nil {
		t.Fatal(err)
	}
	var back map[string]any
	_ = json.Unmarshal([]byte(e.read()), &back)
	var orig map[string]any
	_ = json.Unmarshal([]byte(original), &orig)
	ob, _ := json.Marshal(orig)
	bb, _ := json.Marshal(back)
	if string(ob) != string(bb) {
		t.Fatalf("undo non ha ripristinato:\n%s\n%s", ob, bb)
	}
	cfg3, _ := config.Load()
	if cfg3.PreviousStatusLine != nil || cfg3.ClaudeSetup != nil {
		t.Fatal("config non ripulita dopo undo")
	}
	if len(backups(t, e.settings)) != 2 {
		t.Fatal("undo senza backup")
	}
	// undo di nuovo: la status line non è più di Calamaio, non la tocca
	before := e.read()
	if err := Undo(e.opts("y")); err == nil || e.read() != before {
		t.Fatal("undo ha toccato una status line non di Calamaio")
	}
}

func TestInstallWithoutSettingsFile(t *testing.T) {
	e := newEnv(t)
	if err := Install(e.opts("sì")); err != nil {
		t.Fatal(err)
	}
	var m map[string]any
	if err := json.Unmarshal([]byte(e.read()), &m); err != nil || len(m) != 1 {
		t.Fatalf("settings creato: %v %v", m, err)
	}
	if st, _ := os.Stat(e.settings); st.Mode().Perm() != 0o600 {
		t.Fatalf("permessi %v", st.Mode().Perm())
	}
	if err := Undo(e.opts("y")); err != nil {
		t.Fatal(err)
	}
	if strings.TrimSpace(e.read()) != "{}" {
		t.Fatalf("undo senza precedente deve togliere statusLine: %s", e.read())
	}
}

func TestInvalidSettingsUntouched(t *testing.T) {
	e := newEnv(t)
	e.write(`{"model": "opus",`)
	if err := Install(e.opts("y")); err == nil {
		t.Fatal("JSON rotto accettato")
	}
	if e.read() != `{"model": "opus",` || len(backups(t, e.settings)) != 0 {
		t.Fatal("file rotto modificato")
	}
}

func TestClaudeConfigDir(t *testing.T) {
	d := t.TempDir()
	t.Setenv("CLAUDE_CONFIG_DIR", d)
	if p, _ := SettingsPath(); p != filepath.Join(d, "settings.json") {
		t.Fatalf("CLAUDE_CONFIG_DIR ignorata: %s", p)
	}
}
