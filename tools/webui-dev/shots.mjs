// Screenshot di tutte le pagine (1280 e 390 px) contro il server finto, con prove di interazione
// e controllo della console. Le immagini vanno in out/.   node shots.mjs
import { chromium, webkit } from 'playwright';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(here, 'out');
fs.mkdirSync(OUT, { recursive: true });
const WIDTHS = [1280, 390];
const problems = [];   // errori veri (falliscono la prova)
const expected = [];   // messaggi di rete attesi (409, 404 dell'SSE, servizio spento)

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

function watch(page, tag, allow = []) {
  page.on('pageerror', (e) => problems.push(tag + ' pageerror: ' + e.message));
  page.on('console', (m) => {
    if (m.type() !== 'error' && m.type() !== 'warning') return;
    const t = m.text();
    if (allow.some((re) => re.test(t))) expected.push(tag + ': ' + t);
    else problems.push(tag + ' console.' + m.type() + ': ' + t);
  });
}

async function shot(page, name, viewportOnly) {
  if (!viewportOnly) await page.evaluate(() => window.scrollTo(0, 0));
  await page.waitForTimeout(400);
  const file = path.join(OUT, name + '.png');
  await page.screenshot({ path: file, fullPage: !viewportOnly });
  console.log('  ' + path.relative(here, file));
}

async function run() {
  const chrome = await chromium.launch();
  const NET = [/Failed to load resource/, /net::ERR_/];

  // A. servizio con 2 display, SSE e pacchetto firmware
  const mockA = await startMock(8799);
  const A = 'http://127.0.0.1:8799/';
  for (const w of WIDTHS) {
    const ctx = await chrome.newContext({ viewport: { width: w, height: 900 }, deviceScaleFactor: 1 });
    const page = await ctx.newPage();
    watch(page, 'A' + w, NET);
    await page.goto(A);
    await page.waitForSelector('#sessions .session');
    await page.waitForFunction(() => document.getElementById('conn').dataset.mode === 'sse');
    await shot(page, `panoramica-${w}`);

    await page.goto(A + 'device.html?board=calamaio-3f2a');
    await page.waitForSelector('.set-item');
    await shot(page, `dispositivo-${w}`);

    await page.goto(A + 'device.html?board=calamaio-9c41');
    await page.waitForSelector('.set-item');
    await page.click('text=Esegui');
    await page.waitForSelector('#err:not(:empty)');
    await shot(page, `dispositivo-non-supportato-${w}`);

    await page.goto(A + 'firmware.html');
    await page.waitForSelector('#connect');
    await shot(page, `firmware-${w}`);
    await page.check('input[name="src"][value="pkg"]');
    await page.waitForFunction(() => /scaricate/.test(document.getElementById('pkg-info').textContent));
    await shot(page, `firmware-pacchetto-${w}`);

    // editor: tema «prova» aperto e un elemento scelto (la barra), con le maniglie e le Proprietà
    await page.goto(A + 'editor.html?test=1&theme=prova');
    await page.waitForSelector('#ed-screen[data-drawn]');
    await page.evaluate(() => { const s = window.__editor.store.get(), v = s.hist.present.screens[s.loc.screen].variants[s.loc.variant];
      window.__editor.A.select([(v.elements.find((e) => e.type === 'bar') ?? v.elements[0]).id]); });
    await page.waitForFunction(() => window.__editor.store.get().sel.length === 1);
    await page.waitForSelector('#ed-send:not([disabled])');
    await shot(page, `editor-${w}`);
    await ctx.close();
  }

  // Interazioni sulle impostazioni (1280): scelta → in attesa → confermata; permessi spenti; 400.
  {
    const ctx = await chrome.newContext({ viewport: { width: 1280, height: 900 } });
    const page = await ctx.newPage();
    watch(page, 'A-set', NET);
    await page.goto(A + 'device.html?board=calamaio-3f2a');
    await page.waitForSelector('.set-item');
    await page.click('label:has(input[name="set-anim"][value="continue"])');
    await page.waitForSelector('.pending-line:not([hidden])');
    const pend = await page.textContent('.pending-line:not([hidden])');
    if (!/Continue/.test(pend)) problems.push('pending: testo inatteso ' + pend);
    await shot(page, 'dispositivo-in-attesa-1280');
    await page.waitForFunction(() => /confermato/.test(document.getElementById('msg').textContent), null, { timeout: 8000 });
    if (!(await page.isChecked('input[name="set-anim"][value="continue"]'))) problems.push('anim non confermata a continue');
    await page.click('#set-save');
    await page.waitForFunction(() => /Risparmio energetico = sì/.test(document.getElementById('msg').textContent), null, { timeout: 8000 });
    await page.click('label[for="set-permOn"] >> xpath=..//input');
    await page.waitForFunction(() => /confermato/.test(document.getElementById('msg').textContent) && document.getElementById('set-permOn').disabled, null, { timeout: 8000 });
    if (await page.isChecked('#set-permOn')) problems.push('permOn ancora acceso');
    if (!(await page.isDisabled('#set-permOn'))) problems.push('permOn spento ma ancora modificabile');
    await shot(page, 'dispositivo-permessi-spenti-1280');
    // Il servizio rifiuta l'accensione da remoto anche se qualcuno la manda a mano.
    const r = await page.evaluate(async () => (await fetch('/api/displays/calamaio-3f2a/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"permOn":true}' })).status);
    if (r !== 400) problems.push('permOn true dal mock: atteso 400, avuto ' + r);
    // Il focus della tastiera resta sul controllo dopo gli aggiornamenti in diretta.
    await page.focus('input[name="set-speed"][value="massima"]');
    await page.waitForTimeout(6500);
    const fid = await page.evaluate(() => document.activeElement && document.activeElement.name + '=' + document.activeElement.value);
    if (fid !== 'set-speed=massima') problems.push('focus perso dopo un aggiornamento: ' + fid);
    await ctx.close();
  }
  mockA.kill();

  // B. nessun display, niente SSE (polling ogni 2 s), niente pacchetto firmware
  const mockB = await startMock(8798, ['--displays=0', '--no-sse', '--no-firmware']);
  const B = 'http://127.0.0.1:8798/';
  for (const w of WIDTHS) {
    const ctx = await chrome.newContext({ viewport: { width: w, height: 900 } });
    const page = await ctx.newPage();
    watch(page, 'B' + w, [...NET, /EventSource/]);
    await page.goto(B);
    await page.waitForFunction(() => document.getElementById('conn').dataset.mode === 'poll');
    await page.waitForSelector('#displays .empty');
    await shot(page, `panoramica-vuota-polling-${w}`);
    await page.goto(B + 'device.html');
    await page.waitForSelector('#info .empty');
    await shot(page, `dispositivo-nessuno-${w}`);
    await ctx.close();
  }
  {
    const ctx = await chrome.newContext({ viewport: { width: 1280, height: 900 } });
    const page = await ctx.newPage();
    watch(page, 'B-fw', NET);
    await page.goto(B + 'firmware.html');
    await page.check('input[name="src"][value="pkg"]');
    await page.waitForFunction(() => /non offre/.test(document.getElementById('pkg-info').textContent));
    await shot(page, 'firmware-senza-pacchetto-1280');
    await ctx.close();
  }
  mockB.kill();

  // C. servizio che si spegne: «servizio non raggiungibile» e Goccia in errore
  const mockC = await startMock(8797, ['--offline-after=3']);
  {
    const ctx = await chrome.newContext({ viewport: { width: 1280, height: 900 } });
    const page = await ctx.newPage();
    watch(page, 'C', NET);
    await page.goto('http://127.0.0.1:8797/');
    await page.waitForSelector('#sessions .session');
    await page.waitForFunction(() => document.getElementById('conn').dataset.mode === 'offline', null, { timeout: 15000 });
    await shot(page, 'panoramica-servizio-spento-1280');
    await ctx.close();
  }
  mockC.kill();
  await chrome.close();

  // D. WebKit (motore di Safari): niente Web Serial, la pagina lo spiega
  const mockD = await startMock(8796);
  const wk = await webkit.launch();
  for (const w of WIDTHS) {
    const ctx = await wk.newContext({ viewport: { width: w, height: 900 } });
    const page = await ctx.newPage();
    watch(page, 'D' + w, NET);
    await page.goto('http://127.0.0.1:8796/firmware.html');
    await page.waitForSelector('#noserial:not([hidden])');
    await shot(page, `firmware-webkit-senza-webserial-${w}`);
    await page.goto('http://127.0.0.1:8796/');
    await page.waitForSelector('#sessions .session');
    await shot(page, `panoramica-webkit-${w}`);
    await ctx.close();
  }
  await wk.close();
  mockD.kill();

  await backupFlows();

  console.log('\nmessaggi di rete attesi: ' + expected.length);
  for (const e of [...new Set(expected)]) console.log('  ' + e);
  console.log(problems.length ? '\nPROBLEMI: ' + problems.length : '\nnessun errore in console, prove superate');
  for (const p of problems) console.log('  ' + p);
  process.exit(problems.length ? 1 : 0);
}

// E. Backup e ripristino con un finto esptool (fake-esptool.mjs) e un finto navigator.serial:
// nessuna porta seriale vera viene aperta.
async function fakeSerial(ctx, mac) {
  await ctx.addInitScript((m) => {
    globalThis.__FAKE_MAC = m;
    const port = { getInfo: () => ({ usbVendorId: 0x10c4, usbProductId: 0xea60 }) };
    const serial = new EventTarget();
    serial.requestPort = async () => port;
    serial.getPorts = async () => [port];
    Object.defineProperty(Navigator.prototype, 'serial', { get: () => serial, configurable: true });
  }, mac);
  await ctx.route('**/vendor/esptool-js-0.7.0/esptool.js', (route) =>
    route.fulfill({ contentType: 'text/javascript', body: fs.readFileSync(path.join(here, 'fake-esptool.mjs'), 'utf8') }));
}

async function backupFlows() {
  // Un mock nuovo per ogni prova: l'elenco dei backup parte sempre da 3.
  const mocks = [];
  const fresh = async () => { const port = 8790 + mocks.length; mocks.push(await startMock(port)); return 'http://127.0.0.1:' + port + '/firmware.html'; };
  const chrome = await chromium.launch();
  const pose = (page) => page.getAttribute('.goccia canvas', 'data-pose');
  try {
  for (const w of WIDTHS) {
    const U = await fresh();
    const ctx = await chrome.newContext({ viewport: { width: w, height: 900 } });
    await fakeSerial(ctx, 'aa:bb:cc:dd:48:e8');
    const page = await ctx.newPage();
    watch(page, 'E' + w, [/Failed to load resource/]);
    await page.goto(U);
    await page.click('#connect');
    await page.waitForSelector('#chip:not([hidden])');
    await page.waitForSelector('#backups-count:has-text("3 salvati")');
    if (await page.getAttribute('#backups-box', 'open') !== null) problems.push('tendina dei backup aperta di default');
    await shot(page, `firmware-backup-chiuso-${w}`);
    await page.click('#backups-box > summary');
    await page.waitForSelector('.backup.factory');
    const first = await page.textContent('.backup-list .backup:first-child .t');
    if (!/Originale di fabbrica/.test(first)) problems.push('il backup di fabbrica non è in cima: ' + first);
    if (await page.$$eval('.backup.factory [data-delete]', (x) => x.length)) problems.push('il backup di fabbrica ha Elimina');
    await shot(page, `firmware-backup-aperto-${w}`);

    // Ripristino di un backup di un'altra scheda: avviso, FORZA + SCRIVI, poi la modale del backup.
    await page.click('[data-restore="u-full-4110"]');
    await page.waitForSelector('#dlg-restore[open] #rs-other:not([hidden])');
    await page.fill('#rs-confirm', 'SCRIVI');
    if (!(await page.isDisabled('#rs-ok'))) problems.push('ripristino su altra scheda abilitato senza FORZA');
    await shot(page, `firmware-ripristino-altra-scheda-${w}`, true);
    await page.fill('#rs-force', 'FORZA');
    if (await page.isDisabled('#rs-ok')) problems.push('ripristino non abilitato con FORZA e SCRIVI');
    await page.click('#rs-ok');
    await page.waitForSelector('#dlg-prebackup[open]');
    const appTxt = await page.textContent('#pb-app-text');
    if (!/app1/.test(appTxt) || !/0x340000/.test(appTxt)) problems.push('app attiva non riconosciuta: ' + appTxt);
    await shot(page, `firmware-modale-backup-${w}`, true);
    await page.click('#pb-app');
    await page.waitForSelector('#restore-result .alert.ok', { timeout: 30000 });
    if ((await pose(page)) !== 'done') problems.push('Goccia non in posa done dopo il ripristino');
    await page.waitForSelector('#backups-count:has-text("4 salvati")');
    if (!(await page.$('.backup:has-text("Solo programma (app1)")'))) problems.push('backup del programma non in elenco');
    await shot(page, `firmware-ripristino-fatto-${w}`);
    await ctx.close();
  }

  // Scrittura del pacchetto con backup completo prima; eliminazione; 403 sul protetto.
  {
    const U = await fresh();
    const ctx = await chrome.newContext({ viewport: { width: 1280, height: 900 } });
    await fakeSerial(ctx, 'aa:bb:cc:dd:48:e8');
    const page = await ctx.newPage();
    watch(page, 'E-write', [/Failed to load resource/]);
    await page.goto(U);
    await page.click('#connect');
    await page.waitForSelector('#chip:not([hidden])');
    await page.check('input[name="src"][value="pkg"]');
    await page.waitForFunction(() => /scaricate/.test(document.getElementById('pkg-info').textContent));
    await page.fill('#confirm', 'SCRIVI');
    await page.click('#write');
    await page.waitForSelector('#dlg-prebackup[open]');
    await page.click('#pb-full');
    await page.waitForFunction(() => document.querySelector('.goccia canvas').dataset.pose === 'working');
    await page.waitForSelector('#write-result .alert.ok', { timeout: 30000 });
    if ((await pose(page)) !== 'done') problems.push('Goccia non done dopo la scrittura');
    await page.click('#backups-box > summary');
    await page.waitForSelector('#backups-count:has-text("4 salvati")');
    await shot(page, 'firmware-scrittura-fatta-1280');
    await page.click('[data-delete="u-app-040"]');
    await page.waitForSelector('#dlg-delete[open]');
    await shot(page, 'firmware-elimina-conferma-1280', true);
    await page.click('#dlg-delete button[value="delete"]');
    await page.waitForSelector('#backups-count:has-text("3 salvati")');
    const st = await page.evaluate(async () => (await fetch('/api/backups/factory', { method: 'DELETE' })).status);
    if (st !== 403) problems.push('DELETE sul protetto: atteso 403, avuto ' + st);
    // Esc sulla modale del backup = scrittura annullata, niente scritto.
    await page.click('#connect');
    await page.waitForSelector('#chip:not([hidden])');
    await page.fill('#confirm', 'SCRIVI');
    await page.click('#write');
    await page.waitForSelector('#dlg-prebackup[open]');
    await page.keyboard.press('Escape');
    await page.waitForTimeout(300);
    if (await page.isVisible('#write-progress') && /Scrittura di/.test(await page.textContent('#write-progress'))) problems.push('scrittura partita dopo Esc');
    await ctx.close();
  }
  } finally {
    await chrome.close();
    mocks.forEach((m) => m.kill());
  }
}

run().catch((e) => { console.error(e); process.exit(2); });
