// Package theme: temi delle schermate della scheda (docs/TEMI.md). Lettura e controllo del JSON (parse.go),
// compilazione nel binario CTHM v1 (compile.go). Le regole sono le stesse dell'interprete del firmware.
package theme

import "fmt"

const (
	Format      = "calamaio-theme"
	Version     = 1
	ScreenS     = "250x122"
	ReservedID  = "calamaio"
	MaxBlock    = 4096
	MaxFile     = 65536
	HeaderLen   = 72
	MaxVariants = 8
	MaxElements = 64
	MaxConds    = 8
	MaxSegs     = 16
	MaxRow      = 32
	MaxLiteral  = 255
	CoordMax    = 512
)

// Schermate personalizzabili: codice nel binario = indice + 1 (docs/TEMI.md §1.1).
var ScreenKeys = []string{"home", "detail", "sleep", "offline", "riepilogo", "oggi", "batteria", "stato", "limiti", "focus", "ambiente", "pet"}

var lockedScreens = map[string]bool{"perm": true, "pair": true, "confirm": true, "boot": true, "ota": true, "battlow": true,
	"calib": true, "gbload": true, "gbplay": true}
var laterScreens = map[string]bool{"list": true, "wifiScan": true, "qr": true, "connecting": true, "storico": true, "guest": true,
	"servizi": true, "agenda": true, "game": true}

func ScreenCode(key string) (byte, bool) {
	for i, k := range ScreenKeys {
		if k == key {
			return byte(i + 1), true
		}
	}
	return 0, false
}

type Problem struct {
	Path string `json:"path"`
	Msg  string `json:"msg"`
}

func (p Problem) String() string {
	if p.Path == "" {
		return p.Msg
	}
	return p.Path + ": " + p.Msg
}

type problems []Problem

func (ps *problems) add(path, f string, a ...any) {
	*ps = append(*ps, Problem{path, fmt.Sprintf(f, a...)})
}

type Theme struct {
	Format  string
	Version int
	ID      string
	Name    string
	Screen  string
	Screens map[string]*Layout
}

type Layout struct{ Variants []*Variant }

type Variant struct {
	When     string
	Ref      string
	Elements []*Element // già sciolti i gruppi, senza i nascosti
}

type Font struct {
	Family string `json:"family"`
	Size   int    `json:"size"`
	Weight int    `json:"weight"`
}

// Codici delle condizioni nel binario (docs/TEMI.md §5.4).
const (
	CondPerm byte = 1 + iota
	CondBattBelow
	CondCharging
	CondNight
	CondPC
	CondWorkday
	CondGame
	CondSessionsMin
	CondSessionsMax
	CondState
	CondBlink
)

type Cond struct{ Op, Arg byte }

// Element: un elemento già controllato e con i predefiniti applicati (W e H calcolati per i tipi a misura fissa).
type Element struct {
	Type       string
	X, Y, W, H int
	Invert     bool
	Conds      []Cond
	Scale      int
	Pose       string
	Ref        string // "" = quello della variante (in una riga: row)
	Accessory  bool
	Animated   bool
	Segs       []Seg // text, clock (già abbassato), qr
	Font       Font
	Align      string
	Lines      int
	LineHeight int
	Upper      bool
	Percent    bool
	Source     string
	Style      string
	Sym        string
	Dir        string
	Border     int
	Fill       bool
	Radius     int
	RowH       int
	Gap        int
	PageSec    int
	PageDots   bool
	Row        []*Element
}
