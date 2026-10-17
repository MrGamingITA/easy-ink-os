package display

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/avatar"
	"calamaio/internal/config"
	"calamaio/internal/state"
)

func avatarRig(t *testing.T) (*rig, *avatar.Manager) {
	t.Helper()
	m := avatar.NewManager(avatar.NewLibrary(""), &config.AvatarStore{})
	r := newRig(t, func(h *Hub) { h.Avatars = m })
	r.store.Avatars = m
	return r, m
}

// Avatar sul filo: welcome e state portano id e CRC, avatar-need riceve i binari, che rispettano lo
// schema e coincidono con il CAVT della libreria. Il firmware 0.7.0 riceve il CAVT con la posa run;
// quello ≤ 0.6.0 (che rifiuta il bit 9) il CAVT di prima, con i CRC di prima in welcome e state.
func TestAvatarProtocol(t *testing.T) {
	t.Run("fw 0.7.0 con run", func(t *testing.T) { avatarProtocol(t, "0.7.0", false) })
	t.Run("fw 0.6.0 senza run", func(t *testing.T) { avatarProtocol(t, "0.6.0", true) })
	t.Run("fw 0.3.0 senza run", func(t *testing.T) { avatarProtocol(t, "0.3.0", true) })
}

// CRC del CAVT senza run (docs/AVATAR.md §12.3), quello che il firmware ≤ 0.6.0 conosce già.
var legacyCRC = map[string]uint32{"goccia": 0x0386CC22, "gatto": 0xD6AEBFCB, "demone": 0xC6E4FA9B, "calamaro": 0xC2FB3686}
var runCRC = map[string]uint32{"goccia": 0x33072CEE, "gatto": 0xB2432AD7, "demone": 0xA3130345, "calamaro": 0x2D86BCB3}

func avatarProtocol(t *testing.T, fw string, legacy bool) {
	sch := compileSchema(t)
	r, m := avatarRig(t)
	if err := m.AssignSession("s1", "gatto"); err != nil {
		t.Fatal(err)
	}
	if err := m.SetDefault("demone"); err != nil {
		t.Fatal(err)
	}
	r.store.Update(func(s *state.Snapshot) {
		s.Sessions = []state.Session{
			{ID: "s1", Name: "uno", State: state.Working, Tasks: []state.Task{}, Cwd: "/tmp/uno"},
			{ID: "s2", Name: "due", State: state.Idle, Tasks: []state.Task{}, Cwd: "/tmp/due"},
		}
	})
	c := r.dial(nil)
	c.send(strings.Replace(hello("calamaio-3f2a"), `"fw":"0.3.0"`, `"fw":"`+fw+`"`, 1))
	w := c.next(2 * time.Second)
	wb, _ := json.Marshal(w)
	validate(t, sch, "welcome con avatar", wb)
	crcOf := func(id string) float64 {
		if legacy {
			if m.Lib.Get(id).CRCLegacy != legacyCRC[id] {
				t.Fatalf("%s: CRC senza run %08x", id, m.Lib.Get(id).CRCLegacy)
			}
			return float64(legacyCRC[id])
		}
		if m.Lib.Get(id).CRC != runCRC[id] {
			t.Fatalf("%s: CRC con run %08x", id, m.Lib.Get(id).CRC)
		}
		return float64(runCRC[id])
	}
	binOf := func(id string) []byte {
		if legacy {
			return m.Lib.Get(id).BinLegacy
		}
		return m.Lib.Get(id).Bin
	}
	if w["defaultAvatar"] != "demone" || w["defaultAvatarCrc"] != crcOf("demone") {
		t.Fatalf("welcome: %s", wb)
	}
	st := c.next(2 * time.Second)
	sb, _ := json.Marshal(st)
	validate(t, sch, "state con avatar", sb)
	if bytes.Contains(sb, []byte("/tmp/uno")) || bytes.Contains(sb, []byte(`"cwd"`)) {
		t.Fatalf("la cartella della sessione è finita al display: %s", sb)
	}
	ss := st["sessions"].([]any)
	if a := ss[0].(map[string]any); a["avatar"] != "gatto" || a["avatarCrc"] != crcOf("gatto") {
		t.Fatalf("sessione 1: %v", a)
	}
	if a := ss[1].(map[string]any); a["avatar"] != "demone" || a["avatarCrc"] != crcOf("demone") {
		t.Fatalf("sessione 2 (predefinito): %v", a)
	}
	if st["defaultAvatar"] != "demone" || st["defaultAvatarCrc"] != crcOf("demone") {
		t.Fatalf("state senza predefinito: %s", sb)
	}

	c.send(`{"t":"avatar-need","v":1,"ids":["gatto","demone","inesistente"]}`)
	for _, id := range []string{"gatto", "demone"} {
		a := c.next(2 * time.Second)
		ab, _ := json.Marshal(a)
		validate(t, sch, "avatar "+id, ab)
		if a["t"] != "avatar" || a["id"] != id || a["crc"] != crcOf(id) {
			t.Fatalf("avatar %s: %.200s", id, ab)
		}
		if len(ab) > MaxFrame {
			t.Fatalf("frame di %d byte oltre %d", len(ab), MaxFrame)
		}
		bin, err := base64.StdEncoding.DecodeString(a["data"].(string))
		if err != nil || !bytes.Equal(bin, binOf(id)) {
			t.Fatalf("binario di %s diverso", id)
		}
		back, err := avatar.Unpack(bin)
		if err != nil || back.ID != id || (back.Poses["run"] != nil) == legacy {
			t.Fatalf("unpack %s: %v (posa run presente: %v)", id, err, back != nil && back.Poses["run"] != nil)
		}
		if mask := uint16(bin[16]) | uint16(bin[17])<<8; legacy && mask>>9 != 0 {
			t.Fatalf("%s: poseMask %#x con il bit 9 per un firmware che non lo conosce", id, mask)
		}
		t.Logf("avatar %s: frame di %d byte", id, len(ab))
	}
	c.none(300 * time.Millisecond) // «inesistente»: nessuna risposta
	if !strings.Contains(r.log.String(), `"inesistente" sconosciuto`) {
		t.Fatalf("log: %s", r.log.String())
	}
	// la stessa richiesta subito dopo non rimanda niente (difesa da un ciclo)
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c.none(300 * time.Millisecond)
	// richieste malformate: ignorate e contate
	before := r.hub.Ignored()
	for _, bad := range []string{`{"t":"avatar-need","v":1,"ids":[]}`, `{"t":"avatar-need","v":1,"ids":["Gatto"]}`,
		`{"t":"avatar-need","v":1}`, `{"t":"avatar-need","v":1,"ids":"gatto"}`} {
		c.send(bad)
	}
	c.none(300 * time.Millisecond)
	if got := r.hub.Ignored() - before; got != 4 {
		t.Fatalf("ignorati %d su 4", got)
	}

	// un cambio di assegnazione arriva con uno state nuovo (Touch)
	if err := m.AssignSession("s2", "calamaro"); err != nil {
		t.Fatal(err)
	}
	r.store.Touch()
	st = c.next(2 * time.Second)
	if a := st["sessions"].([]any)[1].(map[string]any); st["t"] != "state" || a["avatar"] != "calamaro" || a["avatarCrc"] != crcOf("calamaro") {
		t.Fatalf("dopo l'assegnazione: %v", st)
	}
}

// Un avatar-need prima di hello si ignora, come gli altri messaggi.
func TestAvatarNeedBeforeHello(t *testing.T) {
	r, _ := avatarRig(t)
	c := r.dial(nil)
	c.send(`{"t":"avatar-need","v":1,"ids":["gatto"]}`)
	c.none(300 * time.Millisecond)
	if !strings.Contains(r.log.String(), "prima di hello") {
		t.Fatalf("log: %s", r.log.String())
	}
}

// Senza libreria (servizio vecchio nei test): niente campi avatar, welcome come prima.
func TestNoAvatarsNoFields(t *testing.T) {
	r := newRig(t, nil)
	c := r.dial(nil)
	c.send(hello("calamaio-3f2a"))
	w := c.next(2 * time.Second)
	if _, ok := w["defaultAvatar"]; ok {
		t.Fatalf("welcome: %v", w)
	}
}

// Il caso peggiore (10 pose × 4 fotogrammi con run, id e CRC massimi) sta nel frame di 15 KB.
func TestAvatarWorstCaseFrame(t *testing.T) {
	m := AvatarMsg{T: "avatar", V: 1, ID: strings.Repeat("a", 24), CRC: 0xFFFFFFFF,
		Data: base64.StdEncoding.EncodeToString(make([]byte, avatar.PackedSize(10, 4, avatar.W, avatar.H)))}
	b, _ := json.Marshal(m)
	if len(b) > MaxFrame || len(m.Data) != 9432 {
		t.Fatalf("frame %d byte, data %d caratteri", len(b), len(m.Data))
	}
	t.Logf("caso peggiore: %d byte su %d", len(b), MaxFrame)
}

// L'esempio protocol/examples/avatar.json è il demone vero della libreria.
func TestAvatarExampleIsReal(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(protocolDir, "examples", "avatar.json"))
	if err != nil {
		t.Fatal(err)
	}
	var m AvatarMsg
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatal(err)
	}
	bin, _ := base64.StdEncoding.DecodeString(m.Data)
	lib := avatar.NewLibrary("")
	e := lib.Get(m.ID)
	if e == nil || !bytes.Equal(bin, e.Bin) || m.CRC != e.CRC || len(bin) != 7072 {
		t.Fatalf("esempio avatar.json diverso dal %s della libreria", m.ID)
	}
	// avatar-senza-run.json: lo stesso per il firmware ≤ 0.6.0 (CAVT di prima, 6 372 byte).
	b, err = os.ReadFile(filepath.Join(protocolDir, "examples", "avatar-senza-run.json"))
	if err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatal(err)
	}
	bin, _ = base64.StdEncoding.DecodeString(m.Data)
	if !bytes.Equal(bin, e.BinLegacy) || m.CRC != e.CRCLegacy || m.CRC != 0xC6E4FA9B || len(bin) != 6372 {
		t.Fatalf("esempio avatar-senza-run.json diverso dal %s senza run", m.ID)
	}
}
