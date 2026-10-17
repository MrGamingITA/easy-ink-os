package display

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
)

// Giornata di lavoro con un display finto che fa la parte della scheda (internal/fakedisplay/workday.go):
// orari in settings/set, comando workday solo autenticato, stato in status.workday.

const wdBoard = "calamaio-3f2a"

// wdDisplay: scheda finta abbinata e autenticata che gestisce la giornata, con l'orologio fisso a now.
func wdDisplay(t *testing.T, r *rig, now time.Time, o fakedisplay.WorkdayOptions) *fakedisplay.Display {
	t.Helper()
	r.pairKey(wdBoard, testKey)
	o.Enabled = true
	if o.Now == nil {
		o.Now = func() time.Time { return now }
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.DialWith(ctx, r.url, wdBoard, testKey, false, fakedisplay.Options{Workday: o})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	if _, err := d.Await("authed", 3*time.Second); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 2*time.Second, func() bool {
		s := statusOf(r.hub, wdBoard)
		return s != nil && s.Remote && s.Workday != nil && s.Workday.Known && s.Settings["workEnd"] == "18:00"
	})
	return d
}

func wdOf(t *testing.T, r *rig) *WorkdayView {
	t.Helper()
	s := statusOf(r.hub, wdBoard)
	if s == nil || s.Workday == nil {
		t.Fatalf("display senza workday: %+v", s)
	}
	return s.Workday
}

func at(h, m int) time.Time {
	n := time.Now()
	return time.Date(n.Year(), n.Month(), n.Day(), h, m, 0, 0, time.Local)
}

func TestWorkdayStartStop(t *testing.T) {
	sch := compileSchema(t)
	r := newRig(t, func(h *Hub) { h.AckTimeout = 2 * time.Second })
	now := at(11, 35)
	d := wdDisplay(t, r, now, fakedisplay.WorkdayOptions{})
	v := wdOf(t, r)
	if !v.Supported || !v.Known || v.On || v.End != nil || v.Pending != nil || v.Last != nil {
		t.Fatalf("all'inizio: %+v", v)
	}

	id, p, err := r.hub.Workday(wdBoard, WorkdayStart)
	if err != nil || p == nil || p.ID != id || p.Action != WorkdayStart || !strings.HasPrefix(id, "c") {
		t.Fatalf("start: %q %+v %v", id, p, err)
	}
	e, err := d.Await("workday", 2*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	b, _ := json.Marshal(e.Msg)
	validate(t, sch, "workday dal servizio", b)
	if e.Msg["id"] != id || e.Msg["action"] != "start" || len(e.Msg) != 4 {
		t.Fatalf("messaggio: %s", b)
	}
	waitFor(t, 2*time.Second, func() bool { v := wdOf(t, r); return v.Last != nil && v.Pending == nil })
	v = wdOf(t, r)
	want := at(18, 0).UnixMilli() // regola A: la fine è l'orario di fine anche se si inizia tardi
	if !v.On || v.End == nil || *v.End != want || !v.Last.OK || v.Last.ID != id || v.Last.Action != "start" || v.Last.Error != "" {
		t.Fatalf("dopo start: %+v (end atteso %d)", v, want)
	}
	if left := time.Duration(*v.End-now.UnixMilli()) * time.Millisecond; left != 6*time.Hour+25*time.Minute {
		t.Fatalf("mancano %s, attesi 6 h 25", left)
	}
	// anche in /debug (calamaio status)
	for _, in := range r.hub.Displays() {
		if in.Board == wdBoard && (in.Workday == nil || !in.Workday.On) {
			t.Fatalf("Info senza giornata: %+v", in)
		}
	}

	if _, _, err := r.hub.Workday(wdBoard, WorkdayStop); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 2*time.Second, func() bool { v := wdOf(t, r); return v.Last != nil && v.Last.Action == "stop" && v.Pending == nil })
	v = wdOf(t, r)
	if v.On || v.End != nil || !v.Last.OK {
		t.Fatalf("dopo stop: %+v", v)
	}
	if on, _, _, _ := d.Workday(); on {
		t.Fatal("la scheda finta ha ancora la giornata accesa")
	}
}

func TestWorkdayRefusedTimeoutBusy(t *testing.T) {
	r := newRig(t, func(h *Hub) { h.AckTimeout = 400 * time.Millisecond })
	// Dopo le 18:00 la scheda rifiuta: la giornata è già finita.
	wdDisplay(t, r, at(19, 5), fakedisplay.WorkdayOptions{})
	if _, _, err := r.hub.Workday(wdBoard, WorkdayStart); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 2*time.Second, func() bool { return wdOf(t, r).Last != nil })
	v := wdOf(t, r)
	if v.On || v.Last.OK || v.Last.Reason != fakedisplay.WorkdayErrOver || !strings.Contains(v.Last.Error, "non ha avviato la giornata") {
		t.Fatalf("rifiuto: %+v", v.Last)
	}
	if s := statusOf(r.hub, wdBoard); s.Error != nil {
		t.Fatalf("l'esito della giornata non va nell'errore delle impostazioni: %+v", s.Error)
	}
}

func TestWorkdayTimeoutAndBusy(t *testing.T) {
	r := newRig(t, func(h *Hub) { h.AckTimeout = 400 * time.Millisecond })
	wdDisplay(t, r, at(10, 0), fakedisplay.WorkdayOptions{NoAck: true})
	id, _, err := r.hub.Workday(wdBoard, WorkdayStart)
	if err != nil {
		t.Fatal(err)
	}
	if v := wdOf(t, r); v.Pending == nil || v.Pending.ID != id || v.Pending.Until <= time.Now().UnixMilli() {
		t.Fatalf("in attesa: %+v", v.Pending)
	}
	if _, _, err := r.hub.Workday(wdBoard, WorkdayStop); !errors.Is(err, ErrWorkdayBusy) {
		t.Fatalf("secondo comando: %v", err)
	}
	waitFor(t, 2*time.Second, func() bool { return wdOf(t, r).Last != nil })
	v := wdOf(t, r)
	if v.Pending != nil || v.Last.OK || !v.Last.Timeout || !strings.Contains(v.Last.Error, "potrebbe non essere stata avviata") {
		t.Fatalf("scadenza: %+v", v.Last)
	}
	// dopo la scadenza si può riprovare
	if _, _, err := r.hub.Workday(wdBoard, WorkdayStop); err != nil {
		t.Fatalf("dopo la scadenza: %v", err)
	}
}

func TestWorkdayLateStatusAndLocalStart(t *testing.T) {
	r := newRig(t, func(h *Hub) { h.AckTimeout = time.Second })
	d := wdDisplay(t, r, at(9, 30), fakedisplay.WorkdayOptions{AckOnly: true})
	if _, _, err := r.hub.Workday(wdBoard, WorkdayStart); err != nil {
		t.Fatal(err)
	}
	// ack senza status: il servizio non inventa lo stato, aspetta la scheda
	waitFor(t, 2*time.Second, func() bool { return wdOf(t, r).Last != nil })
	if v := wdOf(t, r); v.On || !v.Last.OK || v.Last.At < v.At {
		t.Fatalf("prima dello status: %+v", v)
	}
	d.SendWorkdayStatus()
	waitFor(t, 2*time.Second, func() bool { return wdOf(t, r).On })
	// Avvio dal menu della scheda: lo stato arriva solo con status.
	d.Deliver(`{"t":"workday","v":1,"id":"c999","action":"stop"}`)
	d.SendWorkdayStatus()
	waitFor(t, 2*time.Second, func() bool { return !wdOf(t, r).On })
	if !d.StartWorkdayLocal() {
		t.Fatal("avvio dal menu rifiutato")
	}
	waitFor(t, 2*time.Second, func() bool { v := wdOf(t, r); return v.On && v.End != nil && *v.End == at(18, 0).UnixMilli() })
}

func TestWorkdayRejectedBeforeSend(t *testing.T) {
	r := newRig(t, nil)
	if _, _, err := r.hub.Workday(wdBoard, WorkdayStart); !errors.Is(err, ErrNotConnected) {
		t.Fatalf("scollegata: %v", err)
	}
	// Firmware senza giornata: autenticato, ma niente chiavi né workday in status.
	c := remoteDisplay(t, r, wdBoard)
	if _, _, err := r.hub.Workday(wdBoard, WorkdayStart); !errors.Is(err, ErrNoWorkday) {
		t.Fatalf("firmware vecchio: %v", err)
	}
	if v := wdOf(t, r); v.Supported || v.Known {
		t.Fatalf("firmware vecchio: %+v", v)
	}
	if _, _, err := r.hub.SetSettings(wdBoard, raw(t, `{"workEnd":"17:00"}`)); !errors.Is(err, ErrNoWorkday) {
		t.Fatalf("orari a un firmware vecchio: %v", err)
	}
	var inv *InvalidError
	if _, _, err := r.hub.Workday(wdBoard, "pausa"); !errors.As(err, &inv) {
		t.Fatalf("azione sconosciuta: %v", err)
	}
	// Uno status con workday (anche null) dice che il firmware la conosce.
	c.send(`{"t":"status","v":1,"batt":80,"usb":true,"workday":null}`)
	waitFor(t, 2*time.Second, func() bool { return wdOf(t, r).Supported })
	if v := wdOf(t, r); v.Known {
		t.Fatalf("null non è uno stato: %+v", v)
	}

	// Non autenticato: niente comando.
	r2 := newRig(t, nil)
	c2 := r2.dial(nil)
	c2.greet(wdBoard)
	c2.send(`{"t":"status","v":1,"batt":80,"usb":true,"workday":{"on":false,"end":null}}`)
	waitFor(t, 2*time.Second, func() bool { s := statusOf(r2.hub, wdBoard); return s != nil && s.Workday.Known })
	if _, _, err := r2.hub.Workday(wdBoard, WorkdayStart); !errors.Is(err, ErrNotAuthed) {
		t.Fatalf("non autenticato: %v", err)
	}
}

func TestWorkHoursSettings(t *testing.T) {
	sch := compileSchema(t)
	r := newRig(t, func(h *Hub) { h.AckTimeout = 2 * time.Second })
	d := wdDisplay(t, r, at(10, 0), fakedisplay.WorkdayOptions{})
	var inv *InvalidError
	for _, bad := range []string{
		`{"workStart":"9:00"}`, `{"workStart":"24:00"}`, `{"workEnd":"18:60"}`, `{"workEnd":1800}`,
		`{"workStart":"18:00","workEnd":"09:00"}`, `{"workStart":"10:00","workEnd":"10:00"}`,
		`{"workEnd":"08:30"}`,   // prima dell'inizio riferito (09:00)
		`{"workStart":"18:00"}`, // uguale alla fine riferita
		`{"workStart":" 09:00"}`, `{"workStart":"09:00:00"}`, `{"workStart":null}`,
	} {
		if _, _, err := r.hub.SetSettings(wdBoard, raw(t, bad)); !errors.As(err, &inv) {
			t.Errorf("%s: accettato (%v)", bad, err)
		}
	}
	id, pend, err := r.hub.SetSettings(wdBoard, raw(t, `{"workStart":"08:30","workEnd":"17:15"}`))
	if err != nil || pend["workStart"] != "08:30" || pend["workEnd"] != "17:15" {
		t.Fatalf("set: %v %v", pend, err)
	}
	e, err := d.Await("set", 2*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	b, _ := json.Marshal(e.Msg)
	validate(t, sch, "set degli orari", b)
	if e.Msg["id"] != id {
		t.Fatalf("set: %s", b)
	}
	waitFor(t, 2*time.Second, func() bool {
		s := statusOf(r.hub, wdBoard)
		return len(s.SettingsPending) == 0 && s.Settings["workStart"] == "08:30" && s.Settings["workEnd"] == "17:15"
	})
	// L'inizio da solo si controlla contro la fine nuova.
	if _, _, err := r.hub.SetSettings(wdBoard, raw(t, `{"workStart":"17:30"}`)); !errors.As(err, &inv) {
		t.Fatalf("inizio dopo la fine nuova: %v", err)
	}
	// Anche contro un valore ancora in attesa o appena confermato.
	if _, _, err := r.hub.SetSettings(wdBoard, raw(t, `{"workEnd":"19:00"}`)); err != nil {
		t.Fatal(err)
	}
	if _, _, err := r.hub.SetSettings(wdBoard, raw(t, `{"workStart":"18:30"}`)); err != nil {
		t.Fatalf("inizio prima della fine in attesa: %v", err)
	}
}

func TestParseWorkday(t *testing.T) {
	good := map[string]*Workday{
		``:                                      nil,
		`null`:                                  nil,
		`{"on":false,"end":null}`:               {On: false},
		`{"on":false}`:                          {On: false},
		`{"on":false,"end":5}`:                  {On: false}, // spenta: end non conta
		`{"on":true,"end":null}`:                {On: true},
		`{"on":true,"end":1791748800000,"x":1}`: {On: true, End: ptr64(1791748800000)},
	}
	for in, want := range good {
		w, ok := parseWorkday(json.RawMessage(in))
		if !ok || (w == nil) != (want == nil) || (w != nil && (w.On != want.On || (w.End == nil) != (want.End == nil) || (w.End != nil && *w.End != *want.End))) {
			t.Errorf("%s: %+v %v", in, w, ok)
		}
	}
	for _, in := range []string{`{}`, `{"on":"sì"}`, `{"on":true,"end":-1}`, `{"on":true,"end":1.5}`, `{"on":true,"end":"18:00"}`, `true`, `[]`} {
		if _, ok := parseWorkday(json.RawMessage(in)); ok {
			t.Errorf("accettato: %s", in)
		}
	}
	// Uno status con workday non valido si scarta tutto (come gli altri messaggi non validi).
	if _, ok := parsePower([]byte(`{"t":"status","v":1,"batt":50,"usb":true,"workday":{"on":1}}`)); ok {
		t.Error("status con workday non valido accettato")
	}
	for _, s := range []string{"00:00", "09:05", "23:59"} {
		if !ValidClock(s) {
			t.Errorf("%s rifiutato", s)
		}
	}
	for in, want := range map[string]string{"9:00": "09:00", "9.30": "09:30", "1800": "18:00", " 07:45 ": "07:45"} {
		if got, ok := NormalizeClock(in); !ok || got != want {
			t.Errorf("NormalizeClock(%q) = %q %v", in, got, ok)
		}
	}
	for _, in := range []string{"24:00", "9", "18:7", "ore 9"} {
		if _, ok := NormalizeClock(in); ok {
			t.Errorf("NormalizeClock(%q) accettato", in)
		}
	}
}

func ptr64(v int64) *int64 { return &v }

// Uno status senza workday lascia lo stato com'era (campo facoltativo).
func TestWorkdayStatusWithoutFieldKeepsState(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.greet(wdBoard)
	c.send(`{"t":"status","v":1,"batt":80,"usb":true,"workday":{"on":true,"end":1791748800000}}`)
	waitFor(t, 2*time.Second, func() bool { s := statusOf(r.hub, wdBoard); return s != nil && s.Workday.On })
	c.send(`{"t":"status","v":1,"batt":79,"usb":true}`)
	waitFor(t, 2*time.Second, func() bool { s := statusOf(r.hub, wdBoard); return s.Batt != nil && *s.Batt == 79 })
	if v := wdOf(t, r); !v.On || v.End == nil || *v.End != 1791748800000 {
		t.Fatalf("stato perso: %+v", v)
	}
	c.send(`{"t":"status","v":1,"batt":79,"usb":true,"workday":{"on":true,"end":"x"}}`)
	c.send(`{"t":"status","v":1,"batt":78,"usb":true,"workday":{"on":false,"end":null}}`)
	waitFor(t, 2*time.Second, func() bool { return !wdOf(t, r).On })
	if s := statusOf(r.hub, wdBoard); *s.Batt != 78 || s.Workday.End != nil {
		t.Fatalf("dopo lo spegnimento: %+v", s.Workday)
	}
}

// Gli esempi del protocollo passano dai lettori del servizio come li legge la scheda.
func TestWorkdayExamplesParse(t *testing.T) {
	read := func(name string) []byte {
		b, err := os.ReadFile(filepath.Join(protocolDir, "examples", name))
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	p, ok := parsePower(read("status-giornata.json"))
	if !ok || !p.hasWorkday || p.workday == nil || !p.workday.On || p.workday.End == nil || *p.workday.End != 1791820800000 {
		t.Fatalf("status-giornata: %+v %v", p, ok)
	}
	p, ok = parsePower(read("status-giornata-spenta.json"))
	if !ok || p.workday == nil || p.workday.On || p.workday.End != nil {
		t.Fatalf("status-giornata-spenta: %+v", p)
	}
	p, ok = parsePower(read("status.json"))
	if !ok || p.hasWorkday {
		t.Fatalf("status senza giornata: %+v", p)
	}
	var sm settingsMsg
	if err := json.Unmarshal(read("settings-giornata.json"), &sm); err != nil {
		t.Fatal(err)
	}
	vals, unknown, err := parseReported(sm.Settings)
	if err != nil || len(unknown) != 0 || vals["workStart"] != "09:00" || vals["workEnd"] != "18:00" {
		t.Fatalf("settings-giornata: %v %v %v", vals, unknown, err)
	}
	var set struct {
		Settings map[string]json.RawMessage `json:"settings"`
	}
	_ = json.Unmarshal(read("set-orari.json"), &set)
	if _, err := ValidateRemote(set.Settings); err != nil {
		t.Fatalf("set-orari: %v", err)
	}
}
