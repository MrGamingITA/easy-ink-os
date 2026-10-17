package m5

import (
	"crypto/aes"
	"crypto/cipher"
	"errors"
)

// Reti Wi-Fi gestite dal PC (docs/PROTOCOLLO.md «Reti Wi-Fi»). La password viaggia solo cifrata:
//
//	chiave = HKDF-SHA256(SK, salt = id, info = "calamaio-wifi")    32 byte; id = byte UTF-8 dell'id del comando
//	aad    = "wifi-add" ␟ id ␟ ssid                                  ␟ = 0x1F, senza 0x1F finale
//	ct     = AES-256-GCM(chiave, iv, password UTF-8, aad) ‖ tag      iv 12 byte casuali, tag 16 byte
//
// wifi-forget è firmato: mac = HMAC(SK, "wifi-forget" ␟ id ␟ ssid) troncato a 16 byte.

const (
	WifiIVLen  = 12 // nonce di AES-GCM
	WifiTagLen = 16 // tag di AES-GCM, in coda a ct
)

// ErrWifiOpen: ct non si decifra (chiave, iv, aad o tag sbagliati).
var ErrWifiOpen = errors.New("password cifrata non valida: chiave, aad o tag sbagliati")

// WifiKey = HKDF(SK, salt = id, info = "calamaio-wifi"), 32 byte.
func WifiKey(SK []byte, id string) []byte { return HKDF(SK, []byte(id), "calamaio-wifi", KeyLen) }

// WifiAddAAD = "wifi-add" ␟ id ␟ ssid.
func WifiAddAAD(id, ssid string) []byte { return joined("wifi-add", id, ssid) }

func wifiGCM(SK []byte, id string) cipher.AEAD {
	key := WifiKey(SK, id)
	defer clear(key)
	block, err := aes.NewCipher(key)
	if err != nil {
		panic(err) // chiave di 32 byte: non succede
	}
	g, err := cipher.NewGCM(block)
	if err != nil {
		panic(err)
	}
	return g
}

// WifiSeal cifra password per wifi-add: restituisce ct ‖ tag. iv deve essere di 12 byte, mai riusato
// con la stessa chiave (il servizio lo prende da crypto/rand).
func WifiSeal(SK []byte, id, ssid string, iv, password []byte) []byte {
	if len(iv) != WifiIVLen {
		panic("m5: iv di wifi-add non di 12 byte")
	}
	return wifiGCM(SK, id).Seal(nil, iv, password, WifiAddAAD(id, ssid))
}

// WifiOpen decifra ct ‖ tag di wifi-add (la parte della scheda; il servizio la usa nei test e nel
// display finto). Qualunque byte cambiato in id, ssid, iv, ct o tag dà ErrWifiOpen.
func WifiOpen(SK []byte, id, ssid string, iv, ct []byte) ([]byte, error) {
	if len(iv) != WifiIVLen || len(ct) < WifiTagLen {
		return nil, ErrWifiOpen
	}
	pw, err := wifiGCM(SK, id).Open(nil, iv, ct, WifiAddAAD(id, ssid))
	if err != nil {
		return nil, ErrWifiOpen
	}
	return pw, nil
}

// WifiForgetMac = HMAC(SK, "wifi-forget" ␟ id ␟ ssid).
func WifiForgetMac(SK []byte, id, ssid string) []byte {
	return Mac16(SK, joined("wifi-forget", id, ssid))
}
