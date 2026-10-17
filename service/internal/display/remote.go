package display

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"time"

	"calamaio/internal/diag"
	"calamaio/internal/state"
)

// Impostazioni remote e comandi verso i display (docs/PROTOCOLLO.md: settings, set, full-refresh, ack).

// inbound è un messaggio del display già letto e controllato da readLoop, da applicare in run.
type inbound struct {
	t        string
	hello    *Hello
	settings map[string]any
	remote   bool
	ack      *ackMsg
	dismiss  *dismissMsg
	m5       *m5In
	need     []string // avatar-need, accessory-need
	anchors  []AvatarVersion
	wear     *wearMsg
	// OTA e diagnosi (ota.go, diag.go)
	otaStatus *otaStatusMsg
	power     *powerMsg
	log       *logMsg
	crash     *diag.CrashMsg
	wifi      []WifiNetwork // wifi-list (wifi.go)
	themes    *themesMsg    // themes (themes.go)
	themeAck  *themeAckMsg  // theme-ack (themes.go)
	avatarAck *avatarAckMsg // avatar-ack (avatar_dl.go)
}

// dismissMsg (display → servizio): archivia una sessione ferma. Gli id dei display iniziano
// con "d", quelli dei comandi del servizio con "c": gli ack nei due versi non si confondono.
type dismissMsg struct {
	ID      string `json:"id"`
	Session string `json:"session"`
}

// AckMsg (servizio → display): esito di un `dismiss`.
type AckMsg struct {
	T     string `json:"t"`
	V     int    `json:"v"`
	ID    string `json:"id"`
	OK    bool   `json:"ok"`
	Error string `json:"error,omitempty"`
}

// dismiss archivia la sessione chiesta dal display e prepara l'ack.
func (h *Hub) dismiss(c *conn, m *dismissMsg) AckMsg {
	ack := AckMsg{T: "ack", V: ProtocolVersion, ID: m.ID, OK: true}
	var err error
	if h.Archiver == nil {
		err = errors.New("archiviazione non disponibile")
	} else {
		err = h.Archiver.Archive(m.Session)
	}
	switch {
	case err == nil:
		h.Log.Printf("ws: #%d dismiss %s: sessione %s archiviata", c.info.ID, m.ID, m.Session)
		return ack
	case errors.Is(err, state.ErrNoSession):
		ack.Error = "Sessione sconosciuta"
	case errors.Is(err, state.ErrNotArchivable):
		ack.Error = "Si archiviano solo le sessioni finite, in pausa o in errore"
	default:
		ack.Error = state.Truncate(err.Error(), 95)
	}
	ack.OK = false
	h.Log.Printf("ws: #%d dismiss %s rifiutato: %s", c.info.ID, m.ID, ack.Error)
	return ack
}

type settingsMsg struct {
	Settings map[string]json.RawMessage `json:"settings"`
	Remote   bool                       `json:"remote"`
}

type ackMsg struct {
	ID    string `json:"id"`
	OK    *bool  `json:"ok"`
	Error string `json:"error"`
}

// SetMsg (servizio → display): cambia le impostazioni elencate.
type SetMsg struct {
	T        string         `json:"t"`
	V        int            `json:"v"`
	ID       string         `json:"id"`
	Settings map[string]any `json:"settings"`
}

// FullRefreshMsg (servizio → display): refresh completo del pannello.
type FullRefreshMsg struct {
	T  string `json:"t"`
	V  int    `json:"v"`
	ID string `json:"id"`
}

type pendingVal struct {
	value any
	id    string // comando che l'ha chiesto per ultimo
}

type command struct {
	kind  string // "set", "full-refresh", "wifi-add", "wifi-forget" o "workday"
	keys  []string
	timer *time.Timer
	wifi  bool // operazione Wi-Fi: l'esito va a wifi.go, non a lastErr
}

// CmdError è l'ultimo comando fallito (rifiutato dal display o senza `ack` in tempo).
type CmdError struct {
	Text string `json:"text"`
	At   int64  `json:"at"` // ms epoch
}

// DisplayStatus è un display in GET /api/status.
type DisplayStatus struct {
	Board           string         `json:"board"`
	Device          string         `json:"device"`
	Class           string         `json:"class"`
	FW              string         `json:"fw"`
	W               int            `json:"w"`
	H               int            `json:"h"`
	IP              string         `json:"ip"`
	Since           int64          `json:"since"`  // ms epoch del collegamento
	Remote          bool           `json:"remote"` // il firmware accetta set e full-refresh
	Settings        map[string]any `json:"settings"`
	SettingsPending map[string]any `json:"settingsPending"`
	Error           *CmdError      `json:"error"`
	// M5: abbinamento, autenticazione e permessi.
	Paired        bool           `json:"paired"`        // il servizio ha una K per questo display
	DisplayPaired *bool          `json:"displayPaired"` // il display dice di avere una K (null: firmware senza M5)
	Authed        bool           `json:"authed"`        // autenticato su questa connessione
	Pair          string         `json:"pair"`          // none (non abbinato) · ok (abbinato e autenticato) · redo (da rifare) · auth (in corso)
	PairReason    string         `json:"pairReason,omitempty"`
	Pairing       *PairingStatus `json:"pairing"` // abbinamento avviato su questa connessione (anche finito), null se nessuno
	Perms         bool           `json:"perms"`   // riceve le richieste di permesso (autenticato e permOn)
	// OTA (docs/OTA.md): alimentazione dall'ultimo `status` (batt/usb null = la scheda non lo sa;
	// status false = il firmware non manda `status`, quindi non conosce l'aggiornamento remoto),
	// aggiornamento in corso o ultimo, richieste di permesso aperte su questa scheda.
	Status    bool     `json:"status"`
	Batt      *int     `json:"batt"`
	USB       *bool    `json:"usb"`
	Cal       bool     `json:"cal"`
	OpenPerms int      `json:"openPerms"`
	OTA       *OtaView `json:"ota"`
	// Registro da remoto: sessione in corso o ultima (null se mai chiesto).
	Log *LogView `json:"log"`
	// Reti Wi-Fi salvate sulla scheda (ultima wifi-list) e operazione in corso o ultima.
	Wifi *WifiView `json:"wifi"`
	// Giornata di lavoro: stato riferito dalla scheda, comando in attesa e ultimo esito (workday.go).
	Workday *WorkdayView `json:"workday"`
	// Temi delle schermate: ultimo themes della scheda e operazione in corso o ultima (themes.go).
	Themes *ThemesView `json:"themes"`
	// Riempiti dalla API: si può aggiornare adesso e perché no; ultimo crash della scheda.
	OTACheck  *OTACheck   `json:"otaCheck,omitempty"`
	LastCrash *diag.Crash `json:"lastCrash"`
}

// OTACheck dice se l'aggiornamento senza cavo si può avviare adesso (lo ricontrolla la scheda).
type OTACheck struct {
	Can  bool   `json:"can"`
	Why  string `json:"why,omitempty"`  // perché no, in italiano
	Warn string `json:"warn,omitempty"` // si può, ma… (stessa versione, versione più vecchia)
}

func deliver(ctx context.Context, in chan<- inbound, m inbound) error {
	select {
	case in <- m:
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}

// receive applica settings e ack (chiamata da run, dopo hello).
func (h *Hub) receive(c *conn, m inbound) {
	switch m.t {
	case "settings":
		h.mu.Lock()
		prev := c.settings
		c.settings = m.settings
		c.info.Remote = m.remote
		h.queueOpenPerms(c) // permOn appena acceso: le richieste ancora aperte
		h.mu.Unlock()
		h.gameSetting(c, prev, m.settings)
		h.changed()
	case "ack":
		h.mu.Lock()
		cmd, ok := c.cmds[m.ack.ID]
		if !ok {
			h.mu.Unlock()
			h.ignore(c.info.ID, "ack per un comando sconosciuto o scaduto %q", m.ack.ID)
			return
		}
		h.finish(c, m.ack.ID, cmd)
		if cmd.kind == "workday" {
			h.workdayAcked(c, m.ack.ID, *m.ack.OK, m.ack.Error)
			h.mu.Unlock()
			h.changed()
			return
		}
		if cmd.wifi {
			h.wifiAcked(c, m.ack.ID, *m.ack.OK, m.ack.Error)
			h.mu.Unlock()
			h.changed()
			return
		}
		if !*m.ack.OK {
			why := m.ack.Error
			if why == "" {
				why = "senza motivo"
			}
			what := "l'impostazione"
			if cmd.kind == "full-refresh" {
				what = "il refresh completo"
			}
			c.lastErr = &CmdError{Text: fmt.Sprintf("Il display ha rifiutato %s: %s", what, why), At: h.Now().UnixMilli()}
		}
		h.mu.Unlock()
		if !*m.ack.OK {
			h.Log.Printf("ws: #%d ha rifiutato %s %s: %s", c.info.ID, cmd.kind, m.ack.ID, m.ack.Error)
		}
		h.changed()
	}
}

// finish chiude un comando: toglie il timer e i valori in attesa che sono ancora suoi. Con h.mu.
func (h *Hub) finish(c *conn, id string, cmd *command) {
	cmd.timer.Stop()
	delete(c.cmds, id)
	for _, k := range cmd.keys {
		if p, ok := c.pending[k]; ok && p.id == id {
			delete(c.pending, k)
		}
	}
}

func (h *Hub) expire(c *conn, id string) {
	h.mu.Lock()
	cmd, ok := c.cmds[id]
	if !ok {
		h.mu.Unlock()
		return
	}
	h.finish(c, id, cmd)
	if cmd.kind == "workday" {
		h.workdayExpired(c, id)
		h.mu.Unlock()
		h.changed()
		return
	}
	if cmd.wifi {
		h.wifiExpired(c, id)
		h.mu.Unlock()
		h.changed()
		return
	}
	what := "l'impostazione potrebbe non essere stata applicata"
	if cmd.kind == "full-refresh" {
		what = "il refresh completo potrebbe non essere partito"
	}
	c.lastErr = &CmdError{
		Text: fmt.Sprintf("Il display non ha confermato entro %s: %s", h.AckTimeout.Round(time.Second), what),
		At:   h.Now().UnixMilli(),
	}
	h.mu.Unlock()
	h.Log.Printf("ws: #%d nessun ack per %s %s entro %s", c.info.ID, cmd.kind, id, h.AckTimeout)
	h.changed()
}

// target trova la connessione più recente di board che ha fatto hello. Con h.mu.
func (h *Hub) target(board string) *conn {
	var best *conn
	for _, c := range h.conns {
		if c.info.Hello && c.info.Board == board && (best == nil || c.info.ID > best.info.ID) {
			best = c
		}
	}
	return best
}

// enqueue registra un comando e lo mette in coda per run. Con h.mu; restituisce l'id.
func (h *Hub) enqueue(c *conn, kind string, keys []string, build func(id string) any) (string, error) {
	h.nextCmd++
	id := fmt.Sprintf("c%d", h.nextCmd)
	select {
	case c.out <- build(id):
	default:
		return "", ErrBusy
	}
	c.cmds[id] = &command{kind: kind, keys: keys, timer: time.AfterFunc(h.AckTimeout, func() { h.expire(c, id) })}
	c.lastErr = nil // un nuovo comando accettato sostituisce l'errore precedente
	return id, nil
}

// SetSettings manda `set` al display e restituisce l'id del comando e tutte le impostazioni in attesa.
// Errori: *InvalidError (400), ErrNotConnected, ErrNoRemote, ErrBusy (409).
func (h *Hub) SetSettings(board string, raw map[string]json.RawMessage) (string, map[string]any, error) {
	vals, err := ValidateRemote(raw)
	if err != nil {
		return "", nil, err
	}
	h.mu.Lock()
	c := h.target(board)
	if c == nil {
		h.mu.Unlock()
		return "", nil, ErrNotConnected
	}
	if !c.authed {
		h.mu.Unlock()
		return "", nil, ErrNotAuthed
	}
	if !c.info.Remote {
		h.mu.Unlock()
		return "", nil, ErrNoRemote
	}
	if err := checkWorkHoursFor(c, vals); err != nil {
		h.mu.Unlock()
		return "", nil, err
	}
	keys := make([]string, 0, len(vals))
	for k := range vals {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	id, err := h.enqueue(c, "set", keys, func(id string) any {
		return SetMsg{T: "set", V: ProtocolVersion, ID: id, Settings: vals}
	})
	if err != nil {
		h.mu.Unlock()
		return "", nil, err
	}
	for k, v := range vals {
		c.pending[k] = pendingVal{value: v, id: id}
	}
	pend := pendingMap(c)
	h.mu.Unlock()
	h.Log.Printf("ws: #%d set %s %v", c.info.ID, id, vals)
	h.changed()
	return id, pend, nil
}

// FullRefresh chiede al display un refresh completo del pannello.
func (h *Hub) FullRefresh(board string) (string, error) {
	h.mu.Lock()
	c := h.target(board)
	if c == nil {
		h.mu.Unlock()
		return "", ErrNotConnected
	}
	if !c.authed {
		h.mu.Unlock()
		return "", ErrNotAuthed
	}
	if !c.info.Remote {
		h.mu.Unlock()
		return "", ErrNoRemote
	}
	id, err := h.enqueue(c, "full-refresh", nil, func(id string) any {
		return FullRefreshMsg{T: "full-refresh", V: ProtocolVersion, ID: id}
	})
	h.mu.Unlock()
	if err != nil {
		return "", err
	}
	h.Log.Printf("ws: #%d full-refresh %s", c.info.ID, id)
	h.changed()
	return id, nil
}

func pendingMap(c *conn) map[string]any {
	out := make(map[string]any, len(c.pending))
	for k, p := range c.pending {
		out[k] = p.value
	}
	return out
}

// Status restituisce i display che hanno fatto hello (uno per scheda, il collegamento più recente).
func (h *Hub) Status() []DisplayStatus {
	h.mu.Lock()
	newest := map[string]*conn{}
	for _, c := range h.conns {
		if !c.info.Hello {
			continue
		}
		if b := newest[c.info.Board]; b == nil || c.info.ID > b.info.ID {
			newest[c.info.Board] = c
		}
	}
	type row struct {
		id int
		st DisplayStatus
	}
	rows := make([]row, 0, len(newest))
	for _, c := range newest {
		st := DisplayStatus{
			Board: c.info.Board, Device: c.info.Device, Class: c.info.Class, FW: c.info.FW,
			W: c.info.W, H: c.info.H, IP: c.info.IP, Since: c.info.Since.UnixMilli(),
			Remote: c.info.Remote, SettingsPending: pendingMap(c),
			Paired: h.keys[c.info.Board] != nil, DisplayPaired: c.info.DisplayPaired, Authed: c.authed,
			Pairing: c.pair.status(), Perms: h.eligible(c),
		}
		st.Pair, st.PairReason = h.pairState(c)
		if p := c.power; p != nil {
			st.Status, st.Batt, st.USB, st.Cal = true, p.Batt, p.USB, p.Cal
		}
		for id := range h.perms {
			if c.sent[id] {
				st.OpenPerms++
			}
		}
		if h.otaJobs != nil {
			st.OTA = h.otaJobs[c.info.Board].view()
		}
		if bl := h.logs[c.info.Board]; bl != nil {
			st.Log = bl.sess.view(bl.file)
		}
		st.Wifi = h.wifiView(c.info.Board)
		st.Workday = workdayView(c)
		st.Themes = h.themesView(c)
		if c.settings != nil {
			st.Settings = make(map[string]any, len(c.settings))
			for k, v := range c.settings {
				st.Settings[k] = v
			}
		}
		if c.lastErr != nil {
			e := *c.lastErr
			st.Error = &e
		}
		rows = append(rows, row{c.info.ID, st})
	}
	h.mu.Unlock()
	sort.Slice(rows, func(i, j int) bool { return rows[i].id < rows[j].id })
	out := make([]DisplayStatus, len(rows))
	for i, r := range rows {
		out[i] = r.st
	}
	return out
}

// Subscribe avvisa (in modo coalescente) a ogni cambiamento dei display: collegamento,
// scollegamento, impostazioni, comandi in attesa, errori.
func (h *Hub) Subscribe() (<-chan struct{}, func()) {
	ch := make(chan struct{}, 1)
	h.mu.Lock()
	id := h.nextSub
	h.nextSub++
	h.subs[id] = ch
	h.mu.Unlock()
	return ch, func() {
		h.mu.Lock()
		delete(h.subs, id)
		h.mu.Unlock()
	}
}

func (h *Hub) changed() {
	h.mu.Lock()
	subs := make([]chan struct{}, 0, len(h.subs))
	for _, ch := range h.subs {
		subs = append(subs, ch)
	}
	h.mu.Unlock()
	for _, ch := range subs {
		select {
		case ch <- struct{}{}:
		default:
		}
	}
}
