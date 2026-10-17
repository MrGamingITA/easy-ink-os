#!/usr/bin/env node
// Banco di parità fra text.js (webapp) e firmware/src/gfx/text.cpp: compila harness.cpp con clang++, fa girare
// gli stessi casi nei due motori e scrive text-cases.json, i risultati C++ di un sottoinsieme rappresentativo che
// tools/webui-dev/test-theme.mjs riconfronta senza bisogno di clang.
//   node tools/fonts/parity/gen-fixture.mjs           confronta (casi del fixture + 5000 casuali) e scrive il fixture
//   node tools/fonts/parity/gen-fixture.mjs --check   confronta e basta; esce con 1 se il fixture è vecchio
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { FONTS } from '../../../service/internal/webui/static/assets/js/theme/fonts.js';
import { runJs } from './run-js.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..', '..');
const OUT = path.join(HERE, 'text-cases.json');

// generatore deterministico (LCG): stessi casi a ogni giro
let seed = 12345;
const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296;
const int = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (a) => a[Math.floor(rnd() * a.length)];

const POOL = [...'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,:;!?-_/%()"\'àèéìòùÈ·›″−…✓✗▲▼◀▶',
  'fi', 'fl', 'ffi', 'AV', 'To', 'Wa', '  ', '\t', '\n', ' ', ' ', '\u{1F600}', 'ß', 'Ω', '中', '　'];
const FIXED = ['', 'CALAMAIO OS', 'PERMESSO', 'Bash: npm run test', 'api-server', 'calamaio-firmware-esp32-progetto',
  'CONTESTO: CALAMAIO-FIRMWARE-ESP32 87%', 'Ultimo dato alle 14:32 di oggi', '  uno   due  ', '   ', 'parolalunghissimasenzaspazi corta',
  'fi fl ffi office flow', 'AVATAR Tower Wave', '\u{1F600}', '中Ω?', '◀ ▶ ✓ ✗ ▲ ▼ · › ″ − …', 'Già più però così', 'a\tb\nc\rd\fe',
  'è'.repeat(80), 'è'.repeat(200), 'x'.repeat(300), 'parola '.repeat(60)];
const COMBOS = FONTS.map((f) => [f.size, f.weight, f.pix ? 1 : 0])
  .concat([[9, 700, 0], [17, 700, 0], [30, 600, 0], [8, 600, 1], [8, 500, 1], [10, 400, 0], [12, 900, 0], [60, 700, 0], [7, 400, 1]]);
const randStr = (max = 40) => { let s = ''; for (let k = int(1, max); k > 0; k--) s += pick(POOL); return s; };

function fixtureCases() {
  const cs = [];
  for (const [z, w, p] of COMBOS) cs.push(['H', z, w, p, 0, 0, 0, 0, 0, '']);
  // ogni combinazione (anche di ripiego) con i 3 allineamenti, inchiostro e carta, x anche negative
  COMBOS.forEach(([z, w, p], i) => {
    for (let a = 0; a < 3; a++) for (let ink = 0; ink < 2; ink++)
      cs.push(['D', z, w, p, pick([-37, -3, 0, 7, 125, 249]), int(-5, 110), a, ink, 0, FIXED[(i * 6 + a * 2 + ink) % FIXED.length]]);
  });
  // stringhe fisse: larghezze, fit ai bordi, a capo ai bordi, disegno con max
  for (const s of FIXED) {
    for (const [z, w, p] of [[13, 700, 0], [10, 500, 0], [8, 400, 1]]) cs.push(['W', z, w, p, 0, 0, 0, 0, 0, s]);
    for (const max of [0, 1, 5, 40, 150, 9999]) cs.push(['F', ...pick(COMBOS), max, 0, 0, 0, 0, s]);
    for (const [lines, wd] of [[1, 0], [2, 30], [6, 100], [8, 2000]]) cs.push(['L', ...pick(COMBOS), lines, wd, 0, 0, 0, s]);
    cs.push(['D', ...pick(COMBOS), int(-40, 250), int(0, 100), int(0, 2), int(0, 1), pick([1, 30, 80, 200]), s]);
  }
  // un po' di casi casuali per ogni operazione
  for (let i = 0; i < 60; i++) {
    const s = randStr();
    cs.push(['W', ...pick(COMBOS), 0, 0, 0, 0, 0, s]);
    cs.push(['F', ...pick(COMBOS), int(0, 259), 0, 0, 0, 0, s]);
    cs.push(['L', ...pick(COMBOS), int(1, 8), int(0, 259), 0, 0, 0, s]);
    cs.push(['D', ...pick(COMBOS), int(-40, 260), int(-10, 130), int(0, 2), int(0, 1), rnd() < 0.3 ? int(0, 200) : 0, s]);
  }
  return cs;
}
function randomCases(n) {
  const cs = [];
  for (let i = 0; i < n; i++) {
    const s = rnd() < 0.1 ? pick(FIXED) : randStr(), op = pick(['W', 'F', 'L', 'D', 'D']);
    if (op === 'W') cs.push(['W', ...pick(COMBOS), 0, 0, 0, 0, 0, s]);
    else if (op === 'F') cs.push(['F', ...pick(COMBOS), int(0, 259), 0, 0, 0, 0, s]);
    else if (op === 'L') cs.push(['L', ...pick(COMBOS), int(1, 8), int(0, 259), 0, 0, 0, s]);
    else cs.push(['D', ...pick(COMBOS), int(-40, 300), int(-10, 130), int(0, 2), int(0, 1), rnd() < 0.3 ? int(0, 200) : 0, s]);
  }
  return cs;
}

// risultati di text.cpp
function runCpp(cases) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'calamaio-parity-'));
  const bin = path.join(dir, 'harness');
  execFileSync('clang++', ['-std=c++17', '-O1', '-I' + path.join(REPO, 'firmware', 'src'), path.join(HERE, 'harness.cpp'),
    path.join(REPO, 'firmware', 'src', 'gfx', 'text.cpp'), '-o', bin]);
  const hex = (s) => Buffer.from(s, 'utf8').toString('hex');
  const unhex = (h) => Buffer.from(h, 'hex').toString('utf8');
  const input = cases.map((c) => c.slice(0, 9).join(' ') + ' ' + hex(c[9]) + '\n').join('');
  const lines = execFileSync(bin, { input, maxBuffer: 1 << 26 }).toString().split('\n');
  fs.rmSync(dir, { recursive: true, force: true });
  return cases.map((c, i) => {
    const t = lines[i].split(' ');
    if (c[0] === 'H') return Number(t[0]);
    if (c[0] === 'W' || c[0] === 'D') return [Number(t[0]), Number(t[1])];
    if (c[0] === 'F') return unhex(t[0] || '');
    return t.slice(1).map((x) => unhex(x.slice(0, -1)));  // L: «n riga. riga.»
  });
}

function compare(cases, want, label) {
  let bad = 0;
  cases.forEach((c, i) => {
    const got = runJs(c);
    if (JSON.stringify(got) !== JSON.stringify(want[i]) && bad++ < 10)
      console.log('DIFF', label, JSON.stringify(c), 'js', JSON.stringify(got), 'c++', JSON.stringify(want[i]));
  });
  console.log(label + ': ' + cases.length + ' casi, ' + bad + ' differenze');
  return bad;
}

const fx = fixtureCases(), rand = randomCases(5000);
const fxWant = runCpp(fx);
let bad = compare(fx, fxWant, 'fixture') + compare(rand, runCpp(rand), 'casuali');
const text = JSON.stringify({ about: 'Risultati di firmware/src/gfx/text.cpp: generato da tools/fonts/parity/gen-fixture.mjs, non modificare a mano.',
  cases: fx.map((c, i) => [...c, fxWant[i]]) }).replace(/\],\[/g, '],\n[') + '\n';
if (process.argv.includes('--check')) {
  if ((fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '') !== text) { console.error('text-cases.json vecchio'); bad++; }
} else {
  fs.writeFileSync(OUT, text);
  console.log('scritto ' + path.relative(REPO, OUT) + ' (' + text.length + ' byte)');
}
process.exit(bad ? 1 : 0);
