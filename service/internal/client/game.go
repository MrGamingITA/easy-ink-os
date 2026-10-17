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
	"strings"
	"time"

	"calamaio/internal/accessory"
	"calamaio/internal/config"
	"calamaio/internal/gamesvc"
)

// Comandi `calamaio game …` e `calamaio accessory …` (gioco della mascotte). Con il servizio acceso
// passano dalla API locale (display e webapp si aggiornano subito); con il servizio spento lavorano
// su <dati>/calamaio/game.json e <dati>/calamaio/accessories con le stesse regole (gamesvc).

const gameUsage = `uso:
  calamaio game status [--json]          livello, XP, energia, traguardi, accessorio indossato
  calamaio game on | off                 accende o spegne il gioco della mascotte (i progressi restano)
  calamaio game special later | decline  risposta alla proposta dell'accessorio speciale
                                         (la usa Claude: «Più tardi» o «No»)
`

const accessoryUsage = `uso:
  calamaio accessory list [--json]                    accessori: sbloccati, indossato, bloccati
  calamaio accessory add <file> [--special] [--replace]
                                                      aggiunge un accessorio alla tua libreria; con
                                                      --special (accessorio speciale creato da Claude,
                                                      "special": true nel file) lo sblocca e lo indossa
  calamaio accessory wear <id> | none                 indossa un accessorio sbloccato, o nessuno
  calamaio accessory show <id | file>                 anteprima nel terminale
`

// GameIO: dove leggono e scrivono i comandi (test).
type GameIO struct {
	Out, Err io.Writer
	// Local: gioco da usare col servizio spento (nil = quello dell'utente in <dati>/calamaio).
	Local *gamesvc.Service
}

func (o GameIO) local() (*gamesvc.Service, error) {
	if o.Local != nil {
		return o.Local, nil
	}
	d, err := config.DataDir()
	if err != nil {
		return nil, err
	}
	return gamesvc.Open(filepath.Join(d, "game.json"), accessory.NewLibrary(filepath.Join(d, "accessories")), nil, nil), nil
}

func noteGameLocal(o GameIO) {
	fmt.Fprintln(o.Err, "(servizio Calamaio spento o senza gioco: salvato nei file; display e webapp lo vedranno al prossimo avvio del servizio)")
}

// gameAPI: come avatarAPI, per le rotte del gioco.
func gameAPI(c config.Config, method, path, ctype string, body []byte, out any) error {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	var rd io.Reader
	if body != nil {
		rd = bytes.NewReader(body)
	}
	req, err := http.NewRequestWithContext(ctx, method, baseURL(c)+path, rd)
	if err != nil {
		return err
	}
	if body != nil {
		req.Header.Set("Content-Type", ctype)
	}
	resp, err := httpClient.Do(req)
	if err != nil {
		return errNoService
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(io.LimitReader(resp.Body, 4<<20))
	if resp.StatusCode == http.StatusNotFound &&
		(strings.Contains(string(b), "API sconosciuta") || strings.Contains(string(b), "gioco non disponibile")) {
		return errNoService
	}
	if resp.StatusCode/100 != 2 {
		var e struct {
			Error    string   `json:"error"`
			Errors   []string `json:"errors"`
			Warnings []string `json:"warnings"`
		}
		_ = json.Unmarshal(b, &e)
		if e.Error == "" {
			e.Error = fmt.Sprintf("stato %d", resp.StatusCode)
		}
		return &apiError{Status: resp.StatusCode, Msg: e.Error, Errors: e.Errors, Warnings: e.Warnings}
	}
	if out != nil {
		return json.Unmarshal(b, out)
	}
	return nil
}

func runCmd(err error, o GameIO, usage string) int {
	var ue usageError
	switch {
	case errors.As(err, &ue):
		fmt.Fprintln(o.Err, "calamaio:", err)
		fmt.Fprint(o.Err, usage)
		return 2
	case err != nil:
		fmt.Fprintln(o.Err, "calamaio:", err)
		var ae *apiError
		if errors.As(err, &ae) {
			for _, e := range ae.Errors {
				fmt.Fprintln(o.Err, "  ✗", e)
			}
			for _, w := range ae.Warnings {
				fmt.Fprintln(o.Err, "  ! ", w)
			}
		}
		return 1
	}
	return 0
}

// Game esegue `calamaio game …`.
func Game(c config.Config, args []string, o GameIO) int {
	if len(args) == 0 {
		fmt.Fprint(o.Err, gameUsage)
		return 2
	}
	sub, rest := args[0], args[1:]
	var err error
	switch sub {
	case "status":
		err = gameStatus(c, rest, o)
	case "on", "off":
		if len(rest) > 0 {
			err = usageError{sub + " non vuole argomenti"}
			break
		}
		err = gameOn(c, sub == "on", o)
	case "special":
		if len(rest) != 1 || (rest[0] != gamesvc.AnswerLater && rest[0] != gamesvc.AnswerDecline) {
			err = usageError{"special vuole later oppure decline"}
			break
		}
		err = gameSpecial(c, rest[0], o)
	case "help", "-h", "--help":
		fmt.Fprint(o.Out, gameUsage)
		return 0
	default:
		fmt.Fprint(o.Err, gameUsage)
		return 2
	}
	return runCmd(err, o, gameUsage)
}

func gameStatus(c config.Config, args []string, o GameIO) error {
	a, err := parseAvatarArgs(args, nil, []string{"json"})
	if err != nil || len(a.pos) > 0 {
		return usageError{"status vuole solo --json"}
	}
	var st gamesvc.Status
	err = gameAPI(c, http.MethodGet, "/api/game?days=7", "", nil, &st)
	if errors.Is(err, errNoService) {
		g, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		st, err = g.Status(7), nil
		fmt.Fprintln(o.Err, "(servizio Calamaio spento o più vecchio del gioco: stato letto dal file)")
	}
	if err != nil {
		return err
	}
	if a.bools["json"] {
		enc := json.NewEncoder(o.Out)
		enc.SetIndent("", "  ")
		return enc.Encode(st)
	}
	fmt.Fprint(o.Out, GameText(st))
	return nil
}

// GameText: il riepilogo di `calamaio game status`.
func GameText(st gamesvc.Status) string {
	var b strings.Builder
	onoff := "spento (i progressi restano; accendilo con `calamaio game on`)"
	if st.On {
		onoff = "acceso"
	}
	fmt.Fprintf(&b, "Gioco della mascotte: %s\n", onoff)
	fmt.Fprintf(&b, "Livello %d · %d XP (%d/%d verso il livello %d)\n", st.Level, st.XP, st.XP-st.XPLevelStart, st.XPNext-st.XPLevelStart, st.Level+1)
	tired := ""
	if st.Tired {
		tired = " · stanca: fai una pausa di almeno 10 minuti"
	}
	fmt.Fprintf(&b, "Energia %d%% · serie %d giorni · oggi %d/%d XP%s\n", st.Energy, st.Streak, st.TodayXP, st.DailyCap, tired)
	wear := "nessuno"
	for _, a := range st.Accessories {
		if a.Worn {
			wear = fmt.Sprintf("%s (%s)", a.Name, a.ID)
		}
	}
	fmt.Fprintf(&b, "Accessorio: %s · sbloccati %d di %d\n", wear, len(st.Unlocked), len(st.Accessories))
	done := 0
	var last *gamesvc.Achievement
	for i, a := range st.Achievements {
		if a.At != nil {
			done++
			if last == nil || a.At.After(*last.At) {
				last = &st.Achievements[i]
			}
		}
	}
	fmt.Fprintf(&b, "Traguardi: %d di %d", done, len(st.Achievements))
	if last != nil {
		fmt.Fprintf(&b, " (ultimo: %s, %s)", last.Title, last.At.Local().Format("02/01 15:04"))
	}
	b.WriteByte('\n')
	if st.SpecialPending != nil {
		fmt.Fprintf(&b, "Accessorio speciale disponibile (traguardo «%s»): chiedi a Claude «crea un accessorio speciale per Calamaio».\n", st.SpecialPending.Title)
	}
	return b.String()
}

func gameOn(c config.Config, on bool, o GameIO) error {
	body, _ := json.Marshal(map[string]bool{"on": on})
	err := gameAPI(c, http.MethodPost, "/api/game", "application/json", body, nil)
	if errors.Is(err, errNoService) {
		g, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		g.SetOn(on)
		if err = g.Save(); err == nil {
			noteGameLocal(o)
		}
	}
	if err != nil {
		return err
	}
	if on {
		fmt.Fprintln(o.Out, "Gioco della mascotte acceso.")
	} else {
		fmt.Fprintln(o.Out, "Gioco della mascotte spento (i progressi restano salvati).")
	}
	return nil
}

func gameSpecial(c config.Config, answer string, o GameIO) error {
	body, _ := json.Marshal(map[string]string{"answer": answer})
	err := gameAPI(c, http.MethodPost, "/api/game/special", "application/json", body, nil)
	if errors.Is(err, errNoService) {
		g, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		if err = g.SpecialAnswer(answer); err == nil {
			noteGameLocal(o)
		}
	}
	var ae *apiError
	if errors.As(err, &ae) && ae.Status == http.StatusConflict || errors.Is(err, gamesvc.ErrNoSpecial) {
		return errors.New("nessun accessorio speciale in attesa: niente da registrare")
	}
	if err != nil {
		return err
	}
	if answer == gamesvc.AnswerLater {
		fmt.Fprintln(o.Out, "Va bene: l'accessorio speciale resta disponibile, lo riproporrò un altro giorno.")
	} else {
		fmt.Fprintln(o.Out, "Va bene: niente accessorio speciale per questo traguardo.")
	}
	return nil
}

// Accessory esegue `calamaio accessory …`.
func Accessory(c config.Config, args []string, o GameIO) int {
	if len(args) == 0 {
		fmt.Fprint(o.Err, accessoryUsage)
		return 2
	}
	sub, rest := args[0], args[1:]
	var err error
	switch sub {
	case "list":
		err = accessoryList(c, rest, o)
	case "add":
		err = accessoryAdd(c, rest, o)
	case "wear":
		err = accessoryWear(c, rest, o)
	case "show":
		err = accessoryShow(c, rest, o)
	case "help", "-h", "--help":
		fmt.Fprint(o.Out, accessoryUsage)
		return 0
	default:
		fmt.Fprint(o.Err, accessoryUsage)
		return 2
	}
	return runCmd(err, o, accessoryUsage)
}

func statusOf(c config.Config, o GameIO) (gamesvc.Status, bool, error) {
	var st gamesvc.Status
	err := gameAPI(c, http.MethodGet, "/api/game?days=1", "", nil, &st)
	if errors.Is(err, errNoService) {
		g, lerr := o.local()
		if lerr != nil {
			return st, true, lerr
		}
		return g.Status(1), true, nil
	}
	return st, false, err
}

func accessoryList(c config.Config, args []string, o GameIO) error {
	a, err := parseAvatarArgs(args, nil, []string{"json"})
	if err != nil || len(a.pos) > 0 {
		return usageError{"list vuole solo --json"}
	}
	st, local, err := statusOf(c, o)
	if err != nil {
		return err
	}
	if a.bools["json"] {
		enc := json.NewEncoder(o.Out)
		enc.SetIndent("", "  ")
		return enc.Encode(map[string]any{"accessories": st.Accessories, "problems": st.Problems, "on": st.On})
	}
	if local {
		fmt.Fprintln(o.Err, "(servizio Calamaio spento o più vecchio del gioco: libreria letta dai file)")
	}
	for _, x := range st.Accessories {
		state := "bloccato"
		switch {
		case x.Worn:
			state = "indossato"
		case x.Unlocked:
			state = "sbloccato"
		}
		tags := ""
		if x.Source == accessory.SourceUser {
			tags += " [utente]"
		}
		if x.Special {
			tags += " [speciale]"
		}
		fmt.Fprintf(o.Out, "%-24s %-24s %-10s%s\n", x.ID, x.Name, state, tags)
	}
	for _, p := range st.Problems {
		fmt.Fprintf(o.Out, "✗ %s: %s\n", p.File, strings.Join(p.Errors, "; "))
	}
	if !st.On {
		fmt.Fprintln(o.Out, "(gioco spento: la mascotte non indossa niente finché non lo accendi con `calamaio game on`)")
	}
	return nil
}

func accessoryAdd(c config.Config, args []string, o GameIO) error {
	a, err := parseAvatarArgs(args, nil, []string{"special", "replace"})
	if err != nil {
		return usageError{err.Error()}
	}
	if len(a.pos) != 1 {
		return usageError{"add vuole un file .accessory.json"}
	}
	raw, err := os.ReadFile(a.pos[0])
	if err != nil {
		return err
	}
	special, replace := a.bools["special"], a.bools["replace"]
	// controllo locale prima di tutto: stessi errori con il servizio acceso o spento
	r := accessory.Validate(raw)
	if !r.OK() {
		return &apiError{Msg: "accessorio non valido: " + a.pos[0], Errors: r.Errors, Warnings: r.Warnings}
	}
	if want := strings.TrimSuffix(filepath.Base(a.pos[0]), ".accessory.json"); strings.HasSuffix(a.pos[0], ".accessory.json") && want != r.Accessory.ID {
		return fmt.Errorf(`id "%s" diverso dal nome del file: il file deve chiamarsi %s.accessory.json`, r.Accessory.ID, r.Accessory.ID)
	}
	if special && !r.Accessory.Special {
		return errors.New(`--special vuole "special": true nel file`)
	}
	q := url.Values{}
	if special {
		q.Set("special", "1")
	}
	if replace {
		q.Set("replace", "1")
	}
	var res struct {
		Accessory *gamesvc.AccessoryView `json:"accessory"`
		Warnings  []string               `json:"warnings"`
		File      string                 `json:"file"`
	}
	err = gameAPI(c, http.MethodPost, "/api/accessories?"+q.Encode(), "application/json", raw, &res)
	if errors.Is(err, errNoService) {
		g, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		e, rr, aerr := g.Lib.Add(raw, replace)
		if aerr != nil {
			if errors.Is(aerr, accessory.ErrInvalid) {
				return &apiError{Msg: "accessorio non valido", Errors: rr.Errors, Warnings: rr.Warnings}
			}
			return aerr
		}
		if special {
			if aerr = g.AddSpecial(e.ID); aerr != nil {
				return aerr
			}
		}
		if aerr = g.Save(); aerr != nil {
			return aerr
		}
		res.File, res.Warnings = e.File, rr.Warnings
		noteGameLocal(o)
		err = nil
	}
	if err != nil {
		return err
	}
	fmt.Fprintf(o.Out, "Aggiunto %s «%s» alla tua libreria (%s).\n", r.Accessory.ID, r.Accessory.Name, res.File)
	for _, w := range res.Warnings {
		fmt.Fprintln(o.Out, "  ! ", w)
	}
	if special {
		fmt.Fprintln(o.Out, "Accessorio speciale sbloccato e indossato: arriva sulla scheda senza riflashare.")
	} else {
		fmt.Fprintln(o.Out, "Si sblocca salendo di livello (a ogni livello pari, dopo quelli del catalogo).")
	}
	return nil
}

func accessoryWear(c config.Config, args []string, o GameIO) error {
	if len(args) != 1 {
		return usageError{"wear vuole l'id di un accessorio sbloccato, oppure none"}
	}
	var id any = args[0]
	if args[0] == "none" || args[0] == "nessuno" {
		id = nil
	}
	body, _ := json.Marshal(map[string]any{"id": id})
	err := gameAPI(c, http.MethodPost, "/api/game/wear", "application/json", body, nil)
	if errors.Is(err, errNoService) {
		g, lerr := o.local()
		if lerr != nil {
			return lerr
		}
		s, _ := id.(string)
		if err = g.Wear(s); err == nil {
			noteGameLocal(o)
		}
	}
	if err != nil {
		return err
	}
	if id == nil {
		fmt.Fprintln(o.Out, "La mascotte non indossa più niente.")
	} else {
		fmt.Fprintf(o.Out, "La mascotte indossa %s.\n", args[0])
	}
	return nil
}

func accessoryShow(c config.Config, args []string, o GameIO) error {
	if len(args) != 1 {
		return usageError{"show vuole l'id di un accessorio (o un file .accessory.json)"}
	}
	var e *accessory.Entry
	if strings.HasSuffix(args[0], ".json") {
		raw, err := os.ReadFile(args[0])
		if err != nil {
			return err
		}
		ent, r := accessory.Load(raw, "file", args[0])
		if ent == nil {
			return &apiError{Msg: "accessorio non valido", Errors: r.Errors, Warnings: r.Warnings}
		}
		e = ent
	} else {
		g, err := o.local()
		if err != nil {
			return err
		}
		if e = g.Lib.Get(args[0]); e == nil {
			return fmt.Errorf("accessorio sconosciuto: %q (calamaio accessory list)", args[0])
		}
	}
	fmt.Fprint(o.Out, AccessoryText(e))
	return nil
}

// AccessoryText: anteprima di un accessorio nel terminale (██ inchiostro, ░░ carta, ▒▒ inverso).
func AccessoryText(e *accessory.Entry) string {
	a := e.Accessory
	var b strings.Builder
	fr := "1 fotogramma"
	if a.Frames == 2 {
		fr = "2 fotogrammi"
	}
	fmt.Fprintf(&b, "%s «%s» · %s · %d×%d, %s · ancora %s, origine (%d,%d) · CACC %d byte, crc32 %08x\n",
		a.ID, a.Name, e.Source, a.W, a.H, fr, a.Anchor, a.Origin[0], a.Origin[1], len(e.Bin), e.CRC)
	if a.Description != "" {
		b.WriteString(a.Description + "\n")
	}
	if len(a.HidePoses) > 0 {
		b.WriteString("nascosto in: " + strings.Join(a.HidePoses, ", ") + "\n")
	}
	b.WriteString("██ inchiostro · ░░ carta · ▒▒ inverso (bianco sul nero, nero sul bianco)\n\n")
	cell := map[byte]string{'.': "  ", '#': "██", '+': "░░", 'x': "▒▒"}
	for y := 0; y < a.H; y++ {
		for f := 0; f < a.Frames; f++ {
			if f > 0 {
				b.WriteString("   ")
			}
			for x := 0; x < a.W; x++ {
				b.WriteString(cell[a.Pixels[f][y][x]])
			}
		}
		b.WriteByte('\n')
	}
	return b.String()
}
