// Package api espone la API locale (solo 127.0.0.1) che ricevono hook e status line.
package api

import (
	"encoding/json"
	"io"
	"log"
	"net"
	"net/http"
	"sort"
	"strings"
	"sync"
	"time"

	"calamaio/internal/avatar"
	"calamaio/internal/backup"
	"calamaio/internal/capture"
	"calamaio/internal/diag"
	"calamaio/internal/display"
	"calamaio/internal/fake"
	"calamaio/internal/firmware"
	"calamaio/internal/gamesvc"
	"calamaio/internal/state"
	"calamaio/internal/theme"
)

// DisplayLister: i display collegati (display.Hub).
type DisplayLister interface {
	Displays() []display.Info
	Ignored() int64
}

// Forcer cambia a mano lo stato finto (fake.Generator).
type Forcer interface {
	Force(fake.ForceRequest) (state.Session, error)
}

// Snapshotter: lo stato attuale (state.Store).
type Snapshotter interface {
	Snapshot() state.Snapshot
}

// HookSink riceve i payload veri (sessions.Engine). nil in modalità --fake.
type HookSink interface {
	Hook(body []byte)
	StatusLine(body []byte)
	Debug() map[string]any
}

// Corpo massimo accettato: PostToolUse può portare l'output intero di un comando.
const maxBody = 16 << 20

type Server struct {
	Capture *capture.Writer // nil se la cattura è spenta
	Log     *log.Logger

	// Facoltativi (M3): se presenti finiscono in /debug.
	Displays DisplayLister
	State    Snapshotter
	Fake     Forcer         // nil se il servizio non è in modalità --fake
	Sink     HookSink       // macchina a stati dei dati veri (nil con --fake)
	Info     map[string]any // porta WebSocket, fuso, versione…

	// Webapp e /api (facoltativi: nil = rotta che risponde vuoto o 409).
	UI            http.Handler   // file statici (webui)
	Remote        RemoteDisplays // display.Hub
	Source        state.Source   // state.Store: stato e avvisi di cambiamento
	Firmware      *firmware.Package
	Backups       *backup.Store // nil = le rotte dei backup rispondono 404
	Service       ServiceInfo
	EventInterval time.Duration // SSE: al massimo un evento ogni 250 ms
	KeepAlive     time.Duration // SSE: commento ogni 15 s

	// M5 (facoltativi): abbinamento e permessi dal display.
	Pairing PairingAPI  // display.Hub
	Perms   Permissions // display.Hub
	Tracker PermTracker // sessions.Engine (nil con --fake)

	// Avatar (facoltativi): libreria e assegnazioni; OnAvatarsChanged avvisa display e webapp
	// (state.Store.Touch) dopo una modifica.
	Avatars          *avatar.Manager
	OnAvatarsChanged func()
	// Game (facoltativo): gioco della mascotte e libreria degli accessori (game.go). nil = 404.
	Game *gamesvc.Service
	// Temi delle schermate (archivio sul PC; nil = 404).
	Themes *theme.Store
	// ThemeSend (facoltativo): manda un tema dell'archivio a una scheda (display.Hub). nil = 409.
	ThemeSend ThemeSender
	// Phones (facoltativo): telefoni abbinati e abbinamento (server.appService). nil = 404.
	Phones PhonesControl
	// Autostart (M6): stato dell'avvio automatico al login per /api/status (nil = sconosciuto).
	Autostart func() any

	// OTA (facoltativi): aggiornamento remoto e registro delle schede (display.Hub), rapporti di crash,
	// archivio degli ELF delle build mandate, file del registro da remoto.
	OTA OTAControl
	// Reti Wi-Fi della scheda (display.Hub, facoltativo: nil = 409).
	Wifi WifiControl
	// Giornata di lavoro (display.Hub, facoltativo: nil = 409).
	Workday  WorkdayControl
	Crashes  *diag.CrashStore
	ElfDir   string
	LogFiles *diag.LogFiles

	mu      sync.Mutex
	started time.Time
	counts  map[string]int
	last    []lastEvent
}

type lastEvent struct {
	TS      time.Time `json:"ts"`
	Source  string    `json:"source"`
	Event   string    `json:"event"`
	Session string    `json:"session_id,omitempty"`
}

func New(cw *capture.Writer, lg *log.Logger) *Server {
	return &Server{Capture: cw, Log: lg, started: time.Now(), counts: map[string]int{}}
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("POST /hook", s.handle("hook"))
	mux.HandleFunc("POST /statusline", s.handle("statusline"))
	mux.HandleFunc("POST /hook/context", s.hookContext)
	mux.HandleFunc("GET /hook/context", postOnly)
	// Senza queste due, GET /hook finirebbe nella webapp: deve restare 405.
	mux.HandleFunc("GET /hook", postOnly)
	mux.HandleFunc("GET /statusline", postOnly)
	mux.HandleFunc("GET /debug", localOnly(s.debug))
	mux.HandleFunc("POST /debug/fake", localOnly(s.fake))

	mux.HandleFunc("GET /api/status", guard(s.apiStatus))
	mux.HandleFunc("GET /api/events", guard(s.apiEvents))
	mux.HandleFunc("POST /api/displays/{board}/settings", postGuard(s.apiSettings))
	mux.HandleFunc("POST /api/displays/{board}/full-refresh", postGuard(s.apiFullRefresh))
	mux.HandleFunc("POST /api/displays/{board}/pair/start", postGuard(s.apiPair("start")))
	mux.HandleFunc("POST /api/displays/{board}/pair/code", postGuard(s.apiPair("code")))
	mux.HandleFunc("POST /api/displays/{board}/pair/cancel", postGuard(s.apiPair("cancel")))
	mux.HandleFunc("POST /api/displays/{board}/unpair", postGuard(s.apiUnpair))
	mux.HandleFunc("POST /api/displays/{board}/ota", postGuard(s.apiOTAStart))
	mux.HandleFunc("POST /api/displays/{board}/ota/image", bodyGuard("application/octet-stream", s.apiOTAImage))
	mux.HandleFunc("POST /api/displays/{board}/ota/cancel", postGuard(s.apiOTACancel))
	mux.HandleFunc("POST /api/displays/{board}/wifi/add", postGuard(s.apiWifiAdd))
	mux.HandleFunc("POST /api/displays/{board}/wifi/forget", postGuard(s.apiWifiForget))
	mux.HandleFunc("POST /api/displays/{board}/workday", postGuard(s.apiWorkday))
	mux.HandleFunc("POST /api/displays/{board}/log/start", postGuard(s.apiLogStart))
	mux.HandleFunc("POST /api/displays/{board}/log/stop", postGuard(s.apiLogStop))
	mux.HandleFunc("GET /api/displays/{board}/log", guard(s.apiLogRead))
	mux.HandleFunc("GET /api/displays/{board}/log/file", guard(s.apiLogFile))
	mux.HandleFunc("GET /api/crashes", guard(s.apiCrashes))
	mux.HandleFunc("GET /api/crashes/{id}", guard(s.apiCrashFile))
	mux.HandleFunc("POST /api/sessions/{id}/archive", postGuard(s.apiArchive(true)))
	mux.HandleFunc("POST /api/sessions/{id}/unarchive", postGuard(s.apiArchive(false)))
	mux.HandleFunc("GET /api/avatars", guard(s.apiAvatars))
	mux.HandleFunc("POST /api/avatars", postGuard(s.apiAvatarAdd))
	mux.HandleFunc("GET /api/avatars/{id}", guard(s.apiAvatarFile))
	mux.HandleFunc("POST /api/avatars/default", postGuard(s.apiAvatarDefault))
	mux.HandleFunc("POST /api/sessions/{id}/avatar", postGuard(s.apiSessionAvatar))
	mux.HandleFunc("POST /api/projects/avatar", postGuard(s.apiProjectAvatar))
	mux.HandleFunc("GET /api/avatars/{id}/anchors", guard(s.apiAvatarAnchors))
	mux.HandleFunc("GET /api/game", guard(s.apiGame))
	mux.HandleFunc("POST /api/game", postGuard(s.apiGameSet))
	mux.HandleFunc("POST /api/game/wear", postGuard(s.apiGameWear))
	mux.HandleFunc("POST /api/game/special", postGuard(s.apiGameSpecial))
	mux.HandleFunc("GET /api/accessories/{id}", guard(s.apiAccessoryFile))
	mux.HandleFunc("POST /api/accessories", postGuard(s.apiAccessoryAdd))
	mux.HandleFunc("GET /api/firmware", guard(s.apiFirmware))
	mux.HandleFunc("GET /api/firmware/parts/{name}", guard(s.apiFirmwarePart))
	mux.HandleFunc("GET /api/backups", guard(s.apiBackups))
	mux.HandleFunc("POST /api/backups", bodyGuard("application/octet-stream", s.apiBackupCreate))
	mux.HandleFunc("GET /api/backups/{id}/data", guard(s.apiBackupData))
	mux.HandleFunc("DELETE /api/backups/{id}", deleteGuard(s.apiBackupDelete))
	mux.HandleFunc("GET /api/", guard(apiNotFound))
	mux.HandleFunc("GET /api/themes", guard(s.apiThemes))
	mux.HandleFunc("POST /api/themes", postGuard(s.apiThemeCreate(false)))
	mux.HandleFunc("POST /api/themes/check", postGuard(s.apiThemeCheck))
	mux.HandleFunc("POST /api/themes/import", postGuard(s.apiThemeCreate(true)))
	mux.HandleFunc("GET /api/themes/{id}", guard(s.apiThemeFile))
	mux.HandleFunc("PUT /api/themes/{id}", postGuard(s.apiThemePut))
	mux.HandleFunc("DELETE /api/themes/{id}", deleteGuard(s.apiThemeDelete))
	mux.HandleFunc("GET /api/themes/{id}/history", guard(s.apiThemeHistory))
	mux.HandleFunc("POST /api/themes/{id}/restore/{n}", postGuard(s.apiThemeRestore))
	mux.HandleFunc("POST /api/themes/{id}/export", postGuard(s.apiThemeExport))
	mux.HandleFunc("POST /api/displays/{board}/theme", postGuard(s.apiDisplayTheme))
	mux.HandleFunc("GET /api/phones", guard(s.apiPhones))
	mux.HandleFunc("POST /api/phones/pair", postGuard(s.apiPhonesPair))
	mux.HandleFunc("POST /api/phones/pair/cancel", postGuard(s.apiPhonesCancel))
	mux.HandleFunc("GET /api/phones/pair/qr.svg", guard(s.apiPhonesQR))
	mux.HandleFunc("DELETE /api/phones/{id}", deleteGuard(s.apiPhoneRevoke))
	mux.HandleFunc("POST /api/", postGuard(apiNotFound))
	mux.HandleFunc("DELETE /api/", deleteGuard(apiNotFound))
	mux.HandleFunc("PUT /api/", postGuard(apiNotFound))
	if s.UI != nil {
		mux.HandleFunc("GET /", guard(s.UI.ServeHTTP))
	}
	return mux
}

func (s *Server) handle(source string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		wait := source == "hook" && r.URL.Query().Get("wait") == "1"
		if wait {
			// La richiesta che può tenere aperta una domanda sul display: solo dal hook locale, mai da
			// una pagina web (un browser manda sempre Origin o Sec-Fetch-Site su una POST).
			if err := hookCaller(r); err != nil {
				s.Log.Printf("hook --wait rifiutato: %v", err)
				http.Error(w, err.Error(), http.StatusForbidden)
				return
			}
		}
		body, err := io.ReadAll(io.LimitReader(r.Body, maxBody))
		if err != nil {
			http.Error(w, "lettura fallita", http.StatusBadRequest)
			return
		}
		ev := capture.EventName(source, body)
		s.record(source, ev, sessionID(body))
		if s.Capture != nil {
			if err := s.Capture.Write(source, body); err != nil {
				s.Log.Printf("cattura: %v", err)
			}
		}
		if s.Sink != nil {
			if source == "statusline" {
				s.Sink.StatusLine(body)
			} else {
				s.Sink.Hook(body)
			}
		}
		if source == "hook" && s.Perms != nil && ev == "SessionEnd" {
			s.Perms.ClosePermissions(sessionID(body)) // perm-close «session»
		}
		if source == "hook" && ev == "SessionEnd" {
			s.sessionAvatarEnded(sessionID(body))
		}
		if wait {
			if out := s.permission(r.Context(), body); out != nil {
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(http.StatusOK)
				_, _ = w.Write(out)
				return
			}
		}
		// Nessuna decisione: corpo vuoto, Claude Code prosegue nel terminale.
		w.WriteHeader(http.StatusOK)
	}
}

func (s *Server) record(source, ev, sid string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.counts[ev]++
	s.last = append(s.last, lastEvent{TS: time.Now(), Source: source, Event: ev, Session: sid})
	if len(s.last) > 50 {
		s.last = s.last[len(s.last)-50:]
	}
}

func (s *Server) debug(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	keys := make([]string, 0, len(s.counts))
	for k := range s.counts {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	counts := make([]map[string]any, 0, len(keys))
	for _, k := range keys {
		counts = append(counts, map[string]any{"event": k, "n": s.counts[k]})
	}
	out := map[string]any{
		"started": s.started,
		"capture": s.Capture != nil,
		"counts":  counts,
		"last":    append([]lastEvent(nil), s.last...),
	}
	if s.Capture != nil {
		out["capture_dir"] = s.Capture.Dir()
	}
	s.mu.Unlock()
	for k, v := range s.Info {
		out[k] = v
	}
	out["fake"] = s.Fake != nil
	if s.Sink != nil {
		out["sessions"] = s.Sink.Debug()
	}
	if s.Displays != nil {
		out["displays"] = s.Displays.Displays()
		out["ws_ignored"] = s.Displays.Ignored()
	}
	if s.State != nil {
		out["state"] = s.State.Snapshot()
	}
	if s.Pairing != nil {
		out["paired"] = s.Pairing.PairedBoards()
	}
	if s.Perms != nil {
		out["permissions"] = s.Perms.Permissions()
	}
	if s.Crashes != nil {
		out["last_crash"] = s.Crashes.Last("")
	}
	if st := s.otaStatus(); st != nil {
		out["ota"] = st
	}
	w.Header().Set("Content-Type", "application/json")
	enc := json.NewEncoder(w)
	enc.SetIndent("", "  ")
	_ = enc.Encode(out)
}

func sessionID(body []byte) string {
	var h struct {
		ID string `json:"session_id"`
	}
	_ = json.Unmarshal(body, &h)
	return h.ID
}

// fake: POST /debug/fake {"session":"web-app","state":"permission","detail":"Bash: rm -rf build"}.
func (s *Server) fake(w http.ResponseWriter, r *http.Request) {
	if s.Fake == nil {
		http.Error(w, "stato finto spento: avvia il servizio con `calamaio serve --fake`", http.StatusConflict)
		return
	}
	// Solo JSON: un browser non può mandarlo senza preflight CORS, che qui non è gestito.
	if !strings.HasPrefix(r.Header.Get("Content-Type"), "application/json") {
		http.Error(w, "serve Content-Type: application/json", http.StatusUnsupportedMediaType)
		return
	}
	var req fake.ForceRequest
	dec := json.NewDecoder(io.LimitReader(r.Body, 64<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&req); err != nil {
		http.Error(w, "JSON non valido: "+err.Error(), http.StatusBadRequest)
		return
	}
	ss, err := s.Fake.Force(req)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	s.Log.Printf("debug: forzato %s → %s %q", ss.Name, req.State, ss.Detail)
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true, "session": ss})
}

// localOnly rifiuta le richieste con un Host che non sia locale (difesa dal DNS rebinding:
// /debug espone nomi di sessione e comandi).
func localOnly(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !loopbackHost(r) {
			http.Error(w, "solo da 127.0.0.1", http.StatusForbidden)
			return
		}
		next(w, r)
	}
}

func splitHostPort(hp string) (string, string, error) { return net.SplitHostPort(hp) }

// isLoopbackName: localhost o un indirizzo di loopback (127.0.0.0/8, ::1), anche tra [ ].
func isLoopbackName(host string) bool {
	host = strings.TrimSuffix(strings.TrimPrefix(host, "["), "]")
	if strings.EqualFold(host, "localhost") {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}
