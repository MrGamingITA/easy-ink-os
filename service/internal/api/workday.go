package api

import (
	"encoding/json"
	"io"
	"net/http"

	"calamaio/internal/display"
)

// Giornata di lavoro dalla webapp e da `calamaio workday` (docs/PROTOCOLLO.md «Giornata di lavoro»).
// Gli orari passano da POST /api/displays/{board}/settings (workStart, workEnd); qui solo start e stop.

// WorkdayControl: avvia e ferma la giornata sulla scheda (display.Hub).
type WorkdayControl interface {
	Workday(board, action string) (string, *display.WorkdayPending, error)
}

// apiWorkday: POST /api/displays/{board}/workday {action: "start"|"stop"} → 202 {id, pending} · 400 ·
// 409 (non collegata, non autenticata, firmware senza giornata, comando già in attesa) · 503.
func (s *Server) apiWorkday(w http.ResponseWriter, r *http.Request) {
	if s.Workday == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	var body struct {
		Action *string `json:"action"`
	}
	dec := json.NewDecoder(io.LimitReader(r.Body, 1<<10))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&body); err != nil || body.Action == nil {
		writeJSON(w, http.StatusBadRequest, errBody(`serve un oggetto JSON {"action": "start" | "stop"}`))
		return
	}
	if dec.More() {
		writeJSON(w, http.StatusBadRequest, errBody("dati in più dopo l'oggetto JSON"))
		return
	}
	id, pending, err := s.Workday.Workday(r.PathValue("board"), *body.Action)
	if err != nil {
		commandError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"id": id, "pending": pending})
}
