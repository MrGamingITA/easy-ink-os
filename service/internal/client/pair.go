package client

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"strings"
	"time"

	"calamaio/internal/config"
	"calamaio/internal/display"
)

// `calamaio pair` e `calamaio unpair` (docs/M5-PERMESSI.md §1 e §5). Parlano con il servizio
// attraverso la stessa API della webapp: il codice lo confronta il servizio, in tempo costante.

// PairPoll: ogni quanto `calamaio pair` rilegge lo stato dell'abbinamento.
var PairPoll = 200 * time.Millisecond

// ErrPairFailed: abbinamento non riuscito (il motivo è già stato scritto).
var ErrPairFailed = errors.New("abbinamento non riuscito")

type apiStatus struct {
	Displays []display.DisplayStatus `json:"displays"`
	Paired   []string                `json:"paired"`
}

func apiCall(ctx context.Context, c config.Config, method, path string, body any) (int, map[string]any, error) {
	var rd io.Reader
	if body != nil {
		b, _ := json.Marshal(body)
		rd = bytes.NewReader(b)
	}
	req, err := http.NewRequestWithContext(ctx, method, baseURL(c)+path, rd)
	if err != nil {
		return 0, nil, err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := httpClient.Do(req)
	if err != nil {
		return 0, nil, fmt.Errorf("il servizio non risponde su %s (è avviato? `calamaio serve`): %w", baseURL(c), err)
	}
	defer resp.Body.Close()
	var m map[string]any
	b, _ := io.ReadAll(io.LimitReader(resp.Body, 8<<20))
	_ = json.Unmarshal(b, &m)
	return resp.StatusCode, m, nil
}

func getAPIStatus(ctx context.Context, c config.Config) (*apiStatus, error) {
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, baseURL(c)+"/api/status", nil)
	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("il servizio non risponde su %s (è avviato? `calamaio serve`): %w", baseURL(c), err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET /api/status: stato %d", resp.StatusCode)
	}
	var st apiStatus
	if err := json.NewDecoder(io.LimitReader(resp.Body, 8<<20)).Decode(&st); err != nil {
		return nil, err
	}
	return &st, nil
}

func errText(m map[string]any, code int) string {
	if s, ok := m["error"].(string); ok && s != "" {
		return s
	}
	return fmt.Sprintf("risposta inattesa del servizio (HTTP %d)", code)
}

// AbortText spiega il motivo di un abbinamento annullato.
func AbortText(reason string) string {
	switch reason {
	case display.AbortCode:
		return "codice sbagliato"
	case display.AbortTime:
		return "tempo scaduto (3 minuti)"
	case display.AbortCancel:
		return "annullato dal PC"
	case "display":
		return "annullato sul display"
	case "connessione":
		return "il display si è scollegato"
	case "impegno":
		return "il display ha ricevuto una chiave del PC che non corrisponde all'impegno: qualcuno in rete potrebbe essersi messo in mezzo"
	case display.AbortMac:
		return "verifica non riuscita (mac sbagliato)"
	case display.AbortKey:
		return "chiave non valida"
	case "":
		return "annullato"
	}
	return reason
}

// PairText descrive lo stato dell'abbinamento di un display.
func PairText(pair, reason string) string {
	switch pair {
	case "ok":
		return "abbinato e autenticato"
	case "redo":
		if reason != "" {
			return "abbinamento da rifare (" + reason + ")"
		}
		return "abbinamento da rifare"
	case "auth":
		return "abbinato, autenticazione in corso"
	case "none", "":
		return "non abbinato"
	}
	return pair
}

func findDisplay(st *apiStatus, board string) *display.DisplayStatus {
	for i := range st.Displays {
		if st.Displays[i].Board == board {
			return &st.Displays[i]
		}
	}
	return nil
}

// lines legge le righe di in su un canale (chiuso alla fine dell'input).
func lines(in io.Reader) <-chan string {
	ch := make(chan string)
	go func() {
		defer close(ch)
		sc := bufio.NewScanner(in)
		for sc.Scan() {
			ch <- sc.Text()
		}
	}()
	return ch
}

// Pair abbina un display: sceglie la scheda (se non è data e ce n'è più di una), avvia l'abbinamento,
// chiede il codice mostrato sul display, aspetta la conferma fisica sul display. interrupt (Ctrl-C)
// annulla l'abbinamento in corso.
func Pair(c config.Config, board string, in io.Reader, out io.Writer, interrupt <-chan os.Signal) error {
	ctx := context.Background()
	st, err := getAPIStatus(ctx, c)
	if err != nil {
		return err
	}
	input := lines(in)
	if board == "" {
		switch len(st.Displays) {
		case 0:
			return errors.New("nessun display collegato: accendi il display e aspetta che si colleghi al servizio")
		case 1:
			board = st.Displays[0].Board
		default:
			fmt.Fprintln(out, "Display collegati:")
			for i, d := range st.Displays {
				fmt.Fprintf(out, "  %d) %s  %s  %s\n", i+1, d.Board, d.IP, PairText(d.Pair, d.PairReason))
			}
			for board == "" {
				fmt.Fprintf(out, "Quale abbino? [1-%d]: ", len(st.Displays))
				var line string
				var ok bool
				select {
				case line, ok = <-input:
				case <-interrupt:
					fmt.Fprintln(out)
					return ErrPairFailed
				}
				if !ok {
					return errors.New("nessuna scelta")
				}
				line = strings.TrimSpace(line)
				if n, err := strconv.Atoi(line); err == nil && n >= 1 && n <= len(st.Displays) {
					board = st.Displays[n-1].Board
				} else if d := findDisplay(st, line); d != nil {
					board = d.Board
				}
			}
		}
	}
	d := findDisplay(st, board)
	if d == nil {
		return fmt.Errorf("il display %s non è collegato", board)
	}
	fmt.Fprintf(out, "Abbino %s (%s).\n", d.Board, d.IP)
	if d.Paired || d.Pair == "redo" {
		fmt.Fprintln(out, "Un nuovo abbinamento sostituisce quello di prima, sul PC e sul display.")
	}

	bpath := "/api/displays/" + url.PathEscape(board) + "/pair/"
	code, m, err := apiCall(ctx, c, http.MethodPost, bpath+"start", map[string]any{})
	if err != nil {
		return err
	}
	if code != http.StatusAccepted {
		return errors.New(errText(m, code))
	}
	id := ""
	if p, ok := m["pairing"].(map[string]any); ok {
		id, _ = p["id"].(string)
	}
	finished := false
	defer func() {
		// Uscita senza abbinamento (errore, Ctrl-C, display sparito): si annulla quello in corso.
		// Se non è più in corso il servizio risponde 409, e va bene così.
		if !finished {
			cctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
			defer cancel()
			_, _, _ = apiCall(cctx, c, http.MethodPost, bpath+"cancel", map[string]any{})
		}
	}()

	// stage legge l'abbinamento dal servizio: nil se il display non c'è più.
	stage := func() (*display.PairingStatus, *display.DisplayStatus, error) {
		st, err := getAPIStatus(ctx, c)
		if err != nil {
			return nil, nil, err
		}
		d := findDisplay(st, board)
		if d == nil {
			return nil, nil, nil
		}
		if d.Pairing == nil || d.Pairing.ID != id {
			return nil, d, nil
		}
		return d.Pairing, d, nil
	}
	failed := func(reason string) error {
		fmt.Fprintf(out, "Abbinamento annullato: %s. Nessuna chiave salvata.\n", AbortText(reason))
		return ErrPairFailed
	}
	// wait aspetta che la fase non sia più from; restituisce la nuova.
	tick := time.NewTicker(PairPoll)
	defer tick.Stop()
	wait := func(from string) (*display.PairingStatus, error) {
		for {
			p, _, err := stage()
			if err != nil {
				return nil, err
			}
			if p == nil {
				return nil, failed("connessione")
			}
			if p.Stage != from {
				return p, nil
			}
			select {
			case <-interrupt:
				fmt.Fprintln(out)
				return nil, failed(display.AbortCancel)
			case <-tick.C:
			}
		}
	}

	fmt.Fprintln(out, "Aspetto il display…")
	p, err := wait(display.StageBegin)
	if err != nil {
		return err
	}
	if p.Stage == display.StageAborted {
		return failed(p.Reason)
	}
	until := time.UnixMilli(p.Until).Local().Format("15:04")
	prompt := true
	for p.Stage == display.StageCode {
		if prompt {
			fmt.Fprintf(out, "Leggi il codice sul display e scrivilo qui: ")
			prompt = false
		}
		var line string
		var ok bool
		select {
		case line, ok = <-input:
		case <-interrupt:
			fmt.Fprintln(out)
			return failed(display.AbortCancel)
		case <-tick.C:
			// il display può annullare o il tempo scadere mentre si scrive
			if q, _, err := stage(); err == nil && (q == nil || q.Stage == display.StageAborted) {
				fmt.Fprintln(out)
				if q == nil {
					return failed("connessione")
				}
				return failed(q.Reason)
			}
			continue
		}
		if !ok {
			fmt.Fprintln(out)
			return failed(display.AbortCancel)
		}
		code, m, err := apiCall(ctx, c, http.MethodPost, bpath+"code", map[string]any{"code": line})
		if err != nil {
			return err
		}
		switch {
		case code == http.StatusAccepted:
			p = &display.PairingStatus{Stage: display.StageConfirm}
		case code == http.StatusBadRequest && m["aborted"] == true:
			return failed(display.AbortCode)
		case code == http.StatusBadRequest:
			fmt.Fprintln(out, "Servono le 6 cifre che vedi sul display.")
			prompt = true
		default:
			return errors.New(errText(m, code))
		}
	}
	fmt.Fprintf(out, "Codice giusto. Ora conferma sul display: premi due volte il tasto (entro le %s).\n", until)
	p, err = wait(display.StageConfirm)
	if err != nil {
		return err
	}
	if p.Stage != display.StageDone {
		return failed(p.Reason)
	}
	finished = true
	// La sfida parte subito: si aspetta l'autenticazione per qualche secondo.
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		_, d, err := stage()
		if err == nil && d != nil && d.Pair == "ok" {
			fmt.Fprintf(out, "Abbinato: %s. Connessione autenticata.\n", board)
			return nil
		}
		time.Sleep(PairPoll)
	}
	fmt.Fprintf(out, "Abbinato: %s. L'autenticazione non è ancora arrivata: controlla con `calamaio status`.\n", board)
	return nil
}

// Unpair dimentica l'abbinamento di board. Con il servizio spento toglie la chiave da config.json.
func Unpair(c config.Config, board string, out io.Writer) error {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	code, m, err := apiCall(ctx, c, http.MethodPost, "/api/displays/"+url.PathEscape(board)+"/unpair", map[string]any{})
	if err != nil {
		ks, kerr := config.NewKeyStore()
		if kerr != nil {
			return kerr
		}
		found, derr := ks.Delete(board)
		if derr != nil {
			return derr
		}
		if !found {
			fmt.Fprintf(out, "%s non era abbinato.\n", board)
			return nil
		}
		fmt.Fprintf(out, "Servizio spento: chiave di %s tolta da config.json.\n", board)
		return nil
	}
	if code != http.StatusOK {
		return errors.New(errText(m, code))
	}
	if m["unpaired"] != true {
		fmt.Fprintf(out, "%s non era abbinato.\n", board)
		return nil
	}
	fmt.Fprintf(out, "Abbinamento di %s dimenticato. Sul display comparirà «Abbinamento: da rifare»; per toglierlo anche lì: Menu › Sicurezza › Dimentica abbinamento.\n", board)
	return nil
}
