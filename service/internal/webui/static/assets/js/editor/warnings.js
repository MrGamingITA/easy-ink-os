// Anteprima della variante e avvisi in tempo reale (editor). Moduli puri: nessun accesso a window/document.
// L'anteprima usa lo stesso renderer della scheda (renderScreen); qui si sceglie solo quale variante mostrare.
import { lowerTheme, parseTemplate, SCREEN_KEYS } from '../theme/lower.js';
import { renderScreen } from '../theme/render.js';
import { whenTrue, refIndex, expand, makeModel } from '../theme/values.js';
import { textWidthMilli, wrapLines, hasFont } from '../theme/text.js';
import { glyphWarnings } from '../theme/glyphs.js';
import { Null } from '../theme/surface.js';

const isObj = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
const arr = (v) => (Array.isArray(v) ? v : []);
const WHEN = { always: 0, none: 1, one: 2, many: 3 };
const SW = 250, SH = 122;
const SIZE_WARN = 3686;  // 90 % di 4 096

const variantsOf = (theme, screen) => arr(isObj(theme) && isObj(theme.screens) && isObj(theme.screens[screen]) ? theme.screens[screen].variants : null);

// il tema con la sola variante aperta, sempre vera (il suo `ref` resta); mai lancia
export function previewTheme(theme, loc) {
  try {
    const v = variantsOf(theme, loc?.screen)[loc?.variant];
    if (!isObj(v)) return lowerTheme(theme);
    const t = { ...theme, screens: { ...theme.screens, [loc.screen]: { ...theme.screens[loc.screen], variants: [{ ...v, when: 'always' }] } } };
    return lowerTheme(t);
  } catch {
    return lowerTheme(null);
  }
}

// indice della prima variante vera col modello di prova (la scelta della scheda), o −1
export function boardVariant(theme, screen, model) {
  const vs = variantsOf(theme, screen);
  for (let i = 0; i < vs.length; i++) {
    if (!isObj(vs[i])) continue;  // lower.js salta le varianti non valide
    const w = Object.hasOwn(WHEN, vs[i].when) ? WHEN[vs[i].when] : 0;
    if (whenTrue(model, w)) return i;
  }
  return -1;
}

const NAMES = { text: 'Testo', clock: 'Orologio', mascot: 'Mascotte', battery: 'Batteria', wifi: 'Wi-Fi', stateIcon: 'Icona di stato', bar: 'Barra',
  qr: 'QR', sym: 'Simbolo', line: 'Linea', rect: 'Rettangolo', circle: 'Cerchio', list: 'Lista', group: 'Gruppo' };
export function elementLabel(el) {
  if (isObj(el) && typeof el.name === 'string' && el.name) return el.name;
  return (isObj(el) && NAMES[el.type]) || 'Elemento';
}

// F15: la scheda disegna una schermata solo se tutte le sue varianti sono nei limiti, altrimenti mostra quella di serie
const NO_ASSETS = { avatar: () => null, accessory: () => null };
function boardWarning(theme, screen, model) {
  if (!isObj(theme?.screens?.[screen])) return null;
  let drawn = false;
  try { drawn = renderScreen(lowerTheme(theme), screen, model, new Null(), NO_ASSETS).drawn; } catch { /* resta false */ }
  if (drawn) return null;
  if (!variantsOf(theme, screen).length) return 'La scheda mostrerebbe la schermata di serie: la schermata non ha varianti';
  if (boardVariant(theme, screen, model) < 0) return 'Nessuna variante è vera con questi dati di prova: la scheda mostrerebbe la schermata di serie';
  return 'La scheda mostrerebbe la schermata di serie: ' + boardReason(lowerTheme(theme).screens[screen]?.variants);
}

// Prima dell'invio: c'è una schermata che la scheda scarterebbe per intero (fuori dai limiti, senza varianti)?
// Non dipende dai dati di prova: ogni variante si prova come se fosse vera (una variante falsa coi dati di prova
// è una scelta, non un errore). → { screen, reason } della prima, o null
export function sendBlock(theme) {
  if (!isObj(theme?.screens)) return null;
  for (const screen of Object.keys(theme.screens)) {
    const sc = theme.screens[screen];
    if (!isObj(sc)) continue;
    const vs = arr(sc.variants);
    if (!vs.length) return { screen, reason: 'la schermata non ha varianti' };
    const all = { ...theme, screens: { ...theme.screens, [screen]: { ...sc, variants: vs.map((v) => (isObj(v) ? { ...v, when: 'always' } : v)) } } };
    let drawn = false, lowered = null;
    try { lowered = lowerTheme(all); drawn = renderScreen(lowered, screen, BLOCK_MODEL, new Null(), NO_ASSETS).drawn; } catch { /* resta false */ }
    if (!drawn) return { screen, reason: boardReason(lowered?.screens?.[screen]?.variants) };
  }
  return null;
}
// un modello qualunque: con ogni variante vera la scelta non dipende dai dati
const BLOCK_MODEL = makeModel({}, {});

// perché la scheda scarta la schermata (stessi limiti di screenOk in render.js)
function boardReason(vs) {
  vs = arr(vs);
  if (!vs.length) return 'la schermata non ha varianti';
  if (vs.length > 8) return 'ci sono più di 8 varianti';
  const all = (es) => es.flatMap((e) => [e, ...arr(e.row)]);
  for (const v of vs) {
    const es = all(arr(v.elems));
    if (es.length > 64) return 'una variante ha più di 64 elementi (righe delle liste comprese)';
    for (const e of es) {
      if (e.type === 12 && (arr(e.row).length < 1 || e.row.length > 32)) return 'una lista deve avere da 1 a 32 elementi di riga';
      if (e.type === 2 && !(e.weight % 100 === 0 && (e.pix ? e.weight === 400 || e.weight === 700 : e.weight >= 500 && e.weight <= 700) && hasFont(e.size, e.weight, e.pix))) return "un testo ha un carattere, una dimensione o un peso che non esistono";
      // scala massima come elemOk in render.js: mascotte 4, batteria e wifi 2, gli altri 3
      if (e.scale !== undefined && !(e.scale >= 1 && e.scale <= (e.type === 1 ? 4 : e.type === 3 || e.type === 4 ? 2 : 3))) return 'un elemento ha una scala non valida';
      if ([e.x, e.y].some((n) => Math.abs(n) > 512) || [e.w, e.h].some((n) => n > 512)) return "c'è un elemento fuori dai limiti";
    }
  }
  return "c'è un elemento con valori fuori dai limiti";
}

// elementi della variante con percorso (stessi indici di layout()) e proprietà ereditate
function collect(theme, loc) {
  const v = variantsOf(theme, loc.screen)[loc.variant];
  const out = [];
  if (!isObj(v)) return { v: null, out };
  const walk = (els, prefix, inRow, hid, cnd) => {
    arr(els).forEach((el, i) => {
      if (!isObj(el)) return;
      const p = `${prefix}[${i}]`, hidden = hid || el.hidden === true, cond = cnd || arr(el.visibleIf).length > 0;
      out.push({ el, path: p, inRow, hidden, cond });
      if (el.type === 'group') walk(el.children, p + '.children', inRow, hidden, cond);
      else if (el.type === 'list') walk(el.row, p + '.row', true, hidden, cond);
    });
  };
  walk(v.elements, `screens.${loc.screen}.variants[${loc.variant}].elements`, false, false, false);
  return { v, out };
}

function fitWarning(el, inRow, v, loc, model) {
  if (el.type !== 'text' || !(el.w > 0)) return null;
  const f = isObj(el.font) ? el.font : {};
  const size = Number.isInteger(f.size) ? f.size : 10, weight = Number.isInteger(f.weight) ? f.weight : 500, pix = f.family === 'pix';
  if (!hasFont(size, weight, pix)) return null;
  const cx = { m: model, now: model.now, local: model.local, frame: model.frame, screen: loc.screen, ref: refIndex(model, loc.screen, v.ref === 'urgent' ? 0 : 1) };
  // nella riga di una lista il renderer passa la sessione della riga; qui la prima
  const t = expand(parseTemplate(typeof el.text === 'string' ? el.text : ''), cx, inRow ? model.sessions[0] ?? null : null, el.upper === true);
  const lines = Number.isInteger(el.lines) && el.lines > 1 ? el.lines : 1;
  if (lines === 1) return textWidthMilli(t, size, weight, pix) > el.w * 1000 ? `Il testo non ci sta: si vede troncato con «${pix ? '..' : '…'}»` : null;
  return wrapLines(t, lines + 1, size, el.w, weight, pix).length > lines ? `Il testo non ci sta in ${lines} righe` : null;
}

// la parte dopo il primo «: »
const after = (s) => { const k = s.indexOf(': '); return k < 0 ? s : s.slice(k + 2); };
const GLYPH_TAIL = 'si vedrà «?»';

export function liveWarnings(theme, loc, model, boxes, check) {
  const out = [];
  const { v, out: els } = collect(theme, loc);
  const byPath = new Map(arr(boxes).map((b) => [b.path, b]));
  const add = (id, kind, msg) => out.push({ id, kind, msg });
  const bw = boardWarning(theme, loc.screen, model);
  if (bw) add(null, 'board', bw);
  if (v && !arr(v.elements).length) add(null, 'empty', 'Variante senza elementi: la scheda mostrerà lo schermo vuoto');
  els.forEach((e, k) => {
    if (e.hidden) return;
    const { el } = e, b = byPath.get(e.path), id = el.id ?? null;
    for (const g of glyphWarnings(el)) add(id, 'glyph', g);
    const fit = fitWarning(el, e.inRow, v, loc, model);
    if (fit) add(id, 'fit', fit);
    if (!b || el.type === 'group') return;
    const w = Math.max(b.w, 1), h = Math.max(b.h, 1);
    if (b.x + w <= 0 || b.y + h <= 0 || b.x >= SW || b.y >= SH) add(id, 'outside', 'Fuori dallo schermo: non si vede');
    else if (b.x < 0 || b.y < 0 || b.x + w > SW || b.y + h > SH) add(id, 'offscreen', 'Esce dallo schermo: si vede tagliato');
    // coperto: interamente dentro un rettangolo pieno o un QR disegnato dopo (l'ordine di layout è quello di disegno)
    for (const o of els.slice(k + 1)) {
      const ob = byPath.get(o.path);
      if (o.hidden || o.cond || (o.el.radius > 0) || !ob || !(o.el.type === 'qr' || (o.el.type === 'rect' && o.el.fill === true)) || !(ob.w > 0 && ob.h > 0)) continue;
      if (b.x >= ob.x && b.y >= ob.y && b.x + w <= ob.x + ob.w && b.y + h <= ob.y + ob.h) { add(id, 'covered', `Coperto da «${elementLabel(o.el)}»: non si vede`); break; }
    }
  });
  // problemi e avvisi del servizio: sull'elemento col percorso (o l'antenato più vicino)
  const base = `screens.${loc.screen}.variants[${loc.variant}]`;
  const inVariant = (p) => {
    if (p === base || p.startsWith(base + '.') || p.startsWith(base + '[')) return true;
    return p === `screens.${loc.screen}` || (p.startsWith(`screens.${loc.screen}.`) && !p.startsWith(`screens.${loc.screen}.variants[`));
  };
  const owner = (p) => {
    let best = null;
    for (const e of els) if ((p === e.path || p.startsWith(e.path + '.') || p.startsWith(e.path + '[')) && (!best || e.path.length > best.path.length)) best = e;
    return best?.el.id ?? null;
  };
  if (check) {
    for (const p of arr(check.problems)) {
      // ogni problema ha la sua riga: «Non salvato: N problemi» non resta mai senza elenco (I2)
      const path = isObj(p) && typeof p.path === 'string' ? p.path : '';
      const msg = isObj(p) && typeof p.msg === 'string' && p.msg ? p.msg : 'tema non valido';
      if (inVariant(path)) add(owner(path), 'service', after(msg));
      // fuori dalla variante aperta (altra variante, altra schermata, tema): col luogo
      else out.push({ id: null, kind: 'elsewhere', msg: path ? after(msg) : msg, where: placeOf(theme, path) });
    }
    for (const s of arr(check.warnings)) {
      if (typeof s !== 'string' || s.endsWith(GLYPH_TAIL)) continue;
      const k = s.indexOf(': ');
      if (k < 0 || !inVariant(s.slice(0, k))) continue;
      add(owner(s.slice(0, k)), 'service', after(s.slice(k + 2)));
    }
    const n = check.screens?.[loc.screen];
    if (n > SIZE_WARN && n <= 4096) add(null, 'size', `Schermata di ${n} byte: vicina al limite di 4 096`);
  }
  return out;
}

// luogo di un percorso del servizio: { screen, variant, id, label, open } (screen null = a livello del tema)
const PATH = /^screens\.([A-Za-z0-9_]+)(?:\.variants\[(\d+)\])?/;
function placeOf(theme, path) {
  const m = PATH.exec(path);
  if (!m) return { screen: null, variant: null, id: null, label: '', open: false };
  const screen = m[1], variant = m[2] === undefined ? null : Number(m[2]);
  let id = null, label = '';
  if (variant !== null) {
    let best = null;
    for (const e of collect(theme, { screen, variant }).out) if ((path === e.path || path.startsWith(e.path + '.') || path.startsWith(e.path + '[')) && (!best || e.path.length > best.path.length)) best = e;
    if (best) { id = best.el.id ?? null; label = elementLabel(best.el); }
  }
  return { screen, variant, id, label, open: SCREEN_KEYS.includes(screen) };
}
// «Oggi › variante 2 › Testo», «Oggi», «tema»; screenLabel(key) → nome della schermata
export function warningPlace(where, screenLabel) {
  if (!isObj(where) || !where.screen) return 'tema';
  return [screenLabel(where.screen), where.variant !== null ? `variante ${where.variant + 1}` : '', where.label].filter(Boolean).join(' › ');
}

// misura della schermata dal controllo del servizio: «N / 4 096 byte», avviso dal 90 %
const grouped = (n) => String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
export function sizeInfo(n) {
  if (!Number.isInteger(n) || n < 0) return null;
  return { text: `${grouped(n)} / ${grouped(4096)} byte`, warn: n > SIZE_WARN };
}

export const warnCount = (n) => (n === 0 ? 'Nessun avviso' : n === 1 ? '1 avviso' : `${n} avvisi`);

// scritta sopra la tela: la scheda non disegnerebbe la schermata (avviso «board», col motivo),
// oppure coi dati di prova sceglierebbe un'altra variante. null = la tela mostra quello che mostrerebbe la scheda.
export function variantBanner(theme, loc, model) {
  if (!isObj(theme?.screens?.[loc?.screen])) return null;
  // testi del brief: nessuna variante vera, poi la schermata scartata (col motivo), poi un'altra variante
  const bv = boardVariant(theme, loc.screen, model);
  if (bv < 0 && variantsOf(theme, loc.screen).length) return 'Con questi dati di prova la scheda mostrerebbe la schermata di serie';
  const bw = boardWarning(theme, loc.screen, model);
  if (bw) return bw;
  return bv === loc.variant ? null : `Con questi dati di prova la scheda mostrerebbe la variante ${bv + 1}`;
}
