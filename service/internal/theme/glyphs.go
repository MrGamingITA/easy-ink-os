package theme

import (
	"fmt"
	"sort"
	"strings"
)

// Glifi di firmware/src/gfx/fonts/f_*.h (TestFontGlyphsMatchFirmware), senza le legature private U+E000/U+E001.
const (
	smallGlyphs = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~°·ÀÈÉÌÒ×Ùàèéìíòóùú–—’…′″‹›€←↑→↓−▲▶▼◀●✓✗"
	// Chakra Petch da 26 px in su (tools/fonts/gen.py, BIG_CHARS): gli altri escono «?».
	bigGlyphs = " %+,-./0123456789:?abcdefghijklmnopqrstuvwxyz…"
)

// ClockGlyphs: i caratteri che l'orologio può mostrare.
const ClockGlyphs = "-0123456789:"

const (
	weekdays = "domenica lunedì martedì mercoledì giovedì venerdì sabato"
	months   = "gennaio febbraio marzo aprile maggio giugno luglio agosto settembre ottobre novembre dicembre"
)

// alpha: le rune di s senza doppioni, in ordine di codepoint.
func alpha(s string) string {
	seen := map[rune]bool{}
	var rs []rune
	for _, r := range s {
		if !seen[r] {
			seen[r] = true
			rs = append(rs, r)
		}
	}
	sort.Slice(rs, func(i, j int) bool { return rs[i] < rs[j] })
	return string(rs)
}

var (
	smallSet = alpha(smallGlyphs)
	bigSet   = alpha(bigGlyphs)
)

// FontGlyphs: i caratteri che il carattere ha, in ordine di codepoint.
func FontGlyphs(f Font) string {
	if f.Family == "ui" && f.Size >= 26 {
		return bigSet
	}
	return smallSet
}

func FontLabel(f Font) string {
	if f.Family == "pix" {
		return fmt.Sprintf("Silkscreen %d", f.Size)
	}
	return fmt.Sprintf("Chakra Petch %d", f.Size)
}

// mapCase come mapCase di values.js: ASCII e U+00E0–U+00FE (tranne ÷) / U+00C0–U+00DE (tranne ×).
func mapCase(s string, upper bool) string {
	return strings.Map(func(c rune) rune {
		switch {
		case upper && ((c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7)):
			return c - 0x20
		case !upper && ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)):
			return c + 0x20
		}
		return c
	}, s)
}

func valueSpecByID(id uint16) (name, glyphs string, free bool) {
	if id == ValClockSec {
		return "ora", ClockGlyphs, false
	}
	for _, v := range Values {
		if v.ID == id {
			return v.Name, v.Glyphs, v.Free
		}
	}
	return fmt.Sprintf("0x%02x", id), "", true
}

func missingIn(have, s string) string {
	var miss []rune
	for _, r := range s {
		if !strings.ContainsRune(have, r) && !strings.ContainsRune(string(miss), r) {
			miss = append(miss, r)
		}
	}
	return string(miss)
}

// GlyphWarnings: avvisi (senza percorso) per i caratteri che il carattere dell'elemento non ha, nell'ordine dei pezzi.
func GlyphWarnings(e *Element) []string {
	if e.Type != "text" && e.Type != "clock" {
		return nil
	}
	have := FontGlyphs(e.Font)
	lbl := FontLabel(e.Font)
	var out []string
	seen := map[string]bool{}
	for _, s := range e.Segs {
		if !s.IsVal {
			t := s.Lit
			if e.Upper {
				t = mapCase(t, true)
			}
			if miss := missingIn(have, t); miss != "" {
				out = append(out, fmt.Sprintf("%s non ha «%s»: si vedrà «?»", lbl, miss))
			}
			continue
		}
		name, g, free := valueSpecByID(s.Val)
		if seen[name] {
			continue
		}
		seen[name] = true
		if free {
			full := true
			for c := rune(0x20); c <= 0x7e; c++ {
				if !strings.ContainsRune(have, c) {
					full = false
					break
				}
			}
			if !full {
				var punct []rune
				for _, r := range have {
					if r != ' ' && !(r >= '0' && r <= '9') && !(r >= 'a' && r <= 'z') && !(r >= 'A' && r <= 'Z') {
						punct = append(punct, r)
					}
				}
				out = append(out, fmt.Sprintf("{%s} è testo libero: %s ha solo cifre, minuscole e «%s»", name, lbl, string(punct)))
			}
			continue
		}
		switch s.Filter {
		case 1:
			g = mapCase(g, true)
		case 2:
			g = mapCase(g, false)
		}
		if e.Upper {
			g = mapCase(g, true)
		}
		if miss := missingIn(have, alpha(g)); miss != "" {
			out = append(out, fmt.Sprintf("{%s} può mostrare «%s», che %s non ha: si vedrà «?»", name, miss, lbl))
		}
	}
	return out
}
