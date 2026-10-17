package main

import (
	"errors"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"golang.org/x/term"

	"calamaio/internal/client"
	"calamaio/internal/config"
)

const wifiUsage = "uso: calamaio wifi list [--board B] | add [--board B] [--stdin] SSID | forget [--board B] [--yes] SSID"

// wifiCmd: calamaio wifi list|add|forget. La password non è mai un argomento (resterebbe nella cronologia
// della shell e in `ps`): si chiede nascosta dal terminale, oppure con --stdin dalla prima riga dello
// standard input.
func wifiCmd(args []string) int {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, wifiUsage)
		return 2
	}
	sub := args[0]
	fs := flag.NewFlagSet("wifi "+sub, flag.ContinueOnError)
	board := fs.String("board", "", "scheda (es. calamaio-3f2a); vuoto = l'unica collegata")
	var stdin, yes *bool
	switch sub {
	case "list":
	case "add":
		stdin = fs.Bool("stdin", false, "legge la password dalla prima riga dello standard input (per gli script)")
	case "forget":
		yes = fs.Bool("yes", false, "non chiedere conferma")
	default:
		fmt.Fprintln(os.Stderr, wifiUsage)
		return 2
	}
	if err := fs.Parse(reorder(args[1:], map[string]bool{"--stdin": true, "-stdin": true, "--yes": true, "-yes": true})); err != nil {
		fmt.Fprintln(os.Stderr, wifiUsage)
		return 2
	}
	c, _ := config.Load()
	w := client.WifiIO{In: os.Stdin, Out: os.Stdout}
	var err error
	switch sub {
	case "list":
		if fs.NArg() != 0 {
			fmt.Fprintln(os.Stderr, wifiUsage)
			return 2
		}
		err = client.WifiList(c, *board, w)
	case "add":
		if fs.NArg() != 1 {
			fmt.Fprintln(os.Stderr, "uso: calamaio wifi add [--board B] [--stdin] SSID   (la password si chiede dopo, nascosta: mai come argomento)")
			return 2
		}
		switch {
		case *stdin:
			w.Password = func(string) ([]byte, error) { return client.ReadPasswordLine(os.Stdin) }
		case term.IsTerminal(int(os.Stdin.Fd())):
			w.Password, w.Confirm = readHidden, true
		default:
			fmt.Fprintln(os.Stderr, "calamaio: la password si chiede solo da un terminale, senza mostrarla; negli script usa --stdin (prima riga dello standard input)")
			return 2
		}
		err = client.WifiAdd(c, *board, fs.Arg(0), w)
	case "forget":
		if fs.NArg() != 1 {
			fmt.Fprintln(os.Stderr, "uso: calamaio wifi forget [--board B] [--yes] SSID")
			return 2
		}
		w.Yes = *yes
		err = client.WifiForget(c, *board, fs.Arg(0), w)
	}
	switch {
	case errors.Is(err, client.ErrWifiFailed):
		return 1
	case err != nil:
		fmt.Fprintln(os.Stderr, "calamaio:", err)
		return 1
	}
	return 0
}

// readHidden chiede la password sul terminale senza eco (prompt sullo standard error). Con Ctrl-C il
// terminale torna com'era: senza, resterebbe senza eco dopo l'uscita.
func readHidden(prompt string) ([]byte, error) {
	fd := int(os.Stdin.Fd())
	old, err := term.GetState(fd)
	if err != nil {
		return nil, err
	}
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM, syscall.SIGHUP)
	done := make(chan struct{})
	defer func() { signal.Stop(sig); close(done) }()
	go func() {
		select {
		case <-sig:
			_ = term.Restore(fd, old)
			fmt.Fprintln(os.Stderr)
			os.Exit(130)
		case <-done:
		}
	}()
	fmt.Fprint(os.Stderr, prompt)
	pw, err := term.ReadPassword(fd)
	fmt.Fprintln(os.Stderr)
	return pw, err
}
