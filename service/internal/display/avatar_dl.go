package display

import (
	"encoding/json"
	"fmt"
	"net/http"
	"slices"
	"time"

	"calamaio/internal/m5"
)

// Avatar a pezzi (firmware 0.14.0, docs/PROTOCOLLO.md «Avatar»). A una scheda che in hello dice caps ["avatar-dl"]
// il servizio non manda il CAVT in base64 dentro il WebSocket (9,5 KB in un blocco solo per la scheda, che dopo il
// Bluetooth spesso non li ha): offre l'avatar come un tema e la scheda lo scarica con GET /avatars/<token> a pezzi,
// direttamente nella cache. Le schede senza la capacità ricevono ancora `avatar`.
//
//	servizio → display   avatar-offer {id, crc, size, token}   una alla volta per connessione, anche senza auth
//	display → servizio   avatar-ack   {id, crc, ok, error?}    dopo il download, i controlli e il salvataggio
//
// Niente autenticazione, come avatar-need e `avatar`: un avatar non è un dato sensibile e non cambia niente sulla
// scheda oltre alla cache. Il token (16 byte casuali, monouso, solo per l'IP di quella connessione) dice soltanto
// «questa connessione l'ha chiesto»; muore con l'ack, con la chiusura della connessione e alla scadenza dell'ack.

// CapAvatarDl: capacità di hello per l'avatar a pezzi.
const CapAvatarDl = "avatar-dl"

// avatarFailHold: dopo una consegna fallita (ack negativo, nessun ack, connessione chiusa a metà, o con `avatar`
// connessione chiusa subito dopo l'invio) lo stesso avatar (id e CRC) non si rimanda alla stessa scheda per questo
// tempo, anche su connessioni nuove: è il ciclo visto dal vivo (1011 → riconnessione → avatar di nuovo, 1 al secondo).
const avatarFailHold = 60 * time.Second

// avatarLegacyGrace: con `avatar`, una connessione che si chiude entro questo tempo dall'invio conta come fallimento.
const avatarLegacyGrace = 10 * time.Second

type avatarOfferOut struct {
	T     string `json:"t"`
	V     int    `json:"v"`
	ID    string `json:"id"`
	CRC   uint32 `json:"crc"`
	Size  int    `json:"size"`
	Token string `json:"token"`
}

type avatarAckMsg struct {
	ID    string  `json:"id"`
	CRC   *uint32 `json:"crc"`
	OK    *bool   `json:"ok"`
	Error string  `json:"error"`
}

// avatarItem: un avatar da offrire (binario fissato quando è stato chiesto).
type avatarItem struct {
	id  string
	crc uint32
	bin []byte
}

type avatarOffer struct {
	avatarItem
	token string
	timer *time.Timer
}

type avatarToken struct {
	conn *conn
	item avatarItem
	ip   string
	exp  time.Time
	used bool
}

type avatarLegacySent struct {
	id  string
	crc uint32
	at  time.Time
}

func parseAvatarAck(data []byte) (*avatarAckMsg, bool) {
	var m avatarAckMsg
	if json.Unmarshal(data, &m) != nil || len(m.ID) > 24 || !avatarIDRe.MatchString(m.ID) || m.CRC == nil || m.OK == nil {
		return nil, false
	}
	m.Error = cleanField(m.Error, 127)
	return &m, true
}

func hasCap(caps []string, c string) bool { return slices.Contains(caps, c) }

func avatarKey(id string, crc uint32) string { return fmt.Sprintf("%s/%08x", id, crc) }

// avatarHeld: id e crc falliti da poco su questa scheda (h.AvatarFailHold). Con h.mu.
func (h *Hub) avatarHeld(board, id string, crc uint32) bool {
	until, ok := h.avFail[board][avatarKey(id, crc)]
	if ok && !h.Now().Before(until) {
		delete(h.avFail[board], avatarKey(id, crc))
		return false
	}
	return ok
}

// avatarFailBoards: tetto di schede con blocchi in memoria.
const avatarFailBoards = 64

// avatarPrune toglie i blocchi scaduti e le schede rimaste senza. Con h.mu.
func (h *Hub) avatarPrune() {
	now := h.Now()
	for b, m := range h.avFail {
		for k, until := range m {
			if !now.Before(until) {
				delete(m, k)
			}
		}
		if len(m) == 0 {
			delete(h.avFail, b)
		}
	}
}

// avatarFailed segna la consegna fallita. Con h.mu.
func (h *Hub) avatarFailed(board, id string, crc uint32, why string) {
	if board == "" {
		return
	}
	if h.avFail == nil {
		h.avFail = map[string]map[string]time.Time{}
	}
	h.avatarPrune()
	if _, ok := h.avFail[board]; !ok && len(h.avFail) >= avatarFailBoards {
		// tetto: il board arriva dall'hello non autenticato; si scarta la scheda col blocco più vecchio
		var oldest string
		var oldestAt time.Time
		for b, m := range h.avFail {
			var last time.Time
			for _, u := range m {
				if u.After(last) {
					last = u
				}
			}
			if oldest == "" || last.Before(oldestAt) {
				oldest, oldestAt = b, last
			}
		}
		delete(h.avFail, oldest)
	}
	if h.avFail[board] == nil {
		h.avFail[board] = map[string]time.Time{}
	}
	h.avFail[board][avatarKey(id, crc)] = h.Now().Add(h.AvatarFailHold)
	h.Log.Printf("avatar: %s (crc %08x) non consegnato a %s: %s; niente rinvii per %s", id, crc, board, why, h.AvatarFailHold)
}

// avatarQueue mette in coda un avatar per una scheda con avatar-dl (un'offerta alla volta). Con h.mu.
func (h *Hub) avatarQueue(c *conn, it avatarItem) {
	if c.avOffer != nil && c.avOffer.id == it.id && c.avOffer.crc == it.crc {
		return
	}
	for _, q := range c.avQueue {
		if q.id == it.id && q.crc == it.crc {
			return
		}
	}
	c.avQueue = append(c.avQueue, it)
	h.avatarNext(c)
}

// avatarNext offre il prossimo avatar in coda, se non c'è un'offerta in corso. Con h.mu.
func (h *Hub) avatarNext(c *conn) {
	for c.avOffer == nil && len(c.avQueue) > 0 {
		it := c.avQueue[0]
		c.avQueue = c.avQueue[1:]
		if h.avatarHeld(c.info.Board, it.id, it.crc) {
			continue
		}
		tok := m5.Hex(m5.Random(16))
		if !h.enqueueOut(c, avatarOfferOut{T: "avatar-offer", V: ProtocolVersion, ID: it.id, CRC: it.crc, Size: len(it.bin), Token: tok}) {
			// coda piena: fallita, vale il blocco di 60 s (niente rinvio a raffica) e si passa al prossimo
			h.avatarFailed(c.info.Board, it.id, it.crc, "coda di uscita piena")
			continue
		}
		if h.avTokens == nil {
			h.avTokens = map[string]*avatarToken{}
		}
		h.avTokens[tok] = &avatarToken{conn: c, item: it, ip: c.info.IP, exp: h.Now().Add(h.TokenTTL)}
		o := &avatarOffer{avatarItem: it, token: tok}
		o.timer = time.AfterFunc(h.AvatarAckTimeout, func() {
			h.mu.Lock()
			defer h.mu.Unlock()
			if c.avOffer == o {
				h.avatarEnd(c, "nessun avatar-ack entro "+h.AvatarAckTimeout.String())
			}
		})
		c.avOffer = o
		h.Log.Printf("ws: #%d avatar-offer %s (crc %08x, %d byte)", c.info.ID, it.id, it.crc, len(it.bin))
	}
}

// avatarEnd chiude l'offerta in corso (why non vuoto = fallita) e passa alla prossima. Con h.mu.
func (h *Hub) avatarEnd(c *conn, why string) {
	o := c.avOffer
	if o == nil {
		return
	}
	o.timer.Stop()
	delete(h.avTokens, o.token)
	c.avOffer = nil
	if why != "" {
		h.avatarFailed(c.info.Board, o.id, o.crc, why)
	}
	h.avatarNext(c)
}

func (h *Hub) onAvatarAck(c *conn, m *avatarAckMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	o := c.avOffer
	if o == nil || o.id != m.ID || o.crc != *m.CRC {
		h.Log.Printf("ws: #%d avatar-ack %s senza un'offerta in corso: ignorato", c.info.ID, m.ID)
		return
	}
	if !*m.OK {
		h.avatarEnd(c, "la scheda l'ha rifiutato: "+m.Error)
		return
	}
	h.Log.Printf("ws: #%d avatar %s salvato dalla scheda", c.info.ID, m.ID)
	h.avatarEnd(c, "")
}

// avatarConnClosed: offerta a metà e `avatar` mandati da poco contano come falliti. Con h.mu.
func (h *Hub) avatarConnClosed(c *conn) {
	if c.avOffer != nil {
		c.avQueue = nil
		h.avatarEnd(c, "connessione chiusa durante la consegna")
	}
	for _, s := range c.avLegacy {
		if h.Now().Sub(s.at) < avatarLegacyGrace {
			h.avatarFailed(c.info.Board, s.id, s.crc, "connessione chiusa subito dopo il messaggio avatar")
		}
	}
	c.avLegacy = nil
}

// ServeAvatar: GET /avatars/{token} sulla porta dei display, stesse regole dei temi (blobTokenStatus).
func (h *Hub) ServeAvatar(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	t := h.avTokens[tok]
	var exp time.Time
	var want string
	if t != nil {
		exp, want = t.exp, t.ip
	}
	code := blobTokenStatus(tok, t != nil, t != nil && t.used, exp, ip, want, h.Now())
	if code != 0 {
		if code == http.StatusGone && t != nil && !t.used {
			delete(h.avTokens, tok)
		}
		h.mu.Unlock()
		h.Log.Printf("avatar: download da %s rifiutato (%d)", ip, code)
		http.Error(w, http.StatusText(code), code)
		return
	}
	head := r.Method == http.MethodHead
	if !head { // HEAD non consuma il token
		t.used = true
	}
	it := t.item
	h.mu.Unlock()
	serveBlob(w, r, it.bin, it.crc)
	if head {
		return
	}
	h.Log.Printf("avatar: %s scaricato da %s (%d byte)", it.id, ip, len(it.bin))
}

// blobTokenStatus: regole comuni dei download con token (temi, avatar; come ServeOTA). 404 token malformato o
// sconosciuto, 410 già usato o scaduto, 403 IP diverso da quello della scheda (il token resta valido). 0 = servire.
func blobTokenStatus(tok string, known, used bool, exp time.Time, ip, want string, now time.Time) int {
	switch {
	case len(tok) != 32 || !m5.IsLowerHex(tok) || !known:
		return http.StatusNotFound
	case used, now.After(exp):
		return http.StatusGone
	case ip != want:
		return http.StatusForbidden
	}
	return 0
}

// serveBlob: 200 application/octet-stream con Content-Length, X-Calamaio-Crc e no-store; HEAD solo le intestazioni.
func serveBlob(w http.ResponseWriter, r *http.Request, bin []byte, crc uint32) {
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Content-Length", fmt.Sprint(len(bin)))
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Crc", fmt.Sprintf("%08x", crc))
	w.WriteHeader(http.StatusOK)
	if r.Method != http.MethodHead {
		_, _ = w.Write(bin)
	}
}
