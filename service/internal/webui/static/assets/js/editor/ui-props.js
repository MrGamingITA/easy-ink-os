// Pannello «Proprietà» (colonna destra / cassetto «Proprietà»). Sottile: i limiti stanno in catalog.js, le modifiche
// in actions.js (setProp, alignSel, distributeSel, setSnap, setVariantProp). Testi sempre con textContent (h()).
// Il pannello si ricostruisce solo quando cambia la sua forma (selezione, tipo, blocco, condizioni); i valori si
// aggiornano sul posto, mai nel campo dove si sta scrivendo.
import { h } from '../ui.js';
import { fieldsFor, FONT_CHOICES, WHEN, REFS, CONDS, SCREENS, condKey, condDefault, valueChoices, templateProblem } from './catalog.js';
import { findEl, SCALED } from './ops.js';
import { layout, selectionBox } from './select.js';
import { elementLabel } from './warnings.js';
import { glyphWarnings } from '../theme/glyphs.js';

const ALIGNS = [['left', 'Sinistra', 'Allinea a sinistra'], ['hcenter', 'Centro ↔', 'Allinea al centro in orizzontale'], ['right', 'Destra', 'Allinea a destra'],
  ['top', 'Alto', 'Allinea in alto'], ['vcenter', 'Centro ↕', 'Allinea al centro in verticale'], ['bottom', 'Basso', 'Allinea in basso']];
const STEPS = [-8, -1, 1, 8];
const FILTERS = [['', 'nessuno'], ['maiuscolo', 'maiuscolo'], ['minuscolo', 'minuscolo']];
const sign = (n) => (n < 0 ? '−' + -n : '+' + n);
// solo interi decimali («−» tipografico ammesso); null altrimenti
const intOf = (v) => { const t = String(v).trim().replace('−', '-'); return /^-?\d+$/.test(t) ? Number(t) : null; };

export function mountProps(store, A) {
  const host = document.getElementById('ed-props');
  // il perché dell'ultima operazione rifiutata anche nel cassetto (sul telefono #ed-notice resta sotto il velo)
  const note = h('p', { class: 'ed-notice ed-props-notice', id: 'ed-props-notice', role: 'status', 'aria-live': 'polite' });
  host.before(note);
  let shape = null, syncs = [];
  const drafts = new Map();   // campo modello → bozza non applicata

  // ---------- mattoni ----------
  const section = (title, ...kids) => h('section', { class: 'ed-prop-sec' }, h('h3', null, title), ...kids);
  const field = (id, label, ...ctl) => h('div', { class: 'ed-field' }, h('label', { for: id }, label), ...ctl);
  const focused = (el) => document.activeElement === el;
  // campo numerico per il tocco: tastiera numerica, Invio = fatto
  const numInput = (k, label) => h('input', { type: 'text', inputmode: 'numeric', class: 'ed-num', id: 'ed-p-' + k, 'data-k': k, autocomplete: 'off',
    spellcheck: 'false', enterkeyhint: 'done', 'aria-label': label });

  // X o Y con −8 −1 [campo] +1 +8; get() → valore, set(v) → applica
  function stepRow(k, label, get, set, live) {
    const input = numInput(k, label);
    const commit = () => { const v = intOf(input.value); if (v !== null) set(v); };
    if (live) input.addEventListener('input', commit); else input.addEventListener('change', commit);
    input.addEventListener('blur', () => { A.endGesture(); input.value = String(get() ?? ''); });
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter' && !e.isComposing) { e.preventDefault(); commit(); A.endGesture(); } });
    const btn = (n) => h('button', { type: 'button', class: 'btn secondary ed-step', 'data-k': k + (n < 0 ? '-' : '+') + Math.abs(n), 'aria-label': `${label} ${sign(n)}`,
      onclick: () => { const v = get(); if (Number.isFinite(v)) { set(v + n); A.endGesture(); } } }, sign(n));
    syncs.push(() => { if (!focused(input)) input.value = String(get() ?? ''); });
    return h('div', { class: 'ed-field' }, h('label', { for: input.id }, label),
      h('div', { class: 'ed-steps' }, btn(STEPS[0]), btn(STEPS[1]), input, btn(STEPS[2]), btn(STEPS[3])));
  }

  function selectOf(k, options, label) {
    const s = h('select', { id: 'ed-p-' + k, 'data-k': k, 'aria-label': label });
    s.replaceChildren(...options.map((o) => h('option', { value: String(o.value), disabled: !!o.disabled }, o.label)));
    return s;
  }

  // ---------- un elemento ----------
  function intField(id, f, el) {
    const input = numInput(f.key, f.label);
    input.placeholder = f.optional ? 'auto' : '';
    input.addEventListener('input', () => {
      const v = input.value.trim().replace('−', '-');
      if ((v === '' && !f.optional) || v === '-') return;
      A.setProp(id, f.key, v);
    });
    // a conferma (uscita o Invio) il campo mostra il valore applicato (riportato nei limiti)
    const done = () => { A.endGesture(); sync(true); };
    input.addEventListener('blur', done);
    input.addEventListener('change', done);
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter' && !e.isComposing) { e.preventDefault(); done(); } });
    const cur = () => findEl(store.get().hist.present, store.get().loc, id)?.el?.[f.key];
    const sync = (force = false) => { const v = cur() ?? f.def; if (force || !focused(input)) input.value = v === undefined ? '' : String(v); };
    syncs.push(() => sync());
    const range = h('span', { class: 'ed-range' }, `${f.min}–${f.max}`);
    return field(input.id, f.label, h('div', { class: 'ed-inline' }, input, range));
  }

  function selectField(id, f, el, inRow) {
    const opts = f.options.map((o) => ({ ...o, disabled: f.key === 'ref' && o.value === 'row' && !inRow }));
    // senza predefinito (source, sym): un valore mancante si vede come tale, non come la prima voce
    if (f.def === undefined) opts.unshift({ value: '', label: '(manca)', disabled: true });
    const s = selectOf(f.key, opts, f.label);
    s.addEventListener('change', () => { A.setProp(id, f.key, f.options.find((o) => String(o.value) === s.value)?.value); A.endGesture(); });
    syncs.push(() => {
      const v = findEl(store.get().hist.present, store.get().loc, id)?.el?.[f.key];
      s.value = String(v ?? f.def ?? '');
      if (s.selectedIndex < 0) s.value = '';
    });
    return field(s.id, f.label, s);
  }

  function boolField(id, f) {
    const c = h('input', { type: 'checkbox', id: 'ed-p-' + f.key, 'data-k': f.key });
    c.addEventListener('change', () => { A.setProp(id, f.key, c.checked); A.endGesture(); });
    syncs.push(() => { const v = findEl(store.get().hist.present, store.get().loc, id)?.el?.[f.key]; c.checked = (v ?? f.def) === true; });
    return h('label', { class: 'ed-check', for: c.id }, c, h('span', null, f.label));
  }

  function fontField(id, f) {
    const s = h('select', { id: 'ed-p-font', 'data-k': 'font' });
    const bad = h('option', { value: '', disabled: true }, '(carattere che la scheda non ha)');
    s.replaceChildren(bad, ...FONT_CHOICES.map((c) => h('option', { value: c.key }, c.label)));
    s.addEventListener('change', () => { A.setProp(id, 'font', s.value); A.endGesture(); });
    syncs.push(() => {
      const ft = findEl(store.get().hist.present, store.get().loc, id)?.el?.font;
      const k = ft ? `${ft.family}:${ft.size}:${ft.weight}` : '';
      s.value = FONT_CHOICES.some((c) => c.key === k) ? k : '';
    });
    return field(s.id, f.label, s);
  }

  // modello con «Inserisci valore»; problemi e glifi mancanti accanto al campo, mentre si scrive
  function templateField(id, f, el, inRow) {
    const multi = el.type === 'text';
    const ta = h(multi ? 'textarea' : 'input', { id: 'ed-p-' + f.key, 'data-k': f.key, class: 'ed-tmpl', autocomplete: 'off', spellcheck: 'false',
      autocapitalize: 'off', ...(multi ? { rows: '2' } : { type: 'text' }) });
    const err = h('p', { class: 'ed-field-err', role: 'alert' });
    const warn = h('ul', { class: 'ed-field-warn', 'aria-live': 'polite' });
    const cur = () => findEl(store.get().hist.present, store.get().loc, id)?.el;
    // draft: testo che il servizio rifiuterebbe (graffa aperta…): resta nel campo col motivo, il tema tiene l'ultimo buono
    let draft = false, seen = el.text ?? '';
    const check = () => {
      const e = cur();
      const why = templateProblem(ta.value, inRow);
      err.textContent = why ? `Non applicato: ${why} — la scheda tiene il testo di prima.` : '';
      if (why) ta.setAttribute('aria-invalid', 'true'); else ta.removeAttribute('aria-invalid');
      warn.replaceChildren(...(e ? glyphWarnings({ ...e, text: ta.value }) : []).map((w) => h('li', null, w)));
    };
    const edit = () => {
      draft = !!templateProblem(ta.value, inRow);
      drafts.set(ta, draft);
      if (!draft) { A.setProp(id, f.key, ta.value); seen = cur()?.text ?? seen; }
      check();
    };
    ta.addEventListener('input', edit);
    ta.addEventListener('blur', () => A.endGesture());
    syncs.push(() => {
      const e = cur();
      if (!e) return;
      const t = e.text ?? '';
      // il testo del tema è cambiato da fuori (annulla, ripeti…): vale quello, anche sopra una bozza
      if (t !== seen) { seen = t; draft = false; drafts.set(ta, false); if (ta.value !== t) ta.value = t; }
      else if (!focused(ta) && !draft && ta.value !== t) ta.value = t;
      check();
    });

    // elenco dei valori: nome, esempio, filtro; riga.* solo dentro una lista
    const pickId = 'ed-p-values-' + f.key;
    const filter = selectOf('value-filter', FILTERS.map(([value, label]) => ({ value, label })), 'Filtro');
    const shown = (ex) => (filter.value === 'maiuscolo' ? ex.toLocaleUpperCase('it') : filter.value === 'minuscolo' ? ex.toLocaleLowerCase('it') : ex);
    const items = valueChoices(inRow).map((v) => {
      const ex = h('span', { class: 'ed-value-ex' });
      const b = h('button', { type: 'button', class: 'ed-value', 'data-value': v.name, onclick: () => insert(v.name) },
        h('span', { class: 'ed-value-name' }, '{' + v.name + '}'), ex);
      return { b, ex, v };
    });
    const relabel = () => { for (const it of items) it.ex.textContent = it.v.example ? shown(it.v.example) : '—'; };
    filter.addEventListener('change', relabel);
    relabel();
    const pick = h('div', { class: 'ed-values', id: pickId, hidden: true },
      h('div', { class: 'ed-field' }, h('label', { for: filter.id }, 'Filtro'), filter),
      h('ul', { class: 'ed-value-list', role: 'list' }, ...items.map((it) => h('li', null, it.b))));
    const toggle = h('button', { type: 'button', class: 'btn secondary ed-insert', 'data-k': 'insert-value', 'aria-expanded': 'false', 'aria-controls': pickId,
      onclick: () => { pick.hidden = !pick.hidden; toggle.setAttribute('aria-expanded', String(!pick.hidden)); } }, 'Inserisci valore');
    function insert(name) {
      const token = '{' + name + (filter.value ? '|' + filter.value : '') + '}';
      const a = ta.selectionStart ?? ta.value.length, b = ta.selectionEnd ?? a;
      ta.value = ta.value.slice(0, a) + token + ta.value.slice(b);
      pick.hidden = true; toggle.setAttribute('aria-expanded', 'false');
      ta.focus();
      ta.setSelectionRange(a + token.length, a + token.length);
      edit();
      A.endGesture();
    }
    return h('div', { class: 'ed-field' }, h('label', { for: ta.id }, f.label), ta, err, warn, toggle, pick);
  }

  // condizioni: tutte vere («e»); al massimo 8 contando gruppi sopra e figli (rifiuto in notice).
  // Il pannello si ricostruisce solo se cambiano numero o tipo delle condizioni; i valori si aggiornano sul posto.
  function condsField(id, f, el) {
    const list0 = Array.isArray(el.visibleIf) ? el.visibleIf : [];
    const now = () => { const v = findEl(store.get().hist.present, store.get().loc, id)?.el?.visibleIf; return Array.isArray(v) ? v : []; };
    // scelte (tipo, sì/no, Togli…): un passo ciascuna; i numeri: un passo per ogni volta che si entra nel campo
    const set = (next) => { A.setProp(id, 'visibleIf', next); A.endGesture(); };
    const live = (i, v) => { const k = condKey(now()[i]); A.setProp(id, 'visibleIf', now().map((x, j) => (j === i ? { [k]: v } : x))); };
    const bad = (msg) => store.set({ notice: msg });
    const rows = list0.map((c, i) => {
      const k = condKey(c);
      const d = CONDS.find((x) => x.key === k);
      const type = selectOf(`cond-${i}-type`, [...(d ? [] : [{ value: '', label: '(sconosciuta)', disabled: true }]), ...CONDS.map((x) => ({ value: x.key, label: x.label }))], `Condizione ${i + 1}`);
      type.value = k ?? '';
      type.addEventListener('change', () => set(now().map((x, j) => (j === i ? condDefault(type.value) : x))));
      const args = [];
      if (d && d.options) {
        const s = selectOf(`cond-${i}-arg`, d.options, d.label);
        s.addEventListener('change', () => { live(i, d.options.find((o) => String(o.value) === s.value)?.value); A.endGesture(); });
        syncs.push(() => { const x = now()[i]; if (x && condKey(x) === k) s.value = String(x[k]); });
        args.push(s);
      } else if (d && d.arg === 'int') {
        const n = numInput(`cond-${i}-arg`, d.label);
        n.addEventListener('input', () => {
          const v = n.value.trim();
          if (v === '') return;
          const x = intOf(v);
          if (x === null || x < d.min || x > d.max) { bad(`«${d.label}»: un numero intero da ${d.min} a ${d.max}.`); return; }
          live(i, x);
        });
        const sync = () => { const x = now()[i]; if (x && condKey(x) === k && !focused(n)) n.value = String(x[k]); };
        n.addEventListener('blur', () => { A.endGesture(); sync(); });
        syncs.push(sync);
        args.push(n, h('span', { class: 'ed-range' }, '%'));
      } else if (d && d.arg === 'sessions') {
        const mk = (side, label) => {
          const n = numInput(`cond-${i}-${side}`, `Sessioni ${label}`);
          n.addEventListener('input', () => {
            const cur = now()[i]?.sessions || {};
            const v = n.value.trim(), next = { ...cur };
            if (v === '') delete next[side];
            else {
              const x = intOf(v);
              if (x === null || x < 0 || x > 6) { bad('«Sessioni da/a»: un numero intero da 0 a 6.'); return; }
              next[side] = x;
            }
            if (next.min === undefined && next.max === undefined) { bad('«Sessioni da/a»: serve almeno uno dei due numeri.'); return; }
            live(i, next);
          });
          const sync = () => { const x = now()[i]?.sessions; if (x && !focused(n)) n.value = x[side] === undefined ? '' : String(x[side]); };
          n.addEventListener('blur', () => { A.endGesture(); sync(); });
          syncs.push(sync);
          return [h('span', { class: 'ed-range' }, label), n];
        };
        args.push(...mk('min', 'da'), ...mk('max', 'a'));
      }
      const del = h('button', { type: 'button', class: 'btn secondary ed-cond-del', 'data-k': `cond-${i}-del`, 'aria-label': `Togli la condizione ${i + 1}`,
        onclick: () => set(now().filter((_, j) => j !== i)) }, 'Togli');
      return h('li', { class: 'ed-cond' }, type, h('div', { class: 'ed-inline' }, ...args, del));
    });
    const add = h('button', { type: 'button', class: 'btn secondary', 'data-k': 'cond-add', onclick: () => set([...now(), condDefault('perm')]) }, 'Aggiungi condizione');
    return h('fieldset', { class: 'ed-conds' }, h('legend', null, f.label),
      h('p', { class: 'hint' }, list0.length ? 'Visibile quando sono tutte vere.' : 'Sempre visibile.'),
      h('ul', { class: 'ed-cond-list' }, ...rows), add);
  }

  // allinea (uno solo: rispetto allo schermo) e, con tre o più, distribuisci
  function alignSec(multi, n = 1) {
    const kids = [h('div', { class: 'ed-align', role: 'group', 'aria-label': 'Allinea' },
      ...ALIGNS.map(([mode, text, label]) => h('button', { type: 'button', class: 'btn secondary', 'data-k': 'align-' + mode, 'data-align': mode, 'aria-label': label, title: label,
        onclick: () => A.alignSel(mode) }, text)))];
    if (multi) {
      // servono almeno 3 elementi: con meno i pulsanti sono spenti e lo dicono
      const few = n < 3;
      kids.push(h('h3', null, 'Distribuisci'), h('div', { class: 'ed-align ed-two', role: 'group', 'aria-label': 'Distribuisci' },
        h('button', { type: 'button', class: 'btn secondary', 'data-k': 'dist-h', 'data-dist': 'h', 'aria-label': 'Distribuisci in orizzontale', disabled: few, onclick: () => A.distributeSel('h') }, 'Orizzontale'),
        h('button', { type: 'button', class: 'btn secondary', 'data-k': 'dist-v', 'data-dist': 'v', 'aria-label': 'Distribuisci in verticale', disabled: few, onclick: () => A.distributeSel('v') }, 'Verticale')),
        few ? h('p', { class: 'hint' }, 'Per distribuire servono almeno 3 elementi.') : null);
    }
    return section(multi ? 'Allinea' : 'Allinea allo schermo', ...kids);
  }

  function snapSec() {
    const btns = [1, 2, 4, 8].map((g) => h('button', { type: 'button', class: 'btn secondary', 'data-k': 'grid-' + g, 'data-grid': String(g),
      'aria-label': `Griglia di ${g} px`, onclick: () => A.setSnap({ grid: g }) }, g + ' px'));
    const others = h('input', { type: 'checkbox', id: 'ed-p-snap-others', 'data-k': 'snap-others' });
    others.addEventListener('change', () => A.setSnap({ others: others.checked }));
    syncs.push((s) => { for (const b of btns) b.setAttribute('aria-pressed', String(Number(b.dataset.grid) === s.snap.grid)); others.checked = s.snap.others; });
    return section('Aggancio', h('div', { class: 'ed-grid-pick', role: 'group', 'aria-label': 'Griglia' }, ...btns),
      h('label', { class: 'ed-check', for: others.id }, others, h('span', null, 'Aggancia agli elementi')));
  }

  function single(s, id) {
    const f = findEl(s.hist.present, s.loc, id);
    const el = f.el, inRow = f.trail.some((a) => a.type === 'list');
    const locked = el.locked === true || f.trail.some((a) => a.locked === true);
    const fields = fieldsFor(el.type);
    const byKey = (k) => fields.find((x) => x.key === k);
    const cur = (k) => findEl(store.get().hist.present, store.get().loc, id)?.el?.[k] ?? 0;
    const pos = ['x', 'y'].map((k) => stepRow(k, byKey(k).label, () => cur(k), (v) => A.setProp(id, k, v), true));
    const rest = fields.filter((x) => x.key !== 'x' && x.key !== 'y').map((x) => {
      switch (x.kind) {
        case 'int': return intField(id, x, el);
        case 'select': return selectField(id, x, el, inRow);
        case 'bool': return boolField(id, x);
        case 'font': return fontField(id, x);
        case 'template': return templateField(id, x, el, inRow);
        case 'conds': return condsField(id, x, el);
        default: return null;
      }
    });
    const type = elementLabel({ type: el.type });
    const head = h('div', { class: 'ed-prop-head' }, h('p', { class: 'ed-prop-name' }, elementLabel(el)),
      el.name ? h('p', { class: 'ed-prop-type' }, type) : null);
    const note = SCALED[el.type] ? h('p', { class: 'hint' }, `Misura a scala: ${SCALED[el.type][0]} × ${SCALED[el.type][1]} punti per la scala.`) : null;
    // bloccato: tutto in sola lettura (fieldset disabled), con il perché
    const body = h('fieldset', { class: 'ed-prop-body', disabled: locked }, h('legend', { class: 'sr-only' }, 'Campi'),
      section('Posizione', ...pos, note), section('Aspetto', ...rest.filter(Boolean)));
    const out = [head];
    if (locked) out.push(h('p', { class: 'ed-locked', role: 'note', 'data-locked': '' }, 'Bloccato: sbloccalo da «Livelli» per modificarlo.'));
    out.push(body);
    if (!locked) out.push(alignSec(false));
    if (el.type === 'group' && !locked) out.push(h('button', { type: 'button', class: 'btn secondary', 'data-k': 'ungroup', onclick: () => A.ungroupSel() }, 'Separa il gruppo'));
    if (!locked) out.push(selActions());
    return out;
  }

  // Duplica ed Elimina anche senza tastiera (telefono): come ⌘D e Canc
  const selActions = () => h('div', { class: 'ed-sel-actions' },
    h('button', { type: 'button', class: 'btn secondary', 'data-k': 'duplicate', onclick: () => A.duplicateSel() }, 'Duplica'),
    h('button', { type: 'button', class: 'btn secondary', 'data-k': 'delete', onclick: () => A.deleteSel() }, 'Elimina'));

  function multiple(s) {
    const box = () => {
      const st = store.get();
      return selectionBox(layout(st.hist.present, st.loc), st.sel);
    };
    const set = (axis) => (v) => {
      const b = box();
      if (!b) return;
      const d = Math.round(v) - (axis === 'x' ? b.x : b.y);
      if (d) A.moveSel(axis === 'x' ? d : 0, axis === 'y' ? d : 0, null, { snap: false });
    };
    return [
      h('p', { class: 'ed-prop-name' }, `${s.sel.length} elementi scelti`),
      section('Posizione', stepRow('x', 'X', () => box()?.x, set('x'), false), stepRow('y', 'Y', () => box()?.y, set('y'), false)),
      alignSec(true, s.sel.length),
      h('button', { type: 'button', class: 'btn secondary', 'data-k': 'group', onclick: () => A.groupSel() }, 'Raggruppa'),
      selActions(),
    ];
  }

  function variantProps(s) {
    const vs = s.hist.present.screens?.[s.loc.screen]?.variants;
    const label = SCREENS.find((x) => x.key === s.loc.screen)?.label ?? s.loc.screen;
    const out = [h('p', { class: 'hint' }, 'Nessun elemento scelto.')];
    if (!Array.isArray(vs) || !vs[s.loc.variant]) {
      out.push(section('Schermata', h('p', { class: 'hint' }, `«${label}»: schermata di serie, nessuna variante nel tema.`)));
      return out;
    }
    const when = selectOf('when', WHEN.map((w) => ({ value: w.key, label: w.label })), 'Quando');
    when.addEventListener('change', () => A.setVariantProp({ when: when.value }));
    const ref = selectOf('vref', REFS.filter((r) => r.key === 'urgent' || r.key === 'selected').map((r) => ({ value: r.key, label: r.label })), 'Sessione di riferimento');
    ref.addEventListener('change', () => A.setVariantProp({ ref: ref.value }));
    const count = h('p', { class: 'hint' });
    syncs.push((st) => {
      const v = st.hist.present.screens?.[st.loc.screen]?.variants?.[st.loc.variant];
      if (!v) return;
      when.value = v.when ?? 'always';
      ref.value = v.ref === 'urgent' ? 'urgent' : 'selected';
      count.textContent = `Variante ${st.loc.variant + 1} di ${st.hist.present.screens[st.loc.screen].variants.length}`;
    });
    out.push(section('Variante', count, field(when.id, 'Quando', when), field(ref.id, 'Sessione di riferimento', ref)),
      section('Schermata', h('p', { class: 'hint' }, `«${label}»: la scheda usa la prima variante vera.`)));
    return out;
  }

  // forma del pannello: se cambia si ricostruisce (col fuoco rimesso dov'era)
  function shapeOf(s) {
    if (!s.hist) return 'none';
    const base = [s.themeId, s.loc.screen, s.loc.variant, s.sel.join(',')];
    if (s.sel.length !== 1) return JSON.stringify([...base, !!s.hist.present.screens?.[s.loc.screen]?.variants?.[s.loc.variant]]);
    const f = findEl(s.hist.present, s.loc, s.sel[0]);
    if (!f) return JSON.stringify([...base, 'gone']);
    const locked = f.el.locked === true || f.trail.some((a) => a.locked === true);
    return JSON.stringify([...base, f.el.type, f.el.name ?? '', locked, f.trail.some((a) => a.type === 'list'), (Array.isArray(f.el.visibleIf) ? f.el.visibleIf : []).map(condKey)]);
  }

  function build(s) {
    if (!s.hist) return [h('p', { class: 'hint' }, 'Nessun tema aperto.')];
    let out;
    if (s.sel.length === 1 && findEl(s.hist.present, s.loc, s.sel[0])) out = single(s, s.sel[0]);
    else if (s.sel.length > 1) out = multiple(s);
    else out = variantProps(s);
    return [...out, snapSec()];
  }

  function render(s) {
    note.textContent = s.notice;
    note.hidden = !s.notice;
    const sh = shapeOf(s);
    if (sh !== shape) {
      const ae = document.activeElement;
      const k = host.contains(ae) ? ae.dataset.k : null;
      const caret = k && typeof ae.selectionStart === 'number' ? [ae.selectionStart, ae.selectionEnd] : null;
      shape = sh;
      syncs = [];
      const lost = [...drafts.values()].some(Boolean);
      drafts.clear();
      if (lost) queueMicrotask(() => store.set({ notice: 'Testo non applicato scartato.' }));
      host.replaceChildren(...build(s));
      if (k) {
        const again = host.querySelector(`[data-k="${CSS.escape(k)}"]`);
        if (again && !again.disabled) {
          again.focus({ preventScroll: true });
          if (caret && typeof again.setSelectionRange === 'function') try { again.setSelectionRange(...caret); } catch { /* select */ }
        }
      }
    }
    for (const fn of syncs) fn(s);
  }
  store.subscribe(render);
  render(store.get());
}
