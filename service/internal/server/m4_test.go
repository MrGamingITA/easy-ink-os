package server

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// M4 dall'esterno: `serve` senza --fake, payload veri delle fixture su /hook e /statusline, un
// display collegato vede le sessioni; archivia e ripristina dalla API della webapp.
func TestServeRealHooksEndToEnd(t *testing.T) {
	s := start(t, false, 0)
	base := fmt.Sprintf("http://127.0.0.1:%d", s.cfg.APIPort)
	_, ch := dialHello(t, s.cfg.WSPort)
	next(t, ch) // welcome
	next(t, ch) // state vuoto

	f, err := os.Open(filepath.Join("..", "..", "..", "protocol", "samples", "fixtures", "cattura-2026-10-11.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 0, 1<<20), 16<<20)
	n := 0
	for sc.Scan() && n <= 38 { // fino allo Stop della sessione A
		var rec struct {
			Source  string          `json:"source"`
			Payload json.RawMessage `json:"payload"`
		}
		if err := json.Unmarshal(sc.Bytes(), &rec); err != nil {
			t.Fatal(err)
		}
		path := "/hook"
		if rec.Source == "statusline" {
			path = "/statusline"
		}
		resp, err := http.Post(base+path, "application/json", bytes.NewReader(rec.Payload))
		if err != nil {
			t.Fatal(err)
		}
		body, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		if resp.StatusCode != 200 || len(body) != 0 {
			t.Fatalf("riga %d: %d %q (nessuna decisione sui permessi fino a M5)", n, resp.StatusCode, body)
		}
		n++
	}
	const sid = "dcbb543b-f2ad-479f-90df-ddfbd6fd58cd"
	deadline := time.Now().Add(3 * time.Second)
	for {
		st := next(t, ch)
		if state, _ := sessionState(st, "Pagina index.html e style.css"); state == "done" {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("il display non ha visto la sessione finita")
		}
	}

	post := func(p string) (int, map[string]any) {
		t.Helper()
		resp, err := http.Post(base+p, "application/json", nil)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		var m map[string]any
		_ = json.NewDecoder(resp.Body).Decode(&m)
		return resp.StatusCode, m
	}
	if code, _ := post("/api/sessions/nessuna/archive"); code != 404 {
		t.Fatalf("sessione sconosciuta: %d", code)
	}
	if code, _ := post("/api/sessions/" + sid + "/archive"); code != 202 {
		t.Fatalf("archive: %d", code)
	}
	status := func() map[string]any {
		resp, err := http.Get(base + "/api/status")
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		var m map[string]any
		_ = json.NewDecoder(resp.Body).Decode(&m)
		return m["state"].(map[string]any)["sessions"].([]any)[0].(map[string]any)
	}
	if ss := status(); ss["archived"] != true || ss["id"] != sid {
		t.Fatalf("/api/status: %v", ss)
	}
	for {
		st := next(t, ch)
		if len(st["sessions"].([]any)) == 0 {
			break
		}
	}
	if code, _ := post("/api/sessions/" + sid + "/unarchive"); code != 202 {
		t.Fatalf("unarchive: %d", code)
	}
	if ss := status(); ss["archived"] != false {
		t.Fatalf("/api/status dopo unarchive: %v", ss)
	}
	// una sessione che lavora non si archivia
	prompt := fmt.Sprintf(`{"session_id":%q,"cwd":"/home/utente/progetto-prova","hook_event_name":"UserPromptSubmit","prompt":"ancora"}`, sid)
	resp, _ := http.Post(base+"/hook", "application/json", bytes.NewReader([]byte(prompt)))
	resp.Body.Close()
	if code, m := post("/api/sessions/" + sid + "/archive"); code != 409 || m["error"] == nil {
		t.Fatalf("archive su working: %d %v", code, m)
	}
	// stesse protezioni delle altre POST della webapp
	req, _ := http.NewRequest("POST", base+"/api/sessions/"+sid+"/archive", nil)
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Origin", "http://evil.example")
	resp, err = http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != 403 {
		t.Fatalf("CSRF: %d", resp.StatusCode)
	}
}
