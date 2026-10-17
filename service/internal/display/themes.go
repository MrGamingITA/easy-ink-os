package display

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"calamaio/internal/m5"
)

// Temi delle schermate (docs/PROTOCOLLO.md «Temi»). Il servizio offre un tema solo a una scheda autenticata che ha
// mandato `themes`; la scheda lo scarica con GET /themes/<token> su questa porta (token monouso, TokenTTL, solo per
// l'IP della scheda: le stesse regole di ServeOTA), lo controlla, risponde theme-ack; poi theme-use lo attiva.
//
//	display → servizio   themes       {have: [{id, crc}] (0..8), cur: id | null}     dopo auth-ok e a ogni cambio
//	servizio → display   theme-offer  {id, crc, size, token}                          solo autenticato
//	display → servizio   theme-ack    {id, ok, error?}                                dopo il download e i controlli
//	servizio → display   theme-use    {id | null}                                     solo autenticato

const (
	ThemeWaiting    = "attesa"
	ThemeOffered    = "offerta"
	ThemeDownloaded = "scaricato"
	ThemeUsing      = "uso"
	ThemeDone       = "fatto"
	ThemeFailed     = "errore"
)

// ThemeHave è un tema salvato sulla scheda, come in `themes`.
type ThemeHave struct {
	ID  string `json:"id"`
	CRC uint32 `json:"crc"`
}

// ThemePayload: il tema da mandare (CTHM già compilato); nil = tema di serie.
type ThemePayload struct {
	ID, Name, Screen string
	CRC              uint32
	Bin              []byte
}

// ThemeJobView è un'operazione «manda il tema» in GET /api/status e nella risposta della API.
type ThemeJobView struct {
	ID      string  `json:"id"`
	Board   string  `json:"board"`
	Theme   *string `json:"theme"` // null = tema di serie
	Name    string  `json:"name"`
	CRC     uint32  `json:"crc"`
	Stage   string  `json:"stage"`
	Error   string  `json:"error,omitempty"`
	Started int64   `json:"started"`
	Updated int64   `json:"updated"`
}

// ThemesView è la parte `themes` di un display in GET /api/status.
type ThemesView struct {
	Supported bool          `json:"supported"` // la scheda autenticata ha mandato `themes` su questa connessione
	Have      []ThemeHave   `json:"have"`
	Cur       *string       `json:"cur"`
	Job       *ThemeJobView `json:"job"`
}

type themeJob struct {
	id, board, stage, err string
	want                  *ThemePayload
	conn                  *conn // connessione che ha ricevuto offerta o theme-use (nil in attesa)
	token                 string
	started, updated      time.Time
	timer                 *time.Timer
	gen                   int
	reoffers              int // offerte di nuovo dopo un theme-use senza esito (al massimo themeMaxReoffers)
}

// themeMaxReoffers: una scheda che a ogni theme-use risponde themes senza il tema non fa girare offerta → download
// → theme-use all'infinito.
const themeMaxReoffers = 2

type boardThemes struct {
	from *conn // connessione autenticata che ha mandato l'ultimo themes (nil = nessuna)
	have []ThemeHave
	cur  *string
	job  *themeJob
}

type themeToken struct {
	job  *themeJob
	ip   string
	exp  time.Time
	used bool
}

type themesMsg struct {
	Have []ThemeHave `json:"have"`
	Cur  *string     `json:"cur"`
}

type themeAckMsg struct {
	ID    string `json:"id"`
	OK    *bool  `json:"ok"`
	Error string `json:"error"`
}

type themeOfferOut struct {
	T     string `json:"t"`
	V     int    `json:"v"`
	ID    string `json:"id"`
	CRC   uint32 `json:"crc"`
	Size  int    `json:"size"`
	Token string `json:"token"`
}

type themeUseOut struct {
	T  string  `json:"t"`
	V  int     `json:"v"`
	ID *string `json:"id"`
}

// parseThemes controlla `themes` come lo schema: have (array, 0..8, voci {id, crc}) e cur (id o null) obbligatori,
// nessun campo in più. Un messaggio non valido si scarta tutto.
func parseThemes(data []byte) (*themesMsg, bool) {
	var raw map[string]json.RawMessage
	if json.Unmarshal(data, &raw) != nil || raw["have"] == nil || raw["cur"] == nil {
		return nil, false
	}
	for k := range raw {
		if k != "t" && k != "v" && k != "have" && k != "cur" {
			return nil, false
		}
	}
	var items []map[string]json.RawMessage
	if json.Unmarshal(raw["have"], &items) != nil || items == nil || len(items) > 8 {
		return nil, false
	}
	m := &themesMsg{Have: make([]ThemeHave, 0, len(items))}
	for _, it := range items {
		var x ThemeHave
		if len(it) != 2 || it["id"] == nil || it["crc"] == nil ||
			json.Unmarshal(it["id"], &x.ID) != nil || json.Unmarshal(it["crc"], &x.CRC) != nil || !themeIDOK(x.ID) {
			return nil, false
		}
		m.Have = append(m.Have, x)
	}
	if string(raw["cur"]) != "null" {
		var cur string
		if json.Unmarshal(raw["cur"], &cur) != nil || !themeIDOK(cur) {
			return nil, false
		}
		m.Cur = &cur
	}
	return m, true
}

func parseThemeAck(data []byte) (*themeAckMsg, bool) {
	var m themeAckMsg
	if json.Unmarshal(data, &m) != nil || !themeIDOK(m.ID) || m.OK == nil {
		return nil, false
	}
	m.Error = cleanField(m.Error, 127)
	return &m, true
}

// themeIDOK: ^[a-z0-9]+(-[a-z0-9]+)*$, al massimo 24 caratteri (come lo schema).
func themeIDOK(id string) bool {
	if len(id) < 1 || len(id) > 24 {
		return false
	}
	for i := 0; i < len(id); i++ {
		c := id[i]
		switch {
		case c >= 'a' && c <= 'z', c >= '0' && c <= '9':
		case c == '-' && i > 0 && i < len(id)-1 && id[i-1] != '-':
		default:
			return false
		}
	}
	return true
}

// Con h.mu.
func (h *Hub) themeInit() {
	if h.themes == nil {
		h.themes = map[string]*boardThemes{}
		h.themeTokens = map[string]*themeToken{}
	}
}

// Con h.mu.
func (h *Hub) boardThemes(board string) *boardThemes {
	h.themeInit()
	bt := h.themes[board]
	if bt == nil {
		bt = &boardThemes{}
		h.themes[board] = bt
	}
	return bt
}

func themeFinal(stage string) bool { return stage == ThemeDone || stage == ThemeFailed }

func (j *themeJob) view() *ThemeJobView {
	if j == nil {
		return nil
	}
	v := &ThemeJobView{ID: j.id, Board: j.board, Stage: j.stage, Error: j.err, Started: j.started.UnixMilli(), Updated: j.updated.UnixMilli()}
	if j.want != nil {
		id := j.want.ID
		v.Theme, v.Name, v.CRC = &id, j.want.Name, j.want.CRC
	}
	return v
}

// SendTheme manda il tema p (nil = di serie) alla scheda e lo attiva. La scheda abbinata può essere scollegata
// (partita, riavvio): l'operazione aspetta in «attesa» e riparte quando la scheda torna e si autentica.
// Sostituisce un'operazione in corso.
func (h *Hub) SendTheme(board string, p *ThemePayload) (*ThemeJobView, error) {
	h.mu.Lock()
	defer h.mu.Unlock()
	c := h.target(board)
	if c == nil && h.keys[board] == nil {
		return nil, ErrNotConnected
	}
	if p != nil && p.Screen != "250x122" {
		return nil, &BlockedError{Msg: "il tema è per uno schermo " + p.Screen + ": questa scheda è 250×122"}
	}
	if c != nil && p != nil && (c.info.W != 250 || c.info.H != 122) {
		return nil, &BlockedError{Msg: fmt.Sprintf("il tema è per uno schermo 250×122, la scheda %s è %d×%d", board, c.info.W, c.info.H)}
	}
	bt := h.boardThemes(board)
	if bt.job != nil {
		h.themeEnd(bt.job, "", "")
	}
	now := h.Now()
	bt.job = &themeJob{id: "t" + m5.Hex(m5.Random(6)), board: board, want: p, stage: ThemeWaiting, started: now, updated: now}
	h.themeStep(c, bt)
	go h.changed()
	return bt.job.view(), nil
}

// ThemeOf restituisce l'operazione in corso o l'ultima della scheda (nil = nessuna).
func (h *Hub) ThemeOf(board string) *ThemeJobView {
	h.mu.Lock()
	defer h.mu.Unlock()
	if bt := h.themes[board]; bt != nil {
		return bt.job.view()
	}
	return nil
}

func (bt *boardThemes) has(p *ThemePayload) bool {
	for _, x := range bt.have {
		if x.ID == p.ID && x.CRC == p.CRC {
			return true
		}
	}
	return false
}

// themeStep fa il passo dovuto dell'operazione in corso. Con h.mu; c può essere nil (scheda scollegata).
func (h *Hub) themeStep(c *conn, bt *boardThemes) {
	j := bt.job
	if j == nil || themeFinal(j.stage) {
		return
	}
	// §7: offerta e theme-use solo a una scheda autenticata che ha mandato themes su questa connessione
	if c == nil || !c.authed || bt.from != c {
		h.themeReset(j)
		return
	}
	if j.conn != nil && j.conn != c { // offerta partita su una connessione sostituita: si ricomincia
		h.themeReset(j)
	}
	switch {
	case j.want == nil && bt.cur == nil, j.want != nil && bt.cur != nil && *bt.cur == j.want.ID && bt.has(j.want):
		h.themeEnd(j, ThemeDone, "")
		h.Log.Printf("temi %s: %s attivo su %s", j.id, themeName(j.want), j.board)
	case j.stage == ThemeUsing && (j.want == nil || bt.has(j.want)):
		// theme-use già mandato: il themes che segue il theme-ack ha il tema ma non ancora come attivo. Si aspetta il
		// themes della risposta (o la scadenza): rimandarlo farebbe due attivazioni e riarmerebbe il timer.
	case j.want == nil || bt.has(j.want):
		var id *string
		if j.want != nil {
			s := j.want.ID
			id = &s
		}
		if h.enqueueOut(c, themeUseOut{T: "theme-use", V: ProtocolVersion, ID: id}) {
			j.conn = c
			h.themeStage(j, ThemeUsing)
			h.themeArm(j, h.ThemeUseTimeout, "la scheda non ha attivato il tema")
			h.Log.Printf("temi %s: theme-use %s a %s", j.id, themeName(j.want), j.board)
		}
	case j.stage == ThemeOffered || j.stage == ThemeDownloaded:
		// offerta già partita: si aspetta theme-ack
	default:
		if j.stage == ThemeUsing { // la scheda non ha il tema che doveva attivare: riofferta
			if j.reoffers >= themeMaxReoffers {
				why := "la scheda non ha attivato il tema"
				h.themeEnd(j, ThemeFailed, why)
				h.Log.Printf("temi %s: %s dopo %d riofferte", j.id, why, j.reoffers)
				return
			}
			j.reoffers++
		}
		tok := m5.Hex(m5.Random(16))
		if !h.enqueueOut(c, themeOfferOut{T: "theme-offer", V: ProtocolVersion, ID: j.want.ID, CRC: j.want.CRC, Size: len(j.want.Bin), Token: tok}) {
			return
		}
		j.conn, j.token = c, tok
		h.themeTokens[tok] = &themeToken{job: j, ip: c.info.IP, exp: h.Now().Add(h.TokenTTL)}
		h.themeStage(j, ThemeOffered)
		h.themeArm(j, h.ThemeAckTimeout, "la scheda non ha risposto all'offerta del tema")
		h.Log.Printf("temi %s: offerta di %s (%d byte, crc %08x) a %s", j.id, j.want.ID, len(j.want.Bin), j.want.CRC, j.board)
	}
}

func themeName(p *ThemePayload) string {
	if p == nil {
		return "il tema di serie"
	}
	return p.ID
}

// Con h.mu.
func (h *Hub) themeStage(j *themeJob, stage string) {
	if j.stage != stage {
		j.stage, j.updated = stage, h.Now()
		go h.changed()
	}
}

// themeArm arma la scadenza della fase. Con h.mu.
func (h *Hub) themeArm(j *themeJob, d time.Duration, why string) {
	j.gen++
	gen := j.gen
	if j.timer != nil {
		j.timer.Stop()
	}
	j.timer = time.AfterFunc(d, func() {
		h.mu.Lock()
		defer h.mu.Unlock()
		if j.gen == gen && !themeFinal(j.stage) {
			h.themeEnd(j, ThemeFailed, why)
			h.Log.Printf("temi %s: %s", j.id, why)
		}
	})
}

// themeEnd ferma timer e token dell'operazione; con stage la chiude (stage vuoto = sostituita: resta com'è ma non
// fa più niente). Con h.mu.
func (h *Hub) themeEnd(j *themeJob, stage, why string) {
	j.gen++
	if j.timer != nil {
		j.timer.Stop()
		j.timer = nil
	}
	if j.token != "" {
		delete(h.themeTokens, j.token)
		j.token = ""
	}
	j.conn = nil
	if stage != "" {
		j.stage, j.err, j.updated = stage, why, h.Now()
		go h.changed()
	}
}

// themeReset riporta in «attesa» un'operazione non finita: token buttato, timer fermo. Con h.mu.
func (h *Hub) themeReset(j *themeJob) {
	if j.stage == ThemeWaiting && j.conn == nil && j.token == "" {
		return
	}
	h.themeEnd(j, "", "")
	h.themeStage(j, ThemeWaiting)
}

func (h *Hub) onThemes(c *conn, m *themesMsg) {
	h.mu.Lock()
	if !c.authed { // come l'offerta: have/cur falsi non devono finire in /api/status né nella scelta «già sulla scheda»
		h.mu.Unlock()
		h.ignore(c.info.ID, "themes da una connessione non autenticata")
		return
	}
	bt := h.boardThemes(c.info.Board)
	bt.from, bt.have, bt.cur = c, append([]ThemeHave{}, m.Have...), m.Cur
	h.themeStep(c, bt)
	h.mu.Unlock()
	h.changed()
}

func (h *Hub) onThemeAck(c *conn, m *themeAckMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	bt := h.themes[c.info.Board]
	var j *themeJob
	if bt != nil {
		j = bt.job
	}
	if !c.authed || j == nil || j.conn != c || j.want == nil || j.want.ID != m.ID ||
		(j.stage != ThemeOffered && j.stage != ThemeDownloaded) {
		h.Log.Printf("temi: theme-ack di %s per %s senza un'offerta in corso: ignorato", c.info.Board, m.ID)
		return
	}
	if !*m.OK {
		h.themeEnd(j, ThemeFailed, "la scheda ha rifiutato il tema: "+m.Error)
		h.Log.Printf("temi %s: %s rifiutato da %s: %s", j.id, j.want.ID, j.board, m.Error)
		return
	}
	if !bt.has(j.want) {
		bt.have = append(bt.have, ThemeHave{j.want.ID, j.want.CRC})
	}
	if j.token != "" { // salvato: il token non serve più
		delete(h.themeTokens, j.token)
		j.token = ""
	}
	h.themeStage(j, ThemeDownloaded) // il passo dopo è theme-use
	h.themeStep(c, bt)
}

// themeConnClosed: la connessione se n'è andata (anche a metà download): l'operazione partita su di lei torna in
// attesa, il suo themes non vale più. Con h.mu, dopo aver tolto c da h.conns.
func (h *Hub) themeConnClosed(c *conn) {
	bt := h.themes[c.info.Board]
	if bt == nil || c.info.Board == "" {
		return
	}
	if bt.from == c {
		bt.from = nil
	}
	if j := bt.job; j != nil && !themeFinal(j.stage) && j.conn == c {
		h.themeReset(j)
	}
}

// themesView: la parte themes di /api/status per la connessione c. Con h.mu.
func (h *Hub) themesView(c *conn) *ThemesView {
	bt := h.themes[c.info.Board]
	if bt == nil {
		return &ThemesView{Have: []ThemeHave{}}
	}
	v := &ThemesView{Have: []ThemeHave{}, Job: bt.job.view()}
	if bt.from == c && c.authed {
		v.Supported, v.Have, v.Cur = true, append(v.Have, bt.have...), bt.cur
	}
	return v
}

// ServeTheme: GET /themes/{token} sulla porta dei display. Stesse regole di ServeOTA (blobTokenStatus): 404 token
// sconosciuto, 410 usato o scaduto, 403 IP diverso da quello della scheda che ha ricevuto l'offerta (il token resta valido).
func (h *Hub) ServeTheme(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	t := h.themeTokens[tok]
	var exp time.Time
	var want string
	if t != nil {
		exp, want = t.exp, t.ip
	}
	switch blobTokenStatus(tok, t != nil, t != nil && t.used, exp, ip, want, h.Now()) {
	case http.StatusNotFound:
		h.mu.Unlock()
		h.Log.Printf("temi: download con un token sconosciuto da %s", ip)
		http.Error(w, "token sconosciuto", http.StatusNotFound)
		return
	case http.StatusGone:
		if t.used {
			h.mu.Unlock()
			h.Log.Printf("temi %s: download ripetuto da %s: il token vale una volta sola", t.job.id, ip)
			http.Error(w, "token già usato", http.StatusGone)
			return
		}
		delete(h.themeTokens, tok)
		h.mu.Unlock()
		h.Log.Printf("temi %s: download da %s con il token scaduto", t.job.id, ip)
		http.Error(w, "token scaduto", http.StatusGone)
		return
	case http.StatusForbidden:
		h.mu.Unlock()
		h.Log.Printf("temi %s: download rifiutato a %s: il token vale solo per %s", t.job.id, ip, t.ip)
		http.Error(w, "non per questo indirizzo", http.StatusForbidden)
		return
	}
	j := t.job
	bin, crc, id := j.want.Bin, j.want.CRC, j.want.ID
	head := r.Method == http.MethodHead
	if !head { // HEAD non consuma il token né segna il tema come scaricato
		t.used = true
		h.themeStage(j, ThemeDownloaded)
	}
	h.mu.Unlock()
	serveBlob(w, r, bin, crc)
	if !head {
		h.Log.Printf("temi %s: %s scaricato da %s (%d byte)", j.id, id, ip, len(bin))
	}
}
