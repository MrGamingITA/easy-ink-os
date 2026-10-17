#!/usr/bin/env node
// Genera protocol/vectors/pad-report-v1.json: report v1 del telefono come controller (spec app iPhone §2.1).
// Implementazione indipendente da firmware (core/pad_phone, test_pad_phone) e app (CalamaioKit/PadReport,
// PadReportTests), che leggono lo stesso file. Uso, dalla radice del repo:
//   node protocol/vectors/gen-pad-vectors.mjs           scrive il file
//   node protocol/vectors/gen-pad-vectors.mjs --check   esce con 1 se il file non è quello che scriverebbe
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, 'pad-report-v1.json');

// stessi bit di pad::State (firmware/src/core/pad.h)
const BITS = { A: 0, B: 1, X: 2, Y: 3, LB: 4, RB: 5, View: 6, Menu: 7, Guide: 8, LS: 9, RS: 10, Share: 11,
  Up: 12, Down: 13, Left: 14, Right: 15 };
const mask = (...names) => names.reduce((m, n) => m | (1 << BITS[n]), 0);

function enc({ ver = 1, seq = 0, buttons = 0, lx = 0, ly = 0, flags = 1, rsv = 0 }) {
  return Buffer.from([ver, seq, buttons & 0xff, (buttons >> 8) & 0xff, lx & 0xff, ly & 0xff, flags, rsv]).toString('hex');
}

// Regole della scheda: lunghezza 8 e versione 1, levette limitate a ±100, secondo piano = tutto rilasciato,
// rsv e bit di flags sconosciuti ignorati.
function decode(hex) {
  const b = Buffer.from(hex, 'hex');
  if (b.length !== 8) return { valid: false, reason: 'lunghezza' };
  if (b[0] !== 1) return { valid: false, reason: 'versione' };
  const clamp = (v) => Math.max(-100, Math.min(100, v));
  const fg = (b[6] & 1) === 1;
  return { valid: true, seq: b[1], buttons: fg ? b.readUInt16LE(2) : 0,
    lx: fg ? clamp(b.readInt8(4)) : 0, ly: fg ? clamp(b.readInt8(5)) : 0, fg };
}

const valid = [
  ['vuoto', { seq: 0 }, true],
  ['A', { seq: 1, buttons: mask('A') }, true],
  ['A+B', { seq: 2, buttons: mask('A', 'B') }, true],
  ['su-sinistra', { seq: 3, buttons: mask('Up', 'Left') }, true],
  ['start = Menu', { seq: 4, buttons: mask('Menu') }, true],
  ['select = View', { seq: 5, buttons: mask('View') }, true],
  ['guida', { seq: 6, buttons: mask('Guide') }, true],
  ['giù-destra', { seq: 7, buttons: mask('Down', 'Right') }, true],
  ['tutti i bit', { seq: 8, buttons: 0xffff }, false],
  ['levetta ai bordi', { seq: 9, lx: -100, ly: 100 }, true],
  ['levetta fuori scala', { seq: 10, lx: 127, ly: -128 }, false],
  ['levetta 101 limitata', { seq: 14, lx: 101, ly: -101 }, false],
  ['secondo piano con levette', { seq: 15, buttons: mask('A'), lx: 50, ly: -50, flags: 0 }, false],
  ['flags 0x80 senza primo piano', { seq: 16, buttons: mask('B'), flags: 0x80 }, false],
  ['seq 255', { seq: 255, buttons: mask('B') }, true],
  ['secondo piano con A', { seq: 11, buttons: mask('A'), flags: 0 }, false],
  ['secondo piano vuoto', { seq: 12, flags: 0 }, true],
  ['rsv e flag sconosciuti', { seq: 13, buttons: mask('A'), flags: 0x81, rsv: 0x5a }, false],
];
const invalid = [
  ['vuoto 0 byte', ''],
  ['corto 7 byte', enc({}).slice(0, 14)],
  ['lungo 9 byte', enc({}) + '00'],
  ['versione 0', enc({ ver: 0 })],
  ['versione 2', enc({ ver: 2 })],
  ['versione 2 e corto 7 byte', enc({ ver: 2 }).slice(0, 14)],
];

const cases = [];
for (const [name, fields, encode] of valid) {
  const hex = enc(fields);
  const d = decode(hex);
  if (!d.valid) throw new Error(`${name}: dovrebbe essere valido`);
  // un caso «encode» deve essere esattamente quello che l'app produce dai campi decodificati
  if (encode && enc({ seq: d.seq, buttons: d.buttons, lx: d.lx, ly: d.ly, flags: d.fg ? 1 : 0 }) !== hex)
    throw new Error(`${name}: encode:true ma i campi decodificati non riproducono l'hex`);
  cases.push({ name, hex, valid: true, encode, seq: d.seq, buttons: d.buttons, lx: d.lx, ly: d.ly, fg: d.fg });
}
for (const [name, hex] of invalid) {
  const d = decode(hex);
  if (d.valid) throw new Error(`${name}: dovrebbe essere scartato`);
  cases.push({ name, hex, valid: false, reason: d.reason });
}
const text = JSON.stringify({ format: 'calamaio-pad-report', version: 1, length: 8, bits: BITS, cases }, null, 1) + '\n';

if (process.argv.includes('--check')) {
  const old = readFileSync(OUT, 'utf8');
  if (old !== text) { console.error('pad-report-v1.json non aggiornato: rigenerarlo'); process.exit(1); }
  console.log(`pad-report-v1.json aggiornato (${cases.length} casi)`);
} else {
  writeFileSync(OUT, text);
  console.log(`scritto ${OUT} (${cases.length} casi)`);
}
