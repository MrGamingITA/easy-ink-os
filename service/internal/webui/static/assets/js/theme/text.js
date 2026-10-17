// Testo come text.cpp del firmware (che a sua volta è T(), fit(), wrapLines() del simulatore): stessi bitmap
// (fonts.js), avanzamenti in 16.16 come HarfBuzz in Chrome, crenatura, legature fi/fl, fase a 1/4 di pixel.
// Aritmetica intera come in C++: tutti i valori restano sotto 2^53 e le divisioni per potenze di 2 sono esatte.
import { FONTS, KERN_KEYS, KERN } from './fonts.js';

const MAX_CPS = 256, LIG_FI = 0xe000, LIG_FL = 0xe001, PHASES = 4;
export const jsRound = (v) => Math.floor(v + 0.5);
const floorDiv = (a, b) => Math.floor(a / b);

const normWeight = (w, pix) => (pix ? (w <= 500 ? 400 : 700) : w <= 500 ? 500 : w <= 600 ? 600 : 700);
function findFont(size, weight, pix) {
  size |= 0; weight |= 0; pix = !!pix;
  const w = normWeight(weight, pix);
  let best = null, bs = 1 << 30;
  for (const f of FONTS) {
    if (f.pix !== pix) continue;
    if (f.size === size && f.weight === w) return f;
    // ripiego: dimensione più vicina, a parità il peso giusto
    const sc = Math.abs(f.size - size) * 4 + (f.weight === w ? 0 : 1);
    if (sc < bs) { bs = sc; best = f; }
  }
  return best;
}
export function hasFont(size, weight, pix) {
  const f = findFont(size, weight, pix);
  return !!f && f.size === (size | 0) && f.weight === normWeight(weight | 0, !!pix);
}
function glyphIndex(f, cp) {
  let lo = 0, hi = f.cps.length - 1;
  while (lo <= hi) { const mid = (lo + hi) >> 1, c = f.cps[mid]; if (c === cp) return mid; if (c < cp) lo = mid + 1; else hi = mid - 1; }
  return -1;
}
function kernOf(f, a, b) {
  if (!f.kern) return 0;
  const key = ((a << 16) | b) >>> 0;
  let lo = 0, hi = KERN_KEYS.length - 1;
  while (lo <= hi) { const mid = (lo + hi) >> 1, k = KERN_KEYS[mid]; if (k === key) return KERN[f.kern][mid]; if (k < key) lo = mid + 1; else hi = mid - 1; }
  return 0;
}
// spazi di \s in JavaScript (split(/\s+/) e trimEnd())
const isJsSpace = (c) => (c >= 0x09 && c <= 0x0d) || c === 0x20 || c === 0xa0 || c === 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
  c === 0x2028 || c === 0x2029 || c === 0x202f || c === 0x205f || c === 0x3000 || c === 0xfeff;

// stringa → codepoint della BMP (fuori BMP: U+FFFD come decode() in C++), al massimo cap; NUL chiude come in C
function decode(s, cap = MAX_CPS) {
  const out = [];
  for (const ch of s == null ? '' : String(s)) {
    if (out.length >= cap) break;
    const c = ch.codePointAt(0);
    if (c === 0) break;
    out.push(c > 0xffff ? 0xfffd : c);
  }
  return out;
}
const utf8Len = (c) => (c < 0x80 ? 1 : c < 0x800 ? 2 : 3);
// codepoint → stringa, senza superare cap − 1 byte UTF-8 (come encode() in C++ con un buffer di cap byte)
function encode(cps, cap) {
  let len = 0, s = '';
  for (const c of cps) { const k = utf8Len(c); if (len + k > cap - 1) break; len += k; s += String.fromCharCode(c); }
  return s;
}
const advFx = (adv, size) => Math.floor((adv * size * 65536) / 1000);
const kernFx = (k, size) => floorDiv(k * Math.floor((size * 65536 * 65536) / 1000) + 32768, 65536);

// percorre i codepoint come li impagina Chrome; fn(indice della voce, posizione in 16.16 px); restituisce la larghezza
function walk(f, cps, fn) {
  const q = glyphIndex(f, 0x3f);
  let u = 0, prev = 0, have = false;
  for (let i = 0; i < cps.length; i++) {
    let c = cps[i];
    if (c === 0x09 || c === 0x0a || c === 0x0c || c === 0x0d) c = 0x20;
    if (!f.pix && c === 0x66 && i + 1 < cps.length) {
      const nx = cps[i + 1];
      if (nx === 0x69 || nx === 0x6c) { c = nx === 0x69 ? LIG_FI : LIG_FL; i++; }
    }
    let g = glyphIndex(f, c);
    if (g < 0) { g = q; c = 0x3f; }  // glifo assente: punto interrogativo
    if (g < 0) continue;
    if (have) u += kernFx(kernOf(f, prev, c), f.size);
    if (fn) fn(g, u);
    u += advFx(f.adv[g], f.size);
    prev = c;
    have = true;
  }
  return u;
}
const fits = (f, cps, max) => walk(f, cps) <= max * 65536;

// fit(): se non sta, toglie caratteri finché s + ell sta, poi trimEnd() + ell
function fitCps(f, pix, cps, max) {
  if (fits(f, cps, max)) return cps;
  const ell = pix ? [0x2e, 0x2e] : [0x2026];
  let n = cps.length;
  while (n > 1 && !fits(f, cps.slice(0, n).concat(ell), max)) n--;
  while (n > 0 && isJsSpace(cps[n - 1])) n--;
  return cps.slice(0, n).concat(ell);
}

export function textWidth(s, size, weight = 600, pix = false) {
  const f = findFont(size, weight, pix);
  return f ? floorDiv(walk(f, decode(s)) + 32768, 65536) : 0;
}
export function textWidthMilli(s, size, weight = 600, pix = false) {
  const f = findFont(size, weight, pix);
  return f ? floorDiv(walk(f, decode(s)) * 1000 + 32768, 65536) : 0;
}
export function fitText(s, size, weight, pix, max) {
  const f = findFont(size, weight, pix);
  let cps = decode(s);
  if (f) cps = fitCps(f, !!pix, cps, max | 0);
  return encode(cps, 256);
}

function blit(surf, f, gi, x0, y0, ink) {
  const off = f.glyphs[5 * gi], w = f.glyphs[5 * gi + 1], h = f.glyphs[5 * gi + 2];
  let bit = 0;
  for (let yy = 0; yy < h; yy++) {
    let run = -1;
    for (let xx = 0; xx < w; xx++, bit++) {
      const on = f.bitmap[off + (bit >> 3)] & (0x80 >> (bit & 7));
      if (on && run < 0) run = xx;
      if (!on && run >= 0) { surf.fillRect(x0 + run, y0 + yy, xx - run, 1, ink); run = -1; }
    }
    if (run >= 0) surf.fillRect(x0 + run, y0 + yy, w - run, 1, ink);
  }
}

// come drawText() di text.cpp: x e y interi, y = bordo alto; o.max tronca come fitCps
export function drawText(surf, s, x, y, size, o = {}) {
  x |= 0; y |= 0;  // int in C++
  const weight = o.weight ?? 600, pix = !!o.pix, ink = o.ink ?? true, a = o.align || 0;
  const f = findFont(size, weight, pix);
  if (!f) return;
  let cps = decode(s);
  const max = o.max | 0;
  if (max) cps = fitCps(f, pix, cps, max);
  const total = walk(f, cps);
  // variante chiaro su scuro se il testo è color carta e il carattere la possiede
  const variant = !ink && f.inv ? PHASES : 0;
  walk(f, cps, (g, u) => {
    // origine del glifo in 1/131072 px, poi quarti di pixel come Skia: floor(4 * gx + 1/2)
    const p2 = x * 131072 - a * total + 2 * u;
    const q = floorDiv(4 * p2 + 65536, 131072);
    const ix = floorDiv(q, 4), ph = q - ix * 4;
    const gi = f.first[g] + variant + ph;
    blit(surf, f, gi, ix + f.glyphs[5 * gi + 3], y + f.glyphs[5 * gi + 4], ink);
  });
}

// come T() di screens.cpp: fitText in un buffer di 256 byte se c'è max, poi Math.round di x e y
export function T(surf, s, x, y, size, o = {}) {
  let str = s || '';
  if (o.max | 0) str = fitText(str, size, o.weight ?? 600, !!o.pix, o.max);
  drawText(surf, str, jsRound(x), jsRound(y), size, { ...o, max: 0 });
}

// come wrapLines() di text.cpp: split(/\s+/) con i token vuoti, righe in char[96]
export function wrapLines(s, maxLines, size, maxW, weight = 500, pix = false) {
  const f = findFont(size, weight, pix);
  const out = [];
  maxLines |= 0; maxW |= 0;  // int in C++
  if (!f || maxLines <= 0) return out;
  const src = decode(s);
  let line = [], i = 0;
  for (;;) {
    let j = i;
    while (j < src.length && !isJsSpace(src[j])) j++;
    const wd = src.slice(i, j);
    let t = line.length ? line.concat([0x20]) : [];
    t = t.concat(wd).slice(0, MAX_CPS);
    if (fits(f, t, maxW)) line = t;
    else {
      if (line.length) { out.push(encode(line, 96)); if (out.length >= maxLines) return out; }
      line = wd;
    }
    if (j >= src.length) break;
    while (j < src.length && isJsSpace(src[j])) j++;
    i = j;
  }
  if (line.length) out.push(encode(line, 96));
  return out;
}
