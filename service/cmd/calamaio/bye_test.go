package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"

	"github.com/santhosh-tekuri/jsonschema/v6"

	"calamaio/internal/config"
	"calamaio/internal/fakedisplay"
	"calamaio/internal/m5"
)

func freePort(t *testing.T) int {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	return ln.Addr().(*net.TCPAddr).Port
}

// M6, dall'esterno: `calamaio serve` vero (questo binario di test come processo, porte libere, config e
// dati in cartelle temporanee), display finto abbinato con una richiesta di permesso aperta, poi
// SIGTERM al processo. Il display deve ricevere prima perm-close «service», poi bye con un riepilogo
// valido contro lo schema; il servizio esce con 0 e il hook in attesa esce senza decisione.
func TestByeOnSIGTERM(t *testing.T) {
	if testing.Short() {
		t.Skip("processo vero")
	}
	dir := t.TempDir()
	cfgDir, dataDir := filepath.Join(dir, "cfg"), filepath.Join(dir, "dati")
	apiPort, wsPort := freePort(t), freePort(t)
	cfg := map[string]any{"api_port": apiPort, "ws_port": wsPort, "tz": "CET-1CEST,M3.5.0,M10.5.0/3",
		// niente originale di fabbrica dal repository (8 MB copiati nella cartella temporanea)
		"factory_backups": []map[string]any{{"file": filepath.Join(dir, "nessuno.bin"), "mac": "aa:bb:cc:dd:ee:ff"}}}
	_ = os.MkdirAll(cfgDir, 0o700)
	b, _ := json.Marshal(cfg)
	if err := os.WriteFile(filepath.Join(cfgDir, "config.json"), b, 0o600); err != nil {
		t.Fatal(err)
	}
	K := m5.Random(32)
	ks := &config.KeyStore{Path: filepath.Join(cfgDir, "config.json")}
	if err := ks.Put(board, K, time.Now()); err != nil {
		t.Fatal(err)
	}
	env := append(os.Environ(), "GORACE=atexit_sleep_ms=0", "CALAMAIO_TEST_MAIN=1", "CALAMAIO_CONFIG_DIR="+cfgDir,
		"CALAMAIO_DATA_DIR="+dataDir, "CALAMAIO_API_PORT="+strconv.Itoa(apiPort), "CALAMAIO_WS_PORT="+strconv.Itoa(wsPort))

	logb := &syncBuf{}
	srv := exec.Command(os.Args[0], "serve", "--no-mdns")
	srv.Env = env
	srv.Stdout, srv.Stderr = logb, logb
	if err := srv.Start(); err != nil {
		t.Fatal(err)
	}
	exited := make(chan struct{})
	go func() { _ = srv.Wait(); close(exited) }()
	t.Cleanup(func() {
		_ = srv.Process.Kill()
		<-exited
		if t.Failed() {
			t.Logf("log del servizio:\n%s", logb)
		}
	})
	base := fmt.Sprintf("http://127.0.0.1:%d", apiPort)
	waitFor(t, "servizio in ascolto", func() bool {
		resp, err := http.Get(base + "/api/status")
		if err != nil {
			return false
		}
		resp.Body.Close()
		return resp.StatusCode == 200
	})
	post := func(body string) {
		t.Helper()
		resp, err := http.Post(base+"/hook", "application/json", strings.NewReader(body))
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
	}
	// due sessioni: una finita con un riepilogo, una che chiederà un permesso
	other := `"session_id":"aaaa0000-1111-2222-3333-444455556666","transcript_path":"/nessuno/t.jsonl","cwd":"/home/utente/docs-site"`
	post(`{` + other + `,"hook_event_name":"SessionStart","source":"startup"}`)
	post(`{` + other + `,"hook_event_name":"UserPromptSubmit","prompt":"aggiorna la guida"}`)
	post(`{` + other + `,"hook_event_name":"Stop","last_assistant_message":"Guida aggiornata e 4 link corretti. Altro testo."}`)
	post(ev("SessionStart", `"source":"startup"`))

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.Dial(ctx, fmt.Sprintf("ws://127.0.0.1:%d/ws", wsPort), board, K, true)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(d.Close)
	await(t, d, "authed")

	// hook --wait vero, come lo lancia Claude Code
	hook := exec.Command(os.Args[0], "hook", "--wait")
	hook.Env = env
	hook.Stdin = strings.NewReader(permPayload)
	hookOut := &syncBuf{}
	hook.Stdout = hookOut
	if err := hook.Start(); err != nil {
		t.Fatal(err)
	}
	hookDone := make(chan struct{})
	go func() { _ = hook.Wait(); close(hookDone) }()
	t.Cleanup(func() { _ = hook.Process.Kill(); <-hookDone })
	req := await(t, d, "perm-request")

	if err := srv.Process.Signal(syscall.SIGTERM); err != nil {
		t.Fatal(err)
	}
	c := await(t, d, "perm-close")
	if c.Text != "service" {
		t.Fatalf("perm-close %q", c.Text)
	}
	bye := await(t, d, "bye")
	select {
	case <-exited:
	case <-time.After(10 * time.Second):
		t.Fatal("il servizio non è uscito dopo SIGTERM")
	}
	if code := srv.ProcessState.ExitCode(); code != 0 {
		t.Fatalf("uscita %d dopo SIGTERM (launchd lo riavvierebbe)", code)
	}
	select {
	case <-hookDone:
	case <-time.After(3 * time.Second):
		t.Fatal("il hook non è uscito")
	}
	if out := hookOut.String(); out != "" {
		t.Fatalf("il hook ha deciso allo spegnimento: %q", out)
	}

	// ordine: perm-close della richiesta prima di bye, nessuno state dopo bye
	var order []string
	for _, e := range d.Log() {
		if e.Msg != nil {
			order = append(order, e.Type)
		}
	}
	iClose, iBye := -1, -1
	for i, typ := range order {
		switch typ {
		case "perm-close":
			iClose = i
		case "bye":
			iBye = i
		}
	}
	if iClose < 0 || iBye < 0 || iClose > iBye || iBye != len(order)-1 {
		t.Fatalf("ordine dei messaggi: %v", order)
	}
	if req.Req == nil || c.Msg["id"] != req.Req.ID {
		t.Fatalf("perm-close di un'altra richiesta: %v / %+v", c.Msg, req.Req)
	}

	// riepilogo valido contro lo schema e con i dati giusti
	raw, _ := json.Marshal(bye.Msg)
	sch := compileSchema(t)
	v, _ := jsonschema.UnmarshalJSON(bytes.NewReader(raw))
	if err := sch.Validate(v); err != nil {
		t.Fatalf("bye non rispetta lo schema: %v\n%s", err, raw)
	}
	if len(raw) > 4096 {
		t.Fatalf("bye di %d byte", len(raw))
	}
	var m struct {
		Summary struct {
			Off           int64 `json:"off"`
			Day           string
			SessionsToday int `json:"sessionsToday"`
			Open, Done    int
			Waiting       int
			Sessions      []struct{ Name, State string }
			Last          *struct{ Name, Summary string }
		}
	}
	_ = json.Unmarshal(raw, &m)
	s := m.Summary
	if off := time.UnixMilli(s.Off); time.Since(off) > 30*time.Second || time.Until(off) > time.Second {
		t.Fatalf("off %v", off)
	}
	if s.Day != time.Now().Format("2006-01-02") || s.SessionsToday != 2 || s.Open != 2 || s.Done != 1 || s.Waiting != 1 || len(s.Sessions) != 2 {
		t.Fatalf("riepilogo: %s", raw)
	}
	if s.Last == nil || s.Last.Name != "docs-site" || !strings.HasPrefix(s.Last.Summary, "Guida aggiornata") {
		t.Fatalf("ultimo messaggio: %s", raw)
	}
	if !strings.Contains(logb.String(), "bye mandato a 1 display su 1") {
		t.Fatalf("log senza il bye:\n%s", logb)
	}
	t.Logf("bye ricevuto (%d byte): %s", len(raw), raw)
}

// SIGINT (Ctrl-C) e SIGHUP (terminale chiuso) sono spegnimenti puliti come SIGTERM: bye e uscita 0.
func TestByeOnSIGINTAndSIGHUP(t *testing.T) {
	if testing.Short() {
		t.Skip("processo vero")
	}
	for _, sig := range []syscall.Signal{syscall.SIGINT, syscall.SIGHUP} {
		t.Run(sig.String(), func(t *testing.T) {
			dir := t.TempDir()
			apiPort, wsPort := freePort(t), freePort(t)
			cfgDir := filepath.Join(dir, "cfg")
			_ = os.MkdirAll(cfgDir, 0o700)
			_ = os.WriteFile(filepath.Join(cfgDir, "config.json"), []byte(`{"factory_backups":[{"file":"`+filepath.Join(dir, "x.bin")+`","mac":"aa:bb:cc:dd:ee:ff"}]}`), 0o600)
			logb := &syncBuf{}
			srv := exec.Command(os.Args[0], "serve", "--no-mdns")
			srv.Env = append(os.Environ(), "GORACE=atexit_sleep_ms=0", "CALAMAIO_TEST_MAIN=1", "CALAMAIO_CONFIG_DIR="+cfgDir,
				"CALAMAIO_DATA_DIR="+filepath.Join(dir, "dati"), "CALAMAIO_API_PORT="+strconv.Itoa(apiPort), "CALAMAIO_WS_PORT="+strconv.Itoa(wsPort))
			srv.Stdout, srv.Stderr = logb, logb
			if err := srv.Start(); err != nil {
				t.Fatal(err)
			}
			exited := make(chan struct{})
			go func() { _ = srv.Wait(); close(exited) }()
			t.Cleanup(func() { _ = srv.Process.Kill(); <-exited })
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			var d *fakedisplay.Display
			waitFor(t, "servizio in ascolto", func() bool {
				var err error
				d, err = fakedisplay.Dial(ctx, fmt.Sprintf("ws://127.0.0.1:%d/ws", wsPort), board, nil, false)
				return err == nil
			})
			t.Cleanup(d.Close)
			await(t, d, "welcome")
			if err := srv.Process.Signal(sig); err != nil {
				t.Fatal(err)
			}
			await(t, d, "bye")
			select {
			case <-exited:
			case <-time.After(10 * time.Second):
				t.Fatal("il servizio non è uscito")
			}
			if code := srv.ProcessState.ExitCode(); code != 0 {
				t.Fatalf("uscita %d\n%s", code, logb)
			}
		})
	}
}
