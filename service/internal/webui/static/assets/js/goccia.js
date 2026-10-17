// La Goccia animata su canvas: 4 fotogrammi, image-rendering: pixelated (vedi app.css).
// Con setAvatar disegna un avatar JSON (docs/AVATAR.md) al posto della Goccia, con la stessa regola
// del display: fotogramma f mod frames. Un solo timer per tutta la pagina: le figure si animano in
// fase tra loro (con uno sfasamento facoltativo, come il Dettaglio del simulatore) e ridisegnarle non
// le fa ripartire da capo.
import { mascot, MASCOT_W, MASCOT_H } from './mascot.js';
import { POSE_LABEL, ICONS } from './model.js';
import { drawAvatar } from './avatar.js';
import { renderWith, paintRows } from './accessory.js';

const INK = '#1E2022', PAPER = '#D6D7CF'; // stessi colori delle tavole del simulatore
const FRAME_MS = 250; // 4 fotogrammi al secondo, come il profilo «fluide» (fps 4)
const reduceMotion = window.matchMedia ? window.matchMedia('(prefers-reduced-motion: reduce)') : null;

const live = new Set();
let tick = 0, timer = 0;
const still = () => (reduceMotion && reduceMotion.matches) || document.hidden;
function schedule() {
  clearInterval(timer);
  timer = 0;
  if (still()) { tick = 0; live.forEach((g) => g.draw()); return; }
  if (live.size) timer = setInterval(step, FRAME_MS);
}
function step() {
  tick = (tick + 1) % 4096;
  for (const g of live) {
    if (!g.c.isConnected && g.attached) { live.delete(g); continue; }
    if (g.c.isConnected) g.attached = true;
    g.draw();
  }
  if (!live.size) schedule();
}
document.addEventListener('visibilitychange', schedule);
if (reduceMotion && reduceMotion.addEventListener) reduceMotion.addEventListener('change', schedule);

export class Goccia {
  // opts: { phase: sfasamento in fotogrammi, label: nome per lo screen reader }
  constructor(canvas, pose, opts) {
    this.c = canvas;
    this.ctx = canvas.getContext('2d');
    this.av = null;
    this.acc = null;
    this.phase = (opts && opts.phase) || 0;
    this.label = (opts && opts.label) || 'Goccia';
    this.pose = null;
    this.attached = canvas.isConnected;
    this.size();
    this.c.setAttribute('role', 'img');
    this.set(pose || 'idle');
    live.add(this);
    if (!timer) schedule();
  }
  size() {
    const w = this.av ? this.av.w : MASCOT_W, hgt = this.av ? this.av.h : MASCOT_H;
    if (this.c.width !== w + 2) this.c.width = w + 2;
    if (this.c.height !== hgt + 2) this.c.height = hgt + 2;
  }
  // av: oggetto avatar JSON, oppure null per la Goccia di mascot().
  setAvatar(av, label) {
    this.av = av || null;
    if (label) this.label = label;
    this.size();
    this.aria();
    this.draw();
  }
  // acc: accessorio JSON (docs/AVATAR.md §14) da disegnare sopra l'avatar, null per nessuno. Serve un
  // avatar con le ancore (av.anchors): senza, l'accessorio non si disegna, come sul display.
  setAccessory(acc) {
    this.acc = acc || null;
    this.draw();
  }
  set(pose) {
    if (pose === this.pose) return;
    this.pose = pose;
    this.c.dataset.pose = pose;
    this.aria();
    this.draw();
  }
  aria() {
    this.c.setAttribute('aria-label', (this.av ? this.av.name : this.label) + ', stato ' + (POSE_LABEL[this.pose] || this.pose));
  }
  frame() { return still() ? 0 : tick + this.phase; }
  draw() {
    const c = this.ctx;
    c.fillStyle = PAPER;
    c.fillRect(0, 0, this.c.width, this.c.height);
    if (this.av && this.acc) {
      const rows = renderWith(this.av, this.acc, this.pose, this.frame());
      if (rows) { paintRows(c, rows, 1, 1, 1, INK, PAPER); return; }
    }
    if (this.av && drawAvatar(c, this.av, 1, 1, 1, this.pose, this.frame(), INK, PAPER)) return;
    mascot(c, 1, 1, 1, this.pose, this.frame() % 4, INK, PAPER);
  }
  stop() { live.delete(this); }
}

// Icona 7×7 dello stato (ICONS del simulatore), disegnata su canvas.
export function stateIcon(state, color) {
  const rows = ICONS[state === 'working' ? 'working0' : state] || ICONS.idle;
  const cv = document.createElement('canvas');
  cv.width = 7; cv.height = 7;
  cv.className = 'px-icon';
  cv.setAttribute('aria-hidden', 'true');
  const c = cv.getContext('2d');
  c.fillStyle = color;
  rows.forEach((r, y) => { for (let x = 0; x < 7; x++) if (r[x] === '#') c.fillRect(x, y, 1, 1); });
  return cv;
}
