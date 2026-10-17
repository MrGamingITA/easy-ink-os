#!/usr/bin/env node
// Golden degli accessori della mascotte (docs/AVATAR.md §14.4) generati dal disegno di riferimento
// tools/avatar/lib/draw-accessory.mjs, per il firmware (firmware/test/test_accessory/golden.h), e confronto con
// la copia del simulatore (drawAccessory di reference/calamaio-os-browser/assets/calamaio-test.js).
//
//   1. catalogo: gli 8 accessori di avatars/accessori × i 4 avatar di avatars/ con le loro ancore × le 10 pose ×
//      i fotogrammi 0..3 (1 280 casi). Per ogni caso il CRC-32 dei 700 codici della griglia (0 trasparente,
//      1 inchiostro, 2 carta: un solo punto diverso cambia il CRC) e se l'accessorio si disegna; per 64 casi
//      (lavora f0 e finito al culmine del salto f2) anche la griglia intera, per vedere dove si sbaglia;
//   2. casi costruiti (gli stessi di tools/avatar/test/draw-accessory.test.mjs): un avatar «mini» con e senza
//      ancore, accessori di prova per x sui tre fondi, contorno, pose nascoste, punto assente, ritocchi, 2 fotogrammi,
//      ripiego della posa e taglio fuori dal riquadro. Qui escono anche i byte CAVT, CANC e CACC, che il firmware
//      legge con i suoi parser.
//
// Uso: node tools/golden/accessory-golden.mjs           scrive golden.h e controlla il simulatore
//      node tools/golden/accessory-golden.mjs --check   solo i controlli: esce con 1 se golden.h è vecchio o il
//                                                       simulatore non coincide col riferimento
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { POSES, REQUIRED, W, H, poseFrames } from '../avatar/lib/format.mjs';
import { pack, crc32 } from '../avatar/lib/pack.mjs';
import { loadWithAnchors, packAnchors } from '../avatar/lib/anchors.mjs';
import { packAccessory } from '../avatar/lib/accessory.mjs';
import { drawAccessory, toGrid } from '../avatar/lib/draw-accessory.mjs';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const OUT = path.join(REPO, 'firmware', 'test', 'test_accessory', 'golden.h');
const AV_IDS = ['goccia', 'gatto', 'demone', 'calamaro'];
const ACC_IDS = ['cappellino', 'occhiali', 'sciarpa', 'papillon', 'corona', 'cuffie', 'fiocco', 'mago'];
const CODE = { '.': 0, '#': 1, '+': 2 };
const clone = (o) => JSON.parse(JSON.stringify(o));

const codesOf = (g) => Uint8Array.from(g.flat().map((ch) => CODE[ch]));
const crcOf = (codes) => crc32(Buffer.from(codes));
// Griglia della mascotte (posa con ripiego, f mod frames) con l'accessorio sopra; drawn = disegnato.
function render(av, acc, pose, f) {
  const frs = poseFrames(av, pose), n = frs.length;
  const g = toGrid(frs[((f % n) + n) % n]);
  const drawn = acc ? !!drawAccessory(g, av, acc, pose, f) : false;
  return { g, drawn };
}

// ---------------------------------------------------------------- catalogo
const avatars = AV_IDS.map((id) => {
  const r = loadWithAnchors(path.join(REPO, 'avatars', id + '.avatar.json'));
  if (!r.av.anchors) throw new Error(id + ': senza ancore');
  return r.av;
});
const accs = ACC_IDS.map((id) => JSON.parse(fs.readFileSync(path.join(REPO, 'avatars', 'accessori', id + '.accessory.json'), 'utf8')));
const cat = [];
accs.forEach((acc, ai) => avatars.forEach((av, vi) => POSES.forEach((pose, pi) => {
  for (let f = 0; f < 4; f++) {
    const { g, drawn } = render(av, acc, pose, f);
    const codes = codesOf(g);
    cat.push({ ai, vi, pi, f, crc: crcOf(codes), drawn, codes, full: (pose === 'working' && f === 0) || (pose === 'done' && f === 2) });
  }
})));

// ---------------------------------------------------------------- casi costruiti (draw-accessory.test.mjs)
const blank = () => Array.from({ length: H }, () => '.'.repeat(W));
const put = (rows, x, y, s) => { rows[y] = rows[y].slice(0, x) + s + rows[y].slice(x + s.length); };
function frameAt(dy) {
  const r = blank();
  for (let y = 10; y <= 21; y++) put(r, 8, y + dy, '#'.repeat(12));
  put(r, 10, 13 + dy, '++'); put(r, 16, 13 + dy, '++');
  return r;
}
function mini(withAnchors = true) {
  const av = { format: 'calamaio-avatar', version: 1, id: 'mini', name: 'Mini', author: 't', description: '', w: W, h: H, frames: 2, poses: {} };
  REQUIRED.forEach((p, i) => {
    av.poses[p] = [frameAt(0), frameAt(-1)].map((fr) => { const c = fr.slice(); put(c, 0, 24, '#'.repeat(i + 1)); return c; });
  });
  if (withAnchors) {
    av.anchors = {};
    for (const p of REQUIRED) av.anchors[p] = [0, -1].map((dy) => ({ 'head-top': [13, 10 + dy], eyes: [13, 13 + dy], mouth: [13, 17 + dy], neck: [13, 19 + dy], hand: null }));
  }
  return av;
}
const ACC = {
  format: 'calamaio-accessory', version: 1, id: 'prova', name: 'Prova', author: 't', description: '', special: false,
  w: 3, h: 2, frames: 1, anchor: 'head-top', origin: [1, 1], outline: false, hidePoses: [], pixels: [['###', 'x+x']]
};
const avMini = mini(), avEyes = clone(avMini), avCorner = clone(avMini);
avEyes.anchors.working[0].eyes = [10, 13];
avCorner.anchors.working[0]['head-top'] = [27, 0];
const miniCavt = pack(avMini);
const ANCS = [['mini', avMini], ['mini-occhio', avEyes], ['mini-angolo', avCorner]].map(([name, av]) => ({ name, av, bin: packAnchors(av) }));
const ACCS = [
  ['base', ACC],
  ['x-sopra', { ...clone(ACC), id: 'x-sopra', pixels: [['xxx', '...']] }],
  ['x-occhio', { ...clone(ACC), id: 'x-occhio', anchor: 'eyes', origin: [0, 0], w: 1, h: 1, pixels: [['x']] }],
  ['contorno', { ...clone(ACC), id: 'contorno', outline: true, pixels: [['...', '.#.']] }],
  ['contorno-x', { ...clone(ACC), id: 'contorno-x', outline: true, pixels: [['...', '.x.']] }],
  ['nascosto', { ...clone(ACC), id: 'nascosto', hidePoses: ['error', 'tired'] }],
  ['mano', { ...clone(ACC), id: 'mano', anchor: 'hand' }],
  ['ritocco', { ...clone(ACC), id: 'ritocco', perAvatar: { mini: { dx: 2, dy: -1 } } }],
  ['ritocco-px', { ...clone(ACC), id: 'ritocco-px', perAvatar: { altro: { dx: 5 }, mini: { pixels: [['+++', '...']] } } }],
  ['due', { ...clone(ACC), id: 'due', frames: 2, pixels: [['###', '...'], ['+++', '...']] }]
].map(([name, a]) => ({ name, a, bin: packAccessory(a) }));
// [accessorio, ancore (-1 = avatar senza ancore), posa, f]
const CASES = [
  ['base', 0, 'working', 0], ['x-sopra', 0, 'working', 0], ['x-occhio', 1, 'working', 0],
  ['contorno', 0, 'working', 0], ['contorno-x', 0, 'working', 0],
  ['nascosto', 0, 'error', 0], ['nascosto', 0, 'tired', 0], ['base', -1, 'working', 0], ['mano', 0, 'working', 0],
  ['ritocco', 0, 'working', 0], ['ritocco-px', 0, 'working', 0], ['due', 0, 'working', 3], ['due', 0, 'working', 2],
  ['base', 0, 'tired', 0], ['base', 0, 'run', 1], ['base', 2, 'working', 0], ['base', 0, 'done', 1]
].map(([acc, anc, pose, f]) => {
  const A = ACCS.find((x) => x.name === acc), av = anc < 0 ? mini(false) : ANCS[anc].av;
  const { g, drawn } = render(av, A.a, pose, f);
  return { acc: ACCS.indexOf(A), anc, pose: POSES.indexOf(pose), f, drawn, rows: g.map((r) => r.join('')).join('') };
});

// ---------------------------------------------------------------- golden.h
const hex8 = (n) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(8, '0');
const bytes = (buf) => {
  let o = '';
  for (let i = 0; i < buf.length; i += 24) o += '  ' + Array.from(buf.subarray(i, i + 24), (b) => '0x' + b.toString(16).padStart(2, '0')).join(', ') + ',\n';
  return o;
};
let out = '// Generato da tools/golden/accessory-golden.mjs dal disegno di riferimento tools/avatar/lib/draw-accessory.mjs:\n';
out += '// NON modificare a mano. Griglie 28×25 riga per riga: \'.\' trasparente, \'#\' inchiostro, \'+\' carta.\n#pragma once\n#include <stdint.h>\n\n';
out += `static const char *const kGoldenAvatars[${AV_IDS.length}] = {${AV_IDS.map((s) => `"${s}"`).join(', ')}};\n`;
out += `static const char *const kGoldenAccessories[${ACC_IDS.length}] = {${ACC_IDS.map((s) => `"${s}"`).join(', ')}};\n\n`;
out += '// Catalogo: CRC-32 dei 700 codici (0, 1, 2) della griglia con l\'accessorio, e se l\'accessorio si disegna.\n';
out += 'struct GoldenCrc { uint8_t acc, av, pose, f; uint32_t crc; bool drawn; };\n';
out += `static const GoldenCrc kGoldenCrc[${cat.length}] = {\n`;
for (const c of cat) out += `  {${c.ai}, ${c.vi}, ${c.pi}, ${c.f}, ${hex8(c.crc)}, ${c.drawn}},\n`;
out += '};\n\n';
const full = cat.filter((c) => c.full);
out += 'struct GoldenGrid { uint8_t acc, av, pose, f; const char *rows; };\n';
out += `static const GoldenGrid kGoldenGrids[${full.length}] = {\n`;
for (const c of full) out += `  {${c.ai}, ${c.vi}, ${c.pi}, ${c.f}, "${Array.from(c.codes, (v) => '.#+'[v]).join('')}"},\n`;
out += '};\n\n';
out += '// Casi costruiti: avatar «mini» (CAVT), ancore (CANC: 0 mini, 1 occhio sinistro in (10,13) in working[0],\n';
out += '// 2 head-top in (27,0) in working[0]), accessori di prova (CACC).\n';
out += `static const uint8_t kMiniCavt[${miniCavt.length}] = {\n${bytes(miniCavt)}};\n`;
ANCS.forEach((a, i) => { out += `static const uint8_t kMiniAnc${i}[${a.bin.length}] = {\n${bytes(a.bin)}};\n`; });
out += `static const uint8_t *const kMiniAnc[${ANCS.length}] = {${ANCS.map((a, i) => `kMiniAnc${i}`).join(', ')}};\n`;
out += `static const uint16_t kMiniAncLen[${ANCS.length}] = {${ANCS.map((a) => a.bin.length).join(', ')}};\n`;
ACCS.forEach((a, i) => { out += `// ${a.name}\nstatic const uint8_t kTestAcc${i}[${a.bin.length}] = {\n${bytes(a.bin)}};\n`; });
out += `static const uint8_t *const kTestAcc[${ACCS.length}] = {${ACCS.map((a, i) => `kTestAcc${i}`).join(', ')}};\n`;
out += `static const uint16_t kTestAccLen[${ACCS.length}] = {${ACCS.map((a) => a.bin.length).join(', ')}};\n`;
out += `static const char *const kTestAccName[${ACCS.length}] = {${ACCS.map((a) => `"${a.name}"`).join(', ')}};\n\n`;
out += '// anc = -1: avatar senza ancore.\nstruct GoldenCase { uint8_t acc; int8_t anc; uint8_t pose, f; bool drawn; const char *rows; };\n';
out += `static const GoldenCase kGoldenCases[${CASES.length}] = {\n`;
for (const c of CASES) out += `  {${c.acc}, ${c.anc}, ${c.pose}, ${c.f}, ${c.drawn}, "${c.rows}"},\n`;
out += '};\n';

// ---------------------------------------------------------------- simulatore
// Si carica il motore del simulatore in una sandbox (come il browser: avatars.js poi calamaio-test.js) e si rifanno
// gli stessi casi con il suo drawAccessory, sui byte che ha caricato lui (catalogo) e su quelli dei casi costruiti.
function simEngine() {
  const dir = path.join(REPO, 'reference', 'calamaio-os-browser', 'assets');
  const ctx = { console, TextDecoder, Uint8Array, Set, Math, Object, Array, String, Date, JSON, Number };
  ctx.window = ctx;
  ctx.DCLogic = class {};
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(dir, 'avatars.js'), 'utf8'), ctx);
  vm.runInContext(fs.readFileSync(path.join(dir, 'calamaio-test.js'), 'utf8'), ctx);
  return ctx.window.CalamaioTest.Component.build();
}
const E = simEngine();
const problems = [];
if (Object.keys(E.ACCESSORIES).length !== ACC_IDS.length) problems.push(`simulatore: ${Object.keys(E.ACCESSORIES).length} accessori caricati invece di ${ACC_IDS.length}`);
for (const id of AV_IDS) if (!E.ANCHORS[id]) problems.push(`simulatore: ancore di ${id} non caricate`);
let simOk = 0;
for (const c of cat) {
  const a = E.AVATARS[AV_IDS[c.vi]], acc = E.ACCESSORIES[ACC_IDS[c.ai]];
  if (!a || !acc) continue;
  const g = E.avGrid(a, POSES[c.pi], c.f);
  const drawn = E.drawAccessory(g, a, E.ANCHORS[a.id] || null, acc, POSES[c.pi], c.f);
  if (crcOf(g) !== c.crc || drawn !== c.drawn) problems.push(`simulatore: ${ACC_IDS[c.ai]} su ${AV_IDS[c.vi]} ${POSES[c.pi]}[${c.f}] diverso dal riferimento`);
  else simOk++;
}
const simMini = E.parseAvatar(Uint8Array.from(miniCavt));
for (const c of CASES) {
  const acc = E.parseAccessory(Uint8Array.from(ACCS[c.acc].bin)), anc = c.anc < 0 ? null : E.parseAnchors(Uint8Array.from(ANCS[c.anc].bin));
  if (!simMini || !acc || (c.anc >= 0 && !anc)) { problems.push(`simulatore: caso ${ACCS[c.acc].name}: CAVT, CACC o CANC rifiutato`); continue; }
  const g = E.avGrid(simMini, POSES[c.pose], c.f);
  const drawn = E.drawAccessory(g, simMini, anc, acc, POSES[c.pose], c.f);
  if (Array.from(g, (v) => '.#+'[v]).join('') !== c.rows || drawn !== c.drawn) problems.push(`simulatore: caso ${ACCS[c.acc].name} ${POSES[c.pose]}[${c.f}] diverso dal riferimento`);
  else simOk++;
}

const check = process.argv.includes('--check');
const cur = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : '';
if (check && cur !== out) problems.push('golden.h da rigenerare: node tools/golden/accessory-golden.mjs');
if (!check) { fs.mkdirSync(path.dirname(OUT), { recursive: true }); fs.writeFileSync(OUT, out); console.log('scritto', path.relative(REPO, OUT), out.length, 'byte'); }
const nDrawn = cat.filter((c) => c.drawn).length;
console.log(`catalogo: ${cat.length} casi (${nDrawn} con l'accessorio disegnato, ${cat.length - nDrawn} nascosto o senza punto), ${full.length} griglie intere; casi costruiti: ${CASES.length}`);
console.log(`simulatore: ${simOk} di ${cat.length + CASES.length} casi uguali al riferimento`);
for (const p of problems.slice(0, 20)) console.log('  ' + p);
if (problems.length > 20) console.log(`  … e altri ${problems.length - 20}`);
process.exit(problems.length ? 1 : 0);
