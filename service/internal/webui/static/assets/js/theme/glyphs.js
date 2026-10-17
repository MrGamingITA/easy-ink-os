// Glifi mancanti: stesse tabelle e stessi avvisi di service/internal/theme/glyphs.go (protocol/themes/catalog.json li tiene allineati).
import { FONTS } from './fonts.js';
import { VALUES } from './values.js';

export const CLOCK_GLYPHS = '-0123456789:';

const WEEKDAYS = ['domenica', 'lunedì', 'martedì', 'mercoledì', 'giovedì', 'venerdì', 'sabato'];
const MONTHS = ['gennaio', 'febbraio', 'marzo', 'aprile', 'maggio', 'giugno', 'luglio', 'agosto', 'settembre', 'ottobre', 'novembre', 'dicembre'];

// Come mapCase di values.js: ASCII e U+00E0–U+00FE (tranne ÷) / U+00C0–U+00DE (tranne ×).
function mapCase(s, upper) {
  let out = '';
  for (const ch of s) {
    let c = ch.codePointAt(0);
    if (upper && ((c >= 0x61 && c <= 0x7a) || (c >= 0xe0 && c <= 0xfe && c !== 0xf7))) c -= 0x20;
    else if (!upper && ((c >= 0x41 && c <= 0x5a) || (c >= 0xc0 && c <= 0xde && c !== 0xd7))) c += 0x20;
    out += String.fromCodePoint(c);
  }
  return out;
}

// Rune uniche in ordine di codepoint.
const alpha = (s) => [...new Set(s)].sort((a, b) => a.codePointAt(0) - b.codePointAt(0)).join('');

const DIG = '0123456789';
// nome corto → [esempio, alfabeto]; alfabeto null = testo libero
const SPEC = {
  nome: ['api-server', null], stato: ['Modifico src/api.ts', null], 'attività': ['Bash: npm run test', null], riassunto: ['Guida aggiornata', null],
  meta: ['3/7 task  ·  12 min', null], contesto: ['42%', alpha(DIG + '%')], task: ['3/7', alpha(DIG + '/')], da: ['12 min', alpha(DIG + ' min h')],
  sub: ['2', alpha(DIG)], test: ['✓', '✓✗'],
  ora: ['10:30', CLOCK_GLYPHS], data: ['domenica 11 ottobre', alpha(WEEKDAYS.join(' ') + ' ' + MONTHS.join(' ') + DIG + ' ')],
  giorno: ['domenica', alpha(WEEKDAYS.join(' '))], sessioni: ['3', alpha(DIG)], inattesa: ['1', alpha(DIG)], batteria: ['78%', alpha(DIG + '%')],
  autonomia: ['Autonomia: circa 3 giorni con il tuo uso', alpha('Autonomia: circa giorni ore con il tuo uso (dalla scarica) calibra per una stima in corso ' + DIG)],
  wifi: ['CasaWiFi', null], pc: ['calamaio.local', null], 'giornata.mancano': ['6 h 25', alpha(DIG + ' h min finita')],
  'giornata.fine': ['18:00', alpha(DIG + ':')], 'oggi.task': ['4', alpha(DIG)], 'oggi.righe': ['+120 −30', alpha(DIG + '+−- ')],
  'oggi.costo': ['1,20 $', alpha(DIG + ',- $')], 'limiti.5h': ['37%', alpha(DIG + '%—')], 'limiti.7g': ['12%', alpha(DIG + '%—')],
  'mascotte.livello': ['3', alpha(DIG + '—')], 'mascotte.xp': ['66/417 XP', alpha(DIG + '/- XP')], 'mascotte.energia': ['80%', alpha(DIG + '%-')],
  co2: ['620 ppm', alpha(DIG + ' ppm')], versione: ['0.13.0-ed', null],
};

// Un valore che manca da SPEC conta come testo libero (mai un'eccezione al caricamento).
export function buildValueGlyphs(names, spec = SPEC) {
  return Object.freeze(Object.fromEntries(names.map((name) => {
    const [example, g] = spec[name.replace(/^(sessione|riga)\./, '')] ?? ['', null];
    return [name, { glyphs: g ?? '', free: g == null, example }];
  })));
}

export const VALUE_GLYPHS = buildValueGlyphs(Object.keys(VALUES));

// Glifi di un carattere (Set di codepoint, senza le legature U+E000/U+E001); null se la combinazione non esiste.
export function fontGlyphs(family, size, weight) {
  const pix = family === 'pix';
  if (!pix && family !== 'ui') return null;
  const w = pix ? (weight <= 500 ? 400 : 700) : weight <= 500 ? 500 : weight <= 600 ? 600 : 700;
  const f = FONTS.find((x) => x.pix === pix && x.size === size && x.weight === w);
  return f ? new Set([...f.cps].filter((c) => c !== 0xe000 && c !== 0xe001)) : null;
}

const label = (font) => (font.family === 'pix' ? 'Silkscreen ' : 'Chakra Petch ') + font.size;
const missing = (have, s) => [...new Set(s)].filter((ch) => !have.has(ch.codePointAt(0))).join('');

// Pezzi come parseTemplate di lower.js, ma con il nome del valore (sconosciuto = ignorato).
function pieces(s) {
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
      out.push({ name, filter: filter === 'maiuscolo' ? 1 : filter === 'minuscolo' ? 2 : 0 });
      i = j;
      continue;
    }
    lit += c;
  }
  flush();
  return out;
}

export function glyphWarnings(el) {
  try {
    if (!el || (el.type !== 'text' && el.type !== 'clock')) return [];
    const f = el.font && typeof el.font === 'object' ? el.font : {};
    const font = { family: f.family === 'pix' ? 'pix' : 'ui', size: Number.isInteger(f.size) ? f.size : 10, weight: Number.isInteger(f.weight) ? f.weight : 500 };
    const have = fontGlyphs(font.family, font.size, font.weight);
    if (!have) return [];
    const lbl = label(font), upper = el.upper === true;
    const segs = el.type === 'clock' ? [{ name: 'ora', filter: 0 }] : pieces(typeof el.text === 'string' ? el.text : '');
    const out = [], seen = new Set();
    for (const s of segs) {
      if (s.lit !== undefined) {
        const miss = missing(have, upper ? mapCase(s.lit, true) : s.lit);
        if (miss) out.push(`${lbl} non ha «${miss}»: si vedrà «?»`);
        continue;
      }
      if (!Object.hasOwn(VALUE_GLYPHS, s.name) || seen.has(s.name)) continue;
      seen.add(s.name);
      const v = VALUE_GLYPHS[s.name];
      if (v.free) {
        let full = true;
        for (let c = 0x20; c <= 0x7e; c++) if (!have.has(c)) { full = false; break; }
        if (!full) {
          const punct = [...have].sort((a, b) => a - b).map((c) => String.fromCodePoint(c)).filter((ch) => !/[ 0-9a-zA-Z]/.test(ch)).join('');
          out.push(`{${s.name}} è testo libero: ${lbl} ha solo cifre, minuscole e «${punct}»`);
        }
        continue;
      }
      let g = v.glyphs;
      if (s.filter === 1) g = mapCase(g, true); else if (s.filter === 2) g = mapCase(g, false);
      if (upper) g = mapCase(g, true);
      const miss = missing(have, alpha(g));
      if (miss) out.push(`{${s.name}} può mostrare «${miss}», che ${lbl} non ha: si vedrà «?»`);
    }
    return out;
  } catch {
    return [];
  }
}
