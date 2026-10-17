// Parte comune delle primitive (HKDF, confronti, esadecimale) e formule del protocollo M5.
#include "crypto.h"

#include <string.h>

namespace cal {
namespace cry {

bool hkdf(const uint8_t *salt, size_t saltLen, const uint8_t *ikm, size_t ikmLen, const uint8_t *info, size_t infoLen,
          uint8_t *out, size_t outLen) {
  if (outLen > 255 * kHash || infoLen > 256) return false;
  static const uint8_t kZero[kHash] = {0};
  uint8_t prk[kHash];
  // Estrazione: senza sale si usa una stringa di HashLen zeri (RFC 5869 §2.2).
  bool ok = hmacSha256(saltLen ? salt : kZero, saltLen ? saltLen : kHash, ikm, ikmLen, prk);
  uint8_t t[kHash], buf[kHash + 256 + 1];
  size_t tLen = 0, done = 0;
  for (uint8_t i = 1; ok && done < outLen; i++) {
    memcpy(buf, t, tLen);
    if (infoLen) memcpy(buf + tLen, info, infoLen);
    buf[tLen + infoLen] = i;
    if (!hmacSha256(prk, kHash, buf, tLen + infoLen + 1, t)) { ok = false; break; }
    tLen = kHash;
    const size_t n = outLen - done < kHash ? outLen - done : kHash;
    memcpy(out + done, t, n);
    done += n;
  }
  wipe(prk, sizeof prk);
  wipe(t, sizeof t);
  wipe(buf, sizeof buf);
  if (!ok) wipe(out, outLen);
  return ok;
}

bool ctEqual(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t d = 0;
  for (size_t k = 0; k < n; k++) d |= a[k] ^ b[k];
  return d == 0;
}

void wipe(void *p, size_t n) {
  volatile uint8_t *v = static_cast<volatile uint8_t *>(p);
  while (n--) *v++ = 0;
}

void toHex(const uint8_t *d, size_t n, char *out) {
  static const char *const kDigits = "0123456789abcdef";
  for (size_t k = 0; k < n; k++) {
    out[2 * k] = kDigits[d[k] >> 4];
    out[2 * k + 1] = kDigits[d[k] & 15];
  }
  out[2 * n] = 0;
}

static int nib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;  // maiuscole e altro: il protocollo vuole l'esadecimale minuscolo
}

bool fromHex(const char *s, uint8_t *out, size_t n) {
  if (!s || strlen(s) != 2 * n) return false;
  for (size_t k = 0; k < n; k++) {
    const int a = nib(s[2 * k]), b = nib(s[2 * k + 1]);
    if (a < 0 || b < 0) return false;
    out[k] = static_cast<uint8_t>(a << 4 | b);
  }
  return true;
}

Msg &Msg::raw(const uint8_t *d, size_t n) {
  if (!ok_ || n > sizeof b_ - n_) { ok_ = false; return *this; }
  if (n) memcpy(b_ + n_, d, n);
  n_ += n;
  return *this;
}
Msg &Msg::str(const char *s) { return raw(reinterpret_cast<const uint8_t *>(s ? s : ""), s ? strlen(s) : 0); }
Msg &Msg::sep() { static const uint8_t k = 0x1F; return raw(&k, 1); }
Msg &Msg::num(int64_t v) {
  char t[24];
  int n = 0;
  uint64_t u = v < 0 ? static_cast<uint64_t>(-(v + 1)) + 1 : static_cast<uint64_t>(v);
  do { t[n++] = static_cast<char>('0' + u % 10); u /= 10; } while (u);
  if (v < 0) t[n++] = '-';
  char r[24];
  for (int k = 0; k < n; k++) r[k] = t[n - 1 - k];
  return raw(reinterpret_cast<const uint8_t *>(r), static_cast<size_t>(n));
}

bool mac16(const uint8_t key[kKey], const Msg &m, uint8_t out[kMac]) {
  uint8_t full[kHash];
  const bool ok = hmacSha256(key, kKey, m.data(), m.size(), full);
  memcpy(out, full, kMac);
  wipe(full, sizeof full);
  return ok;
}

// ---------- autoverifica ----------

namespace {
struct Hx {  // esadecimale → byte, per i vettori qui sotto
  uint8_t b[96];
  size_t n;
  explicit Hx(const char *s) : n(strlen(s) / 2) { fromHex(s, b, n); }
};
bool same(const uint8_t *a, const char *hexWant, size_t n) {
  Hx w(hexWant);
  return w.n == n && memcmp(a, w.b, n) == 0;
}
}  // namespace

bool selfTest(char *why, size_t cap) {
  auto fail = [&](const char *name) {
    if (cap) { strncpy(why, name, cap - 1); why[cap - 1] = 0; }
    return false;
  };
  uint8_t out[96];
  // SHA-256 («abc», FIPS 180-2)
  sha256(reinterpret_cast<const uint8_t *>("abc"), 3, out);
  if (!same(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32)) return fail("sha256 abc");
  // HMAC (RFC 4231 #2)
  static const char kWhat[] = "what do ya want for nothing?";
  hmacSha256(reinterpret_cast<const uint8_t *>("Jefe"), 4, reinterpret_cast<const uint8_t *>(kWhat), sizeof kWhat - 1, out);
  if (!same(out, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32)) return fail("hmac rfc4231#2");
  // HKDF (RFC 5869 A.1)
  {
    uint8_t ikm[22];
    memset(ikm, 0x0b, sizeof ikm);
    Hx salt("000102030405060708090a0b0c"), info("f0f1f2f3f4f5f6f7f8f9");
    hkdf(salt.b, salt.n, ikm, sizeof ikm, info.b, info.n, out, 42);
    if (!same(out, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", 42)) return fail("hkdf rfc5869 A.1");
  }
  // X25519 (RFC 7748 §5.2 #1 e §6.1)
  {
    Hx k("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"), u("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
    if (!x25519(out, k.b, u.b) || !same(out, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", 32)) return fail("x25519 rfc7748 5.2");
    Hx a("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    x25519Base(out, a.b);
    if (!same(out, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32)) return fail("x25519 base rfc7748 6.1");
    uint8_t zero[32] = {0};
    if (x25519(out, a.b, zero)) return fail("x25519 punto nullo accettato");
  }
  // AES-256-GCM (McGrew-Viega #16), cifra e decifra; un tag cambiato va rifiutato.
  {
    Hx key("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308"), iv("cafebabefacedbaddecaf888"),
        aad("feedfacedeadbeeffeedfacedeadbeefabaddad2"),
        pt("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    uint8_t tag[16], back[64];
    if (!gcmEncrypt(key.b, iv.b, aad.b, aad.n, pt.b, pt.n, out, tag)) return fail("gcm cifra");
    if (!same(out, "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662", 60) ||
        !same(tag, "76fc6ece0f4e1768cddf8853bb2d551b", 16))
      return fail("gcm tc16");
    if (!gcmDecrypt(key.b, iv.b, aad.b, aad.n, out, 60, back, tag) || memcmp(back, pt.b, 60) != 0) return fail("gcm decifra");
    tag[0] ^= 1;
    if (gcmDecrypt(key.b, iv.b, aad.b, aad.n, out, 60, back, tag)) return fail("gcm tag falso accettato");
  }
  // Flusso: K e codice dell'abbinamento di protocol/vectors/crypto-vectors.json.
  {
    Hx skD("bf1c605a0b5bc4eac989691d5273772fca475845a8a5c13f8ca14fdb4916feff"),
        pkS("b3d8cf94a51157d4feff5c8b6a652c96dec112b4ca2dc71d663821b4a248857b");
    uint8_t pkD[32], c[32], Z[32], T[32], K[32];
    x25519Base(pkD, skD.b);
    if (!same(pkD, "21349ff35a74f3cfe469402550191ff05edb5ad6bc1a584b6c29bf2943ce6e15", 32)) return fail("flusso pkD");
    m5::commit(pkS.b, c);
    if (!x25519(Z, skD.b, pkS.b)) return fail("flusso Z");
    m5::transcript("calamaio-3f2a", c, pkD, pkS.b, T);
    m5::pairKey(Z, T, K);
    if (!same(T, "b02660e6d4e7b6b61a3065f94fcad9eceaecb86a3ca3380c0b794cfb2fb191b9", 32)) return fail("flusso T");
    if (!same(K, "3d9c852a2bcba1e6337f46470998e3bd093d70c3a6fcaa006c4f7875a15e6ba0", 32)) return fail("flusso K");
    if (m5::sas(Z, T) != 175397u) return fail("flusso codice");
    m5::pairConfirmMac(K, T, out);
    if (!same(out, "b0da518010b82314a49e74266482cc00", 16)) return fail("flusso pair-confirm");
    wipe(Z, sizeof Z);
    wipe(K, sizeof K);
  }
  return true;
}

}  // namespace cry

// ---------- formule M5 ----------
namespace m5 {

using cry::Msg;

void commit(const uint8_t pkS[32], uint8_t out[32]) { cry::sha256(pkS, 32, out); }

void transcript(const char *board, const uint8_t c[32], const uint8_t pkD[32], const uint8_t pkS[32], uint8_t T[32]) {
  Msg m;
  m.str("calamaio-pair-v1").str(board).raw(c, 32).raw(pkD, 32).raw(pkS, 32);
  cry::sha256(m.data(), m.size(), T);
}

static bool hk(const uint8_t *ikm, const uint8_t *salt, size_t saltLen, const char *info, uint8_t *out, size_t n) {
  return cry::hkdf(salt, saltLen, ikm, 32, reinterpret_cast<const uint8_t *>(info), strlen(info), out, n);
}

void pairKey(const uint8_t Z[32], const uint8_t T[32], uint8_t K[32]) { hk(Z, T, 32, "calamaio-key", K, 32); }

uint32_t sas(const uint8_t Z[32], const uint8_t T[32]) {
  uint8_t b[4];
  hk(Z, T, 32, "calamaio-sas", b, 4);
  const uint32_t v = static_cast<uint32_t>(b[0]) << 24 | static_cast<uint32_t>(b[1]) << 16 | static_cast<uint32_t>(b[2]) << 8 | b[3];
  return v % 1000000u;
}

void sasText(uint32_t code, char out[7]) {
  for (int k = 5; k >= 0; k--) { out[k] = static_cast<char>('0' + code % 10); code /= 10; }
  out[6] = 0;
}

void pairConfirmMac(const uint8_t K[32], const uint8_t T[32], uint8_t out[16]) {
  Msg m;
  m.str("pair-s").raw(T, 32);
  cry::mac16(K, m, out);
}

void pairDoneMac(const uint8_t K[32], const uint8_t T[32], uint8_t out[16]) {
  Msg m;
  m.str("pair-d").raw(T, 32);
  cry::mac16(K, m, out);
}

bool authMac(const uint8_t K[32], const char *board, const uint8_t ns[16], const uint8_t nd[16], uint8_t out[16]) {
  Msg m;
  m.str("auth-d").str(board).raw(ns, 16).raw(nd, 16);
  return cry::mac16(K, m, out);
}

bool authOkMac(const uint8_t K[32], const char *board, const uint8_t ns[16], const uint8_t nd[16], uint8_t out[16]) {
  Msg m;
  m.str("auth-s").str(board).raw(nd, 16).raw(ns, 16);
  return cry::mac16(K, m, out);
}

bool sessionKey(const uint8_t K[32], const uint8_t ns[16], const uint8_t nd[16], uint8_t SK[32]) {
  uint8_t salt[32];
  memcpy(salt, ns, 16);
  memcpy(salt + 16, nd, 16);
  return hk(K, salt, 32, "calamaio-session", SK, 32);
}

bool reqMac(const uint8_t SK[32], const char *id, const char *session, const char *name, const char *tool, const char *arg,
            int64_t at, uint8_t out[16]) {
  Msg m;
  m.str("req").sep().str(id).sep().str(session).sep().str(name).sep().str(tool).sep().str(arg).sep().num(at);
  return m.ok() && cry::mac16(SK, m, out);
}

bool closeMac(const uint8_t SK[32], const char *id, const char *reason, uint8_t out[16]) {
  Msg m;
  m.str("close").sep().str(id).sep().str(reason);
  return m.ok() && cry::mac16(SK, m, out);
}

bool repMac(const uint8_t SK[32], const char *id, const char *decision, const char *nonceHex, uint8_t out[16]) {
  Msg m;
  m.str("rep").sep().str(id).sep().str(decision).sep().str(nonceHex);
  return m.ok() && cry::mac16(SK, m, out);
}

bool unpairMac(const uint8_t SK[32], const char *board, uint8_t out[16]) {
  Msg m;
  m.str("unpair").sep().str(board);
  return m.ok() && cry::mac16(SK, m, out);
}

}  // namespace m5
}  // namespace cal
