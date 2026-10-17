package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"time"

	"calamaio/internal/autostart"
	"calamaio/internal/config"
)

// installDeps: ciò che install/uninstall toccano, sostituibile nei test (HOME temporanea, comandi finti).
type installDeps struct {
	Env    func() (autostart.Env, error)
	Runner autostart.Runner
	In     io.Reader
	Out    io.Writer
	Err    io.Writer
	Cwd    func() (string, error)
	// Running: c'è già un servizio che risponde sulla porta della API? managed = avviato dall'avvio
	// automatico.
	Running func() (running, managed bool)
}

func realInstallDeps() installDeps {
	return installDeps{Env: autostart.Detect, Runner: autostart.ExecRunner{}, In: os.Stdin, Out: os.Stdout, Err: os.Stderr,
		Cwd: os.Getwd, Running: runningService}
}

// installCmd: calamaio install [--yes] [--status] [-- opzioni di serve]
func installCmd(args []string, d installDeps) int {
	fs := flag.NewFlagSet("install", flag.ContinueOnError)
	fs.SetOutput(d.Err)
	yes := fs.Bool("yes", false, "non chiedere conferma")
	status := fs.Bool("status", false, "mostra solo se l'avvio automatico è installato")
	fs.Usage = func() {
		fmt.Fprintln(d.Err, "uso: calamaio install [--yes] [-- opzioni di serve, es. --firmware DIR]")
		fmt.Fprintln(d.Err, "     calamaio install --status")
	}
	if err := fs.Parse(args); err != nil {
		return 2
	}
	env, err := d.Env()
	if err != nil {
		fmt.Fprintln(d.Err, "calamaio:", err)
		return 1
	}
	if *status {
		st := env.Stat(d.Runner)
		printAutostart(d.Out, st)
		if !st.Supported || !st.Installed {
			return 1
		}
		return 0
	}
	cwd, _ := d.Cwd()
	if d.Running != nil {
		if running, managed := d.Running(); running && !managed {
			fmt.Fprintln(d.Out, "Attenzione: c'è già un servizio Calamaio avviato a mano su questa porta. Quello del login non")
			fmt.Fprintf(d.Out, "riesce ad ascoltare finché questo è acceso e riprova da solo ogni %d s: dopo l'installazione\n", autostart.RestartSec)
			fmt.Fprintln(d.Out, "ferma quello a mano (Ctrl-C nel suo terminale) e subentra quello del login.")
			fmt.Fprintln(d.Out)
		}
	}
	err = env.Install(d.Runner, autostart.IO{In: d.In, Out: d.Out, Yes: *yes}, fs.Args(), cwd)
	return installExit(d, err)
}

// uninstallCmd: calamaio uninstall [--yes]
func uninstallCmd(args []string, d installDeps) int {
	fs := flag.NewFlagSet("uninstall", flag.ContinueOnError)
	fs.SetOutput(d.Err)
	yes := fs.Bool("yes", false, "non chiedere conferma")
	if err := fs.Parse(args); err != nil {
		return 2
	}
	if fs.NArg() > 0 {
		fmt.Fprintln(d.Err, "uso: calamaio uninstall [--yes]")
		return 2
	}
	env, err := d.Env()
	if err != nil {
		fmt.Fprintln(d.Err, "calamaio:", err)
		return 1
	}
	return installExit(d, env.Uninstall(d.Runner, autostart.IO{In: d.In, Out: d.Out, Yes: *yes}))
}

func installExit(d installDeps, err error) int {
	switch {
	case err == nil:
		return 0
	case errors.Is(err, autostart.ErrAborted):
		fmt.Fprintln(d.Out, err)
		return 1
	default:
		fmt.Fprintln(d.Err, "calamaio:", err)
		return 1
	}
}

// printAutostart: una riga (più un consiglio) sullo stato dell'avvio automatico.
func printAutostart(out io.Writer, st autostart.Status) {
	switch {
	case !st.Supported:
		fmt.Fprintf(out, "Avvio automatico: non disponibile (%s)\n", st.Hint)
		return
	case !st.Installed:
		fmt.Fprintf(out, "Avvio automatico: non installato · %s\n", st.Hint)
		return
	}
	what := map[string]string{"launchd": "LaunchAgent", "systemd": "systemd utente"}[st.Kind]
	line := "Avvio automatico: installato (" + what + ", " + st.File + ")"
	if st.Loaded != nil {
		if *st.Loaded {
			line += " · caricato"
		} else {
			line += " · NON caricato"
		}
	}
	fmt.Fprintln(out, line)
	if st.ExeMissing {
		fmt.Fprintf(out, "  il binario %s non c'è più\n", st.Exe)
	}
	if st.Hint != "" {
		fmt.Fprintf(out, "  %s\n", st.Hint)
	}
}

// runningService: GET /api/status sulla porta della config; managed dal campo autostart.
func runningService() (running, managed bool) {
	c, _ := config.Load()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, fmt.Sprintf("http://127.0.0.1:%d/api/status", c.APIPort), nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return false, false
	}
	defer resp.Body.Close()
	var body struct {
		Autostart *autostart.Status `json:"autostart"`
	}
	_ = json.NewDecoder(io.LimitReader(resp.Body, 8<<20)).Decode(&body)
	return true, body.Autostart != nil && body.Autostart.Managed
}
