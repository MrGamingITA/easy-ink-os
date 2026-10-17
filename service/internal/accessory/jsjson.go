package accessory

import (
	"bytes"
	"encoding/json"
	"math"
	"sort"
	"strconv"
	"strings"
	"unicode"
	"unicode/utf16"
)

// Lettura del JSON con la semantica di JavaScript (JSON.parse, typeof, Number.isInteger,
// Object.keys), per dare gli stessi errori di validateAccessory() in Node. Copia ridotta degli
// aiuti di internal/avatar/format.go, più l'ordine delle chiavi di Object.keys.

type object struct {
	keys []string // ordine di Object.keys: prima le chiavi «indice» in ordine numerico, poi le altre
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
	var ins []string
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
			ins = append(ins, k)
		}
		o.m[k] = v // come JSON.parse: vince l'ultimo
	}
	var idx, rest []string
	for _, k := range ins {
		if arrayIndex(k) {
			idx = append(idx, k)
		} else {
			rest = append(rest, k)
		}
	}
	sort.Slice(idx, func(i, j int) bool {
		a, _ := strconv.ParseUint(idx[i], 10, 64)
		b, _ := strconv.ParseUint(idx[j], 10, 64)
		return a < b
	})
	o.keys = append(idx, rest...)
	return o, true
}

// arrayIndex: chiave che JavaScript ordina come indice ("0", "12"; non "012").
func arrayIndex(k string) bool {
	if k == "" || (len(k) > 1 && k[0] == '0') {
		return false
	}
	n, err := strconv.ParseUint(k, 10, 64)
	return err == nil && n < 1<<32-1
}

func (o object) has(k string) bool { _, ok := o.m[k]; return ok }

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

func asBool(raw json.RawMessage) (bool, bool) {
	switch string(raw) {
	case "true":
		return true, true
	case "false":
		return false, true
	}
	return false, false
}

// asInt: Number.isInteger(v) && lo ≤ v ≤ hi.
func asInt(raw json.RawMessage, lo, hi int) (int, bool) {
	f, ok := asNumber(raw)
	if !ok || f != math.Trunc(f) || f < float64(lo) || f > float64(hi) {
		return 0, false
	}
	return int(f), true
}

func isInteger(raw json.RawMessage) (int, bool) { return asInt(raw, -1<<30, 1<<30) }

func asArray(raw json.RawMessage) ([]json.RawMessage, bool) {
	if len(raw) == 0 || raw[0] != '[' {
		return nil, false
	}
	var arr []json.RawMessage
	if json.Unmarshal(raw, &arr) != nil {
		return nil, false
	}
	return arr, true
}

func isObject(raw json.RawMessage) bool { _, ok := parseObject(raw); return ok }

func utf16Len(s string) int {
	n := 0
	for _, r := range s {
		n += len(utf16.Encode([]rune{r}))
	}
	return n
}

// jsTrim: String.prototype.trim (spazi Unicode e BOM).
func jsTrim(s string) string {
	return strings.TrimFunc(s, func(r rune) bool { return r == 0xFEFF || unicode.IsSpace(r) })
}

// jsText: come JavaScript scrive un valore JSON in un template (`${v}`).
func jsText(raw json.RawMessage) string {
	if s, ok := asString(raw); ok {
		return s
	}
	t := strings.TrimSpace(string(raw))
	switch {
	case strings.HasPrefix(t, "{"):
		return "[object Object]"
	case strings.HasPrefix(t, "["):
		arr, _ := asArray(raw)
		parts := make([]string, len(arr))
		for i, v := range arr {
			if s := strings.TrimSpace(string(v)); s != "null" {
				parts[i] = jsText(v)
			}
		}
		return strings.Join(parts, ",")
	}
	if f, ok := asNumber(raw); ok {
		return strconv.FormatFloat(f, 'f', -1, 64)
	}
	return t
}
