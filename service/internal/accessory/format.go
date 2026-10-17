// Package accessory: accessori della mascotte (docs/AVATAR.md §14): formato JSON calamaio-accessory
// v1, validazione, binario CACC v1 e libreria (predefiniti inclusi nel binario + cartella dell'utente).
//
// È il porting in Go di tools/avatar/lib/accessory.mjs: stesse regole, stessi messaggi, stessi byte.
// I test lo confrontano con Node quando node è installato. Il disegno sopra la mascotte
// (drawAccessory) e i controlli visivi (check, preview) restano negli strumenti Node e, per
// l'anteprima della webapp, nella loro copia JS.
package accessory

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"hash/crc32"
	"regexp"
	"slices"
	"sort"
	"strings"

	"calamaio/internal/avatar"
)

const (
	Format    = "calamaio-accessory"
	Version   = 1
	MaxW      = 14
	MaxH      = 10
	MaxBytes  = 1024 // binario CACC
	Magic     = "CACC"
	Header    = 72
	MaxShift  = 27 // |dx|, |dy| dei ritocchi
	MaxIDLen  = 24
	MaxName   = 24 // byte UTF-8, anche il campo dell'intestazione CACC
	MaxDesc   = 120
	maxIDText = 24
)

var idRe = regexp.MustCompile(`^[a-z0-9]+(-[a-z0-9]+)*$`)

// ValidID: kebab-case, al massimo 24 caratteri.
func ValidID(s string) bool { return len(s) <= MaxIDLen && idRe.MatchString(s) }

var fields = []string{"format", "version", "id", "name", "author", "description", "special", "w", "h", "frames",
	"anchor", "origin", "outline", "hidePoses", "perAvatar", "pixels"}

// Symbols: '.' trasparente, '#' inchiostro, '+' carta, 'x' inverso.
var symbols = map[byte]byte{'.': 0, '#': 1, '+': 2, 'x': 3}
var chars = []byte{'.', '#', '+', 'x'}

// Retouch: ritocco per un avatar (perAvatar).
type Retouch struct {
	DX     int        `json:"dx,omitempty"`
	DY     int        `json:"dy,omitempty"`
	Pixels [][]string `json:"pixels,omitempty"`
}

// Accessory: un accessorio valido (solo dopo Validate senza errori).
type Accessory struct {
	ID          string             `json:"id"`
	Name        string             `json:"name"`
	Author      string             `json:"author"`
	Description string             `json:"description"`
	Special     bool               `json:"special"`
	W           int                `json:"w"`
	H           int                `json:"h"`
	Frames      int                `json:"frames"`
	Anchor      string             `json:"anchor"`
	Origin      [2]int             `json:"origin"`
	Outline     bool               `json:"outline"`
	HidePoses   []string           `json:"hidePoses"`
	PerAvatar   map[string]Retouch `json:"perAvatar"`
	Pixels      [][]string         `json:"pixels"`
}

// Result: esito di Validate. Errors vuoto = valido (Accessory non nil).
type Result struct {
	Errors    []string   `json:"errors"`
	Warnings  []string   `json:"warnings"`
	Accessory *Accessory `json:"-"`
}

func (r Result) OK() bool { return len(r.Errors) == 0 && r.Accessory != nil }

// FrameBytes: ⌈w·h/4⌉.
func FrameBytes(w, h int) int { return (w*h + 3) / 4 }

// Validate controlla un file accessorio come validateAccessory() di Node (stessi testi, stesso ordine).
func Validate(raw []byte) Result {
	r := Result{Errors: []string{}, Warnings: []string{}}
	E := func(f string, a ...any) { r.Errors = append(r.Errors, fmt.Sprintf(f, a...)) }
	Wn := func(f string, a ...any) { r.Warnings = append(r.Warnings, fmt.Sprintf(f, a...)) }
	if !json.Valid(raw) {
		var v any
		err := json.Unmarshal(raw, &v)
		if err == nil {
			err = errors.New("JSON non valido")
		}
		r.Errors = append(r.Errors, "JSON non valido: "+err.Error())
		return r
	}
	top, ok := parseObject(raw)
	if !ok {
		r.Errors = append(r.Errors, "il file non contiene un oggetto JSON")
		return r
	}
	a := &Accessory{PerAvatar: map[string]Retouch{}}
	if s, ok := asString(top.m["format"]); !ok || s != Format {
		E(`format deve essere "%s"`, Format)
	}
	if n, ok := asNumber(top.m["version"]); !ok || n != Version {
		E("version deve essere 1")
	}
	var idOK bool
	if a.ID, idOK = asString(top.m["id"]); !idOK || !idRe.MatchString(a.ID) || utf16Len(a.ID) > maxIDText {
		E("id: kebab-case [a-z0-9-], al massimo 24 caratteri")
	}
	var nameOK bool
	if a.Name, nameOK = asString(top.m["name"]); !nameOK || jsTrim(a.Name) == "" || len(a.Name) > MaxName {
		E("name: non vuoto, al massimo 24 byte UTF-8")
	}
	var authOK bool
	if a.Author, authOK = asString(top.m["author"]); !authOK || jsTrim(a.Author) == "" {
		E("author: obbligatorio")
	}
	var descOK bool
	if a.Description, descOK = asString(top.m["description"]); !descOK || len(a.Description) > MaxDesc {
		E("description: stringa di al massimo 120 byte UTF-8")
	}
	var specOK bool
	if a.Special, specOK = asBool(top.m["special"]); !specOK {
		E("special: true o false")
	}
	w, wOK := asInt(top.m["w"], 1, MaxW)
	if !wOK {
		E("w: da 1 a %d", MaxW)
	}
	h, hOK := asInt(top.m["h"], 1, MaxH)
	if !hOK {
		E("h: da 1 a %d", MaxH)
	}
	a.W, a.H = w, h
	if n, ok := asNumber(top.m["frames"]); !ok || (n != 1 && n != 2) {
		E("frames deve essere 1 o 2")
	} else {
		a.Frames = int(n)
	}
	if s, ok := asString(top.m["anchor"]); !ok || avatar.AnchorIndex(s) < 0 {
		E("anchor: uno tra %s", strings.Join(avatar.AnchorNames, ", "))
	} else {
		a.Anchor = s
	}
	if arr, ok := asArray(top.m["origin"]); !ok || len(arr) != 2 || !allInt(arr) {
		E("origin: serve [x, y] intero")
	} else {
		x, _ := isInteger(arr[0])
		y, _ := isInteger(arr[1])
		a.Origin = [2]int{x, y}
		if wOK && hOK && (x < 0 || x >= w || y < 0 || y >= h) {
			E("origin: (%d,%d) fuori dall'accessorio %d×%d", x, y, w, h)
		}
	}
	var outOK bool
	if a.Outline, outOK = asBool(top.m["outline"]); !outOK {
		E("outline: true o false")
	}
	if arr, ok := asArray(top.m["hidePoses"]); !ok {
		E("hidePoses: elenco di pose (anche vuoto)")
	} else {
		seen := map[string]bool{}
		for _, v := range arr {
			p, isStr := asString(v)
			key := jsText(v)
			switch {
			case !isStr || !slices.Contains(avatar.Poses, p):
				E("hidePoses: posa sconosciuta %s", key)
			case seen[p]:
				E("hidePoses: %s ripetuta", p)
			}
			if isStr {
				seen[key] = true
			}
		}
		all := true
		for _, p := range avatar.Poses {
			all = all && seen[p]
		}
		if all {
			Wn("nascosto in tutte le pose: non si vede mai")
		}
		a.HidePoses = []string{}
		for _, p := range avatar.Poses {
			if seen[p] {
				a.HidePoses = append(a.HidePoses, p)
			}
		}
	}
	for _, k := range top.keys {
		if !slices.Contains(fields, k) {
			E("campo sconosciuto: %s", k)
		}
	}
	if len(r.Errors) > 0 {
		return r
	}

	var ok2 bool
	if a.Pixels, ok2 = framesCheck(top.m["pixels"], a, "pixels", E); ok2 && a.Frames == 2 &&
		strings.Join(a.Pixels[0], "\n") == strings.Join(a.Pixels[1], "\n") {
		Wn("i 2 fotogrammi sono uguali: basta frames 1")
	}
	if pa, has := top.m["perAvatar"]; has {
		obj, ok := parseObject(pa)
		if !ok {
			E("perAvatar: oggetto id avatar → ritocco")
		} else {
			for _, id := range obj.keys {
				at := "perAvatar." + id
				if !idRe.MatchString(id) || utf16Len(id) > maxIDText {
					E(`perAvatar: id avatar "%s" non valido`, id)
					continue
				}
				ro, ok := parseObject(obj.m[id])
				if !ok {
					E("%s: serve un oggetto {dx, dy, pixels}", at)
					continue
				}
				for _, k := range ro.keys {
					if k != "dx" && k != "dy" && k != "pixels" {
						E("%s: campo sconosciuto %s", at, k)
					}
				}
				if !ro.has("dx") && !ro.has("dy") && !ro.has("pixels") {
					E("%s: serve dx, dy o pixels", at)
				}
				var rt Retouch
				for _, k := range []string{"dx", "dy"} {
					if !ro.has(k) {
						continue
					}
					v, ok := asInt(ro.m[k], -MaxShift, MaxShift)
					if !ok {
						E("%s.%s: intero da -%d a %d", at, k, MaxShift, MaxShift)
					}
					if k == "dx" {
						rt.DX = v
					} else {
						rt.DY = v
					}
				}
				if ro.has("pixels") {
					rt.Pixels, _ = framesCheck(ro.m["pixels"], a, at+".pixels", E)
				}
				a.PerAvatar[id] = rt
			}
		}
	}
	if len(r.Errors) == 0 {
		if n := packedLen(a); n > MaxBytes {
			E("binario CACC di %d byte: il massimo è %d (troppi ritocchi con pixels?)", n, MaxBytes)
		}
	}
	if len(r.Errors) == 0 {
		r.Accessory = a
	}
	return r
}

func allInt(arr []json.RawMessage) bool {
	for _, v := range arr {
		if _, ok := isInteger(v); !ok {
			return false
		}
	}
	return true
}

// framesCheck: come framesErrors() di Node (compresa la variabile ok condivisa tra i fotogrammi).
func framesCheck(raw json.RawMessage, a *Accessory, at string, E func(string, ...any)) ([][]string, bool) {
	frs, isArr := asArray(raw)
	if !isArr || len(frs) != a.Frames {
		n := 0
		if isArr {
			n = len(frs)
		}
		E("%s: servono %d fotogrammi, trovati %d", at, a.Frames, n)
		return nil, false
	}
	ok := true
	out := make([][]string, len(frs))
	for f, frRaw := range frs {
		rows, isArr := asArray(frRaw)
		if !isArr || len(rows) != a.H {
			E("%s[%d]: servono %d righe", at, f, a.H)
			ok = false
			continue
		}
		fr := make([]string, len(rows))
		for y, rowRaw := range rows {
			s, isStr := asString(rowRaw)
			switch {
			case !isStr || utf16Len(s) != a.W:
				E("%s[%d] riga %d: servono %d simboli", at, f, y, a.W)
				ok = false
			case strings.Trim(s, ".#+x") != "":
				E("%s[%d] riga %d: simboli ammessi solo . # + x", at, f, y)
				ok = false
			}
			fr[y] = s
		}
		if ok && allDots(fr) {
			E("%s[%d]: accessorio vuoto", at, f)
			ok = false
		}
		out[f] = fr
	}
	return out, ok
}

func allDots(fr []string) bool {
	for _, r := range fr {
		if strings.Trim(r, ".") != "" {
			return false
		}
	}
	return true
}

// ---------------------------------------------------------------- binario CACC v1

func retouchIDs(a *Accessory) []string {
	ids := make([]string, 0, len(a.PerAvatar))
	for id := range a.PerAvatar {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	return ids
}

func packedLen(a *Accessory) int {
	fb := FrameBytes(a.W, a.H)
	total := Header + a.Frames*fb
	for _, id := range retouchIDs(a) {
		total += 1 + len(id) + 3
		if a.PerAvatar[id].Pixels != nil {
			total += a.Frames * fb
		}
	}
	return total
}

func packFrames(buf []byte, off int, frs [][]string, w, h int) int {
	fb := FrameBytes(w, h)
	for _, fr := range frs {
		i := 0
		for _, row := range fr {
			for k := 0; k < len(row); k++ {
				buf[off+(i>>2)] |= symbols[row[k]] << (6 - 2*(i&3))
				i++
			}
		}
		off += fb
	}
	return off
}

// Pack: il binario CACC v1 di un accessorio valido (docs/AVATAR.md §14.3), byte per byte come
// packAccessory() di Node.
func Pack(a *Accessory) ([]byte, error) {
	total := packedLen(a)
	if total > MaxBytes {
		return nil, fmt.Errorf("binario CACC di %d byte: il massimo è %d", total, MaxBytes)
	}
	fb := FrameBytes(a.W, a.H)
	buf := make([]byte, total)
	copy(buf, Magic)
	buf[4], buf[5] = Version, Header
	binary.LittleEndian.PutUint16(buf[6:], uint16(total))
	buf[12], buf[13], buf[14] = byte(a.W), byte(a.H), byte(a.Frames)
	buf[15] = byte(avatar.AnchorIndex(a.Anchor))
	buf[16], buf[17] = byte(a.Origin[0]), byte(a.Origin[1])
	if a.Outline {
		buf[18] |= 1
	}
	if a.Special {
		buf[18] |= 2
	}
	ids := retouchIDs(a)
	buf[19] = byte(len(ids))
	mask := 0
	for _, p := range a.HidePoses {
		mask |= 1 << slices.Index(avatar.Poses, p)
	}
	binary.LittleEndian.PutUint16(buf[20:], uint16(mask))
	binary.LittleEndian.PutUint16(buf[22:], uint16(fb))
	copy(buf[24:48], a.ID)
	copy(buf[48:72], a.Name)
	off := Header
	for _, id := range ids {
		r := a.PerAvatar[id]
		buf[off] = byte(len(id))
		off++
		off += copy(buf[off:], id)
		buf[off], buf[off+1] = byte(int8(r.DX)), byte(int8(r.DY))
		off += 2
		if r.Pixels != nil {
			buf[off] = 1
			off = packFrames(buf, off+1, r.Pixels, a.W, a.H)
		} else {
			off++
		}
	}
	packFrames(buf, off, a.Pixels, a.W, a.H)
	binary.LittleEndian.PutUint32(buf[8:], crc32.ChecksumIEEE(buf[12:total]))
	return buf, nil
}

// CRC: il CRC-32 scritto nell'intestazione (con l'id è la chiave della cache del display).
func CRC(bin []byte) uint32 {
	if len(bin) < 12 {
		return 0
	}
	return binary.LittleEndian.Uint32(bin[8:])
}

func readFrames(buf []byte, off, frames, w, h int) [][]string {
	fb := FrameBytes(w, h)
	out := make([][]string, frames)
	for f := range out {
		rows := make([]string, h)
		for y := 0; y < h; y++ {
			row := make([]byte, w)
			for x := 0; x < w; x++ {
				i := y*w + x
				row[x] = chars[(buf[off+(i>>2)]>>(6-2*(i&3)))&3]
			}
			rows[y] = string(row)
		}
		out[f] = rows
		off += fb
	}
	return out
}

func cstr(b []byte) string {
	for i, c := range b {
		if c == 0 {
			return string(b[:i])
		}
	}
	return string(b)
}

// Unpack decodifica e verifica un CACC (stessi controlli di unpackAccessory() in Node). Autore e
// descrizione non viaggiano nel binario.
func Unpack(buf []byte) (*Accessory, error) {
	if len(buf) < Header || string(buf[:4]) != Magic {
		return nil, errors.New("magia CACC assente")
	}
	if buf[4] != Version {
		return nil, fmt.Errorf("versione %d non supportata", buf[4])
	}
	if buf[5] != Header {
		return nil, fmt.Errorf("intestazione di %d byte, attesa %d", buf[5], Header)
	}
	total := int(binary.LittleEndian.Uint16(buf[6:]))
	if total != len(buf) {
		return nil, fmt.Errorf("lunghezza dichiarata %d, reale %d", total, len(buf))
	}
	if total > MaxBytes {
		return nil, fmt.Errorf("binario di %d byte: il massimo è %d", total, MaxBytes)
	}
	if binary.LittleEndian.Uint32(buf[8:]) != crc32.ChecksumIEEE(buf[12:total]) {
		return nil, errors.New("checksum errato")
	}
	w, h, frames, anchor := int(buf[12]), int(buf[13]), int(buf[14]), int(buf[15])
	if w < 1 || w > MaxW || h < 1 || h > MaxH {
		return nil, fmt.Errorf("dimensioni %d×%d fuori dai limiti %d×%d", w, h, MaxW, MaxH)
	}
	if frames != 1 && frames != 2 {
		return nil, errors.New("fotogrammi devono essere 1 o 2")
	}
	if anchor >= len(avatar.AnchorNames) {
		return nil, fmt.Errorf("ancora %d sconosciuta", anchor)
	}
	if int(buf[16]) >= w || int(buf[17]) >= h {
		return nil, fmt.Errorf("origin (%d,%d) fuori dall'accessorio", buf[16], buf[17])
	}
	if buf[18]>>2 != 0 {
		return nil, errors.New("bit di opzione sconosciuti")
	}
	mask := int(binary.LittleEndian.Uint16(buf[20:]))
	if mask>>len(avatar.Poses) != 0 {
		return nil, errors.New("bit di posa sconosciuti in hidePoses")
	}
	fb := int(binary.LittleEndian.Uint16(buf[22:]))
	if fb != FrameBytes(w, h) {
		return nil, errors.New("frameBytes incoerente")
	}
	a := &Accessory{ID: cstr(buf[24:48]), Name: cstr(buf[48:72]), Author: "(dal binario)", Special: buf[18]&2 != 0,
		W: w, H: h, Frames: frames, Anchor: avatar.AnchorNames[anchor], Origin: [2]int{int(buf[16]), int(buf[17])},
		Outline: buf[18]&1 != 0, HidePoses: []string{}, PerAvatar: map[string]Retouch{}}
	for i, p := range avatar.Poses {
		if mask&(1<<i) != 0 {
			a.HidePoses = append(a.HidePoses, p)
		}
	}
	off := Header
	need := func(n int) error {
		if off+n > total {
			return errors.New("ritocchi oltre la fine del file: dimensione incoerente")
		}
		return nil
	}
	for k := 0; k < int(buf[19]); k++ {
		if err := need(1); err != nil {
			return nil, err
		}
		l := int(buf[off])
		off++
		if l < 1 || l > 24 {
			return nil, errors.New("id del ritocco di lunghezza non valida")
		}
		if err := need(l + 3); err != nil {
			return nil, err
		}
		id := string(buf[off : off+l])
		off += l
		r := Retouch{DX: int(int8(buf[off])), DY: int(int8(buf[off+1]))}
		hasPx := buf[off+2]
		off += 3
		if hasPx > 1 {
			return nil, errors.New("ritocco con opzioni sconosciute")
		}
		if hasPx == 1 {
			if err := need(frames * fb); err != nil {
				return nil, err
			}
			r.Pixels = readFrames(buf, off, frames, w, h)
			off += frames * fb
		}
		a.PerAvatar[id] = r
	}
	if off+frames*fb != total {
		return nil, errors.New("dimensione incoerente con ritocchi e fotogrammi")
	}
	a.Pixels = readFrames(buf, off, frames, w, h)
	return a, nil
}
