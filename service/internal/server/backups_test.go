package server

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"calamaio/internal/backup"
)

// Nessun test di questo pacchetto deve toccare le cartelle vere dell'utente (backup, oggi.json…).
func TestMain(m *testing.M) {
	tmp, err := os.MkdirTemp("", "calamaio-server-test-")
	if err != nil {
		panic(err)
	}
	os.Setenv("CALAMAIO_DATA_DIR", filepath.Join(tmp, "data"))
	os.Setenv("CALAMAIO_CONFIG_DIR", filepath.Join(tmp, "config"))
	code := m.Run()
	_ = os.RemoveAll(tmp)
	os.Exit(code)
}

// factorySrc: flash finta di fabbrica con SHA256.txt accanto (sum vuoto = quello giusto).
func factorySrc(t *testing.T, sum string) (backup.FactorySource, []byte) {
	t.Helper()
	dir := t.TempDir()
	data := bytes.Repeat([]byte{0xff}, 128<<10)
	data[0], data[12], data[13] = 0xE9, 9, 0 // bootloader ESP32-S3
	data[4096] = 0x42
	name := "heltec-wireless-paper_aabbccdd48e8_fabbrica.bin"
	if err := os.WriteFile(filepath.Join(dir, name), data, 0o644); err != nil {
		t.Fatal(err)
	}
	if sum == "" {
		h := sha256.Sum256(data)
		sum = hex.EncodeToString(h[:])
	}
	if err := os.WriteFile(filepath.Join(dir, "SHA256.txt"), []byte(sum+"  "+name+"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	return backup.FactorySource{File: filepath.Join(dir, name), MAC: "aa:bb:cc:dd:48:e8",
		Name: "Originale di fabbrica · Heltec Wireless Paper"}, data
}

func startBackups(t *testing.T, dir string, srcs ...backup.FactorySource) (*started, *web) {
	t.Helper()
	s := startOpts(t, false, 0, func(o *Options) {
		o.FirmwareDir = t.TempDir()
		o.BackupDir = dir
		o.FactoryBackups = srcs
	})
	return s, newWeb(t, s)
}

// raw: richiesta con corpo binario e header a coppie; restituisce codice, corpo e header.
func (w *web) raw(method, path, ctype string, body []byte, hdr ...string) (int, []byte, http.Header) {
	w.t.Helper()
	var rd io.Reader
	if body != nil {
		rd = bytes.NewReader(body)
	}
	req, _ := http.NewRequest(method, w.base+path, rd)
	if ctype != "" {
		req.Header.Set("Content-Type", ctype)
	}
	for i := 0; i+1 < len(hdr); i += 2 {
		if hdr[i] == "Host" {
			req.Host = hdr[i+1]
			continue
		}
		req.Header.Set(hdr[i], hdr[i+1])
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		w.t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, b, resp.Header
}

func (w *web) same() []string {
	return []string{"Origin", "http://" + w.host, "Sec-Fetch-Site", "same-origin"}
}

func (w *web) list() []backup.Backup {
	w.t.Helper()
	code, body, _ := w.raw(http.MethodGet, "/api/backups", "", nil)
	var out struct {
		Backups []backup.Backup `json:"backups"`
	}
	if code != 200 || json.Unmarshal(body, &out) != nil || out.Backups == nil {
		w.t.Fatalf("GET /api/backups: %d %s", code, body)
	}
	return out.Backups
}

func TestBackupsAPI(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "backups")
	src, factoryData := factorySrc(t, "")
	_, w := startBackups(t, dir, src)

	// Originale di fabbrica importato all'avvio, protetto.
	list := w.list()
	if len(list) != 1 {
		t.Fatalf("elenco iniziale: %+v", list)
	}
	f := list[0]
	if f.ID != "factory-aabbccdd48e8" || f.Kind != "factory" || !f.Protected || f.Chip != "ESP32-S3" ||
		f.Board != "calamaio-48e8" || f.Size != int64(len(factoryData)) || f.Name != "Originale di fabbrica · Heltec Wireless Paper" {
		t.Fatalf("fabbrica = %+v", f)
	}

	// Upload come lo fa la webapp (api.js uploadBackup).
	data := bytes.Repeat([]byte("calamaio"), 40000)
	sum := sha256.Sum256(data)
	q := "?name=" + "Backup+completo+%C2%B7+11+ott" + "&chip=ESP32-S3&mac=aa:bb:cc:dd:48:e8&board=calamaio-48e8&flashSize=8388608&offset=65536&note=manuale"
	code, body, hd := w.raw(http.MethodPost, "/api/backups"+q, "application/octet-stream", data, w.same()...)
	if code != 201 {
		t.Fatalf("POST: %d %s", code, body)
	}
	var created struct{ Backup backup.Backup }
	if err := json.Unmarshal(body, &created); err != nil {
		t.Fatal(err)
	}
	b := created.Backup
	if b.Name != "Backup completo · 11 ott" || b.Kind != "user" || b.Protected || b.Offset != 65536 || b.FlashSize != 8388608 ||
		b.Length != int64(len(data)) || b.SHA256 != hex.EncodeToString(sum[:]) || b.Note != "manuale" || b.Created == 0 ||
		hd.Get("Location") != "/api/backups/"+b.ID {
		t.Fatalf("creato %+v (Location %q)", b, hd.Get("Location"))
	}
	// Forma JSON esatta: le chiavi della webapp, nient'altro.
	var rawMap map[string]map[string]any
	_ = json.Unmarshal(body, &rawMap)
	keys := []string{"id", "name", "kind", "protected", "board", "chip", "mac", "flashSize", "offset", "length", "size", "sha256", "created", "note"}
	if len(rawMap["backup"]) != len(keys) {
		t.Fatalf("chiavi: %v", rawMap["backup"])
	}
	for _, k := range keys {
		if _, ok := rawMap["backup"][k]; !ok {
			t.Fatalf("manca %s in %v", k, rawMap["backup"])
		}
	}
	if len(w.list()) != 2 {
		t.Fatal("il backup non compare nell'elenco")
	}

	// Download identico, con nome sensato.
	code, got, hd := w.raw(http.MethodGet, "/api/backups/"+b.ID+"/data", "", nil)
	if code != 200 || !bytes.Equal(got, data) || hd.Get("Content-Type") != "application/octet-stream" ||
		hd.Get("X-Calamaio-Sha256") != b.SHA256 || hd.Get("Content-Disposition") != `attachment; filename=Backup-completo-11-ott.bin` {
		t.Fatalf("download: %d, %d byte, header %v", code, len(got), hd)
	}
	code, got, hd = w.raw(http.MethodGet, "/api/backups/"+f.ID+"/data", "", nil)
	if code != 200 || !bytes.Equal(got, factoryData) ||
		hd.Get("Content-Disposition") != `attachment; filename=Originale-di-fabbrica-Heltec-Wireless-Paper.bin` {
		t.Fatalf("download fabbrica: %d %v", code, hd)
	}

	// Permessi su disco.
	if runtime.GOOS != "windows" {
		check := func(p string, want os.FileMode) {
			t.Helper()
			st, err := os.Stat(p)
			if err != nil || st.Mode().Perm() != want {
				t.Errorf("%s: %v %v, atteso %o", p, st.Mode(), err, want)
			}
		}
		check(dir, 0o700)
		check(filepath.Join(dir, b.ID+".bin"), 0o600)
		check(filepath.Join(dir, b.ID+".json"), 0o600)
		check(filepath.Join(dir, f.ID+".bin"), 0o400)
		check(filepath.Join(dir, f.ID+".json"), 0o400)
	}

	// Protetto: 403 anche con l'id «indovinato».
	code, body, _ = w.raw(http.MethodDelete, "/api/backups/"+f.ID, "", nil, w.same()...)
	if code != 403 || !strings.Contains(string(body), "protetto") {
		t.Fatalf("DELETE fabbrica: %d %s", code, body)
	}
	// Path traversal sull'id: mai fuori dalla cartella, mai sul protetto.
	for _, p := range []string{
		"/api/backups/..%2F" + f.ID, "/api/backups/%2e%2e", "/api/backups/" + f.ID + "%2F..%2F" + f.ID,
		"/api/backups/..%2F..%2Fbackups%2F" + f.ID, "/api/backups/" + strings.ToUpper(f.ID), "/api/backups/" + f.ID + ".json",
		"/api/backups/%2Fetc%2Fpasswd",
	} {
		code, body, _ = w.raw(http.MethodDelete, p, "", nil, w.same()...)
		if code != 404 {
			t.Errorf("DELETE %s: %d %s", p, code, body)
		}
		code, _, _ = w.raw(http.MethodGet, strings.Replace(p, "/api/backups/", "/api/backups/", 1)+"/data", "", nil)
		if code != 404 {
			t.Errorf("GET %s/data: %d", p, code)
		}
	}
	if len(w.list()) != 2 {
		t.Fatal("qualcosa è sparito")
	}

	// CSRF e Host.
	for _, hdr := range [][]string{
		{"Origin", "http://evil.example"},
		{"Sec-Fetch-Site", "cross-site"},
		{"Origin", "null"},
	} {
		if code, _, _ = w.raw(http.MethodPost, "/api/backups?name=x&chip=ESP32", "application/octet-stream", []byte{1}, hdr...); code != 403 {
			t.Errorf("POST con %v: %d", hdr, code)
		}
		if code, _, _ = w.raw(http.MethodDelete, "/api/backups/"+b.ID, "", nil, hdr...); code != 403 {
			t.Errorf("DELETE con %v: %d", hdr, code)
		}
	}
	if code, _, _ = w.raw(http.MethodDelete, "/api/backups/"+b.ID, "", nil); code != 403 {
		t.Errorf("DELETE senza Origin né Sec-Fetch-Site: %d", code)
	}
	evil := append(w.same(), "Host", "evil.example:8765")
	for _, c := range []struct{ m, p, ct string }{
		{http.MethodGet, "/api/backups", ""}, {http.MethodGet, "/api/backups/" + b.ID + "/data", ""},
		{http.MethodPost, "/api/backups?name=x&chip=ESP32", "application/octet-stream"}, {http.MethodDelete, "/api/backups/" + b.ID, ""},
	} {
		var bd []byte
		if c.m == http.MethodPost {
			bd = []byte{1}
		}
		if code, _, _ = w.raw(c.m, c.p, c.ct, bd, evil...); code != 403 {
			t.Errorf("%s %s con Host non locale: %d", c.m, c.p, code)
		}
	}

	// Content-Type e parametri.
	for _, c := range []struct {
		q, ct string
		body  []byte
		want  int
	}{
		{"?name=x&chip=ESP32", "", []byte{1}, 415},
		{"?name=x&chip=ESP32", "application/json", []byte("{}"), 415},
		{"?name=x&chip=ESP32", "text/plain", []byte{1}, 415},
		{"?chip=ESP32", "application/octet-stream", []byte{1}, 400},
		{"?name=x", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&mac=zz", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&offset=-1", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&flashSize=abc", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&flashSize=4&offset=4", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&kind=factory", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&protected=true", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&id=factory-aabbccdd48e8", "application/octet-stream", []byte{1}, 400},
		{"?name=x&name=y&chip=ESP32", "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32", "application/octet-stream", []byte{}, 400},
		{"?name=x&chip=ESP32&sha256=" + strings.Repeat("ab", 32), "application/octet-stream", []byte{1}, 400},
		{"?name=x&chip=ESP32&sha256=" + hex.EncodeToString(sha256.New().Sum(nil)), "application/octet-stream", []byte{1}, 400},
	} {
		code, body, _ = w.raw(http.MethodPost, "/api/backups"+c.q, c.ct, c.body, w.same()...)
		if code != c.want {
			t.Errorf("POST %s (%s): %d %s, atteso %d", c.q, c.ct, code, body, c.want)
		}
		var e map[string]string
		if json.Unmarshal(body, &e) != nil || e["error"] == "" {
			t.Errorf("POST %s: errore senza {error}: %s", c.q, body)
		}
	}
	// sha256 giusto dal client: accettato.
	one := []byte{1}
	s1 := sha256.Sum256(one)
	code, body, _ = w.raw(http.MethodPost, "/api/backups?name=x&chip=ESP32&sha256="+hex.EncodeToString(s1[:]), "application/octet-stream", one, w.same()...)
	if code != 201 {
		t.Fatalf("sha256 giusto: %d %s", code, body)
	}
	var small struct{ Backup backup.Backup }
	_ = json.Unmarshal(body, &small)

	// 413 oltre 16 MiB (al limite esatto passa).
	big := make([]byte, backup.MaxSize+1)
	code, body, _ = w.raw(http.MethodPost, "/api/backups?name=grande&chip=ESP32", "application/octet-stream", big, w.same()...)
	if code != 413 {
		t.Fatalf("17 MiB: %d %s", code, body)
	}
	code, body, _ = w.raw(http.MethodPost, "/api/backups?name=limite&chip=ESP32", "application/octet-stream", big[:backup.MaxSize], w.same()...)
	if code != 201 {
		t.Fatalf("16 MiB esatti: %d %s", code, body)
	}
	var limit struct{ Backup backup.Backup }
	_ = json.Unmarshal(body, &limit)

	// Delete dei backup dell'utente: 204, poi 404.
	for _, id := range []string{b.ID, small.Backup.ID, limit.Backup.ID} {
		if code, body, _ = w.raw(http.MethodDelete, "/api/backups/"+id, "", nil, w.same()...); code != 204 || len(body) != 0 {
			t.Fatalf("DELETE %s: %d %s", id, code, body)
		}
	}
	if code, _, _ = w.raw(http.MethodDelete, "/api/backups/"+b.ID, "", nil, w.same()...); code != 404 {
		t.Fatalf("seconda DELETE: %d", code)
	}
	if code, _, _ = w.raw(http.MethodGet, "/api/backups/"+b.ID+"/data", "", nil); code != 404 {
		t.Fatalf("GET dopo DELETE: %d", code)
	}
	list = w.list()
	if len(list) != 1 || list[0].ID != f.ID {
		t.Fatalf("alla fine: %+v", list)
	}
	ents, _ := os.ReadDir(dir)
	if len(ents) != 2 {
		names := []string{}
		for _, e := range ents {
			names = append(names, e.Name())
		}
		t.Fatalf("file rimasti: %v", names)
	}
	// Il file nel «repository» è ancora lì, intatto.
	if got, err := os.ReadFile(src.File); err != nil || !bytes.Equal(got, factoryData) {
		t.Fatal("sorgente della fabbrica toccata")
	}
}

// Riavvio: l'importazione non duplica; uno SHA sbagliato non importa niente.
func TestBackupsFactoryImportOnStart(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "backups")
	src, _ := factorySrc(t, "")
	s1, w1 := startBackups(t, dir, src)
	if len(w1.list()) != 1 {
		t.Fatal("primo avvio: niente fabbrica")
	}
	s1.cancel()
	select {
	case err := <-s1.done:
		s1.done <- err // lo riprende il Cleanup di startOpts
	case <-time.After(5 * time.Second):
		t.Fatal("il servizio non si ferma")
	}
	_, w2 := startBackups(t, dir, src)
	if l := w2.list(); len(l) != 1 || l[0].Kind != "factory" {
		t.Fatalf("secondo avvio: %+v", l)
	}

	bad, _ := factorySrc(t, strings.Repeat("0", 64))
	_, w3 := startBackups(t, filepath.Join(t.TempDir(), "altro"), bad)
	if l := w3.list(); len(l) != 0 {
		t.Fatalf("SHA sbagliato: importato %+v", l)
	}
}

// Senza archivio (cartella impossibile) il servizio parte e le rotte rispondono 404 come «non gestiti».
func TestBackupsUnavailable(t *testing.T) {
	file := filepath.Join(t.TempDir(), "file")
	_ = os.WriteFile(file, []byte("x"), 0o600)
	_, w := startBackups(t, filepath.Join(file, "backups"))
	if code, _, _ := w.raw(http.MethodGet, "/api/backups", "", nil); code != 404 {
		t.Fatalf("GET senza archivio: %d", code)
	}
}
