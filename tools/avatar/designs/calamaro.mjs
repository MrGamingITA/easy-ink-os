// Calamaro: il cugino della Goccia che vive nel calamaio. Mantello a punta di freccia con le pinne,
// occhi grandi, tentacoli che ondeggiano; due tentacoli lunghi fanno da braccia e, quando lavora,
// uno regge un pennino che scrive col suo inchiostro. Stessa grammatica della Goccia (lib/kit.mjs).
import { make, FACE, stateMark } from '../lib/kit.mjs';

const MANTLE = [
  '............##..............',  // 3  punta
  '...........####.............',  // 4
  '...........####.............',  // 5
  '..........######............',  // 6
  '..........######............',  // 7
  '.........########...........',  // 8  x ≤ 16 fino a y 9: la nuvoletta resta libera anche col sobbalzo
  '.........########...........',  // 9
  '......##############........',  // 10 pinne
  '.....################.......',  // 11
  '.......############.........',  // 12
  '......##############........',  // 13 testa
  '.....################.......',  // 14 occhi 14..17
  '.....################.......',  // 15
  '.....################.......',  // 16
  '.....################.......',  // 17
  '.....################.......',  // 18
  '......##############........'   // 19 bocca, attacco dei tentacoli
];
const TOP = 3;
// Tentacoli corti: due varianti che si alternano (ondeggiano di un punto in punta).
const LEGS = [
  ['......##.##.##.##.##.......',
   '......##.##.##.##.##.......',
   '.....##..##.##..#..##......',
   '.....#...#...#..#...#......'],
  ['......##.##.##.##.##.......',
   '......##.##.##.##.##.......',
   '......##..#.##.##..##......',
   '......#...#..#..#...#......']
];
// Tentacolo lungo destro (il sinistro è lo specchio x → 25 - x), come le braccia della Goccia.
const ARM = {
  rest: [[20, 19], [21, 20], [22, 21], [22, 22], [23, 23]],
  type: [[20, 19], [21, 19], [22, 20], [23, 20]],
  droop: [[20, 19], [20, 20], [21, 21], [21, 22], [21, 23], [22, 24]],
  up0: [[20, 18], [21, 17], [22, 16], [22, 15], [23, 14], [23, 13], [22, 12]],
  up1: [[20, 18], [21, 18], [22, 17], [23, 16], [24, 15], [24, 14], [23, 13]],
  cheer: [[20, 18], [21, 17], [22, 16], [23, 15], [24, 14], [25, 14]]
};
// Pennino tenuto dal tentacolo destro: asta diagonale e punta; scrive alzandosi di un punto.
const PEN = (b) => [[23, 20 - b], [24, 19 - b], [25, 18 - b], [26, 17 - b], [22, 21 - b]];

// Corsa (posa run): il Calamaro di profilo verso destra corre sui tentacoli. Mantello inclinato all'indietro
// (la punta resta indietro), testa con gli occhi in avanti sul bordo destro (colonna 27), tentacoli corti
// raccolti in due fasci che fanno da gambe, tentacoli lunghi che pompano come braccia.
const RUN_BODY = [
  '...........##...............',  // 3  punta, inclinata all'indietro
  '...........###..............',  // 4
  '............####............',  // 5
  '............#####...........',  // 6
  '.............######.........',  // 7
  '.............#######........',  // 8
  '..............########......',  // 9
  '...........##############...',  // 10 pinne
  '..........################..',  // 11
  '.............############...',  // 12
  '.............##############.',  // 13 testa
  '............################',  // 14 occhi 14..17
  '............################',  // 15
  '............################',  // 16
  '............################',  // 17
  '............################',  // 18
  '.............##############.'   // 19 bocca
];
const RUN_TOP = 3;
const RUN_LEGS = {
  // falcata: fascio dietro all'indietro, fascio davanti in avanti, il tentacolo di mezzo penzola
  stride: ['.............###.##.###.....',
           '............###..##..###....',
           '...........###...#....###...',
           '..........###.........###...',
           '.........##.............##..'],
  // raccolta (corpo alzato di 1): il fascio davanti d'appoggio, quello dietro calciato all'indietro
  gather: ['.............###.##.###.....',
           '..............###.#..###....',
           '...........#####.....###....',
           '..........##..........###...',
           '......................###...',
           '.....................####...']
};
// Tentacolo lungo dietro (dalla nuca verso sinistra), alternato su e giù; davanti uno piegato sotto la bocca.
const RUN_ARM = {
  up: [[11, 16], [10, 15], [9, 15], [8, 14], [7, 14], [6, 13]],
  down: [[11, 17], [10, 18], [9, 18], [8, 19], [7, 19], [6, 20]],
  mid: [[11, 16], [10, 17], [9, 17], [8, 17], [7, 18], [6, 18]]
};

function runFrame(pen, f, dy) {
  const y0 = RUN_TOP + dy;
  pen.stamp(RUN_BODY, 0, y0);
  const legs = f % 2 ? RUN_LEGS.gather : RUN_LEGS.stride;
  pen.stamp(legs, 0, 25 - legs.length);
  RUN_ARM[f === 0 ? 'up' : f === 2 ? 'down' : 'mid'].forEach(([x, y]) => pen.I(x, y + dy));
  pen.eye(15, y0 + 11, 'side'); pen.eye(21, y0 + 11, 'side');
  for (let x = 20; x <= 23; x++) pen.O(x, y0 + 16);           // bocca piatta, di lato
}

export default function calamaro() {
  return make({
    id: 'calamaro', name: 'Calamaro', author: 'Calamaio OS',
    description: 'Un calamaro che vive nel calamaio: pinne a freccia, tentacoli che ondeggiano e un pennino per scrivere.'
  }, (pen, pose, f, dy) => {
    if (pose === 'run') return runFrame(pen, f, dy);
    const y0 = TOP + dy;
    const sq = pose === 'idle' && f > 1 ? 1 : 0;   // respiro: la punta del mantello si abbassa
    pen.stamp(MANTLE.slice(sq), 0, y0 + sq);
    const still = pose === 'error' || pose === 'tired' || pose === 'stuffy';
    const legs = LEGS[still ? 0 : f % 2];
    legs.forEach((row, r) => { const y = y0 + 17 + r; if (y < 25) for (let x = 0; x < row.length; x++) if (row[x] === '#') pen.I(x, y); });
    // faccia
    const face = FACE[pose](f);
    pen.eye(7, y0 + 11, face.eyes); pen.eye(15, y0 + 11, face.eyes);
    const my = y0 + 16;
    if (face.mouth === 'grin') { for (let x = 11; x <= 14; x++) pen.O(x, my); pen.O(12, my + 1); pen.O(13, my + 1); }
    else if (face.mouth === 'o') { pen.O(12, my); pen.O(13, my); }
    else if (face.mouth === 'frown') { pen.O(11, my + 1); pen.O(12, my); pen.O(13, my); pen.O(14, my + 1); }
    else for (let x = 11; x <= 14; x++) pen.O(x, my);
    // tentacoli lunghi
    const L = (k) => ARM[k].forEach(([x, y]) => { if (y + dy < 25) pen.I(25 - x, y + dy); });
    const R = (k) => ARM[k].forEach(([x, y]) => { if (y + dy < 25) pen.I(x, y + dy); });
    if (pose === 'working' || pose === 'compact') {
      R('type'); PEN(f % 2).forEach(([x, y]) => pen.I(x, y + dy));
      if (pose === 'working' && f % 2 === 0) pen.I(21, 22 + dy);           // goccia d'inchiostro dal pennino
      L(f % 2 ? 'type' : 'rest');
    } else {
      const arms = {
        input: [f % 2 ? 'up1' : 'up0', 'rest'], permission: [f % 2 ? 'up1' : 'up0', f % 2 ? 'up0' : 'up1'],
        done: dy < 0 ? ['cheer', 'cheer'] : ['rest', 'rest'], error: ['droop', 'droop'], tired: ['droop', 'droop'],
        stuffy: ['droop', 'droop'], idle: ['rest', 'rest']
      }[pose];
      R(arms[0]); L(arms[1]);
    }
    if (pose === 'error') { pen.drop(19, y0 + 2 + (f % 2)); pen.drop(5, y0 + 4 + ((f + 1) % 2)); }   // schizzi d'inchiostro
    else if (pose === 'tired') pen.dropL(7, y0 + 2 + (f % 2));
    else stateMark(pen, pose, f, { sparkA: [[3, 4], [24, 8]], sparkB: [[5, 1], [24, 4]], zPts: [[18, 8], [20, 5], [22, 2], null] });
  });
}
