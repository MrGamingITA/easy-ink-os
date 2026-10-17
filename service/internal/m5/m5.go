// Package m5 contiene le primitive e le formule di docs/M5-PERMESSI.md: abbinamento con X25519 e
// codice a 6 cifre, sfida e risposta a ogni connessione, mac dei messaggi dei permessi.
//
// Stesse formule del firmware (firmware/src/core/crypto.*, namespace m5): i vettori condivisi in
// protocol/vectors/crypto-vectors.json le provano byte per byte (m5_test.go).
package m5

import (
	"crypto/ecdh"
	"crypto/hkdf"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"strconv"
	"unicode/utf8"
)

const (
	KeyLen   = 32 // X25519, K, SK
	MacLen   = 16 // HMAC-SHA256 troncato
	NonceLen = 16 // ns, nd, nonce di perm-reply, id di perm-request
	ReqIDHex = 32 // id di perm-request: 16 byte in esadecimale
	NameMax  = 31 // name di perm-request, byte UTF-8 (nel mac)
	ToolMax  = 63 // tool di perm-request
	ArgMax   = 96 // arg di perm-request
	// SessionMax: session di perm-request (il firmware tiene 39 byte: kIdLen − 1).
	SessionMax = 39
	// PairIDMax: id di pair-begin, 1–32 caratteri.
	PairIDMax = 32
)

const sep = 0x1F

// ---------- primitive ----------

// HKDF-SHA256 (RFC 5869).
func HKDF(ikm, salt []byte, info string, n int) []byte {
	out, err := hkdf.Key(sha256.New, ikm, salt, info, n)
	if err != nil {
		panic(err) // solo per n fuori misura: errore di programmazione
	}
	return out
}

// Mac16 è HMAC-SHA256 di msg con key, troncato ai primi 16 byte.
func Mac16(key, msg []byte) []byte {
	m := hmac.New(sha256.New, key)
	m.Write(msg)
	return m.Sum(nil)[:MacLen]
}

// Equal confronta in tempo costante (lunghezze diverse: false).
func Equal(a, b []byte) bool { return len(a) == len(b) && subtle.ConstantTimeCompare(a, b) == 1 }

// X25519 (RFC 7748): segreto «clampato», bit alto di pk ignorato; un risultato tutto zero
// (punto di ordine basso) è un errore.
func X25519(sk, pk []byte) ([]byte, error) {
	priv, err := ecdh.X25519().NewPrivateKey(sk)
	if err != nil {
		return nil, err
	}
	pub, err := ecdh.X25519().NewPublicKey(pk)
	if err != nil {
		return nil, err
	}
	return priv.ECDH(pub)
}

// X25519Base restituisce la chiave pubblica di sk.
func X25519Base(sk []byte) ([]byte, error) {
	priv, err := ecdh.X25519().NewPrivateKey(sk)
	if err != nil {
		return nil, err
	}
	return priv.PublicKey().Bytes(), nil
}

// NewKeyPair genera una coppia X25519 da r (nil = crypto/rand).
func NewKeyPair(r io.Reader) (sk, pk []byte, err error) {
	if r == nil {
		r = rand.Reader
	}
	sk = make([]byte, KeyLen)
	if _, err = io.ReadFull(r, sk); err != nil {
		return nil, nil, err
	}
	pk, err = X25519Base(sk)
	return sk, pk, err
}

// Random restituisce n byte casuali (crypto/rand: non fallisce sui sistemi supportati).
func Random(n int) []byte {
	b := make([]byte, n)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return b
}

// Hex: esadecimale minuscolo.
func Hex(b []byte) string { return hex.EncodeToString(b) }

// ParseHex accetta esattamente 2n cifre esadecimali minuscole, niente altro (come cry::fromHex).
func ParseHex(s string, n int) ([]byte, bool) {
	if len(s) != 2*n || !IsLowerHex(s) {
		return nil, false
	}
	b, err := hex.DecodeString(s)
	return b, err == nil
}

// IsLowerHex: solo 0-9 e a-f.
func IsLowerHex(s string) bool {
	for i := 0; i < len(s); i++ {
		c := s[i]
		if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f') {
			return false
		}
	}
	return true
}

// TruncUTF8 taglia s a max byte senza spezzare un carattere UTF-8.
func TruncUTF8(s string, max int) string {
	if len(s) <= max {
		return s
	}
	cut := max
	for cut > 0 && !utf8.RuneStart(s[cut]) {
		cut--
	}
	return s[:cut]
}

// ---------- abbinamento (§1) ----------

// Commit = SHA256(pkS).
func Commit(pkS []byte) []byte {
	h := sha256.Sum256(pkS)
	return h[:]
}

// Transcript T = SHA256("calamaio-pair-v1" ‖ board ‖ commit ‖ pkD ‖ pkS).
func Transcript(board string, commit, pkD, pkS []byte) []byte {
	h := sha256.New()
	h.Write([]byte("calamaio-pair-v1"))
	h.Write([]byte(board))
	h.Write(commit)
	h.Write(pkD)
	h.Write(pkS)
	return h.Sum(nil)
}

// PairKey K = HKDF(Z, salt = T, info = "calamaio-key"), 32 byte.
func PairKey(Z, T []byte) []byte { return HKDF(Z, T, "calamaio-key", KeyLen) }

// SASInt: primi 4 byte di HKDF(Z, salt = T, info = "calamaio-sas") come intero big-endian.
func SASInt(Z, T []byte) uint32 { return binary.BigEndian.Uint32(HKDF(Z, T, "calamaio-sas", 4)) }

// SAS: SASInt modulo 1 000 000, 6 cifre con gli zeri iniziali.
func SAS(Z, T []byte) string { return fmt.Sprintf("%06d", SASInt(Z, T)%1000000) }

func cat(label string, parts ...[]byte) []byte {
	b := []byte(label)
	for _, p := range parts {
		b = append(b, p...)
	}
	return b
}

// PairConfirmMac = HMAC(K, "pair-s" ‖ T) · PairDoneMac = HMAC(K, "pair-d" ‖ T).
func PairConfirmMac(K, T []byte) []byte { return Mac16(K, cat("pair-s", T)) }
func PairDoneMac(K, T []byte) []byte    { return Mac16(K, cat("pair-d", T)) }

// ---------- autenticazione (§2) ----------

// AuthMac = HMAC(K, "auth-d" ‖ board ‖ ns ‖ nd).
func AuthMac(K []byte, board string, ns, nd []byte) []byte {
	return Mac16(K, cat("auth-d", []byte(board), ns, nd))
}

// AuthOkMac = HMAC(K, "auth-s" ‖ board ‖ nd ‖ ns).
func AuthOkMac(K []byte, board string, ns, nd []byte) []byte {
	return Mac16(K, cat("auth-s", []byte(board), nd, ns))
}

// SessionKey SK = HKDF(K, salt = ns ‖ nd, info = "calamaio-session").
func SessionKey(K, ns, nd []byte) []byte {
	return HKDF(K, append(append([]byte{}, ns...), nd...), "calamaio-session", KeyLen)
}

// ---------- permessi (§3) ----------

// joined: etichetta e campi come testo, separati da 0x1F, senza 0x1F finale.
func joined(label string, fields ...string) []byte {
	b := []byte(label)
	for _, f := range fields {
		b = append(b, sep)
		b = append(b, f...)
	}
	return b
}

// ReqInput è il messaggio firmato di perm-request: "req" ␟ id ␟ session ␟ name ␟ tool ␟ arg ␟ at.
func ReqInput(id, session, name, tool, arg string, at int64) []byte {
	return joined("req", id, session, name, tool, arg, strconv.FormatInt(at, 10))
}

// ReqMac = HMAC(SK, ReqInput).
func ReqMac(SK []byte, id, session, name, tool, arg string, at int64) []byte {
	return Mac16(SK, ReqInput(id, session, name, tool, arg, at))
}

// CloseMac = HMAC(SK, "close" ␟ id ␟ reason).
func CloseMac(SK []byte, id, reason string) []byte { return Mac16(SK, joined("close", id, reason)) }

// RepMac = HMAC(SK, "rep" ␟ id ␟ decision ␟ nonce), nonce in esadecimale.
func RepMac(SK []byte, id, decision, nonce string) []byte {
	return Mac16(SK, joined("rep", id, decision, nonce))
}

// UnpairMac = HMAC(SK, "unpair" ␟ board).
func UnpairMac(SK []byte, board string) []byte { return Mac16(SK, joined("unpair", board)) }

// ErrFormat: un campo non è nel formato atteso.
var ErrFormat = errors.New("formato non valido")
