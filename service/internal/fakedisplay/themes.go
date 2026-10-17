package fakedisplay

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"io"
	"net/http"
	"strings"
	"sync"
)

// La parte della scheda dei temi (docs/PROTOCOLLO.md «Temi»), come la fa il firmware: themes dopo auth-ok e a ogni
// cambio; theme-offer → GET http://<pc>/themes/<token>, controllo di lunghezza e CRC, theme-ack, themes; theme-use →
// attiva se ce l'ha, poi themes (invariato se non ce l'ha).
type ThemeOptions struct {
	Enabled     bool   // il firmware conosce i temi
	Reject      string // risponde theme-ack ok:false con questo motivo
	NoAck       bool   // non risponde all'offerta (per la scadenza)
	ForgetOnUse bool   // a ogni theme-use perde il tema chiesto (salvato rovinato): risponde themes senza di lui
}

type ThemeHave struct {
	ID  string `json:"id"`
	CRC uint32 `json:"crc"`
}

type themeState struct {
	mu   sync.Mutex
	opt  ThemeOptions
	have []ThemeHave
	cur  string
}

func (d *Display) Themes() ([]ThemeHave, string) {
	d.th.mu.Lock()
	defer d.th.mu.Unlock()
	return append([]ThemeHave{}, d.th.have...), d.th.cur
}

// ForgetTheme toglie un tema dalla scheda finta senza dirlo al servizio.
func (d *Display) ForgetTheme(id string) {
	d.th.mu.Lock()
	defer d.th.mu.Unlock()
	for i, h := range d.th.have {
		if h.ID == id {
			d.th.have = append(d.th.have[:i], d.th.have[i+1:]...)
			break
		}
	}
	if d.th.cur == id {
		d.th.cur = ""
	}
}

func (d *Display) sendThemes() {
	d.th.mu.Lock()
	var cur any
	if d.th.cur != "" {
		cur = d.th.cur
	}
	m := map[string]any{"t": "themes", "have": append([]ThemeHave{}, d.th.have...), "cur": cur}
	d.th.mu.Unlock()
	d.sendJSON(m)
	d.emit(Event{Type: "themes-sent"})
}

func (d *Display) handleThemes(t string, m map[string]any) bool {
	if !d.th.opt.Enabled {
		return false
	}
	switch t {
	case "theme-offer":
		d.emit(Event{Type: "theme-offer", Msg: m})
		if d.th.opt.NoAck {
			return true
		}
		go d.themeDownload(m)
		return true
	case "theme-use":
		d.emit(Event{Type: "theme-use", Msg: m})
		id, _ := m["id"].(string)
		d.th.mu.Lock()
		if d.th.opt.ForgetOnUse && id != "" {
			d.th.mu.Unlock()
			d.ForgetTheme(id)
			d.th.mu.Lock()
		}
		ok := id == ""
		for _, h := range d.th.have {
			ok = ok || h.ID == id
		}
		if ok {
			d.th.cur = id
		}
		d.th.mu.Unlock()
		d.sendThemes()
		return true
	}
	return false
}

func (d *Display) themeDownload(m map[string]any) {
	id, _ := m["id"].(string)
	tok, _ := m["token"].(string)
	crc := uint32(m["crc"].(float64))
	ack := func(ok bool, why string) {
		msg := map[string]any{"t": "theme-ack", "id": id, "ok": ok}
		if why != "" {
			msg["error"] = why
		}
		d.sendJSON(msg)
	}
	if d.th.opt.Reject != "" {
		ack(false, d.th.opt.Reject)
		return
	}
	base := strings.Replace(d.wsURL, "ws://", "http://", 1)
	base = base[:strings.LastIndex(base, "/")]
	res, err := http.Get(base + "/themes/" + tok)
	if err != nil {
		ack(false, "download non riuscito")
		return
	}
	defer res.Body.Close()
	bin, _ := io.ReadAll(res.Body)
	switch {
	case res.StatusCode != 200:
		ack(false, fmt.Sprintf("download non riuscito (HTTP %d)", res.StatusCode))
		return
	case len(bin) < 72 || string(bin[:4]) != "CTHM" || binary.LittleEndian.Uint32(bin[8:]) != crc || crc32.ChecksumIEEE(bin[12:]) != crc:
		ack(false, "CRC sbagliato")
		return
	}
	d.th.mu.Lock()
	keep := d.th.have[:0]
	for _, h := range d.th.have {
		if h.ID != id {
			keep = append(keep, h)
		}
	}
	d.th.have = append(keep, ThemeHave{id, crc})
	d.th.mu.Unlock()
	ack(true, "")
	d.sendThemes()
}
