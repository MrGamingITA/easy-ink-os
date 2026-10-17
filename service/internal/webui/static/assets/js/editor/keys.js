// Mappa delle scorciatoie: da un evento di tastiera a un'azione dell'editor (o null). Puro: niente window/document.
// `mac` sceglie ⌘ o Ctrl come tasto principale.
const ARROWS = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
const LETTERS = { c: 'copy', v: 'paste', d: 'duplicate', a: 'selectAll' };

export function keyAction(e, mac) {
  if (!e || typeof e.key !== 'string') return null;
  const mod = mac ? !!e.metaKey : !!e.ctrlKey;
  const other = mac ? !!e.ctrlKey : !!e.metaKey;  // il tasto principale dell'altro sistema non vale
  const shift = !!e.shiftKey;
  if (other) return null;
  if (!mod) {
    if (e.altKey) return null;
    const a = ARROWS[e.key];
    if (a) { const n = shift ? 8 : 1; return { type: 'nudge', dx: a[0] * n, dy: a[1] * n }; }
    if (e.key === 'Delete' || e.key === 'Backspace') return { type: 'delete' };
    if (e.key === 'Escape') return { type: 'escape' };
    if (e.key === 'Enter') return { type: 'enter' };
    return null;
  }
  if (e.altKey) return null;
  const k = e.key.toLowerCase();
  if (k === 'z') return { type: shift ? 'redo' : 'undo' };
  if (k === 'y' && !mac && !shift) return { type: 'redo' };
  if (k === 'g') return { type: shift ? 'ungroup' : 'group' };
  if (!shift && LETTERS[k]) return { type: LETTERS[k] };
  return null;
}

// azioni che si ripetono tenendo premuto il tasto; le altre (⌘D, Canc, ⌘G…) una volta sola
const REPEATABLE = new Set(['nudge', 'undo', 'redo']);

// il fuoco è in un campo che usa i tasti da sé
function typing(t) {
  if (!t) return false;
  if (t.isContentEditable) return true;
  if (/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName || '')) return true;
  return typeof t.closest === 'function' && !!t.closest('[contenteditable]:not([contenteditable="false"])');
}

// tasto per la tela: null = lascialo al browser o al campo; { repeat: true } = nostro ma da non ripetere
export function canvasKey(e, mac) {
  if (!e || typing(e.target)) return null;
  // un dialogo modale aperto tiene per sé i tasti (niente Canc o frecce sulla selezione dietro)
  if (e.target?.ownerDocument?.querySelector?.('dialog[open]')) return null;
  const a = keyAction(e, mac);
  if (!a) return null;
  // Invio su un pulsante o un link resta al pulsante
  if (a.type === 'enter' && /^(BUTTON|A|SUMMARY)$/.test(e.target?.tagName || '')) return null;
  if (e.repeat && !REPEATABLE.has(a.type)) return { ...a, repeat: true };
  return a;
}
