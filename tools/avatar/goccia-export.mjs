// Converte la Goccia nel formato avatar v1 eseguendo davvero mascot() del simulatore
// (estratta da reference/calamaio-os-browser/assets/calamaio-test.js come fa tools/golden/mascot-golden.js).
// Inchiostro → '#', carta disegnata → '+', mai toccato → '.'. La corsa (run) non è in mascot(): viene da goccia-run.mjs.
// Uso: node tools/avatar/goccia-export.mjs [uscita]   (predefinita: avatars/goccia.avatar.json)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { W, H, POSES, serialize } from './lib/format.mjs';
import { gocciaRun } from './goccia-run.mjs';

// Pose che mascot() sa disegnare (tutte tranne la corsa).
export const MASCOT_POSES = POSES.filter((p) => p !== 'run');

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

export function extractMascot() {
  const src = fs.readFileSync(path.join(root, 'reference/calamaio-os-browser/assets/calamaio-test.js'), 'utf8');
  const hw = src.match(/const HW = \[[^\]]*\];/);
  if (!hw) throw new Error('const HW non trovato: il simulatore è cambiato');
  const start = src.indexOf('function mascot(');
  if (start < 0) throw new Error('function mascot non trovata');
  let i = src.indexOf('{', start), depth = 0;
  for (; i < src.length; i++) { if (src[i] === '{') depth++; else if (src[i] === '}' && --depth === 0) break; }
  const fn = src.slice(start, i + 1);
  return new Function(`const INK='#000', PAPER='#fff'; ${hw[0]} ${fn}; return mascot;`)();
}

export function gocciaFrame(mascot, pose, f) {
  const g = Array.from({ length: H }, () => Array(W).fill('.'));
  const c = { fillStyle: '', fillRect(x, y, w, h) {
    for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++) {
      if (yy < 0 || yy >= H || xx < 0 || xx >= W) throw new Error(`la Goccia esce dalla griglia ${W}×${H}: ${pose}[${f}] (${xx},${yy})`);
      g[yy][xx] = this.fillStyle === '#000' ? '#' : '+';
    }
  } };
  mascot(c, 0, 0, 1, pose, f);
  return g.map((r) => r.join(''));
}

export function goccia() {
  const mascot = extractMascot();
  const av = {
    format: 'calamaio-avatar', version: 1, id: 'goccia', name: 'Goccia', author: 'Calamaio OS',
    description: 'La goccia d\'inchiostro di Calamaio: avatar predefinito, generato da mascot() del simulatore.',
    w: W, h: H, frames: 4, poses: {}
  };
  for (const p of MASCOT_POSES) av.poses[p] = [0, 1, 2, 3].map((f) => gocciaFrame(mascot, p, f));
  av.poses.run = gocciaRun();
  return av;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const out = process.argv[2] || path.join(root, 'avatars/goccia.avatar.json');
  fs.writeFileSync(out, serialize(goccia()));
  console.log('scritto', out);
}
