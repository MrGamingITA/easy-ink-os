package display

import (
	"encoding/json"
	"testing"
)

// «Risparmio energetico» (save): nessuna regola speciale, da remoto si accende e si spegne.
func TestSaveSetting(t *testing.T) {
	for _, in := range []string{`{"save":true}`, `{"save":false}`} {
		var raw map[string]json.RawMessage
		_ = json.Unmarshal([]byte(in), &raw)
		v, err := ValidateRemote(raw)
		if err != nil || v["save"] == nil {
			t.Fatalf("%s: %v %v", in, v, err)
		}
	}
	for _, in := range []string{`{"save":1}`, `{"save":"true"}`} {
		var raw map[string]json.RawMessage
		_ = json.Unmarshal([]byte(in), &raw)
		if _, err := ValidateRemote(raw); err == nil {
			t.Fatalf("%s accettato", in)
		}
	}
	got, unknown, err := parseReported(map[string]json.RawMessage{"save": json.RawMessage("true"), "futura": json.RawMessage("1")})
	if err != nil || got["save"] != true || len(unknown) != 1 {
		t.Fatalf("settings con save: %v %v %v", got, unknown, err)
	}
}
