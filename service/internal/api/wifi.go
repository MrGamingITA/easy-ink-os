package api

import (
	"context"
	"encoding/json"
	"io"
	"net"
	"net/http"

	"calamaio/internal/display"
)

// Reti Wi-Fi della scheda dalla webapp e da `calamaio wifi` (docs/PROTOCOLLO.md «Reti Wi-Fi» e «API
// locale»). La password arriva qui solo da 127.0.0.1 (stesse guardie di Host, origine e JSON delle altre
// POST), non si scrive mai nel log né su disco e va alla scheda solo cifrata (display.Hub.WifiAdd).

// WifiControl: aggiunge e dimentica reti (display.Hub).
type WifiControl interface {
	WifiAdd(ctx context.Context, board, ssid string, password []byte) (display.WifiResult, error)
	WifiForget(ctx context.Context, board, ssid string) (display.WifiResult, error)
}

// maxWifiBody: SSID ≤ 32 byte e password ≤ 64 byte stanno larghi in 4 KB anche scritti come \uXXXX.
const maxWifiBody = 4 << 10

// fromLoopback: la connessione TCP viene da 127.0.0.0/8 o ::1 (il server della API ascolta solo lì:
// controllo in più, così una password non passa mai da un'altra macchina anche se un giorno cambiasse).
func fromLoopback(r *http.Request) bool {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

// decodeWifi legge {ssid, password?} senza campi in più. I messaggi d'errore non ripetono mai i valori.
func decodeWifi(r *http.Request, withPassword bool) (ssid string, password []byte, msg string) {
	var body struct {
		SSID     *string `json:"ssid"`
		Password *string `json:"password"`
	}
	dec := json.NewDecoder(io.LimitReader(r.Body, maxWifiBody))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&body); err != nil || body.SSID == nil {
		if withPassword {
			return "", nil, "serve un oggetto JSON {ssid, password}"
		}
		return "", nil, "serve un oggetto JSON {ssid}"
	}
	if dec.More() {
		return "", nil, "dati in più dopo l'oggetto JSON"
	}
	if withPassword {
		if body.Password == nil {
			return "", nil, "manca la password"
		}
		password = []byte(*body.Password)
	} else if body.Password != nil {
		return "", nil, "per dimenticare una rete non serve la password"
	}
	return *body.SSID, password, ""
}

// writeWifiResult: 200 riuscita · 504 nessun ack in tempo · 409 rifiutata dalla scheda o scheda scollegata.
func writeWifiResult(w http.ResponseWriter, res display.WifiResult) {
	switch {
	case res.OK:
		writeJSON(w, http.StatusOK, res)
	case res.Timeout:
		writeJSON(w, http.StatusGatewayTimeout, res)
	default:
		writeJSON(w, http.StatusConflict, res)
	}
}

func (s *Server) apiWifiAdd(w http.ResponseWriter, r *http.Request) {
	if !fromLoopback(r) {
		writeJSON(w, http.StatusForbidden, errBody("solo da 127.0.0.1"))
		return
	}
	if s.Wifi == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	ssid, pw, msg := decodeWifi(r, true)
	if msg != "" {
		writeJSON(w, http.StatusBadRequest, errBody(msg))
		return
	}
	res, err := s.Wifi.WifiAdd(r.Context(), r.PathValue("board"), ssid, pw) // azzera pw
	if err != nil {
		commandError(w, err)
		return
	}
	writeWifiResult(w, res)
}

func (s *Server) apiWifiForget(w http.ResponseWriter, r *http.Request) {
	if s.Wifi == nil {
		writeJSON(w, http.StatusConflict, errBody(display.ErrNotConnected.Error()))
		return
	}
	ssid, _, msg := decodeWifi(r, false)
	if msg != "" {
		writeJSON(w, http.StatusBadRequest, errBody(msg))
		return
	}
	res, err := s.Wifi.WifiForget(r.Context(), r.PathValue("board"), ssid)
	if err != nil {
		commandError(w, err)
		return
	}
	writeWifiResult(w, res)
}
