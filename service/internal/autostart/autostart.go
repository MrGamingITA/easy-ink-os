// Package autostart: avvio automatico di `calamaio serve` al login (brief 5.2, M6/M9).
// macOS: LaunchAgent utente in ~/Library/LaunchAgents, caricato con `launchctl bootstrap gui/<uid>`.
// Linux: unità systemd utente in ~/.config/systemd/user, `systemctl --user enable --now`.
// Windows: non ancora (messaggio chiaro, TODO M9).
//
// Tutto ciò che tocca il sistema passa da Env (percorsi) e da Runner (comandi): nei test non parte
// niente e i file finiscono in una HOME temporanea.
package autostart

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"calamaio/internal/config"
)

const (
	// Label: etichetta del LaunchAgent (nome del file: Label + ".plist").
	Label = "com.calamaio.service"
	// UnitName: unità systemd utente.
	UnitName = "calamaio.service"
	// EnvMarker: variabile d'ambiente che il servizio riceve quando lo avvia l'avvio automatico
	// (valore launchd o systemd): così sa di essere «gestito».
	EnvMarker = "CALAMAIO_AUTOSTART"
	// RestartSec: dopo un'uscita con errore (porta occupata, per esempio) si riprova ogni 30 s.
	RestartSec = 30
	// StopTimeoutSec: tra SIGTERM e SIGKILL. Lo spegnimento pulito del servizio dura al massimo
	// ~6 s (permessi 1 s + bye 2 s + chiusura 3 s): 15 lascia margine.
	StopTimeoutSec = 15
	// LogName: file di log in <dati utente>/calamaio/logs.
	LogName = "calamaio.log"
)

var (
	ErrAborted     = errors.New("annullato: nessun file modificato")
	ErrUnsupported = errors.New("avvio automatico non ancora disponibile su questo sistema")
)

// Runner esegue un comando di sistema e ne restituisce l'output (stdout e stderr insieme).
type Runner interface {
	Run(name string, args ...string) (string, error)
}

// ExecRunner esegue davvero.
type ExecRunner struct{}

func (ExecRunner) Run(name string, args ...string) (string, error) {
	out, err := exec.Command(name, args...).CombinedOutput()
	return string(out), err
}

// Env descrive la macchina: sistema, cartelle, utente, binario.
type Env struct {
	GOOS       string
	Home       string
	UID        int
	Exe        string // percorso assoluto del binario calamaio
	LogDir     string // <dati utente>/calamaio/logs
	BackupDir  string // copie dei file sostituiti o tolti: <dati utente>/calamaio/autostart
	ConfigHome string // Linux: $XDG_CONFIG_HOME (vuoto = ~/.config)
	Getenv     func(string) string
	Now        func() time.Time
	Sleep      func(time.Duration)
}

// Detect legge l'ambiente vero.
func Detect() (Env, error) {
	e := Env{GOOS: runtime.GOOS, UID: os.Getuid(), Getenv: os.Getenv, Now: time.Now, Sleep: time.Sleep}
	var err error
	if e.Home, err = os.UserHomeDir(); err != nil {
		return e, err
	}
	exe, err := os.Executable()
	if err != nil {
		return e, err
	}
	if r, err := filepath.EvalSymlinks(exe); err == nil {
		exe = r
	}
	e.Exe = exe
	data, err := config.DataDir()
	if err != nil {
		return e, err
	}
	e.LogDir = filepath.Join(data, "logs")
	e.BackupDir = filepath.Join(data, "autostart")
	e.ConfigHome = os.Getenv("XDG_CONFIG_HOME")
	return e, nil
}

func (e Env) getenv(k string) string {
	if e.Getenv == nil {
		return ""
	}
	return e.Getenv(k)
}

func (e Env) now() time.Time {
	if e.Now == nil {
		return time.Now()
	}
	return e.Now()
}

func (e Env) sleep(d time.Duration) {
	if e.Sleep != nil {
		e.Sleep(d)
	}
}

// Kind: launchd, systemd oppure "" (non supportato).
func (e Env) Kind() string {
	switch e.GOOS {
	case "darwin":
		return "launchd"
	case "linux":
		return "systemd"
	}
	return ""
}

// File: dove sta il file dell'avvio automatico.
func (e Env) File() string {
	switch e.Kind() {
	case "launchd":
		return filepath.Join(e.Home, "Library", "LaunchAgents", Label+".plist")
	case "systemd":
		base := e.ConfigHome
		if base == "" || !filepath.IsAbs(base) {
			base = filepath.Join(e.Home, ".config")
		}
		return filepath.Join(base, "systemd", "user", UnitName)
	}
	return ""
}

// LogFile: dove scrive il servizio avviato al login.
func (e Env) LogFile() string { return filepath.Join(e.LogDir, LogName) }

func (e Env) domain() string  { return "gui/" + strconv.Itoa(e.UID) }
func (e Env) service() string { return e.domain() + "/" + Label }

// unsupportedMsg: cosa dire su Windows (e altrove).
func (e Env) unsupportedMsg() string {
	if e.GOOS == "windows" {
		// TODO(M9): attività pianificata all'accesso (schtasks /SC ONLOGON) oppure kardianos/service; va
		// verificato anche come arriva lo spegnimento (niente SIGTERM su Windows) per mandare `bye`.
		return "su Windows l'avvio automatico non c'è ancora (arriva con l'installer di M9): per ora avvia `calamaio serve` a mano"
	}
	return "avvio automatico non disponibile su " + e.GOOS + ": avvia `calamaio serve` a mano"
}

// Plan è ciò che Install farebbe: il file da scrivere e i comandi da eseguire.
type Plan struct {
	Kind    string
	File    string
	Content []byte
	Args    []string // comando del servizio (binario, serve, opzioni)
	LogFile string
}

// pathFlags: opzioni di serve con un percorso, rese assolute (il servizio parte dalla home).
var pathFlags = map[string]bool{"--firmware": true, "-firmware": true, "--samples": true, "-samples": true,
	"--factory-backup": true, "-factory-backup": true}

// ServeArgs: binario + serve + opzioni, con i percorsi relativi resi assoluti rispetto a cwd.
func ServeArgs(exe string, extra []string, cwd string) ([]string, error) {
	if exe == "" || !filepath.IsAbs(exe) {
		return nil, fmt.Errorf("percorso del binario non assoluto: %q", exe)
	}
	if strings.Contains(exe, string(filepath.Separator)+"go-build") {
		return nil, fmt.Errorf("il binario %s è temporaneo (go run): installa prima calamaio (es. in ~/.local/bin) e lancia quello", exe)
	}
	abs := func(p string) string {
		if p == "" || filepath.IsAbs(p) {
			return p
		}
		return filepath.Join(cwd, p)
	}
	out := []string{exe, "serve"}
	for i := 0; i < len(extra); i++ {
		a := extra[i]
		if a == "serve" && i == 0 {
			continue
		}
		if k, v, ok := strings.Cut(a, "="); ok && pathFlags[k] {
			out = append(out, k+"="+abs(v))
			continue
		}
		out = append(out, a)
		if pathFlags[a] && i+1 < len(extra) {
			i++
			out = append(out, abs(extra[i]))
		}
	}
	return out, nil
}

// pathEnv: PATH del servizio avviato al login (launchd ne dà uno minimo): la cartella del binario,
// Homebrew e i percorsi di sistema (per `gh` in M8).
func (e Env) pathEnv() string {
	dirs := []string{filepath.Dir(e.Exe)}
	if e.GOOS == "darwin" {
		dirs = append(dirs, "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin")
	} else {
		dirs = append(dirs, "/usr/local/bin", "/usr/bin", "/bin")
	}
	seen := map[string]bool{}
	var out []string
	for _, d := range dirs {
		if !seen[d] {
			seen[d] = true
			out = append(out, d)
		}
	}
	return strings.Join(out, ":")
}

// Plan prepara il file per questo sistema. extra = opzioni di serve, cwd = cartella da cui si
// risolvono i percorsi relativi.
func (e Env) Plan(extra []string, cwd string) (Plan, error) {
	kind := e.Kind()
	if kind == "" {
		return Plan{}, fmt.Errorf("%w: %s", ErrUnsupported, e.unsupportedMsg())
	}
	args, err := ServeArgs(e.Exe, extra, cwd)
	if err != nil {
		return Plan{}, err
	}
	p := Plan{Kind: kind, File: e.File(), Args: args, LogFile: e.LogFile()}
	switch kind {
	case "launchd":
		p.Content = e.plist(args)
	case "systemd":
		p.Content, err = e.unit(args)
		if err != nil {
			return Plan{}, err
		}
	}
	return p, nil
}

// Status: com'è messo l'avvio automatico.
type Status struct {
	Supported bool   `json:"supported"`
	Kind      string `json:"kind,omitempty"` // launchd · systemd
	Installed bool   `json:"installed"`      // il file c'è
	File      string `json:"file,omitempty"`
	// Managed: questo processo è stato avviato dall'avvio automatico (variabile CALAMAIO_AUTOSTART).
	Managed bool `json:"managed"`
	// Loaded: launchd/systemd lo conoscono (solo dalla riga di comando: chiede a launchctl o systemctl).
	Loaded *bool `json:"loaded,omitempty"`
	// Exe: binario indicato nel file; ExeMissing se non esiste più.
	Exe        string `json:"exe,omitempty"`
	ExeMissing bool   `json:"exeMissing,omitempty"`
	Hint       string `json:"hint,omitempty"` // cosa fare, in italiano
}

// Stat legge lo stato. r nil = senza chiedere a launchctl/systemctl (il servizio lo chiama a ogni
// /api/status: deve costare poco).
func (e Env) Stat(r Runner) Status {
	st := Status{Kind: e.Kind(), File: e.File(), Managed: e.getenv(EnvMarker) != ""}
	if st.Kind == "" {
		st.Hint = e.unsupportedMsg()
		return st
	}
	st.Supported = true
	b, err := os.ReadFile(st.File)
	st.Installed = err == nil
	if st.Installed {
		st.Exe = programOf(st.Kind, b)
		if st.Exe != "" {
			if _, err := os.Stat(st.Exe); err != nil {
				st.ExeMissing = true
			}
		}
	}
	if r != nil {
		l := e.loaded(r)
		st.Loaded = &l
	}
	switch {
	case !st.Installed:
		st.Hint = "Il servizio non parte da solo al login: calamaio install"
	case st.ExeMissing:
		st.Hint = "Il file dell'avvio automatico punta a un binario che non c'è più: calamaio install"
	case st.Loaded != nil && !*st.Loaded:
		st.Hint = "Installato ma non caricato: calamaio install lo ricarica"
	}
	return st
}

// loaded: launchctl print gui/<uid>/<label> esce con 0 se il servizio è nel dominio (l'output non è
// un'API, il codice d'uscita sì); systemctl --user is-enabled esce con 0 se è abilitato.
func (e Env) loaded(r Runner) bool {
	switch e.Kind() {
	case "launchd":
		_, err := r.Run("launchctl", "print", e.service())
		return err == nil
	case "systemd":
		_, err := r.Run("systemctl", "--user", "is-enabled", "--quiet", UnitName)
		return err == nil
	}
	return false
}

func (e Env) active(r Runner) bool {
	_, err := r.Run("systemctl", "--user", "is-active", "--quiet", UnitName)
	return err == nil
}

// IO: dialogo con l'utente.
type IO struct {
	In  io.Reader
	Out io.Writer
	Yes bool // --yes: niente domanda
}

func confirm(o IO, question string) bool {
	if o.Yes {
		return true
	}
	fmt.Fprintf(o.Out, "%s [y/N] ", question)
	line, _ := bufio.NewReader(o.In).ReadString('\n')
	switch strings.ToLower(strings.TrimSpace(line)) {
	case "y", "yes", "s", "si", "sì":
		return true
	}
	fmt.Fprintln(o.Out)
	return false
}

// step è un comando da eseguire, con la descrizione per l'utente.
type step struct {
	why  string
	args []string
}

func (s step) String() string { return strings.Join(quoteAll(s.args), " ") }

func quoteAll(a []string) []string {
	out := make([]string, len(a))
	for i, s := range a {
		if s == "" || strings.ContainsAny(s, " \t'\"$\\") {
			s = "'" + strings.ReplaceAll(s, "'", `'\''`) + "'"
		}
		out[i] = s
	}
	return out
}

// installSteps: i comandi dopo aver scritto il file.
func (e Env) installSteps(p Plan, loaded, active bool) []step {
	switch p.Kind {
	case "launchd":
		var s []step
		s = append(s, step{"controlla il file", []string{"plutil", "-lint", p.File}})
		if loaded {
			s = append(s, step{"ferma la versione caricata (il servizio manda bye e si chiude)", []string{"launchctl", "bootout", e.service()}})
		}
		// niente `launchctl enable` di default: scriverebbe una voce permanente nel database di launchd
		// che uninstall non toglie. Se il servizio era stato disabilitato, bootstrap fallisce e l'errore
		// lo dice (bootstrapHint).
		s = append(s, step{"carica il LaunchAgent: parte subito (RunAtLoad) e a ogni login", []string{"launchctl", "bootstrap", e.domain(), p.File}})
		return s
	case "systemd":
		s := []step{
			{"rilegge le unità", []string{"systemctl", "--user", "daemon-reload"}},
			{"abilita al login e avvia adesso", []string{"systemctl", "--user", "enable", "--now", UnitName}},
		}
		if active {
			s = append(s, step{"riavvia con il file nuovo", []string{"systemctl", "--user", "restart", UnitName}})
		}
		return s
	}
	return nil
}

func (e Env) uninstallSteps(kind string, loaded bool) []step {
	switch kind {
	case "launchd":
		if loaded {
			return []step{{"ferma e scarica il LaunchAgent (il servizio manda bye e si chiude)", []string{"launchctl", "bootout", e.service()}}}
		}
	case "systemd":
		if loaded {
			return []step{{"disabilita e ferma", []string{"systemctl", "--user", "disable", "--now", UnitName}}}
		}
	}
	return nil
}

func (e Env) postUninstallSteps(kind string) []step {
	if kind == "systemd" {
		return []step{{"rilegge le unità", []string{"systemctl", "--user", "daemon-reload"}}}
	}
	return nil
}

// run esegue i passi in ordine; bootstrap si riprova (subito dopo un bootout launchd può rispondere
// «Input/output error» finché il vecchio processo non è uscito).
func (e Env) run(r Runner, o IO, steps []step) error {
	for _, s := range steps {
		fmt.Fprintf(o.Out, "  $ %s\n", s)
		tries := 1
		if len(s.args) > 1 && s.args[0] == "launchctl" && s.args[1] == "bootstrap" {
			tries = 4
		}
		var out string
		var err error
		for i := 0; i < tries; i++ {
			if i > 0 {
				e.sleep(time.Second)
			}
			if out, err = r.Run(s.args[0], s.args[1:]...); err == nil {
				break
			}
		}
		if err != nil {
			err = fmt.Errorf("%s: %v %s", s, err, strings.TrimSpace(out))
			if s.args[0] == "launchctl" && s.args[1] == "bootstrap" {
				err = fmt.Errorf("%w\n%s", err, bootstrapHint(e))
			}
			return err
		}
	}
	return nil
}

// bootstrapHint: le cause note di un bootstrap che fallisce.
func bootstrapHint(e Env) string {
	return "Se il servizio era stato disabilitato: launchctl enable " + e.service() + " e poi di nuovo calamaio install.\n" +
		"Per vedere che cosa dice launchd: launchctl print " + e.service()
}

// backup copia path in BackupDir con data e ora; restituisce la copia.
func (e Env) backup(path string, b []byte) (string, error) {
	if err := os.MkdirAll(e.BackupDir, 0o700); err != nil {
		return "", err
	}
	dst := filepath.Join(e.BackupDir, filepath.Base(path)+"."+e.now().Format("20060102-150405"))
	if err := os.WriteFile(dst, b, 0o600); err != nil {
		return "", err
	}
	return dst, nil
}

func writeAtomic(path string, b []byte, mode fs.FileMode) error {
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, mode); err != nil {
		return err
	}
	if err := os.Chmod(tmp, mode); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, path)
}

// Install scrive il file dell'avvio automatico (dopo conferma, con copia del vecchio) e lo carica.
func (e Env) Install(r Runner, o IO, extra []string, cwd string) error {
	p, err := e.Plan(extra, cwd)
	if err != nil {
		return err
	}
	old, err := os.ReadFile(p.File)
	had := err == nil
	if err != nil && !errors.Is(err, fs.ErrNotExist) {
		return err
	}
	loaded := e.loaded(r)
	active := p.Kind == "systemd" && e.active(r)
	if had && bytes.Equal(old, p.Content) && loaded {
		fmt.Fprintf(o.Out, "Avvio automatico già installato e caricato: %s\n", p.File)
		return nil
	}
	steps := e.installSteps(p, loaded, active)

	what := map[string]string{"launchd": "LaunchAgent utente (launchd)", "systemd": "unità systemd utente"}[p.Kind]
	fmt.Fprintf(o.Out, "Avvio automatico di Calamaio al login: %s\n\n", what)
	fmt.Fprintf(o.Out, "  comando  %s\n", strings.Join(quoteAll(p.Args), " "))
	fmt.Fprintf(o.Out, "  log      %s\n", p.LogFile)
	switch {
	case had && bytes.Equal(old, p.Content):
		fmt.Fprintf(o.Out, "  file     %s (già uguale, non lo riscrivo)\n", p.File)
	case had:
		fmt.Fprintf(o.Out, "  file     %s (esiste già: copia del vecchio in %s)\n", p.File, e.BackupDir)
	default:
		fmt.Fprintf(o.Out, "  file     %s (nuovo)\n", p.File)
	}
	fmt.Fprintf(o.Out, "\nContenuto:\n%s\n", indent(p.Content))
	fmt.Fprintln(o.Out, "Poi eseguo:")
	for _, s := range steps {
		fmt.Fprintf(o.Out, "  $ %-60s # %s\n", s, s.why)
	}
	fmt.Fprintln(o.Out)
	if !confirm(o, "Procedo?") {
		return ErrAborted
	}

	if err := os.MkdirAll(e.LogDir, 0o700); err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p.File), 0o755); err != nil {
		return err
	}
	if had && !bytes.Equal(old, p.Content) {
		bk, err := e.backup(p.File, old)
		if err != nil {
			return fmt.Errorf("copia di sicurezza: %w", err)
		}
		fmt.Fprintf(o.Out, "Copia del file di prima: %s\n", bk)
	}
	if !had || !bytes.Equal(old, p.Content) {
		if err := writeAtomic(p.File, p.Content, 0o644); err != nil {
			return err
		}
		fmt.Fprintf(o.Out, "Scritto %s\n", p.File)
	}
	if err := e.run(r, o, steps); err != nil {
		return err
	}
	fmt.Fprintf(o.Out, "Fatto: il servizio parte da solo a ogni login. Log: %s\n", p.LogFile)
	return nil
}

// Uninstall ferma il servizio dell'avvio automatico e toglie il file (dopo conferma, con copia).
func (e Env) Uninstall(r Runner, o IO) error {
	kind := e.Kind()
	if kind == "" {
		return fmt.Errorf("%w: %s", ErrUnsupported, e.unsupportedMsg())
	}
	file := e.File()
	old, err := os.ReadFile(file)
	had := err == nil
	if err != nil && !errors.Is(err, fs.ErrNotExist) {
		return err
	}
	loaded := e.loaded(r)
	if !had && !loaded {
		fmt.Fprintln(o.Out, "Avvio automatico non installato: niente da togliere.")
		return nil
	}
	steps := e.uninstallSteps(kind, loaded)
	fmt.Fprintln(o.Out, "Tolgo l'avvio automatico di Calamaio:")
	for _, s := range steps {
		fmt.Fprintf(o.Out, "  $ %-60s # %s\n", s, s.why)
	}
	if had {
		fmt.Fprintf(o.Out, "  sposto %s in %s\n", file, e.BackupDir)
	}
	for _, s := range e.postUninstallSteps(kind) {
		fmt.Fprintf(o.Out, "  $ %-60s # %s\n", s, s.why)
	}
	fmt.Fprintln(o.Out, "Il servizio non partirà più al login; per avviarlo a mano: calamaio serve")
	if !confirm(o, "Procedo?") {
		return ErrAborted
	}
	if err := e.run(r, o, steps); err != nil {
		return err
	}
	if had {
		bk, err := e.backup(file, old)
		if err != nil {
			return fmt.Errorf("copia di sicurezza: %w", err)
		}
		if err := os.Remove(file); err != nil {
			return err
		}
		fmt.Fprintf(o.Out, "Tolto %s (copia in %s)\n", file, bk)
	}
	if err := e.run(r, o, e.postUninstallSteps(kind)); err != nil {
		return err
	}
	fmt.Fprintln(o.Out, "Fatto.")
	return nil
}

func indent(b []byte) string {
	var sb strings.Builder
	for _, l := range strings.Split(strings.TrimRight(string(b), "\n"), "\n") {
		sb.WriteString("    " + l + "\n")
	}
	return sb.String()
}
