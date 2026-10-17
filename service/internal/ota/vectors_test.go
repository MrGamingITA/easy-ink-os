package ota

import (
	"crypto/ed25519"
	"encoding/hex"
	"encoding/json"
	"os"
	"testing"

	"calamaio/internal/m5"
)

// protocol/vectors/ota-vectors.json (generato con il crypto di Node, letto anche dal firmware): Ed25519
// RFC 8032 con i casi negativi, forma canonica e firma del manifesto, mac di ota-key e ota-offer.
func TestOtaVectors(t *testing.T) {
	b, err := os.ReadFile("../../../protocol/vectors/ota-vectors.json")
	if err != nil {
		t.Skip("vettori OTA assenti")
	}
	var v struct {
		Ed25519  []struct{ Name, Seed, Pub, Msg, Sig string } `json:"ed25519"`
		Neg      []struct{ Name, Pub, Msg, Sig string }       `json:"ed25519Negative"`
		Manifest struct {
			Key       struct{ Seed, Pub string } `json:"key"`
			Fields    Manifest                   `json:"fields"`
			Canonical string                     `json:"canonical"`
			Sig       string                     `json:"sig"`
			Negative  []struct {
				Name     string   `json:"name"`
				Manifest Manifest `json:"manifest"`
				Sig      string   `json:"sig"`
				Pub      string   `json:"pub"`
			} `json:"negative"`
		} `json:"manifest"`
		OtaKey struct {
			SK, Pub, MacInput, Mac string
		} `json:"otaKey"`
		OtaOffer struct {
			SK, ID, MacInput, Mac string
			Message               struct {
				Manifest Manifest `json:"manifest"`
				Sig      string   `json:"sig"`
				Mac      string   `json:"mac"`
			} `json:"message"`
		} `json:"otaOffer"`
	}
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	H := func(s string) []byte {
		x, err := hex.DecodeString(s)
		if err != nil {
			t.Fatal(err)
		}
		return x
	}
	for _, c := range v.Ed25519 {
		priv := ed25519.NewKeyFromSeed(H(c.Seed))
		if hex.EncodeToString(priv.Public().(ed25519.PublicKey)) != c.Pub || hex.EncodeToString(ed25519.Sign(priv, H(c.Msg))) != c.Sig {
			t.Errorf("%s: diverso", c.Name)
		}
	}
	for _, c := range v.Neg {
		if len(H(c.Pub)) == 32 && len(H(c.Sig)) == 64 && ed25519.Verify(H(c.Pub), H(c.Msg), H(c.Sig)) {
			t.Errorf("negativo accettato: %s", c.Name)
		}
	}
	mk := v.Manifest
	priv := ed25519.NewKeyFromSeed(H(mk.Key.Seed))
	pub := priv.Public().(ed25519.PublicKey)
	if hex.EncodeToString(pub) != mk.Key.Pub {
		t.Fatal("chiave del manifesto")
	}
	if hex.EncodeToString(mk.Fields.Canonical()) != mk.Canonical {
		t.Fatalf("forma canonica diversa:\nGo   %x\nNode %s", mk.Fields.Canonical(), mk.Canonical)
	}
	if sig, _ := mk.Fields.Sign(priv); sig != mk.Sig || !mk.Fields.Verify(pub, mk.Sig) {
		t.Fatalf("firma del manifesto: %s", sig)
	}
	for _, n := range mk.Negative {
		p := pub
		if n.Pub != "" {
			p = H(n.Pub)
		}
		if n.Manifest.Verify(p, n.Sig) {
			t.Errorf("manifesto negativo accettato: %s", n.Name)
		}
	}
	k := v.OtaKey
	if hex.EncodeToString(m5.OtaKeyMac(H(k.SK), k.Pub)) != k.Mac || hex.EncodeToString([]byte("ota-key\x1f"+k.Pub)) != k.MacInput {
		t.Error("mac di ota-key")
	}
	o := v.OtaOffer
	if hex.EncodeToString(m5.OtaOfferMac(H(o.SK), o.ID, o.Message.Manifest.SHA256)) != o.Mac || o.Message.Mac != o.Mac {
		t.Error("mac di ota-offer")
	}
	if !o.Message.Manifest.Verify(pub, o.Message.Sig) {
		t.Error("firma dell'offerta")
	}
	t.Logf("%d Ed25519, %d negativi, manifesto con %d negativi, mac di ota-key e ota-offer: uguali a Node", len(v.Ed25519), len(v.Neg), len(mk.Negative))
}
