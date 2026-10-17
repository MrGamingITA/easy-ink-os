// Pannelli «Aggiungi» e «Livelli» (colonna sinistra / cassetto «Elementi»). Sottile: la logica sta in actions.js e layers.js.
// Testi sempre con textContent (h()): i nomi dei livelli vengono dal tema.
import { h } from '../ui.js';
import { TYPES } from './catalog.js';
import { layerRows, dropTarget, stepTarget } from './layers.js';

const SVG = 'http://www.w3.org/2000/svg';
// icone semplici disegnate con createElementNS (nessun innerHTML): path in una griglia 16×16
function icon(paths) {
  const s = document.createElementNS(SVG, 'svg');
  s.setAttribute('viewBox', '0 0 16 16'); s.setAttribute('width', '20'); s.setAttribute('height', '20'); s.setAttribute('aria-hidden', 'true');
  s.setAttribute('fill', 'none'); s.setAttribute('stroke', 'currentColor'); s.setAttribute('stroke-width', '1.5'); s.setAttribute('stroke-linecap', 'round'); s.setAttribute('stroke-linejoin', 'round');
  for (const d of paths) { const p = document.createElementNS(SVG, 'path'); p.setAttribute('d', d); s.append(p); }
  return s;
}
const ICONS = {
  grip: ['M5 4h.01M5 8h.01M5 12h.01M11 4h.01M11 8h.01M11 12h.01'],
  eye: ['M1 8s2.5-5 7-5 7 5 7 5-2.5 5-7 5-7-5-7-5z', 'M8 6.5a1.5 1.5 0 1 0 .01 0z'],
  eyeOff: ['M1 8s2.5-5 7-5 7 5 7 5-2.5 5-7 5-7-5-7-5z', 'M2 14L14 2'],
  lock: ['M4 7h8v6H4z', 'M5.5 7V5a2.5 2.5 0 0 1 5 0v2'],
  unlock: ['M4 7h8v6H4z', 'M5.5 7V5a2.5 2.5 0 0 1 4.6-1.4'],
  up: ['M8 13V3', 'M4 7l4-4 4 4'],
  down: ['M8 3v10', 'M4 9l4 4 4-4'],
};

// centro della parte visibile della tela, in punti della scheda (null se la scheda è fuori vista)
function visibleCenter() {
  const vp = document.getElementById('ed-viewport'), sc = document.getElementById('ed-screen');
  if (!vp || !sc) return null;
  const a = vp.getBoundingClientRect(), b = sc.getBoundingClientRect();
  if (!b.width) return null;
  const l = Math.max(a.left, b.left), r = Math.min(a.right, b.right), t = Math.max(a.top, b.top), btm = Math.min(a.bottom, b.bottom);
  if (r <= l || btm <= t) return null;
  const k = b.width / 250;
  return { x: ((l + r) / 2 - b.left) / k, y: ((t + btm) / 2 - b.top) / k };
}

export function mountAdd(store, A) {
  const host = document.getElementById('ed-add');
  const grid = h('div', { class: 'ed-add-grid', role: 'group', 'aria-label': 'Tipi di elemento' });
  host.append(grid);
  const btns = TYPES.map((t) => {
    const b = h('button', { type: 'button', class: 'btn secondary ed-add-btn', 'data-type': t.type, onclick: () => A.add(t.type, visibleCenter()) },
      h('span', { class: 'ed-ico', 'data-t': t.type, 'aria-hidden': 'true' }), h('span', { class: 'ed-add-label' }, t.label));
    grid.append(b);
    return b;
  });
  const render = (s) => { for (const b of btns) b.disabled = !s.hist; };
  store.subscribe(render);
  render(store.get());
}

export function mountLayers(store, A) {
  const host = document.getElementById('ed-layers');
  const list = h('ul', { class: 'ed-layer-list', id: 'ed-layer-list' });
  const empty = h('p', { class: 'hint' }, 'Nessun livello: aggiungi un elemento.');
  host.append(list, empty);
  let key = null, rows = [], editing = null, drag = null;
  const pane = document.getElementById('ed-left');

  const iconBtn = (cls, name, pressed, ic, onclick, extra = {}) => h('button', {
    type: 'button', class: 'ed-layer-btn ' + cls, 'aria-label': name, title: name, 'aria-pressed': pressed === null ? null : String(pressed), onclick, ...extra,
  }, icon(ICONS[ic]));

  function renameField(r) {
    let done = false;
    const end = (ok) => {
      if (done) return;
      done = true;
      const v = input.value;
      editing = null;
      if (ok && v !== r.label) A.renameEl(r.id, v);   // invariato = niente passo di cronologia
      draw(store.get(), true);
      list.querySelector(`[data-id="${CSS.escape(r.id)}"] .ed-layer-name`)?.focus();
    };
    const input = h('input', {
      type: 'text', class: 'ed-rename', value: editing.value, 'aria-label': 'Nome del livello', autocomplete: 'off', spellcheck: 'false',
      oninput: () => { editing.value = input.value; },
      onkeydown: (e) => { if (e.key === 'Enter' && e.isComposing) return; if (e.key === 'Enter') { e.preventDefault(); end(true); } else if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); end(false); } },
      onblur: () => end(true),
    });
    return input;
  }
  function startRename(r) {
    editing = { id: r.id, value: r.label, fresh: true };
    draw(store.get(), true);
  }

  function rowEl(r, s) {
    const state = [r.hidden ? 'nascosto' : '', r.locked ? 'bloccato' : ''].filter(Boolean).join(', ');
    const name = editing?.id === r.id ? renameField(r) : h('button', {
      type: 'button', class: 'ed-layer-name', 'aria-pressed': String(r.selected),
      onclick: (e) => A.pickLayer(r.id, e.shiftKey || store.get().touchMulti), ondblclick: () => startRename(r),
    }, h('span', { class: 'ed-layer-text' }, r.label), r.hiddenByData ? h('small', { class: 'ed-layer-note' }, 'nascosto con questi dati') : null);
    const li = h('li', { class: 'ed-layer', 'data-id': r.id, 'data-type': r.type, 'data-depth': String(r.depth),
      'data-selected': r.selected ? '' : null, 'data-hidden': r.hidden ? '' : null, 'data-locked': r.locked || r.lockedAbove ? '' : null },
      h('div', { class: 'ed-layer-main' },
        h('button', { type: 'button', class: 'ed-layer-btn ed-grip', 'aria-label': `Trascina ${r.label}`, title: 'Trascina per cambiare l\'ordine', onpointerdown: (e) => startDrag(e, r) }, icon(ICONS.grip)),
        name,
        iconBtn('ed-eye', r.hidden ? 'Mostra' : 'Nascondi', r.hidden, r.hidden ? 'eyeOff' : 'eye', () => A.toggleHidden(r.id)),
        iconBtn('ed-lock', r.locked ? 'Sblocca' : 'Blocca', r.locked, r.locked ? 'lock' : 'unlock', () => A.toggleLocked(r.id))),
      r.selected && editing?.id !== r.id ? h('div', { class: 'ed-layer-more' },
        h('button', { type: 'button', class: 'btn secondary ed-su', disabled: !stepTarget(rows, r.id, 'up'), onclick: () => step(r, 'up') }, 'Su'),
        h('button', { type: 'button', class: 'btn secondary ed-giu', disabled: !stepTarget(rows, r.id, 'down'), onclick: () => step(r, 'down') }, 'Giù'),
        h('button', { type: 'button', class: 'btn secondary ed-rinomina', onclick: () => startRename(r) }, 'Rinomina'),
        state ? h('span', { class: 'ed-layer-state' }, state) : null) : null);
    li.style.setProperty('--depth', String(r.depth));
    return li;
  }
  function step(r, dir) {
    const t = stepTarget(rows, r.id, dir);
    if (t) A.reorderEl(r.id, t.parentId, t.index);
  }

  // ---------- trascinamento (Pointer Events: mouse, penna, tocco) ----------
  function startDrag(e, r) {
    if (drag || (e.pointerType === 'mouse' && e.button !== 0)) return;
    e.preventDefault();
    const grip = e.currentTarget;
    drag = { id: r.id, pid: e.pointerId, target: null, el: grip.closest('li') };
    try { grip.setPointerCapture(e.pointerId); } catch { /* puntatore sintetico */ }
    drag.el.classList.add('ed-dragging');
    list.classList.add('ed-sorting');
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
    window.addEventListener('pointercancel', onCancel);
    window.addEventListener('blur', onCancel);
  }
  function mark(t) {
    for (const li of list.children) li.removeAttribute('data-drop');
    list.removeAttribute('data-drop');
    if (!t) return;
    if (t.mark === 'end') list.setAttribute('data-drop', 'end');
    else list.querySelector(`[data-id="${CSS.escape(t.overId)}"]`)?.setAttribute('data-drop', t.mark);
  }
  function onMove(e) {
    if (!drag || e.pointerId !== drag.pid) return;
    // vicino ai bordi del pannello: scorre
    if (pane) {
      const pr = pane.getBoundingClientRect();
      if (e.clientY > pr.bottom - 36) pane.scrollTop += 12; else if (e.clientY < pr.top + 36) pane.scrollTop -= 12;
    }
    const items = [...list.children];
    let over = null, frac = 0.5;
    for (const li of items) {
      const b = li.getBoundingClientRect();
      if (e.clientY >= b.top && e.clientY < b.bottom) { over = li.dataset.id; frac = (e.clientY - b.top) / b.height; break; }
    }
    if (!over && items.length) {
      // sopra la prima riga: la prima; negli spazi fra le righe: la più vicina; «fondo» solo sotto l'ultima
      if (e.clientY < items[0].getBoundingClientRect().top) { over = items[0].dataset.id; frac = 0; }
      else if (e.clientY < items[items.length - 1].getBoundingClientRect().bottom) {
        let best = Infinity;
        for (const li of items) {
          const b = li.getBoundingClientRect(), d = Math.min(Math.abs(e.clientY - b.top), Math.abs(e.clientY - b.bottom));
          if (d < best) { best = d; over = li.dataset.id; frac = e.clientY < (b.top + b.bottom) / 2 ? 0 : 1; }
        }
      }
    }
    drag.target = dropTarget(rows, drag.id, over, frac);
    mark(drag.target);
  }
  function endDrag() {
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', onUp);
    window.removeEventListener('pointercancel', onCancel);
    window.removeEventListener('blur', onCancel);
    drag?.el.classList.remove('ed-dragging');
    list.classList.remove('ed-sorting');
    mark(null);
    const d = drag;
    drag = null;
    return d;
  }
  function onUp(e) {
    if (!drag || e.pointerId !== drag.pid) return;
    onMove(e);
    const d = endDrag();
    if (d.target) A.reorderEl(d.id, d.target.parentId, d.target.index);
  }
  function onCancel(e) { if (drag && (!e.pointerId || e.pointerId === drag.pid)) endDrag(); }

  // ---------- disegno ----------
  let lastKey = null;
  function draw(s, force = false) {
    const k = [s.hist?.present, s.loc, s.sel, s.test];
    const same = lastKey && k.every((v, i) => v === lastKey[i]);
    if (same && !force) return;
    if (drag) return;   // la lista non cambia sotto il dito (si ridisegna dopo)
    lastKey = k;
    // il fuoco resta sullo stesso pulsante dello stesso livello
    const a = document.activeElement;
    const keep = a && list.contains(a) && a.closest('li') ? { id: a.closest('li').dataset.id, cls: [...a.classList].find((c) => /^ed-(su|giu|rinomina|eye|lock|layer-name|grip)$/.test(c)) } : null;
    rows = s.hist ? layerRows(s.hist.present, s.loc, s.sel, s.scope, s.test) : [];
    list.replaceChildren(...rows.map((r) => rowEl(r, s)));
    empty.hidden = rows.length > 0;
    const inp = list.querySelector('.ed-rename');
    if (inp) { inp.focus(); if (editing?.fresh) { inp.select(); editing.fresh = false; } }
    else if (keep) {
      const li = list.querySelector(`[data-id="${CSS.escape(keep.id)}"]`);
      const t = li && (li.querySelector('.' + keep.cls + ':not(:disabled)') || li.querySelector('.ed-layer-name'));
      if (t && a !== t) t.focus({ preventScroll: true });
    }
  }
  store.subscribe((s) => draw(s));
  draw(store.get(), true);
}
