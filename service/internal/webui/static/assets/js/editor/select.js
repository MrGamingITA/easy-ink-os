// Riquadri, colpo del puntatore, selezione a riquadro e maniglie (editor). Moduli puri: nessun accesso a window/document.
// Le coordinate sono della scheda (250 × 122); `handles` restituisce px CSS della tela (F12), indipendenti dal dpr.
import { findEl, LOCKED_SCREENS } from './ops.js';

const isObj = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
const arr = (v) => (Array.isArray(v) ? v : []);
const int = (v, d) => (Number.isFinite(v) ? Math.round(v) : d);
const scaleOf = (el) => Math.max(1, int(el.scale, 1));
const MAX_SIDE = 512;

// misure per tipo (stesse di lower.js): [larghezza, altezza]
function size(el) {
  const s = scaleOf(el);
  switch (el.type) {
    case 'mascot': return [28 * s, 25 * s];
    case 'battery': return [16 * s, 8 * s];
    case 'wifi': return [11 * s, 9 * s];
    case 'stateIcon': case 'sym': return [7 * s, 7 * s];
    case 'text': {
      const fs = int(el.font?.size, 10), lh = int(el.lineHeight, fs + 2);
      return [Math.max(0, int(el.w, 0)), Math.max(1, int(el.lines, 1)) * lh];
    }
    case 'clock': return [Math.max(0, int(el.w, 0)), int(el.font?.size, 10) + 2];
    default: return [Math.max(0, int(el.w, 0)), Math.max(0, int(el.h, 0))];
  }
}

// tutti i riquadri della variante, in ordine di disegno (il genitore prima dei figli), in coordinate assolute
export function layout(theme, loc) {
  const scr = loc && !LOCKED_SCREENS.includes(loc.screen) ? theme?.screens?.[loc.screen] : null;
  const v = isObj(scr) ? arr(scr.variants)[loc.variant] : null;
  if (!isObj(v)) return [];
  const out = [];
  const walk = (els, ox, oy, parent, depth, prefix, lock, hid) => {
    arr(els).forEach((el, i) => {
      if (!isObj(el)) return;
      const x = ox + int(el.x, 0), y = oy + int(el.y, 0);
      const locked = lock || el.locked === true, hidden = hid || el.hidden === true;
      const key = el.type === 'group' ? 'children' : el.type === 'list' ? 'row' : null;
      const box = { id: el.id, type: el.type, x, y, w: 0, h: 0, parentId: parent, depth, locked, hidden, path: `${prefix}[${i}]` };
      out.push(box);
      if (el.type !== 'group') [box.w, box.h] = size(el);
      if (key) {
        const first = out.length;
        walk(el[key], x, y, el.id, depth + 1, `${box.path}.${key}`, locked, hidden);
        if (el.type === 'group') {
          // unione dei figli diretti non nascosti (un figlio gruppo porta già la sua unione)
          const kids = out.slice(first).filter((b) => b.parentId === el.id && !b.hiddenOwn);
          const useful = kids.filter((b) => !(b.type === 'group' && b.w === 0 && b.h === 0));
          if (useful.length) {
            const x0 = Math.min(...useful.map((b) => b.x)), y0 = Math.min(...useful.map((b) => b.y));
            box.x = x0; box.y = y0;
            box.w = Math.max(...useful.map((b) => b.x + b.w)) - x0;
            box.h = Math.max(...useful.map((b) => b.y + b.h)) - y0;
          }
        }
      }
      if (el.hidden === true) box.hiddenOwn = true;
    });
  };
  // `hiddenOwn` serve solo qui sopra: si toglie prima di restituire
  walk(v.elements, 0, 0, null, 0, `screens.${loc.screen}.variants[${loc.variant}].elements`, false, false);
  for (const b of out) delete b.hiddenOwn;
  return out;
}

// il riquadro al livello della selezione: senza scope l'antenato di primo livello, con scope il figlio diretto
function resolve(byId, box, scope) {
  for (let b = box, n = 0; b && n < 16; b = byId.get(b.parentId), n++) {
    if (b.parentId === (scope ?? null)) return b;
  }
  return null;
}
const index = (boxes) => new Map(boxes.map((b) => [b.id, b]));
const usable = (b, locked) => !b.hidden && (locked || !b.locked);

const dist = (b, px, py) => Math.hypot(Math.max(b.x - px, px - (b.x + b.w), 0), Math.max(b.y - py, py - (b.y + b.h), 0));

// il più in alto sotto il punto; `locked: true` include anche i bloccati (per mostrarne le maniglie)
export function hitTest(boxes, px, py, { scope = null, slop = 0, locked = false } = {}) {
  const byId = index(boxes);
  const cand = boxes.filter((b) => b.type !== 'group' && usable(b, locked) && resolve(byId, b, scope));
  for (let i = cand.length - 1; i >= 0; i--) {
    const b = cand[i];
    if (px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h) return resolve(byId, b, scope).id;
  }
  if (!(slop > 0)) return null;
  let best = null, bd = Infinity;
  for (let i = cand.length - 1; i >= 0; i--) {
    const d = dist(cand[i], px, py);
    if (d <= slop && d < bd) { bd = d; best = cand[i]; }
  }
  return best ? resolve(byId, best, scope).id : null;
}

// quelli interamente dentro il rettangolo, allo stesso livello di hitTest
export function marquee(boxes, rect, scope = null, { locked = false } = {}) {
  if (!isObj(rect)) return [];
  const out = [];
  for (const b of boxes) {
    if (b.parentId !== (scope ?? null) || !usable(b, locked)) continue;
    if (b.type === 'group' && b.w === 0 && b.h === 0) continue;
    if (b.x >= rect.x && b.y >= rect.y && b.x + b.w <= rect.x + rect.w && b.y + b.h <= rect.y + rect.h) out.push(b.id);
  }
  return out;
}

export function selectionBox(boxes, ids) {
  const set = new Set(arr(ids));
  const sel = boxes.filter((b) => set.has(b.id));
  if (!sel.length) return null;
  const x = Math.min(...sel.map((b) => b.x)), y = Math.min(...sel.map((b) => b.y));
  return { x, y, w: Math.max(...sel.map((b) => b.x + b.w)) - x, h: Math.max(...sel.map((b) => b.y + b.h)) - y };
}

// toglie dalla selezione gli id che non esistono più (gli id si riusano dopo una cancellazione)
export function pruneIds(theme, loc, ids) {
  return arr(ids).filter((id) => findEl(theme, loc, id));
}

const EIGHT = ['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w'];
export function handleNames(type, el) {
  switch (type) {
    case 'text': return ['w', 'e', 's'];
    case 'clock': return ['w', 'e'];
    case 'mascot': case 'battery': case 'wifi': case 'stateIcon': case 'sym': return ['se'];
    case 'line': return el?.dir === 'v' ? ['n', 's'] : ['w', 'e'];
    case 'group': return [];
    default: return EIGHT.slice();
  }
}

const AT = { nw: [0, 0], n: [0.5, 0], ne: [1, 0], e: [1, 0.5], se: [1, 1], s: [0.5, 1], sw: [0, 1], w: [0, 0.5] };

// centri e raggi in px CSS della tela (zoom = px per punto della scheda); r = 5 col mouse, 22 col tocco
export function handles(box, names, zoom, touch) {
  const r = touch ? 22 : 5;
  return arr(names).filter((n) => AT[n]).map((name) => ({
    name, r, cx: (box.x + box.w * AT[name][0]) * zoom, cy: (box.y + box.h * AT[name][1]) * zoom,
  }));
}

export function handleAt(hs, sx, sy) {
  let best = null, bd = Infinity;
  for (const h of arr(hs)) {
    const d = Math.hypot(h.cx - sx, h.cy - sy);
    if (d <= h.r && d < bd) { bd = d; best = h.name; }
  }
  return best;
}

// il lato opposto resta fermo; larghezza e altezza fra 1 e 512
export function resizeBox(box, name, dx, dy) {
  let { x, y, w, h } = box;
  const right = x + w, bottom = y + h;
  if (name.includes('e')) w = w + dx;
  if (name.includes('w')) w = w - dx;
  if (name.includes('s')) h = h + dy;
  if (name.includes('n')) h = h - dy;
  w = Math.max(1, Math.min(MAX_SIDE, Math.round(w)));
  h = Math.max(1, Math.min(MAX_SIDE, Math.round(h)));
  if (name.includes('w')) x = right - w;
  if (name.includes('n')) y = bottom - h;
  return { x, y, w, h };
}
