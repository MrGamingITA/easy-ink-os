package config

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// KeyStore tocca solo "paired": le altre voci restano com'erano, il file resta 0600, e Save (setup
// claude) non perde le chiavi.
func TestKeyStoreKeepsOtherEntries(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("CALAMAIO_CONFIG_DIR", dir)
	t.Setenv("CALAMAIO_API_PORT", "9999")
	p := filepath.Join(dir, "config.json")
	orig := `{"api_port": 8765, "tz": "UTC0", "futura": {"x": [1, 2]}}`
	if err := os.WriteFile(p, []byte(orig), 0o644); err != nil {
		t.Fatal(err)
	}
	ks := &KeyStore{Path: p}
	K := bytes.Repeat([]byte{7}, 32)
	if err := ks.Put("calamaio-3f2a", K, time.Unix(0, 0)); err != nil {
		t.Fatal(err)
	}
	if err := ks.Put("calamaio-3f2a", K, time.Unix(1, 0)); err != nil { // sostituisce, non duplica
		t.Fatal(err)
	}
	b, _ := os.ReadFile(p)
	if !strings.Contains(string(b), `"api_port": 8765`) || !strings.Contains(string(b), `"futura"`) || strings.Contains(string(b), "9999") {
		t.Fatalf("altre voci cambiate:\n%s", b)
	}
	if fi, _ := os.Stat(p); fi.Mode().Perm() != 0o600 {
		t.Fatalf("permessi %v", fi.Mode().Perm())
	}
	list, err := ks.List()
	if err != nil || len(list) != 1 || !bytes.Equal(list[0].KeyBytes(), K) || list[0].Created != "1970-01-01T00:00:01Z" {
		t.Fatalf("List = %+v %v", list, err)
	}
	c, err := Load()
	if err != nil || len(c.Paired) != 1 {
		t.Fatalf("Load: %+v %v", c.Paired, err)
	}
	if err := Save(c); err != nil {
		t.Fatal(err)
	}
	if list, _ := ks.List(); len(list) != 1 {
		t.Fatal("Save ha perso le chiavi")
	}
	if ok, err := ks.Delete("calamaio-3f2a"); !ok || err != nil {
		t.Fatalf("Delete: %v %v", ok, err)
	}
	if ok, _ := ks.Delete("calamaio-3f2a"); ok {
		t.Fatal("Delete di una chiave che non c'è")
	}
	b, _ = os.ReadFile(p)
	if strings.Contains(string(b), "paired") {
		t.Fatalf("voce paired rimasta:\n%s", b)
	}
}
