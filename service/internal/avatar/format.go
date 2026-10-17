// Package avatar: formato degli avatar delle sessioni (docs/AVATAR.md), validazione, binario CAVT,
// librerie (predefinita e dell'utente), assegnazioni e scelta di quelli liberi.
//
// È il porting in Go di tools/avatar/lib/format.mjs e pack.mjs: stesse regole, stesse soglie e
// stessi messaggi. I test confrontano i risultati con gli strumenti Node quando node è installato.
package avatar

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"regexp"
	"strings"
	"unicode"
	"unicode/utf16"
)

const (
	Format  = "calamaio-avatar"
	Version = 1
	W       = 28
	H       = 25
	// MaxJSONBytes: limite del file JSON; MaxBinBytes: limite del binario CAVT (docs/AVATAR.md §5,
	// domanda 1 risolta dal proprietario: 16 KB per il binario, 48 KB per il JSON).
	MaxJSONBytes = 48 * 1024
	MaxBinBytes  = 16 * 1024
	// Soglie euristiche ricavate dalla Goccia (docs/AVATAR.md §4; tools/avatar/lib/format.mjs).
	MinPoseDiff  = 16
	MinInk       = 120
	MaxFrameDiff = 200
	MaxChecker   = 2
	// MaxIDLen e MaxNameBytes: campi id e name (anche i 24 byte fissi dell'intestazione CAVT).
	MaxIDLen         = 24
	MaxNameBytes     = 24
	MaxDescriptionBy = 120
	// DefaultID è l'avatar di ripiego (compilato anche nel firmware).
	DefaultID = "goccia"
)

// Poses: ordine fisso, è anche l'ordine dei bit di poseMask nel binario. run (bit 9) è l'ultima: le
// pose 0-8 restano dove erano nel binario (docs/AVATAR.md §12).
var Poses = []string{"working", "permission", "input", "done", "error", "idle", "compact", "tired", "stuffy", "run"}

// LegacyPoses: le pose che conosce il firmware ≤ 0.6.0 (CAVT senza il bit 9).
var LegacyPoses = Poses[:9]

// Required: le prime 6 pose sono obbligatorie.
var Required = Poses[:6]

// Fallback: ripiego delle pose facoltative.
var Fallback = map[string]string{"compact": "working", "tired": "idle", "stuffy": "idle", "run": "working"}

// Corsa (posa run, docs/AVATAR.md §12): soglie di runChecks() in tools/avatar/lib/format.mjs.
const (
	RunEdge         = W - 1 // colonna del bordo destro in tutti i fotogrammi
	RunFoot         = H - 1 // riga dei piedi
	MinRunStep      = 12    // pixel diversi tra fotogrammi consecutivi
	MaxRunInkSpread = 0.25  // (max − min)/max dell'inchiostro tra i fotogrammi
)

// Nuvoletta: riquadro riservato e pose che lo usano.
var (
	bubbleX0, bubbleX1, bubbleY0, bubbleY1 = 17, 27, 0, 8
	bubblePoses                            = []string{"working", "permission", "input", "compact"}
)

var idRe = regexp.MustCompile(`^[a-z0-9]+(-[a-z0-9]+)*$`)

// ValidID dice se s è un id di avatar valido (kebab-case, al massimo 24 caratteri).
func ValidID(s string) bool { return len(s) <= MaxIDLen && idRe.MatchString(s) }

// Avatar è un avatar valido (solo dopo Validate senza errori).
type Avatar struct {
	ID          string
	Name        string
	Author      string
	Description string
	W, H        int
	Frames      int
	// Poses: posa → fotogrammi → righe (stringhe ASCII di W simboli . # +).
	Poses map[string][][]string
	// Anchors: punti di aggancio degli accessori (docs/AVATAR.md §13), nil se l'avatar non li ha.
	// Dal campo `anchors` del JSON o, per i predefiniti, dal file accanto (ancore/<id>.anchors.json).
	Anchors Anchors
}

// Present restituisce le pose presenti, nell'ordine fisso.
func (a *Avatar) Present() []string {
	out := []string{}
	for _, p := range Poses {
		if _, ok := a.Poses[p]; ok {
			out = append(out, p)
		}
	}
	return out
}

// PoseFrames: fotogrammi effettivi di una posa (con il ripiego delle facoltative).
func (a *Avatar) PoseFrames(p string) [][]string {
	if f, ok := a.Poses[p]; ok {
		return f
	}
	if fb, ok := Fallback[p]; ok {
		return a.Poses[fb]
	}
	return nil
}

// Stats: misure del validatore (come stats di validate() in Node).
type Stats struct {
	JSONBytes   int    `json:"jsonBytes"`
	Poses       int    `json:"poses"`
	MinInk      int    `json:"minInk"`
	MinPoseDiff int    `json:"minPoseDiff"`
	Closest     string `json:"closest"`
	// RunInkSpread: solo con la posa run.
	RunInkSpread *float64 `json:"runInkSpread,omitempty"`
}

// Result è l'esito di Validate. Errors vuoto = avatar valido (Avatar non nil).
type Result struct {
	Errors   []string `json:"errors"`
	Warnings []string `json:"warnings"`
	Stats    Stats    `json:"stats"`
	Avatar   *Avatar  `json:"-"`
}

// OK dice se l'avatar è valido.
func (r Result) OK() bool { return len(r.Errors) == 0 && r.Avatar != nil }

// Validate controlla un file avatar (byte del file così com'è: il limite dei 48 KB vale su questi).
func Validate(raw []byte) Result { return validate(raw, len(raw)) }

// ValidateNoSize come Validate ma senza il controllo del peso (validate(av) senza opts in Node).
func ValidateNoSize(raw []byte) Result { return validate(raw, -1) }

func validate(raw []byte, size int) Result {
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
	str := func(k string) (string, bool) { return asString(top.m[k]) }
	num := func(k string) (float64, bool) { return asNumber(top.m[k]) }

	if s, ok := str("format"); !ok || s != Format {
		E(`format deve essere "%s"`, Format)
	}
	if n, ok := num("version"); !ok || n != Version {
		E("version deve essere %d", Version)
	}
	id, idOK := str("id")
	if !idOK || !idRe.MatchString(id) || utf16Len(id) > MaxIDLen {
		E("id: kebab-case [a-z0-9-], al massimo 24 caratteri")
	}
	name, nameOK := str("name")
	if !nameOK || jsTrim(name) == "" || len(name) > MaxNameBytes {
		E("name: non vuoto, al massimo 24 byte UTF-8")
	}
	author, authOK := str("author")
	if !authOK || jsTrim(author) == "" {
		E("author: obbligatorio")
	}
	desc, descOK := str("description")
	if !descOK || len(desc) > MaxDescriptionBy {
		E("description: stringa di al massimo 120 byte UTF-8")
	}
	if n, ok := num("w"); !ok || n != W {
		E("w deve essere %d", W)
	}
	if n, ok := num("h"); !ok || n != H {
		E("h deve essere %d", H)
	}
	frames, fOK := num("frames")
	if !fOK || (frames != 2 && frames != 4) {
		E("frames deve essere 2 o 4")
	}
	known := map[string]bool{"format": true, "version": true, "id": true, "name": true, "author": true,
		"description": true, "w": true, "h": true, "frames": true, "poses": true, "anchors": true}
	for _, k := range top.keys {
		if !known[k] {
			E("campo sconosciuto: %s", k)
		}
	}
	if size >= 0 {
		r.Stats.JSONBytes = size
		if size > MaxJSONBytes {
			E("file JSON di %d byte: il massimo è %d", size, MaxJSONBytes)
		}
	}
	posesRaw, has := top.m["poses"]
	poses, isObj := parseObject(posesRaw)
	if !has || !isObj {
		E("poses mancante")
		return r
	}
	for _, p := range poses.keys {
		if !contains(Poses, p) {
			E("posa sconosciuta: %s", p)
		}
	}
	for _, p := range Required {
		if !truthy(poses.m[p]) {
			E("posa obbligatoria mancante: %s", p)
		}
	}
	if len(r.Errors) > 0 {
		return r
	}

	// Struttura dei fotogrammi.
	nf := int(frames)
	parsed := map[string][][]string{}
	for _, p := range poses.keys {
		var arr []json.RawMessage
		if !isArray(poses.m[p]) || json.Unmarshal(poses.m[p], &arr) != nil || len(arr) != nf {
			E("%s: servono %d fotogrammi, trovati %d", p, nf, len(arr))
			continue
		}
		frs := make([][]string, len(arr))
		for f, fr := range arr {
			var rows []json.RawMessage
			if !isArray(fr) || json.Unmarshal(fr, &rows) != nil || len(rows) != H {
				E("%s[%d]: servono %d righe", p, f, H)
				continue
			}
			out := make([]string, len(rows))
			for y, row := range rows {
				s, ok := asString(row)
				switch {
				case !ok || utf16Len(s) != W:
					E("%s[%d] riga %d: servono %d simboli", p, f, y, W)
				case strings.Trim(s, ".#+") != "":
					E("%s[%d] riga %d: simboli ammessi solo . # +", p, f, y)
				}
				out[y] = s
			}
			frs[f] = out
		}
		parsed[p] = frs
	}
	if len(r.Errors) > 0 {
		return r
	}
	var anchors Anchors
	if raw, ok := top.m["anchors"]; ok {
		var errs []string
		anchors, errs = parseAnchors(raw, presentOf(poses), nf)
		r.Errors = append(r.Errors, errs...)
	}

	// Controlli di leggibilità (euristici).
	present := []string{}
	for _, p := range Poses {
		if truthy(poses.m[p]) {
			present = append(present, p)
		}
	}
	r.Stats.Poses = len(present)
	r.Stats.MinInk = math.MaxInt
	for _, p := range present {
		fr := parsed[p]
		for f, frame := range fr {
			ink := InkCount(frame)
			r.Stats.MinInk = min(r.Stats.MinInk, ink)
			if ink == 0 {
				E("%s[%d]: silhouette vuota", p, f)
			} else if ink < MinInk {
				Wn("%s[%d]: solo %d pixel d'inchiostro (minimo consigliato %d): a scala 1 non si legge", p, f, ink, MinInk)
			}
			if ck := CheckerCount(frame); ck > MaxChecker {
				Wn("%s[%d]: %d blocchi a scacchiera, il retino fitto sfarfalla sull'e-ink", p, f, ck)
			}
		}
		for f := range fr {
			g := (f + 1) % len(fr)
			if d := Diff(fr[f], fr[g]); d > MaxFrameDiff {
				Wn("%s: %d pixel cambiano tra i fotogrammi %d e %d (massimo consigliato %d): sporca il refresh parziale", p, d, f, g, MaxFrameDiff)
			}
		}
		if nf == 4 && Diff(fr[0], fr[1]) == 0 && Diff(fr[0], fr[2])+Diff(fr[0], fr[3]) > 0 {
			Wn("%s: i fotogrammi 0 e 1 sono uguali, sui pannelli a 2 fotogrammi la posa resta ferma", p)
		}
	}
	if truthy(poses.m["run"]) {
		runChecks(parsed["run"], Wn, &r.Stats)
	}
	r.Stats.MinPoseDiff = math.MaxInt
	for i := 0; i < len(present); i++ {
		for j := i + 1; j < len(present); j++ {
			a, b := present[i], present[j]
			d := Diff(parsed[a][0], parsed[b][0])
			if d < r.Stats.MinPoseDiff {
				r.Stats.MinPoseDiff = d
				r.Stats.Closest = a + "/" + b
			}
			if d < MinPoseDiff {
				E("%s e %s differiscono di %d pixel nel fotogramma 0 (minimo %d): non si distinguono", a, b, d, MinPoseDiff)
			}
		}
	}
	// Nuvoletta: ingombro della Goccia (x 18..26, y 0..6, più la coda (18,7) e (17,8)).
	inBubble := func(x, y int) bool {
		return (x >= 18 && x <= 26 && y <= 6) || (x == 18 && y == 7) || (x == 17 && y == 8)
	}
	for _, p := range present {
		if !contains(bubblePoses, p) {
			continue
		}
		n := 0
		fr0 := parsed[p][0]
		for y := bubbleY0; y <= bubbleY1; y++ {
			for x := bubbleX0; x <= bubbleX1; x++ {
				if fr0[y][x] != '.' {
					n++
				}
			}
		}
		if n == 0 {
			Wn("%s: angolo in alto a destra vuoto, manca il simbolo dello stato (nuvoletta)", p)
		}
		for f, fr := range parsed[p] {
			var out []string
			for y := bubbleY0; y <= bubbleY1; y++ {
				for x := bubbleX0; x <= bubbleX1; x++ {
					if fr[y][x] != '.' && !inBubble(x, y) {
						out = append(out, fmt.Sprintf("(%d,%d)", x, y))
					}
				}
			}
			if len(out) > 0 {
				Wn("%s[%d]: %d pixel nell'angolo della nuvoletta fuori dal suo ingombro %s: il corpo tocca il simbolo dello stato",
					p, f, len(out), strings.Join(out[:min(4, len(out))], " "))
			}
		}
	}
	if len(r.Errors) > 0 {
		return r
	}
	a := &Avatar{ID: id, Name: name, Author: author, Description: desc, W: W, H: H, Frames: nf, Poses: map[string][][]string{}, Anchors: anchors}
	for _, p := range present {
		a.Poses[p] = parsed[p]
	}
	r.Avatar = a
	return r
}

// InkCount: pixel d'inchiostro di un fotogramma.
func InkCount(frame []string) int {
	n := 0
	for _, row := range frame {
		n += strings.Count(row, "#")
	}
	return n
}

// Diff: pixel diversi tra due fotogrammi.
func Diff(a, b []string) int {
	n := 0
	for y := range a {
		for x := 0; x < len(a[y]); x++ {
			if a[y][x] != b[y][x] {
				n++
			}
		}
	}
	return n
}

// CheckerCount: finestre 3×3 a scacchiera perfetta (retino fitto).
func CheckerCount(frame []string) int {
	n := 0
	I := func(x, y int) bool { return frame[y][x] == '#' }
	for y := 1; y+1 < len(frame); y++ {
		for x := 1; x+1 < len(frame[y]); x++ {
			c := I(x, y)
			if I(x-1, y-1) == c && I(x+1, y-1) == c && I(x-1, y+1) == c && I(x+1, y+1) == c &&
				I(x-1, y) != c && I(x+1, y) != c && I(x, y-1) != c && I(x, y+1) != c {
				n++
			}
		}
	}
	return n
}

// object è un oggetto JSON con l'ordine delle chiavi (come Object.keys in JavaScript).
type object struct {
	keys []string
	m    map[string]json.RawMessage
}

func parseObject(raw json.RawMessage) (object, bool) {
	o := object{m: map[string]json.RawMessage{}}
	dec := json.NewDecoder(bytes.NewReader(raw))
	tok, err := dec.Token()
	if err != nil {
		return o, false
	}
	if d, ok := tok.(json.Delim); !ok || d != '{' {
		return o, false
	}
	for dec.More() {
		kt, err := dec.Token()
		if err != nil {
			return o, false
		}
		k, _ := kt.(string)
		var v json.RawMessage
		if err := dec.Decode(&v); err != nil {
			return o, false
		}
		if _, dup := o.m[k]; !dup {
			o.keys = append(o.keys, k)
		}
		o.m[k] = v // come JSON.parse: vince l'ultimo
	}
	return o, true
}

func asString(raw json.RawMessage) (string, bool) {
	if len(raw) == 0 || raw[0] != '"' {
		return "", false
	}
	var s string
	if json.Unmarshal(raw, &s) != nil {
		return "", false
	}
	return s, true
}

func asNumber(raw json.RawMessage) (float64, bool) {
	if len(raw) == 0 {
		return 0, false
	}
	if c := raw[0]; c != '-' && (c < '0' || c > '9') {
		return 0, false
	}
	var f float64
	if json.Unmarshal(raw, &f) != nil {
		return 0, false
	}
	return f, true
}

func isArray(raw json.RawMessage) bool { return len(raw) > 0 && raw[0] == '[' }

// truthy: come la verità di JavaScript per un valore JSON (null, false, 0 e "" sono falsi).
func truthy(raw json.RawMessage) bool {
	s := string(bytes.TrimSpace(raw))
	switch s {
	case "", "null", "false", `""`:
		return false
	}
	if f, ok := asNumber(raw); ok {
		return f != 0
	}
	return true
}

// utf16Len: lunghezza di una stringa come String.length di JavaScript.
func utf16Len(s string) int {
	n := 0
	for _, r := range s {
		n += len(utf16.Encode([]rune{r}))
	}
	return n
}

// jsTrim: come String.prototype.trim (spazi Unicode e BOM).
func jsTrim(s string) string {
	return strings.TrimFunc(s, func(r rune) bool { return r == 0xFEFF || unicode.IsSpace(r) })
}

func contains(list []string, s string) bool {
	for _, x := range list {
		if x == s {
			return true
		}
	}
	return false
}

// runChecks: bordo destro netto e fermo, piedi sulla riga in basso, passo visibile, sagoma coerente
// (come runChecks() in tools/avatar/lib/format.mjs, stessi testi).
func runChecks(frs [][]string, Wn func(string, ...any), st *Stats) {
	inks := make([]int, len(frs))
	for f, fr := range frs {
		inks[f] = InkCount(fr)
		right := -1
		for _, row := range fr {
			for x := 0; x < len(row); x++ {
				if row[x] != '.' && x > right {
					right = x
				}
			}
		}
		if right != RunEdge {
			Wn("run[%d]: bordo destro in colonna %d, deve essere %d in tutti i fotogrammi (si allinea al fronte della barra)", f, right, RunEdge)
		}
		edge := 0
		for _, row := range fr {
			if len(row) > RunEdge && row[RunEdge] == '#' {
				edge++
			}
		}
		if right == RunEdge && edge < 3 {
			Wn("run[%d]: solo %d pixel d'inchiostro in colonna %d: il bordo destro non è netto", f, edge, RunEdge)
		}
		if !strings.Contains(fr[RunFoot], "#") {
			Wn("run[%d]: niente inchiostro sulla riga %d: i piedi non toccano la barra", f, RunFoot)
		}
	}
	for f := range frs {
		g := (f + 1) % len(frs)
		if d := Diff(frs[f], frs[g]); d < MinRunStep {
			Wn("run: solo %d pixel cambiano tra i fotogrammi %d e %d (minimo %d): la corsa sembra ferma", d, f, g, MinRunStep)
		}
	}
	lo, hi := inks[0], inks[0]
	for _, v := range inks {
		lo, hi = min(lo, v), max(hi, v)
	}
	spread := 0.0
	if hi > 0 {
		spread = float64(hi-lo) / float64(hi)
	}
	r := math.Round(spread*100) / 100
	st.RunInkSpread = &r
	if spread > MaxRunInkSpread {
		Wn("run: l'inchiostro varia da %d a %d pixel tra i fotogrammi (oltre il %g%%): la sagoma non resta la stessa", lo, hi, MaxRunInkSpread*100)
	}
}
