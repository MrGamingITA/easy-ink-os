// Stato dell'editor: un oggetto immutabile, set() con una patch o una funzione, notifica in microtask.
// Più set nello stesso giro danno una sola notifica (un solo disegno). Puro: niente window/document.

export function createStore(state) {
  let cur = state, queued = false;
  const subs = new Set();
  const notify = () => { queued = false; for (const fn of [...subs]) fn(cur); };
  return {
    get: () => cur,
    set(patch) {
      const p = typeof patch === 'function' ? patch(cur) : patch;
      if (!p) return;
      cur = { ...cur, ...p };
      if (!queued) { queued = true; queueMicrotask(notify); }
    },
    subscribe(fn) { subs.add(fn); return () => subs.delete(fn); },
  };
}
