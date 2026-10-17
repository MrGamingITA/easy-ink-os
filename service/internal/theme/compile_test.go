package theme

import (
	"bytes"
	"encoding/binary"
	"errors"
	"flag"
	"hash/crc32"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

var update = flag.Bool("update", false, "riscrive i .cthm di protocol/themes/fixtures")

func TestCompileFixtures(t *testing.T) {
	for _, n := range []string{"prova", "elementi", "lista"} {
		_, c, err := CompileJSON(read(t, n+".theme.json"))
		if err != nil {
			t.Fatalf("%s: %v", n, err)
		}
		path := filepath.Join(fixtures, n+".cthm")
		if *update {
			if err := os.WriteFile(path, c.Bin, 0o644); err != nil {
				t.Fatal(err)
			}
		}
		want, err := os.ReadFile(path)
		if err != nil {
			t.Fatalf("%v (go test ./internal/theme -run TestCompileFixtures -update)", err)
		}
		if !bytes.Equal(want, c.Bin) {
			t.Fatalf("%s.cthm vecchio: rigenera con -update", n)
		}
	}
}

func TestCompileHeader(t *testing.T) {
	_, c, err := CompileJSON(read(t, "prova.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	b := c.Bin
	le := binary.LittleEndian
	if string(b[:4]) != "CTHM" || b[4] != 1 || b[5] != HeaderLen || le.Uint16(b[16:]) != 250 || le.Uint16(b[18:]) != 122 {
		t.Fatalf("intestazione: % x", b[:24])
	}
	if int(le.Uint32(b[12:])) != len(b) || le.Uint32(b[8:]) != crc32.ChecksumIEEE(b[12:]) || c.CRC != le.Uint32(b[8:]) {
		t.Fatal("lunghezza o CRC")
	}
	if strings.TrimRight(string(b[20:44]), "\x00") != "prova" || strings.TrimRight(string(b[44:68]), "\x00") != "Mascotte grande" {
		t.Fatal("id o nome")
	}
	if b[68] != 2 || b[72] != 1 || b[80] != 3 { // home (1), sleep (3)
		t.Fatalf("indice: % x", b[68:88])
	}
	home := int(le.Uint32(b[76:]))
	n := int(le.Uint16(b[74:]))
	if c.Screens["home"] != n || b[home] != 3 { // 3 varianti
		t.Fatalf("blocco della home: %d byte, %d varianti", n, b[home])
	}
	// prima variante: when none (1), ref selected (1)
	if b[home+4] != 1 || b[home+5] != 1 {
		t.Fatalf("variante 0: % x", b[home+4:home+10])
	}
}

func TestCompileDeterministic(t *testing.T) {
	_, a, err := CompileJSON(read(t, "elementi.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	_, b, err := CompileJSON(read(t, "elementi.theme.json"))
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(a.Bin, b.Bin) {
		t.Fatal("due compilazioni diverse")
	}
}

func TestCompileBlockLimit(t *testing.T) {
	// 60 testi da 60 byte diversi: più di 4 096 byte in una schermata
	var sb strings.Builder
	sb.WriteString(`{"format":"calamaio-theme","version":1,"id":"grosso","name":"Grosso","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[`)
	for i := 0; i < 60; i++ {
		if i > 0 {
			sb.WriteString(",")
		}
		sb.WriteString(`{"type":"text","x":0,"y":0,"w":100,"font":{"family":"ui","size":10,"weight":500},"text":"`)
		sb.WriteString(strings.Repeat(string(rune('a'+i%26)), 50) + string(rune('A'+i/26)) + strings.Repeat("x", 9))
		sb.WriteString(`"}`)
	}
	sb.WriteString(`]}]}}}`)
	_, _, err := CompileJSON([]byte(sb.String()))
	var ie *InvalidError
	if !errors.As(err, &ie) || !strings.Contains(err.Error(), "oltre 4096 byte") {
		t.Fatalf("limite del blocco: %v", err)
	}
}

func TestCompileStringsShared(t *testing.T) {
	// lo stesso testo fisso usato due volte si scrive una volta sola nella tabella
	j := `{"format":"calamaio-theme","version":1,"id":"due","name":"Due","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[` +
		`{"type":"text","x":0,"y":0,"w":10,"font":{"family":"ui","size":10,"weight":500},"text":"uguale"},` +
		`{"type":"text","x":0,"y":20,"w":10,"font":{"family":"ui","size":10,"weight":500},"text":"uguale"}]}]}}}`
	_, c, err := CompileJSON([]byte(j))
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Count(c.Bin, []byte("uguale")) != 1 {
		t.Fatal("stringa ripetuta")
	}
}

// themeWithLastLen: 19 testi distinti (18 da 200 byte, l'ultimo da n) in una sola schermata: blocco = 4047 + n byte.
func themeWithLastLen(n int) []byte {
	var sb strings.Builder
	sb.WriteString(`{"format":"calamaio-theme","version":1,"id":"limite","name":"Limite","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[`)
	for i := 0; i < 19; i++ {
		if i > 0 {
			sb.WriteString(",")
		}
		l := 200
		if i == 18 {
			l = n
		}
		sb.WriteString(`{"type":"text","x":0,"y":0,"w":100,"font":{"family":"ui","size":10,"weight":500},"text":"`)
		sb.WriteString(strings.Repeat(string(rune('a'+i)), l))
		sb.WriteString(`"}`)
	}
	sb.WriteString(`]}]}}}`)
	return []byte(sb.String())
}

func TestCompileBlockExactLimit(t *testing.T) {
	_, c, err := CompileJSON(themeWithLastLen(49))
	if err != nil {
		t.Fatal(err)
	}
	if got := int(binary.LittleEndian.Uint16(c.Bin[74:])); got != 4096 || c.Screens["home"] != 4096 {
		t.Fatalf("blocco: indice %d, mappa %d", got, c.Screens["home"])
	}
	_, c, err = CompileJSON(themeWithLastLen(50))
	var ie *InvalidError
	if !errors.As(err, &ie) || c == nil || c.Bin != nil || c.Screens["home"] != 4097 {
		t.Fatalf("4097 byte: %v", err)
	}
}
