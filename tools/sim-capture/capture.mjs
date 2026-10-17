#!/usr/bin/env node
// Cattura deterministica delle schermate del simulatore Calamaio OS per la Heltec
// Wireless Paper (250x122, 1 bit). Uso: node capture.mjs [--out DIR] [--only a,b] [--quiet]
//
// Il riferimento e l'uscita di Screen.compose(m, f, ctl): la matrice 0/1 (1 = inchiostro)
// prima della resa con i colori carta/inchiostro e le scie del ghosting.
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile, rm } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { chromium } from 'playwright';
import { encodePng1, encodePbm } from './lib/image.mjs';
import { SCENARIOS } from './scenarios.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..', '..');
const SIM_DIR = path.join(REPO, 'reference', 'calamaio-os-browser');
const PAGE = 'test-heltec-wireless-paper.html';
const SIM_SOURCES = [PAGE, 'assets/calamaio-test.js', 'assets/avatars.js', 'assets/dc-lite.js', 'assets/fonts/fonts.css'];
const DEVICE = 'heltec213';
const W = 250, H = 122;

// Istante congelato: 11 ottobre 2026, 10:30:00 ora di Roma (CEST, UTC+2).
const FROZEN_LOCAL = '2026-10-11T10:30:00';
const TIMEZONE = 'Europe/Rome';
const FROZEN_EPOCH = Date.parse('2026-10-11T10:30:00+02:00');
const RANDOM_SEED = 20261011;

// Tempi dei gesti (il simulatore: lunga >= 700 ms, tenuto >= 3000 ms, conteggio chiuso dopo 380 ms).
const PRESS_MS = 60, GAP_MS = 120, COUNT_WINDOW_MS = 400, LONG_MS = 800, VLONG_MS = 3100;

const FRAMES = [0, 1];
const LAUNCH_ARGS = ['--font-render-hinting=none', '--disable-lcd-text', '--force-color-profile=srgb'];

function parseArgs(argv) {
  const o = { out: path.join(HERE, 'out', DEVICE), only: null, quiet: false };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--out') o.out = path.resolve(argv[++i]);
    else if (a === '--only') o.only = argv[++i].split(',').map((s) => s.trim()).filter(Boolean);
    else if (a === '--quiet') o.quiet = true;
    else if (a === '-h' || a === '--help') { console.log('node capture.mjs [--out DIR] [--only nome1,nome2] [--quiet]'); process.exit(0); }
    else throw new Error('argomento sconosciuto: ' + a);
  }
  return o;
}

const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.woff2': 'font/woff2', '.pdf': 'application/pdf', '.png': 'image/png', '.svg': 'image/svg+xml' };

// Server statico in sola lettura sulla cartella del simulatore (evita i limiti di file://).
function startServer() {
  const srv = createServer(async (req, res) => {
    try {
      const u = decodeURIComponent(new URL(req.url, 'http://x').pathname);
      const p = path.normalize(path.join(SIM_DIR, u === '/' ? '/' + PAGE : u));
      if (!p.startsWith(SIM_DIR + path.sep)) { res.writeHead(403); res.end(); return; }
      const data = await readFile(p);
      res.writeHead(200, { 'content-type': MIME[path.extname(p)] || 'application/octet-stream', 'cache-control': 'no-store' });
      res.end(data);
    } catch (e) {
      res.writeHead(404); res.end();
    }
  });
  return new Promise((ok) => srv.listen(0, '127.0.0.1', () => ok(srv)));
}

// Script iniettato prima del caricamento: Math.random con seme fisso e registro dei testi
// disegnati sul canvas di composizione (per scoprire «Errore di disegno» e per il manifest).
function initScript(seed) {
  let a = seed >>> 0;
  Math.random = function () {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  const orig = CanvasRenderingContext2D.prototype.fillText;
  window.__capText = null;
  CanvasRenderingContext2D.prototype.fillText = function (s, x, y) {
    if (window.__capText && this === window.__capCtx) window.__capText.push({ s: String(s), x: Math.round(x), y: Math.round(y), font: this.font, align: this.textAlign });
    return orig.apply(this, arguments);
  };
}

async function runScenario(browser, base, sc) {
  const context = await browser.newContext({ timezoneId: TIMEZONE, locale: 'it-IT', viewport: { width: 1400, height: 900 }, deviceScaleFactor: 1 });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e && e.message || e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  // Orologio finto: installato un secondo prima e fermato sull'istante congelato.
  await page.clock.install({ time: FROZEN_EPOCH - 1000 });
  await page.clock.pauseAt(FROZEN_EPOCH);
  await page.addInitScript(initScript, RANDOM_SEED);
  await page.goto(base + '/' + PAGE, { waitUntil: 'load' });

  // Font: tutte le facce usate dal disegno, poi document.fonts.ready.
  const fontsOk = await page.evaluate(async () => {
    const faces = ['500 16px "Chakra Petch"', '600 16px "Chakra Petch"', '700 16px "Chakra Petch"', '400 8px "Silkscreen"', '700 8px "Silkscreen"'];
    await Promise.all(faces.map((f) => document.fonts.load(f)));
    await document.fonts.ready;
    return faces.map((f) => [f, document.fonts.check(f)]);
  });
  const missing = fontsOk.filter((x) => !x[1]).map((x) => x[0]);
  if (missing.length) throw new Error(sc.name + ': font non caricati: ' + missing.join(', '));

  const setup = await page.evaluate(() => {
    const inst = window.DC && window.DC.instances[0];
    if (!inst || !inst.scr || !inst.m) return { ok: false };
    // Niente eventi casuali ne derive: sessioni automatiche spente, slowTick fermo.
    inst.auto = false;
    clearInterval(inst.simIv);
    clearInterval(inst.slowIv);
    return { ok: true, device: inst.dev.id, W: inst.dev.W, H: inst.dev.H, t0: Date.now(), avatars: inst.eng().AV_ORDER.length };
  });
  if (!setup.ok) throw new Error(sc.name + ': componente DC.instances[0] non pronto');
  if (setup.device !== DEVICE || setup.W !== W || setup.H !== H) throw new Error(sc.name + ': dispositivo inatteso ' + JSON.stringify(setup));
  if (setup.avatars < 4) throw new Error(sc.name + ': libreria degli avatar non caricata (assets/avatars.js): ' + setup.avatars + ' avatar');
  if (setup.t0 !== FROZEN_EPOCH) throw new Error(sc.name + ': orologio non congelato (' + setup.t0 + ' invece di ' + FROZEN_EPOCH + ')');

  const key = (fn) => page.evaluate((f) => { const i = window.DC.instances[0]; if (f === 'down') i.keyDown('single'); else i.keyUp('single'); }, fn);
  const advance = (ms) => page.clock.runFor(ms);
  const press = async (hold) => { await key('down'); await advance(hold); await key('up'); };

  for (const st of sc.steps) {
    const mx = /^(\d)x$/.exec(st);
    if (mx) {
      const n = +mx[1];
      for (let k = 0; k < n; k++) { if (k) await advance(GAP_MS); await press(PRESS_MS); }
      await advance(COUNT_WINDOW_MS);
    } else if (st === 'long') { await press(LONG_MS); await advance(GAP_MS); }
    else if (st === 'vlong') { await press(VLONG_MS); await advance(GAP_MS); }
    else if (st === 'down') await key('down');  // tasto giù e resta giù (si cattura con il tasto premuto)
    else if (st === 'up') await key('up');
    else if (st.startsWith('wait:')) await advance(parseInt(st.slice(5), 10));
    else if (st.startsWith('sim:')) {
      const id = st.slice(4);
      const ok = await page.evaluate((k) => { const i = window.DC.instances[0]; if (!i.simClicks[k]) return false; i.simClicks[k](); return true; }, id);
      if (!ok) throw new Error(sc.name + ': pulsante Simula sconosciuto ' + id);
      await advance(GAP_MS);
    } else throw new Error(sc.name + ': passo sconosciuto ' + st);
  }

  const res = await page.evaluate((frames) => {
    const inst = window.DC.instances[0], E = inst.eng(), m = inst.m, tp = E.top(m), now = Date.now();
    const out = [];
    for (const f of frames) {
      window.__capCtx = inst.scr.sc; window.__capText = [];
      // f è il contatore dei fotogrammi; a schermo va frameFor(f) (Heltec: f % 2, 0 con il risparmio energetico).
      const fd = E.frameFor(inst.dev, m, f, now);
      const t = inst.scr.compose(m, fd, inst.ctl);
      const texts = window.__capText; window.__capText = null;
      let bits = '', gray = 0;
      for (let i = 0; i < t.length; i++) { if (t[i] !== 0 && t[i] !== 1) gray++; bits += t[i] >= 0.5 ? '1' : '0'; }
      out.push({ f, fd, bits, gray, texts });
    }
    const d = new Date(now), p2 = (n) => String(n).padStart(2, '0');
    const sel = m.sessions[m.ui.sel];
    return {
      frames: out,
      captureLocal: d.getFullYear() + '-' + p2(d.getMonth() + 1) + '-' + p2(d.getDate()) + 'T' + p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds()) + '.' + String(d.getMilliseconds()).padStart(3, '0'),
      now,
      model: {
        screen: tp.id,
        homeStyle: m.set.homeStyle,
        stack: JSON.parse(JSON.stringify(m.ui.stack)),
        uiSel: m.ui.sel,
        uiSelName: sel ? sel.name : null,
        selPinned: m.ui.selUntil > now,
        shownSession: (() => { const i = m.set.homeStyle === 'una' ? E.unaIdx(m) : E.sIdx(m), s = m.sessions[i]; return s ? { index: i, name: s.name, state: s.state } : null; })(),
        sessions: m.sessions.map((s) => ({ name: s.name, state: s.state, done: s.done, total: s.total, detail: s.detail, avatar: s.avatar || '' })),
        defaultAvatar: m.set.defaultAvatar,
        avatars: E.AV_ORDER.slice(),
        toast: m.toast.until > now ? m.toast.msg : '',
        muted: m.set.mute > now,
        h5: m.limits.h5,
        acc: m.acc, co2: m.air.co2,
        sys: { wifi: m.sys.wifi, pc: m.sys.pc, pcOff: m.sys.pcOff, batt: m.sys.batt, rssi: m.sys.rssi }
      },
      lastInput: inst.lastInput, lastAction: inst.lastAction
    };
  }, FRAMES);
  await context.close();
  return { res, errors, t0: setup.t0 };
}

function sha256(buf) { return createHash('sha256').update(buf).digest('hex'); }

async function main() {
  const opt = parseArgs(process.argv);
  const list = opt.only ? SCENARIOS.filter((s) => opt.only.includes(s.name)) : SCENARIOS;
  if (opt.only && list.length !== opt.only.length) throw new Error('scenari sconosciuti in --only');
  const srv = await startServer();
  const base = 'http://127.0.0.1:' + srv.address().port;
  const browser = await chromium.launch({ headless: true, args: LAUNCH_ARGS });
  const browserVersion = browser.version();
  const pwVersion = JSON.parse(await readFile(path.join(HERE, 'node_modules', 'playwright', 'package.json'), 'utf8')).version;
  const srcHashes = {};
  for (const f of SIM_SOURCES) srcHashes['reference/calamaio-os-browser/' + f] = sha256(await readFile(path.join(SIM_DIR, f)));

  if (!opt.only) await rm(opt.out, { recursive: true, force: true });
  await mkdir(opt.out, { recursive: true });
  const captures = [], problems = [];
  try {
    for (const sc of list) {
      const { res, errors } = await runScenario(browser, base, sc);
      for (const fr of res.frames) {
        const bits = Uint8Array.from(fr.bits, (ch) => ch === '1' ? 1 : 0);
        const ink = bits.reduce((a, b) => a + b, 0);
        const base_ = `${sc.name}-f${fr.f}`;
        const png = encodePng1(bits, W, H), pbm = encodePbm(bits, W, H);
        await writeFile(path.join(opt.out, base_ + '.png'), png);
        await writeFile(path.join(opt.out, base_ + '.pbm'), pbm);
        const drawErr = fr.texts.some((t) => t.s === 'Errore di disegno');
        if (drawErr) problems.push(`${base_}: Errore di disegno (${(fr.texts[1] || {}).s || '?'})`);
        if (ink === 0 || ink === W * H) problems.push(`${base_}: schermata uniforme (${ink} pixel di inchiostro)`);
        if (fr.gray) problems.push(`${base_}: ${fr.gray} pixel non binari`);
        captures.push({
          name: sc.name,
          frame: fr.f,
          drawnFrame: fr.fd,
          png: path.posix.join(DEVICE, base_ + '.png'),
          pbm: path.posix.join(DEVICE, base_ + '.pbm'),
          pbmSha256: sha256(pbm),
          inkPixels: ink,
          steps: sc.steps,
          directModelManipulation: false,
          note: sc.note,
          captureLocalTime: res.captureLocal,
          elapsedMs: res.now - FROZEN_EPOCH,
          model: res.model,
          lastInput: res.lastInput,
          lastAction: res.lastAction,
          texts: fr.texts
        });
      }
      for (const e of errors) problems.push(`${sc.name}: ${e}`);
      if (!opt.quiet) console.log(`ok  ${sc.name.padEnd(28)} schermo=${res.model.screen.padEnd(8)} stile=${res.model.homeStyle.padEnd(9)} t=${res.captureLocal}  «${res.lastAction}»`);
    }
  } finally {
    await browser.close();
    srv.close();
  }

  const manifest = {
    tool: 'tools/sim-capture/capture.mjs',
    device: { id: DEVICE, name: 'Heltec Wireless Paper', W, H, cls: 'S', input: 'single (PRG)' },
    source: 'Screen.compose(m, f, ctl) del simulatore: matrice 0/1 prima di colori carta/inchiostro e ghosting',
    simulatorSha256: srcHashes,
    frozenInstant: { local: FROZEN_LOCAL, timezone: TIMEZONE, epochMs: FROZEN_EPOCH, iso: new Date(FROZEN_EPOCH).toISOString() },
    clock: 'Playwright page.clock: installato e fermato su frozenInstant prima del caricamento; avanza solo con runFor (timer compresi)',
    randomSeed: RANDOM_SEED,
    randomGenerator: 'mulberry32 al posto di Math.random',
    environment: [
      'sessioni automatiche spente (inst.auto = false, clearInterval(simIv)): nessun evento casuale',
      'slowTick fermo (clearInterval(slowIv)): batteria 78%, RSSI -58 dBm e temperatura restano ai valori di newModel',
      'nessuna manipolazione diretta del modello: solo gesti del tasto PRG e pulsanti Simula'
    ],
    gestureTiming: { pressMs: PRESS_MS, gapMs: GAP_MS, countWindowMs: COUNT_WINDOW_MS, longMs: LONG_MS, vlongMs: VLONG_MS },
    browser: { engine: 'chromium headless (Playwright ' + pwVersion + ')', version: browserVersion, launchArgs: LAUNCH_ARGS, platform: process.platform + '-' + process.arch },
    formats: { png: 'scala di grigi 1 bit, nero = inchiostro', pbm: 'P4, 1 = inchiostro' },
    frames: FRAMES,
    captures
  };
  // --only: le catture degli altri scenari restano quelle del manifest già scritto (nell'ordine di SCENARIOS).
  if (opt.only) {
    let prev = null;
    try { prev = JSON.parse(await readFile(path.join(opt.out, 'manifest.json'), 'utf8')); } catch (e) { prev = null; }
    if (prev && Array.isArray(prev.captures)) {
      const keep = prev.captures.filter((c) => !opt.only.includes(c.name));
      const order = new Map(SCENARIOS.map((s, i) => [s.name, i]));
      manifest.captures = keep.concat(captures).sort((a, b) => (order.get(a.name) ?? 1e9) - (order.get(b.name) ?? 1e9) || a.frame - b.frame);
      manifest.partial = 'ultima cattura con --only ' + opt.only.join(',') + ': gli hash del simulatore valgono per questi scenari';
    }
  }
  await writeFile(path.join(opt.out, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  if (!opt.quiet) console.log(`\n${captures.length} immagini in ${opt.out}`);
  if (problems.length) { console.error('PROBLEMI:\n' + problems.map((p) => '  - ' + p).join('\n')); process.exit(1); }
}

main().catch((e) => { console.error(e && e.stack || e); process.exit(2); });
