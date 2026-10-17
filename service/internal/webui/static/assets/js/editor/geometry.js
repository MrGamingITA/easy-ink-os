// Aggancio a griglia ed elementi, allinea e distribuisci (editor). Puro: lavora sui riquadri di select.js.
// Gli spostamenti restituiti possono avere mezzi punti (centri dispari): chi applica arrotonda (F13).
import { selectionBox } from './select.js';

const SCREEN_W = 250, SCREEN_H = 122;
const EPS = 1e-9;
const arr = (v) => (Array.isArray(v) ? v : []);

// bersagli degli altri riquadri visibili non selezionati (e non figli di un selezionato) + bordi e centro dello schermo
function targets(boxes, skip) {
  const byId = new Map(boxes.map((b) => [b.id, b]));
  const under = (b) => { for (let p = byId.get(b.parentId), n = 0; p && n < 16; p = byId.get(p.parentId), n++) if (skip.has(p.id)) return true; return false; };
  const tx = [0, SCREEN_W / 2, SCREEN_W], ty = [0, SCREEN_H / 2, SCREEN_H];
  for (const b of boxes) {
    if (skip.has(b.id) || b.hidden || under(b) || (b.w === 0 && b.h === 0)) continue;
    tx.push(b.x, b.x + b.w / 2, b.x + b.w);
    ty.push(b.y, b.y + b.h / 2, b.y + b.h);
  }
  return { x: tx, y: ty };
}

// il migliore aggancio di una serie di bordi contro i bersagli, entro la soglia: { d, at } | null
function best(edges, ts, threshold) {
  let r = null;
  for (const e of edges) for (const t of ts) {
    const d = t - e;
    if (Math.abs(d) <= threshold && (!r || Math.abs(d) < Math.abs(r.d))) r = { d, at: t };
  }
  return r;
}

// guide: i bersagli su cui un bordo cade esattamente dopo l'aggancio
function guidesFor(axis, edges, ts) {
  const out = [];
  for (const t of ts) if (edges.some((e) => Math.abs(e - t) < EPS) && !out.some((g) => g.at === t)) out.push({ axis, at: t });
  return out;
}

const toGrid = (v, grid) => Math.round(v / grid) * grid;

// un asse: bordi proposti → nuovo scarto + guide. Aggancio agli elementi, altrimenti griglia
function snapAxis(axis, edges, ts, d, grid, threshold, useOthers, gridEdge) {
  if (useOthers) {
    const s = best(edges, ts, threshold);
    if (s) return { d: d + s.d, guides: guidesFor(axis, edges.map((e) => e + s.d), ts) };
  }
  if (grid > 1) return { d: d + (toGrid(edges[gridEdge], grid) - edges[gridEdge]), guides: [] };
  return { d, guides: [] };
}

export function snapMove(boxes, ids, dx, dy, { grid = 1, others = true, threshold = 3 } = {}) {
  const sel = selectionBox(boxes, ids);
  const out = { dx, dy, guides: [] };
  if (!sel) return out;
  const ts = others ? targets(boxes, new Set(arr(ids))) : { x: [], y: [] };
  // si agganciano solo gli assi che si sono mossi
  if (dx !== 0) {
    const e = [sel.x + dx, sel.x + dx + sel.w / 2, sel.x + dx + sel.w];
    const r = snapAxis('x', e, ts.x, dx, grid, threshold, others, 0);
    out.dx = r.d; out.guides.push(...r.guides);
  }
  if (dy !== 0) {
    const e = [sel.y + dy, sel.y + dy + sel.h / 2, sel.y + dy + sel.h];
    const r = snapAxis('y', e, ts.y, dy, grid, threshold, others, 0);
    out.dy = r.d; out.guides.push(...r.guides);
  }
  return out;
}

// solo i lati che la maniglia muove; `rect` è il riquadro proposto
export function snapResize(boxes, id, rect, handle, { grid = 1, others = true, threshold = 3 } = {}) {
  const orig = boxes.find((b) => b.id === id);
  const guides = [];
  let { x, y, w, h } = rect;
  if (!orig) return { rect: { x, y, w, h }, guides };
  const ts = others ? targets(boxes, new Set([id])) : { x: [], y: [] };
  const side = (axis, value, moved, list) => {
    if (!moved) return value;
    const r = snapAxis(axis, [value], list, 0, grid, threshold, others, 0);
    guides.push(...r.guides);
    return value + r.d;
  };
  let l = x, t = y, rr = x + w, b = y + h;
  if (handle.includes('w')) l = side('x', l, l !== orig.x, ts.x);
  if (handle.includes('e')) rr = side('x', rr, rr !== orig.x + orig.w, ts.x);
  if (handle.includes('n')) t = side('y', t, t !== orig.y, ts.y);
  if (handle.includes('s')) b = side('y', b, b !== orig.y + orig.h, ts.y);
  // larghezza e altezza minime 1: il lato mosso non supera quello fermo
  if (handle.includes('w')) l = Math.min(l, rr - 1); else rr = Math.max(rr, l + 1);
  if (handle.includes('n')) t = Math.min(t, b - 1); else b = Math.max(b, t + 1);
  return { rect: { x: l, y: t, w: rr - l, h: b - t }, guides };
}

// elementi su cui si può agire: esistenti e non bloccati
function movable(boxes, ids) {
  const want = new Set(arr(ids));
  return boxes.filter((b) => want.has(b.id) && !b.locked);
}

export function align(boxes, ids, mode) {
  const out = new Map(), sel = movable(boxes, ids);
  if (!sel.length) return out;
  const ref = sel.length === 1 ? { x: 0, y: 0, w: SCREEN_W, h: SCREEN_H } : selectionBox(sel, sel.map((b) => b.id));
  for (const b of sel) {
    let dx = 0, dy = 0;
    if (mode === 'left') dx = ref.x - b.x;
    else if (mode === 'hcenter') dx = ref.x + ref.w / 2 - (b.x + b.w / 2);
    else if (mode === 'right') dx = ref.x + ref.w - (b.x + b.w);
    else if (mode === 'top') dy = ref.y - b.y;
    else if (mode === 'vcenter') dy = ref.y + ref.h / 2 - (b.y + b.h / 2);
    else if (mode === 'bottom') dy = ref.y + ref.h - (b.y + b.h);
    else return new Map();
    out.set(b.id, { dx: Math.round(dx), dy: Math.round(dy) });
  }
  return out;
}

// spazi uguali fra il primo e l'ultimo (che restano fermi); servono almeno 3 elementi
export function distribute(boxes, ids, axis) {
  const out = new Map(), sel = movable(boxes, ids);
  if (sel.length < 3 || (axis !== 'h' && axis !== 'v')) return out;
  const h = axis === 'h', pos = (b) => (h ? b.x : b.y), len = (b) => (h ? b.w : b.h);
  const s = sel.slice().sort((a, b) => pos(a) - pos(b));
  const first = s[0], last = s[s.length - 1];
  const gap = (pos(last) + len(last) - pos(first) - s.reduce((n, b) => n + len(b), 0)) / (s.length - 1);
  let at = pos(first);
  for (const b of s) {
    const d = Math.round(at) - pos(b);
    out.set(b.id, h ? { dx: d, dy: 0 } : { dx: 0, dy: d });
    at += len(b) + gap;
  }
  return out;
}
