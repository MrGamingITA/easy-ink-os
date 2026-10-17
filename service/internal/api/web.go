package api

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"net/http"
	"net/url"
	"strings"
	"time"

	"calamaio/internal/diag"
	"calamaio/internal/display"
	"calamaio/internal/firmware"
	"calamaio/internal/state"
)

// Webapp e API per la webapp (/api/*), solo da 127.0.0.1. Forme in docs/PROTOCOLLO.md, «API locale».

// RemoteDisplays: display collegati con impostazioni remote (display.Hub).
type RemoteDisplays interface {
	Status() []display.DisplayStatus
	SetSettings(board string, raw map[string]json.RawMessage) (string, map[string]any, error)
	FullRefresh(board string) (string, error)
	Subscribe() (<-chan struct{}, func())
}

// ServiceInfo è la parte `service` di GET /api/status.
type ServiceInfo struct {
	Version string `json:"version"`
	Host    string `json:"host"`
	TZ      string `json:"tz"`
	WSPort  int    `json:"wsPort"`
}

// CSP: solo file dello stesso servizio. Niente script, stili o attributi style inline; le
// immagini data: servono ai canvas (Goccia). frame-ancestors 'none': la UI non si incornicia.
const csp = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; " +
	"font-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; " +
	"frame-ancestors 'none'"

func securityHeaders(h http.Header) {
	h.Set("Content-Security-Policy", csp)
	h.Set("X-Content-Type-Options", "nosniff")
	h.Set("X-Frame-Options", "DENY")
	h.Set("Referrer-Policy", "no-referrer")
	h.Set("Cross-Origin-Opener-Policy", "same-origin")
	h.Set("Cross-Origin-Resource-Policy", "same-origin")
	h.Set("Permissions-Policy", "serial=(self), usb=(), camera=(), microphone=(), geolocation=()")
}

// loopbackHost: l'Host della richiesta è 127.0.0.1, ::1 o localhost (difesa dal DNS rebinding).
func loopbackHost(r *http.Request) bool {
	host := r.Host
	if h, _, err := splitHostPort(host); err == nil {
		host = h
	}
	return isLoopbackName(host)
}

// guard: Host locale e header di sicurezza su ogni risposta della webapp e di /api. Dalla porta dei telefoni
// l'Host è l'indirizzo del PC: lì protegge il token, e il segno nel contesto c'è solo dopo il token.
func guard(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		securityHeaders(w.Header())
		if !loopbackHost(r) && !fromPhone(r) {
			http.Error(w, "solo da 127.0.0.1", http.StatusForbidden)
			return
		}
		next(w, r)
	}
}

// sameOrigin controlla Origin e Sec-Fetch-Site contro il CSRF da altre pagine aperte nel browser.
// browser = la richiesta ha dimostrato di venire dalla webapp stessa.
func sameOrigin(r *http.Request) (browser bool, err error) {
	if fromPhone(r) {
		return true, nil // richiesta col token di un telefono abbinato (porta 8767): vale come stessa origine
	}
	if sfs := r.Header.Get("Sec-Fetch-Site"); sfs != "" {
		if sfs != "same-origin" {
			return false, fmt.Errorf("richiesta da un altro sito (Sec-Fetch-Site: %s)", sfs)
		}
		browser = true
	}
	if o := r.Header.Get("Origin"); o != "" {
		u, perr := url.Parse(o)
		if perr != nil || u.Scheme != "http" || u.Host == "" || !strings.EqualFold(u.Host, r.Host) {
			return false, fmt.Errorf("origine %q non ammessa", o)
		}
		browser = true
	}
	return browser, nil
}

// postGuard: per le POST di /api. Solo application/json (un altro sito non può mandarlo senza
// preflight CORS, che qui non viene mai concesso) e stessa origine. Unica eccezione: corpo vuoto
// senza Content-Type, ammesso solo se il browser dimostra la stessa origine (fetch senza body).
func postGuard(next http.HandlerFunc) http.HandlerFunc { return bodyGuard("application/json", next) }

// bodyGuard: come postGuard, con il Content-Type richiesto (application/octet-stream per i backup:
// anche quello non è «semplice» per CORS, quindi da un altro sito serve un preflight mai concesso).
func bodyGuard(ctype string, next http.HandlerFunc) http.HandlerFunc {
	return guard(func(w http.ResponseWriter, r *http.Request) {
		browser, err := sameOrigin(r)
		if err != nil {
			writeJSON(w, http.StatusForbidden, errBody(err.Error()))
			return
		}
		ct := r.Header.Get("Content-Type")
		mt, _, _ := mime.ParseMediaType(ct)
		switch {
		case mt == ctype:
		case ctype == "application/json" && ct == "" && r.ContentLength == 0 && browser:
		default:
			writeJSON(w, http.StatusUnsupportedMediaType, errBody("serve Content-Type: "+ctype))
			return
		}
		next(w, r)
	})
}

// deleteGuard: per le DELETE di /api. Niente corpo e stessa origine dimostrata dal browser
// (Origin o Sec-Fetch-Site): una DELETE da un altro sito richiede comunque un preflight.
func deleteGuard(next http.HandlerFunc) http.HandlerFunc {
	return guard(func(w http.ResponseWriter, r *http.Request) {
		browser, err := sameOrigin(r)
		if err != nil {
			writeJSON(w, http.StatusForbidden, errBody(err.Error()))
			return
		}
		if !browser {
			writeJSON(w, http.StatusForbidden, errBody("serve la stessa origine (Origin o Sec-Fetch-Site)"))
			return
		}
		if r.ContentLength != 0 {
			writeJSON(w, http.StatusBadRequest, errBody("DELETE senza corpo"))
			return
		}
		next(w, r)
	})
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func errBody(msg string) map[string]string { return map[string]string{"error": msg} }

type statusBody struct {
	Service  ServiceInfo             `json:"service"`
	Displays []display.DisplayStatus `json:"displays"`
	// State: il messaggio `state` senza t e v, ma con tutte le sessioni, archiviate comprese
	// (campo `archived` in ogni sessione).
	State state.View `json:"state"`
	// M5: schede abbinate (anche non collegate) e richieste di permesso aperte.
	Paired      []string             `json:"paired"`
	Permissions []display.PermStatus `json:"permissions"`
	// Avatar: predefinito e impronta della libreria (assente se il servizio non ha avatar).
	Avatars *AvatarStatus `json:"avatars,omitempty"`
	// Autostart (M6): avvio automatico al login (autostart.Status), null se il servizio non lo sa.
	Autostart any `json:"autostart"`
	// OTA: chiave di firma (null se il servizio non fa aggiornamenti remoti) e ultimo crash di una
	// scheda qualunque (null se nessuno).
	OTA       *OTAStatus  `json:"ota"`
	LastCrash *diag.Crash `json:"lastCrash"`
}

// Viewer: lo stato per la webapp con le archiviate (state.Store).
type Viewer interface {
	View() state.View
}

// viewOf costruisce la vista da un messaggio `state` (sorgenti senza archiviazione).
func viewOf(m state.Message) state.View {
	v := state.View{Time: m.Time, Sessions: make([]state.SessionView, 0, len(m.Sessions)), Limits: m.Limits,
		Today: m.Today, Log: m.Log, Services: m.Services, PRs: m.PRs, Agenda: m.Agenda}
	for _, ss := range m.Sessions {
		v.Sessions = append(v.Sessions, state.SessionView{Session: ss})
	}
	return v
}

func (s *Server) status() statusBody {
	b := statusBody{Service: s.Service, Displays: []display.DisplayStatus{}, Paired: []string{}, Permissions: []display.PermStatus{}}
	if s.Remote != nil {
		b.Displays = s.Remote.Status()
	}
	b.OTA = s.otaStatus()
	s.fillOTA(b.Displays, b.OTA)
	if s.Crashes != nil {
		b.LastCrash = s.Crashes.Last("")
	}
	if s.Pairing != nil {
		b.Paired = s.Pairing.PairedBoards()
	}
	if s.Perms != nil {
		b.Permissions = s.Perms.Permissions()
	}
	b.Avatars = s.avatarStatus()
	if s.Autostart != nil {
		b.Autostart = s.Autostart()
	}
	switch v := s.Source.(type) {
	case Viewer:
		b.State = v.View()
	case nil:
		m := state.Message{Time: time.Now().UnixMilli(), Snapshot: state.Empty().Normalize()}
		b.State = viewOf(m)
	default:
		b.State = viewOf(s.Source.Message())
	}
	return b
}

func (s *Server) apiStatus(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, s.status())
}

// apiEvents: SSE, evento `status` all'apertura e a ogni cambiamento (al massimo uno ogni
// EventInterval), commento di keepalive ogni KeepAlive.
func (s *Server) apiEvents(w http.ResponseWriter, r *http.Request) {
	rc := http.NewResponseController(w)
	hd := w.Header()
	hd.Set("Content-Type", "text/event-stream; charset=utf-8")
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Accel-Buffering", "no")
	w.WriteHeader(http.StatusOK)

	var a, b <-chan struct{}
	if s.Source != nil {
		ch, unsub := s.Source.Subscribe()
		defer unsub()
		a = ch
	}
	if s.Remote != nil {
		ch, unsub := s.Remote.Subscribe()
		defer unsub()
		b = ch
	}
	interval, keep := s.EventInterval, s.KeepAlive
	if interval <= 0 {
		interval = 250 * time.Millisecond
	}
	if keep <= 0 {
		keep = 15 * time.Second
	}

	var last time.Time
	send := func() error {
		data, err := json.Marshal(s.status())
		if err != nil {
			return err
		}
		if _, err := fmt.Fprintf(w, "event: status\ndata: %s\n\n", data); err != nil {
			return err
		}
		last = time.Now()
		return rc.Flush()
	}
	if _, err := io.WriteString(w, "retry: 2000\n\n"); err != nil {
		return
	}
	if send() != nil {
		return
	}
	ka := time.NewTicker(keep)
	defer ka.Stop()
	var delay *time.Timer
	var delayC <-chan time.Time
	defer func() {
		if delay != nil {
			delay.Stop()
		}
	}()
	changed := func() error {
		if delayC != nil {
			return nil // un invio è già in programma: questo cambiamento ci finisce dentro
		}
		if wait := interval - time.Since(last); wait > 0 {
			delay = time.NewTimer(wait)
			delayC = delay.C
			return nil
		}
		return send()
	}
	for {
		var err error
		select {
		case <-r.Context().Done():
			return
		case <-a:
			err = changed()
		case <-b:
			err = changed()
		case <-delayC:
			delayC = nil
			err = send()
		case <-ka.C:
			if _, err = io.WriteString(w, ": keepalive\n\n"); err == nil {
				err = rc.Flush()
			}
		}
		if err != nil {
			return
		}
	}
}

// commandError traduce gli errori dei comandi in codici HTTP.
func commandError(w http.ResponseWriter, err error) {
	var inv *display.InvalidError
	switch {
	case errors.As(err, &inv):
		writeJSON(w, http.StatusBadRequest, errBody(inv.Msg))
	case errors.Is(err, display.ErrWrongCode):
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": err.Error(), "aborted": true})
	case errors.Is(err, display.ErrNotConnected), errors.Is(err, display.ErrNoRemote), errors.Is(err, display.ErrBusy),
		errors.Is(err, display.ErrNotAuthed), errors.Is(err, display.ErrPairBusy), errors.Is(err, display.ErrNoPairing),
		errors.Is(err, display.ErrPairNotReady), errors.Is(err, display.ErrWifiBusy), errors.Is(err, display.ErrWifiOTA),
		errors.Is(err, display.ErrNoWorkday), errors.Is(err, display.ErrWorkdayBusy):
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
	case errors.Is(err, display.ErrWifiUnknown):
		writeJSON(w, http.StatusNotFound, errBody(err.Error()))
	case errors.Is(err, display.ErrStopping):
		writeJSON(w, http.StatusServiceUnavailable, errBody(err.Error()))
	default:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	}
}

func (s *Server) apiSettings(w http.ResponseWriter, r *http.Request) {
	if s.Remote == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	var raw map[string]json.RawMessage
	dec := json.NewDecoder(io.LimitReader(r.Body, 4<<10))
	if err := dec.Decode(&raw); err != nil || raw == nil {
		writeJSON(w, http.StatusBadRequest, errBody("serve un oggetto JSON {chiave: valore}"))
		return
	}
	if dec.More() {
		writeJSON(w, http.StatusBadRequest, errBody("dati in più dopo l'oggetto JSON"))
		return
	}
	id, pending, err := s.Remote.SetSettings(r.PathValue("board"), raw)
	if err != nil {
		commandError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"id": id, "pending": pending})
}

func (s *Server) apiFullRefresh(w http.ResponseWriter, r *http.Request) {
	if s.Remote == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	id, err := s.Remote.FullRefresh(r.PathValue("board"))
	if err != nil {
		commandError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"id": id})
}

// apiArchive: POST /api/sessions/{id}/archive e …/unarchive → 202 | 404 | 409.
func (s *Server) apiArchive(archive bool) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		a, ok := s.Source.(state.Archiver)
		if !ok {
			writeJSON(w, http.StatusConflict, errBody("archiviazione non disponibile"))
			return
		}
		id := r.PathValue("id")
		var err error
		if archive {
			err = a.Archive(id)
		} else {
			err = a.Unarchive(id)
		}
		switch {
		case errors.Is(err, state.ErrNoSession):
			writeJSON(w, http.StatusNotFound, errBody("sessione sconosciuta"))
		case errors.Is(err, state.ErrNotArchivable):
			writeJSON(w, http.StatusConflict, errBody("Si archiviano solo le sessioni finite, in pausa o in errore"))
		case err != nil:
			writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		default:
			writeJSON(w, http.StatusAccepted, map[string]any{"id": id, "archived": archive})
		}
	}
}

func (s *Server) apiFirmware(w http.ResponseWriter, _ *http.Request) {
	if s.Firmware == nil {
		writeJSON(w, http.StatusOK, firmware.Info{Parts: []firmware.Part{}, Problem: "pacchetto firmware non configurato"})
		return
	}
	writeJSON(w, http.StatusOK, s.Firmware.Read())
}

func (s *Server) apiFirmwarePart(w http.ResponseWriter, r *http.Request) {
	if s.Firmware == nil {
		writeJSON(w, http.StatusNotFound, errBody("pacchetto firmware non configurato"))
		return
	}
	name := r.PathValue("name")
	f, part, err := s.Firmware.Open(name)
	if err != nil {
		writeJSON(w, http.StatusNotFound, errBody("parte "+name+" non trovata"))
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	hd := w.Header()
	hd.Set("Content-Type", "application/octet-stream")
	hd.Set("Content-Disposition", `attachment; filename="`+part.Name+`"`)
	hd.Set("Cache-Control", "no-store")
	hd.Set("X-Calamaio-Sha256", part.SHA256)
	http.ServeContent(w, r, part.Name, st.ModTime(), f)
}

func apiNotFound(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusNotFound, errBody("API sconosciuta: "+r.Method+" "+r.URL.Path))
}

func postOnly(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Allow", "POST")
	http.Error(w, "metodo non ammesso", http.StatusMethodNotAllowed)
}
