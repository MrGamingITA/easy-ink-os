package display

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/theme"
)

const themeBoard = "calamaio-7e11"

func provaPayload(t *testing.T) *ThemePayload {
	t.Helper()
	b, err := os.ReadFile(filepath.Join("..", "..", "..", "protocol", "themes", "fixtures", "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	th, c, err := theme.CompileJSON(b)
	if err != nil {
		t.Fatal(err)
	}
	return &ThemePayload{ID: th.ID, Name: th.Name, Screen: th.Screen, CRC: c.CRC, Bin: c.Bin}
}

func themeRig(t *testing.T, tune func(*Hub)) *rig {
	r := newRig(t, func(h *Hub) {
		h.ThemeAckTimeout = 2 * time.Second
		h.ThemeUseTimeout = time.Second
		if tune != nil {
			tune(h)
		}
	})
	r.pairKey(themeBoard, testKey)
	return r
}

func themeDisplay(t *testing.T, r *rig, opt fakedisplay.ThemeOptions) *fakedisplay.Display {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	opt.Enabled = true
	d, err := fakedisplay.DialWith(ctx, r.url, themeBoard, testKey, false, fakedisplay.Options{Themes: opt})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	awaitEv(t, d, "authed")
	awaitEv(t, d, "themes-sent")
	return d
}

func waitTheme(t *testing.T, r *rig, stage string) *ThemeJobView {
	t.Helper()
	deadline := time.Now().Add(5 * time.Second)
	for {
		v := r.hub.ThemeOf(themeBoard)
		if v != nil && v.Stage == stage {
			return v
		}
		if time.Now().After(deadline) {
			t.Fatalf("fase %q non raggiunta: %+v\n%s", stage, v, r.log.String())
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func TestThemeFullFlow(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	p := provaPayload(t)
	if _, err := r.hub.SendTheme(themeBoard, p); err != nil {
		t.Fatal(err)
	}
	off := awaitEv(t, d, "theme-offer")
	if off.Msg["id"] != "prova" || len(off.Msg["token"].(string)) != 32 {
		t.Fatalf("offerta: %v", off.Msg)
	}
	awaitEv(t, d, "theme-use")
	waitTheme(t, r, ThemeDone)
	// il themes che segue il theme-ack non rimanda theme-use: uno solo per operazione
	if _, err := d.Await("theme-use", 300*time.Millisecond); err == nil {
		t.Fatal("theme-use doppio")
	}
	have, cur := d.Themes()
	if cur != "prova" || len(have) != 1 || have[0].CRC != p.CRC {
		t.Fatalf("scheda: %v %q", have, cur)
	}
	// stesso tema ancora: niente download, solo theme-use (già attivo: subito fatto)
	if _, err := r.hub.SendTheme(themeBoard, p); err != nil {
		t.Fatal(err)
	}
	waitTheme(t, r, ThemeDone)
	// tema di serie
	if _, err := r.hub.SendTheme(themeBoard, nil); err != nil {
		t.Fatal(err)
	}
	awaitEv(t, d, "theme-use")
	waitTheme(t, r, ThemeDone)
	if _, cur := d.Themes(); cur != "" {
		t.Fatalf("di serie: %q", cur)
	}
	st := r.hub.Status()
	if len(st) != 1 || st[0].Themes == nil || !st[0].Themes.Supported || st[0].Themes.Cur != nil {
		t.Fatalf("stato: %+v", st[0].Themes)
	}
}

func TestThemeTokenRules(t *testing.T) {
	r := themeRig(t, func(h *Hub) { h.TokenTTL = time.Second }) // margine largo anche con -race
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{NoAck: true})
	if _, err := r.hub.SendTheme(themeBoard, provaPayload(t)); err != nil {
		t.Fatal(err)
	}
	off := awaitEv(t, d, "theme-offer")
	tok := off.Msg["token"].(string)
	base := strings.Replace(r.url, "ws://", "http://", 1)
	base = base[:strings.LastIndex(base, "/")]
	// IP sbagliato: 403, il token resta valido
	req := httptest.NewRequest(http.MethodGet, "/themes/"+tok, nil)
	req.RemoteAddr = "10.0.0.99:4321"
	req.SetPathValue("token", tok)
	rec := httptest.NewRecorder()
	r.hub.ServeTheme(rec, req)
	if rec.Code != http.StatusForbidden {
		t.Fatalf("IP sbagliato: %d", rec.Code)
	}
	if res, _ := http.Get(base + "/themes/" + strings.Repeat("b", 32)); res.StatusCode != http.StatusNotFound {
		t.Fatalf("sconosciuto: %d", res.StatusCode)
	}
	res, err := http.Get(base + "/themes/" + tok)
	if err != nil || res.StatusCode != 200 || res.ContentLength != int64(len(provaPayload(t).Bin)) {
		t.Fatalf("download: %v %d", err, res.StatusCode)
	}
	res.Body.Close()
	waitTheme(t, r, ThemeDownloaded)
	if res, _ := http.Get(base + "/themes/" + tok); res.StatusCode != http.StatusGone {
		t.Fatalf("riusato: %d", res.StatusCode)
	}
	// scaduto
	r.hub.SendTheme(themeBoard, provaPayload(t))
	off = awaitEv(t, d, "theme-offer")
	time.Sleep(1200 * time.Millisecond)
	if res, _ := http.Get(base + "/themes/" + off.Msg["token"].(string)); res.StatusCode != http.StatusGone {
		t.Fatalf("scaduto: %d", res.StatusCode)
	}
}

func TestThemeRejectedAndTimeout(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{Reject: "CRC sbagliato"})
	r.hub.SendTheme(themeBoard, provaPayload(t))
	awaitEv(t, d, "theme-offer")
	if v := waitTheme(t, r, ThemeFailed); !strings.Contains(v.Error, "CRC sbagliato") {
		t.Fatalf("%+v", v)
	}
	r2 := themeRig(t, nil)
	d2 := themeDisplay(t, r2, fakedisplay.ThemeOptions{NoAck: true})
	r2.hub.SendTheme(themeBoard, provaPayload(t))
	awaitEv(t, d2, "theme-offer")
	if v := waitTheme(t, r2, ThemeFailed); !strings.Contains(v.Error, "non ha risposto") {
		t.Fatalf("%+v", v)
	}
}

// La scheda non ha più il tema che il servizio crede di averle dato (tolto perché rovinato): al theme-use risponde
// themes senza di lui, e il servizio, visto che manca, lo riofferta invece di aspettare a vuoto.
func TestThemeUseUnknownReoffers(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	p := provaPayload(t)
	r.hub.SendTheme(themeBoard, p)
	waitTheme(t, r, ThemeDone)
	awaitEv(t, d, "theme-offer")
	d.ForgetTheme("prova")
	r.hub.SendTheme(themeBoard, nil)
	waitTheme(t, r, ThemeDone)
	r.hub.mu.Lock()
	r.hub.themes[themeBoard].have = []ThemeHave{{"prova", p.CRC}} // il servizio crede che ci sia ancora
	r.hub.mu.Unlock()
	r.hub.SendTheme(themeBoard, p)
	awaitEv(t, d, "theme-use")   // prima prova: la scheda non lo trova e risponde themes invariato
	awaitEv(t, d, "theme-offer") // allora si riofferta
	waitTheme(t, r, ThemeDone)
	if _, cur := d.Themes(); cur != "prova" {
		t.Fatalf("cur %q", cur)
	}
}

func TestThemeResumeAfterReconnect(t *testing.T) {
	r := themeRig(t, nil)
	if _, err := r.hub.SendTheme(themeBoard, provaPayload(t)); err != nil { // scheda abbinata ma non collegata
		t.Fatal(err)
	}
	waitTheme(t, r, ThemeWaiting)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{NoAck: true})
	old := awaitEv(t, d, "theme-offer").Msg["token"].(string)
	d.Close() // scollegata a metà (partita del Game Boy, Wi-Fi spento)
	waitTheme(t, r, ThemeWaiting)
	// il token della connessione chiusa non vale più
	if code := themeGet(t, r, old, "127.0.0.1:5555"); code != http.StatusNotFound {
		t.Fatalf("token della connessione chiusa: %d", code)
	}
	d2 := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	off := awaitEv(t, d2, "theme-offer")
	if len(off.Msg["token"].(string)) != 32 {
		t.Fatal(off.Msg)
	}
	waitTheme(t, r, ThemeDone)
}

// §7: offerta e theme-use solo a una scheda autenticata. Con la chiave sbagliata la scheda resta collegata ma non
// autenticata (auth-fail): il suo themes si ignora, l'operazione resta in «attesa» e non parte nessuna offerta.
func TestThemeNotAuthedWaits(t *testing.T) {
	r := themeRig(t, nil)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	wrong := append([]byte(nil), testKey...)
	wrong[0] ^= 1
	d, err := fakedisplay.DialWith(ctx, r.url, themeBoard, wrong, false, fakedisplay.Options{Themes: fakedisplay.ThemeOptions{Enabled: true}})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	awaitEv(t, d, "auth-fail")
	// una scheda che manda themes lo stesso (il firmware non lo fa): ignorato
	if err := d.Send(`{"t":"themes","v":1,"have":[{"id":"prova","crc":1}],"cur":"prova"}`); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(3 * time.Second)
	for !strings.Contains(r.log.String(), "themes da una connessione non autenticata") {
		if time.Now().After(deadline) {
			t.Fatalf("themes non ignorato:\n%s", r.log.String())
		}
		time.Sleep(10 * time.Millisecond)
	}
	if _, err := r.hub.SendTheme(themeBoard, provaPayload(t)); err != nil {
		t.Fatal(err)
	}
	waitTheme(t, r, ThemeWaiting)
	if _, err := d.Await("theme-offer", 500*time.Millisecond); err == nil {
		t.Fatal("offerta a una scheda non autenticata")
	}
	if v := r.hub.ThemeOf(themeBoard); v == nil || v.Stage != ThemeWaiting {
		t.Fatalf("fase: %+v", v)
	}
	st := r.hub.Status()
	if len(st) != 1 || st[0].Themes == nil || st[0].Themes.Supported || st[0].Themes.Cur != nil || len(st[0].Themes.Have) != 0 {
		t.Fatalf("stato: %+v", st[0].Themes)
	}
}

func TestThemeScreenMismatch(t *testing.T) {
	r := themeRig(t, nil)
	themeDisplay(t, r, fakedisplay.ThemeOptions{})
	p := provaPayload(t)
	p.Screen = "800x480"
	if _, err := r.hub.SendTheme(themeBoard, p); err == nil || !strings.Contains(err.Error(), "250×122") {
		t.Fatalf("schermo: %v", err)
	}
	if _, err := r.hub.SendTheme("calamaio-0000", provaPayload(t)); err != ErrNotConnected {
		t.Fatalf("scheda sconosciuta: %v", err)
	}
}

// themeGet chiede /themes/<tok> all'hub come se arrivasse da addr; restituisce il codice HTTP.
func themeGet(t *testing.T, r *rig, tok, addr string) int {
	t.Helper()
	req := httptest.NewRequest(http.MethodGet, "/themes/"+tok, nil)
	req.RemoteAddr = addr
	req.SetPathValue("token", tok)
	rec := httptest.NewRecorder()
	r.hub.ServeTheme(rec, req)
	return rec.Code
}

// Token malformati: sempre 404, anche se somigliano a uno vero.
func TestThemeTokenMalformed(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{NoAck: true})
	r.hub.SendTheme(themeBoard, provaPayload(t))
	tok := awaitEv(t, d, "theme-offer").Msg["token"].(string)
	for _, bad := range []string{"", tok[:31], tok + "0", strings.ToUpper(tok), "../" + tok[3:], strings.Repeat("z", 32)} {
		if code := themeGet(t, r, bad, "127.0.0.1:5555"); code != http.StatusNotFound {
			t.Fatalf("%q: %d", bad, code)
		}
	}
	// IP sbagliato più volte: 403, poi l'IP giusto scarica ancora
	for range 3 {
		if code := themeGet(t, r, tok, "192.168.1.50:80"); code != http.StatusForbidden {
			t.Fatalf("IP sbagliato: %d", code)
		}
	}
	if code := themeGet(t, r, tok, "127.0.0.1:5555"); code != http.StatusOK {
		t.Fatalf("IP giusto: %d", code)
	}
	if code := themeGet(t, r, tok, "127.0.0.1:5555"); code != http.StatusGone {
		t.Fatalf("secondo download: %d", code)
	}
}

// Una seconda connessione della stessa scheda sostituisce quella che aveva l'offerta: l'offerta vecchia muore
// (token compreso) e la nuova connessione autenticata ne riceve una nuova.
func TestThemeReplacedConnReoffers(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{NoAck: true})
	r.hub.SendTheme(themeBoard, provaPayload(t))
	old := awaitEv(t, d, "theme-offer").Msg["token"].(string)
	d2 := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	tok := awaitEv(t, d2, "theme-offer").Msg["token"].(string)
	if tok == old {
		t.Fatal("stesso token sulla connessione nuova")
	}
	waitTheme(t, r, ThemeDone)
	if code := themeGet(t, r, old, "127.0.0.1:5555"); code != http.StatusNotFound {
		t.Fatalf("token della connessione sostituita: %d", code)
	}
}

// theme-ack senza un'offerta in corso (o per un altro tema) si ignora: niente tema aggiunto, niente theme-use.
func TestThemeAckWithoutOffer(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{})
	if err := d.Send(`{"t":"theme-ack","v":1,"id":"prova","ok":true}`); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(3 * time.Second)
	for !strings.Contains(r.log.String(), "senza un'offerta in corso") {
		if time.Now().After(deadline) {
			t.Fatalf("theme-ack non ignorato:\n%s", r.log.String())
		}
		time.Sleep(10 * time.Millisecond)
	}
	if st := r.hub.Status(); len(st[0].Themes.Have) != 0 {
		t.Fatalf("have: %+v", st[0].Themes.Have)
	}
}

// Scheda che a ogni theme-use risponde themes senza il tema: le riofferte finiscono dopo 2, con un errore.
func TestThemeReofferCapped(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{ForgetOnUse: true})
	r.hub.SendTheme(themeBoard, provaPayload(t))
	v := waitTheme(t, r, ThemeFailed)
	if !strings.Contains(v.Error, "non ha attivato il tema") {
		t.Fatalf("%+v", v)
	}
	offers := 0
	for _, e := range d.Log() {
		if e.Type == "theme-offer" {
			offers++
		}
	}
	if offers != 3 { // la prima offerta più 2 riofferte
		t.Fatalf("offerte: %d", offers)
	}
	d.Drain() // gli eventi delle 3 offerte già contate
	if _, err := d.Await("theme-offer", 500*time.Millisecond); err == nil {
		t.Fatal("riofferta dopo l'errore")
	}
}

// manualThemes: scheda autenticata che non conosce i temi nella scheda finta: i themes e i theme-ack li manda il test.
func manualThemes(t *testing.T, r *rig) *fakedisplay.Display {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.DialWith(ctx, r.url, themeBoard, testKey, false, fakedisplay.Options{})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	awaitEv(t, d, "authed")
	return d
}

func waitLog(t *testing.T, r *rig, s string, n int) {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for strings.Count(r.log.String(), s) < n {
		if time.Now().After(deadline) {
			t.Fatalf("manca %q (×%d):\n%s", s, n, r.log.String())
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// themes senza have o cur, con campi in più o malformati si scarta tutto, come fa lo schema: un {"t":"themes"}
// nudo non deve chiudere come «fatto» il ritorno al tema di serie.
func TestThemesStrictParse(t *testing.T) {
	r := themeRig(t, nil)
	d := manualThemes(t, r)
	if err := d.Send(`{"t":"themes","v":1,"have":[{"id":"prova","crc":1}],"cur":"prova"}`); err != nil {
		t.Fatal(err)
	}
	if _, err := r.hub.SendTheme(themeBoard, nil); err != nil {
		t.Fatal(err)
	}
	awaitEv(t, d, "theme-use")
	bad := []string{
		`{"t":"themes","v":1}`,
		`{"t":"themes","v":1,"have":[]}`,
		`{"t":"themes","v":1,"cur":null}`,
		`{"t":"themes","v":1,"have":null,"cur":null}`,
		`{"t":"themes","v":1,"have":[],"cur":null,"extra":1}`,
		`{"t":"themes","v":1,"have":[{"id":"prova"}],"cur":null}`,
		`{"t":"themes","v":1,"have":[{"crc":1}],"cur":null}`,
		`{"t":"themes","v":1,"have":[{"id":"prova","crc":1,"x":2}],"cur":null}`,
		`{"t":"themes","v":1,"have":[{"id":"prova","crc":-1}],"cur":null}`,
		`{"t":"themes","v":1,"have":[{"id":"prova","crc":4294967296}],"cur":null}`,
		`{"t":"themes","v":1,"have":[],"cur":"Prova"}`,
		`{"t":"themes","v":1,"have":{},"cur":null}`,
	}
	for _, s := range bad {
		if err := d.Send(s); err != nil {
			t.Fatal(err)
		}
	}
	waitLog(t, r, "themes non valido", len(bad))
	if v := r.hub.ThemeOf(themeBoard); v.Stage != ThemeUsing {
		t.Fatalf("un themes non valido ha cambiato l'operazione: %+v", v)
	}
	// quello buono chiude
	if err := d.Send(`{"t":"themes","v":1,"have":[{"id":"prova","crc":1}],"cur":null}`); err != nil {
		t.Fatal(err)
	}
	waitTheme(t, r, ThemeDone)
}

// Durante un'offerta vera: un theme-ack per un altro tema si ignora; un secondo theme-ack per lo stesso tema
// (ripetuto) si ignora e non fa partire un secondo theme-use.
func TestThemeAckOtherIDAndRepeated(t *testing.T) {
	r := themeRig(t, nil)
	d := manualThemes(t, r)
	if err := d.Send(`{"t":"themes","v":1,"have":[],"cur":null}`); err != nil {
		t.Fatal(err)
	}
	r.hub.SendTheme(themeBoard, provaPayload(t))
	awaitEv(t, d, "theme-offer")
	waitTheme(t, r, ThemeOffered)
	if err := d.Send(`{"t":"theme-ack","v":1,"id":"altro","ok":true}`); err != nil {
		t.Fatal(err)
	}
	waitLog(t, r, "senza un'offerta in corso", 1)
	if v := r.hub.ThemeOf(themeBoard); v.Stage != ThemeOffered {
		t.Fatalf("ack di un altro tema: %+v", v)
	}
	for range 2 {
		if err := d.Send(`{"t":"theme-ack","v":1,"id":"prova","ok":true}`); err != nil {
			t.Fatal(err)
		}
	}
	waitLog(t, r, "senza un'offerta in corso", 2)
	waitTheme(t, r, ThemeUsing)
	awaitEv(t, d, "theme-use")
	if _, err := d.Await("theme-use", 300*time.Millisecond); err == nil {
		t.Fatal("theme-use doppio")
	}
	if st := r.hub.Status(); len(st[0].Themes.Have) != 1 {
		t.Fatalf("have: %+v", st[0].Themes.Have)
	}
}

// M3: HEAD non consuma il token né segna il tema come scaricato.
func TestThemeHeadKeepsToken(t *testing.T) {
	r := themeRig(t, nil)
	d := themeDisplay(t, r, fakedisplay.ThemeOptions{NoAck: true})
	if _, err := r.hub.SendTheme(themeBoard, provaPayload(t)); err != nil {
		t.Fatal(err)
	}
	tok := awaitEv(t, d, "theme-offer").Msg["token"].(string)
	base := strings.Replace(r.url, "ws://", "http://", 1)
	base = base[:strings.LastIndex(base, "/")]
	res, err := http.Head(base + "/themes/" + tok)
	if err != nil || res.StatusCode != 200 {
		t.Fatalf("HEAD: %v %v", err, res)
	}
	res.Body.Close()
	if strings.Contains(r.log.String(), "scaricato") {
		t.Fatalf("HEAD registrato come download:\n%s", r.log.String())
	}
	res, err = http.Get(base + "/themes/" + tok)
	if err != nil || res.StatusCode != 200 {
		t.Fatalf("GET dopo HEAD: %v %v", err, res)
	}
	res.Body.Close()
	waitTheme(t, r, ThemeDownloaded)
}
