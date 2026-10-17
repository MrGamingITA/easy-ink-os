// Panoramica: display collegati, sessioni, limiti. Dati da /api/status (SSE o polling).
import { watchStatus, listAvatars, assignSessionAvatar, assignProjectAvatar } from './api.js';
import { h, heroGoccia, setConn } from './ui.js';
import { stateIcon, Goccia } from './goccia.js';
import { cachedAvatar, ensureAvatar } from './avatar.js';
import { STATES, deviceName, DEVICES, isAttn, urgentIdx, pose, ago, hm, resetLabel, extras, stateText, workdayText } from './model.js';

const goccia = heroGoccia('idle', 'In attesa del servizio');
const $ = (id) => document.getElementById(id);
let last = null, connMode = '', lastAt = 0;
// Libreria degli avatar (GET /api/avatars), riletta quando cambia status.avatars.rev.
let lib = null, libRev = '', libLoading = false, deferred = false;
const avErr = new Map(); // sessione → errore dell'ultima assegnazione
function sinceText(t) {
  if (!t) return '—';
  const d = new Date(t);
  const same = d.toDateString() === new Date().toDateString();
  return 'da ' + ago(t) + ' (' + (same ? 'alle ' : d.toLocaleDateString('it-IT', { day: 'numeric', month: 'short' }) + ' ') + hm(d) + ')';
}

function renderService(st) {
  const s = st.service || {};
  const bits = ['Servizio Calamaio', s.version ? 'versione ' + s.version : null, s.host || null, s.wsPort ? 'WebSocket :' + s.wsPort : null];
  $('svc').textContent = bits.filter(Boolean).join(' · ');
}

function renderDisplays(list) {
  const box = $('displays');
  box.replaceChildren();
  if (!list.length) {
    box.append(h('div', { class: 'empty' },
      h('strong', null, 'Nessun display collegato'),
      'Accendi il display: trova il PC da solo in rete locale (mDNS, servizio _calamaio._tcp) e compare qui appena si collega.'));
    return;
  }
  for (const d of list) {
    const dev = DEVICES[d.device];
    const pending = d.settingsPending && Object.keys(d.settingsPending).length;
    const wd = workdayText(d.workday); // riga solo con la giornata accesa
    box.append(h('article', { class: 'note' },
      h('div', { class: 'card-head' },
        h('span', { class: 't' }, d.board),
        h('span', { class: 'badge soft' }, 'classe ' + (d.class || (dev && dev.cls) || '?'))),
      h('dl', { class: 'kv' },
        h('dt', null, 'Modello'), h('dd', null, deviceName(d.device)),
        h('dt', null, 'Schermo'), h('dd', null, d.w && d.h ? d.w + '×' + d.h : '—'),
        h('dt', null, 'Indirizzo'), h('dd', { class: 'mono' }, d.ip || '—'),
        h('dt', null, 'Firmware'), h('dd', { class: 'mono' }, d.fw || '—'),
        h('dt', null, 'Collegato'), h('dd', null, sinceText(d.since)),
        wd ? [h('dt', null, 'Giornata'), h('dd', { class: 'wd-left', 'data-board': d.board }, wd)] : null,
        h('dt', null, 'Impostazioni'), h('dd', null, d.settings ? (pending ? 'in attesa di conferma dal display' : 'modificabili da qui') : 'non ancora supportate dal firmware')),
      h('div', { class: 'card-actions' },
        h('a', { class: 'btn small secondary', href: 'device.html?board=' + encodeURIComponent(d.board) }, 'Impostazioni del display'))));
  }
}

function taskCount(s) {
  const tasks = Array.isArray(s.tasks) ? s.tasks : [];
  return { total: tasks.length, done: tasks.filter((t) => t[1] === 2).length };
}

function baseName(p) { return (p || '').replace(/[\\/]+$/, '').split(/[\\/]/).pop() || p; }

// Figura animata della sessione: il suo avatar (o la Goccia finché il file non è arrivato).
function sessionFigure(s, i, limits) {
  const cv = h('canvas', { class: 'avatar-px' });
  const g = new Goccia(cv, pose(limits, s), { phase: i * 3, label: 'Goccia' });
  if (s.avatar) {
    const av = cachedAvatar(s.avatar, s.avatarCrc);
    if (av) g.setAvatar(av);
    else ensureAvatar(s.avatar, s.avatarCrc).then((a) => g.setAvatar(a)).catch(() => {});
  }
  return cv;
}

// Selettore dell'avatar di una sessione: avatar (con libero / in uso) e a cosa si applica.
function avatarControls(s, sessions) {
  if (!lib) return null;
  const others = sessions.filter((o) => o.id !== s.id && !o.archived);
  const usedBy = (id) => others.filter((o) => o.avatar === id).map((o) => o.name);
  const def = lib.avatars.find((a) => a.default);
  const own = s.avatarFrom === 'sessione' || s.avatarFrom === 'progetto';
  const scopeSel = h('select', { id: 'scope-' + s.id, 'aria-label': 'Vale per' },
    h('option', { value: 'progetto', disabled: !s.project }, 'per il progetto'),
    h('option', { value: 'sessione' }, 'solo questa sessione'));
  scopeSel.value = s.avatarFrom === 'sessione' || !s.project ? 'sessione' : 'progetto';
  const opts = [h('option', { value: '' }, 'Predefinito' + (def ? ' · ' + def.name : ''))];
  for (const a of lib.avatars) {
    const u = usedBy(a.id);
    const tag = a.default ? 'predefinito' : u.length ? 'in uso: ' + u.join(', ') : 'libero';
    opts.push(h('option', { value: a.id }, a.name + ' · ' + tag));
  }
  const sel = h('select', { id: 'av-' + s.id }, opts);
  sel.value = own ? s.avatar : '';
  const err = avErr.get(s.id);
  const from = s.avatarFrom === 'sessione' ? 'scelto per questa sessione'
    : s.avatarFrom === 'progetto' ? 'dal progetto ' + baseName(s.project)
    : 'predefinito: nessun avatar scelto per ' + (s.project ? 'il progetto ' + baseName(s.project) : 'la sessione');
  sel.addEventListener('change', async () => {
    const id = sel.value || null;
    sel.disabled = scopeSel.disabled = true;
    const r = scopeSel.value === 'progetto' ? await assignProjectAvatar(s.project, id) : await assignSessionAvatar(s.id, id);
    if (r.status === 200) avErr.delete(s.id); else avErr.set(s.id, r.body.error || ('errore ' + r.status));
    sel.disabled = scopeSel.disabled = false;
    sel.blur();
    if (deferred || r.status !== 200) { deferred = false; render(last); }
  });
  return h('div', { class: 'avatar-ctl' },
    h('label', { for: 'av-' + s.id }, 'Avatar'), sel, scopeSel,
    h('p', { class: 'meta' }, from),
    err ? h('p', { class: 'alert error', role: 'alert' }, err) : null);
}

function renderSessions(sessions, limits) {
  const box = $('sessions');
  box.replaceChildren();
  if (!sessions.length) {
    box.append(h('div', { class: 'empty' }, h('strong', null, 'Nessuna sessione aperta'),
      'Le sessioni di Claude Code compaiono qui quando gli hook del plugin Calamaio mandano i primi eventi.'));
    return;
  }
  sessions.forEach((s, i) => {
    const st = STATES[s.state] || STATES.idle, attn = isAttn(s), tc = taskCount(s);
    const meta = [tc.total ? tc.done + '/' + tc.total + ' task' : 'senza task', ago(s.since)].concat(extras(s));
    if (s.ctx !== null && s.ctx !== undefined) meta.push('contesto ' + s.ctx + '%');
    if (attn && s.attnAt) meta.push('aspetta da ' + ago(s.attnAt));
    const what = stateText(s) + (s.state === 'error' && s.error ? ' (' + s.error + ')' : '');
    const showDetail = s.state !== 'working' && s.detail && !(s.state === 'done' && s.summary);
    const badge = h('span', { class: 'badge' + (attn ? ' attn' : '') }, stateIcon(s.state, attn ? '#FFFFFF' : '#14181C'), st.label);
    box.append(h('article', { class: 'note session' + (attn ? ' attn' : '') + (s.archived ? ' archived' : ''), 'aria-label': s.name + ', ' + st.label },
      h('div', { class: 'session-row' },
        sessionFigure(s, i, limits),
        h('div', { class: 'session-main' },
          h('div', { class: 'card-head' }, h('span', { class: 't' }, s.name), badge),
          h('p', { class: 'what' }, what),
          showDetail ? h('p', { class: 'meta' }, s.detail) : null)),
      tc.total ? h('div', { class: 'bar', role: 'img', 'aria-label': 'Task completati: ' + tc.done + ' su ' + tc.total },
        h('span', { style: { width: (100 * tc.done / tc.total).toFixed(1) + '%' } })) : null,
      h('p', { class: 'meta' }, meta.flatMap((m, k) => (k ? ['  ·  ', h('span', null, m)] : [h('span', null, m)]))),
      avatarControls(s, sessions)));
  });
}

// Rilegge la libreria quando cambia (nuovo avatar, predefinito cambiato).
async function refreshLib(st) {
  const rev = st.avatars ? st.avatars.rev : '';
  if (!st.avatars) { lib = null; return; }
  if (rev === libRev || libLoading) return;
  libLoading = true;
  try { lib = await listAvatars(); libRev = rev; } catch (e) { /* si riprova al prossimo stato */ }
  libLoading = false;
  if (last) render(last);
}

function renderLimits(L) {
  const box = $('limits');
  box.replaceChildren();
  const rows = [];
  if (L && typeof L.h5 === 'number') rows.push(['Ultime 5 ore', L.h5, L.h5Reset]);
  if (L && typeof L.d7 === 'number') rows.push(['Ultimi 7 giorni', L.d7, L.d7Reset]);
  if (!rows.length) {
    box.append(h('div', { class: 'empty' }, h('strong', null, 'Limiti non disponibili'),
      'Arrivano dalla status line di Claude Code, solo con i piani Pro e Max e dopo la prima risposta della sessione.'));
    return;
  }
  for (const [label, pct, reset] of rows) {
    const v = Math.max(0, Math.min(100, Math.round(pct)));
    box.append(h('article', { class: 'note limit' },
      h('div', { class: 'row' }, h('h3', null, label), h('span', { class: 'pct' }, v + '%')),
      h('div', { class: 'bar big', role: 'img', 'aria-label': label + ': ' + v + '% usato' }, h('span', { style: { width: v + '%' } })),
      h('p', { class: 'muted' }, reset ? resetLabel(reset) : 'ora di azzeramento non nota')));
  }
}

function renderGoccia(st) {
  if (connMode === 'offline') { goccia.set('error', 'Il servizio sul PC non risponde'); return; }
  const displays = st.displays || [], sessions = (st.state && st.state.sessions) || [];
  const dflt = st.state || {};
  if (!displays.length || !sessions.length) heroAvatar(dflt.defaultAvatar, dflt.defaultAvatarCrc);
  if (!displays.length) { goccia.set('idle', 'Nessun display collegato'); return; }
  if (!sessions.length) { goccia.set('idle', 'Nessuna sessione aperta'); return; }
  const s = sessions[urgentIdx(sessions)];
  const p = pose(st.state.limits, s);
  heroAvatar(s.avatar, s.avatarCrc);
  goccia.set(p, s.name + ' · ' + (p === 'tired' ? 'limite 5 ore oltre il 90%' : (STATES[s.state] || STATES.idle).long.toLowerCase()));
}

// La figura in alto è quella della sessione più urgente, come la Home del display; senza sessioni
// il predefinito.
function heroAvatar(id, crc) {
  if (!id) { goccia.setAvatar(null); return; }
  const av = cachedAvatar(id, crc);
  if (av) { goccia.setAvatar(av); return; }
  ensureAvatar(id, crc).then((a) => goccia.setAvatar(a)).catch(() => {});
}

// Avviso discreto se il servizio non parte da solo al login (M6: calamaio install).
function renderAutostart(a) {
  const box = $('autostart');
  const show = !!(a && a.supported && (!a.installed || a.exeMissing));
  box.hidden = !show;
  if (!show) return;
  box.replaceChildren(a.installed
    ? 'L’avvio automatico punta a un binario che non c’è più: '
    : 'Il servizio non parte da solo al login: ', h('code', null, 'calamaio install'));
}

function render(st) {
  if (st !== last) lastAt = Date.now();
  last = st;
  refreshLib(st);
  renderService(st);
  renderAutostart(st.autostart);
  renderDisplays(st.displays || []);
  const state = st.state || {};
  // Mentre si sceglie un avatar non si ridisegna (la tendina aperta si chiuderebbe): si rimanda.
  const focus = document.activeElement;
  if (focus && focus.tagName === 'SELECT' && $('sessions').contains(focus)) deferred = true;
  else renderSessions(state.sessions || [], state.limits);
  renderLimits(state.limits);
  renderGoccia(st);
}

watchStatus(render, (mode) => {
  connMode = mode;
  setConn(mode);
  const stale = $('stale');
  stale.hidden = mode !== 'offline';
  document.body.classList.toggle('stale', mode === 'offline' && !!last);
  if (mode === 'offline') stale.textContent = 'Il servizio Calamaio sul PC non risponde. ' + (last ? 'I dati qui sotto sono fermi alle ' + hm(new Date(lastAt)) + '.' : 'Controlla che sia avviato (calamaio serve).') + ' La pagina riprova da sola.';
  if (last || mode === 'offline') renderGoccia(last || {});
});
$('sessions').addEventListener('focusout', () => {
  setTimeout(() => { if (deferred && last && !$('sessions').contains(document.activeElement)) { deferred = false; render(last); } }, 0);
});
// Conto alla rovescia della giornata: solo il testo, ogni 5 s (senza ridisegnare le schede).
setInterval(() => {
  if (!last) return;
  for (const el of document.querySelectorAll('.wd-left')) {
    const d = (last.displays || []).find((x) => x.board === el.dataset.board);
    const t = d && workdayText(d.workday);
    if (t && el.textContent !== t) el.textContent = t;
  }
}, 5000);
// I tempi relativi («da 12 min») si aggiornano anche senza dati nuovi.
setInterval(() => { if (last) render(last); }, 30000);
