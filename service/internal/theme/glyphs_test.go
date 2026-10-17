package theme

import (
	"encoding/json"
	"fmt"
	"os"
	"reflect"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

// I glifi del servizio sono quelli del firmware: entries[] di f_<ui|pix><peso>_<px>.h.
func TestFontGlyphsMatchFirmware(t *testing.T) {
	re := regexp.MustCompile(`^\s*\{0x([0-9a-fA-F]+),`)
	for _, f := range Fonts {
		name := fmt.Sprintf("f_%s%d_%d", f.Family, f.Weight, f.Size)
		b, err := os.ReadFile("../../../firmware/src/gfx/fonts/" + name + ".h")
		if err != nil {
			t.Fatal(err)
		}
		in := false
		var rs []rune
		for _, ln := range strings.Split(string(b), "\n") {
			if strings.Contains(ln, "_entries[]") {
				in = true
				continue
			}
			if in && strings.HasPrefix(strings.TrimSpace(ln), "};") {
				break
			}
			if m := re.FindStringSubmatch(ln); in && m != nil {
				v, _ := strconv.ParseInt(m[1], 16, 32)
				if v != 0xE000 && v != 0xE001 {
					rs = append(rs, rune(v))
				}
			}
		}
		if got := FontGlyphs(Font{f.Family, f.Size, f.Weight}); got != string(rs) {
			t.Errorf("%s: servizio %q, firmware %q", name, got, string(rs))
		}
	}
	g := FontGlyphs(Font{"ui", 44, 700})
	for _, r := range "$AÌì—" {
		if strings.ContainsRune(g, r) {
			t.Errorf("ui 44/700 non deve avere %q", r)
		}
	}
}

type catalogFile struct {
	Comment string `json:"comment"`
	Fonts   []struct {
		Family string `json:"family"`
		Size   int    `json:"size"`
		Weight int    `json:"weight"`
		Glyphs string `json:"glyphs"`
	} `json:"fonts"`
	Clock  string `json:"clock"`
	Values []struct {
		Name    string `json:"name"`
		ID      int    `json:"id"`
		Example string `json:"example"`
		Glyphs  string `json:"glyphs"`
		Free    bool   `json:"free"`
	} `json:"values"`
	Cases []struct {
		Font     Font     `json:"font"`
		Text     string   `json:"text"`
		Upper    bool     `json:"upper"`
		Kind     string   `json:"kind"`
		Warnings []string `json:"warnings"`
	} `json:"cases"`
}

// catalog.json = tabelle di Go (fonts, clock, values con example/glyphs/free) e ogni caso dà gli avvisi scritti.
func TestGlyphCatalog(t *testing.T) {
	b, err := os.ReadFile("../../../protocol/themes/catalog.json")
	if err != nil {
		t.Fatal(err)
	}
	var c catalogFile
	if err := json.Unmarshal(b, &c); err != nil {
		t.Fatal(err)
	}
	if c.Comment == "" || len(c.Fonts) != len(Fonts) || len(c.Values) != len(Values) || c.Clock != ClockGlyphs {
		t.Fatalf("catalogo incompleto: %d caratteri, %d valori, clock %q", len(c.Fonts), len(c.Values), c.Clock)
	}
	for i, f := range Fonts {
		g := c.Fonts[i]
		if g.Family != f.Family || g.Size != f.Size || g.Weight != f.Weight || g.Glyphs != FontGlyphs(Font{f.Family, f.Size, f.Weight}) {
			t.Errorf("font %d: %+v", i, g.Family)
		}
	}
	for i, v := range Values {
		g := c.Values[i]
		if g.Name != v.Name || g.ID != int(v.ID) || g.Example != v.Example || g.Glyphs != v.Glyphs || g.Free != v.Free {
			t.Errorf("valore %s: catalogo %+v", v.Name, g)
		}
	}
	if len(c.Cases) < 8 {
		t.Fatalf("pochi casi: %d", len(c.Cases))
	}
	for _, k := range c.Cases {
		e := &Element{Type: "text", Font: k.Font, Upper: k.Upper}
		if k.Kind == "clock" {
			e.Type = "clock"
			id := uint16(0x80)
			if k.Text == "hms" {
				id = ValClockSec
			}
			e.Segs = []Seg{{Val: id, IsVal: true}}
		} else {
			segs, err := ParseTemplate(k.Text, false)
			if err != nil {
				t.Fatal(err)
			}
			e.Segs = segs
		}
		got := GlyphWarnings(e)
		if !(len(got) == 0 && len(k.Warnings) == 0) && !reflect.DeepEqual(got, k.Warnings) {
			t.Errorf("%+v %q: %q invece di %q", k.Font, k.Text, got, k.Warnings)
		}
	}
}

func TestGlyphWarnings(t *testing.T) {
	big := Font{"ui", 44, 700}
	cases := []struct {
		text  string
		upper bool
		want  []string
	}{
		{"{oggi.costo}", false, []string{"{oggi.costo} può mostrare «$», che Chakra Petch 44 non ha: si vedrà «?»"}},
		{"Costo $ €", false, []string{"Chakra Petch 44 non ha «C$€»: si vedrà «?»"}},
		{"{giorno}", false, []string{"{giorno} può mostrare «ì», che Chakra Petch 44 non ha: si vedrà «?»"}},
		{"{batteria}", true, nil},
		{"ok", true, []string{"Chakra Petch 44 non ha «OK»: si vedrà «?»"}},
		{"{sessione.nome} {sessione.nome}", false, []string{"{sessione.nome} è testo libero: Chakra Petch 44 ha solo cifre, minuscole e «%+,-./:?…»"}},
	}
	for _, f := range []Font{big, {"ui", 12, 600}, {"pix", 8, 400}} {
		for _, c := range cases {
			segs, err := ParseTemplate(c.text, false)
			if err != nil {
				t.Fatal(err)
			}
			got := GlyphWarnings(&Element{Type: "text", Font: f, Upper: c.upper, Segs: segs})
			want := c.want
			if f != big {
				want = nil
			}
			if !reflect.DeepEqual(got, want) {
				t.Errorf("%v %q: %#v invece di %#v", f, c.text, got, want)
			}
		}
	}
	if FontLabel(Font{"pix", 8, 400}) != "Silkscreen 8" || FontLabel(big) != "Chakra Petch 44" {
		t.Error("FontLabel")
	}
	if GlyphWarnings(&Element{Type: "battery"}) != nil {
		t.Error("battery senza avvisi")
	}
}
