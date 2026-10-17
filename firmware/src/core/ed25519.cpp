// Ed25519, sola verifica: vedi ed25519.h. Struttura e nomi di tweetnacl.c (pubblico dominio).
#include "ed25519.h"

#include <string.h>

namespace cal {
namespace ed {

namespace {

typedef int64_t gf[16];

const gf gf0 = {0};
const gf gf1 = {1};
// Costanti ricalcolate da zero (Python, aritmetica intera) e uguali a tweetnacl.c: d = −121665/121666,
// 2d, il punto base (X, Y = 4/5) e I = √−1 = 2^((p−1)/4), in 16 cifre da 16 bit, la meno significativa prima.
const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
const gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

// ---------------------------------------------------------------- campo GF(2^255 − 19)
void set25519(gf r, const gf a) { for (int i = 0; i < 16; i++) r[i] = a[i]; }

void car25519(gf o) {
  for (int i = 0; i < 16; i++) {
    o[i] += int64_t(1) << 16;
    const int64_t c = o[i] >> 16;  // spostamento aritmetico (gcc/clang)
    if (i < 15) o[i + 1] += c - 1;
    else o[0] += 38 * (c - 1);
    o[i] -= c * 65536;  // c << 16 senza spostare un negativo
  }
}

void sel25519(gf p, gf q, int b) {
  const int64_t c = ~int64_t(b - 1);
  for (int i = 0; i < 16; i++) {
    const int64_t t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

void pack25519(uint8_t *o, const gf n) {
  gf m, t;
  for (int i = 0; i < 16; i++) t[i] = n[i];
  car25519(t);
  car25519(t);
  car25519(t);
  for (int j = 0; j < 2; j++) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    const int b = int((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    sel25519(t, m, 1 - b);
  }
  for (int i = 0; i < 16; i++) {
    o[2 * i] = uint8_t(t[i] & 0xff);
    o[2 * i + 1] = uint8_t(t[i] >> 8);
  }
}

int vn32(const uint8_t *x, const uint8_t *y) {
  uint32_t d = 0;
  for (int i = 0; i < 32; i++) d |= x[i] ^ y[i];
  return (1 & ((d - 1) >> 8)) - 1;  // 0 se uguali, −1 altrimenti
}

int neq25519(const gf a, const gf b) {
  uint8_t c[32], d[32];
  pack25519(c, a);
  pack25519(d, b);
  return vn32(c, d);
}

uint8_t par25519(const gf a) {
  uint8_t d[32];
  pack25519(d, a);
  return d[0] & 1;
}

void unpack25519(gf o, const uint8_t *n) {
  for (int i = 0; i < 16; i++) o[i] = n[2 * i] + (int64_t(n[2 * i + 1]) << 8);
  o[15] &= 0x7fff;
}

void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

void M(gf o, const gf a, const gf b) {
  int64_t t[31];
  for (int i = 0; i < 31; i++) t[i] = 0;
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; i++) o[i] = t[i];
  car25519(o);
  car25519(o);
}

void S(gf o, const gf a) { M(o, a, a); }

void inv25519(gf o, const gf i) {
  gf c;
  for (int a = 0; a < 16; a++) c[a] = i[a];
  for (int a = 253; a >= 0; a--) {
    S(c, c);
    if (a != 2 && a != 4) M(c, c, i);
  }
  for (int a = 0; a < 16; a++) o[a] = c[a];
}

void pow2523(gf o, const gf i) {
  gf c;
  for (int a = 0; a < 16; a++) c[a] = i[a];
  for (int a = 250; a >= 0; a--) {
    S(c, c);
    if (a != 1) M(c, c, i);
  }
  for (int a = 0; a < 16; a++) o[a] = c[a];
}

// ---------------------------------------------------------------- SHA-512
const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

inline uint64_t ror(uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }

uint64_t dl64(const uint8_t *x) {
  uint64_t u = 0;
  for (int i = 0; i < 8; i++) u = (u << 8) | x[i];
  return u;
}

void ts64(uint8_t *x, uint64_t u) {
  for (int i = 7; i >= 0; --i) {
    x[i] = uint8_t(u);
    u >>= 8;
  }
}

struct Sha512 {
  uint64_t h[8];
  uint8_t buf[128];
  size_t n;
  uint64_t total;
  Sha512() : n(0), total(0) {
    static const uint64_t kIv[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                                    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    memcpy(h, kIv, sizeof h);
  }
  void block(const uint8_t *m) {
    uint64_t w[16], a[8], b[8];
    for (int i = 0; i < 16; i++) w[i] = dl64(m + 8 * i);
    for (int i = 0; i < 8; i++) a[i] = h[i];
    for (int i = 0; i < 80; i++) {
      for (int j = 0; j < 8; j++) b[j] = a[j];
      const uint64_t t = a[7] + (ror(a[4], 14) ^ ror(a[4], 18) ^ ror(a[4], 41)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + K512[i] + w[i % 16];
      b[7] = t + (ror(a[0], 28) ^ ror(a[0], 34) ^ ror(a[0], 39)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
      b[3] += t;
      for (int j = 0; j < 8; j++) a[(j + 1) % 8] = b[j];
      if (i % 16 == 15)
        for (int j = 0; j < 16; j++) {
          const uint64_t w1 = w[(j + 1) % 16], w14 = w[(j + 14) % 16];
          w[j] += w[(j + 9) % 16] + (ror(w1, 1) ^ ror(w1, 8) ^ (w1 >> 7)) + (ror(w14, 19) ^ ror(w14, 61) ^ (w14 >> 6));
        }
    }
    for (int i = 0; i < 8; i++) h[i] += a[i];
  }
  void update(const uint8_t *d, size_t len) {
    total += len;
    while (len) {
      const size_t take = len < 128 - n ? len : 128 - n;
      memcpy(buf + n, d, take);
      n += take;
      d += take;
      len -= take;
      if (n == 128) { block(buf); n = 0; }
    }
  }
  void final(uint8_t out[64]) {
    const uint64_t bits = total * 8;
    const uint8_t pad = 0x80;
    update(&pad, 1);
    const uint8_t z = 0;
    while (n != 112) update(&z, 1);
    uint8_t len[16] = {0};
    ts64(len + 8, bits);  // lunghezze sotto i 2^64 bit: la metà alta resta zero
    update(len, 16);
    for (int i = 0; i < 8; i++) ts64(out + 8 * i, h[i]);
  }
};

// ---------------------------------------------------------------- punti (coordinate estese)
void add(gf p[4], gf q[4]) {
  gf a, b, c, d, t, e, f, g, h;
  Z(a, p[1], p[0]);
  Z(t, q[1], q[0]);
  M(a, a, t);
  A(b, p[0], p[1]);
  A(t, q[0], q[1]);
  M(b, b, t);
  M(c, p[3], q[3]);
  M(c, c, D2);
  M(d, p[2], q[2]);
  A(d, d, d);
  Z(e, b, a);
  Z(f, d, c);
  A(g, d, c);
  A(h, b, a);
  M(p[0], e, f);
  M(p[1], h, g);
  M(p[2], g, f);
  M(p[3], e, h);
}

void cswap(gf p[4], gf q[4], uint8_t b) {
  for (int i = 0; i < 4; i++) sel25519(p[i], q[i], b);
}

void pack(uint8_t *r, gf p[4]) {
  gf tx, ty, zi;
  inv25519(zi, p[2]);
  M(tx, p[0], zi);
  M(ty, p[1], zi);
  pack25519(r, ty);
  r[31] ^= par25519(tx) << 7;
}

void scalarmult(gf p[4], gf q[4], const uint8_t *s) {
  set25519(p[0], gf0);
  set25519(p[1], gf1);
  set25519(p[2], gf1);
  set25519(p[3], gf0);
  for (int i = 255; i >= 0; --i) {
    const uint8_t b = (s[i / 8] >> (i & 7)) & 1;
    cswap(p, q, b);
    add(q, p);
    add(p, p);
    cswap(p, q, b);
  }
}

void scalarbase(gf p[4], const uint8_t *s) {
  gf q[4];
  set25519(q[0], X);
  set25519(q[1], Y);
  set25519(q[2], gf1);
  M(q[3], X, Y);
  scalarmult(p, q, s);
}

// ---------------------------------------------------------------- scalari modulo L
const int64_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

void modL(uint8_t *r, int64_t x[64]) {
  int64_t carry;
  int i, j;
  for (i = 63; i >= 32; --i) {
    carry = 0;
    for (j = i - 32; j < i - 12; ++j) {
      x[j] += carry - 16 * x[i] * L[j - (i - 32)];
      carry = (x[j] + 128) >> 8;
      x[j] -= carry * 256;
    }
    x[j] += carry;
    x[i] = 0;
  }
  carry = 0;
  for (j = 0; j < 32; j++) {
    x[j] += carry - (x[31] >> 4) * L[j];
    carry = x[j] >> 8;
    x[j] &= 255;
  }
  for (j = 0; j < 32; j++) x[j] -= carry * L[j];
  for (i = 0; i < 32; i++) {
    x[i + 1] += x[i] >> 8;
    r[i] = uint8_t(x[i] & 255);
  }
}

void reduce(uint8_t *r) {
  int64_t x[64];
  for (int i = 0; i < 64; i++) x[i] = int64_t(r[i]);
  for (int i = 0; i < 64; i++) r[i] = 0;
  modL(r, x);
}

// S < L (RFC 8032 §5.1.7): confronto dal byte più significativo.
bool scalarCanonical(const uint8_t s[32]) {
  for (int i = 31; i >= 0; i--) {
    if (s[i] < L[i]) return true;
    if (s[i] > L[i]) return false;
  }
  return false;  // S = L
}

// −A dalla chiave pubblica (decompressione); −1 se y non corrisponde a un punto della curva.
int unpackneg(gf r[4], const uint8_t p[32]) {
  gf t, chk, num, den, den2, den4, den6;
  set25519(r[2], gf1);
  unpack25519(r[1], p);
  S(num, r[1]);
  M(den, num, D);
  Z(num, num, r[2]);
  A(den, r[2], den);

  S(den2, den);
  S(den4, den2);
  M(den6, den4, den2);
  M(t, den6, num);
  M(t, t, den);

  pow2523(t, t);
  M(t, t, num);
  M(t, t, den);
  M(t, t, den);
  M(r[0], t, den);

  S(chk, r[0]);
  M(chk, chk, den);
  if (neq25519(chk, num)) M(r[0], r[0], I);

  S(chk, r[0]);
  M(chk, chk, den);
  if (neq25519(chk, num)) return -1;

  if (par25519(r[0]) == (p[31] >> 7)) Z(r[0], gf0, r[0]);

  M(r[3], r[0], r[1]);
  return 0;
}

}  // namespace

void sha512(const uint8_t *d, size_t n, uint8_t out[64]) {
  Sha512 s;
  s.update(d, n);
  s.final(out);
}

bool verify(const uint8_t sig[kSig], const uint8_t *msg, size_t n, const uint8_t pk[kPub]) {
  gf p[4], q[4];
  uint8_t h[64], t[32];
  if (!scalarCanonical(sig + 32)) return false;
  if (unpackneg(q, pk)) return false;
  // h = SHA-512(R ‖ A ‖ M) mod L; il punto [h](−A) + [S]B deve dare R.
  Sha512 s;
  s.update(sig, 32);
  s.update(pk, 32);
  s.update(msg, n);
  s.final(h);
  reduce(h);
  scalarmult(p, q, h);
  scalarbase(q, sig + 32);
  add(p, q);
  pack(t, p);
  return vn32(sig, t) == 0;
}

}  // namespace ed
}  // namespace cal
