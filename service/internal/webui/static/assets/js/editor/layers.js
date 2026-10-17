// Pannello «Livelli»: righe, bersagli di trascinamento e passi Su/Giù. Puro: niente window/document (F7).
// Ordine delle righe = dal più in alto (ultimo disegnato) al più in basso; i figli stanno sotto il loro gruppo/lista.
import { lowerTheme } from '../theme/lower.js';
import { condsTrue, urgentIdx, utf8Cut } from '../theme/values.js';
import { elementLabel } from './warnings.js';
import { testModel } from './testdata.js';
import { LOCKED_SCREENS } from './ops.js';

const isObj = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
const arr = (v) => (Array.isArray(v) ? v : []);

// nome di un livello: al massimo 32 byte (come parse.go; il nome del tema ne ha 24), senza a capo né caratteri di controllo
export const NAME_MAX = 32;
export function cleanName(s) {
  const t = typeof s === 'string' ? s.replace(/[\x00-\x1f\x7f]/g, ' ').trim() : '';
  return utf8Cut(t, NAME_MAX).trim();
}

// visibleIf falso con il modello di prova? Si abbassa un rettangolo finto con le stesse condizioni (stesso lower.js della scheda)
function condsFail(visibleIf, m, screen) {
  if (!Array.isArray(visibleIf) || !visibleIf.length) return false;
  try {
    const low = lowerTheme({ screens: { home: { variants: [{ when: 'always', elements: [{ type: 'rect', w: 1, h: 1, visibleIf }] }] } } });
    const conds = low.screens.home?.variants?.[0]?.elems?.[0]?.conds ?? [];
    const subject = m.sessions.length ? m.sessions[urgentIdx(m)] : null;
    return !condsTrue(conds, { m, frame: m.frame, screen, ref: -1 }, subject);
  } catch { return false; }
}

// righe del pannello. test = opzioni dei dati di prova (state.test).
// Oltre ai campi del brief: parentId, index (nel contenitore), count (fratelli), kids (figli diretti), lockedAbove (antenato bloccato).
export function layerRows(theme, loc, sel, scope = null, test = {}) {
  const scr = loc && !LOCKED_SCREENS.includes(loc.screen) ? theme?.screens?.[loc.screen] : null;
  const v = isObj(scr) ? arr(scr.variants)[loc.variant] : null;
  if (!isObj(v)) return [];
  const m = testModel(test), picked = new Set(arr(sel)), out = [];
  const walk = (els, depth, parentId, lockedAbove, failAbove) => {
    const list = arr(els);
    for (let i = list.length - 1; i >= 0; i--) {
      const el = list[i];
      if (!isObj(el) || typeof el.id !== 'string') continue;
      const key = el.type === 'group' ? 'children' : el.type === 'list' ? 'row' : null;
      const fail = failAbove || condsFail(el.visibleIf, m, loc.screen);
      out.push({
        id: el.id, depth, label: elementLabel(el), type: el.type, hidden: el.hidden === true, locked: el.locked === true,
        selected: picked.has(el.id), container: key !== null, hiddenByData: fail,
        parentId, index: i, count: list.length, kids: key ? arr(el[key]).filter((c) => isObj(c) && typeof c.id === 'string').length : 0,
        lockedAbove,
      });
      if (key) walk(el[key], depth + 1, el.id, lockedAbove || el.locked === true, fail);
    }
  };
  walk(v.elements, 0, null, false, false);
  return out;
}

// dove cadrebbe `dragId` se lo si lasciasse sulla riga `overId` (null = sotto l'ultima riga) a `frac` (0–1) della sua altezza.
// → { parentId, index, mark: 'above' | 'below' | 'into' | 'end', overId } con `index` nella lista senza l'elemento (come ops.reorder), o null
export function dropTarget(rows, dragId, overId, frac = 0.5) {
  const di = rows.findIndex((r) => r.id === dragId);
  if (di < 0) return null;
  const drag = rows[di];
  // la riga e i suoi discendenti non sono bersagli (niente gruppo dentro di sé)
  const inside = new Set([dragId]);
  for (let i = di + 1; i < rows.length && rows[i].depth > drag.depth; i++) inside.add(rows[i].id);
  let parentId, raw, mark;
  if (overId === null || overId === undefined) { parentId = null; raw = 0; mark = 'end'; }
  else {
    if (inside.has(overId)) return null;
    const over = rows.find((r) => r.id === overId);
    if (!over) return null;
    if (over.container && frac >= 0.25 && frac <= 0.75) { parentId = over.id; raw = over.kids; mark = 'into'; }
    else if (frac < 0.5) { parentId = over.parentId; raw = over.index + 1; mark = 'above'; }
    else { parentId = over.parentId; raw = over.index; mark = 'below'; }
  }
  const same = parentId === drag.parentId;
  const index = same && drag.index < raw ? raw - 1 : raw;
  if (same && index === drag.index) return null;
  return { parentId, index, mark, overId: overId ?? null };
}

// «Su» = verso l'alto del pannello = disegnato dopo (indice + 1); «Giù» il contrario. null ai bordi.
export function stepTarget(rows, id, dir) {
  const r = rows.find((x) => x.id === id);
  if (!r) return null;
  const index = dir === 'up' ? r.index + 1 : r.index - 1;
  if (index < 0 || index >= r.count) return null;
  return { parentId: r.parentId, index };
}
