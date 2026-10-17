package theme

import (
	"fmt"
	"strings"
)

// Seg: un pezzo di modello. Filter: 0 nessuno, 1 maiuscolo, 2 minuscolo.
type Seg struct {
	Lit    string
	Val    uint16
	IsVal  bool
	Filter byte
}

// ParseTemplate: «testo {valore|filtro} testo»; {{ e }} = graffe. inRow: dentro un riquadro lista (riga.* ammessi).
func ParseTemplate(s string, inRow bool) ([]Seg, error) {
	var out []Seg
	var lit strings.Builder
	flush := func() error {
		if lit.Len() == 0 {
			return nil
		}
		if lit.Len() > MaxLiteral {
			return fmt.Errorf("testo fisso di %d byte: al massimo %d", lit.Len(), MaxLiteral)
		}
		out = append(out, Seg{Lit: lit.String()})
		lit.Reset()
		return nil
	}
	for i := 0; i < len(s); i++ {
		c := s[i]
		switch {
		case c == '{' && i+1 < len(s) && s[i+1] == '{':
			lit.WriteByte('{')
			i++
		case c == '}' && i+1 < len(s) && s[i+1] == '}':
			lit.WriteByte('}')
			i++
		case c == '{':
			j := strings.IndexByte(s[i:], '}')
			if j < 0 {
				return nil, fmt.Errorf("graffa non chiusa in %q", s)
			}
			body := s[i+1 : i+j]
			name, filter, _ := strings.Cut(body, "|")
			id, ok := ValueID(name)
			if !ok {
				return nil, fmt.Errorf("valore sconosciuto {%s}", name)
			}
			if id >= 0x40 && id < 0x80 && !inRow {
				return nil, fmt.Errorf("{%s}: riga.* solo dentro un riquadro lista", name)
			}
			var f byte
			switch filter {
			case "":
			case "maiuscolo":
				f = 1
			case "minuscolo":
				f = 2
			default:
				return nil, fmt.Errorf("filtro sconosciuto «%s» (maiuscolo o minuscolo)", filter)
			}
			if err := flush(); err != nil {
				return nil, err
			}
			out = append(out, Seg{Val: id, IsVal: true, Filter: f})
			i += j
		default:
			lit.WriteByte(c)
		}
	}
	if err := flush(); err != nil {
		return nil, err
	}
	if len(out) > MaxSegs {
		return nil, fmt.Errorf("%d pezzi nel testo: al massimo %d", len(out), MaxSegs)
	}
	return out, nil
}
