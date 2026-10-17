#!/usr/bin/env node
// Banco di parità fra qr.js (webapp) e firmware/src/gfx/qr.cpp + gfx/qr/qrcode_ricmoo.cpp: compila harness.cpp con
// clang++, codifica gli stessi testi nei due motori e scrive qr-cases.json (matrici C++ di un insieme fisso di testi)
// che tools/webui-dev/test-theme.mjs riconfronta senza bisogno di clang.
//   node tools/qr-parity/gen-fixture.mjs           confronta (fissi + 3000 casuali) e scrive il fixture
//   node tools/qr-parity/gen-fixture.mjs --check   confronta e basta; esce con 1 se il fixture è vecchio
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { encode, modulesHex } from '../../service/internal/webui/static/assets/js/theme/qr.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const OUT = path.join(HERE, 'qr-cases.json');

let seed = 4242;
const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296;
const int = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (a) => a[Math.floor(rnd() * a.length)];

const DIG = '0123456789', ALNUM = DIG + 'ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:';
const POOL = [...ALNUM, ...'abcdefghijklmnopqrstuvwxyz;=?&_#@!', 'è', 'à', '€', '·', '\u{1F600}', '中'];
const rep = (set, n) => { let s = ''; for (let k = 0; k < n; k++) s += set[k % set.length]; return s; };
// lunghezze ai confini fra versioni: capienze in byte M [14, 26, 42, 62, 84]
const EDGES = [1, 13, 14, 15, 25, 26, 27, 41, 42, 43, 61, 62, 63, 83, 84, 85];
const FIXED = ['', 'x', '0', 'A', '123456', 'HELLO WORLD', 'http://calamaio.local', 'HTTP://CALAMAIO.LOCAL/API',
  'https://github.com/ricmoo/QRCode', 'WIFI:T:WPA;S:Calamaio-AB12;P:k3x9-ab7q-mz2p;;', 'Ciao è 3 api-server', 'è', 'è'.repeat(42),
  'è'.repeat(43), '€'.repeat(28), '\u{1F600}'.repeat(21), '中文 QR', 'ab\0cd', 'x'.repeat(255), '1'.repeat(255),
  'calamaio-firmware-esp32-progetto 87%', 'Bash: npm run test', '0'.repeat(100)];
// maschere a pari penalità (trovati coi casuali): vince la prima, come il «<» del C++
FIXED.push('D4J$P3$MFLJD+JIKQ8CRQ%S1LU%FD% OHY4-AXP73%AT:8G%S.2504-Q%C4XKOSZ4XXGJ4*UI BCU:', 'eeutAg87oYm*$Mqà7LLsoNCvL8€ =dD',
  '298165189769732492640167374707259540094375225934909', '490REN0LTSLROXUX:PCJ2Q5Q/E');
for (const n of EDGES) FIXED.push('x'.repeat(n), rep(DIG, n), rep(ALNUM, n), rep('Ab9', n));

const randStr = () => {
  const kind = int(0, 3), n = int(0, 90);
  if (kind === 0) return rep([...Array(n)].map(() => pick([...DIG])).join('') || '7', n);
  if (kind === 1) return [...Array(n)].map(() => pick([...ALNUM])).join('');
  let s = ''; for (let k = 0; k < n; k++) s += pick(POOL); return s;
};

function runCpp(texts) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'calamaio-qr-'));
  const bin = path.join(dir, 'harness'), src = path.join(REPO, 'firmware', 'src');
  execFileSync('clang++', ['-std=c++17', '-O1', '-I' + src, path.join(HERE, 'harness.cpp'), path.join(src, 'gfx', 'qr.cpp'),
    path.join(src, 'gfx', 'qr', 'qrcode_ricmoo.cpp'), '-o', bin]);
  const input = texts.map((t) => Buffer.from(t, 'utf8').toString('hex') + '\n').join('');
  const lines = execFileSync(bin, { input, maxBuffer: 1 << 26 }).toString().split('\n');
  fs.rmSync(dir, { recursive: true, force: true });
  return texts.map((_, i) => {
    if (lines[i] === '?') throw new Error('qrcode_initText e qr::encode non coincidono');
    if (lines[i] === '-') return null;
    const [v, size, mask, hex] = lines[i].split(' ');
    return [Number(v), Number(size), Number(mask), hex];
  });
}
const runJs = (t) => { const q = encode(t); return q ? [q.version, q.size, q.mask, modulesHex(q)] : null; };

function compare(texts, want, label) {
  let bad = 0;
  texts.forEach((t, i) => {
    const got = runJs(t);
    if (JSON.stringify(got) !== JSON.stringify(want[i]) && bad++ < 10)
      console.log('DIFF', label, JSON.stringify(t), 'js', JSON.stringify(got), 'c++', JSON.stringify(want[i]));
  });
  console.log(label + ': ' + texts.length + ' testi, ' + bad + ' differenze');
  return bad;
}

const rand = [...Array(3000)].map(randStr);
const fxWant = runCpp(FIXED), randWant = runCpp(rand);
let bad = compare(FIXED, fxWant, 'fissi') + compare(rand, randWant, 'casuali');
// copertura: tutte le versioni e tutte le maschere nei fissi
const vs = new Set(fxWant.filter(Boolean).map((r) => r[0])), ms = new Set(fxWant.filter(Boolean).map((r) => r[2]));
console.log('versioni nei fissi: ' + [...vs].sort().join(',') + ' · maschere: ' + [...ms].sort().join(','));
if (vs.size !== 5 || ms.size !== 8) { console.error('copertura incompleta'); bad++; }
const text = JSON.stringify({ about: 'Matrici di firmware/src/gfx/qr.cpp + qrcode_ricmoo.cpp: generato da tools/qr-parity/gen-fixture.mjs, non modificare a mano. [testo UTF-8 in hex, [versione, size, maschera, moduli in hex] o null]',
  cases: FIXED.map((t, i) => [Buffer.from(t, 'utf8').toString('hex'), fxWant[i]]) }).replace(/\],\[/g, '],\n[') + '\n';
if (process.argv.includes('--check')) {
  if ((fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '') !== text) { console.error('qr-cases.json vecchio'); bad++; }
} else {
  fs.writeFileSync(OUT, text);
  console.log('scritto ' + path.relative(REPO, OUT) + ' (' + text.length + ' byte)');
}
process.exit(bad ? 1 : 0);
