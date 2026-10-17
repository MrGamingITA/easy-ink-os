package autostart

import (
	"bytes"
	"encoding/xml"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

// fakeRunner registra i comandi invece di eseguirli; fail dice quali falliscono (per prefisso).
type fakeRunner struct {
	calls [][]string
	fail  map[string]int // comando unito da spazi (prefisso) → quante volte fallisce ancora
}

func (f *fakeRunner) Run(name string, args ...string) (string, error) {
	c := append([]string{name}, args...)
	f.calls = append(f.calls, c)
	line := strings.Join(c, " ")
	for p, n := range f.fail {
		if strings.HasPrefix(line, p) && n != 0 {
			if n > 0 {
				f.fail[p] = n - 1
			}
			return "errore finto", errors.New("exit status 1")
		}
	}
	return "", nil
}

func (f *fakeRunner) lines() []string {
	var out []string
	for _, c := range f.calls {
		out = append(out, strings.Join(c, " "))
	}
	return out
}

// testEnv: HOME temporanea, binario finto (esiste davvero), niente attese.
func testEnv(t *testing.T, goos string) Env {
	t.Helper()
	home := t.TempDir()
	exe := filepath.Join(home, ".local", "bin", "calamaio")
	if err := os.MkdirAll(filepath.Dir(exe), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(exe, []byte("#!/bin/sh\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	data := filepath.Join(home, "Library", "Application Support", "calamaio")
	if goos == "linux" {
		data = filepath.Join(home, ".local", "share", "calamaio")
	}
	return Env{GOOS: goos, Home: home, UID: 501, Exe: exe, LogDir: filepath.Join(data, "logs"),
		BackupDir: filepath.Join(data, "autostart"), Getenv: func(string) string { return "" },
		Now: func() time.Time { return time.Date(2026, 10, 11, 19, 30, 0, 0, time.UTC) }, Sleep: func(time.Duration) {}}
}

func dialog(in string) (IO, *bytes.Buffer) {
	var out bytes.Buffer
	return IO{In: strings.NewReader(in), Out: &out}, &out
}

// plistDoc legge il plist generato come XML generico (è XML valido?) e restituisce chiavi e valori
// di primo livello in ordine.
func plistKeys(t *testing.T, b []byte) []string {
	t.Helper()
	dec := xml.NewDecoder(bytes.NewReader(b))
	dec.Strict = true
	var keys []string
	depth := 0
	inKey := false
	for {
		tok, err := dec.Token()
		if err != nil {
			if err.Error() == "EOF" {
				break
			}
			t.Fatalf("plist non è XML valido: %v", err)
		}
		switch v := tok.(type) {
		case xml.StartElement:
			depth++
			inKey = v.Name.Local == "key" && depth == 3 // plist > dict > key
		case xml.EndElement:
			depth--
			inKey = false
		case xml.CharData:
			if inKey {
				keys = append(keys, string(v))
			}
		}
	}
	return keys
}

func TestLaunchdPlan(t *testing.T) {
	e := testEnv(t, "darwin")
	p, err := e.Plan([]string{"--firmware", "firmware/.pio/build/heltec213", "--no-mdns"}, "/Users/u/CalamaiOS")
	if err != nil {
		t.Fatal(err)
	}
	if p.File != filepath.Join(e.Home, "Library", "LaunchAgents", "com.calamaio.service.plist") {
		t.Fatalf("file %s", p.File)
	}
	want := []string{e.Exe, "serve", "--firmware", "/Users/u/CalamaiOS/firmware/.pio/build/heltec213", "--no-mdns"}
	if strings.Join(p.Args, "|") != strings.Join(want, "|") {
		t.Fatalf("argomenti %q", p.Args)
	}
	c := string(p.Content)
	keys := plistKeys(t, p.Content)
	for _, k := range []string{"Label", "ProgramArguments", "RunAtLoad", "KeepAlive", "ThrottleInterval", "ExitTimeOut",
		"ProcessType", "LimitLoadToSessionType", "WorkingDirectory", "EnvironmentVariables", "StandardOutPath", "StandardErrorPath"} {
		found := false
		for _, x := range keys {
			found = found || x == k
		}
		if !found {
			t.Errorf("manca la chiave %s: %v", k, keys)
		}
	}
	for _, frag := range []string{
		"<string>com.calamaio.service</string>",
		"<key>SuccessfulExit</key>\n\t\t<false/>",
		"<key>RunAtLoad</key>\n\t<true/>",
		"<string>" + xmlText(e.Exe) + "</string>\n\t\t<string>serve</string>",
		"<key>CALAMAIO_AUTOSTART</key>\n\t\t<string>launchd</string>",
		"<string>" + xmlText(filepath.Join(e.LogDir, "calamaio.log")) + "</string>",
		"<integer>30</integer>", "<integer>15</integer>",
	} {
		if !strings.Contains(c, frag) {
			t.Errorf("nel plist manca %q\n%s", frag, c)
		}
	}
	if got := programOf("launchd", p.Content); got != e.Exe {
		t.Fatalf("programOf %q", got)
	}
	// caratteri speciali nei percorsi: XML scappato e riletto uguale
	e2 := e
	e2.Exe = "/Users/a&b <c>/bin/calamaio"
	p2, _ := e2.Plan(nil, "/")
	plistKeys(t, p2.Content)
	if got := programOf("launchd", p2.Content); got != e2.Exe {
		t.Fatalf("programOf con & e <: %q", got)
	}
	// plutil, se c'è (macOS), conferma che è un plist valido. Solo lettura di un file temporaneo.
	if pl, err := exec.LookPath("plutil"); err == nil && runtime.GOOS == "darwin" {
		f := filepath.Join(t.TempDir(), "x.plist")
		_ = os.WriteFile(f, p.Content, 0o644)
		if out, err := exec.Command(pl, "-lint", f).CombinedOutput(); err != nil {
			t.Fatalf("plutil -lint: %v %s", err, out)
		}
	}
}

func TestLaunchdInstall(t *testing.T) {
	e := testEnv(t, "darwin")
	// non caricato: launchctl print fallisce
	r := &fakeRunner{fail: map[string]int{"launchctl print": -1}}
	o, out := dialog("n\n")
	if err := e.Install(r, o, nil, "/"); !errors.Is(err, ErrAborted) {
		t.Fatalf("risposta n: %v", err)
	}
	if _, err := os.Stat(e.File()); err == nil {
		t.Fatal("con la risposta n il file non va scritto")
	}
	if len(r.calls) != 1 || r.calls[0][0] != "launchctl" || r.calls[0][1] != "print" {
		t.Fatalf("con la risposta n si esegue solo la lettura: %v", r.lines())
	}
	for _, frag := range []string{"LaunchAgent", e.File(), "launchctl bootstrap gui/501", "[y/N]", "<key>RunAtLoad</key>"} {
		if !strings.Contains(out.String(), frag) {
			t.Errorf("l'anteprima non dice %q:\n%s", frag, out)
		}
	}

	r = &fakeRunner{fail: map[string]int{"launchctl print": -1}}
	o, out = dialog("y\n")
	if err := e.Install(r, o, nil, "/"); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	want := []string{
		"launchctl print gui/501/com.calamaio.service",
		"plutil -lint " + e.File(),
		"launchctl bootstrap gui/501 " + e.File(),
	}
	if strings.Join(r.lines(), "\n") != strings.Join(want, "\n") {
		t.Fatalf("comandi:\n%s\nattesi:\n%s", strings.Join(r.lines(), "\n"), strings.Join(want, "\n"))
	}
	st, err := os.Stat(e.File())
	if err != nil || st.Mode().Perm() != 0o644 {
		t.Fatalf("file: %v %v", st, err)
	}
	if st, err := os.Stat(e.LogDir); err != nil || !st.IsDir() || st.Mode().Perm() != 0o700 {
		t.Fatalf("cartella dei log: %v %v", st, err)
	}

	// già installato, uguale e caricato: niente domanda, niente comandi
	r = &fakeRunner{}
	o, out = dialog("")
	if err := e.Install(r, o, nil, "/"); err != nil || !strings.Contains(out.String(), "già installato") {
		t.Fatalf("%v %s", err, out)
	}
	if len(r.calls) != 1 {
		t.Fatalf("comandi: %v", r.lines())
	}

	// file diverso (altre opzioni) e caricato: copia del vecchio, bootout, bootstrap riprovato
	old, _ := os.ReadFile(e.File())
	r = &fakeRunner{fail: map[string]int{"launchctl bootstrap": 2}}
	o, out = dialog("")
	o.Yes = true
	if err := e.Install(r, o, []string{"--no-mdns"}, "/"); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	if strings.Contains(out.String(), "[y/N]") {
		t.Fatal("con --yes non si chiede")
	}
	lines := strings.Join(r.lines(), "\n")
	if !strings.Contains(lines, "launchctl bootout gui/501/com.calamaio.service") || strings.Count(lines, "launchctl bootstrap") != 3 {
		t.Fatalf("comandi:\n%s", lines)
	}
	bk := filepath.Join(e.BackupDir, "com.calamaio.service.plist.20261011-193000")
	if b, err := os.ReadFile(bk); err != nil || !bytes.Equal(b, old) {
		t.Fatalf("copia del vecchio: %v", err)
	}
	if b, _ := os.ReadFile(e.File()); !strings.Contains(string(b), "<string>--no-mdns</string>") {
		t.Fatal("file nuovo non scritto")
	}

	// bootstrap che fallisce sempre: errore con l'output
	r = &fakeRunner{fail: map[string]int{"launchctl print": -1, "launchctl bootstrap": -1}}
	_ = os.Remove(e.File())
	o, _ = dialog("")
	o.Yes = true
	if err := e.Install(r, o, nil, "/"); err == nil || !strings.Contains(err.Error(), "errore finto") || !strings.Contains(err.Error(), "launchctl enable") {
		t.Fatalf("errore atteso: %v", err)
	}
}

func TestLaunchdStatusAndUninstall(t *testing.T) {
	e := testEnv(t, "darwin")
	st := e.Stat(nil)
	if !st.Supported || st.Installed || st.Kind != "launchd" || st.Loaded != nil || !strings.Contains(st.Hint, "calamaio install") {
		t.Fatalf("prima: %+v", st)
	}
	o, _ := dialog("")
	o.Yes = true
	if err := e.Uninstall(&fakeRunner{fail: map[string]int{"launchctl print": -1}}, o); err != nil {
		t.Fatal(err)
	}
	if err := e.Install(&fakeRunner{fail: map[string]int{"launchctl print": -1}}, o, nil, "/"); err != nil {
		t.Fatal(err)
	}
	r := &fakeRunner{}
	st = e.Stat(r)
	if !st.Installed || st.Exe != e.Exe || st.ExeMissing || st.Loaded == nil || !*st.Loaded || st.Hint != "" || st.Managed {
		t.Fatalf("installato: %+v", st)
	}
	e.Getenv = func(k string) string {
		if k == EnvMarker {
			return "launchd"
		}
		return ""
	}
	if !e.Stat(nil).Managed {
		t.Fatal("CALAMAIO_AUTOSTART non riconosciuto")
	}
	_ = os.Remove(e.Exe)
	if st := e.Stat(nil); !st.ExeMissing || !strings.Contains(st.Hint, "binario") {
		t.Fatalf("binario sparito: %+v", st)
	}

	// uninstall: risposta n non tocca niente
	r = &fakeRunner{}
	o2, _ := dialog("\n")
	if err := e.Uninstall(r, o2); !errors.Is(err, ErrAborted) {
		t.Fatal(err)
	}
	if _, err := os.Stat(e.File()); err != nil {
		t.Fatal("con la risposta vuota il file deve restare")
	}
	r = &fakeRunner{}
	o3, out := dialog("sì\n")
	if err := e.Uninstall(r, o3); err != nil {
		t.Fatalf("%v %s", err, out)
	}
	if strings.Join(r.lines(), "\n") != "launchctl print gui/501/com.calamaio.service\nlaunchctl bootout gui/501/com.calamaio.service" {
		t.Fatalf("comandi: %v", r.lines())
	}
	if _, err := os.Stat(e.File()); err == nil {
		t.Fatal("file non tolto")
	}
	if m, _ := filepath.Glob(filepath.Join(e.BackupDir, "com.calamaio.service.plist.*")); len(m) != 1 {
		t.Fatalf("copie: %v", m)
	}
}

func TestSystemdInstall(t *testing.T) {
	e := testEnv(t, "linux")
	e.Exe = filepath.Join(e.Home, "my bin", "cal%amaio")
	_ = os.MkdirAll(filepath.Dir(e.Exe), 0o755)
	_ = os.WriteFile(e.Exe, nil, 0o755)
	p, err := e.Plan([]string{"--firmware=fw dir"}, "/home/u")
	if err != nil {
		t.Fatal(err)
	}
	if p.File != filepath.Join(e.Home, ".config", "systemd", "user", "calamaio.service") {
		t.Fatalf("file %s", p.File)
	}
	c := string(p.Content)
	for _, frag := range []string{
		`ExecStart="` + filepath.Join(e.Home, "my bin", "cal%%amaio") + `" "serve" "--firmware=/home/u/fw dir"`,
		"Restart=on-failure", "RestartSec=30", "TimeoutStopSec=15",
		`Environment="CALAMAIO_AUTOSTART=systemd"`,
		"StandardOutput=append:" + filepath.Join(e.LogDir, "calamaio.log"),
		"WantedBy=default.target",
	} {
		if !strings.Contains(c, frag) {
			t.Errorf("nell'unità manca %q\n%s", frag, c)
		}
	}
	if got := programOf("systemd", p.Content); got != e.Exe {
		t.Fatalf("programOf %q", got)
	}
	// XDG_CONFIG_HOME
	e2 := e
	e2.ConfigHome = filepath.Join(e.Home, "cfg")
	if e2.File() != filepath.Join(e.Home, "cfg", "systemd", "user", "calamaio.service") {
		t.Fatal(e2.File())
	}

	r := &fakeRunner{fail: map[string]int{"systemctl --user is-enabled": -1, "systemctl --user is-active": -1}}
	o, out := dialog("y\n")
	if err := e.Install(r, o, nil, "/"); err != nil {
		t.Fatalf("%v %s", err, out)
	}
	want := "systemctl --user is-enabled --quiet calamaio.service\nsystemctl --user is-active --quiet calamaio.service\n" +
		"systemctl --user daemon-reload\nsystemctl --user enable --now calamaio.service"
	if strings.Join(r.lines(), "\n") != want {
		t.Fatalf("comandi:\n%s", strings.Join(r.lines(), "\n"))
	}
	// reinstallazione con opzioni nuove mentre gira: restart
	r = &fakeRunner{}
	o, _ = dialog("y\n")
	if err := e.Install(r, o, []string{"--no-mdns"}, "/"); err != nil {
		t.Fatal(err)
	}
	if l := r.lines(); l[len(l)-1] != "systemctl --user restart calamaio.service" {
		t.Fatalf("comandi: %v", l)
	}
	r = &fakeRunner{}
	o, _ = dialog("y\n")
	if err := e.Uninstall(r, o); err != nil {
		t.Fatal(err)
	}
	want = "systemctl --user is-enabled --quiet calamaio.service\nsystemctl --user disable --now calamaio.service\nsystemctl --user daemon-reload"
	if strings.Join(r.lines(), "\n") != want {
		t.Fatalf("comandi:\n%s", strings.Join(r.lines(), "\n"))
	}
	if _, err := os.Stat(p.File); err == nil {
		t.Fatal("unità non tolta")
	}
}

func TestWindowsUnsupported(t *testing.T) {
	e := testEnv(t, "windows")
	r := &fakeRunner{}
	o, _ := dialog("y\n")
	err := e.Install(r, o, nil, "/")
	if !errors.Is(err, ErrUnsupported) || !strings.Contains(err.Error(), "Windows") {
		t.Fatalf("%v", err)
	}
	if err := e.Uninstall(r, o); !errors.Is(err, ErrUnsupported) {
		t.Fatal(err)
	}
	if len(r.calls) != 0 {
		t.Fatalf("comandi: %v", r.lines())
	}
	if st := e.Stat(r); st.Supported || st.Installed || !strings.Contains(st.Hint, "Windows") {
		t.Fatalf("%+v", st)
	}
}

func TestServeArgsRejectsTempBinary(t *testing.T) {
	if _, err := ServeArgs("/var/folders/x/go-build123/b001/exe/calamaio", nil, "/"); err == nil {
		t.Fatal("binario di go run accettato")
	}
	if _, err := ServeArgs("calamaio", nil, "/"); err == nil {
		t.Fatal("percorso relativo accettato")
	}
	a, err := ServeArgs("/bin/calamaio", []string{"serve", "--samples", "s", "--fake"}, "/r")
	if err != nil || strings.Join(a, " ") != "/bin/calamaio serve --samples /r/s --fake" {
		t.Fatalf("%q %v", a, err)
	}
}
