package avatar

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"hash/crc32"
	"io/fs"
	"math"
)

// Punti di aggancio degli accessori (docs/AVATAR.md §13): porting di anchorErrors() in
// tools/avatar/lib/format.mjs e di packAnchors()/unpackAnchors()/sidecarErrors() in
// tools/avatar/lib/anchors.mjs, stessi messaggi e stessi byte.

// AnchorNames: i cinque punti, nell'ordine del blocco CANC.
var AnchorNames = []string{"head-top", "eyes", "mouth", "neck", "hand"}

// Point: un punto di aggancio; OK false = il punto non esiste in quel fotogramma (null nel JSON).
type Point struct {
	X, Y int
	OK   bool
}

// AnchorFrame: i 5 punti di un fotogramma, nell'ordine di AnchorNames.
type AnchorFrame [5]Point

// Anchors: posa disegnata → un AnchorFrame per fotogramma.
type Anchors map[string][]AnchorFrame

// AnchorIndex: posizione di name in AnchorNames, -1 se sconosciuto.
func AnchorIndex(name string) int {
	for i, n := range AnchorNames {
		if n == name {
			return i
		}
	}
	return -1
}

// MarshalJSON: come nel file, {"head-top": [x, y] | null, …} per fotogramma.
func (f AnchorFrame) MarshalJSON() ([]byte, error) {
	m := make(map[string]any, len(AnchorNames))
	for i, n := range AnchorNames {
		if f[i].OK {
			m[n] = [2]int{f[i].X, f[i].Y}
		} else {
			m[n] = nil
		}
	}
	return json.Marshal(m)
}

// Anchor: il punto name della posa e del fotogramma (f mod fotogrammi, ripiego delle pose facoltative),
// come getAnchor() in Node. ok false se l'avatar non ha ancore o il punto lì non c'è.
func (a *Avatar) Anchor(pose string, frame int, name string) (Point, bool) {
	i := AnchorIndex(name)
	if a == nil || a.Anchors == nil || i < 0 {
		return Point{}, false
	}
	p := pose
	if _, ok := a.Poses[p]; !ok {
		if fb, ok := Fallback[p]; ok {
			if _, ok := a.Poses[fb]; ok {
				p = fb
			}
		}
	}
	frs := a.Anchors[p]
	if len(frs) == 0 {
		return Point{}, false
	}
	n := len(frs)
	pt := frs[((frame%n)+n)%n][i]
	return pt, pt.OK
}

func presentOf(poses object) []string {
	out := []string{}
	for _, p := range Poses {
		if truthy(poses.m[p]) {
			out = append(out, p)
		}
	}
	return out
}

// parseAnchors controlla il valore di `anchors` per le pose disegnate (present) e restituisce le
// ancore (nil con errori). Messaggi identici ad anchorErrors() di Node.
func parseAnchors(raw json.RawMessage, present []string, frames int) (Anchors, []string) {
	var errs []string
	E := func(f string, a ...any) { errs = append(errs, fmt.Sprintf(f, a...)) }
	obj, ok := parseObject(raw)
	if !ok {
		return nil, []string{"anchors: deve essere un oggetto posa → fotogrammi"}
	}
	for _, p := range obj.keys {
		if !contains(present, p) {
			E("anchors: posa %s assente dal disegno", p)
		}
	}
	out := Anchors{}
	for _, p := range present {
		frsRaw, has := obj.m[p]
		var arr []json.RawMessage
		if !has || !isArray(frsRaw) || json.Unmarshal(frsRaw, &arr) != nil {
			E("anchors.%s: mancano le ancore della posa", p)
			continue
		}
		if len(arr) != frames {
			E("anchors.%s: servono %d fotogrammi, trovati %d", p, frames, len(arr))
			continue
		}
		frs := make([]AnchorFrame, len(arr))
		for f, ptRaw := range arr {
			at := fmt.Sprintf("anchors.%s[%d]", p, f)
			pt, ok := parseObject(ptRaw)
			if !ok {
				E("%s: serve un oggetto con i punti head-top, eyes, mouth, neck, hand", at)
				continue
			}
			for _, k := range pt.keys {
				if AnchorIndex(k) < 0 {
					E("%s: punto sconosciuto %s", at, k)
				}
			}
			for i, n := range AnchorNames {
				v, has := pt.m[n]
				if !has {
					E("%s: manca %s (null se il punto non esiste)", at, n)
					continue
				}
				if string(v) == "null" {
					continue
				}
				x, y, ok := intPair(v)
				switch {
				case !ok:
					E("%s.%s: serve [x, y] intero oppure null", at, n)
				case x < 0 || x >= W || y < 0 || y >= H:
					E("%s.%s: (%d,%d) fuori dal riquadro %d×%d", at, n, x, y, W, H)
				default:
					frs[f][i] = Point{X: x, Y: y, OK: true}
				}
			}
		}
		out[p] = frs
	}
	if len(errs) > 0 {
		return nil, errs
	}
	return out, nil
}

// intPair: [x, y] di due numeri interi (come Array.isArray(v) && v.length === 2 && v.every(Number.isInteger)).
func intPair(raw json.RawMessage) (int, int, bool) {
	if !isArray(raw) {
		return 0, 0, false
	}
	var arr []json.RawMessage
	if json.Unmarshal(raw, &arr) != nil || len(arr) != 2 {
		return 0, 0, false
	}
	var v [2]int
	for i, r := range arr {
		n, ok := asNumber(r)
		if !ok || n != math.Trunc(n) || math.Abs(n) > 1<<30 {
			return 0, 0, false
		}
		v[i] = int(n)
	}
	return v[0], v[1], true
}

// ---------------------------------------------------------------- file accanto (libreria predefinita)

// SidecarFormat: il file ancore/<id>.anchors.json dei predefiniti (docs/AVATAR.md §13.3).
const SidecarFormat = "calamaio-anchors"

// sidecarAnchors legge il file accanto per l'avatar a (già valido, senza ancore nel JSON) e lo
// controlla come sidecarErrors() di Node: formato, avatar, CRC del CAVT di oggi, ancore.
func sidecarAnchors(a *Avatar, raw []byte, cavtCRC uint32) (Anchors, []string) {
	top, ok := parseObject(raw)
	if !ok {
		return nil, []string{"file delle ancore: non è un oggetto JSON"}
	}
	var errs []string
	E := func(f string, x ...any) { errs = append(errs, fmt.Sprintf(f, x...)) }
	if s, ok := asString(top.m["format"]); !ok || s != SidecarFormat {
		E(`file delle ancore: format deve essere "%s"`, SidecarFormat)
	}
	if n, ok := asNumber(top.m["version"]); !ok || n != 1 {
		E("file delle ancore: version deve essere 1")
	}
	for _, k := range top.keys {
		switch k {
		case "format", "version", "avatar", "avatarCrc", "anchors":
		default:
			E("file delle ancore: campo sconosciuto %s", k)
		}
	}
	if id, _ := asString(top.m["avatar"]); id != a.ID {
		E(`file delle ancore: è per l'avatar "%s", non "%s"`, id, a.ID)
	}
	if a.Anchors != nil {
		E("ancore in due posti: nel JSON dell'avatar e nel file accanto (ne serve uno solo)")
	}
	want := fmt.Sprintf("%08x", cavtCRC)
	if got, _ := asString(top.m["avatarCrc"]); got != want {
		E("file delle ancore: fatte per un altro disegno (CRC %s, il CAVT di oggi è %s): rifarle con anchors --auto e rivederle", got, want)
	}
	anchors, aerr := parseAnchors(top.m["anchors"], a.Present(), a.Frames)
	errs = append(errs, aerr...)
	if len(errs) > 0 {
		return nil, errs
	}
	return anchors, nil
}

// loadSidecar: le ancore del predefinito id da fsys (ancore/<id>.anchors.json); nil senza file.
func loadSidecar(fsys fs.FS, a *Avatar, cavtCRC uint32) (Anchors, []string) {
	if fsys == nil {
		return nil, nil
	}
	raw, err := fs.ReadFile(fsys, "ancore/"+a.ID+".anchors.json")
	if err != nil {
		return nil, nil
	}
	return sidecarAnchors(a, raw, cavtCRC)
}

// ---------------------------------------------------------------- blocco binario CANC v1

const (
	AnchorMagic  = "CANC"
	AnchorHeader = 20
)

// AnchorsSize: 20 + pose × fotogrammi × 10 byte.
func AnchorsSize(nPoses, frames int) int { return AnchorHeader + nPoses*frames*len(AnchorNames)*2 }

// PackAnchors: il blocco CANC v1 delle ancore di a, legato al CRC del suo CAVT (docs/AVATAR.md §13.4).
func PackAnchors(a *Avatar, cavtCRC uint32) ([]byte, error) {
	if a.Anchors == nil {
		return nil, fmt.Errorf("avatar %s senza ancore", a.ID)
	}
	present := a.Present()
	mask := 0
	for i, p := range Poses {
		if _, ok := a.Poses[p]; ok {
			mask |= 1 << i
		}
	}
	total := AnchorsSize(len(present), a.Frames)
	buf := make([]byte, total)
	copy(buf, AnchorMagic)
	buf[4], buf[5] = 1, AnchorHeader
	binary.LittleEndian.PutUint16(buf[6:], uint16(total))
	binary.LittleEndian.PutUint32(buf[12:], cavtCRC)
	binary.LittleEndian.PutUint16(buf[16:], uint16(mask))
	buf[18], buf[19] = byte(a.Frames), byte(len(AnchorNames))
	off := AnchorHeader
	for _, p := range present {
		frs := a.Anchors[p]
		if len(frs) != a.Frames {
			return nil, fmt.Errorf("ancore non valide: anchors.%s: servono %d fotogrammi", p, a.Frames)
		}
		for _, fr := range frs {
			for _, pt := range fr {
				if pt.OK {
					buf[off], buf[off+1] = byte(pt.X), byte(pt.Y)
				} else {
					buf[off], buf[off+1] = 0xFF, 0xFF
				}
				off += 2
			}
		}
	}
	binary.LittleEndian.PutUint32(buf[8:], crc32.ChecksumIEEE(buf[12:total]))
	return buf, nil
}

// UnpackedAnchors: il contenuto di un blocco CANC.
type UnpackedAnchors struct {
	AvatarCRC uint32
	PoseMask  int
	Frames    int
	Anchors   Anchors
}

// UnpackAnchors decodifica e verifica un blocco CANC (stessi controlli di unpackAnchors() in Node).
func UnpackAnchors(buf []byte) (*UnpackedAnchors, error) {
	if len(buf) < AnchorHeader || string(buf[:4]) != AnchorMagic {
		return nil, errors.New("magia CANC assente")
	}
	if buf[4] != 1 {
		return nil, fmt.Errorf("versione %d non supportata", buf[4])
	}
	if buf[5] != AnchorHeader {
		return nil, fmt.Errorf("intestazione di %d byte, attesa %d", buf[5], AnchorHeader)
	}
	total := int(binary.LittleEndian.Uint16(buf[6:]))
	if total != len(buf) {
		return nil, fmt.Errorf("lunghezza dichiarata %d, reale %d", total, len(buf))
	}
	if binary.LittleEndian.Uint32(buf[8:]) != crc32.ChecksumIEEE(buf[12:total]) {
		return nil, errors.New("checksum errato")
	}
	u := &UnpackedAnchors{AvatarCRC: binary.LittleEndian.Uint32(buf[12:]), PoseMask: int(binary.LittleEndian.Uint16(buf[16:])), Frames: int(buf[18]), Anchors: Anchors{}}
	if int(buf[19]) != len(AnchorNames) {
		return nil, fmt.Errorf("punti per fotogramma %d, attesi %d", buf[19], len(AnchorNames))
	}
	if u.Frames != 2 && u.Frames != 4 {
		return nil, errors.New("fotogrammi devono essere 2 o 4")
	}
	if u.PoseMask>>len(Poses) != 0 {
		return nil, errors.New("bit di posa sconosciuti")
	}
	var present []string
	for i, p := range Poses {
		if u.PoseMask&(1<<i) != 0 {
			present = append(present, p)
		}
	}
	if total != AnchorsSize(len(present), u.Frames) {
		return nil, errors.New("dimensione incoerente con pose e fotogrammi")
	}
	off := AnchorHeader
	for _, p := range present {
		frs := make([]AnchorFrame, u.Frames)
		for f := range frs {
			for i, n := range AnchorNames {
				x, y := int(buf[off]), int(buf[off+1])
				off += 2
				if x == 0xFF && y == 0xFF {
					continue
				}
				if x >= W || y >= H {
					return nil, fmt.Errorf("%s[%d].%s: (%d,%d) fuori dal riquadro %d×%d", p, f, n, x, y, W, H)
				}
				frs[f][i] = Point{X: x, Y: y, OK: true}
			}
		}
		u.Anchors[p] = frs
	}
	return u, nil
}
