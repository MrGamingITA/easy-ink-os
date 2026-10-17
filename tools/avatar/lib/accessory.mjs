// Accessori della mascotte: formato JSON calamaio-accessory v1, validazione, binario CACC v1.
// La specifica è docs/AVATAR.md §14: se questo file e la specifica divergono, si corregge il codice.
import { POSES, ANCHOR_NAMES } from './format.mjs';
import { crc32 } from './pack.mjs';

export const ACC_FORMAT = 'calamaio-accessory';
export const ACC_MAX_W = 14;
export const ACC_MAX_H = 10;
export const ACC_MAX_BYTES = 1024;
export const ACC_SYMBOLS = { '.': 0, '#': 1, '+': 2, x: 3 };   // trasparente, inchiostro, carta, inverso
const CHARS = ['.', '#', '+', 'x'];
export const ACC_MAGIC = 'CACC';
export const ACC_HEADER = 72;
export const MAX_SHIFT = 27;    // dx, dy dei ritocchi: |valore| ≤ 27 (oltre si esce comunque dal riquadro 28×25)
export const accFrameBytes = (w, h) => Math.ceil((w * h) / 4);

const ID_RE = /^[a-z0-9]+(-[a-z0-9]+)*$/;
const FIELDS = ['format', 'version', 'id', 'name', 'author', 'description', 'special', 'w', 'h', 'frames', 'anchor', 'origin', 'outline', 'hidePoses', 'perAvatar', 'pixels'];
const utf8Len = (s) => Buffer.byteLength(String(s), 'utf8');
const isInt = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;

function framesErrors(frs, a, at, E) {
  if (!Array.isArray(frs) || frs.length !== a.frames) { E(`${at}: servono ${a.frames} fotogrammi, trovati ${Array.isArray(frs) ? frs.length : 0}`); return false; }
  let ok = true;
  frs.forEach((fr, f) => {
    if (!Array.isArray(fr) || fr.length !== a.h) { E(`${at}[${f}]: servono ${a.h} righe`); ok = false; return; }
    fr.forEach((row, y) => {
      if (typeof row !== 'string' || row.length !== a.w) { E(`${at}[${f}] riga ${y}: servono ${a.w} simboli`); ok = false; }
      else if (!/^[.#+x]*$/.test(row)) { E(`${at}[${f}] riga ${y}: simboli ammessi solo . # + x`); ok = false; }
    });
    if (ok && fr.every((r) => /^\.*$/.test(r))) { E(`${at}[${f}]: accessorio vuoto`); ok = false; }
  });
  return ok;
}

// Restituisce { errors, warnings }. errors vuoto = accessorio valido.
export function validateAccessory(a) {
  const errors = [], warnings = [];
  const E = (m) => errors.push(m), Wn = (m) => warnings.push(m);
  if (!a || typeof a !== 'object' || Array.isArray(a)) return { errors: ['il file non contiene un oggetto JSON'], warnings };
  if (a.format !== ACC_FORMAT) E(`format deve essere "${ACC_FORMAT}"`);
  if (a.version !== 1) E('version deve essere 1');
  if (typeof a.id !== 'string' || !ID_RE.test(a.id) || a.id.length > 24) E('id: kebab-case [a-z0-9-], al massimo 24 caratteri');
  if (typeof a.name !== 'string' || !a.name.trim() || utf8Len(a.name) > 24) E('name: non vuoto, al massimo 24 byte UTF-8');
  if (typeof a.author !== 'string' || !a.author.trim()) E('author: obbligatorio');
  if (typeof a.description !== 'string' || utf8Len(a.description) > 120) E('description: stringa di al massimo 120 byte UTF-8');
  if (typeof a.special !== 'boolean') E('special: true o false');
  if (!isInt(a.w, 1, ACC_MAX_W)) E(`w: da 1 a ${ACC_MAX_W}`);
  if (!isInt(a.h, 1, ACC_MAX_H)) E(`h: da 1 a ${ACC_MAX_H}`);
  if (a.frames !== 1 && a.frames !== 2) E('frames deve essere 1 o 2');
  if (!ANCHOR_NAMES.includes(a.anchor)) E(`anchor: uno tra ${ANCHOR_NAMES.join(', ')}`);
  if (!Array.isArray(a.origin) || a.origin.length !== 2 || !a.origin.every(Number.isInteger)) E('origin: serve [x, y] intero');
  else if (isInt(a.w, 1, ACC_MAX_W) && isInt(a.h, 1, ACC_MAX_H) && (a.origin[0] < 0 || a.origin[0] >= a.w || a.origin[1] < 0 || a.origin[1] >= a.h)) {
    E(`origin: (${a.origin[0]},${a.origin[1]}) fuori dall'accessorio ${a.w}×${a.h}`);
  }
  if (typeof a.outline !== 'boolean') E('outline: true o false');
  if (!Array.isArray(a.hidePoses)) E('hidePoses: elenco di pose (anche vuoto)');
  else {
    const seen = new Set();
    for (const p of a.hidePoses) {
      if (!POSES.includes(p)) E(`hidePoses: posa sconosciuta ${p}`);
      else if (seen.has(p)) E(`hidePoses: ${p} ripetuta`);
      seen.add(p);
    }
    if (POSES.every((p) => seen.has(p))) Wn('nascosto in tutte le pose: non si vede mai');
  }
  for (const k of Object.keys(a)) if (!FIELDS.includes(k)) E(`campo sconosciuto: ${k}`);
  if (errors.length) return { errors, warnings };

  if (framesErrors(a.pixels, a, 'pixels', E) && a.frames === 2 && a.pixels[0].join('\n') === a.pixels[1].join('\n')) {
    Wn('i 2 fotogrammi sono uguali: basta frames 1');
  }
  if (a.perAvatar !== undefined) {
    if (!a.perAvatar || typeof a.perAvatar !== 'object' || Array.isArray(a.perAvatar)) E('perAvatar: oggetto id avatar → ritocco');
    else for (const [id, r] of Object.entries(a.perAvatar)) {
      const at = `perAvatar.${id}`;
      if (!ID_RE.test(id) || id.length > 24) { E(`perAvatar: id avatar "${id}" non valido`); continue; }
      if (!r || typeof r !== 'object' || Array.isArray(r)) { E(`${at}: serve un oggetto {dx, dy, pixels}`); continue; }
      for (const k of Object.keys(r)) if (!['dx', 'dy', 'pixels'].includes(k)) E(`${at}: campo sconosciuto ${k}`);
      if (r.dx === undefined && r.dy === undefined && r.pixels === undefined) E(`${at}: serve dx, dy o pixels`);
      for (const k of ['dx', 'dy']) if (r[k] !== undefined && !isInt(r[k], -MAX_SHIFT, MAX_SHIFT)) E(`${at}.${k}: intero da -${MAX_SHIFT} a ${MAX_SHIFT}`);
      if (r.pixels !== undefined) framesErrors(r.pixels, a, `${at}.pixels`, E);
    }
  }
  if (!errors.length) {
    const n = packAccessory(a, { unchecked: true }).length;
    if (n > ACC_MAX_BYTES) E(`binario CACC di ${n} byte: il massimo è ${ACC_MAX_BYTES} (troppi ritocchi con pixels?)`);
  }
  return { errors, warnings };
}

// ---------------------------------------------------------------- binario CACC v1
// Little-endian, intestazione di 72 byte (docs/AVATAR.md §14.3), poi i ritocchi, poi i fotogrammi a 2 bit per simbolo.
function packFrames(buf, off, frs, w, h) {
  const fb = accFrameBytes(w, h);
  for (const fr of frs) {
    let i = 0;
    for (const row of fr) for (const ch of row) { buf[off + (i >> 2)] |= ACC_SYMBOLS[ch] << (6 - 2 * (i & 3)); i++; }
    off += fb;
  }
  return off;
}
function readFrames(buf, off, frames, w, h) {
  const fb = accFrameBytes(w, h), out = [];
  for (let f = 0; f < frames; f++) {
    const rows = [];
    for (let y = 0; y < h; y++) {
      let r = '';
      for (let x = 0; x < w; x++) { const i = y * w + x; r += CHARS[(buf[off + (i >> 2)] >> (6 - 2 * (i & 3))) & 3]; }
      rows.push(r);
    }
    out.push(rows);
    off += fb;
  }
  return out;
}
const retouches = (a) => Object.keys(a.perAvatar || {}).sort().map((id) => [id, a.perAvatar[id]]);

export function packAccessory(a, opts = {}) {
  if (!opts.unchecked) {
    const { errors } = validateAccessory(a);
    if (errors.length) throw new Error('accessorio non valido: ' + errors[0]);
  }
  const fb = accFrameBytes(a.w, a.h), rs = retouches(a);
  let total = ACC_HEADER + a.frames * fb;
  for (const [id, r] of rs) total += 1 + id.length + 3 + (r.pixels ? a.frames * fb : 0);
  const buf = Buffer.alloc(total);
  buf.write(ACC_MAGIC, 0, 'ascii');
  buf[4] = 1; buf[5] = ACC_HEADER; buf.writeUInt16LE(Math.min(total, 0xFFFF), 6);
  buf[12] = a.w; buf[13] = a.h; buf[14] = a.frames; buf[15] = ANCHOR_NAMES.indexOf(a.anchor);
  buf[16] = a.origin[0]; buf[17] = a.origin[1];
  buf[18] = (a.outline ? 1 : 0) | (a.special ? 2 : 0);
  buf[19] = rs.length;
  let mask = 0;
  for (const p of a.hidePoses) mask |= 1 << POSES.indexOf(p);
  buf.writeUInt16LE(mask, 20); buf.writeUInt16LE(fb, 22);
  buf.write(a.id, 24, 24, 'ascii');
  Buffer.from(a.name, 'utf8').copy(buf, 48, 0, 24);
  let off = ACC_HEADER;
  for (const [id, r] of rs) {
    buf[off++] = id.length; buf.write(id, off, 'ascii'); off += id.length;
    buf.writeInt8(r.dx || 0, off++); buf.writeInt8(r.dy || 0, off++);
    buf[off++] = r.pixels ? 1 : 0;
    if (r.pixels) off = packFrames(buf, off, r.pixels, a.w, a.h);
  }
  packFrames(buf, off, a.pixels, a.w, a.h);
  buf.writeUInt32LE(crc32(buf, 12, total), 8);
  return buf;
}

function readStr(buf, off, len) {
  let e = off; while (e < off + len && buf[e] !== 0) e++;
  return buf.toString('utf8', off, e);
}

export function unpackAccessory(buf) {
  buf = Buffer.from(buf);
  if (buf.length < ACC_HEADER || buf.toString('ascii', 0, 4) !== ACC_MAGIC) throw new Error('magia CACC assente');
  if (buf[4] !== 1) throw new Error(`versione ${buf[4]} non supportata`);
  if (buf[5] !== ACC_HEADER) throw new Error(`intestazione di ${buf[5]} byte, attesa ${ACC_HEADER}`);
  const total = buf.readUInt16LE(6);
  if (total !== buf.length) throw new Error(`lunghezza dichiarata ${total}, reale ${buf.length}`);
  if (total > ACC_MAX_BYTES) throw new Error(`binario di ${total} byte: il massimo è ${ACC_MAX_BYTES}`);
  if (buf.readUInt32LE(8) !== crc32(buf, 12, total)) throw new Error('checksum errato');
  const w = buf[12], h = buf[13], frames = buf[14], anchor = buf[15];
  if (w < 1 || w > ACC_MAX_W || h < 1 || h > ACC_MAX_H) throw new Error(`dimensioni ${w}×${h} fuori dai limiti ${ACC_MAX_W}×${ACC_MAX_H}`);
  if (frames !== 1 && frames !== 2) throw new Error('fotogrammi devono essere 1 o 2');
  if (anchor >= ANCHOR_NAMES.length) throw new Error(`ancora ${anchor} sconosciuta`);
  if (buf[16] >= w || buf[17] >= h) throw new Error(`origin (${buf[16]},${buf[17]}) fuori dall'accessorio`);
  if (buf[18] >> 2) throw new Error('bit di opzione sconosciuti');
  const mask = buf.readUInt16LE(20);
  if (mask >> POSES.length) throw new Error('bit di posa sconosciuti in hidePoses');
  const fb = buf.readUInt16LE(22);
  if (fb !== accFrameBytes(w, h)) throw new Error('frameBytes incoerente');
  const a = {
    format: ACC_FORMAT, version: 1, id: readStr(buf, 24, 24), name: readStr(buf, 48, 24), author: '(dal binario)', description: '',
    special: !!(buf[18] & 2), w, h, frames, anchor: ANCHOR_NAMES[anchor], origin: [buf[16], buf[17]], outline: !!(buf[18] & 1),
    hidePoses: POSES.filter((p, i) => mask & (1 << i)), perAvatar: {}, pixels: null
  };
  let off = ACC_HEADER;
  const need = (n) => { if (off + n > total) throw new Error('ritocchi oltre la fine del file: dimensione incoerente'); };
  for (let k = 0; k < buf[19]; k++) {
    need(1); const len = buf[off++];
    if (len < 1 || len > 24) throw new Error('id del ritocco di lunghezza non valida');
    need(len + 3);
    const id = buf.toString('ascii', off, off + len); off += len;
    const r = {}, dx = buf.readInt8(off++), dy = buf.readInt8(off++), hasPx = buf[off++];
    if (hasPx > 1) throw new Error('ritocco con opzioni sconosciute');
    if (dx) r.dx = dx;
    if (dy) r.dy = dy;
    if (hasPx) { need(frames * fb); r.pixels = readFrames(buf, off, frames, w, h); off += frames * fb; }
    a.perAvatar[id] = r;
  }
  if (off + frames * fb !== total) throw new Error('dimensione incoerente con ritocchi e fotogrammi');
  a.pixels = readFrames(buf, off, frames, w, h);
  return a;
}

// Stesso disegno e stesse regole (autore e descrizione non viaggiano nel binario; dx/dy 0 = assenti).
export function canonicalAccessory(a) {
  const pa = {};
  for (const id of Object.keys(a.perAvatar || {}).sort()) {
    const r = a.perAvatar[id], c = {};
    if (r.dx) c.dx = r.dx;
    if (r.dy) c.dy = r.dy;
    if (r.pixels) c.pixels = r.pixels;
    pa[id] = c;
  }
  const out = {};
  for (const k of FIELDS) if (!['author', 'description', 'perAvatar'].includes(k)) out[k] = a[k];
  out.hidePoses = POSES.filter((p) => a.hidePoses.includes(p));
  out.perAvatar = pa;
  return JSON.stringify(out);
}

// Serializza con una riga del disegno per riga di testo, come gli avatar.
export function serializeAccessory(a) {
  const head = {};
  for (const k of FIELDS) if (k !== 'pixels' && k !== 'perAvatar' && a[k] !== undefined) head[k] = a[k];
  const frames = (frs, ind) => '[\n' + frs.map((fr) => `${ind}  [\n` + fr.map((r) => `${ind}    "${r}"`).join(',\n') + `\n${ind}  ]`).join(',\n') + `\n${ind}]`;
  let out = JSON.stringify(head, null, 2)
    .replace(/"origin": \[\s*(\d+),\s*(\d+)\s*\]/, '"origin": [$1, $2]')
    .replace(/"hidePoses": \[([^\]]*)\]/, (m, inner) => `"hidePoses": [${inner.split(',').map((s) => s.trim()).filter(Boolean).join(', ')}]`)
    .replace(/\n}$/, '');
  if (a.perAvatar !== undefined) {
    const ids = Object.keys(a.perAvatar);
    out += ',\n  "perAvatar": {' + (ids.length ? '\n' + ids.map((id, i) => {
      const r = a.perAvatar[id], parts = [];
      if (r.dx !== undefined) parts.push(`"dx": ${r.dx}`);
      if (r.dy !== undefined) parts.push(`"dy": ${r.dy}`);
      if (r.pixels !== undefined) parts.push(`"pixels": ${frames(r.pixels, '    ')}`);
      return `    "${id}": {${parts.join(', ')}}${i < ids.length - 1 ? ',' : ''}`;
    }).join('\n') + '\n  ' : '') + '}';
  }
  return out + ',\n  "pixels": ' + frames(a.pixels, '  ') + '\n}\n';
}
