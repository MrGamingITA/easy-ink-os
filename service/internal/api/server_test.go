package api

import (
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"calamaio/internal/capture"
)

func TestHookCapturedAndEmptyReply(t *testing.T) {
	dir := t.TempDir()
	cw, err := capture.New(dir)
	if err != nil {
		t.Fatal(err)
	}
	srv := httptest.NewServer(New(cw, log.New(io.Discard, "", 0)).Handler())
	defer srv.Close()

	resp, err := http.Post(srv.URL+"/hook?wait=1", "application/json",
		strings.NewReader(`{"hook_event_name":"PermissionRequest","session_id":"s1"}`))
	if err != nil {
		t.Fatal(err)
	}
	body, _ := io.ReadAll(resp.Body)
	resp.Body.Close()
	if resp.StatusCode != 200 || len(body) != 0 {
		t.Fatalf("risposta %d %q: in M0 deve essere 200 vuota", resp.StatusCode, body)
	}
	if _, err := http.Post(srv.URL+"/statusline", "application/json", strings.NewReader(`{"session_id":"s1"}`)); err != nil {
		t.Fatal(err)
	}

	files, _ := filepath.Glob(filepath.Join(dir, "capture-*.jsonl"))
	if len(files) != 1 {
		t.Fatalf("file di cattura = %v", files)
	}
	b, _ := os.ReadFile(files[0])
	if n := strings.Count(string(b), "\n"); n != 2 {
		t.Fatalf("righe catturate = %d", n)
	}

	resp, err = http.Get(srv.URL + "/debug")
	if err != nil {
		t.Fatal(err)
	}
	dbg, _ := io.ReadAll(resp.Body)
	if !strings.Contains(string(dbg), `"PermissionRequest"`) {
		t.Fatalf("debug senza l'evento: %s", dbg)
	}
}

func TestGetOnHookRejected(t *testing.T) {
	srv := httptest.NewServer(New(nil, log.New(io.Discard, "", 0)).Handler())
	defer srv.Close()
	resp, err := http.Get(srv.URL + "/hook")
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != http.StatusMethodNotAllowed {
		t.Fatalf("GET /hook = %d", resp.StatusCode)
	}
}
