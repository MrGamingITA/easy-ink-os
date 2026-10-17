package display

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"sort"
	"strconv"
	"time"

	"calamaio/internal/m5"
	"calamaio/internal/ota"
)

// Aggiornamento remoto (docs/OTA.md, docs/PROTOCOLLO.md «OTA»): il servizio manda alla scheda
// autenticata la sua chiave pubblica di firma (`ota-key`), poi l'offerta (`ota-offer`) con il manifesto
// firmato e l'indirizzo da cui scaricare l'immagine: GET /ota/<token> su questa stessa porta, token
// monouso, valido TokenTTL, servito solo all'IP della scheda che ha ricevuto l'offerta. La scheda
// riferisce con `ota-status`. Un solo aggiornamento per scheda alla volta; annullabile (`ota-cancel`)
// prima della fase «scrivo».

// Fasi di un aggiornamento: quelle di ota-status più «offerta» (offerta mandata, nessuna risposta) e
// «annullato» (dal PC).
const (
	OtaOffer    = "offerta"
	OtaRejected = "rifiutato"
	OtaReceive  = "ricevo"
	OtaVerify   = "verifico"
	OtaWrite    = "scrivo"
	OtaReboot   = "riavvio"
	OtaDone     = "fatto"
	OtaError    = "errore"
	OtaCanceled = "annullato"
)

// otaRank: ordine delle fasi; un ota-status che torna indietro si ignora.
var otaRank = map[string]int{OtaOffer: 0, OtaReceive: 1, OtaVerify: 2, OtaWrite: 3, OtaReboot: 4}

func otaFinal(stage string) bool {
	return stage == OtaRejected || stage == OtaDone || stage == OtaError || stage == OtaCanceled
}

var (
	ErrOTABusy    = errors.New("c'è già un aggiornamento in corso per questa scheda")
	ErrNoOTA      = errors.New("nessun aggiornamento in corso per questa scheda")
	ErrOTATooLate = errors.New("non si può più annullare: la scheda sta già scrivendo la versione nuova")
	ErrOTAPerm    = errors.New("c'è una richiesta di permesso aperta su questa scheda: rispondi prima (sul display o nel terminale)")
)

// BlockedError: l'operazione non si può fare adesso, per il motivo detto (HTTP 409).
type BlockedError struct{ Msg string }

func (e *BlockedError) Error() string { return e.Msg }

// OTAImage è l'immagine da offrire (già letta e controllata: ota.Resolve).
type OTAImage struct {
	Bin     []byte
	Version string
	Device  string
	Built   int64  // ms epoch
	Source  string // da dove viene, per il log («pacchetto firmware del servizio», percorso…)
}

// Messaggi (servizio → display).
type (
	otaKeyMsg struct {
		T   string `json:"t"`
		V   int    `json:"v"`
		Pub string `json:"pub"`
		Mac string `json:"mac"`
	}
	// OtaOfferMsg: offerta firmata.
	OtaOfferMsg struct {
		T        string       `json:"t"`
		V        int          `json:"v"`
		ID       string       `json:"id"`
		Manifest ota.Manifest `json:"manifest"`
		Sig      string       `json:"sig"`
		URL      string       `json:"url"`
		Mac      string       `json:"mac"`
	}
	otaCancelMsg struct {
		T   string `json:"t"`
		V   int    `json:"v"`
		ID  string `json:"id"`
		Mac string `json:"mac"`
	}
)

// otaStatusMsg (display → servizio).
type otaStatusMsg struct {
	ID    string `json:"id"`
	Phase string `json:"phase"`
	Done  *int64 `json:"done"`
	Total *int64 `json:"total"`
	Error string `json:"error"`
}

// MaxOtaError: byte al massimo del motivo in ota-status.
const MaxOtaError = 127

func parseOtaStatus(data []byte) (*otaStatusMsg, bool) {
	var m otaStatusMsg
	if err := json.Unmarshal(data, &m); err != nil || m.ID == "" || len(m.ID) > 32 || len(m.Error) > MaxOtaError {
		return nil, false
	}
	switch m.Phase {
	case OtaRejected, OtaReceive, OtaVerify, OtaWrite, OtaReboot, OtaDone, OtaError:
	default:
		return nil, false
	}
	if (m.Done != nil && *m.Done < 0) || (m.Total != nil && *m.Total < 0) {
		return nil, false
	}
	m.Error = cleanField(m.Error, MaxOtaError)
	return &m, true
}

type otaJob struct {
	id, board          string
	from, to, device   string
	size               int64
	sha                string
	source             string
	stage, err         string
	done, total, sent  int64
	started, updated   time.Time
	ended              time.Time
	connID             int
	token              string
	gen                int // cambia a ogni nuovo timer: un timer vecchio non fa niente
	timer              *time.Timer
	bin                []byte
	dlCtx              context.Context
	dlCancel           context.CancelFunc
	downloads, refused int
}

type otaToken struct {
	job  *otaJob
	ip   string
	exp  time.Time
	used bool
}

// OtaView è un aggiornamento in GET /api/status (displays[].ota).
type OtaView struct {
	ID         string `json:"id"`
	Board      string `json:"board"`
	Stage      string `json:"stage"`
	Active     bool   `json:"active"`
	Cancelable bool   `json:"cancelable"`
	From       string `json:"from"`
	To         string `json:"to"`
	Size       int64  `json:"size"`
	SHA256     string `json:"sha256"`
	Done       int64  `json:"done"`
	Total      int64  `json:"total"`
	Sent       int64  `json:"sent"` // byte già serviti dal servizio
	Error      string `json:"error,omitempty"`
	Started    int64  `json:"started"`
	Updated    int64  `json:"updated"`
	Ended      int64  `json:"ended,omitempty"`
}

func (j *otaJob) view() *OtaView {
	if j == nil {
		return nil
	}
	v := &OtaView{ID: j.id, Board: j.board, Stage: j.stage, Active: !otaFinal(j.stage), From: j.from, To: j.to, Size: j.size, SHA256: j.sha,
		Done: j.done, Total: j.total, Sent: j.sent, Error: j.err, Started: j.started.UnixMilli(), Updated: j.updated.UnixMilli()}
	v.Cancelable = j.stage == OtaOffer || j.stage == OtaReceive || j.stage == OtaVerify
	if !j.ended.IsZero() {
		v.Ended = j.ended.UnixMilli()
	}
	return v
}

func (h *Hub) otaInit() {
	if h.otaJobs == nil {
		h.otaJobs = map[string]*otaJob{}
		h.otaTokens = map[string]*otaToken{}
	}
}

// OtaKeyPub: chiave pubblica di firma attuale ("" se non c'è) e l'errore che la rende inutilizzabile.
func (h *Hub) OtaKeyPub() (string, error) {
	k, err := h.OTAKey.Get()
	if err != nil {
		return "", err
	}
	return k.PubHex(), nil
}

// queueOtaKey manda ota-key a c se non l'ha già con la chiave attuale. Con h.mu; pub già letta.
func (h *Hub) queueOtaKey(c *conn, pub string) {
	if pub == "" || !c.authed || c.otaKeyPub == pub {
		return
	}
	c.otaKeyPub = pub
	h.enqueueOut(c, lazyMsg(func() any {
		h.mu.Lock()
		defer h.mu.Unlock()
		if !c.authed || c.sk == nil {
			c.otaKeyPub = ""
			return nil
		}
		return otaKeyMsg{T: "ota-key", V: ProtocolVersion, Pub: pub, Mac: m5.Hex(m5.OtaKeyMac(c.sk, pub))}
	}))
}

// afterAuth: ota-key appena la connessione è autenticata (docs/OTA.md §2) e chiusura di un
// aggiornamento che aspettava il ritorno della scheda dopo il riavvio. Dal goroutine della connessione.
func (h *Hub) afterAuth(c *conn) {
	pub, _ := h.OtaKeyPub()
	h.mu.Lock()
	defer h.mu.Unlock()
	h.otaInit()
	h.queueOtaKey(c, pub)
	j := h.otaJobs[c.info.Board]
	if j == nil || otaFinal(j.stage) || (j.stage != OtaReboot && j.stage != OtaWrite) {
		return
	}
	if c.info.FW == j.to {
		h.otaEnd(j, OtaDone, "")
		h.Log.Printf("ota %s: %s è ripartita con la versione %s e si è autenticata: aggiornamento riuscito", j.id, j.board, j.to)
		return
	}
	h.otaEnd(j, OtaError, fmt.Sprintf("la scheda è ripartita con la versione %s: la nuova (%s) non si è avviata, è tornata alla precedente", c.info.FW, j.to))
}

// StartOTA offre img alla scheda board. Controlli del servizio: scheda collegata e autenticata, chiave di
// firma, nessun altro aggiornamento in corso, nessuna richiesta di permesso aperta, profilo giusto. Le
// condizioni di alimentazione (cavo, batteria ≥ 50%) le controlla la scheda subito prima di partire.
func (h *Hub) StartOTA(board string, img OTAImage) (*OtaView, error) {
	key, err := h.OTAKey.Get()
	if err != nil {
		return nil, err
	}
	pub := key.PubHex()
	sum := sha256.Sum256(img.Bin)
	man := ota.Manifest{Board: board, Device: img.Device, Version: img.Version, Size: int64(len(img.Bin)),
		SHA256: hex.EncodeToString(sum[:]), Built: img.Built}
	sig, err := man.Sign(key.Priv)
	if err != nil {
		return nil, &InvalidError{Msg: err.Error()}
	}

	h.mu.Lock()
	defer h.mu.Unlock()
	h.otaInit()
	if h.stopping || h.closed || h.farewell {
		return nil, ErrStopping
	}
	c := h.target(board)
	if c == nil {
		return nil, ErrNotConnected
	}
	if !c.authed || c.sk == nil {
		return nil, ErrNotAuthed
	}
	if j := h.otaJobs[board]; j != nil && !otaFinal(j.stage) {
		return nil, ErrOTABusy
	}
	if c.info.Device != "" && c.info.Device != img.Device {
		return nil, &BlockedError{Msg: fmt.Sprintf("l'immagine è per il profilo %s, la scheda %s è %s", img.Device, board, c.info.Device)}
	}
	for id := range h.perms {
		if c.sent[id] {
			return nil, ErrOTAPerm
		}
	}
	local := c.local
	if local == "" {
		return nil, &BlockedError{Msg: "indirizzo del PC verso la scheda sconosciuto"}
	}
	token := m5.Hex(m5.Random(16))
	now := h.Now()
	j := &otaJob{id: "o" + m5.Hex(m5.Random(6)), board: board, from: c.info.FW, to: img.Version, device: img.Device,
		size: man.Size, sha: man.SHA256, source: img.Source, stage: OtaOffer, total: man.Size, started: now, updated: now,
		connID: c.info.ID, token: token, bin: img.Bin}
	j.dlCtx, j.dlCancel = context.WithCancel(context.Background())
	url := "http://" + local + "/ota/" + token
	h.queueOtaKey(c, pub) // prima l'offerta no: la scheda deve già avere la chiave
	if c.otaKeyPub != pub {
		j.dlCancel()
		return nil, ErrBusy
	}
	id := j.id
	if !h.enqueueOut(c, lazyMsg(func() any {
		h.mu.Lock()
		defer h.mu.Unlock()
		if !c.authed || c.sk == nil || h.otaJobs[board] != j || j.stage != OtaOffer {
			return nil
		}
		return OtaOfferMsg{T: "ota-offer", V: ProtocolVersion, ID: id, Manifest: man, Sig: sig, URL: url,
			Mac: m5.Hex(m5.OtaOfferMac(c.sk, id, man.SHA256))}
	})) {
		j.dlCancel()
		return nil, ErrBusy
	}
	if old := h.otaJobs[board]; old != nil {
		old.bin = nil
	}
	h.otaJobs[board] = j
	h.otaTokens[token] = &otaToken{job: j, ip: c.info.IP, exp: now.Add(h.TokenTTL)}
	h.otaArm(j, h.OfferTimeout, "la scheda non ha risposto all'offerta: firmware senza aggiornamento remoto o scheda occupata")
	h.Log.Printf("ota %s: offerta a %s (%s → %s, %d byte, sha256 %s…, da %s), download da %s", id, board, j.from, j.to,
		man.Size, man.SHA256[:12], img.Source, c.info.IP)
	go h.changed()
	return j.view(), nil
}

// otaArm programma la scadenza della fase attuale: senza notizie entro d l'aggiornamento finisce in
// errore con why. Con h.mu.
func (h *Hub) otaArm(j *otaJob, d time.Duration, why string) {
	j.gen++
	gen := j.gen
	if j.timer != nil {
		j.timer.Stop()
	}
	j.timer = time.AfterFunc(d, func() {
		h.mu.Lock()
		if j.gen != gen || otaFinal(j.stage) {
			h.mu.Unlock()
			return
		}
		h.otaEnd(j, OtaError, why)
		h.mu.Unlock()
		h.Log.Printf("ota %s: %s", j.id, why)
	})
}

// otaEnd chiude l'aggiornamento: token revocato, download interrotto, immagine liberata. Con h.mu.
func (h *Hub) otaEnd(j *otaJob, stage, why string) {
	j.gen++
	if j.timer != nil {
		j.timer.Stop()
	}
	j.stage, j.err = stage, why
	j.updated = h.Now()
	j.ended = j.updated
	delete(h.otaTokens, j.token)
	if j.dlCancel != nil {
		j.dlCancel()
	}
	j.bin = nil
	go h.changed()
}

// CancelOTA annulla l'aggiornamento di board prima della fase «scrivo»: ota-cancel alla scheda,
// download interrotto, token revocato.
func (h *Hub) CancelOTA(board string) (*OtaView, error) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.otaInit()
	j := h.otaJobs[board]
	if j == nil || otaFinal(j.stage) {
		return nil, ErrNoOTA
	}
	if j.stage == OtaWrite || j.stage == OtaReboot {
		return nil, ErrOTATooLate
	}
	if c := h.target(board); c != nil && c.authed {
		id := j.id
		h.enqueueOut(c, lazyMsg(func() any {
			h.mu.Lock()
			defer h.mu.Unlock()
			if !c.authed || c.sk == nil {
				return nil
			}
			return otaCancelMsg{T: "ota-cancel", V: ProtocolVersion, ID: id, Mac: m5.Hex(m5.OtaCancelMac(c.sk, id))}
		}))
	}
	h.otaEnd(j, OtaCanceled, "annullato dal PC")
	h.Log.Printf("ota %s: annullato dal PC in fase %s", j.id, j.stage)
	return j.view(), nil
}

// onOtaStatus applica un ota-status (solo da una connessione autenticata). Dal goroutine della connessione.
func (h *Hub) onOtaStatus(c *conn, m *otaStatusMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.otaInit()
	if !c.authed {
		h.ignoreLocked(c.info.ID, "ota-status da una connessione non autenticata")
		return
	}
	board := c.info.Board
	j := h.otaJobs[board]
	if j == nil || j.id != m.ID {
		// Rapporto di un aggiornamento che il servizio non segue più (riavvio del servizio, ritorno alla
		// versione precedente riferito dopo il riavvio della scheda): resta come esito, se non c'è un
		// aggiornamento in corso.
		h.Log.Printf("ota: %s riferisce %s per l'aggiornamento %s che il servizio non segue: %s", board, m.Phase, m.ID, m.Error)
		if (m.Phase == OtaError || m.Phase == OtaDone) && (j == nil || otaFinal(j.stage)) {
			now := h.Now()
			h.otaJobs[board] = &otaJob{id: m.ID, board: board, stage: m.Phase, err: m.Error, started: now, updated: now, ended: now}
			go h.changed()
		}
		return
	}
	if m.Done != nil {
		j.done = *m.Done
	}
	if m.Total != nil && *m.Total > 0 {
		j.total = *m.Total
	}
	j.connID = c.info.ID
	if otaFinal(j.stage) {
		// già chiuso dal servizio: la scheda può solo correggere l'esito con un errore (rollback)
		if m.Phase == OtaError && j.stage != OtaCanceled {
			j.stage, j.err, j.updated = OtaError, m.Error, h.Now()
			go h.changed()
		}
		return
	}
	switch m.Phase {
	case OtaRejected, OtaError:
		why := m.Error
		if why == "" {
			why = "senza motivo"
		}
		h.otaEnd(j, m.Phase, why)
		h.Log.Printf("ota %s: %s %s: %s", j.id, board, m.Phase, why)
		return
	case OtaDone:
		h.otaEnd(j, OtaDone, "")
		h.Log.Printf("ota %s: %s dice fatto", j.id, board)
		return
	}
	if otaRank[m.Phase] < otaRank[j.stage] {
		return // stato vecchio arrivato dopo
	}
	if m.Phase != j.stage {
		h.Log.Printf("ota %s: %s in fase %s", j.id, board, m.Phase)
	}
	j.stage, j.updated = m.Phase, h.Now()
	switch m.Phase {
	case OtaReboot:
		delete(h.otaTokens, j.token)
		h.otaArm(j, h.RebootWait, "la scheda non è tornata dopo il riavvio: controlla il display")
	case OtaWrite:
		h.otaArm(j, h.StallTimeout, "la scheda non manda più notizie durante la scrittura")
	default:
		h.otaArm(j, h.StallTimeout, "la scheda non manda più notizie: aggiornamento interrotto, resta la versione di prima")
	}
	go h.changed()
}

// otaConnClosed: la connessione di una scheda si è chiusa. In «scrivo» vuol dire riavvio; nelle altre
// fasi decidono le scadenze (la scheda può ricollegarsi e continuare a riferire). Con h.mu.
func (h *Hub) otaConnClosed(c *conn) {
	j := h.otaJobs[c.info.Board]
	if j == nil || otaFinal(j.stage) || j.connID != c.info.ID {
		return
	}
	if j.stage == OtaWrite {
		j.stage, j.updated = OtaReboot, h.Now()
		h.otaArm(j, h.RebootWait, "la scheda non è tornata dopo il riavvio: controlla il display")
		go h.changed()
	}
}

// OtaJobs: per ogni scheda l'aggiornamento in corso o l'ultimo (anche di schede scollegate, per esempio
// durante il riavvio), in ordine di scheda.
func (h *Hub) OtaJobs() []OtaView {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.otaInit()
	out := make([]OtaView, 0, len(h.otaJobs))
	for _, j := range h.otaJobs {
		out = append(out, *j.view())
	}
	sort.Slice(out, func(i, k int) bool { return out[i].Board < out[k].Board })
	return out
}

// OtaOf restituisce l'aggiornamento in corso o l'ultimo di board.
func (h *Hub) OtaOf(board string) *OtaView {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.otaInit()
	return h.otaJobs[board].view()
}

// ServeOTA: GET /ota/{token} sulla porta dei display. 404 token sconosciuto, 410 scaduto o già usato,
// 403 IP diverso da quello della scheda che ha ricevuto l'offerta (il token resta valido).
func (h *Hub) ServeOTA(w http.ResponseWriter, r *http.Request) {
	tok := r.PathValue("token")
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	h.otaInit()
	t := h.otaTokens[tok]
	now := h.Now()
	switch {
	case len(tok) != 32 || !m5.IsLowerHex(tok) || t == nil:
		h.mu.Unlock()
		h.Log.Printf("ota: download con un token sconosciuto da %s", ip)
		http.Error(w, "token sconosciuto", http.StatusNotFound)
		return
	case t.used:
		h.mu.Unlock()
		h.Log.Printf("ota %s: download ripetuto da %s: il token vale una volta sola", t.job.id, ip)
		http.Error(w, "token già usato", http.StatusGone)
		return
	case now.After(t.exp):
		delete(h.otaTokens, tok)
		h.mu.Unlock()
		h.Log.Printf("ota %s: download da %s con il token scaduto", t.job.id, ip)
		http.Error(w, "token scaduto", http.StatusGone)
		return
	case ip != t.ip:
		t.job.refused++
		h.mu.Unlock()
		h.Log.Printf("ota %s: download rifiutato a %s: il token vale solo per %s", t.job.id, ip, t.ip)
		http.Error(w, "non per questo indirizzo", http.StatusForbidden)
		return
	}
	t.used = true
	j := t.job
	j.downloads++
	bin, ctx := j.bin, j.dlCtx
	h.mu.Unlock()
	if bin == nil {
		http.Error(w, "aggiornamento chiuso", http.StatusGone)
		return
	}
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Content-Length", strconv.Itoa(len(bin)))
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Sha256", j.sha)
	w.WriteHeader(http.StatusOK)
	if r.Method == http.MethodHead {
		return
	}
	h.Log.Printf("ota %s: download di %d byte verso %s", j.id, len(bin), ip)
	const chunk = 4096
	var sent int
	for sent < len(bin) {
		select {
		case <-ctx.Done():
			h.Log.Printf("ota %s: download interrotto dal servizio dopo %d byte", j.id, sent)
			panic(http.ErrAbortHandler) // la scheda vede la connessione chiusa prima della fine
		case <-r.Context().Done():
			h.Log.Printf("ota %s: download interrotto dalla scheda dopo %d byte", j.id, sent)
			return
		default:
		}
		n := min(chunk, len(bin)-sent)
		if _, err := w.Write(bin[sent : sent+n]); err != nil {
			h.Log.Printf("ota %s: download interrotto dopo %d byte: %v", j.id, sent, err)
			return
		}
		sent += n
		h.mu.Lock()
		j.sent = int64(sent)
		h.mu.Unlock()
	}
	h.Log.Printf("ota %s: download completo (%d byte) verso %s", j.id, sent, ip)
}

// localAddr: indirizzo del PC sulla connessione (lo stesso a cui la scheda si è collegata), per l'url.
func localAddr(r *http.Request) string {
	if a, ok := r.Context().Value(http.LocalAddrContextKey).(net.Addr); ok {
		if host, port, err := net.SplitHostPort(a.String()); err == nil {
			if ip := net.ParseIP(host); ip != nil {
				if v4 := ip.To4(); v4 != nil {
					host = v4.String() // ::ffff:192.168.1.10 → 192.168.1.10
				}
			}
			return net.JoinHostPort(host, port)
		}
	}
	return ""
}

func (h *Hub) ignoreLocked(id int, format string, args ...any) {
	h.ignored.Add(1)
	h.Log.Printf("ws: #%d ignorato: "+format, append([]any{id}, args...)...)
}
