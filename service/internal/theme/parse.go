package theme

import (
	"encoding/json"
	"fmt"
	"regexp"
	"sort"
	"unicode/utf8"
)

var idRe = regexp.MustCompile(`^[a-z0-9]+(-[a-z0-9]+)*$`)

// Campi ammessi per tipo (oltre ai comuni).
var commonKeys = []string{"type", "id", "name", "x", "y", "w", "h", "visibleIf", "invert", "hidden", "locked"}
var typeKeys = map[string][]string{
	"mascot":    {"scale", "pose", "ref", "accessory", "animated"},
	"text":      {"text", "font", "align", "lines", "lineHeight", "upper"},
	"clock":     {"format", "font", "align"},
	"battery":   {"scale", "percent"},
	"wifi":      {"scale"},
	"stateIcon": {"scale", "ref"},
	"bar":       {"source", "style"},
	"qr":        {"text"},
	"sym":       {"sym", "scale"},
	"line":      {"dir", "style"},
	"rect":      {"border", "fill", "radius"},
	"circle":    {"border", "fill"},
	"list":      {"rowH", "gap", "pageSec", "pageDots", "row"},
	"group":     {"children"},
}
var groupKeys = []string{"type", "id", "name", "x", "y", "visibleIf", "hidden", "locked", "children"}

var poses = []string{"working", "permission", "input", "done", "error", "idle", "compact", "tired", "stuffy"}
var states = []string{"working", "permission", "input", "error", "compact", "done", "idle"}
var syms = []string{"plug", "bell", "moon", "lock", "bolt", "dot", "glass", "gauge"}
var sources = []string{"context", "tasks", "workday", "battery", "limit5h", "limit7d"}

func index(list []string, s string) int {
	for i, v := range list {
		if v == s {
			return i
		}
	}
	return -1
}

// rawElem: un elemento come arriva, con i campi facoltativi a puntatore per i predefiniti.
type rawElem struct {
	Type       string            `json:"type"`
	ID         string            `json:"id"`
	Name       string            `json:"name"`
	X          int               `json:"x"`
	Y          int               `json:"y"`
	W          *int              `json:"w"`
	H          *int              `json:"h"`
	VisibleIf  []json.RawMessage `json:"visibleIf"`
	Invert     bool              `json:"invert"`
	Hidden     bool              `json:"hidden"`
	Locked     bool              `json:"locked"`
	Scale      *int              `json:"scale"`
	Pose       string            `json:"pose"`
	Ref        string            `json:"ref"`
	Accessory  *bool             `json:"accessory"`
	Animated   *bool             `json:"animated"`
	Text       *string           `json:"text"`
	Font       *Font             `json:"font"`
	Align      string            `json:"align"`
	Lines      *int              `json:"lines"`
	LineHeight *int              `json:"lineHeight"`
	Upper      bool              `json:"upper"`
	Format     string            `json:"format"`
	Percent    bool              `json:"percent"`
	Source     string            `json:"source"`
	Style      string            `json:"style"`
	Sym        string            `json:"sym"`
	Dir        string            `json:"dir"`
	Border     *int              `json:"border"`
	Fill       bool              `json:"fill"`
	Radius     int               `json:"radius"`
	RowH       int               `json:"rowH"`
	Gap        int               `json:"gap"`
	PageSec    *int              `json:"pageSec"`
	PageDots   *bool             `json:"pageDots"`
	Row        []json.RawMessage `json:"row"`
	Children   []json.RawMessage `json:"children"`
}

type parser struct {
	ps    problems
	warns []string
}

// Parse controlla un tema. Errori vuoti = valido; gli avvisi non impediscono la compilazione.
func Parse(data []byte) (*Theme, []Problem, []string) {
	p := &parser{}
	var top map[string]json.RawMessage
	if err := json.Unmarshal(data, &top); err != nil {
		p.ps.add("", "JSON non valido: %v", err)
		return nil, p.ps, nil
	}
	p.keys(top, "", []string{"format", "version", "id", "name", "screen", "screens", "menu"})
	t := &Theme{Screens: map[string]*Layout{}}
	p.field(top, "format", &t.Format)
	p.field(top, "version", &t.Version)
	p.field(top, "id", &t.ID)
	p.field(top, "name", &t.Name)
	p.field(top, "screen", &t.Screen)
	if t.Format != Format {
		p.ps.add("format", "deve essere %q", Format)
	}
	if t.Version != Version {
		p.ps.add("version", "versione %d non supportata (questa è la %d)", t.Version, Version)
	}
	switch {
	case t.ID == ReservedID:
		p.ps.add("id", "«calamaio» è il tema di serie: scegli un altro id")
	case len(t.ID) < 1 || len(t.ID) > 24 || !idRe.MatchString(t.ID):
		p.ps.add("id", "id: kebab-case (a-z, 0-9, trattini), da 1 a 24 caratteri")
	}
	if !textOK(t.Name, 24) {
		p.ps.add("name", "nome: da 1–24 byte, senza a capo né caratteri di controllo")
	}
	if t.Screen != ScreenS {
		p.ps.add("screen", "schermo %q: per ora solo 250x122", t.Screen)
	}
	if _, ok := top["menu"]; ok {
		p.ps.add("menu", "la sezione menu arriva con la fase 4: per ora toglila")
	}
	var screens map[string]json.RawMessage
	p.field(top, "screens", &screens)
	keys := make([]string, 0, len(screens))
	for k := range screens {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		path := "screens." + k
		switch {
		case lockedScreens[k]:
			p.ps.add(path, "schermata bloccata: regole di sicurezza (Permessi, abbinamento e sistema restano di serie)")
			continue
		case laterScreens[k]:
			p.ps.add(path, "schermata non ancora personalizzabile")
			continue
		}
		if _, ok := ScreenCode(k); !ok {
			p.ps.add(path, "schermata sconosciuta")
			continue
		}
		if l := p.layout(screens[k], path); l != nil {
			t.Screens[k] = l
		}
	}
	if len(screens) == 0 {
		p.ps.add("screens", "nessuna schermata")
	}
	return t, p.ps, p.warns
}

func textOK(s string, max int) bool {
	if len(s) < 1 || len(s) > max || !utf8.ValidString(s) {
		return false
	}
	for _, r := range s {
		if r < 0x20 || r == 0x7f {
			return false
		}
	}
	return true
}

func (p *parser) keys(m map[string]json.RawMessage, path string, allowed []string) {
	ks := make([]string, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	for _, k := range ks {
		if index(allowed, k) < 0 {
			p.ps.add(path, "campo sconosciuto «%s»", k)
		}
	}
}

func (p *parser) field(m map[string]json.RawMessage, k string, dst any) {
	raw, ok := m[k]
	if !ok {
		return
	}
	if err := json.Unmarshal(raw, dst); err != nil {
		p.ps.add(k, "valore del tipo sbagliato")
	}
}

func (p *parser) layout(raw json.RawMessage, path string) *Layout {
	var l struct {
		Variants []json.RawMessage `json:"variants"`
	}
	var m map[string]json.RawMessage
	if json.Unmarshal(raw, &m) != nil || json.Unmarshal(raw, &l) != nil {
		p.ps.add(path, "serve un oggetto {variants: [...]}")
		return nil
	}
	p.keys(m, path, []string{"variants"})
	if len(l.Variants) < 1 || len(l.Variants) > MaxVariants {
		p.ps.add(path+".variants", "da 1 a %d varianti", MaxVariants)
		return nil
	}
	out := &Layout{}
	for i, vr := range l.Variants {
		vp := fmt.Sprintf("%s.variants[%d]", path, i)
		var vm map[string]json.RawMessage
		var v struct {
			When     string            `json:"when"`
			Ref      string            `json:"ref"`
			Elements []json.RawMessage `json:"elements"`
		}
		if json.Unmarshal(vr, &vm) != nil || json.Unmarshal(vr, &v) != nil {
			p.ps.add(vp, "variante non valida")
			continue
		}
		p.keys(vm, vp, []string{"when", "ref", "elements"})
		if index([]string{"always", "none", "one", "many"}, v.When) < 0 {
			p.ps.add(vp, "when: always, none, one o many")
		}
		if v.Ref == "" {
			v.Ref = "selected"
		}
		if v.Ref != "selected" && v.Ref != "urgent" {
			p.ps.add(vp, "ref: selected o urgent")
		}
		var els []*Element
		for j, er := range v.Elements {
			els = append(els, p.element(er, fmt.Sprintf("%s.elements[%d]", vp, j), false, 0, 0, 0, nil)...)
		}
		n := len(els)
		for _, e := range els {
			n += len(e.Row)
		}
		if n > MaxElements {
			p.ps.add(vp, "%d elementi: al massimo %d elementi per variante (righe comprese)", n, MaxElements)
		}
		out.Variants = append(out.Variants, &Variant{When: v.When, Ref: v.Ref, Elements: els})
	}
	return out
}

// element controlla un elemento e lo restituisce pronto (un gruppo diventa i suoi figli).
// dx, dy: posizione del gruppo che lo contiene; inherited: condizioni del gruppo.
func (p *parser) element(raw json.RawMessage, path string, inRow bool, depth, dx, dy int, inherited []Cond) []*Element {
	var m map[string]json.RawMessage
	var r rawElem
	if json.Unmarshal(raw, &m) != nil {
		p.ps.add(path, "serve un oggetto")
		return nil
	}
	if err := json.Unmarshal(raw, &r); err != nil {
		p.ps.add(path, "valore del tipo sbagliato (%v)", err)
		return nil
	}
	extra, ok := typeKeys[r.Type]
	if !ok {
		p.ps.add(path, "tipo sconosciuto «%s»", r.Type)
		return nil
	}
	if r.Type == "group" {
		p.keys(m, path, groupKeys)
	} else {
		p.keys(m, path, append(append([]string{}, commonKeys...), extra...))
	}
	if r.Hidden {
		return nil
	}
	if len(r.Name) > 32 || len(r.ID) > 32 {
		p.ps.add(path, "id e name: al massimo 32 byte")
	}
	conds := append(append([]Cond{}, inherited...), p.conds(r.VisibleIf, path)...)
	if len(conds) > MaxConds {
		p.ps.add(path, "%d condizioni: al massimo %d (con quelle dei gruppi)", len(conds), MaxConds)
	}
	x, y := r.X+dx, r.Y+dy
	if x < -CoordMax || x > CoordMax {
		p.ps.add(path, "x: fuori da ±512")
	}
	if y < -CoordMax || y > CoordMax {
		p.ps.add(path, "y: fuori da ±512")
	}
	if r.Type == "group" {
		if depth >= 4 {
			p.ps.add(path, "gruppi annidati: al massimo 4 livelli")
			return nil
		}
		if len(r.Children) == 0 {
			p.ps.add(path, "gruppo vuoto")
		}
		var out []*Element
		for i, c := range r.Children {
			out = append(out, p.element(c, fmt.Sprintf("%s.children[%d]", path, i), inRow, depth+1, x, y, conds)...)
		}
		return out
	}
	e := &Element{Type: r.Type, X: x, Y: y, Invert: r.Invert, Conds: conds, Ref: r.Ref}
	need := func(v *int, name string) int {
		if v == nil {
			p.ps.add(path, "%s obbligatoria", name)
			return 0
		}
		if *v < 0 || *v > CoordMax {
			p.ps.add(path, "%s: da 0 a 512", name)
		}
		return *v
	}
	rng := func(v *int, def, lo, hi int, name string) int {
		if v == nil {
			return def
		}
		if *v < lo || *v > hi {
			p.ps.add(path, "%s: da %d a %d", name, lo, hi)
		}
		return *v
	}
	ref := func(allowNobody bool) {
		switch e.Ref {
		case "", "selected", "urgent":
		case "nobody":
			if !allowNobody {
				p.ps.add(path, "ref: selected, urgent o row")
			}
		case "row":
			if !inRow {
				p.ps.add(path, "ref row solo dentro un riquadro lista")
			}
		default:
			p.ps.add(path, "ref: selected, urgent, row o nobody")
		}
	}
	font := func() {
		if r.Font == nil || !FontOK(r.Font.Family, r.Font.Size, r.Font.Weight) {
			p.ps.add(path, "font: carattere che la scheda non ha (vedi docs/TEMI.md §2)")
			return
		}
		e.Font = *r.Font
	}
	align := func() {
		e.Align = r.Align
		if e.Align == "" {
			e.Align = "left"
		}
		if index([]string{"left", "center", "right"}, e.Align) < 0 {
			p.ps.add(path, "align: left, center o right")
		}
	}
	tmpl := func() {
		if r.Text == nil || *r.Text == "" {
			p.ps.add(path, "text obbligatorio")
			return
		}
		segs, err := ParseTemplate(*r.Text, inRow)
		if err != nil {
			p.ps.add(path, "%v", err)
			return
		}
		e.Segs = segs
	}
	switch r.Type {
	case "mascot":
		e.Scale = rng(r.Scale, 1, 1, 4, "scale")
		e.W, e.H = 28*e.Scale, 25*e.Scale
		e.Pose = r.Pose
		if e.Pose == "" {
			e.Pose = "auto"
		}
		if e.Pose != "auto" && index(poses, e.Pose) < 0 {
			p.ps.add(path, "pose sconosciuta «%s»", e.Pose)
		}
		ref(true)
		e.Accessory = r.Accessory == nil || *r.Accessory
		e.Animated = r.Animated == nil || *r.Animated
	case "text", "clock":
		e.W = need(r.W, "w")
		font()
		align()
		e.Lines = 1
		if r.Type == "text" {
			tmpl()
			e.Lines = rng(r.Lines, 1, 1, 8, "lines")
			e.Upper = r.Upper
		} else {
			switch r.Format {
			case "", "hm":
				e.Segs = []Seg{{Val: 0x80, IsVal: true}}
			case "hms":
				e.Segs = []Seg{{Val: ValClockSec, IsVal: true}}
			default:
				p.ps.add(path, "format: hm o hms")
			}
		}
		e.LineHeight = rng(r.LineHeight, e.Font.Size+2, 1, 64, "lineHeight")
		e.H = e.Lines * e.LineHeight
		for _, w := range GlyphWarnings(e) {
			p.warns = append(p.warns, path+": "+w)
		}
	case "battery":
		e.Scale = rng(r.Scale, 1, 1, 2, "scale")
		e.W, e.H, e.Percent = 16*e.Scale, 8*e.Scale, r.Percent
	case "wifi":
		e.Scale = rng(r.Scale, 1, 1, 2, "scale")
		e.W, e.H = 11*e.Scale, 9*e.Scale
	case "stateIcon":
		e.Scale = rng(r.Scale, 1, 1, 3, "scale")
		e.W, e.H = 7*e.Scale, 7*e.Scale
		ref(true)
	case "sym":
		e.Scale = rng(r.Scale, 1, 1, 3, "scale")
		e.W, e.H, e.Sym = 7*e.Scale, 7*e.Scale, r.Sym
		if index(syms, r.Sym) < 0 {
			p.ps.add(path, "sym: plug, bell, moon, lock, bolt, dot, glass o gauge")
		}
	case "bar":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.Source, e.Style = r.Source, r.Style
		if index(sources, r.Source) < 0 {
			p.ps.add(path, "source: context, tasks, workday, battery, limit5h o limit7d")
		}
		if e.Style == "" {
			e.Style = "big"
		}
		if e.Style != "big" && e.Style != "bar" {
			p.ps.add(path, "style: big o bar")
		}
	case "qr":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		tmpl()
	case "line":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.Dir, e.Style = r.Dir, r.Style
		if e.Dir == "" {
			e.Dir = "h"
		}
		if e.Style == "" {
			e.Style = "solid"
		}
		if (e.Dir != "h" && e.Dir != "v") || (e.Style != "solid" && e.Style != "dotted") {
			p.ps.add(path, "dir: h o v; style: solid o dotted")
		}
	case "rect", "circle":
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.Border = rng(r.Border, 1, 0, 8, "border")
		e.Fill = r.Fill
		if r.Type == "rect" {
			e.Radius = r.Radius
			if e.Radius < 0 || e.Radius > 8 {
				p.ps.add(path, "radius: da 0 a 8")
			}
		}
		if e.Border == 0 && !e.Fill {
			p.warns = append(p.warns, path+": né bordo né riempimento, non si vede")
		}
	case "list":
		if inRow {
			p.ps.add(path, "un riquadro lista non può stare dentro un altro")
			return nil
		}
		e.W, e.H = need(r.W, "w"), need(r.H, "h")
		e.RowH, e.Gap = r.RowH, r.Gap
		if e.RowH < 1 || e.RowH > 122 {
			p.ps.add(path, "rowH: da 1 a 122")
		}
		if e.Gap < 0 || e.Gap > 60 {
			p.ps.add(path, "gap: da 0 a 60")
		}
		e.PageSec = rng(r.PageSec, 5, 1, 60, "pageSec")
		e.PageDots = r.PageDots == nil || *r.PageDots
		for i, c := range r.Row {
			e.Row = append(e.Row, p.element(c, fmt.Sprintf("%s.row[%d]", path, i), true, depth, 0, 0, nil)...)
		}
		if len(e.Row) < 1 || len(e.Row) > MaxRow {
			p.ps.add(path, "row: da 1 a %d elementi", MaxRow)
		}
	}
	return []*Element{e}
}

func (p *parser) conds(raws []json.RawMessage, path string) []Cond {
	var out []Cond
	boolOps := map[string]byte{"perm": CondPerm, "charging": CondCharging, "night": CondNight, "pc": CondPC, "workday": CondWorkday, "game": CondGame}
	for i, raw := range raws {
		cp := fmt.Sprintf("%s.visibleIf[%d]", path, i)
		var m map[string]json.RawMessage
		if json.Unmarshal(raw, &m) != nil || len(m) != 1 {
			p.ps.add(cp, "ogni condizione è un oggetto con una sola chiave")
			continue
		}
		for k, v := range m {
			if op, ok := boolOps[k]; ok {
				var b bool
				if json.Unmarshal(v, &b) != nil {
					p.ps.add(cp, "%s: true o false", k)
				}
				out = append(out, Cond{op, b2u(b)})
				continue
			}
			switch k {
			case "battBelow":
				var n int
				if json.Unmarshal(v, &n) != nil || n < 1 || n > 100 {
					p.ps.add(cp, "battBelow: da 1 a 100")
				}
				out = append(out, Cond{CondBattBelow, byte(n)})
			case "sessions":
				var s struct{ Min, Max *int }
				if json.Unmarshal(v, &s) != nil || (s.Min == nil && s.Max == nil) {
					p.ps.add(cp, "sessions: {min, max} da 0 a 6")
					continue
				}
				for _, mm := range []struct {
					v  *int
					op byte
				}{{s.Min, CondSessionsMin}, {s.Max, CondSessionsMax}} {
					if mm.v == nil {
						continue
					}
					if *mm.v < 0 || *mm.v > 6 {
						p.ps.add(cp, "sessions: {min, max} da 0 a 6")
					}
					out = append(out, Cond{mm.op, byte(*mm.v)})
				}
			case "state":
				var s string
				_ = json.Unmarshal(v, &s)
				k := index(states, s)
				if k < 0 {
					p.ps.add(cp, "state: working, permission, input, error, compact, done o idle")
				}
				out = append(out, Cond{CondState, byte(k)})
			case "blink":
				var s string
				_ = json.Unmarshal(v, &s)
				if s != "even" && s != "odd" {
					p.ps.add(cp, "blink: even o odd")
				}
				out = append(out, Cond{CondBlink, b2u(s == "odd")})
			default:
				p.ps.add(cp, "condizione sconosciuta «%s»", k)
			}
		}
	}
	return out
}

func b2u(b bool) byte {
	if b {
		return 1
	}
	return 0
}
