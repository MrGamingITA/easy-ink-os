// Cronologia pura per annulla/ripeti (editor dei temi). I valori sono immutabili: mai modificati sul posto.
// Un trascinamento = un passo: i commit con lo stesso `merge` sostituiscono il presente finché il gesto non si chiude.

export function createHistory(value) {
  return { present: value, past: [], future: [], open: null };
}

const same = (a, b) => a === b || (a !== undefined && b !== undefined && JSON.stringify(a) === JSON.stringify(b));

export function commit(h, next, merge = null) {
  if (same(next, h.present)) return h;
  if (merge !== null && merge === h.open) {
    // il trascinamento è tornato al punto di partenza: nessun passo
    if (h.past.length && same(next, h.past[h.past.length - 1])) return { present: next, past: h.past.slice(0, -1), future: h.future, open: null };
    return { ...h, present: next };
  }
  return { present: next, past: [...h.past, h.present], future: [], open: merge };
}

export function endGesture(h) {
  return h.open === null ? h : { ...h, open: null };
}

export function undo(h) {
  if (!h.past.length) return endGesture(h);
  return { present: h.past[h.past.length - 1], past: h.past.slice(0, -1), future: [h.present, ...h.future], open: null };
}

export function redo(h) {
  if (!h.future.length) return endGesture(h);
  return { present: h.future[0], past: [...h.past, h.present], future: h.future.slice(1), open: null };
}

export const canUndo = (h) => h.past.length > 0;
export const canRedo = (h) => h.future.length > 0;

// Ricarica (dopo un conflitto): nuovo presente, niente passi.
export function replace(h, value) {
  return { present: value, past: [], future: [], open: null };
}
