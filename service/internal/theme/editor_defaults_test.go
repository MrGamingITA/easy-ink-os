package theme

import (
	"encoding/json"
	"os"
	"sort"
	"testing"
)

// I predefiniti dell'editor (assets/js/editor/ops.js, riportati in protocol/themes/editor-defaults.json
// dalla prova node) devono essere salvabili: ognuno compila, da solo, in un gruppo e come tema nuovo.
func TestEditorDefaultsCompile(t *testing.T) {
	b, err := os.ReadFile("../../../protocol/themes/editor-defaults.json")
	if err != nil {
		t.Fatal(err)
	}
	var f struct {
		NewTheme map[string]any            `json:"newTheme"`
		Defaults map[string]map[string]any `json:"defaults"`
	}
	if err := json.Unmarshal(b, &f); err != nil {
		t.Fatal(err)
	}
	if len(f.Defaults) != 13 {
		t.Fatalf("%d predefiniti invece di 13", len(f.Defaults))
	}
	compile := func(name string, th map[string]any) {
		t.Helper()
		data, _ := json.Marshal(th)
		if _, _, err := CompileJSON(data); err != nil {
			t.Errorf("%s: %v", name, err)
		}
	}
	withEls := func(els ...any) map[string]any {
		th := map[string]any{}
		for k, v := range f.NewTheme {
			th[k] = v
		}
		th["screens"] = map[string]any{"home": map[string]any{"variants": []any{map[string]any{"when": "always", "elements": els}}}}
		return th
	}
	compile("tema nuovo", f.NewTheme)
	types := make([]string, 0, len(f.Defaults))
	for k := range f.Defaults {
		types = append(types, k)
	}
	sort.Strings(types)
	for i, ty := range types {
		el := map[string]any{"id": "e1"}
		for k, v := range f.Defaults[ty] {
			el[k] = v
		}
		compile(ty, withEls(el))
		compile(ty+" in un gruppo", withEls(map[string]any{"type": "group", "id": "g1", "x": 3, "y": 4, "children": []any{el}}))
		_ = i
	}
	// i casi che l'editor deve evitare: il servizio li rifiuta
	for name, th := range map[string]map[string]any{
		"gruppo vuoto":    withEls(map[string]any{"type": "group", "id": "g1", "x": 0, "y": 0, "children": []any{}}),
		"senza schermate": func() map[string]any { x := withEls(); x["screens"] = map[string]any{}; return x }(),
	} {
		data, _ := json.Marshal(th)
		if _, _, err := CompileJSON(data); err == nil {
			t.Errorf("%s: dovrebbe essere rifiutato", name)
		}
	}
}
