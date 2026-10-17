package ota

import (
	"strconv"
	"strings"
)

// CompareVersions confronta due versioni a numeri separati da punti (0.10.0 > 0.9.1); le parti non
// numeriche si confrontano come testo. −1, 0, 1.
func CompareVersions(a, b string) int {
	pa := strings.FieldsFunc(a, func(r rune) bool { return r == '.' || r == '-' || r == '+' })
	pb := strings.FieldsFunc(b, func(r rune) bool { return r == '.' || r == '-' || r == '+' })
	for i := 0; i < max(len(pa), len(pb)); i++ {
		var x, y string
		if i < len(pa) {
			x = pa[i]
		}
		if i < len(pb) {
			y = pb[i]
		}
		nx, ex := strconv.Atoi(x)
		ny, ey := strconv.Atoi(y)
		switch {
		case ex == nil && ey == nil && nx != ny:
			if nx < ny {
				return -1
			}
			return 1
		case (ex != nil || ey != nil) && x != y:
			if x < y {
				return -1
			}
			return 1
		}
	}
	return 0
}

// RunPoseFW: prima versione del firmware che conosce la posa run degli avatar (bit 9 del CAVT).
const RunPoseFW = "0.7.0"

// KnowsRunPose: il firmware fw accetta un CAVT con la posa run. Una versione vuota o più vecchia di
// RunPoseFW no (rifiuta tutto l'avatar e disegna la Goccia).
func KnowsRunPose(fw string) bool { return fw != "" && CompareVersions(fw, RunPoseFW) >= 0 }
