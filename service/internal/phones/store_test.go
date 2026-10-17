package phones

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestStoreAddLookupRevoke(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sub", "phones.json")
	s, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	tok, p, err := s.Add("  iPhone di Prova\n ")
	if err != nil {
		t.Fatal(err)
	}
	if !ValidToken(tok) || p.Name != "iPhone di Prova" || p.ID == "" || p.Paired == 0 || len(p.TokenHash) != 64 {
		t.Fatalf("%q %+v", tok, p)
	}
	if got, ok := s.Lookup(tok); !ok || got.ID != p.ID {
		t.Fatalf("lookup: %v %+v", ok, got)
	}
	for _, bad := range []string{"", strings.ToUpper(tok), tok[:63], tok + "0", strings.Repeat("0", 64), p.TokenHash} {
		if _, ok := s.Lookup(bad); ok {
			t.Fatalf("token falso accettato: %q", bad)
		}
	}
	// sul disco solo l'hash, file 0600 in una cartella 0700
	b, _ := os.ReadFile(path)
	if strings.Contains(string(b), tok) || !strings.Contains(string(b), p.TokenHash) {
		t.Fatalf("phones.json: %s", b)
	}
	if st, _ := os.Stat(path); st.Mode().Perm() != 0o600 {
		t.Fatalf("permessi del file %v", st.Mode().Perm())
	}
	if st, _ := os.Stat(filepath.Dir(path)); st.Mode().Perm() != 0o700 {
		t.Fatalf("permessi della cartella %v", st.Mode().Perm())
	}
	s2, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := s2.Lookup(tok); !ok || !s2.Has(p.ID) {
		t.Fatal("telefono perso al riavvio")
	}
	if err := s2.Revoke(p.ID); err != nil {
		t.Fatal(err)
	}
	if _, ok := s2.Lookup(tok); ok || s2.Has(p.ID) {
		t.Fatal("revocato ma ancora valido")
	}
	if err := s2.Revoke(p.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("seconda revoca: %v", err)
	}
	if s3, _ := Open(path); s3.Len() != 0 {
		t.Fatal("revoca non salvata")
	}
}

func TestStoreTouchThrottled(t *testing.T) {
	path := filepath.Join(t.TempDir(), "phones.json")
	now := time.UnixMilli(1_791_000_000_000)
	s, _ := Open(path)
	s.Now = func() time.Time { return now }
	tok, p, _ := s.Add("x")
	disk := func() int64 { d, _ := Open(path); return d.List()[0].LastUsed }

	now = now.Add(time.Minute)
	s.Touch(p.ID)
	if got, _ := s.Lookup(tok); got.LastUsed != now.UnixMilli() {
		t.Fatal("ultimo uso non aggiornato in memoria")
	}
	if disk() != now.UnixMilli() {
		t.Fatal("il primo uso va scritto subito")
	}
	first := now
	now = now.Add(5 * time.Minute)
	s.Touch(p.ID)
	if disk() != first.UnixMilli() {
		t.Fatal("scritto prima di 10 minuti")
	}
	now = now.Add(6 * time.Minute)
	s.Touch(p.ID)
	if disk() != now.UnixMilli() {
		t.Fatal("non scritto dopo 10 minuti")
	}
	now = now.Add(time.Minute)
	s.Touch(p.ID)
	if err := s.Flush(); err != nil || disk() != now.UnixMilli() {
		t.Fatalf("Flush: %v", err)
	}
}

func TestStoreBrokenFileNotOverwritten(t *testing.T) {
	path := filepath.Join(t.TempDir(), "phones.json")
	os.WriteFile(path, []byte("{rotto"), 0o600)
	if _, err := Open(path); err == nil {
		t.Fatal("file rotto accettato")
	}
	if b, _ := os.ReadFile(path); string(b) != "{rotto" {
		t.Fatal("file rotto toccato")
	}
}

func TestCleanName(t *testing.T) {
	cases := map[string]string{
		"":                       "iPhone",
		"   ":                    "iPhone",
		"iPhone\x00 di\tProva\n": "iPhone diProva",
		strings.Repeat("è", 50):  strings.Repeat("è", 40),
	}
	for in, want := range cases {
		if got := cleanName(in); got != want {
			t.Errorf("cleanName(%q) = %q, atteso %q", in, got, want)
		}
	}
}
