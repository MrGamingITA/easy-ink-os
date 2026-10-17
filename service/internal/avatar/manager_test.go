package avatar

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"calamaio/internal/config"
)

func memManager(t *testing.T, userDir string) *Manager {
	t.Helper()
	m := NewManager(NewLibrary(userDir), &config.AvatarStore{})
	m.Home = t.TempDir()
	return m
}

// Stessa definizione di usage()/freeChoices() di tools/avatar/lib/format.mjs, sulla stessa fixture
// (tools/avatar/test/stato-esempio.json).
func TestUsageMatchesNodeFixture(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(repoRoot, "tools", "avatar", "test", "stato-esempio.json"))
	if err != nil {
		t.Skip("fixture non trovata")
	}
	var st struct {
		Assignments config.AvatarAssignments `json:"assignments"`
		Sessions    []struct {
			ID, Name, Cwd string
			Archived      bool
		} `json:"sessions"`
	}
	if err := json.Unmarshal(b, &st); err != nil {
		t.Fatal(err)
	}
	m := memManager(t, "")
	if err := m.Assign.Update(func(a *config.AvatarAssignments) error { *a = st.Assignments.Clone(); return nil }); err != nil {
		t.Fatal(err)
	}
	var ss []SessionInfo
	for _, s := range st.Sessions {
		ss = append(ss, SessionInfo{ID: s.ID, Name: s.Name, Cwd: s.Cwd, Archived: s.Archived})
	}
	list, free, def := m.Usage(ss, "sess-corrente")
	u := map[string]Use{}
	for _, x := range list {
		u[x.ID] = x
	}
	if u["gatto"].State != "in-uso" || strings.Join(u["gatto"].UsedBy, ",") != "api-server" {
		t.Errorf("gatto: %+v", u["gatto"]) // per progetto
	}
	if u["demone"].State != "libero" { // solo una sessione archiviata
		t.Errorf("demone: %+v", u["demone"])
	}
	if u["calamaro"].State != "libero" { // assegnato solo alla sessione corrente
		t.Errorf("calamaro: %+v", u["calamaro"])
	}
	if u["goccia"].State != "in-uso" || !u["goccia"].Default || def != "goccia" { // web-app non ha assegnazioni
		t.Errorf("goccia: %+v", u["goccia"])
	}
	if strings.Join(free, ",") != "calamaro,demone" { // Node: [demone, calamaro] (ordine della cartella); qui per id
		t.Errorf("liberi: %v", free)
	}
	_, free, _ = m.Usage(nil, "")
	for _, id := range free {
		if id == "goccia" {
			t.Fatal("la Goccia non va mai proposta")
		}
	}
}

// Il predefinito cambiabile: vale per le sessioni senza assegnazione e non è mai proposto come libero.
func TestDefaultAvatar(t *testing.T) {
	m := memManager(t, "")
	if m.DefaultEntry().ID != "goccia" {
		t.Fatal("predefinito iniziale")
	}
	if err := m.SetDefault("gatto"); err != nil {
		t.Fatal(err)
	}
	if r := m.Resolve("s", "/x"); r.Entry.ID != "gatto" || r.From != FromDefault {
		t.Fatalf("%+v", r)
	}
	list, free, def := m.Usage(nil, "")
	if def != "gatto" || strings.Contains(strings.Join(free, ","), "gatto") || !strings.Contains(strings.Join(free, ","), "goccia") {
		t.Fatalf("liberi %v, predefinito %s", free, def)
	}
	for _, u := range list {
		if u.Default != (u.ID == "gatto") {
			t.Fatalf("%+v", u)
		}
	}
	if err := m.SetDefault("nessuno"); !errors.Is(err, ErrUnknown) {
		t.Fatalf("predefinito sconosciuto: %v", err)
	}
	if err := m.SetDefault("goccia"); err != nil || m.Assignments().Default != "" {
		t.Fatalf("goccia = valore vuoto: %v %+v", err, m.Assignments())
	}
}

func TestResolvePrecedenceAndGitRoot(t *testing.T) {
	m := memManager(t, "")
	repo := t.TempDir()
	sub := filepath.Join(repo, "a", "b")
	if err := os.MkdirAll(sub, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.Mkdir(filepath.Join(repo, ".git"), 0o755); err != nil {
		t.Fatal(err)
	}
	plain := t.TempDir()
	if got := m.ProjectDir(sub); got != NormDir(repo) {
		t.Fatalf("radice git di %s: %s", sub, got)
	}
	if got := m.ProjectDir(plain); got != NormDir(plain) {
		t.Fatalf("senza git: %s", got)
	}
	// assegnare da una sottocartella assegna tutto il repository
	dir, err := m.AssignProject(sub, "demone")
	if err != nil || dir != NormDir(repo) {
		t.Fatalf("%s %v", dir, err)
	}
	if r := m.Resolve("s1", filepath.Join(repo, "a")); r.Entry.ID != "demone" || r.From != FromProject || r.Project != NormDir(repo) {
		t.Fatalf("progetto: %+v", r)
	}
	if err := m.AssignSession("s1", "calamaro"); err != nil {
		t.Fatal(err)
	}
	if r := m.Resolve("s1", sub); r.Entry.ID != "calamaro" || r.From != FromSession {
		t.Fatalf("sessione vince: %+v", r)
	}
	if !m.HasAssignment("s2", sub) || m.HasAssignment("s2", plain) || !m.HasAssignment("s1", plain) {
		t.Fatal("HasAssignment")
	}
	// avatar sparito dalla libreria = assegnazione assente
	_ = m.Assign.Update(func(a *config.AvatarAssignments) error { a.Sessions["s3"] = "fantasma"; return nil })
	if r := m.Resolve("s3", plain); r.Entry.ID != "goccia" || r.From != FromDefault {
		t.Fatalf("fantasma: %+v", r)
	}
	if !m.ForgetSession("s1") || m.ForgetSession("s1") {
		t.Fatal("ForgetSession")
	}
	if r := m.Resolve("s1", sub); r.From != FromProject {
		t.Fatalf("dopo SessionEnd: %+v", r)
	}
	if _, err := m.AssignProject(sub, ""); err != nil || m.Resolve("", sub).From != FromDefault {
		t.Fatal("unassign progetto")
	}
	if _, err := m.AssignProject("relativa", "gatto"); !errors.Is(err, ErrBadTarget) {
		t.Fatalf("percorso relativo: %v", err)
	}
	if err := m.AssignSession("a b", "gatto"); !errors.Is(err, ErrBadTarget) {
		t.Fatalf("sessione con spazio: %v", err)
	}
	if err := m.AssignSession("s9", "boh"); !errors.Is(err, ErrUnknown) {
		t.Fatalf("avatar sconosciuto: %v", err)
	}
}

// Una home tenuta in git non fa di tutta la home un unico progetto.
func TestHomeGitIgnored(t *testing.T) {
	home := t.TempDir()
	_ = os.Mkdir(filepath.Join(home, ".git"), 0o755)
	p := filepath.Join(home, "src", "web")
	_ = os.MkdirAll(p, 0o755)
	m := memManager(t, "")
	m.Home = home
	if got := m.ProjectDir(p); got != NormDir(p) {
		t.Fatalf("%s", got)
	}
}

func TestLibraryAdd(t *testing.T) {
	dir := filepath.Join(t.TempDir(), "avatars")
	lib := NewLibrary(dir)
	raw := readLib(t, "gatto")
	m := generic(t, raw)
	m["id"], m["name"] = "gatto-blu", "Gatto blu"
	blue := enc(t, m)

	if _, _, err := lib.Add(raw, true); !errors.Is(err, ErrReserved) {
		t.Fatalf("predefinito sostituito: %v", err)
	}
	e, _, err := lib.Add(blue, false)
	if err != nil || e.Source != SourceUser || e.File != filepath.Join(dir, "gatto-blu.avatar.json") {
		t.Fatalf("%+v %v", e, err)
	}
	if fi, _ := os.Stat(dir); fi.Mode().Perm() != 0o700 {
		t.Errorf("cartella %v", fi.Mode())
	}
	if _, _, err := lib.Add(blue, false); !errors.Is(err, ErrExists) {
		t.Fatalf("doppione: %v", err)
	}
	m["name"] = "Gatto blu 2"
	if e, _, err := lib.Add(enc(t, m), true); err != nil || lib.Get("gatto-blu").Name != "Gatto blu 2" || e.CRC == 0 {
		t.Fatalf("replace: %v", err)
	}
	bad := generic(t, raw)
	bad["id"] = "rotto"
	delete(poses(bad), "idle")
	if _, r, err := lib.Add(enc(t, bad), false); !errors.Is(err, ErrInvalid) || len(r.Errors) == 0 {
		t.Fatalf("non valido: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "rotto.avatar.json")); err == nil {
		t.Fatal("un avatar non valido è stato copiato")
	}
	// file messi a mano: id diverso dal nome, id di un predefinito, JSON rotto → Problems, non caricati
	_ = os.WriteFile(filepath.Join(dir, "altro.avatar.json"), blue, 0o644)
	_ = os.WriteFile(filepath.Join(dir, "gatto.avatar.json"), raw, 0o644)
	_ = os.WriteFile(filepath.Join(dir, "rotto.avatar.json"), []byte("{"), 0o644)
	lib.Invalidate()
	cat := lib.Catalog()
	if len(cat.Problems) != 3 {
		t.Fatalf("problemi: %+v", cat.Problems)
	}
	if g := cat.Get("gatto"); g == nil || g.Source != SourceBuiltin {
		t.Fatal("il gatto predefinito è stato sostituito")
	}
	ids := []string{}
	for _, e := range cat.Entries {
		ids = append(ids, e.ID)
	}
	if strings.Join(ids, ",") != "goccia,calamaro,demone,gatto,gatto-blu" {
		t.Fatalf("ordine: %v", ids)
	}
	// rilettura dopo una modifica fatta a mano (TTL)
	lib.TTL = time.Millisecond
	_ = os.Remove(filepath.Join(dir, "gatto-blu.avatar.json"))
	time.Sleep(5 * time.Millisecond)
	if lib.Get("gatto-blu") != nil {
		t.Fatal("file tolto ancora in libreria")
	}
}

// Le assegnazioni in config.json non toccano le altre voci, e convivono con KeyStore.
func TestAssignmentsInConfigFile(t *testing.T) {
	p := filepath.Join(t.TempDir(), "config.json")
	if err := os.WriteFile(p, []byte(`{"api_port": 9999, "tz": "UTC0", "paired": []}`), 0o600); err != nil {
		t.Fatal(err)
	}
	st := &config.AvatarStore{Path: p}
	m := NewManager(NewLibrary(""), st)
	if err := m.SetDefault("demone"); err != nil {
		t.Fatal(err)
	}
	if _, err := m.AssignProject("/progetto/web", "gatto"); err != nil {
		t.Fatal(err)
	}
	ks := &config.KeyStore{Path: p}
	if err := ks.Put("calamaio-3f2a", make([]byte, 32), time.Now()); err != nil {
		t.Fatal(err)
	}
	if err := m.AssignSession("s1", "calamaro"); err != nil {
		t.Fatal(err)
	}
	b, _ := os.ReadFile(p)
	var raw map[string]json.RawMessage
	if err := json.Unmarshal(b, &raw); err != nil {
		t.Fatal(err)
	}
	if string(raw["api_port"]) != "9999" || string(raw["tz"]) != `"UTC0"` || len(raw["paired"]) < 10 {
		t.Fatalf("voci perse: %s", b)
	}
	var a config.AvatarAssignments
	_ = json.Unmarshal(raw["avatars"], &a)
	if a.Default != "demone" || a.Projects["/progetto/web"] != "gatto" || a.Sessions["s1"] != "calamaro" {
		t.Fatalf("avatars: %s", raw["avatars"])
	}
	if fi, _ := os.Stat(p); fi.Mode().Perm() != 0o600 {
		t.Fatalf("permessi %v", fi.Mode())
	}
	// un altro processo cambia il file: Get lo rilegge
	a.Default = "gatto"
	nb, _ := json.Marshal(a)
	raw["avatars"] = nb
	out, _ := json.MarshalIndent(raw, "", "  ")
	time.Sleep(10 * time.Millisecond)
	_ = os.WriteFile(p, append(out, ' ', '\n'), 0o600)
	if m.DefaultEntry().ID != "gatto" {
		t.Fatal("modifica esterna non vista")
	}
}
