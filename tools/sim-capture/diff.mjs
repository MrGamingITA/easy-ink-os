#!/usr/bin/env node
// Confronto tra due immagini PBM 1 bit (P1 o P4), riferimento del simulatore e firmware.
//
//   node diff.mjs RIF.pbm FW.pbm [--out diff.png] [--threshold 0.5] [--size 250x122] [--json]
//   node diff.mjs --ref-dir out/heltec213 --fw-dir DIR [--out-dir DIR] [--threshold 0.5] [--json]
//
// Percentuale = pixel diversi / pixel totali * 100.
// PNG di differenza: rosso = inchiostro solo nel riferimento, blu = solo nel firmware,
// grigio chiaro = inchiostro in entrambi, bianco = carta in entrambi.
// Codici di uscita: 0 = entro la soglia, 1 = oltre la soglia (o file mancante in modalita
// cartella, salvo --allow-missing), 2 = errore d'uso o di formato.
import { readFileSync, writeFileSync, readdirSync, mkdirSync, existsSync } from 'node:fs';
import path from 'node:path';
import { decodePbm, encodePngRgb } from './lib/image.mjs';

const USAGE = `uso:
  node diff.mjs RIF.pbm FW.pbm [--out diff.png] [--threshold PCT] [--size WxH] [--json]
  node diff.mjs --ref-dir DIR --fw-dir DIR [--out-dir DIR] [--threshold PCT] [--size WxH] [--json]
  --allow-missing  (cartelle) le schermate assenti nel firmware non fanno fallire
  --threshold PCT  percentuale massima di pixel diversi accettata (predefinita 0)
  --size WxH       dimensioni attese (predefinite 250x122; "any" per non controllarle)`;

function fail(msg) { console.error('errore: ' + msg + '\n' + USAGE); process.exit(2); }

function parseArgs(argv) {
  const o = { files: [], out: null, threshold: 0, size: '250x122', json: false, refDir: null, fwDir: null, outDir: null, allowMissing: false };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i], next = () => { if (i + 1 >= argv.length) fail('manca il valore di ' + a); return argv[++i]; };
    if (a === '--out') o.out = next();
    else if (a === '--threshold') { o.threshold = Number(next()); if (!(o.threshold >= 0 && o.threshold <= 100)) fail('soglia non valida'); }
    else if (a === '--size') o.size = next();
    else if (a === '--json') o.json = true;
    else if (a === '--allow-missing') o.allowMissing = true;
    else if (a === '--ref-dir') o.refDir = next();
    else if (a === '--fw-dir') o.fwDir = next();
    else if (a === '--out-dir') o.outDir = next();
    else if (a === '-h' || a === '--help') { console.log(USAGE); process.exit(0); }
    else if (a.startsWith('--')) fail('opzione sconosciuta ' + a);
    else o.files.push(a);
  }
  return o;
}

function load(file, size) {
  let img;
  try { img = decodePbm(readFileSync(file)); } catch (e) { fail(file + ': ' + e.message); }
  if (size !== 'any') {
    const [w, h] = size.split('x').map(Number);
    if (img.w !== w || img.h !== h) fail(`${file}: ${img.w}x${img.h}, attese ${w}x${h}`);
  }
  return img;
}

export function compare(ref, fw) {
  if (ref.w !== fw.w || ref.h !== fw.h) throw new Error(`dimensioni diverse: ${ref.w}x${ref.h} contro ${fw.w}x${fw.h}`);
  const n = ref.w * ref.h, rgb = Buffer.alloc(n * 3);
  let onlyRef = 0, onlyFw = 0, both = 0;
  let x0 = ref.w, y0 = ref.h, x1 = -1, y1 = -1;
  for (let i = 0; i < n; i++) {
    const a = ref.bits[i], b = fw.bits[i], o = i * 3;
    let c;
    if (a && b) { both++; c = [200, 200, 200]; }
    else if (a) { onlyRef++; c = [220, 0, 0]; }
    else if (b) { onlyFw++; c = [0, 70, 230]; }
    else c = [255, 255, 255];
    rgb[o] = c[0]; rgb[o + 1] = c[1]; rgb[o + 2] = c[2];
    if (a !== b) { const x = i % ref.w, y = (i / ref.w) | 0; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
  }
  const diff = onlyRef + onlyFw;
  return {
    w: ref.w, h: ref.h, total: n, diff, onlyRef, onlyFw, bothInk: both,
    percent: Math.round(diff / n * 100 * 10000) / 10000,
    bbox: diff ? { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 } : null,
    rgb
  };
}

function report(name, r, threshold, outPng) {
  const ok = r.percent <= threshold;
  return {
    name, ok, percent: r.percent, diffPixels: r.diff, onlyRef: r.onlyRef, onlyFw: r.onlyFw, bothInk: r.bothInk,
    total: r.total, bbox: r.bbox, threshold, diffPng: outPng || null
  };
}

function line(rep) {
  const b = rep.bbox ? ` bbox ${rep.bbox.x},${rep.bbox.y} ${rep.bbox.w}x${rep.bbox.h}` : '';
  return `${rep.ok ? 'OK  ' : 'DIFF'} ${rep.name}: ${rep.percent.toFixed(4)}% (${rep.diffPixels} px: ${rep.onlyRef} solo rif, ${rep.onlyFw} solo fw)${b}${rep.diffPng ? ' -> ' + rep.diffPng : ''}`;
}

function main() {
  const o = parseArgs(process.argv);
  if (o.refDir || o.fwDir) {
    if (!o.refDir || !o.fwDir) fail('servono sia --ref-dir sia --fw-dir');
    const refs = readdirSync(o.refDir).filter((f) => f.endsWith('.pbm')).sort();
    if (!refs.length) fail('nessun .pbm in ' + o.refDir);
    if (o.outDir) mkdirSync(o.outDir, { recursive: true });
    const reps = [], missing = [];
    for (const f of refs) {
      const fwFile = path.join(o.fwDir, f);
      if (!existsSync(fwFile)) { missing.push(f); continue; }
      const r = compare(load(path.join(o.refDir, f), o.size), load(fwFile, o.size));
      let png = null;
      if (o.outDir) { png = path.join(o.outDir, f.replace(/\.pbm$/, '-diff.png')); writeFileSync(png, encodePngRgb(r.rgb, r.w, r.h)); }
      reps.push(report(f, r, o.threshold, png));
    }
    const bad = reps.filter((r) => !r.ok);
    if (o.json) console.log(JSON.stringify({ compared: reps.length, failed: bad.length, missingInFw: missing, results: reps }, null, 2));
    else {
      reps.forEach((r) => console.log(line(r)));
      if (missing.length) console.log(`MANCANO nel firmware (${missing.length}): ${missing.join(', ')}`);
      console.log(`\n${reps.length} confrontate, ${bad.length} oltre la soglia del ${o.threshold}%, ${missing.length} mancanti`);
    }
    process.exit(bad.length || (missing.length && !o.allowMissing) ? 1 : 0);
  }
  if (o.files.length !== 2) fail('servono due file PBM');
  const r = compare(load(o.files[0], o.size), load(o.files[1], o.size));
  if (o.out) writeFileSync(o.out, encodePngRgb(r.rgb, r.w, r.h));
  const rep = report(path.basename(o.files[1]), r, o.threshold, o.out);
  console.log(o.json ? JSON.stringify(rep, null, 2) : line(rep));
  process.exit(rep.ok ? 0 : 1);
}

if (import.meta.url === `file://${process.argv[1]}` || process.argv[1]?.endsWith('diff.mjs')) {
  try { main(); } catch (e) { console.error('errore: ' + (e && e.message || e)); process.exit(2); }
}
