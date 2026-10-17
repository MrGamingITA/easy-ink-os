package api

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"

	"calamaio/internal/avatar"
)

// Avatar delle sessioni (docs/AVATAR.md §6-7, docs/PROTOCOLLO.md «API locale»): libreria,
// caricamento, assegnazioni, predefinito. Stesse guardie delle altre rotte (Host, CSRF, JSON).

// AvatarStatus: la parte `avatars` di GET /api/status.
type AvatarStatus struct {
	Default string `json:"default"`
	Rev     string `json:"rev"` // cambia quando cambia la libreria o il predefinito: la webapp rilegge /api/avatars
}

// AvatarList: GET /api/avatars (stessa forma di `calamaio avatar list --json`, più qualche campo).
type AvatarList struct {
	Avatars  []avatar.Use     `json:"avatars"`
	Free     []string         `json:"free"`
	Default  string           `json:"default"`
	Current  *string          `json:"current"` // sessione considerata «corrente» (?session=)
	Problems []avatar.Problem `json:"problems"`
	UserDir  string           `json:"userDir"`
	Rev      string           `json:"rev"`
}

// maxAvatarUpload: si legge un po' oltre i 48 KB, così il validatore può dire «file JSON di N byte».
const maxAvatarUpload = avatar.MaxJSONBytes + 16<<10

// HookContext è il testo che il hook SessionStart sincrono passa a Claude (additionalContext)
// quando il progetto della sessione non ha un avatar.
const HookContext = "Calamaio: questo progetto non ha un avatar; usa la skill avatar per proporre all'utente un avatar libero o crearne uno."

func (s *Server) avatarsChanged() {
	if s.OnAvatarsChanged != nil {
		s.OnAvatarsChanged()
	}
}

func (s *Server) avatarSessions() []avatar.SessionInfo {
	var out []avatar.SessionInfo
	v, ok := s.Source.(Viewer)
	if !ok {
		return out
	}
	for _, ss := range v.View().Sessions {
		out = append(out, avatar.SessionInfo{ID: ss.ID, Name: ss.Name, Cwd: ss.Cwd, Archived: ss.Archived})
	}
	return out
}

// AvatarListFor costruisce l'elenco rispetto alla sessione current ("" = nessuna).
func (s *Server) AvatarListFor(current string) AvatarList {
	m := s.Avatars
	list, free, def := m.Usage(s.avatarSessions(), current)
	cat := m.Lib.Catalog()
	out := AvatarList{Avatars: list, Free: free, Default: def, Problems: cat.Problems, UserDir: m.Lib.UserDir, Rev: m.Rev()}
	if out.Problems == nil {
		out.Problems = []avatar.Problem{}
	}
	if current != "" {
		out.Current = &current
	}
	return out
}

func (s *Server) noAvatars(w http.ResponseWriter) bool {
	if s.Avatars == nil {
		writeJSON(w, http.StatusNotFound, errBody("avatar non disponibili in questo servizio"))
		return true
	}
	return false
}

// GET /api/avatars[?session=ID]
func (s *Server) apiAvatars(w http.ResponseWriter, r *http.Request) {
	if s.noAvatars(w) {
		return
	}
	writeJSON(w, http.StatusOK, s.AvatarListFor(r.URL.Query().Get("session")))
}

// GET /api/avatars/{id}: il file JSON dell'avatar (per disegnarlo), con il CRC del binario.
func (s *Server) apiAvatarFile(w http.ResponseWriter, r *http.Request) {
	if s.noAvatars(w) {
		return
	}
	e := s.Avatars.Lib.Get(r.PathValue("id"))
	if e == nil {
		writeJSON(w, http.StatusNotFound, errBody("avatar sconosciuto"))
		return
	}
	hd := w.Header()
	hd.Set("Content-Type", "application/json; charset=utf-8")
	hd.Set("Cache-Control", "no-cache")
	hd.Set("ETag", fmt.Sprintf(`"%08x"`, e.CRC))
	hd.Set("X-Calamaio-Crc", fmt.Sprintf("%08x", e.CRC))
	if r.Header.Get("If-None-Match") == fmt.Sprintf(`"%08x"`, e.CRC) {
		w.WriteHeader(http.StatusNotModified)
		return
	}
	_, _ = w.Write(e.JSON)
}

// POST /api/avatars[?replace=1]: corpo = il file avatar JSON. 201 {avatar, warnings, replaced} ·
// 400 {error, errors, warnings} · 409 {error} (id già presente senza replace, o di un predefinito).
func (s *Server) apiAvatarAdd(w http.ResponseWriter, r *http.Request) {
	if s.noAvatars(w) {
		return
	}
	raw, err := io.ReadAll(io.LimitReader(r.Body, maxAvatarUpload+1))
	if err != nil {
		writeJSON(w, http.StatusBadRequest, errBody("lettura fallita"))
		return
	}
	if len(raw) > maxAvatarUpload {
		writeJSON(w, http.StatusRequestEntityTooLarge, map[string]any{"error": fmt.Sprintf("file oltre %d byte", avatar.MaxJSONBytes),
			"errors": []string{fmt.Sprintf("file JSON di oltre %d byte: il massimo è %d", maxAvatarUpload, avatar.MaxJSONBytes)}, "warnings": []string{}})
		return
	}
	replace := r.URL.Query().Get("replace") == "1" || r.URL.Query().Get("replace") == "true"
	existed := false
	if res := avatar.ValidateNoSize(raw); res.Avatar != nil {
		existed = s.Avatars.Lib.Get(res.Avatar.ID) != nil
	}
	e, res, err := s.Avatars.Lib.Add(raw, replace)
	switch {
	case errors.Is(err, avatar.ErrInvalid):
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "avatar non valido", "errors": res.Errors, "warnings": res.Warnings})
		return
	case errors.Is(err, avatar.ErrExists), errors.Is(err, avatar.ErrReserved):
		writeJSON(w, http.StatusConflict, map[string]any{"error": err.Error(), "errors": []string{}, "warnings": res.Warnings})
		return
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	s.Log.Printf("avatar: %s «%s» aggiunto alla libreria dell'utente (%s, crc %08x)", e.ID, e.Name, e.File, e.CRC)
	s.avatarsChanged()
	warn := res.Warnings
	if warn == nil {
		warn = []string{}
	}
	writeJSON(w, http.StatusCreated, map[string]any{"avatar": useOf(s, e.ID), "warnings": warn, "replaced": existed && replace, "file": e.File})
}

func useOf(s *Server, id string) *avatar.Use {
	l := s.AvatarListFor("")
	for i := range l.Avatars {
		if l.Avatars[i].ID == id {
			return &l.Avatars[i]
		}
	}
	return nil
}

// avatarID legge {"id": "gatto"} (null o "" = togli l'assegnazione).
type avatarBody struct {
	ID  *string `json:"id"`
	Dir string  `json:"dir"`
}

func readAvatarBody(w http.ResponseWriter, r *http.Request) (avatarBody, bool) {
	var b avatarBody
	dec := json.NewDecoder(io.LimitReader(r.Body, 8<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&b); err != nil {
		writeJSON(w, http.StatusBadRequest, errBody("serve un oggetto JSON {id}"))
		return b, false
	}
	return b, true
}

func idOf(b avatarBody) string {
	if b.ID == nil {
		return ""
	}
	return strings.TrimSpace(*b.ID)
}

func avatarError(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, avatar.ErrUnknown), errors.Is(err, avatar.ErrBadTarget):
		writeJSON(w, http.StatusBadRequest, errBody(err.Error()))
	default:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	}
}

// POST /api/sessions/{id}/avatar {id}: avatar della sessione (id null o "" = togli).
// La sessione può non essere ancora nota al servizio (appena riavviato): si assegna lo stesso.
func (s *Server) apiSessionAvatar(w http.ResponseWriter, r *http.Request) {
	if s.noAvatars(w) {
		return
	}
	b, ok := readAvatarBody(w, r)
	if !ok {
		return
	}
	sid := r.PathValue("id")
	if err := s.Avatars.AssignSession(sid, idOf(b)); err != nil {
		avatarError(w, err)
		return
	}
	s.avatarsChanged()
	cwd, known := "", false
	for _, x := range s.avatarSessions() {
		if x.ID == sid {
			cwd, known = x.Cwd, true
		}
	}
	res := s.Avatars.Resolve(sid, cwd)
	writeJSON(w, http.StatusOK, map[string]any{"session": sid, "known": known, "avatar": entryID(res), "from": res.From})
}

// POST /api/projects/avatar {dir, id}: avatar del progetto della cartella dir (radice git se c'è).
func (s *Server) apiProjectAvatar(w http.ResponseWriter, r *http.Request) {
	if s.noAvatars(w) {
		return
	}
	b, ok := readAvatarBody(w, r)
	if !ok {
		return
	}
	proj, err := s.Avatars.AssignProject(b.Dir, idOf(b))
	if err != nil {
		avatarError(w, err)
		return
	}
	s.avatarsChanged()
	res := s.Avatars.Resolve("", proj)
	writeJSON(w, http.StatusOK, map[string]any{"dir": proj, "avatar": entryID(res), "from": res.From})
}

// POST /api/avatars/default {id}: avatar predefinito (null, "" o "goccia" = la Goccia).
func (s *Server) apiAvatarDefault(w http.ResponseWriter, r *http.Request) {
	if s.noAvatars(w) {
		return
	}
	b, ok := readAvatarBody(w, r)
	if !ok {
		return
	}
	if err := s.Avatars.SetDefault(idOf(b)); err != nil {
		avatarError(w, err)
		return
	}
	s.avatarsChanged()
	d := s.Avatars.DefaultEntry()
	id := ""
	if d != nil {
		id = d.ID
	}
	writeJSON(w, http.StatusOK, map[string]any{"default": id})
}

func entryID(r avatar.Resolution) string {
	if r.Entry == nil {
		return ""
	}
	return r.Entry.ID
}

// hookContext: POST /hook/context (lo usa `calamaio hook --context` nei hook sincroni SessionStart e
// UserPromptSubmit). Corpo = payload del hook. 200 {additionalContext} se il progetto della sessione
// non ha un avatar (SessionStart) o se c'è da proporre l'accessorio speciale del gioco
// (UserPromptSubmit), altrimenti 204. Come /hook?wait=1: solo dal hook locale, mai da un browser.
func (s *Server) hookContext(w http.ResponseWriter, r *http.Request) {
	if err := hookCaller(r); err != nil {
		http.Error(w, err.Error(), http.StatusForbidden)
		return
	}
	var p struct {
		Event     string `json:"hook_event_name"`
		SessionID string `json:"session_id"`
		Cwd       string `json:"cwd"`
		Source    string `json:"source"`
	}
	body, _ := io.ReadAll(io.LimitReader(r.Body, 1<<20))
	if txt := s.gameProposal(body); txt != "" {
		writeJSON(w, http.StatusOK, map[string]string{"additionalContext": txt})
		return
	}
	if s.Avatars == nil || json.Unmarshal(body, &p) != nil || p.Event != "SessionStart" || p.Cwd == "" {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	// Solo all'apertura (startup), alla ripresa e al fork: dopo /clear e /compact non si chiede di nuovo.
	switch p.Source {
	case "", "startup", "resume", "fork":
	default:
		w.WriteHeader(http.StatusNoContent)
		return
	}
	if s.Avatars.HasAssignment(p.SessionID, p.Cwd) {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	proj := s.Avatars.ProjectDir(p.Cwd)
	text := HookContext + fmt.Sprintf(" Cartella del progetto: %s (assegna con `calamaio avatar assign <id> --project %q`).", proj, proj)
	writeJSON(w, http.StatusOK, map[string]string{"additionalContext": text})
}

// sessionAvatarEnded: SessionEnd toglie l'assegnazione per sessione (docs/AVATAR.md §6).
func (s *Server) sessionAvatarEnded(sid string) {
	if s.Avatars != nil && s.Avatars.ForgetSession(sid) {
		s.avatarsChanged()
	}
}

// avatarStatus per GET /api/status (nil senza libreria).
func (s *Server) avatarStatus() *AvatarStatus {
	if s.Avatars == nil {
		return nil
	}
	st := &AvatarStatus{Rev: s.Avatars.Rev()}
	if d := s.Avatars.DefaultEntry(); d != nil {
		st.Default = d.ID
	}
	return st
}
