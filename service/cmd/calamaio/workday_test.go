package main

import (
	"strings"
	"sync"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
)

// Giornata di lavoro dall'esterno: servizio vero su porte libere, scheda finta abbinata che gestisce la
// giornata, `calamaio workday` vero lanciato come processo.
func TestCLIWorkday(t *testing.T) {
	s := startSvc(t, 0)
	var mu sync.Mutex
	n := time.Now()
	clock := time.Date(n.Year(), n.Month(), n.Day(), 0, 1, 0, 0, time.Local)
	now := func() time.Time { mu.Lock(); defer mu.Unlock(); return clock }
	d := s.otaDisplay(fakedisplay.Options{Workday: fakedisplay.WorkdayOptions{Enabled: true, Now: now}}, nil)
	waitFor(t, "giornata riferita", func() bool { w := s.status().Workday; return w != nil && w.Known && s.status().Remote })

	for _, args := range [][]string{{"workday"}, {"workday", "pausa"}, {"workday", "hours", "09:00"}, {"workday", "start", "subito"}} {
		if r := s.cli("", args...); r.code != 2 || !strings.Contains(r.err, "uso: calamaio workday") {
			t.Errorf("%v: %+v", args, r)
		}
	}
	r := s.cli("", "workday", "status")
	if r.code != 0 || !strings.Contains(r.out, "Giornata di lavoro di "+board+": spenta (orari 09:00–18:00).") {
		t.Fatalf("status: %+v", r)
	}
	if r := s.cli("", "workday", "hours", "18:00", "9:00"); r.code != 1 || !strings.Contains(r.err, "prima di quello di fine") {
		t.Fatalf("orari al contrario: %+v", r)
	}
	if r := s.cli("", "workday", "hours", "25:00", "9:00"); r.code != 1 || !strings.Contains(r.err, "HH:MM") {
		t.Fatalf("orario impossibile: %+v", r)
	}
	// «8:30» va bene: la CLI lo scrive 08:30. Fine alle 23:59 perché la prova non dipenda dall'ora.
	r = s.cli("", "workday", "hours", "8:30", "23:59", "--board", board)
	if r.code != 0 || !strings.Contains(r.out, "Orari della giornata di lavoro su "+board+": 08:30–23:59.") {
		t.Fatalf("hours: %+v", r)
	}
	if _, _, a, b := d.Workday(); a != "08:30" || b != "23:59" {
		t.Fatalf("scheda: %s–%s", a, b)
	}
	r = s.cli("", "workday", "start")
	if r.code != 0 || !strings.Contains(r.out, "Giornata di lavoro avviata su "+board+": in corso, mancan") ||
		!strings.Contains(r.out, "fino alle 23:59 (orari 08:30–23:59)") {
		t.Fatalf("start: %+v", r)
	}
	if on, end, _, _ := d.Workday(); !on || time.UnixMilli(end).Format("15:04") != "23:59" {
		t.Fatalf("scheda dopo start: %v %d", on, end)
	}
	if r := s.cli("", "workday", "status"); r.code != 0 || !strings.Contains(r.out, ": in corso, ") {
		t.Fatalf("status in corso: %+v", r)
	}
	if r := s.cli("", "status"); r.code != 0 || !strings.Contains(r.out, "giornata: mancan") {
		t.Fatalf("calamaio status: %+v", r)
	}
	if r := s.cli("", "workday", "stop"); r.code != 0 || !strings.Contains(r.out, "Giornata di lavoro fermata su "+board+".") {
		t.Fatalf("stop: %+v", r)
	}
	// Dopo l'orario di fine la scheda rifiuta: il motivo arriva fino al terminale.
	mu.Lock()
	clock = clock.Add(23*time.Hour + 58*time.Minute + 30*time.Second)
	mu.Unlock()
	r = s.cli("", "workday", "start")
	if r.code != 1 || !strings.Contains(r.out, "Non riuscito: la scheda non ha avviato la giornata di lavoro: "+fakedisplay.WorkdayErrOver) {
		t.Fatalf("start rifiutato: %+v", r)
	}
	if r := s.cli("", "workday", "start", "--board", "altra"); r.code != 1 || !strings.Contains(r.err, "non è collegata") {
		t.Fatalf("scheda sconosciuta: %+v", r)
	}
}

// Firmware senza giornata: la CLI lo dice senza mandare niente.
func TestCLIWorkdayOldFirmware(t *testing.T) {
	s := startSvc(t, 0)
	d := s.otaDisplay(fakedisplay.Options{}, nil)
	waitFor(t, "autenticato", func() bool { return s.status().Authed })
	if r := s.cli("", "workday", "start"); r.code != 1 || !strings.Contains(r.err, "non gestisce la giornata di lavoro") {
		t.Fatalf("start: %+v", r)
	}
	if r := s.cli("", "workday", "status"); r.code != 0 || !strings.Contains(r.out, "non gestita dal firmware") {
		t.Fatalf("status: %+v", r)
	}
	for _, f := range d.Frames() {
		if strings.Contains(f, `"workday"`) {
			t.Fatalf("partito verso un firmware senza giornata: %s", f)
		}
	}
}
