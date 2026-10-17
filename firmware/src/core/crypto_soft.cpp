// Primitive portabili per i test in native (sulla scheda c'è mbedTLS: hw/crypto_mbedtls.cpp).
// Lente ma semplici, verificate con i vettori standard e con quelli del flusso.
//   SHA-256: FIPS 180-4 · HMAC: RFC 2104 · AES-256: FIPS 197 · GCM: NIST SP 800-38D (GHASH bit per bit)
//   X25519: aritmetica di tweetnacl.c (D. J. Bernstein, B. van Gastel, W. Janssen, T. Lange, P. Schwabe,
//   S. Smetsers, versione 20140427), pubblico dominio; qui senza spostamenti di numeri negativi.
#ifndef ARDUINO
#include <string.h>

#include <new>

#include "crypto.h"

namespace cal {
namespace cry {

// ---------------------------------------------------------------- SHA-256
namespace {

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha {
  uint32_t h[8];
  uint8_t buf[64];
  size_t n;
  uint64_t total;
  Sha() : n(0), total(0) {
    static const uint32_t kH0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(h, kH0, sizeof h);
  }
  void block(const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = k + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
      const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
  }
  void update(const uint8_t *d, size_t len) {
    total += len;
    while (len) {
      const size_t take = 64 - n < len ? 64 - n : len;
      memcpy(buf + n, d, take);
      n += take; d += take; len -= take;
      if (n == 64) { block(buf); n = 0; }
    }
  }
  void finish(uint8_t out[32]) {
    const uint64_t bits = total * 8;
    const uint8_t one = 0x80, zero = 0;
    update(&one, 1);
    while (n != 56) update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = uint8_t(bits >> (56 - 8 * i));
    update(len, 8);
    for (int i = 0; i < 8; i++) { out[4 * i] = uint8_t(h[i] >> 24); out[4 * i + 1] = uint8_t(h[i] >> 16); out[4 * i + 2] = uint8_t(h[i] >> 8); out[4 * i + 3] = uint8_t(h[i]); }
  }
};

}  // namespace

void sha256(const uint8_t *d, size_t n, uint8_t out[kHash]) {
  Sha s;
  s.update(d, n);
  s.finish(out);
}

static_assert(sizeof(Sha) <= 256, "Sha256Stream: stato troppo piccolo");
Sha256Stream::Sha256Stream() { new (st_) Sha(); }
Sha256Stream::~Sha256Stream() { wipe(st_, sizeof st_); }
void Sha256Stream::update(const uint8_t *d, size_t n) { reinterpret_cast<Sha *>(st_)->update(d, n); }
void Sha256Stream::finish(uint8_t out[kHash]) { reinterpret_cast<Sha *>(st_)->finish(out); }

bool hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *d, size_t n, uint8_t out[kHash]) {
  uint8_t k0[64] = {0}, pad[64], inner[kHash];
  if (keyLen > 64) sha256(key, keyLen, k0);
  else if (keyLen) memcpy(k0, key, keyLen);
  Sha a;
  for (int i = 0; i < 64; i++) pad[i] = k0[i] ^ 0x36;
  a.update(pad, 64);
  a.update(d, n);
  a.finish(inner);
  Sha b;
  for (int i = 0; i < 64; i++) pad[i] = k0[i] ^ 0x5c;
  b.update(pad, 64);
  b.update(inner, kHash);
  b.finish(out);
  wipe(k0, sizeof k0);
  wipe(pad, sizeof pad);
  return true;  // niente heap: non fallisce
}

// ---------------------------------------------------------------- AES-256 (solo cifratura) e GCM
namespace {

uint8_t xt(uint8_t x) { return uint8_t((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

const uint8_t *sbox() {
  static uint8_t s[256];
  static bool ready = false;
  if (ready) return s;
  // S-box calcolata (inverso in GF(2^8) e trasformazione affine), così non c'è una tabella da ricopiare.
  uint8_t p = 1, q = 1;
  do {
    p = uint8_t(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
    q ^= q << 1; q ^= q << 2; q ^= q << 4;
    if (q & 0x80) q ^= 0x09;
    const uint8_t x = uint8_t(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
    s[p] = x ^ 0x63;
  } while (p != 1);
  s[0] = 0x63;
  ready = true;
  return s;
}

struct Aes256 {
  uint8_t rk[240];
  explicit Aes256(const uint8_t key[32]) {
    const uint8_t *S = sbox();
    memcpy(rk, key, 32);
    uint8_t rcon = 1;
    for (int i = 8; i < 60; i++) {
      uint8_t t[4];
      memcpy(t, rk + 4 * (i - 1), 4);
      if (i % 8 == 0) {
        const uint8_t u = t[0];
        t[0] = uint8_t(S[t[1]] ^ rcon); t[1] = S[t[2]]; t[2] = S[t[3]]; t[3] = S[u];
        rcon = xt(rcon);
      } else if (i % 8 == 4) {
        for (int k = 0; k < 4; k++) t[k] = S[t[k]];
      }
      for (int k = 0; k < 4; k++) rk[4 * i + k] = rk[4 * (i - 8) + k] ^ t[k];
    }
  }
  ~Aes256() { wipe(rk, sizeof rk); }
  void encrypt(const uint8_t in[16], uint8_t out[16]) const {
    const uint8_t *S = sbox();
    uint8_t s[16];
    for (int k = 0; k < 16; k++) s[k] = in[k] ^ rk[k];
    for (int r = 1; r <= 14; r++) {
      uint8_t t[16];
      for (int k = 0; k < 16; k++) t[k] = S[s[k]];
      // ShiftRows (stato per colonne: byte k = riga k%4, colonna k/4)
      for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) s[4 * c + row] = t[4 * ((c + row) % 4) + row];
      if (r != 14) {
        for (int c = 0; c < 4; c++) {
          uint8_t *a = s + 4 * c;
          const uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3], all = a0 ^ a1 ^ a2 ^ a3;
          a[0] ^= all ^ xt(a0 ^ a1);
          a[1] ^= all ^ xt(a1 ^ a2);
          a[2] ^= all ^ xt(a2 ^ a3);
          a[3] ^= all ^ xt(a3 ^ a0);
        }
      }
      for (int k = 0; k < 16; k++) s[k] ^= rk[16 * r + k];
    }
    memcpy(out, s, 16);
  }
};

// Moltiplicazione in GF(2^128) di SP 800-38D (Algoritmo 1), bit per bit.
void gmul(uint8_t x[16], const uint8_t y[16]) {
  uint8_t z[16] = {0}, v[16];
  memcpy(v, y, 16);
  for (int i = 0; i < 128; i++) {
    if (x[i / 8] & (0x80 >> (i % 8)))
      for (int k = 0; k < 16; k++) z[k] ^= v[k];
    const bool lsb = v[15] & 1;
    for (int k = 15; k > 0; k--) v[k] = uint8_t((v[k] >> 1) | (v[k - 1] << 7));
    v[0] >>= 1;
    if (lsb) v[0] ^= 0xe1;
  }
  memcpy(x, z, 16);
}

void ghashBlocks(uint8_t y[16], const uint8_t H[16], const uint8_t *d, size_t n) {
  while (n) {
    const size_t take = n < 16 ? n : 16;
    for (size_t k = 0; k < take; k++) y[k] ^= d[k];
    gmul(y, H);
    d += take;
    n -= take;
  }
}

void inc32(uint8_t c[16]) {
  for (int k = 15; k >= 12; k--)
    if (++c[k]) break;
}

void gcmCore(const Aes256 &aes, const uint8_t iv[12], const uint8_t *aad, size_t aadLen, const uint8_t *in, size_t n,
             uint8_t *out, bool encrypting, uint8_t tag[16]) {
  uint8_t H[16] = {0}, J0[16], ctr[16], ks[16], y[16] = {0};
  aes.encrypt(H, H);
  memcpy(J0, iv, 12);
  J0[12] = J0[13] = J0[14] = 0;
  J0[15] = 1;
  memcpy(ctr, J0, 16);
  ghashBlocks(y, H, aad, aadLen);
  // Il GHASH va sul cifrato: in decifratura è l'ingresso, in cifratura l'uscita.
  if (!encrypting) ghashBlocks(y, H, in, n);
  for (size_t off = 0; off < n; off += 16) {
    inc32(ctr);
    aes.encrypt(ctr, ks);
    const size_t take = n - off < 16 ? n - off : 16;
    for (size_t k = 0; k < take; k++) out[off + k] = in[off + k] ^ ks[k];
  }
  if (encrypting) ghashBlocks(y, H, out, n);
  uint8_t len[16];
  const uint64_t a = uint64_t(aadLen) * 8, c = uint64_t(n) * 8;
  for (int k = 0; k < 8; k++) { len[k] = uint8_t(a >> (56 - 8 * k)); len[8 + k] = uint8_t(c >> (56 - 8 * k)); }
  ghashBlocks(y, H, len, 16);
  aes.encrypt(J0, ks);
  for (int k = 0; k < 16; k++) tag[k] = ks[k] ^ y[k];
  wipe(ks, sizeof ks);
  wipe(H, sizeof H);
}

}  // namespace

bool gcmEncrypt(const uint8_t key[kKey], const uint8_t iv[kGcmIv], const uint8_t *aad, size_t aadLen, const uint8_t *in,
                size_t n, uint8_t *out, uint8_t tag[kGcmTag]) {
  Aes256 aes(key);
  gcmCore(aes, iv, aad, aadLen, in, n, out, true, tag);
  return true;
}

bool gcmDecrypt(const uint8_t key[kKey], const uint8_t iv[kGcmIv], const uint8_t *aad, size_t aadLen, const uint8_t *in,
                size_t n, uint8_t *out, const uint8_t tag[kGcmTag]) {
  Aes256 aes(key);
  uint8_t t[16];
  gcmCore(aes, iv, aad, aadLen, in, n, out, false, t);
  if (ctEqual(t, tag, 16)) return true;
  wipe(out, n);  // niente testo in chiaro non autenticato in giro
  return false;
}

// ---------------------------------------------------------------- X25519 (da tweetnacl.c)
namespace {

typedef int64_t gf[16];
const gf k121665 = {0xDB41, 1};

void car(gf o) {
  for (int i = 0; i < 16; i++) {
    o[i] += int64_t(1) << 16;
    const int64_t c = o[i] >> 16;  // spostamento aritmetico anche per i negativi (gcc e clang)
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c * 65536;
  }
}
void sel(gf p, gf q, int b) {
  const int64_t c = ~(int64_t(b) - 1);
  for (int i = 0; i < 16; i++) {
    const int64_t t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}
void pack(uint8_t *o, const gf n) {
  gf m, t;
  for (int i = 0; i < 16; i++) t[i] = n[i];
  car(t); car(t); car(t);
  for (int j = 0; j < 2; j++) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    const int b = int((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    sel(t, m, 1 - b);
  }
  for (int i = 0; i < 16; i++) { o[2 * i] = uint8_t(t[i] & 0xff); o[2 * i + 1] = uint8_t(t[i] >> 8); }
}
void unpack(gf o, const uint8_t *n) {
  for (int i = 0; i < 16; i++) o[i] = n[2 * i] + (int64_t(n[2 * i + 1]) << 8);
  o[15] &= 0x7fff;
}
void add(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
void sub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
void mul(gf o, const gf a, const gf b) {
  int64_t t[31] = {0};
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; i++) o[i] = t[i];
  car(o);
  car(o);
}
void sqr(gf o, const gf a) { mul(o, a, a); }
void inv(gf o, const gf in) {
  gf c;
  for (int a = 0; a < 16; a++) c[a] = in[a];
  for (int a = 253; a >= 0; a--) {
    sqr(c, c);
    if (a != 2 && a != 4) mul(c, c, in);
  }
  for (int a = 0; a < 16; a++) o[a] = c[a];
}

void scalarmult(uint8_t *q, const uint8_t *n, const uint8_t *p) {
  uint8_t z[32];
  int64_t x[80];
  gf a, b, c, d, e, f;
  for (int i = 0; i < 31; i++) z[i] = n[i];
  z[31] = uint8_t((n[31] & 127) | 64);
  z[0] &= 248;
  unpack(x, p);
  for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
  a[0] = d[0] = 1;
  for (int i = 254; i >= 0; --i) {
    const int r = (z[i >> 3] >> (i & 7)) & 1;
    sel(a, b, r);
    sel(c, d, r);
    add(e, a, c);
    sub(a, a, c);
    add(c, b, d);
    sub(b, b, d);
    sqr(d, e);
    sqr(f, a);
    mul(a, c, a);
    mul(c, b, e);
    add(e, a, c);
    sub(a, a, c);
    sqr(b, a);
    sub(c, d, f);
    mul(a, c, k121665);
    add(a, a, d);
    mul(c, c, a);
    mul(a, d, f);
    mul(d, b, x);
    sqr(b, e);
    sel(a, b, r);
    sel(c, d, r);
  }
  for (int i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; }
  inv(x + 32, x + 32);
  mul(x + 16, x + 16, x + 32);
  pack(q, x + 16);
  wipe(z, sizeof z);
}

}  // namespace

bool x25519(uint8_t out[kKey], const uint8_t sk[kKey], const uint8_t pk[kKey]) {
  scalarmult(out, sk, pk);
  uint8_t any = 0;
  for (size_t k = 0; k < kKey; k++) any |= out[k];
  return any != 0;
}

void x25519Base(uint8_t pk[kKey], const uint8_t sk[kKey]) {
  static const uint8_t k9[32] = {9};
  scalarmult(pk, sk, k9);
}

}  // namespace cry
}  // namespace cal
#endif  // !ARDUINO
