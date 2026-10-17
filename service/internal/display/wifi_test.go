package display

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/m5"
)

// Reti Wi-Fi con un display finto che fa la parte della scheda (internal/fakedisplay): decifra la
// password con la chiave derivata da SK, controlla aad e tag, rifiuta lista piena e password corta,
// verifica il mac di wifi-forget e dopo l'ack si riavvia.

const wifiBoard = "calamaio-3f2a"

func wifiRig(t *testing.T, ack time.Duration) *rig {
	t.Helper()
	r := newRig(t, func(h *Hub) { h.AckTimeout = ack })
	r.pairKey(wifiBoard, testKey)
	return r
}

// wifiDisplay: scheda finta abbinata e autenticata, con le reti nets (wifi-list già arrivata).
func wifiDisplay(t *testing.T, r *rig, nets ...fakedisplay.SavedNetwork) *fakedisplay.Display {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.Dial(ctx, r.url, wifiBoard, testKey, false)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	if _, err := d.Await("authed", 3*time.Second); err != nil {
		t.Fatal(err)
	}
	if nets != nil {
		d.SetWifi(nets...)
		waitFor(t, 2*time.Second, func() bool {
			v := r.hub.WifiOf(wifiBoard)
			return v.Networks != nil && len(v.Networks) == len(nets)
		})
	}
	return d
}

func rssi(v int) *int { return &v }

func TestWifiListKept(t *testing.T) {
	r := wifiRig(t, 2*time.Second)
	wifiDisplay(t, r, fakedisplay.SavedNetwork{SSID: "CasaMia", Current: true, RSSI: rssi(-52)},
		fakedisplay.SavedNetwork{SSID: "Ufficio"})
	st := r.hub.Status()
	if len(st) != 1 || st[0].Wifi == nil {
		t.Fatalf("status: %+v", st)
	}
	n := st[0].Wifi.Networks
	if len(n) != 2 || n[0].SSID != "CasaMia" || !n[0].Current || n[0].RSSI == nil || *n[0].RSSI != -52 ||
		n[1].SSID != "Ufficio" || n[1].Current || n[1].RSSI != nil || n[1].LastUsed != nil {
		t.Fatalf("reti: %+v", n)
	}
	if st[0].Wifi.At == 0 || st[0].Wifi.Pending != nil || st[0].Wifi.Last != nil {
		t.Fatalf("vista: %+v", st[0].Wifi)
	}
	// una lista non valida si scarta tutta
	for _, bad := range []string{
		`{"t":"wifi-list","v":1}`,
		`{"t":"wifi-list","v":1,"networks":[{"ssid":"","current":false,"rssi":null,"lastUsed":null}]}`,
		`{"t":"wifi-list","v":1,"networks":[{"ssid":"` + strings.Repeat("x", 33) + `","current":false,"rssi":null,"lastUsed":null}]}`,
		`{"t":"wifi-list","v":1,"networks":[{"ssid":"a","current":false,"rssi":12,"lastUsed":null}]}`,
	} {
		if _, ok := parseWifiList([]byte(bad)); ok {
			t.Errorf("accettata: %s", bad)
		}
	}
	if _, ok := parseWifiList([]byte(`{"t":"wifi-list","v":1,"networks":[]}`)); !ok {
		t.Error("lista vuota rifiutata")
	}
}

func TestWifiAddEncryptedAndVerified(t *testing.T) {
	r := wifiRig(t, 2*time.Second)
	d := wifiDisplay(t, r, fakedisplay.SavedNetwork{SSID: "CasaMia", Current: true})
	const secret = "Zq8!segreta-di-prova-4417"
	res, err := r.hub.WifiAdd(context.Background(), wifiBoard, "Ufficio", []byte(secret))
	if err != nil || !res.OK || res.Op != WifiOpAdd || res.SSID != "Ufficio" || !strings.HasPrefix(res.ID, "c") {
		t.Fatalf("WifiAdd: %+v %v", res, err)
	}
	e := awaitEv(t, d, "wifi-add")
	if e.Text != secret {
		t.Fatalf("la scheda ha decifrato %q", e.Text)
	}
	// la scheda ha verificato aad e tag (fakedisplay: m5.WifiOpen); qui si rifà con SK della connessione
	raw, _ := json.Marshal(e.Msg)
	var msg WifiAddMsg
	_ = json.Unmarshal(raw, &msg)
	iv, _ := m5.ParseHex(msg.IV, m5.WifiIVLen)
	ct, _ := m5.ParseHex(msg.CT, len(msg.CT)/2)
	if pw, err := m5.WifiOpen(d.SK(), msg.ID, msg.SSID, iv, ct); err != nil || string(pw) != secret {
		t.Fatalf("decifrato %q %v", pw, err)
	}
	if len(ct) != len(secret)+m5.WifiTagLen {
		t.Fatalf("ct senza tag: %d byte", len(ct))
	}
	// la password non passa mai in chiaro: né nei frame, né nel log, né in /api/status
	for _, f := range d.Frames() {
		if strings.Contains(f, secret) || strings.Contains(f, m5.Hex([]byte(secret))) {
			t.Fatalf("password in chiaro nel frame %s", f)
		}
	}
	if strings.Contains(r.log.String(), secret) || strings.Contains(r.log.String(), m5.Hex([]byte(secret))) {
		t.Fatalf("password nel log:\n%s", r.log.String())
	}
	if !strings.Contains(r.log.String(), "wifi-add "+res.ID+" «Ufficio» (password cifrata)") {
		t.Fatalf("log senza la riga di wifi-add:\n%s", r.log.String())
	}
	st, _ := json.Marshal(r.hub.Status())
	if strings.Contains(string(st), secret) {
		t.Fatal("password nello stato")
	}
	// due iv diversi per due invii della stessa rete (iv casuale)
	res2, err := r.hub.WifiAdd(context.Background(), wifiBoard, "Ufficio", []byte(secret))
	if err != nil || !res2.OK {
		t.Fatalf("seconda WifiAdd: %+v %v", res2, err)
	}
	e2 := awaitEv(t, d, "wifi-add")
	if e2.Msg["iv"] == e.Msg["iv"] || e2.Msg["id"] == e.Msg["id"] {
		t.Fatal("iv o id ripetuti")
	}
	waitFor(t, 2*time.Second, func() bool { return len(r.hub.WifiOf(wifiBoard).Networks) == 2 })
	if got := d.Wifi(); len(got) != 2 || got[1].Password != secret {
		t.Fatalf("reti sulla scheda: %+v", got)
	}
	if v := r.hub.WifiOf(wifiBoard); v.Last == nil || !v.Last.OK || v.Pending != nil {
		t.Fatalf("vista: %+v", v)
	}
}

func TestWifiAddErrors(t *testing.T) {
	r := wifiRig(t, 500*time.Millisecond)
	ctx := context.Background()
	// scheda non collegata
	if _, err := r.hub.WifiAdd(ctx, wifiBoard, "Casa", []byte("password-lunga")); !errors.Is(err, ErrNotConnected) {
		t.Fatalf("non collegata: %v", err)
	}
	// controlli del servizio (400): mai la password nel messaggio
	for _, c := range []struct{ ssid, pw, want string }{
		{"", "password-lunga", "manca il nome"},
		{strings.Repeat("s", 33), "password-lunga", "troppo lungo"},
		{"Casa\x1fX", "password-lunga", "caratteri non ammessi"},
		{"Casa", "", "serve la password"},
		{"Casa", "corta77", "troppo corta"},
		{"Casa", strings.Repeat("p", 64), "troppo lunga"},
		{"Casa", "riga\nnuova!", "caratteri non ammessi"},
	} {
		_, err := r.hub.WifiAdd(ctx, wifiBoard, c.ssid, []byte(c.pw))
		var inv *InvalidError
		if !errors.As(err, &inv) || !strings.Contains(err.Error(), c.want) || (c.pw != "" && strings.Contains(err.Error(), c.pw)) {
			t.Errorf("%q/%q: %v", c.ssid, c.pw, err)
		}
	}
	if err := ValidateWifiPassword([]byte(strings.Repeat("ab", 32))); err == nil { // 64 cifre: la scheda la rifiuta
		t.Error("password di 64 byte accettata")
	}
	if err := ValidateWifiPassword([]byte(strings.Repeat("è", 31) + "x")); err != nil { // 63 byte UTF-8
		t.Errorf("63 byte rifiutati: %v", err)
	}

	full := make([]fakedisplay.SavedNetwork, fakedisplay.WifiMax)
	for i := range full {
		full[i] = fakedisplay.SavedNetwork{SSID: "Rete" + string(rune('A'+i)), Password: "password-" + string(rune('a'+i))}
	}
	d := wifiDisplay(t, r, full...)
	// lista piena: la decide la scheda (ack negativo), il servizio la riferisce
	res, err := r.hub.WifiAdd(ctx, wifiBoard, "Sesta", []byte("password-sesta"))
	if err != nil || res.OK || res.Reason != fakedisplay.WifiErrFull || !strings.Contains(res.Error, "non ha aggiunto la rete «Sesta»") {
		t.Fatalf("lista piena: %+v %v", res, err)
	}
	// la stessa rete con la lista piena si aggiorna
	if res, err := r.hub.WifiAdd(ctx, wifiBoard, "ReteA", []byte("password-nuova")); err != nil || !res.OK {
		t.Fatalf("aggiornamento con lista piena: %+v %v", res, err)
	}
	// password corta rifiutata anche dalla scheda (il servizio qui non la controlla: wifiSend diretto)
	res, err = r.hub.wifiSend(ctx, wifiBoard, WifiOpAdd, "ReteB", []byte("corta"))
	if err != nil || res.OK || res.Reason != fakedisplay.WifiErrShort {
		t.Fatalf("password corta: %+v %v", res, err)
	}
	// nessun ack (firmware senza più reti) → scadenza
	d.WifiOptions(false, true)
	res, err = r.hub.WifiAdd(ctx, wifiBoard, "ReteC", []byte("password-lunga"))
	if err != nil || res.OK || !res.Timeout || !strings.Contains(res.Error, "non ha confermato") {
		t.Fatalf("scadenza: %+v %v", res, err)
	}
	if v := r.hub.WifiOf(wifiBoard); v.Pending != nil || v.Last == nil || !v.Last.Timeout {
		t.Fatalf("vista dopo la scadenza: %+v", v)
	}
	// un'operazione alla volta per scheda
	done := make(chan WifiResult, 1)
	go func() {
		res, _ := r.hub.WifiAdd(ctx, wifiBoard, "ReteD", []byte("password-lunga"))
		done <- res
	}()
	waitFor(t, 2*time.Second, func() bool { return r.hub.WifiOf(wifiBoard).Pending != nil })
	if p := r.hub.WifiOf(wifiBoard).Pending; p.Op != WifiOpAdd || p.SSID != "ReteD" || p.Until == 0 {
		t.Fatalf("pending: %+v", p)
	}
	if _, err := r.hub.WifiForget(ctx, wifiBoard, "ReteA"); !errors.Is(err, ErrWifiBusy) {
		t.Fatalf("seconda operazione: %v", err)
	}
	<-done
	// un ack tardivo (dopo la scadenza) si ignora
	if !strings.Contains(r.log.String(), "nessun ack per wifi-add") {
		t.Fatalf("log:\n%s", r.log.String())
	}
}

func TestWifiNotAuthenticated(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.greet(wifiBoard) // scheda senza abbinamento
	if _, err := r.hub.WifiAdd(context.Background(), wifiBoard, "Casa", []byte("password-lunga")); !errors.Is(err, ErrNotAuthed) {
		t.Fatalf("add non autenticato: %v", err)
	}
	if _, err := r.hub.WifiForget(context.Background(), wifiBoard, "Casa"); !errors.Is(err, ErrNotAuthed) {
		t.Fatalf("forget non autenticato: %v", err)
	}
	c.none(200 * time.Millisecond) // niente wifi-add né wifi-forget sul filo
	// la lista si accetta anche senza autenticazione (come status): non contiene segreti
	c.send(`{"t":"wifi-list","v":1,"networks":[{"ssid":"Casa","current":true,"rssi":-60,"lastUsed":1791748800000}]}`)
	waitFor(t, 2*time.Second, func() bool { return len(r.hub.WifiOf(wifiBoard).Networks) == 1 })
}

func TestWifiForget(t *testing.T) {
	r := wifiRig(t, 2*time.Second)
	ctx := context.Background()
	d := wifiDisplay(t, r, fakedisplay.SavedNetwork{SSID: "CasaMia", Current: true}, fakedisplay.SavedNetwork{SSID: "Ufficio"})
	if _, err := r.hub.WifiForget(ctx, wifiBoard, "Sconosciuta"); !errors.Is(err, ErrWifiUnknown) {
		t.Fatalf("rete sconosciuta: %v", err)
	}
	res, err := r.hub.WifiForget(ctx, wifiBoard, "Ufficio")
	if err != nil || !res.OK || !res.Reboot || res.WasLast {
		t.Fatalf("forget: %+v %v", res, err)
	}
	e := awaitEv(t, d, "wifi-forget")
	if e.Msg["mac"] != m5.Hex(m5.WifiForgetMac(d.SK(), res.ID, "Ufficio")) {
		t.Fatalf("mac: %v", e.Msg)
	}
	if v := r.hub.WifiOf(wifiBoard); len(v.Networks) != 1 || v.Networks[0].SSID != "CasaMia" {
		t.Fatalf("lista dopo forget: %+v", v.Networks)
	}
	// dopo l'ack la scheda si riavvia: la connessione cade
	select {
	case <-d.Done():
	case <-time.After(2 * time.Second):
		t.Fatal("la scheda finta non si è riavviata")
	}
	waitFor(t, 2*time.Second, func() bool { return len(r.hub.Status()) == 0 })
	if _, err := r.hub.WifiForget(ctx, wifiBoard, "CasaMia"); !errors.Is(err, ErrNotConnected) {
		t.Fatalf("scheda in riavvio: %v", err)
	}

	// ritorno con una rete sola: è l'ultima
	d2 := wifiDisplay(t, r, fakedisplay.SavedNetwork{SSID: "CasaMia", Current: true})
	d2.WifiOptions(true, false)
	res, err = r.hub.WifiForget(ctx, wifiBoard, "CasaMia")
	if err != nil || !res.OK || !res.WasLast {
		t.Fatalf("ultima rete: %+v %v", res, err)
	}
	// mac sbagliato (frame falsificato): la scheda rifiuta, il servizio lo riferisce
	d2.SetWifi(fakedisplay.SavedNetwork{SSID: "Altra"})
	d2.Deliver(`{"t":"wifi-forget","v":1,"id":"c999","ssid":"Altra","mac":"` + strings.Repeat("0", 32) + `"}`)
	if e := awaitEv(t, d2, "rejected"); !strings.Contains(e.Text, fakedisplay.WifiErrMac) {
		t.Fatalf("mac falso: %+v", e)
	}
	if len(d2.Wifi()) != 1 {
		t.Fatal("la scheda ha dimenticato con un mac falso")
	}
}

// La connessione cade mentre un'operazione aspetta l'ack: l'esito arriva subito, senza aspettare 10 s.
func TestWifiDisconnectWhilePending(t *testing.T) {
	r := wifiRig(t, 5*time.Second)
	d := wifiDisplay(t, r, fakedisplay.SavedNetwork{SSID: "CasaMia", Current: true})
	d.WifiOptions(false, true)
	done := make(chan WifiResult, 1)
	go func() {
		res, _ := r.hub.WifiAdd(context.Background(), wifiBoard, "Nuova", []byte("password-lunga"))
		done <- res
	}()
	waitFor(t, 2*time.Second, func() bool { return r.hub.WifiOf(wifiBoard).Pending != nil })
	d.Close()
	select {
	case res := <-done:
		if res.OK || !strings.Contains(res.Error, "scollegata") {
			t.Fatalf("esito: %+v", res)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("l'operazione non si è chiusa con la connessione")
	}
}

func TestWifiMessagesMatchSchema(t *testing.T) {
	sch := compileSchema(t)
	r := wifiRig(t, 2*time.Second)
	d := wifiDisplay(t, r, fakedisplay.SavedNetwork{SSID: "Caffè ☕", Current: true, RSSI: rssi(-70)}, fakedisplay.SavedNetwork{SSID: "Ufficio"})
	d.WifiOptions(true, false)
	if res, err := r.hub.WifiAdd(context.Background(), wifiBoard, "Caffè ☕ 5G", []byte("pässwörd finta!")); err != nil || !res.OK {
		t.Fatal(res, err)
	}
	if res, err := r.hub.WifiForget(context.Background(), wifiBoard, "Ufficio"); err != nil || !res.OK {
		t.Fatal(res, err)
	}
	n := 0
	for _, e := range d.Log() {
		if typ, _ := e.Msg["t"].(string); typ == "wifi-add" || typ == "wifi-forget" {
			b, _ := json.Marshal(e.Msg)
			validate(t, sch, "dal servizio: "+typ, b)
			n++
		}
	}
	if n != 2 {
		t.Fatalf("messaggi controllati: %d", n)
	}
}

// Gli esempi di protocol/examples sono veri (SK = 32 × 0x02, i vettori di internal/m5/wifi_test.go) e
// passano dai parser del servizio.
func TestWifiExamplesAreReal(t *testing.T) {
	sk := make([]byte, 32)
	for i := range sk {
		sk[i] = 2
	}
	read := func(name string, v any) {
		b, err := os.ReadFile(filepath.Join(protocolDir, "examples", name))
		if err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(b, v); err != nil {
			t.Fatal(err)
		}
	}
	var add WifiAddMsg
	read("wifi-add.json", &add)
	iv, _ := m5.ParseHex(add.IV, m5.WifiIVLen)
	ct, _ := m5.ParseHex(add.CT, len(add.CT)/2)
	if pw, err := m5.WifiOpen(sk, add.ID, add.SSID, iv, ct); err != nil || string(pw) != "password-finta-1234" {
		t.Fatalf("wifi-add.json: %q %v", pw, err)
	}
	var fg WifiForgetMsg
	read("wifi-forget.json", &fg)
	if fg.Mac != m5.Hex(m5.WifiForgetMac(sk, fg.ID, fg.SSID)) {
		t.Fatalf("wifi-forget.json: %+v", fg)
	}
	b, _ := os.ReadFile(filepath.Join(protocolDir, "examples", "wifi-list.json"))
	if list, ok := parseWifiList(b); !ok || len(list) != 3 || !list[0].Current {
		t.Fatalf("wifi-list.json: %+v %v", list, ok)
	}
}

// I messaggi dei vettori del firmware (protocol/vectors/wifi-vectors.json) rispettano lo schema e la
// wifi-list passa dal parser del servizio.
func TestWifiVectorMessages(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(protocolDir, "vectors", "wifi-vectors.json"))
	if err != nil {
		t.Skip("wifi-vectors.json non c'è ancora")
	}
	var v struct {
		Add    []struct{ Message json.RawMessage }
		Forget []struct{ Message json.RawMessage }
		List   struct {
			Message json.RawMessage
			JSON    string `json:"json"`
		}
	}
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	sch := compileSchema(t)
	for i, a := range v.Add {
		validate(t, sch, "vettore add "+string(rune('0'+i)), a.Message)
	}
	for i, f := range v.Forget {
		validate(t, sch, "vettore forget "+string(rune('0'+i)), f.Message)
	}
	validate(t, sch, "vettore list", []byte(v.List.JSON))
	list, ok := parseWifiList([]byte(v.List.JSON))
	if !ok || len(list) != 4 || !list[0].Current || list[2].RSSI != nil || list[3].LastUsed != nil {
		t.Fatalf("wifi-list dei vettori: %+v %v", list, ok)
	}
}
