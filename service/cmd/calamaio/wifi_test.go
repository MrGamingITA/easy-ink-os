package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/m5"
)

// Reti Wi-Fi dall'esterno: servizio vero su porte libere, scheda finta abbinata attraverso la API (che
// decifra la password e controlla aad e tag), `calamaio wifi` vero lanciato come processo, richieste
// HTTP come le fa la webapp. In fondo a ogni prova si cerca la password nel log del servizio.

type webReq struct {
	method, path, ctype, body string
	hdr                       map[string]string
	host                      string
}

func (s *svc) web(r webReq) (int, map[string]any) {
	s.t.Helper()
	var rd io.Reader
	if r.body != "" {
		rd = strings.NewReader(r.body)
	}
	req, _ := http.NewRequest(r.method, s.base+r.path, rd)
	if r.ctype != "" {
		req.Header.Set("Content-Type", r.ctype)
	}
	for k, v := range r.hdr {
		req.Header.Set(k, v)
	}
	if r.host != "" {
		req.Host = r.host
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		s.t.Fatal(err)
	}
	defer resp.Body.Close()
	var m map[string]any
	b, _ := io.ReadAll(resp.Body)
	_ = json.Unmarshal(b, &m)
	return resp.StatusCode, m
}

// browser: le intestazioni che manda la webapp (stessa origine).
func (s *svc) browser() map[string]string {
	return map[string]string{"Origin": s.base, "Sec-Fetch-Site": "same-origin"}
}

// noSecret: la password non c'è nel log del servizio, né in chiaro né in esadecimale.
func (s *svc) noSecret(secrets ...string) {
	s.t.Helper()
	lg := s.log.String()
	for _, p := range secrets {
		if strings.Contains(lg, p) || strings.Contains(lg, m5.Hex([]byte(p))) {
			s.t.Fatalf("password %q nel log del servizio:\n%s", p, lg)
		}
	}
	if !strings.Contains(lg, "wifi-") {
		s.t.Fatalf("il log non registra le operazioni Wi-Fi (la ricerca della password non dimostra niente):\n%s", lg)
	}
}

func wifiSvc(t *testing.T, nets ...fakedisplay.SavedNetwork) (*svc, *fakedisplay.Display) {
	t.Helper()
	s := startSvc(t, 0)
	d := s.otaDisplay(fakedisplay.Options{}, nil) // abbinata attraverso la API, autenticata
	d.SetWifi(nets...)
	waitFor(t, "wifi-list arrivata", func() bool {
		w := s.status().Wifi
		return w != nil && w.Networks != nil && len(w.Networks) == len(nets)
	})
	return s, d
}

func TestWifiAPI(t *testing.T) {
	s, d := wifiSvc(t, fakedisplay.SavedNetwork{SSID: "CasaMia", Current: true, RSSI: ptr(-55)},
		fakedisplay.SavedNetwork{SSID: "Ufficio"})
	path := "/api/displays/" + board + "/wifi/"
	const secret = "Pw-dal-browser-7781!"
	body := `{"ssid":"Caffè","password":"` + secret + `"}`

	// guardie: Host, origine, tipo, forma del corpo
	guards := []struct {
		name string
		r    webReq
		want int
	}{
		{"Host non locale", webReq{"POST", path + "add", "application/json", body, s.browser(), "evil.example:8765"}, 403},
		{"altra origine", webReq{"POST", path + "add", "application/json", body, map[string]string{"Origin": "http://evil.example"}, ""}, 403},
		{"altro sito", webReq{"POST", path + "add", "application/json", body, map[string]string{"Sec-Fetch-Site": "cross-site"}, ""}, 403},
		{"form", webReq{"POST", path + "add", "application/x-www-form-urlencoded", "ssid=Caffè&password=" + secret, s.browser(), ""}, 415},
		{"text/plain", webReq{"POST", path + "add", "text/plain", body, s.browser(), ""}, 415},
		{"campo in più", webReq{"POST", path + "add", "application/json", `{"ssid":"Caffè","password":"` + secret + `","salva":true}`, s.browser(), ""}, 400},
		{"senza password", webReq{"POST", path + "add", "application/json", `{"ssid":"Caffè"}`, s.browser(), ""}, 400},
		{"password corta", webReq{"POST", path + "add", "application/json", `{"ssid":"Caffè","password":"Corta-1"}`, s.browser(), ""}, 400},
		{"forget con password", webReq{"POST", path + "forget", "application/json", `{"ssid":"Ufficio","password":"` + secret + `"}`, s.browser(), ""}, 400},
		{"forget sconosciuta", webReq{"POST", path + "forget", "application/json", `{"ssid":"Boh"}`, s.browser(), ""}, 404},
		{"GET", webReq{"GET", path + "add", "", "", s.browser(), ""}, 404},
	}
	for _, g := range guards {
		code, m := s.web(g.r)
		if code != g.want {
			t.Errorf("%s: %d %v, atteso %d", g.name, code, m, g.want)
		}
		if b, _ := json.Marshal(m); strings.Contains(string(b), secret) || strings.Contains(string(b), "Corta-1") {
			t.Errorf("%s: la risposta ripete la password: %s", g.name, b)
		}
	}
	if len(d.Wifi()) != 2 {
		t.Fatalf("una richiesta respinta è arrivata alla scheda: %+v", d.Wifi())
	}

	// dalla webapp: 200 dopo l'ack della scheda, che ha decifrato la password giusta
	code, m := s.web(webReq{"POST", path + "add", "application/json", body, s.browser(), ""})
	if code != 200 || m["ok"] != true || m["ssid"] != "Caffè" || m["op"] != "add" {
		t.Fatalf("add: %d %v", code, m)
	}
	if e := await(t, d, "wifi-add"); e.Text != secret {
		t.Fatalf("la scheda ha decifrato %q", e.Text)
	}
	for _, f := range d.Frames() {
		if strings.Contains(f, secret) {
			t.Fatalf("password in chiaro sul WebSocket: %s", f)
		}
	}
	waitFor(t, "rete nell'elenco", func() bool { return len(s.status().Wifi.Networks) == 3 })
	st, _ := json.Marshal(s.apiStatus())
	if strings.Contains(string(st), secret) {
		t.Fatal("password in /api/status")
	}

	// rifiuto della scheda: 409 con il motivo
	d.SetWifi(fakedisplay.SavedNetwork{SSID: "A"}, fakedisplay.SavedNetwork{SSID: "B"}, fakedisplay.SavedNetwork{SSID: "C"},
		fakedisplay.SavedNetwork{SSID: "D"}, fakedisplay.SavedNetwork{SSID: "E"})
	waitFor(t, "lista piena", func() bool { return len(s.status().Wifi.Networks) == 5 })
	code, m = s.web(webReq{"POST", path + "add", "application/json", `{"ssid":"F","password":"Altra-password-1"}`, s.browser(), ""})
	if code != 409 || m["reason"] != fakedisplay.WifiErrFull || !strings.Contains(m["error"].(string), "non ha aggiunto") {
		t.Fatalf("lista piena: %d %v", code, m)
	}

	// dimentica: 200, poi la scheda si riavvia
	d.WifiOptions(false, false)
	code, m = s.web(webReq{"POST", path + "forget", "application/json", `{"ssid":"C"}`, s.browser(), ""})
	if code != 200 || m["reboot"] != true || m["wasLast"] == true {
		t.Fatalf("forget: %d %v", code, m)
	}
	select {
	case <-d.Done():
	case <-time.After(3 * time.Second):
		t.Fatal("la scheda non si è riavviata")
	}
	// scheda scollegata: 409
	waitFor(t, "scheda scollegata", func() bool { return s.status().Board == "" })
	if code, m := s.web(webReq{"POST", path + "add", "application/json", body, s.browser(), ""}); code != 409 {
		t.Fatalf("scollegata: %d %v", code, m)
	}
	s.noSecret(secret, "Altra-password-1", "Corta-1")
}

// Display collegato ma non abbinato: 409, e la password non parte.
func TestWifiAPINotAuthenticated(t *testing.T) {
	s := startSvc(t, 0)
	d := s.dial(nil, false)
	waitFor(t, "display collegato", func() bool { return s.status().Board == board })
	code, m := s.web(webReq{"POST", "/api/displays/" + board + "/wifi/add", "application/json",
		`{"ssid":"Casa","password":"Non-autenticato-55"}`, s.browser(), ""})
	if code != 409 || !strings.Contains(m["error"].(string), "non abbinato") {
		t.Fatalf("non autenticato: %d %v", code, m)
	}
	code, _ = s.web(webReq{"POST", "/api/displays/" + board + "/wifi/forget", "application/json", `{"ssid":"Casa"}`, s.browser(), ""})
	if code != 409 {
		t.Fatalf("forget non autenticato: %d", code)
	}
	time.Sleep(100 * time.Millisecond)
	for _, f := range d.Frames() {
		if strings.Contains(f, "wifi-") || strings.Contains(f, "Non-autenticato-55") {
			t.Fatalf("partito verso un display non autenticato: %s", f)
		}
	}
	if strings.Contains(s.log.String(), "Non-autenticato-55") {
		t.Fatal("password nel log")
	}
}

func TestCLIWifi(t *testing.T) {
	s, d := wifiSvc(t, fakedisplay.SavedNetwork{SSID: "CasaMia", Current: true, RSSI: ptr(-52)},
		fakedisplay.SavedNetwork{SSID: "Ufficio", RSSI: ptr(-79)})

	r := s.cli("", "wifi", "list")
	if r.code != 0 || !strings.Contains(r.out, "Reti Wi-Fi di "+board) || !strings.Contains(r.out, "* CasaMia") ||
		!strings.Contains(r.out, "in uso") || !strings.Contains(r.out, "-79 dBm (debole)") {
		t.Fatalf("wifi list: %+v", r)
	}

	// la password non è mai un argomento
	if r := s.cli("", "wifi", "add", "--password", "Arg-password-1", "Nuova"); r.code != 2 {
		t.Fatalf("--password accettato: %+v", r)
	}
	// senza terminale e senza --stdin: rifiuto, niente mandato
	if r := s.cli("Senza-terminale-1\n", "wifi", "add", "Nuova"); r.code != 2 || !strings.Contains(r.err, "--stdin") {
		t.Fatalf("senza terminale: %+v", r)
	}
	if len(d.Wifi()) != 2 {
		t.Fatal("rete mandata senza password valida")
	}
	// --stdin: prima riga dello standard input
	const secret = "Pw-da-script 42!"
	r = s.cli(secret+"\nriga in più\n", "wifi", "add", "--stdin", "Nuova")
	if r.code != 0 || !strings.Contains(r.out, "Rete «Nuova» salvata su "+board) {
		t.Fatalf("wifi add --stdin: %+v", r)
	}
	if strings.Contains(r.out+r.err, secret) {
		t.Fatalf("password stampata: %+v", r)
	}
	if e := await(t, d, "wifi-add"); e.Text != secret {
		t.Fatalf("la scheda ha decifrato %q", e.Text)
	}
	// flag dopo lo SSID; password corta: errore del servizio, prima di mandare
	if r := s.cli("corta\n", "wifi", "add", "Corta", "--stdin"); r.code != 1 || !strings.Contains(r.err, "troppo corta") {
		t.Fatalf("password corta: %+v", r)
	}
	// lista piena: motivo della scheda
	d.SetWifi(fakedisplay.SavedNetwork{SSID: "A", Current: true}, fakedisplay.SavedNetwork{SSID: "B"}, fakedisplay.SavedNetwork{SSID: "C"},
		fakedisplay.SavedNetwork{SSID: "D"}, fakedisplay.SavedNetwork{SSID: "E"})
	waitFor(t, "lista piena", func() bool { return len(s.status().Wifi.Networks) == 5 })
	if r := s.cli("Lista-piena-77\n", "wifi", "add", "--stdin", "F"); r.code != 1 || !strings.Contains(r.out, fakedisplay.WifiErrFull) {
		t.Fatalf("lista piena: %+v", r)
	}

	// dimentica: senza conferma non succede niente
	r = s.cli("\n", "wifi", "forget", "B")
	if r.code != 1 || !strings.Contains(r.out, "si riavvia") || !strings.Contains(r.out, "Niente fatto") || len(d.Wifi()) != 5 {
		t.Fatalf("forget senza conferma: %+v", r)
	}
	if r := s.cli("y\n", "wifi", "forget", "Zeta"); r.code != 1 || !strings.Contains(r.err, "non ha una rete salvata") {
		t.Fatalf("forget sconosciuta: %+v", r)
	}
	r = s.cli("s\n", "wifi", "forget", "A")
	if r.code != 0 || !strings.Contains(r.out, "È la rete in uso") || !strings.Contains(r.out, "Rete «A» dimenticata. La scheda si riavvia.") {
		t.Fatalf("forget: %+v", r)
	}
	<-d.Done()

	// ritorno con una rete sola: l'ultima, con l'avviso della configurazione
	d2 := s.otaDisplay(fakedisplay.Options{}, d.Key())
	d2.SetWifi(fakedisplay.SavedNetwork{SSID: "Ultima", Current: true})
	waitFor(t, "lista dopo il riavvio", func() bool {
		w := s.status().Wifi
		return w != nil && len(w.Networks) == 1 && w.Networks[0].SSID == "Ultima"
	})
	r = s.cli("", "wifi", "forget", "--yes", "Ultima")
	if r.code != 0 || !strings.Contains(r.out, "l'ultima rete salvata") || !strings.Contains(r.out, "Era l'ultima rete") {
		t.Fatalf("forget dell'ultima: %+v", r)
	}
	s.noSecret(secret, "Lista-piena-77", "Senza-terminale-1", "Arg-password-1")
	if !bytes.Contains([]byte(s.log.String()), []byte("wifi-forget")) {
		t.Fatal("log senza wifi-forget")
	}
}
