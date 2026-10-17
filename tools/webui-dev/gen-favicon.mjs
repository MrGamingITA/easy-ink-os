// Genera service/internal/webui/static/assets/favicon.svg dalla Goccia (posa working, fotogramma 0).
//   node gen-favicon.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { mascot } from '../../service/internal/webui/static/assets/js/mascot.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const INK = '#1E2022', PAPER = '#D6D7CF';
const px = new Map();
const ctx = { fillStyle: '', fillRect(x, y) { px.set(x + ',' + y, this.fillStyle); } };
mascot(ctx, 1, 1, 1, 'working', 0, INK, PAPER);
const rects = [...px].filter(([, c]) => c === INK).map(([k]) => { const [x, y] = k.split(','); return `<rect x="${x}" y="${y}" width="1" height="1"/>`; });
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 29 29" shape-rendering="crispEdges"><rect width="29" height="29" rx="4" fill="${PAPER}"/><g fill="${INK}" transform="translate(0 1)">${rects.join('')}</g></svg>\n`;
const out = path.resolve(here, '../../service/internal/webui/static/assets/favicon.svg');
fs.writeFileSync(out, svg);
console.log('scritto ' + out + ' (' + rects.length + ' punti)');
