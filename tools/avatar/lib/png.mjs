// PNG RGB minimo (zlib di Node, nessuna dipendenza) e una tela con un font 3×5 per le etichette.
import zlib from 'node:zlib';
import { crc32 } from './pack.mjs';

export const PAPER_RGB = [214, 215, 207];  // come PAPER_RGB del simulatore
export const INK_RGB = [30, 32, 34];       // come INK_RGB del simulatore

export class Canvas {
  constructor(w, h, bg = PAPER_RGB) {
    this.w = w; this.h = h; this.px = Buffer.alloc(w * h * 3);
    this.rect(0, 0, w, h, bg);
  }
  rect(x, y, w, h, rgb) {
    for (let yy = Math.max(0, y); yy < Math.min(this.h, y + h); yy++) {
      for (let xx = Math.max(0, x); xx < Math.min(this.w, x + w); xx++) {
        const i = (yy * this.w + xx) * 3; this.px[i] = rgb[0]; this.px[i + 1] = rgb[1]; this.px[i + 2] = rgb[2];
      }
    }
  }
  box(x, y, w, h, rgb) { this.rect(x, y, w, 1, rgb); this.rect(x, y + h - 1, w, 1, rgb); this.rect(x, y, 1, h, rgb); this.rect(x + w - 1, y, 1, h, rgb); }
  // Disegna un fotogramma: '#' inchiostro, '+' carta, '.' lascia lo sfondo.
  frame(fr, x, y, s, ink = INK_RGB, paper = PAPER_RGB) {
    fr.forEach((row, yy) => { for (let xx = 0; xx < row.length; xx++) {
      const ch = row[xx];
      if (ch === '#') this.rect(x + xx * s, y + yy * s, s, s, ink);
      else if (ch === '+') this.rect(x + xx * s, y + yy * s, s, s, paper);
    } });
  }
  text(str, x, y, s = 2, rgb = INK_RGB) {
    let cx = x;
    for (const ch of String(str).toUpperCase()) {
      const g = FONT[ch] || FONT['?'];
      g.forEach((bits, r) => { for (let c = 0; c < 3; c++) if (bits & (4 >> c)) this.rect(cx + c * s, y + r * s, s, s, rgb); });
      cx += 4 * s;
    }
    return cx;
  }
  png() {
    const raw = Buffer.alloc((this.w * 3 + 1) * this.h);
    for (let y = 0; y < this.h; y++) { raw[y * (this.w * 3 + 1)] = 0; this.px.copy(raw, y * (this.w * 3 + 1) + 1, y * this.w * 3, (y + 1) * this.w * 3); }
    const chunk = (type, data) => {
      const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
      const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
      const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
      return Buffer.concat([len, td, crc]);
    };
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(this.w, 0); ihdr.writeUInt32BE(this.h, 4); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
  }
}

// Font 3×5: 5 righe da 3 bit (bit 2 = colonna sinistra).
const FONT = {
  A: [2, 5, 7, 5, 5], B: [6, 5, 6, 5, 6], C: [3, 4, 4, 4, 3], D: [6, 5, 5, 5, 6], E: [7, 4, 6, 4, 7], F: [7, 4, 6, 4, 4],
  G: [3, 4, 5, 5, 3], H: [5, 5, 7, 5, 5], I: [7, 2, 2, 2, 7], J: [1, 1, 1, 5, 2], K: [5, 5, 6, 5, 5], L: [4, 4, 4, 4, 7],
  M: [5, 7, 7, 5, 5], N: [6, 5, 5, 5, 5], O: [2, 5, 5, 5, 2], P: [6, 5, 6, 4, 4], Q: [2, 5, 5, 6, 3], R: [6, 5, 6, 5, 5],
  S: [3, 4, 2, 1, 6], T: [7, 2, 2, 2, 2], U: [5, 5, 5, 5, 7], V: [5, 5, 5, 5, 2], W: [5, 5, 7, 7, 5], X: [5, 5, 2, 5, 5],
  Y: [5, 5, 2, 2, 2], Z: [7, 1, 2, 4, 7], 0: [7, 5, 5, 5, 7], 1: [2, 6, 2, 2, 7], 2: [6, 1, 2, 4, 7], 3: [6, 1, 2, 1, 6],
  4: [5, 5, 7, 1, 1], 5: [7, 4, 6, 1, 6], 6: [3, 4, 7, 5, 7], 7: [7, 1, 1, 2, 2], 8: [7, 5, 7, 5, 7], 9: [7, 5, 7, 1, 6],
  ' ': [0, 0, 0, 0, 0], '-': [0, 0, 7, 0, 0], '.': [0, 0, 0, 0, 2], ':': [0, 2, 0, 2, 0], '/': [1, 1, 2, 4, 4],
  '(': [1, 2, 2, 2, 1], ')': [4, 2, 2, 2, 4], '×': [0, 5, 2, 5, 0], '?': [6, 1, 2, 0, 2], '!': [2, 2, 2, 0, 2],
  ',': [0, 0, 0, 2, 4], '<': [1, 2, 4, 2, 1], '>': [4, 2, 1, 2, 4], '=': [0, 7, 0, 7, 0], '+': [0, 2, 7, 2, 0], '%': [5, 1, 2, 4, 5]
};
