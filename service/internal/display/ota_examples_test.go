package display

import (
	"bytes"
	"crypto/ed25519"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"calamaio/internal/diag"
	"calamaio/internal/m5"
	"calamaio/internal/ota"
)

// Gli esempi OTA di protocol/examples sono veri: chiave di firma dal seme 32 × 0x01, chiave di sessione
// SK = 32 × 0x02 (vettori fissi, gli stessi di internal/ota/ota_test.go).
func TestOtaExamplesAreReal(t *testing.T) {
	seed := bytes.Repeat([]byte{1}, 32)
	sk := bytes.Repeat([]byte{2}, 32)
	pub := ed25519.NewKeyFromSeed(seed).Public().(ed25519.PublicKey)
	read := func(name string, v any) {
		b, err := os.ReadFile(filepath.Join(protocolDir, "examples", name))
		if err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(b, v); err != nil {
			t.Fatal(err)
		}
	}
	var key otaKeyMsg
	read("ota-key.json", &key)
	if key.Pub != m5.Hex(pub) || key.Mac != m5.Hex(m5.OtaKeyMac(sk, key.Pub)) {
		t.Fatalf("ota-key.json: %+v", key)
	}
	var off OtaOfferMsg
	read("ota-offer.json", &off)
	if !off.Manifest.Verify(pub, off.Sig) || off.Mac != m5.Hex(m5.OtaOfferMac(sk, off.ID, off.Manifest.SHA256)) {
		t.Fatalf("ota-offer.json: firma o mac non validi")
	}
	if string(off.Manifest.Canonical()) != "calamaio-3f2a\x1fheltec213\x1f0.7.0\x1f1556448\x1f"+off.Manifest.SHA256+"\x1f1791748800000" {
		t.Fatalf("forma canonica %q", off.Manifest.Canonical())
	}
	var c otaCancelMsg
	read("ota-cancel.json", &c)
	if c.ID != off.ID || c.Mac != m5.Hex(m5.OtaCancelMac(sk, c.ID)) {
		t.Fatalf("ota-cancel.json: %+v", c)
	}
	// i messaggi del display passano dai parser del servizio
	for _, f := range []string{"ota-status.json", "ota-status-rifiutato.json", "ota-status-errore.json"} {
		b, _ := os.ReadFile(filepath.Join(protocolDir, "examples", f))
		if _, ok := parseOtaStatus(b); !ok {
			t.Errorf("%s rifiutato da parseOtaStatus", f)
		}
	}
	for _, f := range []string{"status.json", "status-sconosciuto.json"} {
		b, _ := os.ReadFile(filepath.Join(protocolDir, "examples", f))
		if _, ok := parsePower(b); !ok {
			t.Errorf("%s rifiutato", f)
		}
	}
	b, _ := os.ReadFile(filepath.Join(protocolDir, "examples", "log.json"))
	if _, ok := parseLog(b); !ok {
		t.Error("log.json rifiutato")
	}
	for _, f := range []string{"crash.json", "crash-senza-coredump.json"} {
		b, _ := os.ReadFile(filepath.Join(protocolDir, "examples", f))
		var m diag.CrashMsg
		if err := json.Unmarshal(b, &m); err != nil || m.Validate() != nil {
			t.Errorf("%s: %v %v", f, err, m.Validate())
		}
	}
	_ = ota.SigLen
}

// Gli esempi dei messaggi del display in protocol/vectors/ota-vectors.json (scritti per il firmware)
// rispettano lo schema e passano dai parser del servizio.
func TestOtaVectorExamples(t *testing.T) {
	b, err := os.ReadFile(filepath.Join(protocolDir, "vectors", "ota-vectors.json"))
	if err != nil {
		t.Skip("vettori OTA assenti")
	}
	var v struct {
		OtaKey   struct{ Message json.RawMessage } `json:"otaKey"`
		OtaOffer struct{ Message json.RawMessage } `json:"otaOffer"`
		Examples map[string]json.RawMessage        `json:"examples"`
	}
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	sch := compileSchema(t)
	msgs := []json.RawMessage{v.OtaKey.Message, v.OtaOffer.Message}
	for _, raw := range v.Examples {
		var arr []json.RawMessage
		if json.Unmarshal(raw, &arr) == nil {
			msgs = append(msgs, arr...)
		} else {
			msgs = append(msgs, raw)
		}
	}
	for _, m := range msgs {
		validate(t, sch, "vettore", m)
		var head struct{ T string }
		_ = json.Unmarshal(m, &head)
		ok := true
		switch head.T {
		case "ota-status":
			_, ok = parseOtaStatus(m)
		case "log":
			_, ok = parseLog(m)
		case "crash":
			_, ok = parseCrash(m)
		}
		if !ok {
			t.Errorf("%s rifiutato dal servizio: %s", head.T, m)
		}
	}
	t.Logf("%d messaggi dei vettori controllati", len(msgs))
}
