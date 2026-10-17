// Test del disegno di riferimento degli accessori e dei controlli visivi (docs/AVATAR.md §14.2, §14.4).
// Uso: node --test tools/avatar/test/*.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { W, H, REQUIRED } from '../lib/format.mjs';
import { drawAccessory, checkAccessory, toGrid, fromGrid, renderWith, MIN_CONTRAST } from '../lib/draw-accessory.mjs';

const clone = (o) => JSON.parse(JSON.stringify(o));
const blank = () => Array.from({ length: H }, () => '.'.repeat(W));
const put = (rows, x, y, s) => { rows[y] = rows[y].slice(0, x) + s + rows[y].slice(x + s.length); };

// Avatar minimo: un blocco d'inchiostro x 8..19, y 10..21, due occhi di carta in y 13; fotogrammi 0 e 1 (1 = su di 1).
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
// Accessorio 3×2 a `head-top`: riga 0 = ### (sopra la testa, su trasparente), riga 1 = x+x (sulla testa).
const ACC = {
  format: 'calamaio-accessory', version: 1, id: 'prova', name: 'Prova', author: 't', description: '', special: false,
  w: 3, h: 2, frames: 1, anchor: 'head-top', origin: [1, 1], outline: false, hidePoses: [],
  pixels: [['###', 'x+x']]
};
const at = (grid, x, y) => grid[y][x];

test('x: inchiostro → carta, trasparente → inchiostro, carta → inchiostro; # e + coprono', () => {
  const av = mini(), g = toGrid(av.poses.working[0]);
  const res = drawAccessory(g, av, ACC, 'working', 0);
  assert.ok(res, 'disegnato');
  // origine (1,1) sul punto (13,10): l'accessorio occupa x 12..14, y 9..10
  assert.equal(at(g, 12, 9), '#'); assert.equal(at(g, 13, 9), '#'); assert.equal(at(g, 14, 9), '#');   // # su trasparente
  assert.equal(at(g, 12, 10), '+'); assert.equal(at(g, 14, 10), '+');                                     // x su inchiostro
  assert.equal(at(g, 13, 10), '+');                                                                        // + su inchiostro
  const t = clone(ACC); t.pixels = [['xxx', '...']];
  const g2 = toGrid(av.poses.working[0]);
  drawAccessory(g2, av, t, 'working', 0);
  assert.equal(at(g2, 12, 9), '#', 'x sopra il trasparente diventa inchiostro');
  // x sopra la carta (occhio) diventa inchiostro
  const e = { ...clone(ACC), anchor: 'eyes', origin: [0, 0], pixels: [['x']] };
  e.w = 1; e.h = 1;
  const g3 = toGrid(av.poses.working[0]);
  const av3 = clone(av); av3.anchors.working[0].eyes = [10, 13];
  drawAccessory(g3, av3, e, 'working', 0);
  assert.equal(at(g3, 10, 13), '#');
  assert.equal(at(g3, 11, 13), '+', 'il resto dell\'occhio non cambia');
});

test('contorno: carta sull\'inchiostro della mascotte attorno ai pixel d\'inchiostro dell\'accessorio', () => {
  const av = mini();
  const a = { ...clone(ACC), outline: true, pixels: [['...', '.#.']] };   // un pixel d'inchiostro sulla testa (13,10)
  const g = toGrid(av.poses.working[0]);
  const res = drawAccessory(g, av, a, 'working', 0);
  assert.equal(at(g, 13, 10), '#');
  // vicini sull'inchiostro → carta; quelli sul trasparente (riga 9) restano trasparenti
  for (const [x, y] of [[12, 10], [14, 10], [12, 11], [13, 11], [14, 11]]) assert.equal(at(g, x, y), '+', `(${x},${y})`);
  for (const [x, y] of [[12, 9], [13, 9], [14, 9]]) assert.equal(at(g, x, y), '.', `(${x},${y})`);
  assert.equal(res.ring.length, 5);
  // senza outline nessun contorno
  const g2 = toGrid(av.poses.working[0]);
  drawAccessory(g2, av, { ...a, outline: false }, 'working', 0);
  assert.equal(at(g2, 12, 10), '#');
  // un pixel x che diventa carta non ha bisogno di contorno
  const g3 = toGrid(av.poses.working[0]);
  const r3 = drawAccessory(g3, av, { ...a, pixels: [['...', '.x.']] }, 'working', 0);
  assert.equal(r3.ring.length, 0);
  assert.equal(at(g3, 12, 10), '#');
});

test('pose nascoste e avatar senza ancore: griglia invariata', () => {
  const av = mini();
  const g = toGrid(av.poses.error[0]), before = fromGrid(g);
  assert.equal(drawAccessory(g, av, { ...ACC, hidePoses: ['error'] }, 'error', 0), null);
  assert.deepEqual(fromGrid(g), before);
  const bare = mini(false), g2 = toGrid(bare.poses.working[0]), b2 = fromGrid(g2);
  assert.equal(drawAccessory(g2, bare, ACC, 'working', 0), null);
  assert.deepEqual(fromGrid(g2), b2);
  // punto assente (hand = null): niente
  const g3 = toGrid(av.poses.working[0]);
  assert.equal(drawAccessory(g3, av, { ...ACC, anchor: 'hand' }, 'working', 0), null);
});

test('ritocchi per avatar: spostamento e pixel sostitutivi; fotogrammi f mod frames', () => {
  const av = mini();
  const a = { ...clone(ACC), perAvatar: { mini: { dx: 2, dy: -1 } } };
  const g = toGrid(av.poses.working[0]);
  drawAccessory(g, av, a, 'working', 0);
  assert.equal(at(g, 14, 8), '#'); assert.equal(at(g, 16, 8), '#'); assert.equal(at(g, 12, 9), '.');
  const b = { ...clone(ACC), perAvatar: { altro: { dx: 5 }, mini: { pixels: [['+++', '...']] } } };
  const g2 = toGrid(av.poses.working[0]);
  drawAccessory(g2, av, b, 'working', 0);
  assert.equal(at(g2, 12, 9), '+'); assert.equal(at(g2, 12, 10), '#');
  // 2 fotogrammi: f = 3 → accessorio 1, avatar 1 (ancora su di 1)
  const c = { ...clone(ACC), frames: 2, pixels: [['###', '...'], ['+++', '...']] };
  const g3 = toGrid(av.poses.working[1]);
  drawAccessory(g3, av, c, 'working', 3);
  assert.equal(at(g3, 12, 8), '+');
});

test('ripiego della posa: tired senza disegno usa le ancore di idle', () => {
  const av = mini();
  const g = toGrid(av.poses.idle[0]);
  assert.ok(drawAccessory(g, av, ACC, 'tired', 0));
  assert.equal(at(g, 13, 9), '#');
  // nascosto in tired: conta la posa chiesta, anche se si disegna idle
  const g2 = toGrid(av.poses.idle[0]);
  assert.equal(drawAccessory(g2, av, { ...ACC, hidePoses: ['tired'] }, 'tired', 0), null);
});

test('fuori dal riquadro: tagliato, non si rompe', () => {
  const av = mini(); av.anchors.working[0]['head-top'] = [27, 0];
  const g = toGrid(av.poses.working[0]);
  const res = drawAccessory(g, av, ACC, 'working', 0);
  assert.equal(res.clipped, 4);   // riga -1 intera (3) e colonna 28 della riga 0 (1)
  assert.equal(at(g, 26, 0), '#'); assert.equal(at(g, 27, 0), '+');
});

test('renderWith: la mascotte con l\'accessorio, senza toccare l\'avatar', () => {
  const av = mini(), before = JSON.stringify(av);
  const rows = renderWith(av, ACC, 'working', 0);
  assert.equal(rows[9].slice(12, 15), '###');
  assert.equal(JSON.stringify(av), before);
  assert.deepEqual(renderWith(av, null, 'working', 0), av.poses.working[0]);
});

test('checkAccessory: nuvoletta, fuori riquadro, contrasto', () => {
  const av = mini();
  assert.deepEqual(checkAccessory(ACC, [av]), []);
  // nella nuvoletta (x 17..27, y 0..8) in working, non in done
  const b = clone(av);
  for (const p of REQUIRED) b.anchors[p].forEach((pt) => { pt['head-top'] = [20, 5]; });
  const pb = checkAccessory(ACC, [b]).join('\n');
  assert.match(pb, /mini working\[0,1\]: entra nell'angolo della nuvoletta/);
  assert.match(pb, /mini permission/); assert.match(pb, /mini input/);
  assert.doesNotMatch(pb, /mini done/);
  // fuori riquadro
  const c = clone(av); c.anchors.idle[1]['head-top'] = [0, 0];
  assert.match(checkAccessory(ACC, [c]).join('\n'), /mini idle\[1\]: 4 pixel fuori dal riquadro 28×25/);
  // contrasto: tutto inchiostro sopra l'inchiostro del corpo
  const dark = { ...clone(ACC), anchor: 'mouth', pixels: [['###', '###']] };
  const pd = checkAccessory(dark, [av]).join('\n');
  assert.match(pd, /mini working\[0,1\]: solo 0% dei pixel si distingue dal fondo/);
  assert.ok(MIN_CONTRAST === 0.6);
  // lo stesso con x: si legge
  assert.deepEqual(checkAccessory({ ...dark, pixels: [['xxx', 'xxx']] }, [av]), []);
  // con outline l'inchiostro sull'inchiostro si stacca grazie al contorno di carta: si legge
  assert.deepEqual(checkAccessory({ ...dark, outline: true }, [av]), []);
  // ma un blocco pieno con il centro lontano dal contorno no: 5×5, solo il bordo (16 su 25 = 64%) tocca il contorno…
  const big = { ...clone(ACC), anchor: 'mouth', origin: [2, 2], outline: true, w: 5, h: 5, pixels: [Array(5).fill('#####')] };
  const avBig = mini(); for (const p of REQUIRED) avBig.anchors[p].forEach((pt) => { pt.mouth = [13, 15]; });
  assert.deepEqual(checkAccessory(big, [avBig]), []);
  // …7×7: 24 su 49 = 48,9%, non basta
  const huge = { ...big, origin: [3, 3], w: 7, h: 7, pixels: [Array(7).fill('#######')] };
  assert.match(checkAccessory(huge, [avBig]).join('\n'), /solo 48% dei pixel si distingue/);
  // avatar senza ancore: nessun problema (non si disegna)
  assert.deepEqual(checkAccessory(dark, [mini(false)]), []);
});
