// Firmware › Aggiornamento senza cavo (docs/OTA.md §4): schede collegate con versione attuale → nuova,
// batteria e cavo, motivo se non si può, «Aggiorna» con conferma, avanzamento in diretta dagli
// ota-status (SSE di /api/events) con la mascotte predefinita che corre sul fronte della barra (posa run,
// docs/AVATAR.md §12.2; ripiego working), esito. Il controllo vero lo fa la scheda subito prima di partire.
import { watchStatus, getFirmware, postOta, postOtaCancel } from './api.js';
import { h, fmtBytes } from './ui.js';
import { loadAvatar, drawAvatar } from './avatar.js';

const $ = (id) => document.getElementById(id);
const INK = '#1E2022', PAPER = '#D6D7CF';
const RECENT_MS = 10 * 60 * 1000; // un esito resta in vista per 10 minuti

const STAGE = {
  offerta: 'Offerta mandata: la scheda controlla firma, batteria e cavo',
  ricevo: 'Ricezione',
  verifico: 'Verifica dell’immagine',
  scrivo: 'Scrittura: non spegnere la scheda',
  riavvio: 'Riavvio: aspetto che la scheda torni',
  fatto: 'Aggiornata',
  rifiutato: 'Rifiutato dalla scheda',
  errore: 'Non riuscito',
  annullato: 'Annullato'
};

let last = null, pkg = null, pkgAt = 0, avatar = null, avatarId = '';
const cards = new Map(); // board → { el, sig, parts }
let onGoccia = () => {};
let gocciaState = null;
let busy = new Set();

const yesNo = (v) => (v === true ? 'collegato' : v === false ? 'staccato' : 'non si sa');
const battText = (b) => (b === null || b === undefined ? 'non si sa' : b + '%');

function showErr(t) { $('ota-err').textContent = t || ''; }

async function refreshPkg(force) {
  if (!force && pkg && Date.now() - pkgAt < 10000) return;
  pkgAt = Date.now();
  try { pkg = await getFirmware(); } catch (e) { pkg = null; }
  const info = $('ota-pkg');
  if (!pkg || !pkg.available) {
    info.className = 'alert info';
    info.textContent = 'Il servizio non ha un pacchetto firmware da mandare' + (pkg && pkg.problem ? ' (' + pkg.problem + ')' : '') +
      '. Avvia il servizio con «calamaio serve --firmware DIR», oppure manda una build dal terminale con «calamaio ota push --build DIR».';
  } else {
    info.className = 'hint';
    info.textContent = 'Pacchetto del servizio: firmware ' + (pkg.version || '?') + ' per ' + (pkg.chip || '?') +
      (pkg.built ? ', compilato il ' + new Date(pkg.built).toLocaleString('it-IT') : '') + '.';
  }
}

async function ensureAvatar(st) {
  const id = (st.avatars && st.avatars.default) || 'goccia';
  if (id === avatarId) return;
  avatarId = id;
  try { avatar = await loadAvatar(id, st.avatars && st.avatars.rev); } catch (e) { avatar = null; }
}

// Fotogrammi della corsa: run, se l'avatar ce l'ha, altrimenti working (§12.1).
const runFrames = () => (avatar && avatar.poses ? avatar.poses.run || avatar.poses.working : null);

// Barra con la figura sul fronte: un fotogramma nuovo a ogni passo del 5% (non a tempo), bordo destro
// della figura sul fronte della parte completata, ferma a sinistra finché non c'è spazio.
function makeTrack() {
  const fill = h('span');
  const bar = h('div', { class: 'bar big accent', role: 'progressbar', 'aria-valuemin': '0', 'aria-valuemax': '100', 'aria-valuenow': '0' }, fill);
  const cv = h('canvas', { class: 'ota-runner', 'aria-hidden': 'true' });
  const track = h('div', { class: 'ota-track' }, cv, bar);
  let step = -1;
  return {
    el: track,
    set(pct, label) {
      fill.style.width = pct.toFixed(1) + '%';
      bar.setAttribute('aria-valuenow', String(Math.floor(pct)));
      bar.setAttribute('aria-label', label);
      const frs = runFrames();
      if (!frs) { cv.hidden = true; return; }
      cv.hidden = false;
      const k = Math.floor(pct / 5);
      const w = avatar.w || 28, hh = avatar.h || 25;
      if (cv.width !== w || cv.height !== hh) { cv.width = w; cv.height = hh; step = -1; }
      if (k !== step) {
        step = k;
        const c = cv.getContext('2d');
        c.clearRect(0, 0, w, hh);
        drawAvatar(c, { ...avatar, poses: { run: frs } }, 0, 0, 1, 'run', k, INK, PAPER);
      }
      const tw = track.clientWidth, fw = cv.clientWidth || w * 2;
      const left = Math.max(0, Math.min(tw - fw, (tw * pct) / 100 - fw));
      cv.style.left = left + 'px';
    }
  };
}

function checkLine(d) {
  const ck = d.otaCheck;
  if (!ck) return h('p', { class: 'set-note' }, 'Il servizio non fa ancora aggiornamenti remoti.');
  if (!ck.can) return h('p', { class: 'set-note' }, ck.why);
  if (ck.warn) return h('p', { class: 'set-help' }, ck.warn);
  return h('p', { class: 'set-help' }, 'Pronta: la scheda ricontrolla batteria e cavo subito prima di partire.');
}

function card(board) {
  let c = cards.get(board);
  if (c) return c;
  const el = h('article', { class: 'note ota-card', 'aria-labelledby': 'ota-' + board });
  c = { el, sig: '' };
  cards.set(board, c);
  return c;
}

async function start(d) {
  const dlg = $('dlg-ota');
  $('ota-dlg-text').textContent = 'Aggiorno ' + d.board + ' da ' + (d.fw || '?') + ' a ' + (pkg && pkg.version ? pkg.version : '?') +
    '. La scheda scarica il firmware via Wi‑Fi, controlla firma e SHA-256 e si riavvia; se la versione nuova non parte torna da sola a quella di prima.';
  $('ota-dlg-warn').textContent = d.otaCheck && d.otaCheck.warn ? d.otaCheck.warn : '';
  dlg.returnValue = '';
  const v = await new Promise((resolve) => { dlg.addEventListener('close', () => resolve(dlg.returnValue), { once: true }); dlg.showModal(); });
  if (v !== 'ok') return;
  busy.add(d.board); showErr(''); render();
  try {
    const r = await postOta(d.board);
    if (r.status !== 202) showErr((r.body && r.body.error) || 'Aggiornamento non partito (HTTP ' + r.status + ').');
  } catch (e) { showErr('Il servizio sul PC non risponde: aggiornamento non partito.'); }
  busy.delete(d.board); render();
}

async function cancel(board) {
  busy.add(board); render();
  try {
    const r = await postOtaCancel(board);
    if (r.status !== 202) showErr((r.body && r.body.error) || 'Annullamento non riuscito (HTTP ' + r.status + ').');
  } catch (e) { showErr('Il servizio sul PC non risponde.'); }
  busy.delete(board); render();
}

function jobOf(board) {
  const jobs = (last && last.ota && last.ota.jobs) || [];
  return jobs.find((j) => j.board === board) || null;
}

function renderCard(board, d, job) {
  const c = card(board);
  const active = job && job.active;
  const recent = job && !job.active && job.ended && Date.now() - job.ended < RECENT_MS;
  const sig = JSON.stringify([board, d && [d.fw, d.batt, d.usb, d.cal, d.authed, d.otaCheck], job && [job.id, job.stage, job.active, job.error, job.cancelable],
    recent, busy.has(board), pkg && pkg.version]);
  if (c.sig !== sig) {
    c.sig = sig;
    const kids = [h('div', { class: 'card-head' }, h('span', { class: 't', id: 'ota-' + board }, board),
      h('span', { class: 'badge soft' }, d ? 'collegata' : 'non collegata'))];
    kids.push(h('dl', { class: 'kv' },
      h('dt', null, 'Versione'), h('dd', { class: 'mono' }, (d ? d.fw : job ? job.from : '?') + ' → ' + (active || recent ? job.to : pkg && pkg.version ? pkg.version : '?')),
      h('dt', null, 'Batteria'), h('dd', null, d && d.status ? battText(d.batt) + (d.cal ? ' · calibrazione in corso' : '') : 'non si sa'),
      h('dt', null, 'Cavo USB'), h('dd', null, d && d.status ? yesNo(d.usb) : 'non si sa')));
    c.track = null;
    if (active) {
      c.track = makeTrack();
      c.phase = h('div', { class: 'phase' });
      c.nums = h('div', { class: 'nums', 'aria-live': 'off' });
      kids.push(h('div', { class: 'progress' }, c.phase, c.track.el, c.nums));
      if (job.cancelable) kids.push(h('div', { class: 'card-actions' }, h('button', { type: 'button', class: 'btn secondary', disabled: busy.has(board), onclick: () => cancel(board) }, 'Annulla')));
      else kids.push(h('p', { class: 'set-note' }, 'Non spegnere la scheda: da qui l’aggiornamento non si annulla più.'));
    } else {
      if (recent) {
        const ok = job.stage === 'fatto';
        kids.push(h('p', { class: 'alert ' + (ok ? 'ok' : 'error'), role: 'status' }, ok ? 'Aggiornata alla versione ' + job.to + '.'
          : STAGE[job.stage] + (job.error ? ': ' + job.error : '') + '. Resta la versione di prima.'));
      }
      if (d) {
        kids.push(checkLine(d));
        const can = d.otaCheck && d.otaCheck.can && !busy.has(board);
        kids.push(h('div', { class: 'card-actions' }, h('button', { type: 'button', class: 'btn', disabled: !can, 'aria-disabled': can ? null : 'true', onclick: () => start(d) }, 'Aggiorna')));
      }
    }
    c.el.replaceChildren(...kids);
  }
  if (active && c.track) {
    const pct = job.total ? Math.min(100, (100 * job.done) / job.total) : 0;
    c.phase.textContent = STAGE[job.stage] || job.stage;
    c.track.set(pct, STAGE[job.stage] || job.stage);
    c.nums.replaceChildren(h('span', null, fmtBytes(job.done) + ' di ' + fmtBytes(job.total)), h('span', null, Math.floor(pct) + '%'));
  }
  return c.el;
}

function render() {
  if (!last) return;
  const list = last.displays || [];
  const boards = new Set(list.map((d) => d.board));
  for (const j of (last.ota && last.ota.jobs) || []) if (j.active || (j.ended && Date.now() - j.ended < RECENT_MS)) boards.add(j.board);
  const els = [...boards].sort().map((b) => renderCard(b, list.find((d) => d.board === b) || null, jobOf(b)));
  for (const b of [...cards.keys()]) if (!boards.has(b)) cards.delete(b);
  if (!els.length) els.push(h('div', { class: 'empty' }, h('strong', null, 'Nessuna scheda collegata'), 'Accendi il display e aspetta che si colleghi al servizio.'));
  $('ota-list').replaceChildren(...els);
  const k = $('ota-key');
  k.hidden = !last.ota || last.ota.key;
  if (last.ota && !last.ota.key) k.textContent = 'Manca la chiave di firma sul PC: nel terminale esegui «calamaio ota keygen». ' + (last.ota.keyError || '');
  updateGoccia();
}

function updateGoccia() {
  const jobs = (last && last.ota && last.ota.jobs) || [];
  const act = jobs.find((j) => j.active);
  const done = jobs.filter((j) => !j.active && j.ended && Date.now() - j.ended < 15000).sort((a, b) => b.ended - a.ended)[0];
  let g = null;
  if (act) {
    const pct = act.total ? Math.floor((100 * act.done) / act.total) : 0;
    g = act.stage === 'offerta' ? { pose: 'input', text: act.board + ': la scheda decide' }
      : act.stage === 'riavvio' ? { pose: 'working', text: act.board + ' si riavvia' }
      : { pose: 'working', text: 'Aggiornamento ' + act.board + ': ' + pct + '%' };
  } else if (done) {
    g = done.stage === 'fatto' ? { pose: 'done', text: done.board + ' aggiornata a ' + done.to } : { pose: 'error', text: 'Aggiornamento ' + STAGE[done.stage].toLowerCase() };
  }
  const s = JSON.stringify(g);
  if (s !== JSON.stringify(gocciaState)) { gocciaState = g; onGoccia(); }
}

// initOta avvia la sezione. notify() viene chiamata quando cambia la posa suggerita per la Goccia.
export function initOta(notify) {
  onGoccia = notify || (() => {});
  refreshPkg(true).then(render);
  watchStatus((st) => {
    last = st;
    ensureAvatar(st).then(render);
    refreshPkg(false).then(render);
    render();
  }, () => {});
  setInterval(() => { if (last) render(); }, 5000); // gli esiti escono di scena anche senza eventi
  return { goccia: () => gocciaState };
}
