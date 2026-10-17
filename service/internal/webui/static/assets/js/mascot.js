// La Goccia: funzione mascot() del simulatore (reference/calamaio-os-browser/assets/calamaio-test.js),
// copiata senza cambiare la logica. Disegna punto per punto con P/I/O e scala s.
// Pose: working, permission, input, done, error, idle, compact, tired, stuffy; 4 fotogrammi.

const INK = '#000', PAPER = '#fff';
const HW = [1, 1, 2, 2, 3, 4, 5, 6, 7, 7, 8, 8, 8, 8, 8, 7, 7, 6, 4];

export function mascot(c, ox, oy, s, st, f, ink, paper) {
  ink = ink || INK; paper = paper || PAPER;
  const fr = ((f % 4) + 4) % 4;
  const P = (x, y, col) => { c.fillStyle = col; c.fillRect(ox + x * s, oy + y * s, s, s); };
  const I = (x, y) => P(x, y, ink), O = (x, y) => P(x, y, paper);
  let dy = 0, sq = 0;
  if (st === 'done') dy = [0, -2, -3, -1][fr];
  else if (st === 'working') dy = [0, 0, 1, 0][fr];
  else if (st === 'permission') dy = [0, -1, 0, -1][fr];
  else if (st === 'error') dy = 1;
  else if (st === 'idle') sq = fr > 1 ? 1 : 0;
  const top = 4 + dy, cx = 13;
  for (let r = sq; r < HW.length; r++) { const h = HW[r]; for (let x = cx - h; x < cx + h; x++) I(x, top + r); }
  if (st === 'done' && dy <= -2) { I(10, top + 19); I(15, top + 19); } else { I(9, top + 19); I(10, top + 19); I(15, top + 19); I(16, top + 19); }
  const ey = top + 9;
  const eye = (ex, k) => {
    if (k === 'closed') { for (let x = 0; x < 4; x++) O(ex + x, ey + 2); return; }
    if (k === 'happy') { O(ex, ey + 2); O(ex + 1, ey + 1); O(ex + 2, ey + 1); O(ex + 3, ey + 2); return; }
    if (k === 'x') { O(ex, ey); O(ex + 3, ey); O(ex + 1, ey + 1); O(ex + 2, ey + 1); O(ex + 1, ey + 2); O(ex + 2, ey + 2); O(ex, ey + 3); O(ex + 3, ey + 3); return; }
    for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) O(ex + x, ey + y);
    let px = ex + 1, py = ey + 1;
    if (k === 'down') py = ey + 2; else if (k === 'up') { py = ey; px = ex + 2; }
    I(px, py); I(px + 1, py); I(px, py + 1); I(px + 1, py + 1);
  };
  const eyes = (k) => { eye(7, k); eye(15, k); };
  const mouth = (k) => {
    const y = top + 14;
    if (k === 'grin') { O(11, y); O(12, y); O(13, y); O(14, y); O(12, y + 1); O(13, y + 1); }
    else if (k === 'o') { O(12, y); O(13, y); O(12, y + 1); O(13, y + 1); }
    else if (k === 'frown') { O(11, y + 1); O(12, y); O(13, y); O(14, y + 1); }
    else { O(11, y + 1); O(12, y + 1); O(13, y + 1); O(14, y + 1); }
  };
  const arm = (k, side) => {
    const mm = side < 0 ? ((x) => 25 - x) : ((x) => x), A = (x, y) => I(mm(x), top + y);
    if (k === 'up0') { A(21, 12); A(22, 11); A(23, 10); A(23, 9); }
    else if (k === 'up1') { A(21, 12); A(22, 12); A(23, 11); A(24, 10); }
    else if (k === 'rest') { A(21, 13); A(22, 14); }
    else if (k === 'type') { A(21, 13); A(22, 13); }
    else if (k === 'droop') { A(21, 14); A(22, 15); }
    else if (k === 'cheer') { A(21, 11); A(22, 10); A(23, 9); }
  };
  const bubble = (inv) => {
    for (let x = 19; x <= 25; x++) { I(x, 0); I(x, 6); }
    for (let y = 1; y <= 5; y++) { I(18, y); I(26, y); }
    for (let y = 1; y <= 5; y++) for (let x = 19; x <= 25; x++) P(x, y, inv ? ink : paper);
    I(18, 7); I(17, 8);
  };
  const Gp = (inv) => (x, y) => P(x, y, inv ? paper : ink);
  const plus = (x, y) => { I(x, y - 1); I(x - 1, y); I(x, y); I(x + 1, y); I(x, y + 1); };
  const zz = (x, y) => { I(x, y); I(x + 1, y); I(x + 2, y); I(x + 1, y + 1); I(x, y + 2); I(x + 1, y + 2); I(x + 2, y + 2); };
  if (st === 'working') { eyes(fr === 3 ? 'open' : 'down'); mouth('flat'); arm(fr % 2 ? 'type' : 'rest', 1); arm(fr % 2 ? 'rest' : 'type', -1); bubble(false); const g = Gp(false); [20, 22, 24].forEach((x, k) => g(x, k === fr % 3 ? 2 : 3)); }
  else if (st === 'input') { eyes(fr === 3 ? 'closed' : 'up'); mouth('o'); arm(fr % 2 ? 'up1' : 'up0', 1); arm('rest', -1); bubble(false); const g = Gp(false); g(21, 1); g(22, 1); g(23, 1); g(23, 2); g(22, 3); g(22, 5); }
  else if (st === 'permission') { const inv = fr % 2 === 1; eyes('open'); mouth('o'); arm(fr % 2 ? 'up1' : 'up0', 1); arm(fr % 2 ? 'up0' : 'up1', -1); bubble(inv); const g = Gp(inv); g(22, 1); g(22, 2); g(22, 3); g(22, 5); }
  else if (st === 'done') { eyes('happy'); mouth('grin'); if (dy < 0) { arm('cheer', 1); arm('cheer', -1); } else { arm('rest', 1); arm('rest', -1); } if (fr % 2 === 0) { plus(3, 3); plus(23, 5); } else { plus(2, 9); plus(24, 1); } }
  else if (st === 'error') { eyes('x'); mouth('frown'); arm('droop', 1); arm('droop', -1); const d = fr % 2; I(19, top + 3 + d); I(19, top + 4 + d); I(20, top + 4 + d); }
  else if (st === 'compact') { eyes('up'); mouth('flat'); arm(fr % 2 ? 'type' : 'rest', 1); arm(fr % 2 ? 'rest' : 'type', -1); bubble(false); const g = Gp(false); [2, 4].forEach((yy, k) => { const len = (fr + k) % 2 ? 3 : 5; for (let x = 0; x < len; x++) g(20 + x, yy); }); }
  else if (st === 'tired') { eyes('closed'); O(7, ey + 1); O(10, ey + 1); O(15, ey + 1); O(18, ey + 1); mouth('flat'); arm('droop', 1); arm('droop', -1); const d = fr % 2; I(5, top + 4 + d); I(5, top + 5 + d); I(4, top + 5 + d); }
  else if (st === 'stuffy') { eyes('closed'); mouth('o'); arm('droop', 1); arm('droop', -1); const w = fr % 2; [[1, 1], [4, 0], [21, 1], [24, 0]].forEach((p) => { I(p[0], p[1] + w); I(p[0] + 1, p[1] + 1 - w); I(p[0] + 2, p[1] + w); }); }
  else { eyes('closed'); mouth('flat'); const Z = [[18, 8], [20, 5], [22, 2], null]; [Z[fr], Z[(fr + 2) % 4]].forEach((p) => { if (p) zz(p[0], p[1]); }); }
}

// Ingombro di un fotogramma in punti (x 0..26, y 0..24).
export const MASCOT_W = 27, MASCOT_H = 25;
