package theme

import (
	"encoding/binary"
	"hash/crc32"
	"strings"
)

const (
	TypeMascot byte = 1 + iota
	TypeText
	TypeBattery
	TypeWifi
	TypeStateIcon
	TypeBar
	TypeQr
	TypeSym
	TypeLine
	TypeRect
	TypeCircle
	TypeList
)

var typeCodes = map[string]byte{"mascot": TypeMascot, "text": TypeText, "clock": TypeText, "battery": TypeBattery, "wifi": TypeWifi,
	"stateIcon": TypeStateIcon, "bar": TypeBar, "qr": TypeQr, "sym": TypeSym, "line": TypeLine, "rect": TypeRect, "circle": TypeCircle,
	"list": TypeList}

type Compiled struct {
	Bin      []byte
	CRC      uint32
	Screens  map[string]int
	Warnings []string
}

type InvalidError struct{ Problems []Problem }

func (e *InvalidError) Error() string {
	s := make([]string, len(e.Problems))
	for i, p := range e.Problems {
		s[i] = p.String()
	}
	return strings.Join(s, "; ")
}

func CompileJSON(data []byte) (*Theme, *Compiled, error) {
	t, ps, warns := Parse(data)
	if len(ps) > 0 {
		return t, nil, &InvalidError{ps}
	}
	c, err := Compile(t)
	if c != nil {
		c.Warnings = append(warns, c.Warnings...)
	}
	return t, c, err
}

// strtab: tabella delle stringhe di un blocco, senza doppioni; posizioni dall'inizio della tabella.
type strtab struct {
	data []byte
	at   map[string]int
}

func (s *strtab) ref(v string) int {
	if k, ok := s.at[v]; ok {
		return k
	}
	k := len(s.data)
	s.at[v] = k
	s.data = append(append(s.data, byte(len(v))), v...)
	return k
}

func le16(b []byte, v int) []byte { return binary.LittleEndian.AppendUint16(b, uint16(int16(v))) }

func refCode(r string, inRow bool) byte {
	switch r {
	case "urgent":
		return 0
	case "selected":
		return 1
	case "row":
		return 2
	case "nobody":
		return 3
	}
	if inRow {
		return 2
	}
	return 4
}

func segs(b []byte, ss []Seg, st *strtab) []byte {
	b = append(b, byte(len(ss)))
	for _, s := range ss {
		if s.IsVal {
			b = append(b, 1|s.Filter<<2)
			b = le16(b, int(s.Val))
		} else {
			b = append(b, 0)
			b = le16(b, st.ref(s.Lit))
		}
	}
	return b
}

func payload(e *Element, inRow bool, st *strtab) []byte {
	var p []byte
	switch e.Type {
	case "mascot":
		pose := byte(0xFF)
		if e.Pose != "auto" {
			pose = byte(index(poses, e.Pose))
		}
		p = []byte{byte(e.Scale), pose, refCode(e.Ref, inRow), b2u(e.Accessory) | b2u(e.Animated)<<1}
	case "text", "clock":
		wp := byte(e.Font.Weight / 100)
		if e.Font.Family == "pix" {
			wp |= 0x80
		}
		p = []byte{byte(e.Font.Size), wp, byte(index([]string{"left", "center", "right"}, e.Align)), byte(e.Lines), byte(e.LineHeight), b2u(e.Upper)}
		p = segs(p, e.Segs, st)
	case "battery":
		p = []byte{byte(e.Scale), b2u(e.Percent)}
	case "wifi":
		p = []byte{byte(e.Scale)}
	case "stateIcon":
		p = []byte{byte(e.Scale), refCode(e.Ref, inRow)}
	case "bar":
		p = []byte{byte(index(sources, e.Source)), b2u(e.Style == "bar")}
	case "qr":
		p = segs(nil, e.Segs, st)
	case "sym":
		p = []byte{byte(index(syms, e.Sym)), byte(e.Scale)}
	case "line":
		p = []byte{b2u(e.Dir == "v"), b2u(e.Style == "dotted")}
	case "rect":
		p = []byte{byte(e.Border), b2u(e.Fill), byte(e.Radius)}
	case "circle":
		p = []byte{byte(e.Border), b2u(e.Fill)}
	case "list":
		p = []byte{byte(e.RowH), byte(e.Gap), byte(e.PageSec), b2u(e.PageDots), byte(len(e.Row)), 0}
	}
	return p
}

func elem(b []byte, e *Element, inRow bool, st *strtab) []byte {
	p := payload(e, inRow, st)
	b = append(b, typeCodes[e.Type], b2u(e.Invert))
	b = le16(b, e.X)
	b = le16(b, e.Y)
	b = le16(b, e.W)
	b = le16(b, e.H)
	b = append(b, byte(len(e.Conds)), byte(len(p)))
	for _, c := range e.Conds {
		b = append(b, c.Op, c.Arg)
	}
	b = append(b, p...)
	for _, r := range e.Row {
		b = elem(b, r, true, st)
	}
	return b
}

func block(l *Layout) []byte {
	st := &strtab{at: map[string]int{}}
	nv := len(l.Variants)
	var bodies [][]byte
	for _, v := range l.Variants {
		var b []byte
		for _, e := range v.Elements {
			b = elem(b, e, false, st)
		}
		bodies = append(bodies, b)
	}
	off := 4 + 6*nv
	so := off
	for _, b := range bodies {
		so += len(b)
	}
	out := []byte{byte(nv), 0}
	out = le16(out, so)
	for i, v := range l.Variants {
		when := byte(index([]string{"always", "none", "one", "many"}, v.When))
		out = append(out, when, refCode(v.Ref, false))
		out = le16(out, off)
		out = le16(out, len(v.Elements))
		off += len(bodies[i])
	}
	for _, b := range bodies {
		out = append(out, b...)
	}
	return append(out, st.data...)
}

// Compile scrive il CTHM v1 (docs/TEMI.md §5). Il tema deve venire da Parse senza errori.
func Compile(t *Theme) (*Compiled, error) {
	c := &Compiled{Screens: map[string]int{}}
	var ps problems
	type scr struct {
		code byte
		b    []byte
	}
	var list []scr
	for _, k := range ScreenKeys { // ordine dei codici
		l, ok := t.Screens[k]
		if !ok {
			continue
		}
		code, _ := ScreenCode(k)
		b := block(l)
		c.Screens[k] = len(b)
		if len(b) > MaxBlock {
			ps.add("screens."+k, "%d byte: oltre %d byte per schermata (togli elementi o testi)", len(b), MaxBlock)
		}
		list = append(list, scr{code, b})
	}
	out := make([]byte, HeaderLen, HeaderLen+8*len(list))
	copy(out, "CTHM")
	out[4], out[5] = 1, HeaderLen
	binary.LittleEndian.PutUint16(out[16:], 250)
	binary.LittleEndian.PutUint16(out[18:], 122)
	copy(out[20:44], t.ID)
	copy(out[44:68], t.Name)
	out[68] = byte(len(list))
	pos := HeaderLen + 8*len(list)
	for _, s := range list {
		out = append(out, s.code, 0)
		out = binary.LittleEndian.AppendUint16(out, uint16(len(s.b)))
		out = binary.LittleEndian.AppendUint32(out, uint32(pos))
		pos += len(s.b)
	}
	for _, s := range list {
		out = append(out, s.b...)
	}
	// oggi irraggiungibile (12 schermate x 4096 + intestazione < 65536): difesa per schermate future
	if len(out) > MaxFile {
		ps.add("", "%d byte: oltre %d byte per tema", len(out), MaxFile)
	}
	if len(ps) > 0 {
		return c, &InvalidError{ps}
	}
	binary.LittleEndian.PutUint32(out[12:], uint32(len(out)))
	c.CRC = crc32.ChecksumIEEE(out[12:])
	binary.LittleEndian.PutUint32(out[8:], c.CRC)
	c.Bin = out
	return c, nil
}
