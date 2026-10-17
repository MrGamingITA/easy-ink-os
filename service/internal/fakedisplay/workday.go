package fakedisplay

import (
	"encoding/json"
	"fmt"
	"sync"
	"time"
)

// La parte della scheda della giornata di lavoro (docs/PROTOCOLLO.md «Giornata di lavoro»), come la deve
// fare il firmware: orari workStart/workEnd nelle impostazioni (set solo autenticato, workStart < workEnd),
// comando workday start/stop solo autenticato; la fine è l'orario di fine di oggi anche se si inizia tardi;
// dopo un cambiamento manda subito `status` con `workday`, poi l'ack.

// Motivi degli ack negativi della scheda finta.
const (
	WorkdayErrAuth  = "display non abbinato: cambia le impostazioni dal menu del display"
	WorkdayErrOver  = "giornata già finita"
	WorkdayErrHours = "orari non validi"
)

// WorkdayOptions: comportamento della scheda finta per la giornata di lavoro.
type WorkdayOptions struct {
	Enabled bool             // il firmware gestisce la giornata (altrimenti niente chiavi, niente workday in status)
	NoAck   bool             // non risponde a workday (per la scadenza)
	AckOnly bool             // risponde ack ma non manda status (per l'attesa dello stato)
	Now     func() time.Time // orologio della scheda (nil = time.Now)
}

type workdayState struct {
	mu         sync.Mutex
	opt        WorkdayOptions
	start, end string // orari
	on         bool
	endAt      int64 // ms epoch
}

func (d *Display) wdNow() time.Time {
	if d.wd.opt.Now != nil {
		return d.wd.opt.Now()
	}
	return time.Now()
}

// Workday restituisce lo stato della giornata sulla scheda finta.
func (d *Display) Workday() (on bool, end int64, start, stop string) {
	d.wd.mu.Lock()
	defer d.wd.mu.Unlock()
	return d.wd.on, d.wd.endAt, d.wd.start, d.wd.end
}

// SendWorkdayStatus manda `status` con lo stato della giornata (e l'alimentazione delle opzioni OTA).
func (d *Display) SendWorkdayStatus() {
	d.wd.mu.Lock()
	wd := map[string]any{"on": d.wd.on, "end": nil}
	if d.wd.on {
		wd["end"] = d.wd.endAt
	}
	d.wd.mu.Unlock()
	d.ota.mu.Lock()
	batt, usb := d.ota.opt.Batt, d.ota.opt.USB
	d.ota.mu.Unlock()
	d.sendJSON(map[string]any{"t": "status", "batt": batt, "usb": usb, "workday": wd})
}

// StartWorkdayLocal avvia la giornata dal menu della scheda (Menu › Strumenti) e manda status.
func (d *Display) StartWorkdayLocal() bool {
	ok := d.wdStart() == ""
	d.SendWorkdayStatus()
	return ok
}

// wdStart applica «start» (regola A: la fine è l'orario di fine di oggi). "" se riuscito.
func (d *Display) wdStart() string {
	d.wd.mu.Lock()
	defer d.wd.mu.Unlock()
	now := d.wdNow()
	var hh, mm int
	fmt.Sscanf(d.wd.end, "%d:%d", &hh, &mm)
	end := time.Date(now.Year(), now.Month(), now.Day(), hh, mm, 0, 0, now.Location())
	if !now.Before(end) {
		return WorkdayErrOver
	}
	d.wd.on, d.wd.endAt = true, end.UnixMilli()
	return ""
}

func (d *Display) handleWorkday(t string, m map[string]any, raw []byte) bool {
	d.wd.mu.Lock()
	opt := d.wd.opt
	d.wd.mu.Unlock()
	if !opt.Enabled || (t != "workday" && t != "set") {
		return false
	}
	id := str(m, "id")
	if t == "set" {
		var s struct {
			Settings map[string]json.RawMessage `json:"settings"`
		}
		_ = json.Unmarshal(raw, &s)
		if _, a := s.Settings["workStart"]; !a {
			if _, b := s.Settings["workEnd"]; !b {
				return false // le altre impostazioni: come prima (solo l'evento)
			}
		}
		if !d.Authed() {
			d.ack(id, false, WorkdayErrAuth)
			d.emit(Event{Type: "rejected", Text: "set senza autenticazione", Msg: m})
			return true
		}
		d.wd.mu.Lock()
		start, end := d.wd.start, d.wd.end
		for k, dst := range map[string]*string{"workStart": &start, "workEnd": &end} {
			v, ok := s.Settings[k]
			if !ok {
				continue
			}
			if json.Unmarshal(v, dst) != nil {
				d.wd.mu.Unlock()
				d.ack(id, false, WorkdayErrHours)
				return true
			}
		}
		if len(start) != 5 || len(end) != 5 || start >= end {
			d.wd.mu.Unlock()
			d.ack(id, false, WorkdayErrHours)
			return true
		}
		d.wd.start, d.wd.end = start, end
		d.wd.mu.Unlock()
		d.emit(Event{Type: "set", Msg: m})
		_ = d.Send(d.settingsMsg(true))
		d.ack(id, true, "")
		return true
	}
	// workday
	if opt.NoAck {
		d.emit(Event{Type: "ignored", Text: t, Msg: m})
		return true
	}
	if !d.Authed() {
		d.ack(id, false, WorkdayErrAuth)
		d.emit(Event{Type: "rejected", Text: "workday senza autenticazione", Msg: m})
		return true
	}
	why := ""
	switch str(m, "action") {
	case "start":
		why = d.wdStart()
	case "stop":
		d.wd.mu.Lock()
		d.wd.on, d.wd.endAt = false, 0
		d.wd.mu.Unlock()
	default:
		why = "azione sconosciuta"
	}
	d.emit(Event{Type: "workday", Text: str(m, "action"), Msg: m})
	if why == "" && !opt.AckOnly {
		d.SendWorkdayStatus()
	}
	d.ack(id, why == "", why)
	return true
}
