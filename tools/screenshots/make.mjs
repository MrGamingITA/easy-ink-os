#!/usr/bin/env node
// Immagini del README in docs/img/. Nessuna è disegnata a mano:
//   - display: device.cpp compilato con i sorgenti del firmware (core, gfx, ui) disegna i PBM 250×122; qui si
//     ingrandiscono ×3 senza interpolazione (pixel per pixel) e si mettono in una cornice semplice;
//   - webapp: tools/webui-dev/mock-server.mjs con demo-state.json, fotografato con Playwright a 1280 px.
// Dati solo finti (demo-state.json). Uso: node tools/screenshots/make.mjs [--only device|web]
import { execFileSync, spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const OUT = path.join(REPO, 'docs', 'img');
const only = (() => { const i = process.argv.indexOf('--only'); return i > 0 ? process.argv[i + 1] : ''; })();
fs.mkdirSync(OUT, { recursive: true });

// ---------------------------------------------------------------- PNG RGBA, senza dipendenze
const CRC = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc32 = (b) => { let c = -1; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function png(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

// ---------------------------------------------------------------- PBM P4 → PNG con cornice
function readPbm(file) {
  const d = fs.readFileSync(file);
  const head = d.subarray(0, 20).toString('latin1').match(/^P4\s+(\d+)\s+(\d+)\s/);
  if (!head) throw new Error('PBM non valido: ' + file);
  const w = +head[1], h = +head[2], off = head[0].length, stride = (w + 7) >> 3;
  return { w, h, ink: (x, y) => (d[off + y * stride + (x >> 3)] >> (7 - (x & 7))) & 1 };
}

const S = 3;                    // ingrandimento intero, vicino più prossimo
const PAD = 30, R = 26;         // cornice: margine attorno allo schermo, raggio degli angoli
const PAPER = [236, 235, 228], INK = [27, 27, 27], BEZEL = [42, 42, 46], EDGE = [20, 20, 22];

function framed(pbm) {
  const { w, h, ink } = readPbm(pbm);
  const sw = w * S, sh = h * S, W = sw + 2 * PAD, H = sh + 2 * PAD;
  const px = Buffer.alloc(W * H * 4);
  const put = (x, y, c, a = 255) => { const i = (y * W + x) * 4; px[i] = c[0]; px[i + 1] = c[1]; px[i + 2] = c[2]; px[i + 3] = a; };
  // copertura degli angoli arrotondati (4×4 campioni per pixel)
  const cover = (x, y) => {
    let n = 0;
    for (let sy = 0; sy < 4; sy++) for (let sx = 0; sx < 4; sx++) {
      const fx = x + (sx + 0.5) / 4, fy = y + (sy + 0.5) / 4;
      const cx = Math.min(Math.max(fx, R), W - R), cy = Math.min(Math.max(fy, R), H - R);
      if ((fx - cx) ** 2 + (fy - cy) ** 2 <= R * R) n++;
    }
    return n / 16;
  };
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const a = cover(x, y);
    if (a > 0) put(x, y, BEZEL, Math.round(a * 255));
  }
  // bordo scuro di 2 px attorno al vetro, poi lo schermo pixel per pixel
  for (let y = PAD - 2; y < PAD + sh + 2; y++) for (let x = PAD - 2; x < PAD + sw + 2; x++) put(x, y, EDGE);
  for (let y = 0; y < sh; y++) for (let x = 0; x < sw; x++) put(PAD + x, PAD + y, ink((x / S) | 0, (y / S) | 0) ? INK : PAPER);
  return png(W, H, px);
}

function device() {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'calamaio-shots-'));
  const fw = path.join(REPO, 'firmware');
  const json = path.join(fw, '.pio', 'libdeps', 'native', 'ArduinoJson', 'src');
  if (!fs.existsSync(json)) throw new Error('manca ArduinoJson: prima `cd firmware && pio test -e native -f test_core` (scarica le librerie native)');
  const srcs = ['core', 'gfx', 'ui'].flatMap((d) => fs.readdirSync(path.join(fw, 'src', d), { recursive: true })
    .filter((f) => f.endsWith('.cpp')).map((f) => path.join(fw, 'src', d, f)));
  const bin = path.join(tmp, 'device');
  console.log('compilo device.cpp con ' + srcs.length + ' sorgenti del firmware…');
  execFileSync('c++', ['-std=c++17', '-O1', '-w', '-I' + path.join(fw, 'src'), '-I' + path.join(fw, 'lib', 'peanut_gb'), '-I' + json,
    ...srcs, path.join(HERE, 'device.cpp'), '-o', bin], { stdio: 'inherit' });
  execFileSync(bin, [tmp, REPO], { stdio: 'inherit' });
  for (const f of fs.readdirSync(tmp).filter((f) => f.endsWith('.pbm')).sort()) {
    const out = path.join(OUT, 'display-' + f.replace(/\.pbm$/, '.png'));
    fs.writeFileSync(out, framed(path.join(tmp, f)));
    console.log('  ' + path.relative(REPO, out));
  }
  fs.rmSync(tmp, { recursive: true, force: true });
}

// ---------------------------------------------------------------- webapp
function playwright() {
  const where = [process.env.PLAYWRIGHT_DIR, path.join(REPO, 'tools', 'webui-dev'), path.join(REPO, 'tools', 'sim-capture')].filter(Boolean);
  for (const d of where) {
    try { return createRequire(path.join(d, 'package.json'))('playwright'); } catch (e) { /* prossima */ }
  }
  throw new Error('playwright non trovato: `cd tools/webui-dev && npm install` (o PLAYWRIGHT_DIR=cartella con node_modules)');
}

// Testi che non devono finire nelle immagini: percorsi del Mac, più i dati personali (nome, MAC vero) scritti
// uno per riga in tools/screenshots/vietati.local.txt, escluso da git.
function forbidden() {
  const out = [/\/Users\//];
  try {
    for (const l of fs.readFileSync(path.join(REPO, 'tools', 'screenshots', 'vietati.local.txt'), 'utf8').split('\n')) {
      const t = l.trim();
      if (t && !t.startsWith('#')) out.push(new RegExp(t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i'));
    }
  } catch {}
  return out;
}
const FORBIDDEN = forbidden();

async function web() {
  const { chromium } = playwright();
  const port = 8791;
  const mock = spawn(process.execPath, [path.join(REPO, 'tools', 'webui-dev', 'mock-server.mjs'), '--port=' + port, '--displays=1',
    '--no-backups', '--state=' + path.join(HERE, 'demo-state.json'), '--host=PC-di-casa'], { stdio: ['ignore', 'pipe', 'inherit'] });
  await new Promise((ok, ko) => { mock.stdout.on('data', (d) => { if (String(d).includes('webui mock su')) ok(); }); mock.on('exit', ko); });
  const base = `http://127.0.0.1:${port}/`;
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 1000 }, deviceScaleFactor: 1, colorScheme: 'light' });
  const page = await ctx.newPage();
  // clip: zona della pagina intera (coordinate del documento); senza, la finestra 1280×1000
  const shot = async (name, clip) => {
    const text = await page.evaluate(() => document.body.innerText + ' ' + [...document.querySelectorAll('input')].map((i) => i.value).join(' '));
    for (const re of FORBIDDEN) if (re.test(text)) throw new Error(name + ': testo vietato ' + re);
    await page.waitForTimeout(300);
    const file = path.join(OUT, 'web-' + name + '.png');
    await page.screenshot(clip ? { path: file, fullPage: true, clip } : { path: file });
    console.log('  ' + path.relative(REPO, file));
  };
  try {
    await page.goto(base);
    await page.waitForSelector('#sessions .session');
    await page.waitForFunction(() => document.getElementById('conn').dataset.mode === 'sse');
    // dall'inizio fino in fondo alle sessioni
    const bottom = await page.evaluate(() => Math.ceil(document.getElementById('sessions').getBoundingClientRect().bottom + scrollY));
    await shot('panoramica', { x: 0, y: 0, width: 1280, height: Math.min(bottom + 32, 1800) });
    await page.goto(base + 'device.html?board=calamaio-3f2a');
    await page.waitForSelector('.set-item');
    // le schede Display e Avvisi (le stesse voci dei menu del display)
    const top = await page.evaluate(() => {
      const h = [...document.querySelectorAll('h3')].find((x) => x.textContent.trim() === 'Display');
      return Math.floor(h.getBoundingClientRect().top + scrollY) - 64;
    });
    await shot('impostazioni', { x: 0, y: top, width: 1280, height: 860 });
    await page.goto(base + 'editor.html?theme=prova');
    await page.waitForSelector('#ed-screen[data-drawn]');
    await shot('editor');
  } finally {
    await browser.close();
    mock.kill();
  }
}

if (only !== 'web') device();
if (only !== 'device') await web();
