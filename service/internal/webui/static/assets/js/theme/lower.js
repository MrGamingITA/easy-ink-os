// Abbassa un tema JSON (docs/TEMI.md §1–§4) nella forma che disegna render.js, con gli stessi codici del binario (§5)
// e le stesse regole di service/internal/theme/parse.go e compile.go (predefiniti, gruppi sciolti, nascosti tolti,
// orologio → testo, riquadri calcolati). Il tema dovrebbe essere già valido (lo controlla il servizio), ma l'editor
// passa anche JSON a metà: non lancia mai. Dove parse.go rifiuterebbe, qui un elemento, una variante o una condizione
// sbagliati si tolgono, un campo sbagliato prende il predefinito, una schermata non personalizzabile si toglie.
import { VALUES, STATES } from './values.js';

const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'];
const SYMS = ['plug', 'bell', 'moon', 'lock', 'bolt', 'dot', 'glass', 'gauge'];
const SOURCES = ['context', 'tasks', 'workday', 'battery', 'limit5h', 'limit7d'];
const TYPES = { mascot: 1, text: 2, clock: 2, battery: 3, wifi: 4, stateIcon: 5, bar: 6, qr: 7, sym: 8, line: 9, rect: 10, circle: 11, list: 12 };
const WHEN = { always: 0, none: 1, one: 2, many: 3 };
const BOOL = { perm: 1, charging: 3, night: 4, pc: 5, workday: 6, game: 7 };
const REFS = { urgent: 0, selected: 1, row: 2, nobody: 3 };
const ALIGN = ['left', 'center', 'right'];
// le 12 schermate personalizzabili (ScreenKeys di theme.go): Permessi, abbinamento e sistema restano di serie
export const SCREEN_KEYS = ['home', 'detail', 'sleep', 'offline', 'riepilogo', 'oggi', 'batteria', 'stato', 'limiti', 'focus', 'ambiente', 'pet'];

const isObj = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
const arr = (v) => (Array.isArray(v) ? v : []);
// solo chiavi proprie delle tabelle: «constructor», «toString»… non valgono
const own = (t, k) => (typeof k === 'string' && Object.hasOwn(t, k) ? t[k] : undefined);

const int = (v, def) => (Number.isInteger(v) ? v : def);
const idx = (list, v, def) => { const k = list.indexOf(v); return k < 0 ? def : k; };

// ParseTemplate di template.go: «testo {valore|filtro} testo»; {{ e }} = graffe. Nomi sconosciuti → {sessioni}.
export function parseTemplate(s) {
  const out = [];
  let lit = '';
  const flush = () => { if (lit) out.push({ lit }); lit = ''; };
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '{' && s[i + 1] === '{') { lit += '{'; i++; continue; }
    if (c === '}' && s[i + 1] === '}') { lit += '}'; i++; continue; }
    if (c === '{') {
      const j = s.indexOf('}', i);
      if (j < 0) { lit += s.slice(i); break; }
      const body = s.slice(i + 1, j), bar = body.indexOf('|');
      const name = bar < 0 ? body : body.slice(0, bar), filter = bar < 0 ? '' : body.slice(bar + 1);
      flush();
      out.push({ val: own(VALUES, name) ?? 0x84, filter: filter === 'maiuscolo' ? 1 : filter === 'minuscolo' ? 2 : 0 });
      i = j;
      continue;
    }
    lit += c;
  }
  flush();
  return out;
}

// conds() di parse.go: [[op, arg]] nell'ordine delle chiavi. Una condizione che parse.go rifiuterebbe si toglie.
function conds(list) {
  const out = [];
  const okInt = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
  for (const c of arr(list)) {
    if (!isObj(c) || Object.keys(c).length !== 1) continue;
    const [k, v] = Object.entries(c)[0];
    if (own(BOOL, k) !== undefined) { if (typeof v === 'boolean') out.push([BOOL[k], v ? 1 : 0]); }
    else if (k === 'battBelow') { if (okInt(v, 1, 100)) out.push([2, v]); }
    else if (k === 'sessions') {
      if (!isObj(v)) continue;
      if (okInt(v.min, 0, 6)) out.push([8, v.min]);
      if (okInt(v.max, 0, 6)) out.push([9, v.max]);
    } else if (k === 'state') { const n = STATES.indexOf(v); if (n >= 0) out.push([10, n]); }
    else if (k === 'blink') { if (v === 'odd' || v === 'even') out.push([11, v === 'odd' ? 1 : 0]); }
  }
  return out;
}

// refCode() di compile.go: 0 urgente, 1 selezionata, 2 riga, 3 nessuna, 4 quella della variante
const refCode = (r, inRow) => own(REFS, r) ?? (inRow ? 2 : 4);

// element() di parse.go: un gruppo diventa i suoi figli (posizione e condizioni sommate), un nascosto niente.
// depth: livello dei gruppi (parse.go: al massimo 4, oltre il gruppo si toglie).
function element(j, inRow, depth, dx, dy, inherited) {
  if (!isObj(j) || j.hidden === true) return [];
  const cs = inherited.concat(conds(j.visibleIf));
  const x = int(j.x, 0) + dx, y = int(j.y, 0) + dy;
  if (j.type === 'group') return depth >= 4 ? [] : arr(j.children).flatMap((c) => element(c, inRow, depth + 1, x, y, cs));
  const type = own(TYPES, j.type);
  if (type === undefined) return [];  // tipo sconosciuto: parse.go lo rifiuta
  const e = { type, invert: j.invert === true, x, y, w: int(j.w, 0), h: int(j.h, 0), conds: cs };
  const sc = () => int(j.scale, 1);
  switch (j.type) {
    case 'mascot':
      e.scale = sc(); e.w = 28 * e.scale; e.h = 25 * e.scale;
      e.pose = !j.pose || j.pose === 'auto' ? 255 : idx(POSES, j.pose, 255);
      e.ref = refCode(j.ref, inRow); e.acc = j.accessory !== false; e.anim = j.animated !== false;
      break;
    case 'text': case 'clock': {
      const f = isObj(j.font) ? j.font : {};
      e.size = int(f.size, 10); e.weight = int(f.weight, 500); e.pix = f.family === 'pix';
      e.align = idx(ALIGN, j.align || 'left', 0);
      e.lines = j.type === 'text' ? int(j.lines, 1) : 1;
      e.lh = int(j.lineHeight, e.size + 2);
      e.upper = j.type === 'text' && j.upper === true;
      e.segs = j.type === 'text' ? parseTemplate(typeof j.text === 'string' ? j.text : '')
        : [{ val: j.format === 'hms' ? 0x81 : 0x80, filter: 0 }];
      e.h = e.lines * e.lh;
      break;
    }
    case 'battery': e.scale = sc(); e.w = 16 * e.scale; e.h = 8 * e.scale; e.percent = j.percent === true; break;
    case 'wifi': e.scale = sc(); e.w = 11 * e.scale; e.h = 9 * e.scale; break;
    case 'stateIcon': e.scale = sc(); e.w = e.h = 7 * e.scale; e.ref = refCode(j.ref, inRow); break;
    case 'sym': e.scale = sc(); e.w = e.h = 7 * e.scale; e.sym = idx(SYMS, j.sym, 0); break;
    case 'bar': e.src = idx(SOURCES, j.source, 0); e.big = j.style !== 'bar'; break;
    case 'qr': e.segs = parseTemplate(typeof j.text === 'string' ? j.text : ''); break;
    case 'line': e.vertical = j.dir === 'v'; e.dotted = j.style === 'dotted'; break;
    case 'rect': e.border = int(j.border, 1); e.fill = j.fill === true; e.radius = int(j.radius, 0); break;
    case 'circle': e.border = int(j.border, 1); e.fill = j.fill === true; break;
    case 'list':
      if (inRow) return [];  // una lista dentro una riga: parse.go la rifiuta
      e.rowH = int(j.rowH, 0); e.gap = int(j.gap, 0); e.pageSec = int(j.pageSec, 5); e.dots = j.pageDots !== false;
      // righe: posizioni relative alla riga, senza le condizioni della lista (valgono per la lista intera)
      e.row = arr(j.row).flatMap((c) => element(c, true, depth, 0, 0, []));
      break;
  }
  return [e];
}

export function lowerTheme(t) {
  const screens = {}, src = isObj(t) && isObj(t.screens) ? t.screens : {};
  for (const k of SCREEN_KEYS) {
    if (!Object.hasOwn(src, k) || !isObj(src[k])) continue;
    // variante non valida: parse.go la salta
    const vs = arr(src[k].variants).filter(isObj);
    screens[k] = { variants: vs.map((v) => ({ when: own(WHEN, v.when) ?? 0, ref: v.ref === 'urgent' ? 0 : 1,
      elems: arr(v.elements).flatMap((e) => element(e, false, 0, 0, 0, [])) })) };
  }
  return { id: isObj(t) && typeof t.id === 'string' ? t.id : '', name: isObj(t) && typeof t.name === 'string' ? t.name : '', screens };
}
