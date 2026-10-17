// API locale del servizio Calamaio (stessa origine, http://127.0.0.1:8765).

const POLL_MS = 2000;
const SSE_RETRY_MS = 30000;

async function readJSON(res) {
  const txt = await res.text();
  if (!txt) return {};
  try { return JSON.parse(txt); } catch (e) { return { error: txt.slice(0, 200) }; }
}

export async function getStatus() {
  const res = await fetch('/api/status', { cache: 'no-store' });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.json();
}

export async function getFirmware() {
  const res = await fetch('/api/firmware', { cache: 'no-store' });
  if (res.status === 404) return { available: false };
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.json();
}

// Risposta { status, body }: 202 con { pending }, 409/400 con { error }.
export async function postSettings(board, values) {
  const res = await fetch('/api/displays/' + encodeURIComponent(board) + '/settings', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(values)
  });
  return { status: res.status, body: await readJSON(res) };
}

export async function postFullRefresh(board) {
  const res = await fetch('/api/displays/' + encodeURIComponent(board) + '/full-refresh', { method: 'POST' });
  return { status: res.status, body: await readJSON(res) };
}

// Abbinamento (M5): op = 'start' | 'code' | 'cancel'. → { status, body }: 202 { pairing },
// 400 { error, aborted? } (codice sbagliato: aborted true), 409 { error }.
export async function postPair(board, op, body) {
  const res = await fetch('/api/displays/' + encodeURIComponent(board) + '/pair/' + op, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {})
  });
  return { status: res.status, body: await readJSON(res) };
}

// Stato in diretta: prova GET /api/events (SSE, evento «status»); se non c'è o cade,
// interroga /api/status ogni 2 s e riprova l'SSE ogni 30 s se aveva funzionato.
// onStatus(status) a ogni dato nuovo; onConn(mode) con mode = 'sse' | 'poll' | 'offline'.
export function watchStatus(onStatus, onConn) {
  let es = null, pollTimer = 0, retryTimer = 0, sseWorked = false, mode = '';
  const setMode = (m) => { if (m !== mode) { mode = m; onConn(m); } };

  async function pollOnce() {
    try { onStatus(await getStatus()); setMode(es && es.readyState === 1 ? 'sse' : 'poll'); }
    catch (e) { setMode('offline'); }
  }
  function startPolling() {
    if (pollTimer) return;
    pollOnce();
    pollTimer = setInterval(pollOnce, POLL_MS);
  }
  function stopPolling() { clearInterval(pollTimer); pollTimer = 0; }

  function startSSE() {
    if (!('EventSource' in window)) { startPolling(); return; }
    let opened = false;
    es = new EventSource('/api/events');
    es.addEventListener('open', () => { opened = true; sseWorked = true; stopPolling(); setMode('sse'); });
    es.addEventListener('status', (ev) => {
      try { onStatus(JSON.parse(ev.data)); setMode('sse'); } catch (e) { /* evento malformato: si aspetta il prossimo */ }
    });
    es.addEventListener('error', () => {
      // 404 o connessione caduta: si passa al polling (EventSource si richiuderebbe da solo
      // sui 404 e riproverebbe all'infinito sulle cadute).
      if (es) es.close();
      es = null;
      startPolling();
      if (opened || sseWorked) { clearTimeout(retryTimer); retryTimer = setTimeout(startSSE, SSE_RETRY_MS); }
    });
  }

  pollOnce();
  startSSE();
  return {
    refresh: pollOnce,
    stop() { stopPolling(); clearTimeout(retryTimer); if (es) es.close(); }
  };
}

/* ---------- backup della flash ---------- */

// { status, backups }: status 404 se il servizio non gestisce ancora i backup.
export async function listBackups() {
  const res = await fetch('/api/backups', { cache: 'no-store' });
  if (!res.ok) return { status: res.status, backups: [] };
  const body = await res.json();
  return { status: res.status, backups: Array.isArray(body.backups) ? body.backups : [] };
}

// meta: { name, chip, mac, board, flashSize, offset, note }; data: Uint8Array. → { status, body }
export async function uploadBackup(meta, data) {
  const q = new URLSearchParams();
  for (const [k, v] of Object.entries(meta)) if (v !== undefined && v !== null && v !== '') q.set(k, String(v));
  const res = await fetch('/api/backups?' + q.toString(), { method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: data });
  return { status: res.status, body: await readJSON(res) };
}

export const backupDataUrl = (id) => '/api/backups/' + encodeURIComponent(id) + '/data';

export async function fetchBackupData(id) {
  const res = await fetch(backupDataUrl(id), { cache: 'no-store' });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return new Uint8Array(await res.arrayBuffer());
}

export async function deleteBackup(id) {
  const res = await fetch('/api/backups/' + encodeURIComponent(id), { method: 'DELETE' });
  return { status: res.status, body: await readJSON(res) };
}

/* ---------- avatar delle sessioni (docs/AVATAR.md) ---------- */

async function postJSON(path, body, query) {
  const res = await fetch(path + (query || ''), {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: typeof body === 'string' ? body : JSON.stringify(body)
  });
  return { status: res.status, body: await readJSON(res) };
}

// { avatars: [{id, name, description, author, state, usedBy, default, source, frames, poses, crc, bytes, warnings}],
//   free: [id], default, current, problems: [{file, errors}], userDir, rev } · null se il servizio non ha avatar.
export async function listAvatars(session) {
  const res = await fetch('/api/avatars' + (session ? '?session=' + encodeURIComponent(session) : ''), { cache: 'no-store' });
  if (res.status === 404) return null;
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.json();
}

// id null = togli l'assegnazione (torna il predefinito).
export const assignSessionAvatar = (session, id) => postJSON('/api/sessions/' + encodeURIComponent(session) + '/avatar', { id });
export const assignProjectAvatar = (dir, id) => postJSON('/api/projects/avatar', { dir, id });
export const setDefaultAvatar = (id) => postJSON('/api/avatars/default', { id });
// text: contenuto del file .avatar.json. → { status: 201 | 400 | 409 | 413, body: {avatar, warnings, replaced} | {error, errors, warnings} }
export const uploadAvatar = (text, replace) => postJSON('/api/avatars', text, replace ? '?replace=1' : '');

/* ---------- aggiornamento senza cavo, registro e crash (docs/OTA.md) ---------- */

const boardPath = (board) => '/api/displays/' + encodeURIComponent(board);

// → { status: 202 | 409 | 503, body: { ota } | { error } }: offre il pacchetto firmware del servizio.
export const postOta = (board) => postJSON(boardPath(board) + '/ota', {});
export const postOtaCancel = (board) => postJSON(boardPath(board) + '/ota/cancel', {});

// Reti Wi-Fi della scheda. La password va solo qui (stessa origine, 127.0.0.1): il servizio la manda
// alla scheda cifrata. → { status: 200 | 400 | 404 | 409 | 504, body: WifiResult | { error } }; la risposta
// arriva dopo la conferma della scheda (al massimo ~10 s).
export const postWifiAdd = (board, ssid, password) => postJSON(boardPath(board) + '/wifi/add', { ssid, password });
export const postWifiForget = (board, ssid) => postJSON(boardPath(board) + '/wifi/forget', { ssid });

// Giornata di lavoro: action = 'start' | 'stop'. → { status: 202 | 400 | 409 | 503, body: { id, pending } | { error } };
// l'esito arriva poi in /api/status (displays[].workday.last), dopo la conferma della scheda.
export const postWorkday = (board, action) => postJSON(boardPath(board) + '/workday', { action });

// Registro da remoto: follow = righe nuove in diretta per minutes minuti (1–60).
export const postLogStart = (board, follow, minutes) => postJSON(boardPath(board) + '/log/start', { follow, minutes });
export const postLogStop = (board) => postJSON(boardPath(board) + '/log/stop', {});
export const logFileUrl = (board) => boardPath(board) + '/log/file';
export const crashUrl = (id) => '/api/crashes/' + encodeURIComponent(id);

// { lines: [{seq, at, text}], next, log } · con wait (secondi) aspetta righe nuove se il registro è attivo.
export async function getLog(board, after, wait, signal) {
  const res = await fetch(boardPath(board) + '/log?after=' + (after || 0) + (wait ? '&wait=' + wait : ''), { cache: 'no-store', signal });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.json();
}

/* ---------- gioco della mascotte e accessori ---------- */

// Tutto il gioco (gamesvc.Status) · null se il servizio non ha il gioco (404).
export async function getGame(days) {
  const res = await fetch('/api/game' + (days ? '?days=' + days : ''), { cache: 'no-store' });
  if (res.status === 404) return null;
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.json();
}
export const setGameOn = (on) => postJSON('/api/game', { on });
export const wearAccessory = (id) => postJSON('/api/game/wear', { id: id || null });
export const answerSpecial = (answer) => postJSON('/api/game/special', { answer });

// File JSON di un accessorio; cache per id + crc (il CRC cambia se l'accessorio viene sostituito).
const accCache = new Map();
export function loadAccessory(id, crc) {
  const key = id + ':' + crc;
  if (!accCache.has(key)) {
    accCache.set(key, fetch('/api/accessories/' + encodeURIComponent(id), { cache: 'no-cache' })
      .then((r) => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .catch((e) => { accCache.delete(key); throw e; }));
  }
  return accCache.get(key);
}

// Ancore di un avatar (docs/AVATAR.md §13): { avatar, avatarCrc, anchors } con anchors null se non ne ha.
const ancCache = new Map();
export function loadAnchors(id, crc) {
  const key = id + ':' + crc;
  if (!ancCache.has(key)) {
    ancCache.set(key, fetch('/api/avatars/' + encodeURIComponent(id) + '/anchors', { cache: 'no-cache' })
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error('HTTP ' + r.status))))
      .catch((e) => { ancCache.delete(key); throw e; }));
  }
  return ancCache.get(key);
}

/* ---------- telefoni abbinati (app iPhone; solo dalla webapp sul PC: dalla porta dei telefoni sono 404) ---------- */

// { status, body }: 200 { phones, pairing, app } · 404 servizio senza telefoni (o pagina aperta dall'app).
export async function listPhones() {
  const res = await fetch('/api/phones', { cache: 'no-store' });
  return { status: res.status, body: await readJSON(res) };
}

export const pairPhone = () => postJSON('/api/phones/pair', {});
export const cancelPhonePairing = () => postJSON('/api/phones/pair/cancel', {});

export async function revokePhone(id) {
  const res = await fetch('/api/phones/' + encodeURIComponent(id), { method: 'DELETE' });
  return { status: res.status, body: await readJSON(res) };
}

// expires nell'indirizzo: un QR nuovo per ogni finestra (niente cache del browser).
export const phoneQrUrl = (expires) => '/api/phones/pair/qr.svg?e=' + encodeURIComponent(expires);

/* ---------- temi dell'editor (docs/TEMI.md §8) ---------- */

// 400 con { error, errors: [{path, msg}] }: problems = errors del servizio
export class InvalidError extends Error {
  constructor(msg, problems) { super(msg); this.name = 'InvalidError'; this.problems = Array.isArray(problems) ? problems : []; }
}

async function themeFail(res) {
  const body = await readJSON(res);
  const msg = body.error || 'HTTP ' + res.status;
  if (res.status === 400) throw new InvalidError(msg, body.errors);
  // 404: tema sconosciuto (eliminato altrove, M3)
  if (res.status === 404) { const e = new Error(msg); e.name = 'NotFoundError'; throw e; }
  throw new Error(msg);
}

// → Info[] (404 = servizio senza temi)
export async function listThemes() {
  const res = await fetch('/api/themes', { cache: 'no-store' });
  if (res.status === 404) throw new Error('il servizio non gestisce ancora i temi');
  if (!res.ok) return themeFail(res);
  const body = await res.json();
  return Array.isArray(body.themes) ? body.themes : [];
}

// → { theme, base }: base = X-Calamaio-Updated (ms dell'ultima modifica), da rimandare con l'autosave (F8)
export async function getTheme(id) {
  const res = await fetch('/api/themes/' + encodeURIComponent(id), { cache: 'no-store' });
  if (!res.ok) return themeFail(res);
  const base = Number(res.headers.get('X-Calamaio-Updated'));
  return { theme: await res.json(), base: Number.isFinite(base) && base > 0 ? base : 0 };
}

// → Info del tema creato
export async function createTheme(theme) {
  const r = await fetch('/api/themes', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(theme) });
  if (!r.ok) return themeFail(r);
  return (await readJSON(r)).theme;
}

// → Info con problems e warnings (mai un errore per un tema non valido)
export async function checkTheme(theme) {
  const r = await fetch('/api/themes/check', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(theme) });
  if (!r.ok) return themeFail(r);
  return (await readJSON(r)).theme;
}

// 409 dell'autosave: il file è stato cambiato altrove; theme = Info del file vivo
export class ConflictError extends Error {
  constructor(msg, theme) { super(msg); this.name = 'ConflictError'; this.theme = theme || null; }
}

// Salvataggio automatico (PUT ?autosave=1&base=): il client manda SEMPRE la base letta (F8). → Info
// fresh: «Sovrascrivi», versione nuova invece dell'unione (quella dell'altro dispositivo resta in cronologia)
export async function putTheme(id, theme, { base, fresh = false } = {}) {
  // base 0 o assente = errore del client: il servizio lo prenderebbe per «crea», mai mandarlo
  if (!Number.isInteger(base) || base <= 0) throw new Error('autosave senza base: errore dell\'editor');
  const q = '?autosave=1&base=' + encodeURIComponent(String(base)) + (fresh ? '&fresh=1' : '');
  const r = await fetch('/api/themes/' + encodeURIComponent(id) + q, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(theme) });
  if (r.status === 409) { const b = await readJSON(r); throw new ConflictError(b.error || 'il tema è stato cambiato altrove', b.theme); }
  if (!r.ok) return themeFail(r);
  return (await readJSON(r)).theme;
}

// Importa un file .theme.json così com'è; l'id lo sceglie il servizio (se c'è già, -2, -3…). → Info
export async function importTheme(text) {
  const r = await fetch('/api/themes/import', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: String(text) });
  if (!r.ok) return themeFail(r);
  return (await readJSON(r)).theme;
}

export async function deleteTheme(id) {
  const r = await fetch('/api/themes/' + encodeURIComponent(id), { method: 'DELETE' });
  if (!r.ok) return themeFail(r);
}

// Invia un tema alla scheda: POST /api/displays/{board}/theme {id}. → ThemeJob (202; si segue in /api/status,
// displays[].themes.job). 409/404 e gli altri errori → Error col messaggio del servizio.
export async function sendTheme(board, id) {
  const r = await fetch(boardPath(board) + '/theme', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ id }) });
  const body = await readJSON(r);
  if (!r.ok) throw new Error(body.error || 'HTTP ' + r.status);
  return body.theme;
}
