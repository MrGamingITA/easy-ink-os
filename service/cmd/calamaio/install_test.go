package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/autostart"
)

type recRunner struct{ calls []string }

func (r *recRunner) Run(name string, args ...string) (string, error) {
	r.calls = append(r.calls, strings.Join(append([]string{name}, args...), " "))
	if name == "launchctl" && len(args) > 0 && args[0] == "print" {
		return "", os.ErrNotExist // non caricato
	}
	return "", nil
}

// La CLI in una HOME temporanea: --status, conferma, --yes, opzioni di serve dopo --, avviso con un
// servizio avviato a mano. Nessun comando parte davvero (recRunner).
func TestInstallCLI(t *testing.T) {
	home := t.TempDir()
	exe := filepath.Join(home, "bin", "calamaio")
	_ = os.MkdirAll(filepath.Dir(exe), 0o755)
	_ = os.WriteFile(exe, nil, 0o755)
	env := autostart.Env{GOOS: "darwin", Home: home, UID: 501, Exe: exe, LogDir: filepath.Join(home, "dati", "logs"),
		BackupDir: filepath.Join(home, "dati", "autostart"), Now: time.Now}
	run := &recRunner{}
	var out, errb bytes.Buffer
	deps := func(in string, running bool) installDeps {
		out.Reset()
		errb.Reset()
		return installDeps{Env: func() (autostart.Env, error) { return env, nil }, Runner: run, In: strings.NewReader(in),
			Out: &out, Err: &errb, Cwd: func() (string, error) { return "/repo/service", nil },
			Running: func() (bool, bool) { return running, false }}
	}
	if code := installCmd([]string{"--status"}, deps("", false)); code != 1 || !strings.Contains(out.String(), "non installato") {
		t.Fatalf("--status prima: %d %s", code, out.String())
	}
	if code := installCmd(nil, deps("n\n", true)); code != 1 || !strings.Contains(out.String(), "avviato a mano") {
		t.Fatalf("risposta n: %d %s", code, out.String())
	}
	if _, err := os.Stat(env.File()); err == nil {
		t.Fatal("file scritto senza conferma")
	}
	if code := installCmd([]string{"--yes", "--", "--firmware", "../firmware/.pio/build/heltec213"}, deps("", false)); code != 0 {
		t.Fatalf("--yes: %d %s %s", code, out.String(), errb.String())
	}
	b, _ := os.ReadFile(env.File())
	if !strings.Contains(string(b), "<string>/repo/firmware/.pio/build/heltec213</string>") {
		t.Fatalf("percorso di --firmware non reso assoluto:\n%s", b)
	}
	if code := installCmd([]string{"--status"}, deps("", false)); code != 0 || !strings.Contains(out.String(), "installato (LaunchAgent") || !strings.Contains(out.String(), "NON caricato") {
		t.Fatalf("--status dopo: %d %s", code, out.String())
	}
	if code := uninstallCmd([]string{"--yes"}, deps("", false)); code != 0 {
		t.Fatalf("uninstall: %d %s %s", code, out.String(), errb.String())
	}
	if _, err := os.Stat(env.File()); err == nil {
		t.Fatal("uninstall non ha tolto il file")
	}
	if code := uninstallCmd([]string{"boh"}, deps("", false)); code != 2 {
		t.Fatalf("argomento in più: %d", code)
	}
	for _, c := range run.calls {
		if !strings.HasPrefix(c, "launchctl ") && !strings.HasPrefix(c, "plutil ") {
			t.Fatalf("comando inatteso: %s", c)
		}
	}
}
