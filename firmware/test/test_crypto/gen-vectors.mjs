#!/usr/bin/env node
// Genera protocol/vectors/crypto-vectors.json con il crypto di Node (OpenSSL): un'implementazione
// indipendente dal C++ del firmware (test_crypto lo rilegge e deve trovare gli stessi byte) e dal Go
// del servizio, che userà lo stesso file. Uso, dalla radice del repo:
//   node firmware/test/test_crypto/gen-vectors.mjs
// I vettori standard (RFC 7748, 5869, 4231, AES-GCM di McGrew e Viega) sono scritti qui come nelle
// fonti e ricontrollati con Node prima di scrivere il file: se uno non torna, il file non si scrive.
import c from 'node:crypto';
import { writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const OUT = path.join(ROOT, 'protocol', 'vectors', 'crypto-vectors.json');

const H = (s) => Buffer.from(s, 'hex');
const hex = (b) => Buffer.from(b).toString('hex');
const A = (s) => Buffer.from(s, 'utf8');
const SEP = Buffer.from([0x1f]);
const sha = (...p) => c.createHash('sha256').update(Buffer.concat(p)).digest();
const hmac = (k, ...p) => c.createHmac('sha256', k).update(Buffer.concat(p)).digest();
const mac16 = (k, ...p) => hmac(k, ...p).subarray(0, 16);
const hkdf = (ikm, salt, info, L) => Buffer.from(c.hkdfSync('sha256', ikm, salt, info, L));
const priv = (sk) => c.createPrivateKey({ key: Buffer.concat([H('302e020100300506032b656e04220420'), sk]), format: 'der', type: 'pkcs8' });
const pub = (pk) => c.createPublicKey({ key: Buffer.concat([H('302a300506032b656e032100'), pk]), format: 'der', type: 'spki' });
const x25519 = (sk, pk) => c.diffieHellman({ privateKey: priv(sk), publicKey: pub(pk) });
const pubOf = (sk) => Buffer.from(c.createPublicKey(priv(sk)).export({ format: 'der', type: 'spki' })).subarray(12);
const gcm = (key, iv, aad, pt) => {
  const g = c.createCipheriv('aes-256-gcm', key, iv);
  g.setAAD(aad);
  const ct = Buffer.concat([g.update(pt), g.final()]);
  return { ct, tag: g.getAuthTag() };
};
const fixed = (label, n = 32) => sha(A('calamaio-test ' + label)).subarray(0, n);  // valori «casuali» fissi
const joined = (...f) => { const out = []; f.forEach((x, i) => { if (i) out.push(SEP); out.push(typeof x === 'string' ? A(x) : x); }); return Buffer.concat(out); };

const problems = [];
const expect = (name, got, want) => { if (hex(got) !== want) problems.push(`${name}: ${hex(got)} invece di ${want}`); return want; };
const seq = (a, b) => Buffer.from(Array.from({ length: b - a + 1 }, (_, i) => a + i));

// ---------------------------------------------------------------- standard
const x25519Std = [
  { name: 'RFC 7748 5.2 #1', scalar: 'a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4', u: 'e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c', out: 'c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552' },
  { name: 'RFC 7748 5.2 #2', scalar: '4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d', u: 'e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493', out: '95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957' },
  { name: 'RFC 7748 6.1 Alice·Bob', scalar: '77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a', u: 'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f', out: '4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742' },
  { name: 'RFC 7748 6.1 Bob·Alice', scalar: '5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb', u: '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a', out: '4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742' },
];
x25519Std.forEach((v) => expect(v.name, x25519(H(v.scalar), H(v.u)), v.out));
const x25519Base = [
  { name: 'RFC 7748 6.1 Alice', scalar: '77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a', pk: '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a' },
  { name: 'RFC 7748 6.1 Bob', scalar: '5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb', pk: 'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f' },
];
x25519Base.forEach((v) => expect(v.name, pubOf(H(v.scalar)), v.pk));
const x25519Iter = { name: 'RFC 7748 5.2 iterazioni (k = u = 9)', after1: '422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079', after1000: '684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51' };
{
  let k = Buffer.alloc(32); k[0] = 9; let u = Buffer.from(k);
  for (let i = 1; i <= 1000; i++) { const r = x25519(k, u); u = k; k = r; if (i === 1) expect('iter 1', k, x25519Iter.after1); }
  expect('iter 1000', k, x25519Iter.after1000);
}
// Punti di ordine basso: il risultato è tutto zero e va rifiutato (RFC 7748 §6.1, «MAY check»).
const x25519LowOrder = [
  { name: 'u = 0', u: '0000000000000000000000000000000000000000000000000000000000000000' },
  { name: 'u = 1', u: '0100000000000000000000000000000000000000000000000000000000000000' },
  { name: 'ordine 8', u: 'e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800' },
];

const hkdfStd = [
  { name: 'RFC 5869 A.1', ikm: hex(Buffer.alloc(22, 0x0b)), salt: '000102030405060708090a0b0c', info: 'f0f1f2f3f4f5f6f7f8f9', L: 42, okm: '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865' },
  { name: 'RFC 5869 A.2', ikm: hex(seq(0, 0x4f)), salt: hex(seq(0x60, 0xaf)), info: hex(seq(0xb0, 0xff)), L: 82, okm: 'b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87' },
  { name: 'RFC 5869 A.3', ikm: hex(Buffer.alloc(22, 0x0b)), salt: '', info: '', L: 42, okm: '8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8' },
];
hkdfStd.forEach((v) => expect(v.name, hkdf(H(v.ikm), H(v.salt), H(v.info), v.L), v.okm));

const hmacStd = [
  { name: 'RFC 4231 #1', key: hex(Buffer.alloc(20, 0x0b)), data: hex(A('Hi There')), mac: 'b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7' },
  { name: 'RFC 4231 #2', key: hex(A('Jefe')), data: hex(A('what do ya want for nothing?')), mac: '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843' },
  { name: 'RFC 4231 #3', key: hex(Buffer.alloc(20, 0xaa)), data: hex(Buffer.alloc(50, 0xdd)), mac: '773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe' },
  { name: 'RFC 4231 #4', key: hex(seq(1, 25)), data: hex(Buffer.alloc(50, 0xcd)), mac: '82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b' },
  { name: 'RFC 4231 #5 (troncato a 128 bit)', key: hex(Buffer.alloc(20, 0x0c)), data: hex(A('Test With Truncation')), mac: 'a3b6167473100ee06e0c796c2955552b' },
  { name: 'RFC 4231 #6', key: hex(Buffer.alloc(131, 0xaa)), data: hex(A('Test Using Larger Than Block-Size Key - Hash Key First')), mac: '60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54' },
  { name: 'RFC 4231 #7', key: hex(Buffer.alloc(131, 0xaa)), data: hex(A('This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.')), mac: '9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2' },
];
hmacStd.forEach((v) => expect(v.name, hmac(H(v.key), H(v.data)).subarray(0, v.mac.length / 2), v.mac));

const shaStd = [
  { name: 'FIPS 180-2 «abc»', data: hex(A('abc')), hash: 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' },
  { name: 'stringa vuota', data: '', hash: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855' },
  { name: 'FIPS 180-2 448 bit', data: hex(A('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq')), hash: '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1' },
];
shaStd.forEach((v) => expect(v.name, sha(H(v.data)), v.hash));

const gcmStd = [
  { name: 'McGrew-Viega #13 (AES-256, vuoto)', key: hex(Buffer.alloc(32)), iv: hex(Buffer.alloc(12)), aad: '', pt: '', ct: '', tag: '530f8afbc74536b9a963b4f1c4cb738b' },
  { name: 'McGrew-Viega #14', key: hex(Buffer.alloc(32)), iv: hex(Buffer.alloc(12)), aad: '', pt: hex(Buffer.alloc(16)), ct: 'cea7403d4d606b6e074ec5d3baf39d18', tag: 'd0d1c8a799996bf0265b98b5d48ab919' },
  { name: 'McGrew-Viega #16 (con AAD)', key: 'feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308', iv: 'cafebabefacedbaddecaf888', aad: 'feedfacedeadbeeffeedfacedeadbeefabaddad2',
    pt: 'd9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39',
    ct: '522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662', tag: '76fc6ece0f4e1768cddf8853bb2d551b' },
];
gcmStd.forEach((v) => { const r = gcm(H(v.key), H(v.iv), H(v.aad), H(v.pt)); expect(v.name + ' ct', r.ct, v.ct); expect(v.name + ' tag', r.tag, v.tag); });

// ---------------------------------------------------------------- flusso Calamaio
const board = 'calamaio-3f2a';
const skS = fixed('skS'), skD = fixed('skD');
const pkS = pubOf(skS), pkD = pubOf(skD);
const commit = sha(pkS);
const Z = x25519(skD, pkS);
expect('Z simmetrico', x25519(skS, pkD), hex(Z));
const T = sha(A('calamaio-pair-v1'), A(board), commit, pkD, pkS);
const K = hkdf(Z, T, A('calamaio-key'), 32);
const sasBytes = hkdf(Z, T, A('calamaio-sas'), 32);
const sasNum = sasBytes.readUInt32BE(0) % 1000000;
const sas = String(sasNum).padStart(6, '0');
const pairConfirm = mac16(K, A('pair-s'), T);
const pairDone = mac16(K, A('pair-d'), T);
const pairId = 'p7';

const ns = fixed('ns', 16), nd = fixed('nd', 16);
const authIn = Buffer.concat([A('auth-d'), A(board), ns, nd]);
const authOkIn = Buffer.concat([A('auth-s'), A(board), nd, ns]);
const SK = hkdf(K, Buffer.concat([ns, nd]), A('calamaio-session'), 32);

const at = 1791707400000;
const reqs = [
  { id: hex(fixed('req', 16)), session: '8c1d4e2a-6b3f-4c5d-9e7f-0a1b2c3d4e5f', name: 'web-app', tool: 'Bash', arg: 'npm run test', at },
  { id: hex(fixed('req2', 16)), session: '8c1d4e2a-6b3f-4c5d-9e7f-0a1b2c3d4e5f', name: 'web-app', tool: 'Edit', arg: 'src/città/perché.ts', at: at + 61000 },
];
const reqVec = reqs.map((r) => {
  const input = joined('req', r.id, r.session, r.name, r.tool, r.arg, String(r.at));
  const mac = mac16(SK, input);
  return { fields: r, macInput: hex(input), mac: hex(mac),
    message: { t: 'perm-request', v: 1, id: r.id, session: r.session, name: r.name, tool: r.tool, arg: r.arg, at: r.at, mac: hex(mac) } };
});
const replies = [
  { id: reqs[0].id, decision: 'allow', nonce: hex(fixed('nonce', 16)) },
  { id: reqs[1].id, decision: 'deny', nonce: hex(fixed('nonce2', 16)) },
].map((r) => {
  const input = joined('rep', r.id, r.decision, r.nonce);
  const mac = mac16(SK, input);
  return { fields: r, macInput: hex(input), mac: hex(mac), message: { t: 'perm-reply', v: 1, id: r.id, decision: r.decision, nonce: r.nonce, mac: hex(mac) } };
});
const closeFields = { id: reqs[0].id, reason: 'terminal' };
const closeIn = joined('close', closeFields.id, closeFields.reason);
const closeMac = mac16(SK, closeIn);
const closeMsg = { t: 'perm-close', v: 1, id: closeFields.id, reason: closeFields.reason, mac: hex(closeMac) };
// Casi negativi: il display li deve scartare (e scriverlo nel log).
const negative = {
  permRequestNameAltered: { expect: 'rifiutato', why: 'name è nel mac', message: { ...reqVec[0].message, name: 'altra-sessione' } },
  permRequestNameTooLong: { expect: 'rifiutato', why: 'name oltre 31 byte (il servizio tronca prima del mac)', message: { ...reqVec[0].message, name: 'n'.repeat(32) } },
  permCloseNoMac: { expect: 'ignorato', why: 'perm-close senza mac', message: { t: 'perm-close', v: 1, id: closeFields.id, reason: closeFields.reason } },
  permCloseBadMac: { expect: 'ignorato', why: 'perm-close con mac sbagliato', message: { ...closeMsg, mac: hex(mac16(SK, joined('close', closeFields.id, 'display'))) } }
};
const unpairIn = joined('unpair', board);
const unpairMac = mac16(SK, unpairIn);

// Segreti sulla scheda: chiave eFuse finta (nella scheda vera non si legge mai), sale, KE e un record.
const efuseKey = fixed('efuse');
const salt = fixed('salt', 16);
const keIn = Buffer.concat([A('calamaio-nvs'), salt]);
const KE = hmac(efuseKey, keIn);
const recIv = fixed('iv', 12);
const passPlain = Buffer.concat([A('segreta-123'), Buffer.from([0])]);  // la password si salva con il terminatore (rete aperta = solo 0x00)
const recPass = gcm(KE, recIv, A('wifi-pass'), passPlain);
const recIv2 = fixed('iv2', 12);
const recK = gcm(KE, recIv2, A('pair-key'), K);
const record = (iv, r) => hex(Buffer.concat([Buffer.from([1]), iv, r.tag, r.ct]));

if (problems.length) { console.error('vettori che non tornano:\n  ' + problems.join('\n  ')); process.exit(1); }

const doc = {
  $comment: 'Vettori di prova di M5 (docs/M5-PERMESSI.md). Generato da firmware/test/test_crypto/gen-vectors.mjs con il crypto di Node (OpenSSL); il firmware (test_crypto) e il servizio Go devono riprodurre ogni byte. Non modificare a mano.',
  format: {
    hex: 'tutte le stringhe binarie sono esadecimali minuscole',
    concat: 'pair, auth e KE: concatenazione di byte grezzi, etichette e board in ASCII senza separatori (commit, pk, ns, nd sono i byte, non l\'esadecimale)',
    sep: 'perm-request («req» ␟ id ␟ session ␟ name ␟ tool ␟ arg ␟ at), perm-close («close» ␟ id ␟ reason), perm-reply («rep» ␟ id ␟ decision ␟ nonce) e unpair («unpair» ␟ board): etichetta e campi come testo UTF-8 del JSON (id, nonce in esadecimale; at in decimale), separati da un byte 0x1F, senza 0x1F finale. name ≤ 31, tool ≤ 63, arg ≤ 96 byte, troncati in modo sicuro per UTF-8 prima del mac',
    mac: 'HMAC-SHA256 troncato ai primi 16 byte (32 caratteri esadecimali)',
    sas: 'HKDF(Z, salt = T, info = "calamaio-sas"): i primi 4 byte (uguali per qualsiasi lunghezza L) come intero big-endian, modulo 1 000 000, 6 cifre con zeri iniziali',
    x25519: 'RFC 7748: segreto «clampato» e bit alto di u ignorato; un risultato tutto zero si rifiuta',
    record: 'segreti sulla scheda: 0x01 ‖ iv (12) ‖ tag (16) ‖ cifrato; AES-256-GCM con chiave KE = HMAC_eFuse("calamaio-nvs" ‖ sale) e AAD = nome del record in ASCII («wifi-pass»: password UTF-8 più 0x00; «pair-key»: K, 32 byte). Il record cifrato sta in NVS sotto «<nome>.e», il chiaro (prima di Proteggi le chiavi) sotto «<nome>»'
  },
  standard: {
    sha256: shaStd,
    hmacSha256: hmacStd,
    hkdfSha256: hkdfStd,
    x25519: x25519Std,
    x25519Base,
    x25519Iterated: x25519Iter,
    x25519LowOrder,
    aes256Gcm: gcmStd
  },
  flow: {
    board,
    pair: {
      id: pairId,
      skS: hex(skS), pkS: hex(pkS), commit: hex(commit),
      skD: hex(skD), pkD: hex(pkD),
      Z: hex(Z),
      Tinput: hex(Buffer.concat([A('calamaio-pair-v1'), A(board), commit, pkD, pkS])),
      T: hex(T), K: hex(K),
      sasBytes: hex(sasBytes), sasInt: sasBytes.readUInt32BE(0), sas,
      pairConfirmMac: hex(pairConfirm), pairDoneMac: hex(pairDone),
      messages: [
        { t: 'pair-begin', v: 1, id: pairId, commit: hex(commit) },
        { t: 'pair-key', v: 1, id: pairId, pkD: hex(pkD) },
        { t: 'pair-reveal', v: 1, id: pairId, pkS: hex(pkS) },
        { t: 'pair-confirm', v: 1, id: pairId, mac: hex(pairConfirm) },
        { t: 'pair-done', v: 1, id: pairId, mac: hex(pairDone) }
      ],
      wrongCode: { t: 'pair-abort', v: 1, id: pairId, reason: 'codice' }
    },
    auth: {
      ns: hex(ns), nd: hex(nd),
      authInput: hex(authIn), authMac: hex(mac16(K, authIn)),
      authOkInput: hex(authOkIn), authOkMac: hex(mac16(K, authOkIn)),
      SK: hex(SK),
      messages: [
        { t: 'auth-challenge', v: 1, ns: hex(ns) },
        { t: 'auth', v: 1, nd: hex(nd), mac: hex(mac16(K, authIn)) },
        { t: 'auth-ok', v: 1, mac: hex(mac16(K, authOkIn)) }
      ]
    },
    permRequest: reqVec,
    permReply: replies,
    permClose: { fields: closeFields, macInput: hex(closeIn), mac: hex(closeMac), message: closeMsg },
    negative,
    unpair: { macInput: hex(unpairIn), mac: hex(unpairMac), message: { t: 'unpair', v: 1, mac: hex(unpairMac) } },
    vault: {
      efuseKey: hex(efuseKey), salt: hex(salt),
      keInput: hex(keIn), KE: hex(KE),
      records: [
        { name: 'wifi-pass', plaintext: hex(passPlain), iv: hex(recIv), record: record(recIv, recPass) },
        { name: 'pair-key', plaintext: hex(K), iv: hex(recIv2), record: record(recIv2, recK) }
      ]
    }
  }
};
writeFileSync(OUT, JSON.stringify(doc, null, 2) + '\n');
console.log('scritto', path.relative(ROOT, OUT), '· SAS', sas);
