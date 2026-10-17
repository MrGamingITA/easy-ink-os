// Primitive crittografiche di M5 (docs/M5-PERMESSI.md, «Primitive») e formule del protocollo.
//
// Due implementazioni delle primitive, stessa interfaccia, stessi vettori di prova:
//   - sul dispositivo (ARDUINO): mbedTLS 2.28 dell'IDF 4.4, con SHA e AES in hardware sull'ESP32-S3
//     (hw/crypto_mbedtls.cpp). La sdkconfig del core ha MBEDTLS_ECP_DP_CURVE25519_ENABLED; HKDF non è
//     compilato (manca MBEDTLS_HKDF_C), quindi HKDF è qui sopra HMAC, uguale nei due casi;
//   - in native (test): codice portabile in core/crypto_soft.cpp (SHA-256 e AES da FIPS 180-4 e 197,
//     GCM da SP 800-38D, X25519 da TweetNaCl, pubblico dominio).
// I vettori sono in protocol/vectors/crypto-vectors.json (generati con il crypto di Node, cioè OpenSSL:
// un'implementazione indipendente) e li usa anche il servizio Go. selfTest() ne rifà una parte all'avvio
// della scheda, così anche il ramo mbedTLS è provato con gli stessi numeri.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace cry {

const size_t kHash = 32;      // SHA-256
const size_t kKey = 32;       // X25519, K, SK, KE
const size_t kMac = 16;       // HMAC-SHA256 troncato
const size_t kNonce = 16;     // ns, nd, nonce di perm-reply
const size_t kGcmIv = 12;
const size_t kGcmTag = 16;

// ---------- primitive (crypto_soft.cpp in native, hw/crypto_mbedtls.cpp sulla scheda) ----------
void sha256(const uint8_t *d, size_t n, uint8_t out[kHash]);
// false se la libreria fallisce (sulla scheda: memoria per i contesti di mbedTLS); out azzerato, da non usare.
bool hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *d, size_t n, uint8_t out[kHash]);
// X25519 (RFC 7748): il segreto viene «clampato» e il bit alto di pk ignorato, come chiede la RFC.
// false se il risultato è tutto zero (punto di ordine basso) o la libreria rifiuta pk.
bool x25519(uint8_t out[kKey], const uint8_t sk[kKey], const uint8_t pk[kKey]);
void x25519Base(uint8_t pk[kKey], const uint8_t sk[kKey]);
// AES-256-GCM con IV di 12 byte e tag di 16. decrypt: false se il tag non torna (out non va usato).
bool gcmEncrypt(const uint8_t key[kKey], const uint8_t iv[kGcmIv], const uint8_t *aad, size_t aadLen,
                const uint8_t *in, size_t n, uint8_t *out, uint8_t tag[kGcmTag]);
bool gcmDecrypt(const uint8_t key[kKey], const uint8_t iv[kGcmIv], const uint8_t *aad, size_t aadLen,
                const uint8_t *in, size_t n, uint8_t *out, const uint8_t tag[kGcmTag]);

// SHA-256 a pezzi (OTA: l'immagine arriva in streaming e non sta in RAM). Stessa implementazione di sha256():
// mbedTLS con l'acceleratore sulla scheda, FIPS 180-4 portabile in native. Lo stato sta qui dentro (niente heap).
class Sha256Stream {
 public:
  Sha256Stream();
  ~Sha256Stream();
  void update(const uint8_t *d, size_t n);
  void finish(uint8_t out[kHash]);  // dopo finish() si ricomincia solo con un oggetto nuovo

 private:
  Sha256Stream(const Sha256Stream &) = delete;
  Sha256Stream &operator=(const Sha256Stream &) = delete;
  alignas(8) uint8_t st_[256];
};

// ---------- costruiti sopra (crypto.cpp, uguali ovunque) ----------
// HKDF-SHA256 (RFC 5869). outLen al massimo 255 × 32. false anche se un HMAC fallisce (out da non usare).
bool hkdf(const uint8_t *salt, size_t saltLen, const uint8_t *ikm, size_t ikmLen, const uint8_t *info, size_t infoLen,
          uint8_t *out, size_t outLen);
// Confronto in tempo costante.
bool ctEqual(const uint8_t *a, const uint8_t *b, size_t n);
// Azzera senza che il compilatore lo tolga.
void wipe(void *p, size_t n);
// Esadecimale minuscolo. toHex scrive 2n caratteri + terminatore.
void toHex(const uint8_t *d, size_t n, char *out);
// Esattamente 2n cifre minuscole, niente altro: false altrimenti.
bool fromHex(const char *s, uint8_t *out, size_t n);

// Messaggio a capienza fissa per le concatenazioni del protocollo (il più lungo, perm-request, sta sotto
// i 300 byte). ok() falso se qualcosa non c'è stato: i chiamanti lo trattano come errore.
class Msg {
 public:
  Msg() : n_(0), ok_(true) {}
  Msg &raw(const uint8_t *d, size_t n);
  Msg &str(const char *s);
  Msg &sep();                 // 0x1F
  Msg &num(int64_t v);        // decimale ASCII
  const uint8_t *data() const { return b_; }
  size_t size() const { return n_; }
  bool ok() const { return ok_; }

 private:
  uint8_t b_[512];
  size_t n_;
  bool ok_;
};

// HMAC-SHA256 troncato ai primi 16 byte. false se l'HMAC fallisce.
bool mac16(const uint8_t key[kKey], const Msg &m, uint8_t out[kMac]);

// Autoverifica con vettori fissi (RFC 7748, 4231, 5869, AES-GCM e un passo del flusso di abbinamento).
// false e il nome del primo vettore che non torna in why.
bool selfTest(char *why, size_t cap);

}  // namespace cry

// ---------- formule del protocollo (docs/M5-PERMESSI.md) ----------
// Tutte le stringhe binarie del JSON sono esadecimali minuscole. Le formule con «‖» concatenano byte grezzi
// (board in ASCII, chiavi e nonce come byte); quelle dei permessi e di unpair usano i testi del JSON
// separati da 0x1F, etichetta compresa. Gli esempi completi sono in protocol/vectors/crypto-vectors.json.
namespace m5 {

const int kSasDigits = 6;
const int kReqIdHex = 32;    // id di perm-request: 16 byte
const int kArgMax = 96;      // arg di perm-request, byte UTF-8
const int kNameMax = 31;     // name di perm-request, byte UTF-8 (nel mac)
const int kToolMax = 63;     // tool di perm-request, byte UTF-8
// name, tool e arg: il servizio li tronca (UTF-8 sicuro) prima di calcolare il mac; il display rifiuta quelli più lunghi.

// commit = SHA256(pkS)
void commit(const uint8_t pkS[32], uint8_t out[32]);
// T = SHA256("calamaio-pair-v1" ‖ board ‖ commit ‖ pkD ‖ pkS)
void transcript(const char *board, const uint8_t commit[32], const uint8_t pkD[32], const uint8_t pkS[32], uint8_t T[32]);
// K = HKDF(Z, salt = T, info = "calamaio-key"), 32 byte
void pairKey(const uint8_t Z[32], const uint8_t T[32], uint8_t K[32]);
// SAS = HKDF(Z, salt = T, info = "calamaio-sas"): primi 4 byte big-endian modulo 1 000 000
uint32_t sas(const uint8_t Z[32], const uint8_t T[32]);
void sasText(uint32_t code, char out[7]);  // 6 cifre con gli zeri iniziali
// pair-confirm: HMAC(K, "pair-s" ‖ T) · pair-done: HMAC(K, "pair-d" ‖ T)
void pairConfirmMac(const uint8_t K[32], const uint8_t T[32], uint8_t out[16]);
void pairDoneMac(const uint8_t K[32], const uint8_t T[32], uint8_t out[16]);
// auth: HMAC(K, "auth-d" ‖ board ‖ ns ‖ nd) · auth-ok: HMAC(K, "auth-s" ‖ board ‖ nd ‖ ns)
// authMac, authOkMac, sessionKey: false se il calcolo fallisce (memoria): niente mac sbagliato al servizio.
bool authMac(const uint8_t K[32], const char *board, const uint8_t ns[16], const uint8_t nd[16], uint8_t out[16]);
bool authOkMac(const uint8_t K[32], const char *board, const uint8_t ns[16], const uint8_t nd[16], uint8_t out[16]);
// SK = HKDF(K, salt = ns ‖ nd, info = "calamaio-session")
bool sessionKey(const uint8_t K[32], const uint8_t ns[16], const uint8_t nd[16], uint8_t SK[32]);
// perm-request: HMAC(SK, "req" ␟ id ␟ session ␟ name ␟ tool ␟ arg ␟ at) con at in decimale
bool reqMac(const uint8_t SK[32], const char *id, const char *session, const char *name, const char *tool, const char *arg,
            int64_t at, uint8_t out[16]);
// perm-close: HMAC(SK, "close" ␟ id ␟ reason)
bool closeMac(const uint8_t SK[32], const char *id, const char *reason, uint8_t out[16]);
// perm-reply: HMAC(SK, "rep" ␟ id ␟ decision ␟ nonce) con nonce in esadecimale (32 caratteri)
bool repMac(const uint8_t SK[32], const char *id, const char *decision, const char *nonceHex, uint8_t out[16]);
// unpair: HMAC(SK, "unpair" ␟ board)
bool unpairMac(const uint8_t SK[32], const char *board, uint8_t out[16]);

}  // namespace m5
}  // namespace cal
