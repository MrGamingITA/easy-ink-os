// Kit di disegno con la «grammatica» della Goccia (mascot() del simulatore): stessa nuvoletta,
// stessi simboli di stato, stesse scintille, stesse Z, stessa goccia di sudore, stessi ritmi.
// Un disegno procedurale (tools/avatar/designs/*.mjs) usa make() e definisce solo il corpo e la faccia.
import { W, H, POSES } from './format.mjs';

// Spostamenti verticali per posa e fotogramma, identici a mascot().
export const DY = {
  done: [0, -2, -3, -1], working: [0, 0, 1, 0], permission: [0, -1, 0, -1], error: [1, 1, 1, 1],
  run: [0, -1, 0, -1]   // corsa: falcata in basso (0, 2), gambe raccolte e sobbalzo di 1 punto (1, 3)
};
export const dyOf = (pose, f) => (DY[pose] ? DY[pose][f] : 0);

export class Pen {
  constructor() { this.g = Array.from({ length: H }, () => Array(W).fill('.')); }
  P(x, y, v) { if (x >= 0 && x < W && y >= 0 && y < H) this.g[y][x] = v; else throw new Error(`fuori griglia (${x},${y})`); }
  I(x, y) { this.P(x, y, '#'); }
  O(x, y) { this.P(x, y, '+'); }
  E(x, y) { this.P(x, y, '.'); }                       // cancella (torna trasparente)
  stamp(lines, x0, y0) {                               // '#' inchiostro, '+' carta, '-' cancella, altro = salta
    lines.forEach((row, y) => { for (let x = 0; x < row.length; x++) {
      const ch = row[x];
      if (ch === '#') this.I(x0 + x, y0 + y); else if (ch === '+') this.O(x0 + x, y0 + y); else if (ch === '-') this.E(x0 + x, y0 + y);
    } });
  }
  rows() { return this.g.map((r) => r.join('')); }

  // ---- grammatica condivisa (coordinate assolute, come la Goccia) ----
  bubble(inv) {
    for (let x = 19; x <= 25; x++) { this.I(x, 0); this.I(x, 6); }
    for (let y = 1; y <= 5; y++) { this.I(18, y); this.I(26, y); }
    for (let y = 1; y <= 5; y++) for (let x = 19; x <= 25; x++) this.P(x, y, inv ? '#' : '+');
    this.I(18, 7); this.I(17, 8);
  }
  bubbleDots(f) { this.bubble(false); [20, 22, 24].forEach((x, k) => this.I(x, k === f % 3 ? 2 : 3)); }      // working: …
  bubbleQuestion() { this.bubble(false); [[21, 1], [22, 1], [23, 1], [23, 2], [22, 3], [22, 5]].forEach(([x, y]) => this.I(x, y)); }  // input: ?
  bubbleBang(inv) { this.bubble(inv); [1, 2, 3, 5].forEach((y) => this.P(22, y, inv ? '+' : '#')); }  // permission: ! (lampeggia)
  bubbleLines(f) { this.bubble(false); [2, 4].forEach((yy, k) => { const len = (f + k) % 2 ? 3 : 5; for (let x = 0; x < len; x++) this.I(20 + x, yy); }); }  // compact
  plus(x, y) { this.I(x, y - 1); this.I(x - 1, y); this.I(x, y); this.I(x + 1, y); this.I(x, y + 1); }        // scintilla di done
  zz(x, y) { this.I(x, y); this.I(x + 1, y); this.I(x + 2, y); this.I(x + 1, y + 1); this.I(x, y + 2); this.I(x + 1, y + 2); this.I(x + 2, y + 2); }
  idleZ(f, pts = [[18, 8], [20, 5], [22, 2], null]) { [pts[f], pts[(f + 2) % 4]].forEach((p) => { if (p) this.zz(p[0], p[1]); }); }
  drop(x, y) { this.I(x, y); this.I(x, y + 1); this.I(x + 1, y + 1); }                                        // sudore (errore)
  dropL(x, y) { this.I(x, y); this.I(x, y + 1); this.I(x - 1, y + 1); }                                       // sudore (stanca)
  waves(f, pts = [[1, 1], [4, 0], [21, 1], [24, 0]]) { pts.forEach((p) => { this.I(p[0], p[1] + f % 2); this.I(p[0] + 1, p[1] + 1 - f % 2); this.I(p[0] + 2, p[1] + f % 2); }); }
  // Righe di velocità dietro la figura (a sinistra): [x, y, lunghezza]; nei fotogrammi dispari scorrono di 1 a sinistra.
  speed(f, lines) { lines.forEach(([x, y, len]) => { const s = f % 2; for (let k = 0; k < len; k++) this.I(x - s + k, y); }); }
  sparkles(f, a = [[3, 3], [23, 5]], b = [[2, 9], [24, 1]]) { (f % 2 === 0 ? a : b).forEach(([x, y]) => this.plus(x, y)); }

  // Occhi 4×4 come la Goccia: ex = colonna sinistra, ey = riga alta. Disegnati in carta su inchiostro.
  eye(ex, ey, k) {
    if (k === 'closed') { for (let x = 0; x < 4; x++) this.O(ex + x, ey + 2); return; }
    if (k === 'tired') { for (let x = 0; x < 4; x++) this.O(ex + x, ey + 2); this.O(ex, ey + 1); this.O(ex + 3, ey + 1); return; }
    if (k === 'happy') { this.O(ex, ey + 2); this.O(ex + 1, ey + 1); this.O(ex + 2, ey + 1); this.O(ex + 3, ey + 2); return; }
    if (k === 'x') { [[0, 0], [3, 0], [1, 1], [2, 1], [1, 2], [2, 2], [0, 3], [3, 3]].forEach(([x, y]) => this.O(ex + x, ey + y)); return; }
    for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) this.O(ex + x, ey + y);
    let px = ex + 1, py = ey + 1;
    if (k === 'down') py = ey + 2; else if (k === 'up') { py = ey; px = ex + 2; } else if (k === 'side') px = ex + 2;
    this.I(px, py); this.I(px + 1, py); this.I(px, py + 1); this.I(px + 1, py + 1);
  }
}

// Occhi e bocca per posa, come la Goccia (si possono sovrascrivere nel disegno).
export const FACE = {
  working: (f) => ({ eyes: f === 3 ? 'open' : 'down', mouth: 'flat' }),
  permission: () => ({ eyes: 'open', mouth: 'o' }),
  input: (f) => ({ eyes: f === 3 ? 'closed' : 'up', mouth: 'o' }),
  done: () => ({ eyes: 'happy', mouth: 'grin' }),
  error: () => ({ eyes: 'x', mouth: 'frown' }),
  idle: () => ({ eyes: 'closed', mouth: 'flat' }),
  compact: () => ({ eyes: 'up', mouth: 'flat' }),
  tired: () => ({ eyes: 'tired', mouth: 'flat' }),
  stuffy: () => ({ eyes: 'closed', mouth: 'o' }),
  run: () => ({ eyes: 'side', mouth: 'flat' })
};

// Simbolo di stato condiviso (nuvoletta e decorazioni) per posa.
export function stateMark(pen, pose, f, opt = {}) {
  if (pose === 'working') pen.bubbleDots(f);
  else if (pose === 'input') pen.bubbleQuestion();
  else if (pose === 'permission') pen.bubbleBang(f % 2 === 1);
  else if (pose === 'compact') pen.bubbleLines(f);
  else if (pose === 'done') pen.sparkles(f, opt.sparkA, opt.sparkB);
  else if (pose === 'idle') pen.idleZ(f, opt.zPts);
  else if (pose === 'stuffy') pen.waves(f, opt.wavePts);
}

// Costruisce l'avatar chiamando draw(pen, pose, f, dy) per ogni posa e fotogramma.
export function make(meta, draw, frames = 4) {
  const av = { format: 'calamaio-avatar', version: 1, ...meta, w: W, h: H, frames, poses: {} };
  for (const p of POSES) {
    av.poses[p] = [];
    for (let f = 0; f < frames; f++) { const pen = new Pen(); draw(pen, p, f, dyOf(p, f)); av.poses[p].push(pen.rows()); }
  }
  return av;
}
