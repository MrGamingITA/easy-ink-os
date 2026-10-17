package display

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

const avBoard = "calamaio-a7a7"

// helloDl: hello del firmware 0.14.0, con la capacità avatar-dl.
func helloDl(board string) string {
	return fmt.Sprintf(`{"t":"hello","v":1,"board":%q,"device":"heltec213","class":"S","fw":"0.14.0","w":250,"h":122,"paired":false,"caps":["avatar-dl"]}`, board)
}

// dlDisplay: scheda con avatar-dl (non abbinata: l'offerta non chiede l'autenticazione), fino allo state.
func dlDisplay(t *testing.T, r *rig, hello string) *client {
	t.Helper()
	c := r.dial(nil)
	c.send(hello)
	if w := c.next(2 * time.Second); w["t"] != "welcome" {
		t.Fatalf("atteso welcome: %v", w)
	}
	if s := c.next(2 * time.Second); s["t"] != "state" {
		t.Fatalf("atteso state: %v", s)
	}
	return c
}

func avGet(t *testing.T, r *rig, tok string) (int, []byte, http.Header) {
	t.Helper()
	res, err := http.Get(r.srv.URL + "/avatars/" + tok)
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	b, _ := io.ReadAll(res.Body)
	return res.StatusCode, b, res.Header
}

func ackJSON(id string, crc any, ok bool, why string) string {
	m := map[string]any{"t": "avatar-ack", "v": 1, "id": id, "crc": crc, "ok": ok}
	if why != "" {
		m["error"] = why
	}
	b, _ := json.Marshal(m)
	return string(b)
}

// Il giro intero: avatar-need → avatar-offer (uno alla volta) → GET a pezzi → avatar-ack → offerta seguente.
func TestAvatarOfferFlow(t *testing.T) {
	sch := compileSchema(t)
	r, m := avatarRig(t)
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto","demone"]}`)
	off := c.next(2 * time.Second)
	ob, _ := json.Marshal(off)
	validate(t, sch, "avatar-offer", ob)
	g := m.Lib.Get("gatto")
	if off["t"] != "avatar-offer" || off["id"] != "gatto" || off["crc"] != float64(g.CRC) || off["size"] != float64(len(g.Bin)) {
		t.Fatalf("offerta: %s", ob)
	}
	c.none(200 * time.Millisecond) // un'offerta alla volta
	tok := off["token"].(string)
	code, body, hd := avGet(t, r, tok)
	if code != 200 || !bytes.Equal(body, g.Bin) || hd.Get("X-Calamaio-Crc") != fmt.Sprintf("%08x", g.CRC) || hd.Get("Cache-Control") != "no-store" {
		t.Fatalf("download: %d, %d byte, %v", code, len(body), hd)
	}
	if code, _, _ := avGet(t, r, tok); code != http.StatusGone {
		t.Fatalf("token riusato: %d", code)
	}
	ack := ackJSON("gatto", g.CRC, true, "")
	validate(t, sch, "avatar-ack", []byte(ack))
	c.send(ack)
	off2 := c.next(2 * time.Second)
	if off2["t"] != "avatar-offer" || off2["id"] != "demone" || off2["token"] == tok {
		t.Fatalf("seconda offerta: %v", off2)
	}
	c.send(ackJSON("demone", off2["crc"], true, ""))
	waitLog(t, r, "avatar demone salvato dalla scheda", 1)
	c.none(200 * time.Millisecond)
}

// Senza la capacità (firmware ≤ 0.13): il messaggio avatar di sempre, mai un'offerta.
func TestAvatarWithoutCapsStaysLegacy(t *testing.T) {
	r, _ := avatarRig(t)
	c := dlDisplay(t, r, strings.Replace(helloDl(avBoard), `,"caps":["avatar-dl"]`, "", 1))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if a := c.next(2 * time.Second); a["t"] != "avatar" || a["id"] != "gatto" {
		t.Fatalf("atteso avatar: %v", a)
	}
}

// R4: dopo una consegna fallita lo stesso avatar non si rimanda alla stessa scheda per AvatarFailHold, nemmeno su una
// connessione nuova; passato il tempo sì.
func TestAvatarFailHoldAcrossConnections(t *testing.T) {
	r, m := avatarRig(t)
	r.hub.AvatarFailHold = 600 * time.Millisecond
	g := m.Lib.Get("gatto")
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c.next(2 * time.Second)
	c.send(ackJSON("gatto", g.CRC, false, "memoria insufficiente"))
	waitLog(t, r, "la scheda l'ha rifiutato: memoria insufficiente", 1)
	_ = c.ws.Close(websocket.StatusNormalClosure, "")
	c2 := dlDisplay(t, r, helloDl(avBoard))
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c2.none(250 * time.Millisecond)
	waitLog(t, r, "avatar gatto non rimandato", 1)
	time.Sleep(400 * time.Millisecond)
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if off := c2.next(2 * time.Second); off["t"] != "avatar-offer" || off["id"] != "gatto" {
		t.Fatalf("dopo la pausa: %v", off)
	}
	// un'altra scheda non è toccata dal fallimento della prima
	c3 := dlDisplay(t, r, helloDl("calamaio-b8b8"))
	c3.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if off := c3.next(2 * time.Second); off["t"] != "avatar-offer" {
		t.Fatalf("altra scheda: %v", off)
	}
}

// La connessione si chiude con l'offerta in corso (la 1011 vista dal vivo): fallita, niente rinvio subito.
func TestAvatarConnClosedMidOffer(t *testing.T) {
	r, _ := avatarRig(t)
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	tok := c.next(2 * time.Second)["token"].(string)
	_ = c.ws.Close(websocket.StatusInternalError, "")
	waitLog(t, r, "connessione chiusa durante la consegna", 1)
	if code, _, _ := avGet(t, r, tok); code != http.StatusNotFound {
		t.Fatalf("token dopo la chiusura: %d", code)
	}
	c2 := dlDisplay(t, r, helloDl(avBoard))
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c2.none(300 * time.Millisecond)
}

// Il ciclo visto dal vivo con la 0.13.0-ed: avatar mandato, connessione chiusa subito, riconnessione, avatar-need.
// Prima della 0.14.0 il servizio lo rimandava a ogni connessione (1 al secondo); ora aspetta AvatarFailHold.
func TestAvatarLegacyCloseHeld(t *testing.T) {
	r, _ := avatarRig(t)
	hl := strings.Replace(hello(avBoard), `"fw":"0.3.0"`, `"fw":"0.13.0-ed"`, 1)
	c := dlDisplay(t, r, hl)
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	if a := c.next(2 * time.Second); a["t"] != "avatar" {
		t.Fatalf("atteso avatar: %v", a)
	}
	_ = c.ws.Close(websocket.StatusInternalError, "")
	waitLog(t, r, "connessione chiusa subito dopo il messaggio avatar", 1)
	c2 := dlDisplay(t, r, hl)
	c2.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c2.none(300 * time.Millisecond)
}

// Nessun avatar-ack entro AvatarAckTimeout: fallita, token morto, si passa al prossimo id.
func TestAvatarAckTimeout(t *testing.T) {
	r, _ := avatarRig(t)
	r.hub.AvatarAckTimeout = 200 * time.Millisecond
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto","demone"]}`)
	tok := c.next(2 * time.Second)["token"].(string)
	if off := c.next(2 * time.Second); off["id"] != "demone" {
		t.Fatalf("dopo la scadenza: %v", off)
	}
	waitLog(t, r, "nessun avatar-ack entro", 1)
	if code, _, _ := avGet(t, r, tok); code != http.StatusNotFound {
		t.Fatalf("token scaduto con l'offerta: %d", code)
	}
}

// Token: sconosciuto 404, IP diverso 403 (resta valido), poi 200. Ack sbagliati o senza offerta: ignorati.
func TestAvatarTokenAndAckRules(t *testing.T) {
	r, m := avatarRig(t)
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	tok := c.next(2 * time.Second)["token"].(string)
	if code, _, _ := avGet(t, r, strings.Repeat("b", 32)); code != http.StatusNotFound {
		t.Fatalf("sconosciuto: %d", code)
	}
	req := httptest.NewRequest(http.MethodGet, "/avatars/"+tok, nil)
	req.RemoteAddr = "10.0.0.99:4321"
	req.SetPathValue("token", tok)
	rec := httptest.NewRecorder()
	r.hub.ServeAvatar(rec, req)
	if rec.Code != http.StatusForbidden {
		t.Fatalf("IP sbagliato: %d", rec.Code)
	}
	if code, _, _ := avGet(t, r, tok); code != 200 {
		t.Fatalf("download dopo il 403: %d", code)
	}
	before := r.hub.Ignored()
	for _, bad := range []string{`{"t":"avatar-ack","v":1,"id":"gatto","ok":true}`, `{"t":"avatar-ack","v":1,"id":"Gatto","crc":1,"ok":true}`,
		`{"t":"avatar-ack","v":1,"id":"gatto","crc":1}`} {
		c.send(bad)
	}
	c.send(ackJSON("gatto", 12345, true, "")) // CRC diverso dall'offerta
	waitLog(t, r, "avatar-ack gatto senza un'offerta in corso", 1)
	if got := r.hub.Ignored() - before; got != 3 {
		t.Fatalf("ignorati %d su 3", got)
	}
	c.send(ackJSON("gatto", m.Lib.Get("gatto").CRC, true, ""))
	waitLog(t, r, "avatar gatto salvato dalla scheda", 1)
}

// I1: i blocchi per scheda non crescono senza limite con board inventati (l'hello non è autenticato).
func TestAvatarFailMapBounded(t *testing.T) {
	r, _ := avatarRig(t)
	h := r.hub
	h.mu.Lock()
	for i := 0; i < 200; i++ {
		h.avatarFailed(fmt.Sprintf("calamaio-%04x", i), "gatto", 1, "prova")
	}
	if n := len(h.avFail); n > avatarFailBoards {
		h.mu.Unlock()
		t.Fatalf("schede in memoria: %d", n)
	}
	// passato il blocco, il fallimento seguente toglie tutto il scaduto
	now := time.Now().Add(2 * h.AvatarFailHold)
	h.Now = func() time.Time { return now }
	h.avatarFailed("calamaio-ffff", "gatto", 1, "prova")
	n := len(h.avFail)
	h.mu.Unlock()
	if n != 1 {
		t.Fatalf("dopo la scadenza: %d", n)
	}
}

// M3: HEAD non consuma il token.
func TestAvatarHeadKeepsToken(t *testing.T) {
	r, m := avatarRig(t)
	c := dlDisplay(t, r, helloDl(avBoard))
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	tok := c.next(2 * time.Second)["token"].(string)
	res, err := http.Head(r.srv.URL + "/avatars/" + tok)
	if err != nil || res.StatusCode != 200 {
		t.Fatalf("HEAD: %v %v", err, res)
	}
	res.Body.Close()
	if code, body, _ := avGet(t, r, tok); code != 200 || !bytes.Equal(body, m.Lib.Get("gatto").Bin) {
		t.Fatalf("GET dopo HEAD: %d", code)
	}
	if strings.Contains(r.log.String(), "scaricato") && strings.Count(r.log.String(), "scaricato") != 1 {
		t.Fatalf("registro:\n%s", r.log.String())
	}
}
