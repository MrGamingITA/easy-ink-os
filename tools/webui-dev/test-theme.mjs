// Prove del renderer dei temi (docs/TEMI.md): caratteri, testo, primitive, QR, valori, abbassamento, disegno.
//   node test-theme.mjs
import { FONTS } from '../../service/internal/webui/static/assets/js/theme/fonts.js';
import * as Tx from '../../service/internal/webui/static/assets/js/theme/text.js';
import * as Pr from '../../service/internal/webui/static/assets/js/theme/prims.js';
import { Bitmap, Swap, Null, crc32 } from '../../service/internal/webui/static/assets/js/theme/surface.js';
import * as Qr from '../../service/internal/webui/static/assets/js/theme/qr.js';
import * as V from '../../service/internal/webui/static/assets/js/theme/values.js';
import * as G from '../../service/internal/webui/static/assets/js/theme/glyphs.js';
import { lowerTheme } from '../../service/internal/webui/static/assets/js/theme/lower.js';
import { renderScreen } from '../../service/internal/webui/static/assets/js/theme/render.js';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runJs } from '../fonts/parity/run-js.mjs';
import { runJs as runValues } from '../theme-parity/run-js.mjs';

let fail = 0;
const check = (ok, msg) => { if (!ok) { fail++; console.log('FAIL', msg); } };
const eq = (a, b, msg) => check(a === b, msg + ': ' + JSON.stringify(a) + ' invece di ' + JSON.stringify(b));

// 1. caratteri: le 22 combinazioni di f_registry.h
eq(FONTS.length, 22, 'caratteri');
check(Tx.hasFont(46, 700, false) && Tx.hasFont(8, 400, true) && !Tx.hasFont(17, 700, false), 'hasFont');
check(Tx.hasFont(8, 600, true), 'Silkscreen 600 = 700, come normWeight() in text.cpp');

// 2. le stesse prove di firmware/test/test_text (test_fit, test_wrap, «?» per i glifi assenti)
eq(Tx.textWidth('', 12), 0, 'larghezza vuota');
eq(Tx.fitText('api-server', 13, 700, false, 150), 'api-server', 'fit: sta già');
const cut = Tx.fitText('calamaio-firmware-esp32-progetto', 13, 700, false, 150);
check(cut.endsWith('…') && Tx.textWidthMilli(cut, 13, 700) <= 150000, 'fit con «…»');
const px = Tx.fitText('CONTESTO: CALAMAIO-FIRMWARE-ESP32 87%', 8, 400, true, 80);
check(px.endsWith('..') && Tx.textWidthMilli(px, 8, 400, true) <= 80000, 'fit Silkscreen con «..»');
for (let max = 20; max < 120; max++) {
  const s = Tx.fitText('Ultimo dato alle 14:32 di oggi', 10, 600, false, max);
  if (s.length >= 2) check(s[s.length - 2] !== ' ', 'trimEnd a ' + max);
}
eq(Tx.fitText('PERMESSO', 20, 700, false, 1), 'P…', 'resta un carattere');
eq(Tx.wrapLines('', 6, 10, 100).length, 0, 'wrap vuoto');
eq(Tx.wrapLines('   ', 6, 10, 100).length, 0, 'wrap spazi');
eq(JSON.stringify(Tx.wrapLines('  uno   due  ', 6, 10, 200)), '["uno due "]', 'wrap come split(/\\s+/)');
eq(Tx.wrapLines('parolalunghissimasenzaspazi corta', 6, 10, 30).length, 2, 'parola lunga');
eq(Tx.textWidthMilli('\u{1F600}', 12), Tx.textWidthMilli('?', 12), 'fuori BMP = ?');
const long = Tx.wrapLines('è'.repeat(80), 2, 10, 2000);
check(new TextEncoder().encode(long[0]).length <= 95, 'riga al massimo 95 byte come char[96]');

// 3. disegno: testo chiaro su scuro toglie inchiostro
const surf = (() => { const px = new Uint8Array(250 * 122); return { px, fillRect(x, y, w, h, ink) {
  for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++) if (xx >= 0 && yy >= 0 && xx < 250 && yy < 122) px[yy * 250 + xx] = ink ? 1 : 0; } }; })();
surf.fillRect(0, 0, 250, 20, true);
const before = surf.px.reduce((a, b) => a + b, 0);
Tx.drawText(surf, 'NEGA', 10, 2, 14, { weight: 700, ink: false });
check(surf.px.reduce((a, b) => a + b, 0) < before, 'testo carta su inchiostro');

// 4. parità con text.cpp: risultati C++ fissati da tools/fonts/parity/gen-fixture.mjs (serve clang++ solo per rigenerarli)
{
  const { cases } = JSON.parse(fs.readFileSync(new URL('../fonts/parity/text-cases.json', import.meta.url), 'utf8'));
  let bad = 0;
  for (const c of cases) {
    const got = JSON.stringify(runJs(c.slice(0, 10))), want = JSON.stringify(c[10]);
    if (got !== want && bad++ < 5) console.log('FAIL text.cpp', JSON.stringify(c.slice(0, 10)), 'js', got, 'c++', want);
  }
  check(bad === 0, 'parità con text.cpp: ' + bad + ' casi diversi su ' + cases.length);
}
// x e max interi come gli int di C++
eq(Tx.fitText('calamaio-firmware-esp32-progetto', 13, 700, false, 80.9), Tx.fitText('calamaio-firmware-esp32-progetto', 13, 700, false, 80), 'max troncato');
eq(JSON.stringify(Tx.wrapLines('uno due tre quattro', 3.7, 10, 40.9)), JSON.stringify(Tx.wrapLines('uno due tre quattro', 3, 10, 40)), 'wrap interi');
{
  const log = (o) => { const r = []; Tx.drawText({ fillRect: (...a) => r.push(a.join()) }, 'Wave fi', ...o, 12, { weight: 600 }); return r.join(';'); };
  eq(log([10.75, 3.6]), log([10, 3]), 'x e y troncati');
  eq(log([-4.75, 3]), log([-4, 3]), 'x negativa troncata verso lo zero');
}

// 5. primitive: stessi casi di tools/golden/prims-golden.js, confronto con firmware/test/test_prims/golden.h
const here = path.dirname(fileURLToPath(import.meta.url));
const gh = fs.readFileSync(path.resolve(here, '../../firmware/test/test_prims/golden.h'), 'utf8');
const pgNames = JSON.parse('[' + /PG_NAMES\[\] = \{([^}]*)\}/.exec(gh)[1] + ']');
const pg = [...gh.matchAll(/^  "([.#]+)",$/gm)].map((m) => m[1]);
eq(pg.length, pgNames.length, 'righe di golden.h');
const S = (st, done, total) => ({ done, total, working: st === 'working' });
const CASES = {
  box: (c) => Pr.box(c, 2, 3, 20, 10, true, 1),
  box_t2: (c) => Pr.box(c, 1, 1, 30, 20, true, 2),
  dots: (c) => { Pr.dotsH(c, 0, 60, 2); Pr.dotsV(c, 5, 4, 30); },
  icons: (c) => { ['done', 'error', 'input', 'permission', 'idle', 'compact'].forEach((s, k) => Pr.iconForState(c, s, 1 + k * 9, 1, 1, 0)); Pr.iconForState(c, 'working', 1, 12, 2, 0); Pr.iconForState(c, 'working', 20, 12, 2, 1); },
  syms: (c) => { for (let k = 0; k < 8; k++) Pr.sym(c, k, 1 + k * 8, 1, 1, true); },
  bar_work: (c) => Pr.bar(c, 1, 1, 60, 9, S('working', 3, 7), 0),
  bar_work_f1: (c) => Pr.bar(c, 1, 1, 60, 9, S('working', 3, 7), 1),
  bar_done: (c) => Pr.bar(c, 1, 1, 60, 9, S('done', 6, 6), 0),
  bar_many: (c) => Pr.bar(c, 1, 12, 61, 7, S('working', 5, 20), 2),
  bar_none: (c) => Pr.bar(c, 1, 1, 50, 8, S('working', 0, 0), 3),
  bar_none_idle: (c) => Pr.bar(c, 1, 1, 50, 8, S('idle', 0, 0), 3),
  wifi: (c) => { Pr.wifiIcon(c, 1, 1, 1, 0, true); Pr.wifiIcon(c, 14, 1, 1, 2, true); Pr.wifiIcon(c, 27, 1, 1, 4, true); Pr.wifiIcon(c, 40, 1, 1, -1, true); Pr.wifiIcon(c, 1, 12, 2, 3, true); },
  batt: (c) => { Pr.battIcon(c, 1, 1, 1, 78, true); Pr.battIcon(c, 20, 1, 1, 5, true); Pr.battIcon(c, 1, 12, 2, 33, true); },
  bigbar: (c) => { Pr.bigBar(c, 1, 1, 60, 10, 62); Pr.bigBar(c, 1, 16, 41, 7, 100); },
};
const shot = (draw) => {
  const b = new Bitmap(64, 32);
  draw(b);
  let s = '';
  for (let y = 0; y < 32; y++) for (let x = 0; x < 64; x++) s += b.get(x, y) ? '#' : '.';
  return s;
};
for (const [name, draw] of Object.entries(CASES)) {
  const k = pgNames.indexOf(name);
  check(k >= 0 && shot(draw) === pg[k], 'primitiva ' + name);
}
// argomenti interi come gli int di C++ (troncati verso lo zero)
eq(shot((c) => Pr.bar(c, 1.9, 1.2, 60.7, 9.5, S('working', 3.8, 7.2), 1.6)), pg[pgNames.indexOf('bar_work_f1')], 'bar con interi troncati');
eq(shot((c) => Pr.box(c, 2.5, 3.9, 20.2, 10.99, true, 1.7)), pg[pgNames.indexOf('box')], 'box con interi troncati');

// superficie: formato di Canvas::data(), Swap, Null
{
  const b = new Bitmap(10, 2);
  b.fillRect(-3, 0, 5, 1, true);  // x 0..1
  b.fillRect(9, 1, 4, 5, true);   // ritagliato
  b.fillRect(4, 0, -2, 1, true);  // larghezza negativa: niente
  eq(Array.from(b.packed()).join(), [0xc0, 0x00, 0x00, 0x40].join(), 'packed');
  eq(b.countInk(), 3, 'countInk');
  new Swap(b).fillRect(0, 0, 1, 1, true);
  check(!b.get(0, 0) && b.get(1, 0), 'Swap inverte');
  new Null().fillRect(0, 0, 10, 2, true);
  b.clear(true);
  eq(b.countInk(), 20, 'clear(true)');
  check(!b.get(-1, 0) && !b.get(10, 0), 'get fuori');
}
eq(crc32(new TextEncoder().encode('123456789')), 0xcbf43926, 'crc32');

// 6. QR: versione come qr::encode del firmware, motivi di posizione, capienza
const q1 = Qr.encode('123456');
check(q1 && q1.version === 1 && q1.size === 21, 'QR numerico in versione 1');
check(Qr.encode('x'.repeat(15)).version === 2 && Qr.encode('x'.repeat(84)).version === 5, 'scelta della versione');
check(Qr.encode('x'.repeat(85)) === null, 'oltre 84 byte: niente');
const finder = (q, ox, oy) => { for (let y = 0; y < 7; y++) for (let x = 0; x < 7; x++) {
  const ring = x === 0 || y === 0 || x === 6 || y === 6, core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
  if (q.get(ox + x, oy + y) !== (ring || core)) return false; } return true; };
const q5 = Qr.encode('Ciao è 3 api-server');
check(finder(q5, 0, 0) && finder(q5, q5.size - 7, 0) && finder(q5, 0, q5.size - 7), 'motivi di posizione');
check(!q5.get(-1, 0) && !q5.get(q5.size, 0), 'QR: fuori dalla griglia è carta');
// parità con qr.cpp + qrcode_ricmoo.cpp: matrici fissate da tools/qr-parity/gen-fixture.mjs
{
  const { cases } = JSON.parse(fs.readFileSync(new URL('../qr-parity/qr-cases.json', import.meta.url), 'utf8'));
  let bad = 0;
  for (const [hex, want] of cases) {
    const text = Buffer.from(hex, 'hex').toString('utf8'), q = Qr.encode(text);
    const got = q ? [q.version, q.size, q.mask, Qr.modulesHex(q)] : null;
    if (JSON.stringify(got) !== JSON.stringify(want) && bad++ < 5) console.log('FAIL qr.cpp', JSON.stringify(text), 'js', JSON.stringify(got), 'c++', JSON.stringify(want));
  }
  check(cases.length >= 40 && bad === 0, 'parità con qr.cpp: ' + bad + ' casi diversi su ' + cases.length);
}

// 7. valori: gli stessi casi di firmware/test/test_theme_values
const NOW = 1791707400000;
const LOCAL = Date.UTC(2026, 9, 11, 10, 30, 5);  // ora locale come WallClock.localMs
const sess = (k, o = {}) => ({ name: 'sessione-' + k, state: 'working', detail: 'Bash: npm run test', ago: 12 * 60000, ctx: 42, done: 3, total: 7, ...o });
const base = { now: NOW, local: LOCAL, frame: 0, wallOk: true, sys: { batt: 78, usb: false, wifi: true, ssid: 'CasaWiFi', rssi: -58, pc: true }, sessions: [] };
const cxOf = (m, screen = 'home') => ({ m, now: m.now, local: m.local, frame: m.frame, screen, ref: V.refIndex(m, screen, 1) });
let m = V.makeModel(base, { sessions: [sess(0), sess(1, { state: 'permission' })] });
let cx = cxOf(m);
eq(V.valueText(0x00, cx), 'sessione-1', 'selezionata = urgente');
eq(V.valueText(0x01, cx), 'Chiede un permesso', 'stato');
eq(V.valueText(0x05, cx), '42%', 'contesto');
eq(V.valueText(0x06, cx), '3/7', 'task');
eq(V.valueText(0x07, cx), '12 min', 'da');
eq(V.valueText(0x08, cx), '', 'sub vuoto');
eq(V.valueText(0x40, cx, m.sessions[0]), 'sessione-0', 'riga');
eq(V.valueText(0x80, cx), '10:30', 'ora');
eq(V.valueText(0x81, cx), '10:30:05', 'ora con i secondi');
eq(V.valueText(0x82, cx), 'domenica 11 ottobre', 'data');
eq(V.valueText(0x85, cx), '1', 'in attesa');
eq(V.valueText(0x86, cx), '78%', 'batteria');
eq(V.valueText(0x8F, cx), '—', 'limiti senza dati');
m = V.makeModel(base, { sessions: [sess(0)], limits: { h5: 36.5, d7: null }, stats: { tasks: 4, linesAdd: 120, linesDel: 30, cost: 1.2 } });
cx = cxOf(m);
eq(V.valueText(0x8F, cx), '37%', 'limiti 5 h');
eq(V.valueText(0x8D, cx), '+120 −30', 'righe');
eq(V.valueText(0x8E, cx), '1,20 $', 'costo');
eq(V.valueText(0x03, cx), '', 'riassunto vuoto');
eq(V.valueText(0x04, cx), '3/7 task  ·  12 min  ·  Bash: npm run test', 'meta');
eq(V.valueText(0x80, cxOf(V.makeModel(base, { wallOk: false }))), '--:--', 'ora mai sincronizzata');
check(V.condTrue(2, 80, cx, m.sessions[0]) && !V.condTrue(2, 78, cx, m.sessions[0]), 'battBelow');
check(V.condTrue(10, 0, cx, m.sessions[0]) && !V.condTrue(10, 0, cx, null), 'state');
check(V.condTrue(11, 1, { ...cx, frame: -3 }, null), 'blink dispari con fotogramma negativo');
m = V.makeModel(base, { sessions: [sess(0, { name: 'àbc-ÈFG' })] });
cx = cxOf(m);
eq(V.expand([{ lit: 'Ciao ' }, { val: 0, filter: 1 }, { val: 0, filter: 2 }], cx, null, false), 'Ciao ÀBC-ÈFGàbc-èfg', 'filtri');
m = V.makeModel(base, { sessions: [sess(0, { summary: 'è'.repeat(63) })] });
cx = cxOf(m);
const big = V.expand([{ val: 3 }, { val: 3 }, { val: 3 }], cx, null, false);
check(new TextEncoder().encode(big).length <= 255 && !big.includes('�'), 'troncato su un confine UTF-8');
// note di porting: %.2f come printf, filtri solo À–Þ/à–þ, battBelow sul valore grezzo, byte UTF-8
const costOf = (c) => V.valueText(0x8e, cxOf(V.makeModel(base, { stats: { cost: c } })));
eq(costOf(0.125), '0,12 $', '%.2f: pari merito esatto al pari');
eq(costOf(0.375), '0,38 $', '%.2f: pari merito esatto al pari (su)');
eq(costOf(1.005), '1,00 $', '%.2f: 1,005 è 1,00499…');
eq(costOf(2.675), '2,67 $', '%.2f: 2,675 è 2,67499…');
eq(V.expand([{ lit: 'ß×÷ÿ' }], cx, null, true), 'ß×÷ÿ', 'maiuscolo: ß × ÷ ÿ restano');
eq(V.expand([{ lit: 'ÀÞ×' }, { lit: 'àþ÷' }], cx, null, false).length, 6, 'nessuna mappa senza filtro');
eq(V.expand([{ lit: 'ÀÞ×ẞ', }], cx, null, false), 'ÀÞ×ẞ', 'letterale intatto');
eq(V.expand([{ val: 0, filter: 2 }], cxOf(V.makeModel(base, { sessions: [sess(0, { name: 'ÀÞ×ẞ' })] })), null, false), 'àþ×ẞ', 'minuscolo: × e ẞ restano');
{
  const mb = V.makeModel(base, { sys: { batt: 79.6 } }), c = cxOf(mb);
  check(V.valueText(0x86, c) === '80%' && V.condTrue(2, 80, c, null), 'battBelow sul valore grezzo, testo arrotondato');
}
eq(V.utf8Cut('aè€\u{1F600}', 6), 'aè€', 'utf8Cut in byte');
eq(new TextEncoder().encode(V.makeModel(base, { sessions: [sess(0, { name: 'è'.repeat(40) })] }).sessions[0].name).length, 30, 'nome al massimo 31 byte come char[32]');
eq(V.expand([{ lit: 'a\uD800b' }], cx, null, false), 'a�b', 'surrogato isolato = U+FFFD come TextEncoder');
{
  const ms = V.makeModel(base, { sessions: [sess(0), sess(1, { state: 'input' }), sess(2)], sel: 2 });
  eq(V.refIndex(ms, 'home', 1), 2, 'selezionata da sel per 15 s');
  eq(V.refIndex(ms, 'home', 0), 1, 'urgente');
  eq(V.refIndex(V.makeModel(base, { sessions: [sess(0), sess(1, { state: 'input' })], sel: -1 }), 'home', 1), 1, 'senza sel: sIdx = urgente');
  eq(V.refIndex(V.makeModel(base, {}), 'home', 0), -1, 'senza sessioni');
  eq(V.pose(ms, ms.sessions[1]), 'input', 'posa');
  eq(V.valueText(0x95, cxOf(V.makeModel(base, { fw: '0.13.0-ed' }))), '0.13.0-ed', 'versione dal chiamante');
}

// parità con theme_values.cpp: risposte C++ fissate da tools/theme-parity/gen-fixture.mjs (serve clang++ solo per rigenerarle)
{
  const { cases } = JSON.parse(fs.readFileSync(new URL('../theme-parity/values-cases.json', import.meta.url), 'utf8'));
  let bad = 0, n = 0;
  for (const c of cases) {
    const got = runValues(c);
    got.forEach((g, k) => { n++; if (g !== c.want[k] && bad++ < 5) console.log('FAIL theme_values.cpp', JSON.stringify(c.q[k]), 'js', g, 'c++', c.want[k]); });
  }
  check(cases.length >= 30 && n > 5000 && bad === 0, 'parità con theme_values.cpp: ' + bad + ' risposte diverse su ' + n);
}

// 8. abbassamento: come parse.go
const fx = (n) => JSON.parse(fs.readFileSync(path.resolve(here, '../../protocol/themes/fixtures/' + n), 'utf8'));
const lp = lowerTheme(fx('prova.theme.json'));
eq(lp.screens.home.variants.length, 3, 'varianti');
eq(lp.screens.home.variants[0].when, 1, 'when none');
eq(lp.screens.home.variants[0].elems[2].type, 2, 'orologio → testo');
eq(lp.screens.home.variants[0].elems[2].segs[0].val, 0x80, 'orologio = {ora}');
eq(lp.screens.home.variants[2].elems[0].row.length, 5, 'righe della lista');
eq(lp.screens.home.variants[2].elems[0].row[0].ref, 2, 'mascotte in una riga: ref row');
const le = lowerTheme(fx('elementi.theme.json'));
const rect = le.screens.pet.variants[0].elems.find((e) => e.type === 10);
check(rect && rect.x === 100 && rect.y === 60 && rect.conds.length === 1 && rect.conds[0][0] === 10, 'gruppo sciolto');
eq(le.screens.pet.variants[0].elems.filter((e) => e.type === 10).length, 1, 'nascosto tolto');
// la forma abbassata ricodificata come compile.go deve dare byte per byte i .cthm delle fixture (scritti dal Go)
function encodeLowered(t) {
  const KEYS = ['home', 'detail', 'sleep', 'offline', 'riepilogo', 'oggi', 'batteria', 'stato', 'limiti', 'focus', 'ambiente', 'pet'];
  const le16 = (b, v) => b.push(v & 255, (v >> 8) & 255);
  const block = (l) => {
    const st = [], at = new Map();
    const ref = (v) => { if (!at.has(v)) { const u = new TextEncoder().encode(v); at.set(v, st.length); st.push(u.length, ...u); } return at.get(v); };
    const segs = (b, ss) => { b.push(ss.length); for (const g of ss) { if (g.lit !== undefined) { b.push(0); le16(b, ref(g.lit)); } else { b.push(1 | (g.filter << 2)); le16(b, g.val); } } };
    const elem = (b, e) => {
      const p = [];
      switch (e.type) {
        case 1: p.push(e.scale, e.pose, e.ref, (e.acc ? 1 : 0) | (e.anim ? 2 : 0)); break;
        case 2: p.push(e.size, (e.weight / 100) | (e.pix ? 0x80 : 0), e.align, e.lines, e.lh, e.upper ? 1 : 0); segs(p, e.segs); break;
        case 3: p.push(e.scale, e.percent ? 1 : 0); break;
        case 4: p.push(e.scale); break;
        case 5: p.push(e.scale, e.ref); break;
        case 6: p.push(e.src, e.big ? 0 : 1); break;
        case 7: segs(p, e.segs); break;
        case 8: p.push(e.sym, e.scale); break;
        case 9: p.push(e.vertical ? 1 : 0, e.dotted ? 1 : 0); break;
        case 10: p.push(e.border, e.fill ? 1 : 0, e.radius); break;
        case 11: p.push(e.border, e.fill ? 1 : 0); break;
        case 12: p.push(e.rowH, e.gap, e.pageSec, e.dots ? 1 : 0, e.row.length, 0); break;
      }
      b.push(e.type, e.invert ? 1 : 0); le16(b, e.x); le16(b, e.y); le16(b, e.w); le16(b, e.h); b.push(e.conds.length, p.length);
      for (const [op, arg] of e.conds) b.push(op, arg);
      b.push(...p);
      for (const r of e.row || []) elem(b, r);
    };
    const bodies = l.variants.map((v) => { const b = []; for (const e of v.elems) elem(b, e); return b; });
    let off = 4 + 6 * bodies.length, so = off + bodies.reduce((a, b) => a + b.length, 0);
    const out = [bodies.length, 0]; le16(out, so);
    l.variants.forEach((v, i) => { out.push(v.when, v.ref); le16(out, off); le16(out, v.elems.length); off += bodies[i].length; });
    return out.concat(...bodies, st);
  };
  const list = KEYS.map((k, i) => [i + 1, t.screens[k]]).filter(([, l]) => l).map(([c, l]) => [c, block(l)]);
  const out = new Array(72).fill(0);
  out.splice(0, 4, ...new TextEncoder().encode('CTHM')); out[4] = 1; out[5] = 72; out[16] = 250; out[18] = 122;
  new TextEncoder().encode(t.id).forEach((b, i) => { out[20 + i] = b; });
  new TextEncoder().encode(t.name).forEach((b, i) => { out[44 + i] = b; });
  out[68] = list.length;
  let pos = 72 + 8 * list.length;
  for (const [c, b] of list) { out.push(c, 0); le16(out, b.length); out.push(pos & 255, (pos >> 8) & 255, 0, 0); pos += b.length; }
  for (const [, b] of list) out.push(...b);
  const u = Uint8Array.from(out), dv = new DataView(u.buffer);
  dv.setUint32(12, u.length, true);
  dv.setUint32(8, crc32(u.subarray(12)), true);
  return u;
}
// JSON a metà dall'editor: mai un'eccezione. Dove parse.go rifiuterebbe: elemento, variante o condizione tolti,
// campo al predefinito, schermata non personalizzabile tolta.
{
  const noThrow = (f, msg) => { try { return f(); } catch (e) { check(false, msg + ': ' + e.message); return null; } };
  const bad = [null, 5, 'x', [], true, { screens: null }, { screens: 5 }, { screens: { home: null } }, { screens: { home: { variants: 'x' } } },
    { screens: { home: { variants: [null, 5, 'x', [], { elements: null }, { elements: [null, 7, 'a', [], { type: 'constructor' }, { type: 'toString' },
      { type: '__proto__' }, { type: 'group', children: 'x' }, { type: 'group', children: [null, { type: 'rect', w: 'a', h: null }] }, { type: 'list', row: 'x' },
      { type: 'list', row: [null, 3, { type: 'mascot', ref: 'valueOf' }] }] }] } } }];
  for (const t of bad) noThrow(() => lowerTheme(t), 'lowerTheme(' + JSON.stringify(t) + ')');
  const one = (els, v = {}) => lowerTheme({ screens: { home: { variants: [{ when: 'always', ...v, elements: els }] } } }).screens.home.variants[0];
  eq(one([{ type: 'constructor' }, { type: 'toString' }, { type: 'hasOwnProperty' }, null, 3, 'rect', [{ type: 'rect' }]]).elems.length, 0, 'tipi sconosciuti e non oggetti tolti');
  eq(one([], { when: 'toString' }).when, 0, 'when sconosciuto → always');
  eq(one([], { when: 'constructor', ref: 'valueOf' }).ref, 1, 'ref della variante sconosciuto → selected');
  eq(one([{ type: 'mascot', ref: 'valueOf' }]).elems[0].ref, 4, 'ref sconosciuto → quella della variante');
  eq(one([{ type: 'stateIcon', ref: 'constructor' }]).elems[0].ref, 4, 'ref constructor → quella della variante');
  const t1 = one([{ type: 'text', w: 50, text: '{constructor}{toString|maiuscolo}{__proto__}{sessioni}' }]).elems[0];
  eq(JSON.stringify(t1.segs.map((g) => g.val)), '[132,132,132,132]', 'valori sconosciuti → {sessioni}, mai un oggetto');
  const c1 = one([{ type: 'rect', w: 5, h: 5, visibleIf: [{ constructor: true }, { toString: 1 }, { perm: 'si' }, { battBelow: 'a' }, { battBelow: 0 },
    { sessions: 3 }, { sessions: { min: 'a', max: 9 } }, { state: 'constructor' }, { blink: 'x' }, null, 5, [], { perm: true, night: true }, { night: true }] }]).elems[0];
  eq(JSON.stringify(c1.conds), '[[4,1]]', 'condizioni sbagliate tolte');
  const r1 = one([{ type: 'rect', x: 'a', y: null, w: '5', h: {}, border: 'x', radius: [], fill: 'yes', invert: 1, hidden: 'yes' }]).elems[0];
  check(r1 && r1.x === 0 && r1.y === 0 && r1.w === 0 && r1.h === 0 && r1.border === 1 && r1.radius === 0 && !r1.fill && !r1.invert, 'campi sbagliati → predefiniti');
  const tx = one([{ type: 'text', font: 'x', text: 5, lines: 'a', align: 'toString' }, { type: 'clock', font: null, format: 7 }]).elems;
  check(tx[0].size === 10 && tx[0].weight === 500 && tx[0].lines === 1 && tx[0].align === 0 && tx[0].segs.length === 0 && tx[1].segs[0].val === 0x80, 'testo e orologio sbagliati → predefiniti');
  let deep = { type: 'rect', w: 1, h: 1 };
  for (let k = 0; k < 5; k++) deep = { type: 'group', children: [deep] };
  eq(one([deep]).elems.length, 0, 'gruppi oltre 4 livelli tolti come in parse.go');
  eq(one([{ type: 'list', rowH: 10, row: [{ type: 'list', row: [] }, { type: 'rect', w: 1, h: 1 }] }]).elems[0].row.length, 1, 'lista in una riga tolta');
  const scr = lowerTheme({ screens: { perm: { variants: [{ elements: [] }] }, pair: { variants: [] }, confirm: {}, boot: {}, gbplay: {}, list: {}, nuova: {},
    constructor: {}, __proto__: {}, toString: {}, home: { variants: [] } } });
  eq(Object.keys(scr.screens).join(), 'home', 'solo le 12 schermate personalizzabili: niente Permessi a tema');
  const lp2 = lowerTheme(JSON.parse('{"screens":{"__proto__":{"variants":[]},"home":{"variants":[]}}}'));
  eq(Object.keys(lp2.screens).join(), 'home', '__proto__ come chiave JSON');
  // makeModel
  for (const o of [null, 5, 'x', { sessions: [null, 5, 'x', [], { name: 7 }] }, { sessions: 'x' }, { sys: 'x', set: 5, limits: [], bat: { cal: 'x', mix: 'x' } },
    JSON.parse('{"__proto__":{"now":5},"sessions":[{"__proto__":{"name":"x"}}]}')])
    noThrow(() => { const mm = V.makeModel(base, o); V.valueText(4, cxOf(mm)); return mm; }, 'makeModel(' + JSON.stringify(o) + ')');
  const mn = V.makeModel(base, { sessions: [null, 5, { name: 'a\nb\tc\u0001d', ctx: 150, subs: 120, detail: 'x\ry' }, { ctx: -5, done: 2.5, total: -2.5 }] });
  eq(mn.sessions.length, 2, 'sessioni non oggetto tolte');
  eq(mn.sessions[0].name + '|' + mn.sessions[0].detail, 'a b c d|x y', 'caratteri di controllo → spazi come text() di apply_state');
  eq([mn.sessions[0].ctx, mn.sessions[0].subs, mn.sessions[1].ctx].join(), '100,99,0', 'ctx 0–100, subs 0–99');
  eq([mn.sessions[1].done, mn.sessions[1].total].join(), '3,-3', 'interi come llround');
  eq(V.makeModel(base, { stats: { linesAdd: 2.5, linesDel: -0.5, tasks: 2e6 } }).stats.linesAdd + ',' + V.makeModel(base, { stats: { linesDel: -0.5 } }).stats.linesDel
    + ',' + V.makeModel(base, { stats: { tasks: 2e6 } }).stats.tasks, '3,-1,1000000', 'llround e tasks fino a 1000000');
}
// %.2f oltre 2^46: pari merito esatti in BigInt
eq(costOf(2 ** 47 + 0.125), '140737488355328,12 $', '%.2f: pari merito a 2^47');
eq(costOf(2 ** 49 + 0.375), '562949953421312,38 $', '%.2f: pari merito a 2^49');
for (const n of ['prova', 'elementi', 'lista']) {
  const want = fs.readFileSync(path.resolve(here, '../../protocol/themes/fixtures/' + n + '.cthm'));
  const got = encodeLowered(lowerTheme(fx(n + '.theme.json')));
  check(Buffer.compare(Buffer.from(got), want) === 0, 'abbassamento di ' + n + ' = ' + n + '.cthm byte per byte (' + got.length + ' / ' + want.length + ')');
}

// 9. renderer: gira senza il golden (la parità pixel per pixel col C++ è firmware/test/test_theme_parity)
{
  const blank = Array(25).fill('.'.repeat(28));
  const noAvatar = { avatar: () => ({ id: 'vuoto', poses: { working: [blank], idle: [blank] } }), accessory: () => null };
  const bm = new Bitmap();
  const ri = renderScreen(lp, 'home', V.makeModel(base, {}), bm, noAvatar);
  check(ri.drawn && bm.countInk() > 0, 'home di «prova» con 0 sessioni');
  check(ri.hasMascot && ri.mx === 83 && ri.my === 6 && ri.mw === 84 && ri.mh === 75, 'riquadro della mascotte 28·s × 25·s');
  const b0 = new Bitmap();
  check(!renderScreen(lp, 'oggi', V.makeModel(base, {}), b0, noAvatar).drawn && b0.countInk() === 0, 'schermata assente: di serie, niente disegnato');
  const b1 = new Bitmap();
  check(!renderScreen(lowerTheme({ screens: { home: { variants: [] } } }), 'home', V.makeModel(base, {}), b1, noAvatar).drawn && b1.countInk() === 0,
    'variants: [] = di serie, come drawThemeBlock');
  // l'orologio coi secondi chiede il secondo dopo
  const hms = lowerTheme({ screens: { home: { variants: [{ elements: [{ type: 'clock', format: 'hms', x: 0, y: 0, w: 100, font: { family: 'pix', size: 8, weight: 400 } }] }] } } });
  eq(renderScreen(hms, 'home', V.makeModel(base, { local: LOCAL + 250 }), new Bitmap(), noAvatar).nextChange, NOW + 750, 'hms: nextChange al secondo dopo');
  // JSON a metà: come la scheda, un elemento che checkBlock rifiuterebbe (in qualunque variante) fa la schermata di serie:
  // niente eccezioni, niente cicli infiniti, niente disegnato, drawn falso
  const badEls = [{ type: 'mascot', scale: 100 }, { type: 'mascot', scale: 5, x: 0, y: 0 }, { type: 'text', text: 'abc def', w: 50, lines: 0 },
    { type: 'text', text: 'abc def', w: 50, lines: 3, lineHeight: -4 }, { type: 'text', text: 'abc def', w: 50, lines: 9 },
    { type: 'text', text: 'abc', font: { size: 17 } }, { type: 'text', text: 'x'.repeat(256), w: 50 }, { type: 'text', text: '{riga.nome}', w: 50 },
    { type: 'list', w: 250, h: 100, rowH: 0, gap: 0, row: [{ type: 'rect', w: 5, h: 5, fill: true }] },
    { type: 'list', w: 250, h: 100, rowH: 10, pageSec: 0, row: [{ type: 'rect', w: 5, h: 5, fill: true }] }, { type: 'list', w: 250, h: 100, rowH: 10, row: [] },
    { type: 'list', w: 250, h: 100, rowH: 10, row: [{ type: 'rect', w: 600, h: 5, fill: true }] },
    { type: 'line', w: 1e9, h: 1e9, style: 'dotted' }, { type: 'bar', w: 1e9, h: 1e9, style: 'bar', source: 'tasks' }, { type: 'rect', x: -1e9, w: 2e9, h: 5, border: 50, radius: -3 },
    { type: 'rect', w: 5, h: 5, border: 9 }, { type: 'circle', w: 1e6, h: 1e6, border: 1e6 }, { type: 'battery', scale: 9, percent: true }, { type: 'sym', sym: 'bolt', scale: 0 },
    { type: 'qr', w: 1e9, h: 1e9, text: 'x' }, { type: 'mascot', ref: 'row' }, { type: 'rect', w: 5, h: 5, x: 513 },
    { type: 'rect', w: 5, h: 5, visibleIf: Array.from({ length: 9 }, () => ({ night: false })) }];
  const good = { type: 'rect', x: 0, y: 0, w: 20, h: 20, fill: true };
  const mBad = V.makeModel(base, { sessions: [sess(0), sess(1), sess(2)] });
  const seeBad = (t, msg) => {
    const b = new Bitmap();
    try {
      const r = renderScreen(lowerTheme(t), 'home', mBad, b, noAvatar);
      check(!r.drawn && b.countInk() === 0 && !r.hasMascot && r.nextChange === 0, msg);
    } catch (e) { check(false, msg + ': ' + e.message); }
  };
  for (const el of badEls) {
    seeBad({ screens: { home: { variants: [{ elements: [good, el] }] } } }, 'elemento fuori dai limiti = schermata di serie: ' + JSON.stringify(el));
    seeBad({ screens: { home: { variants: [{ when: 'always', elements: [good] }, { when: 'none', elements: [el] }] } } }, 'anche in un\'altra variante: ' + JSON.stringify(el));
  }
  seeBad({ screens: { home: { variants: [{ elements: Array.from({ length: 65 }, () => good) }] } } }, 'oltre 64 elementi');
  seeBad({ screens: { home: { variants: [{ elements: [{ type: 'list', w: 250, h: 100, rowH: 10, row: Array.from({ length: 64 }, () => good) }] }] } } }, 'oltre 64 elementi con le righe');
  seeBad({ screens: { home: { variants: Array.from({ length: 9 }, () => ({ elements: [good] })) } } }, 'oltre 8 varianti');
  const okB = new Bitmap();
  check(renderScreen(lowerTheme({ screens: { home: { variants: [{ elements: [good] }] } } }), 'home', mBad, okB, noAvatar).drawn && okB.countInk() === 400, 'tema valido disegnato');
}

// 9. glifi: stesse tabelle e stessi avvisi del servizio (protocol/themes/catalog.json)
{
  const REPO9 = path.join(path.dirname(fileURLToPath(import.meta.url)), '../..');
  const cat = JSON.parse(fs.readFileSync(path.join(REPO9, 'protocol/themes/catalog.json'), 'utf8'));
  for (const f of cat.fonts) eq([...G.fontGlyphs(f.family, f.size, f.weight)].map((c) => String.fromCodePoint(c)).join(''), f.glyphs, 'glifi ' + f.family + f.size + '/' + f.weight);
  check(G.fontGlyphs('ui', 17, 700) === null, 'combinazione inesistente = null');
  eq(G.CLOCK_GLYPHS, cat.clock, 'orologio');
  for (const v of cat.values) eq(JSON.stringify(G.VALUE_GLYPHS[v.name]), JSON.stringify({ glyphs: v.glyphs, free: v.free, example: v.example }), 'valore ' + v.name);
  for (const c of cat.cases) eq(JSON.stringify(G.glyphWarnings(c.kind === 'clock' ? { type: 'clock', format: c.text, font: c.font } : { type: 'text', text: c.text, font: c.font, upper: c.upper })), JSON.stringify(c.warnings), 'caso ' + c.text);
  eq(JSON.stringify(G.glyphWarnings({ type: 'text', text: '{oggi.co', font: { family: 'ui', size: 44, weight: 700 } })), JSON.stringify(['Chakra Petch 44 non ha «{»: si vedrà «?»']), 'graffa aperta = testo fisso, come parseTemplate');
  eq(JSON.stringify(G.buildValueGlyphs(['nuovo.valore'])['nuovo.valore']), JSON.stringify({ glyphs: '', free: true, example: '' }), 'valore fuori da SPEC = testo libero');
  eq(JSON.stringify(G.buildValueGlyphs(['sessione.nome'], {})), JSON.stringify({ 'sessione.nome': { glyphs: '', free: true, example: '' } }), 'SPEC vuota non lancia');
  eq(JSON.stringify(G.glyphWarnings({ type: 'text', text: '{boh} {x', font: null })), '[]', 'valore sconosciuto e font assente: nessuna eccezione');
}
console.log(fail ? fail + ' prove fallite' : 'temi: tutto ok');
process.exit(fail ? 1 : 0);
