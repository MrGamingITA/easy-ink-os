package display

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"
	"unicode/utf8"

	"calamaio/internal/m5"
)

// Reti Wi-Fi della scheda gestite dal PC (docs/PROTOCOLLO.md «Reti Wi-Fi»):
//
//	display → servizio   wifi-list   {networks: [{ssid, current, rssi, lastUsed}]}     mai password
//	servizio → display   wifi-add    {id, ssid, iv, ct}     password cifrata con una chiave derivata da SK
//	servizio → display   wifi-forget {id, ssid, mac}        firmato con SK; dopo l'ack la scheda si riavvia
//	display → servizio   ack         {id, ok, error?}
//
// La password non entra mai nel log, in /api/status né su disco: resta in memoria il tempo di cifrarla.

// Limiti (802.11 e WPA2).
const (
	WifiSSIDMax   = 32 // byte dello SSID
	WifiPassMin   = 8  // passphrase WPA2: 8–63 caratteri
	WifiPassMax   = 63 // oltre: solo la chiave PSK di 64 cifre esadecimali
	WifiPSKHexLen = 64
	WifiListMax   = 16 // reti accettate in wifi-list (il firmware ne tiene 5: margine per versioni future)
	WifiRSSIMin   = -127
)

// Operazioni.
const (
	WifiOpAdd    = "add"
	WifiOpForget = "forget"
)

// Errori delle operazioni Wi-Fi prima dell'invio (HTTP 409 / 404).
var (
	ErrWifiBusy    = errors.New("c'è già un'operazione sulle reti Wi-Fi di questa scheda in corso: aspetta che finisca")
	ErrWifiUnknown = errors.New("la scheda non ha una rete salvata con questo nome")
	ErrWifiOTA     = errors.New("aggiornamento della scheda in corso: dimentica la rete quando ha finito (la scheda si riavvia)")
)

// WifiNetwork è una rete salvata sulla scheda, come in wifi-list.
type WifiNetwork struct {
	SSID     string `json:"ssid"`
	Current  bool   `json:"current"`  // la scheda è collegata a questa rete adesso
	RSSI     *int   `json:"rssi"`     // dBm, null = non vista nell'ultima scansione
	LastUsed *int64 `json:"lastUsed"` // ms epoch dell'ultimo collegamento riuscito, null = mai o non si sa
}

// WifiPending è l'operazione in attesa di ack (GET /api/status).
type WifiPending struct {
	ID    string `json:"id"`
	Op    string `json:"op"` // add · forget
	SSID  string `json:"ssid"`
	Until int64  `json:"until"` // ms epoch: oltre, senza ack l'operazione è fallita
}

// WifiResult è l'esito di un'operazione.
type WifiResult struct {
	ID      string `json:"id"`
	Op      string `json:"op"`
	SSID    string `json:"ssid"`
	OK      bool   `json:"ok"`
	Error   string `json:"error,omitempty"`   // frase in italiano (contiene il motivo della scheda, se c'è)
	Reason  string `json:"reason,omitempty"`  // motivo dell'ack negativo, come l'ha scritto la scheda
	Timeout bool   `json:"timeout,omitempty"` // nessun ack in tempo
	Reboot  bool   `json:"reboot,omitempty"`  // forget riuscito: la scheda si riavvia
	WasLast bool   `json:"wasLast,omitempty"` // forget dell'ultima rete: la scheda torna in configurazione
	At      int64  `json:"at"`
}

// WifiView è la parte `wifi` di un display in GET /api/status.
type WifiView struct {
	Networks []WifiNetwork `json:"networks"` // null = la scheda non ha mai mandato wifi-list
	At       int64         `json:"at,omitempty"`
	Pending  *WifiPending  `json:"pending"`
	Last     *WifiResult   `json:"last"`
}

// WifiAddMsg (servizio → display).
type WifiAddMsg struct {
	T    string `json:"t"`
	V    int    `json:"v"`
	ID   string `json:"id"`
	SSID string `json:"ssid"`
	IV   string `json:"iv"`
	CT   string `json:"ct"`
}

// WifiForgetMsg (servizio → display).
type WifiForgetMsg struct {
	T    string `json:"t"`
	V    int    `json:"v"`
	ID   string `json:"id"`
	SSID string `json:"ssid"`
	Mac  string `json:"mac"`
}

type boardWifi struct {
	networks []WifiNetwork // nil = mai ricevuta
	at       time.Time
	op       *wifiOp
	last     *WifiResult
}

type wifiOp struct {
	id, op, ssid string
	until        time.Time
	conn         *conn
	wasLast      bool
	done         chan struct{}
	res          WifiResult
}

func (h *Hub) wifiOf(board string) *boardWifi { // con h.mu
	if h.wifi == nil {
		h.wifi = map[string]*boardWifi{}
	}
	b := h.wifi[board]
	if b == nil {
		b = &boardWifi{}
		h.wifi[board] = b
	}
	return b
}

// wifiView: la vista per /api/status. Con h.mu.
func (h *Hub) wifiView(board string) *WifiView {
	v := &WifiView{}
	b := h.wifi[board]
	if b == nil {
		return v
	}
	if b.networks != nil {
		v.Networks = append([]WifiNetwork{}, b.networks...)
		v.At = b.at.UnixMilli()
	}
	if b.op != nil {
		v.Pending = &WifiPending{ID: b.op.id, Op: b.op.op, SSID: b.op.ssid, Until: b.op.until.UnixMilli()}
	}
	if b.last != nil {
		l := *b.last
		v.Last = &l
	}
	return v
}

// WifiOf restituisce le reti di board come le conosce il servizio (anche se la scheda è scollegata).
func (h *Hub) WifiOf(board string) *WifiView {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.wifiView(board)
}

// ---------- wifi-list ----------

type wifiListMsg struct {
	Networks *[]WifiNetwork `json:"networks"`
}

// parseWifiList controlla wifi-list. Una lista non valida si scarta tutta.
func parseWifiList(data []byte) ([]WifiNetwork, bool) {
	var m wifiListMsg
	if err := json.Unmarshal(data, &m); err != nil || m.Networks == nil || len(*m.Networks) > WifiListMax {
		return nil, false
	}
	// password o altri campi segreti non sono previsti: un campo sconosciuto non si conserva comunque
	// (si tengono solo ssid, current, rssi, lastUsed).
	out := make([]WifiNetwork, 0, len(*m.Networks))
	for _, n := range *m.Networks {
		if n.SSID == "" || len(n.SSID) > WifiSSIDMax || !utf8.ValidString(n.SSID) {
			return nil, false
		}
		if n.RSSI != nil && (*n.RSSI < WifiRSSIMin || *n.RSSI > 0) {
			return nil, false
		}
		if n.LastUsed != nil && *n.LastUsed < 0 {
			return nil, false
		}
		out = append(out, n)
	}
	return out, true
}

func (h *Hub) onWifiList(c *conn, list []WifiNetwork) {
	h.mu.Lock()
	b := h.wifiOf(c.info.Board)
	b.networks, b.at = list, h.Now()
	h.mu.Unlock()
	h.changed()
}

// ---------- controlli ----------

func hasControl(s string) bool {
	for _, r := range s {
		if r < 0x20 || r == 0x7f {
			return true
		}
	}
	return false
}

// ValidateWifiSSID: 1–32 byte UTF-8, senza caratteri di controllo (0x1F separa i campi dell'aad e del mac).
func ValidateWifiSSID(ssid string) error {
	switch {
	case ssid == "":
		return invalid("manca il nome della rete (SSID)")
	case !utf8.ValidString(ssid) || hasControl(ssid):
		return invalid("il nome della rete contiene caratteri non ammessi")
	case len(ssid) > WifiSSIDMax:
		return invalid("nome della rete troppo lungo: al massimo %d byte (ne ha %d)", WifiSSIDMax, len(ssid))
	}
	return nil
}

// ValidateWifiPassword: passphrase WPA2 di 8–63 byte UTF-8 senza caratteri di controllo (la scheda vuole
// 8–63 byte senza byte zero; la chiave PSK di 64 cifre esadecimali non è ammessa). I messaggi d'errore non
// contengono mai la password.
func ValidateWifiPassword(pw []byte) error {
	switch {
	case len(pw) == 0:
		return invalid("serve la password: dal PC si aggiungono solo reti protette (WPA2/WPA3)")
	case !utf8.Valid(pw) || hasControl(string(pw)):
		return invalid("la password contiene caratteri non ammessi (caratteri di controllo o UTF-8 non valido)")
	case len(pw) < WifiPassMin:
		return invalid("password troppo corta: almeno %d caratteri (WPA2)", WifiPassMin)
	case len(pw) > WifiPassMax:
		return invalid("password troppo lunga: al massimo %d byte", WifiPassMax)
	}
	return nil
}

// ---------- wifi-add, wifi-forget ----------

// WifiAdd manda la rete alla scheda (password cifrata) e aspetta l'ack, al massimo AckTimeout.
// password si azzera prima di tornare. Errori prima dell'invio: *InvalidError (400), ErrNotConnected,
// ErrNotAuthed, ErrWifiBusy, ErrBusy (409), ErrStopping (503). Dopo l'invio l'esito è in WifiResult
// (OK falso: rifiutata, scollegata o senza ack); err = ctx.Err() se chi chiama smette di aspettare.
func (h *Hub) WifiAdd(ctx context.Context, board, ssid string, password []byte) (WifiResult, error) {
	defer clear(password)
	if err := ValidateWifiSSID(ssid); err != nil {
		return WifiResult{}, err
	}
	if err := ValidateWifiPassword(password); err != nil {
		return WifiResult{}, err
	}
	return h.wifiSend(ctx, board, WifiOpAdd, ssid, password)
}

// WifiForget chiede alla scheda di dimenticare ssid e aspetta l'ack. Se la scheda ha già mandato
// wifi-list, ssid deve esserci (ErrWifiUnknown, 404). Con l'ack positivo la scheda si riavvia.
func (h *Hub) WifiForget(ctx context.Context, board, ssid string) (WifiResult, error) {
	if err := ValidateWifiSSID(ssid); err != nil {
		return WifiResult{}, err
	}
	return h.wifiSend(ctx, board, WifiOpForget, ssid, nil)
}

// wifiSend prepara, manda e aspetta (i controlli su ssid e password li fanno WifiAdd e WifiForget: i test
// lo chiamano direttamente per provare i rifiuti della scheda).
func (h *Hub) wifiSend(ctx context.Context, board, op, ssid string, password []byte) (WifiResult, error) {
	h.mu.Lock()
	if h.stopping || h.closed || h.farewell {
		h.mu.Unlock()
		return WifiResult{}, ErrStopping
	}
	c := h.target(board)
	if c == nil {
		h.mu.Unlock()
		return WifiResult{}, ErrNotConnected
	}
	if !c.authed || c.sk == nil {
		h.mu.Unlock()
		return WifiResult{}, ErrNotAuthed
	}
	b := h.wifiOf(board)
	if b.op != nil {
		h.mu.Unlock()
		return WifiResult{}, ErrWifiBusy
	}
	wasLast := false
	if op == WifiOpForget {
		if j := h.otaJobs[board]; j != nil && !otaFinal(j.stage) {
			h.mu.Unlock()
			return WifiResult{}, ErrWifiOTA
		}
		if b.networks != nil {
			if !hasSSID(b.networks, ssid) {
				h.mu.Unlock()
				return WifiResult{}, ErrWifiUnknown
			}
			wasLast = len(b.networks) == 1
		}
	}
	h.nextCmd++
	id := fmt.Sprintf("c%d", h.nextCmd)
	var msg any
	if op == WifiOpAdd {
		iv := m5.Random(m5.WifiIVLen)
		ct := m5.WifiSeal(c.sk, id, ssid, iv, password)
		msg = WifiAddMsg{T: "wifi-add", V: ProtocolVersion, ID: id, SSID: ssid, IV: m5.Hex(iv), CT: m5.Hex(ct)}
	} else {
		msg = WifiForgetMsg{T: "wifi-forget", V: ProtocolVersion, ID: id, SSID: ssid, Mac: m5.Hex(m5.WifiForgetMac(c.sk, id, ssid))}
	}
	select {
	case c.out <- msg:
	default:
		h.mu.Unlock()
		return WifiResult{}, ErrBusy
	}
	o := &wifiOp{id: id, op: op, ssid: ssid, until: h.Now().Add(h.AckTimeout), conn: c, wasLast: wasLast, done: make(chan struct{})}
	b.op = o
	c.cmds[id] = &command{kind: "wifi-" + op, wifi: true, timer: time.AfterFunc(h.AckTimeout, func() { h.expire(c, id) })}
	h.mu.Unlock()
	if op == WifiOpAdd {
		h.Log.Printf("ws: #%d wifi-add %s «%s» (password cifrata)", c.info.ID, id, ssid)
	} else {
		h.Log.Printf("ws: #%d wifi-forget %s «%s»", c.info.ID, id, ssid)
	}
	h.changed()

	select {
	case <-o.done:
		return o.res, nil
	case <-ctx.Done():
		return WifiResult{ID: id, Op: op, SSID: ssid}, ctx.Err()
	}
}

// wifiFinish chiude l'operazione id della connessione c con l'esito res (Error già composto) e sveglia
// chi aspetta. Con h.mu; false se l'operazione non c'è più.
func (h *Hub) wifiFinish(c *conn, id string, res WifiResult) bool {
	b := h.wifi[c.info.Board]
	if b == nil || b.op == nil || b.op.id != id || b.op.conn != c {
		return false
	}
	o := b.op
	res.ID, res.Op, res.SSID, res.At = o.id, o.op, o.ssid, h.Now().UnixMilli()
	if res.OK {
		switch o.op {
		case WifiOpAdd:
			// la scheda rimanda wifi-list; intanto la rete compare nell'elenco (senza segnale).
			if b.networks != nil && !hasSSID(b.networks, o.ssid) {
				b.networks = append(b.networks, WifiNetwork{SSID: o.ssid})
			}
		case WifiOpForget:
			res.Reboot, res.WasLast = true, o.wasLast
			if b.networks != nil {
				kept := []WifiNetwork{}
				for _, n := range b.networks {
					if n.SSID != o.ssid {
						kept = append(kept, n)
					}
				}
				b.networks = kept
			}
		}
	}
	o.res = res
	last := res
	b.op, b.last = nil, &last
	close(o.done)
	return true
}

func hasSSID(list []WifiNetwork, ssid string) bool {
	for _, n := range list {
		if n.SSID == ssid {
			return true
		}
	}
	return false
}

// pendingOp: l'operazione in attesa di c con quell'id. Con h.mu.
func (h *Hub) pendingOp(c *conn, id string) *wifiOp {
	if b := h.wifi[c.info.Board]; b != nil && b.op != nil && b.op.id == id && b.op.conn == c {
		return b.op
	}
	return nil
}

// wifiAcked: ack della scheda per un'operazione Wi-Fi. Con h.mu.
func (h *Hub) wifiAcked(c *conn, id string, ok bool, why string) {
	o := h.pendingOp(c, id)
	if o == nil {
		return
	}
	res := WifiResult{OK: ok}
	switch {
	case ok && o.op == WifiOpForget:
		h.Log.Printf("ws: #%d wifi-forget %s: rete «%s» dimenticata, la scheda si riavvia", c.info.ID, id, o.ssid)
	case ok:
		h.Log.Printf("ws: #%d wifi-add %s: rete «%s» salvata dalla scheda", c.info.ID, id, o.ssid)
	default:
		if why = strings.TrimSpace(why); why == "" {
			why = "senza motivo"
		}
		verb := "aggiunto"
		if o.op == WifiOpForget {
			verb = "dimenticato"
		}
		res.Reason = why
		res.Error = fmt.Sprintf("la scheda non ha %s la rete «%s»: %s", verb, o.ssid, why)
		h.Log.Printf("ws: #%d wifi-%s %s rifiutato dalla scheda: %s", c.info.ID, o.op, id, why)
	}
	h.wifiFinish(c, id, res)
}

// wifiExpired: nessun ack entro AckTimeout. Con h.mu.
func (h *Hub) wifiExpired(c *conn, id string) {
	o := h.pendingOp(c, id)
	if o == nil {
		return
	}
	what := "la rete potrebbe non essere stata aggiunta (il firmware della scheda gestisce più reti?)"
	if o.op == WifiOpForget {
		what = "la rete potrebbe non essere stata dimenticata"
	}
	h.Log.Printf("ws: #%d nessun ack per wifi-%s %s entro %s", c.info.ID, o.op, id, h.AckTimeout)
	h.wifiFinish(c, id, WifiResult{Timeout: true,
		Error: fmt.Sprintf("la scheda non ha confermato entro %s: %s", h.AckTimeout.Round(time.Second), what)})
}

// wifiConnClosed: la connessione si è chiusa con un'operazione in attesa. Con h.mu.
func (h *Hub) wifiConnClosed(c *conn) {
	b := h.wifi[c.info.Board]
	if b == nil || b.op == nil || b.op.conn != c {
		return
	}
	text := "la scheda si è scollegata prima di confermare: la rete potrebbe non essere stata aggiunta"
	if b.op.op == WifiOpForget {
		text = "la scheda si è scollegata prima di confermare: se si riavvia, la rete è stata dimenticata"
	}
	h.wifiFinish(c, b.op.id, WifiResult{Error: text})
}
