// Anteprima ingrandita: la bitmap viene SOLO dal renderer condiviso (renderScreen, come la scheda);
// qui si copia sul canvas pixel per pixel e si disegna la sovrapposizione dell'editor (selezione, maniglie, guide).
// Nessun accesso a window/document all'importazione.
import { renderScreen } from '../theme/render.js';
import { Bitmap } from '../theme/surface.js';
import { previewTheme } from './warnings.js';

export const W = 250, H = 122;
const INK = '#1E2022', PAPER = '#D6D7CF';
const SEL = '#2646C4', LOCK = '#5C6670', WARN = '#9A2A12', GRID = 'rgba(30, 32, 34, 0.14)', GUIDE = '#C4267A';

// la variante aperta resa come sulla scheda: { bitmap, info } (info.drawn falso = niente disegnato)
export function renderPreview(theme, loc, model, assets) {
  const bitmap = new Bitmap(W, H);
  let info;
  try { info = renderScreen(previewTheme(theme, loc), loc?.screen, model, bitmap, assets); }
  catch { bitmap.clear(); info = { drawn: false }; }
  return { bitmap, info };
}

// Una sola scala intera per anteprima e sovrapposizione: s pixel del canvas per pixel della scheda,
// e il canvas largo W·s si mostra a W·s/dpr px CSS (niente ricampionamento). css = px CSS per pixel della scheda:
// è lo zoom «vero» con cui convertire il puntatore (vicino a zoom, uguale quando zoom·dpr è intero).
export function scaleFor(zoom, dpr) {
  const s = Math.max(1, Math.round(zoom * dpr));
  return { s, css: s / dpr };
}

export function sizeCanvas(canvas, zoom, dpr) {
  const { s } = scaleFor(zoom, dpr);
  if (canvas.width !== W * s) canvas.width = W * s;
  if (canvas.height !== H * s) canvas.height = H * s;
  canvas.style.width = (W * s) / dpr + 'px';
  canvas.style.height = (H * s) / dpr + 'px';
  return s;
}

export function paintBitmap(canvas, bitmap, zoom, dpr) {
  const s = sizeCanvas(canvas, zoom, dpr);
  const c = canvas.getContext('2d');
  c.imageSmoothingEnabled = false;
  c.setTransform(1, 0, 0, 1, 0, 0);
  c.fillStyle = PAPER;
  c.fillRect(0, 0, canvas.width, canvas.height);
  c.fillStyle = INK;
  // tratti orizzontali d'inchiostro, multipli interi: niente sfumature
  for (let y = 0; y < bitmap.h; y++) {
    const row = y * bitmap.w;
    for (let x = 0; x < bitmap.w;) {
      if (!bitmap.px[row + x]) { x++; continue; }
      let e = x;
      while (e < bitmap.w && bitmap.px[row + e]) e++;
      c.fillRect(x * s, y * s, (e - x) * s, s);
      x = e;
    }
  }
}

// sovrapposizione in px CSS (le maniglie arrivano già in px CSS, F12)
export function paintOverlay(canvas, { boxes = [], sel = [], handles = [], guides = [], marquee = null, warned = [], zoom, dpr, grid = false } = {}) {
  const s = sizeCanvas(canvas, zoom, dpr);
  const c = canvas.getContext('2d');
  c.setTransform(1, 0, 0, 1, 0, 0);
  c.clearRect(0, 0, canvas.width, canvas.height);
  // le coordinate restano «board·zoom» (come handles() di select.js); k le porta sulla stessa scala s dell'anteprima
  const k = s / zoom;
  c.setTransform(k, 0, 0, k, 0, 0);
  const px = 1 / k;   // una linea di un pixel del canvas
  if (grid && zoom >= 3) {
    c.fillStyle = GRID;
    for (let x = 1; x < W; x++) c.fillRect(x * zoom, 0, px, H * zoom);
    for (let y = 1; y < H; y++) c.fillRect(0, y * zoom, W * zoom, px);
  }
  const rect = (b, color, dash, width) => {
    c.save();
    c.strokeStyle = color; c.lineWidth = width; c.setLineDash(dash);
    const w = Math.max(b.w, 1 / zoom), h = Math.max(b.h, 1 / zoom);
    c.strokeRect(b.x * zoom + width / 2, b.y * zoom + width / 2, w * zoom - width, h * zoom - width);
    c.restore();
  };
  const warnedSet = new Set(warned), selSet = new Set(sel);
  for (const b of boxes) if (warnedSet.has(b.id) && !selSet.has(b.id)) rect(b, WARN, [4, 3], 1);
  // bloccato: grigio e a tratti corti, senza maniglie (si sceglie solo da «Livelli»)
  for (const b of boxes) if (selSet.has(b.id)) rect(b, b.locked ? LOCK : SEL, b.locked ? [2, 2] : warnedSet.has(b.id) ? [4, 3] : [], 2);
  c.fillStyle = GUIDE;
  for (const g of guides) {
    if (g.axis === 'x') c.fillRect(g.at * zoom - 0.5, 0, 1, H * zoom);
    else c.fillRect(0, g.at * zoom - 0.5, W * zoom, 1);
  }
  if (marquee) {
    c.fillStyle = 'rgba(38, 70, 196, 0.10)';
    c.fillRect(marquee.x * zoom, marquee.y * zoom, marquee.w * zoom, marquee.h * zoom);
    rect(marquee, SEL, [3, 3], 1);
  }
  // maniglie: quadratini visibili da 8 px; l'area utile (r) è più larga col tocco
  for (const hd of handles) {
    c.fillStyle = '#FFFFFF'; c.fillRect(hd.cx - 4, hd.cy - 4, 8, 8);
    c.strokeStyle = SEL; c.lineWidth = 1.5; c.setLineDash([]); c.strokeRect(hd.cx - 4, hd.cy - 4, 8, 8);
  }
}

// zoom iniziale: il più grande intero (1–6) che, con la scala vera di scaleFor, fa stare la scheda nella larghezza
export function fitZoom(width, dpr = 1) {
  const w = Number(width) || 0;
  for (let z = 6; z > 1; z--) if (W * scaleFor(z, dpr).css <= w) return z;
  return 1;
}
