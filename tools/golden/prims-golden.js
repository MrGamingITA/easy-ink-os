// Genera firmware/test/test_prims/golden.h: le primitive del simulatore (box, bar, icon, …)
// disegnate su un riquadro 64×32 per una serie di casi. Uso: node tools/golden/prims-golden.js
const fs = require('fs');
const path = require('path');
const root = path.resolve(__dirname, '../..');
const src = fs.readFileSync(path.join(root, 'reference/calamaio-os-browser/assets/calamaio-test.js'), 'utf8');

function grab(name) {
  const start = src.indexOf('function ' + name + '(');
  if (start < 0) throw new Error('manca ' + name);
  let i = src.indexOf('{', start), d = 0;
  for (; i < src.length; i++) { if (src[i] === '{') d++; else if (src[i] === '}' && --d === 0) break; }
  return src.slice(start, i + 1);
}
const konst = (name) => src.match(new RegExp('const ' + name + ' = \\{[\\s\\S]*?\\n    \\};'))[0];
const names = ['box', 'dotsH', 'dotsV', 'glyph', 'strike', 'tri', 'chevL', 'icon', 'bar', 'checkbox', 'wifiIcon', 'battIcon', 'scrollbar', 'bigBar'];
const lib = new Function(`const INK='#000', PAPER='#fff'; const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
  ${konst('G')} ${konst('ICONS')} ${names.map(grab).join('\n')}
  return { G, ICONS, ${names.join(', ')} };`)();

const W = 64, H = 32;
// Ogni caso: [nome, funzione che disegna]. L'ordine deve coincidere con firmware/test/test_prims/test_main.cpp.
const S = (st, done, total) => ({ state: st, done, total });
const CASES = [
  ['box', (c) => lib.box(c, 2, 3, 20, 10, '#000', 1)],
  ['box_t2', (c) => lib.box(c, 1, 1, 30, 20, '#000', 2)],
  ['dots', (c) => { lib.dotsH(c, 0, 60, 2); lib.dotsV(c, 5, 4, 30); }],
  ['strike', (c) => lib.strike(c, 3, 3, 6, 2, '#000', '#fff')],
  ['tri', (c) => { lib.tri(c, 1, 1, 1, 'up'); lib.tri(c, 10, 1, 2, 'down'); lib.tri(c, 30, 1, 1, 'left'); lib.tri(c, 40, 1, 2, 'right'); }],
  ['chev', (c) => { lib.chevL(c, 1, 1, '#000'); lib.chevL(c, 10, 1, '#000', 2); }],
  ['icons', (c) => { ['done', 'error', 'input', 'permission', 'idle', 'compact'].forEach((s, k) => lib.icon(c, s, 1 + k * 9, 1, 1, 0)); lib.icon(c, 'working', 1, 12, 2, 0); lib.icon(c, 'working', 20, 12, 2, 1); }],
  ['syms', (c) => { ['plug', 'bell', 'moon', 'lock', 'bolt', 'dot', 'glass', 'gauge'].forEach((g, k) => lib.glyph(c, lib.G[g], 1 + k * 8, 1, 1, '#000')); }],
  ['bar_work', (c) => lib.bar(c, 1, 1, 60, 9, S('working', 3, 7), 0)],
  ['bar_work_f1', (c) => lib.bar(c, 1, 1, 60, 9, S('working', 3, 7), 1)],
  ['bar_done', (c) => lib.bar(c, 1, 1, 60, 9, S('done', 6, 6), 0)],
  ['bar_many', (c) => lib.bar(c, 1, 12, 61, 7, S('working', 5, 20), 2)],
  ['bar_none', (c) => lib.bar(c, 1, 1, 50, 8, S('working', 0, 0), 3)],
  ['bar_none_idle', (c) => lib.bar(c, 1, 1, 50, 8, S('idle', 0, 0), 3)],
  ['checkbox', (c) => { lib.checkbox(c, 1, 1, 0, 0); lib.checkbox(c, 14, 1, 1, 0); lib.checkbox(c, 27, 1, 1, 1); lib.checkbox(c, 40, 1, 2, 0); lib.checkbox(c, 1, 14, 2, 0, 14); lib.checkbox(c, 20, 14, 2, 0, 7); }],
  ['wifi', (c) => { lib.wifiIcon(c, 1, 1, 1, 0, '#000'); lib.wifiIcon(c, 14, 1, 1, 2, '#000'); lib.wifiIcon(c, 27, 1, 1, 4, '#000'); lib.wifiIcon(c, 40, 1, 1, -1, '#000'); lib.wifiIcon(c, 1, 12, 2, 3, '#000'); }],
  ['batt', (c) => { lib.battIcon(c, 1, 1, 1, 78, '#000'); lib.battIcon(c, 20, 1, 1, 5, '#000'); lib.battIcon(c, 1, 12, 2, 33, '#000'); }],
  ['scroll', (c) => { lib.scrollbar(c, 2, 1, 30, 0, 4, 10); lib.scrollbar(c, 10, 1, 30, 3, 4, 10); lib.scrollbar(c, 18, 1, 30, 6, 4, 10); lib.scrollbar(c, 26, 1, 30, 0, 5, 5); }],
  ['bigbar', (c) => { lib.bigBar(c, 1, 1, 60, 10, 62); lib.bigBar(c, 1, 16, 41, 7, 100); }],
];

let out = '// Generato da tools/golden/prims-golden.js: NON modificare a mano.\n#pragma once\n';
out += `static const int PG_W = ${W}, PG_H = ${H}, PG_N = ${CASES.length};\n`;
out += 'static const char *const PG_NAMES[] = {' + CASES.map((k) => JSON.stringify(k[0])).join(', ') + '};\n';
out += 'static const char *const PG[] = {\n';
for (const [, draw] of CASES) {
  const g = Array.from({ length: H }, () => Array(W).fill('.'));
  const c = { fillStyle: '', fillRect(x, y, w, h) {
    if ([x, y, w, h].some((v) => !Number.isInteger(v))) throw new Error('coordinate non intere: ' + [x, y, w, h]);
    for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++)
      if (yy >= 0 && yy < H && xx >= 0 && xx < W) g[yy][xx] = this.fillStyle === '#fff' ? '.' : '#';
  } };
  draw(c);
  out += '  "' + g.map((r) => r.join('')).join('') + '",\n';
}
out += '};\n';
const dst = path.join(root, 'firmware/test/test_prims/golden.h');
fs.mkdirSync(path.dirname(dst), { recursive: true });
fs.writeFileSync(dst, out);
console.log('scritto', dst, CASES.length, 'casi');
