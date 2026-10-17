// Prova dell'editor nel browser contro il server finto: Chromium a 1280 e WebKit a 393×852 col tocco.
// Ogni richiesta della pagina deve stare nell'elenco della porta dei telefoni (phone-routes.json).
//   node smoke-editor.mjs        (porte proprie 8780–8783, F11: shots.mjs usa 8790–8799)
import { chromium, webkit } from 'playwright';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const PORT = 8780;
const problems = [];
const children = [];
process.on('exit', () => children.forEach((c) => c.kill()));

function startMock(port, extra = []) {
  return new Promise((resolve, reject) => {
    const p = spawn(process.execPath, [path.join(here, 'mock-server.mjs'), '--port=' + port, ...extra], { stdio: ['ignore', 'pipe', 'inherit'] });
    children.push(p);
    p.stdout.on('data', (d) => { if (String(d).includes('webui mock su')) resolve(p); });
    p.on('exit', (c) => reject(new Error('mock uscito ' + c)));
  });
}

// modelli di Go (net/http): {x} = un segmento, / finale = prefisso, {$} = esatto; GET vale anche per HEAD
const ROUTES = JSON.parse(fs.readFileSync(path.join(here, 'phone-routes.json'), 'utf8')).routes.map((r) => {
  const [method, pat] = r.split(' ');
  const exact = pat.endsWith('{$}');
  const body = exact ? pat.slice(0, -3) : pat;
  const prefix = !exact && body.endsWith('/');
  const re = body.split('/').map((seg) => (/^\{[^}]+\}$/.test(seg) ? '[^/]+' : seg.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))).join('/');
  return { r, method, re: new RegExp('^' + re + (prefix ? '' : '$')) };
});
export function phoneAllowed(method, pathname) {
  return ROUTES.some((x) => (x.method === method || (x.method === 'GET' && method === 'HEAD')) && x.re.test(pathname));
}

// allow: errori di console attesi (per esempio il 409 voluto dal server finto)
function watch(page, tag, origin, allow = null) {
  page.on('pageerror', (e) => problems.push(tag + ' pageerror: ' + e.message));
  page.on('console', (m) => { if ((m.type() === 'error' || m.type() === 'warning') && !(allow && allow.test(m.text()))) problems.push(tag + ' console.' + m.type() + ': ' + m.text()); });
  page.on('request', (req) => {
    const u = new URL(req.url());
    if (u.protocol === 'data:' || u.protocol === 'blob:') return;
    if (u.origin !== origin) { problems.push(tag + ' richiesta fuori origine: ' + req.method() + ' ' + req.url()); return; }
    if (!phoneAllowed(req.method(), u.pathname)) problems.push(tag + ' richiesta fuori dall\'elenco dei telefoni: ' + req.method() + ' ' + u.pathname);
  });
}

const INK = [0x1e, 0x20, 0x22];
async function inkPixels(page) {
  return page.evaluate((ink) => {
    // copia in un canvas nuovo: una sola lettura per canvas (niente avviso di Chromium sulle letture ripetute)
    const src = document.getElementById('ed-screen'), c = document.createElement('canvas');
    c.width = src.width; c.height = src.height;
    const g = c.getContext('2d', { willReadFrequently: true });
    g.drawImage(src, 0, 0);
    const d = g.getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] === ink[0] && d[i + 1] === ink[1] && d[i + 2] === ink[2]) n++;
    return n;
  }, INK);
}

async function common(page, tag) {
  await page.waitForSelector('#ed-screen[data-drawn]', { timeout: 10000 });
  if (!((await inkPixels(page)) > 0)) problems.push(tag + ': nessun pixel d\'inchiostro sulla tela');
  const opt = await page.$('#ed-screens option[value="perm"]');
  if (!opt) problems.push(tag + ': manca Permessi nel selettore delle schermate');
  else {
    if ((await opt.textContent()) !== 'Permessi — bloccata (regole di sicurezza)') problems.push(tag + ': testo di Permessi: ' + (await opt.textContent()));
    if (!(await opt.evaluate((o) => o.disabled))) problems.push(tag + ': Permessi non è disattivata');
  }
  const theme = await page.$eval('#ed-themes', (s) => s.value).catch(() => '');
  if (theme !== 'prova') problems.push(tag + ': tema aperto ' + JSON.stringify(theme));
}

// ---------- tela interattiva (Task 10): stato da window.__editor (solo con ?test=1) ----------
// elemento della variante aperta per tipo (il primo); coordinate della scheda → px del client con la scala vera della tela
const elOf = (page, type, n = 0) => page.evaluate(([type, n]) => {
  const s = window.__editor.store.get(), v = s.hist.present.screens[s.loc.screen].variants[s.loc.variant];
  const e = v.elements.filter((x) => x.type === type)[n];
  return e ? { id: e.id, x: e.x, y: e.y, w: e.w, h: e.h, scale: e.scale } : null;
}, [type, n]);
const stateOf = (page) => page.evaluate(() => { const s = window.__editor.store.get(); return { sel: s.sel, zoom: s.zoom, steps: s.hist.past.length, notice: s.notice, multi: s.touchMulti }; });
const toClient = (page, bx, by) => page.evaluate(([bx, by]) => {
  const r = document.getElementById('ed-screen').getBoundingClientRect(), css = r.width / 250;
  return [r.left + bx * css, r.top + by * css];
}, [bx, by]);
const noSnap = (page) => page.evaluate(() => window.__editor.store.set({ snap: { grid: 1, others: false } }));
// Pointer Events inviati a mano (tocco o mouse): [{ id, x, y }] per passo
const touchSeq = (page, steps, kind = 'touch') => page.evaluate(([steps, kind]) => {
  const vp = document.getElementById('ed-viewport');
  for (const [type, pts] of steps) for (const p of pts) {
    const target = document.elementFromPoint(p.x, p.y) || vp;
    target.dispatchEvent(new PointerEvent(type, { pointerId: p.id, pointerType: kind, isPrimary: p.primary ?? p.id === 1, clientX: p.x, clientY: p.y,
      bubbles: true, cancelable: true, buttons: type === 'pointerup' ? 0 : 1, button: type === 'pointermove' ? -1 : 0 }));
  }
}, [steps, kind]);
const drag = (from, to, n = 5, id = 1) => [['pointerdown', [{ id, x: from[0], y: from[1] }]],
  ...Array.from({ length: n }, (_, i) => ['pointermove', [{ id, x: from[0] + (to[0] - from[0]) * (i + 1) / n, y: from[1] + (to[1] - from[1]) * (i + 1) / n }]]),
  ['pointerup', [{ id, x: to[0], y: to[1] }]]];
const expect = (tag, ok, msg) => { if (!ok) problems.push(tag + ': ' + msg); };

async function canvasMouse(br, origin) {
  const tag = 'chromium-1280-tela';
  const ctx = await br.newContext({ viewport: { width: 1280, height: 900 } });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  await noSnap(page);
  const css = await page.evaluate(() => document.getElementById('ed-screen').getBoundingClientRect().width / 250);
  const bar = await elOf(page, 'bar');
  // clic su un elemento
  await page.mouse.click(...(await toClient(page, bar.x + 10, bar.y + 4)));
  expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify([bar.id]), 'clic sulla barra: selezione ' + JSON.stringify((await stateOf(page)).sel));
  // trascinamento di 20 px: un passo, ⌘Z/Ctrl+Z torna indietro
  const st0 = await stateOf(page);
  const [sx, sy] = await toClient(page, bar.x + 10, bar.y + 4);
  await page.mouse.move(sx, sy); await page.mouse.down();
  for (let i = 1; i <= 4; i++) await page.mouse.move(sx + 5 * i, sy);
  await page.mouse.up();
  let b1 = await elOf(page, 'bar');
  expect(tag, b1.x === bar.x + Math.round(20 / css) && b1.y === bar.y, `trascinamento di 20 px: x ${bar.x} → ${b1.x} (atteso ${bar.x + Math.round(20 / css)})`);
  expect(tag, (await stateOf(page)).steps === st0.steps + 1, 'trascinamento: un passo solo');
  await page.keyboard.press('ControlOrMeta+z');
  b1 = await elOf(page, 'bar');
  expect(tag, b1.x === bar.x, '⌘Z/Ctrl+Z: x ' + b1.x);
  // frecce: 1 e 8
  await page.keyboard.press('ArrowRight');
  expect(tag, (await elOf(page, 'bar')).x === bar.x + 1, 'freccia: +1');
  await page.keyboard.press('Shift+ArrowRight');
  expect(tag, (await elOf(page, 'bar')).x === bar.x + 9, 'Maiusc+freccia: +8');
  await page.keyboard.press('Shift+ArrowLeft'); await page.keyboard.press('ArrowLeft');
  expect(tag, (await elOf(page, 'bar')).x === bar.x, 'frecce indietro');
  // le frecce in un selettore restano al selettore
  await page.focus('#ed-variant'); const sv = (await stateOf(page)).steps; await page.keyboard.press('ArrowRight');
  expect(tag, (await stateOf(page)).steps === sv && (await elOf(page, 'bar')).x === bar.x, 'freccia nel selettore: la tela non si muove');
  // maniglia est: +10 px CSS
  await page.mouse.click(...(await toClient(page, bar.x + 10, bar.y + 4)));
  const [hx, hy] = await toClient(page, bar.x + bar.w, bar.y + bar.h / 2);
  await page.mouse.move(hx, hy); await page.mouse.down(); await page.mouse.move(hx + 5, hy); await page.mouse.move(hx + 10, hy); await page.mouse.up();
  const b2 = await elOf(page, 'bar');
  expect(tag, b2.w === bar.w + Math.round(10 / css) && b2.x === bar.x, `maniglia: w ${bar.w} → ${b2.w}`);
  // riquadro sul vuoto attorno a batteria e simbolo
  const bat = await elOf(page, 'battery'), sym = await elOf(page, 'sym');
  const [m0x, m0y] = await toClient(page, 228, 105), [m1x, m1y] = await toClient(page, 249.5, 121.5);
  await page.mouse.move(m0x, m0y); await page.mouse.down(); await page.mouse.move((m0x + m1x) / 2, (m0y + m1y) / 2); await page.mouse.move(m1x, m1y); await page.mouse.up();
  const ms = (await stateOf(page)).sel.slice().sort();
  expect(tag, JSON.stringify(ms) === JSON.stringify([bat.id, sym.id].sort()), 'riquadro: ' + JSON.stringify(ms));
  await page.keyboard.press('Escape');
  expect(tag, (await stateOf(page)).sel.length === 0, 'Esc deseleziona');
  // R1-I1: un pointerup perso non blocca i gesti dopo; blur annulla senza mezzi passi
  {
    const b0 = await elOf(page, 'bar'), n0 = (await stateOf(page)).steps;
    const p0 = await toClient(page, b0.x + 10, b0.y + 4);
    await touchSeq(page, [['pointerdown', [{ id: 77, x: p0[0], y: p0[1], primary: true }]], ['pointermove', [{ id: 77, x: p0[0] + 30, y: p0[1] }]]], 'mouse');
    expect(tag, (await elOf(page, 'bar')).x !== b0.x, 'gesto sintetico senza pointerup: la barra si sposta');
    await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    expect(tag, (await elOf(page, 'bar')).x === b0.x && (await stateOf(page)).steps === n0, 'blur: gesto annullato, nessun passo');
    await touchSeq(page, [['pointerdown', [{ id: 78, x: p0[0], y: p0[1], primary: true }]], ['pointermove', [{ id: 78, x: p0[0] + 30, y: p0[1] }]]], 'mouse');
    // pointerup mai arrivato: il trascinamento vero dopo funziona e il vecchio non lascia traccia
    await page.mouse.move(p0[0], p0[1]); await page.mouse.down();
    for (let i = 1; i <= 4; i++) await page.mouse.move(p0[0] + 5 * i, p0[1]);
    await page.mouse.up();
    const b3 = await elOf(page, 'bar');
    expect(tag, b3.x === b0.x + Math.round(20 / css), `dopo un pointerup perso: x ${b0.x} → ${b3.x} (atteso ${b0.x + Math.round(20 / css)})`);
    expect(tag, (await stateOf(page)).steps === n0 + 1, 'dopo un pointerup perso: un passo solo');
    await page.mouse.click(...(await toClient(page, 236, 113)));
    const bat2 = await elOf(page, 'battery');
    expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify([bat2.id]), 'i clic dopo funzionano');
  }
  await ctx.close();
}

// dpr 1.5, zoom 1: scala vera 2/1.5 = 1.33 px CSS per punto (dividere per lo zoom sbaglierebbe di un terzo)
async function canvasDpr(br, origin) {
  const tag = 'chromium-dpr1.5-tela';
  const ctx = await br.newContext({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 1.5 });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  await page.selectOption('#ed-zoom', '1');
  await page.waitForFunction(() => document.getElementById('ed-screen').width === 250 * 2);
  await noSnap(page);
  const css = await page.evaluate(() => document.getElementById('ed-screen').getBoundingClientRect().width / 250);
  expect(tag, Math.abs(css - 4 / 3) < 1e-3, 'scala CSS ' + css);
  const bat = await elOf(page, 'battery');
  await page.mouse.click(...(await toClient(page, bat.x + 3, bat.y + 4)));
  expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify([bat.id]), 'clic sulla batteria a destra: ' + JSON.stringify((await stateOf(page)).sel));
  const [sx, sy] = await toClient(page, bat.x + 3, bat.y + 4);
  await page.mouse.move(sx, sy); await page.mouse.down(); await page.mouse.move(sx - 20, sy); await page.mouse.move(sx - 40, sy); await page.mouse.up();
  const b1 = await elOf(page, 'battery');
  expect(tag, b1.x === bat.x - 30, `40 px CSS = 30 punti: x ${bat.x} → ${b1.x}`);
  await page.keyboard.press('ControlOrMeta+z');
  expect(tag, (await elOf(page, 'battery')).x === bat.x, 'annullato');
  // maniglia della batteria (a scala): diventa scala intera 2
  const [hx, hy] = await toClient(page, bat.x + 16, bat.y + 8);
  await page.mouse.move(hx, hy); await page.mouse.down(); await page.mouse.move(hx + 10, hy + 5); await page.mouse.move(hx + 20, hy + 10); await page.mouse.up();
  const b2 = await elOf(page, 'battery');
  expect(tag, b2.scale === 2 && b2.w === undefined && b2.x === bat.x, 'maniglia a scala: ' + JSON.stringify(b2));
  await ctx.close();
}

async function canvasTouch(br, origin) {
  const tag = 'webkit-393-tela';
  const ctx = await br.newContext({ viewport: { width: 393, height: 852 }, hasTouch: true, isMobile: true, deviceScaleFactor: 3 });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  await noSnap(page);
  for (const id of ['ed-zoom-out', 'ed-zoom-in', 'ed-multi', 'ed-step']) {
    const r = await page.$eval('#' + id, (e) => { const b = e.getBoundingClientRect(); return [b.width, b.height]; });
    expect(tag, r[0] >= 44 && r[1] >= 44, '#' + id + ' sotto 44×44: ' + r.join('×'));
  }
  expect(tag, await page.isVisible('#ed-multi'), '«Seleziona più» non visibile col tocco');
  const bar = await elOf(page, 'bar');
  // tocco: seleziona
  await page.touchscreen.tap(...(await toClient(page, bar.x + 20, bar.y + 4)));
  expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify([bar.id]), 'tocco sulla barra: ' + JSON.stringify((await stateOf(page)).sel));
  // un dito sull'elemento lo sposta, anche toccato al centro di una barra alta 8 (R1-I2: niente maniglia n/s)
  const from = await toClient(page, bar.x + bar.w / 2, bar.y + bar.h / 2);
  await touchSeq(page, drag(from, [from[0] + 20, from[1] + 10]));
  const b1 = await elOf(page, 'bar');
  const css = await page.evaluate(() => document.getElementById('ed-screen').getBoundingClientRect().width / 250);
  expect(tag, b1.x === bar.x + Math.round(20 / css) && b1.y === bar.y + Math.round(10 / css), `un dito sposta la barra: ${bar.x},${bar.y} → ${b1.x},${b1.y}`);
  // ±1 e ±8
  await page.tap('.ed-nudge [data-dx="1"]');
  expect(tag, (await elOf(page, 'bar')).x === b1.x + 1, '→ di 1');
  await page.tap('#ed-step');
  expect(tag, (await page.getAttribute('#ed-step', 'aria-pressed')) === 'true', '±8 premuto');
  await page.tap('.ed-nudge [data-dy="1"]');
  expect(tag, (await elOf(page, 'bar')).y === b1.y + 8, '↓ di 8');
  await page.tap('#ed-step');
  // «Seleziona più»: il secondo tocco aggiunge
  await page.tap('#ed-multi');
  expect(tag, (await page.getAttribute('#ed-multi', 'aria-pressed')) === 'true', '«Seleziona più» premuto');
  const bat = await elOf(page, 'battery');
  await page.touchscreen.tap(...(await toClient(page, bat.x + 3, bat.y + 4)));
  expect(tag, (await stateOf(page)).sel.length === 2, 'Seleziona più: due scelti ' + JSON.stringify((await stateOf(page)).sel));
  await page.tap('#ed-multi');
  // un dito sul vuoto sposta la vista
  const r0 = await page.evaluate(() => { const r = document.getElementById('ed-screen').getBoundingClientRect(); return { left: r.left, top: r.top, w: r.width }; });
  const empty = [Math.max(4, r0.left - 30), r0.top + 40];
  const sel0 = (await stateOf(page)).sel;
  await touchSeq(page, drag(empty, [empty[0] + 40, empty[1] + 6]));
  const r1 = await page.evaluate(() => { const r = document.getElementById('ed-screen').getBoundingClientRect(); return { left: r.left, top: r.top }; });
  expect(tag, Math.abs(r1.left - r0.left - 40) < 0.6 && Math.abs(r1.top - r0.top - 6) < 0.6, `un dito sul vuoto: vista spostata di ${r1.left - r0.left},${r1.top - r0.top}`);
  expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify(sel0), 'il pan non cambia la selezione');
  // pizzico: le dita si allontanano del doppio → zoom 2; il punto fra le dita resta lì
  const z0 = (await stateOf(page)).zoom;
  const cx = r1.left + 125, cy = r1.top + 60;
  const before = await page.evaluate(([cx, cy]) => { const r = document.getElementById('ed-screen').getBoundingClientRect(), c = r.width / 250; return [(cx - r.left) / c, (cy - r.top) / c]; }, [cx, cy]);
  await touchSeq(page, [
    ['pointerdown', [{ id: 1, x: cx - 20, y: cy }]], ['pointerdown', [{ id: 2, x: cx + 20, y: cy }]],
    ['pointermove', [{ id: 1, x: cx - 30, y: cy }, { id: 2, x: cx + 30, y: cy }]],
    ['pointermove', [{ id: 1, x: cx - 40, y: cy }, { id: 2, x: cx + 40, y: cy }]],
    ['pointerup', [{ id: 2, x: cx + 40, y: cy }]], ['pointerup', [{ id: 1, x: cx - 40, y: cy }]],
  ]);
  await page.waitForFunction((z) => window.__editor.store.get().zoom === z, z0 * 2, { timeout: 3000 }).catch(() => {});
  const z1 = (await stateOf(page)).zoom;
  expect(tag, z1 === z0 * 2, `pizzico: zoom ${z0} → ${z1}`);
  const after = await page.evaluate(([cx, cy]) => { const r = document.getElementById('ed-screen').getBoundingClientRect(), c = r.width / 250; return [(cx - r.left) / c, (cy - r.top) / c, r.width]; }, [cx, cy]);
  expect(tag, Math.abs(after[0] - before[0]) < 0.6 && Math.abs(after[1] - before[1]) < 0.6, `pizzico: punto fra le dita ${before.map((v) => v.toFixed(1))} → ${after.slice(0, 2).map((v) => v.toFixed(1))}`);
  expect(tag, (await elOf(page, 'bar')).x === b1.x + 1, 'il pizzico non sposta gli elementi');
  // «+» e «−»
  await page.tap('#ed-zoom-out');
  expect(tag, (await stateOf(page)).zoom === z1 - 1, 'pulsante −');
  // il pan non porta la scheda fuori: almeno un quarto resta nel riquadro
  {
    const r = await page.evaluate(() => { const r = document.getElementById('ed-screen').getBoundingClientRect(); return [r.left, r.top]; });
    const e0 = [Math.max(4, r[0] - 30), r[1] + 40];
    await touchSeq(page, drag(e0, [e0[0] + 2000, e0[1] + 900], 4));
    const lim = await page.evaluate(() => { const r = document.getElementById('ed-screen').getBoundingClientRect(), v = document.getElementById('ed-viewport').getBoundingClientRect();
      return { right: v.right - r.left, bottom: v.bottom - r.top, w: r.width, h: r.height }; });
    expect(tag, lim.right >= lim.w / 4 - 0.6 && lim.bottom >= lim.h / 4 - 0.6, 'pan limitato: ' + JSON.stringify(lim));
  }
  const wide = await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth);
  expect(tag, wide, 'scorrimento orizzontale');
  await ctx.close();
}

// ---------- Aggiungi e Livelli (Task 11) ----------
const orderOf = (page) => page.evaluate(() => { const s = window.__editor.store.get(); return s.hist.present.screens[s.loc.screen].variants[s.loc.variant].elements.map((e) => e.id); });
const elById = (page, id) => page.evaluate((id) => { const s = window.__editor.store.get(); return s.hist.present.screens[s.loc.screen].variants[s.loc.variant].elements.find((e) => e.id === id) ?? null; }, id);
// impronta (FNV-1a) dei pixel della tela: cambia se cambia l'ordine di disegno
const screenHash = (page) => page.evaluate(() => {
  const src = document.getElementById('ed-screen'), c = document.createElement('canvas');
  c.width = src.width; c.height = src.height;
  const g = c.getContext('2d', { willReadFrequently: true }); g.drawImage(src, 0, 0);
  const d = g.getImageData(0, 0, c.width, c.height).data;
  let hsh = 2166136261;
  for (let i = 0; i < d.length; i++) { hsh ^= d[i]; hsh = Math.imul(hsh, 16777619) >>> 0; }
  return hsh;
});
const layerIds = (page) => page.$$eval('#ed-layer-list > li', (l) => l.map((x) => x.dataset.id));
const box = async (page, sel) => page.$eval(sel, (e) => { const r = e.getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height, cx: r.left + r.width / 2, cy: r.top + r.height / 2, bottom: r.bottom }; });

async function layersMouse(br, origin) {
  const tag = 'chromium-1280-livelli';
  const ctx = await br.newContext({ viewport: { width: 1280, height: 900 } });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  const n0 = (await orderOf(page)).length;
  expect(tag, (await page.$$('#ed-add .ed-add-btn')).length >= 12, 'tipi in «Aggiungi»: ' + (await page.$$('#ed-add .ed-add-btn')).length);
  expect(tag, (await layerIds(page)).length === n0, 'un livello per elemento');
  // Aggiungi › Rettangolo: in cima, selezionato; il rettangolo pieno al centro copre il resto
  const h0 = await screenHash(page);
  await page.click('#ed-add [data-type="rect"]');
  const ids1 = await orderOf(page), nid = ids1[ids1.length - 1];
  expect(tag, ids1.length === n0 + 1 && (await layerIds(page))[0] === nid, 'il nuovo livello è in cima');
  expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify([nid]), 'il nuovo elemento è selezionato');
  // pieno e in negativo: l'ordine di disegno rispetto al testo sotto si vede nei pixel
  await page.evaluate((id) => window.__editor.A.commit((t) => { const c = JSON.parse(JSON.stringify(t)); const s = window.__editor.store.get();
    const e = c.screens[s.loc.screen].variants[s.loc.variant].elements.find((x) => x.id === id); e.fill = true; e.invert = true; e.w = 120; e.h = 60; return c; }), nid);
  const h1 = await screenHash(page);
  expect(tag, h1 !== h0, 'il rettangolo cambia la tela');
  // trascinare la riga in fondo = disegnata per prima: la tela cambia
  const grip = await box(page, `#ed-layer-list [data-id="${nid}"] .ed-grip`), last = await box(page, '#ed-layer-list > li:last-child');
  await page.mouse.move(grip.cx, grip.cy); await page.mouse.down();
  await page.mouse.move(grip.cx, grip.cy + 20); await page.mouse.move(last.cx, last.bottom - 2); await page.mouse.move(last.cx, last.bottom + 8);
  expect(tag, (await page.getAttribute('#ed-layer-list', 'data-drop')) === 'end', 'durante il trascinamento: indicatore in fondo');
  await page.mouse.up();
  const ids2 = await orderOf(page);
  expect(tag, ids2[0] === nid && ids2.length === n0 + 1, 'ordine di disegno: ora il primo ' + JSON.stringify(ids2));
  expect(tag, (await layerIds(page)).slice(-1)[0] === nid, 'il livello è in fondo al pannello');
  expect(tag, (await screenHash(page)) !== h1, 'la tela cambia con l\'ordine di disegno');
  await page.keyboard.press('ControlOrMeta+z');
  expect(tag, (await orderOf(page))[ids2.length - 1] === nid, '⌘Z riporta l\'ordine');
  // Su / Giù
  await page.click(`#ed-layer-list [data-id="${nid}"] .ed-layer-name`);
  expect(tag, await page.isDisabled('#ed-layer-list .ed-layer[data-selected] .ed-su'), '«Su» spento in cima');
  await page.click('#ed-layer-list .ed-layer[data-selected] .ed-giu');
  const ids3 = await orderOf(page);
  expect(tag, ids3[ids3.length - 2] === nid, '«Giù» scende di un posto ' + JSON.stringify(ids3));
  expect(tag, await page.evaluate(() => document.activeElement.classList.contains('ed-giu') || document.activeElement.classList.contains('ed-layer-name')), 'il fuoco resta sulla riga');
  await page.click('#ed-layer-list .ed-layer[data-selected] .ed-su');
  expect(tag, (await orderOf(page)).slice(-1)[0] === nid, '«Su» risale');
  // nascondi, blocca
  await page.click(`#ed-layer-list [data-id="${nid}"] .ed-eye`);
  expect(tag, (await elById(page, nid)).hidden === true && (await page.getAttribute(`#ed-layer-list [data-id="${nid}"] .ed-eye`, 'aria-pressed')) === 'true', 'nascondi: aria-pressed');
  expect(tag, (await page.getAttribute(`#ed-layer-list [data-id="${nid}"] .ed-eye`, 'aria-label')) === 'Mostra', 'l\'occhio ora dice «Mostra»');
  await page.click(`#ed-layer-list [data-id="${nid}"] .ed-eye`);
  expect(tag, (await elById(page, nid)).hidden === undefined, 'mostra');
  await page.click(`#ed-layer-list [data-id="${nid}"] .ed-lock`);
  expect(tag, (await elById(page, nid)).locked === true && (await page.getAttribute(`#ed-layer-list [data-id="${nid}"] .ed-lock`, 'aria-pressed')) === 'true', 'blocca: aria-pressed');
  // un bloccato non si colpisce sulla tela; da «Livelli» sì, e non si sposta
  await page.keyboard.press('Escape');
  await page.click(`#ed-layer-list [data-id="${nid}"] .ed-layer-name`);
  expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify([nid]), 'Livelli sceglie il bloccato');
  const before = await elById(page, nid);
  await page.keyboard.press('ArrowRight');
  expect(tag, JSON.stringify(await elById(page, nid)) === JSON.stringify(before), 'bloccato: le frecce non lo spostano');
  expect(tag, /bloccato/.test((await stateOf(page)).notice), 'bloccato: il motivo è a schermo ' + (await stateOf(page)).notice);
  const cx = await toClient(page, before.x + before.w / 2, before.y + before.h / 2);
  await page.mouse.move(...cx); await page.mouse.down(); await page.mouse.move(cx[0] + 30, cx[1]); await page.mouse.up();
  expect(tag, JSON.stringify(await elById(page, nid)) === JSON.stringify(before), 'bloccato: il mouse sulla tela non lo sposta');
  await page.click(`#ed-layer-list [data-id="${nid}"] .ed-lock`);
  // rinomina: pulsante, Invio; Esc annulla; 24 byte
  await page.click(`#ed-layer-list [data-id="${nid}"] .ed-layer-name`);
  await page.click('#ed-layer-list .ed-layer[data-selected] .ed-rinomina');
  expect(tag, await page.evaluate(() => document.activeElement.classList.contains('ed-rename')), 'Rinomina: il campo ha il fuoco');
  await page.keyboard.press('ControlOrMeta+a'); await page.keyboard.type('Sfondo <b>x</b> ' + 'è'.repeat(20)); await page.keyboard.press('Enter');
  const nm = (await elById(page, nid)).name;
  expect(tag, typeof nm === 'string' && new TextEncoder().encode(nm).length <= 32 && nm.startsWith('Sfondo <b>'), 'rinomina: nome tagliato a 32 byte ' + JSON.stringify(nm));
  expect(tag, (await page.textContent(`#ed-layer-list [data-id="${nid}"] .ed-layer-text`)) === nm && !(await page.$('#ed-layer-list b')), 'il nome è testo, mai HTML');
  await page.dblclick(`#ed-layer-list [data-id="${nid}"] .ed-layer-name`);
  expect(tag, !!(await page.$('#ed-layer-list .ed-rename')), 'doppio clic: campo del nome');
  await page.keyboard.type('zzz'); await page.keyboard.press('Escape');
  expect(tag, (await elById(page, nid)).name === nm && !(await page.$('#ed-layer-list .ed-rename')), 'Esc annulla la rinomina');
  await page.dblclick(`#ed-layer-list [data-id="${nid}"] .ed-layer-name`);
  await page.keyboard.press('ControlOrMeta+a'); await page.keyboard.press('Backspace'); await page.keyboard.press('Enter');
  expect(tag, (await elById(page, nid)).name === undefined, 'nome vuoto: torna quello del tipo');
  // rinomina confermata senza cambiare: nessun passo
  { const n = (await stateOf(page)).steps;
    await page.dblclick(`#ed-layer-list [data-id="${nid}"] .ed-layer-name`); await page.keyboard.press('Enter');
    expect(tag, (await stateOf(page)).steps === n && (await elById(page, nid)).name === undefined, 'Invio senza modifiche: niente nome, niente passo'); }
  // rilascio nello spazio fra due righe: la riga più vicina, mai «fondo»
  { const ids = await layerIds(page), mover = ids[ids.length - 1], k = ids.length;
    await page.$eval('#ed-layer-list > li:last-child', (e) => e.scrollIntoView({ block: 'end' }));
    const r1 = await box(page, `#ed-layer-list > li:nth-child(${k - 3})`), r2 = await box(page, `#ed-layer-list > li:nth-child(${k - 2})`);
    const gy = (r1.bottom + r2.y) / 2, gp = await box(page, `#ed-layer-list [data-id="${mover}"] .ed-grip`);
    expect(tag, r2.y - r1.bottom > 0.5 && gp.cy < 900, 'c\'è uno spazio fra le righe, maniglia in vista: ' + (r2.y - r1.bottom));
    await page.mouse.move(gp.cx, gp.cy); await page.mouse.down(); await page.mouse.move(gp.cx, gp.cy - 20); await page.mouse.move(r1.cx, gy);
    expect(tag, (await page.getAttribute('#ed-layer-list', 'data-drop')) === null && (await page.$('#ed-layer-list [data-drop]')) !== null, 'nello spazio fra le righe: un indicatore sulla riga più vicina, non «fondo»');
    await page.mouse.up();
    const after = await layerIds(page);
    expect(tag, after.indexOf(mover) >= k - 4 && after.indexOf(mover) <= k - 2, 'rilascio nello spazio: vicino alle righe, non in fondo (' + after.indexOf(mover) + ' di ' + k + ')'); }
  // Maiusc+clic aggiunge
  const other = (await layerIds(page)).find((x) => x !== nid);
  await page.click(`#ed-layer-list [data-id="${other}"] .ed-layer-name`, { modifiers: ['Shift'] });
  expect(tag, (await stateOf(page)).sel.length === 2, 'Maiusc+clic aggiunge alla selezione');
  expect(tag, (await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)), 'scorrimento orizzontale');
  await ctx.close();
}

async function layersTouch(br, origin) {
  const tag = 'webkit-393-livelli';
  const ctx = await br.newContext({ viewport: { width: 393, height: 852 }, hasTouch: true, isMobile: true, deviceScaleFactor: 3 });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  const n0 = (await orderOf(page)).length;
  await page.tap('#ed-open-left');
  await page.waitForSelector('#ed-left[data-open]');
  for (const sel of ['#ed-add [data-type="text"]', '#ed-add [data-type="list"]', '#ed-layer-list .ed-grip', '#ed-layer-list .ed-eye', '#ed-layer-list .ed-lock', '#ed-layer-list .ed-layer-name']) {
    const b = await box(page, sel);
    expect(tag, b.w >= 44 && b.h >= 44, sel + ' sotto 44×44: ' + b.w.toFixed(1) + '×' + b.h.toFixed(1));
  }
  // Aggiungi › Testo: il cassetto si chiude, il livello compare
  await page.tap('#ed-add [data-type="text"]');
  await page.waitForSelector('#ed-left:not([data-open])');
  const ids1 = await orderOf(page), nid = ids1[ids1.length - 1];
  expect(tag, ids1.length === n0 + 1 && (await elById(page, nid)).type === 'text', 'aggiunto un testo');
  expect(tag, JSON.stringify((await stateOf(page)).sel) === JSON.stringify([nid]), 'il nuovo testo è selezionato');
  await page.tap('#ed-open-left');
  await page.waitForSelector('#ed-left[data-open]');
  expect(tag, (await layerIds(page))[0] === nid, 'il nuovo livello compare in «Livelli», in cima');
  expect(tag, (await box(page, '#ed-layer-list .ed-layer[data-selected] .ed-su')).h >= 44 && (await box(page, '#ed-layer-list .ed-layer[data-selected] .ed-giu')).h >= 44, '«Su»/«Giù» almeno 44 pt');
  // «Giù» col tocco
  await page.tap('#ed-layer-list .ed-layer[data-selected] .ed-giu');
  expect(tag, (await layerIds(page))[1] === nid, '«Giù» col tocco: scende di un posto');
  await page.tap('#ed-layer-list .ed-layer[data-selected] .ed-su');
  // trascinamento col tocco: dalla maniglia della prima riga sotto l'ultima
  await page.evaluate(() => { const p = document.getElementById('ed-left'), l = document.getElementById('ed-layer-list'); p.scrollTop = l.offsetTop - 12; });
  const grip = await box(page, `#ed-layer-list [data-id="${nid}"] .ed-grip`), last = await box(page, '#ed-layer-list > li:last-child');
  expect(tag, last.bottom + 6 < 852 && grip.y >= 0, `maniglia e ultima riga in vista (${grip.y}, ${last.bottom})`);
  await touchSeq(page, drag([grip.cx, grip.cy], [last.cx, last.bottom + 6], 6));
  const ids2 = await orderOf(page);
  expect(tag, ids2[0] === nid, 'trascinamento col tocco: il livello è il primo disegnato ' + JSON.stringify(ids2));
  expect(tag, (await layerIds(page)).slice(-1)[0] === nid, 'e sta in fondo al pannello');
  // annullato dal tocco interrotto: nessuna modifica
  const g2 = await box(page, `#ed-layer-list [data-id="${nid}"] .ed-grip`);
  await touchSeq(page, [['pointerdown', [{ id: 1, x: g2.cx, y: g2.cy }]], ['pointermove', [{ id: 1, x: g2.cx, y: g2.cy - 120 }]], ['pointercancel', [{ id: 1, x: g2.cx, y: g2.cy - 120 }]]]);
  expect(tag, JSON.stringify(await orderOf(page)) === JSON.stringify(ids2), 'trascinamento interrotto: l\'ordine non cambia');
  expect(tag, (await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)), 'scorrimento orizzontale col pannello pieno');
  await ctx.close();
}

// ---------- Proprietà (Task 12) ----------
const A_ = (page, fn, ...args) => page.evaluate(([fn, args]) => window.__editor.A[fn](...args), [fn, args]);
const notice = (page) => page.evaluate(() => window.__editor.store.get().notice);

async function propsMouse(br, origin) {
  const tag = 'chromium-1280-proprietà';
  const ctx = await br.newContext({ viewport: { width: 1280, height: 900 } });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  // nessuna selezione: la variante
  await page.waitForSelector('#ed-p-when');
  const v0 = await page.$eval('#ed-p-when', (s) => s.value);
  await page.selectOption('#ed-p-when', 'always');
  expect(tag, (await page.evaluate(() => { const s = window.__editor.store.get(); return s.hist.present.screens[s.loc.screen].variants[s.loc.variant].when; })) === 'always' || v0 === 'always', 'variante: «Quando»');
  await page.keyboard.press(process.platform === 'darwin' ? 'Meta+z' : 'Control+z');
  // la barra: larghezza scritta con tre tasti = un passo
  const bar = await elOf(page, 'bar');
  await A_(page, 'select', [bar.id]);
  await page.waitForSelector('#ed-p-w');
  const n0 = (await stateOf(page)).steps;
  await page.click('#ed-p-w', { clickCount: 3 });
  await page.keyboard.type('100');
  await page.keyboard.press('Tab');
  expect(tag, (await elById(page, bar.id)).w === 100, 'larghezza 100: ' + (await elById(page, bar.id)).w);
  expect(tag, (await stateOf(page)).steps === n0 + 1, 'tre tasti = un passo: ' + ((await stateOf(page)).steps - n0));
  // ±8 / −1 su X
  const x0 = (await elById(page, bar.id)).x;
  await page.click('[data-k="x+8"]'); await page.click('[data-k="x-1"]');
  expect(tag, (await elById(page, bar.id)).x === x0 + 7, '+8 e −1: ' + (await elById(page, bar.id)).x);
  expect(tag, (await page.$eval('#ed-p-x', (i) => i.value)) === String(x0 + 7), 'il campo X segue');
  // condizioni: aggiungi, cambia tipo, valore, togli
  await page.click('[data-k="cond-add"]');
  expect(tag, JSON.stringify((await elById(page, bar.id)).visibleIf) === '[{"perm":true}]', 'condizione aggiunta');
  await page.selectOption('[data-k="cond-0-type"]', 'battBelow');
  const sc = (await stateOf(page)).steps;
  await page.click('[data-k="cond-0-arg"]', { clickCount: 3 }); await page.keyboard.type('35');
  expect(tag, (await stateOf(page)).steps === sc + 1, 'numero della condizione: un passo per tutto il campo (' + ((await stateOf(page)).steps - sc) + ')');
  expect(tag, JSON.stringify((await elById(page, bar.id)).visibleIf) === '[{"battBelow":35}]', 'batteria sotto 35: ' + JSON.stringify((await elById(page, bar.id)).visibleIf));
  expect(tag, await page.evaluate(() => document.activeElement?.dataset.k === 'cond-0-arg'), 'il fuoco resta nel campo dopo la ricostruzione');
  await page.click('[data-k="cond-0-del"]');
  expect(tag, (await elById(page, bar.id)).visibleIf === undefined, 'condizione tolta');
  // aggancio
  await page.click('[data-k="grid-8"]');
  expect(tag, (await page.evaluate(() => window.__editor.store.get().snap.grid)) === 8 && (await page.getAttribute('[data-k="grid-8"]', 'aria-pressed')) === 'true', 'griglia 8');
  await page.click('#ed-p-snap-others');
  expect(tag, (await page.evaluate(() => window.__editor.store.get().snap.others)) === false, 'aggancio agli elementi spento');
  // tre elementi: allinea a sinistra, poi distribuisci
  const texts = await page.evaluate(() => { const s = window.__editor.store.get(); return s.hist.present.screens[s.loc.screen].variants[s.loc.variant].elements.filter((e) => e.type === 'text').map((e) => e.id); });
  // con due soli elementi «Distribuisci» è spento
  await A_(page, 'select', texts.slice(0, 2));
  await page.waitForSelector('[data-k="dist-h"]');
  expect(tag, await page.$eval('[data-k="dist-h"]', (b) => b.disabled) && (await page.textContent('#ed-props')).includes('almeno 3 elementi'), 'distribuisci spento con 2');
  await A_(page, 'setProp', texts[1], 'x', 133); await A_(page, 'endGesture');
  await A_(page, 'select', texts);
  await page.waitForSelector('[data-k="dist-h"]');
  const s1 = (await stateOf(page)).steps;
  const xs0 = await page.evaluate((ids) => { const s = window.__editor.store.get(); return ids.map((id) => s.hist.present.screens[s.loc.screen].variants[s.loc.variant].elements.find((e) => e.id === id).x); }, texts);
  await page.click('[data-align="left"]');
  const xs = await page.evaluate((ids) => { const s = window.__editor.store.get(); return ids.map((id) => s.hist.present.screens[s.loc.screen].variants[s.loc.variant].elements.find((e) => e.id === id).x); }, texts);
  expect(tag, new Set(xs).size === 1 && (await stateOf(page)).steps === s1 + 1, 'allinea a sinistra: un passo, stessa x ' + xs.join(',') + ' (prima ' + xs0.join(',') + ')');
  await page.click('[data-align="left"]');
  expect(tag, (await stateOf(page)).steps === s1 + 1 && (await notice(page)) === 'Già allineati.', 'di nuovo: nessun passo vuoto');
  expect(tag, (await page.textContent('#ed-props')).includes(texts.length + ' elementi scelti'), 'più elementi: il conto');
  // un bloccato: campi in sola lettura e «bloccato»
  await A_(page, 'toggleLocked', bar.id); await A_(page, 'pickLayer', bar.id);
  await page.waitForSelector('.ed-locked');
  expect(tag, await page.$eval('#ed-p-x', (i) => i.matches(':disabled')), 'bloccato: X in sola lettura');
  await ctx.close();
}

async function propsTouch(br, origin) {
  const tag = 'webkit-393-proprietà';
  const ctx = await br.newContext({ viewport: { width: 393, height: 852 }, hasTouch: true, isMobile: true, deviceScaleFactor: 3 });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  const t = await elOf(page, 'text');
  await A_(page, 'select', [t.id]);
  await page.tap('#ed-open-right');
  await page.waitForSelector('#ed-right[data-open]');
  expect(tag, await page.isVisible('#ed-p-text'), 'cassetto «Proprietà» aperto col campo «Testo»');
  for (const sel of ['[data-k="x-8"]', '[data-k="x-1"]', '[data-k="x+1"]', '[data-k="x+8"]', '#ed-p-x', '#ed-p-text', '#ed-p-font', '[data-k="insert-value"]', '[data-align="left"]', '[data-k="grid-1"]', '[data-k="cond-add"]', 'label[for="ed-p-upper"]']) {
    const b = await box(page, sel);
    expect(tag, b.w >= 44 && b.h >= 44, sel + ' sotto 44×44: ' + b.w.toFixed(1) + '×' + b.h.toFixed(1));
  }
  expect(tag, (await page.getAttribute('#ed-p-x', 'inputmode')) === 'numeric', 'X: tastiera numerica');
  // R1-I2: un rifiuto si legge col cassetto aperto
  await page.waitForTimeout(300);   // fine della transizione del cassetto
  await page.fill('#ed-p-w', 'abc');
  const seen = await page.evaluate(() => {
    const n = document.getElementById('ed-props-notice'), r = n.getBoundingClientRect();
    const hit = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
    return { text: n.textContent, ok: !n.hidden && r.width > 0 && r.top >= 0 && r.bottom <= innerHeight && n.contains(hit) };
  });
  expect(tag, seen.ok && /serve un numero/.test(seen.text), 'rifiuto visibile nel cassetto: ' + JSON.stringify(seen));
  await page.fill('#ed-p-w', String(t.w));
  const wide = () => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth);
  const fits = () => page.evaluate(() => { const p = document.getElementById('ed-right'); return p.scrollWidth <= p.clientWidth; });
  expect(tag, (await wide()) && (await fits()), 'nessuno scorrimento orizzontale nel cassetto');
  // ±1/±8 col tocco
  await page.tap('[data-k="x+8"]'); await page.tap('[data-k="x-1"]');
  expect(tag, (await elById(page, t.id)).x === t.x + 7, '+8 −1 col tocco: ' + (await elById(page, t.id)).x);
  // Chakra Petch 44 · 700 e {oggi.costo}: il «$» segnalato accanto al campo e in #ed-warnings entro 1 s
  await page.selectOption('#ed-p-font', 'ui:44:700');
  expect(tag, (await page.$eval('#ed-p-font', (s) => s.selectedOptions[0].textContent)) === 'Chakra Petch 44 · 700', 'carattere scelto');
  await page.tap('#ed-p-text');
  await page.fill('#ed-p-text', '{oggi.costo}');
  const t0 = Date.now();
  const okW = await page.waitForFunction(() => /«\$»/.test(document.getElementById('ed-warnings').textContent), null, { timeout: 1000 }).then(() => true, () => false);
  expect(tag, okW, 'avviso del «$» in #ed-warnings entro 1 s (' + (Date.now() - t0) + ' ms): ' + (await page.textContent('#ed-warnings')));
  expect(tag, /«\$»/.test(await page.textContent('#ed-p-text ~ .ed-field-warn')), 'avviso del «$» accanto al campo');
  expect(tag, (await elById(page, t.id)).text === '{oggi.costo}', 'testo salvato nel tema');
  // graffa aperta: resta nel campo col motivo, il tema no
  await page.fill('#ed-p-text', '{oggi.co');
  expect(tag, /Graffa/.test(await page.textContent('#ed-p-text ~ .ed-field-err')) && (await elById(page, t.id)).text === '{oggi.costo}', 'graffa aperta: motivo e tema invariato');
  await page.fill('#ed-p-text', 'Costo ');
  // «Inserisci valore»: niente riga.* fuori da una lista; un tocco inserisce al cursore
  await page.tap('[data-k="insert-value"]');
  await page.waitForSelector('.ed-values:not([hidden])');
  expect(tag, (await page.$$('.ed-value[data-value^="riga."]')).length === 0, 'fuori da una lista: niente riga.*');
  const ex = await page.textContent('.ed-value[data-value="ora"] .ed-value-ex');
  expect(tag, ex === '10:30', 'esempio di {ora}: ' + ex);
  await page.selectOption('[data-k="value-filter"]', 'maiuscolo');
  await page.tap('.ed-value[data-value="giorno"]');
  expect(tag, (await elById(page, t.id)).text === 'Costo {giorno|maiuscolo}', 'valore inserito: ' + (await elById(page, t.id)).text);
  expect(tag, (await wide()) && (await fits()), 'nessuno scorrimento orizzontale con l\'elenco dei valori');
  // bozza: annulla la rimpiazza; cambiare elemento la scarta con un avviso
  await page.fill('#ed-p-text', 'Costo {oggi.co');
  expect(tag, /^Non applicato: .*la scheda tiene il testo di prima\.$/.test(await page.textContent('#ed-p-text ~ .ed-field-err')), 'bozza: «Non applicato»');
  await A_(page, 'undo');
  expect(tag, (await page.$eval('#ed-p-text', (x) => x.value)) === (await elById(page, t.id)).text, 'annulla con la bozza aperta: il campo mostra il tema');
  await page.fill('#ed-p-text', '{oggi.co');
  const other = await elOf(page, 'bar');
  await A_(page, 'select', [other.id]);
  await page.waitForFunction(() => document.getElementById('ed-props-notice').textContent === 'Testo non applicato scartato.', null, { timeout: 1000 }).catch(() => {});
  expect(tag, (await notice(page)) === 'Testo non applicato scartato.', 'bozza scartata: avviso ' + JSON.stringify(await notice(page)));
  await ctx.close();
}

// ---------- barra in alto (Task 13): temi, salvataggio automatico, dati di prova, animazione, conflitti ----------
const saveIs = (page, text, timeout = 5000) => page.waitForFunction((t) => document.getElementById('ed-save').textContent === t, text, { timeout }).then(() => true, () => false);
const recordRequests = (page) => {
  const reqs = [];
  page.on('request', (r) => { const u = new URL(r.url()); if (u.pathname.startsWith('/api/themes')) reqs.push(r.method() + ' ' + u.pathname + u.search); });
  return reqs;
};
const putOk = (r) => /^PUT \/api\/themes\/[^/?]+\?autosave=1&base=[1-9]\d*(&fresh=1)?$/.test(r);
const themeId = (page) => page.evaluate(() => window.__editor.store.get().themeId);
const dialogOpen = (page) => page.evaluate(() => !!document.querySelector('dialog[open]'));
// tutti gli elementi visibili della barra dentro lo schermo; pulsanti e selettori ≥ 44×44
const barProblems = (page) => page.evaluate(() => {
  const out = [], W = window.innerWidth;
  for (const el of document.querySelectorAll('#ed-top button, #ed-top select, #ed-top .ed-pick, #ed-top #ed-save')) {
    const r = el.getBoundingClientRect();
    if (!r.width || !r.height || getComputedStyle(el).visibility === 'hidden') continue;
    const name = el.id || el.textContent.trim().slice(0, 20);
    if (r.left < 0 || r.right > W + 0.5) out.push(name + ' fuori dallo schermo (' + Math.round(r.left) + '…' + Math.round(r.right) + ')');
    if ((el.tagName === 'BUTTON' || el.tagName === 'SELECT') && (r.width < 44 || r.height < 44)) out.push(name + ' sotto 44×44: ' + Math.round(r.width) + '×' + Math.round(r.height));
  }
  if (document.documentElement.scrollWidth > W) out.push('scorrimento orizzontale');
  return out;
});

async function topbarMouse(br, origin) {
  const tag = 'chromium-1280-barra';
  const ctx = await br.newContext({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  const reqs = recordRequests(page);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  // schermata di serie: spenta, con la spiegazione
  expect(tag, await page.$eval('#ed-stock', (b) => b.disabled && b.title === 'Arriva con le schermate di serie come dati'), '«Parti dalla schermata di serie» spento con il titolo');
  expect(tag, !(await page.isVisible('#ed-more-btn')), 'a 1280 niente «Altro»');
  // salvataggio automatico: PUT con la base, poi «Salvato»
  const n0 = (await stateOf(page)).steps;
  await A_(page, 'add', 'rect');
  expect(tag, (await page.textContent('#ed-save')) === 'Modifiche in sospeso…', 'in sospeso: ' + (await page.textContent('#ed-save')));
  expect(tag, await saveIs(page, 'Salvato'), 'salvato dopo la modifica: ' + (await page.textContent('#ed-save')));
  expect(tag, reqs.some(putOk), 'PUT ?autosave=1&base=: ' + reqs.join(', '));
  expect(tag, reqs.includes('POST /api/themes/check'), 'controllo del servizio');
  // Annulla/Ripeti dai pulsanti
  await page.click('#ed-undo');
  expect(tag, (await stateOf(page)).steps === n0, '«Annulla»');
  await page.click('#ed-redo');
  expect(tag, (await stateOf(page)).steps === n0 + 1, '«Ripeti»');
  expect(tag, await saveIs(page, 'Salvato'), 'annulla/ripeti salvati');
  // con un dialogo aperto i tasti non toccano la tela
  const bar = await elOf(page, 'bar');
  await A_(page, 'select', [bar.id]);
  await page.click('#ed-new');
  expect(tag, await dialogOpen(page), 'Nuovo: dialogo aperto');
  await page.focus('#ed-dlg-cancel');
  await page.keyboard.press('ArrowRight'); await page.keyboard.press('Delete');
  const b2 = await elOf(page, 'bar');
  expect(tag, b2 && b2.id === bar.id && b2.x === bar.x, 'dialogo aperto: frecce e Canc ignorati');
  await page.fill('#ed-dlg-input', 'Notte fonda');
  await page.click('#ed-dlg-ok');
  await page.waitForFunction(() => window.__editor.store.get().themeId === 'notte-fonda', null, { timeout: 5000 }).catch(() => {});
  expect(tag, (await themeId(page)) === 'notte-fonda', 'Nuovo «Notte fonda» → notte-fonda: ' + (await themeId(page)));
  expect(tag, (await page.$eval('#ed-themes', (s) => s.value)) === 'notte-fonda' && new URL(page.url()).searchParams.get('theme') === 'notte-fonda', 'Nuovo: selettore e indirizzo');
  expect(tag, reqs.includes('POST /api/themes'), 'Nuovo: POST /api/themes');
  // Rinomina: solo il nome
  await page.click('#ed-rename');
  expect(tag, (await page.$eval('#ed-dlg-input', (i) => i.value)) === 'Notte fonda', 'Rinomina: il nome attuale nel campo');
  await page.fill('#ed-dlg-input', 'Notte piena');
  const putReq = page.waitForRequest((r) => r.method() === 'PUT' && r.url().includes('/api/themes/notte-fonda?'), { timeout: 5000 }).catch(() => null);
  await page.click('#ed-dlg-ok');
  await page.waitForFunction(() => document.getElementById('ed-themes').selectedOptions[0]?.textContent === 'Notte piena', null, { timeout: 2000 }).catch(() => {});
  await putReq;
  expect(tag, await saveIs(page, 'Salvato'), 'Rinomina salvata');
  expect(tag, (await page.$eval('#ed-themes', (s) => s.selectedOptions[0].textContent)) === 'Notte piena' && (await themeId(page)) === 'notte-fonda', 'Rinomina: nome nuovo, stesso id');
  expect(tag, reqs.some((r) => r.startsWith('PUT /api/themes/notte-fonda?autosave=1&base=')), 'Rinomina: PUT del tema');
  // Duplica: import, id scelto dal servizio
  await page.click('#ed-dup');
  await page.waitForFunction(() => window.__editor.store.get().themeId === 'notte-fonda-2', null, { timeout: 5000 }).catch(() => {});
  expect(tag, (await themeId(page)) === 'notte-fonda-2' && reqs.includes('POST /api/themes/import'), 'Duplica → notte-fonda-2 via import: ' + (await themeId(page)));
  expect(tag, (await page.evaluate(() => window.__editor.store.get().hist.present.name)) === 'Notte piena copia', 'Duplica: nome «… copia»');
  // Esporta: un file <id>.theme.json
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 5000 }), page.click('#ed-export')]);
  expect(tag, dl.suggestedFilename() === 'notte-fonda-2.theme.json', 'Esporta: ' + dl.suggestedFilename());
  const exported = JSON.parse(fs.readFileSync(await dl.path(), 'utf8'));
  expect(tag, exported.id === 'notte-fonda-2' && exported.name === 'Notte piena copia', 'Esporta: il tema');
  // Importa
  await page.setInputFiles('#ed-import-file', { name: 'x.theme.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify({ ...exported, name: 'Importato' })) });
  await page.waitForFunction(() => window.__editor.store.get().hist?.present?.name === 'Importato', null, { timeout: 5000 }).catch(() => {});
  const imp = 'notte-fonda-2-2';   // l'id c'è già: il servizio aggiunge -2
  expect(tag, (await themeId(page)) === imp, 'Importa → ' + imp + ': ' + (await themeId(page)));
  // Elimina: conferma, poi DELETE
  await page.click('#ed-delete');
  expect(tag, (await page.textContent('#ed-dlg-title')) === 'Eliminare il tema «Importato»?' && (await page.textContent('#ed-dlg-text')) === 'Si perde anche la cronologia. Sulla scheda resta finché non ne invii un altro.', 'Elimina: testo della conferma');
  await page.click('#ed-dlg-cancel');
  expect(tag, (await themeId(page)) === imp && !reqs.some((r) => r.startsWith('DELETE')), 'Elimina annullato: niente');
  await page.click('#ed-delete'); await page.click('#ed-dlg-ok');
  await page.waitForFunction((id) => window.__editor.store.get().themeId !== id, imp, { timeout: 5000 }).catch(() => {});
  expect(tag, reqs.includes('DELETE /api/themes/' + imp), 'Elimina: DELETE');
  expect(tag, !(await page.$(`#ed-themes option[value="${imp}"]`)), 'Elimina: tolto dal selettore');
  // varianti dai pulsanti
  await page.selectOption('#ed-themes', 'notte-fonda');
  await page.waitForFunction(() => window.__editor.store.get().themeId === 'notte-fonda');
  expect(tag, await page.$eval('#ed-var-del', (b) => b.disabled), 'Togli spento sull\'unica variante dell\'unica schermata');
  await page.click('#ed-var-add');
  expect(tag, (await page.$$eval('#ed-variant option', (o) => o.length)) === 2 && (await page.$eval('#ed-variant', (s) => s.value)) === '1', 'Aggiungi variante: aperta la nuova');
  await A_(page, 'setVariantProp', { when: 'many' });   // due varianti uguali scambiate = nessun passo
  await page.click('#ed-var-up');
  expect(tag, (await page.$eval('#ed-variant', (s) => s.value)) === '0', 'Su: ' + (await page.$eval('#ed-variant', (s) => s.value)));
  await page.click('#ed-var-del');
  expect(tag, (await page.$$eval('#ed-variant option', (o) => o.length)) === 1, 'Togli');
  // dati di prova, avvisi e animazione: l'unica variante vale con una sessione, con sei la scheda mostra quella di serie
  await A_(page, 'setVariantProp', { when: 'one' });
  const c1 = await page.textContent('#ed-warn-count');
  await page.selectOption('#ed-sessions', '6');
  await page.waitForFunction((c) => document.getElementById('ed-warn-count').textContent !== c, c1, { timeout: 2000 }).catch(() => {});
  expect(tag, (await page.textContent('#ed-warn-count')) !== c1, 'contatore degli avvisi con 6 sessioni: ' + c1);
  expect(tag, (await page.isVisible('#ed-banner')) && /schermata di serie/.test(await page.textContent('#ed-banner')), 'scritta sopra la tela: ' + (await page.textContent('#ed-banner')));
  await page.click('[data-test="night"]');
  expect(tag, (await page.getAttribute('[data-test="night"]', 'aria-pressed')) === 'true' && (await page.evaluate(() => window.__editor.store.get().test.night)), '«Notte»');
  await page.click('#ed-play');
  await page.waitForFunction(() => window.__editor.store.get().frame === 1, null, { timeout: 2000 }).catch(() => {});
  expect(tag, (await page.evaluate(() => window.__editor.store.get().frame)) === 1, '«Anima»: fotogramma 1');
  await page.click('#ed-play');
  expect(tag, (await page.evaluate(() => { const s = window.__editor.store.get(); return [s.playing, s.frame, s.clock].join(); })) === 'false,0,0', '«Anima» fermo');
  expect(tag, (await page.textContent('#ed-size')).includes('/ 4 096 byte'), 'misura della schermata: ' + (await page.textContent('#ed-size')));
  // problemi fuori dalla variante aperta (I2): elencati col luogo; il tocco apre la variante e sceglie l'elemento
  await A_(page, 'add', 'rect');
  await page.click('#ed-var-add');
  expect(tag, await saveIs(page, 'Salvato'), 'variante aggiunta e salvata');
  const el0 = await page.evaluate(() => window.__editor.store.get().hist.present.screens.home.variants[0].elements[0]?.id);
  await page.evaluate(() => { const st = window.__editor.store; st.set({ check: { ...st.get().check, problems: [
    { path: 'screens.home.variants[0].elements[0].x', msg: 'x: fuori da ±512' }, { path: 'name', msg: 'nome da 1 a 24 byte' }] } }); });
  const rows = await page.$$eval('#ed-warn-list [data-kind="elsewhere"]', (ls) => ls.map((l) => [l.textContent, !!l.querySelector('button')]));
  expect(tag, rows.length === 2 && /^Home › variante 1 › .+: fuori da ±512$/.test(rows[0][0]) && rows[0][1] && rows[1][0] === 'tema: nome da 1 a 24 byte' && !rows[1][1], 'problemi altrove col luogo: ' + JSON.stringify(rows));
  await page.click('#ed-warn-list [data-kind="elsewhere"] button');
  const at = await page.evaluate(() => { const s = window.__editor.store.get(); return { loc: s.loc, sel: s.sel }; });
  expect(tag, at.loc.screen === 'home' && at.loc.variant === 0 && at.sel.length === 1 && at.sel[0] === el0, 'il tocco apre la variante e sceglie l\'elemento: ' + JSON.stringify(at));
  expect(tag, (await barProblems(page)).length === 0, 'barra: ' + (await barProblems(page)).join('; '));
  for (const r of reqs) if (r.startsWith('PUT ') && !putOk(r)) problems.push(tag + ': PUT senza base: ' + r);
  await ctx.close();
}

async function topbarTouch(br, origin) {
  const tag = 'webkit-393-barra';
  const ctx = await br.newContext({ viewport: { width: 393, height: 852 }, hasTouch: true, isMobile: true, deviceScaleFactor: 3 });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  for (const p of await barProblems(page)) problems.push(tag + ': ' + p);
  // selettori della barra: aspetto proprio e 44 pt anche su WebKit
  const sel = await page.$$eval('#ed-top select', (l) => l.filter((s) => s.getBoundingClientRect().width).map((s) => [s.id, Math.round(s.getBoundingClientRect().height), getComputedStyle(s).webkitAppearance || getComputedStyle(s).appearance]));
  for (const [id, hgt, ap] of sel) expect(tag, hgt >= 44 && ap === 'none', '#' + id + ': ' + hgt + ' px, appearance ' + ap);
  expect(tag, await page.isVisible('#ed-more-btn'), '«Altro» visibile');
  expect(tag, !(await page.isVisible('#ed-import')), '«Importa» non nella barra');
  await page.tap('#ed-more-btn');
  await page.waitForSelector('#ed-more:not([hidden])');
  expect(tag, await page.$eval('#ed-more', (m) => !!m.querySelector('#ed-import') && !!m.querySelector('#ed-sessions') && !!m.querySelector('#ed-stock')), '«Altro» contiene Importa, dati di prova e schermata di serie');
  expect(tag, (await page.textContent('#ed-stock-why')) === 'Arriva con le schermate di serie come dati' && (await page.isVisible('#ed-stock-why')), 'spiegazione visibile nel menu');
  for (const p of await barProblems(page)) problems.push(tag + ' col menu aperto: ' + p);
  // Nuovo dal menu
  await page.tap('#ed-new');
  await page.waitForSelector('#ed-dialog[open]');
  expect(tag, await page.isHidden('#ed-more'), 'il menu si chiude');
  await page.fill('#ed-dlg-input', 'Notte fonda');
  await page.tap('#ed-dlg-ok');
  await page.waitForFunction(() => window.__editor.store.get().themeId === 'notte-fonda', null, { timeout: 5000 }).catch(() => {});
  expect(tag, (await themeId(page)) === 'notte-fonda', 'Nuovo dal menu');
  // contatore degli avvisi: cambia scegliendo «6 sessioni»
  await A_(page, 'setVariantProp', { when: 'one' });
  const c1 = await page.textContent('#ed-warn-count');
  await page.tap('#ed-more-btn');
  await page.selectOption('#ed-sessions', '6');
  await page.waitForFunction((c) => document.getElementById('ed-warn-count').textContent !== c, c1, { timeout: 2000 }).catch(() => {});
  expect(tag, (await page.textContent('#ed-warn-count')) !== c1, 'contatore con 6 sessioni: ' + c1 + ' → ' + (await page.textContent('#ed-warn-count')));
  await page.tap('#ed-more-btn');
  await page.waitForSelector('#ed-more[hidden]', { state: 'attached' });
  for (const p of await barProblems(page)) problems.push(tag + ' alla fine: ' + p);
  expect(tag, await saveIs(page, 'Salvato'), 'salvato: ' + (await page.textContent('#ed-save')));
  await ctx.close();
}

// 409 dal server finto (--theme-conflict=2): mai una sovrascrittura senza scelta
async function conflictFlow(br, origin) {
  const tag = 'chromium-1280-conflitto';
  const ctx = await br.newContext({ viewport: { width: 1280, height: 900 } });
  const page = await ctx.newPage();
  watch(page, tag, origin, /status of (409 \(Conflict\)|404 \(Not Found\))/);
  const reqs = recordRequests(page);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  const mine = await page.evaluate(() => window.__editor.store.get().hist.present.name);
  await A_(page, 'add', 'rect');
  expect(tag, await saveIs(page, 'Cambiato altrove'), '409 → «Cambiato altrove»: ' + (await page.textContent('#ed-save')));
  expect(tag, (await page.isVisible('#ed-reload')) && (await page.isVisible('#ed-overwrite')), 'Ricarica e Sovrascrivi');
  const live = () => page.evaluate(() => fetch('/api/themes/prova', { cache: 'no-store' }).then((r) => r.json()));
  expect(tag, (await live()).name === 'Dal telefono', 'la versione del telefono non è sovrascritta');
  // Ricarica: versione del servizio, le mie da parte
  await page.click('#ed-reload');
  expect(tag, await saveIs(page, 'Salvato'), 'Ricarica → Salvato');
  expect(tag, (await page.evaluate(() => window.__editor.store.get().hist.present.name)) === 'Dal telefono', 'Ricarica: versione del servizio');
  expect(tag, await page.isVisible('#ed-restore'), '«Riprendi le mie modifiche» offerto');
  await page.click('#ed-restore');
  expect(tag, (await page.textContent('#ed-dlg-list')).includes('Home'), 'Riprendi: cosa cambia ' + (await page.textContent('#ed-dlg-list')));
  await page.click('#ed-dlg-ok');
  // il secondo salvataggio trova di nuovo il tema cambiato: Sovrascrivi, dopo aver visto cosa cambia
  expect(tag, await saveIs(page, 'Cambiato altrove'), 'secondo 409');
  const versions = () => page.evaluate(() => fetch('/api/themes/prova/history', { cache: 'no-store' }).then((r) => r.json()).then((b) => b.versions.length));
  const before = await versions();
  await page.click('#ed-overwrite');
  await page.waitForSelector('#ed-dialog[open]');
  const list = await page.textContent('#ed-dlg-list');
  expect(tag, list.includes(`il nome («Dal telefono» → «${mine}»)`) && list.includes('Home'), 'Sovrascrivi: cosa cambia ' + list);
  await page.click('#ed-dlg-ok');
  expect(tag, await saveIs(page, 'Salvato'), 'Sovrascritto → Salvato: ' + (await page.textContent('#ed-save')));
  expect(tag, (await live()).name === mine, 'sul servizio la mia versione');
  const after = await versions();
  expect(tag, after === before + 1, `Sovrascrivi: versione nuova, quella del telefono resta in cronologia (${before} → ${after})`);
  const puts = reqs.filter((r) => r.startsWith('PUT '));
  expect(tag, puts.at(-1)?.includes('&fresh=1') && puts.slice(0, -1).every((r) => !r.includes('fresh')), 'fresh=1 solo sul PUT di Sovrascrivi: ' + puts.join(', '));
  expect(tag, puts.length >= 3 && puts.every(putOk), 'ogni PUT con la base: ' + puts.join(', '));
  // eliminato altrove (M3): «Eliminato altrove», poi «Salva come nuovo» (import)
  await page.evaluate(() => fetch('/api/themes/prova', { method: 'DELETE' }));
  await A_(page, 'add', 'rect');
  expect(tag, await saveIs(page, 'Eliminato altrove'), '404 → «Eliminato altrove»: ' + (await page.textContent('#ed-save')));
  expect(tag, (await page.isVisible('#ed-save-new')) && !(await page.isVisible('#ed-retry')), '«Salva come nuovo», niente «Riprova»');
  await page.click('#ed-save-new');
  expect(tag, await saveIs(page, 'Salvato'), 'salvato come nuovo');
  expect(tag, reqs.filter((r) => r === 'POST /api/themes/import').length === 1 && (await live()).name === mine, 'salvato come nuovo via import, col mio contenuto');
  await ctx.close();
}

// ---------- prova completa (Task 14): dal tema nuovo all'invio alla scheda, a 1280 col mouse e a 393 col tocco ----------
// a 393: niente scorrimento orizzontale e ogni comando visibile ≥ 44×44 (le caselle contano con la loro etichetta)
const touchProblems = (page) => page.evaluate(() => {
  const out = [], W = innerWidth, H = innerHeight;
  if (document.documentElement.scrollWidth > W) out.push('scorrimento orizzontale (' + document.documentElement.scrollWidth + ' > ' + W + ')');
  for (const el of document.querySelectorAll('button, select, input, [role=button]')) {
    if (el.checkVisibility && !el.checkVisibility({ checkOpacity: true, checkVisibilityCSS: true })) continue;
    let r = el.getBoundingClientRect();
    if (!r.width || !r.height || r.right <= 0 || r.left >= W || r.bottom <= 0 || r.top >= H + 4000) continue;
    if (el.closest('[hidden], dialog:not([open])')) continue;
    // fuori dallo schermo per scelta (cassetto chiuso spostato di lato)
    if (el.closest('.ed-pane') && !el.closest('.ed-pane[data-open]') && innerWidth < 900) continue;
    if ((el.type === 'checkbox' || el.type === 'radio') && el.labels?.[0]) r = el.labels[0].getBoundingClientRect();
    if (r.width < 44 || r.height < 44) out.push((el.id ? '#' + el.id : el.tagName.toLowerCase() + (el.dataset.k ? '[data-k=' + el.dataset.k + ']' : '') + ' «' + (el.textContent || el.value || '').trim().slice(0, 18) + '»') + ' ' + Math.round(r.width) + '×' + Math.round(r.height));
  }
  return [...new Set(out)];
});
const sendLine = (page) => page.textContent('#ed-send-state');
const waitSend = (page, text, timeout = 8000) => page.waitForFunction((t) => document.getElementById('ed-send-state').textContent === t, text, { timeout }).then(() => true, () => false);

async function fullFlow(br, origin, touch) {
  const tag = (touch ? 'webkit-393' : 'chromium-1280') + '-completa';
  const ctx = await br.newContext(touch ? { viewport: { width: 393, height: 852 }, hasTouch: true, isMobile: true, deviceScaleFactor: 3 } : { viewport: { width: 1280, height: 900 } });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  const reqs = []; page.on('request', (r) => { const u = new URL(r.url()); if (u.pathname.startsWith('/api/')) reqs.push(r.method() + ' ' + u.pathname); });
  const press = (sel) => (touch ? page.tap(sel) : page.click(sel));
  const fit = async (step) => { if (touch) for (const p of await touchProblems(page)) problems.push(tag + ' [' + step + ']: ' + p); };
  const openDrawer = async (side) => { if (!touch) return; const id = side === 'left' ? 'ed-left' : 'ed-right'; if (!(await page.$('#' + id + '[data-open]'))) { await page.tap('#ed-open-' + side); await page.waitForSelector('#' + id + '[data-open]'); await page.waitForTimeout(300); } };
  const closeDrawer = async (side) => { if (!touch) return; const id = side === 'left' ? 'ed-left' : 'ed-right'; if (await page.$('#' + id + '[data-open]')) { await page.tap('#' + id + ' .ed-close'); await page.waitForSelector('#' + id + ':not([data-open])'); await page.waitForTimeout(300); } };
  const els = () => page.evaluate(() => { const s = window.__editor.store.get(); return s.hist.present.screens[s.loc.screen].variants[s.loc.variant].elements.map((e) => ({ id: e.id, type: e.type, x: e.x, y: e.y, w: e.w, children: e.children?.length })); });

  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  await noSnap(page);
  await fit('apertura');
  // 1. nuovo tema
  if (touch) { await page.tap('#ed-more-btn'); await page.waitForSelector('#ed-more:not([hidden])'); }
  await press('#ed-new');
  await page.waitForSelector('#ed-dialog[open]');
  await page.fill('#ed-dlg-input', 'Prova completa');
  await fit('dialogo Nuovo');
  await press('#ed-dlg-ok');
  await page.waitForFunction(() => window.__editor.store.get().themeId === 'prova-completa', null, { timeout: 5000 }).catch(() => {});
  expect(tag, (await themeId(page)) === 'prova-completa', 'nuovo tema: ' + (await themeId(page)));
  await fit('nuovo tema');
  // 2. aggiungi un testo e una mascotte (per «Anima»)
  for (const type of ['mascot', 'text']) {
    await openDrawer('left');
    await press(`.ed-add-btn[data-type="${type}"]`);
    if (touch) await page.waitForSelector('#ed-left:not([data-open])');
  }
  let list = await els();
  const txt = list.find((e) => e.type === 'text'), masc = list.find((e) => e.type === 'mascot');
  expect(tag, !!txt && !!masc, 'aggiunti testo e mascotte: ' + JSON.stringify(list));
  await fit('aggiunti');
  // 3. Proprietà: {oggi.costo} in Chakra Petch 44 · 700 → avviso del «$»
  await A_(page, 'select', [txt.id]);
  await openDrawer('right');
  await page.waitForSelector('#ed-p-font');
  await page.selectOption('#ed-p-font', 'ui:44:700');
  await page.fill('#ed-p-text', '{oggi.costo}');
  await page.waitForFunction(() => document.getElementById('ed-warnings').textContent.includes('{oggi.costo} può mostrare «$», che Chakra Petch 44 non ha: si vedrà «?»'), null, { timeout: 2000 }).catch(() => {});
  expect(tag, (await page.textContent('#ed-warnings')).includes('{oggi.costo} può mostrare «$», che Chakra Petch 44 non ha: si vedrà «?»'), 'avviso del «$» in #ed-warnings: ' + (await page.textContent('#ed-warnings')));
  await fit('proprietà');
  await closeDrawer('right');
  // 4. sposta, annulla, ripeti
  const t0 = (await els()).find((e) => e.id === txt.id);
  const at = await toClient(page, t0.x + 4, t0.y + 4);
  if (touch) await touchSeq(page, drag(at, [at[0] + 15, at[1]]));
  else { await page.mouse.move(at[0], at[1]); await page.mouse.down(); for (let i = 1; i <= 3; i++) await page.mouse.move(at[0] + 5 * i, at[1]); await page.mouse.up(); }
  const t1 = (await els()).find((e) => e.id === txt.id);
  expect(tag, t1.x > t0.x, 'spostato: ' + t0.x + ' → ' + t1.x);
  await press('#ed-undo');
  expect(tag, (await els()).find((e) => e.id === txt.id).x === t0.x, 'annulla: torna a ' + t0.x);
  await press('#ed-redo');
  expect(tag, (await els()).find((e) => e.id === txt.id).x === t1.x, 'ripeti: di nuovo a ' + t1.x);
  await fit('sposta');
  // 5. duplica, raggruppa, separa (dalle Proprietà: niente tastiera sul telefono)
  await A_(page, 'select', [txt.id]);
  await openDrawer('right');
  await press('[data-k="duplicate"]');
  list = await els();
  expect(tag, list.filter((e) => e.type === 'text').length === 2, 'duplicato: ' + list.length + ' elementi');
  await fit('duplica');
  await A_(page, 'select', list.filter((e) => e.type === 'text').map((e) => e.id));
  await page.waitForSelector('[data-k="group"]');
  await press('[data-k="group"]');
  list = await els();
  expect(tag, list.some((e) => e.type === 'group' && e.children === 2), 'raggruppati: ' + JSON.stringify(list.map((e) => e.type)));
  await fit('raggruppa');
  await page.waitForSelector('[data-k="ungroup"]');
  await press('[data-k="ungroup"]');
  list = await els();
  expect(tag, !list.some((e) => e.type === 'group') && list.filter((e) => e.type === 'text').length === 2, 'separati: ' + JSON.stringify(list.map((e) => e.type)));
  await fit('separa');
  await closeDrawer('right');
  // 6. «6 sessioni» e «Anima»: la tela cambia fra due fotogrammi (mascotte animata)
  if (touch) { await page.tap('#ed-more-btn'); await page.waitForSelector('#ed-more:not([hidden])'); }
  await page.selectOption('#ed-sessions', '6');
  expect(tag, (await page.evaluate(() => window.__editor.store.get().test.sessions)) === 6, '6 sessioni');
  if (touch) { await page.tap('#ed-more-btn'); await page.waitForSelector('#ed-more[hidden]', { state: 'attached' }); }
  await A_(page, 'select', [masc.id]);
  await A_(page, 'setProp', masc.id, 'anim', true); await A_(page, 'endGesture');
  await A_(page, 'select', []);
  await press('#ed-play');
  await page.waitForTimeout(100);
  const h1 = await screenHash(page); await page.waitForTimeout(600); const h2 = await screenHash(page);
  expect(tag, h1 !== h2, '«Anima»: la tela cambia fra due fotogrammi');
  await press('#ed-play');
  await fit('anima');
  // 7. «Invia alla scheda» → «Tema attivo sulla scheda»; prima del POST il PUT del salvataggio
  expect(tag, await saveIs(page, 'Salvato', 8000), 'salvato prima di inviare: ' + (await page.textContent('#ed-save')));
  // una modifica appena fatta: l'invio la salva subito, senza aspettare i 1 500 ms
  await A_(page, 'setProp', txt.id, 'x', 3);
  const nPut = reqs.filter((r) => r.startsWith('PUT ')).length;
  await press('#ed-send');
  expect(tag, await waitSend(page, 'Tema attivo sulla scheda'), '«Tema attivo sulla scheda»: ' + (await sendLine(page)));
  const iPut = reqs.findLastIndex((r) => r.startsWith('PUT /api/themes/prova-completa')), iSend = reqs.indexOf('POST /api/displays/calamaio-3f2a/theme');
  expect(tag, reqs.filter((r) => r.startsWith('PUT ')).length > nPut && iPut >= 0 && iSend > iPut, 'PUT prima del POST di invio: ' + reqs.slice(-8).join(', '));
  expect(tag, reqs.includes('GET /api/status'), 'fasi da GET /api/status');
  await fit('inviato');
  await ctx.close();
}

// invio non riuscito: server finto a parte con --theme-fail
async function sendFail(br, origin, touch) {
  const tag = (touch ? 'webkit-393' : 'chromium-1280') + '-invio-fallito';
  const ctx = await br.newContext(touch ? { viewport: { width: 393, height: 852 }, hasTouch: true, isMobile: true, deviceScaleFactor: 3 } : { viewport: { width: 1280, height: 900 } });
  const page = await ctx.newPage();
  watch(page, tag, origin);
  await page.goto(origin + '/editor.html?test=1');
  await page.waitForSelector('#ed-screen[data-drawn]');
  await page.waitForFunction(() => !document.getElementById('ed-send').disabled, null, { timeout: 5000 }).catch(() => {});
  await (touch ? page.tap('#ed-send') : page.click('#ed-send'));
  expect(tag, await waitSend(page, 'Non riuscito: download non riuscito (HTTP 410)'), 'errore: ' + (await sendLine(page)));
  if (touch) for (const p of await touchProblems(page)) problems.push(tag + ': ' + p);
  // una schermata che la scheda scarterebbe: niente invio, col motivo
  await page.evaluate(() => window.__editor.A.commit((t) => { const u = structuredClone(t); u.screens.home.variants[0].elements[0].x = 900; return u; }));
  const before = await page.evaluate(() => performance.getEntriesByType('resource').filter((e) => e.name.includes('/theme') && e.initiatorType === 'fetch').length);
  await (touch ? page.tap('#ed-send') : page.click('#ed-send'));
  await page.waitForFunction(() => /schermata di serie/.test(document.getElementById('ed-send-state').textContent), null, { timeout: 3000 }).catch(() => {});
  expect(tag, /^Non inviato: la scheda mostrerebbe la schermata di serie/.test(await sendLine(page)), 'schermata di serie: ' + (await sendLine(page)));
  const after = await page.evaluate(() => performance.getEntriesByType('resource').filter((e) => e.name.includes('/displays/') && e.initiatorType === 'fetch').length);
  expect(tag, after <= before, 'schermata di serie: nessun POST di invio');
  await ctx.close();
}

async function run() {
  // il confronto con l'elenco deve funzionare prima di fidarsi del risultato
  for (const [m, u, ok] of [['GET', '/', true], ['GET', '/index.html', true], ['HEAD', '/editor.html', true], ['GET', '/assets/js/editor.js', true],
    ['GET', '/api/themes/prova', true], ['PUT', '/api/themes/prova', true], ['POST', '/api/themes/check', true], ['DELETE', '/api/themes/prova', true],
    ['POST', '/api/themes/import', true], ['POST', '/api/displays/calamaio-3f2a/theme', true], ['DELETE', '/api/themes', false], ['GET', '/api/avatars/gatto/anchors', true],
    ['GET', '/firmware.html', false], ['GET', '/api/backups', false], ['GET', '/api/themes/a/b', false], ['POST', '/api/themes/prova', false], ['GET', '/x/', false]]) {
    if (phoneAllowed(m, u) !== ok) problems.push('elenco dei telefoni: ' + m + ' ' + u + ' dovrebbe essere ' + (ok ? 'ammesso' : 'rifiutato'));
  }
  const origin = 'http://127.0.0.1:' + PORT;

  // Chromium a 1280: tre colonne
  {
    const mock = await startMock(PORT);
    const br = await chromium.launch();
    const ctx = await br.newContext({ viewport: { width: 1280, height: 900 } });
    const page = await ctx.newPage();
    watch(page, 'chromium-1280', origin);
    await page.goto(origin + '/editor.html');
    await common(page, 'chromium-1280');
    for (const id of ['ed-left', 'ed-stage', 'ed-right']) if (!(await page.isVisible('#' + id))) problems.push('1280: #' + id + ' non visibile');
    if (await page.isVisible('#ed-open-left')) problems.push('1280: il pulsante dei cassetti si vede con tre colonne');
    const z = await page.evaluate(() => document.getElementById('ed-screen').getBoundingClientRect().width / 250);
    if (!(Number.isInteger(z) && z >= 2)) problems.push('1280: zoom iniziale ' + z);
    // cambio di schermata: Riposo di prova si disegna, una schermata assente mostra il messaggio
    await page.selectOption('#ed-screens', 'oggi');
    await page.waitForSelector('#ed-empty:not([hidden])');
    const msg = await page.textContent('#ed-empty');
    if (msg.trim() !== 'Schermata di serie: aggiungi un elemento o parti dalla schermata di serie') problems.push('1280: messaggio della schermata vuota: ' + msg);
    await page.selectOption('#ed-screens', 'sleep');
    await page.waitForSelector('#ed-empty[hidden]', { state: 'attached' });
    if (!((await inkPixels(page)) > 0)) problems.push('1280: Riposo non disegnato');
    await ctx.close();
    // I1: con dpr non intero anteprima e griglia hanno la stessa scala intera s, senza ricampionamento
    for (const dpr of [1.25, 1.5]) {
      const c2 = await br.newContext({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: dpr });
      const pg = await c2.newPage();
      const tag = 'chromium-dpr' + dpr;
      watch(pg, tag, origin);
      await pg.goto(origin + '/editor.html');
      await pg.waitForSelector('#ed-screen[data-drawn]');
      await pg.selectOption('#ed-zoom', '3');
      const s = Math.round(3 * dpr);
      await pg.waitForFunction((w) => document.getElementById('ed-screen').width === w, 250 * s);
      const r = await pg.evaluate((s) => {
        const read = (id) => { const src = document.getElementById(id), c = document.createElement('canvas'); c.width = src.width; c.height = src.height;
          const g = c.getContext('2d', { willReadFrequently: true }); g.drawImage(src, 0, 0); return { d: g.getImageData(0, 0, c.width, c.height).data, w: c.width, h: c.height }; };
        const scr = read('ed-screen'), ov = read('ed-overlay');
        const rect = document.getElementById('ed-screen').getBoundingClientRect(), orect = document.getElementById('ed-overlay').getBoundingClientRect();
        // bordi delle corse d'inchiostro su ogni riga dell'anteprima: multipli di s
        let badEdge = 0, edges = 0;
        for (let y = 0; y < scr.h; y++) for (let x = 1; x < scr.w; x++) {
          const a = scr.d[(y * scr.w + x - 1) * 4] < 128, b = scr.d[(y * scr.w + x) * 4] < 128;
          if (a !== b) { edges++; if (x % s) badEdge++; }
        }
        // colonne della griglia su una riga a metà di un pixel della scheda: tutte e sole x = k·s
        const y = Math.floor(s / 2) + s * 60, cols = [];
        for (let x = 0; x < ov.w; x++) { const i = (y * ov.w + x) * 4; if (ov.d[i + 3] > 0 && ov.d[i] < 60 && ov.d[i + 1] < 60) cols.push(x); }
        return { cssW: rect.width, dpr: devicePixelRatio, canvasW: scr.w, ovW: ov.w, same: rect.left === orect.left && rect.top === orect.top && rect.width === orect.width,
          edges, badEdge, cols: cols.length, badCols: cols.filter((x) => x % s).length };
      }, s);
      if (r.ovW !== r.canvasW) problems.push(tag + ': canvas di dimensioni diverse ' + r.ovW + '/' + r.canvasW);
      if (!r.same) problems.push(tag + ': sovrapposizione non allineata alla tela');
      if (Math.abs(r.cssW * r.dpr - r.canvasW) > 0.5) problems.push(tag + ': ricampionato (' + r.cssW + ' px CSS · ' + r.dpr + ' ≠ ' + r.canvasW + ')');
      if (!r.edges || r.badEdge) problems.push(tag + ': pixel della scheda non uniformi (' + r.badEdge + '/' + r.edges + ' bordi fuori dalla scala ' + s + ')');
      if (r.cols !== 249 || r.badCols) problems.push(tag + ': griglia fuori dai bordi dei pixel (' + r.badCols + ' su ' + r.cols + ' colonne, scala ' + s + ')');
      await c2.close();
    }
    await canvasMouse(br, origin);
    await canvasDpr(br, origin);
    await layersMouse(br, origin);
    await propsMouse(br, origin);
    await topbarMouse(br, origin);
    await fullFlow(br, origin, false);
    await br.close(); mock.kill();
    // conflitti: un server finto a parte coi primi due salvataggi «cambiati altrove»
    const mc = await startMock(PORT + 2, ['--theme-conflict=2']);
    const bc = await chromium.launch();
    await conflictFlow(bc, 'http://127.0.0.1:' + (PORT + 2));
    await bc.close(); mc.kill();
    // invio non riuscito (F10: un server finto a parte, --theme-fail)
    const mf = await startMock(PORT + 3, ['--theme-fail']);
    const bf = await chromium.launch();
    await sendFail(bf, 'http://127.0.0.1:' + (PORT + 3), false);
    await bf.close();
    const wf = await webkit.launch();
    await sendFail(wf, 'http://127.0.0.1:' + (PORT + 3), true);
    await wf.close(); mf.kill();
  }

  // WebKit a 393×852 col tocco: cassetti
  {
    const mock = await startMock(PORT + 1);
    const o2 = 'http://127.0.0.1:' + (PORT + 1);
    const br = await webkit.launch();
    const ctx = await br.newContext({ viewport: { width: 393, height: 852 }, hasTouch: true, isMobile: true, deviceScaleFactor: 3 });
    const page = await ctx.newPage();
    watch(page, 'webkit-393', o2);
    await page.goto(o2 + '/editor.html');
    await common(page, 'webkit-393');
    const wide = () => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth);
    if (!(await wide())) problems.push('393: scorrimento orizzontale');
    for (const id of ['ed-open-left', 'ed-open-right']) {
      const b = await page.$eval('#' + id, (e) => { const r = e.getBoundingClientRect(); return [r.width, r.height]; });
      if (b[0] < 44 || b[1] < 44) problems.push('393: #' + id + ' sotto 44×44: ' + b.join('×'));
    }
    if (await page.isVisible('#ed-left')) problems.push('393: cassetto sinistro aperto all\'inizio');
    await page.tap('#ed-open-left');
    await page.waitForSelector('#ed-left[data-open]');
    if (!(await page.isVisible('#ed-left'))) problems.push('393: cassetto «Elementi» non visibile');
    if (!(await wide())) problems.push('393: scorrimento orizzontale col cassetto aperto');
    await page.tap('#ed-left .ed-close');
    await page.waitForSelector('#ed-left:not([data-open])');
    await page.tap('#ed-open-right');
    await page.waitForSelector('#ed-right[data-open]');
    await page.touchscreen.tap(20, 800);   // fuori dal cassetto di destra
    await page.waitForSelector('#ed-right:not([data-open])');
    if (!(await wide())) problems.push('393: scorrimento orizzontale alla fine');
    await ctx.close();
    await canvasTouch(br, o2);
    await layersTouch(br, o2);
    await propsTouch(br, o2);
    await topbarTouch(br, o2);
    await fullFlow(br, o2, true);
    await br.close(); mock.kill();
  }

  console.log(problems.length ? 'PROBLEMI: ' + problems.length : 'smoke-editor: ok');
  for (const p of problems) console.log('  ' + p);
  process.exit(problems.length ? 1 : 0);
}

run().catch((e) => { console.error(e); process.exit(2); });
