// Testi a schermo dell'editor, estratti dal codice: letterali delle stringhe (commenti esclusi) dei file
// dell'editor e degli avvisi dei glifi, più testi e aria-label di editor.html. Serve alla prova che confronta
// l'elenco di DECISIONI («Testi nuovi a schermo dell'editor») col codice (M2 della revisione finale).
// Ogni testo diventa i suoi pezzi fissi (le parti `${…}` si tolgono): la prova cerca i pezzi, non la forma esatta,
// così N, <motivo> e «…» in DECISIONI vanno bene.
import fs from 'node:fs';
import path from 'node:path';

// letterali di un sorgente JS: [pezzi] per ciascuno (un modello `a ${x} b` → ['a ', ' b'])
export function jsLiterals(src) {
  const out = [];
  let i = 0;
  const str = (q) => {
    let s = ''; i++;
    while (i < src.length && src[i] !== q) { if (src[i] === '\\') { s += src[i + 1]; i += 2; continue; } s += src[i++]; }
    i++; return s;
  };
  const tpl = () => {
    const parts = []; let s = ''; i++;
    while (i < src.length && src[i] !== '`') {
      if (src[i] === '\\') { s += src[i + 1]; i += 2; continue; }
      if (src[i] === '$' && src[i + 1] === '{') {
        parts.push(s); s = ''; i += 2;
        for (let d = 1; d && i < src.length;) {
          const c = src[i];
          if (c === "'" || c === '"') { out.push([str(c)]); continue; }
          if (c === '`') { out.push(tpl()); continue; }
          if (c === '{') d++; else if (c === '}') d--;
          i++;
        }
        continue;
      }
      s += src[i++];
    }
    i++; parts.push(s); return parts;
  };
  while (i < src.length) {
    const c = src[i];
    if (c === '/' && src[i + 1] === '/') { while (i < src.length && src[i] !== '\n') i++; continue; }
    if (c === '/' && src[i + 1] === '*') { i = src.indexOf('*/', i) + 2; continue; }
    if (c === "'" || c === '"') { out.push([str(c)]); continue; }
    if (c === '`') { out.push(tpl()); continue; }
    // espressione regolare: una / dove non può stare una divisione
    if (c === '/' && /[=(,:;!&|?{}[\n]$/.test(src.slice(Math.max(0, i - 40), i).trimEnd().slice(-1) || '\n')) {
      i++;
      for (let cls = false; i < src.length; i++) {
        const d = src[i];
        if (d === '\\') { i++; continue; }
        if (d === '[') cls = true; else if (d === ']') cls = false; else if (d === '/' && !cls) break;
      }
      i++; continue;
    }
    i++;
  }
  return out;
}

// testi di editor.html: nodi di testo e aria-label, senza <head> e senza la barra delle pagine (non sono nuovi)
export function htmlTexts(src) {
  const body = src.replace(/<head>[\s\S]*?<\/head>/, '').replace(/<nav class="topbar"[\s\S]*?<\/nav>/, '');
  const out = [];
  for (const m of body.matchAll(/>([^<>]+)</g)) out.push([m[1].trim()]);
  for (const m of body.matchAll(/aria-label="([^"]+)"/g)) out.push([m[1]]);
  return out.filter((p) => p[0]);
}

// non sono testi a schermo: classi, selettori, media query, tasti, nomi degli errori, colori, CSS
const NOT_TEXT = [/^(btn|ed-)[\w -]*$/, /^[.[#(]/, /rgba\(|translate\(/, /^\w+Error$/, /^(Escape|Enter|Tab|Delete|Backspace|Arrow)$/, /^button, select/];
// letterali che non arrivano allo schermo (solo console)
const IGNORE = new Set(['editor: operazione non riuscita']);
const PUNCT = /^[\s·:,.;{}«»()—–\-?…]+|[\s·:,.;{}«»()—–\-…]+$/g;

// pezzi fissi da cercare: ≥ 4 caratteri dopo aver tolto la punteggiatura ai capi
export function fragments(parts) {
  return parts.map((p) => p.replace(PUNCT, '').replace(/ /g, ' ')).filter((p) => p.length >= 4 && /[a-zà-ù]{2}/i.test(p));
}

const isText = (parts) => {
  const j = parts.join('…');
  if (!/[a-zàèéìòù]{2,}/.test(j) || IGNORE.has(j) || NOT_TEXT.some((r) => r.test(j.trim()))) return false;
  // una parola sola minuscola è una chiave (home, text, …), non un testo
  return /\s/.test(j.trim()) || /^[A-ZÀ-Ý«]/.test(j);
};

// → [{ file, text, frags }]
export function editorStrings(jsRoot, htmlFile) {
  const files = ['editor.js', ...fs.readdirSync(path.join(jsRoot, 'editor')).filter((f) => f.endsWith('.js') && f !== 'testdata.js').map((f) => 'editor/' + f), 'theme/glyphs.js'];
  const out = [];
  for (const f of files) {
    for (const parts of jsLiterals(fs.readFileSync(path.join(jsRoot, f), 'utf8'))) {
      // glyphs.js: solo i testi degli avvisi (gli altri letterali sono tabelle di caratteri)
      if (f === 'theme/glyphs.js' && !parts.join('').includes('«')) continue;
      if (isText(parts)) out.push({ file: f, text: parts.join('…'), frags: fragments(parts) });
    }
  }
  for (const parts of htmlTexts(fs.readFileSync(htmlFile, 'utf8'))) if (isText(parts)) out.push({ file: 'editor.html', text: parts[0], frags: fragments(parts) });
  return out.filter((x) => x.frags.length);
}

// la voce di DECISIONI, fino alla voce successiva dello stesso livello
export function decisionList(md) {
  const k = md.indexOf("- **Testi nuovi a schermo dell'editor**");
  if (k < 0) return '';
  const rest = md.slice(k + 1);
  const end = rest.search(/\n(- \*\*|## )/);
  return md.slice(k, end < 0 ? md.length : k + 1 + end).replace(/ /g, ' ');
}
