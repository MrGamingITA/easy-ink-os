package client

import (
	"bufio"
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

// `calamaio wifi list|add|forget` (docs/PROTOCOLLO.md «Reti Wi-Fi»). Parlano con il servizio attraverso
// la API della webapp; la password non è mai un argomento: si chiede nascosta dal terminale oppure, con
// --stdin, si legge dallo standard input (per gli script). Il servizio la manda alla scheda cifrata.

// ErrWifiFailed: operazione non riuscita (il motivo è già stato scritto).
var ErrWifiFailed = errors.New("operazione Wi-Fi non riuscita")

// WifiIO: ingressi e uscite dei comandi wifi.
type WifiIO struct {
	In  io.Reader // conferma (y/N) di forget
	Out io.Writer
	// Password legge la password (prompt sullo standard error se dal terminale). confirm = chiedila due
	// volte (input nascosto: un errore di battitura non si vede).
	Password func(prompt string) ([]byte, error)
	Confirm  bool // la password si chiede due volte
	Yes      bool // forget senza conferma
}

// SignalText descrive l'RSSI.
func SignalText(rssi *int) string {
	if rssi == nil {
		return "non in vista"
	}
	q := "debole"
	switch r := *rssi; {
	case r >= -55:
		q = "ottimo"
	case r >= -67:
		q = "buono"
	case r >= -75:
		q = "discreto"
	}
	return fmt.Sprintf("segnale %d dBm (%s)", *rssi, q)
}

// wifiBoard: la scheda indicata o l'unica collegata (con più schede serve --board: niente scelta
// interattiva, perché lo standard input può servire per la password).
func wifiBoard(list []display.DisplayStatus, board string) (*display.DisplayStatus, error) {
	if board == "" && len(list) > 1 {
		names := make([]string, len(list))
		for i, d := range list {
			names[i] = d.Board
		}
		return nil, fmt.Errorf("ci sono più schede collegate (%s): indica quale con --board", strings.Join(names, ", "))
	}
	return chooseBoard(list, board, nil, io.Discard, nil)
}

// WifiList stampa le reti salvate sulla scheda (ultima wifi-list arrivata al servizio); senza board e con
// più schede collegate, quelle di tutte.
func WifiList(c config.Config, board string, w WifiIO) error {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	st, err := getAPIStatus(ctx, c)
	if err != nil {
		return err
	}
	var list []display.DisplayStatus
	if board == "" && len(st.Displays) > 1 {
		list = st.Displays
	} else {
		d, err := chooseBoard(st.Displays, board, nil, io.Discard, nil)
		if err != nil {
			return err
		}
		list = []display.DisplayStatus{*d}
	}
	var errs []string
	for i := range list {
		if i > 0 {
			fmt.Fprintln(w.Out)
		}
		if err := printWifi(&list[i], w.Out); err != nil {
			errs = append(errs, err.Error())
		}
	}
	if len(errs) > 0 {
		return errors.New(strings.Join(errs, "; "))
	}
	return nil
}

func printWifi(d *display.DisplayStatus, out io.Writer) error {
	if d.Wifi == nil {
		return errors.New("il servizio in esecuzione non conosce le reti Wi-Fi (versione vecchia): riavvialo")
	}
	if d.Wifi.Networks == nil {
		return fmt.Errorf("la scheda %s non ha mandato l'elenco delle reti: serve un firmware che gestisce più reti Wi-Fi (fw attuale %s)", d.Board, d.FW)
	}
	nets := d.Wifi.Networks
	when := ""
	if d.Wifi.At > 0 {
		when = " (elenco di " + agoText(time.Since(time.UnixMilli(d.Wifi.At))) + ")"
	}
	fmt.Fprintf(out, "Reti Wi-Fi di %s%s:\n", d.Board, when)
	if len(nets) == 0 {
		fmt.Fprintln(out, "  nessuna")
	}
	width := 0
	for _, n := range nets {
		width = max(width, len([]rune(n.SSID)))
	}
	for _, n := range nets {
		mark := " "
		parts := []string{}
		if n.Current {
			mark = "*"
			parts = append(parts, "in uso")
		}
		parts = append(parts, SignalText(n.RSSI))
		if n.LastUsed != nil && *n.LastUsed > 0 && !n.Current {
			parts = append(parts, "ultimo uso "+time.UnixMilli(*n.LastUsed).Local().Format("02/01 15:04"))
		}
		pad := strings.Repeat(" ", width-len([]rune(n.SSID)))
		fmt.Fprintf(out, "  %s %s%s  %s\n", mark, n.SSID, pad, strings.Join(parts, " · "))
	}
	if p := d.Wifi.Pending; p != nil {
		fmt.Fprintf(out, "In corso: %s «%s».\n", opText(p.Op), p.SSID)
	}
	if !d.Authed {
		fmt.Fprintln(out, "Per aggiungere o dimenticare reti la scheda deve essere abbinata e autenticata (calamaio pair).")
	}
	return nil
}

func opText(op string) string {
	if op == display.WifiOpForget {
		return "dimentico"
	}
	return "aggiungo"
}

func agoText(d time.Duration) string {
	switch {
	case d < 5*time.Second:
		return "adesso"
	case d < time.Minute:
		return fmt.Sprintf("%d s fa", int(d.Seconds()))
	case d < time.Hour:
		return fmt.Sprintf("%d min fa", int(d.Minutes()))
	}
	return fmt.Sprintf("%d h fa", int(d.Hours()))
}

// wifiPost manda l'operazione e traduce la risposta.
func wifiPost(c config.Config, board, op string, body map[string]any) (int, map[string]any, error) {
	// il servizio aspetta l'ack della scheda al massimo 10 s
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	code, m, err := apiCall(ctx, c, http.MethodPost, "/api/displays/"+url.PathEscape(board)+"/wifi/"+op, body)
	if err != nil {
		return 0, nil, err
	}
	if code == http.StatusNotFound {
		if e, _ := m["error"].(string); strings.HasPrefix(e, "API sconosciuta") {
			return code, m, errors.New("il servizio in esecuzione non conosce le reti Wi-Fi (versione vecchia): riavvialo")
		}
	}
	return code, m, nil
}

// WifiAdd aggiunge (o aggiorna) una rete sulla scheda.
func WifiAdd(c config.Config, board, ssid string, w WifiIO) error {
	if err := display.ValidateWifiSSID(ssid); err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	st, err := getAPIStatus(ctx, c)
	if err != nil {
		return err
	}
	d, err := wifiBoard(st.Displays, board)
	if err != nil {
		return err
	}
	if !d.Authed {
		return fmt.Errorf("la scheda %s non è abbinata e autenticata: le reti si aggiungono dal PC solo così (calamaio pair)", d.Board)
	}
	if d.Wifi != nil && d.Wifi.Networks != nil {
		for _, n := range d.Wifi.Networks {
			if n.SSID == ssid {
				fmt.Fprintf(w.Out, "La rete «%s» è già salvata sulla scheda: ne aggiorno la password.\n", ssid)
			}
		}
	}
	pw, err := w.Password(fmt.Sprintf("Password di «%s» (non si vede mentre scrivi): ", ssid))
	if err != nil {
		return err
	}
	defer clear(pw)
	if err := display.ValidateWifiPassword(pw); err != nil {
		return err
	}
	if w.Confirm {
		again, err := w.Password("Ripeti la password: ")
		if err != nil {
			return err
		}
		same := string(again) == string(pw)
		clear(again)
		if !same {
			return errors.New("le due password non coincidono: niente mandato")
		}
	}
	fmt.Fprintf(w.Out, "Mando «%s» a %s (password cifrata con la chiave della sessione)…\n", ssid, d.Board)
	code, m, err := wifiPost(c, d.Board, "add", map[string]any{"ssid": ssid, "password": string(pw)})
	if err != nil {
		return err
	}
	if code != http.StatusOK {
		fmt.Fprintf(w.Out, "Non riuscito: %s.\n", errText(m, code))
		return ErrWifiFailed
	}
	fmt.Fprintf(w.Out, "Rete «%s» salvata su %s.\n", ssid, d.Board)
	return nil
}

// AskYes chiede una conferma (default no). y, s, sì, si, yes valgono sì.
func AskYes(in *bufio.Reader, out io.Writer, prompt string) bool {
	fmt.Fprint(out, prompt)
	line, _ := in.ReadString('\n')
	switch strings.ToLower(strings.TrimSpace(line)) {
	case "y", "yes", "s", "si", "sì":
		return true
	}
	return false
}

// WifiForget fa dimenticare una rete alla scheda, che poi si riavvia.
func WifiForget(c config.Config, board, ssid string, w WifiIO) error {
	if err := display.ValidateWifiSSID(ssid); err != nil {
		return err
	}
	in := bufio.NewReader(w.In)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	st, err := getAPIStatus(ctx, c)
	if err != nil {
		return err
	}
	d, err := wifiBoard(st.Displays, board)
	if err != nil {
		return err
	}
	if !d.Authed {
		return fmt.Errorf("la scheda %s non è abbinata e autenticata: le reti si dimenticano dal PC solo così (calamaio pair)", d.Board)
	}
	last, current := false, false
	if d.Wifi != nil && d.Wifi.Networks != nil {
		found := false
		for _, n := range d.Wifi.Networks {
			if n.SSID == ssid {
				found, current = true, n.Current
			}
		}
		if !found {
			return fmt.Errorf("la scheda %s non ha una rete salvata «%s» (calamaio wifi list)", d.Board, ssid)
		}
		last = len(d.Wifi.Networks) == 1
	}
	fmt.Fprintf(w.Out, "%s dimentica «%s» e poi si riavvia (pulizia profonda della memoria): resta scollegata per qualche secondo.\n", d.Board, ssid)
	if current && !last {
		fmt.Fprintln(w.Out, "È la rete in uso: dopo il riavvio la scheda si collega a un'altra rete salvata.")
	}
	if last {
		fmt.Fprintln(w.Out, "ATTENZIONE: è l'ultima rete salvata. Dopo il riavvio la scheda torna in configurazione e non si ricollega al PC finché non le dai una rete dal suo portale.")
	}
	if !w.Yes && !AskYes(in, w.Out, "Continuo? [y/N] ") {
		fmt.Fprintln(w.Out, "Niente fatto.")
		return ErrWifiFailed
	}
	code, m, err := wifiPost(c, d.Board, "forget", map[string]any{"ssid": ssid})
	if err != nil {
		return err
	}
	if code != http.StatusOK {
		fmt.Fprintf(w.Out, "Non riuscito: %s.\n", errText(m, code))
		return ErrWifiFailed
	}
	fmt.Fprintf(w.Out, "Rete «%s» dimenticata. La scheda si riavvia.\n", ssid)
	if m["wasLast"] == true {
		fmt.Fprintln(w.Out, "Era l'ultima rete: la scheda è in configurazione.")
	}
	return nil
}

// ReadPasswordLine legge la password dalla prima riga di r (per --stdin): toglie solo \n e \r finali.
func ReadPasswordLine(r io.Reader) ([]byte, error) {
	br := bufio.NewReaderSize(r, 256)
	var buf []byte
	for {
		b, err := br.ReadByte()
		if err != nil {
			if err == io.EOF {
				break
			}
			return nil, err
		}
		if b == '\n' {
			break
		}
		if len(buf) >= 1024 {
			clear(buf)
			return nil, errors.New("password troppo lunga")
		}
		buf = append(buf, b)
	}
	for len(buf) > 0 && buf[len(buf)-1] == '\r' {
		buf = buf[:len(buf)-1]
	}
	if len(buf) == 0 {
		return nil, errors.New("nessuna password sullo standard input")
	}
	return buf, nil
}
