package phones

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"flag"
	"net"
	"os"
	"testing"
)

var updateVectors = flag.Bool("update", false, "riscrive protocol/vectors/app-pairing.json")

const vectorsPath = "../../../protocol/vectors/app-pairing.json"

type pairingVectors struct {
	Note string `json:"note"`
	Cert struct {
		DER    string `json:"der_b64"`
		SHA256 string `json:"sha256"`
	} `json:"cert"`
	Link struct {
		Hosts []string `json:"hosts"`
		Port  int      `json:"port"`
		Code  string   `json:"code"`
		Name  string   `json:"name"`
		URI   string   `json:"uri"`
	} `json:"link"`
}

func TestLink(t *testing.T) {
	got := Link([]string{"192.168.1.20", "10.0.0.7"}, 8767, "ab", "cd", "Mac di Prova & C.")
	want := "calamaio://pair?v=1&h=192.168.1.20,10.0.0.7&p=8767&fp=ab&c=cd&n=Mac%20di%20Prova%20%26%20C."
	if got != want {
		t.Fatalf("\n%s\n%s", got, want)
	}
}

// Vettori condivisi con ios/CalamaioKit (Task 8): stessa impronta e stesso link dalle due parti.
func TestAppPairingVectors(t *testing.T) {
	if *updateVectors {
		c, fp, err := LoadOrCreateCert("", "vettori", t0)
		if err != nil {
			t.Fatal(err)
		}
		var v pairingVectors
		v.Note = "Vettori condivisi fra service/internal/phones e ios/CalamaioKit: impronta del certificato e link del QR. " +
			"Rigenerare con: cd service && go test ./internal/phones -run TestAppPairingVectors -update"
		v.Cert.DER = base64.StdEncoding.EncodeToString(c.Certificate[0])
		v.Cert.SHA256 = fp
		v.Link.Hosts = []string{"192.168.1.20", "10.0.0.7"}
		v.Link.Port = 8767
		v.Link.Code = "00112233445566778899aabbccddeeff"
		v.Link.Name = "Mac di Prova & C."
		v.Link.URI = Link(v.Link.Hosts, v.Link.Port, fp, v.Link.Code, v.Link.Name)
		var b bytes.Buffer
		enc := json.NewEncoder(&b)
		enc.SetEscapeHTML(false) // «&» leggibile nell'URI
		enc.SetIndent("", "  ")
		if err := enc.Encode(v); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(vectorsPath, b.Bytes(), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	b, err := os.ReadFile(vectorsPath)
	if err != nil {
		t.Fatalf("%v (generali con -update)", err)
	}
	var v pairingVectors
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatal(err)
	}
	der, err := base64.StdEncoding.DecodeString(v.Cert.DER)
	if err != nil || Fingerprint(der) != v.Cert.SHA256 {
		t.Fatalf("impronta: %v %s", err, v.Cert.SHA256)
	}
	if got := Link(v.Link.Hosts, v.Link.Port, v.Cert.SHA256, v.Link.Code, v.Link.Name); got != v.Link.URI {
		t.Fatalf("link\n%s\n%s", got, v.Link.URI)
	}
}

func TestLocalIPv4s(t *testing.T) {
	ips := LocalIPv4s()
	if len(ips) > 4 {
		t.Fatalf("troppi: %v", ips)
	}
	for _, s := range ips {
		ip := net.ParseIP(s)
		if ip == nil || ip.To4() == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
			t.Fatalf("indirizzo non ammesso: %q", s)
		}
	}
}
