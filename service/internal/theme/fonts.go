package theme

type FontSpec struct {
	Family string
	Size   int
	Weight int
}

// Le 22 combinazioni di firmware/src/gfx/fonts/f_registry.h (TestFontsMatchFirmware).
var Fonts = []FontSpec{
	{"ui", 10, 500}, {"ui", 10, 600}, {"ui", 10, 700}, {"ui", 11, 500}, {"ui", 11, 600}, {"ui", 12, 500}, {"ui", 12, 600},
	{"ui", 12, 700}, {"ui", 13, 600}, {"ui", 13, 700}, {"ui", 14, 700}, {"ui", 15, 700}, {"ui", 16, 700}, {"ui", 18, 700},
	{"ui", 20, 700}, {"ui", 22, 700}, {"ui", 26, 700}, {"ui", 40, 700}, {"ui", 44, 700}, {"ui", 46, 700},
	{"pix", 8, 400}, {"pix", 8, 700},
}

func FontOK(family string, size, weight int) bool {
	for _, f := range Fonts {
		if f.Family == family && f.Size == size && f.Weight == weight {
			return true
		}
	}
	return false
}
