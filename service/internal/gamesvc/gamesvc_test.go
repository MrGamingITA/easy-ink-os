package gamesvc

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"calamaio/internal/accessory"
	"calamaio/internal/game"
)

var zone = time.FixedZone("CEST", 2*3600)

func at(day, hh, mm int) time.Time { return time.Date(2026, 10, day, hh, mm, 0, 0, zone) }

type clock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *clock) now() time.Time  { c.mu.Lock(); defer c.mu.Unlock(); return c.t }
func (c *clock) set(t time.Time) { c.mu.Lock(); c.t = t; c.mu.Unlock() }

func open(t *testing.T, path string, c *clock) *Service {
	t.Helper()
	return Open(path, accessory.NewLibrary(""), nil, c.now)
}

func ev(kind string, t time.Time, key string) game.Event {
	return game.Event{At: t, Session: "s1", Kind: kind, Key: key}
}

// Riavvio del servizio: gli stessi eventi rigiocati non danno XP una seconda volta (Review Focus).
func TestRestartNoDoubleXP(t *testing.T) {
	path := filepath.Join(t.TempDir(), "game.json")
	c := &clock{t: at(12, 9, 0)}
	s := open(t, path, c)
	s.SetOn(true)
	evs := []game.Event{
		ev(game.KindTaskDone, at(12, 9, 0), "task|s1|1"),
		ev(game.KindTaskDone, at(12, 9, 5), "task|s1|2"),
		ev(game.KindTurnDone, at(12, 9, 10), "turn|s1|p1"),
	}
	for _, e := range evs {
		s.Apply(e)
	}
	xp := s.State().XP
	if xp != 5+10+10+20 {
		t.Fatalf("XP %d, attesi 45 (bonus 5 + 2 task + 1 turno)", xp)
	}
	// nessun Save esplicito: gli eventi con chiave si salvano subito
	s2 := open(t, path, c)
	for _, e := range evs {
		s2.Apply(e)
	}
	if got := s2.State().XP; got != xp {
		t.Fatalf("dopo il riavvio XP %d, prima %d: eventi contati due volte", got, xp)
	}
	if h := s2.HistoryMap()["2026-10-12"]; h != 45 {
		t.Fatalf("storico %d", h)
	}
	s2.Apply(ev(game.KindTaskDone, at(12, 9, 20), "task|s1|3"))
	if got := s2.State().XP; got != xp+10 {
		t.Fatalf("evento nuovo dopo il riavvio: %d", got)
	}
}

// Gioco spento: messaggio senza accessorio né sbloccati, nessuna proposta a Claude, progressi salvati.
func TestOffKeepsProgressNoWearNoProposal(t *testing.T) {
	path := filepath.Join(t.TempDir(), "game.json")
	c := &clock{t: at(12, 9, 0)}
	all := accessory.CatalogOrder
	st := game.State{On: true, XP: 900, Unlocked: append([]string{}, all...), Wear: "corona", SpecialPending: "task-10"}
	writeState(t, path, st)
	s := open(t, path, c)
	if m := s.Message(); m.Wear == nil || m.Wear.ID != "corona" || len(m.Unlocked) != 8 || !m.Special {
		t.Fatalf("acceso: %+v", m)
	}
	s.SetOn(false)
	m := s.Message()
	if m.On || m.Wear != nil || len(m.Unlocked) != 0 || m.XP != 900 || m.Level != game.LevelFor(900) || m.Special {
		t.Fatalf("spento: %+v", m)
	}
	if txt := s.ProposalContext(); txt != "" {
		t.Fatalf("proposta a gioco spento: %q", txt)
	}
	s.Apply(ev(game.KindTaskDone, at(12, 9, 1), "task|s1|9"))
	if s.State().XP != 900 {
		t.Fatal("XP a gioco spento")
	}
	s2 := open(t, path, c)
	if st := s2.State(); st.On || st.XP != 900 || st.Wear != "corona" || len(st.Unlocked) != 8 {
		t.Fatalf("riaperto: %+v", st)
	}
	s2.SetOn(true)
	if m := s2.Message(); m.Wear == nil || m.Wear.ID != "corona" {
		t.Fatalf("riacceso: %+v", m)
	}
	// l'evento visto da spento non si rigioca da acceso
	s2.Apply(ev(game.KindTaskDone, at(12, 9, 2), "task|s1|9"))
	if s2.State().XP != 900 {
		t.Fatalf("evento visto da spento contato da acceso: %d", s2.State().XP)
	}
}

func writeState(t *testing.T, path string, st game.State) {
	t.Helper()
	b, _ := json.Marshal(fileData{Format: FileFormat, Version: 1, State: st})
	if err := os.WriteFile(path, b, 0o600); err != nil {
		t.Fatal(err)
	}
}

func TestProposalOncePerDayLaterDecline(t *testing.T) {
	path := filepath.Join(t.TempDir(), "game.json")
	c := &clock{t: at(12, 9, 0)}
	writeState(t, path, game.State{On: true, Unlocked: append([]string{}, accessory.CatalogOrder...), SpecialPending: "task-10"})
	s := open(t, path, c)
	txt := s.ProposalContext()
	if !strings.Contains(txt, "«10 task»") || !strings.Contains(txt, "AskUserQuestion") || !strings.Contains(txt, "calamaio:accessorio") {
		t.Fatalf("testo: %q", txt)
	}
	if again := s.ProposalContext(); again != "" {
		t.Fatalf("seconda proposta lo stesso giorno: %q", again)
	}
	if err := s.SpecialAnswer(AnswerLater); err != nil {
		t.Fatal(err)
	}
	c.set(at(12, 17, 0))
	if again := s.ProposalContext(); again != "" {
		t.Fatal("più tardi: riproposta lo stesso giorno")
	}
	c.set(at(13, 9, 0))
	if again := open(t, path, c).ProposalContext(); again == "" {
		t.Fatal("più tardi: non riproposta il giorno dopo (anche dopo un riavvio)")
	}
	s = open(t, path, c)
	if err := s.SpecialAnswer(AnswerDecline); err != nil {
		t.Fatal(err)
	}
	if s.State().SpecialPending != "" || s.ProposalContext() != "" {
		t.Fatal("no: la proposta resta aperta")
	}
	if s.Message().Special {
		t.Fatal("no: special resta nel messaggio game")
	}
	if err := s.SpecialAnswer(AnswerLater); err != ErrNoSpecial {
		t.Fatalf("risposta senza proposta: %v", err)
	}
	c.set(at(14, 9, 0))
	if s.ProposalContext() != "" {
		t.Fatal("no: riproposta il giorno dopo")
	}
}

func TestAddSpecialUnlocksWearsAndClears(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "game.json")
	c := &clock{t: at(12, 9, 0)}
	writeState(t, path, game.State{On: true, Unlocked: append([]string{}, accessory.CatalogOrder...), SpecialPending: "task-10"})
	lib := accessory.NewLibrary(filepath.Join(dir, "accessories"))
	s := Open(path, lib, nil, c.now)
	ch, cancel := s.Subscribe()
	defer cancel()
	raw, _ := os.ReadFile("../../../avatars/accessori/corona.accessory.json")
	var m map[string]any
	_ = json.Unmarshal(raw, &m)
	m["id"], m["name"], m["special"] = "corona-stellata", "Corona stellata", true
	b, _ := json.Marshal(m)
	if _, _, err := lib.Add(b, false); err != nil {
		t.Fatal(err)
	}
	if err := s.AddSpecial("corona-stellata"); err != nil {
		t.Fatal(err)
	}
	st := s.State()
	if s.Message().Special {
		t.Fatal("special dopo AddSpecial")
	}
	if st.SpecialPending != "" || st.Wear != "corona-stellata" || st.Unlocked[len(st.Unlocked)-1] != "corona-stellata" {
		t.Fatalf("stato: pending %q wear %q unlocked %v", st.SpecialPending, st.Wear, st.Unlocked)
	}
	select {
	case msg := <-ch:
		if msg.Celebrate == nil || msg.Celebrate.Title != "Corona stellata" || msg.Wear == nil || msg.Wear.ID != "corona-stellata" {
			t.Fatalf("messaggio: %+v", msg)
		}
	case <-time.After(time.Second):
		t.Fatal("nessun messaggio game")
	}
	if err := s.AddSpecial("inesistente"); err == nil {
		t.Fatal("AddSpecial di un id sconosciuto")
	}
}

// game a ogni cambio (e solo allora); la festa solo nel messaggio del traguardo.
func TestMessagesOnChangeAndCelebrate(t *testing.T) {
	c := &clock{t: at(12, 9, 0)}
	s := Open("", accessory.NewLibrary(""), nil, c.now)
	ch, cancel := s.Subscribe()
	defer cancel()
	s.SetOn(true)
	first := <-ch
	if !first.On || first.Celebrate != nil {
		t.Fatalf("acceso: %+v", first)
	}
	s.Apply(ev(game.KindActivity, at(12, 9, 0), "")) // prima sessione: traguardo
	m := <-ch
	if m.Celebrate == nil || m.Celebrate.ID != "prima-sessione" || m.Celebrate.Title != "Prima sessione" {
		t.Fatalf("festa: %+v", m.Celebrate)
	}
	s.Apply(ev(game.KindActivity, at(12, 9, 0), "")) // niente di nuovo
	select {
	case m := <-ch:
		t.Fatalf("messaggio senza cambi: %+v", m)
	case <-time.After(50 * time.Millisecond):
	}
	s.Apply(ev(game.KindTaskDone, at(12, 9, 1), "a"))
	m = <-ch
	if m.Celebrate == nil || m.Celebrate.ID != "primo-task" || m.XP != 15 {
		t.Fatalf("primo task: %+v", m)
	}
	// energia che cala lavorando: un messaggio nuovo dal Tick, senza festa
	for t0 := at(12, 9, 5); t0.Before(at(12, 10, 0)); t0 = t0.Add(5 * time.Minute) {
		s.Apply(ev(game.KindActivity, t0, ""))
	}
	for len(ch) > 0 {
		<-ch
	}
	c.set(at(12, 10, 3))
	s.Tick()
	select {
	case m := <-ch:
		if m.Celebrate != nil || m.Energy >= 100 {
			t.Fatalf("tick: %+v", m)
		}
	case <-time.After(time.Second):
		t.Fatal("nessun game dopo il calo di energia")
	}
	// livello 2: festa col livello
	s.Apply(ev(game.KindTurnDone, at(12, 10, 4), "t1"))
	s.Apply(ev(game.KindTurnDone, at(12, 10, 5), "t2"))
	s.Apply(ev(game.KindTurnDone, at(12, 10, 6), "t3"))
	s.Apply(ev(game.KindTurnDone, at(12, 10, 7), "t4"))
	s.Apply(ev(game.KindTurnDone, at(12, 10, 8), "t5"))
	found := false
	for len(ch) > 0 {
		if m := <-ch; m.Celebrate != nil && (m.Celebrate.ID == "livello-2" || m.Level == 2) {
			found = true
		}
	}
	if !found || s.State().Level != 2 {
		t.Fatalf("livello 2: livello %d", s.State().Level)
	}
	if u := s.Message().Unlocked; len(u) != 1 || u[0].ID != "cappellino" || u[0].Name != "Cappellino" {
		t.Fatalf("sbloccato al livello 2: %+v", u)
	}
}

func TestBrokenFileMovedAside(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "game.json")
	_ = os.WriteFile(path, []byte("{rotto"), 0o600)
	c := &clock{t: at(12, 9, 0)}
	s := open(t, path, c)
	s.SetOn(true)
	if _, err := os.Stat(path); err != nil {
		t.Fatal("game.json nuovo non scritto")
	}
	m, _ := filepath.Glob(filepath.Join(dir, "game.json.rotto-*"))
	if len(m) != 1 {
		t.Fatalf("file rotto non messo da parte: %v", m)
	}
}

func TestWearRules(t *testing.T) {
	c := &clock{t: at(12, 9, 0)}
	s := Open("", accessory.NewLibrary(""), nil, c.now)
	s.SetOn(true)
	if err := s.Wear("corona"); err == nil {
		t.Fatal("bloccato indossato")
	}
	if err := s.Wear("boh"); err == nil {
		t.Fatal("sconosciuto indossato")
	}
	if err := s.Wear(""); err != nil {
		t.Fatal(err)
	}
}

func TestUnlockedCappedAt20(t *testing.T) {
	dir := t.TempDir()
	lib := accessory.NewLibrary(dir)
	raw, _ := os.ReadFile("../../../avatars/accessori/papillon.accessory.json")
	var unlocked []string
	for i := 0; i < 25; i++ {
		var m map[string]any
		_ = json.Unmarshal(raw, &m)
		id := "papillon-" + string(rune('a'+i))
		m["id"], m["name"] = id, "Papillon "+string(rune('A'+i))
		b, _ := json.Marshal(m)
		if _, _, err := lib.Add(b, false); err != nil {
			t.Fatal(err)
		}
		unlocked = append(unlocked, id)
	}
	path := filepath.Join(t.TempDir(), "game.json")
	writeState(t, path, game.State{On: true, Unlocked: unlocked, Wear: "papillon-a"})
	c := &clock{t: at(12, 9, 0)}
	m := Open(path, lib, nil, c.now).Message()
	if len(m.Unlocked) != MaxUnlocked || m.Unlocked[0].ID != "papillon-a" || m.Unlocked[MaxUnlocked-1].ID != "papillon-y" {
		t.Fatalf("unlocked %d: %v … %v", len(m.Unlocked), m.Unlocked[0], m.Unlocked[len(m.Unlocked)-1])
	}
}
