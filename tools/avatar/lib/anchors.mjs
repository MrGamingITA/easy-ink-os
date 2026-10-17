// Punti di aggancio degli accessori negli avatar (docs/AVATAR.md §13).
// Le ancore stanno nel campo facoltativo `anchors` del JSON oppure, per la libreria predefinita, in un file
// accanto (`ancore/<id>.anchors.json`, legato al disegno dal CRC del CAVT). Il CAVT non le contiene mai:
// viaggiano nel blocco separato CANC.
import fs from 'node:fs';
import path from 'node:path';
import { POSES, FALLBACK, W, H, ANCHOR_NAMES, anchorErrors, serializeAnchors, load } from './format.mjs';
import { pack, crc32 } from './pack.mjs';

export { ANCHOR_NAMES };

// Punto di aggancio `name` della posa e del fotogramma (f mod frames, ripiego delle pose facoltative).
// null se l'avatar non ha ancore o il punto non esiste in quella posa: mai posizioni inventate.
export function getAnchor(av, pose, frame, name) {
  if (!av || !av.anchors || !ANCHOR_NAMES.includes(name)) return null;
  let p = pose;
  if (!av.poses[p] && FALLBACK[p] && av.poses[FALLBACK[p]]) p = FALLBACK[p];
  const frs = av.anchors[p];
  if (!Array.isArray(frs) || !frs.length) return null;
  const n = frs.length, pt = frs[((frame % n) + n) % n];
  const v = pt && pt[name];
  return Array.isArray(v) ? [v[0], v[1]] : null;
}

// ---------------------------------------------------------------- file accanto (libreria predefinita)
export const SIDECAR_FORMAT = 'calamaio-anchors';
const hex = (n) => (n >>> 0).toString(16).padStart(8, '0');
export const avatarCrc = (av) => pack(av).readUInt32LE(8);

export function sidecarPath(avatarFile, id) {
  return path.join(path.dirname(path.resolve(avatarFile)), 'ancore', id + '.anchors.json');
}
export function makeSidecar(av, anchors) {
  return { format: SIDECAR_FORMAT, version: 1, avatar: av.id, avatarCrc: hex(avatarCrc(av)), anchors };
}
export function serializeSidecar(sc) {
  return '{\n' + `  "format": ${JSON.stringify(sc.format)},\n  "version": ${sc.version},\n  "avatar": ${JSON.stringify(sc.avatar)},\n` +
    `  "avatarCrc": ${JSON.stringify(sc.avatarCrc)},\n  "anchors": ${serializeAnchors(sc.anchors, '  ')}\n}\n`;
}
export function sidecarErrors(av, sc) {
  const e = [];
  if (!sc || typeof sc !== 'object') return ['file delle ancore: non è un oggetto JSON'];
  if (sc.format !== SIDECAR_FORMAT) e.push(`file delle ancore: format deve essere "${SIDECAR_FORMAT}"`);
  if (sc.version !== 1) e.push('file delle ancore: version deve essere 1');
  for (const k of Object.keys(sc)) if (!['format', 'version', 'avatar', 'avatarCrc', 'anchors'].includes(k)) e.push(`file delle ancore: campo sconosciuto ${k}`);
  if (sc.avatar !== av.id) e.push(`file delle ancore: è per l'avatar "${sc.avatar}", non "${av.id}"`);
  if (av.anchors !== undefined) e.push('ancore in due posti: nel JSON dell\'avatar e nel file accanto (ne serve uno solo)');
  const want = hex(avatarCrc(av));
  if (sc.avatarCrc !== want) e.push(`file delle ancore: fatte per un altro disegno (CRC ${sc.avatarCrc}, il CAVT di oggi è ${want}): rifarle con anchors --auto e rivederle`);
  return e.concat(anchorErrors(av, sc.anchors));
}
// Carica l'avatar con le sue ancore: dal JSON, oppure dal file accanto se c'è. source = 'json' | 'sidecar' | null.
export function loadWithAnchors(file) {
  const r = load(file);
  if (r.av.anchors) return { ...r, source: 'json', errors: anchorErrors(r.av, r.av.anchors) };
  const sp = sidecarPath(file, r.av.id);
  if (!fs.existsSync(sp)) return { ...r, source: null, errors: [] };
  const sc = JSON.parse(fs.readFileSync(sp, 'utf8'));
  const errors = sidecarErrors(r.av, sc);
  if (errors.length) return { ...r, source: null, sidecar: sp, errors };
  return { ...r, av: { ...r.av, anchors: sc.anchors }, source: 'sidecar', sidecar: sp, errors };
}

// ---------------------------------------------------------------- blocco binario CANC v1
// Little-endian. Intestazione di 20 byte, poi per ogni posa presente (ordine di POSES) e fotogramma i 5 punti
// nell'ordine di ANCHOR_NAMES, 2 byte ciascuno (x, y); 0xFF 0xFF = punto assente.
export const ANCHOR_MAGIC = 'CANC';
export const ANCHOR_HEADER = 20;
export const anchorsSize = (nPoses, frames) => ANCHOR_HEADER + nPoses * frames * ANCHOR_NAMES.length * 2;

export function packAnchors(av) {
  if (!av.anchors) throw new Error(`avatar ${av.id} senza ancore`);
  const errs = anchorErrors(av, av.anchors);
  if (errs.length) throw new Error('ancore non valide: ' + errs[0]);
  const present = POSES.filter((p) => av.poses[p]);
  let mask = 0;
  present.forEach((p) => { mask |= 1 << POSES.indexOf(p); });
  const total = anchorsSize(present.length, av.frames), buf = Buffer.alloc(total);
  buf.write(ANCHOR_MAGIC, 0, 'ascii');
  buf[4] = 1; buf[5] = ANCHOR_HEADER; buf.writeUInt16LE(total, 6);
  buf.writeUInt32LE(avatarCrc(av), 12);
  buf.writeUInt16LE(mask, 16); buf[18] = av.frames; buf[19] = ANCHOR_NAMES.length;
  let off = ANCHOR_HEADER;
  for (const p of present) for (const pt of av.anchors[p]) for (const n of ANCHOR_NAMES) {
    const v = pt[n];
    buf[off] = v ? v[0] : 0xFF; buf[off + 1] = v ? v[1] : 0xFF;
    off += 2;
  }
  buf.writeUInt32LE(crc32(buf, 12, total), 8);
  return buf;
}

export function unpackAnchors(buf) {
  if (buf.length < ANCHOR_HEADER || buf.toString('ascii', 0, 4) !== ANCHOR_MAGIC) throw new Error('magia CANC assente');
  if (buf[4] !== 1) throw new Error(`versione ${buf[4]} non supportata`);
  if (buf[5] !== ANCHOR_HEADER) throw new Error(`intestazione di ${buf[5]} byte, attesa ${ANCHOR_HEADER}`);
  const total = buf.readUInt16LE(6);
  if (total !== buf.length) throw new Error(`lunghezza dichiarata ${total}, reale ${buf.length}`);
  if (buf.readUInt32LE(8) !== crc32(buf, 12, total)) throw new Error('checksum errato');
  const avatarCrc = buf.readUInt32LE(12), mask = buf.readUInt16LE(16), frames = buf[18];
  if (buf[19] !== ANCHOR_NAMES.length) throw new Error(`punti per fotogramma ${buf[19]}, attesi ${ANCHOR_NAMES.length}`);
  if (frames !== 2 && frames !== 4) throw new Error('fotogrammi devono essere 2 o 4');
  if (mask >> POSES.length) throw new Error('bit di posa sconosciuti');
  const present = POSES.filter((p, i) => mask & (1 << i));
  if (total !== anchorsSize(present.length, frames)) throw new Error('dimensione incoerente con pose e fotogrammi');
  const anchors = {};
  let off = ANCHOR_HEADER;
  for (const p of present) {
    anchors[p] = [];
    for (let f = 0; f < frames; f++) {
      const pt = {};
      for (const n of ANCHOR_NAMES) {
        const x = buf[off], y = buf[off + 1];
        off += 2;
        if (x === 0xFF && y === 0xFF) { pt[n] = null; continue; }
        if (x >= W || y >= H) throw new Error(`${p}[${f}].${n}: (${x},${y}) fuori dal riquadro ${W}×${H}`);
        pt[n] = [x, y];
      }
      anchors[p].push(pt);
    }
  }
  return { avatarCrc, poseMask: mask, frames, anchors };
}

// ---------------------------------------------------------------- proposta automatica (anchors --auto)
// Si misura un fotogramma di riferimento (permission[0]: occhi aperti, a terra; per la corsa run[0]) e si
// insegue lo spostamento rigido del corpo negli altri fotogrammi. Il risultato va SEMPRE rivisto sul foglio
// ancore-<id>.png: è una proposta, non una misura.
const key = (x, y) => y * W + x;
function components(fr, pred, diag) {
  const seen = new Set(), out = [];
  const nb = diag ? [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]] : [[1, 0], [-1, 0], [0, 1], [0, -1]];
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    if (seen.has(key(x, y)) || !pred(fr[y][x], x, y)) continue;
    const comp = [], st = [[x, y]];
    seen.add(key(x, y));
    while (st.length) {
      const [cx, cy] = st.pop(); comp.push([cx, cy]);
      for (const [dx, dy] of nb) {
        const nx = cx + dx, ny = cy + dy;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H || seen.has(key(nx, ny)) || !pred(fr[ny][nx], nx, ny)) continue;
        seen.add(key(nx, ny)); st.push([nx, ny]);
      }
    }
    out.push(comp);
  }
  return out;
}
// Corpo = la componente 4-connessa più grande di pixel disegnati (la nuvoletta e i simboli restano fuori).
function body(fr) {
  const comps = components(fr, (c) => c !== '.', false).sort((a, b) => b.length - a.length);
  return new Set((comps[0] || []).map(([x, y]) => key(x, y)));
}
const bbox = (pts) => pts.reduce((b, [x, y]) => ({ x0: Math.min(b.x0, x), x1: Math.max(b.x1, x), y0: Math.min(b.y0, y), y1: Math.max(b.y1, y) }), { x0: W, x1: -1, y0: H, y1: -1 });
function topAt(bd, x, from = 0) {
  for (let y = from; y < H; y++) if (bd.has(key(x, y))) return y;
  return -1;
}

// Misura sul fotogramma di riferimento: occhi = componente di carta più alta (e quelle alla stessa altezza),
// bocca = carta sotto gli occhi, testa = cima del corpo sulla colonna centrale degli occhi, collo = sotto la bocca.
export function measure(fr) {
  const bd = body(fr);
  const paper = components(fr, (c, x, y) => c === '+' && bd.has(key(x, y)), true).filter((c) => c.length >= 3);
  if (!paper.length) return null;
  paper.sort((a, b) => bbox(a).y0 - bbox(b).y0);
  const first = bbox(paper[0]);
  const eyes = paper.filter((c) => { const b = bbox(c); return b.y0 <= first.y1 && b.y1 >= first.y0; });
  const eb = bbox(eyes.flat());
  const ex = Math.floor((eb.x0 + eb.x1) / 2);
  // bocca: la carta del corpo più vicina sotto la cima degli occhi (anche di profilo, fuori dalla larghezza degli occhi)
  const rest = components(fr, (c, x, y) => c === '+' && bd.has(key(x, y)), true).filter((c) => !eyes.includes(c) && bbox(c).y0 > eb.y0);
  let mouth = null;
  if (rest.length) {
    const top = Math.min(...rest.map((c) => bbox(c).y0));
    const m = rest.filter((c) => bbox(c).y0 <= top + 1).flat(), mb = bbox(m);
    mouth = [Math.floor((mb.x0 + mb.x1) / 2), Math.round(m.reduce((s, p) => s + p[1], 0) / m.length)];
  }
  const ht = topAt(bd, ex);
  return { bd, eyesBox: eb, eyes: [ex, eb.y0], mouth, neck: mouth ? [mouth[0], mouth[1] + 1] : null, head: ht >= 0 ? [ex, ht] : null };
}

// Spostamento (dx, dy) del corpo di fr rispetto al riferimento, sulla finestra della faccia.
function track(ref, fr, win) {
  const bd = body(fr);
  let best = null;
  for (let dy = -4; dy <= 4; dy++) for (let dx = -3; dx <= 3; dx++) {
    let score = 0;
    for (let y = win.y0; y <= win.y1; y++) for (let x = win.x0; x <= win.x1; x++) {
      const a = ref.bd.has(key(x, y)), nx = x + dx, ny = y + dy;
      const b = nx >= 0 && ny >= 0 && nx < W && ny < H && bd.has(key(nx, ny));
      if (a === b) score++;
    }
    const cost = Math.abs(dx) + Math.abs(dy);
    if (!best || score > best.score || (score === best.score && cost < best.cost)) best = { dx, dy, score, cost };
  }
  return { dx: best.dx, dy: best.dy, bd };
}

// Mano: estremità del corpo che sporge di almeno 2 punti a destra della faccia (bordo destro del corpo sulle
// righe degli occhi), dagli occhi in giù; null se non sporge niente (zampe corte, tentacoli staccati).
function handOf(bd, eb) {
  let face = -1, best = null;
  bd.forEach((k) => { const x = k % W, y = (k - x) / W; if (y >= eb.y0 && y <= eb.y0 + 3 && x > face) face = x; });
  bd.forEach((k) => {
    const x = k % W, y = (k - x) / W;
    if (y < eb.y0 || x < face + 2) return;
    if (!best || x > best[0] || (x === best[0] && y < best[1])) best = [x, y];
  });
  return best;
}

const clampPt = ([x, y]) => (x >= 0 && x < W && y >= 0 && y < H ? [x, y] : null);

export function autoAnchors(av) {
  const out = {};
  const groups = [
    { ref: av.poses.permission[0], poses: POSES.filter((p) => p !== 'run' && av.poses[p]) },
    av.poses.run ? { ref: av.poses.run[0], poses: ['run'] } : null
  ].filter(Boolean);
  for (const g of groups) {
    const m = measure(g.ref);
    if (!m) { g.poses.forEach((p) => { out[p] = av.poses[p].map(() => Object.fromEntries(ANCHOR_NAMES.map((n) => [n, null]))); }); continue; }
    const win = { x0: Math.max(0, m.eyesBox.x0 - 2), x1: Math.min(W - 1, m.eyesBox.x1 + 2), y0: m.head ? m.head[1] : m.eyesBox.y0, y1: m.neck ? m.neck[1] : m.eyesBox.y1 };
    for (const p of g.poses) {
      out[p] = av.poses[p].map((fr) => {
        const { dx, dy, bd } = track(m, fr, win);
        const mv = (pt) => (pt ? clampPt([pt[0] + dx, pt[1] + dy]) : null);
        let head = mv(m.head);
        if (head) { const t = topAt(bd, head[0]); head = t >= 0 ? [head[0], t] : head; }   // cima reale della testa
        const eb = { ...m.eyesBox, x0: m.eyesBox.x0 + dx, x1: m.eyesBox.x1 + dx, y0: m.eyesBox.y0 + dy };
        return { 'head-top': head, eyes: mv(m.eyes), mouth: mv(m.mouth), neck: mv(m.neck), hand: p === 'run' ? null : handOf(bd, eb) };
      });
    }
  }
  return Object.fromEntries(POSES.filter((p) => out[p]).map((p) => [p, out[p]]));
}
