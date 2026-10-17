// Demone: sagoma nera e snella di profilo, muso allungato coi dentini, due cornetti, UN grande occhio
// tondo; braccia sottili con gli artigli, gambe sottili e una lunga coda a freccia che sale in alto a sinistra
// (l'angolo in alto a destra resta alla nuvoletta). Stessa grammatica della Goccia (lib/kit.mjs).
import { make, FACE, stateMark } from '../lib/kit.mjs';

// Sagoma senza braccia né coda, coordinate assolute (y 3..23). Muso a sinistra, cornetti in alto:
// a x ≥ 16 niente sopra y 9, così la nuvoletta resta libera anche col sobbalzo di permission.
const BODY = [
  '.............#..#...........',  // 3  punte dei cornetti, piegate all'indietro
  '............#..#............',  // 4
  '...........##.##............',  // 5
  '.........######.............',  // 6  testa
  '.......#########............',  // 7  occhio 7..11
  '.....###########............',  // 8
  '....#############...........',  // 9  punta del muso
  '....#############...........',  // 10
  '....#############...........',  // 11
  '.....############...........',  // 12 bocca
  '.....###########............',  // 13 mascella
  '.......########.............',  // 14 mento
  '...........####.............',  // 15 collo
  '..........######............',  // 16 spalle
  '..........######............',  // 17
  '..........######............',  // 18
  '...........####.............',  // 19
  '...........#..#.............',  // 20 gambe
  '..........#....#............',  // 21
  '.........#......#...........',  // 22
  '.......###......###.........'   // 23 piedi
];
const TOP = 3;

// Occhio grande 6×5 dagli angoli smussati, in carta: ex = colonna sinistra, ey = riga alta.
function bigEye(pen, ex, ey, k) {
  const O = (x, y) => pen.O(ex + x, ey + y);
  if (k === 'closed') { [[0, 2], [1, 3], [2, 3], [3, 3], [4, 3], [5, 2]].forEach(([x, y]) => O(x, y)); return; }
  if (k === 'tired') {                                   // palpebra calata a metà: si vede solo la pupilla in basso
    for (let x = 0; x < 6; x++) { O(x, 2); O(x, 3); } for (let x = 1; x < 5; x++) O(x, 4);
    [[2, 3], [3, 3], [2, 4], [3, 4]].forEach(([x, y]) => pen.I(ex + x, ey + y)); return;
  }
  if (k === 'happy') { [[0, 3], [1, 2], [2, 1], [3, 1], [4, 2], [5, 3]].forEach(([x, y]) => O(x, y)); return; }
  if (k === 'x') { [[0, 0], [5, 0], [1, 1], [4, 1], [2, 2], [3, 2], [1, 3], [4, 3], [0, 4], [5, 4]].forEach(([x, y]) => O(x, y)); return; }
  for (let y = 0; y < 5; y++) for (let x = 0; x < 6; x++) if (!((y === 0 || y === 4) && (x === 0 || x === 5))) O(x, y);
  const [px, py] = { open: [1, 1], down: [2, 2], up: [3, 0], side: [3, 1] }[k] || [1, 1];
  [[0, 0], [1, 0], [0, 1], [1, 1]].forEach(([x, y]) => pen.I(ex + px + x, ey + py + y));
}

// Bocca sotto il muso (riga my = 12), in carta, aperta sul davanti: il muso sporge di un punto sulla mascella.
// Finisce a x 8-9 per restare staccata dall'occhio. Dentini: una zanna di carta sotto la riga (bocca chiusa)
// o d'inchiostro dentro la bocca spalancata.
function mouth(pen, my, k) {
  const O = (x, y) => pen.O(x, my + y), I = (x, y) => pen.I(x, my + y);
  if (k === 'grin') { for (let x = 5; x <= 9; x++) { O(x, 0); O(x, 1); } I(6, 0); I(8, 0); return; }
  if (k === 'o') { for (let x = 5; x <= 7; x++) { O(x, 0); O(x, 1); } return; }
  if (k === 'frown') { O(5, 0); O(6, 0); O(7, 0); O(8, 1); O(9, 1); return; }
  for (let x = 5; x <= 8; x++) O(x, 0); O(7, 1);
}

// Braccia a destra (profilo): F dalla spalla (16,16), B dalla vita (16,18). Punti assoluti.
const ARMS = {
  rest: [[16, 16], [17, 16], [18, 17], [19, 18], [19, 19], [18, 20], [20, 20]],
  typeA: [[16, 16], [17, 16], [18, 16], [19, 16], [20, 16], [21, 17], [20, 17], [16, 18], [17, 18], [18, 18], [19, 19], [18, 19]],
  typeB: [[16, 16], [17, 16], [18, 16], [19, 16], [20, 17], [19, 17], [16, 18], [17, 18], [18, 18], [19, 18], [20, 19], [19, 19]],
  upF0: [[16, 16], [17, 15], [18, 14], [19, 13], [20, 12], [20, 11], [19, 10], [21, 10]],
  upF1: [[16, 16], [17, 15], [18, 14], [19, 14], [20, 13], [21, 12], [21, 11], [20, 10], [22, 10]],
  upB0: [[16, 18], [17, 17], [18, 16], [19, 15], [20, 14], [21, 13], [22, 12], [22, 11], [21, 11], [23, 11]],
  upB1: [[16, 18], [17, 17], [18, 17], [19, 16], [20, 15], [21, 15], [22, 14], [23, 13], [23, 12], [22, 12], [24, 12]],
  cheerB: [[16, 18], [17, 17], [18, 17], [19, 16], [20, 16], [21, 15], [22, 15], [23, 14], [23, 13], [24, 15]],
  droop: [[16, 17], [17, 17], [18, 18], [19, 19], [18, 20], [20, 20]]
};

// Coda: dalla schiena giù verso sinistra, poi su lungo il bordo fino alla freccia in alto a sinistra.
const TAIL = [[10, 19], [9, 20], [8, 21], [7, 21], [6, 21], [5, 21], [4, 20], [3, 19], [2, 18], [2, 17], [1, 16], [1, 15], [1, 14], [1, 13], [1, 12], [1, 11], [1, 10], [1, 9], [1, 8], [2, 7]];
const ARROW = (s) => [[2, 3], [1, 4], [2, 4], [3, 4], [0, 5], [1, 5], [2, 5], [3, 5], [4, 5], [2, 6]].map(([x, y]) => [x + s, y]);
// Coda a terra (errore, stanchezza): la freccia punta a sinistra sul pavimento.
const TAIL_DOWN = [[10, 19], [9, 20], [8, 21], [7, 22], [6, 22], [5, 22], [4, 22], [3, 22], [2, 22], [1, 22], [2, 21], [2, 23], [3, 21], [3, 23], [0, 22]];

// Corsa (posa run): il Demone si gira verso destra (specchio della sagoma, muso sul bordo destro, colonna 27),
// testa e busto come nelle altre pose, gambe sottili coi piedi sulla riga 24, braccia che pompano e coda a
// freccia tesa all'indietro che frusta su e giù. Falcata (0, 2) e gambe raccolte col sobbalzo (1, 3).
const MIRROR = 31;                                   // x → 31 − x: la punta del muso (x 4) va in colonna 27
const RUN_HEAD = BODY.slice(0, 17).map((r) => ('.'.repeat(MIRROR - 27) + Array.from(r).reverse().join('')).slice(0, 28));
const RUN_TOP = 4;                                   // un punto più in basso: i piedi passano dalla riga 23 alla 24
const RUN_LEGS = {
  stride: [[20, 21], [21, 22], [22, 23], [23, 24], [24, 24], [25, 24], [17, 21], [16, 22], [15, 23], [14, 24], [13, 24], [12, 24]],
  // raccolta: gamba d'appoggio dritta, l'altra col ginocchio alzato davanti (la coda resta libera dietro)
  gather: [[19, 20], [19, 21], [19, 22], [19, 23], [19, 24], [20, 24], [21, 24], [20, 20], [21, 20], [22, 21], [22, 22], [23, 22]]
};
// Braccio vicino, dalla spalla: avanti nella falcata 0, indietro e in su nella 2, piegato al petto nelle raccolte.
const RUN_ARMS = {
  fwd: [[22, 18], [23, 18], [24, 17], [25, 16], [25, 15]],
  back: [[15, 17], [14, 16], [13, 15], [12, 15]],
  mid: [[22, 18], [23, 18], [24, 17]]
};
// Coda dalla groppa: indietro in basso, poi su come nelle altre pose, con la freccia in alto a sinistra.
// sway = 0/1: la parte alta oscilla di un punto a sinistra.
const RUN_TAIL = (sway) => [[15, 20], [14, 21], [13, 21], [12, 21], [11, 20], [10, 19], [9, 18], [9, 17],
  [8 - sway, 16], [8 - sway, 15], [8 - sway, 14], [8 - sway, 13], [8 - sway, 12]];
const RUN_ARROW = (sway) => ARROW(6 - sway).map(([x, y]) => [x, y + 5]);

function runFrame(pen, f, dy) {
  const y0 = RUN_TOP + dy;
  pen.stamp(RUN_HEAD, 0, y0);
  const I = ([x, y]) => pen.I(x, y + dy);
  (f % 2 ? RUN_LEGS.gather : RUN_LEGS.stride).forEach(([x, y]) => pen.I(x, y));
  RUN_ARMS[f === 0 ? 'fwd' : f === 2 ? 'back' : 'mid'].forEach(I);
  const sway = f === 1 || f === 2 ? 1 : 0;
  RUN_TAIL(sway).forEach(I); RUN_ARROW(sway).forEach(I);
  bigEye(pen, 17, y0 + 4, 'side');
  [23, 24, 25, 26].forEach((x) => pen.O(x, y0 + 9)); pen.O(24, y0 + 10);   // bocca col dentino, sotto il muso
}

export default function demone() {
  return make({
    id: 'demone', name: 'Demone', author: 'Calamaio OS',
    description: 'Un demonietto nero e snello: un grande occhio tondo, muso coi dentini, cornetti e coda a freccia.'
  }, (pen, pose, f, dy) => {
    if (pose === 'run') return runFrame(pen, f, dy);
    const y0 = TOP + dy;
    const sq = pose === 'idle' && f > 1 ? 1 : 0;   // respiro: le punte dei cornetti si abbassano
    pen.stamp(BODY.slice(sq), 0, y0 + sq);
    const I = ([x, y]) => pen.I(x, Math.min(24, y + dy));
    if (pose === 'error' || pose === 'tired') TAIL_DOWN.forEach(I);
    else { TAIL.forEach(I); ARROW((pose === 'working' || pose === 'idle' || pose === 'done') ? f % 2 : 0).forEach(I); }
    const face = FACE[pose](f);
    bigEye(pen, 9, y0 + 4, face.eyes);
    mouth(pen, y0 + 9, face.mouth);
    const arms = {
      working: [f % 2 ? 'typeB' : 'typeA'], compact: [f % 2 ? 'typeB' : 'typeA'],
      input: [f % 2 ? 'upF1' : 'upF0', 'rest'], permission: [f % 2 ? 'upF1' : 'upF0', f % 2 ? 'upB0' : 'upB1'],
      done: dy < 0 ? ['upF0', 'cheerB'] : ['rest'], error: ['droop'], tired: ['droop'], stuffy: ['droop'], idle: ['rest']
    }[pose];
    arms.forEach((k) => ARMS[k].forEach(I));
    if (pose === 'error') pen.drop(19, y0 + 1 + (f % 2));
    else if (pose === 'tired') pen.dropL(3, y0 + 2 + (f % 2));
    else stateMark(pen, pose, f, { sparkA: [[21, 2], [25, 7]], sparkB: [[24, 1], [20, 6]], zPts: [[18, 8], [20, 5], [22, 2], null], wavePts: [[6, 0], [9, 1], [21, 1], [24, 0]] });
  });
}
