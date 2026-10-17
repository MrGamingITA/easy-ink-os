// Cataloghi dell'editor: schermate, tipi aggiungibili, caratteri, condizioni. Puro: niente window/document (F7).
import { SCREEN_KEYS } from '../theme/lower.js';
import { FONTS } from '../theme/fonts.js';
import { elementDefaults } from './ops.js';
import { elementLabel } from './warnings.js';
import { VALUE_GLYPHS } from '../theme/glyphs.js';
import { STATES, POSE_LABEL } from '../model.js';

const SCREEN_LABELS = {
  home: 'Home', detail: 'Dettaglio', sleep: 'Riposo', offline: 'PC non raggiungibile', riepilogo: 'PC spento', oggi: 'Oggi',
  batteria: 'Batteria', stato: 'Stato', limiti: 'Limiti', focus: 'Focus', ambiente: 'Ambiente', pet: 'La mascotte',
};
// Permessi si vede ma non si apre (regole di sicurezza)
export const SCREENS = [
  ...SCREEN_KEYS.map((key) => ({ key, label: SCREEN_LABELS[key] })),
  { key: 'perm', label: 'Permessi — bloccata (regole di sicurezza)', locked: true },
];

// i gruppi nascono da «Raggruppa», non da «Aggiungi»
export const TYPES = ['text', 'clock', 'mascot', 'battery', 'wifi', 'stateIcon', 'sym', 'bar', 'qr', 'line', 'rect', 'circle', 'list']
  .filter((type) => elementDefaults(type, 0, 0))
  .map((type) => ({ type, label: elementLabel({ type }) }));

// le 22 combinazioni di f_registry.h
export const FONT_CHOICES = FONTS.map((f) => {
  const font = { family: f.pix ? 'pix' : 'ui', size: f.size, weight: f.weight };
  return { key: `${font.family}:${f.size}:${f.weight}`, label: `${f.pix ? 'Silkscreen' : 'Chakra Petch'} ${f.size} · ${f.weight}`, font };
});

export const WHEN = [
  { key: 'always', label: 'sempre' },
  { key: 'none', label: 'nessuna sessione' },
  { key: 'one', label: 'una sessione' },
  { key: 'many', label: 'più sessioni' },
];

// sessione di riferimento (docs/TEMI.md §1)
export const REFS = [
  { key: 'urgent', label: 'la più urgente' },
  { key: 'selected', label: 'quella scelta' },
  { key: 'row', label: 'quella della riga' },
  { key: 'nobody', label: 'nessuna' },
];

const YESNO = [{ value: true, label: 'sì' }, { value: false, label: 'no' }];
const opts = (pairs) => pairs.map(([value, label]) => ({ value, label }));

// condizioni di visibleIf (docs/TEMI.md §4); arg: bool, int [min, max], sessions, state, blink
export const CONDS = [
  { key: 'perm', label: 'Permesso in attesa', arg: 'bool', options: YESNO },
  { key: 'battBelow', label: 'Batteria sotto N %', arg: 'int', min: 1, max: 100 },
  { key: 'charging', label: 'In carica', arg: 'bool', options: YESNO },
  { key: 'night', label: 'Notte', arg: 'bool', options: YESNO },
  { key: 'pc', label: 'PC collegato', arg: 'bool', options: YESNO },
  { key: 'workday', label: 'Giornata di lavoro attiva', arg: 'bool', options: YESNO },
  { key: 'game', label: 'Gioco della mascotte acceso', arg: 'bool', options: YESNO },
  { key: 'sessions', label: 'Sessioni da/a', arg: 'sessions', min: 0, max: 6 },
  // stessi stati e stesso ordine di values.js, etichette di INFO (= STATES di model.js)
  { key: 'state', label: 'Stato della sessione', arg: 'state', options: Object.keys(STATES).map((k) => ({ value: k, label: STATES[k].label })) },
  { key: 'blink', label: 'Lampeggio', arg: 'blink', options: opts([['odd', 'fotogramma dispari'], ['even', 'pari']]) },
];

// la chiave di una condizione ({perm: true} → 'perm'), o null se non è una condizione
export function condKey(c) {
  if (!c || typeof c !== 'object' || Array.isArray(c)) return null;
  const ks = Object.keys(c);
  return ks.length === 1 && CONDS.some((x) => x.key === ks[0]) ? ks[0] : null;
}

// condizione nuova col valore di partenza
export function condDefault(key) {
  switch (key) {
    case 'battBelow': return { battBelow: 20 };
    case 'sessions': return { sessions: { min: 1, max: 6 } };
    case 'state': return { state: 'permission' };
    case 'blink': return { blink: 'odd' };
    default: return CONDS.some((x) => x.key === key) ? { [key]: true } : null;
  }
}

// condizioni come le conta il servizio: «sessions» con min e max sono due
export const condCount = (list) => (Array.isArray(list) ? list : []).reduce((n, c) => {
  const s = c?.sessions;
  return n + (s && typeof s === 'object' ? (Number.isInteger(s.min) ? 1 : 0) + (Number.isInteger(s.max) ? 1 : 0) || 1 : 1);
}, 0);

// una condizione che il servizio accetta (parse.go conds)
export function condOk(c) {
  const k = condKey(c);
  if (!k) return false;
  const v = c[k], d = CONDS.find((x) => x.key === k);
  const int = (n, lo, hi) => Number.isInteger(n) && n >= lo && n <= hi;
  if (d.arg === 'bool') return typeof v === 'boolean';
  if (d.arg === 'int') return int(v, d.min, d.max);
  if (d.arg === 'sessions') {
    if (!v || typeof v !== 'object' || Array.isArray(v) || Object.keys(v).some((x) => x !== 'min' && x !== 'max')) return false;
    if (v.min === undefined && v.max === undefined) return false;
    return (v.min === undefined || int(v.min, 0, 6)) && (v.max === undefined || int(v.max, 0, 6));
  }
  return d.options.some((o) => o.value === v);
}

// valori per «Inserisci valore»: riga.* solo dentro una lista (in una riga prima quelli della riga)
export function valueChoices(inRow) {
  const all = Object.keys(VALUE_GLYPHS).map((name) => ({ name, example: VALUE_GLYPHS[name].example, free: VALUE_GLYPHS[name].free }));
  const row = all.filter((v) => v.name.startsWith('riga.'));
  const rest = all.filter((v) => !v.name.startsWith('riga.'));
  return inRow ? [...row, ...rest] : rest;
}

const utf8Len = (s) => new TextEncoder().encode(s).length;
const ROW_MSG = 'Un elemento che usa i valori della riga ({riga.…}) può stare solo dentro una lista.';

// perché il modello non passerebbe ParseTemplate del servizio (template.go), o null
export function templateProblem(s, inRow) {
  if (typeof s !== 'string' || s === '') return 'Il testo non può essere vuoto.';
  let lit = '', segs = 0;
  const flush = () => {
    if (!lit) return null;
    if (utf8Len(lit) > 255) return `Testo fisso di ${utf8Len(lit)} byte: al massimo 255.`;
    segs++; lit = '';
    return null;
  };
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '{' && s[i + 1] === '{') { lit += '{'; i++; continue; }
    if (c === '}' && s[i + 1] === '}') { lit += '}'; i++; continue; }
    if (c === '{') {
      const j = s.indexOf('}', i);
      if (j < 0) return 'Graffa non chiusa: chiudi il valore con «}» (per una graffa scrivi «{{»).';
      const body = s.slice(i + 1, j), bar = body.indexOf('|');
      const name = bar < 0 ? body : body.slice(0, bar), filter = bar < 0 ? '' : body.slice(bar + 1);
      if (!Object.hasOwn(VALUE_GLYPHS, name)) return `Valore sconosciuto {${name}}.`;
      if (name.startsWith('riga.') && !inRow) return ROW_MSG;
      if (filter && filter !== 'maiuscolo' && filter !== 'minuscolo') return `Filtro sconosciuto «${filter}»: maiuscolo o minuscolo.`;
      const e = flush();
      if (e) return e;
      segs++;
      i = j;
      continue;
    }
    lit += c;
  }
  const e = flush();
  if (e) return e;
  return segs > 16 ? `${segs} pezzi nel testo: al massimo 16.` : null;
}

// ---------- campi del pannello «Proprietà» (docs/TEMI.md §2) ----------
// Field = { key, label, kind: 'int'|'select'|'bool'|'template'|'font'|'conds', min, max, options, def, optional }
// def: il valore che vale anche senza la chiave (mostrato; per select e bool scriverlo toglie la chiave);
// optional: campo vuoto = chiave tolta
const int = (key, label, min, max, extra = {}) => ({ key, label, kind: 'int', min, max, ...extra });
const sel = (key, label, pairs, extra = {}) => ({ key, label, kind: 'select', options: opts(pairs), ...extra });
const bool = (key, label, def = false) => ({ key, label, kind: 'bool', def });
const XY = [int('x', 'X', -512, 512), int('y', 'Y', -512, 512)];
const W = int('w', 'Larghezza', 0, 512), H = int('h', 'Altezza', 0, 512);
const REF_OPTS = [['', 'come la variante'], ...REFS.map((r) => [r.key, r.label])];
const ALIGN = sel('align', 'Allineamento', [['left', 'a sinistra'], ['center', 'al centro'], ['right', 'a destra']], { def: 'left' });
const FONT = { key: 'font', label: 'Carattere', kind: 'font' };
const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'];

const OWN = {
  mascot: [int('scale', 'Scala', 1, 4, { def: 1 }), sel('pose', 'Posa', [['auto', 'automatica (dallo stato)'], ...POSES.map((p) => [p, POSE_LABEL[p]])], { def: 'auto' }),
    sel('ref', 'Sessione', REF_OPTS, { def: '' }), bool('accessory', 'Accessorio', true), bool('animated', 'Animata', true)],
  text: [W, { key: 'text', label: 'Testo', kind: 'template' }, FONT, ALIGN, int('lines', 'Righe', 1, 8, { def: 1 }),
    int('lineHeight', 'Altezza della riga', 1, 64, { optional: true }), bool('upper', 'Tutto maiuscolo')],
  clock: [W, sel('format', 'Formato', [['hm', 'ore:minuti'], ['hms', 'ore:minuti:secondi']], { def: 'hm' }), FONT, ALIGN],
  battery: [int('scale', 'Scala', 1, 2, { def: 1 }), bool('percent', 'Percentuale')],
  wifi: [int('scale', 'Scala', 1, 2, { def: 1 })],
  stateIcon: [int('scale', 'Scala', 1, 3, { def: 1 }), sel('ref', 'Sessione', REF_OPTS, { def: '' })],
  bar: [W, H, sel('source', 'Valore', [['context', 'Contesto'], ['tasks', 'Task'], ['workday', 'Giornata di lavoro'], ['battery', 'Batteria'],
    ['limit5h', 'Limite 5 h'], ['limit7d', 'Limite 7 giorni']]), sel('style', 'Stile', [['big', 'grande'], ['bar', 'barra']], { def: 'big' })],
  qr: [W, H, { key: 'text', label: 'Contenuto', kind: 'template' }],
  sym: [sel('sym', 'Simbolo', [['plug', 'spina'], ['bell', 'campanella'], ['moon', 'luna'], ['lock', 'lucchetto'], ['bolt', 'fulmine'],
    ['dot', 'punto'], ['glass', 'clessidra'], ['gauge', 'indicatore']]), int('scale', 'Scala', 1, 3, { def: 1 })],
  line: [W, H, sel('dir', 'Direzione', [['h', 'orizzontale'], ['v', 'verticale']], { def: 'h' }), sel('style', 'Tratto', [['solid', 'continuo'], ['dotted', 'a puntini']], { def: 'solid' })],
  rect: [W, H, int('border', 'Bordo', 0, 8, { def: 1 }), bool('fill', 'Pieno'), int('radius', 'Raggio degli angoli', 0, 8, { def: 0 })],
  circle: [W, H, int('border', 'Bordo', 0, 8, { def: 1 }), bool('fill', 'Pieno')],
  list: [W, H, int('rowH', 'Altezza della riga', 1, 122), int('gap', 'Spazio fra le righe', 0, 60, { def: 0 }), int('pageSec', 'Secondi per pagina', 1, 60, { def: 5 }),
    bool('pageDots', 'Puntini delle pagine', true)],
};
const CONDS_FIELD = { key: 'visibleIf', label: 'Visibile se', kind: 'conds' };

// campi del tipo, nell'ordine del pannello; [] per un tipo sconosciuto
export function fieldsFor(type) {
  const out = type === 'group' ? [...XY, CONDS_FIELD] : OWN[type] ? [...XY, ...OWN[type], bool('invert', 'Inverti (carta su inchiostro)'), CONDS_FIELD] : [];
  return out.map((f) => ({ ...f }));
}
