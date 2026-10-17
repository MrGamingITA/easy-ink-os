package main

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"calamaio/internal/fakedisplay"
	"calamaio/internal/ota"
	"calamaio/internal/server"
)

// Aggiornamento remoto, registro e crash dall'esterno: servizio vero su porte libere, display finto
// (internal/fakedisplay), `calamaio ota keygen|push`, `calamaio logs` e `calamaio status` veri lanciati
// come processi, cartelle temporanee (mai la config dell'utente).

func otaSvc(t *testing.T) (*svc, string) {
	t.Helper()
	var pkg string
	s := startSvcWith(t, 0, func(dir string, o *server.Options) {
		pkg = filepath.Join(dir, "pacchetto")
		os.MkdirAll(pkg, 0o755)
		elf := []byte("\x7fELF della build 0.7.0")
		os.WriteFile(filepath.Join(pkg, "firmware.bin"), ota.FakeImage("0.7.0", "heltec213", 120_000, elf), 0o644)
		os.WriteFile(filepath.Join(pkg, "firmware.elf"), elf, 0o644)
		os.WriteFile(filepath.Join(pkg, "version"), []byte("0.7.0\n"), 0o644)
		for _, n := range []string{"bootloader.bin", "partitions.bin", "boot_app0.bin"} {
			os.WriteFile(filepath.Join(pkg, n), []byte{0xE9, 1, 2, 3}, 0o644)
		}
		o.FirmwareDir = pkg
		o.OTAKeyPath = filepath.Join(dir, ota.KeyFile)
		data := filepath.Join(dir, "dati")
		o.CrashDir, o.DeviceLogDir, o.ElfDir = filepath.Join(data, "crash"), filepath.Join(data, "logs"), filepath.Join(data, "elf")
		o.Addr2line = "/nessuno/addr2line"
	})
	return s, pkg
}

type cliResult struct {
	out, err string
	code     int
}

// cli lancia questo binario di test come `calamaio args…` con le cartelle e le porte del servizio di prova.
func (s *svc) cli(stdin string, args ...string) cliResult {
	s.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, os.Args[0], args...)
	cmd.Env = append(os.Environ(), "GORACE=atexit_sleep_ms=0", "CALAMAIO_TEST_MAIN=1", "CALAMAIO_CONFIG_DIR="+s.cfgDir,
		"CALAMAIO_DATA_DIR="+filepath.Join(s.cfgDir, "dati"), "CALAMAIO_API_PORT="+strconv.Itoa(s.cfg.APIPort))
	cmd.Stdin = strings.NewReader(stdin)
	var out, errb bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &errb
	_ = cmd.Run()
	return cliResult{out.String(), errb.String(), cmd.ProcessState.ExitCode()}
}

// otaDisplay: display finto abbinato attraverso la API (come in M5), con le opzioni OTA.
func (s *svc) otaDisplay(o fakedisplay.Options, K []byte) *fakedisplay.Display {
	s.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.DialWith(ctx, s.wsURL, board, K, false, o)
	if err != nil {
		s.t.Fatal(err)
	}
	s.t.Cleanup(d.Close)
	await(s.t, d, "welcome")
	if K == nil {
		s.pair(d)
	} else {
		await(s.t, d, "authed")
	}
	return d
}

func (s *svc) apiStatus() map[string]any {
	s.t.Helper()
	r, err := http.Get(s.base + "/api/status")
	if err != nil {
		s.t.Fatal(err)
	}
	defer r.Body.Close()
	var m map[string]any
	_ = json.NewDecoder(r.Body).Decode(&m)
	return m
}

func TestCLIOtaKeygen(t *testing.T) {
	s, _ := otaSvc(t)
	r := s.cli("", "ota", "keygen")
	if r.code != 0 || !strings.Contains(r.out, "Chiave pubblica: ") {
		t.Fatalf("keygen: %d %q %q", r.code, r.out, r.err)
	}
	path := filepath.Join(s.cfgDir, ota.KeyFile)
	if st, err := os.Stat(path); err != nil || st.Mode().Perm() != 0o600 {
		t.Fatalf("chiave: %v", err)
	}
	before, _ := os.ReadFile(path)
	r = s.cli("", "ota", "keygen")
	if r.code == 0 || !strings.Contains(r.err, "--force") {
		t.Fatalf("seconda keygen: %d %q", r.code, r.err)
	}
	if after, _ := os.ReadFile(path); !bytes.Equal(before, after) {
		t.Fatal("chiave sovrascritta senza --force")
	}
	if r = s.cli("", "ota", "keygen", "--force"); r.code != 0 || !strings.Contains(r.out, "riabbini") {
		t.Fatalf("--force: %d %q", r.code, r.err)
	}
	// il servizio la vede subito (senza riavvio)
	waitFor(t, "chiave vista dal servizio", func() bool {
		o, _ := s.apiStatus()["ota"].(map[string]any)
		return o != nil && o["key"] == true
	})
}

func TestCLIOtaPushPackage(t *testing.T) {
	s, pkg := otaSvc(t)
	if r := s.cli("", "ota", "keygen"); r.code != 0 {
		t.Fatal(r.err)
	}
	// prima della chiave nessun aggiornamento: qui c'è
	d := s.otaDisplay(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(false), Batt: ptr(80)}}, nil)
	await(t, d, "ota-key")
	d.SetPower(ptr(80), ptr(false))
	waitFor(t, "status arrivato", func() bool { return s.status().Batt != nil })
	st := s.apiStatus()
	dd := st["displays"].([]any)[0].(map[string]any)
	if ck := dd["otaCheck"].(map[string]any); ck["can"] != true {
		t.Fatalf("otaCheck: %v", ck)
	}

	// la scheda si riavvia e torna con la versione nuova mentre la CLI aspetta
	go func() {
		if _, err := d.Await("ota-reboot", 10*time.Second); err != nil {
			return
		}
		<-d.Done()
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		d2, err := fakedisplay.DialWith(ctx, s.wsURL, board, d.Key(), false, fakedisplay.Options{FW: "0.7.0", OtaPub: d.OtaPub()})
		if err == nil {
			t.Cleanup(d2.Close)
		}
	}()
	r := s.cli("", "ota", "push", "--board", board, "--yes")
	if r.code != 0 || !strings.Contains(r.out, "Aggiorno calamaio-3f2a: 0.6.0 → 0.7.0") ||
		!strings.Contains(r.out, "Aggiornato: calamaio-3f2a ora ha la versione 0.7.0.") || !strings.Contains(r.out, "100%") {
		t.Fatalf("push: %d\n%s\n%s\n%s", r.code, r.out, r.err, s.log)
	}
	t.Logf("calamaio ota push:\n%s", r.out)
	// l'ELF del pacchetto è archiviato per tradurre i crash di questa build
	if l := ota.ListELF(filepath.Join(s.cfgDir, "dati", "elf")); len(l) != 1 || l[0].Version != "0.7.0" {
		t.Fatalf("archivio ELF: %+v", l)
	}
	_ = pkg
	checkSchema(t, d)
}

func TestCLIOtaPushRejected(t *testing.T) {
	s, _ := otaSvc(t)
	if r := s.cli("", "ota", "keygen"); r.code != 0 {
		t.Fatal(r.err)
	}
	d := s.otaDisplay(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(false), Batt: ptr(30)}}, nil)
	await(t, d, "ota-key")
	d.SetPower(ptr(30), ptr(false))
	waitFor(t, "status arrivato", func() bool { return s.status().Batt != nil })
	// la webapp lo dice prima
	dd := s.apiStatus()["displays"].([]any)[0].(map[string]any)
	if ck := dd["otaCheck"].(map[string]any); ck["can"] != false || !strings.Contains(ck["why"].(string), "Batteria al 30%") {
		t.Fatalf("otaCheck: %v", ck)
	}
	// FILE con --build: build del repository finta (versione da model.h, profilo dalla cartella)
	repo := filepath.Join(t.TempDir(), "firmware")
	os.MkdirAll(filepath.Join(repo, "src", "core"), 0o755)
	os.WriteFile(filepath.Join(repo, "src", "core", "model.h"), []byte(`#define CALAMAIO_FW_VERSION "0.7.1"`), 0o644)
	build := filepath.Join(repo, ".pio", "build", "heltec213")
	os.MkdirAll(build, 0o755)
	os.WriteFile(filepath.Join(build, "firmware.bin"), ota.FakeImage("0.7.1", "heltec213", 30_000, nil), 0o644)
	r := s.cli("s\n", "ota", "push", "--build", build)
	if r.code != 1 || !strings.Contains(r.out, "0.6.0 → 0.7.1") || !strings.Contains(r.out, "Procedo?") ||
		!strings.Contains(r.out, "La scheda ha rifiutato l'aggiornamento: batteria al 30%: collega il cavo USB") {
		t.Fatalf("push rifiutato: %d\n%s\n%s", r.code, r.out, r.err)
	}
	// rispondere no non manda niente
	r = s.cli("n\n", "ota", "push", "--build", build)
	if r.code != 1 || !strings.Contains(r.out, "Non mandato.") {
		t.Fatalf("risposta no: %d %s", r.code, r.out)
	}
	// immagine con un'altra versione dentro: rifiutata prima di mandarla
	os.WriteFile(filepath.Join(repo, "src", "core", "model.h"), []byte(`#define CALAMAIO_FW_VERSION "0.8.0"`), 0o644)
	if r = s.cli("", "ota", "push", "--yes", "--build", build); r.code != 1 || !strings.Contains(r.err, "non compare nell'immagine") {
		t.Fatalf("versione stantia: %d %s", r.code, r.err)
	}
}

func TestOtaAPIGuards(t *testing.T) {
	s, _ := otaSvc(t)
	req, _ := http.NewRequest(http.MethodPost, s.base+"/api/displays/"+board+"/ota", strings.NewReader("{}"))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Origin", "http://cattivo.example")
	if r, _ := http.DefaultClient.Do(req); r.StatusCode != http.StatusForbidden {
		t.Fatalf("Origin di un altro sito: %d", r.StatusCode)
	}
	if r, _ := http.Post(s.base+"/api/displays/"+board+"/ota", "text/plain", strings.NewReader("{}")); r.StatusCode != http.StatusUnsupportedMediaType {
		t.Fatalf("text/plain: %d", r.StatusCode)
	}
	if r, _ := http.Post(s.base+"/api/displays/"+board+"/ota/image?version=1&device=x", "application/json", strings.NewReader("{}")); r.StatusCode != http.StatusUnsupportedMediaType {
		t.Fatalf("immagine come JSON: %d", r.StatusCode)
	}
	req, _ = http.NewRequest(http.MethodGet, s.base+"/api/displays/"+board+"/log", nil)
	req.Host = "cattivo.example"
	if r, _ := http.DefaultClient.Do(req); r.StatusCode != http.StatusForbidden {
		t.Fatalf("Host non locale: %d", r.StatusCode)
	}
	if code, m := s.post("/api/displays/"+board+"/ota", map[string]any{}); code != http.StatusConflict {
		t.Fatalf("display non collegato: %d %v", code, m)
	}
}

func TestCLILogsAndCrash(t *testing.T) {
	s, _ := otaSvc(t)
	d := s.otaDisplay(fakedisplay.Options{FW: "0.7.0"}, nil)
	d.LogRing("[100] avvio", "[200] wifi collegato", "[300] PC trovato", "[400] autenticato")
	r := s.cli("", "logs", "--board", board)
	if r.code != 0 || !strings.Contains(r.out, "[100] avvio\n[200] wifi collegato\n[300] PC trovato\n[400] autenticato") ||
		!strings.Contains(r.err, "Salvato in ") {
		t.Fatalf("logs: %d\n%s\n%s", r.code, r.out, r.err)
	}
	files, _ := filepath.Glob(filepath.Join(s.cfgDir, "dati", "logs", board+"-*.log"))
	if len(files) != 1 {
		t.Fatalf("file del registro: %v", files)
	}
	// file scaricabile dalla API
	resp, err := http.Get(s.base + "/api/displays/" + board + "/log/file")
	if err != nil || resp.StatusCode != 200 || !strings.Contains(resp.Header.Get("Content-Disposition"), board) {
		t.Fatalf("scarica: %v %v", err, resp)
	}
	resp.Body.Close()

	d.SendCrash(map[string]any{"fw": "0.7.0", "reason": "LoadProhibited", "pc": "0x42012a3c", "backtrace": []string{"0x42012a39:0x3fcebf00"}})
	waitFor(t, "crash salvato", func() bool { return s.apiStatus()["lastCrash"] != nil })
	r = s.cli("", "status")
	if r.code != 0 || !strings.Contains(r.out, "Ultimo crash: calamaio-3f2a, firmware 0.7.0") || !strings.Contains(r.out, "LoadProhibited") {
		t.Fatalf("status: %d\n%s\n%s", r.code, r.out, r.err)
	}
	t.Logf("calamaio status:\n%s", r.out)
}

func ptr[T any](v T) *T { return &v }
