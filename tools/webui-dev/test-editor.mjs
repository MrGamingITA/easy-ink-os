// Prove dell'editor dei temi (moduli puri in assets/js/editor/). Ogni sezione ha il suo blocco { }.
//   node test-editor.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as Hi from '../../service/internal/webui/static/assets/js/editor/history.js';
import * as O from '../../service/internal/webui/static/assets/js/editor/ops.js';
import * as G from '../../service/internal/webui/static/assets/js/editor/geometry.js';
import * as K from '../../service/internal/webui/static/assets/js/editor/keys.js';
import * as S from '../../service/internal/webui/static/assets/js/editor/select.js';
import * as D from '../../service/internal/webui/static/assets/js/editor/testdata.js';
import * as W from '../../service/internal/webui/static/assets/js/editor/warnings.js';
import { lowerTheme } from '../../service/internal/webui/static/assets/js/theme/lower.js';
import { renderScreen } from '../../service/internal/webui/static/assets/js/theme/render.js';
import { makeModel } from '../../service/internal/webui/static/assets/js/theme/values.js';
import { Bitmap, crc32 } from '../../service/internal/webui/static/assets/js/theme/surface.js';
import { loadWithAnchors } from '../avatar/lib/anchors.mjs';
import { createStore } from '../../service/internal/webui/static/assets/js/editor/store.js';
import { createActions, initialState, saveText, themeChanges, boards, sendText, sendLine } from '../../service/internal/webui/static/assets/js/editor/actions.js';
import { STOCK } from '../../service/internal/webui/static/assets/js/theme/stock.js';
import { createAssets } from '../../service/internal/webui/static/assets/js/editor/assets.js';
import { renderPreview, scaleFor, fitZoom } from '../../service/internal/webui/static/assets/js/editor/view.js';
import { scan, resolveSpec } from './parity-scan.mjs';
import { editorStrings, decisionList, fragments } from './ui-strings.mjs';
import { putTheme, importTheme, deleteTheme, sendTheme } from '../../service/internal/webui/static/assets/js/api.js';
import * as C from '../../service/internal/webui/static/assets/js/editor/catalog.js';
import * as Ly from '../../service/internal/webui/static/assets/js/editor/layers.js';
import * as Ge from '../../service/internal/webui/static/assets/js/editor/gesture.js';
import { VALUE_GLYPHS } from '../../service/internal/webui/static/assets/js/theme/glyphs.js';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

let fail = 0;
const check = (ok, msg) => { if (!ok) { fail++; console.log('FAIL', msg); } };
const eq = (a, b, msg) => check(a === b, msg + ': ' + JSON.stringify(a) + ' invece di ' + JSON.stringify(b));
const same = (a, b, msg) => eq(JSON.stringify(a), JSON.stringify(b), msg);

// aiuti comuni: la resa di un tema come nel golden (tools/golden/theme-golden.mjs)
const fixture = (n) => JSON.parse(fs.readFileSync(path.join(REPO, 'protocol/themes/fixtures', n + '.theme.json'), 'utf8'));
const avs = {}, accs = {};
for (const f of fs.readdirSync(path.join(REPO, 'avatars')).filter((f) => f.endsWith('.avatar.json'))) {
  const { av } = loadWithAnchors(path.join(REPO, 'avatars', f)); avs[av.id] = av;
}
for (const f of fs.readdirSync(path.join(REPO, 'avatars', 'accessori')).filter((f) => f.endsWith('.accessory.json'))) {
  const a = JSON.parse(fs.readFileSync(path.join(REPO, 'avatars', 'accessori', f), 'utf8')); accs[a.id] = a;
}
const assets = { avatar: (id) => avs[id] || null, accessory: (id) => accs[id] || null };
const sc = JSON.parse(fs.readFileSync(path.join(REPO, 'protocol/themes/scenarios.json'), 'utf8'));
const merge = (a, b) => { const o = { ...a }; for (const [k, v] of Object.entries(b)) o[k] = v && typeof v === 'object' && !Array.isArray(v) && a[k] && typeof a[k] === 'object' ? merge(a[k], v) : v; return o; };
// api e timer finti per le azioni (Task 9 e seguenti)
const flush = () => new Promise((r) => setTimeout(r, 0));
function fakeApi(names = ['prova'], { withPut = false } = {}) {
  const files = new Map(names.map((n) => [n, { theme: { ...fixture('prova'), id: n, name: n }, updated: 1000 }]));
  const info = (id) => ({ id, name: files.get(id).theme.name, screens: {}, warnings: [], problems: [], updated: files.get(id).updated });
  const api = {
    calls: [],
    async listThemes() { api.calls.push('list'); if (api.down) throw new Error('servizio non raggiungibile'); return [...files.keys()].sort().map(info); },
    async getTheme(id) { api.calls.push('get ' + id); if (!files.has(id)) throw new Error('tema sconosciuto'); const f = files.get(id); return { theme: JSON.parse(JSON.stringify(f.theme)), base: f.updated }; },
    async createTheme(t) { api.calls.push('create ' + t.id); files.set(t.id, { theme: t, updated: 2000 }); return info(t.id); },
    async checkTheme(t) { api.calls.push('check'); return { id: t.id, screens: {}, warnings: [], problems: [] }; },
    // cambio fatto altrove (telefono): contenuto e updated nuovi
    touch(id, patch) { const f = files.get(id); f.theme = { ...f.theme, ...patch }; f.updated += 1000; },
    file: (id) => files.get(id) ?? null,
    forget: (id) => files.delete(id),
  };
  if (withPut) api.putTheme = async (id, t, { base }) => {
    api.calls.push('put ' + id + ' base=' + base);
    if (api.gate) await api.gate;
    const f = files.get(id);
    if (f.updated !== base) { const e = new Error('il tema è stato cambiato altrove'); e.name = 'ConflictError'; throw e; }
    f.theme = t; f.updated += 7; return info(id);
  };
  return api;
}
// api finta per la barra in alto (Task 13): conta i PUT, controllo e PUT del prossimo giro scelti dalla prova
function autoApi(names = ['altro', 'prova']) {
  const api = fakeApi(names, { withPut: true });
  const put = api.putTheme;
  api.puts = []; api.nextCheck = null; api.nextPut = null; api.checkGate = null;
  api.failPuts = 0;
  api.putTheme = async (id, t, o) => {
    const mode = api.nextPut; api.nextPut = null;
    if (mode === 'network' || api.failPuts > 0) { if (api.failPuts > 0) api.failPuts--; api.calls.push('put ' + id + ' rete giù'); throw new TypeError('Failed to fetch'); }
    if (mode === 'invalid') { api.calls.push('put ' + id + ' 400'); const e = new Error('problemi'); e.name = 'InvalidError'; e.problems = api.putProblems ?? []; throw e; }
    if (!api.file(id)) { api.calls.push('put ' + id + ' 404'); const e = new Error('tema sconosciuto'); e.name = 'NotFoundError'; throw e; }
    // cambiato altrove (telefono) appena prima del nostro PUT
    if (mode === 'conflict') api.touch(id, { name: 'Dal telefono' });
    const r = await put(id, t, o);
    api.puts.push({ id, base: o.base, theme: t, fresh: !!o.fresh });
    return r;
  };
  api.checkTheme = async (t) => {
    api.calls.push('check');
    if (api.checkDown) throw new TypeError('Failed to fetch');
    const extra = api.nextCheck; api.nextCheck = null;
    if (api.checkGate) await api.checkGate;
    return { id: t.id, screens: { home: 1200 }, warnings: [], problems: [], _for: t, ...(extra || {}) };
  };
  api.importTheme = async (text) => {
    api.calls.push('import');
    let t; try { t = JSON.parse(text); } catch { const e = new Error('JSON non valido'); e.name = 'InvalidError'; e.problems = [{ path: '', msg: 'JSON non valido' }]; throw e; }
    let id = t.id;
    for (let k = 2; api.file(id); k++) id = t.id.slice(0, 22) + '-' + k;
    return api.createTheme({ ...t, id });
  };
  api.deleteTheme = async (id) => { api.calls.push('delete ' + id); if (!api.forget(id)) throw new Error('tema sconosciuto'); };
  return api;
}
function fakeTimers() {
  let t = 0, seq = 0; const q = new Map();
  return {
    now: () => t,
    setTimeout: (fn, ms) => { q.set(++seq, { fn, at: t + ms }); return seq; },
    clearTimeout: (id) => { q.delete(id); },
    advance(ms) { const end = t + ms; for (;;) { const due = [...q].filter(([, v]) => v.at <= end).sort((a, b) => a[1].at - b[1].at)[0]; if (!due) break; q.delete(due[0]); t = due[1].at; due[1].fn(); } t = end; },
  };
}

const crcOf = (theme, screen, scenario) => {
  const s = sc.scenarios.find((x) => x.name === scenario);
  const over = s.extends ? merge(sc.scenarios.find((x) => x.name === s.extends), s) : s;
  const b = new Bitmap();
  renderScreen(lowerTheme(theme), screen, makeModel(sc.base, over), b, assets);
  return crc32(b.packed());
};

// 1. cronologia
{
  let h = Hi.createHistory({ a: 1 });
  h = Hi.commit(h, { a: 2 }); h = Hi.commit(h, { a: 3 });
  eq(Hi.undo(h).present.a, 2, 'annulla');
  eq(Hi.redo(Hi.undo(h)).present.a, 3, 'ripeti');
  check(!Hi.canRedo(Hi.commit(Hi.undo(h), { a: 9 })), 'una modifica dopo annulla cancella ripeti');
  let g = Hi.createHistory({ x: 0 });
  for (let i = 1; i <= 20; i++) g = Hi.commit(g, { x: i }, 'move:e1');
  eq(g.past.length, 1, 'un trascinamento = un passo');
  g = Hi.commit(Hi.endGesture(g), { x: 30 }, 'move:e1');
  eq(g.past.length, 2, 'gesto chiuso: passo nuovo');
  eq(Hi.commit(g, { x: 30 }), g, 'niente cambia: nessun passo');
  let big = Hi.createHistory(0); for (let i = 1; i <= 5000; i++) big = Hi.commit(big, i);
  for (let i = 0; i < 5000; i++) big = Hi.undo(big);
  eq(big.present, 0, 'illimitati'); check(!Hi.canUndo(big), 'in fondo');
  eq(Hi.undo(Hi.createHistory(1)).present, 1, 'annulla a vuoto');
  const r = Hi.replace(h, { a: 7 });
  check(!Hi.canUndo(r) && !Hi.canRedo(r) && r.present.a === 7, 'replace: niente passi');
  check(Hi.undo(Hi.commit(Hi.createHistory(1), 2, 'k')).open === null, 'undo chiude il gesto');
  let back = Hi.createHistory({ x: 0 });
  for (const x of [5, 9, 0]) back = Hi.commit(back, { x }, 'move:e1');
  check(!Hi.canUndo(back) && back.present.x === 0, 'trascinamento tornato al punto di partenza: nessun passo');
  back = Hi.commit(back, { x: 4 }, 'move:e1');
  eq(back.past.length, 1, 'dopo il ritorno, un nuovo movimento è un passo');
}

// 2. operazioni sul tema
{
  const rawLista = fixture('lista');
  const base = O.ensureIds(rawLista);
  const L = { screen: 'home', variant: 0 };
  const els = base.screens.home.variants[0].elements;
  const [firstId, listId, clockId] = els.map((e) => e.id);
  const idA = firstId, idB = clockId;
  const ids = O.allIds;
  eq(new Set(ids(base)).size, ids(base).length, 'ensureIds: tutti unici');
  eq(ids(base).length, 3 + 6 + 2, 'ensureIds: anche le righe della lista e le altre schermate');
  const dup = JSON.parse(JSON.stringify(rawLista));
  dup.screens.home.variants[0].elements[0].id = 'e3'; dup.screens.home.variants[0].elements[1].id = 'e3';
  const fixed = O.ensureIds(dup);
  eq(new Set(ids(fixed)).size, ids(fixed).length, 'ensureIds: doppi');
  eq(fixed.screens.home.variants[0].elements[0].id, 'e3', 'ensureIds: il primo tiene l\'id');
  eq(O.ensureIds(base), base, 'ensureIds: già a posto, stesso oggetto');
  eq(crcOf(base, 'home', 'sei'), crcOf(rawLista, 'home', 'sei'), 'ensureIds non cambia la resa');

  // schermate bloccate e fuori elenco
  for (const s of O.LOCKED_SCREENS) {
    eq(O.addEl(base, { screen: s, variant: 0 }, O.elementDefaults('text', 0, 0)).id, null, 'addEl rifiuta ' + s);
    check(!(s in O.fromStock(base, s, { variants: [{ when: 'always', elements: [] }] }).screens), 'fromStock rifiuta ' + s);
  }
  check(!('pippo' in O.fromStock(base, 'pippo', { variants: [{ when: 'always', elements: [] }] }).screens), 'fromStock: schermata sconosciuta');

  // aggiunta
  const a1 = O.addEl(base, L, O.elementDefaults('rect', 5, 6));
  check(a1.id && !ids(base).includes(a1.id), 'addEl: id nuovo');
  eq(O.findEl(a1.theme, L, a1.id).index, 3, 'addEl in cima');
  eq(O.addEl(base, L, O.elementDefaults('rect', 0, 0), { index: 0 }).theme.screens.home.variants[0].elements[0].type, 'rect', 'addEl all\'indice');
  const fresh = O.addEl({ ...base, screens: {} }, { screen: 'oggi', variant: 0 }, O.elementDefaults('text', 1, 2));
  eq(fresh.theme.screens.oggi.variants[0].when, 'always', 'addEl crea schermata e variante');
  eq(O.addEl(base, { screen: 'home', variant: 3 }, O.elementDefaults('text', 0, 0)).id, null, 'addEl: variante lontana');
  eq(O.addEl(base, L, O.elementDefaults('list', 0, 0), { parentId: listId }).id, null, 'lista dentro una riga');
  eq(O.addEl(base, L, { type: 'boh' }).id, null, 'tipo sconosciuto');
  eq(O.addEl(base, L, O.elementDefaults('text', 0, 0), { parentId: 'nonesiste' }).id, null, 'contenitore mancante');
  check(O.addEl(base, L, O.elementDefaults('text', 0, 0), { parentId: listId }).id !== null, 'testo nella riga');
  eq(O.elementDefaults('boh', 0, 0), null, 'predefinito sconosciuto');

  // modifica
  eq(O.findEl(O.updateEl(base, L, firstId, { text: 'x' }), L, firstId).el.text, 'x', 'updateEl');
  check(!('align' in O.findEl(O.updateEl(O.updateEl(base, L, firstId, { align: 'right' }), L, firstId, { align: undefined }), L, firstId).el), 'updateEl: undefined toglie');
  eq(O.findEl(O.updateEl(base, L, firstId, { id: 'zz', type: 'rect' }), L, firstId).el.type, 'text', 'updateEl: id e tipo non si cambiano');
  eq(O.updateEl(base, L, 'nonesiste', { x: 1 }), base, 'updateEl: id sconosciuto');

  // blocchi e spostamenti
  const locked = O.updateEl(base, L, firstId, { locked: true });
  eq(O.findEl(O.moveEls(locked, L, [firstId], 5, 0), L, firstId).el.x, O.findEl(locked, L, firstId).el.x, 'moveEls salta i bloccati');
  eq(O.moveEls(base, L, [firstId], 0, 0), base, 'moveEls: zero');
  eq(O.findEl(O.moveEls(base, L, [firstId], 3, -2), L, firstId).el.y, O.findEl(base, L, firstId).el.y - 2, 'moveEls');
  const gg = O.groupEls(base, L, [idA, idB]);
  const lockedG = O.updateEl(gg.theme, L, gg.id, { locked: true });
  const kid = O.findEl(lockedG, L, idA).el.x;
  eq(O.findEl(O.moveEls(lockedG, L, [idA], 9, 0), L, idA).el.x, kid, 'moveEls: antenato bloccato');
  const both = O.moveEls(gg.theme, L, [gg.id, idA], 10, 0);
  eq(O.findEl(both, L, idA).el.x, O.findEl(gg.theme, L, idA).el.x, 'moveEls: figlio con antenato in ids non si sposta due volte');
  eq(O.findEl(both, L, gg.id).el.x, O.findEl(gg.theme, L, gg.id).el.x + 10, 'moveEls: il gruppo si sposta');

  // gruppi: posizioni assolute invariate, ungroup ritorna allo stesso disegno
  eq(crcOf(gg.theme, 'home', 'sei'), crcOf(base, 'home', 'sei'), 'raggruppa: stessa resa');
  const ug = O.ungroup(gg.theme, L, gg.id);
  eq(crcOf(ug.theme, 'home', 'sei'), crcOf(base, 'home', 'sei'), 'separa: stessa resa');
  same(ug.ids.slice().sort(), [idA, idB].sort(), 'separa: id dei figli');
  const grp = O.findEl(gg.theme, L, gg.id);
  eq(grp.el.x, Math.min(O.findEl(base, L, idA).el.x, O.findEl(base, L, idB).el.x), 'gruppo: angolo in alto a sinistra');
  eq(grp.index, 1, 'gruppo al posto del più in alto (resta la lista, poi il gruppo)');
  eq(O.groupEls(base, L, [idA, 'x']).id, null, 'groupEls: id sconosciuto');
  const inList = O.findEl(base, L, listId).el.row.map((r) => r.id);
  eq(O.groupEls(base, L, [idA, inList[0]]).id, null, 'groupEls: contenitori diversi');
  const gRow = O.groupEls(base, L, [inList[0], inList[1]]);
  check(gRow.id !== null, 'groupEls dentro una riga');
  eq(O.findEl(gRow.theme, L, gRow.id).list, 'row', 'il gruppo sta nella riga');
  // visibleIf del gruppo sui figli, al massimo 8
  const withIf = O.updateEl(gg.theme, L, gg.id, { visibleIf: [{ state: 'permission' }] });
  eq(O.findEl(O.ungroup(withIf, L, gg.id).theme, L, idA).el.visibleIf.length, 1, 'separa: visibleIf del gruppo');
  const many = O.updateEl(O.updateEl(withIf, L, idA, { visibleIf: Array(8).fill({ online: true }) }), L, gg.id, { visibleIf: [{ state: 'permission' }] });
  eq(O.ungroup(many, L, gg.id).ids, null, 'separa: oltre 8 condizioni, invariato');
  eq(O.ungroup(many, L, gg.id).theme, many, 'separa: tema invariato');
  const nested4 = (t) => {
    const a = O.addEl(t, L, O.elementDefaults('rect', 0, 0));
    let cur = a.theme, id = a.id;
    for (let i = 0; i < 4; i++) { const g = O.groupEls(cur, L, [id]); cur = g.theme; id = g.id; }
    return { theme: cur, id };
  };
  const n4 = nested4(base);
  check(n4.id !== null, 'quattro livelli di gruppi');
  eq(O.groupEls(n4.theme, L, [n4.id]).id, null, 'groupEls: oltre il quinto');
  // gruppo dentro quattro livelli: il quinto non si aggiunge
  let deepest = n4.theme, deepId = n4.id;
  for (let i = 0; i < 3; i++) { deepId = O.findEl(deepest, L, deepId).el.children[0].id; }
  eq(O.findEl(deepest, L, deepId).depth, 3, 'profondità del gruppo più interno');
  const leaf = O.findEl(deepest, L, deepId).el.children[0].id;
  eq(O.groupEls(deepest, L, [leaf]).id, null, 'groupEls oltre 4 livelli');
  eq(O.addEl(deepest, L, { type: 'group', x: 0, y: 0, children: [O.elementDefaults('rect', 0, 0)] }, { parentId: deepId }).id, null, 'addEl: gruppo oltre 4 livelli');

  // rimozione: un gruppo rimasto vuoto sparisce, una riga non resta vuota
  const rm = O.removeEls(gg.theme, L, [idA, idB]);
  check(!ids(rm).includes(gg.id), 'removeEls: gruppo svuotato sparisce');
  eq(O.findEl(O.removeEls(base, L, inList), L, listId).el.row.length, 6, 'removeEls: la riga non si svuota');
  eq(O.findEl(O.removeEls(base, L, [inList[0]]), L, listId).el.row.length, 5, 'removeEls: un elemento della riga');
  check(!ids(O.removeEls(base, L, [listId])).includes(inList[0]), 'removeEls: lista con le sue righe');

  // riordino
  const r1 = O.reorder(base, L, idB, { parentId: null, index: 0 });
  eq(r1.screens.home.variants[0].elements[0].id, idB, 'reorder: indice');
  const r2 = O.reorder(base, L, idA, { parentId: listId, index: 0 });
  const mv = O.findEl(r2, L, idA);
  eq(mv.list, 'row', 'reorder: dentro la riga');
  eq(mv.ox + mv.el.x, O.findEl(base, L, idA).el.x, 'reorder: x assoluta resta');
  eq(mv.oy + mv.el.y, O.findEl(base, L, idA).el.y, 'reorder: y assoluta resta');
  const r3 = O.reorder(gg.theme, L, idA, { parentId: null, index: 0 });
  const mv3 = O.findEl(r3, L, idA);
  eq(mv3.el.x, O.findEl(gg.theme, L, idA).ox + O.findEl(gg.theme, L, idA).el.x, 'reorder: fuori dal gruppo');
  eq(O.reorder(gg.theme, L, gg.id, { parentId: gg.id }), gg.theme, 'reorder: dentro sé stesso');
  eq(O.reorder(base, L, listId, { parentId: null, index: 0 }).screens.home.variants[0].elements[0].id, listId, 'reorder: lista in cima');
  check(!ids(O.reorder(O.removeEls(gg.theme, L, [idB]), L, idA, { parentId: null })).includes(gg.id), 'reorder: gruppo svuotato sparisce');
  eq(O.findEl(O.reorder(O.removeEls(base, L, inList.slice(1)), L, inList[0], { parentId: null }), L, listId).el.row.length, 1, 'reorder: l\'ultima riga non esce');

  // misure
  const mAdd = O.addEl(base, L, O.elementDefaults('mascot', 0, 0));
  const mId = mAdd.id, withMascot = mAdd.theme;
  eq(O.findEl(O.setBox(withMascot, L, mId, { x: 0, y: 0, w: 60, h: 50 }), L, mId).el.scale, 2, 'setBox: scala della mascotte (60 / 28 → 2)');
  eq(O.findEl(O.setBox(withMascot, L, mId, { x: 0, y: 0, w: 500, h: 50 }), L, mId).el.scale, 4, 'setBox: scala al massimo');
  const bAdd = O.addEl(base, L, O.elementDefaults('battery', 0, 0));
  eq(O.findEl(O.setBox(bAdd.theme, L, bAdd.id, { x: 0, y: 0, w: 500, h: 50 }), L, bAdd.id).el.scale, 2, 'setBox: batteria al massimo 2');
  const tBox = O.findEl(O.setBox(base, L, firstId, { x: 10, y: 11, w: 100, h: 28 }), L, firstId).el;
  check(tBox.x === 10 && tBox.y === 11 && tBox.w === 100 && tBox.lines === 2, 'setBox: testo (28 / 14 → 2 righe)');
  eq(O.findEl(O.setBox(base, L, firstId, { x: 0, y: 0, w: 100, h: 900 }), L, firstId).el.lines, 8, 'setBox: righe al massimo 8');
  const rb = O.findEl(O.setBox(withMascot, L, mId, { x: 7, y: 8, w: 28, h: 25 }), L, mId).el;
  check(rb.x === 7 && rb.y === 8, 'setBox: posizione');
  const gb = O.setBox(gg.theme, L, gg.id, { x: 50, y: 60, w: 1, h: 1 });
  const gbEl = O.findEl(gb, L, gg.id).el;
  check(gbEl.x === 50 && gbEl.y === 60 && !('w' in gbEl), 'setBox: il gruppo si sposta e basta');
  const rowBox = O.setBox(base, L, inList[1], { x: 20, y: 30, w: 50, h: 12 });
  const rbEl = O.findEl(rowBox, L, inList[1]);
  check(rbEl.el.x === 20 - rbEl.ox && rbEl.el.y === 30 - rbEl.oy, 'setBox: nella riga relativo alla lista');

  // duplica, copia, incolla
  const d = O.duplicateEls(base, L, [idA]);
  eq(O.findEl(d.theme, L, d.ids[0]).el.x, O.findEl(base, L, idA).el.x + 8, 'duplica +8 in x');
  eq(O.findEl(d.theme, L, d.ids[0]).el.y, O.findEl(base, L, idA).el.y + 8, 'duplica +8 in y');
  eq(O.findEl(d.theme, L, d.ids[0]).index, O.findEl(base, L, idA).index + 1, 'duplica subito sopra');
  const dg = O.duplicateEls(gg.theme, L, [gg.id, idA]);
  eq(dg.ids.length, 1, 'duplica: figlio con antenato in ids una volta sola');
  eq(new Set(ids(dg.theme)).size, ids(dg.theme).length, 'duplica: id tutti unici');
  eq(O.duplicateEls(base, L, ['x']).ids, null, 'duplica: id sconosciuto');
  const clip = O.copyEls(base, L, [inList[0]]);
  const lst = O.findEl(base, L, listId).el;
  eq(clip.items[0].x, lst.x + O.findEl(base, L, inList[0]).el.x, 'copia: x assoluta');
  eq(clip.items[0].y, lst.y + O.findEl(base, L, inList[0]).el.y, 'copia: y assoluta');
  const p = O.pasteEls(base, L, clip);
  eq(O.findEl(p.theme, L, p.ids[0]).el.x, clip.items[0].x + 8, 'incolla +8');
  eq(O.findEl(p.theme, L, p.ids[0]).index, 3, 'incolla in cima');
  const pRow = O.pasteEls(base, L, clip, { dx: 0, dy: 0, parentId: listId });
  eq(O.findEl(pRow.theme, L, pRow.ids[0]).el.x, O.findEl(base, L, inList[0]).el.x, 'incolla nella riga: relativo alla lista');
  eq(O.pasteEls(base, L, O.copyEls(base, L, [listId]), { parentId: listId }).ids, null, 'incolla: lista in una riga');
  eq(O.pasteEls(base, L, { items: [] }).ids, null, 'incolla: appunti vuoti');
  eq(O.pasteEls(base, L, null).ids, null, 'incolla: appunti nulli');
  eq(O.pasteEls(base, { screen: 'perm', variant: 0 }, clip).ids, null, 'incolla: perm');
  same(JSON.parse(JSON.stringify(clip)), clip, 'appunti: solo JSON');

  // varianti
  const av = O.addVariant(base, 'home', 'none');
  eq(av.index, 1, 'addVariant');
  eq(av.theme.screens.home.variants[1].when, 'none', 'addVariant: when');
  eq(O.addVariant(base, 'perm', 'none').index, null, 'addVariant: perm');
  eq(O.addVariant(base, 'home', 'boh').index, null, 'addVariant: when sconosciuto');
  let full = base; for (let i = 0; i < 7; i++) full = O.addVariant(full, 'home', 'one').theme;
  eq(O.addVariant(full, 'home', 'one').index, null, 'addVariant: al massimo 8');
  eq(O.addVariant({ ...base, screens: {} }, 'pet', 'always').index, 0, 'addVariant: schermata nuova');
  const dv = O.duplicateVariant(base, 'home', 0);
  eq(dv.index, 1, 'duplicateVariant');
  eq(new Set(ids(dv.theme)).size, ids(dv.theme).length, 'duplicateVariant: id nuovi');
  same(dv.theme.screens.home.variants[1].elements.map((e) => e.type), els.map((e) => e.type), 'duplicateVariant: stessi elementi');
  eq(O.removeVariant(dv.theme, 'home', 1).screens.home.variants.length, 1, 'removeVariant');
  const single = { ...base, screens: { home: base.screens.home } };
  eq(O.removeVariant(single, 'home', 0), single, 'removeVariant: ultima variante dell\'unica schermata');
  check(!('detail' in O.removeVariant(base, 'detail', 0).screens) && 'home' in O.removeVariant(base, 'detail', 0).screens, 'removeVariant: ultima variante toglie la schermata');
  eq(O.removeVariant(base, 'home', 5), base, 'removeVariant: indice fuori');
  eq(O.moveVariant(dv.theme, 'home', 1, 0).screens.home.variants[0], dv.theme.screens.home.variants[1], 'moveVariant');
  eq(O.moveVariant(dv.theme, 'home', 1, 9), dv.theme, 'moveVariant: fuori');
  const uv = O.updateVariant(base, 'home', 0, { when: 'many', ref: undefined });
  check(uv.screens.home.variants[0].when === 'many' && !('ref' in uv.screens.home.variants[0]), 'updateVariant');
  eq(O.updateVariant(base, 'home', 0, { when: 'boh' }), base, 'updateVariant: when non valido');
  eq(O.updateVariant(base, 'home', 0, { ref: 'urgent' }), base, 'updateVariant: nessun cambiamento');

  // schermata di serie
  const stock = { variants: [{ when: 'always', elements: [{ type: 'rect', id: 'e1', x: 0, y: 0, w: 5, h: 5 }, { type: 'rect', id: 'e1', x: 1, y: 1, w: 5, h: 5 }] }] };
  const fs1 = O.fromStock(base, 'pet', stock);
  eq(fs1.screens.pet.variants[0].elements.length, 2, 'fromStock: copia');
  eq(new Set(ids(fs1)).size, ids(fs1).length, 'fromStock: id nuovi');
  stock.variants[0].elements[0].x = 99;
  eq(fs1.screens.pet.variants[0].elements[0].x, 0, 'fromStock: copia, non riferimento');
  const fsHome = O.fromStock(base, 'home', stock);
  eq(new Set(ids(fsHome)).size, ids(fsHome).length, 'fromStock sulla schermata esistente: id unici');
  eq(O.fromStock(base, 'pet', { variants: [] }), base, 'fromStock: senza varianti');

  // nomi
  eq(O.kebabId('Città Notte!', ['citta-notte']), 'citta-notte-2', 'kebabId');
  eq(O.kebabId('Città Notte!', ['citta-notte', 'citta-notte-2']), 'citta-notte-3', 'kebabId: terzo');
  eq(O.kebabId('', []), 'tema', 'kebabId: vuoto');
  eq(O.kebabId('!!!', []), 'tema', 'kebabId: solo simboli');
  eq(O.kebabId('Calamaio', []), 'calamaio-2', 'kebabId: mai calamaio');
  eq(O.kebabId('a'.repeat(40), []), 'a'.repeat(24), 'kebabId: al massimo 24');
  eq(O.kebabId('a'.repeat(24), ['a'.repeat(24)]), 'a'.repeat(22) + '-2', 'kebabId: suffisso dentro i 24');
  eq(O.kebabId('--ciao---mondo--', []), 'ciao-mondo', 'kebabId: trattini');
  const nt = O.newTheme('Città Notte!', ['citta-notte']);
  same([nt.id, nt.name, nt.format, nt.version, nt.screen], ['citta-notte-2', 'Città Notte!', 'calamaio-theme', 1, '250x122'], 'newTheme');
  same(nt.screens, { home: { variants: [{ when: 'always', elements: [] }] } }, 'newTheme: schermata home vuota');
  check(new TextEncoder().encode(O.newTheme('è'.repeat(30), []).name).length <= 24, 'newTheme: nome al massimo 24 byte');
  eq(O.renameTheme(base, 'Nuovo').name, 'Nuovo', 'renameTheme');
  eq(O.renameTheme(base, 'Nuovo').id, base.id, 'renameTheme: l\'id non cambia');
  check(new TextEncoder().encode(O.renameTheme(base, 'è'.repeat(30)).name).length <= 24, 'renameTheme: al massimo 24 byte');
  eq(O.renameTheme(base, '   '), base, 'renameTheme: vuoto');

  // nessuna funzione lancia con argomenti sbagliati
  for (const [n, f] of Object.entries(O)) {
    if (typeof f !== 'function') continue;
    for (const bad of [[], [null], [undefined, undefined, undefined], [{}, null, 'x', 5], [base, { screen: 'home', variant: 9 }, [1], {}], [base, null, null, null]]) {
      try { f(...bad); } catch (e) { check(false, n + ' lancia con ' + JSON.stringify(bad) + ': ' + e.message); }
    }
  }

  // i predefiniti: il file comune è quello che il servizio compila (service/internal/theme/editor_defaults_test.go)
  const defaults = Object.fromEntries(['text', 'clock', 'mascot', 'battery', 'wifi', 'stateIcon', 'sym', 'bar', 'qr', 'line', 'rect', 'circle', 'list']
    .map((t) => [t, O.elementDefaults(t, 10, 10)]));
  const outFile = path.join(REPO, 'protocol/themes/editor-defaults.json');
  const wanted = JSON.stringify({ comment: 'Generato da tools/webui-dev/test-editor.mjs --write: i predefiniti dell\'editor (ops.js). Il servizio li compila: service/internal/theme/editor_defaults_test.go.',
    newTheme: O.newTheme('Prova', []), defaults }, null, 2) + '\n';
  if (process.argv.includes('--write')) fs.writeFileSync(outFile, wanted);
  eq(fs.readFileSync(outFile, 'utf8'), wanted, 'editor-defaults.json aggiornato (node test-editor.mjs --write)');
}

// 3. correzioni del giro 1: righe, schermate bloccate, gruppi, limiti
{
  const base = O.ensureIds(fixture('lista'));
  const L = { screen: 'home', variant: 0 };
  const [textId, listId, clockId] = base.screens.home.variants[0].elements.map((e) => e.id);
  const row = O.findEl(base, L, listId).el.row;
  const rowText = row[1].id;  // {riga.nome}

  // I1: un elemento di riga non esce dalla lista
  eq(O.reorder(base, L, rowText, { parentId: null }), base, 'riga: reorder fuori dalla lista rifiutato');
  check(/lista/.test(O.refusal('reorder', base, L, rowText, { parentId: null }) || ''), 'riga: motivo del rifiuto');
  const gRow = O.groupEls(base, L, [rowText, row[2].id]);
  eq(O.reorder(gRow.theme, L, gRow.id, { parentId: null }), gRow.theme, 'riga: gruppo con {riga.} non esce');
  const clip = O.copyEls(base, L, [rowText]);
  const pOut = O.pasteEls(base, L, clip);
  check(pOut.ids === null && /lista/.test(pOut.reason), 'riga: incolla fuori dalla lista rifiutato, con motivo');
  check(O.pasteEls(base, L, clip, { parentId: listId }).ids !== null, 'riga: incolla dentro la lista');
  eq(O.addEl(base, L, { type: 'text', x: 0, y: 0, text: '{riga.da}', font: { family: 'ui', size: 12, weight: 600 } }).id, null, 'riga: addEl fuori dalla lista');
  check(O.reorder(base, L, row[0].id, { parentId: null }) !== base, 'stateIcon senza {riga.} può uscire');
  eq(O.reorder(base, L, listId, { parentId: null, index: 0 }).screens.home.variants[0].elements[0].id, listId, 'la lista con le sue righe si sposta');

  // I2: nessuna operazione tocca Permessi (né altre schermate bloccate)
  const withPerm = JSON.parse(JSON.stringify(base));
  withPerm.screens.perm = { variants: [{ when: 'always', elements: [{ type: 'rect', id: 'p1', x: 1, y: 1, w: 5, h: 5 },
    { type: 'group', id: 'p2', x: 0, y: 0, children: [{ type: 'rect', id: 'p3', x: 1, y: 1, w: 5, h: 5 }] }] }, { when: 'none', elements: [] }] };
  const P = { screen: 'perm', variant: 0 };
  const before = JSON.stringify(withPerm);
  const t = withPerm, el = O.elementDefaults('rect', 0, 0);
  const results = {
    addEl: O.addEl(t, P, el).theme, updateEl: O.updateEl(t, P, 'p1', { x: 9 }), moveEls: O.moveEls(t, P, ['p1'], 3, 3),
    setBox: O.setBox(t, P, 'p1', { x: 9, y: 9, w: 9, h: 9 }), removeEls: O.removeEls(t, P, ['p1']),
    reorder: O.reorder(t, P, 'p1', { parentId: 'p2' }), groupEls: O.groupEls(t, P, ['p1', 'p2']).theme, ungroup: O.ungroup(t, P, 'p2').theme,
    duplicateEls: O.duplicateEls(t, P, ['p1']).theme, pasteEls: O.pasteEls(t, P, { items: [el] }).theme,
    addVariant: O.addVariant(t, 'perm', 'one').theme, duplicateVariant: O.duplicateVariant(t, 'perm', 0).theme,
    removeVariant: O.removeVariant(t, 'perm', 0), moveVariant: O.moveVariant(t, 'perm', 0, 1), updateVariant: O.updateVariant(t, 'perm', 0, { when: 'many' }),
    fromStock: O.fromStock(t, 'perm', { variants: [{ when: 'always', elements: [] }] }),
  };
  for (const [n, r] of Object.entries(results)) eq(JSON.stringify(r), before, 'perm intatta: ' + n);
  eq(O.findEl(t, P, 'p1'), null, 'findEl non vede Permessi');
  eq(JSON.stringify(O.copyEls(t, P, ['p1'])), '{"items":[]}', 'copyEls non legge Permessi');
  const exported = Object.keys(O).filter((k) => typeof O[k] === 'function');
  const covered = new Set([...Object.keys(results), 'findEl', 'copyEls', 'ensureIds', 'newTheme', 'kebabId', 'renameTheme', 'elementDefaults', 'allIds', 'refusal']);
  check(exported.every((k) => covered.has(k)), 'ogni operazione esportata è nella prova di Permessi: ' + exported.filter((k) => !covered.has(k)));

  // I4: setBox su un gruppo con un figlio a coordinate negative
  const g0 = O.groupEls(base, L, [textId, clockId]);
  const g1 = O.updateEl(g0.theme, L, textId, { x: -5 });
  const gEl = () => O.findEl(g1, L, g0.id).el;
  const unionLeft = gEl().x + Math.min(...gEl().children.map((c) => c.x));
  const g2 = O.setBox(g1, L, g0.id, { x: unionLeft, y: 20, w: 1, h: 1 });
  eq(O.findEl(g2, L, g0.id).el.x, gEl().x, 'setBox gruppo: stesso riquadro, nessuno spostamento');
  const g3 = O.setBox(g1, L, g0.id, { x: unionLeft + 10, y: 20, w: 1, h: 1 });
  eq(O.findEl(g3, L, g0.id).el.x, gEl().x + 10, 'setBox gruppo: sposta dell\'unione (figlio negativo)');

  // altri limiti
  eq(JSON.stringify(O.findEl(O.updateEl(g0.theme, L, g0.id, { children: [], row: [], x: 3.6 }), L, g0.id).el.children.length), '2', 'updateEl: children e row protetti');
  eq(O.findEl(O.updateEl(base, L, textId, { x: 3.6 }), L, textId).el.x, 4, 'updateEl: x intero');
  eq(O.findEl(O.moveEls(base, L, [textId], 9999, -9999), L, textId).el.x, 512, 'moveEls: al massimo +512');
  eq(O.findEl(O.moveEls(base, L, [textId], 9999, -9999), L, textId).el.y, -512, 'moveEls: al massimo -512');
  eq(O.findEl(O.setBox(base, L, textId, { x: 9999, y: -9999, w: 10, h: 10 }), L, textId).el.x, 512, 'setBox: al massimo 512');
  let full = base;
  for (let i = 0; full.screens.home.variants[0].elements.length < 100; i++) {
    const a = O.addEl(full, L, O.elementDefaults('rect', i, 0));
    if (a.id === null) { eq(a.reason, 'Al massimo 64 elementi per variante.', 'limite 64: motivo'); break; }
    full = a.theme;
    check(i < 80, 'limite 64 mai raggiunto');
  }
  const countAll = (t) => { let n = 0; for (const e of t.screens.home.variants[0].elements) n += 1 + (e.row ? e.row.length : 0); return n; };
  eq(countAll(full), 64, 'limite 64: si ferma a 64');
  eq(O.duplicateEls(full, L, [textId]).ids, null, 'limite 64: duplica');
  eq(O.pasteEls(full, L, O.copyEls(full, L, [textId])).ids, null, 'limite 64: incolla');

  // motivi dei no-op
  const lockedT = O.updateEl(base, L, textId, { locked: true });
  eq(O.refusal('moveEls', lockedT, L, [textId], 5, 5), 'L\'elemento è bloccato.', 'motivo: bloccato');
  eq(O.refusal('moveEls', base, L, [textId], 5, 5), null, 'motivo: nessuno se cambia');
  const lastRowT = O.removeEls(base, L, row.slice(1).map((r) => r.id));
  eq(O.refusal('removeEls', lastRowT, L, [row[0].id]), 'La riga di una lista deve avere almeno un elemento.', 'motivo: ultima riga');
  eq(O.groupEls(base, L, [textId, rowText]).reason.length > 0, true, 'groupEls: motivo');
  check(O.ungroup(base, L, textId).reason.length > 0, 'ungroup: motivo');
}

// 4. prova larga: sequenze casuali (seme fisso) di operazioni; ogni tema ottenuto deve compilare nel servizio
//    (service/internal/theme/editor_fuzz_test.go le genera con node e le compila)
{
  let seed = 20261017;
  const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
  const pick = (a) => a[Math.floor(rnd() * a.length)];
  const fx = ['lista', 'elementi', 'prova'].map((n) => O.ensureIds(fixture(n)));
  const types = ['text', 'clock', 'mascot', 'battery', 'wifi', 'stateIcon', 'sym', 'bar', 'qr', 'line', 'rect', 'circle', 'list'];
  const out = [];
  for (let run = 0; run < 200; run++) {
    let t = pick(fx);
    const scr = Object.keys(t.screens).concat(['home', 'oggi']);
    for (let s = 0; s < 20; s++) {
      const sc = pick(scr), v = Math.floor(rnd() * 2), L = { screen: sc, variant: v };
      const all = O.allIds(t);
      const some = () => Array.from({ length: 1 + Math.floor(rnd() * 3) }, () => pick(all)).filter(Boolean);
      const op = Math.floor(rnd() * 16);
      let r = t;
      try {
        if (op === 0) r = O.addEl(t, L, O.elementDefaults(pick(types), 5, 5), { parentId: rnd() < 0.4 ? (pick(all) ?? null) : null }).theme;
        else if (op === 1) r = O.removeEls(t, L, some());
        else if (op === 2) r = O.groupEls(t, L, some()).theme;
        else if (op === 3) r = O.ungroup(t, L, pick(all)).theme;
        else if (op === 4) r = O.duplicateEls(t, L, some()).theme;
        else if (op === 5) r = O.reorder(t, L, pick(all), { parentId: rnd() < 0.5 ? pick(all) : null, index: Math.floor(rnd() * 4) });
        else if (op === 6) r = O.setBox(t, L, pick(all), { x: Math.floor(rnd() * 300) - 20, y: Math.floor(rnd() * 150), w: Math.floor(rnd() * 100), h: Math.floor(rnd() * 60) });
        else if (op === 7) r = O.moveEls(t, L, some(), Math.floor(rnd() * 40) - 20, Math.floor(rnd() * 40) - 20);
        else if (op === 8) r = O.pasteEls(t, L, O.copyEls(t, L, some()), { parentId: rnd() < 0.3 ? pick(all) : null }).theme;
        else if (op === 9) r = O.addVariant(t, sc, pick(['always', 'none', 'one', 'many'])).theme;
        else if (op === 10) r = O.removeVariant(t, sc, v);
        else if (op === 11) r = O.duplicateVariant(t, sc, v).theme;
        else if (op === 12) r = O.moveVariant(t, sc, 0, 1);
        else if (op === 13) r = O.updateEl(t, L, pick(all), { visibleIf: Array.from({ length: Math.floor(rnd() * 4) }, () => ({ state: 'permission' })) });
        else if (op === 14) r = O.updateEl(t, L, pick(all), { hidden: rnd() < 0.5 });
        else r = O.addEl(t, L, { type: 'group', x: 1, y: 1, children: [O.elementDefaults('rect', 0, 0)] }).theme;
      } catch (e) { check(false, 'fuzz: eccezione in op ' + op + ': ' + e.message); }
      const ii = O.allIds(r);
      check(new Set(ii).size === ii.length, 'fuzz: id doppi dopo op ' + op);
      t = r;
    }
    out.push(t);
  }
  const text = '[\n' + out.map((t) => JSON.stringify(t)).join(',\n') + '\n]\n';
  // il file non è nel repo: lo scrive il test Go (node test-editor.mjs --fuzz-out <file>)
  const i = process.argv.indexOf('--fuzz-out');
  if (i > 0 && process.argv[i + 1]) fs.writeFileSync(process.argv[i + 1], text);
  check(out.length === 200, 'fuzz: 200 sequenze');
}

// 5. riquadri, colpo del puntatore, selezione e maniglie
{
  const L = { screen: 'home', variant: 0 };
  const mk = (elements) => ({ screens: { home: { variants: [{ when: 'always', elements }] } } });
  const rect = (id, x, y, w, h, extra = {}) => ({ id, type: 'rect', x, y, w, h, ...extra });
  const deepEl = rect('deep', 3, 4, 10, 10);
  const th = mk([
    rect('a', 10, 10, 30, 20),
    { id: 'g1', type: 'group', x: 100, y: 20, children: [
      rect('inner', 5, 5, 20, 20),
      { id: 'g2', type: 'group', x: 40, y: 0, children: [deepEl] },
      rect('hid', 200, 200, 5, 5, { hidden: true }),
    ] },
    { id: 'lst', type: 'list', x: 10, y: 50, w: 100, h: 40, rowH: 20, gap: 2,
      row: [{ id: 'row1', type: 'text', x: 2, y: 6, w: 50, text: '{riga.nome}', font: { family: 'ui', size: 12, weight: 600 } }] },
    { id: 'm', type: 'mascot', x: 160, y: 5, scale: 2 },
    { id: 'tx', type: 'text', x: 0, y: 90, w: 80, lines: 2, text: 'x', font: { family: 'ui', size: 10, weight: 500 } },
    { id: 'ck', type: 'clock', x: 0, y: 0, w: 40, font: { family: 'ui', size: 12, weight: 700 } },
    { id: 'tiny', type: 'rect', x: 200, y: 100, w: 2, h: 2 },
    rect('lk', 220, 10, 20, 20, { locked: true }),
    { id: 'gl', type: 'group', x: 0, y: 70, locked: true, children: [rect('lc', 0, 0, 10, 10)] },
    { id: 'ge', type: 'group', x: 150, y: 80, children: [] },
  ]);
  const bx = S.layout(th, L), by = (id) => bx.find((b) => b.id === id);
  check(bx.every((b) => Number.isInteger(b.x + b.y + b.w + b.h)), 'riquadri interi');
  eq(by('row1').y, by('lst').y + 6, 'layout: righe della lista');
  eq(by('row1').path, 'screens.home.variants[0].elements[2].row[0]', 'layout: percorso di una riga');
  eq(by('deep').x, 100 + 40 + 3, 'layout: gruppi annidati sommano le posizioni');
  eq(by('deep').path, 'screens.home.variants[0].elements[1].children[1].children[0]', 'layout: percorso annidato');
  eq(by('deep').depth, 2, 'layout: profondità');
  same([by('g1').x, by('g1').y, by('g1').w, by('g1').h], [105, 24, 48, 21], 'layout: il gruppo è l\'unione dei figli non nascosti');
  same([by('ge').x, by('ge').y, by('ge').w, by('ge').h], [150, 80, 0, 0], 'layout: gruppo vuoto 0 × 0');
  same([by('m').w, by('m').h], [56, 50], 'mascotte x2');
  same([by('tx').w, by('tx').h], [80, 24], 'testo a 2 righe');
  same([by('ck').w, by('ck').h], [40, 14], 'orologio');
  check(by('lc').locked && by('gl').locked && !by('a').locked, 'bloccato anche per un antenato');
  check(by('hid').hidden, 'nascosto');
  eq(S.layout(th, { screen: 'perm', variant: 0 }).length, 0, 'schermata bloccata: niente riquadri');
  eq(S.layout(th, { screen: 'home', variant: 5 }).length, 0, 'variante assente');

  eq(S.hitTest(bx, 111, 31), 'g1', 'senza scope: il gruppo');
  eq(S.hitTest(bx, 111, 31, { scope: 'g1' }), 'inner', 'dentro il gruppo: il figlio');
  eq(S.hitTest(bx, 145, 31, { scope: 'g1' }), 'g2', 'dentro il gruppo: il gruppo annidato');
  eq(S.hitTest(bx, 145, 31, { scope: 'g2' }), 'deep', 'scope annidato');
  eq(S.hitTest(bx, 20, 20), 'a', 'colpo semplice');
  eq(S.hitTest(bx, 15, 60), 'lst', 'la lista, anche sulla riga');
  eq(S.hitTest(bx, 15, 60, { scope: 'lst' }), 'row1', 'scope lista: la riga');
  eq(S.hitTest(bx, 1000, 1000), null, 'fuori da tutto');
  eq(S.hitTest(bx, 222, 12), null, 'hitTest salta i bloccati');
  eq(S.hitTest(bx, 222, 12, { locked: true }), 'lk', 'con locked: true li trova');
  eq(S.hitTest(bx, 5, 75), null, 'bloccato per un antenato');
  eq(S.hitTest(bx, 301 - 100, 201 - 0, {}), null, 'nascosti non si colpiscono');
  eq(S.hitTest(bx, 100 + 200 + 1, 20 + 200 + 1), null, 'nascosto dentro un gruppo');
  eq(S.hitTest(bx, 150, 80), null, 'gruppo vuoto: niente');
  eq(S.hitTest(bx, 200 + 2 + 3, 100, { slop: 4 }), 'tiny', 'slop col tocco');
  eq(S.hitTest(bx, 200 + 2 + 9, 100), null, 'senza slop niente');
  eq(S.hitTest(bx, 200 + 2 + 9, 100, { slop: 4 }), null, 'oltre lo slop niente');
  eq(S.hitTest([{ id: 'p', type: 'rect', x: 0, y: 0, w: 10, h: 10, parentId: null, locked: false, hidden: false },
    { id: 'q', type: 'rect', x: 5, y: 5, w: 10, h: 10, parentId: null, locked: false, hidden: false }], 7, 7), 'q', 'il più in alto vince');

  same(S.marquee(bx, { x: 0, y: 0, w: 250, h: 122 }, null).sort(), ['a', 'ck', 'g1', 'lst', 'm', 'tiny', 'tx'].sort(), 'marquee: primo livello, senza bloccati, nascosti e gruppi vuoti');
  same(S.marquee(bx, { x: 0, y: 0, w: 250, h: 122 }, 'g1'), ['inner', 'g2'], 'marquee con scope');
  same(S.marquee(bx, { x: 0, y: 0, w: 50, h: 40 }, null).sort(), ['a', 'ck'], 'marquee: solo interamente dentro');
  eq(S.marquee(bx, { x: 0, y: 0, w: 250, h: 122 }, null).includes('hid'), false, 'marquee salta i nascosti');
  same(S.selectionBox(bx, ['a', 'ck']), { x: 0, y: 0, w: 40, h: 30 }, 'selectionBox: unione');
  eq(S.selectionBox(bx, ['nessuno']), null, 'selectionBox vuota');
  same(S.pruneIds(th, L, ['a', 'zzz', 'deep']), ['a', 'deep'], 'pruneIds toglie gli id spariti');

  same(S.handleNames('text'), ['w', 'e', 's'], 'maniglie del testo');
  same(S.handleNames('clock'), ['w', 'e'], 'maniglie dell\'orologio');
  same(S.handleNames('mascot'), ['se'], 'maniglie a scala');
  same(S.handleNames('line', { dir: 'v' }), ['n', 's'], 'linea verticale');
  same(S.handleNames('line', {}), ['w', 'e'], 'linea orizzontale');
  same(S.handleNames('group'), [], 'gruppo senza maniglie');
  eq(S.handleNames('rect').length, 8, 'rettangolo: otto');
  check(S.handles(bx[0], ['se'], 4, true)[0].r * 2 >= 44, 'maniglie da 44 pt col tocco');
  eq(S.handles(bx[0], ['se'], 4, false)[0].r, 5, 'raggio col mouse');
  same(S.handles({ x: 10, y: 20, w: 30, h: 10 }, ['se', 'n'], 4, false).map((h) => [h.name, h.cx, h.cy]), [['se', 160, 120], ['n', 100, 80]], 'maniglie in px della tela');
  const hs = S.handles({ x: 10, y: 20, w: 30, h: 10 }, ['nw', 'se'], 4, false);
  eq(S.handleAt(hs, 42, 82), 'nw', 'handleAt dentro il raggio'); eq(S.handleAt(hs, 100, 100), null, 'handleAt fuori');
  same(S.resizeBox({ x: 10, y: 10, w: 20, h: 20 }, 'nw', 5, 5), { x: 15, y: 15, w: 15, h: 15 }, 'resize nw');
  same(S.resizeBox({ x: 10, y: 10, w: 20, h: 20 }, 'se', 5, -3), { x: 10, y: 10, w: 25, h: 17 }, 'resize se');
  eq(S.resizeBox({ x: 10, y: 10, w: 20, h: 20 }, 'e', -40, 0).w, 1, 'larghezza minima 1');
  same(S.resizeBox({ x: 10, y: 10, w: 20, h: 20 }, 'w', 40, 0), { x: 29, y: 10, w: 1, h: 20 }, 'il lato destro resta fermo');
  eq(S.resizeBox({ x: 0, y: 0, w: 20, h: 20 }, 'se', 900, 900).w, 512, 'massimo 512');
}

// 6. aggancio, allineamento, tasti
{
  const bxs = (...l) => l.map(([id, x, y, w, h, extra]) => ({ id, type: 'rect', x, y, w, h, parentId: null, depth: 0, locked: false, hidden: false, path: '', ...extra }));
  const two = bxs(['a', 2, 10, 10, 10], ['b', 100, 24, 20, 20]);
  eq(G.snapMove(two, ['a'], 7, 0, { grid: 8, others: false }).dx, 6, 'griglia 8: x da 9 a 8');
  const sm = G.snapMove(two, ['a'], 0, 2, { grid: 1 });
  eq(sm.dy, 4, 'aggancio a un altro elemento entro 3 px'); check(sm.guides.some((g) => g.axis === 'y' && g.at === 24), 'guida a y = 24');
  eq(sm.dx, 0, 'l\'asse fermo non si aggancia');
  eq(G.snapMove(two, ['a'], 50, 0, { grid: 8 }).dx, 54, 'nessun elemento entro 3 px: vince la griglia (x 52 → 56)');
  eq(G.snapMove(two, ['a'], 20, 0, { grid: 1 }).dx, 20, 'oltre 3 px: nessun aggancio');
  eq(G.snapMove(two, ['a'], 85, 0, { grid: 1 }).dx, 88, 'lato destro di a (97) contro il lato sinistro di b (100)');
  eq(G.snapMove(two, ['a'], 0, 2, { grid: 1, others: false }).dy, 2, 'others: false');
  eq(G.snapMove(two, ['a'], 117 - 2 + 2, 0, { grid: 1 }).guides.some((g) => g.axis === 'x' && g.at === 120), true, 'guida a x = 120');
  eq(G.snapMove(two, ['a', 'zzz'], 0, 0).dx, 0, 'zero resta zero');
  eq(G.snapMove(two, [], 3, 3).dx, 3, 'nessuna selezione');
  const hid = bxs(['a', 2, 10, 10, 10], ['h', 100, 24, 20, 20, { hidden: true }]);
  eq(G.snapMove(hid, ['a'], 0, 2, { grid: 1 }).dy, 2, 'i nascosti non fanno da riferimento');

  const three = bxs(['a', 10, 10, 20, 10], ['b', 40, 30, 10, 10], ['c', 100, 50, 30, 20]);
  eq(G.align(three, ['a', 'b', 'c'], 'left').get('b').dx, three[0].x - three[1].x, 'allinea a sinistra');
  eq(G.align(three, ['a', 'b', 'c'], 'right').get('a').dx, 130 - 30, 'allinea a destra');
  eq(G.align(three, ['a', 'b', 'c'], 'top').get('c').dy, 10 - 50, 'allinea in alto');
  eq(G.align(three, ['a', 'b', 'c'], 'bottom').get('a').dy, 70 - 20, 'allinea in basso');
  eq(G.align(three, ['a', 'b', 'c'], 'hcenter').get('a').dx, 70 - 20, 'centro orizzontale');
  eq(G.align(three, ['a', 'b', 'c'], 'vcenter').get('b').dy, 40 - 35, 'centro verticale');
  same([...G.align(three, ['b'], 'hcenter')], [['b', { dx: 125 - 45, dy: 0 }]], 'un solo elemento: rispetto allo schermo');
  eq(G.align(three, ['b'], 'bottom').get('b').dy, 122 - 40, 'un solo elemento in basso');
  eq(G.align(three, ['a', 'b'], 'storto').size, 0, 'modo sconosciuto');
  const withLocked = bxs(['a', 10, 10, 20, 10], ['lk', 50, 10, 10, 10, { locked: true }]);
  check(!G.align(withLocked, ['a', 'lk'], 'left').has('lk'), 'align salta i bloccati');
  eq(G.align(withLocked, ['a', 'lk'], 'right').get('a').dx, 250 - 30, 'bloccato non è riferimento: a è solo, va rispetto allo schermo');
  eq(G.distribute(two, ['a', 'b'], 'h').size, 0, 'distribuisci: almeno 3');
  const dd = G.distribute(three, ['a', 'b', 'c'], 'h');
  same([dd.get('a'), dd.get('b'), dd.get('c')], [{ dx: 0, dy: 0 }, { dx: 20, dy: 0 }, { dx: 0, dy: 0 }], 'distribuisci in orizzontale: spazi 30');
  const dv = G.distribute(bxs(['a', 10, 10, 20, 10], ['b', 40, 35, 10, 10], ['c', 100, 50, 30, 20]), ['c', 'b', 'a'], 'v');
  same([dv.get('a'), dv.get('b'), dv.get('c')], [{ dx: 0, dy: 0 }, { dx: 0, dy: -5 }, { dx: 0, dy: 0 }], 'distribuisci in verticale, ordine qualunque');
  eq(G.distribute(bxs(['a', 0, 0, 10, 10], ['b', 5, 0, 10, 10], ['c', 100, 0, 10, 10, { locked: true }]), ['a', 'b', 'c'], 'h').size, 0, 'bloccati esclusi: restano due');
  const sr = G.snapResize(two, 'a', { x: 2, y: 10, w: 95, h: 10 }, 'e', { grid: 1 });
  eq(sr.rect.w, 98, 'snapResize: il lato destro va a 100'); eq(sr.rect.x, 2, 'snapResize: il sinistro resta');
  check(sr.guides.some((g) => g.axis === 'x' && g.at === 100), 'snapResize: guida');
  eq(G.snapResize(two, 'a', { x: 2, y: 13, w: 10, h: 10 }, 'e', { grid: 1 }).rect.y, 13, 'snapResize: solo i lati della maniglia');
  eq(G.snapResize(two, 'a', { x: 2, y: 10, w: 21, h: 10 }, 'e', { grid: 8, others: false }).rect.w, 22, 'snapResize: griglia sul lato destro (23 → 24)');
  eq(G.snapResize(two, 'a', { x: 20, y: 10, w: -5, h: 10 }, 'w', { grid: 1, others: false }).rect.w >= 1, true, 'snapResize: larghezza minima');

  const kd = (e, mac = true) => JSON.stringify(K.keyAction(e, mac));
  eq(kd({ key: 'ArrowLeft', shiftKey: true }), JSON.stringify({ type: 'nudge', dx: -8, dy: 0 }), 'Maiusc+freccia = 8');
  eq(kd({ key: 'ArrowDown' }), JSON.stringify({ type: 'nudge', dx: 0, dy: 1 }), 'freccia = 1');
  eq(K.keyAction({ key: 'z', metaKey: true, shiftKey: true }, true).type, 'redo', '⇧⌘Z');
  eq(K.keyAction({ key: 'Z', metaKey: true, shiftKey: true }, true).type, 'redo', '⇧⌘Z maiuscola');
  eq(K.keyAction({ key: 'z', metaKey: true }, true).type, 'undo', '⌘Z');
  eq(K.keyAction({ key: 'z', ctrlKey: true }, false).type, 'undo', 'Ctrl+Z');
  eq(K.keyAction({ key: 'z', ctrlKey: true }, true), null, 'Ctrl+Z su Mac: niente');
  eq(K.keyAction({ key: 'y', ctrlKey: true }, false).type, 'redo', 'Ctrl+Y');
  for (const [k, t] of [['c', 'copy'], ['v', 'paste'], ['d', 'duplicate'], ['g', 'group'], ['a', 'selectAll']]) eq(K.keyAction({ key: k, metaKey: true }, true)?.type, t, '⌘' + k);
  eq(K.keyAction({ key: 'g', metaKey: true, shiftKey: true }, true).type, 'ungroup', '⇧⌘G');
  eq(K.keyAction({ key: 'Delete' }, true).type, 'delete', 'Canc'); eq(K.keyAction({ key: 'Backspace' }, false).type, 'delete', 'Backspace');
  eq(K.keyAction({ key: 'Escape' }, true).type, 'escape', 'Esc'); eq(K.keyAction({ key: 'Enter' }, true).type, 'enter', 'Invio');
  eq(K.keyAction({ key: 'q' }, true), null, 'tasto senza azione');
  eq(K.keyAction({ key: 'ArrowLeft', metaKey: true }, true), null, '⌘+freccia: niente');
}

// 5. dati di prova, anteprima della variante e avvisi
{
  eq(JSON.stringify(D.BASE), JSON.stringify(sc.base), 'BASE = scenarios.json');
  for (const [n, name] of [[0, 'zero'], [1, 'una'], [6, 'sei']]) eq(JSON.stringify(D.SESSIONS[n]), JSON.stringify(sc.scenarios.find((s) => s.name === name).sessions ?? []), 'sessioni ' + n);
  eq(D.testModel().sessions.length, 1, 'predefinito: una sessione');
  eq(D.testModel({ sessions: 6 }).sessions.length, 6, 'sei sessioni');
  eq(D.testModel({ sessions: 0, perm: true }).sessions[0].state, 'permission', 'permesso con 0 sessioni');
  eq(D.testModel({ sessions: 0, perm: true }).sessions[0].name, 'api-server', 'nome della sessione aggiunta');
  eq(D.testModel({ perm: true }).sessions[0].detail, 'Bash: rm -rf build', 'permesso: dettaglio');
  eq(D.testModel({ pcOff: true }).sys.pc, false, 'PC scollegato');
  eq(D.testModel({ pcOff: true }).sys.pcOff, true, 'PC spento');
  eq(D.testModel({ lowBatt: true }).sys.batt, 9, 'batteria scarica'); eq(D.testModel({ lowBatt: true }).bat.trend, 1.5, 'tendenza');
  eq(D.testModel({ night: true }).set.night, true, 'notte');
  const mm = D.testModel({ now: D.BASE.now + 60000 });
  eq(mm.now, D.BASE.now + 60000, 'now'); eq(mm.local, D.BASE.local + 60000, 'local si sposta uguale');
  eq(D.testModel({ frame: 3 }).frame, 3, 'frame');

  const themeWith = (...els) => ({ id: 'p', name: 'P', screens: { home: { variants: [{ when: 'always', elements: els }] } } });
  const L = { screen: 'home', variant: 0 };
  const m = D.testModel();
  const warn = (t, svc = null, model = m) => W.liveWarnings(t, L, model, S.layout(t, L), svc);
  const costo = themeWith({ type: 'text', id: 't', x: 0, y: 0, w: 200, text: '{oggi.costo}', font: { family: 'ui', size: 44, weight: 700 } });
  check(warn(costo, null, D.testModel()).some((w) => w.kind === 'glyph' && w.id === 't' && w.msg.includes('«$»')), 'glifo $ a 44');
  const fuori = themeWith({ type: 'rect', id: 'f', x: 300, y: 10, w: 20, h: 20 });
  check(warn(fuori).some((w) => w.kind === 'outside' && w.id === 'f' && w.msg === 'Fuori dallo schermo: non si vede'), 'fuori dallo schermo');
  const tagliato = themeWith({ type: 'rect', id: 'f', x: 240, y: 10, w: 20, h: 20 });
  check(warn(tagliato).some((w) => w.kind === 'offscreen' && w.msg === 'Esce dallo schermo: si vede tagliato'), 'esce dallo schermo');
  eq(warn(themeWith({ type: 'rect', id: 'f', x: 10, y: 10, w: 20, h: 20 })).length, 0, 'tutto dentro: nessun avviso');
  const lungo = themeWith({ type: 'text', id: 'l', x: 0, y: 0, w: 20, text: 'Un testo molto più lungo dello spazio', font: { family: 'ui', size: 12, weight: 600 } });
  check(warn(lungo).some((w) => w.kind === 'fit' && w.msg === 'Il testo non ci sta: si vede troncato con «…»'), 'non ci sta');
  const lungoPix = themeWith({ type: 'text', id: 'l', x: 0, y: 0, w: 20, text: 'Un testo molto più lungo dello spazio', font: { family: 'pix', size: 8, weight: 400 } });
  check(warn(lungoPix).some((w) => w.kind === 'fit' && w.msg === 'Il testo non ci sta: si vede troncato con «..»'), 'non ci sta (Silkscreen)');
  const righe = themeWith({ type: 'text', id: 'l', x: 0, y: 0, w: 60, lines: 2, text: 'uno due tre quattro cinque sei sette otto nove dieci', font: { family: 'ui', size: 12, weight: 600 } });
  check(warn(righe).some((w) => w.kind === 'fit' && w.msg === 'Il testo non ci sta in 2 righe'), 'non ci sta in 2 righe');
  eq(warn(themeWith({ type: 'text', id: 'l', x: 0, y: 0, w: 200, text: 'ciao', font: { family: 'ui', size: 12, weight: 600 } })).filter((w) => w.kind === 'fit').length, 0, 'ci sta');
  // il testo si espande col modello di prova
  check(warn(themeWith({ type: 'text', id: 'n', x: 0, y: 0, w: 20, text: '{sessione.nome}', font: { family: 'ui', size: 12, weight: 600 } })).some((w) => w.kind === 'fit'), 'fit col valore espanso');
  // coperto: dentro un rettangolo pieno (o un QR) disegnato dopo
  const coperto = themeWith({ type: 'text', id: 'a', x: 20, y: 20, w: 30, text: 'ciao', font: { family: 'ui', size: 12, weight: 600 } },
    { type: 'rect', id: 'r', name: 'Sfondo', x: 10, y: 10, w: 100, h: 60, fill: true });
  check(warn(coperto).some((w) => w.kind === 'covered' && w.id === 'a' && w.msg === 'Coperto da «Sfondo»: non si vede'), 'coperto da un rettangolo');
  const prima = themeWith({ type: 'rect', id: 'r', x: 10, y: 10, w: 100, h: 60, fill: true }, { type: 'rect', id: 'a', x: 20, y: 20, w: 30, h: 10 });
  check(!warn(prima).some((w) => w.kind === 'covered'), 'disegnato dopo: non coperto');
  const vuoto = themeWith({ type: 'rect', id: 'r', x: 10, y: 10, w: 100, h: 60 }, { type: 'rect', id: 'a', x: 20, y: 20, w: 30, h: 10 });
  check(!warn(vuoto).some((w) => w.kind === 'covered'), 'rettangolo senza riempimento non copre');
  const nascosto = themeWith({ type: 'rect', id: 'a', x: 20, y: 20, w: 30, h: 10 }, { type: 'rect', id: 'r', x: 10, y: 10, w: 100, h: 60, fill: true, hidden: true });
  check(!warn(nascosto).some((w) => w.kind === 'covered'), 'chi copre è nascosto');
  const sottoQr = themeWith({ type: 'rect', id: 'a', x: 20, y: 20, w: 10, h: 10 }, { type: 'qr', id: 'q', x: 10, y: 10, w: 50, h: 50, text: 'x' });
  check(warn(sottoQr).some((w) => w.kind === 'covered' && w.msg === 'Coperto da «QR»: non si vede'), 'coperto da un QR');
  const parziale = themeWith({ type: 'rect', id: 'a', x: 5, y: 20, w: 30, h: 10 }, { type: 'rect', id: 'r', x: 10, y: 10, w: 100, h: 60, fill: true });
  check(!warn(parziale).some((w) => w.kind === 'covered'), 'solo in parte: non coperto');
  // variante vuota
  const vv = { screens: { home: { variants: [{ when: 'always', elements: [] }] } } };
  check(warn(vv).some((w) => w.kind === 'empty' && w.id === null && w.msg === 'Variante senza elementi: la scheda mostrerà lo schermo vuoto'), 'variante vuota');
  // problemi e avvisi del servizio sull'elemento giusto
  const conGruppo = themeWith({ type: 'rect', id: 'x0', x: 0, y: 0, w: 5, h: 5 },
    { type: 'group', id: 'g', children: [{ type: 'rect', id: 'figlio0', x: 1, y: 1, w: 5, h: 5 }] });
  const svc = { screens: { home: 900 }, warnings: [], problems: [{ path: 'screens.home.variants[0].elements[1].children[0]', msg: 'x: fuori da ±512' }] };
  const sv = warn(conGruppo, svc).find((w) => w.kind === 'service');
  eq(sv.id, 'figlio0', "problemi del servizio sull'elemento giusto"); eq(sv.msg, 'fuori da ±512', 'messaggio dopo «: »');
  eq(warn(conGruppo, { screens: {}, warnings: [], problems: [{ path: 'screens.home.variants[0].elements[1].children[0].font.size', msg: 'dimensione: no' }] }).find((w) => w.kind === 'service').id, 'figlio0', 'percorso più profondo: elemento più vicino');
  eq(warn(conGruppo, { screens: {}, warnings: [], problems: [{ path: 'screens.home.variants[0].elements[10]', msg: 'a: b' }] }).find((w) => w.kind === 'service').id, null, 'elements[10] non è elements[1]');
  eq(warn(conGruppo, { screens: {}, warnings: [], problems: [{ path: 'screens.home.variants[1].elements[0]', msg: 'a: b' }] }).filter((w) => w.kind === 'service').length, 0, 'altra variante: niente');
  eq(warn(conGruppo, { screens: {}, warnings: [], problems: [{ path: 'screens.detail.variants[0].elements[0]', msg: 'a: b' }] }).filter((w) => w.kind === 'service').length, 0, 'altra schermata: niente');
  const sw = warn(conGruppo, { screens: {}, problems: [], warnings: ['screens.home.variants[0].elements[0]: font: strano', 'screens.home.variants[0].elements[0]: Chakra Petch 44/700 non ha «$»: si vedrà «?»'] }).filter((w) => w.kind === 'service');
  eq(sw.length, 1, 'avvisi dei glifi del servizio saltati'); eq(sw[0].id, 'x0', 'avviso legato all\'elemento'); eq(sw[0].msg, 'strano', 'avviso: testo dopo «: »');
  // problemi fuori dalla variante aperta (I2): elencati col luogo, mai nascosti
  { const two = { id: 'p', name: 'P', screens: { home: { variants: [{ when: 'always', elements: [{ type: 'rect', id: 'a', x: 0, y: 0, w: 5, h: 5 }] },
      { when: 'always', elements: [{ type: 'rect', id: 'b', x: 0, y: 0, w: 5, h: 5 }, { type: 'text', id: 'tt', name: 'Titolo', x: 0, y: 0, w: 50, text: 'x' }] }] },
      oggi: { variants: [{ when: 'always', elements: [] }] } } };
    const lbl = (k) => C.SCREENS.find((x) => x.key === k)?.label ?? k;
    const out = (path, msg = 'a: b') => warn(two, { screens: {}, warnings: [], problems: [{ path, msg }] }).filter((w) => w.kind === 'elsewhere');
    let e = out('screens.oggi', 'schermata oltre 4 096 byte');
    eq(e.length, 1, 'altra schermata: elencato');
    same(e[0].where, { screen: 'oggi', variant: null, id: null, label: '', open: true }, 'altra schermata: luogo');
    eq(W.warningPlace(e[0].where, lbl), 'Oggi', 'luogo: la schermata'); eq(e[0].msg, 'schermata oltre 4 096 byte', 'altra schermata: messaggio intero');
    e = out('screens.home.variants[1].elements[1].font.size', 'dimensione: no');
    same(e[0].where, { screen: 'home', variant: 1, id: 'tt', label: 'Titolo', open: true }, 'altra variante: elemento');
    eq(W.warningPlace(e[0].where, lbl), 'Home › variante 2 › Titolo', 'luogo: schermata › variante › elemento'); eq(e[0].msg, 'no', 'altra variante: dopo «: »');
    e = out('screens.home.variants[1].when');
    eq(W.warningPlace(e[0].where, lbl), 'Home › variante 2', 'luogo: la variante');
    for (const p of ['name', 'id', '', 'screens']) {
      e = out(p, 'nome da 1 a 24 byte');
      eq(e.length, 1, 'a livello del tema: ' + JSON.stringify(p)); eq(W.warningPlace(e[0].where, lbl), 'tema', 'luogo «tema»: ' + JSON.stringify(p)); eq(e[0].where.open, false, 'tema: niente da aprire');
    }
    eq(out('', 'JSON non valido: x')[0].msg, 'JSON non valido: x', 'percorso vuoto: messaggio intero');
    eq(out('screens.perm')[0].where.open, false, 'Permessi non si apre');
    eq(out('screens.home.variants[0].elements[0]').length, 0, 'nella variante aperta resta sull\'elemento');
    // nessun problema senza riga
    const ps = ['name', 'screens.oggi', 'screens.home.variants[0].elements[0]', 'screens.home.variants[1]', 'screens.perm', ''].map((path) => ({ path, msg: 'x: y' }));
    eq(warn(two, { screens: {}, warnings: [], problems: ps }).filter((w) => w.kind === 'service' || w.kind === 'elsewhere').length, ps.length, 'ogni problema ha la sua riga');
    eq(warn(two, { screens: {}, warnings: [], problems: [null, { path: 3 }, { msg: '' }] }).filter((w) => w.kind === 'elsewhere').length, 3, 'anche i problemi malformati hanno una riga'); }
  // dimensione
  check(warn(conGruppo, { screens: { home: 3700 }, warnings: [], problems: [] }).some((w) => w.kind === 'size' && w.msg === 'Schermata di 3700 byte: vicina al limite di 4 096'), 'avviso di dimensione');
  eq(warn(conGruppo, { screens: { home: 3686 }, warnings: [], problems: [] }).filter((w) => w.kind === 'size').length, 0, 'sotto il 90 %');
  eq(warn(conGruppo, { screens: { home: 5000 }, warnings: [], problems: [] }).filter((w) => w.kind === 'size').length, 0, 'oltre il limite lo dice il servizio');
  // nomi
  eq(W.elementLabel({ type: 'text', name: 'Titolo' }), 'Titolo', 'nome'); eq(W.elementLabel({ type: 'qr' }), 'QR', 'QR');
  eq(W.elementLabel({ type: 'stateIcon' }), 'Icona di stato', 'icona'); eq(W.elementLabel({ type: 'rect', name: '' }), 'Rettangolo', 'nome vuoto');
  for (const [t, n] of [['text', 'Testo'], ['clock', 'Orologio'], ['mascot', 'Mascotte'], ['battery', 'Batteria'], ['wifi', 'Wi-Fi'], ['bar', 'Barra'], ['sym', 'Simbolo'], ['line', 'Linea'], ['circle', 'Cerchio'], ['list', 'Lista'], ['group', 'Gruppo']]) eq(W.elementLabel({ type: t }), n, 'etichetta ' + t);

  // variante della scheda
  const vs = { screens: { home: { variants: [{ when: 'none', elements: [] }, { when: 'one', elements: [] }, { when: 'many', elements: [] }] } } };
  eq(W.boardVariant(vs, 'home', D.testModel({ sessions: 0 })), 0, 'scheda: nessuna sessione'); eq(W.boardVariant(vs, 'home', D.testModel({ sessions: 1 })), 1, 'scheda: una');
  eq(W.boardVariant(vs, 'home', D.testModel({ sessions: 6 })), 2, 'scheda: molte');
  eq(W.boardVariant({ screens: { home: { variants: [{ when: 'one', elements: [] }] } } }, 'home', D.testModel({ sessions: 6 })), -1, 'scheda: nessuna vera');
  eq(W.boardVariant({ screens: {} }, 'home', m), -1, 'scheda: schermata assente');

  // anteprima = scheda: per ogni caso drawn del golden la variante scelta dalla scheda, resa con previewTheme, ha lo stesso CRC
  const golden = JSON.parse(fs.readFileSync(path.join(REPO, 'firmware/test/test_theme_parity/golden.json'), 'utf8'));
  const gm = (name) => {
    const s = sc.scenarios.find((x) => x.name === name);
    return makeModel(sc.base, s.extends ? merge(sc.scenarios.find((x) => x.name === s.extends), s) : s);
  };
  let n = 0;
  for (const c of golden.cases.filter((c) => c.drawn)) {
    const t = fixture(c.theme), mod = gm(c.scenario), vi = W.boardVariant(t, c.screen, mod);
    const b = new Bitmap();
    renderScreen(W.previewTheme(t, { screen: c.screen, variant: vi }), c.screen, mod, b, assets);
    eq(crc32(b.packed()), c.crc, 'parità ' + c.theme + '/' + c.screen + '/' + c.scenario); n++;
  }
  eq(n, golden.cases.filter((c) => c.drawn).length, 'casi di parità = casi drawn del golden');
  // `board` scatta se e solo se la scheda non disegna (tutta la schermata, ogni caso del golden)
  for (const c of golden.cases) {
    const t = fixture(c.theme), mod = gm(c.scenario), lc = { screen: c.screen, variant: Math.max(0, W.boardVariant(t, c.screen, mod)) };
    eq(W.liveWarnings(t, lc, mod, S.layout(t, lc), null).some((w) => w.kind === 'board'), !c.drawn, 'board <=> non disegnata ' + c.theme + '/' + c.screen + '/' + c.scenario);
  }
  // coprire: visibleIf e angoli arrotondati non coprono
  const cov = (r) => W.liveWarnings(themeWith({ type: 'rect', id: 'a', x: 20, y: 20, w: 10, h: 10 }, r), L, m, S.layout(themeWith({ type: 'rect', id: 'a', x: 20, y: 20, w: 10, h: 10 }, r), L), null).some((w) => w.kind === 'covered');
  check(cov({ type: 'rect', id: 'r', x: 0, y: 0, w: 100, h: 60, fill: true }), 'copre (controllo)');
  check(!cov({ type: 'rect', id: 'r', x: 0, y: 0, w: 100, h: 60, fill: true, visibleIf: [{ night: true }] }), 'visibleIf: non copre');
  check(!cov({ type: 'rect', id: 'r', x: 0, y: 0, w: 100, h: 60, fill: true, radius: 4 }), 'angoli arrotondati: non copre');
  const reason = (t) => W.liveWarnings(t, L, m, S.layout(t, L), null).find((w) => w.kind === 'board')?.msg;
  check(reason({ screens: { home: { variants: [] } } })?.includes('non ha varianti'), 'motivo: nessuna variante');
  check(reason(themeWith({ type: 'text', id: 'x', x: 0, y: 0, w: 20, text: 'a', font: { family: 'ui', size: 13, weight: 650 } }))?.includes('carattere'), 'motivo: carattere');
  // tema rotto: non lancia, schermata di serie
  const rotto = { screens: { home: { variants: [{ elements: [{ type: 'text', x: 600, text: '{oggi.co' }] }] } } };
  check(!renderScreen(W.previewTheme(rotto, L), 'home', m, new Bitmap(), assets).drawn, 'preview: tema rotto non lancia, schermata di serie');
  check(!renderScreen(W.previewTheme(null, L), 'home', m, new Bitmap(), assets).drawn, 'preview: tema nullo');
  // F15: un elemento fuori dai limiti in un'altra variante manda la scheda alla schermata di serie, anche se l'anteprima disegna
  const duo = { screens: { home: { variants: [{ when: 'none', elements: [{ type: 'rect', id: 'a', x: 1, y: 1, w: 5, h: 5 }] }, { when: 'one', elements: [{ type: 'rect', id: 'b', x: 900, y: 1, w: 5, h: 5 }] }] } } };
  const l0 = { screen: 'home', variant: 0 }, m0 = D.testModel({ sessions: 0 });
  check(renderScreen(W.previewTheme(duo, l0), 'home', m0, new Bitmap(), assets).drawn, 'F15: la variante aperta si disegna');
  check(W.liveWarnings(duo, l0, m0, S.layout(duo, l0), null).some((w) => w.kind === 'board' && w.id === null && w.msg === "La scheda mostrerebbe la schermata di serie: c'è un elemento fuori dai limiti"), 'F15: avviso sull\'intera schermata');
  check(!W.liveWarnings(costo, L, m, S.layout(costo, L), null).some((w) => w.kind === 'board'), 'F15: tema sano, nessun avviso');
  const nessuna = { screens: { home: { variants: [{ when: 'one', elements: [{ type: 'rect', id: 'a', x: 1, y: 1, w: 5, h: 5 }] }] } } };
  check(W.liveWarnings(nessuna, L, D.testModel({ sessions: 6 }), S.layout(nessuna, L), null).some((w) => w.kind === 'board' && w.msg.includes('Nessuna variante')), 'nessuna variante vera con i dati di prova');
}

// 7. stato e azioni (api finta in memoria), catalogo, anteprima ingrandita, parità del sorgente
{
  // store: notifica una volta, in microtask
  const s0 = createStore({ a: 1 }); let calls = 0; const un = s0.subscribe(() => calls++);
  s0.set({ a: 2 }); s0.set((x) => ({ a: x.a + 1 })); eq(calls, 0, 'store: niente notifica sincrona');
  await flush(); eq(calls, 1, 'store: una notifica per i set dello stesso giro'); eq(s0.get().a, 3, 'store: set con funzione');
  un(); s0.set({ a: 4 }); await flush(); eq(calls, 1, 'store: unsubscribe');

  const api = fakeApi(['altro', 'prova']);
  const st = createStore(initialState()); const A = createActions(st, api, fakeTimers());
  same(Object.keys(initialState()).sort(), ['check', 'clip', 'clock', 'drawer', 'error', 'frame', 'grid', 'hist', 'loc', 'notice', 'playing', 'save', 'scope', 'sel', 'send', 'snap', 'test', 'themeId', 'themes', 'touchMulti', 'unsaved', 'zoom'], 'stato iniziale');
  await A.init({ want: 'prova' }); eq(st.get().themes.length, 2, 'elenco dei temi'); eq(st.get().themeId, 'prova', 'init apre ?theme=');
  await A.openTheme('prova'); check(st.get().hist.present.id === 'prova', 'tema aperto');
  eq(st.get().save.base, 1000, 'base dal servizio (X-Calamaio-Updated)');
  check(!Hi.canUndo(st.get().hist), 'aprire non è una modifica');
  check(O.allIds(st.get().hist.present).length > 0, 'id assegnati in apertura');
  // F19: variante d'apertura = quella che la scheda sceglie coi dati di prova (1 sessione → «one»)
  eq(st.get().loc.variant, W.boardVariant(st.get().hist.present, 'home', D.testModel()), 'variante della scheda');
  eq(st.get().loc.variant, 1, 'prova home: variante «una sessione»');
  A.commit((t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 10, 10)));
  eq(st.get().sel.length, 1, 'il nuovo elemento è selezionato'); A.undo(); eq(st.get().sel.length, 0, 'annulla toglie la selezione che non esiste più');
  A.redo(); eq(st.get().sel.length, 0, 'ripeti non inventa una selezione');
  A.setLoc({ screen: 'perm', variant: 0 }); eq(st.get().loc.screen, 'home', 'Permessi non si apre');
  A.setLoc({ screen: 'sleep', variant: 0 }); eq(st.get().loc.screen, 'sleep', 'Riposo si apre');
  A.setLoc({ screen: 'home', variant: 7 }); eq(st.get().loc.variant, 0, 'variante inesistente → la prima');
  // rifiuto: tema invariato, motivo da mostrare
  const h0 = st.get().hist;
  A.commit((t) => O.addEl(t, { screen: 'perm', variant: 0 }, O.elementDefaults('rect', 0, 0)));
  eq(st.get().hist, h0, 'rifiuto: cronologia invariata'); eq(st.get().notice, 'Non si può mettere lì.', 'rifiuto: motivo');
  A.commit((t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 0, 0))); eq(st.get().notice, '', 'una modifica riuscita toglie il motivo');
  // gesto: un passo solo
  const id = st.get().sel[0], past = st.get().hist.past.length;
  for (let i = 1; i <= 5; i++) A.commit((t) => O.moveEls(t, st.get().loc, [id], 1, 0), 'move:' + id);
  eq(st.get().hist.past.length, past + 1, 'trascinamento = un passo'); A.endGesture(); eq(st.get().hist.open, null, 'gesto chiuso');
  A.select([id, 'nessuno', id]); same(st.get().sel, [id], 'select: id inesistenti e doppi tolti');
  A.setZoom(9); eq(st.get().zoom, 6, 'zoom massimo 6'); A.setZoom(0); eq(st.get().zoom, 1, 'zoom minimo 1'); A.setZoom(2.6); eq(st.get().zoom, 3, 'zoom intero');
  A.setDrawer('left'); eq(st.get().drawer, 'left', 'cassetto'); A.setDrawer('boh'); eq(st.get().drawer, null, 'cassetto sconosciuto = chiuso');
  // cronologie per tema per tutta la sessione
  await A.openTheme('altro'); eq(st.get().themeId, 'altro', 'altro tema'); check(!Hi.canUndo(st.get().hist), 'altro tema: cronologia sua');
  await A.openTheme('prova'); check(Hi.canUndo(st.get().hist), 'tornando su prova (non cambiato altrove) la cronologia c\'è ancora');
  eq(st.get().sel.length, 0, 'cambio tema: selezione vuota');
  eq(st.get().save.state, 'pending', 'le modifiche non salvate restano «in sospeso», mai «salvato»');
  eq(st.get().save.base, 1000, 'la base resta quella del contenuto che abbiamo');
  // I2: cambiato altrove mentre eravamo su un altro tema → vale la versione del servizio, le modifiche locali da parte
  await A.openTheme('altro'); api.touch('prova', { name: 'Dal telefono' });
  await A.openTheme('prova');
  eq(st.get().hist.present.name, 'Dal telefono', 'riaperto: contenuto del servizio, non la copia vecchia');
  eq(st.get().save.base, 2000, 'riaperto: base nuova insieme al contenuto nuovo');
  check(!Hi.canUndo(st.get().hist), 'riaperto: cronologia ripartita dal servizio');
  eq(st.get().save.state, 'saved', 'riaperto: niente da salvare');
  check(/cambiato altrove/.test(st.get().notice) && /modifiche locali non salvate/.test(st.get().notice), 'riaperto: avviso');
  check(A.localChanges('prova') && Hi.canUndo(A.localChanges('prova')), 'modifiche locali messe da parte, non perse');
  // cambiato altrove ma senza modifiche locali: versione aggiornata, nessuna «messa da parte»
  await A.openTheme('altro'); api.touch('prova', { name: 'Ancora' }); await A.openTheme('prova');
  eq(st.get().hist.present.name, 'Ancora', 'versione aggiornata'); eq(A.localStack('prova').length, 1, 'niente di nuovo da mettere da parte (quella di prima resta)');
  // eccezione dentro un'operazione: avviso e console, tema invariato
  { const h1 = st.get().hist, err = console.error; let logged = 0; console.error = () => logged++;
    A.commit(() => { throw new Error('boom'); }); console.error = err;
    eq(st.get().hist, h1, 'eccezione: tema invariato'); check(/errore interno/i.test(st.get().notice), 'eccezione: avviso'); eq(logged, 1, 'eccezione: nella console'); }
  // prima di cambiare tema si salva (con la base); un 409 resta conflitto e non sovrascrive
  { const api2 = fakeApi(['altro', 'prova'], { withPut: true }), s5 = createStore(initialState()), A5 = createActions(s5, api2, fakeTimers());
    await A5.init({ want: 'prova' });
    A5.commit((t) => O.addEl(t, s5.get().loc, O.elementDefaults('rect', 1, 1)));
    await A5.openTheme('altro');
    const iPut = api2.calls.indexOf('put prova base=1000'), iGet = api2.calls.indexOf('get altro');
    check(iPut >= 0 && iPut < iGet, 'flush prima di cambiare tema, con la base: ' + api2.calls.join(', '));
    await A5.openTheme('prova'); eq(s5.get().save.state, 'saved', 'salvato'); eq(s5.get().save.base, 1007, 'base del salvataggio');
    check(Hi.canUndo(s5.get().hist), 'salvato da noi: la cronologia resta');
    A5.commit((t) => O.addEl(t, s5.get().loc, O.elementDefaults('rect', 2, 2)));
    api2.touch('prova', { name: 'Telefono' });
    await A5.openTheme('altro');
    check(api2.calls.includes('put prova base=1007'), 'il secondo flush manda la base vecchia');
    eq((await api2.getTheme('prova')).theme.name, 'Telefono', '409: la modifica del telefono non è sovrascritta');
    await A5.openTheme('prova'); eq(s5.get().hist.present.name, 'Telefono', 'dopo il 409: versione del servizio');
    check(A5.localChanges('prova'), 'dopo il 409: modifiche locali da parte'); }
  // R1: due salvataggi sovrapposti dello stesso tema: il secondo aspetta e usa la base nuova (niente 409)
  { const api6 = fakeApi(['prova'], { withPut: true }), s6 = createStore(initialState()), A6 = createActions(s6, api6, fakeTimers());
    await A6.init();
    let open; api6.gate = new Promise((r) => { open = r; });
    A6.commit((t) => O.addEl(t, s6.get().loc, O.elementDefaults('rect', 1, 1)));
    const p1 = A6.saveNow();
    A6.commit((t) => O.addEl(t, s6.get().loc, O.elementDefaults('rect', 3, 3)));
    const p2 = A6.saveNow();
    await flush(); same(api6.calls.filter((c) => c.startsWith('put')), ['put prova base=1000'], 'uno solo in volo');
    api6.gate = null; open();
    same([await p1, await p2], [true, true], 'tutti e due riusciti');
    same(api6.calls.filter((c) => c.startsWith('put')), ['put prova base=1000', 'put prova base=1007'], 'il secondo con la base del primo');
    eq(s6.get().save.state, 'saved', 'alla fine salvato'); eq(s6.get().save.base, 1014, 'base finale'); }
  // base mancante: niente PUT alla cieca
  { const api7 = fakeApi(['prova'], { withPut: true }); const g = api7.getTheme; api7.getTheme = async (id) => ({ ...(await g(id)), base: 0 });
    const s7 = createStore(initialState()), A7 = createActions(s7, api7, fakeTimers()); await A7.init();
    A7.commit((t) => O.addEl(t, s7.get().loc, O.elementDefaults('rect', 1, 1)));
    eq(await A7.saveNow(), false, 'senza base non salva'); check(!api7.calls.some((c) => c.startsWith('put')), 'nessun PUT senza base');
    eq(s7.get().save.state, 'error', 'senza base: errore visibile');
    const realFetch = globalThis.fetch; let fetched = 0; globalThis.fetch = async () => { fetched++; return new Response('{}'); };
    for (const base of [0, undefined, -5, 1.5]) { let threw = false; try { await putTheme('prova', {}, { base }); } catch { threw = true; } check(threw, 'api.putTheme rifiuta base ' + base); }
    globalThis.fetch = realFetch; eq(fetched, 0, 'api.putTheme senza base non chiama il servizio');
    const urls = []; globalThis.fetch = async (u) => { urls.push(u); return new Response('{"theme":{"updated":5}}'); };
    await putTheme('prova', {}, { base: 7 }); await putTheme('prova', {}, { base: 7, fresh: true });
    globalThis.fetch = realFetch;
    same(urls, ['/api/themes/prova?autosave=1&base=7', '/api/themes/prova?autosave=1&base=7&fresh=1'], 'api.putTheme: fresh=1 solo per «Sovrascrivi»'); }
  // init: senza ?theme il primo; nessun tema; servizio giù
  const s2 = createStore(initialState()); await createActions(s2, fakeApi(['altro', 'prova']), fakeTimers()).init(); eq(s2.get().themeId, 'altro', 'init: il primo dell\'elenco');
  const s3 = createStore(initialState()); await createActions(s3, fakeApi([]), fakeTimers()).init(); eq(s3.get().themeId, null, 'init: nessun tema'); eq(s3.get().hist, null, 'init: niente cronologia');
  const down = fakeApi(); down.down = true; const s4 = createStore(initialState()); await createActions(s4, down, fakeTimers()).init();
  check(/servizio non raggiungibile/.test(s4.get().error), 'init: errore mostrato');

  // catalogo
  eq(C.SCREENS.length, 13, '12 schermate + Permessi');
  same(C.SCREENS.slice(0, 12).map((x) => x.key), ['home', 'detail', 'sleep', 'offline', 'riepilogo', 'oggi', 'batteria', 'stato', 'limiti', 'focus', 'ambiente', 'pet'], 'chiavi delle schermate');
  same(C.SCREENS.slice(0, 12).map((x) => x.label), ['Home', 'Dettaglio', 'Riposo', 'PC non raggiungibile', 'PC spento', 'Oggi', 'Batteria', 'Stato', 'Limiti', 'Focus', 'Ambiente', 'La mascotte'], 'nomi delle schermate');
  same(C.SCREENS[12], { key: 'perm', label: 'Permessi — bloccata (regole di sicurezza)', locked: true }, 'Permessi bloccata');
  eq(C.FONT_CHOICES.length, 22, '22 caratteri');
  check(C.FONT_CHOICES.some((f) => f.label === 'Chakra Petch 12 · 600' && f.font.family === 'ui' && f.font.size === 12 && f.font.weight === 600), 'Chakra Petch 12 · 600');
  check(C.FONT_CHOICES.some((f) => f.label === 'Silkscreen 8 · 400' && f.font.family === 'pix'), 'Silkscreen 8 · 400');
  same(C.WHEN.map((w) => w.label), ['sempre', 'nessuna sessione', 'una sessione', 'più sessioni'], 'quando');
  check(C.TYPES.every((t) => O.elementDefaults(t.type, 0, 0)) && !C.TYPES.some((t) => t.type === 'group'), 'tipi aggiungibili = quelli con un predefinito');
  eq(C.TYPES.find((t) => t.type === 'rect').label, 'Rettangolo', 'etichetta del tipo');

  // anteprima ingrandita: solo dal renderer condiviso
  const prova = O.ensureIds(fixture('prova')), L1 = { screen: 'home', variant: 1 };
  const pv = renderPreview(prova, L1, D.testModel(), assets);
  check(pv.info.drawn && pv.bitmap.countInk() > 0, 'anteprima disegnata');
  const ref = new Bitmap(); renderScreen(W.previewTheme(prova, L1), 'home', D.testModel(), ref, assets);
  eq(crc32(pv.bitmap.packed()), crc32(ref.packed()), 'anteprima = renderScreen(previewTheme)');
  check(!renderPreview(prova, { screen: 'oggi', variant: 0 }, D.testModel(), assets).info.drawn, 'schermata assente: non disegnata');

  // risorse: avatar con le ancore, errori → null
  const fakeLoad = {
    avatar: async (id) => { if (!avs[id]) throw new Error('HTTP 404'); const { anchors, ...rest } = avs[id]; return rest; },
    anchors: async (id) => ({ avatar: id, anchors: avs[id]?.anchors ?? null }),
    accessory: async (id) => { if (!accs[id]) throw new Error('HTTP 404'); return accs[id]; },
  };
  const as = createAssets(fakeLoad);
  const mm = D.testModel({ sessions: 6 }); mm.sessions[5].avatar = 'nessuno';
  await as.ensure(mm);
  check(as.avatar('gatto')?.anchors && JSON.stringify(as.avatar('gatto').anchors) === JSON.stringify(avs.gatto.anchors), 'avatar con le ancore');
  eq(as.avatar('nessuno'), null, 'avatar sconosciuto → null (Goccia)');
  check(as.avatar('goccia'), 'la Goccia sempre caricata');
  const accId = Object.keys(accs)[0]; const mp = D.testModel(); mp.pet = { ...(mp.pet || {}), on: true, wear: accId };
  await as.ensure(mp); check(as.accessory(accId), 'accessorio caricato');
  // errore di rete: niente null fisso, si riprova dopo un po'; 404 = definitivo
  { let t = 0, down = true, n404 = 0;
    const flaky = { ...fakeLoad, avatar: async (id) => { if (id === 'sparito') { n404++; throw new Error('HTTP 404'); } if (down && id === 'gatto') throw new Error('Failed to fetch'); return fakeLoad.avatar(id); } };
    const a2 = createAssets(flaky, { now: () => t }), m1 = D.testModel(); m1.sessions[0].avatar = 'gatto';
    await a2.ensure(m1); eq(a2.avatar('gatto'), null, 'rete giù: per ora null');
    down = false; await a2.ensure(m1); eq(a2.avatar('gatto'), null, 'non si riprova subito');
    t += 10001; check(await a2.ensure(m1), 'riprova: arrivato, da ridisegnare'); check(a2.avatar('gatto'), 'rete tornata: avatar');
    m1.sessions[0].avatar = 'sparito'; await a2.ensure(m1); t += 60000; await a2.ensure(m1); eq(n404, 1, '404 non si richiede'); }

  // parità: nessun disegno fuori dal renderer condiviso (F14, rafforzata: parity-scan.mjs)
  const JS = path.join(REPO, 'service/internal/webui/static/assets/js');
  const walk = (d) => fs.readdirSync(d, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? walk(path.join(d, e.name)) : e.name.endsWith('.js') ? [path.join(d, e.name)] : []));
  const real = {};
  for (const f of [...walk(path.join(JS, 'editor')), path.join(JS, 'editor.js'), ...fs.readdirSync(JS).filter((f) => /^ui-.*\.js$/.test(f)).map((f) => path.join(JS, f))])
    real[path.relative(JS, f).split(path.sep).join('/')] = fs.readFileSync(f, 'utf8');
  const readDep = (rel) => { try { return fs.readFileSync(path.join(JS, rel), 'utf8'); } catch { return null; } };
  const found = scan(real, readDep);
  for (const p of found) check(false, 'parità: ' + p);
  check(real['editor/ui-canvas.js'] && real['editor/view.js'] && real['editor.js'], 'file dell\'editor scanditi');
  eq(resolveSpec('editor/view.js', '../theme/./prims.js?v=1#x'), 'theme/prims.js', 'specificatore normalizzato');
  // regressioni: i tentativi della revisione (devono essere presi) e i casi legittimi (devono passare)
  const add = (rel, extra) => ({ ...real, [rel]: real[rel] + '\n' + extra + '\n' });
  const sub = (rel, a, b) => { check(real[rel].includes(a), 'regressione: testo da sostituire in ' + rel); return { ...real, [rel]: real[rel].replace(a, b) }; };
  const uiReexport = (rel) => (rel === 'ui.js' ? readDep('ui.js').replace('export function h(', "export { box as h } from './theme/prims.js';\nfunction hh(") : readDep(rel));
  const UC = 'editor/ui-canvas.js', VW = 'editor/view.js';
  const caught = [
    ['getContext + fillRect in ui-canvas', add(UC, "screen.getContext('2d').fillRect(0, 0, 1, 1);")],
    ['drawImage fuori da view', add(UC, 'overlay.drawImage(screen, 0, 0);')],
    ['T con alias da text.js', add(UC, "import { T as write } from '../theme/text.js';")],
    ['glyphs.js in ui-canvas', add(UC, "import { fontGlyphs } from '../theme/glyphs.js';")],
    ['qr.js', add(UC, "import { encode } from '../theme/qr.js';")],
    ['* as render.js', sub(VW, "import { renderScreen } from '../theme/render.js';", "import * as R from '../theme/render.js';")],
    ['prims con query', add(UC, "import { box } from '../theme/prims.js?v=1';")],
    ['prims con ./', add(UC, "import { box } from '../theme/./prims.js';")],
    ['riesportazione via ui.js', real, uiReexport],
    ['bitmap.fillRect in view', add(VW, 'export const bad = (bitmap) => bitmap.fillRect(0, 0, 5, 5, true);')],
    ['bitmap.px scritto in view', add(VW, 'export const bad2 = (bitmap) => { bitmap.px[0] = 1; };')],
    ['sottocartella', { ...real, 'editor/sub/x.js': "export const x = (c) => c.getContext('2d');" }],
    ['import() dinamico', add(UC, "const P = await import('../theme/prims.js');")],
    ['getContext fra virgolette quadre', add(UC, "screen['getContext']('2d');")],
    ['canvas creato a mano', add(UC, "document.createElement('canvas');")],
    ['import predefinito', add(UC, "import prims from '../theme/prims.js';")],
    ['import per effetto', add(UC, "import '../mascot.js';")],
    ['renderScreen con alias in ui-canvas', add(UC, "import { renderScreen as rs } from '../theme/render.js';")],
    ['renderScreen in ui-canvas', add(UC, "import { renderScreen } from '../theme/render.js';")],
    ['bitmap.px scritto in ui-canvas', add(UC, 'export const bad3 = (bitmap) => { bitmap.px[3] = 1; };')],
    ['px.fill in un altro file', add('editor/actions.js', 'export const bad4 = (b) => b.px.fill(1);')],
    ['eval', add(UC, "eval('1');")],
    ['new Function', add(UC, "const f = new Function('return 1');")],
    ['Function()', add(UC, "const g = Function('return 1');")],
    ['import a metà riga', add(UC, "/* x */ import { box } from '../theme/prims.js';")],
    ['riesportazione da un file dell\'editor', add('editor/catalog.js', "export { box } from '../theme/prims.js';")],
  ];
  for (const [name, files, read = readDep] of caught) check(scan(files, read).length > 0, 'la scansione prende: ' + name);
  const legit = [
    ['ui.js con query', sub('editor/ui-topbar.js', "from '../ui.js'", "from '../ui.js?v=2'")],
    ['virgolette doppie', sub('editor/ui-topbar.js', "from '../ui.js'", 'from "../ui.js"')],
    ['./ in più', sub('editor/actions.js', "from './history.js'", "from './././history.js'")],
    ['un altro c.fillRect in view', add(VW, "export const ok = (c) => c.fillRect(0, 0, 1, 1);")],
  ];
  for (const [name, files] of legit) same(scan(files, readDep), [], 'la scansione lascia passare: ' + name);

  eq(fitZoom(728), 2, 'zoom iniziale a 1280'); eq(fitZoom(361, 3), 1, 'zoom iniziale a 393'); eq(fitZoom(10), 1, 'zoom minimo');
  eq(fitZoom(1600), 6, 'zoom massimo'); eq(fitZoom(600, 1.25), 2, 'zoom 2 a dpr 1.25 = 600 px CSS: ci sta'); eq(fitZoom(599, 1.25), 1, '…e a 599 no');
  // I1: anteprima e sovrapposizione con la stessa scala intera, nessun ricampionamento
  for (const dpr of [1, 1.25, 1.5, 2, 2.625, 3]) for (let z = 1; z <= 6; z++) {
    const { s, css } = scaleFor(z, dpr), k = s / z;
    check(Number.isInteger(s) && s >= 1, 'scala intera ' + z + '@' + dpr);
    check(Math.abs(250 * css * dpr - 250 * s) < 1e-6, 'larghezza CSS·dpr = pixel del canvas ' + z + '@' + dpr);
    // una linea della griglia della sovrapposizione (coordinate board·zoom, trasformazione k) cade sul bordo del pixel
    for (const x of [1, 7, 125, 249]) check(Math.abs(x * z * k - x * s) < 1e-9, 'griglia sul bordo ' + x + ' ' + z + '@' + dpr);
  }
}

// 8. gesti (mouse e tocco), tastiera, azioni della tela
{
  const L = { screen: 'home', variant: 0 };
  const mk = (elements) => ({ id: 'g', name: 'g', format: 1, screens: { home: { variants: [{ when: 'always', elements }] } } });
  const rect = (id, x, y, w, h, extra = {}) => ({ id, type: 'rect', x, y, w, h, ...extra });
  const th = mk([
    rect('a', 10, 10, 30, 20), rect('b', 60, 10, 20, 20),
    { id: 'gr', type: 'group', x: 100, y: 60, children: [rect('c1', 0, 0, 10, 10), rect('c2', 20, 0, 10, 10)] },
    { id: 'm', type: 'mascot', x: 150, y: 60, scale: 2 },
    rect('lk', 220, 10, 20, 20, { locked: true }), rect('o', 200, 100, 7, 5),
  ]);
  const bx = S.layout(th, L), box = (id) => bx.find((b) => b.id === id);
  const a = box('a'), b = box('b');
  const ctxOf = (boxes, sel, zoom, extra = {}) => ({ boxes, sel, scope: null, zoom, handles: [], multi: false, ...extra });
  const pt = (type, x, y, id = 1, extra = {}) => ({ id, x, y, type, shift: false, time: 0, primary: id === 1, ...extra });

  // dall'esempio del piano
  const gs = Ge.createGesture(), ctx = ctxOf(bx, [], 4);
  eq(gs.down(pt('touch', a.x + 1, a.y + 1), ctx).kind, 'select', 'tocco su elemento: seleziona');
  eq(gs.move(pt('touch', a.x + 1 + 1, a.y + 1), ctx), null, 'tocco: 1 px non sposta (soglia 6 px)');
  eq(gs.move(pt('touch', a.x + 1 + 10, a.y + 1), ctx).kind, 'move', 'poi sposta');
  const g2 = Ge.createGesture(); g2.down(pt('touch', 240, 5, 1), ctxOf(bx, [], 4));
  eq(g2.move(pt('touch', 230, 5, 1), ctxOf(bx, [], 4)).kind, 'pan', 'un dito sul vuoto: sposta la vista');
  const g3 = Ge.createGesture(); g3.down(pt('touch', 100, 50, 1), ctx); g3.down(pt('touch', 120, 50, 2), ctx);
  eq(g3.move(pt('touch', 130, 50, 2), ctx).kind, 'zoom', 'due dita: pizzica');
  const g4 = Ge.createGesture(); eq(g4.down(pt('mouse', 240, 5), ctx), null); eq(g4.move(pt('mouse', 200, 40), ctx).kind, 'marquee', 'mouse sul vuoto: riquadro');
  const withMulti = (g, id) => g.down(pt('touch', box(id).x + 1, box(id).y + 1), ctxOf(bx, ['a'], 4, { multi: true }));
  check(withMulti(Ge.createGesture(), b.id).add, 'Seleziona più: aggiunge');
  same(withMulti(Ge.createGesture(), b.id).ids, ['a', 'b'], 'Seleziona più: a + b');
  same(withMulti(Ge.createGesture(), 'a').ids, [], 'Seleziona più: toglie chi c\'era');

  // trascinamento: delta cumulativi dall'inizio, in px della scheda (F13)
  { const g = Ge.createGesture(), c = ctxOf(bx, [], 4);
    same(g.down(pt('mouse', 11, 11), c), { kind: 'select', ids: ['a'], add: false }, 'mouse su elemento: seleziona');
    eq(g.move(pt('mouse', 11.25, 11), c), null, 'mouse: 1 px dello schermo non sposta (soglia 2 px)');
    same(g.move(pt('mouse', 12, 11), c), { kind: 'move', dx: 1, dy: 0, done: false }, 'mouse: 4 px = 1 punto');
    same(g.move(pt('mouse', 15.5, 13), c), { kind: 'move', dx: 4.5, dy: 2, done: false }, 'cumulativo, frazioni ammesse');
    same(g.up(pt('mouse', 16, 13), c), { kind: 'move', dx: 5, dy: 2, done: true }, 'rilascio: done');
    eq(g.move(pt('mouse', 30, 30), c), null, 'dopo il rilascio niente'); }
  { const g = Ge.createGesture(), c = ctxOf(bx, ['a', 'b'], 4);
    same(g.down(pt('mouse', 61, 11), c).ids, ['a', 'b'], 'su un elemento già scelto: la selezione resta (si trascinano tutti)');
    same(g.up(pt('mouse', 61, 11), c), { kind: 'select', ids: ['b'], add: false }, 'clic senza trascinare: solo quello'); }
  { const g = Ge.createGesture(), c = ctxOf(bx, ['a'], 4);
    same(g.down(pt('mouse', 61, 11, 1, { shift: true }), c), { kind: 'select', ids: ['a', 'b'], add: true }, 'Maiusc aggiunge');
    const g5 = Ge.createGesture();
    same(g5.down(pt('mouse', 11, 11, 1, { shift: true }), c), { kind: 'select', ids: [], add: true }, 'Maiusc toglie');
    eq(g5.move(pt('mouse', 30, 30), c), null, 'tolto con Maiusc: non si trascina'); }
  // bloccati: mai colpiti sulla tela, neanche con lo slop del tocco
  { const g = Ge.createGesture(), c = ctxOf(bx, [], 4);
    eq(g.down(pt('mouse', 225, 15), c), null, 'bloccato: niente selezione col mouse');
    eq(g.move(pt('mouse', 200, 40), c).kind, 'marquee', '…si comincia un riquadro');
    const t = Ge.createGesture(); eq(t.down(pt('touch', 225, 15), c), null, 'bloccato: niente col tocco');
    eq(t.up(pt('touch', 225, 15), c).kind, 'deselect', 'tocco breve sul vuoto: deseleziona'); }
  // tocco: slop = 22/zoom
  { const g = Ge.createGesture();
    eq(g.down(pt('touch', 40 + 4, 15), ctxOf(bx, [], 4))?.kind, 'select', 'tocco vicino (4 punti, slop 5.5): colpito');
    const g6 = Ge.createGesture(); eq(g6.down(pt('touch', 40 + 4, 15), ctxOf(bx, [], 8)), null, 'a zoom 8 lo slop è 2.75: mancato'); }
  // maniglie: prima degli elementi; centri e raggi in px CSS (= punti · zoom)
  { const hs = S.handles(a, ['se', 'e'], 4, false), c = ctxOf(bx, ['a'], 4, { handles: hs });
    const g = Ge.createGesture();
    eq(g.down(pt('mouse', 40, 30), c), null, 'su una maniglia: nessuna selezione');
    same(g.move(pt('mouse', 42, 31), c), { kind: 'resize', id: 'a', handle: 'se', dx: 2, dy: 1, done: false }, 'maniglia: ridimensiona');
    same(g.up(pt('mouse', 43, 31), c), { kind: 'resize', id: 'a', handle: 'se', dx: 3, dy: 1, done: true }, 'maniglia: rilascio');
    const t = Ge.createGesture(), ct = ctxOf(bx, ['a'], 4, { handles: S.handles(a, ['se'], 4, true) });
    eq(t.down(pt('touch', 40 + 5, 30 + 2), ct), null, 'maniglia col tocco: 22 px di raggio');
    eq(t.move(pt('touch', 45.5, 32), ct), null, 'maniglia col tocco: soglia di 6 px');
    eq(t.move(pt('touch', 48, 32), ct).kind, 'resize', 'poi ridimensiona');
    // dentro l'elemento scelto la maniglia vince solo vicino al suo centro: un elemento sottile resta trascinabile
    const thin = { id: 'bar', type: 'rect', x: 120, y: 70, w: 126, h: 8, parentId: null, depth: 0, locked: false, hidden: false };
    const cth = ctxOf([thin], ['bar'], 1, { handles: S.handles(thin, S.handleNames('rect'), 1, true) });
    const tb = Ge.createGesture(); eq(tb.down(pt('touch', 140, 74), cth).kind, 'select', 'tocco dentro una barra sottile: elemento, non maniglia');
    eq(tb.move(pt('touch', 160, 84), cth).kind, 'move', 'e si sposta');
    const tc = Ge.createGesture(); eq(tc.down(pt('touch', 122, 74), cth), null, 'entro 8/3 px dal centro della maniglia: maniglia');
    const te = Ge.createGesture(); eq(te.down(pt('touch', 124, 74), cth).kind, 'select', 'a 4 px dentro una barra di 8: elemento');
    const td = Ge.createGesture(); eq(td.down(pt('touch', 110, 74), cth), null, 'fuori dall\'elemento: tutta l\'area di 22 px'); }
  // riquadro: rettangolo normalizzato; un clic sul vuoto deseleziona
  { const g = Ge.createGesture(), c = ctxOf(bx, ['a'], 4);
    g.down(pt('mouse', 90, 40), c);
    same(g.move(pt('mouse', 5, 5), c), { kind: 'marquee', rect: { x: 5, y: 5, w: 85, h: 35 }, add: false, done: false }, 'riquadro verso l\'alto a sinistra');
    same(g.up(pt('mouse', 5, 5), c), { kind: 'marquee', rect: { x: 5, y: 5, w: 85, h: 35 }, add: false, done: true }, 'riquadro: fine');
    const g7 = Ge.createGesture(); g7.down(pt('mouse', 90, 40), c); same(g7.up(pt('mouse', 90, 40), c), { kind: 'deselect' }, 'clic sul vuoto: deseleziona');
    const g8 = Ge.createGesture(); g8.down(pt('mouse', 90, 40, 1, { shift: true }), c); eq(g8.up(pt('mouse', 90, 40, 1, { shift: true }), c), null, 'Maiusc+clic sul vuoto: la selezione resta');
    const g9 = Ge.createGesture(); g9.down(pt('mouse', 90, 40, 1, { shift: true }), c); eq(g9.move(pt('mouse', 5, 5, 1, { shift: true }), c).add, true, 'Maiusc+riquadro aggiunge'); }
  // tocco: pan in px dello schermo, cumulativo; sotto i 6 px è un tocco
  { const g = Ge.createGesture(), c = ctxOf(bx, ['a'], 4);
    g.down(pt('touch', 240, 100), c);
    eq(g.move(pt('touch', 241, 100), c), null, 'pan: 4 px non bastano');
    same(g.move(pt('touch', 230, 102), c), { kind: 'pan', dx: -40, dy: 8 }, 'pan: px dello schermo');
    same(g.up(pt('touch', 230, 102), c), { kind: 'pan', dx: -40, dy: 8, done: true }, 'pan: fine'); }
  // pizzico: rapporto delle distanze, centri di partenza e attuale; il secondo dito annulla lo spostamento
  { const g = Ge.createGesture(), c = ctxOf(bx, [], 4);
    g.down(pt('touch', 11, 11, 1), c);
    eq(g.move(pt('touch', 14, 11, 1), c).kind, 'move', 'un dito sposta a');
    same(g.down(pt('touch', 30, 11, 2), c), { kind: 'move', dx: 0, dy: 0, done: true }, 'il secondo dito annulla lo spostamento');
    same(g.move(pt('touch', 46, 11, 2), c), { kind: 'zoom', factor: 2, cx: 30, cy: 11, x0: 22, y0: 11, done: false }, 'zoom: fattore 2 (16 → 32), centro da 22 a 30');
    same(g.up(pt('touch', 46, 11, 2), c), { kind: 'zoom', factor: 2, cx: 30, cy: 11, x0: 22, y0: 11, done: true }, 'zoom: un dito alzato chiude');
    eq(g.move(pt('touch', 20, 20, 1), c), null, 'dopo il pizzico il dito rimasto non fa niente');
    eq(g.up(pt('touch', 20, 20, 1), c), null, 'e alzandolo nemmeno'); }
  { const g = Ge.createGesture(), c = ctxOf(bx, ['a'], 4, { handles: S.handles(a, ['se'], 4, true) });
    g.down(pt('touch', 40, 30, 1), c); g.move(pt('touch', 45, 35, 1), c);
    same(g.down(pt('touch', 60, 60, 2), c), { kind: 'resize', id: 'a', handle: 'se', dx: 0, dy: 0, done: true }, 'il secondo dito annulla il ridimensionamento'); }
  // R1-I2: barra 8×100 toccata al centro: si sposta (raggio interno = min(8, lato minore/3) px CSS)
  for (const z of [1, 4 / 3, 4]) {
    const tb = { id: 'tb', type: 'rect', x: 50, y: 40, w: 100, h: 8, parentId: null, depth: 0, locked: false, hidden: false };
    const c = ctxOf([tb], ['tb'], z, { handles: S.handles(tb, S.handleNames('rect'), z, true) });
    const g = Ge.createGesture();
    eq(g.down(pt('touch', 100, 44), c)?.kind, 'select', 'barra 8×100 al centro (zoom ' + z + '): elemento');
    eq(g.move(pt('touch', 100 + 20 / z, 44), c)?.kind, 'move', 'barra 8×100 al centro: si sposta');
    const h = Ge.createGesture(); eq(h.down(pt('touch', 150 + 3 / z, 44), c), null, 'fuori dal lato destro: maniglia est');
  }
  // R1-I1: un gesto rimasto aperto (pointerup perso) non blocca il successivo; il primario nuovo riparte da capo
  { const g = Ge.createGesture(), c = ctxOf(bx, [], 4);
    g.down(pt('mouse', 11, 11), c); eq(g.move(pt('mouse', 20, 11), c).kind, 'move', 'gesto vecchio in corso');
    same(g.down(pt('mouse', 61, 11, 1, { primary: true }), c), { kind: 'select', ids: ['b'], add: false }, 'nuovo primario: nuovo gesto');
    same(g.move(pt('mouse', 63, 11), c), { kind: 'move', dx: 2, dy: 0, done: false }, 'nuovo gesto: scarto dal suo inizio');
    same(g.up(pt('mouse', 63, 11), c), { kind: 'move', dx: 2, dy: 0, done: true }, 'nuovo gesto: chiuso');
    const t = Ge.createGesture(); t.down(pt('touch', 240, 100, 5, { primary: true }), c); t.move(pt('touch', 230, 100, 5), c);
    eq(t.down(pt('touch', 11, 11, 7, { primary: true }), c).kind, 'select', 'tocco: un nuovo primo dito riparte (non è un pizzico)');
    eq(t.move(pt('touch', 20, 11, 7), c).kind, 'move', 'e sposta');
    const p2 = Ge.createGesture(); p2.down(pt('touch', 11, 11, 1), c);
    eq(p2.down(pt('touch', 30, 11, 2, { primary: false }), c), null, 'secondo dito non primario: pizzico'); eq(p2.live(), true, 'gesto vivo');
    p2.cancel(); eq(p2.live(), false, 'dopo cancel: niente gesto'); }
  // annulla (pointercancel)
  { const g = Ge.createGesture(), c = ctxOf(bx, [], 4);
    g.down(pt('mouse', 11, 11), c); g.move(pt('mouse', 20, 11), c);
    same(g.cancel(), { kind: 'move', dx: 0, dy: 0, done: true }, 'cancel: spostamento a zero');
    eq(g.cancel(), null, 'cancel a vuoto');
    const m = Ge.createGesture(); m.down(pt('mouse', 90, 40), c); m.move(pt('mouse', 5, 5), c);
    same(m.cancel(), { kind: 'marquee', rect: null, add: false, done: true }, 'cancel: riquadro tolto'); }
  // doppio clic / doppio tocco entro 300 ms su un gruppo: entra
  { const c = ctxOf(bx, [], 4), g = Ge.createGesture();
    eq(g.down(pt('mouse', 101, 61, 1, { time: 1000 }), c).ids[0], 'gr', 'clic sul figlio: il gruppo');
    g.up(pt('mouse', 101, 61, 1, { time: 1080 }), c);
    same(g.down(pt('mouse', 101, 61, 1, { time: 1200 }), c), { kind: 'enter', id: 'gr' }, 'doppio clic: entra nel gruppo');
    g.up(pt('mouse', 101, 61, 1, { time: 1250 }), c);
    const h = Ge.createGesture(); h.down(pt('touch', 101, 61, 1, { time: 0 }), c); h.up(pt('touch', 101, 61, 1, { time: 50 }), c);
    eq(h.down(pt('touch', 101, 61, 1, { time: 400 }), c).kind, 'select', 'oltre 300 ms: niente');
    const r = Ge.createGesture(); r.down(pt('mouse', 11, 11, 1, { time: 0 }), c); r.up(pt('mouse', 11, 11, 1, { time: 10 }), c);
    eq(r.down(pt('mouse', 11, 11, 1, { time: 100 }), c).kind, 'select', 'doppio clic su un rettangolo: niente');
    const sc = ctxOf(bx, [], 4, { scope: 'gr' }), q = Ge.createGesture();
    eq(q.down(pt('mouse', 101, 61), sc).ids[0], 'c1', 'dentro il gruppo: il figlio'); }

  // tastiera: campi di testo, ripetizione, tasti del browser
  { const ev = (key, extra = {}) => ({ key, target: { tagName: 'DIV' }, ...extra });
    eq(K.canvasKey(ev('ArrowLeft', { target: { tagName: 'INPUT' } }), true), null, 'freccia in un campo: al campo');
    eq(K.canvasKey(ev('Backspace', { target: { tagName: 'TEXTAREA' } }), true), null, 'Backspace in un textarea');
    eq(K.canvasKey(ev('ArrowDown', { target: { tagName: 'SELECT' } }), true), null, 'select');
    eq(K.canvasKey(ev('Delete', { target: { tagName: 'DIV', isContentEditable: true } }), true), null, 'contenteditable');
    eq(K.canvasKey(ev('Enter', { target: { tagName: 'BUTTON' } }), true), null, 'Invio su un pulsante: al pulsante');
    eq(K.canvasKey(ev('r', { metaKey: true }), true), null, '⌘R al browser'); eq(K.canvasKey(ev('w', { metaKey: true }), true), null, '⌘W al browser');
    eq(K.canvasKey(ev('r', { ctrlKey: true }), false), null, 'Ctrl+R al browser'); eq(K.canvasKey(ev('t', { metaKey: true }), true), null, '⌘T');
    eq(K.canvasKey(ev('ArrowLeft', { repeat: true }), true).type, 'nudge', 'freccia tenuta: si ripete');
    eq(K.canvasKey(ev('d', { metaKey: true, repeat: true }), true).repeat, true, '⌘D tenuto: non si ripete');
    eq(K.canvasKey(ev('Delete', { repeat: true }), true).repeat, true, 'Canc tenuto: non si ripete');
    eq(K.canvasKey(ev('z', { metaKey: true }), true).type, 'undo', '⌘Z');
    eq(K.canvasKey({ key: 'z', metaKey: true }, true).type, 'undo', 'senza target');
    const doc = (open) => ({ querySelector: (q) => (open && q === 'dialog[open]' ? {} : null) });
    eq(K.canvasKey(ev('Delete', { target: { tagName: 'BUTTON', ownerDocument: doc(true) } }), true), null, 'dialogo aperto: Canc resta al dialogo');
    eq(K.canvasKey(ev('ArrowLeft', { target: { tagName: 'BODY', ownerDocument: doc(true) } }), true), null, 'dialogo aperto: frecce ignorate');
    eq(K.canvasKey(ev('ArrowLeft', { target: { tagName: 'BODY', ownerDocument: doc(false) } }), true).type, 'nudge', 'nessun dialogo: freccia'); }

  // azioni: spostamento come un passo, base del gesto, aggancio arrotondato, bloccati, rifiuti
  const api = fakeApi(['prova']);
  api.getTheme = async () => ({ theme: JSON.parse(JSON.stringify(th)), base: 1000 });
  const st = createStore(initialState()), A = createActions(st, api, fakeTimers());
  await A.init(); A.setLoc({ screen: 'home', variant: 0 });
  const el = (id) => O.findEl(st.get().hist.present, st.get().loc, id)?.el;
  const steps = () => st.get().hist.past.length;
  st.set({ snap: { grid: 1, others: false } });
  A.select(['a']);
  const p0 = steps();
  for (const d of [1, 2, 3, 4.4]) A.moveSel(d, 0, 'drag1');
  eq(el('a').x, 14, 'trascinamento: delta cumulativo dalla base (4.4 → 4)');
  eq(steps(), p0 + 1, 'trascinamento = un passo'); A.endGesture();
  A.moveSel(3, 0, 'drag2'); A.moveSel(0, 0, 'drag2'); A.endGesture();
  eq(el('a').x, 14, 'tornato al punto di partenza'); eq(steps(), p0 + 1, 'tornato al punto di partenza: nessun passo');
  A.moveSel(5, 0, 'drag3'); A.endGesture(); A.moveSel(5, 0, 'drag4'); A.endGesture();
  eq(el('a').x, 24, 'due trascinamenti di fila: basi diverse'); eq(steps(), p0 + 3, 'due trascinamenti: due passi');
  A.undo(); A.undo(); eq(el('a').x, 14, 'annulla due volte');
  // aggancio: i mezzi punti (centri dispari) si arrotondano
  st.set({ snap: { grid: 1, others: true } });
  A.select(['b']);
  { const base = st.get().hist.present, boxes = S.layout(base, st.get().loc);
    const r = G.snapMove(boxes, ['b'], 143.2, 0, st.get().snap);
    eq(r.dx, 143.5, 'snapMove: mezzo punto (centro di o = 203.5)');
    A.moveSel(143.2, 0, 'drag5'); A.endGesture();
    eq(el('b').x, 60 + Math.round(143.5), 'aggancio: mezzo punto arrotondato a intero'); }
  A.undo();
  // tastiera: frecce 1/8 senza aggancio, una tenuta = un passo
  A.select(['b']); const bx0 = el('b').x, p1 = steps();
  check(A.key({ type: 'nudge', dx: 1, dy: 0 }), 'freccia: gestita');
  A.key({ type: 'nudge', dx: 8, dy: 0 }); A.endGesture();
  eq(el('b').x, bx0 + 9, 'frecce: 1 + 8, mai agganciate'); eq(steps(), p1 + 1, 'freccia tenuta: un passo (chiuso al keyup)');
  A.select([]); eq(A.key({ type: 'nudge', dx: 1, dy: 0 }), false, 'freccia senza selezione: non gestita (niente preventDefault)');
  eq(A.key({ type: 'copy' }), false, '⌘C senza selezione: al browser');
  // bloccato scelto da Livelli: non si sposta, motivo visibile
  A.select(['lk']); same(st.get().sel, ['lk'], 'bloccato selezionabile (da Livelli)');
  A.key({ type: 'nudge', dx: 1, dy: 0 }); eq(el('lk').x, 220, 'bloccato: fermo'); eq(st.get().notice, 'L\'elemento è bloccato.', 'bloccato: motivo');
  A.moveSel(10, 0, 'drag6'); A.endGesture(); eq(el('lk').x, 220, 'bloccato: il trascinamento non lo sposta');
  // ridimensionamento: rettangolo libero, arrotondato; a scala: scala intera nei limiti
  A.select(['a']);
  A.resizeSel('a', { x: 14, y: 10, w: 40.4, h: 25.6 }, 'rs1'); A.resizeSel('a', { x: 14, y: 10, w: 41, h: 26 }, 'rs1'); A.endGesture();
  same([el('a').w, el('a').h], [41, 26], 'ridimensiona: w/h liberi'); eq(steps(), p1 + 2, 'ridimensiona: un passo');
  A.resizeSel('a', { x: 14, y: 10, w: 60, h: 30 }, 'rs1b'); A.resizeSel('a', { x: 14, y: 10, w: 41, h: 26 }, 'rs1b'); A.endGesture();
  same([el('a').w, el('a').h], [41, 26], 'ridimensiona e torna: misura di partenza'); eq(steps(), p1 + 2, 'ridimensiona e torna: nessun passo');
  eq(st.get().notice, '', 'ridimensionare senza cambiare: nessun avviso');
  A.select(['m']);
  A.resizeSel('m', { x: 150, y: 60, w: 80, h: 80 }, 'rs2'); A.endGesture();
  eq(el('m').scale, 3, 'mascotte: scala intera ((80/28 + 80/25) / 2 ≈ 3)'); eq(el('m').w, undefined, 'mascotte: niente w libera');
  A.resizeSel('m', { x: 150, y: 60, w: 400, h: 400 }, 'rs3'); A.endGesture(); eq(el('m').scale, 4, 'mascotte: al massimo 4');
  A.resizeSel('m', { x: 150, y: 60, w: 3, h: 3 }, 'rs4'); A.endGesture(); eq(el('m').scale, 1, 'mascotte: almeno 1');
  same(A.scaledRect('m', { x: 150, y: 60, w: 60, h: 40 }), { x: 150, y: 60, w: 56, h: 50 }, 'riquadro a scala intera');
  // cancella, duplica, copia/incolla, raggruppa/separa, seleziona tutto, entra/esci
  A.select(['a', 'b']); A.duplicateSel(); eq(st.get().sel.length, 2, 'duplica: selezionati i nuovi');
  const dup = st.get().sel; A.deleteSel(); eq(st.get().sel.length, 0, 'cancella: selezione vuota (pruneIds)');
  check(dup.every((id) => !el(id)), 'cancellati');
  A.select(['a']); A.copySel(); A.paste(); eq(st.get().sel.length, 1, 'incolla: selezionato il nuovo'); check(st.get().sel[0] !== 'a', 'incolla: id nuovo');
  A.deleteSel();
  A.select(['a', 'b']); A.groupSel(); const gid = st.get().sel[0]; eq(el(gid).type, 'group', 'raggruppa'); 
  A.ungroupSel(); same(st.get().sel.slice().sort(), ['a', 'b'], 'separa: selezionati i figli');
  A.undo(); A.undo();
  A.selectAll(); same(st.get().sel.slice().sort(), ['a', 'b', 'gr', 'm', 'o'], 'seleziona tutto: niente bloccati');
  A.select([]); A.enter('gr'); eq(st.get().scope, 'gr', 'entra nel gruppo'); eq(st.get().sel.length, 0, 'entrando: selezione vuota');
  A.selectAll(); same(st.get().sel, ['c1', 'c2'], 'seleziona tutto dentro il gruppo');
  A.enter('a'); eq(st.get().scope, 'gr', 'un rettangolo non si apre');
  check(A.key({ type: 'escape' }), 'Esc gestito'); eq(st.get().scope, null, 'Esc esce dal gruppo'); same(st.get().sel, ['gr'], 'uscendo: il gruppo selezionato');
  A.key({ type: 'escape' }); same(st.get().sel, [], 'Esc di nuovo: deseleziona');
  A.select(['gr']); A.key({ type: 'enter' }); eq(st.get().scope, 'gr', 'Invio entra'); A.exitScope();
  // rifiuti spiegati: la riga di una lista non si cancella tutta
  A.select([]); A.deleteSel(); eq(st.get().notice, '', 'cancella senza selezione: niente');
  { const t2 = mk([{ id: 'ls', type: 'list', x: 0, y: 0, w: 100, h: 40, rowH: 20, gap: 2, row: [{ id: 'r1', type: 'text', x: 0, y: 0, w: 50, text: '{riga.nome}', font: { family: 'ui', size: 12, weight: 600 } }] }]);
    api.getTheme = async () => ({ theme: t2, base: 2000 }); const s2 = createStore(initialState()), A2 = createActions(s2, api, fakeTimers());
    await A2.init(); A2.setLoc({ screen: 'home', variant: 0 }); A2.enter('ls'); A2.select(['r1']); A2.deleteSel();
    eq(s2.get().notice, 'La riga di una lista deve avere almeno un elemento.', 'cancella: motivo del rifiuto'); }
  // zoom del pizzico: intero alla fine
  A.setZoom(2); A.pinchZoom(2, 1.7); eq(st.get().zoom, 3, 'pizzico: 2 · 1.7 → 3'); A.pinchZoom(3, 0.1); eq(st.get().zoom, 1, 'pizzico: minimo 1');
  A.toggleMulti(); eq(st.get().touchMulti, true, 'Seleziona più acceso'); A.toggleMulti(); eq(st.get().touchMulti, false, 'spento');
}

// 9. Aggiungi e Livelli: righe, bersagli, azioni
{
  const L = { screen: 'home', variant: 0 };
  const rect = (id, x, y, w, h, extra = {}) => ({ id, type: 'rect', x, y, w, h, ...extra });
  const mk = (elements) => ({ id: 'g', name: 'g', format: 1, screens: { home: { variants: [{ when: 'always', elements }] } } });
  const th = mk([rect('a', 10, 10, 30, 20), rect('b', 60, 10, 20, 20, { visibleIf: [{ perm: true }] }),
    { id: 'gr', type: 'group', x: 100, y: 60, name: 'Gruppo mio', children: [rect('c1', 0, 0, 10, 10), rect('c2', 20, 0, 10, 10)] }, rect('z', 200, 100, 7, 5, { locked: true, hidden: true })]);
  const rows = Ly.layerRows(th, L, ['c1'], null);
  same(rows.map((r) => r.id), ['z', 'gr', 'c2', 'c1', 'b', 'a'], 'livelli: l\'ultimo disegnato in cima, i figli sotto il gruppo');
  same(rows.map((r) => r.depth), [0, 0, 1, 1, 0, 0], 'livelli: profondità');
  eq(rows[1].label, 'Gruppo mio', 'livelli: il nome'); eq(rows[1].container, true, 'livelli: il gruppo è un contenitore');
  check(rows[0].hidden && rows[0].locked && !rows[5].hidden, 'livelli: nascosto e bloccato'); check(rows[3].selected && !rows[2].selected, 'livelli: selezionato');
  eq(rows.find((r) => r.id === 'b').hiddenByData, true, 'visibleIf perm coi dati senza permesso: nascosto con questi dati');
  eq(Ly.layerRows(th, L, [], null, { perm: true }).find((r) => r.id === 'b').hiddenByData, false, 'con il permesso: visibile');
  eq(rows.find((r) => r.id === 'a').hiddenByData, false, 'senza condizioni: visibile');
  same(Ly.layerRows(th, { screen: 'perm', variant: 0 }, [], null), [], 'Permessi: nessun livello');
  // bersagli del trascinamento
  const a = rows.find((r) => r.id === 'a');
  same(Ly.dropTarget(rows, 'z', null, 0.5), { parentId: null, index: 0, mark: 'end', overId: null }, 'in fondo: sotto tutto');
  eq(Ly.dropTarget(rows, 'a', null), null, 'già in fondo: niente');
  same(Ly.dropTarget(rows, 'a', 'z', 0.1)?.index, 3, 'sopra la prima riga: in cima (dopo la rimozione)');
  same(Ly.dropTarget(rows, 'a', 'gr', 0.5), { parentId: 'gr', index: 2, mark: 'into', overId: 'gr' }, 'sul gruppo: dentro, in cima');
  eq(Ly.dropTarget(rows, 'a', 'gr', 0.1).mark, 'above', 'bordo alto del gruppo: prima'); eq(Ly.dropTarget(rows, 'a', 'gr', 0.9).mark, 'below', 'bordo basso del gruppo: dopo');
  eq(Ly.dropTarget(rows, 'gr', 'c1', 0.5), null, 'un gruppo non entra nei suoi figli');
  eq(Ly.dropTarget(rows, 'gr', 'gr', 0.5), null, 'né in sé');
  same(Ly.dropTarget(rows, 'c1', 'a', 0.9), { parentId: null, index: 0, mark: 'below', overId: 'a' }, 'sotto «a»: fuori dal gruppo, in fondo');
  same(Ly.stepTarget(rows, 'a', 'up'), { parentId: null, index: 1 }, 'Su: indice + 1'); eq(Ly.stepTarget(rows, 'a', 'down'), null, 'Giù in fondo: niente');
  eq(Ly.stepTarget(rows, 'z', 'up'), null, 'Su in cima: niente'); same(Ly.stepTarget(rows, 'c2', 'down'), { parentId: 'gr', index: 0 }, 'Giù dentro il gruppo');
  eq(Ly.cleanName('x'.repeat(40)).length, 32, 'nome: 32 byte'); eq(Ly.cleanName('è'.repeat(20)), 'è'.repeat(16), 'nome: taglio su un confine di carattere');
  eq(Ly.cleanName('  a\nb '), 'a b', 'nome: niente a capo');

  // azioni
  const api = fakeApi(['prova']); api.getTheme = async () => ({ theme: JSON.parse(JSON.stringify(th)), base: 1000 });
  const st = createStore(initialState()), A = createActions(st, api, fakeTimers());
  await A.init({ want: 'prova' }); A.setLoc({ screen: 'home', variant: 0 });
  const el = (id) => O.findEl(st.get().hist.present, L, id)?.el;
  A.setDrawer('left');
  check(A.add('text'), 'Aggiungi › Testo'); const nid = st.get().sel[0];
  eq(el(nid).type, 'text', 'aggiunto un testo'); same(st.get().sel, [nid], 'il nuovo è selezionato'); eq(st.get().drawer, null, 'cassetto chiuso');
  { const b = S.layout(st.get().hist.present, L).find((x) => x.id === nid);
    check(b.x >= 0 && b.y >= 0 && b.x + b.w <= 250 && b.y + b.h <= 122, 'dentro lo schermo');
    check(Math.abs(b.x + b.w / 2 - 125) <= 1 && Math.abs(b.y + b.h / 2 - 61) <= 1, 'al centro: ' + JSON.stringify(b)); }
  eq(Ly.layerRows(st.get().hist.present, L, [], null)[0].id, nid, 'in cima ai livelli');
  A.add('rect', { x: 1000, y: -50 }); { const b = S.layout(st.get().hist.present, L).find((x) => x.id === st.get().sel[0]);
    check(b.x + b.w === 250 && b.y === 0, 'centro fuori: riportato dentro lo schermo ' + JSON.stringify(b)); }
  A.enter('gr'); A.add('text', { x: 125, y: 61 }); const inGr = st.get().sel[0];
  eq(O.findEl(st.get().hist.present, L, inGr).parentId, 'gr', 'con scope: il testo nasce nel gruppo');
  { const b = S.layout(st.get().hist.present, L).find((x) => x.id === inGr); check(Math.abs(b.x + b.w / 2 - 125) <= 1, 'dentro il gruppo: centro in coordinate della scheda'); }
  A.exitScope(); eq(A.add('nonesiste'), false, 'tipo sconosciuto: niente');
  // nascondi, blocca, rinomina
  A.toggleHidden('a'); eq(el('a').hidden, true, 'nascosto'); A.toggleHidden('a'); check(!('hidden' in el('a')), 'mostrato: la chiave sparisce');
  A.toggleLocked('a'); eq(el('a').locked, true, 'bloccato');
  A.pickLayer('a'); same(st.get().sel, ['a'], 'Livelli sceglie anche i bloccati');
  const before = st.get().hist.present; A.moveSel(5, 0); eq(st.get().hist.present, before, 'bloccato: moveSel non sposta');
  eq(st.get().notice, 'L\'elemento è bloccato.', 'bloccato: il motivo');
  A.toggleLocked('a'); check(!('locked' in el('a')), 'sbloccato');
  A.renameEl('a', 'x'.repeat(40)); eq(el('a').name.length, 32, 'rinomina: 32 byte'); A.renameEl('a', ''); check(!('name' in el('a')), 'rinomina vuota toglie name');
  A.renameEl('a', 'Bordo'); eq(el('a').name, 'Bordo', 'rinomina');
  // ordine
  const order = () => Ly.layerRows(st.get().hist.present, L, [], null).filter((r) => r.parentId === null).map((r) => r.id);
  const o0 = order(); const top = o0[0];
  check(A.reorderEl(top, null, 0), 'in fondo'); eq(order()[order().length - 1], top, 'ordine di disegno: ora il primo disegnato');
  eq(A.reorderEl(top, null, 0), false, 'stessa posizione: niente');
  A.undo(); eq(order()[0], top, 'annulla ripristina l\'ordine');
  A.reorderEl('a', 'gr', 0); eq(O.findEl(st.get().hist.present, L, 'a').parentId, 'gr', 'dentro il gruppo'); A.undo();
  // gruppo bloccato: né spostare dentro né aggiungere dentro
  A.toggleLocked('gr'); { const h = st.get().hist.present;
    eq(A.reorderEl('a', 'gr', 0), false, 'dentro un gruppo bloccato: rifiutato'); eq(st.get().notice, 'L\'elemento è bloccato.', 'gruppo bloccato: il motivo'); eq(st.get().hist.present, h, 'gruppo bloccato: tema invariato');
    A.enter('gr'); eq(A.add('text'), false, 'aggiungere in un gruppo bloccato: rifiutato'); eq(st.get().notice, 'L\'elemento è bloccato.', 'aggiungere: il motivo'); eq(st.get().hist.present, h, 'aggiungere: tema invariato');
    A.exitScope(); A.toggleLocked('gr'); }
  // fuori da un gruppo bloccato: rifiutato
  A.toggleLocked('gr'); { const h = st.get().hist.present;
    eq(A.reorderEl('c1', null, 0), false, 'fuori da un gruppo bloccato: rifiutato'); eq(st.get().notice, 'L\'elemento è bloccato.', 'fuori dal gruppo bloccato: il motivo'); eq(st.get().hist.present, h, 'fuori dal gruppo bloccato: tema invariato');
    A.toggleLocked('gr'); check(A.reorderEl('c1', null, 0), 'sbloccato: si può'); A.undo(); }
  // nome invariato: nessun passo
  { const n = st.get().hist.past.length; A.renameEl('a', 'Bordo'); eq(st.get().hist.past.length, n, 'stesso nome: niente passo'); }
  // rifiuti: un elemento con {riga.*} non esce dalla lista
  { const t2 = mk([{ id: 'ls', type: 'list', x: 0, y: 0, w: 100, h: 40, rowH: 20, gap: 2, row: [{ id: 'r1', type: 'text', x: 0, y: 0, w: 50, text: '{riga.nome}', font: { family: 'ui', size: 12, weight: 600 } }] }, rect('q', 0, 50, 5, 5)]);
    api.getTheme = async () => ({ theme: t2, base: 2000 }); const s2 = createStore(initialState()), A2 = createActions(s2, api, fakeTimers());
    await A2.init(); A2.setLoc({ screen: 'home', variant: 0 });
    eq(A2.reorderEl('r1', null, 0), false, 'riga fuori dalla lista: rifiutato'); check(/lista/.test(s2.get().notice), 'riga fuori dalla lista: motivo ' + s2.get().notice);
    A2.select(['q']); check(A2.add('list'), 'aggiunge una lista'); }
}

// 10. Proprietà: campi per tipo, condizioni, valori, allinea, distribuisci, aggancio
{
  const L = { screen: 'home', variant: 0 };
  const keys = (t) => C.fieldsFor(t).map((f) => f.key);
  const fld = (t, k) => C.fieldsFor(t).find((f) => f.key === k);
  { const sc = fld('mascot', 'scale'); check(sc && sc.kind === 'int' && sc.min === 1 && sc.max === 4, 'mascotte: scala 1–4'); }
  same(fld('mascot', 'pose').options.map((o) => o.value), ['auto', 'working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'], 'mascotte: auto + le 9 pose');
  check(!keys('text').includes('h') && keys('text').includes('w') && keys('text').includes('lines'), 'testo: niente h, w e righe sì');
  for (const t of ['mascot', 'battery', 'wifi', 'stateIcon', 'sym']) check(!keys(t).includes('w') && !keys(t).includes('h') && keys(t).includes('scale'), t + ': scala invece di w/h');
  same(keys('group'), ['x', 'y', 'visibleIf'], 'gruppo: posizione e condizioni');
  for (const t of C.TYPES.map((x) => x.type)) check(keys(t).includes('invert') && keys(t).includes('visibleIf') && keys(t)[0] === 'x', t + ': invert e visibleIf');
  eq(fld('rect', 'border').max, 8, 'rettangolo: bordo 0–8'); eq(fld('list', 'rowH').max, 122, 'lista: rowH 1–122');
  same(C.fieldsFor('boh'), [], 'tipo sconosciuto: nessun campo');
  { const f1 = C.fieldsFor('text'); f1[0].label = 'x'; eq(C.fieldsFor('text')[0].label, 'X', 'fieldsFor: copie'); }
  // caratteri: 22, uno per combinazione di f_registry.h (la stessa tabella dei glifi)
  eq(C.FONT_CHOICES.length, 22, '22 caratteri'); eq(new Set(C.FONT_CHOICES.map((f) => f.key)).size, 22, 'caratteri: chiavi uniche');
  check(C.FONT_CHOICES.some((f) => f.label === 'Chakra Petch 44 · 700'), 'Chakra Petch 44 · 700');
  // condizioni
  same(C.CONDS.map((c) => c.label), ['Permesso in attesa', 'Batteria sotto N %', 'In carica', 'Notte', 'PC collegato', 'Giornata di lavoro attiva',
    'Gioco della mascotte acceso', 'Sessioni da/a', 'Stato della sessione', 'Lampeggio'], 'condizioni: etichette');
  same(C.CONDS.find((c) => c.key === 'state').options.map((o) => o.label), ['LAVORA', 'PERMESSO', 'ATTENDE TE', 'ERRORE', 'RIORDINA', 'FINITO', 'IN PAUSA'], 'stato: etichette di INFO');
  same(C.CONDS.find((c) => c.key === 'blink').options.map((o) => o.label), ['fotogramma dispari', 'pari'], 'lampeggio');
  same(C.CONDS.find((c) => c.key === 'perm').options.map((o) => o.label), ['sì', 'no'], 'booleane: sì/no');
  for (const c of C.CONDS) check(C.condOk(C.condDefault(c.key)), 'condizione di partenza valida: ' + c.key);
  check(!C.condOk({ battBelow: 0 }) && !C.condOk({ sessions: { min: 7 } }) && !C.condOk({ perm: 1 }) && !C.condOk({ perm: true, night: true }), 'condizioni non valide');
  eq(C.condCount([{ sessions: { min: 1, max: 3 } }, { perm: true }]), 3, 'sessions con min e max contano due');
  // valori: riga.* solo dentro una lista
  check(!C.valueChoices(false).some((v) => v.name.startsWith('riga.')), 'fuori da una lista: niente riga.*');
  check(C.valueChoices(true).some((v) => v.name === 'riga.nome'), 'in una riga: riga.nome');
  eq(C.valueChoices(false).find((v) => v.name === 'oggi.costo').example, VALUE_GLYPHS['oggi.costo'].example, 'esempio di VALUE_GLYPHS');
  eq(C.templateProblem('{oggi.costo}', false), null, 'modello valido');
  check(/Graffa/.test(C.templateProblem('{oggi.co', false)), 'graffa non chiusa'); check(/sconosciuto/.test(C.templateProblem('{boh}', false)), 'valore sconosciuto');
  check(/lista/.test(C.templateProblem('{riga.nome}', false)), 'riga fuori dalla lista'); eq(C.templateProblem('{riga.nome}', true), null, 'riga nella lista');
  check(/Filtro/.test(C.templateProblem('{ora|grande}', false)), 'filtro sconosciuto'); eq(C.templateProblem('{{a}}', false), null, 'graffe doppie');
  check(C.templateProblem('', false), 'vuoto'); check(C.templateProblem('{ora}'.repeat(17), false), 'oltre 16 pezzi');

  // azioni
  const txt = (id, x, extra = {}) => ({ id, type: 'text', x, y: 10, w: 40, text: 'a', font: { family: 'ui', size: 12, weight: 600 }, ...extra });
  const th = { id: 'g', name: 'g', format: 'calamaio-theme', version: 1, screen: '250x122', screens: { home: { variants: [{ when: 'always', elements: [
    txt('t1', 10), txt('t2', 60), txt('t3', 150, { y: 40 }), { id: 'm', type: 'mascot', x: 0, y: 60, scale: 1 },
    { id: 'gr', type: 'group', x: 100, y: 80, visibleIf: [{ perm: true }, { sessions: { min: 1, max: 6 } }], children: [txt('c', 0)] },
    { id: 'ls', type: 'list', x: 0, y: 0, w: 100, h: 40, rowH: 20, row: [txt('r', 0, { text: '{riga.nome}' })] },
    txt('lk', 200, { locked: true })] }] } } };
  const api = fakeApi(['prova']); api.getTheme = async () => ({ theme: JSON.parse(JSON.stringify(th)), base: 1000 });
  const st = createStore(initialState()), A = createActions(st, api, fakeTimers());
  await A.init({ want: 'prova' }); A.setLoc({ screen: 'home', variant: 0 });
  const el = (id) => O.findEl(st.get().hist.present, L, id)?.el;
  const steps = () => st.get().hist.past.length;
  // cinque tasti di fila = un passo
  { const n = steps(); for (const v of ['1', '12', '123', '12', '1']) A.setProp('t1', 'w', v);
    eq(el('t1').w, 1, 'larghezza scritta'); eq(steps(), n + 1, 'setProp: 5 tasti di fila, un passo');
    A.endGesture(); A.setProp('t1', 'w', '30'); eq(steps(), n + 2, 'dopo l\'uscita dal campo: passo nuovo');
    A.undo(); A.undo(); eq(el('t1').w, 40, 'annulla: due passi'); }
  A.setProp('m', 'scale', 9); eq(el('m').scale, 4, 'scala riportata nei limiti'); A.endGesture();
  eq(A.setProp('m', 'scale', 'abc'), false, 'non un numero: niente'); check(/numero/.test(st.get().notice), 'non un numero: il motivo');
  A.setProp('m', 'pose', 'tired'); eq(el('m').pose, 'tired', 'posa'); A.setProp('m', 'pose', 'auto'); check(!('pose' in el('m')), 'posa auto = chiave tolta');
  eq(A.setProp('m', 'pose', 'vola'), false, 'posa sconosciuta: niente');
  eq(A.setProp('m', 'ref', 'row'), false, 'ref riga fuori da una lista: rifiutato'); check(/lista/.test(st.get().notice), 'ref riga: il motivo');
  A.setProp('m', 'accessory', false); eq(el('m').accessory, false, 'accessorio spento'); A.setProp('m', 'accessory', true); check(!('accessory' in el('m')), 'accessorio acceso = predefinito');
  A.setProp('t1', 'font', 'ui:44:700'); same(el('t1').font, { family: 'ui', size: 44, weight: 700 }, 'carattere');
  eq(A.setProp('t1', 'font', 'ui:45:700'), false, 'carattere inesistente: niente');
  // testo: {riga.*} fuori dalla lista no, dentro sì; graffa aperta no
  { const h0 = st.get().hist.present;
    eq(A.setProp('t1', 'text', 'x {riga.nome}'), false, 'riga.* fuori dalla lista: rifiutato'); check(/lista/.test(st.get().notice), 'il motivo');
    eq(A.setProp('t1', 'text', '{oggi.co'), false, 'graffa aperta: non entra'); eq(st.get().hist.present, h0, 'tema invariato'); }
  check(A.setProp('r', 'text', '{riga.nome} {riga.stato}'), 'riga.* dentro la lista');
  check(A.setProp('t1', 'text', '{oggi.costo}'), 'modello valido'); eq(st.get().notice, '', 'nessun avviso dopo una modifica riuscita');
  eq(A.setProp('t1', 'h', 10), false, 'testo: niente h');
  // bloccato: rifiutato col motivo
  eq(A.setProp('lk', 'x', 5), false, 'bloccato: niente'); eq(st.get().notice, 'L\'elemento è bloccato.', 'bloccato: il motivo');
  // condizioni: al massimo 8 con quelle dei gruppi
  check(A.setProp('t2', 'visibleIf', [{ night: false }, { battBelow: 30 }]), 'condizioni'); same(el('t2').visibleIf, [{ night: false }, { battBelow: 30 }], 'condizioni scritte');
  A.setProp('t2', 'visibleIf', []); check(!('visibleIf' in el('t2')), 'nessuna condizione = chiave tolta');
  eq(A.setProp('t2', 'visibleIf', [{ battBelow: 300 }]), false, 'condizione fuori dai limiti: niente');
  const six = [{ perm: true }, { night: true }, { pc: true }, { game: false }, { charging: true }, { workday: true }];
  check(A.setProp('c', 'visibleIf', six.slice(0, 5)), 'nel gruppo: 5 + 3 = 8');
  eq(A.setProp('c', 'visibleIf', six), false, 'nel gruppo: 6 + 3 = 9 rifiutato'); check(/8 condizioni/.test(st.get().notice), 'oltre 8: il motivo ' + st.get().notice);
  check(A.setProp('r', 'visibleIf', [...six, { blink: 'odd' }, { state: 'error' }]), 'riga: 8 (la lista non passa le sue)');
  // R1-I1: il gruppo passa le sue a ogni figlio: figlio con 6, gruppo con 5 = 11, rifiutato
  check(A.setProp('gr', 'visibleIf', []), 'gruppo senza condizioni'); check(A.setProp('c', 'visibleIf', six), 'figlio con 6');
  { const h0 = st.get().hist.present;
    eq(A.setProp('gr', 'visibleIf', six.slice(0, 5)), false, 'gruppo con 5 sopra un figlio da 6: rifiutato');
    eq(st.get().hist.present, h0, 'tema invariato'); check(/8 condizioni.*dentro il gruppo/.test(st.get().notice), 'il motivo: ' + st.get().notice); }
  check(A.setProp('gr', 'visibleIf', [{ perm: true }, { night: true }]), 'gruppo con 2 sopra un figlio da 6: 8, ammesso');
  check(!A.setProp('m', 'scale', '1e2') && /numero/.test(st.get().notice), 'solo interi decimali: 1e2 no');
  check(!A.setProp('m', 'scale', '0x2'), '0x2 no'); check(A.setProp('m', 'scale', '−2') || O.findEl(st.get().hist.present, L, 'm').el.scale === 1, '− tipografico ammesso'); A.endGesture();
  // variante
  A.setVariantProp({ ref: 'urgent' }); eq(st.get().hist.present.screens.home.variants[0].ref, 'urgent', 'ref della variante');
  A.setVariantProp({ when: 'many' }); eq(st.get().hist.present.screens.home.variants[0].when, 'many', 'quando della variante');
  A.undo(); A.undo();
  // allinea a sinistra tre elementi: un passo; di nuovo = nessun passo
  A.select(['t1', 't2', 't3']);
  { const n = steps(); check(A.alignSel('left'), 'allinea a sinistra');
    same(['t1', 't2', 't3'].map((id) => el(id).x), [10, 10, 10], 'tutti a x = 10'); eq(steps(), n + 1, 'allinea: un passo');
    eq(A.alignSel('left'), false, 'già allineati: niente'); eq(steps(), n + 1, 'nessun passo vuoto'); eq(st.get().notice, 'Già allineati.', 'il motivo'); A.undo(); }
  // al centro con una larghezza dispari: scarti interi
  A.setProp('t2', 'w', 41); A.endGesture(); A.select(['t1', 't2']); A.alignSel('hcenter');
  check(Number.isInteger(el('t1').x) && Number.isInteger(el('t2').x), 'centro: interi'); A.undo(); A.undo();
  // un bloccato non si allinea; solo bloccati = motivo
  A.select(['t1', 'lk']); A.alignSel('right'); eq(el('lk').x, 200, 'il bloccato resta'); A.undo();
  A.select(['lk']); eq(A.alignSel('left'), false, 'solo bloccati: niente'); eq(st.get().notice, 'L\'elemento è bloccato.', 'solo bloccati: il motivo');
  // distribuisci
  A.select(['t1', 't2']); eq(A.distributeSel('h'), false, 'distribuire due: niente'); check(/almeno 3/.test(st.get().notice), 'distribuire: il motivo');
  A.select(['t1', 't2', 't3']); A.setProp('t2', 'x', 20); A.endGesture(); check(A.distributeSel('h'), 'distribuisci in orizzontale');
  { const xs = ['t1', 't2', 't3'].map((id) => el(id).x); eq(xs[1] - (xs[0] + 40), xs[2] - (xs[1] + 40), 'spazi uguali ' + xs.join(',')); }
  // aggancio
  same(st.get().snap, { grid: 1, others: true }, 'aggancio iniziale');
  A.setSnap({ grid: 8 }); same(st.get().snap, { grid: 8, others: true }, 'griglia 8'); A.setSnap({ grid: 3 }); eq(st.get().snap.grid, 8, 'griglia 3: ignorata');
  A.setSnap({ others: false }); eq(st.get().snap.others, false, 'senza aggancio agli elementi');
}

// 11. barra in alto: salvataggio automatico, conflitti, temi, varianti, dati di prova, animazione, avvisi
{
  const api = autoApi(), timers = fakeTimers();
  const st = createStore(initialState()); const A = createActions(st, api, timers);
  await A.init({ want: 'prova' });
  const addRect = (t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 10, 10));
  const nChecks = () => api.calls.filter((c) => c === 'check').length;
  const homeCount = () => st.get().hist.present.screens.home.variants.reduce((n, v) => n + v.elements.length, 0);
  eq(st.get().test.sessions, 1, 'dati di prova iniziali');
  eq(saveText(st.get().save), 'Salvato', 'testo: salvato');

  // salvataggio automatico: controllo dopo 300 ms, PUT dopo 1 500 ms senza modifiche
  const c0 = nChecks();
  A.commit(addRect); eq(st.get().save.state, 'pending', 'in sospeso'); eq(saveText(st.get().save), 'Modifiche in sospeso…', 'testo: in sospeso');
  timers.advance(299); await flush(); eq(nChecks(), c0, 'nessun controllo prima di 300 ms');
  timers.advance(1); await flush(); eq(nChecks(), c0 + 1, 'controllo dopo 300 ms');
  check(st.get().check && st.get().check._for === st.get().hist.present, 'controllo del tema attuale');
  timers.advance(1199); await flush(); eq(api.puts.length, 0, 'niente PUT prima di 1 500 ms');
  timers.advance(1); await flush(); eq(api.puts.length, 1, 'salvataggio dopo 1 500 ms'); eq(st.get().save.state, 'saved', 'salvato');
  eq(api.puts[0].base, 1000, 'il PUT porta la base letta');
  // modifiche di fila: il conto riparte a ogni modifica
  A.commit(addRect); timers.advance(1000); A.commit(addRect); timers.advance(1000); await flush();
  eq(api.puts.length, 1, 'il timer riparte a ogni modifica');
  timers.advance(500); await flush(); eq(api.puts.length, 2, 'poi un PUT solo'); eq(api.puts[1].base, 1007, 'con la base del PUT di prima');
  // controllo scartato se nel frattempo il tema è cambiato
  { let open; api.checkGate = new Promise((r) => { open = r; });
    A.commit(addRect); const t1 = st.get().hist.present; timers.advance(300); A.commit(addRect);
    api.checkGate = null; open(); await flush();
    check(st.get().check._for !== t1, 'controllo vecchio scartato');
    timers.advance(300); await flush(); check(st.get().check._for === st.get().hist.present, 'controllo nuovo tenuto');
    timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'poi salvato'); }
  const nPut = () => api.puts.length;
  // tema non valido: non si salva
  { const n = nPut();
    api.nextCheck = { problems: [{ path: 'screens.home', msg: 'x' }] }; A.commit(addRect); timers.advance(1500); await flush();
    eq(st.get().save.state, 'invalid', 'autosave: tema non valido non si salva'); eq(nPut(), n, 'nessun put');
    eq(saveText(st.get().save), 'Non salvato: 1 problema', 'testo: un problema');
    api.nextCheck = { problems: [{ path: 'a', msg: 'x' }, { path: 'b', msg: 'y' }] }; A.commit(addRect); timers.advance(1500); await flush();
    eq(saveText(st.get().save), 'Non salvato: 2 problemi', 'testo: più problemi');
    A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'tornato valido: salvato'); }
  // controllo giù, decide il PUT (400): i problemi finiscono in check e si vedono nell'elenco (I2)
  { const shown = () => { const s = st.get(); return W.liveWarnings(s.hist.present, s.loc, D.testModel(), S.layout(s.hist.present, s.loc), s.check).filter((w) => w.kind === 'service' || w.kind === 'elsewhere'); };
    api.checkDown = true;
    api.putProblems = [{ path: 'screens.oggi', msg: 'schermata oltre 4 096 byte' }, { path: 'name', msg: 'nome da 1 a 24 byte' }];
    api.nextPut = 'invalid'; A.commit(addRect); timers.advance(1500); await flush();
    eq(saveText(st.get().save), 'Non salvato: 2 problemi', '400: due problemi');
    same(st.get().check?.problems, api.putProblems, '400: problemi copiati nel controllo');
    eq(shown().length, 2, '400: tutti e due nell\'elenco');
    // 400 senza elenco: il messaggio diventa il problema, l'elenco non è mai vuoto
    api.putProblems = []; api.nextPut = 'invalid'; A.commit(addRect); timers.advance(1500); await flush();
    eq(saveText(st.get().save), 'Non salvato: 1 problema', '400 senza elenco: un problema');
    eq(shown().length, 1, '400 senza elenco: una riga');
    api.checkDown = false; api.putProblems = null;
    A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'controllo di nuovo su: salvato'); }
  // 409 → conflitto, mai sovrascritto in silenzio
  { const n = nPut();
    api.nextPut = 'conflict'; A.commit(addRect); timers.advance(1500); await flush();
    eq(st.get().save.state, 'conflict', 'autosave: 409 → conflict'); eq(saveText(st.get().save), 'Cambiato altrove', 'testo: conflitto');
    eq(api.file('prova').theme.name, 'Dal telefono', 'la versione del telefono resta');
    A.commit(addRect); timers.advance(5000); await flush();
    eq(st.get().save.state, 'conflict', 'conflitto: altre modifiche non lo sbloccano'); eq(nPut(), n, 'conflitto: nessun PUT');
    check(!(await A.saveNow()), 'conflitto: neanche saveNow');
    check(A.unsaved(), 'conflitto: la pagina chiede prima di uscire');
    const mine = st.get().hist.present;
    await A.reload(); eq(st.get().save.state, 'saved', 'ricarica'); check(!Hi.canUndo(st.get().hist), 'ricarica azzera la cronologia del tema');
    eq(st.get().hist.present.name, 'Dal telefono', 'ricarica: versione del servizio');
    eq(A.localChanges('prova')?.present, mine, 'ricarica: le mie modifiche messe da parte'); check(st.get().save.local, 'ricarica: offerte nella barra');
    // riprendi le mie: un passo sopra la versione del servizio, salvato con la base nuova
    const base = st.get().save.base;
    check(A.restoreLocal(), 'riprendi le mie modifiche'); eq(st.get().hist.present, mine, 'riprese');
    check(Hi.canUndo(st.get().hist), 'riprese: si possono annullare'); eq(A.localChanges('prova'), null, 'riprese: non più da parte'); check(!st.get().save.local, 'riprese: tolte dalla barra');
    timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'riprese e salvate'); eq(api.puts.at(-1).base, base, 'con la base della ricarica'); }
  // sovrascrivi: prima si vede cosa è cambiato, poi PUT con la base nuova
  { api.nextPut = 'conflict'; A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'conflict', 'secondo conflitto');
    const d = await A.conflictDiff();
    same(d.changes, ['il nome («Dal telefono» → «prova»)', 'Home'], 'cosa cambia sovrascrivendo');
    eq(d.base, api.file('prova').updated, 'base del file vivo');
    // cambiato ancora fra la lettura e la conferma: di nuovo conflitto, niente sovrascritto
    api.touch('prova', { name: 'Ancora' }); const n = nPut();
    eq(await A.overwrite(d.base), false, 'base vecchia: rifiutato'); eq(st.get().save.state, 'conflict', 'di nuovo conflitto'); eq(api.file('prova').theme.name, 'Ancora', 'niente sovrascritto');
    const d2 = await A.conflictDiff(); eq(await A.overwrite(d2.base), true, 'sovrascritto'); eq(nPut(), n + 1, 'un PUT');
    eq(api.puts.at(-1).base, d2.base, 'con la base appena letta'); eq(st.get().save.state, 'saved', 'sovrascritto: salvato');
    eq(api.puts.at(-1).fresh, true, 'sovrascrivi: fresh (versione nuova, quella dell\'altro resta in cronologia)');
    check(api.puts.slice(0, -1).every((x) => !x.fresh), 'il salvataggio automatico non manda fresh');
    eq(api.file('prova').theme.name, 'prova', 'sul servizio la mia versione'); check(Hi.canUndo(st.get().hist), 'sovrascrivere non tocca la cronologia');
    eq(await A.overwrite(d2.base), false, 'senza conflitto sovrascrivi non fa niente'); }
  // scarta le modifiche messe da parte
  { api.nextPut = 'conflict'; A.commit(addRect); timers.advance(1500); await flush(); await A.reload();
    check(A.localChanges('prova'), 'da parte'); check(A.dropLocal(), 'scarta'); eq(A.localChanges('prova'), null, 'scartate'); check(!st.get().save.local, 'scartate: tolte dalla barra'); }
  same(themeChanges({ name: 'a', screens: { home: { variants: [] } } }, { name: 'a', screens: { home: { variants: [] } } }), [], 'nessuna differenza');
  same(themeChanges({ name: 'a', screens: { home: { variants: [] } } }, { name: 'a', screens: { sleep: { variants: [] } } }), ['Home', 'Riposo'], 'schermate tolte e aggiunte');
  // errore di rete → error, poi riprova
  api.nextPut = 'network'; A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'error', 'autosave: errore di rete → error');
  eq(saveText(st.get().save), 'Non salvato: Failed to fetch · Riprovo…', 'testo: errore, si riprova da soli');
  await A.retrySave(); eq(st.get().save.state, 'saved', 'poi riprova');
  // durante un gesto non si salva
  A.moveSel(5, 0, 'move:x', { snap: false }); timers.advance(5000); await flush(); eq(st.get().save.state, 'pending', 'mai durante un trascinamento');
  A.endGesture(); timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'dopo il gesto sì');
  // annulla e ripeti si salvano come le altre modifiche
  A.undo(); eq(st.get().save.state, 'pending', 'annulla: in sospeso'); timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'annulla: salvato');
  A.redo(); timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'ripeti: salvato'); check(!A.unsaved(), 'salvato: si esce senza domande');
  // esporta
  { const x = A.exportTheme(); eq(x.filename, 'prova.theme.json', 'esporta'); same(JSON.parse(x.text), st.get().hist.present, 'esporta: il tema attuale'); }

  // temi: nuovo, rinomina, duplica, elimina, importa
  { const id = await A.newTheme('Notte fonda');
    eq(id, 'notte-fonda', 'nuovo: id dal nome'); eq(st.get().themeId, 'notte-fonda', 'nuovo: aperto'); check(st.get().themes.some((t) => t.id === 'notte-fonda'), 'nuovo: in elenco');
    eq(st.get().save.state, 'saved', 'nuovo: salvato'); check(!Hi.canUndo(st.get().hist), 'nuovo: cronologia vuota');
    eq(await A.newTheme('Notte fonda'), 'notte-fonda-2', 'nuovo: stesso nome, id libero');
    const long = await A.newTheme('Città di notte lunghissima e piena');
    check(new TextEncoder().encode(st.get().hist.present.name).length <= 24, 'nuovo: nome al massimo 24 byte (F22)'); check(long && long.length <= 24, 'nuovo: id al massimo 24');
    await A.openTheme('notte-fonda');
    check(A.renameTheme('Notte piena'), 'rinomina'); eq(st.get().hist.present.name, 'Notte piena', 'rinomina: nome'); eq(st.get().hist.present.id, 'notte-fonda', 'rinomina: l\'id resta');
    A.renameTheme('àèìòù'.repeat(6)); check(new TextEncoder().encode(st.get().hist.present.name).length <= 24, 'rinomina: 24 byte (F22)'); A.undo();
    eq(A.renameTheme('   '), false, 'rinomina: vuoto non cambia');
    timers.advance(1500); await flush(); eq(api.file('notte-fonda').theme.name, 'Notte piena', 'rinomina: salvata');
    // duplica: passa da import, l'id lo sceglie il servizio
    A.commit(addRect);
    const dup = await A.duplicateTheme();
    check(api.calls.includes('import'), 'duplica: import'); eq(dup, 'notte-fonda-3', 'duplica: id del servizio'); eq(st.get().themeId, dup, 'duplica: aperto');
    eq(st.get().hist.present.name, 'Notte piena copia', 'duplica: nome');
    eq(api.file('notte-fonda').theme.screens.home.variants[0].elements.length, 1, 'duplica: prima ha salvato l\'originale');
    eq(api.file(dup).theme.screens.home.variants[0].elements.length, 1, 'duplica: con le modifiche');
    check(A.renameTheme('Ventiquattro byte giusti'), 'nome di 24 byte'); const d2 = await A.duplicateTheme();
    eq(st.get().hist.present.name, 'Ventiquattro byte copia', 'duplica: nome tagliato a 24 byte, «copia» resta');
    // elimina: solo dopo la conferma della pagina; niente PUT dopo
    A.commit(addRect); const n = nPut();
    check(await A.deleteTheme(), 'elimina'); check(!api.file(d2), 'elimina: tolto dal servizio'); check(!st.get().themes.some((t) => t.id === d2), 'elimina: tolto dall\'elenco');
    eq(st.get().themeId, st.get().themes[0].id, 'elimina: aperto il primo che resta');
    timers.advance(5000); await flush(); check(!api.puts.slice(n).some((p) => p.id === d2), 'elimina: nessun salvataggio del tema eliminato');
    // importa
    const text = JSON.stringify({ ...fixture('prova'), id: 'prova', name: 'Importato' });
    eq(await A.importTheme(text), 'prova-2', 'importa: id libero dal servizio'); eq(st.get().hist.present.name, 'Importato', 'importa: aperto');
    eq(await A.importTheme('{nope'), null, 'importa: non valido'); check(/Non importato/.test(st.get().notice), 'importa: motivo ' + st.get().notice); }
  // eliminare l'ultimo tema: stato vuoto
  { const a1 = autoApi(['solo']), s1 = createStore(initialState()), A1 = createActions(s1, a1, fakeTimers()); await A1.init();
    check(await A1.deleteTheme(), 'elimina l\'ultimo'); eq(s1.get().themeId, null, 'nessun tema aperto'); eq(s1.get().hist, null, 'niente cronologia'); eq(s1.get().save.state, 'saved', 'niente da salvare'); }

  // varianti
  { await A.openTheme('notte-fonda'); A.setLoc({ screen: 'home', variant: 0 });
    eq(A.removeVariant(), false, 'togli l\'unica variante dell\'unica schermata: no'); check(/almeno una schermata/.test(st.get().notice), 'togli: motivo');
    check(A.addVariant('many'), 'aggiungi variante'); eq(st.get().loc.variant, 1, 'aggiungi: aperta la nuova'); eq(st.get().hist.present.screens.home.variants[1].when, 'many', 'aggiungi: quando');
    check(A.moveVariant(-1), 'su'); eq(st.get().loc.variant, 0, 'su: la segue'); eq(st.get().hist.present.screens.home.variants[0].when, 'many', 'su: ordine');
    eq(A.moveVariant(-1), false, 'su dalla prima: no');
    check(A.moveVariant(1), 'giù'); eq(st.get().loc.variant, 1, 'giù: la segue');
    check(A.removeVariant(), 'togli'); eq(st.get().loc.variant, 0, 'togli: resta una valida'); eq(st.get().hist.present.screens.home.variants.length, 1, 'togli: una in meno');
    A.setLoc({ screen: 'sleep' }); check(A.addVariant(), 'variante in una schermata nuova'); eq(st.get().loc.variant, 0, 'schermata nuova: la prima');
    check(A.removeVariant(), 'togli l\'unica di una schermata (ne restano altre)'); check(!st.get().hist.present.screens.sleep, 'schermata tornata di serie');
    A.setLoc({ screen: 'home', variant: 0 }); for (let i = 0; i < 7; i++) A.addVariant();
    eq(A.addVariant(), false, 'al massimo 8 varianti'); check(/8 varianti/.test(st.get().notice), 'massimo: motivo'); }
  // schermata di serie: registro vuoto in questa fase
  { same(STOCK, {}, 'registro delle schermate di serie vuoto'); eq(A.hasStock('home'), false, 'niente schermata di serie per ora');
    const h0 = st.get().hist; eq(A.fromStock(), false, 'parti dalla schermata di serie: spento'); eq(st.get().hist, h0, 'tema invariato');
    const s9 = createStore(initialState()), A9 = createActions(s9, autoApi(['prova']), { ...fakeTimers(), stock: { sleep: { variants: [{ when: 'always', elements: [{ type: 'rect', x: 1, y: 1, w: 5, h: 5 }] }] } } });
    await A9.init(); A9.setLoc({ screen: 'sleep' }); check(A9.hasStock('sleep'), 'registro iniettato'); check(A9.fromStock(), 'dalla schermata di serie');
    eq(s9.get().hist.present.screens.sleep.variants.length, 1, 'schermata sostituita'); check(s9.get().hist.present.screens.sleep.variants[0].elements[0].id, 'id nuovi'); }

  // dati di prova
  { check(A.setTest({ sessions: 6 }), 'sei sessioni'); eq(st.get().test.sessions, 6, 'sessioni 6');
    eq(A.setTest({ sessions: 3 }), false, 'solo 0, 1, 6'); eq(A.setTest({ sessions: 6 }), false, 'uguale: niente');
    check(A.setTest({ night: true, lowBatt: true }), 'interruttori'); same(st.get().test, { sessions: 6, lowBatt: true, night: true, perm: false, pcOff: false }, 'dati di prova');
    eq(A.setTest({ perm: 'sì' }), false, 'solo booleani');
    A.setTest({ sessions: 1, night: false, lowBatt: false }); }
  // animazione: un timer di 500 ms, fotogramma 0/1 e orologio
  { const t2 = fakeTimers(), s2 = createStore(initialState()), A2 = createActions(s2, autoApi(['prova']), t2); await A2.init();
    A2.togglePlay(); eq(s2.get().playing, true, 'anima');
    t2.advance(499); eq(s2.get().frame, 0, 'non prima di 500 ms'); t2.advance(1); eq(s2.get().frame, 1, 'fotogramma 1'); eq(s2.get().clock, 500, 'orologio 500');
    t2.advance(500); eq(s2.get().frame, 0, 'fotogramma 0'); eq(s2.get().clock, 1000, 'orologio 1000');
    t2.advance(5000); eq(s2.get().clock, 6000, 'un solo timer: 2 fps');
    A2.togglePlay(); same([s2.get().playing, s2.get().frame, s2.get().clock], [false, 0, 0], 'fermo: fotogramma 0 e ora di BASE');
    t2.advance(2000); same([s2.get().frame, s2.get().clock], [0, 0], 'fermo: timer tolto'); }

  // avvisi: misura in byte e scritta sopra la tela
  same(W.sizeInfo(1200), { text: '1 200 / 4 096 byte', warn: false }, 'misura');
  same(W.sizeInfo(3687), { text: '3 687 / 4 096 byte', warn: true }, 'misura oltre il 90 %');
  eq(W.sizeInfo(undefined), null, 'misura assente');
  eq(W.warnCount(0), 'Nessun avviso', 'contatore 0'); eq(W.warnCount(1), '1 avviso', 'contatore 1'); eq(W.warnCount(4), '4 avvisi', 'contatore 4');
  { const prova = O.ensureIds(fixture('prova'));
    eq(W.variantBanner(prova, { screen: 'home', variant: 1 }, D.testModel()), null, 'variante giusta: niente scritta');
    eq(W.variantBanner(prova, { screen: 'home', variant: 0 }, D.testModel()), 'Con questi dati di prova la scheda mostrerebbe la variante 2', 'altra variante');
    const one = { ...prova, screens: { home: { variants: [{ when: 'one', elements: [] }] } } };
    eq(W.variantBanner(one, { screen: 'home', variant: 0 }, D.testModel({ sessions: 6 })), 'Con questi dati di prova la scheda mostrerebbe la schermata di serie', 'nessuna vera: testo del brief');
    eq(W.variantBanner(prova, { screen: 'oggi', variant: 0 }, D.testModel()), null, 'schermata assente: c\'è già il messaggio sulla tela'); }

  // api.js: import e delete
  { const realFetch = globalThis.fetch; const seen = [];
    globalThis.fetch = async (u, o = {}) => { seen.push([o.method || 'GET', u, o.body]);
      if (u === '/api/themes/import') return new Response(JSON.stringify(o.body === 'bad' ? { error: 'x', errors: [{ path: 'id', msg: 'no' }] } : { theme: { id: 'a-2' } }), { status: o.body === 'bad' ? 400 : 201 });
      if (u === '/api/themes/a') return new Response(null, { status: 204 });
      return new Response(JSON.stringify({ error: 'tema sconosciuto' }), { status: 404 }); };
    eq((await importTheme('{"id":"a"}')).id, 'a-2', 'importTheme → Info'); same(seen[0], ['POST', '/api/themes/import', '{"id":"a"}'], 'importTheme: testo così com\'è');
    let e1 = null; try { await importTheme('bad'); } catch (e) { e1 = e; } eq(e1?.name, 'InvalidError', 'importTheme 400'); eq(e1?.problems.length, 1, 'importTheme: problemi');
    await deleteTheme('a'); same(seen.at(-1).slice(0, 2), ['DELETE', '/api/themes/a'], 'deleteTheme');
    let e2 = null; try { await deleteTheme('b'); } catch (e) { e2 = e; } eq(e2?.message, 'tema sconosciuto', 'deleteTheme 404'); eq(e2?.name, 'NotFoundError', '404 → NotFoundError');
    globalThis.fetch = realFetch; }
}

// 11 (seguito). revisione del Task 13: campo di testo aperto, temi lasciati non salvati, pila delle messe da parte,
// riprova automatica, eliminato altrove, PUT in volo (gli scenari di probe13.mjs)
{
  const fresh = async (names = ['altro', 'prova']) => {
    const api = autoApi(names), timers = fakeTimers(), st = createStore(initialState()), A = createActions(st, api, timers);
    await A.init({ want: 'prova' });
    return { api, timers, st, A, addRect: (t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 10, 10)) };
  };
  const els = (st) => st.get().hist.present.screens.home.variants[st.get().loc.variant].elements;
  const countEls = (t) => t.screens.home.variants.reduce((n, v) => n + v.elements.length, 0);
  const puts = (api) => api.calls.filter((c) => c.startsWith('put'));
  // I1: testo scritto col campo ancora attivo: si salva lo stesso dopo la quiete, e al «nascondi» subito
  { const { api, timers, st, A } = await fresh();
    const txt = els(st).find((e) => e.type === 'text');
    A.setProp(txt.id, 'text', 'Ciao'); eq(st.get().hist.open, 'prop:' + txt.id + ':text', 'campo aperto');
    timers.advance(60000); await flush();
    eq(api.puts.length, 1, 'I1: senza blur, 60 s → un PUT'); eq(st.get().save.state, 'saved', 'I1: salvato');
    eq(st.get().hist.open, null, 'I1: il passo del campo si chiude al salvataggio');
    const sv = api.file('prova').theme.screens.home.variants[st.get().loc.variant].elements.find((e) => e.id === txt.id);
    eq(sv.text, 'Ciao', 'I1: sul servizio il testo attuale');
    A.setProp(txt.id, 'text', 'Ciao 2'); A.setProp(txt.id, 'text', 'Ciao 22'); eq(Hi.canUndo(st.get().hist) && st.get().hist.past.length, 2, 'dopo il salvataggio un passo nuovo');
    const nc = api.calls.filter((c) => c === 'check').length;
    eq(await A.saveOnHide(), true, 'I1: il salvataggio al «nascondi» riesce'); eq(st.get().save.state, 'saved', 'nascondi: salvato'); eq(st.get().hist.open, null, 'nascondi: passo chiuso');
    eq(api.calls.filter((c) => c === 'check').length, nc, 'M2: al «nascondi» niente controllo prima del PUT');
    eq(api.file('prova').theme.screens.home.variants[st.get().loc.variant].elements.find((e) => e.id === txt.id).text, 'Ciao 22', 'nascondi: sul servizio');
    // durante un trascinamento il «nascondi» salva lo stato attuale
    A.select([els(st).find((e) => e.type === 'bar').id]); A.moveSel(7, 0, 'move:b', { snap: false });
    eq(await A.saveOnHide(), true, 'nascondi durante un trascinamento'); eq(st.get().save.state, 'saved', 'trascinamento: salvato');
    A.endGesture(); timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'e poi niente da salvare'); }
  // I2: cambio tema con il salvataggio fallito: avviso col nome, tema tra i non salvati, poi riprova
  { const { api, timers, st, A } = await fresh();
    A.commit((t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 10, 10))); api.nextPut = 'network';
    await A.openTheme('altro');
    eq(st.get().themeId, 'altro', 'I2: il cambio avviene'); check(/«prova» non è salvato/.test(st.get().notice), 'I2: avviso col nome: ' + st.get().notice);
    check(A.unsaved(), 'I2: unsaved() guarda tutti i temi'); same(st.get().unsaved.map((x) => x.id), ['prova'], 'I2: elenco dei non salvati');
    eq(st.get().unsaved[0].state, 'error', 'I2: con lo stato');
    timers.advance(5000); await flush(); same(st.get().unsaved, [], 'I2: riprovato da solo dopo 5 s'); check(!A.unsaved(), 'I2: niente più in sospeso');
    await A.openTheme('prova'); A.commit((t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 11, 11))); api.failPuts = 1;
    await A.openTheme('altro'); same(st.get().unsaved.map((x) => x.id), ['prova'], 'di nuovo non salvato');
    eq(await A.retryTheme('prova'), true, 'I2: «Riprova» per l\'altro tema'); same(st.get().unsaved, [], 'riprova: salvato'); }
  // M8: elimina con un PUT in volo: prima finisce il PUT, poi il DELETE, e il tema non torna
  { const { api, timers, st, A, addRect } = await fresh();
    let open; api.gate = new Promise((r) => { open = r; });
    A.commit(addRect); timers.advance(1500); await flush(); await flush();
    eq(st.get().save.state, 'saving', 'PUT in volo');
    const p = A.deleteTheme(); await flush(); check(!api.calls.some((c) => c.startsWith('delete')), 'il DELETE aspetta il PUT');
    api.gate = null; open(); eq(await p, true, 'eliminato'); await flush();
    check(!api.file('prova'), 'il tema non torna'); check(api.calls.indexOf('delete prova') > api.calls.findIndex((c) => c.startsWith('put prova')), 'PUT poi DELETE');
    const n = puts(api).length; timers.advance(600000); await flush(); eq(puts(api).length, n, 'nessun PUT dopo il DELETE'); }
  // M8: modifiche durante un PUT in volo: un secondo PUT con la base nuova, il servizio finisce come la pagina
  { const { api, timers, st, A, addRect } = await fresh();
    let open; api.gate = new Promise((r) => { open = r; });
    A.commit(addRect); timers.advance(1500); await flush(); await flush();
    A.commit(addRect); A.commit(addRect); timers.advance(1500); await flush();
    api.gate = null; open(); for (let i = 0; i < 10; i++) await flush();
    same(api.puts.map((x) => x.base), [1000, 1007], 'due PUT, il secondo con la base del primo');
    eq(countEls(api.file('prova').theme), countEls(st.get().hist.present), 'il servizio come la pagina'); eq(st.get().save.state, 'saved', 'salvato'); }
  // M4: A→B→A con il PUT in volo: la riapertura aspetta il PUT, niente «cambiato altrove» né conflitto dopo
  { const { api, timers, st, A, addRect } = await fresh();
    let open; api.gate = new Promise((r) => { open = r; });
    A.commit(addRect);
    const pB = A.openTheme('altro'); await flush();
    const pA = A.openTheme('prova'); await flush();
    api.gate = null; open(); await pB; await pA; for (let i = 0; i < 5; i++) await flush();
    eq(st.get().themeId, 'prova', 'M4: riaperto'); eq(st.get().notice, '', 'M4: nessun avviso spurio'); eq(st.get().save.base, api.file('prova').updated, 'M4: base aggiornata');
    A.commit(addRect); timers.advance(1500); for (let i = 0; i < 5; i++) await flush(); eq(st.get().save.state, 'saved', 'M4: la modifica dopo si salva (niente 409)'); }
  // M1: riprova automatica dopo 5 s, 15 s, 60 s
  { const { api, timers, st, A, addRect } = await fresh();
    api.failPuts = 3; A.commit(addRect); timers.advance(1500); await flush();
    eq(st.get().save.state, 'error', 'M1: errore'); check(st.get().save.retry && /Riprovo…/.test(saveText(st.get().save)), 'M1: «Riprovo…»');
    const tries = () => api.calls.filter((c) => c.endsWith('rete giù')).length;
    timers.advance(4999); await flush(); eq(tries(), 1, 'M1: non prima di 5 s');
    timers.advance(1); await flush(); eq(tries(), 2, 'M1: dopo 5 s');
    timers.advance(14999); await flush(); eq(tries(), 2, 'M1: non prima di 15 s'); timers.advance(1); await flush(); eq(tries(), 3, 'M1: dopo 15 s');
    timers.advance(59999); await flush(); eq(api.puts.length, 0, 'M1: non prima di 60 s'); timers.advance(1); await flush();
    eq(api.puts.length, 1, 'M1: dopo 60 s riesce'); eq(st.get().save.state, 'saved', 'M1: salvato'); check(!st.get().save.retry, 'M1: niente più riprova');
    // il «nascondi» salva anche dopo un errore
    api.nextPut = 'network'; A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'error', 'errore');
    eq(await A.saveOnHide(), true, 'M1: il «nascondi» riprova subito'); eq(st.get().save.state, 'saved', 'nascondi: salvato');
    const n = api.calls.length; timers.advance(600000); await flush(); eq(api.calls.length, n, 'nessuna riprova dopo il salvataggio'); }
  // I3: le versioni messe da parte non si perdono: pila delle ultime 5, contano per l'uscita, si riprendono una alla volta
  { const { api, timers, st, A, addRect } = await fresh();
    const conflict = async () => { api.nextPut = 'conflict'; A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'conflict', 'conflitto'); return st.get().hist.present; };
    const first = await conflict(); await A.reload();
    check(A.unsaved(), 'I3: le messe da parte contano per l\'uscita'); eq(st.get().save.state, 'saved', 'I3: stato salvato');
    const second = await conflict(); await A.reload();
    same(A.localStack('prova').map((h) => h.present === first || h.present === second), [true, true], 'I3: due versioni da parte');
    eq(A.localStack('prova')[0].present, first, 'I3: la prima resta'); eq(A.localChanges('prova').present, second, 'I3: in cima la più recente'); eq(st.get().save.local, 2, 'I3: la barra ne mostra 2');
    for (let i = 0; i < 4; i++) { await conflict(); await A.reload(); }
    eq(A.localStack('prova').length, 5, 'I3: al massimo 5'); check(/la più vecchia è stata scartata/.test(st.get().notice), 'I3: lo si dice: ' + st.get().notice);
    check(A.restoreLocal(), 'riprendi la più recente'); eq(A.localStack('prova').length, 4, 'una in meno');
    timers.advance(1500); await flush(); eq(st.get().save.state, 'saved', 'ripresa e salvata');
    while (A.localStack('prova').length) A.dropLocal();
    check(!A.unsaved(), 'scartate tutte: niente in sospeso'); eq(st.get().save.local, 0, 'barra vuota'); }
  // M3: eliminato altrove: «Eliminato altrove», niente riprova infinita, «Salva come nuovo» via import
  { const { api, timers, st, A, addRect } = await fresh();
    api.forget('prova'); A.commit(addRect); timers.advance(1500); await flush();
    eq(st.get().save.state, 'gone', 'M3: 404 → eliminato altrove'); eq(saveText(st.get().save), 'Eliminato altrove', 'M3: testo');
    const n = puts(api).length; A.commit(addRect); timers.advance(600000); await flush(); eq(puts(api).length, n, 'M3: nessun altro PUT in 10 min');
    eq(await A.retrySave(), false, 'M3: niente «Riprova»'); check(A.unsaved(), 'M3: conta come non salvato');
    const mine = st.get().hist.present;
    eq(await A.saveAsNew(), 'prova', 'M3: salvato come nuovo (import)'); check(api.calls.includes('import'), 'M3: via import');
    eq(st.get().save.state, 'saved', 'M3: poi salvato'); eq(countEls(api.file('prova').theme), countEls(mine), 'M3: con le mie modifiche'); check(!A.unsaved(), 'M3: niente in sospeso'); }
  // N1: «Salva come nuovo» porta con sé le versioni messe da parte; due tocchi = un solo import
  { const { api, timers, st, A, addRect } = await fresh();
    api.nextPut = 'conflict'; A.commit(addRect); timers.advance(1500); await flush(); await A.reload();
    const aside = A.localChanges('prova'); check(aside, 'una versione da parte');
    api.forget('prova'); A.commit(addRect); timers.advance(1500); await flush(); eq(st.get().save.state, 'gone', 'eliminato altrove');
    const [a1, a2] = await Promise.all([A.saveAsNew(), A.saveAsNew()]);
    eq(api.calls.filter((c) => c === 'import').length, 1, 'N1: due tocchi, un solo import'); eq(a1, 'prova', 'N1: il primo salva'); eq(a2, null, 'N1: il secondo no');
    eq(A.localStack(a1).length, 1, 'N1: la pila passa al tema nuovo'); eq(A.localStack(a1)[0], aside, 'N1: la stessa versione');
    eq(st.get().save.local, 1, 'N1: la barra la mostra'); check(A.unsaved(), 'N1: conta per l\'uscita'); }
  // M6: «Rinomina» dice quando ha accorciato
  { const { st, A } = await fresh();
    A.renameTheme('Un nome davvero troppo lungo per il tema'); check(/Nome accorciato a 24 byte/.test(st.get().notice), 'M6: avviso');
    A.renameTheme('Corto'); eq(st.get().notice, '', 'M6: nome corto, nessun avviso'); }
}

// 12. «Invia alla scheda»: schede adatte, salvataggio prima dell'invio, rifiuti, fasi dell'operazione (Task 14)
{
  const B1 = 'calamaio-3f2a';
  const disp = (board, w, h, supported, job = null) => ({ board, device: 'heltec213', w, h, themes: { supported, have: [], cur: null, job } });
  // schede: solo 250×122; senza themes.supported c'è ma non si sceglie
  const bs = boards({ displays: [disp(B1, 250, 122, true), disp('calamaio-9c41', 400, 300, true), disp('calamaio-0001', 250, 122, false)] });
  eq(bs.length, 2, 'boards: scartata la 400×300');
  eq(bs[0].board, B1, 'boards: la prima'); eq(bs[0].ok, true, 'boards: con i temi'); eq(bs[0].why, '', 'boards: nessun motivo');
  check(bs[0].label.includes(B1), 'boards: etichetta col nome della scheda: ' + bs[0].label);
  eq(bs[1].ok, false, 'boards: senza themes.supported'); eq(bs[1].why, 'la scheda non ha ancora detto quali temi ha', 'boards: motivo');
  same(boards(null), [], 'boards: niente stato'); same(boards({ displays: [{ board: 'x' }] }), [], 'boards: senza misure');
  // testi delle fasi
  const T = { attesa: 'In attesa della scheda…', offerta: 'Offerto alla scheda…', scaricato: 'Scaricato, lo attivo…', uso: 'Attivazione…', fatto: 'Tema attivo sulla scheda' };
  for (const [k, v] of Object.entries(T)) eq(sendText({ stage: k }), v, 'testo ' + k);
  eq(sendText({ stage: 'errore', error: 'download non riuscito (HTTP 410)' }), 'Non riuscito: download non riuscito (HTTP 410)', 'testo errore');
  eq(sendText({ stage: 'rifiutato', error: 'Correggi i problemi prima di inviare' }), 'Correggi i problemi prima di inviare', 'testo rifiuto');
  eq(sendText(null), '', 'testo: niente invio');

  // api finta: sendTheme crea l'operazione, ogni GET /api/status la porta avanti di una fase (script)
  const fresh = async (script = ['offerta', 'scaricato', 'uso', 'fatto']) => {
    const api = autoApi(['altro', 'prova']), timers = fakeTimers(), st = createStore(initialState()), A = createActions(st, api, timers);
    let job = null, n = 0; api.statusCalls = 0; api.script = [];
    api.sendTheme = async (board, id) => {
      api.calls.push('send ' + board + ' ' + id);
      if (api.sendFail) throw new Error(api.sendFail);
      job = { id: 'j' + (++n), board, theme: id, stage: 'attesa' }; api.script = [...script];
      return { ...job };
    };
    api.getStatus = async () => {
      api.statusCalls++;
      if (job && api.script.length) { const x = api.script.shift(); Object.assign(job, typeof x === 'string' ? { stage: x } : x); }
      return { displays: [disp(B1, 250, 122, true, job && { ...job })] };
    };
    await A.init({ want: 'prova' });
    return { api, timers, st, A, addRect: (t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 10, 10)) };
  };
  const tick = async (timers, ms = 1000) => { timers.advance(ms); for (let i = 0; i < 4; i++) await flush(); };
  const idx = (api, re) => api.calls.findIndex((c) => re.test(c));

  // modifiche in sospeso: prima il PUT (subito, senza i 1 500 ms), poi l'invio
  { const { api, st, A, addRect } = await fresh();
    A.commit(addRect); eq(st.get().save.state, 'pending', 'in sospeso prima dell\'invio');
    eq(await A.send(B1), true, 'invio accettato');
    const p = idx(api, /^put prova/), s = idx(api, /^send /);
    check(p >= 0 && s > p, 'putTheme prima di sendTheme: ' + api.calls.join(', '));
    eq(api.calls[s], 'send ' + B1 + ' prova', 'sendTheme con scheda e id');
    eq(st.get().save.state, 'saved', 'salvato prima di inviare');
    eq(api.file('prova').theme, api.puts.at(-1).theme, 'sul servizio la versione inviata'); }
  // le fasi dallo stato finto fino a «Tema attivo sulla scheda»; poi niente più richieste
  { const { api, timers, st, A } = await fresh();
    eq(await A.send(B1), true, 'invio senza modifiche');
    eq(idx(api, /^put /), -1, 'niente PUT senza modifiche');
    eq(sendText(st.get().send), 'In attesa della scheda…', 'subito: in attesa'); eq(st.get().send.board, B1, 'send.board');
    const seen = [];
    for (let i = 0; i < 4; i++) { await tick(timers); seen.push(sendText(st.get().send)); }
    same(seen, ['Offerto alla scheda…', 'Scaricato, lo attivo…', 'Attivazione…', 'Tema attivo sulla scheda'], 'fasi');
    const n = api.statusCalls; await tick(timers, 600000); eq(api.statusCalls, n, 'operazione finita: niente più GET /api/status');
    // riga d'invio (M1): modificato dopo l'invio, poi un altro tema aperto
    eq(sendLine(st.get()), 'Tema attivo sulla scheda', 'riga: tema aperto, invariato');
    A.commit((t) => O.addEl(t, st.get().loc, O.elementDefaults('rect', 10, 10)));
    eq(sendLine(st.get()), 'Tema attivo sulla scheda · modificato dopo l\'invio', 'riga: modificato dopo l\'invio');
    A.undo(); eq(sendLine(st.get()), 'Tema attivo sulla scheda', 'riga: annullato, di nuovo uguale');
    await A.openTheme('altro');
    eq(sendLine(st.get()), '«prova»: Tema attivo sulla scheda', 'riga: un altro tema aperto, col nome di quello inviato');
    await A.openTheme('prova');
    eq(sendLine(st.get()), 'Tema attivo sulla scheda', 'riga: di nuovo il tema inviato');
    eq(sendLine({ ...st.get(), send: null }), '', 'riga: niente invio');
    // di nuovo, dopo la fine
    eq(await A.send(B1), true, 'si reinvia dopo la fine'); }
  // errore della scheda
  { const { timers, st, A } = await fresh(['offerta', { stage: 'errore', error: 'download non riuscito (HTTP 410)' }]);
    await A.send(B1); await tick(timers); await tick(timers);
    eq(sendText(st.get().send), 'Non riuscito: download non riuscito (HTTP 410)', 'errore della scheda'); }
  // stato non valido: niente invio
  { const { api, timers, st, A, addRect } = await fresh();
    api.nextCheck = { problems: [{ path: 'screens.home', msg: 'qualcosa' }] };
    A.commit(addRect);
    eq(await A.send(B1), false, 'invalid: rifiutato');
    eq(idx(api, /^send /), -1, 'invalid: niente sendTheme'); eq(idx(api, /^put /), -1, 'invalid: niente PUT');
    eq(st.get().save.state, 'invalid', 'invalid: stato');
    eq(sendText(st.get().send), 'Correggi i problemi prima di inviare', 'invalid: il motivo');
    await tick(timers, 600000); eq(api.statusCalls, 0, 'invalid: nessun controllo dell\'operazione'); }
  // conflitto aperto: niente invio finché non si sceglie
  { const { api, timers, st, A, addRect } = await fresh();
    api.nextPut = 'conflict'; A.commit(addRect); await tick(timers, 1500);
    eq(st.get().save.state, 'conflict', 'conflitto');
    eq(await A.send(B1), false, 'conflitto: rifiutato'); eq(idx(api, /^send /), -1, 'conflitto: niente sendTheme');
    check(/cambiato altrove/.test(sendText(st.get().send)) && /Ricarica/.test(sendText(st.get().send)), 'conflitto: il motivo: ' + sendText(st.get().send)); }
  // salvataggio non riuscito (servizio giù): niente invio di una versione vecchia
  { const { api, st, A, addRect } = await fresh();
    api.failPuts = 1; A.commit(addRect);
    eq(await A.send(B1), false, 'errore di rete: rifiutato'); eq(idx(api, /^send /), -1, 'errore di rete: niente sendTheme');
    check(/non è salvato/.test(sendText(st.get().send)), 'errore di rete: il motivo: ' + sendText(st.get().send)); }
  // la scheda mostrerebbe la schermata di serie (un elemento fuori dai limiti, in qualunque variante): bloccato
  { const { api, st, A } = await fresh();
    A.commit((t) => { const u = JSON.parse(JSON.stringify(t)); u.screens.home.variants.at(-1).elements[0].x = 900; return u; });
    const why = W.sendBlock(st.get().hist.present);
    check(why && why.screen === 'home' && /fuori dai limiti/.test(why.reason), 'sendBlock: ' + JSON.stringify(why));
    eq(await A.send(B1), false, 'schermata di serie: rifiutato'); eq(idx(api, /^send /), -1, 'schermata di serie: niente sendTheme');
    check(/schermata di serie/.test(sendText(st.get().send)) && /Home/.test(sendText(st.get().send)), 'schermata di serie: il motivo: ' + sendText(st.get().send)); }
  eq(W.sendBlock(fixture('prova')), null, 'sendBlock: il tema di prova si invia');
  // una variante falsa coi dati di prova non blocca (la scheda sceglie coi dati veri)
  { const t = fixture('prova'); t.screens.home.variants = [{ ...t.screens.home.variants[0], when: 'none' }];
    eq(W.sendBlock(t), null, 'sendBlock: una variante «nessuna sessione» si invia'); }
  // il servizio rifiuta l'invio (409): «Non riuscito: …» col messaggio
  { const { api, st, A } = await fresh();
    api.sendFail = 'Display non collegato o senza temi.';
    eq(await A.send(B1), false, '409: false');
    eq(sendText(st.get().send), 'Non riuscito: Display non collegato o senza temi.', '409: il messaggio del servizio'); }
  // un tetto ai controlli; niente controlli con la pagina nascosta, ripresa quando torna
  { const { api, timers, st, A } = await fresh([]);
    await A.send(B1);
    await tick(timers, 500); eq(api.statusCalls, 0, 'niente controlli prima di 1 s');
    for (let i = 0; i < 3; i++) await tick(timers); const n1 = api.statusCalls; eq(n1, 3, 'un controllo al secondo');
    A.pauseFollow(); await tick(timers, 60000); eq(api.statusCalls, n1, 'pagina nascosta: nessun controllo');
    A.resumeFollow(); await tick(timers, 10); eq(api.statusCalls, n1 + 1, 'pagina di nuovo visibile: subito un controllo');
    for (let i = 0; i < 200; i++) await tick(timers);
    eq(api.statusCalls, 120, 'tetto ai controlli');
    eq(st.get().send.stage, 'attesa', 'tetto: resta in attesa');
    check(/2 minuti/.test(sendText(st.get().send)), 'tetto: lo dice: ' + sendText(st.get().send));
    const n2 = api.statusCalls; A.resumeFollow(); await tick(timers, 60000); eq(api.statusCalls, n2, 'dopo il tetto «riprendi» non riparte'); }
  // un invio alla volta
  { const { api, A } = await fresh([]);
    const [a, b] = await Promise.all([A.send(B1), A.send(B1)]);
    eq([a, b].filter(Boolean).length, 1, 'due tocchi: un invio'); eq(api.calls.filter((c) => c.startsWith('send ')).length, 1, 'due tocchi: un sendTheme'); }
  // api.sendTheme: POST {id}, 202 → l'operazione; 409/404 → Error col messaggio del servizio
  { const realFetch = globalThis.fetch; const seen = [];
    let reply = [202, { theme: { id: 't1', stage: 'attesa' } }];
    globalThis.fetch = async (u, o = {}) => { seen.push([o.method, u, o.body]); return new Response(JSON.stringify(reply[1]), { status: reply[0] }); };
    const job = await sendTheme('calamaio-3f2a', 'prova');
    same(seen[0], ['POST', '/api/displays/calamaio-3f2a/theme', '{"id":"prova"}'], 'api.sendTheme: richiesta');
    eq(job.id, 't1', 'api.sendTheme: operazione');
    for (const [code, msg] of [[409, 'Display non collegato.'], [404, 'tema sconosciuto']]) {
      reply = [code, { error: msg }];
      let err = null; try { await sendTheme('calamaio-3f2a', 'prova'); } catch (e) { err = e; }
      eq(err?.message, msg, 'api.sendTheme ' + code);
    }
    globalThis.fetch = realFetch; }
}

// 13. DECISIONI elenca ogni testo a schermo dell'editor (M2): i pezzi fissi di ogni testo del codice stanno nell'elenco
{
  const strs = editorStrings(path.join(REPO, 'service/internal/webui/static/assets/js'), path.join(REPO, 'service/internal/webui/static/editor.html'));
  const list = decisionList(fs.readFileSync(path.join(REPO, 'docs/DECISIONI.md'), 'utf8'));
  check(list.length > 1000, 'DECISIONI: voce dei testi dell\'editor trovata');
  // l'estrazione trova davvero i testi (niente prova vuota): letterali, modelli, html, avvisi dei glifi
  check(strs.length > 250, 'testi estratti: ' + strs.length);
  for (const t of ['Tema attivo sulla scheda', 'vicina al limite di 4 096', 'Vai alla tela', 'è testo libero', 'Allinea al centro in verticale'])
    check(strs.some((x) => x.frags.some((f) => f.includes(t))), 'estratto: ' + t);
  check(!strs.some((x) => /^(btn|ed-)|Escape|rgba/.test(x.text)), 'niente classi, tasti o colori');
  same(fragments(['Schermata di ', ' byte: vicina al limite di 4 096']), ['Schermata di', 'byte: vicina al limite di 4 096'], 'pezzi fissi di un modello');
  const missing = [...new Set(strs.flatMap((x) => x.frags.filter((f) => !list.includes(f)).map((f) => x.file + ': ' + f)))];
  same(missing, [], 'testi del codice che mancano in DECISIONI');
  check(!list.includes('Testo che non esiste da nessuna parte'), 'la prova vede i testi mancanti');
}

if (fail) { console.log(fail + ' prove fallite'); process.exit(1); }
console.log('test-editor: ok');
