package fakedisplay

import (
	"encoding/json"
	"strings"
	"sync"
	"time"

	"calamaio/internal/m5"
)

// La parte della scheda delle reti Wi-Fi gestite dal PC (docs/PROTOCOLLO.md «Reti Wi-Fi»), come la deve
// fare il firmware: wifi-add solo autenticato, password decifrata con la chiave derivata da SK e l'aad
// "wifi-add" ␟ id ␟ ssid (tag verificato da AES-GCM), al massimo WifiMax reti, password di 8–63 byte
// senza byte zero; wifi-forget solo autenticato e con il mac giusto, poi ack e riavvio (connessione chiusa).

// WifiMax: reti salvate al massimo dalla scheda finta (come il firmware).
const WifiMax = 5

// Motivi degli ack negativi, gli stessi del firmware (protocol/vectors/wifi-vectors.json, format.ack).
const (
	WifiErrAuth    = "display non abbinato: cambia le impostazioni dal menu del display"
	WifiErrCrypto  = "decifratura fallita"
	WifiErrShort   = "password non valida"
	WifiErrFull    = "lista piena"
	WifiErrUnknown = "rete sconosciuta"
	WifiErrMac     = "mac non valido"
	WifiErrSSID    = "ssid non valido"
)

// SavedNetwork è una rete salvata sulla scheda finta (con la password in chiaro: è la cassaforte finta).
type SavedNetwork struct {
	SSID     string
	Password string
	Current  bool
	RSSI     *int
}

type wifiState struct {
	mu       sync.Mutex
	nets     []SavedNetwork
	noReboot bool // dopo un forget riuscito non chiude la connessione
	noAck    bool // non risponde a wifi-add e wifi-forget (firmware senza più reti)
}

// SetWifi imposta le reti salvate e manda wifi-list.
func (d *Display) SetWifi(nets ...SavedNetwork) {
	d.wifi.mu.Lock()
	d.wifi.nets = append([]SavedNetwork(nil), nets...)
	d.wifi.mu.Unlock()
	d.SendWifiList()
}

// WifiOptions: niente riavvio dopo forget, niente ack.
func (d *Display) WifiOptions(noReboot, noAck bool) {
	d.wifi.mu.Lock()
	d.wifi.noReboot, d.wifi.noAck = noReboot, noAck
	d.wifi.mu.Unlock()
}

// Wifi restituisce le reti salvate.
func (d *Display) Wifi() []SavedNetwork {
	d.wifi.mu.Lock()
	defer d.wifi.mu.Unlock()
	return append([]SavedNetwork(nil), d.wifi.nets...)
}

// SendWifiList manda wifi-list (mai password).
func (d *Display) SendWifiList() {
	d.wifi.mu.Lock()
	list := make([]map[string]any, 0, len(d.wifi.nets))
	for _, n := range d.wifi.nets {
		var last any
		if n.Current {
			last = time.Now().UnixMilli()
		}
		var rssi any
		if n.RSSI != nil {
			rssi = *n.RSSI
		}
		list = append(list, map[string]any{"ssid": n.SSID, "current": n.Current, "rssi": rssi, "lastUsed": last})
	}
	d.wifi.mu.Unlock()
	d.sendJSON(map[string]any{"t": "wifi-list", "networks": list})
}

func (d *Display) ack(id string, ok bool, why string) {
	m := map[string]any{"t": "ack", "id": id, "ok": ok}
	if !ok {
		m["error"] = why
	}
	d.sendJSON(m)
}

func (d *Display) handleWifi(t string, m map[string]any, raw []byte) bool {
	if t != "wifi-add" && t != "wifi-forget" {
		return false
	}
	d.wifi.mu.Lock()
	noAck := d.wifi.noAck
	d.wifi.mu.Unlock()
	if noAck {
		d.emit(Event{Type: "ignored", Text: t, Msg: m})
		return true
	}
	d.mu.Lock()
	authed, sk := d.authed, append([]byte(nil), d.sk...)
	d.mu.Unlock()
	id, ssid := str(m, "id"), str(m, "ssid")
	if !authed {
		d.ack(id, false, WifiErrAuth)
		d.emit(Event{Type: "rejected", Text: t + " senza autenticazione", Msg: m})
		return true
	}
	if t == "wifi-add" {
		var msg struct{ ID, SSID, IV, CT string }
		_ = json.Unmarshal(raw, &msg)
		iv, ok1 := m5.ParseHex(msg.IV, m5.WifiIVLen)
		ct, ok2 := m5.ParseHex(msg.CT, len(msg.CT)/2)
		var pw []byte
		var err error = m5.ErrWifiOpen
		if ok1 && ok2 && len(msg.CT)%2 == 0 {
			pw, err = m5.WifiOpen(sk, msg.ID, msg.SSID, iv, ct)
		}
		if err != nil {
			d.ack(id, false, WifiErrCrypto)
			d.emit(Event{Type: "rejected", Text: "wifi-add: " + err.Error(), Msg: m})
			return true
		}
		if ssid == "" || len(ssid) > 32 {
			d.ack(id, false, WifiErrSSID)
			d.emit(Event{Type: "rejected", Text: "wifi-add: " + WifiErrSSID, Msg: m})
			return true
		}
		if len(pw) < 8 || len(pw) > 63 || strings.IndexByte(string(pw), 0) >= 0 {
			d.ack(id, false, WifiErrShort)
			d.emit(Event{Type: "rejected", Text: "wifi-add: " + WifiErrShort, Msg: m})
			return true
		}
		d.wifi.mu.Lock()
		idx := -1
		for i, n := range d.wifi.nets {
			if n.SSID == ssid {
				idx = i
			}
		}
		if idx < 0 && len(d.wifi.nets) >= WifiMax {
			d.wifi.mu.Unlock()
			d.ack(id, false, WifiErrFull)
			d.emit(Event{Type: "rejected", Text: "wifi-add: " + WifiErrFull, Msg: m})
			return true
		}
		if idx < 0 {
			d.wifi.nets = append(d.wifi.nets, SavedNetwork{SSID: ssid, Password: string(pw)})
		} else {
			d.wifi.nets[idx].Password = string(pw) // stessa rete: nuova password
		}
		d.wifi.mu.Unlock()
		d.ack(id, true, "")
		d.emit(Event{Type: t, Text: string(pw), Msg: m})
		d.SendWifiList()
		return true
	}
	// wifi-forget
	mac, ok := m5.ParseHex(str(m, "mac"), m5.MacLen)
	if !ok || !m5.Equal(mac, m5.WifiForgetMac(sk, id, ssid)) {
		d.ack(id, false, WifiErrMac)
		d.emit(Event{Type: "rejected", Text: "wifi-forget: " + WifiErrMac, Msg: m})
		return true
	}
	d.wifi.mu.Lock()
	kept := d.wifi.nets[:0:0]
	found := false
	for _, n := range d.wifi.nets {
		if n.SSID == ssid {
			found = true
			continue
		}
		kept = append(kept, n)
	}
	if !found {
		d.wifi.mu.Unlock()
		d.ack(id, false, WifiErrUnknown)
		d.emit(Event{Type: "rejected", Text: "wifi-forget: " + WifiErrUnknown, Msg: m})
		return true
	}
	d.wifi.nets = kept
	noReboot := d.wifi.noReboot
	d.wifi.mu.Unlock()
	d.ack(id, true, "")
	d.emit(Event{Type: t, Text: ssid, Msg: m})
	if !noReboot {
		// pulizia profonda della NVS e riavvio dopo l'ack (DECISIONI, «Pulizia profonda»)
		go func() {
			time.Sleep(50 * time.Millisecond)
			d.Close()
		}()
	}
	return true
}
