package api

import (
	"errors"
	"net/http"

	"calamaio/internal/phones"
)

// Telefoni abbinati e finestra di abbinamento, solo dalla webapp sul PC (porta 8765): dalla porta dei telefoni
// queste rotte non sono nell'elenco (spec §3.1, «abbinamento di altri telefoni»).

// PhonesControl: server.appService.
type PhonesControl interface {
	PhonesView() PhonesView
	OpenPairing() (PhonesView, error)
	CancelPairing() PhonesView
	RevokePhone(id string) error
	PairingQR() ([]byte, bool) // SVG del link della finestra aperta
}

type PhoneView struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Paired   int64  `json:"paired"`
	LastUsed int64  `json:"lastUsed"`
}

// PairingView: la finestra aperta. Link = contenuto del QR (con il codice monouso).
type PairingView struct {
	Link    string `json:"link"`
	Expires int64  `json:"expires"`
}

type AppView struct {
	Port        int      `json:"port"`
	Listening   bool     `json:"listening"`
	Fingerprint string   `json:"fingerprint"`
	Hosts       []string `json:"hosts"`
	Error       string   `json:"error,omitempty"`
}

type PhonesView struct {
	Phones  []PhoneView  `json:"phones"`
	Pairing *PairingView `json:"pairing"`
	App     AppView      `json:"app"`
}

func (s *Server) phonesOff(w http.ResponseWriter, r *http.Request) bool {
	if s.Phones == nil || fromPhone(r) { // dal telefono mai, anche se l'elenco chiuso cambiasse
		apiNotFound(w, r)
		return true
	}
	return false
}

func (s *Server) apiPhones(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	writeJSON(w, http.StatusOK, s.Phones.PhonesView())
}

func (s *Server) apiPhonesPair(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	v, err := s.Phones.OpenPairing()
	if err != nil {
		writeJSON(w, http.StatusConflict, errBody(err.Error()))
		return
	}
	s.Log.Printf("telefoni: finestra di abbinamento aperta (5 minuti)")
	writeJSON(w, http.StatusOK, v)
}

func (s *Server) apiPhonesCancel(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	writeJSON(w, http.StatusOK, s.Phones.CancelPairing())
}

func (s *Server) apiPhoneRevoke(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	id := r.PathValue("id")
	switch err := s.Phones.RevokePhone(id); {
	case errors.Is(err, phones.ErrNotFound):
		writeJSON(w, http.StatusNotFound, errBody("telefono sconosciuto"))
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, errBody("revocato fino al riavvio del servizio, ma phones.json non è stato scritto: "+err.Error()))
	default:
		s.Log.Printf("telefoni: revocato %s", id)
		w.WriteHeader(http.StatusNoContent)
	}
}

func (s *Server) apiPhonesQR(w http.ResponseWriter, r *http.Request) {
	if s.phonesOff(w, r) {
		return
	}
	svg, ok := s.Phones.PairingQR()
	if !ok {
		writeJSON(w, http.StatusNotFound, errBody("nessun abbinamento aperto"))
		return
	}
	w.Header().Set("Content-Type", "image/svg+xml")
	w.Header().Set("Cache-Control", "no-store")
	_, _ = w.Write(svg)
}
