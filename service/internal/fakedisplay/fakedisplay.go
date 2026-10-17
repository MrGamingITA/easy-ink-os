// Package fakedisplay è un display finto per i test: parla il protocollo v1 sul WebSocket del servizio
// e fa la parte del display in M5 come firmware/src/core/link.cpp (abbinamento, sfida e risposta,
// verifica delle richieste e delle chiusure, risposte firmate), con la stessa crittografia (internal/m5).
package fakedisplay

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/coder/websocket"

	"calamaio/internal/m5"
)

// Event è ciò che il display finto ha visto o fatto.
type Event struct {
	Type string         // tipo del messaggio del servizio, oppure: pair-code, pair-ready, pair-done, authed, redo, rejected
	Text string         // codice (pair-code), motivo (pair-abort, redo, rejected, perm-close)
	Msg  map[string]any // messaggio originale (se c'è)
	Req  *m5.PermRequest
}

// Display è una connessione di un display finto.
type Display struct {
	Board string

	ws     *websocket.Conn
	events chan Event
	done   chan struct{}

	mu       sync.Mutex
	permOn   bool   // Menu › Avvisi › Permessi dal display
	K        []byte // chiave di abbinamento (nil = non abbinato)
	authed   bool
	ns, nd   []byte
	sk       []byte
	seen     map[string]bool
	pairID   string
	skD, pkD []byte
	commit   []byte
	T, Kc    []byte
	ready    bool
	log      []Event

	ota    otaState     // ota.go
	wifi   wifiState    // wifi.go
	wd     workdayState // workday.go
	th     themeState   // themes.go
	wsURL  string       // ws://<pc>:<porta>/ws: da qui l'url di download dei temi
	frames []string     // tutti i frame ricevuti, com'erano (i test cercano segreti in chiaro)
}

// Settings sono le impostazioni mandate in `settings` (permOn da PermOn).
func (d *Display) settingsMsg(remote bool) string {
	d.mu.Lock()
	permOn := d.permOn
	d.mu.Unlock()
	d.wd.mu.Lock()
	work := ""
	if d.wd.opt.Enabled {
		work = fmt.Sprintf(`,"workStart":%q,"workEnd":%q`, d.wd.start, d.wd.end)
	}
	d.wd.mu.Unlock()
	return fmt.Sprintf(`{"t":"settings","v":1,"settings":{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":%t,"sound":true,"led":true,"mute":"off","save":false%s},"remote":%t}`, permOn, work, remote)
}

// Dial collega il display finto a url (ws://…/ws), manda hello (con paired) e settings.
func Dial(ctx context.Context, url, board string, K []byte, permOn bool) (*Display, error) {
	return DialWith(ctx, url, board, K, permOn, Options{})
}

// Options: versione del firmware e comportamento OTA del display finto (ota.go).
type Options struct {
	FW     string // hello.fw (vuoto = 0.5.0)
	Device string // hello.device (vuoto = heltec213)
	OtaPub []byte // chiave pubblica di firma già salvata (come nel vault dopo un aggiornamento precedente)
	OTA    OTAOptions
	// Giornata di lavoro (workday.go): orari 09:00–18:00 all'accensione, giornata spenta.
	Workday WorkdayOptions
	// Temi delle schermate (themes.go): spenti = firmware che non li conosce.
	Themes ThemeOptions
}

// DialWith è Dial con le opzioni.
func DialWith(ctx context.Context, url, board string, K []byte, permOn bool, o Options) (*Display, error) {
	ws, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		return nil, err
	}
	if o.FW == "" {
		o.FW = "0.5.0"
	}
	if o.Device == "" {
		o.Device = "heltec213"
	}
	d := &Display{Board: board, permOn: permOn, ws: ws, events: make(chan Event, 256), done: make(chan struct{}),
		seen: map[string]bool{}}
	d.ota.fw, d.ota.device, d.ota.opt = o.FW, o.Device, o.OTA
	d.wd.opt, d.wd.start, d.wd.end = o.Workday, "09:00", "18:00"
	d.th.opt, d.wsURL = o.Themes, url
	if o.OtaPub != nil {
		d.ota.pub = append([]byte(nil), o.OtaPub...)
	}
	if K != nil {
		d.K = append([]byte(nil), K...)
	}
	go d.loop()
	hello := map[string]any{"t": "hello", "v": 1, "board": board, "device": o.Device, "class": "S", "fw": o.FW,
		"w": 250, "h": 122, "paired": K != nil}
	b, _ := json.Marshal(hello)
	if err := d.Send(string(b)); err != nil {
		return nil, err
	}
	if err := d.Send(d.settingsMsg(false)); err != nil {
		return nil, err
	}
	if o.Workday.Enabled {
		d.SendWorkdayStatus() // come il firmware: status subito dopo hello, con la giornata
	}
	return d, nil
}

// Send manda un frame di testo così com'è (anche falso: i test degli attacchi lo usano).
func (d *Display) Send(s string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	return d.ws.Write(ctx, websocket.MessageText, []byte(s))
}

func (d *Display) sendJSON(v map[string]any) {
	v["v"] = 1
	b, _ := json.Marshal(v)
	_ = d.Send(string(b))
}

// Close chiude la connessione (display spento).
func (d *Display) Close() { _ = d.ws.Close(websocket.StatusNormalClosure, "spento") }

// Done si chiude quando la connessione è finita.
func (d *Display) Done() <-chan struct{} { return d.done }

// Key restituisce la chiave di abbinamento attuale.
func (d *Display) Key() []byte {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]byte(nil), d.K...)
}

// Authed: auth-ok valido ricevuto su questa connessione.
func (d *Display) Authed() bool {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.authed
}

// SK è la chiave di sessione (per i test che costruiscono messaggi a mano).
func (d *Display) SK() []byte {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]byte(nil), d.sk...)
}

// Frames restituisce tutti i frame ricevuti dal servizio, così come sono arrivati.
func (d *Display) Frames() []string {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]string(nil), d.frames...)
}

// Log restituisce tutti gli eventi visti finora.
func (d *Display) Log() []Event {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]Event(nil), d.log...)
}

func (d *Display) emit(e Event) {
	d.mu.Lock()
	d.log = append(d.log, e)
	d.mu.Unlock()
	select {
	case d.events <- e:
	default:
	}
}

// Await aspetta il primo evento di tipo typ (gli altri si scartano, restano in Log).
func (d *Display) Await(typ string, timeout time.Duration) (Event, error) {
	t := time.NewTimer(timeout)
	defer t.Stop()
	for {
		select {
		case e := <-d.events:
			if e.Type == typ {
				return e, nil
			}
		case <-t.C:
			return Event{}, fmt.Errorf("%s: evento %q non arrivato entro %s", d.Board, typ, timeout)
		case <-d.done:
			// gli eventi già in coda valgono ancora
			select {
			case e := <-d.events:
				if e.Type == typ {
					return e, nil
				}
				continue
			default:
			}
			return Event{}, fmt.Errorf("%s: connessione chiusa aspettando %q", d.Board, typ)
		}
	}
}

// Drain scarta gli eventi in coda.
func (d *Display) Drain() {
	for {
		select {
		case <-d.events:
		default:
			return
		}
	}
}

func (d *Display) loop() {
	defer close(d.done)
	for {
		_, b, err := d.ws.Read(context.Background())
		if err != nil {
			return
		}
		d.mu.Lock()
		d.frames = append(d.frames, string(b))
		d.mu.Unlock()
		var m map[string]any
		if json.Unmarshal(b, &m) != nil {
			continue
		}
		d.handle(m, b)
	}
}

func str(m map[string]any, k string) string { s, _ := m[k].(string); return s }

func (d *Display) handle(m map[string]any, raw []byte) {
	t := str(m, "t")
	if d.handleOTA(t, m, raw) { // ota.go: ota-key, ota-offer, ota-cancel, log-start, log-stop
		return
	}
	if d.handleWifi(t, m, raw) { // wifi.go: wifi-add, wifi-forget
		return
	}
	if d.handleWorkday(t, m, raw) { // workday.go: workday, set degli orari
		return
	}
	if d.handleThemes(t, m) { // themes.go: theme-offer, theme-use
		return
	}
	switch t {
	case "welcome":
		d.emit(Event{Type: t, Msg: m})
		if p, ok := m["paired"].(bool); ok {
			has := d.Key() != nil
			if p && !has {
				d.emit(Event{Type: "redo", Text: "il PC ha una chiave, il display no"})
			} else if !p && has {
				d.emit(Event{Type: "redo", Text: "il display ha una chiave, il PC no"})
			}
		}
	case "auth-challenge":
		ns, ok := m5.ParseHex(str(m, "ns"), m5.NonceLen)
		d.mu.Lock()
		if !ok || d.K == nil {
			d.mu.Unlock()
			d.emit(Event{Type: "redo", Text: "sfida senza chiave", Msg: m})
			return
		}
		d.ns, d.nd = ns, m5.Random(m5.NonceLen)
		d.sk = m5.SessionKey(d.K, d.ns, d.nd)
		d.authed = false
		mac := m5.AuthMac(d.K, d.Board, d.ns, d.nd)
		nd := d.nd
		d.mu.Unlock()
		d.emit(Event{Type: t, Msg: m})
		d.sendJSON(map[string]any{"t": "auth", "nd": m5.Hex(nd), "mac": m5.Hex(mac)})
	case "auth-ok":
		mac, ok := m5.ParseHex(str(m, "mac"), m5.MacLen)
		d.mu.Lock()
		good := ok && d.K != nil && d.ns != nil && m5.Equal(mac, m5.AuthOkMac(d.K, d.Board, d.ns, d.nd))
		d.authed = good
		d.mu.Unlock()
		if !good {
			d.emit(Event{Type: "redo", Text: "auth-ok con un mac sbagliato", Msg: m})
			return
		}
		d.emit(Event{Type: "authed", Msg: m})
		_ = d.Send(d.settingsMsg(true)) // come il firmware: dopo auth-ok le impostazioni remote valgono
		if d.th.opt.Enabled {
			d.sendThemes() // come il firmware: themes dopo auth-ok
		}
	case "auth-fail":
		d.mu.Lock()
		d.authed, d.sk = false, nil
		d.mu.Unlock()
		d.emit(Event{Type: t, Text: str(m, "reason"), Msg: m})
		d.emit(Event{Type: "redo", Text: "auth-fail", Msg: m})
	case "pair-begin":
		c, ok := m5.ParseHex(str(m, "commit"), 32)
		id := str(m, "id")
		if !ok || id == "" {
			d.emit(Event{Type: "rejected", Text: "pair-begin non valido", Msg: m})
			return
		}
		sk, pk, _ := m5.NewKeyPair(nil)
		d.mu.Lock()
		d.pairID, d.commit, d.skD, d.pkD, d.ready = id, c, sk, pk, false
		d.mu.Unlock()
		d.emit(Event{Type: t, Msg: m})
		d.sendJSON(map[string]any{"t": "pair-key", "id": id, "pkD": m5.Hex(pk)})
	case "pair-reveal":
		pkS, ok := m5.ParseHex(str(m, "pkS"), 32)
		d.mu.Lock()
		if !ok || d.pairID == "" || str(m, "id") != d.pairID {
			d.mu.Unlock()
			d.emit(Event{Type: "rejected", Text: "pair-reveal fuori sequenza", Msg: m})
			return
		}
		if !m5.Equal(m5.Commit(pkS), d.commit) {
			id := d.pairID
			d.pairID = ""
			d.mu.Unlock()
			d.sendJSON(map[string]any{"t": "pair-abort", "id": id, "reason": "impegno"})
			d.emit(Event{Type: "pair-abort", Text: "impegno", Msg: m})
			return
		}
		Z, err := m5.X25519(d.skD, pkS)
		if err != nil {
			d.mu.Unlock()
			return
		}
		d.T = m5.Transcript(d.Board, d.commit, d.pkD, pkS)
		d.Kc = m5.PairKey(Z, d.T)
		code := m5.SAS(Z, d.T)
		d.mu.Unlock()
		d.emit(Event{Type: "pair-code", Text: code, Msg: m})
	case "pair-confirm":
		mac, ok := m5.ParseHex(str(m, "mac"), m5.MacLen)
		d.mu.Lock()
		if d.pairID == "" || str(m, "id") != d.pairID || d.Kc == nil {
			d.mu.Unlock()
			d.emit(Event{Type: "rejected", Text: "pair-confirm fuori sequenza", Msg: m})
			return
		}
		if !ok || !m5.Equal(mac, m5.PairConfirmMac(d.Kc, d.T)) {
			id := d.pairID
			d.pairID = ""
			d.mu.Unlock()
			d.sendJSON(map[string]any{"t": "pair-abort", "id": id, "reason": "mac"})
			d.emit(Event{Type: "pair-abort", Text: "mac", Msg: m})
			return
		}
		d.ready = true
		d.mu.Unlock()
		d.emit(Event{Type: "pair-ready", Msg: m})
	case "pair-abort":
		d.mu.Lock()
		match := d.pairID != "" && str(m, "id") == d.pairID
		if match {
			d.pairID, d.Kc, d.ready = "", nil, false
		}
		d.mu.Unlock()
		if !match {
			d.emit(Event{Type: "rejected", Text: "pair-abort per un abbinamento che non c'è", Msg: m})
			return
		}
		d.emit(Event{Type: t, Text: str(m, "reason"), Msg: m})
	case "perm-request":
		var r m5.PermRequest
		_ = json.Unmarshal(raw, &r)
		d.mu.Lock()
		if !d.authed {
			d.mu.Unlock()
			d.emit(Event{Type: "rejected", Text: "perm-request senza autenticazione", Msg: m})
			return
		}
		if err := r.Verify(d.sk); err != nil {
			d.mu.Unlock()
			d.emit(Event{Type: "rejected", Text: "perm-request: " + err.Error(), Msg: m})
			return
		}
		if d.seen[r.ID] {
			d.mu.Unlock()
			d.emit(Event{Type: "rejected", Text: "perm-request ripetuta", Msg: m})
			return
		}
		d.seen[r.ID] = true
		d.mu.Unlock()
		d.emit(Event{Type: t, Msg: m, Req: &r})
	case "perm-close":
		var c m5.PermClose
		_ = json.Unmarshal(raw, &c)
		d.mu.Lock()
		authed, sk := d.authed, d.sk
		d.mu.Unlock()
		if !authed {
			d.emit(Event{Type: "rejected", Text: "perm-close senza autenticazione", Msg: m})
			return
		}
		if err := c.Verify(sk); err != nil {
			d.emit(Event{Type: "rejected", Text: "perm-close: " + err.Error(), Msg: m})
			return
		}
		d.emit(Event{Type: t, Text: c.Reason, Msg: m})
	default:
		d.emit(Event{Type: t, Msg: m})
	}
}

// Deliver fa elaborare al display un messaggio come se arrivasse dal servizio (test degli attacchi
// verso il display: perm-close falso, name alterato…).
func (d *Display) Deliver(raw string) {
	var m map[string]any
	if json.Unmarshal([]byte(raw), &m) == nil {
		d.handle(m, []byte(raw))
	}
}

// Confirm è la conferma fisica (2 pressioni) dopo pair-ready: pair-done e K salvata.
func (d *Display) Confirm() error {
	d.mu.Lock()
	if !d.ready || d.pairID == "" {
		d.mu.Unlock()
		return errors.New("niente da confermare")
	}
	mac := m5.PairDoneMac(d.Kc, d.T)
	id := d.pairID
	d.K = d.Kc
	d.pairID, d.Kc, d.ready, d.authed, d.sk = "", nil, false, false, nil
	d.mu.Unlock()
	d.sendJSON(map[string]any{"t": "pair-done", "id": id, "mac": m5.Hex(mac)})
	d.emit(Event{Type: "pair-done"})
	return nil
}

// CancelPair annulla dal display (pressione lunga): pair-abort con reason.
func (d *Display) CancelPair(reason string) {
	d.mu.Lock()
	id := d.pairID
	d.pairID, d.Kc, d.ready = "", nil, false
	d.mu.Unlock()
	if id != "" {
		d.sendJSON(map[string]any{"t": "pair-abort", "id": id, "reason": reason})
	}
}

// Reply risponde a una richiesta: perm-reply con nonce nuovo e mac. Restituisce il frame mandato
// (per i test di replica).
func (d *Display) Reply(id string, allow bool) string {
	d.mu.Lock()
	sk := d.sk
	d.mu.Unlock()
	decision := "deny"
	if allow {
		decision = "allow"
	}
	nonce := m5.Hex(m5.Random(m5.NonceLen))
	b, _ := json.Marshal(map[string]any{"t": "perm-reply", "v": 1, "id": id, "decision": decision, "nonce": nonce,
		"mac": m5.Hex(m5.RepMac(sk, id, decision, nonce))})
	_ = d.Send(string(b))
	return string(b)
}

// SetPermOn cambia «Permessi dal display» dal menu e manda settings.
func (d *Display) SetPermOn(on bool) {
	d.mu.Lock()
	d.permOn = on
	d.mu.Unlock()
	_ = d.Send(d.settingsMsg(d.Authed()))
}

// Unpair: «Dimentica abbinamento» dal menu (unpair firmato se autenticato, poi K cancellata).
func (d *Display) Unpair() {
	d.mu.Lock()
	authed, sk := d.authed, d.sk
	d.K, d.authed, d.sk = nil, false, nil
	d.mu.Unlock()
	if authed {
		d.sendJSON(map[string]any{"t": "unpair", "mac": m5.Hex(m5.UnpairMac(sk, d.Board))})
	}
}
