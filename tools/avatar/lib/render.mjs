// Fogli di anteprima (PNG) e anteprima testuale per il terminale.
import { POSES, FALLBACK, BUBBLE, poseFrames, W as AW, H as AH } from './format.mjs';
import { Canvas, INK_RGB, PAPER_RGB } from './png.mjs';
import { getAnchor, ANCHOR_NAMES } from './anchors.mjs';
import { renderWith, frameProblems } from './draw-accessory.mjs';

const GUIDE = [178, 180, 170];
const LABEL = { working: 'lavora', permission: 'permesso', input: 'attende', done: 'finito', error: 'errore', idle: 'pausa', compact: 'compatta', tired: 'stanca', stuffy: 'aria', run: 'corre' };

// Foglio di un avatar: per ogni posa i fotogrammi a scala 4, poi a scala 1, poi su fondo nero (scala 2).
export function sheet(av, opts = {}) {
  const s = 4, fw = av.w * s, fh = av.h * s, gap = 10, lab = 150, pad = 16;
  const smallW = av.frames * (av.w + 6), invW = av.frames * (av.w * 2 + 6);
  const W = pad + lab + av.frames * (fw + gap) + 14 + smallW + 14 + invW + pad;
  const rowH = fh + 14, H = pad + 40 + POSES.length * rowH + pad;
  const c = new Canvas(W, H);
  c.text(`${av.name}  (${av.id})  ${av.frames} fotogrammi`, pad, pad, 3);
  c.text('scala 4 - scala 1 - su inchiostro', pad, pad + 22, 2, GUIDE.map((v) => v - 60));
  POSES.forEach((p, r) => {
    const y = pad + 40 + r * rowH;
    const frs = poseFrames(av, p), own = !!av.poses[p];
    c.text(p, pad, y + 4, 2);
    c.text(LABEL[p], pad, y + 18, 2, GUIDE.map((v) => v - 60));
    if (!own) c.text(`= ${FALLBACK[p]}`, pad, y + 32, 2, GUIDE.map((v) => v - 60));
    frs.forEach((fr, f) => {
      const x = pad + lab + f * (fw + gap);
      if (opts.guides !== false) {
        c.box(x - 1, y - 1, fw + 2, fh + 2, GUIDE);
        c.box(x + BUBBLE.x0 * s, y + BUBBLE.y0 * s, (BUBBLE.x1 - BUBBLE.x0 + 1) * s, (BUBBLE.y1 - BUBBLE.y0 + 1) * s, [200, 201, 193]);
      }
      c.frame(fr, x, y, s);
      c.text(String(f), x + 2, y + fh - 12, 2, GUIDE);
      const sx = pad + lab + av.frames * (fw + gap) + 14 + f * (av.w + 6);
      c.frame(fr, sx, y + 4, 1);
      const ix = pad + lab + av.frames * (fw + gap) + 14 + smallW + 14 + f * (av.w * 2 + 6);
      c.rect(ix - 2, y + 2, av.w * 2 + 4, av.h * 2 + 4, INK_RGB);
      c.frame(fr, ix, y + 4, 2, PAPER_RGB, INK_RGB);  // colori invertiti, come sul fondo nero dei banner
    });
  });
  return c.png();
}

// Confronto di più avatar: righe = pose, colonne = avatar (fotogrammi 0 e 1 a scala 3, poi scala 1).
export function compare(avs) {
  const s = 3, fw = 28 * s, fh = 25 * s, pad = 16, lab = 120;
  const colW = 2 * (fw + 6) + 28 + 24;
  const W = pad + lab + avs.length * colW + pad, rowH = fh + 10, H = pad + 30 + POSES.length * rowH + pad;
  const c = new Canvas(W, H);
  avs.forEach((av, k) => c.text(av.name, pad + lab + k * colW, pad, 3));
  POSES.forEach((p, r) => {
    const y = pad + 30 + r * rowH;
    c.text(p, pad, y + 4, 2);
    avs.forEach((av, k) => {
      const frs = poseFrames(av, p), x0 = pad + lab + k * colW;
      [0, 1].forEach((f) => { c.frame(frs[f], x0 + f * (fw + 6), y, s); });
      c.frame(frs[0], x0 + 2 * (fw + 6), y + 4, 1);
    });
  });
  return c.png();
}

// Schermata Home «Goccia» della Heltec 250×122 (scala 4 in x 4, y 10) con un riquadro di testo finto.
export function homeMock(av, pose, f) {
  const c = new Canvas(250 * 3, 122 * 3);
  const sub = new Canvas(250, 122);
  sub.frame(poseFrames(av, pose)[f], 4, 10, 4);
  sub.rect(120, 18, 90, 14, INK_RGB); sub.rect(120, 44, 70, 9, INK_RGB); sub.rect(120, 61, 110, 6, INK_RGB);
  sub.box(120, 78, 124, 8, INK_RGB); sub.rect(120, 78, 60, 8, INK_RGB);
  for (let y = 0; y < 122; y++) for (let x = 0; x < 250; x++) {
    const i = (y * 250 + x) * 3; c.rect(x * 3, y * 3, 3, 3, [sub.px[i], sub.px[i + 1], sub.px[i + 2]]);
  }
  return c.png();
}

// Anteprima testuale: due caratteri per pixel; ██ inchiostro, ░░ carta disegnata, spazi trasparente.
export function text(av, poses = POSES) {
  const out = [];
  for (const p of poses) {
    const frs = poseFrames(av, p);
    out.push(`${p}${av.poses[p] ? '' : ` (ripiego: ${FALLBACK[p]})`}`);
    for (let y = 0; y < av.h; y++) {
      out.push(frs.map((fr) => Array.from(fr[y], (ch) => (ch === '#' ? '██' : ch === '+' ? '░░' : '  ')).join('')).join(' │ '));
    }
    out.push('');
  }
  return out.join('\n');
}

// Barra di avanzamento dell'aggiornamento (docs/OTA.md §1 punto 6, docs/AVATAR.md §12): 230×10 su uno schermo largo 250.
export const BAR = { screenW: 250, x: 10, w: 230, h: 10 };
// Origine della figura che corre: bordo destro (colonna w−1) sul fronte della parte completata, piedi (riga h−1)
// sul bordo superiore della barra; agli estremi la figura resta dentro lo schermo.
export function runOrigin(pct, s, barY, bar = BAR) {
  const front = bar.x + Math.round((bar.w * pct) / 100);
  const ox = Math.max(0, Math.min(bar.screenW - AW * s, front - AW * s));
  return { front, ox, oy: barY - AH * s };
}
export function drawBar(c, pct, barY, bar = BAR, ink = INK_RGB) {
  const done = Math.round((bar.w * pct) / 100);
  c.box(bar.x, barY, bar.w, bar.h, ink);
  c.rect(bar.x, barY, done, bar.h, ink);
}

// Simulazione della barra: 6 istantanee (0, 20, … 100%) a scala 1 e 2, a ogni istantanea il fotogramma successivo.
// Ogni pixel dello schermo è ingrandito zoom volte per guardarla; frames2 = profilo ridotto (solo fotogrammi 0-1).
export function barSheet(av, opts = {}) {
  const zoom = opts.zoom || 3, steps = [0, 20, 40, 60, 80, 100], scales = [1, 2], pad = 16, gap = 24, lab = 30;
  const stripH = (s) => 6 + AH * s + BAR.h + 6;
  const colW = BAR.screenW * zoom, W = pad + scales.length * colW + (scales.length - 1) * gap + pad;
  const rowH = stripH(2) * zoom + 8, H = pad + lab + steps.length * rowH + pad;
  const c = new Canvas(W, H);
  const frs = poseFrames(av, 'run'), nf = opts.frames2 ? 2 : frs.length;
  c.text(`${av.name}: corsa sulla barra  ${av.poses.run ? '' : '(ripiego working)'}${opts.frames2 ? '  profilo ridotto: fotogrammi 0-1' : ''}`, pad, pad, 3);
  scales.forEach((s, k) => c.text(`scala ${s}`, pad + k * (colW + gap), pad + 18, 2, GUIDE.map((v) => v - 60)));
  steps.forEach((pct, r) => {
    const fr = frs[r % nf];
    scales.forEach((s, k) => {
      const sub = new Canvas(BAR.screenW, stripH(s));
      const barY = stripH(s) - 6 - BAR.h;
      drawBar(sub, pct, barY);
      const { ox, oy } = runOrigin(pct, s, barY);
      sub.frame(fr, ox, oy, s);
      const x0 = pad + k * (colW + gap), y0 = pad + lab + r * rowH;
      for (let y = 0; y < sub.h; y++) for (let x = 0; x < BAR.screenW; x++) {
        const i = (y * sub.w + x) * 3; c.rect(x0 + x * zoom, y0 + y * zoom, zoom, zoom, [sub.px[i], sub.px[i + 1], sub.px[i + 2]]);
      }
      c.box(x0 - 1, y0 - 1, colW + 2, sub.h * zoom + 2, GUIDE);
      c.text(`${pct}% f${r % nf}`, x0 + 4, y0 + 4, 2, GUIDE.map((v) => v - 60));
    });
  });
  return c.png();
}

// Foglio delle ancore: ogni posa × fotogramma a scala 6, con i punti di aggancio marcati a colori
// (riquadro attorno al punto, il pixel sotto resta visibile). Serve a rivederle a occhio (docs/AVATAR.md §13).
export const ANCHOR_RGB = { 'head-top': [220, 30, 30], eyes: [30, 90, 230], mouth: [20, 160, 60], neck: [240, 140, 0], hand: [200, 40, 200] };
export function anchorSheet(av) {
  const s = 6, fw = av.w * s, fh = av.h * s, gap = 12, lab = 130, pad = 16;
  const W = pad + lab + av.frames * (fw + gap) + pad, rowH = fh + 12, H = pad + 56 + POSES.length * rowH + pad;
  const c = new Canvas(W, H);
  c.text(`${av.name}  (${av.id})  ancore`, pad, pad, 3);
  let lx = pad;
  for (const n of ANCHOR_NAMES) { c.rect(lx, pad + 26, 12, 12, ANCHOR_RGB[n]); lx = c.text(n, lx + 16, pad + 28, 2) + 18; }
  if (!av.anchors) c.text('nessuna ancora: gli accessori non si disegnano', lx, pad + 28, 2, ANCHOR_RGB['head-top']);
  POSES.forEach((p, r) => {
    const y = pad + 56 + r * rowH, frs = poseFrames(av, p);
    c.text(p, pad, y + 4, 2);
    if (!av.poses[p]) c.text(`= ${FALLBACK[p]}`, pad, y + 18, 2, GUIDE.map((v) => v - 60));
    frs.forEach((fr, f) => {
      const x = pad + lab + f * (fw + gap);
      c.box(x - 1, y - 1, fw + 2, fh + 2, GUIDE);
      for (let yy = 0; yy < av.h; yy += 1) for (let xx = 0; xx < av.w; xx++) if ((xx + yy) % 2 === 0 && fr[yy][xx] === '.') c.rect(x + xx * s + 2, y + yy * s + 2, 2, 2, [200, 201, 193]);
      c.frame(fr, x, y, s);
      for (const n of ANCHOR_NAMES) {
        const pt = getAnchor(av, p, f, n);
        if (!pt) continue;
        const px = x + pt[0] * s, py = y + pt[1] * s, rgb = ANCHOR_RGB[n];
        c.box(px - 2, py - 2, s + 4, s + 4, rgb); c.box(px - 1, py - 1, s + 2, s + 2, rgb);
      }
      c.text(String(f), x + 2, y + fh - 12, 2, GUIDE);
    });
  });
  return c.png();
}

// ---------------------------------------------------------------- accessori (docs/AVATAR.md §14.4)
// Foglio di un accessorio: per ogni avatar (due per fascia) le 10 pose × i 4 fotogrammi a scala 3, poi i
// fotogrammi 0 e 1 a scala 1 (Contatori). Riquadro rosso = fotogramma con un problema di checkAccessory.
const RED = [220, 30, 30];
export function accessorySheet(acc, avatars) {
  const s = 3, fw = 28 * s, fh = 25 * s, gap = 8, lab = 104, pad = 16, small = 2 * (28 + 6) + 8;
  const blockW = 4 * (fw + gap) + 12 + small, perBand = 2, bands = Math.ceil(avatars.length / perBand);
  const rowH = fh + gap, bandH = 26 + POSES.length * rowH + 14;
  const W = pad + perBand * (lab + blockW) + pad, H = pad + 44 + bands * bandH + pad;
  const c = new Canvas(W, H);
  c.text(`${acc.name}  (${acc.id})  ${acc.w}x${acc.h}  ${acc.frames} fotogramm${acc.frames > 1 ? 'i' : 'a'}  ancora ${acc.anchor}${acc.outline ? '  contorno' : ''}`, pad, pad, 3);
  c.text(`nascosto in: ${acc.hidePoses.length ? acc.hidePoses.join(' ') : 'nessuna posa'}   scala 3 - scala 1   riquadro rosso = problema`, pad, pad + 22, 2, GUIDE.map((v) => v - 60));
  avatars.forEach((av, k) => {
    const bx = pad + (k % perBand) * (lab + blockW), by = pad + 44 + Math.floor(k / perBand) * bandH;
    c.text(av.name + (av.anchors ? '' : '  (senza ancore: niente accessori)'), bx, by, 3, av.anchors ? INK_RGB : RED);
    POSES.forEach((p, r) => {
      const y = by + 26 + r * rowH;
      c.text(p, bx, y + 4, 2);
      if (acc.hidePoses.includes(p)) c.text('nascosto', bx, y + 18, 2, GUIDE.map((v) => v - 60));
      for (let f = 0; f < 4; f++) {
        const x = bx + lab + f * (fw + gap);
        const rows = renderWith(av, acc, p, f);
        const bad = frameProblems(acc, av, p, f).length > 0;
        c.box(x - 1, y - 1, fw + 2, fh + 2, bad ? RED : GUIDE);
        if (bad) c.box(x - 2, y - 2, fw + 4, fh + 4, RED);
        c.box(x + BUBBLE.x0 * s, y + BUBBLE.y0 * s, (BUBBLE.x1 - BUBBLE.x0 + 1) * s, (BUBBLE.y1 - BUBBLE.y0 + 1) * s, [200, 201, 193]);
        c.frame(rows, x, y, s);
        if (f < 2) c.frame(rows, bx + lab + 4 * (fw + gap) + 12 + f * (28 + 6), y + 4, 1);
      }
    });
  });
  return c.png();
}

// Catalogo: una riga per accessorio, per ogni avatar quattro istantanee a scala 3 (lavora, permesso, finito, corre).
export const CATALOG_SHOTS = [['working', 0], ['permission', 1], ['done', 2], ['run', 0]];
export function accessoryCatalog(accs, avatars) {
  const s = 3, fw = 28 * s, fh = 25 * s, gap = 6, lab = 150, pad = 16, gapAv = 18;
  const avW = CATALOG_SHOTS.length * (fw + gap) + gapAv, rowH = fh + 12;
  const W = pad + lab + avatars.length * avW + pad, H = pad + 40 + accs.length * rowH + pad;
  const c = new Canvas(W, H);
  avatars.forEach((av, k) => c.text(av.name, pad + lab + k * avW, pad, 3));
  c.text(CATALOG_SHOTS.map(([p, f]) => `${p} ${f}`).join('  '), pad + lab, pad + 22, 2, GUIDE.map((v) => v - 60));
  accs.forEach((acc, r) => {
    const y = pad + 40 + r * rowH;
    c.text(acc.name, pad, y + 4, 2);
    c.text(acc.anchor, pad, y + 18, 2, GUIDE.map((v) => v - 60));
    avatars.forEach((av, k) => CATALOG_SHOTS.forEach(([p, f], j) => {
      const x = pad + lab + k * avW + j * (fw + gap);
      c.box(x - 1, y - 1, fw + 2, fh + 2, GUIDE);
      c.frame(renderWith(av, acc, p, f), x, y, s);
    }));
  });
  return c.png();
}
