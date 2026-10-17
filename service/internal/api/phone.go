package api

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"mime"
	"net/http"
	"strings"

	"calamaio/internal/phones"
)

// Porta dei telefoni (8767, spec app iPhone §3.1): prima il token, poi l'elenco chiuso, poi la stessa mux
// della webapp con un segno nel contesto. Il segno lo mette solo NewPhoneHandler: su 8765 non c'è mai.

type phoneKey struct{}

func phoneOf(r *http.Request) (phones.Phone, bool) {
	p, ok := r.Context().Value(phoneKey{}).(phones.Phone)
	return p, ok
}

func fromPhone(r *http.Request) bool {
	_, ok := phoneOf(r)
	return ok
}

// PhoneAuth: token e abbinamento dei telefoni (phones.Manager).
type PhoneAuth interface {
	Lookup(token string) (phones.Phone, bool)
	Touch(id string)
	Redeem(code, name, oldToken string) (string, phones.Phone, error)
	Track(ctx context.Context, id string) (context.Context, func())
}

// PhoneInfo: ciò che di un telefono può uscire dal PC (mai l'impronta del token).
type PhoneInfo struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Paired   int64  `json:"paired"`
	LastUsed int64  `json:"lastUsed"`
}

func infoOf(p phones.Phone) PhoneInfo {
	return PhoneInfo{ID: p.ID, Name: p.Name, Paired: p.Paired, LastUsed: p.LastUsed}
}

type PhoneOptions struct {
	Auth   PhoneAuth
	Whoami func(PhoneInfo) any // corpo di GET /app/whoami
	Next   http.Handler        // la mux della webapp (Server.Handler)
	Log    *log.Logger
}

// phoneRoutes: l'elenco chiuso. Una rotta nuova della webapp NON si raggiunge dal telefono finché non si
// aggiunge qui (e in TestPhoneAllowedRoutes). Le rotte dei Giochi (piano C) vanno aggiunte da quel piano.
var phoneRoutes = []string{
	"GET /app/whoami",
	"GET /api/status",
	"GET /api/events",
	"POST /api/displays/{board}/settings",
	"POST /api/displays/{board}/full-refresh",
	"POST /api/displays/{board}/workday",
	"POST /api/displays/{board}/theme",
	"GET /api/crashes",
	"GET /api/crashes/{id}",
	"POST /api/sessions/{id}/archive",
	"POST /api/sessions/{id}/unarchive",
	"POST /api/sessions/{id}/avatar",
	"GET /api/avatars",
	"POST /api/avatars",
	"GET /api/avatars/{id}",
	"POST /api/avatars/default",
	"GET /api/avatars/{id}/anchors",
	"POST /api/projects/avatar",
	"GET /api/game",
	"POST /api/game",
	"POST /api/game/wear",
	"POST /api/game/special",
	"GET /api/accessories/{id}",
	"POST /api/accessories",
	"GET /api/themes",
	"POST /api/themes",
	"POST /api/themes/check",
	"POST /api/themes/import",
	"GET /api/themes/{id}",
	"PUT /api/themes/{id}",
	"DELETE /api/themes/{id}",
	"GET /api/themes/{id}/history",
	"POST /api/themes/{id}/restore/{n}",
	"POST /api/themes/{id}/export",
	// pagine e file della webapp; Firmware no (Web Serial non c'è su iOS)
	"GET /{$}",
	"GET /index.html",
	"GET /avatars.html",
	"GET /game.html",
	"GET /device.html",
	"GET /editor.html",
	"GET /assets/",
}

// PhoneHiddenPages: pagine della webapp che dal telefono rispondono 404.
var PhoneHiddenPages = []string{"firmware.html"}

func NewPhoneHandler(o PhoneOptions) http.Handler {
	allow := http.NewServeMux()
	ok := http.HandlerFunc(func(http.ResponseWriter, *http.Request) {})
	for _, p := range phoneRoutes {
		allow.Handle(p, ok)
	}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		securityHeaders(w.Header())
		// /app/pair solo col percorso esatto (anche in forma grezza)
		if r.URL.Path == "/app/pair" && r.URL.RawPath == "" {
			phonePair(o, w, r)
			return
		}
		p, found := phones.Phone{}, false
		if tok, ok := bearer(r); ok {
			p, found = o.Auth.Lookup(tok)
		}
		if !found {
			w.Header().Set("WWW-Authenticate", "Bearer")
			writeJSON(w, http.StatusUnauthorized, errBody("non autorizzato"))
			return
		}
		// percorso non canonico (%2F, %2e...) o con segmenti «.»/«..»: la webapp lo risolverebbe altrove
		if r.URL.RawPath != "" || dotSegment(r.URL.Path) {
			writeJSON(w, http.StatusNotFound, errBody("non trovato"))
			return
		}
		// pattern vuoto = nessuna rotta dell'elenco (anche per metodo sbagliato o percorso da ripulire verso una
		// rotta vietata). Un percorso da ripulire verso una rotta ammessa passa e la mux vera risponde 301.
		if _, pat := allow.Handler(r); pat == "" {
			writeJSON(w, http.StatusNotFound, errBody("non trovato"))
			return
		}
		o.Auth.Touch(p.ID)
		ctx, done := o.Auth.Track(context.WithValue(r.Context(), phoneKey{}, p), p.ID)
		defer done()
		r = r.WithContext(ctx)
		r.Header.Del("Authorization")
		if r.URL.Path == "/app/whoami" {
			writeJSON(w, http.StatusOK, o.Whoami(infoOf(p)))
			return
		}
		o.Next.ServeHTTP(w, r)
	})
}

func dotSegment(p string) bool {
	for _, seg := range strings.Split(p, "/") {
		if seg == "." || seg == ".." {
			return true
		}
	}
	return false
}

// bearer: «Bearer <64 cifre esadecimali minuscole>», nient'altro.
func bearer(r *http.Request) (string, bool) {
	t, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
	return t, ok && phones.ValidToken(t)
}

// phonePair: POST /app/pair {code, name}. L'unica richiesta senza token; con «Authorization: Bearer <token di
// prima>» il telefono abbinato prima (stesso telefono, QR nuovo) si revoca.
func phonePair(o PhoneOptions, w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeJSON(w, http.StatusMethodNotAllowed, errBody("solo POST"))
		return
	}
	if mt, _, _ := mime.ParseMediaType(r.Header.Get("Content-Type")); mt != "application/json" {
		writeJSON(w, http.StatusUnsupportedMediaType, errBody("serve Content-Type: application/json"))
		return
	}
	var body struct {
		Code string `json:"code"`
		Name string `json:"name"`
	}
	dec := json.NewDecoder(io.LimitReader(r.Body, 4<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&body); err != nil {
		writeJSON(w, http.StatusBadRequest, errBody("JSON non valido"))
		return
	}
	if _, err := dec.Token(); err != io.EOF { // niente dati dopo il JSON
		writeJSON(w, http.StatusBadRequest, errBody("JSON non valido"))
		return
	}
	old, _ := bearer(r)
	tok, p, err := o.Auth.Redeem(body.Code, body.Name, old)
	var ce *phones.CodeError
	switch {
	case err == nil:
		o.Log.Printf("telefoni: abbinato «%s» (%s)", p.Name, p.ID)
		writeJSON(w, http.StatusOK, map[string]any{"token": tok, "id": p.ID, "name": p.Name})
	case errors.As(err, &ce):
		o.Log.Printf("telefoni: codice di abbinamento sbagliato (%d tentativi rimasti)", ce.Left)
		writeJSON(w, http.StatusForbidden, map[string]any{"error": "codice sbagliato", "left": ce.Left})
	case errors.Is(err, phones.ErrNoWindow):
		writeJSON(w, http.StatusConflict, errBody("nessun abbinamento aperto: aprilo dalla webapp sul PC"))
	default:
		o.Log.Printf("telefoni: abbinamento non salvato: %v", err)
		writeJSON(w, http.StatusInternalServerError, errBody("abbinamento non salvato sul PC"))
	}
}
