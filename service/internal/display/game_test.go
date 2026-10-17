package display

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/accessory"
	"calamaio/internal/avatar"
	"calamaio/internal/config"
	"calamaio/internal/game"
	"calamaio/internal/gamesvc"
)

func gameRig(t *testing.T, st game.State) (*rig, *gamesvc.Service) {
	t.Helper()
	path := filepath.Join(t.TempDir(), "game.json")
	b, _ := json.Marshal(map[string]any{"format": gamesvc.FileFormat, "version": 1, "state": st})
	if err := os.WriteFile(path, b, 0o600); err != nil {
		t.Fatal(err)
	}
	g := gamesvc.Open(path, accessory.NewLibrary(""), nil, nil)
	m := avatar.NewManager(avatar.NewLibrary(""), &config.AvatarStore{})
	r := newRig(t, func(h *Hub) { h.Avatars = m; h.Game = g })
	r.store.Avatars = m
	done := make(chan struct{})
	go r.hub.RunGame(done)
	t.Cleanup(func() { close(done) })
	return r, g
}

func rawOf(t *testing.T, m map[string]any) []byte {
	b, err := json.Marshal(m)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

// game dopo hello, poi a ogni cambio; accessory-need, anchors-need e accessory-wear; tutti i messaggi
// rispettano lo schema.
func TestGameProtocol(t *testing.T) {
	sch := compileSchema(t)
	r, g := gameRig(t, game.State{On: false, XP: 500, Unlocked: []string{"cappellino", "occhiali"}, Wear: "occhiali"})
	c := r.dial(nil)
	c.greet("calamaio-3f2a")
	gm := c.nextOf("game", 2*time.Second)
	validate(t, sch, "game spento", rawOf(t, gm))
	if gm["on"] != false || gm["wear"] != nil || len(gm["unlocked"].([]any)) != 0 || gm["xp"] != 500.0 {
		t.Fatalf("game spento: %v", gm)
	}
	if _, has := gm["celebrate"]; has {
		t.Fatal("celebrate fuori dal momento del traguardo")
	}

	g.SetOn(true)
	gm = c.nextOf("game", 2*time.Second)
	validate(t, sch, "game acceso", rawOf(t, gm))
	wear := gm["wear"].(map[string]any)
	occ := accessory.NewLibrary("").Get("occhiali")
	if wear["id"] != "occhiali" || wear["crc"] != float64(occ.CRC) || len(gm["unlocked"].([]any)) != 2 {
		t.Fatalf("game acceso: %v", gm)
	}
	if gm["level"] != float64(game.LevelFor(500)) || gm["xpLevelStart"] != float64(game.LevelStart(game.LevelFor(500))) {
		t.Fatalf("livello: %v", gm)
	}

	// accessory-need: il CACC della libreria; gli id sconosciuti si saltano
	c.send(`{"t":"accessory-need","v":1,"ids":["occhiali","boh"]}`)
	am := c.nextOf("accessory", 2*time.Second)
	validate(t, sch, "accessory", rawOf(t, am))
	bin, _ := base64.StdEncoding.DecodeString(am["data"].(string))
	if a, err := accessory.Unpack(bin); err != nil || a.ID != "occhiali" || am["crc"] != float64(occ.CRC) {
		t.Fatalf("accessory: %v %v", err, am["crc"])
	}
	c.none(150 * time.Millisecond)

	// anchors-need: solo per il disegno che ha il display (CRC del CAVT)
	gatto := avatar.NewLibrary("").Get("gatto")
	c.send(fmt.Sprintf(`{"t":"anchors-need","v":1,"avatars":[{"id":"gatto","crc":%d},{"id":"goccia","crc":1}]}`, gatto.CRC))
	an := c.nextOf("anchors", 2*time.Second)
	validate(t, sch, "anchors", rawOf(t, an))
	cb, _ := base64.StdEncoding.DecodeString(an["data"].(string))
	if u, err := avatar.UnpackAnchors(cb); err != nil || u.AvatarCRC != gatto.CRC || an["avatar"] != "gatto" || an["avatarCrc"] != float64(gatto.CRC) {
		t.Fatalf("anchors: %v %v", err, an)
	}
	c.none(150 * time.Millisecond)

	// accessory-wear dal menu della scheda: cambia e torna game a tutti
	c.send(`{"t":"accessory-wear","v":1,"id":"cappellino"}`)
	gm = c.nextOf("game", 2*time.Second)
	if gm["wear"].(map[string]any)["id"] != "cappellino" || g.State().Wear != "cappellino" {
		t.Fatalf("wear: %v", gm)
	}
	c.send(`{"t":"accessory-wear","v":1,"id":null}`)
	gm = c.nextOf("game", 2*time.Second)
	if gm["wear"] != nil || g.State().Wear != "" {
		t.Fatalf("wear null: %v", gm)
	}
	// bloccato: rifiutato, ma la scheda riceve game con quello vero
	c.send(`{"t":"accessory-wear","v":1,"id":"corona"}`)
	gm = c.nextOf("game", 2*time.Second)
	if gm["wear"] != nil || g.State().Wear != "" {
		t.Fatalf("wear bloccato: %v", gm)
	}
	// malformati: ignorati
	before := r.hub.Ignored()
	for _, m := range []string{
		`{"t":"accessory-wear","v":1}`, `{"t":"accessory-wear","v":1,"id":"Corona"}`,
		`{"t":"accessory-need","v":1,"ids":[]}`, `{"t":"anchors-need","v":1,"avatars":[{"id":"gatto"}]}`,
	} {
		c.send(m)
	}
	waitFor(t, 2*time.Second, func() bool { return r.hub.Ignored() == before+4 })

	// festa: un traguardo, solo in quel messaggio
	// ora fissa di giorno: dalle 00 alle 06 il lavoro non conta (regola del gioco), il test non deve dipendere dall'ora
	noon := time.Now()
	noon = time.Date(noon.Year(), noon.Month(), noon.Day(), 12, 0, 0, 0, time.Local)
	g.Apply(game.Event{At: noon, Session: "s1", Kind: game.KindActivity})
	gm = c.nextOf("game", 2*time.Second)
	validate(t, sch, "game con festa", rawOf(t, gm))
	if cel, ok := gm["celebrate"].(map[string]any); !ok || cel["id"] != "prima-sessione" {
		t.Fatalf("festa: %v", gm)
	}
}

// settings.game cambiato sulla scheda accende o spegne il gioco; il primo settings di una connessione no.
func TestGameSettingFromBoard(t *testing.T) {
	r, g := gameRig(t, game.State{On: true})
	c := r.dial(nil)
	c.greet("calamaio-3f2a")
	c.nextOf("game", 2*time.Second)
	set := func(on bool) string {
		return fmt.Sprintf(`{"t":"settings","v":1,"remote":true,"settings":{"homeStyle":"goccia","anim":"auto","speed":"profilo","invert":false,"rot":false,"night":false,"permOn":true,"sound":true,"led":true,"mute":"off","save":false,"game":%v}}`, on)
	}
	c.send(set(false)) // primo: vale il servizio
	time.Sleep(150 * time.Millisecond)
	if !g.On() {
		t.Fatal("il primo settings ha spento il gioco")
	}
	c.send(set(true)) // la scheda si è allineata
	time.Sleep(100 * time.Millisecond)
	c.send(set(false)) // l'utente lo spegne dal menu
	waitFor(t, 2*time.Second, func() bool { return !g.On() })
	gm := c.nextOf("game", 2*time.Second)
	if gm["on"] != false {
		t.Fatalf("game dopo lo spegnimento dal menu: %v", gm)
	}
	if strings.Contains(r.log.String(), "chiavi sconosciute scartate [game]") {
		t.Fatal("game scartata come chiave sconosciuta")
	}
}

// Dopo auth-ok il servizio rimanda game (la scheda abbinata può averlo perso nella sfida).
func TestGameAfterAuth(t *testing.T) {
	r, _ := gameRig(t, game.State{On: true, Unlocked: append([]string{}, accessory.CatalogOrder...), SpecialPending: "task-10"})
	c := authedDisplay(t, r, "calamaio-3f2a")
	gm := c.nextOf("game", 2*time.Second)
	validate(t, compileSchema(t), "game con special", rawOf(t, gm))
	if gm["special"] != true {
		t.Fatalf("special assente con una proposta aperta: %v", gm)
	}
}

// Gli esempi accessory.json e anchors.json sono i binari veri della libreria.
func TestGameExamplesAreReal(t *testing.T) {
	read := func(name string) map[string]any {
		b, err := os.ReadFile(filepath.Join(protocolDir, "examples", name))
		if err != nil {
			t.Fatal(err)
		}
		var m map[string]any
		_ = json.Unmarshal(b, &m)
		return m
	}
	am := read("accessory.json")
	bin, _ := base64.StdEncoding.DecodeString(am["data"].(string))
	e := accessory.NewLibrary("").Get(am["id"].(string))
	if a, err := accessory.Unpack(bin); err != nil || e == nil || a.ID != e.ID || am["crc"] != float64(e.CRC) || accessory.CRC(bin) != e.CRC {
		t.Fatalf("accessory.json: %v", err)
	}
	an := read("anchors.json")
	cb, _ := base64.StdEncoding.DecodeString(an["data"].(string))
	g := avatar.NewLibrary("").Get(an["avatar"].(string))
	if u, err := avatar.UnpackAnchors(cb); err != nil || g == nil || u.AvatarCRC != g.CRC || an["avatarCrc"] != float64(g.CRC) {
		t.Fatalf("anchors.json: %v", err)
	}
	gm := read("game.json")
	if w := gm["wear"].(map[string]any); w["crc"] != float64(accessory.NewLibrary("").Get(w["id"].(string)).CRC) {
		t.Fatalf("game.json: crc di %v", w)
	}
}
