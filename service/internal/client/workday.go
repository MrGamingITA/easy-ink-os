package client

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"

	"calamaio/internal/config"
	"calamaio/internal/display"
)

// `calamaio workday start|stop|status|hours` (docs/PROTOCOLLO.md «Giornata di lavoro»). La giornata la
// tiene la scheda: il comando passa dalla API della webapp e aspetta la conferma (ack) della scheda.

// ErrWorkdayFailed: comando non riuscito (il motivo è già stato scritto).
var ErrWorkdayFailed = errors.New("comando della giornata di lavoro non riuscito")

// RemainingText: «mancano 6 h 25», «mancano 25 min». Minuti arrotondati per eccesso come sul display
// (core/workday.cpp), così webapp, terminale e scheda mostrano lo stesso numero.
func RemainingText(left time.Duration) string {
	if left <= 0 {
		return "mancano 0 min"
	}
	m := int((left + time.Minute - 1) / time.Minute)
	if m < 60 {
		return fmt.Sprintf("mancano %d min", m)
	}
	return fmt.Sprintf("mancano %d h %02d", m/60, m%60)
}

// WorkdayLine descrive la giornata di un display (una riga, senza il nome della scheda).
func WorkdayLine(d *display.DisplayStatus, now time.Time) string {
	w := d.Workday
	hours := ""
	if a, ok := d.Settings["workStart"].(string); ok {
		if b, ok := d.Settings["workEnd"].(string); ok {
			hours = " (orari " + a + "–" + b + ")"
		}
	}
	switch {
	case w == nil:
		return "il servizio in esecuzione non conosce la giornata di lavoro (versione vecchia): riavvialo"
	case !w.Supported:
		return "non gestita dal firmware " + d.FW + ": serve un aggiornamento"
	case !w.Known:
		return "stato non ancora riferito dalla scheda" + hours
	case !w.On:
		return "spenta" + hours
	case w.End == nil:
		return "in corso (la scheda non ha detto quando finisce)" + hours
	}
	end := time.UnixMilli(*w.End).Local()
	left := end.Sub(now)
	if left <= 0 {
		return "finita alle " + end.Format("15:04") + hours
	}
	return "in corso, " + RemainingText(left) + ", fino alle " + end.Format("15:04") + hours
}

func workdayDisplays(c config.Config, board string) ([]display.DisplayStatus, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	st, err := getAPIStatus(ctx, c)
	if err != nil {
		return nil, err
	}
	if board == "" && len(st.Displays) > 1 {
		return st.Displays, nil
	}
	d, err := chooseBoard(st.Displays, board, nil, io.Discard, nil)
	if err != nil {
		return nil, err
	}
	return []display.DisplayStatus{*d}, nil
}

// WorkdayStatus stampa la giornata della scheda (senza board e con più schede: di tutte).
func WorkdayStatus(c config.Config, board string, out io.Writer, now time.Time) error {
	list, err := workdayDisplays(c, board)
	if err != nil {
		return err
	}
	if len(list) == 1 && list[0].Workday == nil {
		return errors.New("il servizio in esecuzione non conosce la giornata di lavoro (versione vecchia): riavvialo")
	}
	for i := range list {
		d := &list[i]
		fmt.Fprintf(out, "Giornata di lavoro di %s: %s.\n", d.Board, WorkdayLine(d, now))
		if p := d.Workday; p != nil && p.Pending != nil {
			fmt.Fprintf(out, "In attesa della conferma della scheda: %s.\n", actionText(p.Pending.Action))
		}
	}
	return nil
}

func actionText(a string) string {
	if a == display.WorkdayStop {
		return "fermo la giornata"
	}
	return "avvio la giornata"
}

// workdayTarget: la scheda indicata o l'unica collegata, che gestisce la giornata ed è autenticata.
func workdayTarget(c config.Config, board string) (*display.DisplayStatus, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	st, err := getAPIStatus(ctx, c)
	if err != nil {
		return nil, err
	}
	d, err := wifiBoard(st.Displays, board)
	if err != nil {
		return nil, err
	}
	switch {
	case d.Workday == nil:
		return nil, errors.New("il servizio in esecuzione non conosce la giornata di lavoro (versione vecchia): riavvialo")
	case !d.Workday.Supported:
		return nil, fmt.Errorf("il firmware %s di %s non gestisce la giornata di lavoro: aggiornalo", d.FW, d.Board)
	case !d.Authed:
		return nil, fmt.Errorf("la scheda %s non è abbinata e autenticata: la giornata si comanda dal PC solo così (calamaio pair), oppure dal menu della scheda (Strumenti)", d.Board)
	}
	return d, nil
}

// pollDisplay rilegge /api/status finché done dice basta (al massimo wait).
func pollDisplay(c config.Config, board string, wait time.Duration, done func(*display.DisplayStatus) bool) (*display.DisplayStatus, error) {
	deadline := time.Now().Add(wait)
	for {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		st, err := getAPIStatus(ctx, c)
		cancel()
		if err != nil {
			return nil, err
		}
		d := findDisplay(st, board)
		if d == nil {
			return nil, fmt.Errorf("la scheda %s si è scollegata prima di confermare", board)
		}
		if done(d) || time.Now().After(deadline) {
			return d, nil
		}
		time.Sleep(150 * time.Millisecond)
	}
}

// WorkdayCommand avvia (start) o ferma (stop) la giornata e aspetta la conferma della scheda.
func WorkdayCommand(c config.Config, board, action string, out io.Writer) error {
	d, err := workdayTarget(c, board)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	code, m, err := apiCall(ctx, c, http.MethodPost, "/api/displays/"+url.PathEscape(d.Board)+"/workday", map[string]any{"action": action})
	if err != nil {
		return err
	}
	if code != http.StatusAccepted {
		if e, _ := m["error"].(string); code == http.StatusNotFound && strings.HasPrefix(e, "API sconosciuta") {
			return errors.New("il servizio in esecuzione non conosce la giornata di lavoro (versione vecchia): riavvialo")
		}
		fmt.Fprintf(out, "Non riuscito: %s.\n", errText(m, code))
		return ErrWorkdayFailed
	}
	id, _ := m["id"].(string)
	// il servizio aspetta l'ack al massimo 10 s
	d, err = pollDisplay(c, d.Board, 15*time.Second, func(x *display.DisplayStatus) bool {
		return x.Workday != nil && x.Workday.Last != nil && x.Workday.Last.ID == id
	})
	if err != nil {
		return err
	}
	last := d.Workday.Last
	if last == nil || last.ID != id {
		fmt.Fprintln(out, "Nessuna risposta dal servizio sull'esito: controlla con calamaio workday status.")
		return ErrWorkdayFailed
	}
	if !last.OK {
		fmt.Fprintf(out, "Non riuscito: %s.\n", last.Error)
		return ErrWorkdayFailed
	}
	// La scheda manda lo stato nuovo con `status` (di solito prima dell'ack): lo aspetto un poco.
	want := action == display.WorkdayStart
	d, err = pollDisplay(c, d.Board, 3*time.Second, func(x *display.DisplayStatus) bool {
		return x.Workday != nil && x.Workday.Known && x.Workday.On == want
	})
	if err != nil {
		return err
	}
	if action == display.WorkdayStop {
		fmt.Fprintf(out, "Giornata di lavoro fermata su %s.\n", d.Board)
		return nil
	}
	fmt.Fprintf(out, "Giornata di lavoro avviata su %s: %s.\n", d.Board, WorkdayLine(d, time.Now()))
	return nil
}

// WorkdayHours cambia gli orari (workStart, workEnd) e aspetta la conferma della scheda.
func WorkdayHours(c config.Config, board, start, end string, out io.Writer) error {
	s, ok1 := display.NormalizeClock(start)
	e, ok2 := display.NormalizeClock(end)
	if !ok1 || !ok2 {
		return errors.New("orari non validi: scrivili come HH:MM, tra 00:00 e 23:59 (es. calamaio workday hours 09:00 18:00)")
	}
	if err := display.CheckWorkHours(s, e); err != nil {
		return err
	}
	d, err := workdayTarget(c, board)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	code, m, err := apiCall(ctx, c, http.MethodPost, "/api/displays/"+url.PathEscape(d.Board)+"/settings",
		map[string]any{"workStart": s, "workEnd": e})
	if err != nil {
		return err
	}
	if code != http.StatusAccepted {
		fmt.Fprintf(out, "Non riuscito: %s.\n", errText(m, code))
		return ErrWorkdayFailed
	}
	d, err = pollDisplay(c, d.Board, 15*time.Second, func(x *display.DisplayStatus) bool {
		_, a := x.SettingsPending["workStart"]
		_, b := x.SettingsPending["workEnd"]
		return !a && !b
	})
	if err != nil {
		return err
	}
	if d.Settings["workStart"] != s || d.Settings["workEnd"] != e {
		why := "la scheda non li ha confermati"
		if d.Error != nil {
			why = d.Error.Text
		}
		fmt.Fprintf(out, "Non riuscito: %s.\n", why)
		return ErrWorkdayFailed
	}
	fmt.Fprintf(out, "Orari della giornata di lavoro su %s: %s–%s.\n", d.Board, s, e)
	return nil
}
