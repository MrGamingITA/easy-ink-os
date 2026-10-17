package server

import (
	"context"
	"fmt"
	"net/http"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
)

// Giornata di lavoro dalla webapp: servizio vero, scheda finta che gestisce la giornata
// (internal/fakedisplay/workday.go), richieste HTTP come le fa il browser, stato via SSE.

func workdayOf(st map[string]any) map[string]any {
	d := firstDisplay(st)
	if d == nil {
		return nil
	}
	w, _ := d["workday"].(map[string]any)
	return w
}

func TestWebWorkdayEndToEnd(t *testing.T) {
	s := start(t, false, 0)
	w := newWeb(t, s)
	events := sse(t, w)
	if err := s.keys.Put("calamaio-3f2a", testKey, time.Now()); err != nil {
		t.Fatal(err)
	}
	if err := s.run.Hub.LoadKeys(); err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	clock := time.Date(now.Year(), now.Month(), now.Day(), 11, 35, 0, 0, time.Local)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.DialWith(ctx, fmt.Sprintf("ws://127.0.0.1:%d/ws", s.cfg.WSPort), "calamaio-3f2a", testKey, false,
		fakedisplay.Options{Workday: fakedisplay.WorkdayOptions{Enabled: true, Now: func() time.Time { return clock }}})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	if _, err := d.Await("authed", 3*time.Second); err != nil {
		t.Fatal(err)
	}
	waitStatus(t, w, func(st map[string]any) bool {
		wd := workdayOf(st)
		return wd != nil && wd["supported"] == true && wd["known"] == true && wd["on"] == false && firstDisplay(st)["remote"] == true
	})

	// Errori prima dell'invio.
	for body, want := range map[string]int{
		`{"action":"pausa"}`:               400,
		`{}`:                               400,
		`{"action":"start","end":"18:00"}`: 400,
		`[]`:                               400,
	} {
		if code, m := w.post("/api/displays/calamaio-3f2a/workday", body); code != want {
			t.Errorf("%s: %d %v", body, code, m)
		}
	}
	if code, m := w.post("/api/displays/altra/workday", `{"action":"start"}`); code != 409 {
		t.Errorf("scheda non collegata: %d %v", code, m)
	}
	// Stesse guardie delle altre POST: niente JSON, niente stessa origine.
	if code, _, _ := w.do(http.MethodPost, "/api/displays/calamaio-3f2a/workday", "text/plain", `{"action":"start"}`,
		"Origin", "http://"+w.host); code != 415 {
		t.Errorf("text/plain: %d", code)
	}
	if code, _, _ := w.do(http.MethodPost, "/api/displays/calamaio-3f2a/workday", "application/json", `{"action":"start"}`,
		"Origin", "http://evil.example"); code != 403 {
		t.Errorf("altra origine: %d", code)
	}
	if on, _, _, _ := d.Workday(); on {
		t.Fatal("una richiesta rifiutata ha avviato la giornata")
	}

	code, m := w.post("/api/displays/calamaio-3f2a/workday", `{"action":"start"}`)
	p, _ := m["pending"].(map[string]any)
	if code != 202 || m["id"] == nil || p == nil || p["action"] != "start" || p["id"] != m["id"] {
		t.Fatalf("start: %d %v", code, m)
	}
	// Lo stato in diretta arriva via SSE.
	want := float64(time.Date(now.Year(), now.Month(), now.Day(), 18, 0, 0, 0, time.Local).UnixMilli())
	deadline := time.After(3 * time.Second)
	for got := false; !got; {
		select {
		case st := <-events:
			wd := workdayOf(st)
			got = wd != nil && wd["on"] == true && wd["end"] == want && wd["pending"] == nil
		case <-deadline:
			t.Fatalf("SSE: giornata accesa non vista; status = %v", workdayOf(w.status()))
		}
	}
	last := workdayOf(w.status())["last"].(map[string]any)
	if last["ok"] != true || last["action"] != "start" {
		t.Fatalf("last = %v", last)
	}

	// Orari: validati dal servizio, poi confermati dalla scheda.
	if code, m := w.post("/api/displays/calamaio-3f2a/settings", `{"workStart":"18:30"}`); code != 400 || !strings.Contains(fmt.Sprint(m["error"]), "prima di quello di fine") {
		t.Fatalf("inizio dopo la fine: %d %v", code, m)
	}
	if code, m := w.post("/api/displays/calamaio-3f2a/settings", `{"workStart":"08:00","workEnd":"17:00"}`); code != 202 {
		t.Fatalf("orari: %d %v", code, m)
	}
	waitStatus(t, w, func(st map[string]any) bool {
		set, _ := firstDisplay(st)["settings"].(map[string]any)
		return set["workStart"] == "08:00" && set["workEnd"] == "17:00"
	})
	if _, _, a, b := d.Workday(); a != "08:00" || b != "17:00" {
		t.Fatalf("scheda: %s–%s", a, b)
	}

	if code, m := w.post("/api/displays/calamaio-3f2a/workday", `{"action":"stop"}`); code != 202 {
		t.Fatalf("stop: %d %v", code, m)
	}
	waitStatus(t, w, func(st map[string]any) bool {
		wd := workdayOf(st)
		return wd["on"] == false && wd["end"] == nil && wd["pending"] == nil
	})
}
