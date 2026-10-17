// Server finto per sviluppare la webapp senza il servizio Go: serve service/internal/webui/static/
// e risponde alle API con dati coerenti con protocol/examples/state.json. Nessuna dipendenza.
//
//   node mock-server.mjs [--port=8799] [--displays=2|1|0] [--no-sse] [--no-firmware] [--no-backups] [--offline-after=SEC] [--theme-fail] [--theme-conflict=N]
//
// Display finti: calamaio-3f2a (Heltec, impostazioni remote supportate) e calamaio-9c41
// (CrowPanel 4,2″ con firmware vecchio: settings null, le POST rispondono 409).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(here, '../../service/internal/webui/static');
const argv0 = (n) => { const a = process.argv.find((x) => x.startsWith('--' + n + '=')); return a ? a.slice(n.length + 3) : ''; };
// --state=FILE: un altro state finto (tools/screenshots/demo-state.json); --host=NOME: nome del PC mostrato
const STATE_JSON = argv0('state') ? path.resolve(argv0('state')) : path.resolve(here, '../../protocol/examples/state.json');
const HOST = argv0('host') || 'mac-sviluppo';

const arg = (name, def) => {
  const a = process.argv.find((x) => x === '--' + name || x.startsWith('--' + name + '='));
  if (!a) return def;
  return a.includes('=') ? a.split('=')[1] : true;
};
const PORT = parseInt(arg('port', '8799'), 10);
const N_DISPLAYS = parseInt(arg('displays', '2'), 10);
const NO_SSE = !!arg('no-sse', false);
const NO_FW = !!arg('no-firmware', false);
const NO_BACKUPS = !!arg('no-backups', false);
const MAX_BACKUP = 32 * 1024 * 1024; // oltre: 413
const THEME_FAIL = !!arg('theme-fail', false); // l'ultima fase dell'invio di un tema è «errore» (HTTP 410)
const MAX_THEME = 256 * 1024; // oltre: 413
// i primi N salvataggi automatici trovano il tema «cambiato altrove» (nome «Dal telefono») e rispondono 409
let themeConflicts = (() => { const v = arg('theme-conflict', false); return v === true ? 1 : v ? parseInt(v, 10) || 0 : 0; })();
const CONFIRM_MS = 2500; // tempo finto in cui il display conferma un'impostazione

/* ---------- stato finto ---------- */
const now = Date.now();
const raw = JSON.parse(fs.readFileSync(STATE_JSON, 'utf8'));
const shift = now - raw.time; // l'esempio è del 2026-10-11 09:50 UTC: lo portiamo ad adesso
const sh = (t) => (typeof t === 'number' ? t + shift : t);
const state = {
  time: now,
  sessions: raw.sessions.map((s) => ({ ...s, since: sh(s.since), attnAt: sh(s.attnAt) })),
  limits: raw.limits ? { h5: raw.limits.h5, h5Reset: sh(raw.limits.h5Reset), d7: raw.limits.d7, d7Reset: sh(raw.limits.d7Reset) } : null,
  today: raw.today,
  log: raw.log.map((l) => ({ ...l, t: sh(l.t) })),
  services: raw.services, prs: raw.prs,
  agenda: raw.agenda.map((a) => ({ ...a, t: sh(a.t) }))
};

const DISPLAYS = [
  { board: 'calamaio-3f2a', device: 'heltec213', class: 'S', fw: '0.4.0', w: 250, h: 122, ip: '192.168.1.42', since: now - 25 * 60000,
    // abbinata e autenticata (M5): la webapp comanda le impostazioni solo con remote e authed
    remote: true, authed: true,
    settings: { homeStyle: 'goccia', anim: 'auto', speed: 'profilo', save: false, invert: false, rot: false, night: false, permOn: true, mute: 'off' }, settingsPending: {},
    themes: { supported: true, have: [], cur: null, job: null } },
  { board: 'calamaio-9c41', device: 'crow42', class: 'M', fw: '0.3.0', w: 400, h: 300, ip: '192.168.1.57', since: now - 3 * 3600000 - 7 * 60000,
    remote: false, authed: false,
    settings: null, settingsPending: {}, themes: { supported: false, have: [], cur: null, job: null } }
].slice(0, N_DISPLAYS);

const VALID = {
  homeStyle: ['goccia', 'righe', 'contatori', 'una'],
  anim: ['auto', 'raffiche', 'continue', 'spente'],
  speed: ['profilo', 'massima'],
  mute: ['off', '1h', 'night'],
  save: 'bool', invert: 'bool', rot: 'bool', night: 'bool', permOn: 'bool', sound: 'bool', led: 'bool'
};

function status() {
  return {
    service: { version: '0.1.0-mock', host: HOST, tz: 'CET-1CEST,M3.5.0,M10.5.0/3', wsPort: 8766 },
    displays: DISPLAYS,
    state: { ...state, time: Date.now() }
  };
}

/* ---------- SSE ---------- */
const clients = new Set();
function broadcast() {
  const msg = 'event: status\ndata: ' + JSON.stringify(status()) + '\n\n';
  for (const res of clients) res.write(msg);
}
setInterval(() => { for (const res of clients) res.write(': ping\n\n'); }, 15000);

// Le sessioni cambiano ogni 6 s, così si vede l'aggiornamento in diretta.
const SCRIPT = [
  ['c3', 'working', 'Read lib/epd/driver.cpp'],
  ['c3', 'compact', 'Riordina la memoria'],
  ['c3', 'working', 'Edit src/ui/home.cpp'],
  ['a1', 'input', 'Domanda: quale formato per le date?'],
  ['a1', 'working', 'Edit src/routes/orders.ts'],
  ['c3', 'done', '4 task completati']
];
let step = 0;
setInterval(() => {
  const [id, st, detail] = SCRIPT[step++ % SCRIPT.length];
  const s = state.sessions.find((x) => x.id === id);
  if (!s) return;
  s.state = st; s.detail = detail; s.since = Date.now();
  s.attnAt = st === 'input' || st === 'permission' ? Date.now() : null;
  if (st === 'done') s.summary = 'Driver del pannello pronto e layout 400×300 provato.';
  broadcast();
}, 6000);

/* ---------- firmware finto ---------- */
function fakePart(name, size, seed) {
  const b = Buffer.alloc(size);
  let x = seed >>> 0;
  for (let i = 0; i < size; i++) { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; b[i] = x & 0xff; }
  if (name !== 'partizioni' && name !== 'boot_app0') { b[0] = 0xe9; b[12] = 9; b[13] = 0; } // intestazione ESP32-S3
  if (name === 'partizioni') { b[0] = 0xaa; b[1] = 0x50; }
  return b;
}
const FW_PARTS = [
  { name: 'bootloader', offset: 0x0, data: fakePart('bootloader', 15104, 1) },
  { name: 'partizioni', offset: 0x8000, data: fakePart('partizioni', 3072, 2) },
  { name: 'boot_app0', offset: 0xe000, data: fakePart('boot_app0', 8192, 3) },
  { name: 'app', offset: 0x10000, data: fakePart('app', 412160, 4) }
];
const firmwareManifest = () => NO_FW ? { available: false } : {
  available: true, version: '0.4.0', chip: 'ESP32-S3',
  parts: FW_PARTS.map((p) => ({ name: p.name, offset: p.offset, url: '/api/firmware/parts/' + p.name + '.bin', size: p.data.length,
    sha256: crypto.createHash('sha256').update(p.data).digest('hex') }))
};

/* ---------- backup finti ---------- */
// Contenuto finto: 0xFF con qualche byte riconoscibile (non è una flash vera).
function fakeFlash(len, seed) {
  const b = Buffer.alloc(len, 0xff);
  for (let i = 0; i < len; i += 4096) b.writeUInt32LE((seed * 2654435761 + i) >>> 0, i);
  return b;
}
const MB8 = 8 * 1024 * 1024;
const backups = [];
let mockPhones = [{ id: 'a1b2c3d4e5f6', name: 'iPhone di Prova', paired: Date.now() - 3 * 86400000, lastUsed: Date.now() - 5 * 60000 }];
let mockPairing = null;
function addBackup(meta, data) {
  const b = { id: meta.id || crypto.randomBytes(6).toString('hex'), name: meta.name, kind: meta.kind || 'user', protected: !!meta.protected,
    board: meta.board || '', chip: meta.chip || '', mac: meta.mac || '', flashSize: Number(meta.flashSize) || 0,
    offset: Number(meta.offset) || 0, length: data.length, size: data.length,
    sha256: crypto.createHash('sha256').update(data).digest('hex'), created: meta.created || Date.now(), note: meta.note || '' };
  backups.push({ meta: b, data });
  return b;
}
if (!NO_BACKUPS) {
  addBackup({ id: 'factory', name: 'Originale di fabbrica', kind: 'factory', protected: true, board: 'calamaio-48e8', chip: 'ESP32-S3', mac: 'aa:bb:cc:dd:48:e8',
    flashSize: MB8, offset: 0, created: Date.parse('2026-10-11T08:12:00+02:00'), note: 'Flash completa com’era alla consegna (esptool read-flash a 115200 baud).' }, fakeFlash(MB8, 1));
  addBackup({ id: 'u-app-040', name: 'Solo programma (ota_0) prima di 0.4.0', board: 'calamaio-48e8', chip: 'ESP32-S3', mac: 'aa:bb:cc:dd:48:e8',
    flashSize: MB8, offset: 0x10000, created: now - 70 * 60000, note: 'prima di scrivere il firmware 0.4.0 del servizio' }, fakeFlash(0x330000, 2));
  addBackup({ id: 'u-full-4110', name: 'Backup completo della seconda Heltec', board: 'calamaio-4110', chip: 'ESP32-S3', mac: 'f4:12:fa:9c:41:10',
    flashSize: MB8, offset: 0, created: now - 22 * 3600000, note: 'seconda Heltec, prima delle prove' }, fakeFlash(MB8, 3));
}

function readBinary(req, limit) {
  return new Promise((resolve) => {
    const chunks = []; let n = 0, over = false;
    req.on('data', (c) => { n += c.length; if (n > limit) over = true; else chunks.push(c); });
    req.on('end', () => resolve(over ? null : Buffer.concat(chunks)));
  });
}


/* ---------- temi finti (stesse forme di service/internal/api/themes.go) ---------- */
const REPO = path.resolve(here, '../..');
const LOCKED = new Set(['perm', 'pair', 'confirm', 'boot', 'ota', 'battlow', 'calib', 'gbload', 'gbplay']);
const LATER = new Set(['list', 'wifiScan', 'qr', 'connecting', 'storico', 'guest', 'servizi', 'agenda', 'game']);
const themes = new Map(); // id → {text, updated, versions: [{n, at, bytes}], auto: {n, at} | null}
let themeClock = 0;
const touch = () => { themeClock = Math.max(Date.now(), themeClock + 1); return themeClock; };
const AUTOSAVE_MERGE = 120000;
// Come Store: ogni scrittura fa una versione; l'autosave riscrive quella creata da un altro autosave < 2 minuti fa
// (il segno vale per il servizio, non per chi scrive). fresh («Sovrascrivi») toglie il segno: versione nuova.
function addTheme(text, { auto = false, fresh = false } = {}) {
  const t = JSON.parse(text);
  const pretty = JSON.stringify(t, null, 2) + '\n';
  const old = themes.get(t.id);
  const it = { text: pretty, updated: touch(), versions: old ? old.versions : [], auto: old && !fresh ? old.auto : null };
  const last = it.versions.at(-1);
  const bytes = Buffer.byteLength(pretty);
  if (auto && it.auto && last && last.n === it.auto.n && it.updated - it.auto.at < AUTOSAVE_MERGE) {
    last.bytes = bytes; last.at = it.updated;
  } else {
    const n = (last?.n ?? 0) + 1;
    it.versions = [...it.versions, { n, at: it.updated, bytes }].slice(-5);
    it.auto = auto ? { n, at: it.updated } : null;
  }
  themes.set(t.id, it);
}
try { addTheme(fs.readFileSync(path.join(REPO, 'protocol/themes/fixtures/prova.theme.json'), 'utf8')); } catch (e) { console.log('mock: tema prova non trovato'); }

// Come Store.Check: problemi e avvisi dentro, mai un errore. I byte per schermata sono stimati (JSON / 2).
function themeInfo(text, updated) {
  const info = { id: '', name: '', screen: '', crc: 0, bytes: 0, screens: {}, updated: updated ?? Date.now(), warnings: [], problems: [] };
  let t;
  try { t = JSON.parse(text); } catch (e) { info.problems.push({ path: '', msg: 'JSON non valido: ' + e.message }); return info; }
  if (!t || typeof t !== 'object' || Array.isArray(t)) { info.problems.push({ path: '', msg: 'serve un oggetto JSON' }); return info; }
  const bad = (p, msg) => info.problems.push({ path: p, msg });
  info.id = typeof t.id === 'string' ? t.id : ''; info.name = typeof t.name === 'string' ? t.name : ''; info.screen = typeof t.screen === 'string' ? t.screen : '';
  if (t.format !== 'calamaio-theme') bad('format', 'deve essere «calamaio-theme»');
  if (!/^[a-z0-9]+(-[a-z0-9]+)*$/.test(info.id) || info.id.length > 24) bad('id', 'id non valido (minuscole, cifre e trattini, al massimo 24)');
  else if (info.id === 'calamaio') bad('id', '«calamaio» è il tema di serie');
  if (!info.name || Buffer.byteLength(info.name) > 24) bad('name', 'nome da 1 a 24 byte');
  if (!t.screens || typeof t.screens !== 'object' || !Object.keys(t.screens).length) bad('screens', 'nessuna schermata');
  else {
    for (const [k, v] of Object.entries(t.screens)) {
      if (LOCKED.has(k)) { bad('screens.' + k, 'schermata bloccata: regole di sicurezza (Permessi, abbinamento e sistema restano di serie)'); continue; }
      if (LATER.has(k)) { bad('screens.' + k, 'schermata non ancora personalizzabile'); continue; }
      info.screens[k] = Math.round(JSON.stringify(v).length / 2);
    }
    info.bytes = Object.values(info.screens).reduce((a, b) => a + b, 0);
  }
  info.crc = crypto.createHash('sha1').update(text).digest().readUInt32BE(0);
  if (info.problems.length) { info.screens = {}; info.bytes = 0; info.crc = 0; }
  return info;
}
const themeInfoOf = (id) => themeInfo(themes.get(id).text, themes.get(id).updated);
const themeBad = (res, info) => json(res, 400, { error: info.problems.map((x) => (x.path ? x.path + ': ' : '') + x.msg).join('; '), errors: info.problems });
const bodyTooBig = (text) => Buffer.byteLength(text) > MAX_THEME;

async function themesApi(req, res, url, p) {
  if (p === '/api/themes' && req.method === 'GET')
    return json(res, 200, { themes: [...themes.keys()].sort().map(themeInfoOf), dir: '/mock/calamaio/themes' });
  if ((p === '/api/themes' || p === '/api/themes/import' || p === '/api/themes/check') && req.method === 'POST') {
    const text = await readBody(req);
    if (bodyTooBig(text)) return json(res, 413, { error: 'tema oltre 256 KB' });
    if (p.endsWith('/check')) return json(res, 200, { theme: themeInfo(text) });
    const info = themeInfo(text);
    if (info.problems.length) return themeBad(res, info);
    const t = JSON.parse(text);
    if (p.endsWith('/import')) {
      let id = t.id;
      for (let k = 2; themes.has(id); k++) { if (k > 99) return json(res, 409, { error: "c'è già un tema con questo id" }); const suf = '-' + k; id = t.id.slice(0, 24 - suf.length).replace(/-+$/, '') + suf; }
      t.id = id;
    } else if (themes.has(t.id)) return json(res, 409, { error: "c'è già un tema con questo id" });
    addTheme(JSON.stringify(t));
    return json(res, 201, { theme: themeInfoOf(t.id) });
  }
  const mh = /^\/api\/themes\/([^/]+)\/history$/.exec(p);
  if (mh && req.method === 'GET') {
    let id; try { id = decodeURIComponent(mh[1]); } catch (e) { return json(res, 404, { error: 'tema sconosciuto' }); }
    const it = themes.get(id);
    if (!it) return json(res, 404, { error: 'tema sconosciuto' });
    return json(res, 200, { versions: [...it.versions].reverse().map(({ n, at, bytes }) => ({ n, at, bytes })) });
  }
  const m = /^\/api\/themes\/([^/]+)$/.exec(p);
  if (!m) return false;
  let id;
  try { id = decodeURIComponent(m[1]); } catch (e) { return json(res, 404, { error: 'tema sconosciuto' }); }
  if (req.method === 'GET') {
    const it = themes.get(id);
    if (!it) return json(res, 404, { error: 'tema sconosciuto' });
    const crc = themeInfoOf(id).crc.toString(16).padStart(8, '0');
    res.writeHead(200, { 'Content-Type': 'application/json', ETag: '"' + crc + '"', 'X-Calamaio-Crc': crc,
      'X-Calamaio-Updated': String(it.updated), 'Cache-Control': 'no-cache' });
    return res.end(it.text);
  }
  if (req.method === 'PUT') {
    if (id === 'calamaio') return json(res, 409, { error: '«calamaio» è il tema di serie: non si cambia' });
    const text = await readBody(req);
    if (bodyTooBig(text)) return json(res, 413, { error: 'tema oltre 256 KB' });
    const info = themeInfo(text);
    if (!info.problems.length && info.id !== id) info.problems.push({ path: 'id', msg: "l'id nel file (\"" + info.id + "\") non è quello del percorso (\"" + id + "\")" });
    if (info.problems.length) return themeBad(res, info);
    if (url.searchParams.get('autosave') === '1') {
      const base = Number(url.searchParams.get('base') || 0);
      if (!Number.isInteger(base) || base < 0) return json(res, 400, { error: "base: millisecondi dell'ultima modifica" });
      if (base > 0) {
        if (!themes.has(id)) return json(res, 404, { error: 'tema sconosciuto' });
        if (themeConflicts > 0 && themes.get(id).updated === base) {
          themeConflicts--;
          addTheme(JSON.stringify({ ...JSON.parse(themes.get(id).text), name: 'Dal telefono' }), { auto: true });
        }
        if (themes.get(id).updated !== base) return json(res, 409, { error: 'il tema è stato cambiato altrove', theme: themeInfoOf(id) });
      }
      addTheme(text, { auto: true, fresh: url.searchParams.get('fresh') === '1' });
      return json(res, 200, { theme: themeInfoOf(id) });
    }
    addTheme(text);
    return json(res, 200, { theme: themeInfoOf(id) });
  }
  if (req.method === 'DELETE') {
    if (id === 'calamaio') return json(res, 409, { error: '«calamaio» è il tema di serie: non si cambia' });
    if (!themes.delete(id)) return json(res, 404, { error: 'tema sconosciuto' });
    res.writeHead(204); return res.end();
  }
  return false;
}

// Invio finto alla scheda: attesa → offerta → scaricato → uso → fatto (o errore), ogni 300 ms.
function sendTheme(d, id) {
  const it = id === null ? null : themes.get(id);
  const info = it ? themeInfoOf(id) : null;
  const job = { id: crypto.randomBytes(4).toString('hex'), board: d.board, theme: id, name: info ? info.name : 'Calamaio', crc: info ? info.crc : 0,
    stage: 'attesa', started: Date.now(), updated: Date.now() };
  d.themes.job = job;
  const steps = ['offerta', 'scaricato', 'uso', THEME_FAIL ? 'errore' : 'fatto'];
  const tick = () => {
    const st = steps.shift();
    job.stage = st; job.updated = Date.now();
    if (st === 'errore') job.error = 'download non riuscito (HTTP 410)';
    if (st === 'fatto') { d.themes.cur = id; d.themes.have = id && !d.themes.have.includes(id) ? [...d.themes.have, id] : d.themes.have; }
    broadcast();
    if (steps.length) setTimeout(tick, 300);
  };
  setTimeout(tick, 300);
  broadcast();
  return job;
}

// Avatar, ancore e accessori dalla cartella avatars/ del repo (la pagina li chiede per disegnare l'anteprima).
function avatarFiles(req, res, p) {
  if (req.method !== 'GET') return false;
  const safe = (x) => /^[a-z0-9-]+$/.test(x);
  let m = /^\/api\/avatars\/([^/]+)$/.exec(p);
  if (m && safe(m[1])) {
    const f = path.join(REPO, 'avatars', m[1] + '.avatar.json');
    if (!fs.existsSync(f)) return json(res, 404, { error: 'avatar sconosciuto' });
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-cache' });
    return res.end(fs.readFileSync(f));
  }
  m = /^\/api\/avatars\/([^/]+)\/anchors$/.exec(p);
  if (m && safe(m[1])) {
    if (!fs.existsSync(path.join(REPO, 'avatars', m[1] + '.avatar.json'))) return json(res, 404, { error: 'avatar sconosciuto' });
    const f = path.join(REPO, 'avatars/ancore', m[1] + '.anchors.json');
    if (!fs.existsSync(f)) return json(res, 200, { avatar: m[1], avatarCrc: '', from: '', anchors: null });
    const a = JSON.parse(fs.readFileSync(f, 'utf8'));
    return json(res, 200, { avatar: m[1], avatarCrc: a.avatarCrc, from: 'ancore/' + m[1] + '.anchors.json', anchors: a.anchors });
  }
  m = /^\/api\/accessories\/([^/]+)$/.exec(p);
  if (m && safe(m[1])) {
    const f = path.join(REPO, 'avatars/accessori', m[1] + '.accessory.json');
    if (!fs.existsSync(f)) return json(res, 404, { error: 'accessorio sconosciuto' });
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-cache' });
    return res.end(fs.readFileSync(f));
  }
  return false;
}

/* ---------- HTTP ---------- */
const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.woff2': 'font/woff2', '.svg': 'image/svg+xml', '.png': 'image/png', '.txt': 'text/plain; charset=utf-8', '.json': 'application/json' };

function json(res, code, body) {
  res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(body === undefined ? '' : JSON.stringify(body));
}

function readBody(req) {
  return new Promise((resolve) => { let d = ''; req.on('data', (c) => { d += c; }); req.on('end', () => resolve(d)); });
}

async function api(req, res, url) {
  const p = url.pathname;
  if (req.method === 'GET' && p === '/api/status') return json(res, 200, status());
  if (req.method === 'GET' && p === '/api/events') {
    if (NO_SSE) return json(res, 404, { error: 'not found' });
    res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-store', Connection: 'keep-alive' });
    res.write('event: status\ndata: ' + JSON.stringify(status()) + '\n\n');
    clients.add(res);
    req.on('close', () => clients.delete(res));
    return;
  }
  if (req.method === 'GET' && p === '/api/firmware') return json(res, 200, firmwareManifest());
  const part = /^\/api\/firmware\/parts\/([a-z_0-9]+)\.bin$/.exec(p);
  if (req.method === 'GET' && part) {
    const fp = FW_PARTS.find((x) => x.name === part[1]);
    if (!fp || NO_FW) return json(res, 404, { error: 'parte sconosciuta' });
    res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': fp.data.length });
    return res.end(fp.data);
  }
  // Telefoni abbinati (un telefono finto; la finestra dura 5 minuti come nel servizio).
  if (p === '/api/phones' || p.startsWith('/api/phones/')) {
    const view = () => ({ phones: mockPhones, pairing: mockPairing && mockPairing.expires > Date.now() ? mockPairing : null,
      app: { port: 8767, listening: true, fingerprint: 'ab'.repeat(32), hosts: ['192.168.1.20'] } });
    if (req.method === 'GET' && p === '/api/phones') return json(res, 200, view());
    if (req.method === 'POST' && p === '/api/phones/pair') {
      mockPairing = { link: 'calamaio://pair?v=1&h=192.168.1.20&p=8767&fp=' + 'ab'.repeat(32) + '&c=' + 'cd'.repeat(16) + '&n=PC%20finto', expires: Date.now() + 300000 };
      return json(res, 200, view());
    }
    if (req.method === 'POST' && p === '/api/phones/pair/cancel') { mockPairing = null; return json(res, 200, view()); }
    if (req.method === 'GET' && p === '/api/phones/pair/qr.svg') {
      res.writeHead(200, { 'Content-Type': 'image/svg+xml', 'Cache-Control': 'no-store' });
      return res.end('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 29 29"><rect width="29" height="29" fill="#fff"/><path d="M4 4h7v7h-7z"/></svg>');
    }
    const m = /^\/api\/phones\/([^/]+)$/.exec(p);
    if (req.method === 'DELETE' && m) {
      const n = mockPhones.length;
      mockPhones = mockPhones.filter((x) => x.id !== decodeURIComponent(m[1]));
      if (mockPhones.length === n) return json(res, 404, { error: 'telefono sconosciuto' });
      res.writeHead(204); return res.end();
    }
    return json(res, 404, { error: 'non trovato' });
  }
  if (p === '/api/backups' || p.startsWith('/api/backups/')) {
    if (NO_BACKUPS) return json(res, 404, { error: 'not found' });
    if (req.method === 'GET' && p === '/api/backups') return json(res, 200, { backups: backups.map((x) => x.meta) });
    if (req.method === 'POST' && p === '/api/backups') {
      if ((req.headers['content-type'] || '') !== 'application/octet-stream') return json(res, 400, { error: 'Serve Content-Type application/octet-stream.' });
      const q = url.searchParams;
      if (!q.get('name') || !q.get('chip')) return json(res, 400, { error: 'Mancano name o chip.' });
      const data = await readBinary(req, MAX_BACKUP);
      if (data === null) return json(res, 413, { error: 'Backup troppo grande.' });
      if (!data.length) return json(res, 400, { error: 'Backup vuoto.' });
      const b = addBackup(Object.fromEntries(q), data);
      return json(res, 201, { backup: b });
    }
    const bm = /^\/api\/backups\/([^/]+)(\/data)?$/.exec(p);
    const item = bm && backups.find((x) => x.meta.id === decodeURIComponent(bm[1]));
    if (!item) return json(res, 404, { error: 'Backup sconosciuto.' });
    if (req.method === 'GET' && bm[2]) {
      res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': item.data.length });
      return res.end(item.data);
    }
    if (req.method === 'DELETE' && !bm[2]) {
      if (item.meta.protected) return json(res, 403, { error: 'Backup protetto.' });
      backups.splice(backups.indexOf(item), 1);
      res.writeHead(204); return res.end();
    }
    return json(res, 405, { error: 'metodo non ammesso' });
  }
  if (p.startsWith('/api/themes')) { const r = await themesApi(req, res, url, p); if (r !== false) return r; }
  if (p.startsWith('/api/avatars/') || p.startsWith('/api/accessories/')) { const r = avatarFiles(req, res, p); if (r !== false) return r; }
  const tm = /^\/api\/displays\/([^/]+)\/theme$/.exec(p);
  if (req.method === 'POST' && tm) {
    const d = DISPLAYS.find((x) => x.board === decodeURIComponent(tm[1]));
    if (!d || !d.themes.supported) return json(res, 409, { error: 'Display non collegato o senza temi.' });
    let body;
    try { body = JSON.parse(await readBody(req)); } catch (e) { body = null; }
    if (!body || typeof body !== 'object' || Array.isArray(body) || !('id' in body)) return json(res, 400, { error: 'serve un oggetto JSON {"id": "tema" | null}' });
    if (body.id !== null && typeof body.id !== 'string') return json(res, 400, { error: 'id: una stringa o null' });
    if (body.id !== null && !themes.has(body.id)) return json(res, 404, { error: 'tema sconosciuto' });
    return json(res, 202, { theme: sendTheme(d, body.id) });
  }
  const m = /^\/api\/displays\/([^/]+)\/(settings|full-refresh)$/.exec(p);
  if (req.method === 'POST' && m) {
    const d = DISPLAYS.find((x) => x.board === decodeURIComponent(m[1]));
    if (!d) return json(res, 404, { error: 'Display non collegato.' });
    if (!d.settings) return json(res, 409, { error: 'Il display ' + d.board + ' (firmware ' + d.fw + ') non supporta ancora le impostazioni remote: arriveranno con un prossimo firmware.' });
    if (m[2] === 'full-refresh') return json(res, 202, {});
    let body;
    try { body = JSON.parse(await readBody(req)); } catch (e) { return json(res, 400, { error: 'JSON non valido.' }); }
    if (!body || typeof body !== 'object' || Array.isArray(body) || !Object.keys(body).length) return json(res, 400, { error: 'Serve un oggetto {chiave: valore}.' });
    for (const [k, v] of Object.entries(body)) {
      const rule = VALID[k];
      if (!rule || !(k in d.settings)) return json(res, 400, { error: 'Impostazione sconosciuta per questo display: ' + k });
      if (rule === 'bool' ? typeof v !== 'boolean' : !rule.includes(v)) return json(res, 400, { error: 'Valore non valido per ' + k + '.' });
      if (k === 'permOn' && v === true) return json(res, 400, { error: '«Permessi dal display» si accende solo dal tasto sul display.' });
    }
    Object.assign(d.settingsPending, body);
    broadcast();
    setTimeout(() => {
      for (const [k, v] of Object.entries(body)) { if (d.settingsPending[k] === v) { d.settings[k] = v; delete d.settingsPending[k]; } }
      broadcast();
    }, CONFIRM_MS);
    return json(res, 202, { pending: body });
  }
  return json(res, 404, { error: 'not found' });
}

function serveStatic(req, res, url) {
  let rel = decodeURIComponent(url.pathname);
  if (rel.endsWith('/')) rel += 'index.html';
  const file = path.resolve(ROOT, '.' + rel);
  if (!file.startsWith(ROOT + path.sep)) { res.writeHead(403); return res.end(); }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }); return res.end('404'); }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  if (url.pathname.startsWith('/api/')) return api(req, res, url).catch((e) => json(res, 500, { error: String(e) }));
  if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405); return res.end(); }
  serveStatic(req, res, url);
});
server.listen(PORT, '127.0.0.1', () => {
  console.log('webui mock su http://127.0.0.1:' + PORT + '/  (display: ' + DISPLAYS.length + ', sse: ' + !NO_SSE + ', firmware: ' + !NO_FW + ')');
});
const offAfter = parseInt(arg('offline-after', '0'), 10);
if (offAfter) setTimeout(() => { console.log('mock: spengo per provare «servizio non raggiungibile»'); for (const r of clients) r.destroy(); server.close(); server.closeAllConnections(); }, offAfter * 1000);
