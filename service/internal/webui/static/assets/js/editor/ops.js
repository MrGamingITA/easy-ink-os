// Operazioni pure sul tema (editor): ogni funzione restituisce un tema nuovo che condivide le parti non toccate,
// non lancia mai, e con un argomento non valido lascia il tema invariato. Nessun accesso a window/document.
import { SCREEN_KEYS } from '../theme/lower.js';

export const LOCKED_SCREENS = ['perm', 'pair', 'confirm', 'boot', 'ota', 'battlow', 'calib', 'gbload', 'gbplay'];

const WHENS = ['always', 'none', 'one', 'many'];
const MAX_ELEMENTS = 64, COORD = 512;
const MAX_VARIANTS = 8, MAX_DEPTH = 4, MAX_NAME = 24, MAX_CONDS = 8;
const TYPES = ['text', 'clock', 'mascot', 'battery', 'wifi', 'stateIcon', 'sym', 'bar', 'qr', 'line', 'rect', 'circle', 'list', 'group'];
// scala: [base larghezza, base altezza, massimo]
export const SCALED = { mascot: [28, 25, 4], battery: [16, 8, 2], wifi: [11, 9, 2], stateIcon: [7, 7, 3], sym: [7, 7, 3] };

const isObj = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
const arr = (v) => (Array.isArray(v) ? v : []);
const clone = (v) => JSON.parse(JSON.stringify(v));
const isScreen = (s) => SCREEN_KEYS.includes(s);
const num = (v) => Number.isFinite(v);
const clampXY = (v) => Math.max(-COORD, Math.min(COORD, Math.round(v)));

const R = {
  row: 'Un elemento che usa i valori della riga ({riga.…}) può stare solo dentro una lista.',
  list: 'Una lista non può stare dentro un\'altra lista.',
  depth: 'I gruppi si annidano al massimo per 4 livelli.',
  place: 'Non si può mettere lì.',
  max: 'Al massimo 64 elementi per variante.',
  lastRow: 'La riga di una lista deve avere almeno un elemento.',
  locked: 'L\'elemento è bloccato.',
  conds: 'Oltre 8 condizioni: toglierne qualcuna prima.',
  noop: 'Nessuna modifica.',
};

// l'elemento (o un discendente) usa i valori di riga: vale solo dentro una lista
function usesRow(el) {
  if (!isObj(el) || el.type === 'list') return false;  // la lista porta le sue righe con sé
  if (el.ref === 'row' || (typeof el.text === 'string' && /\{riga\./.test(el.text))) return true;
  const k = childrenKey(el);
  return k ? arr(el[k]).some(usesRow) : false;
}

// elementi contati dal servizio: niente gruppi né nascosti; la lista conta anche le sue righe
function countEl(el) {
  if (!isObj(el) || el.hidden === true) return 0;
  if (el.type === 'group') return arr(el.children).reduce((n, c) => n + countEl(c), 0);
  return 1 + (el.type === 'list' ? arr(el.row).reduce((n, c) => n + countEl(c), 0) : 0);
}
const countVariant = (v) => arr(v?.elements).reduce((n, c) => n + countEl(c), 0);

// ---------- id ----------

const childrenKey = (el) => (el.type === 'group' ? 'children' : el.type === 'list' ? 'row' : null);

// visita in profondità: gruppo → figli, lista → righe
function eachEl(els, fn) {
  for (const el of arr(els)) {
    if (!isObj(el)) continue;
    fn(el);
    const k = childrenKey(el);
    if (k) eachEl(el[k], fn);
  }
}

function eachElInTheme(theme, fn) {
  const screens = isObj(theme) && isObj(theme.screens) ? theme.screens : {};
  for (const s of Object.keys(screens)) for (const v of arr(screens[s]?.variants)) eachEl(v?.elements, fn);
}

export function allIds(theme) {
  const out = [];
  eachElInTheme(theme, (el) => { if (typeof el.id === 'string') out.push(el.id); });
  return out;
}

function nextFree(used) {
  let n = 1;
  while (used.has('e' + n)) n++;
  return 'e' + n;
}

// copia con id nuovi (sé stesso e i discendenti); `used` si aggiorna
function withFreshIds(el, used) {
  const c = { ...el, id: nextFree(used) };
  used.add(c.id);
  const k = childrenKey(c);
  if (k) c[k] = arr(c[k]).map((x) => (isObj(x) ? withFreshIds(x, used) : x));
  return c;
}

const usedIds = (theme) => new Set(allIds(theme));

export function ensureIds(theme) {
  if (!isObj(theme) || !isObj(theme.screens)) return theme;
  const used = usedIds(theme), seen = new Set();
  // passa 1: id validi e non ancora visti restano, gli altri ne ricevono uno libero
  const fixList = (els) => {
    if (!Array.isArray(els)) return els;
    let changed = false;
    const out = els.map((el) => {
      if (!isObj(el)) return el;
      let e = el;
      if (typeof e.id !== 'string' || e.id === '' || seen.has(e.id)) {
        const id = nextFree(used);
        used.add(id);
        e = { ...e, id };
      }
      seen.add(e.id);
      const k = childrenKey(e);
      if (k) {
        const sub = fixList(e[k]);
        if (sub !== e[k]) e = { ...e, [k]: sub };
      }
      if (e !== el) changed = true;
      return e;
    });
    return changed ? out : els;
  };
  let screens = theme.screens, schanged = false;
  const nextScreens = {};
  for (const s of Object.keys(screens)) {
    const sc = screens[s];
    let scr = sc;
    if (isObj(sc) && Array.isArray(sc.variants)) {
      let vchanged = false;
      const vs = sc.variants.map((v) => {
        if (!isObj(v)) return v;
        const els = fixList(v.elements);
        if (els === v.elements) return v;
        vchanged = true;
        return { ...v, elements: els };
      });
      if (vchanged) scr = { ...sc, variants: vs };
    }
    if (scr !== sc) schanged = true;
    nextScreens[s] = scr;
  }
  return schanged ? { ...theme, screens: nextScreens } : theme;
}

// ---------- nomi ----------

const byteLen = (s) => new TextEncoder().encode(s).length;

// al massimo 24 byte (parse.go: textOK), senza a capo né caratteri di controllo
function cleanName(name) {
  let s = String(name ?? '').replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim();
  while (byteLen(s) > MAX_NAME) s = Array.from(s).slice(0, -1).join('');
  return s.trim();
}

export function kebabId(name, taken = []) {
  let base = String(name ?? '').toLowerCase().normalize('NFD').replace(/\p{M}+/gu, '')
    .replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, MAX_NAME).replace(/-+$/g, '');
  if (!base) base = 'tema';
  const bad = (id) => id === 'calamaio' || arr(taken).includes(id);
  if (!bad(base)) return base;
  for (let n = 2; ; n++) {
    const suf = '-' + n;
    const id = base.slice(0, MAX_NAME - suf.length).replace(/-+$/g, '') + suf;
    if (!bad(id)) return id;
  }
}

export function newTheme(name, taken = []) {
  const nm = cleanName(name) || 'Tema';
  return { format: 'calamaio-theme', version: 1, id: kebabId(nm, taken), name: nm, screen: '250x122',
    screens: { home: { variants: [{ when: 'always', elements: [] }] } } };
}

export function renameTheme(theme, name) {
  const nm = cleanName(name);
  if (!isObj(theme) || !nm || nm === theme.name) return theme;
  return { ...theme, name: nm };
}

// ---------- predefiniti ----------

const FONT = () => ({ family: 'ui', size: 12, weight: 600 });

export function elementDefaults(type, x = 0, y = 0) {
  const base = { type, x, y };
  switch (type) {
    case 'text': return { ...base, w: 80, text: 'Testo', font: FONT() };
    case 'clock': return { ...base, w: 40, font: { family: 'ui', size: 12, weight: 700 } };
    case 'mascot': return { ...base, scale: 1 };
    case 'battery': case 'wifi': case 'stateIcon': return base;
    case 'sym': return { ...base, sym: 'bell' };
    case 'bar': return { ...base, w: 60, h: 6, source: 'context', style: 'bar' };
    case 'qr': return { ...base, w: 40, h: 40, text: '{pc}' };
    case 'line': return { ...base, w: 60, h: 1 };
    case 'rect': return { ...base, w: 40, h: 20 };
    case 'circle': return { ...base, w: 20, h: 20 };
    // il testo della riga ha il carattere: il servizio lo richiede
    case 'list': return { ...base, w: 120, h: 60, rowH: 20, gap: 2,
      row: [{ type: 'text', x: 0, y: 4, w: 120, text: '{riga.nome}', font: FONT() }] };
    default: return null;
  }
}

// ---------- ricerca ----------

function variantOf(theme, loc) {
  if (!isScreen(loc?.screen)) return null;  // Permessi e le altre bloccate: mai
  const v = theme?.screens?.[loc?.screen]?.variants?.[loc?.variant];
  return isObj(v) ? v : null;
}

// ricerca ricorsiva: cammino (antenati) fino all'elemento
function search(els, id, trail) {
  const list = arr(els);
  for (let i = 0; i < list.length; i++) {
    const el = list[i];
    if (!isObj(el)) continue;
    if (el.id === id) return { el, index: i, trail, siblings: list };
    const k = childrenKey(el);
    if (k) {
      const r = search(el[k], id, trail.concat(el));
      if (r) return r;
    }
  }
  return null;
}

export function findEl(theme, loc, id) {
  const v = variantOf(theme, loc);
  if (!v || typeof id !== 'string') return null;
  const r = search(v.elements, id, []);
  if (!r) return null;
  const parent = r.trail.length ? r.trail[r.trail.length - 1] : null;
  let ox = 0, oy = 0, depth = 0;
  for (const a of r.trail) { ox += a.x || 0; oy += a.y || 0; if (a.type === 'group') depth++; }
  return { el: r.el, parentId: parent ? parent.id : null, list: parent ? (parent.type === 'list' ? 'row' : 'children') : 'elements',
    index: r.index, ox, oy, depth, trail: r.trail };
}

const isLockedPath = (f) => f.el.locked === true || f.trail.some((a) => a.locked === true);

// il contenitore di destinazione (o un suo antenato) è bloccato: niente dentro
function lockedTarget(theme, loc, parentId) {
  if (parentId === null || parentId === undefined) return false;
  const f = findEl(theme, loc, parentId);
  return !!f && isLockedPath(f);
}

// livello di gruppi annidati sotto un elemento (un gruppo senza gruppi dentro = 1)
function groupHeight(el) {
  if (!isObj(el)) return 0;
  const k = childrenKey(el);
  const inner = k ? arr(el[k]).reduce((m, c) => Math.max(m, groupHeight(c)), 0) : 0;
  return inner + (el.type === 'group' ? 1 : 0);
}

const hasList = (el) => isObj(el) && (el.type === 'list' || arr(childrenKey(el) ? el[childrenKey(el)] : []).some(hasList));

// ---------- scrittura ----------

// sostituisce la variante (crea tutto ciò che manca se `create`)
function setVariant(theme, loc, fn, create = false) {
  if (!isObj(theme) || !isObj(loc) || !isScreen(loc.screen)) return theme;
  const scr = theme.screens?.[loc.screen];
  const variants = isObj(scr) && Array.isArray(scr.variants) ? scr.variants : null;
  let v = variants ? variants[loc.variant] : undefined;
  if (!isObj(v)) {
    if (!create || !Number.isInteger(loc.variant) || loc.variant < 0 || loc.variant > (variants ? variants.length : 0) || loc.variant >= MAX_VARIANTS) return theme;
    v = { when: 'always', elements: [] };
  }
  const nv = fn(v);
  if (!nv || nv === v) return theme;
  const list = variants ? variants.slice() : [];
  list[loc.variant] = nv;
  return { ...theme, screens: { ...theme.screens, [loc.screen]: { ...(isObj(scr) ? scr : {}), variants: list } } };
}

// applica fn all'array del contenitore (parentId null = variante); fn restituisce il nuovo array o null
function editContainer(els, parentId, fn) {
  if (parentId === null) return fn(arr(els));
  let done = false;
  const walk = (list) => {
    let changed = false;
    const out = list.map((el) => {
      if (done || !isObj(el)) return el;
      const k = childrenKey(el);
      if (!k) return el;
      if (el.id === parentId) {
        done = true;
        const nl = fn(arr(el[k]));
        if (!nl) return el;
        changed = true;
        return { ...el, [k]: nl };
      }
      const sub = walk(arr(el[k]));
      if (sub === null) return el;
      changed = true;
      return { ...el, [k]: sub };
    });
    return changed ? out : null;
  };
  return walk(arr(els));
}

// cambia un solo elemento (anche annidato)
function mapOne(els, id, fn) {
  let hit = false;
  const walk = (list) => {
    let changed = false;
    const out = list.map((el) => {
      if (hit || !isObj(el)) return el;
      if (el.id === id) { hit = true; const n = fn(el); if (n !== el) changed = true; return n; }
      const k = childrenKey(el);
      if (!k || !Array.isArray(el[k])) return el;
      const sub = walk(el[k]);
      if (sub === el[k]) return el;
      changed = true;
      return { ...el, [k]: sub };
    });
    return changed ? out : list;
  };
  return walk(arr(els));
}

const editVariantEls = (theme, loc, fn) => setVariant(theme, loc, (v) => {
  const next = fn(arr(v.elements));
  return next && next !== v.elements ? { ...v, elements: next } : v;
});

// toglie gli id dati; un gruppo rimasto vuoto sparisce; una riga non resta mai vuota (resta com'era)
function removeIds(els, ids) {
  let changed = false;
  const out = [];
  for (const el of els) {
    if (isObj(el) && ids.has(el.id)) { changed = true; continue; }
    const k = isObj(el) ? childrenKey(el) : null;
    if (k && Array.isArray(el[k])) {
      const sub = removeIds(el[k], ids);
      if (sub !== el[k]) {
        if (el.type === 'group' && sub.length === 0) { changed = true; continue; }
        if (el.type === 'list' && sub.length === 0) { out.push(el); continue; }
        changed = true;
        out.push({ ...el, [k]: sub });
        continue;
      }
    }
    out.push(el);
  }
  return changed ? out : els;
}

// ids senza quelli che hanno un antenato già in ids, nell'ordine di disegno
function topLevelIds(theme, loc, ids) {
  const v = variantOf(theme, loc);
  if (!v) return [];
  const want = new Set(arr(ids));
  const out = [];
  const walk = (els, covered) => {
    for (const el of arr(els)) {
      if (!isObj(el)) continue;
      const take = want.has(el.id) && !covered;
      if (take) out.push(el.id);
      const k = childrenKey(el);
      if (k) walk(el[k], covered || take);
    }
  };
  walk(v.elements, false);
  return out;
}

// controlla dove va un elemento: contenitore valido, niente lista in una riga, al massimo 4 livelli di gruppi
function containerInfo(theme, loc, parentId) {
  if (parentId === null || parentId === undefined) return { level: 0, inRow: false, ox: 0, oy: 0 };
  const f = findEl(theme, loc, parentId);
  if (!f || (f.el.type !== 'group' && f.el.type !== 'list')) return null;
  const inRow = f.el.type === 'list' || f.trail.some((a) => a.type === 'list');
  return { level: f.depth + (f.el.type === 'group' ? 1 : 0), inRow, ox: f.ox + (f.el.x || 0), oy: f.oy + (f.el.y || 0) };
}

// perché `el` non può andare in questo contenitore (null = può)
function placeReason(info, el) {
  if (!info) return R.place;
  if (info.inRow && hasList(el)) return R.list;
  if (!info.inRow && usesRow(el)) return R.row;
  if (info.level + groupHeight(el) > MAX_DEPTH) return R.depth;
  return null;
}
const fitsIn = (info, el) => placeReason(info, el) === null;

function insertAt(list, index, items) {
  const i = Number.isInteger(index) ? Math.max(0, Math.min(list.length, index)) : list.length;
  return [...list.slice(0, i), ...items, ...list.slice(i)];
}

// ---------- aggiunta ----------

export function addEl(theme, loc, el, opts) {
  const { parentId = null, index } = opts ?? {};
  const none = (reason) => ({ theme, id: null, reason });
  if (!isObj(theme) || !isObj(el) || !TYPES.includes(el.type) || !loc || !isScreen(loc.screen)) return none(R.place);
  if (lockedTarget(theme, loc, parentId)) return none(R.locked);
  const info = containerInfo(theme, loc, parentId);
  const why = placeReason(info, el);
  if (why) return none(why);
  if (countVariant(variantOf(theme, loc)) + countEl(el) > MAX_ELEMENTS) return none(R.max);
  const used = usedIds(theme);
  const item = withFreshIds(el, used);
  const next = setVariant(theme, loc, (v) => {
    const els = editContainer(v.elements, parentId ?? null, (l) => insertAt(l, index, [item]));
    return els ? { ...v, elements: els } : v;
  }, true);
  return next === theme ? none(R.place) : { theme: next, id: item.id };
}

const PROTECTED = new Set(['id', 'type', 'children', 'row']);

export function updateEl(theme, loc, id, patch) {
  if (!isObj(patch) || !findEl(theme, loc, id)) return theme;
  return editVariantEls(theme, loc, (els) => mapOne(els, id, (el) => {
    const n = { ...el };
    for (const [k, val] of Object.entries(patch)) {
      if (PROTECTED.has(k)) continue;
      if (val === undefined) delete n[k];
      else if ((k === 'x' || k === 'y') && !num(val)) continue;
      else n[k] = k === 'x' || k === 'y' ? clampXY(val) : val;
    }
    return JSON.stringify(n) === JSON.stringify(el) ? el : n;
  }));
}

export function moveEls(theme, loc, ids, dx, dy) {
  if (!num(dx) || !num(dy) || (dx === 0 && dy === 0)) return theme;
  const move = new Set();
  for (const id of topLevelIds(theme, loc, ids)) {
    const f = findEl(theme, loc, id);
    if (f && !isLockedPath(f)) move.add(id);
  }
  if (!move.size) return theme;
  return editVariantEls(theme, loc, (els) => {
    let cur = els;
    for (const id of move) cur = mapOne(cur, id, (el) => ({ ...el, x: clampXY((el.x || 0) + dx), y: clampXY((el.y || 0) + dy) }));
    return cur;
  });
}

export function setBox(theme, loc, id, box) {
  const f = findEl(theme, loc, id);
  if (!f || !isObj(box) || !num(box.x) || !num(box.y)) return theme;
  const el = f.el, patch = { x: Math.round(box.x - f.ox), y: Math.round(box.y - f.oy) };
  if (el.type === 'group') {
    // il riquadro di un gruppo è l'unione dei figli: si sposta della differenza dall'angolo dell'unione
    const tl = topLeft(el);
    return updateEl(theme, loc, id, { x: (el.x || 0) + patch.x - tl.x, y: (el.y || 0) + patch.y - tl.y });
  }
  const w = num(box.w) ? Math.round(box.w) : null, h = num(box.h) ? Math.round(box.h) : null;
  const sc = SCALED[el.type];
  if (sc) {
    if (w !== null) patch.scale = Math.max(1, Math.min(sc[2], Math.round(w / sc[0])));
  } else if (el.type === 'text') {
    if (w !== null) patch.w = Math.max(1, w);
    if (h !== null) {
      const lh = Number.isInteger(el.lineHeight) && el.lineHeight > 0 ? el.lineHeight : (Number.isInteger(el.font?.size) ? el.font.size : 10) + 2;
      patch.lines = Math.max(1, Math.min(8, Math.round(h / lh)));
    }
  } else if (el.type === 'clock') {
    if (w !== null) patch.w = Math.max(1, w);
  } else if (el.type !== 'group') {
    if (w !== null) patch.w = Math.max(1, w);
    if (h !== null) patch.h = Math.max(1, h);
  }
  return updateEl(theme, loc, id, patch);
}

export function removeEls(theme, loc, ids) {
  const set = new Set(arr(ids));
  return editVariantEls(theme, loc, (els) => removeIds(els, set));
}

export function reorder(theme, loc, id, opts) {
  const { parentId = null, index } = opts ?? {};
  const f = findEl(theme, loc, id);
  if (!f) return theme;
  const target = parentId ?? null;
  if (target === id || (target !== null && search(arr([f.el]), target, [])?.el)) return theme;  // dentro sé stesso
  if (lockedTarget(theme, loc, target) || f.trail.some((a) => a.locked === true)) return theme;   // il contenuto di un gruppo bloccato non cambia, né dentro né fuori
  const info = containerInfo(theme, loc, target);
  if (!fitsIn(info, f.el)) return theme;
  const dx = f.ox - info.ox, dy = f.oy - info.oy;  // la posizione assoluta resta
  const moved = { ...f.el, x: (f.el.x || 0) + dx, y: (f.el.y || 0) + dy };
  const next = editVariantEls(theme, loc, (els) => {
    // 1) toglie l'elemento (senza ripulire), 2) lo inserisce, 3) ripulisce gruppi vuoti
    const without = mapRemoveOne(els, id);
    const placed = editContainer(without, target, (l) => insertAt(l, index, [moved]));
    return placed ? pruneEmpty(placed) : null;
  });
  return rowsOk(next) ? next : theme;
}

function mapRemoveOne(els, id) {
  const out = [];
  for (const el of els) {
    if (isObj(el) && el.id === id) continue;
    const k = isObj(el) ? childrenKey(el) : null;
    if (k && Array.isArray(el[k])) {
      const sub = mapRemoveOne(el[k], id);
      if (sub.length !== el[k].length || sub.some((s, i) => s !== el[k][i])) { out.push({ ...el, [k]: sub }); continue; }
    }
    out.push(el);
  }
  return out;
}

function pruneEmpty(els) {
  let changed = false;
  const out = [];
  for (const el of els) {
    const k = isObj(el) ? childrenKey(el) : null;
    if (k && Array.isArray(el[k])) {
      const sub = pruneEmpty(el[k]);
      if (el.type === 'group' && sub.length === 0) { changed = true; continue; }
      if (sub !== el[k]) { changed = true; out.push({ ...el, [k]: sub }); continue; }
    }
    out.push(el);
  }
  return changed ? out : els;
}

// nessuna riga di lista vuota (il servizio la rifiuterebbe)
function rowsOk(theme) {
  let ok = true;
  eachElInTheme(theme, (el) => { if (el.type === 'list' && arr(el.row).length === 0) ok = false; });
  return ok;
}

// ---------- gruppi ----------

// angolo in alto a sinistra di un elemento nel suo contenitore (per un gruppo: l'unione dei figli)
function topLeft(el) {
  let x = el.x || 0, y = el.y || 0;
  if (el.type === 'group' && arr(el.children).length) {
    const tl = arr(el.children).filter(isObj).map(topLeft);
    x += Math.min(...tl.map((p) => p.x));
    y += Math.min(...tl.map((p) => p.y));
  }
  return { x, y };
}

export function groupEls(theme, loc, ids) {
  const none = (reason) => ({ theme, id: null, reason });
  const want = arr(ids).filter((i) => typeof i === 'string');
  if (!want.length) return none(R.noop);
  const finds = want.map((i) => findEl(theme, loc, i));
  if (finds.some((f) => !f)) return none(R.noop);
  const first = finds[0];
  if (finds.some((f) => f.parentId !== first.parentId)) return none('Per raggruppare gli elementi devono stare nello stesso posto.');
  const info = containerInfo(theme, loc, first.parentId);
  const height = Math.max(...finds.map((f) => groupHeight(f.el)));
  if (!info || info.level + height + 1 > MAX_DEPTH) return none(R.depth);
  const set = new Set(want);
  const used = usedIds(theme);
  const gid = nextFree(used);
  const next = editVariantEls(theme, loc, (els) => editContainer(els, first.parentId, (list) => {
    const members = list.filter((e) => isObj(e) && set.has(e.id));
    if (!members.length) return null;
    const tl = members.map(topLeft);
    const gx = Math.min(...tl.map((p) => p.x)), gy = Math.min(...tl.map((p) => p.y));
    const group = { type: 'group', id: gid, x: gx, y: gy,
      children: members.map((m) => ({ ...m, x: (m.x || 0) - gx, y: (m.y || 0) - gy })) };
    const last = members[members.length - 1];
    const out = [];
    for (const e of list) {
      if (e === last) out.push(group);
      else if (!(isObj(e) && set.has(e.id))) out.push(e);
    }
    return out;
  }));
  return next === theme ? none(R.noop) : { theme: next, id: gid };
}

export function ungroup(theme, loc, id) {
  const none = (reason) => ({ theme, ids: null, reason });
  const f = findEl(theme, loc, id);
  if (!f || f.el.type !== 'group') return none(R.noop);
  const g = f.el, gv = arr(g.visibleIf);
  const kids = arr(g.children).filter(isObj);
  if (!kids.length) return none(R.noop);
  if (kids.some((c) => arr(c.visibleIf).length + gv.length > MAX_CONDS)) return none(R.conds);
  const moved = kids.map((c) => {
    const n = { ...c, x: (c.x || 0) + (g.x || 0), y: (c.y || 0) + (g.y || 0) };
    if (gv.length) n.visibleIf = arr(c.visibleIf).concat(gv);
    if (g.hidden === true) n.hidden = true;
    if (g.locked === true) n.locked = true;
    return n;
  });
  const next = editVariantEls(theme, loc, (els) => editContainer(els, f.parentId, (list) => {
    const i = list.indexOf(g);
    return i < 0 ? null : [...list.slice(0, i), ...moved, ...list.slice(i + 1)];
  }));
  return next === theme ? none(R.noop) : { theme: next, ids: moved.map((m) => m.id) };
}

// ---------- duplica, copia, incolla ----------

export function duplicateEls(theme, loc, ids) {
  const none = (reason) => ({ theme, ids: null, reason });
  const tops = topLevelIds(theme, loc, ids);
  if (!tops.length) return none(R.noop);
  let full = false;
  const used = usedIds(theme), newIds = [];
  let cur = theme;
  for (const id of tops) {
    const f = findEl(cur, loc, id);
    if (!f) continue;
    const copy = withFreshIds({ ...f.el, x: clampXY((f.el.x || 0) + 8), y: clampXY((f.el.y || 0) + 8) }, used);
    if (countVariant(variantOf(cur, loc)) + countEl(copy) > MAX_ELEMENTS) { full = true; break; }
    const next = editVariantEls(cur, loc, (els) => editContainer(els, f.parentId, (list) => {
      const i = list.indexOf(f.el);
      return i < 0 ? null : insertAt(list, i + 1, [copy]);
    }));
    if (next === cur) continue;
    cur = next;
    newIds.push(copy.id);
  }
  return newIds.length ? { theme: cur, ids: newIds } : none(full ? R.max : R.noop);
}

export function copyEls(theme, loc, ids) {
  const items = [];
  for (const id of topLevelIds(theme, loc, ids)) {
    const f = findEl(theme, loc, id);
    if (f) items.push({ ...clone(f.el), x: (f.el.x || 0) + f.ox, y: (f.el.y || 0) + f.oy });
  }
  return { items };
}

export function pasteEls(theme, loc, clip, opts) {
  const { dx = 8, dy = 8, parentId = null } = opts ?? {};
  const none = (reason) => ({ theme, ids: null, reason });
  const items = isObj(clip) ? arr(clip.items).filter((i) => isObj(i) && TYPES.includes(i.type)) : [];
  if (!items.length || !isObj(theme) || !loc || !isScreen(loc.screen) || !num(dx) || !num(dy)) return none(R.noop);
  const target = parentId ?? null;
  const info = containerInfo(theme, loc, target);
  if (!info) return none(R.place);
  for (const i of items) { const why = placeReason(info, i); if (why) return none(why); }
  if (countVariant(variantOf(theme, loc)) + items.reduce((n, i) => n + countEl(i), 0) > MAX_ELEMENTS) return none(R.max);
  const used = usedIds(theme);
  const copies = items.map((i) => withFreshIds({ ...clone(i), x: clampXY((num(i.x) ? i.x : 0) + dx - info.ox), y: clampXY((num(i.y) ? i.y : 0) + dy - info.oy) }, used));
  const next = setVariant(theme, loc, (v) => {
    const els = editContainer(v.elements, target, (l) => l.concat(copies));
    return els ? { ...v, elements: els } : v;
  }, true);
  return next === theme ? none(R.place) : { theme: next, ids: copies.map((c) => c.id) };
}

// ---------- varianti e schermate ----------

const variantsOf = (theme, screen) => {
  if (!isScreen(screen)) return null;
  const s = theme?.screens?.[screen];
  return isObj(s) && Array.isArray(s.variants) ? s.variants : null;
};

const setVariants = (theme, screen, list) => {
  const screens = { ...theme.screens };
  if (list.length) screens[screen] = { ...(isObj(theme.screens[screen]) ? theme.screens[screen] : {}), variants: list };
  else delete screens[screen];
  return { ...theme, screens };
};

export function addVariant(theme, screen, when = 'always') {
  const none = (reason) => ({ theme, index: null, reason });
  if (!isObj(theme) || !isScreen(screen) || !WHENS.includes(when)) return none(R.noop);
  const cur = variantsOf(theme, screen) ?? [];
  if (cur.length >= MAX_VARIANTS) return none('Al massimo 8 varianti per schermata.');
  const base = isObj(theme.screens) ? theme : { ...theme, screens: {} };
  return { theme: setVariants(base, screen, [...cur, { when, elements: [] }]), index: cur.length };
}

export function duplicateVariant(theme, screen, i) {
  const none = (reason) => ({ theme, index: null, reason });
  const cur = variantsOf(theme, screen);
  if (!cur || !isObj(cur[i]) || !isScreen(screen)) return none(R.noop);
  if (cur.length >= MAX_VARIANTS) return none('Al massimo 8 varianti per schermata.');
  const used = usedIds(theme);
  const copy = clone(cur[i]);
  copy.elements = arr(copy.elements).map((e) => (isObj(e) ? withFreshIds(e, used) : e));
  return { theme: setVariants(theme, screen, [...cur.slice(0, i + 1), copy, ...cur.slice(i + 1)]), index: i + 1 };
}

export function removeVariant(theme, screen, i) {
  const cur = variantsOf(theme, screen);
  if (!cur || !isObj(cur[i])) return theme;
  if (cur.length === 1 && Object.keys(theme.screens).length === 1) return theme;  // un tema senza schermate non si salva
  return setVariants(theme, screen, cur.filter((_, k) => k !== i));
}

export function moveVariant(theme, screen, from, to) {
  const cur = variantsOf(theme, screen);
  if (!cur || !Number.isInteger(from) || !Number.isInteger(to) || from === to || !isObj(cur[from]) || to < 0 || to >= cur.length) return theme;
  const list = cur.slice();
  const [v] = list.splice(from, 1);
  list.splice(to, 0, v);
  return setVariants(theme, screen, list);
}

export function updateVariant(theme, screen, i, patch) {
  const cur = variantsOf(theme, screen);
  if (!cur || !isObj(cur[i]) || !isObj(patch)) return theme;
  const v = { ...cur[i] };
  if (patch.when !== undefined && WHENS.includes(patch.when)) v.when = patch.when;
  if ('ref' in patch) {
    if (patch.ref === undefined || patch.ref === 'selected') delete v.ref;
    else if (patch.ref === 'urgent') v.ref = 'urgent';
  }
  if (JSON.stringify(v) === JSON.stringify(cur[i])) return theme;
  return setVariants(theme, screen, cur.map((x, k) => (k === i ? v : x)));
}

// la schermata di serie diventa la schermata del tema (copia, id nuovi); mai le schermate bloccate
export function fromStock(theme, screen, stock) {
  if (!isObj(theme) || !isScreen(screen) || !isObj(stock) || !Array.isArray(stock.variants) || !stock.variants.length
    || stock.variants.length > MAX_VARIANTS || !stock.variants.every(isObj) || stock.variants.some((v) => countVariant(v) > MAX_ELEMENTS)) return theme;
  const used = new Set(allIds({ screens: Object.fromEntries(Object.entries(theme.screens || {}).filter(([k]) => k !== screen)) }));
  const copy = clone(stock);
  copy.variants = copy.variants.map((v) => ({ ...v, elements: arr(v.elements).map((e) => (isObj(e) ? withFreshIds(e, used) : e)) }));
  return { ...theme, screens: { ...(theme.screens || {}), [screen]: copy } };
}

// Perché un'operazione non ha cambiato il tema (stringa per la UI), o null se l'ha cambiato.
// name: nome di una operazione che restituisce un tema; args come per l'operazione.
export function refusal(name, theme, loc, ...rest) {
  const fn = { moveEls, setBox, removeEls, reorder, updateEl }[name];
  if (!fn) return null;
  const out = fn(theme, loc, ...rest);
  if (out !== theme) return null;
  const [a, b] = rest;
  if (name === 'moveEls') {
    const tops = topLevelIds(theme, loc, a);
    if (tops.length && tops.every((id) => { const f = findEl(theme, loc, id); return f && isLockedPath(f); })) return R.locked;
    return R.noop;
  }
  if (name === 'removeEls') {
    const ids = new Set(arr(a));
    let lastRow = false;
    eachEl(variantOf(theme, loc)?.elements, (el) => {
      if (el.type === 'list' && arr(el.row).length && arr(el.row).every((c) => ids.has(c?.id))) lastRow = true;
    });
    return lastRow ? R.lastRow : R.noop;
  }
  if (name === 'reorder') {
    const f = findEl(theme, loc, a);
    if (!f) return R.noop;
    const tgt = b?.parentId ?? null;
    if (lockedTarget(theme, loc, tgt) || f.trail.some((a) => a.locked === true)) return R.locked;
    const why = placeReason(containerInfo(theme, loc, tgt), f.el);
    if (why) return why;
    return f.list === 'row' && arr(f.trail[f.trail.length - 1]?.row).length === 1 ? R.lastRow : R.noop;
  }
  return R.noop;
}
