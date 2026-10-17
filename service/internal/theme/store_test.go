package theme

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

func newStore(t *testing.T) *Store {
	s := NewStore(t.TempDir())
	clock := time.UnixMilli(1791707400000)
	s.Now = func() time.Time { clock = clock.Add(time.Second); return clock }
	return s
}

func renamed(t *testing.T, data []byte, name string) []byte {
	var m map[string]any
	if err := json.Unmarshal(data, &m); err != nil {
		t.Fatal(err)
	}
	m["name"] = name
	b, _ := json.Marshal(m)
	return b
}

func TestStoreCreateGetList(t *testing.T) {
	s := newStore(t)
	in, err := s.Create(read(t, "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	if in.ID != "prova" || in.Bytes == 0 || in.Screens["home"] == 0 || in.CRC == 0 {
		t.Fatalf("%+v", in)
	}
	if _, err := s.Create(read(t, "prova.theme.json")); !errors.Is(err, ErrExists) {
		t.Fatalf("doppione: %v", err)
	}
	data, info, err := s.Get("prova")
	if err != nil || info.Name != "Mascotte grande" || !strings.Contains(string(data), `"prova"`) {
		t.Fatalf("get: %v %+v", err, info)
	}
	if _, _, err := s.Get("manca"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("manca: %v", err)
	}
	// un file rovinato nella cartella compare con i problemi, non blocca gli altri
	os.WriteFile(filepath.Join(s.Dir, "rotto.json"), []byte(`{"format":"x"}`), 0o644)
	list, err := s.List()
	if err != nil || len(list) != 2 || list[0].ID != "prova" || list[1].ID != "rotto" || len(list[1].Problems) == 0 {
		t.Fatalf("list: %v %+v", err, list)
	}
}

func TestStoreRejects(t *testing.T) {
	s := newStore(t)
	_, err := s.Create([]byte(`{"format":"calamaio-theme"}`))
	var ie *InvalidError
	if !errors.As(err, &ie) {
		t.Fatalf("non valido: %v", err)
	}
	if _, err := s.Put("altro", read(t, "prova.theme.json")); err == nil || !strings.Contains(err.Error(), "id") {
		t.Fatalf("id diverso dal percorso: %v", err)
	}
	if err := s.Delete(ReservedID); !errors.Is(err, ErrReserved) {
		t.Fatalf("tema di serie: %v", err)
	}
}

func TestStoreHistoryAndRestore(t *testing.T) {
	s := newStore(t)
	s.Keep = 3
	base := read(t, "prova.theme.json")
	if _, err := s.Create(base); err != nil {
		t.Fatal(err)
	}
	for i := 1; i <= 4; i++ {
		if _, err := s.Put("prova", renamed(t, base, "Versione "+string(rune('0'+i)))); err != nil {
			t.Fatal(err)
		}
	}
	h, err := s.History("prova")
	if err != nil || len(h) != 3 || h[0].N != 5 || h[2].N != 3 {
		t.Fatalf("cronologia: %v %+v", err, h)
	}
	in, err := s.Restore("prova", 3)
	if err != nil || in.Name != "Versione 2" {
		t.Fatalf("ripristino: %v %+v", err, in)
	}
	if h, _ := s.History("prova"); h[0].N != 6 { // il ripristino è una versione nuova
		t.Fatalf("dopo il ripristino: %+v", h)
	}
	if _, err := s.Restore("prova", 1); !errors.Is(err, ErrNotFound) {
		t.Fatalf("versione uscita dalla cronologia: %v", err)
	}
	if err := s.Delete("prova"); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(s.Dir, "prova")); !os.IsNotExist(err) {
		t.Fatal("cronologia rimasta")
	}
}

func TestStoreImportRenames(t *testing.T) {
	s := newStore(t)
	s.Create(read(t, "prova.theme.json"))
	in, err := s.Import(read(t, "prova.theme.json"))
	if err != nil || in.ID != "prova-2" {
		t.Fatalf("import: %v %+v", err, in)
	}
	data, _, _ := s.Get("prova-2")
	if !strings.Contains(string(data), `"id": "prova-2"`) {
		t.Fatalf("id nel file: %s", data[:80])
	}
}

func TestStoreRejectsBadIDs(t *testing.T) {
	s := newStore(t)
	base := read(t, "prova.theme.json")
	if _, err := s.Create(base); err != nil {
		t.Fatal(err)
	}
	// un file fuori dalla cartella dei temi non deve mai essere toccato
	outside := filepath.Join(filepath.Dir(s.Dir), "fuori.json")
	os.WriteFile(outside, []byte("x"), 0o644)
	bad := []string{"../fuori", "..", ".", "", "/etc/passwd", "a/b", `a\b`, "../" + filepath.Base(s.Dir) + "/prova", "Prova", "prova.json", strings.Repeat("a", 25)}
	for _, id := range bad {
		if _, _, err := s.Get(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("Get(%q): %v", id, err)
		}
		if _, err := s.Compiled(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("Compiled(%q): %v", id, err)
		}
		if _, err := s.Put(id, base); err == nil {
			t.Errorf("Put(%q) accettato", id)
		}
		if err := s.Delete(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("Delete(%q): %v", id, err)
		}
		if _, err := s.History(id); !errors.Is(err, ErrNotFound) {
			t.Errorf("History(%q): %v", id, err)
		}
		if _, err := s.Restore(id, 1); !errors.Is(err, ErrNotFound) {
			t.Errorf("Restore(%q): %v", id, err)
		}
	}
	if b, err := os.ReadFile(outside); err != nil || string(b) != "x" {
		t.Fatal("file fuori dalla cartella toccato")
	}
}

func TestStoreAtomicWrite(t *testing.T) {
	s := newStore(t)
	if _, err := s.Create(read(t, "prova.theme.json")); err != nil {
		t.Fatal(err)
	}
	var left []string
	filepath.Walk(s.Dir, func(p string, fi os.FileInfo, err error) error {
		if err == nil && strings.HasSuffix(p, ".tmp") {
			left = append(left, p)
		}
		return nil
	})
	if len(left) != 0 {
		t.Fatalf("temporanei rimasti: %v", left)
	}
	// un tema non valido non deve toccare il file buono
	before, _, _ := s.Get("prova")
	if _, err := s.Put("prova", []byte(`{"format":"x"}`)); err == nil {
		t.Fatal("accettato")
	}
	after, _, _ := s.Get("prova")
	if string(before) != string(after) {
		t.Fatal("file cambiato da un salvataggio rifiutato")
	}
}

func TestStoreDefaultKeep(t *testing.T) {
	s := newStore(t)
	base := read(t, "prova.theme.json")
	s.Create(base)
	for i := 0; i < 20; i++ {
		if _, err := s.Put("prova", renamed(t, base, "V"+strings.Repeat("x", i%5))); err != nil {
			t.Fatal(err)
		}
	}
	h, err := s.History("prova")
	if err != nil || len(h) != 20 || h[0].N != 21 || h[19].N != 2 {
		t.Fatalf("cronologia: %v %d %+v", err, len(h), h)
	}
}

func TestStoreImportThirdAndLongID(t *testing.T) {
	s := newStore(t)
	base := read(t, "prova.theme.json")
	s.Create(base)
	s.Import(base)
	in, err := s.Import(base)
	if err != nil || in.ID != "prova-3" {
		t.Fatalf("terzo: %v %+v", err, in)
	}
	var m map[string]any
	json.Unmarshal(base, &m)
	long := strings.Repeat("a", 24)
	m["id"] = long
	b, _ := json.Marshal(m)
	if _, err := s.Create(b); err != nil {
		t.Fatal(err)
	}
	in, err = s.Import(b)
	if err != nil || len(in.ID) > 24 || !validStoreID(in.ID) || in.ID != strings.Repeat("a", 22)+"-2" {
		t.Fatalf("id lungo: %v %+v", err, in)
	}
	if _, _, err := s.Get(in.ID); err != nil {
		t.Fatal(err)
	}
}

func TestStoreConcurrent(t *testing.T) {
	s := newStore(t)
	var mu sync.Mutex
	clock := time.UnixMilli(1791707400000)
	s.Now = func() time.Time { mu.Lock(); defer mu.Unlock(); clock = clock.Add(time.Second); return clock }
	base := read(t, "prova.theme.json")
	s.Create(base)
	var wg sync.WaitGroup
	for g := 0; g < 4; g++ {
		wg.Add(4)
		go func() {
			defer wg.Done()
			for i := 0; i < 15; i++ {
				s.Put("prova", renamed(t, base, "A"))
			}
		}()
		go func() {
			defer wg.Done()
			for i := 0; i < 15; i++ {
				s.Restore("prova", 1)
			}
		}()
		go func() {
			defer wg.Done()
			for i := 0; i < 15; i++ {
				s.Delete("prova")
				s.Create(base)
			}
		}()
		go func() {
			defer wg.Done()
			for i := 0; i < 15; i++ {
				if _, err := s.List(); err != nil {
					t.Error(err)
				}
				s.History("prova")
			}
		}()
	}
	wg.Wait()
}

// withPerm: il tema con in più una schermata «perm» (bloccata: non si salva mai).
func withPerm(t *testing.T, data []byte) []byte {
	var m map[string]any
	if err := json.Unmarshal(data, &m); err != nil {
		t.Fatal(err)
	}
	sc := m["screens"].(map[string]any)
	sc["perm"] = sc["home"]
	b, _ := json.Marshal(m)
	return b
}

// clockStore: archivio con orologio manuale.
func clockStore(t *testing.T) (*Store, *time.Time) {
	s := NewStore(t.TempDir())
	now := time.UnixMilli(1791707400000)
	s.Now = func() time.Time { return now }
	return s, &now
}

func TestThemeCheckDoesNotWrite(t *testing.T) {
	s := newStore(t)
	in := s.Check(read(t, "prova.theme.json"))
	if in.Screens["home"] == 0 || len(in.Problems) != 0 {
		t.Fatalf("%+v", in)
	}
	if ents, _ := os.ReadDir(s.Dir); len(ents) != 0 {
		t.Fatalf("Check ha scritto: %v", ents)
	}
}

func TestThemeCheckInvalid(t *testing.T) {
	s := newStore(t)
	in := s.Check(withPerm(t, read(t, "prova.theme.json")))
	found := false
	for _, p := range in.Problems {
		found = found || p.Path == "screens.perm"
	}
	if !found {
		t.Fatalf("%+v", in.Problems)
	}
	if in := s.Check([]byte(`{rotto`)); len(in.Problems) == 0 {
		t.Fatal("JSON rotto senza problemi")
	}
}

func TestThemeAutosaveMerges(t *testing.T) {
	s, now := clockStore(t)
	data := read(t, "prova.theme.json")
	if _, err := s.Create(data); err != nil {
		t.Fatal(err)
	}
	count := func() int { h, _ := s.History("prova"); return len(h) }
	before := count()
	t0 := *now
	for i, sec := range []int{0, 30, 90} {
		*now = t0.Add(time.Duration(sec) * time.Second)
		if _, err := s.Autosave("prova", renamed(t, data, "Nome "+strings.Repeat("x", i+1)), 0); err != nil {
			t.Fatal(err)
		}
	}
	if count() != before+1 {
		t.Fatalf("versioni: %d, attese %d", count(), before+1)
	}
	_, in, _ := s.Get("prova")
	if in.Name != "Nome xxx" {
		t.Fatalf("il file vivo non è l'ultimo: %q", in.Name)
	}
	*now = t0.Add(90*time.Second + 121*time.Second)
	if _, err := s.Autosave("prova", renamed(t, data, "Nome 4"), 0); err != nil {
		t.Fatal(err)
	}
	if count() != before+2 {
		t.Fatalf("dopo 121 s: %d", count())
	}
	// un Put manuale in mezzo: versione nuova, e l'Autosave dopo non la riscrive
	*now = now.Add(10 * time.Second)
	if _, err := s.Put("prova", renamed(t, data, "Manuale")); err != nil {
		t.Fatal(err)
	}
	if count() != before+3 {
		t.Fatalf("dopo Put: %d", count())
	}
	*now = now.Add(10 * time.Second)
	if _, err := s.Autosave("prova", renamed(t, data, "Dopo"), 0); err != nil {
		t.Fatal(err)
	}
	if count() != before+4 {
		t.Fatalf("Autosave dopo Put: %d", count())
	}
	vs, _ := s.History("prova")
	b, _ := os.ReadFile(filepath.Join(s.Dir, "prova", "4.json"))
	if vs[1].N != 4 || !strings.Contains(string(b), "Manuale") {
		t.Fatalf("la versione manuale è stata riscritta: %v %s", vs, b)
	}
}

// La finestra parte dalla creazione della versione: un salvataggio al minuto per 5 minuti fa almeno 2 versioni.
func TestThemeAutosaveWindowFixed(t *testing.T) {
	s, now := clockStore(t)
	data := read(t, "prova.theme.json")
	s.Create(data)
	before := func() int { h, _ := s.History("prova"); return len(h) }()
	t0 := *now
	for i := 0; i <= 5; i++ {
		*now = t0.Add(time.Duration(i) * time.Minute)
		if _, err := s.Autosave("prova", renamed(t, data, "N"+strings.Repeat("x", i)), 0); err != nil {
			t.Fatal(err)
		}
	}
	h, _ := s.History("prova")
	if len(h)-before < 2 {
		t.Fatalf("solo %d versioni nuove", len(h)-before)
	}
}

func TestThemeAutosaveConflict(t *testing.T) {
	s, now := clockStore(t)
	data := read(t, "prova.theme.json")
	in, _ := s.Create(data)
	old := in.Updated
	*now = now.Add(5 * time.Second)
	in2, err := s.Put("prova", renamed(t, data, "Altrove"))
	if err != nil {
		t.Fatal(err)
	}
	before, _ := os.ReadFile(s.file("prova"))
	if _, err := s.Autosave("prova", renamed(t, data, "Mio"), old); !errors.Is(err, ErrChanged) {
		t.Fatalf("atteso ErrChanged: %v", err)
	}
	after, _ := os.ReadFile(s.file("prova"))
	if string(before) != string(after) {
		t.Fatal("il file è cambiato")
	}
	if _, err := s.Autosave("prova", renamed(t, data, "Mio"), in2.Updated); err != nil {
		t.Fatal(err)
	}
	// file sparito con base > 0: 404, non lo si ricrea; con base 0 sì
	s.Delete("prova")
	if _, err := s.Autosave("prova", data, in2.Updated); !errors.Is(err, ErrNotFound) {
		t.Fatalf("file mancante: %v", err)
	}
	if _, err := s.Autosave("prova", data, 0); err != nil {
		t.Fatal(err)
	}
}

func TestThemeAutosaveRejectsPerm(t *testing.T) {
	s := newStore(t)
	data := read(t, "prova.theme.json")
	s.Create(data)
	before, _ := os.ReadFile(s.file("prova"))
	if _, err := s.Autosave("prova", withPerm(t, data), 0); err == nil {
		t.Fatal("perm accettata")
	}
	after, _ := os.ReadFile(s.file("prova"))
	if string(before) != string(after) {
		t.Fatal("file cambiato")
	}
}

// Cancellare e ricreare non lascia un segno vecchio: la versione manuale non si riscrive.
func TestThemeAutosaveStaleMarkAfterDelete(t *testing.T) {
	s, now := clockStore(t)
	data := read(t, "prova.theme.json")
	s.Create(data)
	*now = now.Add(time.Second)
	s.Autosave("prova", renamed(t, data, "Auto"), 0) // v2, segno n=2
	if err := s.Delete("prova"); err != nil {
		t.Fatal(err)
	}
	*now = now.Add(time.Second)
	s.Create(data) // v1
	*now = now.Add(time.Second)
	s.Put("prova", renamed(t, data, "Manuale importante")) // v2
	*now = now.Add(30 * time.Second)
	s.Autosave("prova", renamed(t, data, "Auto dopo"), 0)
	b, _ := os.ReadFile(filepath.Join(s.Dir, "prova", "2.json"))
	if !strings.Contains(string(b), "Manuale importante") {
		t.Fatalf("versione manuale riscritta: %s", b)
	}
	if h, _ := s.History("prova"); len(h) != 3 {
		t.Fatalf("versioni: %d", len(h))
	}
}

// Un file di versione toccato fuori dall'Autosave non si riscrive (il segno non combacia più).
func TestThemeAutosaveMarkMustMatchFile(t *testing.T) {
	s, now := clockStore(t)
	data := read(t, "prova.theme.json")
	s.Create(data)
	s.Autosave("prova", renamed(t, data, "Auto"), 0)
	*now = now.Add(10 * time.Second)
	hist := filepath.Join(s.Dir, "prova", "2.json")
	os.WriteFile(hist, []byte("{}"), 0o644)
	os.Chtimes(hist, now.Add(time.Hour), now.Add(time.Hour))
	s.Autosave("prova", renamed(t, data, "Dopo"), 0)
	if b, _ := os.ReadFile(hist); string(b) != "{}" {
		t.Fatalf("riscritta: %s", b)
	}
}

func TestThemeAutosaveConcurrentSameBase(t *testing.T) {
	s := newStore(t)
	data := read(t, "prova.theme.json")
	in, _ := s.Create(data)
	var wg sync.WaitGroup
	var mu sync.Mutex
	ok, changed := 0, 0
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			_, err := s.Autosave("prova", renamed(t, data, "N"+strings.Repeat("x", i+1)), in.Updated)
			mu.Lock()
			defer mu.Unlock()
			if err == nil {
				ok++
			} else if errors.Is(err, ErrChanged) {
				changed++
			}
		}(i)
	}
	wg.Wait()
	if ok != 1 || changed != 7 {
		t.Fatalf("ok=%d changed=%d", ok, changed)
	}
}

func TestThemeAutosaveMergeKeepsLast(t *testing.T) {
	s, now := clockStore(t)
	data := read(t, "prova.theme.json")
	s.Create(data)
	for i, n := range []string{"Uno", "Due", "Tre"} {
		*now = now.Add(time.Duration(i+1) * 10 * time.Second)
		s.Autosave("prova", renamed(t, data, n), 0)
	}
	b, _ := os.ReadFile(filepath.Join(s.Dir, "prova", "2.json"))
	live, _ := os.ReadFile(s.file("prova"))
	if !strings.Contains(string(b), `"Tre"`) || string(b) != string(live) {
		t.Fatalf("la versione unita non ha l'ultimo salvataggio:\n%s", b)
	}
}

// Chiave «screens» doppia: su disco (e in /export) non resta la schermata Permessi.
func TestStoreDuplicateKeyNeverStoresPerm(t *testing.T) {
	var m map[string]json.RawMessage
	json.Unmarshal(read(t, "prova.theme.json"), &m)
	good := string(m["screens"])
	delete(m, "screens")
	rest, _ := json.Marshal(m)
	evil := `{"screens":{"perm":{"variants":[]}},"screens":` + good + `,` + string(rest)[1:]
	for _, how := range []string{"put", "autosave", "import", "create"} {
		s := newStore(t)
		var err error
		switch how {
		case "put":
			_, err = s.Put("prova", []byte(evil))
		case "autosave":
			_, err = s.Autosave("prova", []byte(evil), 0)
		case "import":
			_, err = s.Import([]byte(evil))
		case "create":
			_, err = s.Create([]byte(evil))
		}
		if err != nil {
			t.Fatalf("%s: %v", how, err)
		}
		b, _ := os.ReadFile(s.file("prova"))
		var out struct {
			Screens map[string]json.RawMessage `json:"screens"`
		}
		if json.Unmarshal(b, &out) != nil || strings.Count(string(b), `"screens"`) != 1 || out.Screens["perm"] != nil || out.Screens["home"] == nil {
			t.Fatalf("%s: schermate su disco: %s", how, b)
		}
	}
}

// «Sovrascrivi» entro la finestra: con fresh la versione dell'altro dispositivo resta in cronologia e si ripristina.
func TestThemeAutosaveFreshKeepsOtherVersion(t *testing.T) {
	s, now := clockStore(t)
	data := read(t, "prova.theme.json")
	in, _ := s.Create(data)
	stale := in.Updated
	*now = now.Add(5 * time.Second)
	// il Mac salva in automatico: versione nuova col segno dell'unione
	mac, err := s.Autosave("prova", renamed(t, data, "Mac"), stale)
	if err != nil {
		t.Fatal(err)
	}
	h, _ := s.History("prova")
	if len(h) != 2 {
		t.Fatalf("versioni prima: %d", len(h))
	}
	*now = now.Add(10 * time.Second)
	// l'iPhone con la base vecchia: 409
	if _, err := s.Autosave("prova", renamed(t, data, "iPhone"), stale); !errors.Is(err, ErrChanged) {
		t.Fatalf("atteso ErrChanged: %v", err)
	}
	// fresh non salta il controllo della base
	if _, err := s.AutosaveFresh("prova", renamed(t, data, "iPhone"), stale); !errors.Is(err, ErrChanged) {
		t.Fatalf("fresh con base vecchia: %v", err)
	}
	// «Sovrascrivi»: base viva, fresh
	if _, err := s.AutosaveFresh("prova", renamed(t, data, "iPhone"), mac.Updated); err != nil {
		t.Fatal(err)
	}
	h, _ = s.History("prova")
	if len(h) != 3 {
		t.Fatalf("versioni dopo: %d, attese 3", len(h))
	}
	_, live, _ := s.Get("prova")
	if live.Name != "iPhone" {
		t.Fatalf("file vivo: %q", live.Name)
	}
	// la versione del Mac si ripristina
	r, err := s.Restore("prova", h[1].N)
	if err != nil {
		t.Fatal(err)
	}
	if r.Name != "Mac" {
		t.Fatalf("ripristino: %q", r.Name)
	}
}
