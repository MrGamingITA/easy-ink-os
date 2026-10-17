package client

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"calamaio/internal/config"
	"calamaio/internal/display"
	"calamaio/internal/ota"
)

// `calamaio ota keygen`, `calamaio ota push` e `calamaio logs` (docs/OTA.md §2, §4, §5). Parlano con il
// servizio attraverso la stessa API della webapp.

// OtaPoll: ogni quanto `calamaio ota push` rilegge lo stato.
var OtaPoll = 250 * time.Millisecond

// ErrOtaFailed: aggiornamento non riuscito, rifiutato o annullato (il motivo è già stato scritto).
var ErrOtaFailed = errors.New("aggiornamento non riuscito")

// OtaKeygen crea la chiave di firma in path.
func OtaKeygen(path string, force bool, out io.Writer) error {
	k, err := ota.Generate(path, force, time.Now())
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "Chiave di firma degli aggiornamenti creata: %s (permessi 0600, non va mai nel repository).\n", path)
	fmt.Fprintf(out, "Chiave pubblica: %s\n", k.PubHex())
	if force {
		fmt.Fprintln(out, "Attenzione: le schede che conoscono la chiave di prima rifiutano gli aggiornamenti firmati con questa finché non le riabbini (calamaio pair).")
	} else {
		fmt.Fprintln(out, "Il servizio la manda da solo a ogni scheda abbinata appena si collega (se è acceso, alla prossima connessione).")
	}
	return nil
}

// OtaPushOptions: cosa mandare e a chi.
type OtaPushOptions struct {
	Board   string
	Path    string // firmware.bin o cartella della build; vuoto = il pacchetto firmware del servizio
	Version string // --version
	Device  string // --device
	Yes     bool   // niente domanda di conferma
}

type otaAPIStatus struct {
	Displays []display.DisplayStatus `json:"displays"`
	OTA      *struct {
		Key      bool              `json:"key"`
		KeyError string            `json:"keyError"`
		Jobs     []display.OtaView `json:"jobs"`
	} `json:"ota"`
}

func getOtaStatus(ctx context.Context, c config.Config) (*otaAPIStatus, error) {
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, baseURL(c)+"/api/status", nil)
	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("il servizio non risponde su %s (è avviato? `calamaio serve`): %w", baseURL(c), err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET /api/status: stato %d", resp.StatusCode)
	}
	var st otaAPIStatus
	if err := json.NewDecoder(io.LimitReader(resp.Body, 8<<20)).Decode(&st); err != nil {
		return nil, err
	}
	return &st, nil
}

// chooseBoard: la scheda indicata, l'unica collegata, o una scelta dall'elenco.
func chooseBoard(list []display.DisplayStatus, board string, input <-chan string, out io.Writer, interrupt <-chan os.Signal) (*display.DisplayStatus, error) {
	if board != "" {
		for i := range list {
			if list[i].Board == board {
				return &list[i], nil
			}
		}
		return nil, fmt.Errorf("la scheda %s non è collegata", board)
	}
	switch len(list) {
	case 0:
		return nil, errors.New("nessuna scheda collegata: accendi il display e aspetta che si colleghi al servizio")
	case 1:
		return &list[0], nil
	}
	fmt.Fprintln(out, "Schede collegate:")
	for i, d := range list {
		fmt.Fprintf(out, "  %d) %s  fw %s  %s\n", i+1, d.Board, d.FW, d.IP)
	}
	for {
		fmt.Fprintf(out, "Quale? [1-%d]: ", len(list))
		var line string
		var ok bool
		select {
		case line, ok = <-input:
		case <-interrupt:
			fmt.Fprintln(out)
			return nil, errors.New("annullato")
		}
		if !ok {
			return nil, errors.New("nessuna scelta")
		}
		line = strings.TrimSpace(line)
		if n, err := strconv.Atoi(line); err == nil && n >= 1 && n <= len(list) {
			return &list[n-1], nil
		}
		for i := range list {
			if list[i].Board == line {
				return &list[i], nil
			}
		}
	}
}

func powerText(d *display.DisplayStatus) string {
	if !d.Status {
		return "alimentazione sconosciuta (il firmware non manda `status`)"
	}
	var parts []string
	if d.Batt != nil {
		parts = append(parts, fmt.Sprintf("batteria %d%%", *d.Batt))
	} else {
		parts = append(parts, "batteria ?")
	}
	switch {
	case d.USB == nil:
		parts = append(parts, "cavo ?")
	case *d.USB:
		parts = append(parts, "cavo USB collegato")
	default:
		parts = append(parts, "senza cavo")
	}
	return strings.Join(parts, ", ")
}

// OtaPush manda un aggiornamento e ne segue l'avanzamento. interrupt (Ctrl-C) annulla prima di «scrivo».
func OtaPush(c config.Config, o OtaPushOptions, in io.Reader, out io.Writer, interrupt <-chan os.Signal) error {
	ctx := context.Background()
	st, err := getOtaStatus(ctx, c)
	if err != nil {
		return err
	}
	if st.OTA == nil {
		return errors.New("questo servizio non fa aggiornamenti remoti: aggiornalo (calamaio install o riavvia calamaio serve)")
	}
	if !st.OTA.Key {
		return errors.New(st.OTA.KeyError)
	}
	input := lines(in)
	d, err := chooseBoard(st.Displays, o.Board, input, out, interrupt)
	if err != nil {
		return err
	}
	if !d.Authed {
		return fmt.Errorf("la scheda %s non è abbinata e autenticata: abbinala prima (calamaio pair)", d.Board)
	}
	if d.OTA != nil && d.OTA.Active {
		return fmt.Errorf("c'è già un aggiornamento in corso per %s (fase %s)", d.Board, d.OTA.Stage)
	}

	var (
		build   *ota.Build
		target  string
		summary string
	)
	if o.Path != "" {
		build, err = ota.Resolve(ota.ResolveOptions{Path: o.Path, Version: o.Version, Device: o.Device, DeviceHint: d.Device})
		if err != nil {
			return err
		}
		target = build.Version
		summary = fmt.Sprintf("%s, %s (versione da %s, profilo %s da %s)", build.Path, sizeText(int64(len(build.Bin))),
			build.VersionSrc, build.Device, build.DeviceSrc)
		if build.ELF != "" {
			if dir, err := config.DataDir(); err == nil {
				_, _ = ota.ArchiveELF(filepath.Join(dir, "elf"), build.ELF, build.Version, build.Info.ElfSHA256)
			}
		} else {
			summary += "; " + build.ELFNote + ": i crash di questa versione non si potranno tradurre"
		}
	} else {
		var fw struct {
			Available bool   `json:"available"`
			Version   string `json:"version"`
			Dir       string `json:"dir"`
			Problem   string `json:"problem"`
		}
		code, m, err := apiCall(ctx, c, http.MethodGet, "/api/firmware", nil)
		if err != nil {
			return err
		}
		b, _ := json.Marshal(m)
		_ = json.Unmarshal(b, &fw)
		if code != http.StatusOK || !fw.Available {
			return fmt.Errorf("pacchetto firmware del servizio non disponibile (%s): indica un file o --build DIR", fw.Problem)
		}
		target = fw.Version
		summary = "pacchetto firmware del servizio, " + fw.Dir
	}
	fmt.Fprintf(out, "Aggiorno %s: %s → %s\n  %s\n  %s\n", d.Board, d.FW, target, summary, powerText(d))
	if d.OTACheck != nil {
		switch {
		case !d.OTACheck.Can && (o.Path == "" || !strings.Contains(d.OTACheck.Why, "acchetto")):
			fmt.Fprintf(out, "  Attenzione: %s\n", d.OTACheck.Why)
		case d.OTACheck.Warn != "" && o.Path == "":
			fmt.Fprintf(out, "  Nota: %s\n", d.OTACheck.Warn)
		}
	}
	if o.Path != "" {
		switch cmp := ota.CompareVersions(target, d.FW); {
		case cmp == 0:
			fmt.Fprintf(out, "  Nota: la scheda ha già la versione %s: verrà reinstallata.\n", d.FW)
		case cmp < 0:
			fmt.Fprintf(out, "  Nota: %s è più vecchia della versione della scheda (%s).\n", target, d.FW)
		}
	}
	if !o.Yes {
		fmt.Fprint(out, "Procedo? [s/N] ")
		var line string
		var ok bool
		select {
		case line, ok = <-input:
		case <-interrupt:
			fmt.Fprintln(out)
			return ErrOtaFailed
		}
		if a := strings.ToLower(strings.TrimSpace(line)); !ok || (a != "s" && a != "si" && a != "sì" && a != "y") {
			fmt.Fprintln(out, "Non mandato.")
			return ErrOtaFailed
		}
	}

	bpath := "/api/displays/" + url.PathEscape(d.Board) + "/ota"
	var code int
	var m map[string]any
	if build != nil {
		q := url.Values{"version": {build.Version}, "device": {build.Device}, "built": {strconv.FormatInt(build.Built, 10)},
			"source": {build.Path}}
		code, m, err = apiRaw(ctx, c, bpath+"/image?"+q.Encode(), build.Bin)
	} else {
		code, m, err = apiCall(ctx, c, http.MethodPost, bpath, map[string]any{})
	}
	if err != nil {
		return err
	}
	if code != http.StatusAccepted {
		return errors.New(errText(m, code))
	}
	id := ""
	if v, ok := m["ota"].(map[string]any); ok {
		id, _ = v["id"].(string)
	}
	return followOta(ctx, c, d.Board, id, target, out, interrupt)
}

// apiRaw: POST con un corpo binario (application/octet-stream).
func apiRaw(ctx context.Context, c config.Config, path string, body []byte) (int, map[string]any, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, baseURL(c)+path, bytes.NewReader(body))
	if err != nil {
		return 0, nil, err
	}
	req.Header.Set("Content-Type", "application/octet-stream")
	resp, err := httpClient.Do(req)
	if err != nil {
		return 0, nil, fmt.Errorf("il servizio non risponde su %s: %w", baseURL(c), err)
	}
	defer resp.Body.Close()
	var m map[string]any
	b, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	_ = json.Unmarshal(b, &m)
	return resp.StatusCode, m, nil
}

func sizeText(n int64) string {
	switch {
	case n >= 1<<20:
		return strings.Replace(fmt.Sprintf("%.2f MB", float64(n)/(1<<20)), ".", ",", 1)
	case n >= 1<<10:
		return strings.Replace(fmt.Sprintf("%.1f KB", float64(n)/(1<<10)), ".", ",", 1)
	}
	return fmt.Sprintf("%d B", n)
}

// StageText: le fasi dell'aggiornamento a parole.
func StageText(stage string) string {
	switch stage {
	case display.OtaOffer:
		return "offerta mandata, la scheda controlla firma e batteria"
	case display.OtaReceive:
		return "ricezione"
	case display.OtaVerify:
		return "verifica"
	case display.OtaWrite:
		return "scrittura (non si annulla più)"
	case display.OtaReboot:
		return "riavvio: aspetto che la scheda torni"
	case display.OtaDone:
		return "fatto"
	case display.OtaRejected:
		return "rifiutato"
	case display.OtaError:
		return "errore"
	case display.OtaCanceled:
		return "annullato"
	}
	return stage
}

// OtaBar: barra di testo larga w caratteri.
func OtaBar(done, total int64, w int) string {
	if total <= 0 {
		return "[" + strings.Repeat("·", w) + "]"
	}
	n := int(min(done, total) * int64(w) / total)
	return "[" + strings.Repeat("█", n) + strings.Repeat("·", w-n) + "]"
}

func followOta(ctx context.Context, c config.Config, board, id, target string, out io.Writer, interrupt <-chan os.Signal) error {
	tty := isTerminal(out)
	tick := time.NewTicker(OtaPoll)
	defer tick.Stop()
	var lastStage string
	lastPct := -1
	canceled := false
	line := func(v display.OtaView) {
		pct := 0
		if v.Total > 0 {
			pct = int(min(v.Done, v.Total) * 100 / v.Total)
		}
		if tty {
			fmt.Fprintf(out, "\r%s %3d%%  %s / %s  %-40s", OtaBar(v.Done, v.Total, 30), pct, sizeText(v.Done), sizeText(v.Total), StageText(v.Stage))
		} else if v.Stage != lastStage || pct/10 != lastPct/10 {
			fmt.Fprintf(out, "%s %3d%%  %s\n", OtaBar(v.Done, v.Total, 30), pct, StageText(v.Stage))
		}
		lastPct = pct
	}
	for {
		st, err := getOtaStatus(ctx, c)
		if err == nil && st.OTA != nil {
			var v *display.OtaView
			for i := range st.OTA.Jobs {
				if st.OTA.Jobs[i].Board == board && (id == "" || st.OTA.Jobs[i].ID == id) {
					v = &st.OTA.Jobs[i]
				}
			}
			if v == nil {
				if tty {
					fmt.Fprintln(out)
				}
				return errors.New("l'aggiornamento non risulta più al servizio (riavviato?): controlla il display e calamaio status")
			}
			if v.Stage != lastStage && tty && lastStage != "" {
				fmt.Fprintln(out)
			}
			line(*v)
			lastStage = v.Stage
			if !v.Active {
				if tty {
					fmt.Fprintln(out)
				}
				switch v.Stage {
				case display.OtaDone:
					fmt.Fprintf(out, "Aggiornato: %s ora ha la versione %s.\n", board, target)
					return nil
				case display.OtaRejected:
					fmt.Fprintf(out, "La scheda ha rifiutato l'aggiornamento: %s. Resta la versione di prima.\n", v.Error)
				case display.OtaCanceled:
					fmt.Fprintln(out, "Annullato: la scheda resta sulla versione di prima.")
				default:
					fmt.Fprintf(out, "Aggiornamento non riuscito: %s. Resta la versione di prima.\n", v.Error)
				}
				return ErrOtaFailed
			}
		}
		select {
		case <-tick.C:
		case <-interrupt:
			if canceled {
				fmt.Fprintln(out, "\nEsco: l'aggiornamento continua sulla scheda (calamaio status o la pagina Firmware dicono com'è finito).")
				return ErrOtaFailed
			}
			canceled = true
			cctx, cancel := context.WithTimeout(ctx, 3*time.Second)
			code, m, err := apiCall(cctx, c, http.MethodPost, "/api/displays/"+url.PathEscape(board)+"/ota/cancel", map[string]any{})
			cancel()
			switch {
			case err != nil:
				fmt.Fprintf(out, "\nAnnullamento non riuscito: %v\n", err)
			case code == http.StatusAccepted:
				// l'esito arriva al giro dopo («annullato»)
			default:
				fmt.Fprintf(out, "\n%s. Aspetto la fine (Ctrl-C di nuovo per uscire senza aspettare).\n", capitalize(errText(m, code)))
			}
		}
	}
}

func capitalize(s string) string {
	if s == "" {
		return s
	}
	r := []rune(s)
	return strings.ToUpper(string(r[0])) + string(r[1:])
}

func isTerminal(w io.Writer) bool {
	f, ok := w.(*os.File)
	if !ok {
		return false
	}
	st, err := f.Stat()
	return err == nil && st.Mode()&os.ModeCharDevice != 0
}

// ---------- calamaio logs ----------

// LogsOptions: scheda, diretta e durata.
type LogsOptions struct {
	Board   string
	Follow  bool
	Minutes int
}

// Logs chiede il registro alla scheda e lo stampa; il servizio lo salva in <dati>/calamaio/logs.
func Logs(c config.Config, o LogsOptions, in io.Reader, out io.Writer, interrupt <-chan os.Signal) error {
	ctx := context.Background()
	st, err := getOtaStatus(ctx, c)
	if err != nil {
		return err
	}
	d, err := chooseBoard(st.Displays, o.Board, lines(in), out, interrupt)
	if err != nil {
		return err
	}
	if !d.Authed {
		return fmt.Errorf("la scheda %s non è abbinata e autenticata: il registro si chiede solo a una scheda abbinata (calamaio pair)", d.Board)
	}
	base := "/api/displays/" + url.PathEscape(d.Board) + "/log"
	// righe già in memoria al servizio: si parte dopo l'ultima
	var after int64
	if code, m, err := apiCall(ctx, c, http.MethodGet, base, nil); err == nil && code == http.StatusOK {
		if n, ok := m["next"].(float64); ok {
			after = int64(n)
		}
	}
	code, m, err := apiCall(ctx, c, http.MethodPost, base+"/start", map[string]any{"follow": o.Follow, "minutes": o.Minutes})
	if err != nil {
		return err
	}
	if code != http.StatusAccepted {
		return errors.New(errText(m, code))
	}
	id := ""
	if v, ok := m["log"].(map[string]any); ok {
		id, _ = v["id"].(string)
	}
	if o.Follow {
		fmt.Fprintf(os.Stderr, "Registro di %s in diretta per %d minuti (Ctrl-C per fermarlo).\n", d.Board, max(o.Minutes, 1))
	}
	type resp struct {
		Lines []display.LogLine `json:"lines"`
		Next  int64             `json:"next"`
		Log   *display.LogView  `json:"log"`
	}
	stopped := false
	for {
		done := make(chan struct{})
		var r resp
		var rerr error
		go func() {
			defer close(done)
			rctx, cancel := context.WithTimeout(ctx, 30*time.Second)
			defer cancel()
			req, _ := http.NewRequestWithContext(rctx, http.MethodGet, fmt.Sprintf("%s%s?after=%d&wait=20", baseURL(c), base, after), nil)
			res, err := httpClient.Do(req)
			if err != nil {
				rerr = err
				return
			}
			defer res.Body.Close()
			rerr = json.NewDecoder(io.LimitReader(res.Body, 16<<20)).Decode(&r)
		}()
		select {
		case <-done:
		case <-interrupt:
			if !stopped {
				stopped = true
				cctx, cancel := context.WithTimeout(ctx, 3*time.Second)
				_, _, _ = apiCall(cctx, c, http.MethodPost, base+"/stop", map[string]any{})
				cancel()
			}
			<-done
		}
		if rerr != nil {
			return fmt.Errorf("il servizio non risponde: %w", rerr)
		}
		for _, l := range r.Lines {
			fmt.Fprintln(out, l.Text)
		}
		after = r.Next
		if r.Log == nil || r.Log.ID != id || !r.Log.Active {
			if r.Log != nil && r.Log.File != "" {
				fmt.Fprintf(os.Stderr, "Salvato in %s\n", r.Log.File)
			}
			return nil
		}
	}
}
