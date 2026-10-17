package display

import (
	"context"
	"crypto/subtle"
	"errors"
	"sort"
	"strings"
	"time"

	"github.com/coder/websocket"

	"calamaio/internal/config"
	"calamaio/internal/m5"
)

// M5 (docs/M5-PERMESSI.md): abbinamento (§1), autenticazione a ogni connessione (§2), richieste di
// permesso firmate (§3). Formule in internal/m5, provate con protocol/vectors/crypto-vectors.json.

// KeyStore custodisce le chiavi K per scheda (config.KeyStore: config.json, permessi 0600).
type KeyStore interface {
	List() ([]config.Paired, error)
	Put(board string, key []byte, created time.Time) error
	Delete(board string) (bool, error)
}

// lazyMsg: messaggio costruito dal goroutine della connessione al momento di scriverlo (con la chiave
// di sessione di quel momento). nil = non mandare niente.
type lazyMsg func() any

// m5In: i campi dei messaggi M5 del display (pair-key, pair-done, pair-abort, auth, perm-reply, unpair).
type m5In struct {
	ID       string `json:"id"`
	PkD      string `json:"pkD"`
	Mac      string `json:"mac"`
	Reason   string `json:"reason"`
	Nd       string `json:"nd"`
	Decision string `json:"decision"`
	Nonce    string `json:"nonce"`
}

// Messaggi del servizio.
type (
	pairBeginMsg struct {
		T      string `json:"t"`
		V      int    `json:"v"`
		ID     string `json:"id"`
		Commit string `json:"commit"`
	}
	pairRevealMsg struct {
		T   string `json:"t"`
		V   int    `json:"v"`
		ID  string `json:"id"`
		PkS string `json:"pkS"`
	}
	pairMacMsg struct { // pair-confirm
		T   string `json:"t"`
		V   int    `json:"v"`
		ID  string `json:"id"`
		Mac string `json:"mac"`
	}
	pairAbortMsg struct {
		T      string `json:"t"`
		V      int    `json:"v"`
		ID     string `json:"id"`
		Reason string `json:"reason"`
	}
	authChallengeMsg struct {
		T  string `json:"t"`
		V  int    `json:"v"`
		NS string `json:"ns"`
	}
	authOkMsg struct {
		T   string `json:"t"`
		V   int    `json:"v"`
		Mac string `json:"mac"`
	}
	authFailMsg struct {
		T      string `json:"t"`
		V      int    `json:"v"`
		Reason string `json:"reason,omitempty"`
	}
	// PermRequestMsg (servizio → display).
	PermRequestMsg struct {
		T       string `json:"t"`
		V       int    `json:"v"`
		ID      string `json:"id"`
		Session string `json:"session"`
		Name    string `json:"name"`
		Tool    string `json:"tool"`
		Arg     string `json:"arg"`
		At      int64  `json:"at"`
		Mac     string `json:"mac"`
	}
	// PermCloseMsg (servizio → display).
	PermCloseMsg struct {
		T      string `json:"t"`
		V      int    `json:"v"`
		ID     string `json:"id"`
		Reason string `json:"reason"`
		Mac    string `json:"mac"`
	}
)

// Motivi di pair-abort mandati dal servizio.
const (
	AbortCode   = "codice"    // codice sbagliato scritto sul PC
	AbortTime   = "tempo"     // finestra di 3 minuti scaduta
	AbortCancel = "annullato" // annullato dal PC (Ctrl-C, pulsante Annulla)
	AbortMac    = "mac"       // pair-done con un mac sbagliato
	AbortKey    = "chiave"    // pkD non valida (punto di ordine basso)
)

// Fasi dell'abbinamento.
const (
	StageBegin   = "begin"   // pair-begin mandato, si aspetta pair-key
	StageCode    = "code"    // il display mostra il codice: si aspetta che l'utente lo scriva sul PC
	StageConfirm = "confirm" // codice giusto: si aspetta la conferma fisica sul display (2 pressioni)
	StageDone    = "done"    // abbinato, K salvata
	StageAborted = "aborted" // annullato (Reason dice perché)
)

var (
	ErrPairBusy     = errors.New("c'è già un abbinamento in corso con questo display: completalo o annullalo")
	ErrNoPairing    = errors.New("nessun abbinamento in corso con questo display")
	ErrPairNotReady = errors.New("il display non ha ancora mostrato il codice: aspetta qualche secondo")
	ErrWrongCode    = errors.New("codice sbagliato: abbinamento annullato, nessuna chiave salvata")
	ErrStopping     = errors.New("servizio in arresto")
)

type pairing struct {
	id                       string
	stage, reason            string
	skS, pkS, commit, pkD, T []byte
	K                        []byte
	sas                      string
	until                    time.Time
	timer                    *time.Timer
}

// PairingStatus è l'abbinamento in GET /api/status. Il codice non c'è mai: si legge solo sul display.
type PairingStatus struct {
	ID     string `json:"id"`
	Stage  string `json:"stage"`
	Reason string `json:"reason,omitempty"`
	Until  int64  `json:"until"` // ms epoch: fine della finestra di 3 minuti
}

func (p *pairing) status() *PairingStatus {
	if p == nil {
		return nil
	}
	return &PairingStatus{ID: p.id, Stage: p.stage, Reason: p.reason, Until: p.until.UnixMilli()}
}

func (p *pairing) active() bool {
	return p != nil && (p.stage == StageBegin || p.stage == StageCode || p.stage == StageConfirm)
}

// wipe cancella i segreti dell'abbinamento (fine, in qualunque modo).
func (p *pairing) wipe() {
	for _, b := range [][]byte{p.skS, p.K, p.T} {
		for i := range b {
			b[i] = 0
		}
	}
	p.skS, p.K, p.sas = nil, nil, ""
	if p.timer != nil {
		p.timer.Stop()
	}
}

// LoadKeys legge le chiavi dal KeyStore (all'avvio del servizio).
func (h *Hub) LoadKeys() error {
	if h.Keys == nil {
		return nil
	}
	list, err := h.Keys.List()
	if err != nil {
		return err
	}
	h.mu.Lock()
	for _, p := range list {
		if k := p.KeyBytes(); k != nil {
			h.keys[p.Board] = k
		}
	}
	h.mu.Unlock()
	return nil
}

// PairedBoards restituisce le schede abbinate (anche non collegate), in ordine.
func (h *Hub) PairedBoards() []string {
	h.mu.Lock()
	out := make([]string, 0, len(h.keys))
	for b := range h.keys {
		out = append(out, b)
	}
	h.mu.Unlock()
	sort.Strings(out)
	return out
}

// enqueueOut mette un messaggio in coda per la connessione senza bloccare. Con h.mu.
func (h *Hub) enqueueOut(c *conn, msg any) bool {
	select {
	case c.out <- msg:
		return true
	default:
		h.Log.Printf("ws: #%d coda piena: messaggio M5 non mandato", c.info.ID)
		return false
	}
}

// pairState: none (non abbinato) · ok (abbinato e autenticato) · redo (abbinamento da rifare) ·
// auth (autenticazione in corso). Con h.mu.
func (h *Hub) pairState(c *conn) (string, string) {
	key := h.keys[c.info.Board] != nil
	dp := c.info.DisplayPaired
	switch {
	case !key && dp != nil && *dp:
		return "redo", "il display ha una chiave, il PC no"
	case !key:
		return "none", ""
	case c.authed:
		return "ok", ""
	case c.authFail != "":
		return "redo", c.authFail
	case dp != nil && !*dp:
		return "redo", "il PC ha una chiave, il display no"
	case c.ns != nil && h.Now().Sub(c.challengedAt) > h.AuthWait:
		return "redo", "il display non risponde alla sfida"
	}
	return "auth", ""
}

// eligible: il display riceve le richieste di permesso (autenticato e permOn vero). Con h.mu.
func (h *Hub) eligible(c *conn) bool {
	return c.info.Hello && c.authed && c.settings != nil && c.settings["permOn"] == true
}

// endConnM5: la connessione è chiusa. Con h.mu.
func (h *Hub) endConnM5(c *conn) {
	if c.pair.active() {
		c.pair.stage, c.pair.reason = StageAborted, "connessione"
		h.Log.Printf("ws: #%d abbinamento %s annullato: connessione chiusa", c.info.ID, c.pair.id)
	}
	if c.pair != nil {
		c.pair.wipe()
	}
	c.authed, c.sk, c.ns = false, nil, nil
}

// challenge manda auth-challenge se il servizio ha una K per la scheda e la connessione non è già
// autenticata. Dal goroutine della connessione.
func (h *Hub) challenge(ctx context.Context, c *conn) error {
	h.mu.Lock()
	if h.keys[c.info.Board] == nil || c.authed {
		h.mu.Unlock()
		return nil
	}
	ns := m5.Random(m5.NonceLen)
	c.ns, c.challengedAt, c.authFail = ns, h.Now(), ""
	h.mu.Unlock()
	return h.write(ctx, c, authChallengeMsg{T: "auth-challenge", V: ProtocolVersion, NS: m5.Hex(ns)})
}

// onM5 applica un messaggio M5 del display. Dal goroutine della connessione, dopo hello.
func (h *Hub) onM5(ctx context.Context, c *conn, t string, m *m5In) error {
	switch t {
	case "auth":
		return h.onAuth(ctx, c, m)
	case "pair-key":
		return h.onPairKey(ctx, c, m)
	case "pair-done":
		return h.onPairDone(ctx, c, m)
	case "pair-abort":
		h.mu.Lock()
		p := c.pair
		if !p.active() || m.ID != p.id {
			h.mu.Unlock()
			h.ignore(c.info.ID, "pair-abort per un abbinamento che non c'è (%q)", m.ID)
			return nil
		}
		reason := strings.TrimSpace(m.Reason)
		if reason == "" {
			reason = "display"
		}
		p.stage, p.reason = StageAborted, m5.TruncUTF8(reason, 31)
		p.wipe()
		h.mu.Unlock()
		h.Log.Printf("ws: #%d abbinamento %s annullato dal display: %s", c.info.ID, p.id, reason)
		h.changed()
	case "perm-reply":
		h.onPermReply(c, m)
	case "unpair":
		h.onUnpair(c, m)
	}
	return nil
}

func (h *Hub) onAuth(ctx context.Context, c *conn, m *m5In) error {
	h.mu.Lock()
	board := c.info.Board
	K := h.keys[board]
	ns := c.ns
	if ns == nil || K == nil {
		h.mu.Unlock()
		h.ignore(c.info.ID, "auth senza sfida")
		return nil
	}
	nd, ok1 := m5.ParseHex(m.Nd, m5.NonceLen)
	mac, ok2 := m5.ParseHex(m.Mac, m5.MacLen)
	c.ns = nil // una risposta sola per sfida
	if !ok1 || !ok2 || !m5.Equal(mac, m5.AuthMac(K, board, ns, nd)) {
		c.authed, c.sk = false, nil
		c.authFail = "chiave diversa sul PC e sul display"
		h.mu.Unlock()
		h.Log.Printf("ws: #%d %s: auth con un mac sbagliato: abbinamento da rifare", c.info.ID, board)
		h.ignored.Add(1)
		h.changed()
		return h.write(ctx, c, authFailMsg{T: "auth-fail", V: ProtocolVersion, Reason: "chiave diversa: abbinamento da rifare"})
	}
	c.sk = m5.SessionKey(K, ns, nd)
	c.authed, c.authFail = true, ""
	ok := authOkMsg{T: "auth-ok", V: ProtocolVersion, Mac: m5.Hex(m5.AuthOkMac(K, board, ns, nd))}
	h.mu.Unlock()
	if err := h.write(ctx, c, ok); err != nil {
		return err
	}
	h.Log.Printf("ws: #%d %s autenticato", c.info.ID, board)
	h.mu.Lock()
	h.queueOpenPerms(c) // richieste aperte mentre il display non c'era (il hook aspetta ancora)
	h.mu.Unlock()
	h.afterAuth(c) // ota-key e ritorno della scheda dopo il riavvio di un aggiornamento (ota.go)
	if h.Game != nil {
		m := h.Game.Message()
		h.mu.Lock()
		h.queueGame(c, m)
		h.mu.Unlock()
	}
	h.changed()
	return nil
}

// PairStart avvia l'abbinamento con board: pair-begin con l'impegno sulla chiave del servizio.
func (h *Hub) PairStart(board string) (*PairingStatus, error) {
	sk, pk, err := m5.NewKeyPair(nil)
	if err != nil {
		return nil, err
	}
	h.mu.Lock()
	if h.stopping || h.closed {
		h.mu.Unlock()
		return nil, ErrStopping
	}
	c := h.target(board)
	if c == nil {
		h.mu.Unlock()
		return nil, ErrNotConnected
	}
	if c.pair.active() {
		h.mu.Unlock()
		return nil, ErrPairBusy
	}
	if c.pair != nil {
		c.pair.wipe()
	}
	p := &pairing{id: "p" + m5.Hex(m5.Random(6)), stage: StageBegin, skS: sk, pkS: pk, commit: m5.Commit(pk),
		until: h.Now().Add(h.PairWindow)}
	if !h.enqueueOut(c, pairBeginMsg{T: "pair-begin", V: ProtocolVersion, ID: p.id, Commit: m5.Hex(p.commit)}) {
		h.mu.Unlock()
		return nil, ErrBusy
	}
	c.pair = p
	p.timer = time.AfterFunc(h.PairWindow, func() { h.pairTimeout(c, p) })
	st := p.status()
	h.mu.Unlock()
	h.Log.Printf("ws: #%d abbinamento %s avviato con %s", c.info.ID, p.id, board)
	h.changed()
	return st, nil
}

func (h *Hub) pairTimeout(c *conn, p *pairing) {
	h.mu.Lock()
	if c.pair != p || !p.active() {
		h.mu.Unlock()
		return
	}
	p.stage, p.reason = StageAborted, AbortTime
	p.wipe()
	h.enqueueOut(c, pairAbortMsg{T: "pair-abort", V: ProtocolVersion, ID: p.id, Reason: AbortTime})
	h.mu.Unlock()
	h.Log.Printf("ws: #%d abbinamento %s scaduto (%s)", c.info.ID, p.id, h.PairWindow)
	h.changed()
}

func (h *Hub) onPairKey(ctx context.Context, c *conn, m *m5In) error {
	h.mu.Lock()
	p := c.pair
	if p == nil || p.stage != StageBegin || m.ID != p.id {
		h.mu.Unlock()
		h.ignore(c.info.ID, "pair-key fuori sequenza (%q)", m.ID)
		return nil
	}
	pkD, ok := m5.ParseHex(m.PkD, m5.KeyLen)
	if !ok {
		h.mu.Unlock()
		h.ignore(c.info.ID, "pair-key non valido")
		return nil
	}
	Z, err := m5.X25519(p.skS, pkD)
	if err != nil {
		p.stage, p.reason = StageAborted, AbortKey
		p.wipe()
		h.mu.Unlock()
		h.Log.Printf("ws: #%d abbinamento %s: chiave del display non valida (%v)", c.info.ID, p.id, err)
		h.changed()
		return h.write(ctx, c, pairAbortMsg{T: "pair-abort", V: ProtocolVersion, ID: p.id, Reason: AbortKey})
	}
	p.pkD = pkD
	p.T = m5.Transcript(c.info.Board, p.commit, pkD, p.pkS)
	p.K = m5.PairKey(Z, p.T)
	p.sas = m5.SAS(Z, p.T)
	for i := range Z {
		Z[i] = 0
	}
	for i := range p.skS {
		p.skS[i] = 0
	}
	p.skS = nil
	p.stage = StageCode
	reveal := pairRevealMsg{T: "pair-reveal", V: ProtocolVersion, ID: p.id, PkS: m5.Hex(p.pkS)}
	h.mu.Unlock()
	h.changed()
	return h.write(ctx, c, reveal)
}

// normalizeCode toglie spazi e trattini: «175 397» vale come «175397». false se non sono 6 cifre.
func normalizeCode(s string) (string, bool) {
	var b strings.Builder
	for _, r := range s {
		switch {
		case r == ' ' || r == '-' || r == '\t':
		case r >= '0' && r <= '9':
			b.WriteRune(r)
		default:
			return "", false
		}
	}
	out := b.String()
	return out, len(out) == 6
}

// PairCode controlla il codice scritto dall'utente (confronto in tempo costante). Giusto: pair-confirm,
// poi si aspetta la conferma fisica sul display. Sbagliato: pair-abort «codice» e ErrWrongCode. Un codice
// che non è di 6 cifre è un *InvalidError e non annulla niente.
func (h *Hub) PairCode(board, code string) (*PairingStatus, error) {
	code, ok := normalizeCode(code)
	if !ok {
		return nil, &InvalidError{Msg: "il codice è di 6 cifre"}
	}
	h.mu.Lock()
	c := h.target(board)
	if c == nil {
		h.mu.Unlock()
		return nil, ErrNotConnected
	}
	p := c.pair
	switch {
	case !p.active():
		h.mu.Unlock()
		return nil, ErrNoPairing
	case p.stage == StageBegin:
		h.mu.Unlock()
		return nil, ErrPairNotReady
	case p.stage != StageCode:
		h.mu.Unlock()
		return nil, ErrPairBusy
	}
	if subtle.ConstantTimeCompare([]byte(code), []byte(p.sas)) != 1 {
		p.stage, p.reason = StageAborted, AbortCode
		p.wipe()
		h.enqueueOut(c, pairAbortMsg{T: "pair-abort", V: ProtocolVersion, ID: p.id, Reason: AbortCode})
		h.mu.Unlock()
		h.Log.Printf("ws: #%d abbinamento %s: codice sbagliato, annullato", c.info.ID, p.id)
		h.changed()
		return nil, ErrWrongCode
	}
	if !h.enqueueOut(c, pairMacMsg{T: "pair-confirm", V: ProtocolVersion, ID: p.id, Mac: m5.Hex(m5.PairConfirmMac(p.K, p.T))}) {
		h.mu.Unlock()
		return nil, ErrBusy
	}
	p.stage = StageConfirm
	st := p.status()
	h.mu.Unlock()
	h.Log.Printf("ws: #%d abbinamento %s: codice giusto, si aspetta la conferma sul display", c.info.ID, p.id)
	h.changed()
	return st, nil
}

func (h *Hub) onPairDone(ctx context.Context, c *conn, m *m5In) error {
	h.mu.Lock()
	p := c.pair
	if p == nil || p.stage != StageConfirm || m.ID != p.id {
		h.mu.Unlock()
		h.ignore(c.info.ID, "pair-done fuori sequenza (%q)", m.ID)
		return nil
	}
	mac, ok := m5.ParseHex(m.Mac, m5.MacLen)
	if !ok || !m5.Equal(mac, m5.PairDoneMac(p.K, p.T)) {
		p.stage, p.reason = StageAborted, AbortMac
		p.wipe()
		h.mu.Unlock()
		h.Log.Printf("ws: #%d abbinamento %s: pair-done con un mac sbagliato, annullato", c.info.ID, p.id)
		h.changed()
		return h.write(ctx, c, pairAbortMsg{T: "pair-abort", V: ProtocolVersion, ID: p.id, Reason: AbortMac})
	}
	board := c.info.Board
	K := append([]byte(nil), p.K...)
	h.mu.Unlock()

	// Il display ha già salvato K: un errore qui lascia l'abbinamento «da rifare».
	var err error
	if h.Keys != nil {
		err = h.Keys.Put(board, K, h.Now())
	}

	h.mu.Lock()
	if err != nil {
		p.stage, p.reason = StageAborted, "chiave non salvata sul PC"
		p.wipe()
		h.mu.Unlock()
		h.Log.Printf("ws: #%d abbinamento %s: chiave non salvata: %v", c.info.ID, p.id, err)
		h.changed()
		return nil
	}
	h.keys[board] = K // un nuovo abbinamento sostituisce il vecchio
	p.stage = StageDone
	p.wipe()
	yes := true
	c.info.DisplayPaired = &yes
	c.authed, c.sk, c.authFail = false, nil, ""
	h.mu.Unlock()
	h.Log.Printf("ws: #%d %s abbinato (%s)", c.info.ID, board, p.id)
	h.changed()
	// Subito la sfida: da qui in poi la connessione è autenticata come le altre.
	return h.challenge(ctx, c)
}

// PairCancel annulla l'abbinamento in corso (Ctrl-C di `calamaio pair`, «Annulla» nella webapp).
func (h *Hub) PairCancel(board string) (*PairingStatus, error) {
	h.mu.Lock()
	c := h.target(board)
	if c == nil {
		h.mu.Unlock()
		return nil, ErrNotConnected
	}
	p := c.pair
	if !p.active() {
		h.mu.Unlock()
		return nil, ErrNoPairing
	}
	p.stage, p.reason = StageAborted, AbortCancel
	p.wipe()
	h.enqueueOut(c, pairAbortMsg{T: "pair-abort", V: ProtocolVersion, ID: p.id, Reason: AbortCancel})
	st := p.status()
	h.mu.Unlock()
	h.Log.Printf("ws: #%d abbinamento %s annullato dal PC", c.info.ID, p.id)
	h.changed()
	return st, nil
}

// Unpair dimentica la chiave di board (`calamaio unpair`). Le connessioni della scheda si chiudono:
// il display si ricollega, vede welcome.paired falso e mostra «Abbinamento: da rifare».
func (h *Hub) Unpair(board string) (bool, error) {
	var (
		found bool
		err   error
	)
	if h.Keys != nil {
		found, err = h.Keys.Delete(board)
		if err != nil {
			return false, err
		}
	}
	h.mu.Lock()
	if h.keys[board] != nil {
		found = true
	}
	delete(h.keys, board)
	var drop []*conn
	for _, c := range h.conns {
		if c.info.Board == board {
			c.authed, c.sk, c.ns = false, nil, nil
			drop = append(drop, c)
		}
	}
	h.mu.Unlock()
	for _, c := range drop {
		go func() {
			_ = c.ws.Close(websocket.StatusNormalClosure, "abbinamento rimosso dal PC")
			c.cancel()
		}()
	}
	if found {
		h.Log.Printf("abbinamento di %s dimenticato dal PC", board)
	}
	h.changed()
	return found, nil
}

func (h *Hub) onUnpair(c *conn, m *m5In) {
	h.mu.Lock()
	board := c.info.Board
	if !c.authed {
		h.mu.Unlock()
		h.ignore(c.info.ID, "unpair da una connessione non autenticata")
		return
	}
	mac, ok := m5.ParseHex(m.Mac, m5.MacLen)
	if !ok || !m5.Equal(mac, m5.UnpairMac(c.sk, board)) {
		h.mu.Unlock()
		h.ignore(c.info.ID, "unpair con un mac sbagliato")
		return
	}
	delete(h.keys, board)
	no := false
	c.info.DisplayPaired = &no
	c.authed, c.sk = false, nil
	h.mu.Unlock()
	if h.Keys != nil {
		if _, err := h.Keys.Delete(board); err != nil {
			h.Log.Printf("abbinamento di %s: chiave non cancellata dalla config: %v", board, err)
		}
	}
	h.Log.Printf("ws: #%d %s ha dimenticato l'abbinamento (Menu del display): chiave cancellata", c.info.ID, board)
	h.changed()
}

// ---------- permessi (§3) ----------

// PermQuestion è una richiesta di permesso da mostrare sui display.
type PermQuestion struct {
	Session string // session_id di Claude Code
	Name    string // nome della sessione come nello state
	Tool    string
	Arg     string // argomento corto
}

// PermOutcome è l'esito di una richiesta.
type PermOutcome struct {
	How    string // allow, deny (dal display) · terminal (hook chiuso) · session · service
	Board  string // display che ha risposto (allow/deny)
	WaitMs int64  // dall'arrivo della richiesta alla risposta
}

// Motivi di perm-close.
const (
	CloseDisplay  = "display"
	CloseTerminal = "terminal"
	CloseSession  = "session"
	CloseService  = "service"
)

func permCloseReason(how string) string {
	switch how {
	case "allow", "deny":
		return CloseDisplay
	case CloseTerminal, CloseSession, CloseService:
		return how
	}
	return CloseService
}

type permReq struct {
	id       string
	seq      int
	q        PermQuestion
	at       int64
	done     chan PermOutcome
	resolved bool
}

// PermStatus: una richiesta aperta in GET /api/status e /debug.
type PermStatus struct {
	ID      string   `json:"id"`
	Session string   `json:"session"`
	Name    string   `json:"name"`
	Tool    string   `json:"tool"`
	Arg     string   `json:"arg"`
	At      int64    `json:"at"`
	SentTo  []string `json:"sentTo"`
}

const maxNonceIDs = 256

// cleanField: niente caratteri di controllo (0x1F separa i campi nel mac), poi taglio UTF-8 sicuro.
func cleanField(s string, max int) string {
	s = strings.Map(func(r rune) rune {
		if r < 0x20 || r == 0x7F {
			return ' '
		}
		return r
	}, strings.ToValidUTF8(s, "?"))
	return m5.TruncUTF8(strings.TrimSpace(s), max)
}

// OpenPermission registra una richiesta e la manda a tutti i display autenticati con permOn vero.
// false = nessun display può rispondere (il hook esce subito: decide il terminale).
func (h *Hub) OpenPermission(q PermQuestion) (string, bool) {
	if q.Session == "" || len(q.Session) > m5.SessionMax || cleanField(q.Session, m5.SessionMax) != q.Session {
		return "", false
	}
	q.Name = cleanField(q.Name, m5.NameMax)
	q.Tool = cleanField(q.Tool, m5.ToolMax)
	q.Arg = cleanField(q.Arg, m5.ArgMax)
	if q.Tool == "" {
		return "", false
	}
	h.mu.Lock()
	if h.stopping || h.closed {
		h.mu.Unlock()
		return "", false
	}
	var targets []*conn
	for _, c := range h.conns {
		if h.eligible(c) {
			targets = append(targets, c)
		}
	}
	if len(targets) == 0 {
		h.mu.Unlock()
		return "", false
	}
	h.permSeq++
	req := &permReq{id: m5.Hex(m5.Random(m5.NonceLen)), seq: h.permSeq, q: q, at: h.Now().UnixMilli(), done: make(chan PermOutcome, 1)}
	h.perms[req.id] = req
	sort.Slice(targets, func(i, j int) bool { return targets[i].info.ID < targets[j].info.ID })
	boards := make([]string, 0, len(targets))
	for _, c := range targets {
		h.queuePerm(c, req)
		boards = append(boards, c.info.Board)
	}
	h.mu.Unlock()
	h.Log.Printf("permesso %s: %s · %s %s → %s", req.id, q.Name, q.Tool, q.Arg, strings.Join(boards, ", "))
	h.changed()
	return req.id, true
}

// queuePerm mette in coda perm-request per c (una volta per connessione). Con h.mu.
func (h *Hub) queuePerm(c *conn, req *permReq) {
	if c.sent[req.id] {
		return
	}
	c.sent[req.id] = true
	id := req.id
	ok := h.enqueueOut(c, lazyMsg(func() any {
		h.mu.Lock()
		defer h.mu.Unlock()
		if !c.authed || c.sk == nil || h.perms[id] != req {
			delete(c.sent, id)
			return nil
		}
		q := req.q
		mac := m5.ReqMac(c.sk, id, q.Session, q.Name, q.Tool, q.Arg, req.at)
		return PermRequestMsg{T: "perm-request", V: ProtocolVersion, ID: id, Session: q.Session, Name: q.Name,
			Tool: q.Tool, Arg: q.Arg, At: req.at, Mac: m5.Hex(mac)}
	}))
	if !ok {
		delete(c.sent, id)
	}
}

// queueOpenPerms manda a c le richieste ancora aperte che non ha (display appena autenticato o permOn
// appena acceso). Con h.mu.
func (h *Hub) queueOpenPerms(c *conn) {
	if !h.eligible(c) || len(h.perms) == 0 {
		return
	}
	list := make([]*permReq, 0, len(h.perms))
	for _, r := range h.perms {
		list = append(list, r)
	}
	sort.Slice(list, func(i, j int) bool { return list[i].seq < list[j].seq })
	for _, r := range list {
		h.queuePerm(c, r)
	}
}

// WaitPermission aspetta l'esito, senza limite di tempo (decisione «nessuna scadenza»). Se ctx si
// chiude (Claude Code ha chiuso il hook: risposta nel terminale) la richiesta si chiude con «terminal».
func (h *Hub) WaitPermission(ctx context.Context, id string) PermOutcome {
	h.mu.Lock()
	req := h.perms[id]
	h.mu.Unlock()
	if req == nil {
		return PermOutcome{How: CloseService}
	}
	select {
	case out := <-req.done:
		return out
	case <-ctx.Done():
		h.resolve(id, PermOutcome{How: CloseTerminal})
		return <-req.done // la prima risposta vale: può essere arrivata dal display nello stesso istante
	}
}

// resolve chiude la richiesta (la prima volta sola) e manda perm-close ai display che l'hanno.
func (h *Hub) resolve(id string, out PermOutcome) bool {
	h.mu.Lock()
	req := h.perms[id]
	if req == nil || req.resolved {
		h.mu.Unlock()
		return false
	}
	req.resolved = true
	delete(h.perms, id)
	out.WaitMs = max(h.Now().UnixMilli()-req.at, 0)
	reason := permCloseReason(out.How)
	for _, c := range h.conns {
		if c.authed && c.sent[id] {
			h.enqueueOut(c, h.closeMsg(c, id, reason))
		}
	}
	req.done <- out
	h.mu.Unlock()
	who := ""
	if out.Board != "" {
		who = " da " + out.Board
	}
	h.Log.Printf("permesso %s chiuso: %s%s dopo %s", id, out.How, who, time.Duration(out.WaitMs)*time.Millisecond)
	h.changed()
	return true
}

// closeMsg costruisce perm-close con la chiave di sessione di c al momento di scriverlo.
func (h *Hub) closeMsg(c *conn, id, reason string) lazyMsg {
	return func() any {
		h.mu.Lock()
		defer h.mu.Unlock()
		return h.closeMsgLocked(c, id, reason)
	}
}

// closeMsgLocked: perm-close firmato con SK di c (nil se c non è autenticata). Con h.mu.
func (h *Hub) closeMsgLocked(c *conn, id, reason string) any {
	if !c.authed || c.sk == nil {
		return nil
	}
	delete(c.sent, id)
	return PermCloseMsg{T: "perm-close", V: ProtocolVersion, ID: id, Reason: reason, Mac: m5.Hex(m5.CloseMac(c.sk, id, reason))}
}

// ClosePermission chiude una richiesta risolta altrove (how: terminal o session): perm-close ai
// display, il hook in attesa esce senza decisione. Una risposta del display che arriva dopo si scarta.
func (h *Hub) ClosePermission(id, how string) { h.resolve(id, PermOutcome{How: how}) }

// ClosePermissions chiude le richieste di una sessione finita (SessionEnd): perm-close «session».
func (h *Hub) ClosePermissions(session string) {
	h.mu.Lock()
	var ids []string
	for id, r := range h.perms {
		if r.q.Session == session {
			ids = append(ids, id)
		}
	}
	h.mu.Unlock()
	for _, id := range ids {
		h.resolve(id, PermOutcome{How: CloseSession})
	}
}

// StopPermissions: servizio in arresto. perm-close «service» a tutti (scritto subito, prima che le
// connessioni si chiudano), i hook in attesa escono senza decisione; nessuna richiesta nuova.
func (h *Hub) StopPermissions() {
	type job struct {
		c   *conn
		msg any
	}
	var jobs []job
	h.mu.Lock()
	h.stopping = true
	for id, r := range h.perms {
		r.resolved = true
		for _, c := range h.conns {
			if c.authed && c.sent[id] {
				jobs = append(jobs, job{c, h.closeMsgLocked(c, id, CloseService)})
			}
		}
		r.done <- PermOutcome{How: CloseService, WaitMs: max(h.Now().UnixMilli()-r.at, 0)}
	}
	n := len(h.perms)
	h.perms = map[string]*permReq{}
	h.mu.Unlock()
	if n == 0 {
		return
	}
	done := make(chan struct{}, len(jobs))
	for _, j := range jobs {
		go func() {
			defer func() { done <- struct{}{} }()
			if j.msg == nil {
				return
			}
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			_ = h.write(ctx, j.c, j.msg)
		}()
	}
	for range jobs {
		<-done
	}
	h.Log.Printf("servizio in arresto: %d richieste di permesso chiuse (le decide il terminale)", n)
	h.changed()
}

// Permissions restituisce le richieste aperte, le più vecchie per prime.
func (h *Hub) Permissions() []PermStatus {
	h.mu.Lock()
	list := make([]*permReq, 0, len(h.perms))
	for _, r := range h.perms {
		list = append(list, r)
	}
	sort.Slice(list, func(i, j int) bool { return list[i].seq < list[j].seq })
	out := make([]PermStatus, 0, len(list))
	for _, r := range list {
		st := PermStatus{ID: r.id, Session: r.q.Session, Name: r.q.Name, Tool: r.q.Tool, Arg: r.q.Arg, At: r.at, SentTo: []string{}}
		for _, c := range h.conns {
			if c.sent[r.id] {
				st.SentTo = append(st.SentTo, c.info.Board)
			}
		}
		sort.Strings(st.SentTo)
		out = append(out, st)
	}
	h.mu.Unlock()
	return out
}

// onPermReply: accettata solo da una connessione autenticata, con mac giusto, nonce mai visto per
// quell'id e id ancora in attesa (M5 §3). Ogni scarto finisce nel log con il motivo.
func (h *Hub) onPermReply(c *conn, m *m5In) {
	reject := func(why string) {
		h.ignore(c.info.ID, "perm-reply scartata (%s): id %q", why, clipStr(m.ID))
	}
	h.mu.Lock()
	if !c.authed || c.sk == nil {
		h.mu.Unlock()
		reject("connessione non autenticata")
		return
	}
	mac, okMac := m5.ParseHex(m.Mac, m5.MacLen)
	_, okNonce := m5.ParseHex(m.Nonce, m5.NonceLen)
	if len(m.ID) != m5.ReqIDHex || !m5.IsLowerHex(m.ID) || (m.Decision != "allow" && m.Decision != "deny") || !okNonce || !okMac {
		h.mu.Unlock()
		reject("campi non validi")
		return
	}
	if !m5.Equal(mac, m5.RepMac(c.sk, m.ID, m.Decision, m.Nonce)) {
		h.mu.Unlock()
		reject("mac sbagliato")
		return
	}
	seen := h.nonces[m.ID]
	if seen[m.Nonce] {
		h.mu.Unlock()
		reject("nonce già visto: risposta ripetuta")
		return
	}
	if seen == nil {
		seen = map[string]bool{}
		h.nonces[m.ID] = seen
		h.nonceIDs = append(h.nonceIDs, m.ID)
		if len(h.nonceIDs) > maxNonceIDs {
			delete(h.nonces, h.nonceIDs[0])
			h.nonceIDs = h.nonceIDs[1:]
		}
	}
	seen[m.Nonce] = true
	_, open := h.perms[m.ID]
	board := c.info.Board
	h.mu.Unlock()
	if !open {
		reject("id inesistente o non più in attesa")
		return
	}
	if !h.resolve(m.ID, PermOutcome{How: m.Decision, Board: board}) {
		reject("richiesta già chiusa")
		return
	}
	h.Log.Printf("ws: #%d %s ha risposto %s alla richiesta %s", c.info.ID, board, m.Decision, m.ID)
}

func clipStr(s string) string {
	if len(s) > 40 {
		return m5.TruncUTF8(s, 40) + "…"
	}
	return s
}
