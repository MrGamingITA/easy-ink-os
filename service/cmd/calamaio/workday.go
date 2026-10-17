package main

import (
	"errors"
	"flag"
	"fmt"
	"os"
	"time"

	"calamaio/internal/client"
	"calamaio/internal/config"
	"calamaio/internal/display"
)

const workdayUsage = "uso: calamaio workday start|stop|status [--board B] | hours INIZIO FINE [--board B]   (es. hours 09:00 18:00)"

// workdayCmd: calamaio workday start|stop|status|hours. La giornata la tiene la scheda: da qui si avvia,
// si ferma, si guarda e se ne cambiano gli orari (docs/PROTOCOLLO.md «Giornata di lavoro»).
func workdayCmd(args []string) int {
	if len(args) == 0 {
		fmt.Fprintln(os.Stderr, workdayUsage)
		return 2
	}
	sub := args[0]
	switch sub {
	case "start", "stop", "status", "hours":
	default:
		fmt.Fprintln(os.Stderr, workdayUsage)
		return 2
	}
	fs := flag.NewFlagSet("workday "+sub, flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	board := fs.String("board", "", "scheda (es. calamaio-3f2a); vuoto = l'unica collegata")
	if err := fs.Parse(reorder(args[1:], nil)); err != nil {
		fmt.Fprintln(os.Stderr, workdayUsage)
		return 2
	}
	want := 0
	if sub == "hours" {
		want = 2
	}
	if fs.NArg() != want {
		fmt.Fprintln(os.Stderr, workdayUsage)
		return 2
	}
	c, _ := config.Load()
	var err error
	switch sub {
	case "status":
		err = client.WorkdayStatus(c, *board, os.Stdout, time.Now())
	case "start":
		err = client.WorkdayCommand(c, *board, display.WorkdayStart, os.Stdout)
	case "stop":
		err = client.WorkdayCommand(c, *board, display.WorkdayStop, os.Stdout)
	case "hours":
		err = client.WorkdayHours(c, *board, fs.Arg(0), fs.Arg(1), os.Stdout)
	}
	switch {
	case errors.Is(err, client.ErrWorkdayFailed):
		return 1
	case err != nil:
		fmt.Fprintln(os.Stderr, "calamaio:", err)
		return 1
	}
	return 0
}
