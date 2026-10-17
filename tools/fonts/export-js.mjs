#!/usr/bin/env node
// Esporta i caratteri della scheda (firmware/src/gfx/fonts/*.h, generati da gen.py) nel modulo della webapp
// service/internal/webui/static/assets/js/theme/fonts.js: stessi bitmap, avanzamenti e crenatura, così il renderer
// dei temi disegna come text.cpp. Niente browser: si leggono i .h.
//   node tools/fonts/export-js.mjs          scrive fonts.js
//   node tools/fonts/export-js.mjs --check  esce con 1 se fonts.js è vecchio
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const DIR = path.join(REPO, 'firmware', 'src', 'gfx', 'fonts');
const OUT = path.join(REPO, 'service', 'internal', 'webui', 'static', 'assets', 'js', 'theme', 'fonts.js');

const read = (f) => fs.readFileSync(path.join(DIR, f), 'utf8');
function array(src, name) {
  const m = new RegExp('\\b' + name + '\\[\\] FONT_DATA = \\{([\\s\\S]*?)\\n\\};').exec(src);
  if (!m) throw new Error(name + ' non trovato');
  return m[1].replace(/\/\/[^\n]*/g, '');
}
const nums = (s) => (s.match(/-?0x[0-9a-fA-F]+|-?\d+/g) || []).map((v) => Number(v));
const b64 = (typed) => Buffer.from(typed.buffer, typed.byteOffset, typed.byteLength).toString('base64');

function build() {
  const reg = read('f_registry.h');
  const rows = [...reg.matchAll(/\{(\d+), (\d+), (true|false), (true|false), (\d+), (\w+)_entries, \w+_glyphs, \w+_bitmap, (?:&kern_(\d+)|nullptr)\}/g)];
  if (rows.length !== 22) throw new Error('f_registry.h: ' + rows.length + ' caratteri invece di 22');
  const kern = read('f_kern.h');
  let out = '// Generato da tools/fonts/export-js.mjs dai .h del firmware: non modificare a mano.\n';
  out += 'const b64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));\n';
  out += 'const u16 = (s) => new Uint16Array(b64(s).buffer);\nconst i16 = (s) => new Int16Array(b64(s).buffer);\n';
  out += 'export const KERN_KEYS = new Uint32Array(b64(' + JSON.stringify(b64(Uint32Array.from(nums(array(kern, 'kern_keys_all'))))) + ').buffer);\n';
  out += 'export const KERN = {\n';
  for (const w of [500, 600, 700]) out += '  ' + w + ': i16(' + JSON.stringify(b64(Int16Array.from(nums(array(kern, 'kern_vals_' + w))))) + '),\n';
  out += '};\nexport const FONTS = [\n';
  for (const r of rows) {
    const [, size, weight, pix, inv, n, name, kw] = r;
    const src = read('f_' + name + '.h');
    const ent = nums(array(src, name + '_entries'));
    const gl = nums(array(src, name + '_glyphs'));
    const bm = Uint8Array.from(nums(array(src, name + '_bitmap')));
    if (ent.length !== 3 * Number(n)) throw new Error(name + ': voci ' + ent.length / 3);
    if (gl.length % 5) throw new Error(name + ': glifi non multipli di 5 numeri');
    // Glyph.off è uint16_t in C++ ma qui va in un Int16Array: oltre 32767 diventerebbe negativo
    for (let i = 0; i < gl.length; i += 5)
      if (gl[i] > 32767) throw new Error(name + ': off ' + gl[i] + ' oltre 32767 (bitmap troppo grande per Int16Array)');
    const cps = Uint16Array.from(ent.filter((_, i) => i % 3 === 0));
    const adv = Uint16Array.from(ent.filter((_, i) => i % 3 === 1));
    const first = Uint16Array.from(ent.filter((_, i) => i % 3 === 2));
    out += '  { size: ' + size + ', weight: ' + weight + ', pix: ' + pix + ', inv: ' + inv + ', kern: ' + (kw || 'null') +
      ', cps: u16(' + JSON.stringify(b64(cps)) + '), adv: u16(' + JSON.stringify(b64(adv)) + '), first: u16(' + JSON.stringify(b64(first)) +
      '), glyphs: i16(' + JSON.stringify(b64(Int16Array.from(gl))) + '), bitmap: b64(' + JSON.stringify(b64(bm)) + ') },\n';
  }
  return out + '];\n';
}

const text = build();
if (process.argv.includes('--check')) {
  const old = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '';
  if (old !== text) { console.error('fonts.js vecchio: node tools/fonts/export-js.mjs'); process.exit(1); }
  console.log('fonts.js aggiornato');
} else {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, text);
  console.log('scritto ' + path.relative(REPO, OUT) + ' (' + text.length + ' byte)');
}
