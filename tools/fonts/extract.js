// Gira dentro la pagina di Chrome (Playwright). Rasterizza i glifi con lo stesso font string del
// simulatore, textBaseline 'top', su un canvas CPU (willReadFrequently, come Screen.src) e applica
// la soglia di luminanza 0.5 di Screen.compose. Restituisce bitmap ritagliate, advance e kerning.
// Le funzioni T/fit/wrapLines/wrapT arrivano dal sorgente del simulatore (iniettate da gen.py).
async (cfg) => {
  const F_UI = cfg.F_UI, F_PIX = cfg.F_PIX, INK = '#000', PAPER = '#fff';
  const SIM = new Function('F_UI', 'F_PIX', 'INK', 'PAPER', cfg.simSrc + '\nreturn { fit, T, wrapLines, wrapT };')(F_UI, F_PIX, INK, PAPER);

  const fam = (pix) => (pix ? F_PIX : F_UI);
  const fontStr = (size, w, pix) => w + ' ' + size + 'px ' + fam(pix);
  // Carica tutte le facce prima di misurare (altrimenti il primo glifo esce col fallback).
  for (const cb of cfg.combos) await document.fonts.load(fontStr(cb.size, cb.weight, cb.pix), cfg.allText);
  for (const w of [500, 600, 700]) await document.fonts.load(w + ' 100px ' + F_UI, cfg.allText);
  await document.fonts.ready;

  const lum = (d, j) => (d[j] * 0.299 + d[j + 1] * 0.587 + d[j + 2] * 0.114) / 255;
  const hex = (bits) => {
    let s = '';
    for (let i = 0; i < bits.length; i += 4) {
      let v = 0;
      for (let k = 0; k < 4; k++) v = (v << 1) | (bits[i + k] || 0);
      s += v.toString(16);
    }
    return s;
  };

  // Rasterizza un "glifo" (un carattere o una legatura) alla fase orizzontale ph.
  function raster(c, cw, ch, txt, P, ph, invert) {
    c.setTransform(1, 0, 0, 1, 0, 0);
    c.fillStyle = invert ? INK : PAPER; c.fillRect(0, 0, cw, ch);
    c.fillStyle = invert ? PAPER : INK;
    c.textBaseline = 'top'; c.textAlign = 'left';
    c.fillText(txt, P + ph, P);
    const d = c.getImageData(0, 0, cw, ch).data;
    let x0 = cw, y0 = ch, x1 = -1, y1 = -1;
    const on = new Uint8Array(cw * ch);
    for (let i = 0; i < cw * ch; i++) {
      const l = lum(d, i * 4);
      const ink = invert ? !(l < 0.5) : l < 0.5; // testo chiaro: inchiostro dove resta carta
      if (ink) { on[i] = 1; const x = i % cw, y = (i / cw) | 0; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    if (x1 < 0) return { w: 0, h: 0, x: 0, y: 0, bits: '' };
    const bits = [];
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) bits.push(on[y * cw + x]);
    return { w: x1 - x0 + 1, h: y1 - y0 + 1, x: x0 - P, y: y0 - P, bits: hex(bits) };
  }

  const cv = document.createElement('canvas');
  const c = cv.getContext('2d', { willReadFrequently: true });
  const fonts = [];
  for (const cb of cfg.combos) {
    const P = cb.size * 2, cw = cb.size * 6 + 8, ch = cb.size * 4 + 8;
    cv.width = cw; cv.height = ch;
    c.font = fontStr(cb.size, cb.weight, cb.pix);
    const glyphs = [];
    for (const g of cfg.glyphs) {
      if (g.lig && cb.pix) continue;
      c.font = fontStr(cb.size, cb.weight, cb.pix);
      const adv = c.measureText(g.txt).width;
      const phases = cfg.phases.map((ph) => raster(c, cw, ch, g.txt, P, ph, false));
      const inv = cfg.invert ? cfg.phases.map((ph) => raster(c, cw, ch, g.txt, P, ph, true)) : null;
      glyphs.push({ cp: g.cp, adv, phases, inv });
    }
    fonts.push({ size: cb.size, weight: cb.weight, pix: cb.pix, glyphs });
  }

  // Kerning (GPOS kern di Chakra Petch) misurato a 1000 px: con UPM 1000 sono unità del font.
  const kern = {};
  for (const w of cfg.kernWeights) {
    c.font = w + ' 1000px ' + F_UI;
    const items = cfg.kernGlyphs;
    const wd = items.map((g) => c.measureText(g.txt).width);
    const pairs = [];
    for (let a = 0; a < items.length; a++)
      for (let b = 0; b < items.length; b++) {
        const t = items[a].txt + items[b].txt;
        if (cfg.ligPairs.includes(t)) continue;
        const k = c.measureText(t).width - wd[a] - wd[b];
        if (Math.abs(k) >= 0.25) pairs.push([items[a].cp, items[b].cp, k]);
      }
    kern[w] = pairs;
  }

  // Riferimenti di fedeltà: le stringhe disegnate con T() del simulatore su un canvas 250×122.
  const W = cfg.W, H = cfg.H;
  cv.width = W; cv.height = H;
  const refs = [];
  for (const r of cfg.refs) {
    c.setTransform(1, 0, 0, 1, 0, 0);
    c.fillStyle = PAPER; c.fillRect(0, 0, W, H);
    if (r.bg) { c.fillStyle = INK; c.fillRect(r.bg[0], r.bg[1], r.bg[2], r.bg[3]); }
    const o = { w: r.weight, a: r.align };
    if (r.pix) o.f = F_PIX;
    if (r.max) o.max = r.max;
    if (r.paper) o.c = PAPER;
    let lines = null;
    if (r.wrap) {
      SIM.wrapT(c, r.s, r.x, r.y, r.size, r.wrap.maxW, r.wrap.maxLines, r.wrap.lh, o);
      lines = SIM.wrapLines(c, r.s, r.size, r.wrap.maxW, r.weight, r.pix ? F_PIX : undefined);
    } else SIM.T(c, r.s, r.x, r.y, r.size, o);
    c.font = fontStr(r.size, r.weight, r.pix);
    const fitted = r.max ? SIM.fit(c, r.s, r.max, r.pix ? '..' : '…') : r.s;
    const width = c.measureText(r.s).width, fittedWidth = c.measureText(fitted).width;
    const d = c.getImageData(0, 0, W, H).data, bits = [];
    for (let i = 0; i < W * H; i++) bits.push(lum(d, i * 4) < 0.5 ? 1 : 0);
    refs.push(Object.assign({}, r, { width, fitted, fittedWidth, lines, bits: hex(bits) }));
  }

  // Controllo largo (solo Python): molte stringhe del simulatore, canvas largo quanto serve.
  const wide = [];
  for (const r of cfg.wide || []) {
    c.font = fontStr(r.size, r.weight, r.pix);
    const width = c.measureText(r.s).width;
    const ww = Math.ceil(width) + 16, hh = r.size * 2 + 8;
    cv.width = ww; cv.height = hh;
    c.setTransform(1, 0, 0, 1, 0, 0);
    c.fillStyle = PAPER; c.fillRect(0, 0, ww, hh);
    const o = { w: r.weight };
    if (r.pix) o.f = F_PIX;
    SIM.T(c, r.s, 4, r.size / 2 | 0, r.size, o);
    const d = c.getImageData(0, 0, ww, hh).data, bits = [];
    for (let i = 0; i < ww * hh; i++) bits.push(lum(d, i * 4) < 0.5 ? 1 : 0);
    wide.push(Object.assign({}, r, { width, ww, hh, bits: hex(bits) }));
  }
  return { fonts, kern, refs, wide };
}
