package theme

import (
	"os"
	"regexp"
	"strconv"
	"testing"
)

// La tabella dei caratteri del servizio è quella del firmware (f_registry.h, generato da tools/fonts/gen.py).
func TestFontsMatchFirmware(t *testing.T) {
	b, err := os.ReadFile("../../../firmware/src/gfx/fonts/f_registry.h")
	if err != nil {
		t.Fatal(err)
	}
	re := regexp.MustCompile(`\{(\d+), (\d+), (true|false), (?:true|false), \d+,`)
	got := map[FontSpec]bool{}
	for _, m := range re.FindAllStringSubmatch(string(b), -1) {
		size, _ := strconv.Atoi(m[1])
		w, _ := strconv.Atoi(m[2])
		fam := "ui"
		if m[3] == "true" {
			fam = "pix"
		}
		got[FontSpec{fam, size, w}] = true
	}
	if len(got) != len(Fonts) {
		t.Fatalf("firmware %d caratteri, servizio %d", len(got), len(Fonts))
	}
	for _, f := range Fonts {
		if !got[f] || !FontOK(f.Family, f.Size, f.Weight) {
			t.Fatalf("%+v manca nel firmware", f)
		}
	}
	if FontOK("ui", 17, 700) || FontOK("pix", 8, 600) {
		t.Fatal("combinazione inesistente accettata")
	}
}
