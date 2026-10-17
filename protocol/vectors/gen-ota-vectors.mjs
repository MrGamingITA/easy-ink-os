#!/usr/bin/env node
// Genera protocol/vectors/ota-vectors.json (docs/OTA.md) con il crypto di Node (OpenSSL): un'implementazione
// indipendente dal C++ del firmware (TweetNaCl, test_ota) e dal Go del servizio (crypto/ed25519), che leggono
// lo stesso file. Uso, dalla radice del repo:
//   node protocol/vectors/gen-ota-vectors.mjs
// I vettori RFC 8032 sono scritti qui come nella RFC e ricontrollati con Node: se uno non torna, il file non
// si scrive. La chiave di sessione SK viene da crypto-vectors.json (flow.auth.SK), così i mac di ota-key e
// ota-offer si ricalcolano con la stessa SK dei vettori di M5.
import c from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, 'ota-vectors.json');
const M5 = JSON.parse(readFileSync(path.join(HERE, 'crypto-vectors.json'), 'utf8'));

const H = (s) => Buffer.from(s, 'hex');
const hex = (b) => Buffer.from(b).toString('hex');
const A = (s) => Buffer.from(s, 'utf8');
const SEP = Buffer.from([0x1f]);
const sha = (...p) => c.createHash('sha256').update(Buffer.concat(p)).digest();
const mac16 = (k, ...p) => c.createHmac('sha256', k).update(Buffer.concat(p)).digest().subarray(0, 16);
const joined = (...f) => { const out = []; f.forEach((x, i) => { if (i) out.push(SEP); out.push(typeof x === 'string' ? A(x) : x); }); return Buffer.concat(out); };
const fixed = (label, n = 32) => sha(A('calamaio-test ' + label)).subarray(0, n);

// Ed25519 di Node: seme (32 byte) in PKCS#8, chiave pubblica in SPKI.
const edPriv = (seed) => c.createPrivateKey({ key: Buffer.concat([H('302e020100300506032b657004220420'), seed]), format: 'der', type: 'pkcs8' });
const edPubKey = (pk) => c.createPublicKey({ key: Buffer.concat([H('302a300506032b6570032100'), pk]), format: 'der', type: 'spki' });
const edPub = (seed) => Buffer.from(c.createPublicKey(edPriv(seed)).export({ format: 'der', type: 'spki' })).subarray(12);
const edSign = (seed, msg) => c.sign(null, msg, edPriv(seed));
const edVerify = (pk, msg, sig) => { try { return c.verify(null, msg, edPubKey(pk), sig); } catch (e) { return false; } };

const problems = [];
const expect = (name, got, want) => { if (hex(got) !== want) problems.push(`${name}: ${hex(got)} invece di ${want}`); return want; };

// ---------- RFC 8032 §7.1 (Ed25519 puro) ----------
const RFC = [
  { name: 'RFC 8032 7.1 TEST 1 (messaggio vuoto)', seed: '9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60',
    pub: 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a', msg: '',
    sig: 'e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b' },
  { name: 'RFC 8032 7.1 TEST 2 (1 byte)', seed: '4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb',
    pub: '3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c', msg: '72',
    sig: '92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00' },
  { name: 'RFC 8032 7.1 TEST 3 (2 byte)', seed: 'c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7',
    pub: 'fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025', msg: 'af82',
    sig: '6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a' },
  { name: 'RFC 8032 7.1 TEST SHA(abc)', seed: '833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42',
    pub: 'ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf',
    msg: 'ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f',
    sig: 'dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704' },
];
for (const v of RFC) {
  expect(v.name + ' pub', edPub(H(v.seed)), v.pub);
  expect(v.name + ' sig', edSign(H(v.seed), H(v.msg)), v.sig);
  if (!edVerify(H(v.pub), H(v.msg), H(v.sig))) problems.push(v.name + ': Node non la verifica');
}

// ---------- casi negativi ----------
// L = 2^252 + 27742317777372353535851937790883648493: una firma con S + L è la stessa firma «malleabile»; RFC 8032
// §5.1.7 chiede di rifiutare S ≥ L. Una y senza x sulla curva (x² non quadrato) è una chiave pubblica non valida.
const P = (1n << 255n) - 19n;
const L = (1n << 252n) + 27742317777372353535851937790883648493n;
const D = (-121665n * modInv(121666n)) % P + P;
function modPow(b, e, m) { let r = 1n; b %= m; while (e > 0n) { if (e & 1n) r = r * b % m; b = b * b % m; e >>= 1n; } return r; }
function modInv(a) { return modPow(((a % P) + P) % P, P - 2n, P); }
const leToBig = (b) => { let r = 0n; for (let i = b.length - 1; i >= 0; i--) r = (r << 8n) | BigInt(b[i]); return r; };
const bigToLe = (v, n) => { const b = Buffer.alloc(n); for (let i = 0; i < n; i++) { b[i] = Number(v & 255n); v >>= 8n; } return b; };
function notOnCurve() {
  for (let y = 2n; y < 1000n; y++) {
    const y2 = y * y % P, u = (y2 - 1n + P) % P, v = (D * y2 + 1n) % P;
    const x2 = u * modInv(v) % P;
    if (x2 !== 0n && modPow(x2, (P - 1n) / 2n, P) !== 1n) return bigToLe(y, 32);
  }
  throw new Error('nessuna y fuori curva trovata');
}
const t1 = RFC[1];
const flip = (hexStr, byte, bit) => { const b = H(hexStr); b[byte] ^= 1 << bit; return hex(b); };
const sPlusL = (() => { const s = H(t1.sig); const S = leToBig(s.subarray(32)) + L; return hex(Buffer.concat([s.subarray(0, 32), bigToLe(S, 32)])); })();
const NEG = [
  { name: 'firma con un bit cambiato in R', pub: t1.pub, msg: t1.msg, sig: flip(t1.sig, 0, 0) },
  { name: 'firma con un bit cambiato in S', pub: t1.pub, msg: t1.msg, sig: flip(t1.sig, 40, 3) },
  { name: 'messaggio cambiato', pub: t1.pub, msg: '73', sig: t1.sig },
  { name: 'chiave di un altro', pub: RFC[2].pub, msg: t1.msg, sig: t1.sig },
  { name: 'S + L (non canonica, RFC 8032 5.1.7)', pub: t1.pub, msg: t1.msg, sig: sPlusL },
  { name: 'chiave pubblica fuori dalla curva', pub: hex(notOnCurve()), msg: t1.msg, sig: t1.sig },
];
for (const v of NEG) if (edVerify(H(v.pub), H(v.msg), H(v.sig))) problems.push(v.name + ': Node la accetta');

// ---------- manifesto (docs/OTA.md §2) ----------
// Forma canonica: board ␟ device ␟ version ␟ size ␟ sha256 ␟ built, testi UTF-8 come nel JSON (size e built in
// decimale, sha256 in esadecimale minuscolo), separati da un byte 0x1F, senza etichetta e senza 0x1F finale.
const keySeed = fixed('ota-key');
const keyPub = edPub(keySeed);
const otherSeed = fixed('ota-key-altra');
const image = A('immagine di prova del firmware Calamaio 0.7.0\n'.repeat(64));
const manifest = { board: 'calamaio-3f2a', device: 'heltec213', version: '0.7.0', size: image.length, sha256: hex(sha(image)), built: 1791738000000 };
const canon = (m) => joined(m.board, m.device, m.version, String(m.size), m.sha256, String(m.built));
const mCanon = canon(manifest);
const mSig = edSign(keySeed, mCanon);
if (!edVerify(keyPub, mCanon, mSig)) problems.push('manifesto: firma non verificata da Node');
const mNeg = [
  { name: 'versione cambiata dopo la firma', manifest: { ...manifest, version: '0.7.1' }, sig: hex(mSig) },
  { name: 'dimensione cambiata dopo la firma', manifest: { ...manifest, size: manifest.size + 1 }, sig: hex(mSig) },
  { name: 'board di un\'altra scheda', manifest: { ...manifest, board: 'calamaio-0000' }, sig: hex(mSig) },
  { name: 'firmato con un\'altra chiave', manifest, sig: hex(edSign(otherSeed, mCanon)) },
  { name: 'firma con un bit cambiato', manifest, sig: flip(hex(mSig), 63, 7) },
];
for (const v of mNeg) if (edVerify(keyPub, canon(v.manifest), H(v.sig))) problems.push('manifesto negativo accettato: ' + v.name);

// ---------- mac (SK della connessione autenticata, come i permessi di M5) ----------
const SK = H(M5.flow.auth.SK);
const pubHex = hex(keyPub);
const keyMacIn = joined('ota-key', pubHex);
const keyMac = mac16(SK, keyMacIn);
const offerId = 'c17';
const offerMacIn = joined('ota-offer', offerId, manifest.sha256);
const offerMac = mac16(SK, offerMacIn);
const url = 'http://192.168.1.20:8766/ota/' + hex(fixed('token', 16));
const cancelMacIn = joined('ota-cancel', offerId);
const cancelMac = mac16(SK, cancelMacIn);

if (problems.length) { console.error('vettori che non tornano:\n  ' + problems.join('\n  ')); process.exit(1); }

const doc = {
  $comment: 'Vettori di prova dell\'aggiornamento remoto (docs/OTA.md). Generato da protocol/vectors/gen-ota-vectors.mjs con il crypto di Node (OpenSSL); il firmware (test_ota) e il servizio Go devono riprodurre ogni byte. Non modificare a mano.',
  format: {
    hex: 'tutte le stringhe binarie sono esadecimali minuscole',
    ed25519: 'Ed25519 puro (RFC 8032), firma di 64 byte = 128 caratteri; chiave pubblica di 32 byte = 64 caratteri. Si rifiutano S ≥ L e le chiavi fuori dalla curva',
    manifest: 'forma canonica: board ␟ device ␟ version ␟ size ␟ sha256 ␟ built (testi UTF-8 del JSON; size e built interi in decimale senza zeri iniziali; sha256 esadecimale minuscolo), separati da un byte 0x1F, senza etichetta e senza 0x1F finale. Nessun campo può contenere 0x1F. La firma è sui byte canonici',
    mac: 'HMAC-SHA256 con la SK della connessione (docs/M5-PERMESSI.md §2) troncato a 16 byte (32 caratteri). ota-key: «ota-key» ␟ pub (pub in esadecimale, come nel JSON). ota-offer: «ota-offer» ␟ id ␟ sha256 (sha256 del manifesto, esadecimale). ota-cancel: «ota-cancel» ␟ id',
  },
  ed25519: RFC,
  ed25519Negative: NEG,
  manifest: {
    key: { seed: hex(keySeed), pub: pubHex, note: 'seme = SHA-256("calamaio-test ota-key"): solo per i test' },
    image: { size: image.length, sha256: manifest.sha256, note: '«immagine di prova del firmware Calamaio 0.7.0\\n» ripetuta 64 volte' },
    fields: manifest,
    canonical: hex(mCanon),
    sig: hex(mSig),
    negative: mNeg,
  },
  otaKey: {
    SK: hex(SK),
    pub: pubHex,
    macInput: hex(keyMacIn),
    mac: hex(keyMac),
    message: { t: 'ota-key', v: 1, pub: pubHex, mac: hex(keyMac) },
  },
  otaOffer: {
    SK: hex(SK),
    id: offerId,
    macInput: hex(offerMacIn),
    mac: hex(offerMac),
    message: { t: 'ota-offer', v: 1, id: offerId, manifest, sig: hex(mSig), url, mac: hex(offerMac) },
  },
  otaCancel: {
    SK: hex(SK),
    id: offerId,
    macInput: hex(cancelMacIn),
    mac: hex(cancelMac),
    message: { t: 'ota-cancel', v: 1, id: offerId, mac: hex(cancelMac) },
  },
  // Forme dei messaggi del display (nessuna crittografia): esattamente quello che scrive il firmware 0.7.0
  // (test_ota e test_logring li confrontano byte per byte con gli encoder).
  examples: {
    otaStatus: [
      { t: 'ota-status', v: 1, id: offerId, phase: 'rifiutato', done: 0, total: manifest.size, error: 'batteria al 42%: collega il cavo USB o ricarica fino al 50%' },
      { t: 'ota-status', v: 1, id: offerId, phase: 'ricevo', done: 0, total: manifest.size },
      { t: 'ota-status', v: 1, id: offerId, phase: 'ricevo', done: 1536, total: manifest.size },
      { t: 'ota-status', v: 1, id: offerId, phase: 'verifico', done: manifest.size, total: manifest.size },
      { t: 'ota-status', v: 1, id: offerId, phase: 'scrivo', done: manifest.size, total: manifest.size },
      { t: 'ota-status', v: 1, id: offerId, phase: 'riavvio', done: manifest.size, total: manifest.size },
      { t: 'ota-status', v: 1, id: offerId, phase: 'fatto', done: manifest.size, total: manifest.size },
      { t: 'ota-status', v: 1, id: offerId, phase: 'errore', done: 1536, total: manifest.size, error: 'download interrotto: nessun dato da 15 s' },
      { t: 'ota-status', v: 1, id: offerId, phase: 'errore', done: manifest.size, total: manifest.size, error: 'la nuova versione non si è avviata: tornato alla precedente' },
    ],
    status: { t: 'status', v: 1, batt: 78, usb: false, cal: false },
    statusUnknown: { t: 'status', v: 1, batt: null, usb: null, cal: false },
    logStart: { t: 'log-start', v: 1, id: 'c31', follow: true, minutes: 30 },
    logStop: { t: 'log-stop', v: 1, id: 'c31' },
    logDumpLast: { t: 'log', v: 1, id: 'c31', lines: ['12.345 [ws] connesso a ws://192.168.1.20:8766/ws (tentativo 1)', '12.401 [m5] servizio autenticato (auth-ok): permessi e impostazioni remote attivi'], end: true },
    logFollow: { t: 'log', v: 1, id: 'c31', lines: ['13.500 [ws] state: 4 sessioni, 6120 byte'] },
    logEmptyRing: { t: 'log', v: 1, id: 'c31', lines: [], end: true },
    crash: { t: 'crash', v: 1, fw: '0.7.0', reason: 'LoadProhibited', exc: 'EXCCAUSE 28', task: 'loopTask', pc: '0x42012345', backtrace: ['0x42012345', '0x42008abc', '0x4037a1b2'], last: '815.002 [refresh] partial 644 ms · home · f1 · parziali 3/40' },
    crashWithoutCoredump: { t: 'crash', v: 1, fw: '0.7.0', reason: 'ESP_RST_TASK_WDT', last: '814.100 [ws] state: 4 sessioni\n815.002 [refresh] partial 644 ms · home · f1 · parziali 3/40' },
  },
};
writeFileSync(OUT, JSON.stringify(doc, null, 1) + '\n');
console.log('scritto ' + path.relative(process.cwd(), OUT));
