// Package display gestisce le connessioni WebSocket dei display (docs/PROTOCOLLO.md, v1).
package display

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/url"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"

	"calamaio/internal/ota"
	"calamaio/internal/state"
)

const ProtocolVersion = 1

// Hello è il primo messaggio del display.
type Hello struct {
	T      string `json:"t"`
	V      int    `json:"v"`
	Board  string `json:"board"`
	Device string `json:"device"`
	Class  string `json:"class"`
	FW     string `json:"fw"`
	W      int    `json:"w"`
	H      int    `json:"h"`
	// Paired (M5): il display ha una chiave di abbinamento. Assente nei firmware senza M5.
	Paired *bool `json:"paired,omitempty"`
	// Caps (firmware 0.14.0): capacità dichiarate, per ora solo "avatar-dl" (avatar_dl.go). Assente nei firmware vecchi.
	Caps []string `json:"caps,omitempty"`
}

// Welcome è la risposta del servizio a hello.
type Welcome struct {
	T       string `json:"t"`
	V       int    `json:"v"`
	Time    int64  `json:"time"`
	TZ      string `json:"tz"`
	Host    string `json:"host"`
	Service string `json:"service"`
	// Paired (M5): il servizio ha una chiave per questa scheda (il display la confronta con la sua).
	Paired bool `json:"paired"`
	// Avatar predefinito per le schermate senza sessione (facoltativi, docs/PROTOCOLLO.md «Avatar»).
	DefaultAvatar    string  `json:"defaultAvatar,omitempty"`
	DefaultAvatarCrc *uint32 `json:"defaultAvatarCrc,omitempty"`
}

// Info descrive un display collegato (per /debug e `calamaio status`).
type Info struct {
	ID        int       `json:"id"`
	Board     string    `json:"board"`
	Device    string    `json:"device"`
	Class     string    `json:"class"`
	FW        string    `json:"fw"`
	W         int       `json:"w"`
	H         int       `json:"h"`
	IP        string    `json:"ip"`
	Since     time.Time `json:"since"`
	Hello     bool      `json:"hello"`
	Remote    bool      `json:"remote"`
	States    int       `json:"states"`
	LastState time.Time `json:"last_state,omitzero"`
	// M5: abbinamento e autenticazione (calcolati in Displays).
	Paired        bool   `json:"paired"`         // il servizio ha una K per questa scheda
	DisplayPaired *bool  `json:"display_paired"` // hello.paired
	Authed        bool   `json:"authed"`
	Pair          string `json:"pair"` // none, ok, redo, auth (vedi PairState)
	PairReason    string `json:"pair_reason,omitempty"`
	Perms         bool   `json:"perms"` // riceve le richieste di permesso
	// OTA: alimentazione riferita con `status` e aggiornamento in corso o ultimo (calcolati in Displays).
	Power *Power   `json:"power,omitempty"`
	OTA   *OtaView `json:"ota,omitempty"`
	// Giornata di lavoro riferita dalla scheda (nil = mai riferita su questa connessione).
	Workday *Workday `json:"workday,omitempty"`
}

// Hub accetta i display su /ws e manda loro lo stato letto da Source.
type Hub struct {
	Source state.Source
	// Archiver riceve i `dismiss` dei display (state.Store). nil = si rifiutano.
	Archiver state.Archiver
	// Avatars: libreria degli avatar (avatar-need → avatar). nil = niente avatar.
	Avatars AvatarSource
	// Game: gioco della mascotte (game.go). nil = niente messaggi game.
	Game    GameSource
	Log     *log.Logger
	TZ      string
	Host    string
	Version string

	PingInterval time.Duration // 15 s dal protocollo
	PingTimeout  time.Duration // oltre, la connessione si considera morta
	HelloTimeout time.Duration
	MinInterval  time.Duration // al massimo uno `state` ogni 250 ms per display
	WriteTimeout time.Duration
	AckTimeout   time.Duration // 10 s: oltre, un comando senza `ack` è considerato fallito
	Now          func() time.Time

	mu      sync.Mutex
	conns   map[int]*conn
	nextID  int
	closed  bool
	ignored atomic.Int64
	wg      sync.WaitGroup
	nextCmd int
	subs    map[int]chan struct{}
	nextSub int

	// M5 (m5.go): chiavi di abbinamento, richieste di permesso aperte, nonce visti.
	Keys       KeyStore      // nil = solo in memoria
	PairWindow time.Duration // 3 minuti (M5 §1)
	AuthWait   time.Duration // senza risposta alla sfida entro questo tempo: «da rifare»
	keys       map[string][]byte
	perms      map[string]*permReq
	permSeq    int
	nonces     map[string]map[string]bool // id → nonce visti (anche dopo la risposta)
	nonceIDs   []string                   // ordine di arrivo, per tenerne al massimo maxNonceIDs
	stopping   bool                       // servizio in arresto: niente richieste nuove
	farewell   bool                       // M6: `bye` partito, niente più state né connessioni nuove

	// Aggiornamento remoto (ota.go), registro da remoto e crash (diag.go).
	OTAKey       *ota.KeyCache // chiave di firma (<config>/calamaio/ota-key); nil = nessuna
	TokenTTL     time.Duration // 5 minuti: validità del token di download (docs/OTA.md §3.2)
	OfferTimeout time.Duration // senza un primo ota-status entro questo tempo l'offerta fallisce
	StallTimeout time.Duration // senza ota-status durante ricevo/verifico/scrivo: errore
	RebootWait   time.Duration // dopo «riavvio»: tempo massimo per il ritorno della scheda
	LogFiles     LogSink       // dove si salva il registro da remoto (nil = solo in memoria)
	LogQuiet     time.Duration // senza follow: fine del contenuto dell'anello se la scheda non manda `end`
	Crashes      CrashSaver    // dove si salvano i rapporti di crash (nil = solo nel log)
	otaJobs      map[string]*otaJob
	otaTokens    map[string]*otaToken
	logs         map[string]*boardLog

	// Reti Wi-Fi delle schede (wifi.go): ultima wifi-list e operazione in attesa di ack, per scheda.
	wifi map[string]*boardWifi

	// Temi delle schermate (themes.go): ultimo themes e operazione per scheda, token di download.
	ThemeAckTimeout time.Duration // senza theme-ack entro questo tempo dall'offerta: errore
	ThemeUseTimeout time.Duration // senza il themes col tema attivo entro questo tempo da theme-use: errore
	themes          map[string]*boardThemes
	themeTokens     map[string]*themeToken

	// Avatar a pezzi (avatar_dl.go): token di download e consegne fallite per scheda (board → id/crc → fino a quando).
	AvatarAckTimeout time.Duration // senza avatar-ack entro questo tempo dall'offerta: fallita
	AvatarFailHold   time.Duration // avatarFailHold; nei test più corto
	avTokens         map[string]*avatarToken
	avFail           map[string]map[string]time.Time
}

type conn struct {
	ws     *websocket.Conn
	cancel context.CancelFunc
	info   Info
	out    chan any // comandi del servizio (`set`, `full-refresh`), li scrive run

	// Impostazioni remote (protette da Hub.mu).
	settings map[string]any        // nil finché il display non manda `settings`
	pending  map[string]pendingVal // chiave → valore chiesto e non ancora confermato
	cmds     map[string]*command   // comandi in attesa di `ack`
	lastErr  *CmdError

	// M5 (protetti da Hub.mu).
	ns           []byte    // sfida mandata e non ancora risposta
	challengedAt time.Time // quando
	authed       bool
	authFail     string // perché l'ultima auth è fallita
	sk           []byte // chiave di sessione (solo con authed)
	pair         *pairing
	sent         map[string]bool // richieste di permesso mandate su questa connessione

	// OTA e diagnosi (protetti da Hub.mu).
	local     string // indirizzo:porta del PC su questa connessione (per l'url di download)
	otaKeyPub string // chiave pubblica già mandata con ota-key
	power     *Power // ultimo `status`
	legacyAv  bool   // il firmware non conosce la posa run: avatar e CRC senza run (avatar.go)
	// Avatar a pezzi (avatar_dl.go, protetti da Hub.mu): hello con caps avatar-dl, offerta in corso, coda, `avatar`
	// mandati alle schede senza la capacità (per riconoscere una chiusura subito dopo).
	avatarDl bool
	avOffer  *avatarOffer
	avQueue  []avatarItem
	avLegacy []avatarLegacySent

	// Giornata di lavoro (workday.go, protetti da Hub.mu).
	workday     *Workday       // ultimo stato riferito in `status` (nil = mai su questa connessione)
	workdaySeen bool           // la scheda ha mandato `status.workday` (anche null): conosce la giornata
	wdOp        *workdayOp     // comando workday in attesa di ack
	wdLast      *WorkdayResult // esito dell'ultimo comando
}

func NewHub(src state.Source, lg *log.Logger) *Hub {
	h := &Hub{
		Source:       src,
		Log:          lg,
		Version:      "dev",
		PingInterval: 15 * time.Second,
		PingTimeout:  15 * time.Second,
		HelloTimeout: 10 * time.Second,
		MinInterval:  250 * time.Millisecond,
		WriteTimeout: 5 * time.Second,
		AckTimeout:   10 * time.Second,
		Now:          time.Now,
		conns:        map[int]*conn{},
		subs:         map[int]chan struct{}{},
		PairWindow:   3 * time.Minute,
		AuthWait:     10 * time.Second,
		TokenTTL:     5 * time.Minute,
		OfferTimeout: 30 * time.Second,
		StallTimeout: 60 * time.Second,
		RebootWait:   3 * time.Minute,
		LogQuiet:     3 * time.Second,
		keys:         map[string][]byte{},
		perms:        map[string]*permReq{},
		nonces:       map[string]map[string]bool{},
	}
	h.ThemeAckTimeout, h.ThemeUseTimeout = 60*time.Second, 15*time.Second // temi (themes.go)
	h.AvatarAckTimeout, h.AvatarFailHold = 30*time.Second, avatarFailHold // avatar a pezzi (avatar_dl.go)
	return h
}

// Handler espone /ws.
func (h *Hub) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.Handle("GET /ws", h)
	mux.HandleFunc("GET /ota/{token}", h.ServeOTA)        // immagine dell'aggiornamento remoto (ota.go)
	mux.HandleFunc("GET /themes/{token}", h.ServeTheme)   // temi delle schermate (themes.go)
	mux.HandleFunc("GET /avatars/{token}", h.ServeAvatar) // avatar a pezzi (avatar_dl.go)
	return mux
}

// Displays restituisce i display collegati, i più vecchi per primi.
func (h *Hub) Displays() []Info {
	h.mu.Lock()
	out := make([]Info, 0, len(h.conns))
	for _, c := range h.conns {
		in := c.info
		if in.Hello {
			in.Paired = h.keys[in.Board] != nil
			in.Authed = c.authed
			in.Pair, in.PairReason = h.pairState(c)
			in.Perms = h.eligible(c)
			in.Power = c.power
			if c.workday != nil {
				w := *c.workday
				in.Workday = &w
			}
			if h.otaJobs != nil {
				in.OTA = h.otaJobs[in.Board].view()
			}
		}
		out = append(out, in)
	}
	h.mu.Unlock()
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// Ignored conta i messaggi scartati (malformati, sconosciuti, fuori sequenza).
func (h *Hub) Ignored() int64 { return h.ignored.Load() }

// Close chiude tutte le connessioni (spegnimento del servizio) e aspetta che finiscano.
func (h *Hub) Close() {
	h.StopPermissions() // perm-close «service» prima di chiudere le connessioni (di solito l'ha già fatto Run)
	h.mu.Lock()
	h.closed = true
	conns := make([]*conn, 0, len(h.conns))
	for _, c := range h.conns {
		conns = append(conns, c)
	}
	h.mu.Unlock()
	var wg sync.WaitGroup
	for _, c := range conns {
		wg.Add(1)
		go func() {
			defer wg.Done()
			// `bye` (M6) l'ha già mandato Run, se il servizio si ferma in modo pulito (Bye).
			_ = c.ws.Close(websocket.StatusGoingAway, "servizio in arresto")
			c.cancel()
		}()
	}
	wg.Wait()
	h.wg.Wait()
}

// checkOrigin: i display non mandano Origin, oppure (arduinoWebSockets) mandano "file://".
// I browser mandano sempre un'origine con host: una pagina web di un altro sito non deve
// poter leggere lo stato, quindi si accetta solo se l'host coincide.
func checkOrigin(r *http.Request) error {
	o := r.Header.Get("Origin")
	if o == "" || o == "file://" {
		return nil
	}
	u, err := url.Parse(o)
	if err == nil && u.Host != "" && strings.EqualFold(u.Host, r.Host) {
		return nil
	}
	return fmt.Errorf("origine %q non ammessa", o)
}

func (h *Hub) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	ip := remoteIP(r.RemoteAddr)
	h.mu.Lock()
	closed := h.closed || h.farewell
	h.mu.Unlock()
	if closed {
		http.Error(w, "servizio in arresto", http.StatusServiceUnavailable)
		return
	}
	if err := checkOrigin(r); err != nil {
		h.Log.Printf("ws: rifiutata connessione da %s: %v", ip, err)
		http.Error(w, "origine non ammessa", http.StatusForbidden)
		return
	}
	ws, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
	if err != nil {
		h.Log.Printf("ws: handshake fallito da %s: %v", ip, err)
		return
	}
	ws.SetReadLimit(64 << 10)

	ctx, cancel := context.WithCancel(context.Background())
	c := &conn{ws: ws, cancel: cancel, info: Info{IP: ip, Since: h.Now()}, out: make(chan any, 64),
		pending: map[string]pendingVal{}, cmds: map[string]*command{}, sent: map[string]bool{}, local: localAddr(r)}
	h.mu.Lock()
	if h.closed {
		h.mu.Unlock()
		cancel()
		_ = ws.Close(websocket.StatusGoingAway, "servizio in arresto")
		return
	}
	h.nextID++
	c.info.ID = h.nextID
	h.conns[c.info.ID] = c
	h.wg.Add(1)
	h.mu.Unlock()
	defer h.wg.Done()

	h.Log.Printf("ws: connessione #%d da %s", c.info.ID, ip)
	reason := h.run(ctx, c)

	h.mu.Lock()
	delete(h.conns, c.info.ID)
	info := c.info
	for _, cmd := range c.cmds {
		cmd.timer.Stop()
	}
	h.endConnM5(c)
	h.otaConnClosed(c)
	h.logConnClosed(c)
	h.wifiConnClosed(c)
	h.themeConnClosed(c)
	h.avatarConnClosed(c)
	h.mu.Unlock()
	h.changed()
	cancel()
	_ = ws.CloseNow()
	name := info.Board
	if name == "" {
		name = "(senza hello)"
	}
	h.Log.Printf("ws: display scollegato #%d %s da %s dopo %s, %d stati inviati: %s",
		info.ID, name, ip, h.Now().Sub(info.Since).Round(time.Second), info.States, reason)
}

// run gestisce una connessione fino alla sua fine e restituisce il motivo.
func (h *Hub) run(ctx context.Context, c *conn) string {
	in := make(chan inbound, 8)
	readErr := make(chan error, 1)
	go func() { readErr <- h.readLoop(ctx, c, in) }()

	notify, unsub := h.Source.Subscribe()
	defer unsub()

	ping := time.NewTicker(h.PingInterval)
	defer ping.Stop()
	helloTimer := time.NewTimer(h.HelloTimeout)
	defer helloTimer.Stop()

	var (
		greeted  bool
		last     time.Time
		delay    *time.Timer
		delayC   <-chan time.Time
		pingBusy atomic.Bool
		pingFail = make(chan error, 1)
		avSent   = map[string]avatarSent{}
		accSent  = map[string]avatarSent{} // accessori mandati (game.go)
		ancSent  = map[string]avatarSent{} // ancore mandate (game.go)
		bye      bool                      // `bye` mandato: niente più state
	)
	defer func() {
		if delay != nil {
			delay.Stop()
		}
	}()

	sendState := func() error {
		msg := h.Source.Message()
		if c.legacyAv {
			msg = h.legacyState(msg)
		}
		if err := h.write(ctx, c, msg); err != nil {
			return err
		}
		last = h.Now()
		h.mu.Lock()
		c.info.States++
		c.info.LastState = last
		h.mu.Unlock()
		return nil
	}

	for {
		select {
		case <-ctx.Done():
			return "chiusa dal servizio"
		case err := <-readErr:
			return closeReason(err)
		case err := <-pingFail:
			// Il display non risponde: niente chiusura cortese (aspetterebbe 5 s un frame che non arriva).
			_ = c.ws.CloseNow()
			return "ping senza risposta: " + err.Error()
		case <-helloTimer.C:
			if !greeted {
				_ = c.ws.Close(websocket.StatusPolicyViolation, "hello mancante")
				return fmt.Sprintf("nessun hello entro %s", h.HelloTimeout)
			}
		case m := <-in:
			if m.hello == nil {
				if !greeted {
					h.ignore(c.info.ID, "%q prima di hello", m.t)
					continue
				}
				if m.m5 != nil {
					if err := h.onM5(ctx, c, m.t, m.m5); err != nil {
						return "scrittura fallita: " + err.Error()
					}
					continue
				}
				switch m.t {
				case "ota-status":
					h.onOtaStatus(c, m.otaStatus)
					continue
				case "status":
					h.onPower(c, m.power)
					continue
				case "log":
					h.onLog(c, m.log)
					continue
				case "crash":
					h.onCrash(c, m.crash)
					continue
				case "wifi-list":
					h.onWifiList(c, m.wifi)
					continue
				case "themes":
					h.onThemes(c, m.themes)
					continue
				case "theme-ack":
					h.onThemeAck(c, m.themeAck)
					continue
				case "avatar-ack":
					h.onAvatarAck(c, m.avatarAck)
					continue
				}
				switch m.t {
				case "accessory-need":
					if err := h.sendAccessories(ctx, c, m.need, accSent); err != nil {
						return "scrittura fallita: " + err.Error()
					}
					continue
				case "anchors-need":
					if err := h.sendAnchors(ctx, c, m.anchors, ancSent); err != nil {
						return "scrittura fallita: " + err.Error()
					}
					continue
				case "accessory-wear":
					h.onWear(c, m.wear)
					continue
				}
				if m.t == "avatar-need" {
					if err := h.sendAvatars(ctx, c, m.need, avSent); err != nil {
						return "scrittura fallita: " + err.Error()
					}
					continue
				}
				if m.t == "dismiss" {
					// risposta subito, poi lo state senza la sessione (senza aspettare MinInterval)
					ack := h.dismiss(c, m.dismiss)
					if err := h.write(ctx, c, ack); err != nil {
						return "scrittura fallita: " + err.Error()
					}
					if ack.OK {
						if err := sendState(); err != nil {
							return "scrittura fallita: " + err.Error()
						}
					}
					continue
				}
				h.receive(c, m)
				continue
			}
			if bye || h.farewellSet() {
				continue // servizio in arresto: niente welcome
			}
			hl := *m.hello
			first := !greeted
			greeted = true
			h.mu.Lock()
			c.info.Board, c.info.Device, c.info.Class, c.info.FW = hl.Board, hl.Device, hl.Class, hl.FW
			c.info.W, c.info.H, c.info.Hello = hl.W, hl.H, true
			c.info.DisplayPaired = hl.Paired
			c.avatarDl = hasCap(hl.Caps, CapAvatarDl)
			paired := h.keys[hl.Board] != nil
			h.mu.Unlock()
			if first {
				h.Log.Printf("ws: display collegato #%d %s (%s, classe %s, %dx%d, fw %s) da %s",
					c.info.ID, hl.Board, hl.Device, hl.Class, hl.W, hl.H, hl.FW, c.info.IP)
				h.replaceOlder(c.info.ID, hl.Board)
			} else {
				h.Log.Printf("ws: #%d ha ripetuto hello: rimando welcome e state", c.info.ID)
			}
			wel := Welcome{T: "welcome", V: ProtocolVersion, Time: h.Now().UnixMilli(),
				TZ: h.TZ, Host: h.Host, Service: h.Version, Paired: paired}
			c.legacyAv = !ota.KnowsRunPose(hl.FW)
			h.welcomeAvatar(&wel)
			if c.legacyAv && wel.DefaultAvatarCrc != nil {
				lc := h.legacyCRC(wel.DefaultAvatar, *wel.DefaultAvatarCrc)
				wel.DefaultAvatarCrc = &lc
			}
			if err := h.write(ctx, c, wel); err != nil {
				return "scrittura fallita: " + err.Error()
			}
			if err := sendState(); err != nil {
				return "scrittura fallita: " + err.Error()
			}
			if err := h.sendGame(ctx, c); err != nil {
				return "scrittura fallita: " + err.Error()
			}
			// M5 §2: con una K per questa scheda, sfida a ogni connessione.
			if err := h.challenge(ctx, c); err != nil {
				return "scrittura fallita: " + err.Error()
			}
			if first {
				h.changed()
			}
		case cmd := <-c.out:
			if b, ok := cmd.(*byeCmd); ok {
				err := h.write(ctx, c, b.msg)
				b.done <- err
				bye = true
				if err != nil {
					return "scrittura fallita: " + err.Error()
				}
				continue
			}
			if f, ok := cmd.(lazyMsg); ok {
				// messaggi M5 costruiti all'ultimo momento, con la chiave di sessione attuale
				if cmd = f(); cmd == nil {
					continue
				}
			}
			if err := h.write(ctx, c, cmd); err != nil {
				return "scrittura fallita: " + err.Error()
			}
		case <-notify:
			if !greeted || delayC != nil || bye {
				continue // prima di hello non si manda niente; un invio è già in programma
			}
			if wait := h.MinInterval - h.Now().Sub(last); wait > 0 {
				delay = time.NewTimer(wait)
				delayC = delay.C
				continue
			}
			if err := sendState(); err != nil {
				return "scrittura fallita: " + err.Error()
			}
		case <-delayC:
			delayC = nil
			if bye {
				continue
			}
			if err := sendState(); err != nil {
				return "scrittura fallita: " + err.Error()
			}
		case <-ping.C:
			if !pingBusy.CompareAndSwap(false, true) {
				continue
			}
			go func() {
				defer pingBusy.Store(false)
				pctx, cancel := context.WithTimeout(ctx, h.PingTimeout)
				defer cancel()
				if err := c.ws.Ping(pctx); err != nil && ctx.Err() == nil {
					select {
					case pingFail <- err:
					default:
					}
				}
			}()
		}
	}
}

func (h *Hub) write(ctx context.Context, c *conn, v any) error {
	b, err := json.Marshal(v)
	if err != nil {
		return err
	}
	wctx, cancel := context.WithTimeout(ctx, h.WriteTimeout)
	defer cancel()
	return c.ws.Write(wctx, websocket.MessageText, b)
}

// readLoop legge i messaggi del display. Quelli malformati o sconosciuti si registrano e si ignorano.
func (h *Hub) readLoop(ctx context.Context, c *conn, in chan<- inbound) error {
	id := c.info.ID
	for {
		typ, data, err := c.ws.Read(ctx)
		if err != nil {
			return err
		}
		if typ != websocket.MessageText {
			h.ignore(id, "frame binario di %d byte", len(data))
			continue
		}
		var head struct {
			T string `json:"t"`
			V *int   `json:"v"`
		}
		if err := json.Unmarshal(data, &head); err != nil {
			h.ignore(id, "JSON malformato (%v): %q", err, clip(data))
			continue
		}
		if head.V == nil || *head.V != ProtocolVersion {
			h.ignore(id, "versione del protocollo assente o diversa da %d: %q", ProtocolVersion, clip(data))
			continue
		}
		switch head.T {
		case "hello":
			var hl Hello
			if err := json.Unmarshal(data, &hl); err != nil || hl.Board == "" {
				h.ignore(id, "hello non valido (%v): %q", err, clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: "hello", hello: &hl}); err != nil {
				return err
			}
		case "settings":
			var m settingsMsg
			if err := json.Unmarshal(data, &m); err != nil || m.Settings == nil {
				h.ignore(id, "settings non valido (%v): %q", err, clip(data))
				continue
			}
			vals, unknown, err := parseReported(m.Settings)
			if err != nil {
				h.ignore(id, "settings non valido (%v): %q", err, clip(data))
				continue
			}
			if len(unknown) > 0 {
				h.Log.Printf("ws: #%d settings: chiavi sconosciute scartate %v", id, unknown)
			}
			if err := deliver(ctx, in, inbound{t: "settings", settings: vals, remote: m.Remote}); err != nil {
				return err
			}
		case "ack":
			var m ackMsg
			if err := json.Unmarshal(data, &m); err != nil || m.ID == "" || m.OK == nil {
				h.ignore(id, "ack non valido (%v): %q", err, clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: "ack", ack: &m}); err != nil {
				return err
			}
		case "dismiss":
			var m dismissMsg
			if err := json.Unmarshal(data, &m); err != nil || len(m.ID) < 2 || len(m.ID) > 32 || !strings.HasPrefix(m.ID, "d") || m.Session == "" {
				h.ignore(id, "dismiss non valido (%v): %q", err, clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: "dismiss", dismiss: &m}); err != nil {
				return err
			}
		case "avatar-need":
			ids, ok := parseAvatarNeed(data)
			if !ok {
				h.ignore(id, "avatar-need non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: "avatar-need", need: ids}); err != nil {
				return err
			}
		case "accessory-need":
			ids, ok := parseAvatarNeed(data)
			if !ok {
				h.ignore(id, "accessory-need non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, need: ids}); err != nil {
				return err
			}
		case "anchors-need":
			avs, ok := parseAnchorsNeed(data)
			if !ok {
				h.ignore(id, "anchors-need non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, anchors: avs}); err != nil {
				return err
			}
		case "accessory-wear":
			w, ok := parseWear(data)
			if !ok {
				h.ignore(id, "accessory-wear non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, wear: w}); err != nil {
				return err
			}
		case "ota-status":
			m, ok := parseOtaStatus(data)
			if !ok {
				h.ignore(id, "ota-status non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, otaStatus: m}); err != nil {
				return err
			}
		case "status":
			m, ok := parsePower(data)
			if !ok {
				h.ignore(id, "status non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, power: m}); err != nil {
				return err
			}
		case "log":
			m, ok := parseLog(data)
			if !ok {
				h.ignore(id, "log non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, log: m}); err != nil {
				return err
			}
		case "crash":
			m, ok := parseCrash(data)
			if !ok {
				h.ignore(id, "crash non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, crash: m}); err != nil {
				return err
			}
		case "wifi-list":
			list, ok := parseWifiList(data)
			if !ok {
				h.ignore(id, "wifi-list non valida: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, wifi: list}); err != nil {
				return err
			}
		case "themes":
			m, ok := parseThemes(data)
			if !ok {
				h.ignore(id, "themes non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, themes: m}); err != nil {
				return err
			}
		case "theme-ack":
			m, ok := parseThemeAck(data)
			if !ok {
				h.ignore(id, "theme-ack non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, themeAck: m}); err != nil {
				return err
			}
		case "avatar-ack":
			m, ok := parseAvatarAck(data)
			if !ok {
				h.ignore(id, "avatar-ack non valido: %q", clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, avatarAck: m}); err != nil {
				return err
			}
		case "pair-key", "pair-done", "pair-abort", "auth", "perm-reply", "unpair":
			var m m5In
			if err := json.Unmarshal(data, &m); err != nil {
				h.ignore(id, "%s non valido (%v): %q", head.T, err, clip(data))
				continue
			}
			if err := deliver(ctx, in, inbound{t: head.T, m5: &m}); err != nil {
				return err
			}
		case "":
			h.ignore(id, "messaggio senza tipo: %q", clip(data))
		default:
			h.ignore(id, "tipo sconosciuto %q", head.T)
		}
	}
}

func (h *Hub) ignore(id int, format string, args ...any) {
	h.ignored.Add(1)
	h.Log.Printf("ws: #%d ignorato: "+format, append([]any{id}, args...)...)
}

// replaceOlder chiude le connessioni precedenti dello stesso display (riconnessione dopo un
// calo del Wi-Fi: quella vecchia è morta ma non lo sappiamo ancora).
func (h *Hub) replaceOlder(id int, board string) {
	h.mu.Lock()
	var old []*conn
	for oid, c := range h.conns {
		if oid != id && c.info.Board == board {
			old = append(old, c)
		}
	}
	h.mu.Unlock()
	for _, c := range old {
		h.Log.Printf("ws: #%d sostituita dalla nuova connessione #%d di %s", c.info.ID, id, board)
		go func() {
			_ = c.ws.Close(websocket.StatusGoingAway, "sostituita da una nuova connessione")
			c.cancel()
		}()
	}
}

func closeReason(err error) string {
	if s := websocket.CloseStatus(err); s != -1 {
		return fmt.Sprintf("chiusa dal display (%d %s)", int(s), s)
	}
	if errors.Is(err, context.Canceled) {
		return "chiusa dal servizio"
	}
	return err.Error()
}

func clip(b []byte) string {
	const max = 120
	if len(b) > max {
		return state.Truncate(string(b), max) + "…"
	}
	return string(b)
}

func remoteIP(addr string) string {
	if host, _, err := net.SplitHostPort(addr); err == nil {
		return host
	}
	return addr
}
