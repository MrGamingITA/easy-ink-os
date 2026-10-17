package display

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/diag"
	"calamaio/internal/fakedisplay"
	"calamaio/internal/ota"
)

// Aggiornamento remoto con un display finto che fa la parte della scheda (internal/fakedisplay):
// offerta, chiave, firma, download, stati, rifiuti, token scaduto/riusato/IP sbagliato, annullamento,
// riavvio con la versione nuova o ritorno alla precedente.

const otaBoard = "calamaio-3f2a"

func ptr[T any](v T) *T { return &v }

type otaRig struct {
	*rig
	key  *ota.Key
	dir  string
	logs *diag.LogFiles
	cr   *diag.CrashStore
}

func newOtaRig(t *testing.T, tune func(*Hub)) *otaRig {
	t.Helper()
	dir := t.TempDir()
	key, err := ota.Generate(filepath.Join(dir, ota.KeyFile), false, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	o := &otaRig{key: key, dir: dir, logs: &diag.LogFiles{Dir: filepath.Join(dir, "logs")},
		cr: &diag.CrashStore{Dir: filepath.Join(dir, "crash"), Addr2line: "/nessuno"}}
	o.rig = newRig(t, func(h *Hub) {
		h.OTAKey = &ota.KeyCache{Path: filepath.Join(dir, ota.KeyFile)}
		h.LogFiles = o.logs
		h.Crashes = o.cr
		h.OfferTimeout = 2 * time.Second
		h.StallTimeout = 2 * time.Second
		h.RebootWait = 3 * time.Second
		h.LogQuiet = 300 * time.Millisecond
		if tune != nil {
			tune(h)
		}
	})
	o.pairKey(otaBoard, testKey)
	return o
}

// display: scheda finta abbinata (testKey) e autenticata, che ha ricevuto ota-key.
func (o *otaRig) display(opt fakedisplay.Options) *fakedisplay.Display {
	o.t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	d, err := fakedisplay.DialWith(ctx, o.url, otaBoard, testKey, false, opt)
	if err != nil {
		o.t.Fatal(err)
	}
	o.t.Cleanup(d.Close)
	if _, err := d.Await("authed", 3*time.Second); err != nil {
		o.t.Fatal(err)
	}
	return d
}

func awaitEv(t *testing.T, d *fakedisplay.Display, typ string) fakedisplay.Event {
	t.Helper()
	e, err := d.Await(typ, 4*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	return e
}

func (o *otaRig) waitStage(stage string) *OtaView {
	o.t.Helper()
	var v *OtaView
	deadline := time.Now().Add(5 * time.Second)
	for {
		v = o.hub.OtaOf(otaBoard)
		if v != nil && v.Stage == stage {
			return v
		}
		if time.Now().After(deadline) {
			o.t.Fatalf("fase %q non raggiunta: %+v\n%s", stage, v, o.log.String())
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func image(version string, size int) OTAImage {
	return OTAImage{Bin: ota.FakeImage(version, "heltec213", size, nil), Version: version, Device: "heltec213",
		Built: 1791748800000, Source: "test"}
}

func TestOTAFullFlow(t *testing.T) {
	o := newOtaRig(t, nil)
	d := o.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true)}})
	awaitEv(t, d, "ota-key")
	if string(d.OtaPub()) != string(o.key.Pub) {
		t.Fatal("la scheda non ha la chiave pubblica giusta")
	}
	img := image("0.7.0", 300_000)
	v, err := o.hub.StartOTA(otaBoard, img)
	if err != nil {
		t.Fatal(err)
	}
	if v.From != "0.6.0" || v.To != "0.7.0" || v.Stage != OtaOffer || !v.Cancelable {
		t.Fatalf("%+v", v)
	}
	// un solo aggiornamento per scheda
	if _, err := o.hub.StartOTA(otaBoard, img); !errors.Is(err, ErrOTABusy) {
		t.Fatalf("secondo aggiornamento: %v", err)
	}
	off := awaitEv(t, d, "ota-offer")
	b, _ := json.Marshal(off.Msg)
	validate(t, compileSchema(t), "ota-offer", b)
	awaitEv(t, d, "ota-reboot")
	if string(d.Image()) != string(img.Bin) {
		t.Fatal("immagine scaricata diversa")
	}
	o.waitStage(OtaReboot)
	if v := o.hub.OtaOf(otaBoard); v.Done != int64(len(img.Bin)) || v.Sent != int64(len(img.Bin)) {
		t.Fatalf("avanzamento %+v", v)
	}
	<-d.Done()
	// la scheda torna con la versione nuova e si autentica: fatto
	d2 := o.display(fakedisplay.Options{FW: "0.7.0", OtaPub: d.OtaPub(), OTA: fakedisplay.OTAOptions{USB: ptr(true)}})
	v = o.waitStage(OtaDone)
	if v.Active || v.Error != "" || v.Ended == 0 {
		t.Fatalf("%+v", v)
	}
	_ = d2
	for _, want := range []string{"offerta a calamaio-3f2a (0.6.0 → 0.7.0", "download completo (300000 byte)",
		"ripartita con la versione 0.7.0 e si è autenticata"} {
		if !strings.Contains(o.log.String(), want) {
			t.Errorf("log senza %q:\n%s", want, o.log.String())
		}
	}
	// il token è monouso: dopo il download non vale più (410)
	tok := off.Msg["url"].(string)
	resp, err := http.Get(tok)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode == http.StatusOK {
		t.Fatalf("token riusato: HTTP %d", resp.StatusCode)
	}
}

func TestOTARollback(t *testing.T) {
	o := newOtaRig(t, nil)
	d := o.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true)}})
	awaitEv(t, d, "ota-key")
	if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 50_000)); err != nil {
		t.Fatal(err)
	}
	awaitEv(t, d, "ota-reboot")
	<-d.Done()
	// la nuova non parte: la scheda torna con la vecchia
	d2 := o.display(fakedisplay.Options{FW: "0.6.0", OtaPub: d.OtaPub()})
	v := o.waitStage(OtaError)
	if !strings.Contains(v.Error, "versione 0.6.0") {
		t.Fatalf("%+v", v)
	}
	// e lo conferma con ota-status errore (dopo il riavvio): il motivo della scheda vince
	d2.SendOtaStatus(v.ID, "errore", 0, 0, "la nuova versione non si è avviata: tornato alla precedente")
	waitFor(t, time.Second, func() bool { return strings.Contains(o.hub.OtaOf(otaBoard).Error, "tornato alla precedente") })
}

func TestOTARejections(t *testing.T) {
	t.Run("batteria", func(t *testing.T) {
		o := newOtaRig(t, nil)
		d := o.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{Batt: ptr(30), USB: ptr(false)}})
		awaitEv(t, d, "ota-key")
		if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 10_000)); err != nil {
			t.Fatal(err)
		}
		e := awaitEv(t, d, "ota-rejected")
		v := o.waitStage(OtaRejected)
		if v.Error != e.Text || !strings.Contains(v.Error, "batteria al 30%") {
			t.Fatalf("%+v", v)
		}
	})
	t.Run("chiave diversa da quella salvata", func(t *testing.T) {
		o := newOtaRig(t, nil)
		other, _ := ota.Generate(filepath.Join(t.TempDir(), "k"), false, time.Now())
		d := o.display(fakedisplay.Options{FW: "0.6.0", OtaPub: other.Pub, OTA: fakedisplay.OTAOptions{USB: ptr(true)}})
		if e := awaitEv(t, d, "rejected"); !strings.Contains(e.Text, "nuovo abbinamento") {
			t.Fatal(e.Text)
		}
		if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 10_000)); err != nil {
			t.Fatal(err)
		}
		if v := o.waitStage(OtaRejected); v.Error != "firma non valida" {
			t.Fatalf("%+v", v)
		}
	})
	t.Run("firma manomessa", func(t *testing.T) {
		o := newOtaRig(t, nil)
		d := o.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true), Manual: true}})
		awaitEv(t, d, "ota-key")
		if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 10_000)); err != nil {
			t.Fatal(err)
		}
		off := awaitEv(t, d, "ota-offer")
		for _, mut := range []func(m map[string]any){
			func(m map[string]any) { m["manifest"].(map[string]any)["version"] = "9.9.9" },                // firma
			func(m map[string]any) { m["manifest"].(map[string]any)["sha256"] = strings.Repeat("0", 64) }, // mac
			func(m map[string]any) { m["manifest"].(map[string]any)["board"] = "calamaio-0000" },          // firma
		} {
			b, _ := json.Marshal(off.Msg)
			var m map[string]any
			_ = json.Unmarshal(b, &m)
			mut(m)
			b, _ = json.Marshal(m)
			d.Deliver(string(b))
			e := awaitEv(t, d, "ota-rejected")
			if !strings.Contains(e.Text, "firma non valida") && !strings.Contains(e.Text, "mac sbagliato") {
				t.Fatalf("offerta manomessa accettata: %s", e.Text)
			}
		}
	})
	t.Run("profilo sbagliato e permesso aperto", func(t *testing.T) {
		o := newOtaRig(t, nil)
		d := o.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true)}})
		awaitEv(t, d, "ota-key")
		img := image("0.7.0", 10_000)
		img.Device = "altra290"
		var blk *BlockedError
		if _, err := o.hub.StartOTA(otaBoard, img); !errors.As(err, &blk) {
			t.Fatalf("profilo sbagliato: %v", err)
		}
		d.SetPermOn(true)
		waitFor(t, 2*time.Second, func() bool { return o.hub.Status()[0].Perms })
		if _, ok := o.hub.OpenPermission(PermQuestion{Session: "s1", Name: "web", Tool: "Bash", Arg: "ls"}); !ok {
			t.Fatal("permesso non aperto")
		}
		awaitEv(t, d, "perm-request")
		if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 10_000)); !errors.Is(err, ErrOTAPerm) {
			t.Fatalf("con un permesso aperto: %v", err)
		}
		if o.hub.Status()[0].OpenPerms != 1 {
			t.Fatal("openPerms")
		}
	})
	t.Run("senza chiave e non autenticato", func(t *testing.T) {
		o := newOtaRig(t, nil)
		os.Remove(filepath.Join(o.dir, ota.KeyFile))
		o.display(fakedisplay.Options{FW: "0.6.0"})
		if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 10_000)); !errors.Is(err, ota.ErrNoKey) {
			t.Fatalf("senza chiave: %v", err)
		}
		if _, err := o.hub.StartOTA("calamaio-0000", image("0.7.0", 10_000)); err == nil {
			t.Fatal("scheda non collegata")
		}
	})
	t.Run("firmware vecchio che non risponde", func(t *testing.T) {
		o := newOtaRig(t, func(h *Hub) { h.OfferTimeout = 300 * time.Millisecond })
		c := authedDisplay(t, o.rig, otaBoard) // client grezzo: ignora l'offerta come un firmware 0.6.0
		c.nextOf("ota-key", 2*time.Second)
		if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 10_000)); err != nil {
			t.Fatal(err)
		}
		if v := o.waitStage(OtaError); !strings.Contains(v.Error, "non ha risposto all'offerta") {
			t.Fatalf("%+v", v)
		}
	})
}

func TestOTAToken(t *testing.T) {
	o := newOtaRig(t, func(h *Hub) { h.TokenTTL = 400 * time.Millisecond })
	d := o.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true), Manual: true}})
	awaitEv(t, d, "ota-key")
	img := image("0.7.0", 20_000)
	if _, err := o.hub.StartOTA(otaBoard, img); err != nil {
		t.Fatal(err)
	}
	off := awaitEv(t, d, "ota-offer")
	url := off.Msg["url"].(string)
	tok := url[strings.LastIndex(url, "/")+1:]

	// IP sbagliato: 403, e il token resta buono per la scheda
	req := httptest.NewRequest(http.MethodGet, "/ota/"+tok, nil)
	req.RemoteAddr = "10.0.0.99:4321"
	req.SetPathValue("token", tok)
	rec := httptest.NewRecorder()
	o.hub.ServeOTA(rec, req)
	if rec.Code != http.StatusForbidden {
		t.Fatalf("IP sbagliato: %d", rec.Code)
	}
	// token sconosciuto: 404
	if r, _ := http.Get(strings.Replace(url, tok, strings.Repeat("a", 32), 1)); r.StatusCode != http.StatusNotFound {
		t.Fatalf("token sconosciuto: %d", r.StatusCode)
	}
	// prima richiesta della scheda: 200 con Content-Length giusto
	r, err := http.Get(url)
	if err != nil {
		t.Fatal(err)
	}
	if r.StatusCode != 200 || r.ContentLength != int64(len(img.Bin)) || r.Header.Get("X-Calamaio-Sha256") == "" {
		t.Fatalf("download: %d, %d byte", r.StatusCode, r.ContentLength)
	}
	r.Body.Close()
	// riusato: 410
	if r, _ := http.Get(url); r.StatusCode != http.StatusGone {
		t.Fatalf("token riusato: %d", r.StatusCode)
	}

	// scaduto: un'altra offerta, aspettata oltre la validità
	o2 := newOtaRig(t, func(h *Hub) { h.TokenTTL = 200 * time.Millisecond })
	d2 := o2.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true), Manual: true}})
	awaitEv(t, d2, "ota-key")
	if _, err := o2.hub.StartOTA(otaBoard, img); err != nil {
		t.Fatal(err)
	}
	off2 := awaitEv(t, d2, "ota-offer")
	time.Sleep(300 * time.Millisecond)
	var man ota.Manifest
	b, _ := json.Marshal(off2.Msg["manifest"])
	_ = json.Unmarshal(b, &man)
	if err := d2.Download(off2.Msg["id"].(string), off2.Msg["url"].(string), man); err == nil || !strings.Contains(err.Error(), "410") {
		t.Fatalf("token scaduto: %v", err)
	}
	if v := o2.waitStage(OtaError); !strings.Contains(v.Error, "410") {
		t.Fatalf("%+v", v)
	}
}

func TestOTACancel(t *testing.T) {
	o := newOtaRig(t, nil)
	d := o.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true), ChunkDelay: 30 * time.Millisecond}})
	awaitEv(t, d, "ota-key")
	if _, err := o.hub.StartOTA(otaBoard, image("0.7.0", 1_000_000)); err != nil {
		t.Fatal(err)
	}
	o.waitStage(OtaReceive)
	v, err := o.hub.CancelOTA(otaBoard)
	if err != nil || v.Stage != OtaCanceled {
		t.Fatalf("%v %+v", err, v)
	}
	awaitEv(t, d, "ota-cancel")
	awaitEv(t, d, "ota-error")
	if d.Image() != nil {
		t.Fatal("immagine scritta dopo l'annullamento")
	}
	if _, err := o.hub.CancelOTA(otaBoard); !errors.Is(err, ErrNoOTA) {
		t.Fatalf("niente da annullare: %v", err)
	}

	// in «scrivo» è troppo tardi
	o2 := newOtaRig(t, nil)
	d2 := o2.display(fakedisplay.Options{FW: "0.6.0", OTA: fakedisplay.OTAOptions{USB: ptr(true), NoReboot: true}})
	awaitEv(t, d2, "ota-key")
	if _, err := o2.hub.StartOTA(otaBoard, image("0.7.0", 20_000)); err != nil {
		t.Fatal(err)
	}
	awaitEv(t, d2, "ota-written")
	o2.waitStage(OtaWrite)
	if _, err := o2.hub.CancelOTA(otaBoard); !errors.Is(err, ErrOTATooLate) {
		t.Fatalf("annullamento in scrivo: %v", err)
	}
	// la scheda si scollega in «scrivo»: è il riavvio
	d2.Close()
	o2.waitStage(OtaReboot)
}

func TestOTAStatusOnlyAuthenticated(t *testing.T) {
	o := newOtaRig(t, nil)
	c := o.dial(nil)
	c.greet("calamaio-9999") // non abbinato
	c.send(`{"t":"ota-status","v":1,"id":"o1","phase":"fatto"}`)
	c.send(`{"t":"crash","v":1,"fw":"0.7.0","reason":"x"}`)
	c.send(`{"t":"log","v":1,"id":"l1","lines":["a"]}`)
	c.send(`{"t":"status","v":1,"batt":101,"usb":true}`) // fuori scala: ignorato
	c.send(`{"t":"status","v":1,"batt":64,"usb":null}`)
	waitFor(t, 2*time.Second, func() bool {
		s := o.hub.Status()
		return len(s) == 1 && s[0].Batt != nil && *s[0].Batt == 64
	})
	st := o.hub.Status()[0]
	if !st.Status || st.USB != nil || st.OTA != nil {
		t.Fatalf("%+v", st)
	}
	for _, want := range []string{"ota-status da una connessione non autenticata", "crash da una connessione non autenticata",
		"log da una connessione non autenticata", "status non valido"} {
		if !strings.Contains(o.log.String(), want) {
			t.Errorf("log senza %q", want)
		}
	}
}

func TestRemoteLog(t *testing.T) {
	o := newOtaRig(t, nil)
	d := o.display(fakedisplay.Options{FW: "0.7.0"})
	d.LogRing("[1] avvio", "[2] wifi", "[3] ws", "[4] m5")
	// senza follow: il contenuto dell'anello, poi fine
	v, err := o.hub.LogStart(otaBoard, false, 0)
	if err != nil || !v.Active {
		t.Fatalf("%v %+v", err, v)
	}
	awaitEv(t, d, "log-start")
	waitFor(t, 3*time.Second, func() bool { v := o.hub.LogOf(otaBoard); return v != nil && !v.Active })
	lines, next, view, _ := o.hub.LogRead(otaBoard, 0)
	var texts []string
	for _, l := range lines {
		texts = append(texts, l.Text)
	}
	all := strings.Join(texts, "\n")
	if !strings.Contains(all, "[1] avvio\n[2] wifi\n[3] ws\n[4] m5") || view.Lines != 4 || next != lines[len(lines)-1].Seq {
		t.Fatalf("%q %+v", all, view)
	}
	f, err := os.ReadFile(view.File)
	if err != nil || !strings.Contains(string(f), "[4] m5") || !strings.HasPrefix(filepath.Base(view.File), otaBoard+"-") {
		t.Fatalf("file %s: %v", view.File, err)
	}
	if st, _ := os.Stat(view.File); st.Mode().Perm() != 0o600 {
		t.Fatalf("permessi %o", st.Mode().Perm())
	}
	// con follow: righe nuove, poi log-stop
	v, err = o.hub.LogStart(otaBoard, true, 5)
	if err != nil || !v.Follow || v.Until == 0 {
		t.Fatal(err)
	}
	e := awaitEv(t, d, "log-start")
	d.SendLog(e.Text, "[5] nuova riga\x07 con un controllo")
	waitFor(t, 2*time.Second, func() bool {
		l, _, _, _ := o.hub.LogRead(otaBoard, next)
		for _, x := range l {
			if x.Text == "[5] nuova riga con un controllo" {
				return true
			}
		}
		return false
	})
	if _, err := o.hub.LogStop(otaBoard); err != nil {
		t.Fatal(err)
	}
	awaitEv(t, d, "log-stop")
	if _, err := o.hub.LogStart(otaBoard, true, 61); err == nil {
		t.Fatal("61 minuti accettati")
	}
	// un log con un id che non è stato chiesto si ignora
	d.SendLog("lfalso", "intrusa")
	time.Sleep(100 * time.Millisecond)
	if !strings.Contains(o.log.String(), "log per un registro non richiesto") {
		t.Fatal("log non richiesto accettato")
	}
}

func TestCrashReport(t *testing.T) {
	o := newOtaRig(t, nil)
	d := o.display(fakedisplay.Options{FW: "0.7.0"})
	d.SendCrash(map[string]any{"fw": "0.7.0", "reason": "LoadProhibited", "pc": "0x42012a3c",
		"backtrace": []string{"0x42012a39:0x3fcebf00", "0x4200b8f2:0x3fcebf30"}, "last": "[9] ultima"})
	waitFor(t, 3*time.Second, func() bool { return o.cr.Last(otaBoard) != nil })
	c := o.cr.Last(otaBoard)
	if c.Reason != "LoadProhibited" || c.DecodeNote == "" || len(c.Backtrace) != 2 {
		t.Fatalf("%+v", c)
	}
	if _, err := os.Stat(filepath.Join(o.dir, "crash", c.ID+".txt")); err != nil {
		t.Fatal(err)
	}
	// malformato: ignorato
	d.SendCrash(map[string]any{"fw": "0.7.0", "reason": "x", "pc": "nonhex"})
	time.Sleep(100 * time.Millisecond)
	if !strings.Contains(o.log.String(), "crash non valido") {
		t.Fatal("crash malformato accettato")
	}
}
