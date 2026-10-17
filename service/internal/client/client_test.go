package client

import (
	"bytes"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os/exec"
	"strconv"
	"strings"
	"testing"
	"time"

	"calamaio/internal/config"
)

func cfgFor(t *testing.T, srv *httptest.Server) config.Config {
	_, p, _ := net.SplitHostPort(strings.TrimPrefix(srv.URL, "http://"))
	n, _ := strconv.Atoi(p)
	c := config.Default()
	c.APIPort = n
	return c
}

func freePortConfig(t *testing.T) config.Config {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := ln.Addr().(*net.TCPAddr).Port
	ln.Close() // porta chiusa: nessuno ascolta
	c := config.Default()
	c.APIPort = port
	return c
}

func TestHookSilentWhenServiceDown(t *testing.T) {
	var out bytes.Buffer
	start := time.Now()
	Hook(freePortConfig(t), strings.NewReader(`{"hook_event_name":"Stop"}`), &out, true)
	if out.Len() != 0 {
		t.Fatalf("output inatteso: %q", out.String())
	}
	if time.Since(start) > 3*time.Second {
		t.Fatal("troppo lento con il servizio spento")
	}
}

func TestHookForwardsAndPrintsDecisionOnlyWithWait(t *testing.T) {
	var got []byte
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		got, _ = io.ReadAll(r.Body)
		io.WriteString(w, `{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"deny"}}}`)
	}))
	defer srv.Close()
	c := cfgFor(t, srv)

	var out bytes.Buffer
	Hook(c, strings.NewReader(`{"a":1}`), &out, false)
	if string(got) != `{"a":1}` {
		t.Fatalf("corpo inoltrato = %q", got)
	}
	if out.Len() != 0 {
		t.Fatalf("senza --wait non deve stampare nulla, stampato %q", out.String())
	}
	Hook(c, strings.NewReader(`{"a":1}`), &out, true)
	if !strings.Contains(out.String(), `"behavior":"deny"`) {
		t.Fatalf("decisione non stampata: %q", out.String())
	}
}

func TestHookIgnoresNonJSONReply(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "ok")
	}))
	defer srv.Close()
	var out bytes.Buffer
	Hook(cfgFor(t, srv), strings.NewReader(`{}`), &out, true)
	if out.Len() != 0 {
		t.Fatalf("output inatteso: %q", out.String())
	}
}

func TestStatusLineFastWhenServiceSlow(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(2 * time.Second)
	}))
	defer srv.Close()
	var out bytes.Buffer
	start := time.Now()
	StatusLine(cfgFor(t, srv), strings.NewReader(`{"session_name":"api","context_window":{"used_percentage":42},"rate_limits":{"five_hour":{"used_percentage":23.5}}}`), &out)
	if d := time.Since(start); d > 400*time.Millisecond {
		t.Fatalf("status line lenta: %v", d)
	}
	if out.String() != "api · ctx 42% · 5h 24%\n" {
		t.Fatalf("riga = %q", out.String())
	}
}

func TestSoberLineFallbacks(t *testing.T) {
	cases := map[string]string{
		`{"workspace":{"current_dir":"/home/u/progetto"},"context_window":{"used_percentage":null}}`: "progetto\n",
		`{}`:       "Calamaio\n",
		`non json`: "Calamaio\n",
	}
	for in, want := range cases {
		if got := SoberLine([]byte(in)); got != want {
			t.Errorf("SoberLine(%s) = %q, atteso %q", in, got, want)
		}
	}
}

func TestStatusLineRunsPrevious(t *testing.T) {
	if _, err := exec.LookPath("sh"); err != nil {
		t.Skip("sh non disponibile")
	}
	c := freePortConfig(t)
	c.PreviousStatusLine = &config.StatusLine{Type: "command", Command: "cat"}
	var out bytes.Buffer
	StatusLine(c, strings.NewReader(`{"x":1}`), &out)
	if out.String() != `{"x":1}` {
		t.Fatalf("la status line precedente non ha ricevuto lo stdin: %q", out.String())
	}
}
