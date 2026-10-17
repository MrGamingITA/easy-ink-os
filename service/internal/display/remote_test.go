package display

import (
	"encoding/json"
	"errors"
	"strings"
	"testing"
	"time"
)

const allSettings = `{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true,"mute":"off","save":false}`

func settingsMsgJSON(vals string, remote bool) string {
	r := "false"
	if remote {
		r = "true"
	}
	return `{"t":"settings","v":1,"settings":` + vals + `,"remote":` + r + `}`
}

func raw(t *testing.T, s string) map[string]json.RawMessage {
	t.Helper()
	var m map[string]json.RawMessage
	if err := json.Unmarshal([]byte(s), &m); err != nil {
		t.Fatal(err)
	}
	return m
}

func statusOf(h *Hub, board string) *DisplayStatus {
	for _, d := range h.Status() {
		if d.Board == board {
			return &d
		}
	}
	return nil
}

// remoteDisplay: hello, autenticazione (M5) e settings con remote:true; attende che l'hub lo veda.
func remoteDisplay(t *testing.T, r *rig, board string) *client {
	t.Helper()
	c := authedDisplay(t, r, board)
	c.send(settingsMsgJSON(allSettings, true))
	waitFor(t, 2*time.Second, func() bool {
		d := statusOf(r.hub, board)
		return d != nil && d.Remote && d.Settings != nil
	})
	return c
}

func TestRemoteSetAckFlow(t *testing.T) {
	sch := compileSchema(t)
	r := newRig(t, nil)
	notify, unsub := r.hub.Subscribe()
	defer unsub()
	c := remoteDisplay(t, r, "calamaio-3f2a")
	select {
	case <-notify:
	case <-time.After(time.Second):
		t.Fatal("nessun avviso di cambiamento dopo settings")
	}

	id, pending, err := r.hub.SetSettings("calamaio-3f2a", raw(t, `{"homeStyle":"righe","led":false}`))
	if err != nil {
		t.Fatal(err)
	}
	if pending["homeStyle"] != "righe" || pending["led"] != false || len(pending) != 2 {
		t.Fatalf("pending = %v", pending)
	}
	m := c.next(2 * time.Second)
	b, _ := json.Marshal(m)
	validate(t, sch, "set dal servizio", b)
	if m["t"] != "set" || m["id"] != id {
		t.Fatalf("atteso set %s: %s", id, b)
	}
	if d := statusOf(r.hub, "calamaio-3f2a"); d.SettingsPending["homeStyle"] != "righe" || d.Settings["homeStyle"] != "goccia" {
		t.Fatalf("prima dell'ack: %+v", d)
	}

	// Il display applica, rimanda settings e conferma.
	c.send(settingsMsgJSON(strings.NewReplacer(`"goccia"`, `"righe"`, `"led":true`, `"led":false`).Replace(allSettings), true))
	c.send(`{"t":"ack","v":1,"id":"` + id + `","ok":true}`)
	waitFor(t, 2*time.Second, func() bool {
		d := statusOf(r.hub, "calamaio-3f2a")
		return len(d.SettingsPending) == 0 && d.Settings["homeStyle"] == "righe" && d.Settings["led"] == false
	})
	if d := statusOf(r.hub, "calamaio-3f2a"); d.Error != nil {
		t.Fatalf("errore inatteso: %+v", d.Error)
	}

	// full-refresh: messaggio secondo lo schema, ack lo chiude.
	fid, err := r.hub.FullRefresh("calamaio-3f2a")
	if err != nil {
		t.Fatal(err)
	}
	m = c.next(2 * time.Second)
	b, _ = json.Marshal(m)
	validate(t, sch, "full-refresh dal servizio", b)
	if m["t"] != "full-refresh" || m["id"] != fid {
		t.Fatalf("atteso full-refresh %s: %s", fid, b)
	}
	c.send(`{"t":"ack","v":1,"id":"` + fid + `","ok":true}`)
	waitFor(t, 2*time.Second, func() bool {
		r.hub.mu.Lock()
		defer r.hub.mu.Unlock()
		for _, cn := range r.hub.conns {
			if len(cn.cmds) != 0 {
				return false
			}
		}
		return true
	})
}

func TestRemoteRejections(t *testing.T) {
	r := newRig(t, nil)
	old := r.dial(nil)
	old.greet("calamaio-m3") // firmware M3: niente settings

	// M5: senza autenticazione niente set né full-refresh (prima di ogni altro controllo).
	if _, _, err := r.hub.SetSettings("calamaio-m3", raw(t, `{"led":false}`)); !errors.Is(err, ErrNotAuthed) {
		t.Fatalf("display M3: %v", err)
	}
	if _, err := r.hub.FullRefresh("calamaio-m3"); !errors.Is(err, ErrNotAuthed) {
		t.Fatalf("full-refresh M3: %v", err)
	}
	old.none(150 * time.Millisecond) // e niente arriva al display
	if d := statusOf(r.hub, "calamaio-m3"); d == nil || d.Settings != nil || d.Remote || d.SettingsPending == nil {
		t.Fatalf("status M3 = %+v (settings deve essere null e settingsPending {})", d)
	}
	if _, _, err := r.hub.SetSettings("calamaio-nessuno", raw(t, `{"led":false}`)); !errors.Is(err, ErrNotConnected) {
		t.Fatalf("non collegato: %v", err)
	}
	// settings con remote:false: impostazioni visibili ma non modificabili.
	ro := authedDisplay(t, r, "calamaio-ro")
	ro.send(settingsMsgJSON(allSettings, false))
	waitFor(t, 2*time.Second, func() bool { d := statusOf(r.hub, "calamaio-ro"); return d != nil && d.Settings != nil })
	if _, _, err := r.hub.SetSettings("calamaio-ro", raw(t, `{"led":false}`)); !errors.Is(err, ErrNoRemote) {
		t.Fatalf("remote:false: %v", err)
	}

	c := remoteDisplay(t, r, "calamaio-3f2a")
	bad := map[string]string{
		`{"permOn":true}`:        MsgPermOnRemote,
		`{}`:                     "nessuna impostazione",
		`{"brightness":3}`:       "chiave sconosciuta",
		`{"homeStyle":"Goccia"}`: "non ammesso",
		`{"rot":1}`:              "true o false",
		`{"mute":"2h"}`:          "non ammesso",
		`{"anim":null}`:          "serve una stringa",
	}
	for body, want := range bad {
		_, _, err := r.hub.SetSettings("calamaio-3f2a", raw(t, body))
		var inv *InvalidError
		if !errors.As(err, &inv) || !strings.Contains(inv.Msg, want) {
			t.Errorf("%s: errore %v, atteso %q", body, err, want)
		}
	}
	c.none(150 * time.Millisecond) // niente set per richieste rifiutate
	// Spegnere i permessi da remoto invece si può.
	if _, _, err := r.hub.SetSettings("calamaio-3f2a", raw(t, `{"permOn":false}`)); err != nil {
		t.Fatal(err)
	}
	if m := c.next(time.Second); m["t"] != "set" {
		t.Fatalf("atteso set: %v", m)
	}
}

func TestRemoteAckTimeoutAndRefusal(t *testing.T) {
	r := newRig(t, func(h *Hub) { h.AckTimeout = 150 * time.Millisecond })
	c := remoteDisplay(t, r, "calamaio-3f2a")

	if _, _, err := r.hub.SetSettings("calamaio-3f2a", raw(t, `{"sound":false}`)); err != nil {
		t.Fatal(err)
	}
	c.next(time.Second) // set, senza risposta
	waitFor(t, 2*time.Second, func() bool {
		d := statusOf(r.hub, "calamaio-3f2a")
		return len(d.SettingsPending) == 0 && d.Error != nil
	})
	d := statusOf(r.hub, "calamaio-3f2a")
	if !strings.Contains(d.Error.Text, "non ha confermato") || d.Error.At == 0 {
		t.Fatalf("errore = %+v", d.Error)
	}
	waitFor(t, 2*time.Second, func() bool { return strings.Contains(r.log.String(), "nessun ack") }) // il log segue lo stato
	if !strings.Contains(r.log.String(), "nessun ack") {
		t.Fatalf("log:\n%s", r.log)
	}

	// Un nuovo comando toglie l'errore; il rifiuto del display lo rimette con il motivo.
	id, _, err := r.hub.SetSettings("calamaio-3f2a", raw(t, `{"night":true}`))
	if err != nil {
		t.Fatal(err)
	}
	if d := statusOf(r.hub, "calamaio-3f2a"); d.Error != nil {
		t.Fatalf("l'errore precedente doveva sparire: %+v", d.Error)
	}
	c.next(time.Second)
	c.send(`{"t":"ack","v":1,"id":"` + id + `","ok":false,"error":"pannello occupato"}`)
	waitFor(t, 2*time.Second, func() bool {
		d := statusOf(r.hub, "calamaio-3f2a")
		return d.Error != nil && strings.Contains(d.Error.Text, "pannello occupato") && len(d.SettingsPending) == 0
	})
	// Un ack tardivo o inventato si ignora.
	before := r.hub.Ignored()
	c.send(`{"t":"ack","v":1,"id":"` + id + `","ok":true}`)
	waitFor(t, time.Second, func() bool { return r.hub.Ignored() == before+1 })
}

// Due set sulla stessa chiave: l'ack del primo non toglie il valore in attesa del secondo.
func TestRemotePendingOwnership(t *testing.T) {
	r := newRig(t, nil)
	c := remoteDisplay(t, r, "calamaio-3f2a")
	id1, _, _ := r.hub.SetSettings("calamaio-3f2a", raw(t, `{"anim":"spente"}`))
	_, pend, _ := r.hub.SetSettings("calamaio-3f2a", raw(t, `{"anim":"continue"}`))
	if pend["anim"] != "continue" {
		t.Fatalf("pending = %v", pend)
	}
	c.next(time.Second)
	c.next(time.Second)
	c.send(`{"t":"ack","v":1,"id":"` + id1 + `","ok":true}`)
	time.Sleep(150 * time.Millisecond)
	if d := statusOf(r.hub, "calamaio-3f2a"); d.SettingsPending["anim"] != "continue" {
		t.Fatalf("pending dopo il primo ack = %v", d.SettingsPending)
	}
}

func TestRemoteBeforeHelloAndInvalid(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.send(settingsMsgJSON(allSettings, true)) // prima di hello: fuori sequenza
	waitFor(t, time.Second, func() bool { return r.hub.Ignored() == 1 })
	c.greet("calamaio-3f2a")
	if d := statusOf(r.hub, "calamaio-3f2a"); d.Settings != nil {
		t.Fatalf("settings prima di hello accettato: %+v", d)
	}
	for _, b := range []string{
		settingsMsgJSON(`{"homeStyle":"tonda"}`, true),
		`{"t":"settings","v":1,"remote":true}`,
		`{"t":"ack","v":1,"ok":true}`,
		`{"t":"ack","v":1,"id":"c1"}`,
	} {
		c.send(b)
	}
	waitFor(t, time.Second, func() bool { return r.hub.Ignored() == 5 })
	// Chiavi sconosciute (firmware più nuovo): si scartano, il resto vale.
	c.send(settingsMsgJSON(strings.Replace(allSettings, `{`, `{"futura":1,`, 1), true))
	waitFor(t, time.Second, func() bool { d := statusOf(r.hub, "calamaio-3f2a"); return d.Remote && d.Settings != nil })
	if _, ok := statusOf(r.hub, "calamaio-3f2a").Settings["futura"]; ok {
		t.Fatal("chiave sconosciuta conservata")
	}
}
