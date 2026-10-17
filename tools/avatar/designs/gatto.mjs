// Gatto: testa grande con orecchie a punta, corpo seduto, coda che spazza per terra.
// Stessa grammatica della Goccia (lib/kit.mjs): nuvoletta, simboli, salti, Z, sudore, aria.
import { make, FACE, stateMark } from '../lib/kit.mjs';

// Sagoma a riposo, coordinate assolute (y 10..23). '+' = carta (interno delle orecchie).
const BODY = [
  '.....##............##.......',  // 10 orecchie (y ≥ 10: anche col sobbalzo di permission restano fuori dalla nuvoletta)
  '.....#+#..........#+#.......',  // 11
  '.....################.......',  // 12
  '.....################.......',  // 13 occhi 13..16
  '.....################.......',  // 14
  '....##################......',  // 15 guance
  '....##################......',  // 16
  '....##################......',  // 17 naso
  '.....################.......',  // 18 bocca
  '......##############........',  // 19
  '.......############.........',  // 20 collo
  '......##############........',  // 21
  '......##############........',  // 22
  '.......###......###.........'   // 23 zampe
];
const TOP = 10;

// Braccio destro con zampa (2×2 in cima ai bracci alzati); il sinistro è lo specchio x → 25 - x.
const ARM = {
  rest: [[20, 21], [21, 22]],
  type: [[20, 21], [21, 21]],
  droop: [[20, 22], [20, 23]],
  up0: [[20, 20], [21, 19], [22, 18], [23, 17], [23, 16], [23, 15], [24, 15], [23, 14], [24, 14]],
  up1: [[20, 20], [21, 20], [22, 19], [23, 18], [24, 17], [24, 16], [25, 16], [24, 15], [25, 15]],
  cheer: [[20, 20], [21, 19], [22, 18], [23, 17], [24, 16], [24, 15], [25, 15], [25, 16]]
};
// Corsa (posa run): gatto al galoppo di profilo verso destra. Muso sul bordo destro (colonna 27), coda dritta
// indietro e in su. Falcata (0, 2): zampe anteriori avanti e posteriori indietro; raccolta (1, 3): zampe sotto
// la pancia e schiena alzata di 1. Coordinate assolute a dy = 0.
const RUN_BODY = [
  '..................##....##..',  // 6  orecchie
  '..................#+#..#+#..',  // 7
  '..................#########.',  // 8
  '.................###########',  // 9  occhi 10..13
  '.................###########',  // 10
  '.................###########',  // 11
  '......############.#########',  // 12 schiena
  '.....#######################',  // 13
  '....########################',  // 14 naso e bocca
  '....######################..',  // 15 mento
  '....#################.......',  // 16
  '....################........',  // 17
  '.....##############.........',  // 18
  '......###########...........'   // 19 pancia
];
const RUN_TOP = 6;
const RUN_LEGS = {
  // falcata: anteriori dal petto in avanti, posteriori dalla groppa all'indietro (larghe 2)
  stride: [[16, 20], [17, 20], [17, 21], [18, 21], [19, 22], [20, 22], [20, 23], [21, 23], [22, 24], [23, 24], [24, 24],
           [6, 20], [7, 20], [5, 21], [6, 21], [4, 22], [5, 22], [3, 23], [4, 23], [1, 24], [2, 24], [3, 24]],
  // raccolta: anteriori all'indietro e posteriori in avanti, i piedi vicini sotto la pancia
  gather: [[15, 19], [16, 19], [15, 20], [16, 20], [14, 21], [15, 21], [14, 22], [15, 22], [13, 23], [14, 23], [12, 24], [13, 24], [14, 24],
           [7, 19], [8, 19], [8, 20], [9, 20], [9, 21], [10, 21], [10, 22], [11, 22], [11, 23], [9, 24], [10, 24], [11, 24]]
};
// Coda: dalla groppa indietro e in su; la punta sale (0) o scende (2) di un punto.
const RUN_TAIL = (up) => [[5, 12], [4, 12], [4, 11], [3, 11], [3, 10], [2, 10], [2, 9], [1, 9 - up], [1, 8 - up], [0, 8 - up], [0, 7 - up]];

function runFrame(pen, f, dy) {
  const y0 = RUN_TOP + dy;
  pen.stamp(RUN_BODY, 0, y0);
  RUN_TAIL(f === 2 ? 0 : 1).forEach(([x, y]) => pen.I(x, y + dy));
  (f % 2 ? RUN_LEGS.gather : RUN_LEGS.stride).forEach(([x, y]) => pen.I(x, y));
  // tre quarti: occhio vicino 4×4, occhio lontano schiacciato a 3 colonne, pupille in avanti
  pen.eye(19, y0 + 4, 'side');
  for (let y = 0; y < 4; y++) for (let x = 0; x < 3; x++) pen.O(24 + x, y0 + 4 + y);
  [[25, 1], [26, 1], [25, 2], [26, 2]].forEach(([x, y]) => pen.I(x, y0 + 4 + y));
  pen.O(25, y0 + 8); pen.O(26, y0 + 8);                        // naso
}

// Coda a destra, per terra; la punta oscilla di un punto.
const TAIL = (tip) => [[20, 23], [21, 23], [22, 23], [23, 22], [24, 21 - tip]];

export default function gatto() {
  return make({
    id: 'gatto', name: 'Gatto', author: 'Calamaio OS',
    description: 'Un gatto nero seduto: orecchie a punta, muso a ω e coda che spazza per terra.'
  }, (pen, pose, f, dy) => {
    if (pose === 'run') return runFrame(pen, f, dy);
    const y0 = TOP + dy;
    pen.stamp(BODY, 0, y0);
    if (pose === 'idle' && f > 1) {               // respiro di idle: le orecchie si abbassano di un punto
      [5, 6, 19, 20].forEach((x) => pen.E(x, y0)); pen.I(6, y0 + 1); pen.I(19, y0 + 1);
    }
    // coda
    const tip = (pose === 'working' || pose === 'idle' || pose === 'done') ? f % 2 : 0;
    if (pose !== 'error' && pose !== 'tired') TAIL(tip).forEach(([x, y]) => pen.I(x, y + dy));
    else [[20, 23], [21, 23], [22, 23], [23, 23]].forEach(([x, y]) => pen.I(x, Math.min(24, y + dy)));  // coda a terra, moscia
    // faccia
    const face = FACE[pose](f);
    pen.eye(7, y0 + 3, face.eyes); pen.eye(15, y0 + 3, face.eyes);
    // naso a goccia rovesciata e bocca a ω
    const ny = y0 + 7;
    if (face.mouth !== 'frown') { pen.O(12, ny); pen.O(13, ny); }
    if (face.mouth === 'grin') [[10, 1], [11, 1], [14, 1], [15, 1], [12, 2], [13, 2]].forEach(([x, y]) => pen.O(x, ny + y));
    else if (face.mouth === 'o') [[11, 1], [14, 1], [12, 2], [13, 2]].forEach(([x, y]) => pen.O(x, ny + y));
    else if (face.mouth === 'frown') [[11, 2], [12, 1], [13, 1], [14, 2]].forEach(([x, y]) => pen.O(x, ny + y));
    else { pen.O(11, ny + 1); pen.O(14, ny + 1); }
    // braccia
    const arms = {
      working: [f % 2 ? 'type' : 'rest', f % 2 ? 'rest' : 'type'], compact: [f % 2 ? 'type' : 'rest', f % 2 ? 'rest' : 'type'],
      input: [f % 2 ? 'up1' : 'up0', 'rest'], permission: [f % 2 ? 'up1' : 'up0', f % 2 ? 'up0' : 'up1'],
      done: dy < 0 ? ['cheer', 'cheer'] : ['rest', 'rest'], error: ['droop', 'droop'], tired: ['droop', 'droop'],
      stuffy: ['droop', 'droop'], idle: [null, null]
    }[pose];
    if (arms[0]) ARM[arms[0]].forEach(([x, y]) => pen.I(x, y + dy));
    if (arms[1]) ARM[arms[1]].forEach(([x, y]) => pen.I(25 - x, y + dy));
    // simboli di stato
    if (pose === 'error') pen.drop(23, y0 + 2 + (f % 2));
    else if (pose === 'tired') pen.dropL(3, y0 + 1 + (f % 2));
    else stateMark(pen, pose, f, { sparkA: [[2, 4], [24, 8]], sparkB: [[3, 10], [25, 3]], zPts: [[22, 7], [23, 4], [24, 1], null] });
  });
}
