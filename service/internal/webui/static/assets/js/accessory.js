// Accessori della mascotte (docs/AVATAR.md §14) sopra un avatar, per l'anteprima della webapp.
// È la COPIA di drawAccessory() di tools/avatar/lib/draw-accessory.mjs e di getAnchor() di
// tools/avatar/lib/anchors.mjs: stessa regola pixel per pixel, verificata da
// service/internal/webui/accessory_parity_test.go (node) su tutto il catalogo × avatar × pose.
// Se il riferimento cambia, si ricopia qui. Nessuna dipendenza dal DOM: si importa anche in Node.

export const W = 28, H = 25;
export const ANCHOR_NAMES = ['head-top', 'eyes', 'mouth', 'neck', 'hand'];
const FALLBACK = { compact: 'working', tired: 'idle', stuffy: 'idle', run: 'working' };
const NB8 = [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]];

// Punto di aggancio della posa e del fotogramma (f mod frames, ripiego della posa); null se manca.
export function getAnchor(av, pose, frame, name) {
  if (!av || !av.anchors || !ANCHOR_NAMES.includes(name)) return null;
  let p = pose;
  if (!av.poses[p] && FALLBACK[p] && av.poses[FALLBACK[p]]) p = FALLBACK[p];
  const frs = av.anchors[p];
  if (!Array.isArray(frs) || !frs.length) return null;
  const n = frs.length, pt = frs[((frame % n) + n) % n];
  const v = pt && pt[name];
  return Array.isArray(v) ? [v[0], v[1]] : null;
}

// Fotogrammi effettivi della posa (ripiego delle facoltative), null se mancano.
export function poseFrames(av, pose) {
  if (!av || !av.poses) return null;
  if (av.poses[pose]) return av.poses[pose];
  if (FALLBACK[pose] && av.poses[FALLBACK[pose]]) return av.poses[FALLBACK[pose]];
  return null;
}

export const toGrid = (rows) => rows.map((r) => Array.from(r));
export const fromGrid = (g) => g.map((r) => r.join(''));

// Disegna l'accessorio sulla griglia ('.' '#' '+') che contiene già la mascotte di (pose, frame).
// null (griglia intatta) se la posa è in hidePoses o l'ancora non c'è. Contorno di carta sui pixel
// d'inchiostro della mascotte vicini a un pixel d'inchiostro dell'accessorio; 'x' = l'opposto di sotto.
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

// Fotogramma della mascotte con l'accessorio sopra (righe di testo); acc null = solo la mascotte.
export function renderWith(av, acc, pose, frame) {
  const frs = poseFrames(av, pose);
  if (!frs) return null;
  const n = frs.length;
  const g = toGrid(frs[((frame % n) + n) % n]);
  if (acc) drawAccessory(g, av, acc, pose, frame);
  return fromGrid(g);
}

// Righe → canvas a scala s in (ox, oy).
export function paintRows(c, rows, ox, oy, s, ink, paper) {
  for (let y = 0; y < rows.length; y++) {
    const row = rows[y];
    for (let x = 0; x < row.length; x++) {
      const ch = row[x];
      if (ch === '#') c.fillStyle = ink;
      else if (ch === '+') c.fillStyle = paper;
      else continue;
      c.fillRect(ox + x * s, oy + y * s, s, s);
    }
  }
}
