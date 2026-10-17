// Genera firmware/test/test_core_golden/golden_core.h dal simulatore vero.
// Esegue il componente di calamaio-test.js in Node con:
//   - orologio finto (Date sostituito, fuso UTC), avanzato solo dallo scenario;
//   - Math.random sostituito da xorshift32 con seme fisso (lo stesso di cal::Rng in C++);
//   - class Screen vera su una tela finta, così i limiti che il disegno applica a sel/off avvengono davvero;
//   - setTimeout immediato e redraw() registrato, per sapere che refresh chiede ogni comando.
// Dopo ogni passo dello scenario scrive una «fotografia» testuale del modello: il test C++ rifà gli
// stessi passi con cal::Ui e confronta le fotografie riga per riga.
// Uso: node tools/golden/core-golden.js
process.env.TZ = 'UTC';
const fs = require('fs');
const path = require('path');
const root = path.resolve(__dirname, '../..');
let src = fs.readFileSync(path.join(root, 'reference/calamaio-os-browser/assets/calamaio-test.js'), 'utf8');

// Esporta anche le funzioni pure che il simulatore tiene private.
const EXTRA = ['metaLine', 'pose', 'stateText', 'lineFor', 'hasAlerts', 'extras', 'ago', 'dateStr', 'resetLabel',
  'fmt1', 'splitCmd', 'animOff', 'profileName', 'maxSpeed', 'effFps', 'menuList', 'netItems', 'workLine'];
const exportRe = /return \{\s*\n\s*DEVICES: DEVICES,/;
if (!exportRe.test(src)) throw new Error('export di build() non trovato: il simulatore è cambiato');
src = src.replace(exportRe, (s) => 'return { ' + EXTRA.map((k) => k + ': ' + k).join(', ') + ',\n      DEVICES: DEVICES,');

// ---------- ambiente finto ----------
const T0 = Date.UTC(2026, 9, 11, 10, 29, 30);  // domenica 11 ottobre 2026, 10:29:30
let NOW = T0;
const RealDate = Date;
class FakeDate extends RealDate {
  constructor(...a) { if (a.length === 0) super(NOW); else super(...a); }
  static now() { return NOW; }
}
const SEED = 0xC0FFEE;
let rs = SEED >>> 0;
const rnd = () => { rs ^= rs << 13; rs >>>= 0; rs ^= rs >>> 17; rs ^= rs << 5; rs >>>= 0; return rs / 4294967296; };
const FakeMath = Object.create(Math);
FakeMath.random = rnd;

let drawError = null;
function mockCtx(W, H) {
  return {
    setTransform() {}, fillRect() {}, measureText(s) { return { width: String(s).length * 6 }; },
    fillText(s) { if (s === 'Errore di disegno') drawError = drawError || 'errore di disegno'; },
    getImageData() { return { data: new Uint8ClampedArray(W * H * 4) }; },
    createImageData() { return { data: new Uint8ClampedArray(W * H * 4) }; },
    putImageData() {}
  };
}
function mockCanvas() {
  const cv = { width: 0, height: 0, getContext() { return mockCtx(cv.width || 1000, cv.height || 600); } };
  return cv;
}
const fakeDocument = { hidden: false, createElement: () => mockCanvas(), querySelector: () => null, fonts: { load: () => Promise.resolve() } };
class DCLogic { constructor(props) { this.props = props || {}; } forceUpdate() {} }
const fakeWindow = {};
const immediate = (fn) => { fn(); return 0; };

new Function('window', 'DCLogic', 'document', 'Date', 'Math', 'setTimeout', 'clearTimeout', src)(
  fakeWindow, DCLogic, fakeDocument, FakeDate, FakeMath, immediate, () => {});
const Component = fakeWindow.CalamaioTest.Component;

// ---------- componente ----------
const comp = new Component({ device: 'heltec213' });
comp.ensure();
comp.lastAction = '';  // testo del pannello del banco di prova, non dello schermo
rs = SEED >>> 0;        // init() consuma un Math.random() per l'uid del componente: si riparte dal seme
const E = comp.eng();
const ORDER = ['none', 'partial', 'quick', 'full'];
let rec = 'none', harnessDrawing = false, inPartial = false;
const merge = (k) => { if (ORDER.indexOf(k) > ORDER.indexOf(rec)) rec = k; };
comp.scr = new E.Screen(mockCanvas(), comp.dev);
const scr = comp.scr;
const origPartial = scr.partial.bind(scr), origFull = scr.full.bind(scr);
scr.partial = (m, f, ctl) => { if (!harnessDrawing) merge('partial'); inPartial = true; origPartial(m, f, ctl); inPartial = false; };
scr.full = (m, f, ctl) => { if (!inPartial && !harnessDrawing) merge('full'); origFull(m, f, ctl); };
let pendingKind = 'none';
comp.redraw = (kind) => {
  merge(kind);
  if (pendingKind !== 'quick') pendingKind = kind;
  comp.pending = true;
};
scr.show(comp.m, comp.fr(), comp.ctl);

// ---------- fotografia del modello ----------
const rel = (t) => (t ? t - NOW : 0);
const r6 = (x) => Math.round(x * 1e6);
function screenFields(tp) {
  const id = tp.id;
  if (id === 'boot') return 'until=' + rel(tp.until);
  if (id === 'perm') return 'i=' + tp.i + ',choice=' + tp.choice + ',dl=' + rel(tp.deadline);
  if (id === 'list') return 'menu=' + tp.menu + ',sel=' + (tp.sel || 0);
  if (id === 'wifiScan') return 'sel=' + (tp.sel || 0) + ',scan=' + (tp.scanUntil > NOW ? rel(tp.scanUntil) : 0);
  if (id === 'storico' || id === 'servizi') return 'off=' + (tp.off || 0);
  if (id === 'qr') return 'ssid=' + (tp.ssid || '');
  if (id === 'connecting') return 'ssid=' + tp.ssid + ',until=' + rel(tp.until);
  return '';
}
function snapshot(kind) {
  const m = comp.m, tp = E.top(m), s = m.set, y = m.sys, st = m.stats, dev = comp.dev;
  const out = [];
  out.push('r=' + kind + ' p=' + scr.partials);
  out.push('a=' + comp.lastAction);
  out.push('stack=' + m.ui.stack.map((e) => e.id).join(',') + ' [' + screenFields(tp) + ']');
  out.push('ui=' + m.ui.sel + ',' + (m.ui.selUntil > NOW ? rel(m.ui.selUntil) : 0) + ',' + m.ui.page);
  out.push('toast=' + (m.toast.msg && m.toast.until >= NOW ? m.toast.msg + '@' + rel(m.toast.until) : ''));
  out.push('fr=' + comp.fr() + ',' + comp.frame + ' rate=' + Math.round(E.animRate(dev, m, NOW) * 1000));
  out.push('set=' + [s.homeStyle, s.anim, s.speed, +s.invert, +s.rot, s.mute > NOW ? rel(s.mute) : 0, s.muteKind, +s.night, +s.permOn, +s.save, +s.hookDirect].join(','));
  out.push('sys=' + [+y.wifi, y.ssid, y.rssi, +y.pc, Math.round(y.batt * 1000), Math.round(y.temp * 10), rel(y.lastPc), rel(y.bootAt)].join(','));
  out.push('lim=' + r6(m.limits.h5) + ',' + r6(m.limits.d7) + ',' + rel(m.limits.h5Reset) + ' focus=' + (m.focus.until ? rel(m.focus.until) : 0) + ' burst=' + rel(m.burstUntil) + ',' + rel(m.nextBurst));
  out.push('stats=' + [st.tasks, st.permDisplay, st.permTerminal, st.waitSum, st.waitN, st.linesAdd, st.linesDel, r6(st.cost)].join(','));
  m.sessions.forEach((x, i) => {
    out.push('s' + i + '=' + [x.name, x.state, x.detail, x.done + '/' + x.total, x.ctx, x.subs, x.test === null ? 'null' : x.test, x.summary, x._t || 0,
      x.attnAt ? rel(x.attnAt) : '-', rel(x.since), E.pose(m, x)].join('|'));
    out.push('  meta=' + E.metaLine(x) + ' tasks=' + x.tasks.map((t) => t[1]).join(''));
  });
  out.push('log=' + m.log.length + ' ' + m.log.slice(0, 3).map((e) => rel(e.t.getTime()) + ':' + e.name + ':' + e.text).join(' / '));
  out.push('q=' + [E.urgentIdx(m), E.sIdx(m), E.unaIdx(m), +E.attention(m), +E.quiet(m), +E.muted(m), +E.powerSave(m), +!!E.hasAlerts(m)].join(','));
  out.push('prof=' + E.profileText(dev, m));
  const w = m.work, g = m.game;
  out.push('work=' + [+w.on, w.on ? rel(w.endAt) : 0, w.doneUntil > NOW ? rel(w.doneUntil) : 0, w.party > NOW ? rel(w.party) : 0, E.workLine(m), w.start, w.end].join(',') +
    ' game=' + [g.card, g.prev, g.streak, g.best, g.last, +g.over, g.startBest, g.seed].join(','));
  return out.join('\n');
}
function menus() {
  const m = comp.m, dev = comp.dev, out = [];
  const dump = (title, items) => {
    out.push('# ' + title);
    items.forEach((it) => out.push([it.label, it.val || '', it.go ? 'go:' + it.go : it.open ? 'open:' + it.open : it.act ? 'act:' + it.act : it.net ? 'net' : 'info',
      it.bars === undefined ? '' : 'bars:' + it.bars, it.lock ? 'lock' : '', it.help].join('|')));
  };
  ['main', 'wifi', 'display', 'avvisi', 'info', 'tools', 'work', 'gb'].forEach((id) => { const L = E.menuList(id, dev, m); dump(id + ':' + L.title, L.items); });
  dump('reti', E.netItems(m));
  return out.join('\n');
}

// ---------- scenario ----------
const ops = [];
const G = (g, n) => { for (let k = 0; k < (n || 1); k++) ops.push(['g', g]); };
const adv = (ms) => ops.push(['adv', ms]);
const tick = () => ops.push(['tick']);
const at = (ms) => { adv(ms); tick(); };

tick(); ops.push(['menus']); G('1x'); at(1000); at(1700); tick();
G('1x', 2); at(500); at(15000);
G('2x'); G('1x', 4); G('2x'); at(1000); G('2x'); at(3000); G('long');
ops.push(['raise', 0, 'permission']); G('2x'); G('2x'); G('1x', 3); G('2x'); G('long');
ops.push(['raise', 2, 'permission']); G('2x'); G('2x'); G('3x'); G('long');
G('2x'); at(20001); tick();
G('long'); G('long'); at(500); G('long'); G('3x'); ops.push(['menus']);
G('2x'); G('1x'); G('2x'); G('long');
G('1x'); G('2x'); G('2x'); G('2x'); at(1000); G('long'); G('3x'); ops.push(['menus']); at(25 * 60000); tick();
G('3x'); G('1x', 4); G('2x');
G('2x', 4); G('1x'); G('2x', 4); G('1x'); G('2x'); G('3x'); tick(); at(700); at(700); G('3x'); G('1x', 4); G('2x'); G('1x', 2); G('2x');
G('1x'); G('2x', 2); G('1x'); G('2x', 2); G('1x'); G('2x'); G('1x'); G('2x'); G('1x'); ops.push(['menus']); G('long');
G('1x'); G('2x'); G('2x', 3); G('1x'); G('2x', 2); G('1x'); G('2x'); G('long'); G('3x'); G('2x'); G('2x'); G('3x');
G('1x', 5); G('2x'); G('1x', 2); G('2x'); G('3x');
G('3x'); G('1x', 3); G('2x'); G('1x', 3); G('2x'); G('1x'); at(1600); G('1x', 2); G('2x'); at(1000); at(2000); ops.push(['menus']);
G('2x'); at(1600); G('2x'); G('2x'); at(2700); G('1x', 3); G('2x'); G('1x'); G('2x'); G('3x'); G('1x'); G('2x'); ops.push(['menus']); tick();
G('3x'); G('1x', 3); G('2x'); G('1x', 3); G('2x'); at(1600); G('1x', 2); G('2x'); at(2700); G('3x');
G('3x'); G('1x', 7); G('2x'); G('1x', 8); G('long'); G('1x', 6); G('2x'); G('1x', 2); G('2x'); G('1x', 5); G('long'); G('long');
G('1x', 6); G('2x'); G('2x'); G('2x'); G('long'); G('3x'); G('3x'); G('1x', 5); G('2x'); G('long'); G('1x', 3); G('2x'); G('long');
G('1x'); G('2x'); tick(); at(1000); G('1x');
G('1x'); G('2x'); ops.push(['menus']); G('1x', 8); G('2x'); G('2x'); at(2700);
G('vlong'); G('long');
G('3x'); G('1x', 4); G('2x'); G('2x', 3); G('3x'); G('2x'); G('2x'); G('2x'); at(8001); at(8001); G('1x'); G('2x'); G('long');
for (let k = 0; k < 200; k++) {
  adv(4200); ops.push(['step']); tick();
  if (k % 3 === 0) ops.push(['slow']);
  if (k % 10 === 5) G('1x');
  if (k % 25 === 12) { G('2x'); G('2x'); G('long'); G('long'); }
}
ops.push(['menus']); at(2 * 3600000); tick();
// 0.9.0: Giornata di lavoro (Strumenti, voce 6) e gioco «Alto o basso» (0.11.0: Menu › Gioco, voce 3), con la fine
// della giornata alle 18:00.
// advUntil: avanza fino a quell'ora del giorno (UTC); nel golden diventa un 'adv' con i millisecondi giusti.
const advUntil = (h, mi, sec, ms) => { ops.push(['advUntil', ((h * 60 + mi) * 60 + sec) * 1000 + (ms || 0)]); tick(); };
G('3x'); G('1x'); G('2x'); G('1x', 5); G('2x'); ops.push(['menus']); G('2x'); G('2x'); at(3000); G('1x'); G('2x'); G('2x'); G('1x', 3); G('2x');
G('long'); G('long'); G('1x'); G('2x'); G('2x'); G('1x'); G('2x'); G('1x'); G('2x'); G('1x'); G('2x'); G('1x'); G('2x'); G('1x'); G('2x'); G('1x'); G('2x');
G('2x'); G('1x'); G('long'); ops.push(['menus']); G('2x'); G('1x'); ops.push(['raise', 1, 'permission']); tick();
advUntil(17, 59, 30); advUntil(18, 0, 0, 50); at(1000); at(6000); ops.push(['menus']); advUntil(19, 0, 0, 100);
G('3x'); G('1x'); G('2x'); G('1x', 5); G('2x'); G('2x'); ops.push(['menus']); G('long'); G('3x');

const snaps = [];
for (const op of ops) {
  if (op[0] === 'advUntil') { const d = op[1] - (NOW % 86400000); op[0] = 'adv'; op[1] = d; }
  if (op[0] === 'adv') { NOW += op[1]; snaps.push(null); continue; }
  rec = 'none'; pendingKind = 'none'; comp.pending = null; drawError = null;
  let snap;
  if (op[0] === 'menus') { snaps.push(menus()); continue; }
  if (op[0] === 'g') comp.gesture(op[1]);
  else if (op[0] === 'tick') comp.tick();
  else if (op[0] === 'step') comp.step();
  else if (op[0] === 'slow') comp.slowTick();
  else if (op[0] === 'raise') comp.raise(comp.m.sessions[op[1]], op[2]);
  // il ridisegno in coda, come farebbe il timer da 140 ms del simulatore
  harnessDrawing = true;
  comp.pending = null;
  if (pendingKind === 'quick') scr.quick(comp.m, comp.fr(), comp.ctl);
  else if (pendingKind === 'partial') scr.partial(comp.m, comp.fr(), comp.ctl);
  harnessDrawing = false;
  if (drawError) throw new Error('il disegno del simulatore è fallito al passo ' + snaps.length + ' (' + op.join(' ') + ')');
  snap = snapshot(rec);
  snaps.push(snap);
}

// ---------- funzioni di formato ----------
const fmtCases = [];
const fmtNow = NOW;
[0, 29999, 30000, 59 * 60000, 59 * 60000 + 29999, 59 * 60000 + 30000, 61 * 60000, 125 * 60000, 26 * 3600000, -120000].forEach((d) => fmtCases.push(['ago', d, E.ago(fmtNow - d)]));
[22.4, 22.45, 0, 3.96, -0.04, -2.35, 100, 9.99].forEach((v) => fmtCases.push(['fmt1', Math.round(v * 1000), E.fmt1(v)]));
[[2026, 0, 1, 0, 0], [2026, 1, 28, 23, 59], [2024, 1, 29, 12, 0], [2026, 9, 11, 10, 29], [2027, 11, 31, 8, 5]].forEach((c) => {
  NOW = Date.UTC(c[0], c[1], c[2], c[3], c[4], 0);
  fmtCases.push(['date', NOW, E.dateStr()]);
  fmtCases.push(['hm', NOW, E.hm(new Date(NOW))]);
  [-3600000, 3 * 3600000, 13 * 3600000, (3 * 24 + 4) * 3600000].forEach((d) => fmtCases.push(['reset' + NOW, d, E.resetLabel(NOW + d)]));
});

// ---------- scrittura ----------
const esc = (s) => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n"\n    "') + '"';
let out = '// Generato da tools/golden/core-golden.js: NON modificare a mano.\n';
out += '// Fotografie del modello dopo ogni passo dello scenario, prese dal simulatore vero.\n#pragma once\n#include <stdint.h>\n\n';
out += `static const int64_t GOLDEN_T0 = ${T0}LL;\nstatic const uint32_t GOLDEN_SEED = ${SEED}u;\n\n`;
out += 'struct GoldenOp { const char *op; int a; int b; const char *expect; };\n';
out += 'static const GoldenOp GOLDEN_OPS[] = {\n';
ops.forEach((op, k) => {
  const kind = op[0];
  let a = 0, b = 0, sarg = '';
  if (kind === 'adv') a = op[1];
  else if (kind === 'g') sarg = op[1];
  else if (kind === 'raise') { a = op[1]; b = ['working', 'permission', 'input', 'error', 'compact', 'done', 'idle'].indexOf(op[2]); }
  const name = kind === 'g' ? 'g:' + sarg : kind;
  out += `  {${esc(name)}, ${a}, ${b}, ${snaps[k] === null ? 'nullptr' : esc(snaps[k])}},\n`;
});
out += '};\n\n';
out += 'struct GoldenFmt { const char *fn; int64_t a; int64_t b; const char *expect; };\n';
out += 'static const GoldenFmt GOLDEN_FMT[] = {\n';
fmtCases.forEach((c) => {
  if (c[0].startsWith('reset')) out += `  {"reset", ${c[0].slice(5)}LL, ${c[1]}LL, ${esc(c[2])}},\n`;
  else if (c[0] === 'ago') out += `  {"ago", ${fmtNow}LL, ${c[1]}LL, ${esc(c[2])}},\n`;
  else out += `  {${esc(c[0])}, ${c[1]}LL, 0, ${esc(c[2])}},\n`;
});
out += '};\n';
const dstDir = path.join(root, 'firmware/test/test_core_golden');
fs.mkdirSync(dstDir, { recursive: true });
const dst = path.join(dstDir, 'golden_core.h');
fs.writeFileSync(dst, out);
console.log('scritto', dst, '·', ops.length, 'passi,', fmtCases.length, 'casi di formato');
