#!/usr/bin/env node
// Genera protocol/vectors/wifi-vectors.json (più reti Wi-Fi, firmware 0.8.0) con il crypto di Node (OpenSSL):
// un'implementazione indipendente dal C++ del firmware (test_wifi) e dal Go del servizio, che leggono lo stesso file.
// Uso, dalla radice del repo:
//   node protocol/vectors/gen-wifi-vectors.mjs
// La chiave di sessione SK viene da crypto-vectors.json (flow.auth.SK), come per i vettori dell'OTA. Ogni cifrato
// si ridecifra qui con Node prima di scrivere il file: se uno non torna, il file non si scrive.
//
// Forme (le stesse del servizio):
//   wifi-add    {id, ssid, iv, ct}   ct = AES-256-GCM(key, iv, aad, password UTF-8) ‖ tag (16 byte), esadecimale
//               key = HKDF-SHA256(ikm = SK, salt = id UTF-8, info = "calamaio-wifi"), 32 byte; iv 12 byte
//               aad = "wifi-add" ␟ id ␟ ssid (byte 0x1F tra i campi, UTF-8, senza 0x1F finale)
//   wifi-forget {id, ssid, mac}      mac = HMAC-SHA256(SK, "wifi-forget" ␟ id ␟ ssid) troncato a 16 byte
//   wifi-list   {networks: [{ssid, current, rssi, lastUsed}]}   (display → servizio, mai password)
import c from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, 'wifi-vectors.json');
const M5 = JSON.parse(readFileSync(path.join(HERE, 'crypto-vectors.json'), 'utf8'));

const H = (s) => Buffer.from(s, 'hex');
const hex = (b) => Buffer.from(b).toString('hex');
const A = (s) => Buffer.from(s, 'utf8');
const SEP = Buffer.from([0x1f]);
const sha = (...p) => c.createHash('sha256').update(Buffer.concat(p)).digest();
const mac16 = (k, m) => c.createHmac('sha256', k).update(m).digest().subarray(0, 16);
const joined = (...f) => { const out = []; f.forEach((x, i) => { if (i) out.push(SEP); out.push(typeof x === 'string' ? A(x) : x); }); return Buffer.concat(out); };
const fixed = (label, n) => sha(A('calamaio-test ' + label)).subarray(0, n);
const hkdf = (ikm, salt, info) => Buffer.from(c.hkdfSync('sha256', ikm, salt, A(info), 32));

const SK = H(M5.flow.auth.SK);
const SK2 = fixed('wifi altra connessione', 32);  // SK di un'altra connessione: niente decifra
const problems = [];

const wifiKey = (sk, id) => hkdf(sk, A(id), 'calamaio-wifi');
const addAad = (id, ssid) => joined('wifi-add', id, ssid);
function seal(sk, id, ssid, pwBytes, iv) {
  const g = c.createCipheriv('aes-256-gcm', wifiKey(sk, id), iv);
  g.setAAD(addAad(id, ssid));
  return Buffer.concat([g.update(pwBytes), g.final(), g.getAuthTag()]);
}
function open(sk, id, ssid, iv, ct) {
  try {
    const d = c.createDecipheriv('aes-256-gcm', wifiKey(sk, id), iv);
    d.setAAD(addAad(id, ssid));
    d.setAuthTag(ct.subarray(ct.length - 16));
    return Buffer.concat([d.update(ct.subarray(0, ct.length - 16)), d.final()]);
  } catch (e) { return null; }
}
const addMsg = (id, ssid, iv, ct) => ({ t: 'wifi-add', v: 1, id, ssid, iv: typeof iv === 'string' ? iv : hex(iv), ct: typeof ct === 'string' ? ct : hex(ct) });

// ---------- wifi-add validi ----------
const ADD = [
  { name: 'rete di casa', id: 'c7f3a9e2b1d04', ssid: 'CasaWiFi', password: 'password-di-casa' },
  { name: 'UTF-8 in SSID e password', id: 'c0a1b2c3d4e5f', ssid: 'Caffè «Àrìa» ☕', password: 'pässwörd-ñ-€' },
  { name: 'password di 8 byte (minimo)', id: 'c000000000001', ssid: 'Officina', password: '12345678' },
  { name: 'password di 63 byte (massimo)', id: 'c000000000002', ssid: 'Laboratorio', password: 'a'.repeat(31) + '-' + 'Z'.repeat(31) },
  { name: 'SSID di 32 byte (massimo)', id: 'c000000000003', ssid: 'S'.repeat(32), password: 'spazi e simboli !"#$%&\'()*+,-./' },
];
const add = ADD.map((v, k) => {
  const iv = fixed('wifi iv ' + k, 12), pw = A(v.password);
  const ct = seal(SK, v.id, v.ssid, pw, iv);
  const back = open(SK, v.id, v.ssid, iv, ct);
  if (!back || !back.equals(pw)) problems.push(v.name + ': Node non ridecifra');
  if (pw.length < 8 || pw.length > 63) problems.push(v.name + ': password fuori misura');
  return { name: v.name, id: v.id, ssid: v.ssid, password: v.password, passwordBytes: pw.length, key: hex(wifiKey(SK, v.id)),
           iv: hex(iv), aad: hex(addAad(v.id, v.ssid)), ct: hex(ct), message: addMsg(v.id, v.ssid, iv, ct) };
});

// ---------- wifi-add rifiutati: errore dell'ack ----------
const base = ADD[0], iv0 = fixed('wifi iv 0', 12), ct0 = seal(SK, base.id, base.ssid, A(base.password), iv0);
const flip = (b, i) => { const x = Buffer.from(b); x[i] ^= 0x01; return x; };
const short7 = seal(SK, 'c000000000010', 'Corta', A('1234567'), fixed('wifi iv corta', 12));
const long64 = seal(SK, 'c000000000011', 'Lunga', A('x'.repeat(64)), fixed('wifi iv lunga', 12));
const nul = seal(SK, 'c000000000012', 'Zero', Buffer.concat([A('abcd'), Buffer.from([0]), A('efgh')]), fixed('wifi iv zero', 12));
const addNegative = [
  { name: 'tag alterato', message: addMsg(base.id, base.ssid, iv0, flip(ct0, ct0.length - 1)), error: 'decifratura fallita' },
  { name: 'cifrato alterato', message: addMsg(base.id, base.ssid, iv0, flip(ct0, 0)), error: 'decifratura fallita' },
  { name: 'SSID cambiato (aad)', message: addMsg(base.id, 'CasaWiFi2', iv0, ct0), error: 'decifratura fallita' },
  { name: 'id cambiato (chiave e aad)', message: addMsg('c7f3a9e2b1d05', base.ssid, iv0, ct0), error: 'decifratura fallita' },
  { name: 'iv di 11 byte', message: addMsg(base.id, base.ssid, iv0.subarray(0, 11), ct0), error: 'decifratura fallita' },
  { name: 'iv diverso', message: addMsg(base.id, base.ssid, flip(iv0, 0), ct0), error: 'decifratura fallita' },
  { name: 'cifrato più corto del tag', message: addMsg(base.id, base.ssid, iv0, ct0.subarray(0, 15)), error: 'decifratura fallita' },
  { name: 'esadecimale maiuscolo', message: addMsg(base.id, base.ssid, iv0, hex(ct0).toUpperCase()), error: 'decifratura fallita' },
  { name: 'SK di un\'altra connessione', message: addMsg(base.id, base.ssid, iv0, seal(SK2, base.id, base.ssid, A(base.password), iv0)), error: 'decifratura fallita' },
  { name: 'password di 7 byte', message: addMsg('c000000000010', 'Corta', fixed('wifi iv corta', 12), short7), error: 'password non valida' },
  { name: 'password di 64 byte', message: addMsg('c000000000011', 'Lunga', fixed('wifi iv lunga', 12), long64), error: 'password non valida' },
  { name: 'password con un byte zero', message: addMsg('c000000000012', 'Zero', fixed('wifi iv zero', 12), nul), error: 'password non valida' },
];
for (const v of addNegative) {
  const m = v.message;
  let iv, ct;
  try { iv = H(m.iv); ct = H(m.ct); } catch (e) { continue; }
  if (v.error === 'decifratura fallita' && /^[0-9a-f]*$/.test(m.ct) && iv.length === 12 && ct.length >= 16 && open(SK, m.id, m.ssid, iv, ct))
    problems.push(v.name + ': Node lo decifra');
  if (v.error === 'password non valida' && !open(SK, m.id, m.ssid, iv, ct)) problems.push(v.name + ': Node non lo decifra');
}

// ---------- wifi-forget ----------
const forgetMsg = (id, ssid, mac) => ({ t: 'wifi-forget', v: 1, id, ssid, mac: typeof mac === 'string' ? mac : hex(mac) });
const FORGET = [
  { name: 'rete qualunque', id: 'c5e6f7a8b9c0d', ssid: 'Officina' },
  { name: 'SSID UTF-8', id: 'c0a1b2c3d4e60', ssid: 'Caffè «Àrìa» ☕' },
];
const forget = FORGET.map((v) => {
  const input = joined('wifi-forget', v.id, v.ssid), mac = mac16(SK, input);
  return { name: v.name, id: v.id, ssid: v.ssid, macInput: hex(input), mac: hex(mac), message: forgetMsg(v.id, v.ssid, mac) };
});
const f0 = FORGET[0], m0 = mac16(SK, joined('wifi-forget', f0.id, f0.ssid));
const forgetNegative = [
  { name: 'mac di un altro SSID', message: forgetMsg(f0.id, 'CasaWiFi', m0), error: 'mac non valido' },
  { name: 'mac di un altro id', message: forgetMsg('c5e6f7a8b9c0e', f0.ssid, m0), error: 'mac non valido' },
  { name: 'mac maiuscolo', message: forgetMsg(f0.id, f0.ssid, hex(m0).toUpperCase()), error: 'mac non valido' },
  { name: 'mac con un\'altra SK', message: forgetMsg(f0.id, f0.ssid, mac16(SK2, joined('wifi-forget', f0.id, f0.ssid))), error: 'mac non valido' },
];

// ---------- wifi-list: la forma esatta che manda il display ----------
const listMsg = { t: 'wifi-list', v: 1, networks: [
  { ssid: 'CasaWiFi', current: true, rssi: -58, lastUsed: 1791707400000 },
  { ssid: 'Officina', current: false, rssi: -66, lastUsed: 1791448200000 },
  { ssid: 'Casa Mare', current: false, rssi: null, lastUsed: 1786178400000 },
  { ssid: 'Laboratorio', current: false, rssi: null, lastUsed: null },
] };

if (problems.length) { console.error('vettori che non tornano:\n  ' + problems.join('\n  ')); process.exit(1); }

const out = {
  $comment: 'Vettori di prova delle reti Wi-Fi (firmware 0.8.0: wifi-list, wifi-add, wifi-forget). Generato da protocol/vectors/gen-wifi-vectors.mjs con il crypto di Node (OpenSSL); il firmware (test_wifi) e il servizio Go devono riprodurre ogni byte. Non modificare a mano.',
  format: {
    hex: 'tutte le stringhe binarie sono esadecimali minuscole; un esadecimale maiuscolo o di lunghezza sbagliata vale come cifrato o mac non valido',
    SK: 'chiave di sessione della connessione autenticata (docs/M5-PERMESSI.md §2): qui flow.auth.SK di crypto-vectors.json',
    wifiAdd: 'servizio → display, solo su connessione autenticata. key = HKDF-SHA256(ikm = SK, salt = id in UTF-8, info = "calamaio-wifi"), 32 byte. iv = 12 byte casuali (nuovi a ogni messaggio). aad = "wifi-add" ␟ id ␟ ssid (UTF-8, 0x1F fra i campi, nessun 0x1F finale). ct = AES-256-GCM(key, iv, aad, password UTF-8) seguito dal tag di 16 byte. Password valida: da 8 a 63 byte, senza byte zero',
    wifiForget: 'servizio → display, solo su connessione autenticata. mac = HMAC-SHA256(SK, "wifi-forget" ␟ id ␟ ssid) troncato ai primi 16 byte (32 caratteri)',
    ack: 'il display risponde ack {id, ok, error?} con lo stesso id. Errori di wifi-add: «lista piena» (5 reti e SSID nuovo), «password non valida», «decifratura fallita», «ssid non valido» (vuoto o oltre 32 byte); senza autenticazione «display non abbinato: cambia le impostazioni dal menu del display». Errori di wifi-forget: «mac non valido», «rete sconosciuta». Un ack ok non ha error',
    wifiList: 'display → servizio dopo auth-ok e a ogni cambio della lista, della rete in uso o dopo una ricerca: networks nell\'ordine di inserimento, ogni voce con ssid, current (rete in uso), rssi (dBm, null se la rete non si è vista nell\'ultima ricerca; per la rete in uso la lettura del momento), lastUsed (ms epoch dell\'ultimo collegamento, null = mai). Mai password'
  },
  SK: hex(SK),
  otherSK: hex(SK2),
  add,
  addNegative,
  forget,
  forgetNegative,
  list: { message: listMsg, json: JSON.stringify(listMsg) },
};
writeFileSync(OUT, JSON.stringify(out, null, 1) + '\n');
console.log('scritto', OUT, '·', add.length, 'add,', addNegative.length, 'add negativi,', forget.length, 'forget,', forgetNegative.length, 'forget negativi');
