// Renderer dei temi (docs/TEMI.md §6): lo stesso algoritmo dell'interprete della scheda (firmware/src/ui/theme_draw.cpp)
// sul tema abbassato da lower.js. La parità pixel per pixel la prova firmware/test/test_theme_parity sul golden scritto
// da tools/golden/theme-golden.mjs. Nessuna dipendenza dal DOM: si importa anche in Node.
//
// La scheda disegna solo blocchi che passano checkBlock; l'editor invece passa anche JSON a metà. Come la scheda, se un
// elemento di una variante della schermata è fuori dai limiti di checkBlock/checkElem (theme_format.cpp) l'anteprima è
// la schermata di serie: drawn falso e niente disegnato. Così non ci sono eccezioni, cicli infiniti o divisioni per
// zero, e l'anteprima non mostra mai un tema che la scheda rifiuterebbe. Non si controlla qui il limite di 4 096 byte
// del blocco (serve la codifica di compile.go): lo rifiuta il servizio.
import { T, wrapLines, jsRound, hasFont } from './text.js';
import * as P from './prims.js';
import { encode as qrEncode } from './qr.js';
import { Swap } from './surface.js';
import * as V from './values.js';
import { renderWith } from '../accessory.js';

// ordine di cal::MascotPose (i codici di pose in lower.js)
const POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy'];
const W = 250, H = 122, COORD = 512, MAX_SEGS = 16, MAX_ROW = 32, MAX_ELEMS = 64, MAX_CONDS = 8, MAX_VARIANTS = 8;
const AV_W = 28, AV_H = 25;
const clampi = (v, a, b) => (v < a ? a : v > b ? b : v);
const fdiv = (a, b) => Math.floor(a / b);
const fmod = (a, b) => a - fdiv(a, b) * b;
const rssiBars = (r) => (r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0);  // model.cpp

// ---------- limiti di checkElem ----------
const isInt = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
// fontExists() di theme_format.cpp: combinazione esatta (Chakra 500/600/700, Silkscreen 400/700)
const fontOk = (size, weight, pix) =>
  isInt(weight, 0, 12700) && weight % 100 === 0 && (pix ? weight === 400 || weight === 700 : weight >= 500 && weight <= 700) && hasFont(size, weight, pix);
const segsOk = (segs, inRow) => Array.isArray(segs) && segs.length <= MAX_SEGS && segs.every((g) =>
  g.lit !== undefined ? utf8Bytes(g.lit) <= 255 : isInt(g.val, 0, 0x95) && (inRow || g.val < 0x40 || g.val >= 0x80));
const refOk = (r, inRow) => isInt(r, 0, 4) && (inRow || r !== 2);
const utf8Bytes = (t) => new TextEncoder().encode(t).length;
// checkElem: un elemento (con le righe, se è una lista); count = elementi visti nella variante, righe comprese
function elemOk(e, inRow, count) {
  if (!e || ++count.n > MAX_ELEMS) return false;
  if (!isInt(e.x, -COORD, COORD) || !isInt(e.y, -COORD, COORD) || !isInt(e.w, 0, COORD) || !isInt(e.h, 0, COORD)) return false;
  if (!Array.isArray(e.conds) || e.conds.length > MAX_CONDS) return false;
  switch (e.type) {
    case 1: return isInt(e.scale, 1, 4) && (isInt(e.pose, 0, 8) || e.pose === 255) && refOk(e.ref, inRow);
    case 2: return isInt(e.lines, 1, 8) && isInt(e.lh, 1, 64) && isInt(e.align, 0, 2) && segsOk(e.segs, inRow) && fontOk(e.size, e.weight, e.pix);
    case 3: case 4: return isInt(e.scale, 1, 2);
    case 5: return isInt(e.scale, 1, 3) && refOk(e.ref, inRow);
    case 6: return isInt(e.src, 0, 5);
    case 7: return segsOk(e.segs, inRow);
    case 8: return isInt(e.sym, 0, 7) && isInt(e.scale, 1, 3);
    case 9: return true;
    case 10: return isInt(e.border, 0, 8) && isInt(e.radius, 0, 8);
    case 11: return isInt(e.border, 0, 8);
    case 12: return !inRow && isInt(e.rowH, 1, 122) && isInt(e.gap, 0, 60) && isInt(e.pageSec, 1, 60) && Array.isArray(e.row) &&
      e.row.length >= 1 && e.row.length <= MAX_ROW && e.row.every((r) => elemOk(r, true, count));
  }
  return false;
}
// checkBlock: 1–8 varianti, ognuna con elementi validi (la scheda controlla tutte le varianti, non solo quella scelta)
function screenOk(vs) {
  if (vs.length < 1 || vs.length > MAX_VARIANTS) return false;
  return vs.every((v) => isInt(v.when, 0, 3) && isInt(v.ref, 0, 1) && Array.isArray(v.elems) &&
    v.elems.length <= MAX_ELEMS && v.elems.every((e) => elemOk(e, false, { n: 0 })));
}

// ---------- riquadro e cerchio (solo i pixel del canvas) ----------
function inRound(px, py, x, y, w, h, r) {
  if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h) return false;
  const dx = Math.min(px - x, x + w - 1 - px), dy = Math.min(py - y, y + h - 1 - py);
  return !(dx < r && dy < r && dx + dy < r);
}
function rect(c, x, y, w, h, t, fill, r, ink) {
  const ri = r - t > 0 ? r - t : 0;
  for (let py = clampi(y, 0, H); py < clampi(y + h, 0, H); py++)
    for (let px = clampi(x, 0, W); px < clampi(x + w, 0, W); px++) {
      if (!inRound(px, py, x, y, w, h, r)) continue;
      const border = t > 0 && !inRound(px, py, x + t, y + t, w - 2 * t, h - 2 * t, ri);
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}
const inCircle = (px, py, cx2, cy2, d) => { const dx = 2 * px + 1 - cx2, dy = 2 * py + 1 - cy2; return d > 0 && dx * dx + dy * dy <= d * d; };
function circle(c, x, y, w, h, t, fill, ink) {
  const d = Math.min(w, h), cx2 = 2 * x + d, cy2 = 2 * y + d;
  for (let py = clampi(y, 0, H); py < clampi(y + d, 0, H); py++)
    for (let px = clampi(x, 0, W); px < clampi(x + d, 0, W); px++) {
      if (!inCircle(px, py, cx2, cy2, d)) continue;
      const border = t > 0 && !inCircle(px, py, cx2, cy2, d - 2 * t);
      if (border || fill) c.fillRect(px, py, 1, 1, ink);
    }
}

// ---------- mascotte: kit::mascot = drawAvatar + accOverlay ----------
// sessionAvatar / defaultAvatar di avatars.cpp: id sconosciuto → la Goccia
function avatarOf(m, assets, s) {
  const id = s && s.avatar ? s.avatar : m.set.defAvatar;
  return (id && assets.avatar(id)) || assets.avatar('goccia');
}
function mascot(c, m, assets, s, ox, oy, sc, pose, f, acc, ink) {
  const av = avatarOf(m, assets, s);
  const base = av && renderWith(av, null, pose, f);
  if (!base) return;
  // '#' inchiostro, '+' carta, '.' trasparente; con ink falso si scambiano
  const paint = (rows, only) => rows.forEach((row, y) => {
    for (let x = 0; x < row.length; x++) {
      const ch = row[x];
      if (only ? only[y][x] === ch : ch !== '#' && ch !== '+') continue;
      c.fillRect(ox + x * sc, oy + y * sc, sc, sc, (ch === '#') === ink);
    }
  });
  paint(base, null);
  // accOverlay: con l'accessorio indossato si ridisegnano solo le celle che cambiano ('#' inchiostro, il resto carta)
  if (!acc || !V.petOn(m) || !m.pet.wear) return;
  const a = assets.accessory(m.pet.wear);
  const g = a && renderWith(av, a, pose, f);
  if (g) paint(g, base);
}

function barPct(m, src, s) {
  switch (src) {
    case 0: return s ? s.ctx : 0;
    case 1: return s && s.total ? (100 * s.done) / s.total : 0;
    case 2: return m.work.on ? V.workFrac(m) * 100 : 0;
    case 3: return m.sys.batt;
    case 4: return m.limits.h5 ?? 0;
    default: return m.limits.d7 ?? 0;
  }
}

function wake(info, t) { if (t > 0 && (!info.nextChange || t < info.nextChange)) info.nextChange = t; }
function mascotBox(info, x, y, w, h) {
  if (!info.hasMascot) { Object.assign(info, { hasMascot: true, mx: x, my: y, mw: w, mh: h }); return; }
  const x1 = Math.max(x + w, info.mx + info.mw), y1 = Math.max(y + h, info.my + info.mh);
  info.mx = Math.min(x, info.mx); info.my = Math.min(y, info.my); info.mw = x1 - info.mx; info.mh = y1 - info.my;
}

// Un elemento con origine (ox, oy) (la riga di una lista, altrimenti 0, 0). row: sessione della riga o null.
function elem(R, e, ox, oy, row) {
  const { cx, m } = R;
  const subject = row || (cx.ref >= 0 ? m.sessions[cx.ref] : null);
  if (!V.condsTrue(e.conds, cx, subject)) return;
  const x = e.x + ox, y = e.y + oy, ink = !e.invert;
  const c = e.invert ? new Swap(R.surf) : R.surf;
  switch (e.type) {
    case 1: {
      const s = V.refSession(m, cx, e.ref, row);
      const pose = e.pose === 255 ? (s ? V.pose(m, s) : 'idle') : POSES[e.pose];
      mascot(R.surf, m, R.assets, s, x, y, e.scale, pose, e.anim ? R.f : 0, e.acc, ink);
      mascotBox(R.info, x, y, AV_W * e.scale, AV_H * e.scale);
      break;
    }
    case 2: {
      // l'ora coi secondi chiede il ridisegno al secondo dopo, anche se il testo espanso è vuoto
      if (e.segs.some((g) => g.val === 0x81)) wake(R.info, cx.now + (1000 - fmod(cx.local, 1000)));
      const t = V.expand(e.segs, cx, row, e.upper);
      if (!t) break;
      const ax = e.align === 1 ? x + e.w / 2 : e.align === 2 ? x + e.w : x;
      const o = { weight: e.weight, pix: e.pix, align: e.align, ink, max: e.w };
      if (e.lines <= 1) { T(R.surf, t, ax, y, e.size, o); break; }
      wrapLines(t, e.lines, e.size, e.w, e.weight, e.pix).slice(0, e.lines).forEach((l, i) => T(R.surf, l, ax, y + i * e.lh, e.size, o));
      break;
    }
    case 3:
      P.battIcon(c, x, y, e.scale, m.sys.batt, true);
      if (e.percent) T(R.surf, jsRound(m.sys.batt) + '%', x + 16 * e.scale + 3, y + Math.trunc((8 * e.scale - 8) / 2), 8, { weight: 400, pix: true, align: 0, ink, max: 0 });
      break;
    case 4: P.wifiIcon(c, x, y, e.scale, m.sys.wifi ? rssiBars(m.sys.rssi) : -1, true); break;
    case 5: { const s = V.refSession(m, cx, e.ref, row); if (s) P.iconForState(c, s.state, x, y, e.scale, R.f, true); break; }
    case 6: {
      const pct = barPct(m, e.src, subject);
      if (e.big) { P.bigBar(c, x, y, e.w, e.h, pct); break; }
      const st = e.src === 1 && subject ? { done: subject.done, total: subject.total, working: subject.state === 'working' }
        : { done: jsRound(pct), total: 100, working: false };
      P.bar(c, x, y, e.w, e.h, st, R.f);
      break;
    }
    case 7: {
      const t = V.expand(e.segs, cx, row, false);
      const q = t ? qrEncode(t) : null;
      if (!q) break;
      const px = Math.max(1, Math.trunc(Math.min(e.w, e.h) / (q.size + 2)));
      c.fillRect(x, y, (q.size + 2) * px, (q.size + 2) * px, false);
      for (let r = 0; r < q.size; r++) for (let k = 0; k < q.size; k++) if (q.get(k, r)) c.fillRect(x + px + k * px, y + px + r * px, px, px, true);
      break;
    }
    case 8: P.sym(c, e.sym, x, y, e.scale, true); break;
    case 9:
      if (!e.dotted) c.fillRect(x, y, e.w, e.h, true);
      else if (!e.vertical) for (let r = 0; r < e.h; r++) P.dotsH(c, x, x + e.w, y + r);
      else for (let k = 0; k < e.w; k++) P.dotsV(c, x + k, y, y + e.h);
      break;
    case 10: rect(R.surf, x, y, e.w, e.h, e.border, e.fill, e.radius, ink); break;
    case 11: circle(R.surf, x, y, e.w, e.h, e.border, e.fill, ink); break;
  }
}

// Lista: una riga per sessione, a pagine; le condizioni della lista con la sessione della variante.
function list(R, e) {
  const { cx, m } = R;
  if (!V.condsTrue(e.conds, cx, cx.ref >= 0 ? m.sessions[cx.ref] : null)) return;
  const period = e.pageSec * 1000;
  const per = Math.max(1, Math.trunc((e.h + e.gap) / (e.rowH + e.gap)));
  const n = m.sessions.length, pages = Math.trunc((n + per - 1) / per);
  const slot = fdiv(cx.now, period);
  const page = pages > 1 ? fmod(slot, pages) : 0;
  if (pages > 1) wake(R.info, (slot + 1) * period);
  for (let i = 0; i < per; i++) {
    const k = page * per + i;
    if (k >= n) break;
    for (const r of e.row) elem(R, r, e.x, e.y + i * (e.rowH + e.gap), m.sessions[k]);
  }
  // pallini dopo tutte le righe, sopra
  if (e.dots && pages > 1) {
    const c = e.invert ? new Swap(R.surf) : R.surf;
    for (let pg = 0; pg < pages; pg++) {
      const sx = e.x + e.w - 4 * (pages - pg), sy = e.y + e.h - 3;
      if (pg === page) c.fillRect(sx, sy, 3, 3, true);
      else c.fillRect(sx + 1, sy + 1, 1, 1, true);
    }
  }
}

// Disegna la schermata key del tema abbassato su surf (fillRect(x, y, w, h, ink)). Stessi campi di ThemeDrawInfo:
// drawn falso = schermata di serie (assente, senza varianti, fuori dai limiti di checkBlock o nessuna variante vera),
// e su surf non si scrive niente.
// m: makeModel(); assets: { avatar(id) → avatar JSON con le ancore | null, accessory(id) → accessorio JSON | null }.
export function renderScreen(lowered, key, m, surf, assets) {
  const info = { drawn: false, nextChange: 0, hasMascot: false, mx: 0, my: 0, mw: 0, mh: 0 };
  const scr = lowered && lowered.screens && Object.hasOwn(lowered.screens, key) ? lowered.screens[key] : null;
  const vs = scr && Array.isArray(scr.variants) ? scr.variants : [];
  if (!screenOk(vs)) return info;  // mai a metà, come drawThemeBlock
  const v = vs.find((x) => V.whenTrue(m, x.when));
  if (!v) return info;
  const cx = { m, now: m.now, local: m.local, frame: m.frame, screen: key, ref: V.refIndex(m, key, v.ref) };
  const R = { cx, m, surf, assets, f: m.frame, info };
  for (const e of v.elems) (e.type === 12 ? list(R, e) : elem(R, e, 0, 0, null));
  info.drawn = true;
  return info;
}
