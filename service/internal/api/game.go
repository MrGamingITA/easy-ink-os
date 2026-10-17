package api

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"

	"calamaio/internal/accessory"
	"calamaio/internal/gamesvc"
)

// Gioco della mascotte e accessori per la webapp e per `calamaio game|accessory` (docs/PROTOCOLLO.md,
// «API locale»). Tutte solo da 127.0.0.1, POST con application/json e stessa origine.

// maxAccessoryUpload: un file accessorio (il più grande del catalogo è ~1,5 KB; i ritocchi con pixel
// lo allungano, il binario resta ≤ 1 KB).
const maxAccessoryUpload = 256 << 10

func (s *Server) noGame(w http.ResponseWriter) bool {
	if s.Game == nil {
		writeJSON(w, http.StatusNotFound, errBody("gioco non disponibile"))
		return true
	}
	return false
}

// GET /api/game[?days=N]: tutto il gioco (gamesvc.Status).
func (s *Server) apiGame(w http.ResponseWriter, r *http.Request) {
	if s.noGame(w) {
		return
	}
	days, _ := strconv.Atoi(r.URL.Query().Get("days"))
	if days < 0 || days > gamesvc.HistoryDays {
		days = 0
	}
	writeJSON(w, http.StatusOK, s.Game.Status(days))
}

// decodeStrict legge un corpo JSON piccolo senza campi sconosciuti.
func decodeStrict(r *http.Request, v any) error {
	dec := json.NewDecoder(io.LimitReader(r.Body, 64<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(v); err != nil {
		return fmt.Errorf("JSON non valido: %v", err)
	}
	return nil
}

// POST /api/game {"on": bool}: accende o spegne il gioco. 200 = lo stato.
func (s *Server) apiGameSet(w http.ResponseWriter, r *http.Request) {
	if s.noGame(w) {
		return
	}
	var req struct {
		On *bool `json:"on"`
	}
	if err := decodeStrict(r, &req); err != nil || req.On == nil {
		writeJSON(w, http.StatusBadRequest, errBody(`serve {"on": true} o {"on": false}`))
		return
	}
	s.Game.SetOn(*req.On)
	writeJSON(w, http.StatusOK, s.Game.Status(0))
}

// POST /api/game/wear {"id": "corona"} (null o "" = nessuno). 409 se bloccato, 404 se sconosciuto.
func (s *Server) apiGameWear(w http.ResponseWriter, r *http.Request) {
	if s.noGame(w) {
		return
	}
	var req struct {
		ID *string `json:"id"`
	}
	if err := decodeStrict(r, &req); err != nil {
		writeJSON(w, http.StatusBadRequest, errBody(`serve {"id": "<accessorio>"} oppure {"id": null}`))
		return
	}
	id := ""
	if req.ID != nil {
		id = *req.ID
	}
	switch err := s.Game.Wear(id); {
	case errors.Is(err, gamesvc.ErrUnknown):
		writeJSON(w, http.StatusNotFound, errBody(err.Error()))
	case errors.Is(err, gamesvc.ErrLocked):
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	default:
		writeJSON(w, http.StatusOK, s.Game.Status(0))
	}
}

// POST /api/game/special {"answer": "later" | "decline"}: risposta alla proposta dell'accessorio
// speciale. 409 se non c'è una proposta aperta.
func (s *Server) apiGameSpecial(w http.ResponseWriter, r *http.Request) {
	if s.noGame(w) {
		return
	}
	var req struct {
		Answer string `json:"answer"`
	}
	if err := decodeStrict(r, &req); err != nil || (req.Answer != gamesvc.AnswerLater && req.Answer != gamesvc.AnswerDecline) {
		writeJSON(w, http.StatusBadRequest, errBody(`serve {"answer": "later"} o {"answer": "decline"}`))
		return
	}
	switch err := s.Game.SpecialAnswer(req.Answer); {
	case errors.Is(err, gamesvc.ErrNoSpecial):
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
	default:
		writeJSON(w, http.StatusOK, s.Game.Status(0))
	}
}

// GET /api/accessories/{id}: il file JSON dell'accessorio (anteprima della webapp).
func (s *Server) apiAccessoryFile(w http.ResponseWriter, r *http.Request) {
	if s.noGame(w) {
		return
	}
	e := s.Game.Lib.Get(r.PathValue("id"))
	if e == nil {
		writeJSON(w, http.StatusNotFound, errBody("accessorio sconosciuto"))
		return
	}
	tag := fmt.Sprintf(`"%08x"`, e.CRC)
	hd := w.Header()
	hd.Set("Content-Type", "application/json; charset=utf-8")
	hd.Set("Cache-Control", "no-cache")
	hd.Set("ETag", tag)
	if r.Header.Get("If-None-Match") == tag {
		w.WriteHeader(http.StatusNotModified)
		return
	}
	_, _ = w.Write(e.JSON)
}

// POST /api/accessories[?replace=1][&special=1]: corpo = il file accessorio JSON. Lo valida, lo salva
// nella cartella dell'utente; con special (accessorio speciale creato da Claude, `special: true` nel
// file) lo sblocca, chiude la proposta e lo fa indossare. 201 {accessory, warnings, special} ·
// 400 {error, errors, warnings} · 409 {error}.
func (s *Server) apiAccessoryAdd(w http.ResponseWriter, r *http.Request) {
	if s.noGame(w) {
		return
	}
	raw, err := io.ReadAll(io.LimitReader(r.Body, maxAccessoryUpload+1))
	if err != nil || len(raw) > maxAccessoryUpload {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "file illeggibile o troppo grande", "errors": []string{}, "warnings": []string{}})
		return
	}
	q := r.URL.Query()
	replace := q.Get("replace") == "1" || q.Get("replace") == "true"
	special := q.Get("special") == "1" || q.Get("special") == "true"
	if res := accessory.Validate(raw); special && res.OK() && !res.Accessory.Special {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": `un accessorio speciale deve avere "special": true nel file`,
			"errors": []string{`special: deve essere true per --special`}, "warnings": res.Warnings})
		return
	}
	e, res, err := s.Game.Lib.Add(raw, replace)
	warn := res.Warnings
	if warn == nil {
		warn = []string{}
	}
	switch {
	case errors.Is(err, accessory.ErrInvalid):
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "accessorio non valido", "errors": res.Errors, "warnings": warn})
		return
	case errors.Is(err, accessory.ErrExists), errors.Is(err, accessory.ErrReserved):
		writeJSON(w, http.StatusConflict, map[string]any{"error": err.Error(), "errors": []string{}, "warnings": warn})
		return
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
		return
	}
	s.Log.Printf("accessori: %s «%s» aggiunto (%s, crc %08x, %d byte)", e.ID, e.Name, e.File, e.CRC, len(e.Bin))
	if special {
		if err := s.Game.AddSpecial(e.ID); err != nil {
			writeJSON(w, http.StatusInternalServerError, errBody(err.Error()))
			return
		}
	} else {
		s.Game.LibraryChanged()
	}
	var view *gamesvc.AccessoryView
	for _, a := range s.Game.Status(1).Accessories {
		if a.ID == e.ID {
			a := a
			view = &a
		}
	}
	writeJSON(w, http.StatusCreated, map[string]any{"accessory": view, "warnings": warn, "special": special, "file": e.File})
}

// GET /api/avatars/{id}/anchors: le ancore dell'avatar per l'anteprima degli accessori; anchors null
// se non ne ha (niente accessori su di lui). 404 solo per un avatar sconosciuto.
func (s *Server) apiAvatarAnchors(w http.ResponseWriter, r *http.Request) {
	if s.noAvatars(w) {
		return
	}
	e := s.Avatars.Lib.Get(r.PathValue("id"))
	if e == nil {
		writeJSON(w, http.StatusNotFound, errBody("avatar sconosciuto"))
		return
	}
	if e.Avatar.Anchors == nil {
		writeJSON(w, http.StatusOK, map[string]any{"avatar": e.ID, "avatarCrc": e.CRC, "from": "", "anchors": nil})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"avatar": e.ID, "avatarCrc": e.CRC, "from": e.AnchorsFrom, "anchors": e.Avatar.Anchors})
}

// gameProposal: contesto per Claude al UserPromptSubmit (proposta dell'accessorio speciale), "" se
// non c'è niente da proporre adesso o il gioco è spento.
func (s *Server) gameProposal(body []byte) string {
	if s.Game == nil {
		return ""
	}
	var p struct {
		Event string `json:"hook_event_name"`
	}
	if json.Unmarshal(bytes.TrimSpace(body), &p) != nil || p.Event != "UserPromptSubmit" {
		return ""
	}
	return s.Game.ProposalContext()
}
