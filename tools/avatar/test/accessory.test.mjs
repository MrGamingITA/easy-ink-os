// Test del formato degli accessori e del binario CACC (docs/AVATAR.md §14).
// Uso: node --test tools/avatar/test/*.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  validateAccessory, packAccessory, unpackAccessory, serializeAccessory, canonicalAccessory,
  ACC_MAX_W, ACC_MAX_H, ACC_MAX_BYTES, ACC_HEADER, accFrameBytes
} from '../lib/accessory.mjs';
import { crc32 } from '../lib/pack.mjs';

const clone = (o) => JSON.parse(JSON.stringify(o));

// Cappellino 12×6, un fotogramma: tesa in basso, origine al centro della tesa.
export const CAP = {
  format: 'calamaio-accessory', version: 1,
  id: 'prova', name: 'Prova', author: 'test', description: 'Accessorio di prova.',
  special: false, w: 12, h: 6, frames: 1, anchor: 'head-top', origin: [5, 5], outline: true,
  hidePoses: ['run'],
  pixels: [[
    '....####....',
    '...#xxxx#...',
    '..#xxxxxx#..',
    '..########..',
    '.##########.',
    '############'
  ]]
};
// Due fotogrammi, con + e ritocchi per avatar.
export const FLAG = {
  ...clone(CAP), id: 'bandiera', name: 'Bandierina', frames: 2, anchor: 'neck', origin: [0, 5], outline: false,
  hidePoses: ['error', 'idle', 'tired'],
  perAvatar: { gatto: { dx: 1, dy: -2 }, demone: { pixels: null } },
  pixels: [CAP.pixels[0], CAP.pixels[0].map((r) => r.replace(/x/g, '+'))]
};
FLAG.perAvatar.demone = { dx: -1, pixels: [CAP.pixels[0].map((r) => r.replace(/#/g, 'x')), CAP.pixels[0]] };

const errs = (a) => validateAccessory(a).errors;

test('accessorio valido a 1 e 2 fotogrammi', () => {
  assert.deepEqual(errs(CAP), []);
  assert.deepEqual(validateAccessory(CAP).warnings, []);
  assert.deepEqual(errs(FLAG), []);
  assert.equal(ACC_MAX_W, 14); assert.equal(ACC_MAX_H, 10);
});

test('errori di struttura', () => {
  const cases = [
    ['format', (a) => { a.format = 'calamaio-avatar'; }, /format/],
    ['version', (a) => { a.version = 2; }, /version/],
    ['id', (a) => { a.id = 'Cappello Rosso'; }, /id/],
    ['name lungo', (a) => { a.name = 'è'.repeat(13); }, /name/],
    ['description', (a) => { a.description = 'x'.repeat(121); }, /description/],
    ['special', (a) => { a.special = 'sì'; }, /special/],
    ['w > 14', (a) => { a.w = 15; a.pixels = [a.pixels[0].map((r) => r + '...')]; }, /w: da 1 a 14/],
    ['h > 10', (a) => { a.h = 11; a.pixels = [[...a.pixels[0], ...Array(5).fill('.'.repeat(12))]]; }, /h: da 1 a 10/],
    ['frames', (a) => { a.frames = 3; }, /frames deve essere 1 o 2/],
    ['simbolo ignoto', (a) => { a.pixels[0][2] = '..#xxoxxx#..'; }, /pixels\[0\] riga 2: simboli ammessi solo \. # \+ x/],
    ['righe', (a) => { a.pixels[0] = a.pixels[0].slice(1); }, /pixels\[0\]: servono 6 righe/],
    ['colonne', (a) => { a.pixels[0][1] += '.'; }, /pixels\[0\] riga 1: servono 12 simboli/],
    ['fotogrammi dei pixel', (a) => { a.pixels.push(a.pixels[0]); }, /pixels: servono 1 fotogrammi, trovati 2/],
    ['ancora ignota', (a) => { a.anchor = 'tail'; }, /anchor: uno tra head-top, eyes, mouth, neck, hand/],
    ['origin fuori', (a) => { a.origin = [12, 0]; }, /origin: \(12,0\) fuori dall'accessorio 12×6/],
    ['origin negativa', (a) => { a.origin = [0, -1]; }, /origin/],
    ['origin non intera', (a) => { a.origin = [1.5, 2]; }, /origin/],
    ['outline', (a) => { a.outline = 1; }, /outline/],
    ['posa nascosta ignota', (a) => { a.hidePoses = ['ballo']; }, /hidePoses: posa sconosciuta ballo/],
    ['posa nascosta doppia', (a) => { a.hidePoses = ['run', 'run']; }, /hidePoses: run ripetuta/],
    ['campo extra', (a) => { a.colore = 'rosso'; }, /campo sconosciuto: colore/],
    ['vuoto', (a) => { a.pixels = [a.pixels[0].map((r) => r.replace(/[^.]/g, '.'))]; }, /pixels\[0\]: accessorio vuoto/],
    ['ritocco id', (a) => { a.perAvatar = { 'Gatto!': { dx: 1 } }; }, /perAvatar: id avatar "Gatto!" non valido/],
    ['ritocco vuoto', (a) => { a.perAvatar = { gatto: {} }; }, /perAvatar\.gatto: serve dx, dy o pixels/],
    ['ritocco dx', (a) => { a.perAvatar = { gatto: { dx: 40 } }; }, /perAvatar\.gatto\.dx: intero da -27 a 27/],
    ['ritocco campo', (a) => { a.perAvatar = { gatto: { dz: 1 } }; }, /perAvatar\.gatto: campo sconosciuto dz/],
    ['ritocco pixel', (a) => { a.perAvatar = { gatto: { pixels: [['..']] } }; }, /perAvatar\.gatto\.pixels\[0\]: servono 6 righe/]
  ];
  for (const [name, mut, re] of cases) {
    const a = clone(CAP); mut(a);
    const e = errs(a);
    assert.ok(e.some((m) => re.test(m)), `${name}: ${e.join(' | ')}`);
  }
});

test('avvisi: fotogrammi uguali, tutte le pose nascoste', () => {
  const a = clone(FLAG); a.pixels[1] = a.pixels[0].slice();
  assert.match(validateAccessory(a).warnings.join('\n'), /i 2 fotogrammi sono uguali/);
  const b = clone(CAP); b.hidePoses = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy', 'run'];
  assert.match(validateAccessory(b).warnings.join('\n'), /nascosto in tutte le pose/);
});

test('CACC: andata e ritorno identica, ≤ 1 KB', () => {
  for (const a of [CAP, FLAG]) {
    const buf = packAccessory(a);
    assert.ok(buf.length <= ACC_MAX_BYTES);
    const back = unpackAccessory(buf);
    const want = clone(a); want.author = '(dal binario)'; want.description = '';
    if (!want.perAvatar) want.perAvatar = {};
    assert.deepEqual(back, want);
    assert.equal(canonicalAccessory(back), canonicalAccessory(a));
  }
  assert.equal(ACC_MAX_BYTES, 1024);
  // dimensione: intestazione + pixel; i ritocchi in più
  assert.equal(packAccessory(CAP).length, ACC_HEADER + accFrameBytes(12, 6));
  assert.equal(accFrameBytes(12, 6), 18);
  assert.equal(accFrameBytes(14, 10), 35);
});

test('CACC: intestazione e codifica dei simboli', () => {
  const buf = packAccessory(FLAG);
  assert.equal(buf.toString('ascii', 0, 4), 'CACC');
  assert.equal(buf[4], 1); assert.equal(buf[5], ACC_HEADER); assert.equal(buf.readUInt16LE(6), buf.length);
  assert.equal(buf.readUInt32LE(8), crc32(buf, 12));
  assert.equal(buf[12], 12); assert.equal(buf[13], 6); assert.equal(buf[14], 2);
  assert.equal(buf[15], 3);                  // neck
  assert.equal(buf[16], 0); assert.equal(buf[17], 5);
  assert.equal(buf[18], 0);                  // outline no, special no
  assert.equal(buf[19], 2);                  // due ritocchi
  assert.equal(buf.readUInt16LE(20), (1 << 4) | (1 << 5) | (1 << 7));   // error, idle, tired
  assert.equal(buf.readUInt16LE(22), 18);
  assert.equal(buf.toString('ascii', 24, 32), 'bandiera');
  // pixel: . 0, # 1, + 2, x 3 — primo pixel nei bit alti, come il CAVT
  const one = clone(CAP); one.pixels[0][0] = '#+x.........';
  const b = packAccessory(one);
  assert.equal(b[ACC_HEADER], 0b01101100);
  const flags = clone(CAP); flags.special = true;
  assert.equal(packAccessory(flags)[18], 0b11);
});

test('CACC: rifiuta binari rovinati', () => {
  const buf = packAccessory(FLAG);
  const flip = Buffer.from(buf); flip[buf.length - 3] ^= 1;
  assert.throws(() => unpackAccessory(flip), /checksum/);
  assert.throws(() => unpackAccessory(buf.subarray(0, buf.length - 1)), /lunghezza/);
  const magic = Buffer.from(buf); magic[3] = 0x41;
  assert.throws(() => unpackAccessory(magic), /CACC/);
  const ver = Buffer.from(buf); ver[4] = 2;
  assert.throws(() => unpackAccessory(ver), /versione/);
  const anc = Buffer.from(buf); anc[15] = 7; anc.writeUInt32LE(crc32(anc, 12), 8);
  assert.throws(() => unpackAccessory(anc), /ancora/);
  const big = Buffer.from(buf); big[12] = 15; big.writeUInt32LE(crc32(big, 12), 8);
  assert.throws(() => unpackAccessory(big), /dimensioni/);
  const orig = Buffer.from(buf); orig[17] = 6; orig.writeUInt32LE(crc32(orig, 12), 8);
  assert.throws(() => unpackAccessory(orig), /origin/);
  const mask = Buffer.from(buf); mask.writeUInt16LE(1 << 12, 20); mask.writeUInt32LE(crc32(mask, 12), 8);
  assert.throws(() => unpackAccessory(mask), /bit di posa/);
});

test('CACC: oltre 1 KB rifiutato in validazione', () => {
  const a = clone(FLAG);
  a.w = 14; a.h = 10;
  const fr = Array(10).fill('#'.repeat(14));
  a.pixels = [fr, fr.map((r) => r.replace(/#/g, 'x'))];
  a.perAvatar = {};
  for (let i = 0; i < 12; i++) a.perAvatar['avatar-' + i] = { pixels: [fr, fr] };
  assert.ok(errs(a).some((m) => /binario CACC di \d+ byte: il massimo è 1024/.test(m)), errs(a).join(' | '));
});

test('serializeAccessory: un fotogramma per blocco, rileggibile', () => {
  const s = serializeAccessory(FLAG);
  assert.deepEqual(JSON.parse(s), FLAG);
  assert.match(s, /\n {6}"....####...."/);   // una riga del disegno per riga di testo
});
