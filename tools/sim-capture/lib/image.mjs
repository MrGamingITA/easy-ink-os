// Codifica e decodifica di immagini 1 bit (PBM P1/P4, PNG) senza dipendenze esterne.
// Convenzione interna: Uint8Array di W*H valori, 1 = inchiostro (nero), 0 = carta.
import { deflateSync } from 'node:zlib';

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}

function png(w, h, bitDepth, colorType, raw) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = bitDepth; ihdr[9] = colorType; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0))
  ]);
}

// PNG in scala di grigi a 1 bit: nero = inchiostro (nel PNG il bit 0 e nero).
export function encodePng1(bits, w, h) {
  const stride = Math.ceil(w / 8);
  const raw = Buffer.alloc((stride + 1) * h);
  for (let y = 0; y < h; y++) {
    const o = y * (stride + 1);
    raw[o] = 0; // filtro None
    for (let x = 0; x < w; x++) {
      if (!bits[y * w + x]) raw[o + 1 + (x >> 3)] |= 0x80 >> (x & 7);
    }
    // i bit di riempimento oltre la larghezza restano a 0: ignorati dai lettori
  }
  return png(w, h, 1, 0, raw);
}

// PNG RGB a 8 bit, per le immagini di differenza.
export function encodePngRgb(rgb, w, h) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    const o = y * (w * 3 + 1);
    raw[o] = 0;
    raw.set(rgb.subarray(y * w * 3, (y + 1) * w * 3), o + 1);
  }
  return png(w, h, 8, 2, raw);
}

// PBM binario (P4): 1 = nero, righe allineate al byte.
export function encodePbm(bits, w, h) {
  const stride = Math.ceil(w / 8);
  const head = Buffer.from(`P4\n${w} ${h}\n`, 'ascii');
  const body = Buffer.alloc(stride * h);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) if (bits[y * w + x]) body[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
  }
  return Buffer.concat([head, body]);
}

// Legge PBM P1 (testo) o P4 (binario). Ritorna { w, h, bits }.
export function decodePbm(buf) {
  let pos = 0;
  const isWs = (c) => c === 0x20 || c === 0x09 || c === 0x0a || c === 0x0d || c === 0x0b || c === 0x0c;
  const token = () => {
    for (;;) {
      while (pos < buf.length && isWs(buf[pos])) pos++;
      if (buf[pos] === 0x23) { while (pos < buf.length && buf[pos] !== 0x0a) pos++; continue; } // commento
      break;
    }
    const s = pos;
    while (pos < buf.length && !isWs(buf[pos]) && buf[pos] !== 0x23) pos++;
    return buf.toString('ascii', s, pos);
  };
  const magic = token();
  if (magic !== 'P1' && magic !== 'P4') throw new Error(`formato non supportato: ${magic} (servono P1 o P4)`);
  const w = parseInt(token(), 10), h = parseInt(token(), 10);
  if (!(w > 0 && h > 0)) throw new Error('dimensioni PBM non valide');
  const bits = new Uint8Array(w * h);
  if (magic === 'P4') {
    pos++; // un solo carattere di spazio dopo l'altezza
    const stride = Math.ceil(w / 8);
    if (buf.length - pos < stride * h) throw new Error('PBM P4 troncato');
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) bits[y * w + x] = (buf[pos + y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
  } else {
    let i = 0;
    while (i < w * h && pos < buf.length) {
      const c = buf[pos++];
      if (c === 0x30 || c === 0x31) bits[i++] = c - 0x30;
      else if (c === 0x23) { while (pos < buf.length && buf[pos] !== 0x0a) pos++; }
    }
    if (i < w * h) throw new Error('PBM P1 troncato');
  }
  return { w, h, bits };
}
