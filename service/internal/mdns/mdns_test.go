package mdns

import (
	"strings"
	"testing"
)

func TestConfigs(t *testing.T) {
	d := DisplayConfig(8766, "/ws")
	if d.Type != ServiceType || d.Port != 8766 || d.Host != HostAlias || d.Text["proto"] != "1" || d.Text["path"] != "/ws" {
		t.Fatalf("display: %+v", d)
	}
	fp := strings.Repeat("ab", 32)
	a := AppConfig(8767, fp)
	if a.Type != "_calamaio-app._tcp" || a.Port != 8767 || a.Host != HostAlias || len(a.Text) != 1 || a.Text["fp"] != fp[:16] {
		t.Fatalf("telefoni: %+v", a)
	}
	if a.Name != d.Name || !strings.HasPrefix(a.Name, "Calamaio su ") {
		t.Fatalf("nomi: %q %q", a.Name, d.Name)
	}
}
