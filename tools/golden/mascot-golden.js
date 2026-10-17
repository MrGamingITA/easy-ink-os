// Genera firmware/test/test_mascot/golden.h dal simulatore: la Goccia disegnata da mascot()
// in ogni posa e fotogramma, a scala 1, come righe di '#' (inchiostro) e '.' (carta).
// Uso: node tools/golden/mascot-golden.js
const fs = require('fs');
const path = require('path');
const root = path.resolve(__dirname, '../..');
const src = fs.readFileSync(path.join(root, 'reference/calamaio-os-browser/assets/calamaio-test.js'), 'utf8');

const hw = src.match(/const HW = \[[^\]]*\];/)[0];
const start = src.indexOf('function mascot(');
let i = src.indexOf('{', start), depth = 0;
for (; i < src.length; i++) { if (src[i] === '{') depth++; else if (src[i] === '}' && --depth === 0) break; }
const fn = src.slice(start, i + 1);
const mascot = new Function(`const INK='#000', PAPER='#fff'; ${hw} ${fn}; return mascot;`)();

const W = 28, H = 29;
const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'];
let out = '// Generato da tools/golden/mascot-golden.js: NON modificare a mano.\n#pragma once\n';
out += `static const int GOLDEN_W = ${W}, GOLDEN_H = ${H}, GOLDEN_OX = 0, GOLDEN_OY = 3;\n`;
out += 'static const char *const GOLDEN[9][4] = {\n';
for (const p of POSES) {
  out += '  {\n';
  for (let f = 0; f < 4; f++) {
    // carta di fondo = '.', i pixel scritti con PAPER restano '.', INK = '#'
    const g = Array.from({ length: H }, () => Array(W).fill('.'));
    const c = { fillStyle: '', fillRect(x, y, w, h) {
      for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++)
        if (yy >= 0 && yy < H && xx >= 0 && xx < W) g[yy][xx] = this.fillStyle === '#000' ? '#' : '.';
    } };
    mascot(c, 0, 3, 1, p, f);  // oy=3: dy minimo è -3 (done), così nulla esce dal riquadro
    out += '    "' + g.map((r) => r.join('')).join('') + '",\n';
  }
  out += '  },\n';
}
out += '};\n';
const dst = path.join(root, 'firmware/test/test_mascot/golden.h');
fs.writeFileSync(dst, out);
console.log('scritto', dst);
