package phones

import (
	"bytes"
	"fmt"
	"strings"
	"testing"
)

func TestQRSVG(t *testing.T) {
	link := Link([]string{"192.168.100.200", "10.0.0.7", "172.16.5.4"}, 8767, strings.Repeat("ab", 32), strings.Repeat("cd", 16), "mac-sviluppo")
	svg, err := QRSVG(link)
	if err != nil {
		t.Fatal(err)
	}
	var n int
	if _, err := fmt.Sscanf(string(svg), `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d`, &n); err != nil {
		t.Fatalf("intestazione: %v %.80s", err, svg)
	}
	size := n - 8
	if size < 21 || (size-17)%4 != 0 || size > 4*12+17 {
		t.Fatalf("lato %d moduli: versione oltre la 12, troppo fitto per 240 px", size)
	}
	if !bytes.Contains(svg, []byte("M4 4h1v1h-1z")) || !bytes.HasSuffix(svg, []byte(`"/></svg>`)) {
		t.Fatalf("motivo di posizione in alto a sinistra mancante: %.200s", svg)
	}
	if _, err := QRSVG(strings.Repeat("x", 5000)); err == nil {
		t.Fatal("testo troppo lungo accettato")
	}
}
