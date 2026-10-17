package client

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"text/tabwriter"
	"time"

	"calamaio/internal/config"
	"calamaio/internal/diag"
	"calamaio/internal/display"
	"calamaio/internal/fake"
	"calamaio/internal/state"
)

// debugInfo è la parte di GET /debug che serve a `calamaio status`.
type debugInfo struct {
	Started   time.Time            `json:"started"`
	Version   string               `json:"version"`
	WSPort    int                  `json:"ws_port"`
	TZ        string               `json:"tz"`
	MDNS      bool                 `json:"mdns"`
	Fake      bool                 `json:"fake"`
	Capture   bool                 `json:"capture"`
	Displays  []display.Info       `json:"displays"`
	Ignored   int64                `json:"ws_ignored"`
	State     *state.Snapshot      `json:"state"`
	Paired    []string             `json:"paired"`
	Perms     []display.PermStatus `json:"permissions"`
	LastCrash *diag.Crash          `json:"last_crash"`
	LastHooks []struct {
		TS      time.Time `json:"ts"`
		Source  string    `json:"source"`
		Event   string    `json:"event"`
		Session string    `json:"session_id"`
	} `json:"last"`
}

// Status stampa display collegati, sessioni e ultimi eventi letti da GET /debug.
func Status(c config.Config, out io.Writer, now time.Time) error {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, baseURL(c)+"/debug", nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return fmt.Errorf("il servizio non risponde su %s (è avviato? `calamaio serve`): %w", baseURL(c), err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("GET /debug: stato %d", resp.StatusCode)
	}
	var d debugInfo
	if err := json.NewDecoder(io.LimitReader(resp.Body, 8<<20)).Decode(&d); err != nil {
		return fmt.Errorf("GET /debug: %w", err)
	}
	printStatus(out, d, now)
	return nil
}

func printStatus(out io.Writer, d debugInfo, now time.Time) {
	mode := "sessioni vere"
	if d.Fake {
		mode = "stato finto"
	}
	fmt.Fprintf(out, "Servizio %s attivo da %s · display su porta %d · fuso %s · %s",
		d.Version, ago(now, d.Started), d.WSPort, d.TZ, mode)
	if !d.MDNS {
		fmt.Fprint(out, " · mDNS spento")
	}
	if d.Capture {
		fmt.Fprint(out, " · cattura attiva")
	}
	fmt.Fprintln(out)

	fmt.Fprintf(out, "\nDisplay collegati (%d)", len(d.Displays))
	if d.Ignored > 0 {
		fmt.Fprintf(out, " · %d messaggi ignorati", d.Ignored)
	}
	fmt.Fprintln(out)
	tw := tabwriter.NewWriter(out, 0, 0, 2, ' ', 0)
	for _, x := range d.Displays {
		if !x.Hello {
			fmt.Fprintf(tw, "  #%d\t(senza hello)\t\t%s\tda %s\n", x.ID, x.IP, ago(now, x.Since))
			continue
		}
		pair := PairText(x.Pair, x.PairReason)
		if x.Perms {
			pair += " · permessi dal display"
		}
		if p := x.Power; p != nil {
			b := "batteria ?"
			if p.Batt != nil {
				b = fmt.Sprintf("batteria %d%%", *p.Batt)
			}
			switch {
			case p.USB == nil:
				b += ", cavo ?"
			case *p.USB:
				b += ", cavo USB"
			default:
				b += ", senza cavo"
			}
			if p.Cal {
				b += ", calibrazione"
			}
			pair += " · " + b
		}
		if w := x.Workday; w != nil && w.On {
			if w.End != nil && time.UnixMilli(*w.End).After(now) {
				pair += " · giornata: " + RemainingText(time.UnixMilli(*w.End).Sub(now)) + ", fino alle " + time.UnixMilli(*w.End).Local().Format("15:04")
			} else {
				pair += " · giornata in corso"
			}
		}
		if o := x.OTA; o != nil && o.Active {
			pair += fmt.Sprintf(" · aggiornamento a %s: %s", o.To, StageText(o.Stage))
		} else if o != nil && time.Since(time.UnixMilli(o.Ended)) < time.Hour {
			pair += fmt.Sprintf(" · ultimo aggiornamento: %s %s", StageText(o.Stage), o.Error)
		}
		fmt.Fprintf(tw, "  %s\t%s (%s %d×%d, fw %s)\t%s\tda %s\t%d stati\t%s\n",
			x.Board, x.Device, x.Class, x.W, x.H, x.FW, x.IP, ago(now, x.Since), x.States, pair)
	}
	tw.Flush()
	var away []string
	for _, b := range d.Paired {
		on := false
		for _, x := range d.Displays {
			on = on || (x.Hello && x.Board == b)
		}
		if !on {
			away = append(away, b)
		}
	}
	if len(away) > 0 {
		fmt.Fprintf(out, "  Abbinati e non collegati: %s\n", strings.Join(away, ", "))
	}
	if len(d.Perms) > 0 {
		fmt.Fprintf(out, "\nRichieste di permesso aperte (%d)\n", len(d.Perms))
		for _, p := range d.Perms {
			fmt.Fprintf(out, "  %s  %s: %s %s → %s\n", time.UnixMilli(p.At).Local().Format("15:04:05"), p.Name, p.Tool, p.Arg, strings.Join(p.SentTo, ", "))
		}
	}

	if cr := d.LastCrash; cr != nil {
		fmt.Fprintf(out, "\nUltimo crash: %s, firmware %s, %s · %s\n", cr.Board, cr.FW,
			time.UnixMilli(cr.Received).Local().Format("2006-01-02 15:04"), cr.Reason)
		for i, l := range cr.Decoded {
			if i == 4 {
				fmt.Fprintf(out, "  … (%d righe in tutto)\n", len(cr.Decoded))
				break
			}
			fmt.Fprintf(out, "  %s\n", l)
		}
		if len(cr.Decoded) == 0 && cr.PC != "" {
			fmt.Fprintf(out, "  PC %s %s\n", cr.PC, strings.Join(cr.Backtrace, " "))
		}
		if cr.DecodeNote != "" {
			fmt.Fprintf(out, "  %s\n", cr.DecodeNote)
		}
		fmt.Fprintf(out, "  rapporto: crash/%s.txt nella cartella dei dati\n", cr.ID)
	}

	if d.State != nil {
		fmt.Fprintf(out, "\nSessioni (%d)\n", len(d.State.Sessions))
		tw = tabwriter.NewWriter(out, 0, 0, 2, ' ', 0)
		for _, s := range d.State.Sessions {
			done := 0
			for _, t := range s.Tasks {
				if t.Status == state.TaskDoneSt {
					done++
				}
			}
			extra := []string{}
			if len(s.Tasks) > 0 {
				extra = append(extra, fmt.Sprintf("task %d/%d", done, len(s.Tasks)))
			}
			if s.Ctx != nil {
				extra = append(extra, fmt.Sprintf("ctx %d%%", *s.Ctx))
			}
			if s.Test != nil {
				extra = append(extra, "test "+*s.Test)
			}
			if s.Error != "" {
				extra = append(extra, "errore "+s.Error)
			}
			text := s.Detail
			if s.Summary != "" {
				text += " · " + s.Summary
			}
			fmt.Fprintf(tw, "  %s\t%s\t%s\t%s\t%s\n", s.Name, s.State, ago(now, time.UnixMilli(s.Since)), strings.Join(extra, " "), text)
		}
		tw.Flush()

		fmt.Fprintln(out, "\nUltimi eventi")
		n := min(len(d.State.Log), 10)
		for _, e := range d.State.Log[:n] {
			fmt.Fprintf(out, "  %s  %s: %s\n", time.UnixMilli(e.T).Format("15:04:05"), e.Name, e.Text)
		}
	}
	if len(d.LastHooks) > 0 {
		fmt.Fprintln(out, "\nUltimi hook ricevuti")
		hooks := d.LastHooks
		if len(hooks) > 10 {
			hooks = hooks[len(hooks)-10:]
		}
		for _, h := range hooks {
			fmt.Fprintf(out, "  %s  %s %s %s\n", h.TS.Local().Format("15:04:05"), h.Source, h.Event, h.Session)
		}
	}
}

func ago(now, t time.Time) string {
	if t.IsZero() {
		return "?"
	}
	d := now.Sub(t).Round(time.Second)
	if d < 0 {
		d = 0
	}
	return fmt.Sprintf("%s (%s fa)", t.Local().Format("15:04:05"), d)
}

// Fake chiama POST /debug/fake e stampa la sessione risultante.
func Fake(c config.Config, r fake.ForceRequest, out io.Writer) error {
	body, _ := json.Marshal(r)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, http.MethodPost, baseURL(c)+"/debug/fake", strings.NewReader(string(body)))
	req.Header.Set("Content-Type", "application/json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return fmt.Errorf("il servizio non risponde su %s: %w", baseURL(c), err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("%s", strings.TrimSpace(string(b)))
	}
	var res struct {
		Session state.Session `json:"session"`
	}
	if err := json.Unmarshal(b, &res); err != nil {
		return err
	}
	s := res.Session
	fmt.Fprintf(out, "%s → %s", s.Name, s.State)
	if s.Detail != "" {
		fmt.Fprintf(out, " · %s", s.Detail)
	}
	fmt.Fprintln(out)
	return nil
}
