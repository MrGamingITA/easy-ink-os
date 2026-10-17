package m5

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"testing"
)

// Vettori delle reti Wi-Fi (docs/PROTOCOLLO.md «Reti Wi-Fi»), calcolati a parte con il crypto di Node
// (OpenSSL: hkdfSync, aes-256-gcm, createHmac) con SK = 32 × 0x02. Il firmware scrive i suoi in
// protocol/vectors/wifi-vectors.json: TestWifiSharedVectors li riproduce quando ci sono.
var wifiAddVectors = []struct {
	id, ssid, password, key, aad, iv, ct string
}{
	{"c7", "CasaDiProva", "password-finta-1234",
		"c35654c88d800588a38aa04ecc5c1c9a9ff9025b3fc7a5ed0119f42bc520fe16",
		"776966692d6164641f63371f43617361446950726f7661",
		"000102030405060708090a0b",
		"68b7f817fdd7299119636f5fc67c2350fdf074c82b77821d805f8713715af748705983"},
	{"c12", "Caffè ☕ 5G", "pässwörd finta!",
		"efbd387c6a7aa691b392862eaf3d482f7ffc59d6e23827d710b6279d9767dd62",
		"776966692d6164641f6331321f43616666c3a820e29895203547",
		"a5a5a5a5a5a5a5a5a5a5a5a5",
		"62a3a9bcf7df0b00b644dd724db6f249cf639657259d061a1f159926bb6c1d84cc"},
}

var wifiForgetVectors = []struct{ id, ssid, mac string }{
	{"c8", "CasaDiProva", "c19dbe8f5f6e77e24dcaa81013676272"},
	{"c13", "Caffè ☕ 5G", "badf4b7fe1d9ba8d84fbd98148e098b1"},
}

func unhex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestWifiVectors(t *testing.T) {
	sk := bytes.Repeat([]byte{2}, 32)
	for _, v := range wifiAddVectors {
		if got := Hex(WifiKey(sk, v.id)); got != v.key {
			t.Errorf("%s: chiave %s, attesa %s", v.id, got, v.key)
		}
		if got := Hex(WifiAddAAD(v.id, v.ssid)); got != v.aad {
			t.Errorf("%s: aad %s, atteso %s", v.id, got, v.aad)
		}
		ct := WifiSeal(sk, v.id, v.ssid, unhex(t, v.iv), []byte(v.password))
		if Hex(ct) != v.ct {
			t.Errorf("%s: ct %s, atteso %s", v.id, Hex(ct), v.ct)
		}
		if len(ct) != len(v.password)+WifiTagLen {
			t.Errorf("%s: ct di %d byte: deve comprendere il tag di 16", v.id, len(ct))
		}
		pw, err := WifiOpen(sk, v.id, v.ssid, unhex(t, v.iv), unhex(t, v.ct))
		if err != nil || string(pw) != v.password {
			t.Errorf("%s: decifrato %q, %v", v.id, pw, err)
		}
	}
	for _, v := range wifiForgetVectors {
		if got := Hex(WifiForgetMac(sk, v.id, v.ssid)); got != v.mac {
			t.Errorf("forget %s: mac %s, atteso %s", v.id, got, v.mac)
		}
	}
}

// Ogni campo legato: id, ssid, iv, cifrato, tag e chiave di sessione. Cambiarne uno qualunque fa fallire
// la decifratura (aad e tag verificati), mai una password sbagliata «quasi giusta».
func TestWifiOpenRejectsTampering(t *testing.T) {
	sk := bytes.Repeat([]byte{2}, 32)
	v := wifiAddVectors[0]
	iv, ct := unhex(t, v.iv), unhex(t, v.ct)
	flip := func(b []byte, i int) []byte { c := append([]byte(nil), b...); c[i] ^= 1; return c }
	cases := map[string]func() ([]byte, error){
		"id diverso":         func() ([]byte, error) { return WifiOpen(sk, "c8", v.ssid, iv, ct) },
		"ssid diverso":       func() ([]byte, error) { return WifiOpen(sk, v.id, "CasaDiProvа", iv, ct) },
		"iv cambiato":        func() ([]byte, error) { return WifiOpen(sk, v.id, v.ssid, flip(iv, 0), ct) },
		"cifrato cambiato":   func() ([]byte, error) { return WifiOpen(sk, v.id, v.ssid, iv, flip(ct, 0)) },
		"tag cambiato":       func() ([]byte, error) { return WifiOpen(sk, v.id, v.ssid, iv, flip(ct, len(ct)-1)) },
		"tag mancante":       func() ([]byte, error) { return WifiOpen(sk, v.id, v.ssid, iv, ct[:len(ct)-WifiTagLen]) },
		"troppo corto":       func() ([]byte, error) { return WifiOpen(sk, v.id, v.ssid, iv, ct[:3]) },
		"iv di 16 byte":      func() ([]byte, error) { return WifiOpen(sk, v.id, v.ssid, append(iv, 0, 0, 0, 0), ct) },
		"altra SK":           func() ([]byte, error) { return WifiOpen(flip(sk, 5), v.id, v.ssid, iv, ct) },
		"campi spostati":     func() ([]byte, error) { return WifiOpen(sk, v.id+"\x1fCasa", "DiProva", iv, ct) },
		"ssid con separator": func() ([]byte, error) { return WifiOpen(sk, "c7\x1fCasaDiProva", "", iv, ct) },
	}
	for name, f := range cases {
		pw, err := f()
		if !errors.Is(err, ErrWifiOpen) || pw != nil {
			t.Errorf("%s: accettato (%q, %v)", name, pw, err)
		}
	}
	// i mac di forget dipendono da id e ssid
	if Equal(WifiForgetMac(sk, "c8", "A"), WifiForgetMac(sk, "c8", "B")) || Equal(WifiForgetMac(sk, "c8", "A"), WifiForgetMac(sk, "c9", "A")) {
		t.Error("mac di wifi-forget non legato a id o ssid")
	}
}

// I vettori del firmware (protocol/vectors/wifi-vectors.json, generati da gen-wifi-vectors.mjs): chiavi,
// aad, cifrati e mac di ogni voce, e i negativi (alterati, altra SK, maiuscole) rifiutati.
func TestWifiSharedVectors(t *testing.T) {
	path := filepath.Join("..", "..", "..", "protocol", "vectors", "wifi-vectors.json")
	b, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("%s non c'è ancora (lo scrive il firmware)", path)
	}
	type msg struct{ T, ID, SSID, IV, CT, Mac string }
	var v struct {
		SK, OtherSK string
		Add         []struct {
			Name, ID, SSID, Password, Key, IV, AAD, CT string
			PasswordBytes                              int
			Message                                    msg
		}
		AddNegative []struct {
			Name, Error string
			Message     msg
		}
		Forget []struct {
			Name, ID, SSID, MacInput, Mac string
			Message                       msg
		}
		ForgetNegative []struct {
			Name, Error string
			Message     msg
		}
	}
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatalf("%s: %v", path, err)
	}
	sk, ok := ParseHex(v.SK, KeyLen)
	if !ok || len(v.Add) == 0 || len(v.Forget) == 0 || len(v.AddNegative) == 0 || len(v.ForgetNegative) == 0 {
		t.Fatalf("%s: forma inattesa", path)
	}
	// SK è quella del flusso di crypto-vectors.json
	var cv struct {
		Flow struct{ Auth struct{ SK string } } `json:"flow"`
	}
	if cb, err := os.ReadFile(filepath.Join(filepath.Dir(path), "crypto-vectors.json")); err == nil && json.Unmarshal(cb, &cv) == nil && cv.Flow.Auth.SK != "" && cv.Flow.Auth.SK != v.SK {
		t.Errorf("SK %s diversa da flow.auth.SK di crypto-vectors.json %s", v.SK, cv.Flow.Auth.SK)
	}
	// open: come la scheda (esadecimale minuscolo e della lunghezza giusta, poi AES-GCM)
	open := func(m msg) ([]byte, error) {
		iv, ok1 := ParseHex(m.IV, WifiIVLen)
		ct, ok2 := ParseHex(m.CT, len(m.CT)/2)
		if !ok1 || !ok2 || len(m.CT)%2 != 0 {
			return nil, ErrWifiOpen
		}
		return WifiOpen(sk, m.ID, m.SSID, iv, ct)
	}
	for _, a := range v.Add {
		if Hex(WifiKey(sk, a.ID)) != a.Key {
			t.Errorf("%s: chiave %s, attesa %s", a.Name, Hex(WifiKey(sk, a.ID)), a.Key)
		}
		if Hex(WifiAddAAD(a.ID, a.SSID)) != a.AAD {
			t.Errorf("%s: aad diverso", a.Name)
		}
		iv, _ := ParseHex(a.IV, WifiIVLen)
		if got := Hex(WifiSeal(sk, a.ID, a.SSID, iv, []byte(a.Password))); got != a.CT || a.Message.CT != a.CT || a.Message.IV != a.IV {
			t.Errorf("%s: ct %s, atteso %s", a.Name, got, a.CT)
		}
		if pw, err := open(a.Message); err != nil || string(pw) != a.Password || len(pw) != a.PasswordBytes {
			t.Errorf("%s: decifrato %q %v", a.Name, pw, err)
		}
	}
	for _, n := range v.AddNegative {
		pw, err := open(n.Message)
		switch n.Error {
		case "decifratura fallita":
			if err == nil {
				t.Errorf("%s: decifrato %q, doveva fallire", n.Name, pw)
			}
		case "password non valida":
			// si decifra (stessa SK), ma il servizio non l'avrebbe mai mandata
			if err != nil {
				t.Errorf("%s: non si decifra: %v", n.Name, err)
			} else if !hasZeroOrBadLen(pw) {
				t.Errorf("%s: password %d byte accettabile per la scheda", n.Name, len(pw))
			}
		default:
			t.Errorf("%s: errore atteso sconosciuto %q", n.Name, n.Error)
		}
	}
	for _, f := range v.Forget {
		if Hex(joined("wifi-forget", f.ID, f.SSID)) != f.MacInput {
			t.Errorf("%s: ingresso del mac diverso", f.Name)
		}
		if got := Hex(WifiForgetMac(sk, f.ID, f.SSID)); got != f.Mac || f.Message.Mac != f.Mac {
			t.Errorf("%s: mac %s, atteso %s", f.Name, got, f.Mac)
		}
	}
	for _, n := range v.ForgetNegative {
		mac, ok := ParseHex(n.Message.Mac, MacLen)
		if ok && Equal(mac, WifiForgetMac(sk, n.Message.ID, n.Message.SSID)) {
			t.Errorf("%s: mac accettato", n.Name)
		}
	}
	t.Logf("%s: %d wifi-add, %d negativi, %d wifi-forget, %d negativi", path, len(v.Add), len(v.AddNegative), len(v.Forget), len(v.ForgetNegative))
}

// hasZeroOrBadLen: la password viola le regole della scheda (8–63 byte, nessun byte zero).
func hasZeroOrBadLen(pw []byte) bool {
	return len(pw) < 8 || len(pw) > 63 || bytes.IndexByte(pw, 0) >= 0
}
