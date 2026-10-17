// Corsa della Goccia (posa run, docs/AVATAR.md §12). mascot() del simulatore non ha la corsa: è disegnata
// qui a mano con la grammatica della Goccia (occhi 4×4 con pupilla 2×2, bocca di carta, braccia sottili,
// piedi larghi 2) e goccia-export.mjs la aggiunge alle 9 pose generate da mascot().
// Di profilo verso destra: la punta resta indietro come una goccia che corre, il bordo destro è la colonna 27,
// i piedi appoggiano sulla riga 24. Fotogrammi: 0 e 2 falcata (gambe aperte), 1 e 3 gambe raccolte col
// sobbalzo di 1 punto; da soli 0-1 bastano a leggere la corsa.
import { Pen, dyOf } from './lib/kit.mjs';

// Corpo nella falcata (dy = 0), coordinate assolute; il sobbalzo lo alza di 1.
const BODY = [
  '.......##...................',  // 3  punta, trascinata indietro dalla corsa
  '.......###..................',  // 4
  '........####................',  // 5
  '........######..............',  // 6
  '.........########...........',  // 7
  '.........##########.........',  // 8
  '..........###########.......',  // 9
  '..........#############.....',  // 10
  '..........###############...',  // 11
  '...........###############..',  // 12
  '...........################.',  // 13
  '............################',  // 14 occhi 14..17
  '............################',  // 15
  '............################',  // 16
  '............################',  // 17
  '............################',  // 18
  '.............##############.',  // 19 bocca
  '..............############..',  // 20
  '...............##########...',  // 21
  '.................######.....'   // 22
];
const TOP = 3;
// Gambe larghe 2 (come i piedi della Goccia), dall'orlo del corpo alla riga 24.
const LEGS = {
  stride: [[16, 23], [17, 23], [14, 24], [15, 24], [16, 24], [21, 23], [22, 23], [22, 24], [23, 24], [24, 24]],
  // gamba d'appoggio sotto il corpo, l'altra col tallone calciato indietro
  gather: [[19, 22], [20, 22], [19, 23], [20, 23], [19, 24], [20, 24], [21, 24], [16, 22], [15, 22], [14, 21], [13, 21]]
};
// Braccio dietro la schiena, sottile: in su nella falcata 0, in giù nella 2, a metà nei raccolti.
const ARM = {
  up: [[11, 15], [10, 14], [9, 13]],
  down: [[11, 17], [10, 18], [9, 19]],
  mid: [[11, 16], [10, 16], [9, 17]]
};

export function gocciaRunFrame(f) {
  const pen = new Pen(), dy = dyOf('run', f), y0 = TOP + dy;
  pen.stamp(BODY, 0, y0);
  const stride = f % 2 === 0;
  (stride ? LEGS.stride : LEGS.gather).forEach(([x, y]) => pen.I(x, y));
  (f === 0 ? ARM.up : f === 2 ? ARM.down : ARM.mid).forEach(([x, y]) => pen.I(x, y + dy));
  // tre quarti: l'occhio vicino 4×4, quello lontano schiacciato a 3 colonne verso il bordo destro
  pen.eye(17, y0 + 11, 'side');
  for (let y = 0; y < 4; y++) for (let x = 0; x < 3; x++) pen.O(23 + x, y0 + 11 + y);
  [[24, 1], [25, 1], [24, 2], [25, 2]].forEach(([x, y]) => pen.I(x, y0 + 11 + y));
  for (let x = 21; x <= 24; x++) pen.O(x, y0 + 16);          // bocca piatta, di lato
  pen.speed(f, [[2, y0 + 12, 4], [1, y0 + 16, 5], [3, y0 + 20, 3]]);
  return pen.rows();
}

export const gocciaRun = () => [0, 1, 2, 3].map(gocciaRunFrame);
