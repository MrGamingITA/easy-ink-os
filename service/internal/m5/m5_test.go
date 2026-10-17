package m5

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

// Vettori condivisi con il firmware (generati con il crypto di Node/OpenSSL): ogni byte deve tornare.

type vectors struct {
	Standard struct {
		SHA256 []struct{ Name, Data, Hash string }     `json:"sha256"`
		HMAC   []struct{ Name, Key, Data, Mac string } `json:"hmacSha256"`
		HKDF   []struct {
			Name, Ikm, Salt, Info, Okm string
			L                          int
		} `json:"hkdfSha256"`
		X25519     []struct{ Name, Scalar, U, Out string }            `json:"x25519"`
		X25519Base []struct{ Name, Scalar, Pk string }                `json:"x25519Base"`
		Iterated   struct{ After1, After1000 string }                 `json:"x25519Iterated"`
		LowOrder   []struct{ Name, U string }                         `json:"x25519LowOrder"`
		GCM        []struct{ Name, Key, Iv, Aad, Pt, Ct, Tag string } `json:"aes256Gcm"`
	} `json:"standard"`
	Flow struct {
		Board string `json:"board"`
		Pair  struct {
			ID, SkS, PkS, Commit, SkD, PkD, Z, Tinput, T, K, SasBytes, Sas string
			SasInt                                                         uint32
			PairConfirmMac, PairDoneMac                                    string
			Messages                                                       []map[string]any
			WrongCode                                                      map[string]any
		} `json:"pair"`
		Auth struct {
			Ns, Nd, AuthInput, AuthMac, AuthOkInput, AuthOkMac, SK string
			Messages                                               []map[string]any
		} `json:"auth"`
		PermRequest []struct {
			Fields struct {
				ID, Session, Name, Tool, Arg string
				At                           int64
			}
			MacInput, Mac string
			Message       map[string]any
		} `json:"permRequest"`
		PermReply []struct {
			Fields        struct{ ID, Decision, Nonce string }
			MacInput, Mac string
			Message       map[string]any
		} `json:"permReply"`
		PermClose struct {
			Fields        struct{ ID, Reason string }
			MacInput, Mac string
			Message       map[string]any
		} `json:"permClose"`
		Negative map[string]struct {
			Expect, Why string
			Message     map[string]any
		} `json:"negative"`
		Unpair struct {
			MacInput, Mac string
			Message       map[string]any
		} `json:"unpair"`
		Vault struct {
			EfuseKey, Salt, KeInput, KE string
			Records                     []struct{ Name, Plaintext, Iv, Record string }
		} `json:"vault"`
	} `json:"flow"`
}

func load(t *testing.T) vectors {
	t.Helper()
	b, err := os.ReadFile(filepath.Join("..", "..", "..", "protocol", "vectors", "crypto-vectors.json"))
	if err != nil {
		t.Fatal(err)
	}
	var v vectors
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	return v
}

func hx(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("esadecimale %q: %v", s, err)
	}
	return b
}

func same(t *testing.T, what string, got []byte, want string) {
	t.Helper()
	if Hex(got) != want {
		t.Errorf("%s:\n  ottenuto %s\n  atteso   %s", what, Hex(got), want)
	}
}

func TestStandardVectors(t *testing.T) {
	v := load(t)
	if len(v.Standard.SHA256) == 0 || len(v.Standard.HMAC) != 7 || len(v.Standard.HKDF) != 3 || len(v.Standard.X25519) != 4 ||
		len(v.Standard.X25519Base) != 2 || len(v.Standard.LowOrder) != 3 || len(v.Standard.GCM) != 3 {
		t.Fatal("vettori standard incompleti: il file è cambiato?")
	}
	for _, c := range v.Standard.SHA256 {
		h := sha256.Sum256(hx(t, c.Data))
		same(t, "sha256 "+c.Name, h[:], c.Hash)
	}
	for _, c := range v.Standard.HMAC {
		m := hmac.New(sha256.New, hx(t, c.Key))
		m.Write(hx(t, c.Data))
		full := m.Sum(nil)
		same(t, "hmac "+c.Name, full[:len(c.Mac)/2], c.Mac) // #5 è troncato a 128 bit
	}
	// Mac16 è il troncamento a 16 byte dello stesso HMAC.
	c5 := v.Standard.HMAC[4]
	same(t, "Mac16 RFC 4231 #5", Mac16(hx(t, c5.Key), hx(t, c5.Data)), c5.Mac)
	for _, c := range v.Standard.HKDF {
		same(t, "hkdf "+c.Name, HKDF(hx(t, c.Ikm), hx(t, c.Salt), string(hx(t, c.Info)), c.L), c.Okm)
	}
	for _, c := range v.Standard.X25519 {
		out, err := X25519(hx(t, c.Scalar), hx(t, c.U))
		if err != nil {
			t.Fatalf("x25519 %s: %v", c.Name, err)
		}
		same(t, "x25519 "+c.Name, out, c.Out)
	}
	for _, c := range v.Standard.X25519Base {
		pk, err := X25519Base(hx(t, c.Scalar))
		if err != nil {
			t.Fatal(err)
		}
		same(t, "x25519 base "+c.Name, pk, c.Pk)
	}
	// RFC 7748 5.2: k = u = 9, poi k, u = X25519(k, u), k.
	k := make([]byte, 32)
	k[0] = 9
	u := append([]byte(nil), k...)
	for i := 1; i <= 1000; i++ {
		out, err := X25519(k, u)
		if err != nil {
			t.Fatalf("iterazione %d: %v", i, err)
		}
		u, k = k, out
		if i == 1 {
			same(t, "x25519 1 iterazione", k, v.Standard.Iterated.After1)
		}
	}
	same(t, "x25519 1000 iterazioni", k, v.Standard.Iterated.After1000)
	sk := hx(t, v.Standard.X25519[0].Scalar)
	for _, c := range v.Standard.LowOrder {
		if out, err := X25519(sk, hx(t, c.U)); err == nil {
			t.Errorf("x25519 ordine basso %s accettato: %x", c.Name, out)
		}
	}
	for _, c := range v.Standard.GCM {
		block, _ := aes.NewCipher(hx(t, c.Key))
		g, _ := cipher.NewGCM(block)
		sealed := g.Seal(nil, hx(t, c.Iv), hx(t, c.Pt), hx(t, c.Aad))
		same(t, "gcm ct "+c.Name, sealed[:len(sealed)-16], c.Ct)
		same(t, "gcm tag "+c.Name, sealed[len(sealed)-16:], c.Tag)
		bad := append([]byte(nil), sealed...)
		bad[len(bad)-1] ^= 1
		if _, err := g.Open(nil, hx(t, c.Iv), bad, hx(t, c.Aad)); err == nil {
			t.Errorf("gcm %s: tag falso accettato", c.Name)
		}
	}
}

func TestFlowVectors(t *testing.T) {
	v := load(t)
	f := v.Flow
	p := f.Pair
	board := f.Board

	pkS, err := X25519Base(hx(t, p.SkS))
	if err != nil {
		t.Fatal(err)
	}
	same(t, "pkS", pkS, p.PkS)
	pkD, _ := X25519Base(hx(t, p.SkD))
	same(t, "pkD", pkD, p.PkD)
	commit := Commit(pkS)
	same(t, "commit", commit, p.Commit)
	Z, err := X25519(hx(t, p.SkS), pkD)
	if err != nil {
		t.Fatal(err)
	}
	same(t, "Z (servizio)", Z, p.Z)
	Zd, _ := X25519(hx(t, p.SkD), pkS)
	same(t, "Z (display)", Zd, p.Z)
	tin := append(append(append(append([]byte("calamaio-pair-v1"), board...), commit...), pkD...), pkS...)
	same(t, "Tinput", tin, p.Tinput)
	T := Transcript(board, commit, pkD, pkS)
	same(t, "T", T, p.T)
	K := PairKey(Z, T)
	same(t, "K", K, p.K)
	same(t, "sasBytes", HKDF(Z, T, "calamaio-sas", 32), p.SasBytes)
	if got := SASInt(Z, T); got != p.SasInt {
		t.Errorf("sasInt = %d, atteso %d", got, p.SasInt)
	}
	if got := SAS(Z, T); got != p.Sas || got != "175397" {
		t.Errorf("SAS = %q, atteso %q", got, p.Sas)
	}
	same(t, "pair-confirm mac", PairConfirmMac(K, T), p.PairConfirmMac)
	same(t, "pair-done mac", PairDoneMac(K, T), p.PairDoneMac)
	wantMsgs := []map[string]any{
		{"t": "pair-begin", "v": 1.0, "id": p.ID, "commit": Hex(commit)},
		{"t": "pair-key", "v": 1.0, "id": p.ID, "pkD": Hex(pkD)},
		{"t": "pair-reveal", "v": 1.0, "id": p.ID, "pkS": Hex(pkS)},
		{"t": "pair-confirm", "v": 1.0, "id": p.ID, "mac": Hex(PairConfirmMac(K, T))},
		{"t": "pair-done", "v": 1.0, "id": p.ID, "mac": Hex(PairDoneMac(K, T))},
	}
	sameMsgs(t, "pair", p.Messages, wantMsgs)
	sameMsgs(t, "pair-abort", []map[string]any{p.WrongCode}, []map[string]any{{"t": "pair-abort", "v": 1.0, "id": p.ID, "reason": "codice"}})

	a := f.Auth
	ns, nd := hx(t, a.Ns), hx(t, a.Nd)
	same(t, "authInput", append(append(append([]byte("auth-d"), board...), ns...), nd...), a.AuthInput)
	same(t, "auth mac", AuthMac(K, board, ns, nd), a.AuthMac)
	same(t, "authOkInput", append(append(append([]byte("auth-s"), board...), nd...), ns...), a.AuthOkInput)
	same(t, "auth-ok mac", AuthOkMac(K, board, ns, nd), a.AuthOkMac)
	SK := SessionKey(K, ns, nd)
	same(t, "SK", SK, a.SK)
	sameMsgs(t, "auth", a.Messages, []map[string]any{
		{"t": "auth-challenge", "v": 1.0, "ns": a.Ns},
		{"t": "auth", "v": 1.0, "nd": a.Nd, "mac": a.AuthMac},
		{"t": "auth-ok", "v": 1.0, "mac": a.AuthOkMac},
	})

	if len(f.PermRequest) != 2 || len(f.PermReply) != 2 {
		t.Fatal("vettori dei permessi incompleti")
	}
	for i, r := range f.PermRequest {
		x := r.Fields
		same(t, "req input", ReqInput(x.ID, x.Session, x.Name, x.Tool, x.Arg, x.At), r.MacInput)
		mac := ReqMac(SK, x.ID, x.Session, x.Name, x.Tool, x.Arg, x.At)
		same(t, "req mac", mac, r.Mac)
		sameMsgs(t, "perm-request", []map[string]any{r.Message}, []map[string]any{{"t": "perm-request", "v": 1.0,
			"id": x.ID, "session": x.Session, "name": x.Name, "tool": x.Tool, "arg": x.Arg, "at": float64(x.At), "mac": Hex(mac)}})
		if i == 1 && x.Arg != "src/città/perché.ts" {
			t.Errorf("il secondo vettore deve avere un arg con lettere accentate: %q", x.Arg)
		}
	}
	for _, r := range f.PermReply {
		x := r.Fields
		same(t, "rep input", joined("rep", x.ID, x.Decision, x.Nonce), r.MacInput)
		mac := RepMac(SK, x.ID, x.Decision, x.Nonce)
		same(t, "rep mac", mac, r.Mac)
		sameMsgs(t, "perm-reply", []map[string]any{r.Message}, []map[string]any{{"t": "perm-reply", "v": 1.0,
			"id": x.ID, "decision": x.Decision, "nonce": x.Nonce, "mac": Hex(mac)}})
	}
	c := f.PermClose
	same(t, "close input", joined("close", c.Fields.ID, c.Fields.Reason), c.MacInput)
	same(t, "close mac", CloseMac(SK, c.Fields.ID, c.Fields.Reason), c.Mac)
	same(t, "unpair input", joined("unpair", board), f.Unpair.MacInput)
	same(t, "unpair mac", UnpairMac(SK, board), f.Unpair.Mac)
	sameMsgs(t, "unpair", []map[string]any{f.Unpair.Message}, []map[string]any{{"t": "unpair", "v": 1.0, "mac": f.Unpair.Mac}})

	// Casi negativi: chi li riceve (il display) deve rifiutarli. Qui la verifica che farebbe.
	if len(f.Negative) != 4 {
		t.Fatalf("casi negativi: %d, attesi 4", len(f.Negative))
	}
	for name, n := range f.Negative {
		if CheckDisplaySide(SK, n.Message) == nil {
			t.Errorf("caso negativo %s (%s) accettato: atteso %s", name, n.Why, n.Expect)
		}
	}
	// …e gli stessi messaggi buoni passano.
	for _, r := range f.PermRequest {
		if err := CheckDisplaySide(SK, r.Message); err != nil {
			t.Errorf("perm-request buona rifiutata: %v", err)
		}
	}
	if err := CheckDisplaySide(SK, c.Message); err != nil {
		t.Errorf("perm-close buona rifiutata: %v", err)
	}

	// Cassaforte del firmware (§4): KE e record cifrati. Il servizio non li usa, ma li rifà.
	vv := f.Vault
	keIn := append([]byte("calamaio-nvs"), hx(t, vv.Salt)...)
	same(t, "keInput", keIn, vv.KeInput)
	m := hmac.New(sha256.New, hx(t, vv.EfuseKey))
	m.Write(keIn)
	KE := m.Sum(nil)
	same(t, "KE", KE, vv.KE)
	for _, r := range vv.Records {
		block, _ := aes.NewCipher(KE)
		g, _ := cipher.NewGCM(block)
		sealed := g.Seal(nil, hx(t, r.Iv), hx(t, r.Plaintext), []byte(r.Name))
		ct, tag := sealed[:len(sealed)-16], sealed[len(sealed)-16:]
		rec := append(append(append([]byte{1}, hx(t, r.Iv)...), tag...), ct...)
		same(t, "record "+r.Name, rec, r.Record)
	}
	if !bytes.Equal(hx(t, vv.Records[1].Plaintext), K) {
		t.Error("il record pair-key deve contenere K del flusso")
	}
}

func sameMsgs(t *testing.T, what string, got, want []map[string]any) {
	t.Helper()
	if len(got) != len(want) {
		t.Fatalf("%s: %d messaggi, attesi %d", what, len(got), len(want))
	}
	for i := range got {
		g, _ := json.Marshal(got[i])
		w, _ := json.Marshal(want[i])
		if !bytes.Equal(g, w) {
			t.Errorf("%s[%d]:\n  nel file %s\n  calcolato %s", what, i, g, w)
		}
	}
}

func TestTruncUTF8(t *testing.T) {
	cases := []struct {
		in   string
		max  int
		want string
	}{
		{"abc", 3, "abc"},
		{"abcd", 3, "abc"},
		{"perché", 5, "perch"}, // é = 2 byte: non si spezza
		{"perché", 6, "perch"}, // il sesto byte è a metà di é
		{"perché", 7, "perché"},
		{"€€", 4, "€"},
		{"", 0, ""},
	}
	for _, c := range cases {
		if got := TruncUTF8(c.in, c.max); got != c.want {
			t.Errorf("TruncUTF8(%q, %d) = %q, atteso %q", c.in, c.max, got, c.want)
		}
	}
}

func TestParseHexStrict(t *testing.T) {
	if _, ok := ParseHex("00ff", 2); !ok {
		t.Error("esadecimale minuscolo rifiutato")
	}
	for _, s := range []string{"00FF", "00f", "00fff0", "0g00", " 0ff"} {
		if _, ok := ParseHex(s, 2); ok {
			t.Errorf("ParseHex(%q) accettato", s)
		}
	}
}
