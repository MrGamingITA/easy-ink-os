package display

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/santhosh-tekuri/jsonschema/v6"

	"calamaio/internal/fake"
	"calamaio/internal/m5"
	"calamaio/internal/state"
)

const protocolDir = "../../../protocol"

func compileSchema(t *testing.T) *jsonschema.Schema {
	t.Helper()
	path := filepath.Join(protocolDir, "schema", "message.schema.json")
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	doc, err := jsonschema.UnmarshalJSON(f)
	if err != nil {
		t.Fatal(err)
	}
	c := jsonschema.NewCompiler()
	c.DefaultDraft(jsonschema.Draft2020)
	if err := c.AddResource("message.schema.json", doc); err != nil {
		t.Fatal(err)
	}
	sch, err := c.Compile("message.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	return sch
}

// validate controlla un messaggio contro lo schema e i limiti in byte di PROTOCOLLO.md.
func validate(t *testing.T, sch *jsonschema.Schema, name string, b []byte) {
	t.Helper()
	v, err := jsonschema.UnmarshalJSON(bytes.NewReader(b))
	if err != nil {
		t.Fatalf("%s: JSON non valido: %v", name, err)
	}
	if err := sch.Validate(v); err != nil {
		t.Fatalf("%s: non rispetta lo schema: %v\n%s", name, err, b)
	}
	checkByteLimits(t, name, b)
}

// checkByteLimits: lo schema conta i caratteri, il protocollo i byte UTF-8.
func checkByteLimits(t *testing.T, name string, b []byte) {
	t.Helper()
	var m struct {
		T        string `json:"t"`
		Sessions []struct {
			Name, Detail, Summary string
		} `json:"sessions"`
		Name, Tool, Arg, Session string // perm-request
		Summary                  *struct {
			Sessions []struct{ Name string } `json:"sessions"`
			Last     *struct{ Name, Summary string }
		} `json:"summary"` // bye
	}
	_ = json.Unmarshal(b, &m)
	if m.T == "bye" && m.Summary != nil {
		for _, s := range m.Summary.Sessions {
			if len(s.Name) > state.MaxName {
				t.Fatalf("%s: bye, nome oltre il limite in byte: %q", name, s.Name)
			}
		}
		if l := m.Summary.Last; l != nil && (len(l.Name) > state.MaxName || len(l.Summary) > state.MaxSummary) {
			t.Fatalf("%s: bye, last oltre i limiti in byte: %q / %q", name, l.Name, l.Summary)
		}
		if len(b) > state.MaxByeBytes {
			t.Fatalf("%s: bye di %d byte, oltre %d", name, len(b), state.MaxByeBytes)
		}
	}
	if m.T == "ota-status" {
		var o struct{ Error string }
		_ = json.Unmarshal(b, &o)
		if len(o.Error) > MaxOtaError {
			t.Fatalf("%s: ota-status, error oltre %d byte", name, MaxOtaError)
		}
	}
	if m.T == "wifi-add" || m.T == "wifi-forget" || m.T == "wifi-list" {
		var w struct {
			SSID     string
			Networks []struct{ SSID string }
		}
		_ = json.Unmarshal(b, &w)
		if len(w.SSID) > WifiSSIDMax {
			t.Fatalf("%s: ssid oltre %d byte: %q", name, WifiSSIDMax, w.SSID)
		}
		for _, n := range w.Networks {
			if len(n.SSID) > WifiSSIDMax {
				t.Fatalf("%s: ssid oltre %d byte: %q", name, WifiSSIDMax, n.SSID)
			}
		}
	}
	if m.T == "perm-request" && (len(m.Name) > m5.NameMax || len(m.Tool) > m5.ToolMax || len(m.Arg) > m5.ArgMax || len(m.Session) > m5.SessionMax) {
		t.Fatalf("%s: perm-request oltre i limiti in byte: %q / %q / %q", name, m.Name, m.Tool, m.Arg)
	}
	for _, s := range m.Sessions {
		if len(s.Name) > state.MaxName || len(s.Detail) > state.MaxDetail || len(s.Summary) > state.MaxSummary {
			t.Fatalf("%s: testo oltre il limite in byte: %q / %q / %q", name, s.Name, s.Detail, s.Summary)
		}
	}
}

func TestExamplesMatchSchema(t *testing.T) {
	sch := compileSchema(t)
	files, _ := filepath.Glob(filepath.Join(protocolDir, "examples", "*.json"))
	if len(files) < 27 {
		t.Fatalf("esempi trovati: %v", files)
	}
	// Ogni tipo di messaggio dello schema ha almeno un esempio.
	seen := map[string]bool{}
	for _, f := range files {
		b, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		validate(t, sch, filepath.Base(f), b)
		var head struct{ T string }
		_ = json.Unmarshal(b, &head)
		seen[head.T] = true
	}
	for _, typ := range []string{"hello", "welcome", "state", "settings", "set", "ack", "full-refresh", "dismiss",
		"pair-begin", "pair-key", "pair-reveal", "pair-confirm", "pair-done", "pair-abort",
		"auth-challenge", "auth", "auth-ok", "auth-fail", "perm-request", "perm-reply", "perm-close", "unpair",
		"avatar", "avatar-need", "avatar-offer", "avatar-ack", "bye",
		"status", "ota-key", "ota-offer", "ota-status", "ota-cancel", "log-start", "log-stop", "log", "crash",
		"wifi-list", "wifi-add", "wifi-forget", "workday",
		"game", "accessory", "accessory-need", "anchors", "anchors-need", "accessory-wear",
		"themes", "theme-offer", "theme-ack", "theme-use"} {
		if !seen[typ] {
			t.Errorf("nessun esempio per %q in protocol/examples", typ)
		}
	}
}

// Lo schema deve anche rifiutare: altrimenti i test sopra non dimostrano niente.
func TestSchemaRejects(t *testing.T) {
	sch := compileSchema(t)
	state0, _ := os.ReadFile(filepath.Join(protocolDir, "examples", "state.json"))
	bad := map[string]string{
		"senza v":           `{"t":"hello","board":"b","device":"d","class":"S","fw":"1","w":1,"h":1}`,
		"versione 2":        `{"t":"hello","v":2,"board":"b","device":"d","class":"S","fw":"1","w":1,"h":1}`,
		"tipo sconosciuto":  `{"t":"boh","v":1}`,
		"classe X":          `{"t":"hello","v":1,"board":"b","device":"d","class":"X","fw":"1","w":1,"h":1}`,
		"welcome senza tz":  `{"t":"welcome","v":1,"time":1,"host":"h","service":"s"}`,
		"state incompleto":  `{"t":"state","v":1,"time":1,"sessions":[]}`,
		"stato sessione":    strings.Replace(string(state0), `"state":"working"`, `"state":"busy"`, 1),
		"error senza error": strings.Replace(string(state0), `"state":"working"`, `"state":"error"`, 1),
		"task a 3":          strings.Replace(string(state0), `["Indice",2]`, `["Indice",3]`, 1),
		"ctx non intero":    strings.Replace(string(state0), `"ctx":46`, `"ctx":46.5`, 1),
		"limite a metà":     strings.Replace(string(state0), `"h5":62,"h5Reset":1791715200000,`, `"h5":62,`, 1),
		"campo inventato":   strings.Replace(string(state0), `"subs":2,`, `"subs":2,"sub":2,`, 1),
		"checks strano":     strings.Replace(string(state0), `"checks":"ko"`, `"checks":"failed"`, 1),
		// Impostazioni remote.
		"set permOn true":       `{"t":"set","v":1,"id":"c1","settings":{"permOn":true}}`,
		"set vuoto":             `{"t":"set","v":1,"id":"c1","settings":{}}`,
		"set senza id":          `{"t":"set","v":1,"settings":{"led":false}}`,
		"set chiave inventata":  `{"t":"set","v":1,"id":"c1","settings":{"brightness":3}}`,
		"set stile sbagliato":   `{"t":"set","v":1,"id":"c1","settings":{"homeStyle":"Goccia"}}`,
		"set rot numero":        `{"t":"set","v":1,"id":"c1","settings":{"rot":1}}`,
		"set save stringa":      `{"t":"set","v":1,"id":"c1","settings":{"save":"true"}}`,
		"settings senza save":   `{"t":"settings","v":1,"remote":true,"settings":{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true,"mute":"off"}}`,
		"set mute sbagliato":    `{"t":"set","v":1,"id":"c1","settings":{"mute":"2h"}}`,
		"settings incompleto":   `{"t":"settings","v":1,"remote":true,"settings":{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true}}`,
		"settings senza remote": `{"t":"settings","v":1,"settings":{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true,"mute":"off","save":false}}`,
		"ack senza ok":          `{"t":"ack","v":1,"id":"c1"}`,
		"ack ok con error":      `{"t":"ack","v":1,"id":"c1","ok":true,"error":"x"}`,
		"ack id vuoto":          `{"t":"ack","v":1,"id":"","ok":true}`,
		"full-refresh senza id": `{"t":"full-refresh","v":1}`,
		// M5.
		"hello paired stringa":         `{"t":"hello","v":1,"board":"b","device":"d","class":"S","fw":"1","w":1,"h":1,"paired":"sì"}`,
		"pair-begin commit corto":      `{"t":"pair-begin","v":1,"id":"p7","commit":"ea9dc0cd"}`,
		"pair-begin id lungo":          `{"t":"pair-begin","v":1,"id":"` + strings.Repeat("p", 33) + `","commit":"` + strings.Repeat("a", 64) + `"}`,
		"pair-key maiuscole":           `{"t":"pair-key","v":1,"id":"p7","pkD":"` + strings.Repeat("A", 64) + `"}`,
		"pair-confirm senza mac":       `{"t":"pair-confirm","v":1,"id":"p7"}`,
		"pair-done mac lungo":          `{"t":"pair-done","v":1,"id":"p7","mac":"` + strings.Repeat("a", 64) + `"}`,
		"pair-abort senza motivo":      `{"t":"pair-abort","v":1,"id":"p7"}`,
		"auth-challenge senza ns":      `{"t":"auth-challenge","v":1}`,
		"auth con campo in più":        `{"t":"auth","v":1,"nd":"` + strings.Repeat("a", 32) + `","mac":"` + strings.Repeat("b", 32) + `","k":"x"}`,
		"auth-ok senza mac":            `{"t":"auth-ok","v":1}`,
		"perm-request senza mac":       `{"t":"perm-request","v":1,"id":"` + strings.Repeat("a", 32) + `","session":"s","name":"n","tool":"Bash","arg":"","at":1}`,
		"perm-request AskUserQuestion": `{"t":"perm-request","v":1,"id":"` + strings.Repeat("a", 32) + `","session":"s","name":"n","tool":"AskUserQuestion","arg":"","at":1,"mac":"` + strings.Repeat("b", 32) + `"}`,
		"perm-request name lungo":      `{"t":"perm-request","v":1,"id":"` + strings.Repeat("a", 32) + `","session":"s","name":"` + strings.Repeat("n", 32) + `","tool":"Bash","arg":"","at":1,"mac":"` + strings.Repeat("b", 32) + `"}`,
		"perm-reply decisione strana":  `{"t":"perm-reply","v":1,"id":"` + strings.Repeat("a", 32) + `","decision":"yes","nonce":"` + strings.Repeat("c", 32) + `","mac":"` + strings.Repeat("b", 32) + `"}`,
		"perm-reply senza nonce":       `{"t":"perm-reply","v":1,"id":"` + strings.Repeat("a", 32) + `","decision":"allow","mac":"` + strings.Repeat("b", 32) + `"}`,
		"perm-close motivo strano":     `{"t":"perm-close","v":1,"id":"` + strings.Repeat("a", 32) + `","reason":"timeout","mac":"` + strings.Repeat("b", 32) + `"}`,
		"perm-close senza mac":         `{"t":"perm-close","v":1,"id":"` + strings.Repeat("a", 32) + `","reason":"terminal"}`,
		"unpair senza mac":             `{"t":"unpair","v":1}`,
		// Gioco della mascotte.
		"game senza wear":             `{"t":"game","v":1,"on":true,"level":1,"xp":0,"xpLevelStart":0,"xpNext":100,"energy":100,"tired":false,"unlocked":[]}`,
		"game energia 101":            `{"t":"game","v":1,"on":true,"level":1,"xp":0,"xpLevelStart":0,"xpNext":100,"energy":101,"tired":false,"wear":null,"unlocked":[]}`,
		"game wear senza crc":         `{"t":"game","v":1,"on":true,"level":1,"xp":0,"xpLevelStart":0,"xpNext":100,"energy":100,"tired":false,"wear":{"id":"corona"},"unlocked":[]}`,
		"game titolo lungo":           `{"t":"game","v":1,"on":true,"level":1,"xp":0,"xpLevelStart":0,"xpNext":100,"energy":100,"tired":false,"wear":null,"celebrate":{"id":"x","title":"` + strings.Repeat("t", 25) + `"},"unlocked":[]}`,
		"game special false":          `{"t":"game","v":1,"on":true,"level":1,"xp":0,"xpLevelStart":0,"xpNext":100,"energy":100,"tired":false,"wear":null,"unlocked":[],"special":false}`,
		"game livello 0":              `{"t":"game","v":1,"on":true,"level":0,"xp":0,"xpLevelStart":0,"xpNext":100,"energy":100,"tired":false,"wear":null,"unlocked":[]}`,
		"accessory dati non base64":   `{"t":"accessory","v":1,"id":"corona","crc":1,"data":"non è base64"}`,
		"accessory-need vuoto":        `{"t":"accessory-need","v":1,"ids":[]}`,
		"anchors senza avatarCrc":     `{"t":"anchors","v":1,"avatar":"gatto","data":"Q0FOQw=="}`,
		"anchors-need senza crc":      `{"t":"anchors-need","v":1,"avatars":[{"id":"gatto"}]}`,
		"accessory-wear senza id":     `{"t":"accessory-wear","v":1}`,
		"accessory-wear id maiuscolo": `{"t":"accessory-wear","v":1,"id":"Corona"}`,
		"settings game stringa":       `{"t":"settings","v":1,"remote":true,"settings":{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true,"mute":"off","save":false,"game":"sì"}}`,
		// hello.caps.
		"hello caps maiuscole": `{"t":"hello","v":1,"board":"calamaio-3f2a","device":"heltec213","class":"S","fw":"0.14.0","w":250,"h":122,"caps":["Avatar_DL"]}`,
		"hello caps non array": `{"t":"hello","v":1,"board":"calamaio-3f2a","device":"heltec213","class":"S","fw":"0.14.0","w":250,"h":122,"caps":"avatar-dl"}`,
		"hello caps doppione":  `{"t":"hello","v":1,"board":"calamaio-3f2a","device":"heltec213","class":"S","fw":"0.14.0","w":250,"h":122,"caps":["avatar-dl","avatar-dl"]}`,
		"hello caps nove":      `{"t":"hello","v":1,"board":"calamaio-3f2a","device":"heltec213","class":"S","fw":"0.14.0","w":250,"h":122,"caps":["a","b","c","d","e","f","g","h","i"]}`,
		// Avatar.
		"avatar senza crc":          `{"t":"avatar","v":1,"id":"gatto","data":"Q0FWVA=="}`,
		"avatar id maiuscolo":       `{"t":"avatar","v":1,"id":"Gatto","crc":1,"data":"Q0FWVA=="}`,
		"avatar data non base64":    `{"t":"avatar","v":1,"id":"gatto","crc":1,"data":"Q0F*VA=="}`,
		"avatar crc negativo":       `{"t":"avatar","v":1,"id":"gatto","crc":-1,"data":"Q0FWVA=="}`,
		"avatar crc oltre 32 bit":   `{"t":"avatar","v":1,"id":"gatto","crc":4294967296,"data":"Q0FWVA=="}`,
		"avatar-need vuoto":         `{"t":"avatar-need","v":1,"ids":[]}`,
		"avatar-need doppione":      `{"t":"avatar-need","v":1,"ids":["gatto","gatto"]}`,
		"avatar-need id lungo":      `{"t":"avatar-need","v":1,"ids":["` + strings.Repeat("a", 25) + `"]}`,
		"sessione avatar senza crc": strings.Replace(string(state0), `"subs":2,`, `"subs":2,"avatar":"gatto",`, 1),
		"sessione crc senza avatar": strings.Replace(string(state0), `"subs":2,`, `"subs":2,"avatarCrc":5,`, 1),
		"welcome avatar senza crc":  `{"t":"welcome","v":1,"time":1,"tz":"CET-1","host":"h","service":"s","defaultAvatar":"goccia"}`,
		"state crc senza avatar":    strings.Replace(string(state0), `"time":`, `"defaultAvatarCrc":1,"time":`, 1),
		// M6: bye.
		"bye senza summary":       `{"t":"bye","v":1}`,
		"bye senza off":           `{"t":"bye","v":1,"summary":{"open":1}}`,
		"bye off stringa":         `{"t":"bye","v":1,"summary":{"off":"19:00"}}`,
		"bye campo inventato":     `{"t":"bye","v":1,"summary":{"off":1,"reason":"logout"}}`,
		"bye stato strano":        `{"t":"bye","v":1,"summary":{"off":1,"sessions":[{"name":"a","state":"busy"}]}}`,
		"bye sessione senza nome": `{"t":"bye","v":1,"summary":{"off":1,"sessions":[{"state":"done"}]}}`,
		"bye 7 sessioni":          `{"t":"bye","v":1,"summary":{"off":1,"sessions":[` + strings.TrimSuffix(strings.Repeat(`{"name":"a","state":"done"},`, 7), ",") + `]}}`,
		"bye last senza summary":  `{"t":"bye","v":1,"summary":{"off":1,"last":{"name":"a"}}}`,
		"bye today incompleto":    `{"t":"bye","v":1,"summary":{"off":1,"today":{"tasks":1}}}`,
		"bye day non data":        `{"t":"bye","v":1,"summary":{"off":1,"day":"11/10/2026"}}`,
		"bye negativo":            `{"t":"bye","v":1,"summary":{"off":1,"open":-1}}`,
		// Reti Wi-Fi: mai una password in chiaro, forme esatte.
		"wifi-list con password":   `{"t":"wifi-list","v":1,"networks":[{"ssid":"Casa","current":true,"rssi":-50,"lastUsed":1,"password":"segreta123"}]}`,
		"wifi-list senza networks": `{"t":"wifi-list","v":1}`,
		"wifi-list rssi positivo":  `{"t":"wifi-list","v":1,"networks":[{"ssid":"Casa","current":true,"rssi":5,"lastUsed":null}]}`,
		"wifi-list senza rssi":     `{"t":"wifi-list","v":1,"networks":[{"ssid":"Casa","current":true,"lastUsed":null}]}`,
		"wifi-list ssid vuoto":     `{"t":"wifi-list","v":1,"networks":[{"ssid":"","current":true,"rssi":null,"lastUsed":null}]}`,
		"wifi-list ssid lungo":     `{"t":"wifi-list","v":1,"networks":[{"ssid":"` + strings.Repeat("s", 33) + `","current":true,"rssi":null,"lastUsed":null}]}`,
		"wifi-add con password":    `{"t":"wifi-add","v":1,"id":"c7","ssid":"Casa","iv":"` + strings.Repeat("a", 24) + `","ct":"` + strings.Repeat("b", 48) + `","password":"segreta123"}`,
		"wifi-add iv corto":        `{"t":"wifi-add","v":1,"id":"c7","ssid":"Casa","iv":"` + strings.Repeat("a", 22) + `","ct":"` + strings.Repeat("b", 48) + `"}`,
		"wifi-add senza tag":       `{"t":"wifi-add","v":1,"id":"c7","ssid":"Casa","iv":"` + strings.Repeat("a", 24) + `","ct":"` + strings.Repeat("b", 32) + `"}`,
		"wifi-add ct maiuscolo":    `{"t":"wifi-add","v":1,"id":"c7","ssid":"Casa","iv":"` + strings.Repeat("a", 24) + `","ct":"` + strings.Repeat("B", 48) + `"}`,
		"wifi-add ct dispari":      `{"t":"wifi-add","v":1,"id":"c7","ssid":"Casa","iv":"` + strings.Repeat("a", 24) + `","ct":"` + strings.Repeat("b", 49) + `"}`,
		"wifi-add senza id":        `{"t":"wifi-add","v":1,"ssid":"Casa","iv":"` + strings.Repeat("a", 24) + `","ct":"` + strings.Repeat("b", 48) + `"}`,
		"wifi-forget senza mac":    `{"t":"wifi-forget","v":1,"id":"c8","ssid":"Casa"}`,
		"wifi-forget mac lungo":    `{"t":"wifi-forget","v":1,"id":"c8","ssid":"Casa","mac":"` + strings.Repeat("a", 64) + `"}`,
		// Giornata di lavoro.
		"workday senza id":            `{"t":"workday","v":1,"action":"start"}`,
		"workday azione strana":       `{"t":"workday","v":1,"id":"c1","action":"pause"}`,
		"workday campo in più":        `{"t":"workday","v":1,"id":"c1","action":"start","end":"18:00"}`,
		"set orario a una cifra":      `{"t":"set","v":1,"id":"c1","settings":{"workStart":"9:00"}}`,
		"set orario 24:00":            `{"t":"set","v":1,"id":"c1","settings":{"workEnd":"24:00"}}`,
		"set orario numero":           `{"t":"set","v":1,"id":"c1","settings":{"workEnd":1800}}`,
		"set orario con secondi":      `{"t":"set","v":1,"id":"c1","settings":{"workEnd":"18:00:00"}}`,
		"settings solo workStart":     `{"t":"settings","v":1,"remote":true,"settings":{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true,"mute":"off","save":false,"workStart":"09:00"}}`,
		"status workday senza on":     `{"t":"status","v":1,"batt":1,"usb":true,"workday":{"end":null}}`,
		"status workday senza end":    `{"t":"status","v":1,"batt":1,"usb":true,"workday":{"on":false}}`,
		"status workday end stringa":  `{"t":"status","v":1,"batt":1,"usb":true,"workday":{"on":true,"end":"18:00"}}`,
		"status workday spenta e end": `{"t":"status","v":1,"batt":1,"usb":true,"workday":{"on":false,"end":1791820800000}}`,
		"status workday campo in più": `{"t":"status","v":1,"batt":1,"usb":true,"workday":{"on":true,"end":1,"left":5}}`,
		// Temi delle schermate.
		"theme-offer token a 31 cifre": `{"t":"theme-offer","v":1,"id":"prova","crc":1,"size":3120,"token":"` + strings.Repeat("a", 31) + `"}`,
		"themes con 9 temi":            `{"t":"themes","v":1,"have":[` + strings.TrimSuffix(strings.Repeat(`{"id":"a","crc":1},`, 9), ",") + `],"cur":null}`,
	}
	for name, s := range bad {
		if s == string(state0) {
			t.Fatalf("%s: la sostituzione non ha cambiato niente", name)
		}
		v, err := jsonschema.UnmarshalJSON(strings.NewReader(s))
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if sch.Validate(v) == nil {
			t.Errorf("%s: accettato, doveva essere rifiutato", name)
		}
	}
}

// I messaggi prodotti dal servizio rispettano lo schema: welcome, state vuoto, state finto
// lungo tutto il copione, state con sessioni forzate a mano.
func TestServiceMessagesMatchSchema(t *testing.T) {
	sch := compileSchema(t)

	r := newRig(t, nil)
	c := r.dial(nil)
	c.send(hello("calamaio-3f2a"))
	for _, want := range []string{"welcome", "state"} {
		m := c.next(2 * time.Second)
		b, _ := json.Marshal(m)
		if m["t"] != want {
			t.Fatalf("atteso %s: %s", want, b)
		}
		validate(t, sch, "dal servizio: "+want, b)
	}

	clock := time.Date(2026, 10, 11, 9, 0, 0, 0, time.UTC)
	store := state.NewStore(fake.Seed(clock))
	store.Now = func() time.Time { return clock }
	gen := fake.New(store)
	gen.Now = func() time.Time { return clock }
	seen := map[string]bool{}
	for i := 0; i < 300; i++ {
		b, err := json.Marshal(store.Message())
		if err != nil {
			t.Fatal(err)
		}
		validate(t, sch, "stato finto", b)
		for _, s := range store.Snapshot().Sessions {
			seen[s.State] = true
		}
		gen.Step()
		clock = clock.Add(4 * time.Second)
	}
	for _, st := range []string{"working", "permission", "input", "done", "error", "idle", "compact"} {
		if !seen[st] {
			t.Errorf("il copione finto non mostra mai lo stato %q", st)
		}
	}

	long := strings.Repeat("è", 200)
	for _, f := range []fake.ForceRequest{
		{Session: "web-app", State: "error"},
		{Session: "nuova", State: "input", Detail: &long, Summary: &long},
		{Session: "api-server", State: "done", Summary: &long},
	} {
		if _, err := gen.Force(f); err != nil {
			t.Fatal(err)
		}
		b, _ := json.Marshal(store.Message())
		validate(t, sch, "forzato "+f.Session, b)
	}
}

// `bye` minimo (solo off) e `bye` prodotto dal servizio sullo stato finto, anche con testi lunghi.
func TestByeMatchesSchema(t *testing.T) {
	sch := compileSchema(t)
	validate(t, sch, "bye minimo", []byte(`{"t":"bye","v":1,"summary":{"off":1}}`))
	clock := time.Date(2026, 10, 11, 19, 0, 0, 0, time.UTC)
	store := state.NewStore(fake.Seed(clock))
	gen := fake.New(store)
	gen.Now = func() time.Time { return clock }
	long := strings.Repeat("<è>", 100)
	for i := 0; i < 50; i++ {
		if i == 25 {
			for _, f := range []fake.ForceRequest{{Session: "nuova", State: "input", Detail: &long, Summary: &long},
				{Session: "api-server", State: "done", Summary: &long}} {
				if _, err := gen.Force(f); err != nil {
					t.Fatal(err)
				}
			}
		}
		n := 3
		sum := store.ByeSummary(clock)
		sum.Day, sum.SessionsToday = "2026-10-11", &n
		b, _ := json.Marshal(state.Bye{T: "bye", V: 1, Summary: sum.Fit(state.MaxByeBytes)})
		validate(t, sch, "bye dal servizio", b)
		gen.Step()
		clock = clock.Add(4 * time.Second)
	}
}
