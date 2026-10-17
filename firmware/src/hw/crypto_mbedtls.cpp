// Primitive di core/crypto.h sulla scheda: mbedTLS 2.28 dell'IDF 4.4 (core Arduino 2.0.17).
// SHA-256 e AES usano gli acceleratori dell'ESP32-S3 (MBEDTLS_SHA256_ALT, MBEDTLS_AES_ALT, MBEDTLS_GCM_ALT
// in mbedtls/esp_config.h). X25519 passa da mbedtls_ecp_mul su MBEDTLS_ECP_DP_CURVE25519 (abilitata nella
// sdkconfig del core). HKDF non è compilato nell'IDF: lo fa core/crypto.cpp sopra HMAC.
// Le uscite sono provate all'avvio con cry::selfTest (stessi vettori di test_crypto).
#ifdef ARDUINO
#include <esp_random.h>
#include <mbedtls/ecp.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include <new>

#include "core/crypto.h"

namespace cal {
namespace cry {

void sha256(const uint8_t *d, size_t n, uint8_t out[kHash]) { mbedtls_sha256_ret(d, n, out, 0); }

static_assert(sizeof(mbedtls_sha256_context) <= 256, "Sha256Stream: stato troppo piccolo");
Sha256Stream::Sha256Stream() {
  mbedtls_sha256_context *c = new (st_) mbedtls_sha256_context;
  mbedtls_sha256_init(c);
  mbedtls_sha256_starts_ret(c, 0);
}
Sha256Stream::~Sha256Stream() { mbedtls_sha256_free(reinterpret_cast<mbedtls_sha256_context *>(st_)); }
void Sha256Stream::update(const uint8_t *d, size_t n) { mbedtls_sha256_update_ret(reinterpret_cast<mbedtls_sha256_context *>(st_), d, n); }
void Sha256Stream::finish(uint8_t out[kHash]) { mbedtls_sha256_finish_ret(reinterpret_cast<mbedtls_sha256_context *>(st_), out); }

// mbedtls_md_hmac alloca i contesti sullo heap a ogni chiamata: con l'heap esaurito torna un errore.
bool hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *d, size_t n, uint8_t out[kHash]) {
  const int rc = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keyLen, d, n, out);
  if (rc) wipe(out, kHash);
  return rc == 0;
}

namespace {
int rng(void *, unsigned char *out, size_t n) {
  esp_fill_random(out, n);
  return 0;
}

bool scalarmult(uint8_t out[kKey], const uint8_t sk[kKey], const uint8_t pk[kKey]) {
  uint8_t s[kKey], u[kKey];
  memcpy(s, sk, kKey);
  s[0] &= 248;  // RFC 7748: clamp (mbedtls_ecp_mul rifiuta un segreto non «clampato»)
  s[31] &= 127;
  s[31] |= 64;
  memcpy(u, pk, kKey);
  u[31] &= 127;  // RFC 7748: il bit alto di u si ignora
  mbedtls_ecp_group grp;
  mbedtls_ecp_point P, R;
  mbedtls_mpi m;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&P);
  mbedtls_ecp_point_init(&R);
  mbedtls_mpi_init(&m);
  int rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
  if (!rc) rc = mbedtls_mpi_read_binary_le(&m, s, kKey);
  if (!rc) rc = mbedtls_mpi_read_binary_le(&P.X, u, kKey);
  if (!rc) rc = mbedtls_mpi_lset(&P.Z, 1);
  // Rifiuta da sé i punti di ordine basso (ecp_check_bad_points_mx): niente segreto tutto zero.
  if (!rc) rc = mbedtls_ecp_mul(&grp, &R, &m, &P, rng, nullptr);
  if (!rc) rc = mbedtls_mpi_write_binary_le(&R.X, out, kKey);
  mbedtls_mpi_free(&m);
  mbedtls_ecp_point_free(&R);
  mbedtls_ecp_point_free(&P);
  mbedtls_ecp_group_free(&grp);
  wipe(s, sizeof s);
  if (rc) { memset(out, 0, kKey); return false; }
  uint8_t any = 0;
  for (size_t k = 0; k < kKey; k++) any |= out[k];
  return any != 0;
}
}  // namespace

bool x25519(uint8_t out[kKey], const uint8_t sk[kKey], const uint8_t pk[kKey]) { return scalarmult(out, sk, pk); }

void x25519Base(uint8_t pk[kKey], const uint8_t sk[kKey]) {
  static const uint8_t k9[kKey] = {9};
  scalarmult(pk, sk, k9);
}

bool gcmEncrypt(const uint8_t key[kKey], const uint8_t iv[kGcmIv], const uint8_t *aad, size_t aadLen, const uint8_t *in,
                size_t n, uint8_t *out, uint8_t tag[kGcmTag]) {
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (!rc) rc = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, iv, kGcmIv, aad, aadLen, in, out, kGcmTag, tag);
  mbedtls_gcm_free(&g);
  return rc == 0;
}

bool gcmDecrypt(const uint8_t key[kKey], const uint8_t iv[kGcmIv], const uint8_t *aad, size_t aadLen, const uint8_t *in,
                size_t n, uint8_t *out, const uint8_t tag[kGcmTag]) {
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (!rc) rc = mbedtls_gcm_auth_decrypt(&g, n, iv, kGcmIv, aad, aadLen, tag, kGcmTag, in, out);
  mbedtls_gcm_free(&g);
  if (rc) wipe(out, n);
  return rc == 0;
}

}  // namespace cry
}  // namespace cal
#endif  // ARDUINO
