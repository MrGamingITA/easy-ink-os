package theme

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/santhosh-tekuri/jsonschema/v6"
)

const fixtures = "../../../protocol/themes/fixtures"

func read(t *testing.T, name string) []byte {
	t.Helper()
	b, err := os.ReadFile(filepath.Join(fixtures, name))
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestParseFixtures(t *testing.T) {
	for _, n := range []string{"prova.theme.json", "elementi.theme.json", "lista.theme.json"} {
		th, ps, _ := Parse(read(t, n))
		if len(ps) > 0 {
			t.Fatalf("%s: %v", n, ps)
		}
		if th.ID+".theme.json" != n {
			t.Fatalf("%s: id %q", n, th.ID)
		}
	}
	th, _, _ := Parse(read(t, "elementi.theme.json"))
	if got := len(th.Screens); got != 11 {
		t.Fatalf("elementi: %d schermate", got)
	}
	// il gruppo si scioglie già qui: 2 figli visibili, quello nascosto sparisce, x/y sommati, condizione ereditata
	pet := th.Screens["pet"].Variants[0].Elements
	var r *Element
	for _, e := range pet {
		if e.Type == "rect" {
			r = e
		}
	}
	if r == nil || r.X != 100 || r.Y != 60 || len(r.Conds) != 1 || r.Conds[0].Op != CondState {
		t.Fatalf("gruppo non sciolto: %+v", r)
	}
}

// Ogni caso: modifica del tema «prova» (json già valido) → frammento atteso nell'errore.
func TestParseRejects(t *testing.T) {
	base := string(read(t, "prova.theme.json"))
	cases := []struct{ from, to, want string }{
		{`"version": 1`, `"version": 2`, "versione 2 non supportata"},
		{`"id": "prova"`, `"id": "Prova"`, "id: kebab-case"},
		{`"id": "prova"`, `"id": "calamaio"`, "tema di serie"},
		{`"name": "Mascotte grande"`, `"name": "Un nome davvero troppo lungo, sì"`, "1–24 byte"},
		{`"screen": "250x122"`, `"screen": "400x300"`, "solo 250x122"},
		{`"sleep": {`, `"perm": {`, "schermata bloccata"},
		{`"sleep": {`, `"agenda": {`, "non ancora personalizzabile"},
		{`"scale": 4 }`, `"scale": 5 }`, "scale: da 1 a 4"},
		{`"size": 14, "weight": 700`, `"size": 17, "weight": 700`, "carattere che la scheda non ha"},
		{`{sessione.nome|maiuscolo}`, `{sessione.nomi}`, "valore sconosciuto {sessione.nomi}"},
		{`{sessione.nome|maiuscolo}`, `{sessione.nome|grassetto}`, "filtro sconosciuto"},
		{`{sessione.nome|maiuscolo}`, `{riga.nome}`, "riga.* solo dentro un riquadro lista"},
		{`{sessione.nome|maiuscolo}`, `{sessione.nome`, "graffa non chiusa"},
		{`"x": 83, "y": 6`, `"x": 600, "y": 6`, "x: fuori da ±512"},
		{`"type": "battery", "x": 230, "y": 110 }`, `"type": "battery", "x": 230, "y": 110, "colore": 1 }`, "campo sconosciuto «colore»"},
		{`"type": "line", "x": 0, "y": 33`, `"type": "list", "rowH": 5, "row": [], "x": 0, "y": 33`, "dentro un altro"},
		{`"when": "none"`, `"when": "spesso"`, "when: always, none, one o many"},
		{`{ "charging": false }`, `{ "charging": false, "pc": true }`, "una sola chiave"},
		{`{ "blink": "even" }`, `{ "blink": "pari" }`, "blink: even o odd"},
		{`"screens": {`, `"menu": {}, "screens": {`, "fase 4"},
	}
	for _, c := range cases {
		if !strings.Contains(base, c.from) {
			t.Fatalf("caso %q: testo di partenza assente", c.want)
		}
		_, ps, _ := Parse([]byte(strings.Replace(base, c.from, c.to, 1)))
		found := false
		for _, p := range ps {
			if strings.Contains(p.String(), c.want) {
				found = true
			}
		}
		if !found {
			t.Errorf("atteso %q, avuto %v", c.want, ps)
		}
	}
	if _, ps, _ := Parse([]byte(`{`)); len(ps) == 0 || !strings.Contains(ps[0].Msg, "JSON non valido") {
		t.Fatalf("JSON rotto: %v", ps)
	}
}

func TestParseLimits(t *testing.T) {
	// 65 elementi in una variante
	var b strings.Builder
	b.WriteString(`{"format":"calamaio-theme","version":1,"id":"tanti","name":"Tanti","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[`)
	for i := 0; i < 65; i++ {
		if i > 0 {
			b.WriteString(",")
		}
		b.WriteString(`{"type":"rect","x":0,"y":0,"w":1,"h":1}`)
	}
	b.WriteString(`]}]}}}`)
	_, ps, _ := Parse([]byte(b.String()))
	if len(ps) == 0 || !strings.Contains(ps[0].String(), "al massimo 64 elementi") {
		t.Fatalf("limite elementi: %v", ps)
	}
}

func TestParseWarnsBigFontChars(t *testing.T) {
	_, ps, warns := Parse(read(t, "elementi.theme.json"))
	if len(ps) > 0 {
		t.Fatal(ps)
	}
	if len(warns) == 0 || !strings.Contains(strings.Join(warns, "\n"), "Chakra Petch 40") {
		t.Fatalf("avviso dei caratteri grandi mancante: %v", warns)
	}
}

func TestParseGlyphWarningPath(t *testing.T) {
	src := `{"format":"calamaio-theme","version":1,"id":"g","name":"g","screen":"250x122","screens":{"home":{"variants":[{"when":"always","elements":[{"type":"text","x":0,"y":0,"w":100,"text":"{oggi.costo}","font":{"family":"ui","size":44,"weight":700}}]}]}}}`
	_, ps, warns := Parse([]byte(src))
	if len(ps) > 0 {
		t.Fatal(ps)
	}
	want := "screens.home.variants[0].elements[0]: "
	if len(warns) != 1 || !strings.HasPrefix(warns[0], want) || !strings.Contains(warns[0], "{oggi.costo}") {
		t.Fatalf("avviso con percorso: %q", warns)
	}
}

func TestTemplate(t *testing.T) {
	segs, err := ParseTemplate("A {{x}} {sessione.attività|maiuscolo}!", false)
	if err != nil {
		t.Fatal(err)
	}
	if len(segs) != 3 || segs[0].Lit != "A {x} " || !segs[1].IsVal || segs[1].Val != 0x02 || segs[1].Filter != 1 || segs[2].Lit != "!" {
		t.Fatalf("%+v", segs)
	}
	if _, err := ParseTemplate(strings.Repeat("x", 256), false); err == nil {
		t.Fatal("testo fisso oltre 255 byte accettato")
	}
}

func TestSchemaAcceptsFixtures(t *testing.T) {
	f, err := os.Open("../../../protocol/schema/theme.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	doc, err := jsonschema.UnmarshalJSON(f)
	if err != nil {
		t.Fatal(err)
	}
	c := jsonschema.NewCompiler()
	c.DefaultDraft(jsonschema.Draft2020)
	if err := c.AddResource("theme.schema.json", doc); err != nil {
		t.Fatal(err)
	}
	sch, err := c.Compile("theme.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	for _, n := range []string{"prova.theme.json", "elementi.theme.json", "lista.theme.json"} {
		v, _ := jsonschema.UnmarshalJSON(strings.NewReader(string(read(t, n))))
		if err := sch.Validate(v); err != nil {
			t.Fatalf("%s: %v", n, err)
		}
	}
	bad, _ := jsonschema.UnmarshalJSON(strings.NewReader(`{"format":"calamaio-theme","version":1,"id":"x","name":"x","screen":"250x122","screens":{"perm":{"variants":[]}}}`))
	if sch.Validate(bad) == nil {
		t.Fatal("lo schema accetta la schermata Permessi")
	}
}
