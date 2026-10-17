// Formato binario CAVT v1 (docs/AVATAR.md, «Formato binario per il display»).
// Intestazione di 72 byte, poi i fotogrammi a 2 bit per pixel, pose nell'ordine fisso di POSES.
import { POSES, REQUIRED, SYMBOLS, W, H, VERSION } from './format.mjs';

export const MAGIC = 'CAVT';
export const HEADER = 72;
const CHARS = ['.', '#', '+'];

let CRC_TABLE = null;
export function crc32(buf, start = 0, end = buf.length) {
  if (!CRC_TABLE) {
    CRC_TABLE = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; CRC_TABLE[n] = c >>> 0; }
  }
  let c = 0xFFFFFFFF;
  for (let i = start; i < end; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

export const frameBytes = (w, h) => Math.ceil((w * h) / 4);
export function packedSize(nPoses, frames, w = W, h = H) { return HEADER + nPoses * frames * frameBytes(w, h); }

function fixedStr(buf, off, len, s) {
  const b = Buffer.from(s, 'utf8');
  if (b.length > len) throw new Error(`stringa troppo lunga per il campo di ${len} byte: ${s}`);
  b.copy(buf, off);
}
function readStr(buf, off, len) {
  let e = off; while (e < off + len && buf[e] !== 0) e++;
  return buf.toString('utf8', off, e);
}

export function pack(av) {
  const present = POSES.filter((p) => av.poses[p]);
  let mask = 0;
  present.forEach((p) => { mask |= 1 << POSES.indexOf(p); });
  const fb = frameBytes(av.w, av.h), total = packedSize(present.length, av.frames, av.w, av.h);
  const buf = Buffer.alloc(total);
  buf.write(MAGIC, 0, 'ascii');
  buf[4] = VERSION; buf[5] = HEADER; buf.writeUInt16LE(total, 6);
  buf[12] = av.w; buf[13] = av.h; buf[14] = av.frames; buf[15] = 0;
  buf.writeUInt16LE(mask, 16); buf.writeUInt16LE(fb, 18);
  fixedStr(buf, 20, 24, av.id); fixedStr(buf, 44, 24, av.name);
  let off = HEADER;
  for (const p of present) {
    for (const fr of av.poses[p]) {
      let i = 0;
      for (const row of fr) for (const ch of row) {
        buf[off + (i >> 2)] |= SYMBOLS[ch] << (6 - 2 * (i & 3));  // MSB per primo: pixel 0 nei bit 7-6
        i++;
      }
      off += fb;
    }
  }
  buf.writeUInt32LE(crc32(buf, 12, total), 8);
  return buf;
}

export function unpack(buf) {
  if (buf.length < HEADER || buf.toString('ascii', 0, 4) !== MAGIC) throw new Error('magia CAVT assente');
  if (buf[4] !== VERSION) throw new Error(`versione ${buf[4]} non supportata`);
  const total = buf.readUInt16LE(6);
  if (total !== buf.length) throw new Error(`lunghezza dichiarata ${total}, reale ${buf.length}`);
  if (buf.readUInt32LE(8) !== crc32(buf, 12, total)) throw new Error('checksum errato');
  const w = buf[12], h = buf[13], frames = buf[14], mask = buf.readUInt16LE(16), fb = buf.readUInt16LE(18);
  if (fb !== frameBytes(w, h)) throw new Error('frameBytes incoerente');
  for (const p of REQUIRED) if (!(mask & (1 << POSES.indexOf(p)))) throw new Error(`posa obbligatoria assente: ${p}`);
  if (mask >> POSES.length) throw new Error('bit di posa sconosciuti');
  const present = POSES.filter((p, i) => mask & (1 << i));
  if (total !== packedSize(present.length, frames, w, h)) throw new Error('dimensione incoerente con pose e fotogrammi');
  const av = { format: 'calamaio-avatar', version: VERSION, id: readStr(buf, 20, 24), name: readStr(buf, 44, 24), w, h, frames, poses: {} };
  let off = buf[5];
  for (const p of present) {
    av.poses[p] = [];
    for (let f = 0; f < frames; f++) {
      const rows = [];
      for (let y = 0; y < h; y++) {
        let r = '';
        for (let x = 0; x < w; x++) {
          const i = y * w + x, v = (buf[off + (i >> 2)] >> (6 - 2 * (i & 3))) & 3;
          if (v === 3) throw new Error('codice di pixel 3 riservato');
          r += CHARS[v];
        }
        rows.push(r);
      }
      av.poses[p].push(rows);
      off += fb;
    }
  }
  return av;
}
