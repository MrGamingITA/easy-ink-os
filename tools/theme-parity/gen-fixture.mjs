#!/usr/bin/env node
// Banco di parità fra values.js (webapp) e firmware/src/core/theme_values.cpp: compila harness.cpp con clang++ insieme
// a model.cpp, clock.cpp, workday.cpp, pet.cpp, strbuf.cpp e theme_format.cpp, fa le stesse domande ai due motori e
// scrive values-cases.json (le risposte C++ di un insieme fisso di modelli) che tools/webui-dev/test-theme.mjs
// riconfronta senza bisogno di clang. Serve l'ArduinoJson di firmware/.pio/libdeps/native (pet.h lo include).
//   node tools/theme-parity/gen-fixture.mjs           confronta (fissi + 1500 casuali) e scrive il fixture
//   node tools/theme-parity/gen-fixture.mjs --check   confronta e basta; esce con 1 se il fixture è vecchio
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import * as V from '../../service/internal/webui/static/assets/js/theme/values.js';
import { runJs, hex } from './run-js.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const OUT = path.join(HERE, 'values-cases.json');

let seed = 9090;
const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296;
const int = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (a) => a[Math.floor(rnd() * a.length)];
const bool = (p = 0.5) => rnd() < p;

const NOW = 1791707400000, LOCAL = Date.UTC(2026, 9, 11, 10, 30, 5);
const POOL = [...'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,:-_/%', 'à', 'è', 'È', 'À', 'Þ', 'þ', '×', '÷', 'ß', 'ÿ',
  'Ÿ', 'ẞ', '·', '—', '−', '✓', '€', '中', '\u{1F600}', '{', '}'];
const randStr = (max) => { let s = ''; for (let k = int(0, max); k > 0; k--) s += pick(POOL); return s; };
const COSTS = [0, 1.2, 0.125, 0.375, 0.625, 0.875, 1.005, 2.675, 0.005, 0.015, 12.345, 1e22, 123456.785, 0.1, 99.995, 4503599627370495.5,
  1e-7, 7.0625];
const randNum = (lo, hi, dec) => Math.round((lo + rnd() * (hi - lo)) * 10 ** dec) / 10 ** dec;

function randModel() {
  const n = pick([0, 1, 1, 2, 3, 6, 7]);
  const local = pick([LOCAL, LOCAL, Date.UTC(2030, 1, 28, 23, 59, 59), Date.UTC(1969, 11, 31, 23, 0, 0), int(0, 4e12), -int(0, 4e12)]);
  const now = NOW + int(-1e6, 1e6);
  return {
    now, local, wallOk: bool(0.85),
    sel: pick([-1, -1, 0, 1, 2, 5, 7]),
    sys: { batt: pick([78, 79.5, 80, 0, 100, 14.49, randNum(0, 100, 2)]), usb: bool(), wifi: bool(0.8), ssid: pick(['CasaWiFi', '', randStr(40)]),
      rssi: -58, pc: bool(0.8), pcHost: pick(['', 'pc-sviluppo.local', randStr(60)]) },
    set: { night: bool(0.3), game: bool(), workStart: int(0, 1439), workEnd: pick([1080, int(0, 1439), 5]) },
    work: (() => { const on = bool(); const end = local + int(-5 * 3600e3, 10 * 3600e3);
      return { on, endLocal: end, fromLocal: end - int(0, 12 * 3600e3), doneUntilLocal: pick([0, local + int(-1e6, 1e7)]) }; })(),
    stats: { tasks: int(0, 40), linesAdd: int(0, 100000), linesDel: int(0, 5000), cost: bool(0.6) ? pick(COSTS) : randNum(0, 200, int(0, 4)) },
    limits: { h5: bool(0.3) ? null : pick([36.5, 90, 89.5, 89.49, 0, 120, randNum(0, 110, 1)]), d7: bool(0.4) ? null : randNum(0, 100, 2) },
    pet: { known: bool(), on: bool(), level: int(0, 30), xp: int(0, 5000), xpLevelStart: int(0, 100), xpNext: int(100, 6000), energy: int(0, 100), tired: bool() },
    co2: bool(0.4) ? null : pick([620, 1000, 1000.4, 1000.5, 1500, randNum(300, 2500, 1)]),
    bat: { cal: { valid: bool(), active: randNum(0, 6, 3), idle: randNum(0, 1, 3) }, hasSleep: bool(), sleep: randNum(0, 1, 3),
      mix: [int(0, 100), int(0, 100), int(0, 100)], trend: pick([0, 0.01, 0.011, randNum(0, 3, 3)]) },
    sessions: [...Array(n)].map(() => ({
      name: pick(['api-server', randStr(12), randStr(40)]), state: pick(V.STATES), detail: pick(['Bash: npm run test', '', randStr(120)]),
      summary: pick(['', '', 'Guida aggiornata', randStr(150)]), ago: pick([12 * 60000, 0, 29999, 30000, 89999, 90000, 3599999, int(-6e5, 4e7)]),
      ctx: pick([42, 79, 80, 100, int(0, 100)]), subs: pick([0, 0, 1, 2, int(0, 9)]), test: pick([null, null, 'ok', 'ko']),
      done: int(0, 12), total: pick([0, 7, int(0, 12)]) })),
  };
}

const FIXED = [
  { now: NOW, local: LOCAL, sys: { batt: 78 }, sessions: [] },
  { now: NOW, local: LOCAL, sys: { batt: 78, ssid: 'CasaWiFi' }, sessions: [{ name: 'àbc-ÈFG', state: 'working', detail: 'Bash: npm run test', ago: 720000, ctx: 42, done: 3, total: 7 },
    { name: 'sessione-1', state: 'permission', detail: 'x', ago: 0, ctx: 85, subs: 1, test: 'ko' }] },
  { now: NOW, local: LOCAL, sel: 1, sessions: [{ name: 'è'.repeat(40), state: 'done', summary: 'è'.repeat(63), detail: 'd' }, { name: 'a\uD800b', state: 'idle', detail: '\uDFFFz' }] },
  { now: NOW, local: LOCAL, sessions: [{ name: 'x', state: 'compact', detail: 'ß×÷ÿàþÀÞ'.repeat(20), summary: 'Ÿẞ' }], limits: { h5: 95 }, co2: 1200 },
  ...COSTS.map((cost) => ({ now: NOW, local: LOCAL, stats: { cost }, sessions: [] })),
  ...[-0.125, -0.001, -2.675].map((cost) => ({ now: NOW, local: LOCAL, stats: { cost }, sessions: [] })),
];

function queries(m) {
  const mm = V.makeModel(m), n = mm.sessions.length, q = [];
  for (const sc of [1, 2]) for (const r of [0, 1]) q.push(['r', sc, r]);
  for (let w = 0; w < 4; w++) q.push(['w', w]);
  for (const sc of [1, 2]) for (const vr of [0, 1]) for (let r = 0; r < 5; r++) for (const row of [-1, n - 1]) q.push(['s', sc, vr, r, row]);
  for (let k = 0; k < n; k++) q.push(['p', k]);
  for (const sc of [1, 2]) for (const vr of [0, 1]) for (let id = 0; id <= 10; id++) q.push(['v', sc, vr, 0, id, -1]);
  for (let k = -1; k < n; k++) for (let id = 0x40; id <= 0x4a; id++) q.push(['v', 1, 1, 0, id, k]);
  for (let id = 0x80; id <= 0x94; id++) q.push(['v', 1, 1, 0, id, -1]);
  q.push(['v', 1, 1, 0, 0x96, -1]);
  const b = Math.floor(mm.sys.batt);
  const args = { 1: [0, 1], 2: [1, b, b + 1, Math.floor(mm.sys.batt + 0.5), 100], 3: [0, 1], 4: [0, 1], 5: [0, 1], 6: [0, 1], 7: [0, 1],
    8: [0, 1, 2, 6], 9: [0, 1, 2, 6], 10: [0, 1, 2, 3, 4, 5, 6], 11: [0, 1], 0: [0], 12: [1] };
  for (const [op, as] of Object.entries(args)) for (const a of as) {
    if (+op === 10) for (let k = -1; k < n; k++) q.push(['c', 10, a, 0, k]);
    else if (+op === 11) for (const fr of [0, 1, -3, 7, -2]) q.push(['c', 11, a, fr, -1]);
    else q.push(['c', +op, a, 0, n ? 0 : -1]);
  }
  const ids = [...Array(10).keys(), ...Array.from({ length: 10 }, (_, i) => 0x40 + i), ...Array.from({ length: 21 }, (_, i) => 0x80 + i)];
  for (let t = 0; t < 5; t++) {
    const segs = [...Array(int(1, 16))].map(() => (bool(0.35) ? { lit: randStr(int(0, 1) ? 8 : 90) || 'a', filter: int(0, 2) } : { val: pick(ids), filter: int(0, 2) }));
    q.push(['x', pick([1, 2]), int(0, 1), int(-2, 2), int(-1, n - 1), int(0, 1), segs]);
  }
  q.push(['f']);
  return q;
}

// Righe per harness.cpp: testi grezzi (li tronca scopy), numeri già normalizzati da makeModel.
const B = (v) => (v ? 1 : 0);
function lines(c) {
  const m = V.makeModel(c.m), raw = c.m, rs = raw.sys || {}, out = [];
  const L = m.limits, P = m.pet, T = m.bat;
  out.push(['M', m.now, m.local, B(m.wallOk), m.sys.batt, B(m.sys.usb), B(m.sys.wifi), hex(rs.ssid || ''), B(m.sys.pc), hex(rs.pcHost || ''),
    B(m.set.night), B(m.set.game), m.set.workStart, m.set.workEnd, m.ui.sel, m.ui.selUntil, B(m.work.on), m.work.endLocal, m.work.fromLocal,
    m.work.doneUntilLocal, m.stats.tasks, m.stats.linesAdd, m.stats.linesDel, m.stats.cost, B(L.h5 !== null), L.h5 ?? 0, B(L.d7 !== null), L.d7 ?? 0,
    B(P.known), B(P.on), P.level, P.xp, P.xpLevelStart, P.xpNext, P.energy, B(P.tired), B(m.co2 !== null), m.co2 ?? 0,
    B(T.cal.valid), T.cal.active, T.cal.idle, B(T.hasSleep), T.sleep, ...T.mix, T.trend].join(' '));
  m.sessions.forEach((s, k) => {
    const r = raw.sessions[k];
    out.push(['S', hex(r.name || ''), V.STATES.indexOf(s.state), hex(r.detail || ''), hex(r.summary || ''), s.since, s.ctx, s.subs,
      s.test === 'ok' ? 1 : s.test === 'ko' ? 2 : 0, s.done, s.total].join(' '));
  });
  for (const q of c.q) {
    if (q[0] !== 'x') { out.push(['Q', ...q].join(' ')); continue; }
    const segs = q[6].flatMap((s) => (s.lit !== undefined ? [0, s.filter, hex(V.utf8Cut(s.lit, 255))] : [1, s.filter, s.val]));
    out.push(['Q', ...q.slice(0, 6), q[6].length, ...segs].join(' '));
  }
  return out;
}

function runCpp(cases) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'calamaio-values-'));
  const bin = path.join(dir, 'harness'), src = path.join(REPO, 'firmware', 'src');
  const aj = path.join(REPO, 'firmware', '.pio', 'libdeps', 'native', 'ArduinoJson', 'src');
  if (!fs.existsSync(aj)) throw new Error('manca ' + aj + ': in firmware/ esegui una volta «pio test -e native -f test_theme_values»');
  execFileSync('clang++', ['-std=c++17', '-O1', '-I' + src, '-I' + aj, path.join(HERE, 'harness.cpp'),
    ...['theme_values', 'model', 'clock', 'workday', 'pet', 'strbuf', 'theme_format'].map((f) => path.join(src, 'core', f + '.cpp')), '-o', bin]);
  const input = cases.flatMap(lines).join('\n') + '\n';
  const res = execFileSync(bin, { input, maxBuffer: 1 << 28 }).toString().split('\n');
  fs.rmSync(dir, { recursive: true, force: true });
  let at = 0;
  return cases.map((c) => c.q.map(() => res[at++]));
}

function compare(cases, want, label) {
  let bad = 0, n = 0;
  cases.forEach((c, i) => {
    const got = runJs(c);
    got.forEach((g, k) => {
      n++;
      if (g !== want[i][k] && bad++ < 10)
        console.log('DIFF', label, i, JSON.stringify(c.q[k]), 'js', Buffer.from(g === '-' ? '' : g, 'hex').toString() || g, 'c++', Buffer.from(want[i][k] === '-' ? '' : want[i][k], 'hex').toString() || want[i][k]);
    });
  });
  console.log(label + ': ' + cases.length + ' modelli, ' + n + ' risposte, ' + bad + ' differenze');
  return bad;
}

const mk = (m) => ({ m, q: queries(m) });
const fixed = [...FIXED, ...[...Array(16)].map(randModel)].map(mk);
const rand = [...Array(1500)].map(() => mk(randModel()));
const fxWant = runCpp(fixed), randWant = runCpp(rand);
const bad = compare(fixed, fxWant, 'fissi') + compare(rand, randWant, 'casuali');
const text = JSON.stringify({ about: 'Risposte di firmware/src/core/theme_values.cpp: generato da tools/theme-parity/gen-fixture.mjs, non modificare a mano. [{m: modello, q: domande (run-js.mjs), want: risposte C++}]',
  cases: fixed.map((c, i) => ({ ...c, want: fxWant[i] })) }).replace(/\},\{"m"/g, '},\n{"m"') + '\n';
let stale = 0;
if (process.argv.includes('--check')) {
  if ((fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '') !== text) { console.error('values-cases.json vecchio'); stale = 1; }
} else {
  fs.writeFileSync(OUT, text);
  console.log('scritto ' + path.relative(REPO, OUT) + ' (' + text.length + ' byte)');
}
process.exit(bad || stale ? 1 : 0);
