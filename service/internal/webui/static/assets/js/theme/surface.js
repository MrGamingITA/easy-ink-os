// Superficie 1 bit del renderer dei temi: stesso formato di Canvas<W, H>::data() del firmware.
// Coordinate troncate a intero (| 0) come gli int di Canvas::fillRect.
export class Bitmap {
  constructor(w = 250, h = 122) { this.w = w; this.h = h; this.px = new Uint8Array(w * h); }
  clear(ink = false) { this.px.fill(ink ? 1 : 0); }
  fillRect(x, y, w, h, ink) {
    x |= 0; y |= 0; w |= 0; h |= 0;
    const x0 = Math.max(0, x), y0 = Math.max(0, y), x1 = Math.min(this.w, x + w), y1 = Math.min(this.h, y + h);
    for (let yy = y0; yy < y1; yy++) this.px.fill(ink ? 1 : 0, yy * this.w + x0, yy * this.w + Math.max(x0, x1));
  }
  get(x, y) { return x >= 0 && y >= 0 && x < this.w && y < this.h && this.px[y * this.w + x] === 1; }
  countInk() { let n = 0; for (const v of this.px) n += v; return n; }
  packed() {
    const stride = (this.w + 7) >> 3, out = new Uint8Array(stride * this.h);
    for (let y = 0; y < this.h; y++) for (let x = 0; x < this.w; x++) if (this.px[y * this.w + x]) out[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
    return out;
  }
}
export class Swap { constructor(s) { this.s = s; } fillRect(x, y, w, h, ink) { this.s.fillRect(x, y, w, h, !ink); } }
export class Null { fillRect() {} }

let table = null;
export function crc32(bytes) {
  if (!table) { table = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; table[n] = c >>> 0; } }
  let c = 0xffffffff;
  for (const b of bytes) c = table[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
