package api

import (
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"calamaio/internal/capture"
)

type fakeSink struct {
	mu        sync.Mutex
	hooks, sl []string
}

func (f *fakeSink) Hook(b []byte)         { f.mu.Lock(); f.hooks = append(f.hooks, string(b)); f.mu.Unlock() }
func (f *fakeSink) StatusLine(b []byte)   { f.mu.Lock(); f.sl = append(f.sl, string(b)); f.mu.Unlock() }
func (f *fakeSink) Debug() map[string]any { return map[string]any{"ok": true} }

// --capture insieme ai dati veri: ogni payload finisce sia nel file sia nella macchina a stati.
func TestCaptureAndSinkTogether(t *testing.T) {
	dir := t.TempDir()
	cw, _ := capture.New(dir)
	s := New(cw, log.New(io.Discard, "", 0))
	sink := &fakeSink{}
	s.Sink = sink
	srv := httptest.NewServer(s.Handler())
	defer srv.Close()
	for _, p := range []struct{ path, body string }{
		{"/hook", `{"hook_event_name":"Stop","session_id":"s1"}`},
		{"/statusline", `{"session_id":"s1"}`},
	} {
		resp, err := http.Post(srv.URL+p.path, "application/json", strings.NewReader(p.body))
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
	}
	files, _ := filepath.Glob(filepath.Join(dir, "capture-*.jsonl"))
	b, _ := os.ReadFile(files[0])
	if strings.Count(string(b), "\n") != 2 || len(sink.hooks) != 1 || len(sink.sl) != 1 {
		t.Fatalf("cattura %q, hook %v, status line %v", b, sink.hooks, sink.sl)
	}
}
