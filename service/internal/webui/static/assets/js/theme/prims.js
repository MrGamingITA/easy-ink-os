// Primitive come firmware/src/gfx/prims.cpp (porting delle funzioni del simulatore). c.fillRect(x, y, w, h, ink).
// Gli argomenti int del C++ sono troncati con | 0; pct resta double come là.
import { jsRound } from './text.js';

const clampd = (v, a, b) => (v < a ? a : v > b ? b : v);
const ICONS = {
  done: ['.......', '......#', '.....##', '#...##.', '##.##..', '.###...', '..#....'],
  error: ['#.....#', '##...##', '.##.##.', '..###..', '.##.##.', '##...##', '#.....#'],
  input: ['.#####.', '##...##', '.....##', '...###.', '..##...', '.......', '..##...'],
  permission: ['..##...', '..##...', '..##...', '..##...', '..##...', '.......', '..##...'],
  idle: ['#######', '....##.', '...##..', '..##...', '.##....', '#######', '.......'],
  compact: ['#######', '.#####.', '.......', '..###..', '.......', '...#...', '...#...'],
  working0: ['#######', '.#####.', '..###..', '...#...', '..#.#..', '.#...#.', '#######'],
  working1: ['#######', '.#...#.', '..#.#..', '...#...', '..###..', '.#####.', '#######'],
};
// ordine di enum class Sym: Plug, Bell, Moon, Lock, Bolt, Dot, Glass, Gauge
export const SYMS = [
  ['.#...#.', '.#...#.', '#######', '#######', '.#####.', '..###..', '...#...'],
  ['...#...', '..###..', '.#####.', '.#####.', '.#####.', '#######', '...#...'],
  ['..###..', '.##....', '##.....', '##.....', '##.....', '.##....', '..###..'],
  ['..###..', '.#...#.', '.#...#.', '#######', '###.###', '###.###', '#######'],
  ['...##', '..##.', '.##..', '#####', '..##.', '.##..', '##...'],
  ['..###..', '.#####.', '#######', '#######', '#######', '.#####.', '..###..'],
  ['#######', '.#...#.', '..#.#..', '...#...', '..###..', '.#####.', '#######'],
  ['.#####.', '#.....#', '#.#...#', '#..#..#', '#...#.#', '#.....#', '.#####.'],
];

export function box(c, x, y, w, h, col = true, t = 1) {
  x |= 0; y |= 0; w |= 0; h |= 0; t |= 0;
  if (t < 1) t = 1;
  c.fillRect(x, y, w, t, col); c.fillRect(x, y + h - t, w, t, col); c.fillRect(x, y, t, h, col); c.fillRect(x + w - t, y, t, h, col);
}
export function dotsH(c, x0, x1, y) { x0 |= 0; x1 |= 0; y |= 0; for (let x = x0; x < x1; x += 3) c.fillRect(x, y, 1, 1, true); }
export function dotsV(c, x, y0, y1) { x |= 0; y0 |= 0; y1 |= 0; for (let y = y0; y < y1; y += 2) c.fillRect(x, y, 1, 1, true); }
export function glyph(c, rows, x, y, s, col) {
  x |= 0; y |= 0; s |= 0;
  rows.forEach((row, r) => { for (let q = 0; q < row.length; q++) if (row[q] === '#') c.fillRect(x + q * s, y + r * s, s, s, col); });
}
export function icon(c, name, x, y, s, col = true) { glyph(c, ICONS[name], x, y, s, col); }
export function iconForState(c, state, x, y, s, f, col = true) {
  f |= 0;
  let n = 'idle';
  if (state === 'working') n = ((f % 2) + 2) % 2 ? 'working1' : 'working0';
  else if (['done', 'error', 'input', 'permission', 'compact'].includes(state)) n = state;
  icon(c, n, x, y, s, col);
}
export function sym(c, k, x, y, s, col) { glyph(c, SYMS[k], x, y, s, col); }
// st: { done, total, working } come BarState
export function bar(c, x, y, w, h, st, f) {
  x |= 0; y |= 0; w |= 0; h |= 0; f |= 0;
  const done = st.done | 0, total = st.total | 0;
  box(c, x, y, w, h, true, 1);
  const ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 4;
  if (ih <= 0 || iw <= 0) return;
  if (total > 0) {
    const per = iw / total, fw = jsRound(per * done);
    c.fillRect(ix, iy, fw, ih, true);
    if (st.working && done < total) {
      const sx = ix + fw, sw = jsRound(per) > 2 ? jsRound(per) : 2;
      // % di JS tronca verso lo zero come quello di C++
      for (let py = 0; py < ih; py++) for (let px = 0; px < sw; px++) if ((px + py + f) % 4 === 0) c.fillRect(sx + px, iy + py, 1, 1, true);
    }
    if (total <= 16) for (let k = 1; k < total; k++) {
      const tx = ix + jsRound(per * k);
      if (tx < ix + fw) c.fillRect(tx, iy, 1, ih, false); else c.fillRect(tx, iy + ih - 2, 1, 2, true);
    }
  } else {
    const mv = st.working ? f * 2 : 0;
    for (let px = 0; px < iw; px++) if ((px + mv) % 8 < 3) c.fillRect(ix + px, iy, 1, ih, true);
  }
}
export function wifiIcon(c, x, y, s, level, col) {
  x |= 0; y |= 0; s |= 0; level |= 0;
  for (let i = 0; i < 4; i++) {
    const h = (3 + 2 * i) * s, bx = x + i * 3 * s;
    if (level > i) c.fillRect(bx, y + 9 * s - h, 2 * s, h, col); else c.fillRect(bx, y + 8 * s, 2 * s, s, col);
  }
  if (level < 0) for (let k = 0; k < 5; k++) { c.fillRect(x + k * s, y + k * s, s, s, col); c.fillRect(x + (4 - k) * s, y + k * s, s, s, col); }
}
export function battIcon(c, x, y, s, pct, col) {
  x |= 0; y |= 0; s |= 0;
  box(c, x, y, 14 * s, 8 * s, col, s);
  c.fillRect(x + 14 * s, y + 2 * s, 2 * s, 4 * s, col);
  c.fillRect(x + 2 * s, y + 2 * s, jsRound((10 * s * clampd(pct, 0, 100)) / 100), 4 * s, col);
}
export function bigBar(c, x, y, w, h, pct) {
  x |= 0; y |= 0; w |= 0; h |= 0;
  box(c, x, y, w, h, true, 1);
  c.fillRect(x + 2, y + 2, jsRound(((w - 4) * clampd(pct, 0, 100)) / 100), h - 4 > 1 ? h - 4 : 1, true);
  for (let k = 1; k < 4; k++) c.fillRect(x + jsRound((w * k) / 4), y + h, 1, 3, true);
}
