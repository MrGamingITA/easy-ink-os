// Gesti della tela (mouse, penna, tocco) → intenti per le azioni. Puro: niente window/document.
// Punti in coordinate della scheda (frazioni ammesse); soglie in px dello schermo = distanza · zoom (F12, F13).
// move/resize/pan portano lo scarto CUMULATIVO dall'inizio del gesto (F13).
import { hitTest, handleAt } from './select.js';

const MOUSE_SLOP = 2, TOUCH_SLOP = 6, TOUCH_HIT = 22, DOUBLE_MS = 300;
// dentro l'elemento scelto una maniglia vince solo entro min(8, lato minore / 3) px CSS dal suo centro:
// il centro di un elemento sottile resta trascinabile; fuori dall'elemento vale tutto il raggio (22 col tocco)
const HANDLE_IN = 8;

const norm = (x0, y0, x1, y1) => ({ x: Math.min(x0, x1), y: Math.min(y0, y1), w: Math.abs(x1 - x0), h: Math.abs(y1 - y0) });

export function createGesture() {
  const pts = new Map();     // id → { x, y }
  let g = null;              // gesto in corso
  let lastTap = null;        // { id, time } dell'ultimo clic/tocco breve su un elemento

  const far = (p, zoom, slop) => Math.hypot(p.x - g.x0, p.y - g.y0) * zoom >= slop;
  const reset = () => { g = null; };

  // pizzico: distanza e centro dei primi due diti
  function pinchNow() {
    const [a, b] = [...pts.values()];
    return { d: Math.hypot(b.x - a.x, b.y - a.y), cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2 };
  }
  const zoomIntent = (done) => ({ kind: 'zoom', factor: g.factor, cx: g.cx, cy: g.cy, x0: g.cx0, y0: g.cy0, done });

  // annullamento di quello che si stava facendo (secondo dito, pointercancel)
  function abort() {
    if (!g) return null;
    if (g.mode === 'move') return { kind: 'move', dx: 0, dy: 0, done: true };
    if (g.mode === 'resize' && g.started) return { kind: 'resize', id: g.id, handle: g.handle, dx: 0, dy: 0, done: true };
    if (g.mode === 'marquee') return { kind: 'marquee', rect: null, add: g.add, done: true };
    if (g.mode === 'pinch') return { ...zoomIntent(true), factor: 1, cx: g.cx0, cy: g.cy0 };
    return null;
  }

  return {
    down(p, ctx) {
      const touch = p.type === 'touch';
      // un nuovo puntatore primario con un gesto ancora aperto (pointerup perso): si riparte da capo.
      // L'annullamento del gesto vecchio lo applica chi chiama (cancel() prima di down), qui solo si dimentica.
      if (g && pts.size >= 1 && !p.primary) {
        // un secondo dito: pizzico (annulla lo spostamento in corso); col mouse non c'è un secondo puntatore
        if (!touch || g.type !== 'touch' || pts.size >= 2 || g.mode === 'rest') return null;
        const undo = g.mode === 'pinch' ? null : abort();
        pts.set(p.id, { x: p.x, y: p.y });
        const n = pinchNow();
        g = { mode: 'pinch', type: 'touch', d0: n.d || 1, cx0: n.cx, cy0: n.cy, cx: n.cx, cy: n.cy, factor: 1 };
        return undo;
      }
      pts.clear();
      pts.set(p.id, { x: p.x, y: p.y });
      const zoom = ctx.zoom > 0 ? ctx.zoom : 1;
      g = { type: p.type, x0: p.x, y0: p.y, slop: touch ? TOUCH_SLOP : MOUSE_SLOP, mode: null, time: p.time };
      // maniglie prima di tutto (centri e raggi in px CSS)
      const sx = p.x * zoom, sy = p.y * zoom;
      let h = Array.isArray(ctx.sel) && ctx.sel.length === 1 ? handleAt(ctx.handles, sx, sy) : null;
      if (h) {
        const b = ctx.boxes.find((x) => x.id === ctx.sel[0]), hd = ctx.handles.find((x) => x.name === h);
        const inside = b && p.x >= b.x && p.x < b.x + b.w && p.y >= b.y && p.y < b.y + b.h;
        if (inside && Math.hypot(hd.cx - sx, hd.cy - sy) > Math.min(HANDLE_IN, Math.min(b.w, b.h) * zoom / 3)) h = null;
      }
      if (h) {
        Object.assign(g, { mode: 'resize', id: ctx.sel[0], handle: h, started: false });
        return null;
      }
      // i bloccati non si colpiscono mai sulla tela (si scelgono da Livelli)
      const hit = hitTest(ctx.boxes, p.x, p.y, { scope: ctx.scope ?? null, slop: touch ? TOUCH_HIT / zoom : 0 });
      if (!hit) {
        g.mode = touch ? 'empty' : 'press-empty';
        g.add = !touch && !!p.shift;
        return null;
      }
      const sel = Array.isArray(ctx.sel) ? ctx.sel : [];
      // doppio clic / doppio tocco su un gruppo o una lista: ci si entra
      const b = ctx.boxes.find((x) => x.id === hit);
      if (lastTap && lastTap.id === hit && p.time - lastTap.time <= DOUBLE_MS && b && (b.type === 'group' || b.type === 'list')) {
        lastTap = null;
        g.mode = 'rest';
        return { kind: 'enter', id: hit };
      }
      g.hit = hit;
      const add = touch ? !!ctx.multi : !!p.shift;
      if (add) {
        const ids = sel.includes(hit) ? sel.filter((i) => i !== hit) : [...sel, hit];
        g.mode = ids.includes(hit) ? 'press' : 'rest';
        g.alone = false;
        return { kind: 'select', ids, add: true };
      }
      const ids = sel.includes(hit) ? sel : [hit];
      g.mode = 'press';
      // dentro una selezione multipla: un clic senza trascinare lascia solo questo
      g.alone = ids.length > 1;
      return { kind: 'select', ids, add: false };
    },

    move(p, ctx) {
      if (!g || !pts.has(p.id)) return null;
      pts.set(p.id, { x: p.x, y: p.y });
      const zoom = ctx.zoom > 0 ? ctx.zoom : 1;
      const dx = p.x - g.x0, dy = p.y - g.y0;
      switch (g.mode) {
        case 'pinch': {
          if (pts.size < 2) return null;
          const n = pinchNow();
          Object.assign(g, { factor: n.d / g.d0, cx: n.cx, cy: n.cy });
          return zoomIntent(false);
        }
        case 'press':
          if (!far(p, zoom, g.slop)) return null;
          g.mode = 'move';
          return { kind: 'move', dx, dy, done: false };
        case 'move': return { kind: 'move', dx, dy, done: false };
        case 'resize':
          if (!g.started && !far(p, zoom, g.slop)) return null;
          g.started = true;
          return { kind: 'resize', id: g.id, handle: g.handle, dx, dy, done: false };
        case 'press-empty':
          if (!far(p, zoom, g.slop)) return null;
          g.mode = 'marquee';
          // fallthrough
        case 'marquee': return { kind: 'marquee', rect: norm(g.x0, g.y0, p.x, p.y), add: g.add, done: false };
        case 'empty':
          if (!far(p, zoom, g.slop)) return null;
          g.mode = 'pan';
          // fallthrough
        case 'pan': return { kind: 'pan', dx: dx * zoom, dy: dy * zoom };
        default: return null;
      }
    },

    up(p, ctx) {
      if (!g || !pts.has(p.id)) return null;
      const zoom = ctx.zoom > 0 ? ctx.zoom : 1;
      if (g.mode !== 'pinch') pts.set(p.id, { x: p.x, y: p.y });
      pts.delete(p.id);
      const dx = p.x - g.x0, dy = p.y - g.y0;
      const mode = g.mode;
      if (mode === 'pinch') {
        // il primo dito alzato chiude il pizzico; gli altri non fanno più niente
        const out = zoomIntent(true);
        g = pts.size ? { mode: 'rest', type: 'touch' } : null;
        return out;
      }
      if (pts.size) return null;
      let out = null;
      if (mode === 'move') out = { kind: 'move', dx, dy, done: true };
      else if (mode === 'resize' && g.started) out = { kind: 'resize', id: g.id, handle: g.handle, dx, dy, done: true };
      else if (mode === 'marquee') out = { kind: 'marquee', rect: norm(g.x0, g.y0, p.x, p.y), add: g.add, done: true };
      else if (mode === 'pan') out = { kind: 'pan', dx: dx * zoom, dy: dy * zoom, done: true };
      else if (mode === 'empty' || (mode === 'press-empty' && !g.add)) out = { kind: 'deselect' };
      else if (mode === 'press') {
        lastTap = { id: g.hit, time: g.time };
        if (g.alone) out = { kind: 'select', ids: [g.hit], add: false };
      }
      if (mode !== 'press') lastTap = null;
      reset();
      return out;
    },

    // c'è un gesto aperto (anche solo un dito rimasto dopo il pizzico)
    live() { return g !== null; },

    cancel() {
      const out = abort();
      pts.clear();
      reset();
      lastTap = null;
      return out;
    },
  };
}
