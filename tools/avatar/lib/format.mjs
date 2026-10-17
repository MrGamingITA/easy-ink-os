// Formato avatar Calamaio v1: costanti, lettura, ripieghi delle pose, validazione.
// La specifica è docs/AVATAR.md: se questo file e la specifica divergono, si corregge il codice.
import fs from 'node:fs';
import path from 'node:path';

export const FORMAT = 'calamaio-avatar';
export const VERSION = 1;
export const W = 28;
export const H = 25;
// Ordine fisso: è anche l'ordine dei bit di poseMask nel formato binario.
// run (bit 9) è l'ultima: le pose 0-8 restano dove erano nel binario (docs/AVATAR.md §12).
export const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy', 'run'];
export const REQUIRED = POSES.slice(0, 6);
export const FALLBACK = { compact: 'working', tired: 'idle', stuffy: 'idle', run: 'working' };
export const SYMBOLS = { '.': 0, '#': 1, '+': 2 };  // trasparente, inchiostro, carta
export const MAX_JSON_BYTES = 48 * 1024;
export const MAX_BIN_BYTES = 16 * 1024;
// Zona riservata alla nuvoletta (in alto a destra) e pose che la usano, come la Goccia.
export const BUBBLE = { x0: 17, x1: 27, y0: 0, y1: 8 };
export const BUBBLE_POSES = ['working', 'permission', 'input', 'compact'];
// Soglie euristiche, ricavate dalla Goccia (vedi docs/AVATAR.md, «Controlli di leggibilità»).
export const MIN_POSE_DIFF = 16;     // pixel diversi tra i fotogrammi 0 di due pose (Goccia: minimo 22, permission/input)
export const MIN_INK = 120;          // pixel d'inchiostro minimi per fotogramma (Goccia: minimo 195, error)
export const MAX_FRAME_DIFF = 200;   // pixel diversi tra fotogrammi consecutivi, ~29% di 700 (Goccia: massimo 124, salto di done)
export const MAX_CHECKER = 2;        // finestre 3×3 a scacchiera perfetta, cioè retino fitto (Goccia: 0)
// Corsa (posa run, docs/AVATAR.md §12): la figura corre sul fronte della barra, verso destra.
export const RUN_EDGE = W - 1;       // colonna del bordo destro in tutti i fotogrammi: si allinea al fronte della barra
export const RUN_FOOT = H - 1;       // riga dei piedi: appoggia sul bordo superiore della barra
export const MIN_RUN_STEP = 12;      // pixel diversi tra fotogrammi consecutivi: sotto, la corsa sembra ferma
export const MAX_RUN_INK_SPREAD = 0.25; // (max − min)/max dell'inchiostro tra i fotogrammi: oltre, la figura «pulsa»
// Punti di aggancio degli accessori (docs/AVATAR.md §13), nell'ordine del blocco binario CANC.
export const ANCHOR_NAMES = ['head-top', 'eyes', 'mouth', 'neck', 'hand'];

const ID_RE = /^[a-z0-9]+(-[a-z0-9]+)*$/;

export function load(file) {
  const raw = fs.readFileSync(file);
  return { av: JSON.parse(raw.toString('utf8')), bytes: raw.length, file: path.resolve(file) };
}

// Fotogrammi effettivi di una posa (con il ripiego per le facoltative).
export function poseFrames(av, pose) {
  if (av.poses[pose]) return av.poses[pose];
  if (FALLBACK[pose] && av.poses[FALLBACK[pose]]) return av.poses[FALLBACK[pose]];
  return null;
}

// Fotogramma come matrice di codici 0/1/2.
export function grid(frame) {
  return frame.map((row) => Array.from(row, (ch) => SYMBOLS[ch]));
}

export function inkCount(frame) {
  let n = 0;
  for (const row of frame) for (const ch of row) if (ch === '#') n++;
  return n;
}

export function diff(a, b) {
  let n = 0;
  for (let y = 0; y < a.length; y++) for (let x = 0; x < a[y].length; x++) if (a[y][x] !== b[y][x]) n++;
  return n;
}

// Retino fitto: finestre 3×3 a scacchiera perfetta (il centro uguale ai 4 angoli, diverso dai 4 lati).
// Le diagonali sottili (braccia, occhi a X) non contano: solo il retino sfarfalla sull'e-ink.
export function checkerCount(frame) {
  let n = 0;
  const I = (x, y) => frame[y][x] === '#';
  for (let y = 1; y + 1 < frame.length; y++) {
    for (let x = 1; x + 1 < frame[y].length; x++) {
      const c = I(x, y);
      if (I(x - 1, y - 1) === c && I(x + 1, y - 1) === c && I(x - 1, y + 1) === c && I(x + 1, y + 1) === c &&
          I(x - 1, y) !== c && I(x + 1, y) !== c && I(x, y - 1) !== c && I(x, y + 1) !== c) n++;
    }
  }
  return n;
}

// Bordo destro (colonna più a destra con qualcosa di disegnato) e riga più bassa di un fotogramma.
export function extent(frame) {
  let right = -1, bottom = -1;
  frame.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] !== '.') { if (x > right) right = x; if (y > bottom) bottom = y; } });
  return { right, bottom };
}

// Controlli della corsa: bordo destro netto e fermo, piedi sulla riga in basso, passo visibile, sagoma coerente.
function runChecks(frs, Wn, stats) {
  const inks = frs.map(inkCount);
  frs.forEach((fr, f) => {
    const { right } = extent(fr);
    if (right !== RUN_EDGE) Wn(`run[${f}]: bordo destro in colonna ${right}, deve essere ${RUN_EDGE} in tutti i fotogrammi (si allinea al fronte della barra)`);
    let edge = 0;
    for (let y = 0; y < fr.length; y++) if (fr[y][RUN_EDGE] === '#') edge++;
    if (right === RUN_EDGE && edge < 3) Wn(`run[${f}]: solo ${edge} pixel d'inchiostro in colonna ${RUN_EDGE}: il bordo destro non è netto`);
    if (!fr[RUN_FOOT].includes('#')) Wn(`run[${f}]: niente inchiostro sulla riga ${RUN_FOOT}: i piedi non toccano la barra`);
  });
  for (let f = 0; f < frs.length; f++) {
    const g = (f + 1) % frs.length, d = diff(frs[f], frs[g]);
    if (d < MIN_RUN_STEP) Wn(`run: solo ${d} pixel cambiano tra i fotogrammi ${f} e ${g} (minimo ${MIN_RUN_STEP}): la corsa sembra ferma`);
  }
  const spread = (Math.max(...inks) - Math.min(...inks)) / Math.max(...inks);
  stats.runInkSpread = Math.round(spread * 100) / 100;
  if (spread > MAX_RUN_INK_SPREAD) Wn(`run: l'inchiostro varia da ${Math.min(...inks)} a ${Math.max(...inks)} pixel tra i fotogrammi (oltre il ${MAX_RUN_INK_SPREAD * 100}%): la sagoma non resta la stessa`);
}

function utf8Len(s) { return Buffer.byteLength(String(s), 'utf8'); }

// Restituisce { errors, warnings, stats }. errors vuoto = avatar valido.
export function validate(av, opts = {}) {
  const errors = [], warnings = [], stats = {};
  const E = (m) => errors.push(m), Wn = (m) => warnings.push(m);
  if (!av || typeof av !== 'object' || Array.isArray(av)) return { errors: ['il file non contiene un oggetto JSON'], warnings, stats };
  if (av.format !== FORMAT) E(`format deve essere "${FORMAT}"`);
  if (av.version !== VERSION) E(`version deve essere ${VERSION}`);
  if (typeof av.id !== 'string' || !ID_RE.test(av.id) || av.id.length > 24) E('id: kebab-case [a-z0-9-], al massimo 24 caratteri');
  if (typeof av.name !== 'string' || !av.name.trim() || utf8Len(av.name) > 24) E('name: non vuoto, al massimo 24 byte UTF-8');
  if (typeof av.author !== 'string' || !av.author.trim()) E('author: obbligatorio');
  if (typeof av.description !== 'string' || utf8Len(av.description) > 120) E('description: stringa di al massimo 120 byte UTF-8');
  if (av.w !== W) E(`w deve essere ${W}`);
  if (av.h !== H) E(`h deve essere ${H}`);
  if (av.frames !== 2 && av.frames !== 4) E('frames deve essere 2 o 4');
  const known = new Set(['format', 'version', 'id', 'name', 'author', 'description', 'w', 'h', 'frames', 'poses', 'anchors']);
  for (const k of Object.keys(av)) if (!known.has(k)) E(`campo sconosciuto: ${k}`);
  if (opts.bytes !== undefined) {
    stats.jsonBytes = opts.bytes;
    if (opts.bytes > MAX_JSON_BYTES) E(`file JSON di ${opts.bytes} byte: il massimo è ${MAX_JSON_BYTES}`);
  }
  if (!av.poses || typeof av.poses !== 'object' || Array.isArray(av.poses)) { E('poses mancante'); return { errors, warnings, stats }; }
  for (const p of Object.keys(av.poses)) if (!POSES.includes(p)) E(`posa sconosciuta: ${p}`);
  for (const p of REQUIRED) if (!av.poses[p]) E(`posa obbligatoria mancante: ${p}`);
  if (errors.length) return { errors, warnings, stats };

  // Struttura dei fotogrammi.
  for (const [p, frames] of Object.entries(av.poses)) {
    if (!Array.isArray(frames) || frames.length !== av.frames) { E(`${p}: servono ${av.frames} fotogrammi, trovati ${Array.isArray(frames) ? frames.length : 0}`); continue; }
    frames.forEach((fr, f) => {
      if (!Array.isArray(fr) || fr.length !== H) { E(`${p}[${f}]: servono ${H} righe`); return; }
      fr.forEach((row, y) => {
        if (typeof row !== 'string' || row.length !== W) E(`${p}[${f}] riga ${y}: servono ${W} simboli`);
        else if (!/^[.#+]*$/.test(row)) E(`${p}[${f}] riga ${y}: simboli ammessi solo . # +`);
      });
    });
  }
  if (errors.length) return { errors, warnings, stats };
  if (av.anchors !== undefined) anchorErrors(av, av.anchors).forEach(E);

  // Controlli di leggibilità (euristici).
  const present = POSES.filter((p) => av.poses[p]);
  stats.poses = present.length;
  stats.minInk = Infinity;
  for (const p of present) {
    av.poses[p].forEach((fr, f) => {
      const ink = inkCount(fr);
      stats.minInk = Math.min(stats.minInk, ink);
      if (ink === 0) E(`${p}[${f}]: silhouette vuota`);
      else if (ink < MIN_INK) Wn(`${p}[${f}]: solo ${ink} pixel d'inchiostro (minimo consigliato ${MIN_INK}): a scala 1 non si legge`);
      const ck = checkerCount(fr);
      if (ck > MAX_CHECKER) Wn(`${p}[${f}]: ${ck} blocchi a scacchiera, il retino fitto sfarfalla sull'e-ink`);
    });
    const fr = av.poses[p];
    for (let f = 0; f < fr.length; f++) {
      const g = (f + 1) % fr.length;
      const d = diff(fr[f], fr[g]);
      if (d > MAX_FRAME_DIFF) Wn(`${p}: ${d} pixel cambiano tra i fotogrammi ${f} e ${g} (massimo consigliato ${MAX_FRAME_DIFF}): sporca il refresh parziale`);
    }
    if (av.frames === 4 && diff(fr[0], fr[1]) === 0 && diff(fr[0], fr[2]) + diff(fr[0], fr[3]) > 0) {
      Wn(`${p}: i fotogrammi 0 e 1 sono uguali, sui pannelli a 2 fotogrammi la posa resta ferma`);
    }
  }
  if (av.poses.run) runChecks(av.poses.run, Wn, stats);
  stats.minPoseDiff = Infinity;
  for (let i = 0; i < present.length; i++) {
    for (let j = i + 1; j < present.length; j++) {
      const a = present[i], b = present[j];
      const d = diff(av.poses[a][0], av.poses[b][0]);
      if (d < stats.minPoseDiff) { stats.minPoseDiff = d; stats.closest = `${a}/${b}`; }
      if (d < MIN_POSE_DIFF) E(`${a} e ${b} differiscono di ${d} pixel nel fotogramma 0 (minimo ${MIN_POSE_DIFF}): non si distinguono`);
    }
  }
  // Nuvoletta: nelle pose senza nuvoletta l'angolo può essere usato; in quelle che la usano lo deve occupare qualcosa.
  // L'ingombro della nuvoletta è quello della Goccia: riquadro x 18..26, y 0..6, più la coda (18,7) e (17,8).
  const inBubble = (x, y) => (x >= 18 && x <= 26 && y <= 6) || (x === 18 && y === 7) || (x === 17 && y === 8);
  for (const p of present.filter((q) => BUBBLE_POSES.includes(q))) {
    const fr0 = av.poses[p][0];
    let n = 0;
    for (let y = BUBBLE.y0; y <= BUBBLE.y1; y++) for (let x = BUBBLE.x0; x <= BUBBLE.x1; x++) if (fr0[y][x] !== '.') n++;
    if (n === 0) Wn(`${p}: angolo in alto a destra vuoto, manca il simbolo dello stato (nuvoletta)`);
    av.poses[p].forEach((fr, f) => {
      const out = [];
      for (let y = BUBBLE.y0; y <= BUBBLE.y1; y++) for (let x = BUBBLE.x0; x <= BUBBLE.x1; x++) if (fr[y][x] !== '.' && !inBubble(x, y)) out.push(`(${x},${y})`);
      if (out.length) Wn(`${p}[${f}]: ${out.length} pixel nell'angolo della nuvoletta fuori dal suo ingombro ${out.slice(0, 4).join(' ')}: il corpo tocca il simbolo dello stato`);
    });
  }
  return { errors, warnings, stats };
}

// Ancore (docs/AVATAR.md §13): per ogni posa disegnata, un elemento per fotogramma con i 5 punti, [x, y] o null.
export function anchorErrors(av, anchors) {
  const errors = [], E = (m) => errors.push(m);
  if (!anchors || typeof anchors !== 'object' || Array.isArray(anchors)) return ['anchors: deve essere un oggetto posa → fotogrammi'];
  const present = POSES.filter((p) => av.poses && av.poses[p]);
  for (const p of Object.keys(anchors)) if (!present.includes(p)) E(`anchors: posa ${p} assente dal disegno`);
  for (const p of present) {
    const frs = anchors[p];
    if (!Array.isArray(frs)) { E(`anchors.${p}: mancano le ancore della posa`); continue; }
    if (frs.length !== av.frames) { E(`anchors.${p}: servono ${av.frames} fotogrammi, trovati ${frs.length}`); continue; }
    frs.forEach((pt, f) => {
      const at = `anchors.${p}[${f}]`;
      if (!pt || typeof pt !== 'object' || Array.isArray(pt)) { E(`${at}: serve un oggetto con i punti ${ANCHOR_NAMES.join(', ')}`); return; }
      for (const k of Object.keys(pt)) if (!ANCHOR_NAMES.includes(k)) E(`${at}: punto sconosciuto ${k}`);
      for (const n of ANCHOR_NAMES) {
        if (!(n in pt)) { E(`${at}: manca ${n} (null se il punto non esiste)`); continue; }
        const v = pt[n];
        if (v === null) continue;
        if (!Array.isArray(v) || v.length !== 2 || !v.every(Number.isInteger)) E(`${at}.${n}: serve [x, y] intero oppure null`);
        else if (v[0] < 0 || v[0] >= W || v[1] < 0 || v[1] >= H) E(`${at}.${n}: (${v[0]},${v[1]}) fuori dal riquadro ${W}×${H}`);
      }
    });
  }
  return errors;
}

// Un fotogramma di ancore su una riga: {"head-top": [12, 4], "eyes": …}.
export function anchorLine(pt) {
  return '{' + ANCHOR_NAMES.map((n) => `"${n}": ${pt[n] ? `[${pt[n][0]}, ${pt[n][1]}]` : 'null'}`).join(', ') + '}';
}
export function serializeAnchors(anchors, indent) {
  const ps = POSES.filter((p) => anchors[p]);
  return '{\n' + ps.map((p, i) => `${indent}  "${p}": [\n` + anchors[p].map((pt) => `${indent}    ${anchorLine(pt)}`).join(',\n') +
    `\n${indent}  ]${i < ps.length - 1 ? ',' : ''}\n`).join('') + `${indent}}`;
}

// Avatar «liberi» (docs/AVATAR.md, «Avatar liberi e in uso»).
// library: [{id, name}], assignments: { projects: {dir: id}, sessions: {sid: id} },
// sessions: [{id, name, cwd, archived}], current: id della sessione corrente.
export function avatarOf(session, assignments) {
  const a = assignments || {};
  if (a.sessions && a.sessions[session.id]) return a.sessions[session.id];
  if (a.projects && a.projects[session.cwd]) return a.projects[session.cwd];
  return 'goccia';
}
export function usage(library, assignments, sessions, current) {
  const used = new Map();
  for (const s of sessions || []) {
    if (s.archived || s.id === current) continue;
    const id = avatarOf(s, assignments);
    if (!used.has(id)) used.set(id, []);
    used.get(id).push(s.name);
  }
  return library.map((av) => ({
    id: av.id, name: av.name,
    state: used.has(av.id) ? 'in-uso' : 'libero',
    usedBy: used.get(av.id) || [],
    default: av.id === 'goccia'
  }));
}
// Da proporre: liberi, Goccia esclusa (resta il ripiego, mai una scelta «distintiva»).
export function freeChoices(library, assignments, sessions, current) {
  return usage(library, assignments, sessions, current).filter((u) => u.state === 'libero' && !u.default);
}

// Serializza con una riga del disegno per riga di testo, così il file si legge e si modifica a mano.
export function serialize(av) {
  const head = {};
  for (const k of ['format', 'version', 'id', 'name', 'author', 'description', 'w', 'h', 'frames']) head[k] = av[k];
  let out = JSON.stringify(head, null, 2).replace(/\n}$/, ',\n  "poses": {\n');
  const ps = POSES.filter((p) => av.poses[p]);
  ps.forEach((p, i) => {
    out += `    "${p}": [\n`;
    av.poses[p].forEach((fr, f) => {
      out += '      [\n' + fr.map((r) => `        "${r}"`).join(',\n') + '\n      ]' + (f < av.poses[p].length - 1 ? ',' : '') + '\n';
    });
    out += '    ]' + (i < ps.length - 1 ? ',' : '') + '\n';
  });
  // Ancore facoltative in coda, un fotogramma per riga: senza ancore il testo resta quello di prima.
  if (av.anchors) return out + '  },\n  "anchors": ' + serializeAnchors(av.anchors, '  ') + '\n}\n';
  return out + '  }\n}\n';
}
