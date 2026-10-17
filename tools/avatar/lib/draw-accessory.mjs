// Disegno di riferimento degli accessori sopra la mascotte e controlli visivi (docs/AVATAR.md §14.2, §14.4).
// È la funzione che firmware e simulatore copiano: i loro golden si generano da qui.
import { POSES, W, H, BUBBLE, BUBBLE_POSES, poseFrames } from './format.mjs';
import { getAnchor } from './anchors.mjs';

export const MIN_CONTRAST = 0.6;   // quota minima di pixel dell'accessorio distinguibili dal fondo (specifica §1.4)
const NB8 = [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]];

export const toGrid = (rows) => rows.map((r) => Array.from(r));
export const fromGrid = (g) => g.map((r) => r.join(''));

// Disegna l'accessorio sulla griglia 28×25 ('.' '#' '+') che contiene già la mascotte di (pose, frame).
// Salta (null, griglia intatta) se la posa chiesta è in hidePoses o l'ancora non c'è. Ordine:
// 1. contorno (se outline): carta sui pixel d'INCHIOSTRO della mascotte vicini (8 direzioni) a un pixel
//    dell'accessorio che risulta inchiostro; mai sotto l'accessorio, mai sul trasparente;
// 2. accessorio: '#' inchiostro, '+' carta, 'x' l'opposto di quello che c'è sotto ('#' → carta, '.' o '+' → inchiostro).
// I pixel fuori dal riquadro si tagliano (contati in clipped).
export function drawAccessory(grid, av, acc, pose, frame) {
  if (!acc || (acc.hidePoses || []).includes(pose)) return null;
  const pt = getAnchor(av, pose, frame, acc.anchor);
  if (!pt) return null;
  const r = (acc.perAvatar && acc.perAvatar[av.id]) || {};
  const frs = r.pixels || acc.pixels, n = frs.length;
  const fr = frs[((frame % n) + n) % n];
  const ox = pt[0] - acc.origin[0] + (r.dx || 0), oy = pt[1] - acc.origin[1] + (r.dy || 0);
  const cells = [];
  let clipped = 0;
  fr.forEach((row, y) => {
    for (let x = 0; x < row.length; x++) {
      const sym = row[x];
      if (sym === '.') continue;
      const gx = ox + x, gy = oy + y;
      if (gx < 0 || gy < 0 || gx >= W || gy >= H) { clipped++; continue; }
      const under = grid[gy][gx];
      const final = sym === 'x' ? (under === '#' ? '+' : '#') : sym;
      cells.push({ x: gx, y: gy, sym, under, final });
    }
  });
  const occ = new Set(cells.map((c) => c.y * W + c.x));
  const ring = [];
  if (acc.outline) {
    const seen = new Set();
    for (const c of cells) {
      if (c.final !== '#') continue;
      for (const [dx, dy] of NB8) {
        const x = c.x + dx, y = c.y + dy, k = y * W + x;
        if (x < 0 || y < 0 || x >= W || y >= H || occ.has(k) || seen.has(k) || grid[y][x] !== '#') continue;
        seen.add(k); ring.push([x, y]);
      }
    }
    for (const [x, y] of ring) grid[y][x] = '+';
  }
  for (const c of cells) grid[c.y][c.x] = c.final;
  return { cells, ring, clipped, ox, oy };
}

// Fotogramma della mascotte (con il ripiego della posa) con l'accessorio sopra; acc null = solo la mascotte.
export function renderWith(av, acc, pose, frame) {
  const frs = poseFrames(av, pose), n = frs.length;
  const g = toGrid(frs[((frame % n) + n) % n]);
  if (acc) drawAccessory(g, av, acc, pose, frame);
  return fromGrid(g);
}

// Problemi di un fotogramma (avatar, posa, f del display): fuori dal riquadro, angolo della nuvoletta nelle
// pose che la usano, contrasto sotto MIN_CONTRAST. [] se l'accessorio lì non si disegna.
export function frameProblems(acc, av, pose, f) {
  const frs = poseFrames(av, pose);
  if (!frs) return [];
  const g = toGrid(frs[((f % frs.length) + frs.length) % frs.length]);
  const res = drawAccessory(g, av, acc, pose, f);
  if (!res) return [];
  const out = [];
  if (res.clipped) out.push(`${res.clipped} pixel fuori dal riquadro 28×25`);
  if (BUBBLE_POSES.includes(pose)) {
    const inB = ([x, y]) => x >= BUBBLE.x0 && x <= BUBBLE.x1 && y >= BUBBLE.y0 && y <= BUBBLE.y1;
    const n = res.cells.filter((c) => inB([c.x, c.y])).length + res.ring.filter(inB).length;
    if (n) out.push(`entra nell'angolo della nuvoletta (x ${BUBBLE.x0}..${BUBBLE.x1}, y ${BUBBLE.y0}..${BUBBLE.y1}) con ${n} pixel`);
  }
  if (res.cells.length) {
    // distinguibile: colore diverso da quello sotto; con outline anche un pixel d'inchiostro sull'inchiostro che
    // tocca (8 direzioni) il contorno di carta, perché è il contorno a staccarlo dal corpo
    const ring = new Set(res.ring.map(([x, y]) => y * W + x));
    const touches = (c) => NB8.some(([dx, dy]) => ring.has((c.y + dy) * W + c.x + dx) && c.x + dx >= 0 && c.x + dx < W);
    const seen = res.cells.filter((c) => c.final !== (c.under === '.' ? '+' : c.under) || (c.final === '#' && touches(c))).length;
    const q = seen / res.cells.length;
    if (q < MIN_CONTRAST) out.push(`solo ${Math.floor(q * 100)}% dei pixel si distingue dal fondo (minimo ${MIN_CONTRAST * 100}%)`);
  }
  return out;
}

// Controlli su tutti gli avatar × tutte le pose × i fotogrammi del display (0..max(frames)−1). Gli avatar senza
// ancore non portano accessori: niente da controllare. Messaggi raggruppati per fotogrammi: "gatto working[0,2]: …".
export function checkAccessory(acc, avatars) {
  const found = new Map();
  for (const av of avatars) {
    if (!av.anchors) continue;
    for (const pose of POSES) {
      const frs = poseFrames(av, pose);
      if (!frs) continue;
      for (let f = 0; f < Math.max(frs.length, acc.frames); f++) {
        for (const msg of frameProblems(acc, av, pose, f)) {
          const k = `${av.id} ${pose}|${msg}`;
          if (!found.has(k)) found.set(k, []);
          found.get(k).push(f);
        }
      }
    }
  }
  return [...found.entries()].map(([k, fs]) => { const [head, msg] = k.split('|'); return `${head}[${fs.join(',')}]: ${msg}`; });
}
