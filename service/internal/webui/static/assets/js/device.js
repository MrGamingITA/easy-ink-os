// Dispositivo: impostazioni remote di un display (voci dei menu Display e Avvisi del simulatore),
// abbinamento e stato dei permessi dal display (M5, docs/M5-PERMESSI.md), reti Wi-Fi della scheda,
// giornata di lavoro (conto alla rovescia tenuto dalla scheda).
// Regola di sicurezza (brief §7): «Permessi dal display» da qui si può solo spegnere.
import { watchStatus, postSettings, postFullRefresh, postPair, postLogStart, postLogStop, getLog, logFileUrl, crashUrl, postWifiAdd, postWifiForget, postWorkday, listPhones, pairPhone, cancelPhonePairing, revokePhone, phoneQrUrl } from './api.js';
import { h, heroGoccia, setConn } from './ui.js';
import { DEVICES, deviceName, HOME_LABEL, ANIM_LABEL, SPEED_LABEL, MUTE_LABEL, ago, hm, workdayLeft, workdayText } from './model.js';
import { countdown, lastUsedText, pairedText, appStateText } from './phones.js';

const goccia = heroGoccia('idle', 'In attesa del servizio');
const $ = (id) => document.getElementById(id);
const yes = (v) => (v ? 'sì' : 'no');
const profileName = (dev) => (dev.fps >= 3 ? 'fluide' : dev.fps === 2 ? 'ridotte' : 'a pose');

let boardId = new URLSearchParams(location.search).get('board');
let view = null;            // { sig, board, controls: Map(key → update()) }
let current = null;         // display selezionato (dall'ultimo /api/status)
let prevPending = {};       // settingsPending precedente, per capire cosa il display ha confermato
const localPending = {};    // chiave → { value, at }: inviato con 202, in attesa che lo stato lo riporti
const busy = new Set();     // chiavi con una richiesta in corso
let lastErrorAt = 0, lastOkAt = 0, connMode = '';
let lastStatus = null;      // ultimo /api/status (richieste di permesso aperte)
let pairView = null;        // { sig, flow, state, perms, open }
let pairBusy = false;       // richiesta di abbinamento in corso
let pairSeen = { id: '', stage: '', at: 0, init: false }; // ultima fase vista, per la Goccia

function showErr(text) { $('err').textContent = text || ''; if (text) { lastErrorAt = Date.now(); updateGoccia(); } }
function showMsg(text, kind) { $('msg').textContent = text || ''; $('msg').className = 'alert ' + (kind || 'ok'); }

function describe(key, v) {
  if (key === 'homeStyle') return HOME_LABEL[v] || v;
  if (key === 'anim') return ANIM_LABEL[v] || v;
  if (key === 'speed') return SPEED_LABEL[v] || v;
  if (key === 'mute') return MUTE_LABEL[v] || v;
  if (key === 'workStart' || key === 'workEnd') return v;
  return yes(v);
}

// Voci come menuList('display') e menuList('avvisi') del simulatore.
function itemsFor(d) {
  const dev = DEVICES[d.device] || { fps: 2, fullEvery: 40, cls: d.class };
  const S = (d.class || dev.cls) === 'S';
  const keys = d.settings ? Object.keys(d.settings) : null;
  const has = (k, fallback) => (keys ? keys.includes(k) : fallback);
  const display = [
    has('homeStyle', S) && { key: 'homeStyle', label: 'Stile home', type: 'choice', opts: HOME_LABEL, help: 'Goccia, Righe, Contatori o Una alla volta.' },
    has('anim', true) && { key: 'anim', label: 'Animazioni', type: 'choice', opts: ANIM_LABEL, help: 'Auto muove la mascotte a raffiche e quando qualcuno ti aspetta. Su questo pannello le animazioni sono ' + profileName(dev) + '.' },
    has('speed', true) && { key: 'speed', label: 'Velocità animazioni', type: 'choice', opts: SPEED_LABEL, help: 'Profilo usa la velocità prevista per questo pannello. Massima aggiorna appena il pannello è pronto e usa tutti e 4 i fotogrammi: più fluida, ma consuma di più e lascia più scie.' },
    has('save', true) && { key: 'save', label: 'Risparmio energetico', type: 'bool', help: 'Ferma le animazioni e aggiorna lo schermo solo quando cambia qualcosa. Si attiva da solo sotto il 15% di batteria.' },
    has('invert', true) && { key: 'invert', label: 'Inverti colori', type: 'bool', help: 'Testo chiaro su fondo nero.' },
    has('rot', true) && { key: 'rot', label: 'Ruota 180°', type: 'bool', help: 'Per montarlo capovolto, con il cavo dall’altra parte.' },
    { key: 'full', label: 'Refresh completo', type: 'action', help: 'Pulisce le scie degli aggiornamenti parziali. Parte da solo ogni ' + dev.fullEvery + ' aggiornamenti.' },
    { key: 'profile', label: 'Profilo pannello', type: 'info', val: dev.fps + '/s · ' + dev.fullEvery, help: 'Velocità stimata del refresh parziale e ogni quanti aggiornamenti scatta quello completo.' }
  ].filter(Boolean);
  const avvisi = [
    has('mute', true) && { key: 'mute', label: 'Silenzia', type: 'choice', opts: MUTE_LABEL, help: 'Ferma i richiami animati e il suono. Le sessioni restano visibili.' },
    has('night', true) && { key: 'night', label: 'Notturno 22-8', type: 'bool', help: 'Di notte niente animazioni e un aggiornamento ogni 5 minuti. Qui lo attivi subito per provarlo.' },
    has('permOn', true) && { key: 'permOn', label: 'Permessi dal display', type: 'perm', help: 'Permette di rispondere Consenti o Nega dal display. NEGA è sempre preselezionato.' },
    has('sound', !!dev.buzzer) && { key: 'sound', label: dev.buzzer ? 'Cicalino' : 'Buzzer esterno', type: 'bool', help: 'Un bip quando una sessione chiede un permesso.' },
    has('led', !!dev.led) && { key: 'led', label: 'LED', type: 'bool', help: 'Il LED lampeggia finché qualcuno ti aspetta.' }
  ].filter(Boolean);
  return { display, avvisi };
}

const LABELS = { workStart: 'Inizio della giornata', workEnd: 'Fine della giornata' };

function pendingOf(key) {
  if (localPending[key]) return { has: true, value: localPending[key].value };
  const p = current && current.settingsPending;
  if (p && Object.prototype.hasOwnProperty.call(p, key)) return { has: true, value: p[key] };
  return { has: false };
}

async function send(key, value) {
  if (!current) return;
  const board = current.board;
  busy.add(key); showErr(''); showMsg(''); refresh();
  try {
    const r = await postSettings(board, { [key]: value });
    if (r.status === 202) {
      const pend = (r.body && r.body.pending) || { [key]: value };
      for (const [k, v] of Object.entries(pend)) localPending[k] = { value: v, at: Date.now() };
      showMsg(LABELS[key] + ': ' + describe(key, value) + ' · inviato, in attesa di conferma dal display.', 'info');
    } else if (r.status === 409) {
      showErr((r.body && r.body.error) || 'Il display non accetta ancora impostazioni da remoto.');
    } else if (r.status === 400) {
      showErr('Il servizio ha rifiutato il valore' + (r.body && r.body.error ? ': ' + r.body.error : '.'));
    } else {
      showErr('Risposta inattesa del servizio (HTTP ' + r.status + (r.body && r.body.error ? ': ' + r.body.error : '') + ').');
    }
  } catch (e) {
    showErr('Il servizio sul PC non risponde: impostazione non inviata.');
  }
  busy.delete(key);
  refresh();
}

async function fullRefresh(btn) {
  if (!current) return;
  btn.disabled = true; showErr(''); showMsg('');
  try {
    const r = await postFullRefresh(current.board);
    if (r.status === 202 || r.status === 200 || r.status === 204) { showMsg('Refresh completo richiesto: il display pulisce le scie.'); lastOkAt = Date.now(); }
    else if (r.status === 409) showErr((r.body && r.body.error) || 'Il display non supporta ancora il refresh completo da remoto.');
    else showErr('Risposta inattesa del servizio (HTTP ' + r.status + (r.body && r.body.error ? ': ' + r.body.error : '') + ').');
  } catch (e) {
    showErr('Il servizio sul PC non risponde: richiesta non inviata.');
  }
  btn.disabled = false;
  updateGoccia();
}

// Ogni controllo ha una funzione update() che rilegge valore confermato e in attesa senza ricrearsi
// (così il fuoco della tastiera non si perde agli aggiornamenti ogni 2 s).
function buildItem(it, supported) {
  LABELS[it.key] = it.label;
  const id = 'set-' + it.key;
  const help = h('p', { class: 'set-help', id: id + '-help' }, it.help);
  const pendLine = h('p', { class: 'pending-line', hidden: true }, h('span', { class: 'badge pending' }, 'IN ATTESA'), h('span', { class: 'pend-text' }));
  const li = h('li', { class: 'set-item' });
  let update = () => {};

  if (it.type === 'choice') {
    const fs = h('fieldset', { class: 'seg', 'aria-describedby': id + '-help' }, h('legend', null, it.label));
    const opts = h('div', { class: 'seg-opts' });
    const inputs = Object.entries(it.opts).map(([v, label]) => {
      const inp = h('input', { type: 'radio', name: id, value: v, onchange: () => send(it.key, v) });
      opts.append(h('label', null, inp, h('span', null, label)));
      return inp;
    });
    fs.append(opts);
    li.append(h('div', { class: 'set-row' }, h('span', { class: 'set-label', 'aria-hidden': 'true' }, it.label), fs), help, pendLine);
    update = (conf, pend) => {
      const v = pend.has ? pend.value : conf;
      inputs.forEach((inp) => { inp.checked = inp.value === v; inp.disabled = !supported || busy.has(it.key); });
    };
  } else if (it.type === 'bool') {
    const inp = h('input', { type: 'checkbox', role: 'switch', id, 'aria-describedby': id + '-help', onchange: () => send(it.key, inp.checked) });
    const txt = h('span', { 'aria-hidden': 'true' });
    li.append(h('div', { class: 'set-row' }, h('label', { class: 'set-label', for: id }, it.label), h('span', { class: 'switch' }, inp, txt)), help, pendLine);
    update = (conf, pend) => {
      const v = pend.has ? pend.value : conf;
      inp.checked = !!v; inp.disabled = !supported || busy.has(it.key);
      txt.textContent = supported ? yes(v) : 'non disponibile';
    };
  } else if (it.type === 'perm') {
    const inp = h('input', { type: 'checkbox', role: 'switch', id, 'aria-describedby': id + '-help ' + id + '-note',
      onchange: () => { if (!inp.checked) send('permOn', false); else inp.checked = false; } });
    const txt = h('span', { 'aria-hidden': 'true' });
    const note = h('p', { class: 'set-note', id: id + '-note' });
    li.append(h('div', { class: 'set-row' }, h('label', { class: 'set-label', for: id }, it.label), h('span', { class: 'switch' }, inp, txt)), help, note, pendLine);
    update = (conf, pend) => {
      const v = pend.has ? pend.value : conf;
      inp.checked = v === true;
      // Si può solo spegnere: il controllo è attivo solo quando i permessi sono accesi.
      inp.disabled = !supported || busy.has('permOn') || v !== true;
      txt.textContent = supported ? yes(v) : 'non disponibile';
      note.textContent = !supported ? 'Si cambia dal menu del display: Menu › Avvisi › Permessi dal display.'
        : v === true ? 'Da qui puoi solo spegnerli. Per riaccenderli serve il tasto sul display: Menu › Avvisi › Permessi dal display.'
        : 'Spenti. Per riaccenderli serve il tasto sul display: Menu › Avvisi › Permessi dal display. Da qui non si possono accendere, per sicurezza.';
    };
  } else if (it.type === 'action') {
    const btn = h('button', { type: 'button', class: 'btn small secondary', 'aria-describedby': id + '-help', onclick: () => fullRefresh(btn) }, 'Esegui');
    li.append(h('div', { class: 'set-row' }, h('span', { class: 'set-label' }, it.label), btn), help);
  } else {
    li.append(h('div', { class: 'set-row' }, h('span', { class: 'set-label' }, it.label), h('span', { class: 'mono' }, it.val)), help);
  }

  const full = (conf, pend) => {
    update(conf, pend);
    pendLine.hidden = !pend.has;
    if (pend.has) pendLine.querySelector('.pend-text').textContent = describe(it.key, pend.value) + ': aspetta la conferma dal display.';
  };
  return { li, update: it.type === 'action' || it.type === 'info' ? null : full };
}

// Le impostazioni si cambiano da qui solo con un display abbinato e autenticato che accetta set (M5).
const remoteOK = (d) => !!d.settings && d.remote === true && d.authed === true;

function build(d) {
  const supported = remoteOK(d);
  const { display, avvisi } = itemsFor(d);
  const controls = new Map();
  const group = (title, kicker, items) => {
    const ul = h('ul', { class: 'set-list' });
    for (const it of items) { const b = buildItem(it, supported); ul.append(b.li); if (b.update) controls.set(it.key, b.update); }
    return h('section', { class: 'note', 'aria-labelledby': 'g-' + title }, h('p', { class: 'kicker' }, kicker), h('h3', { id: 'g-' + title }, title), ul);
  };
  $('groups').replaceChildren(group('Display', 'Menu › Display', display), group('Avvisi', 'Menu › Avvisi', avvisi));
  return controls;
}

function renderInfo(d) {
  const box = $('info');
  box.replaceChildren(h('article', { class: 'note' },
    h('div', { class: 'card-head' }, h('span', { class: 't' }, d.board), h('span', { class: 'badge soft' }, 'classe ' + (d.class || '?'))),
    h('dl', { class: 'kv' },
      h('dt', null, 'Modello'), h('dd', null, deviceName(d.device)),
      h('dt', null, 'Indirizzo'), h('dd', { class: 'mono' }, d.ip || '—'),
      h('dt', null, 'Firmware'), h('dd', { class: 'mono' }, d.fw || '—'),
      h('dt', null, 'Collegato'), h('dd', null, d.since ? 'da ' + ago(d.since) : '—'),
      d.status ? [h('dt', null, 'Batteria'), h('dd', null, (d.batt === null || d.batt === undefined ? 'non si sa' : d.batt + '%') +
        (d.usb === true ? ' · cavo USB collegato' : d.usb === false ? ' · senza cavo' : '') + (d.cal ? ' · calibrazione in corso' : ''))] : null),
    !d.settings ? h('p', { class: 'alert info' },
      h('strong', null, 'Impostazioni remote non ancora supportate. '),
      'Il firmware ' + (d.fw || 'di questo display') + ' non le gestisce: arriveranno con un prossimo aggiornamento. Intanto si cambiano dai menu del display.')
      : remoteOK(d) ? null : h('p', { class: 'alert info' },
        h('strong', null, 'Impostazioni in sola lettura. '),
        'Da qui si cambiano solo su un display abbinato e autenticato: abbinalo qui sotto. Intanto si cambiano dai menu del display.')));
}

function renderTabs(list) {
  const nav = $('tabs');
  nav.hidden = list.length < 2;
  nav.replaceChildren(...list.map((d) => h('a', { href: '?board=' + encodeURIComponent(d.board), 'aria-current': d.board === (current && current.board) ? 'page' : null }, d.board)));
}

function renderEmpty(text) {
  view = null;
  $('info').replaceChildren(h('div', { class: 'empty' }, h('strong', null, 'Nessun display collegato'), text));
  $('groups').replaceChildren();
}

function refresh() {
  if (!view || !current) return;
  for (const [key, upd] of view.controls) upd(current.settings ? current.settings[key] : undefined, pendingOf(key));
  updateGoccia();
}

function updateGoccia() {
  if (connMode === 'offline') return goccia.set('error', 'Il servizio sul PC non risponde');
  if (!current) return goccia.set('idle', 'Nessun display collegato');
  // Abbinamento: chi deve fare la prossima mossa (tu, sul PC o sul display) e com'è finito.
  const p = current.pairing;
  if (p && p.stage === 'begin') return goccia.set('working', 'Aspetto il display');
  if (p && p.stage === 'code') return goccia.set('input', 'Scrivi qui il codice del display');
  if (p && p.stage === 'confirm') return goccia.set('input', 'Conferma sul display: 2 pressioni');
  const fresh = Date.now() - pairSeen.at < 6000;
  if (p && p.stage === 'done' && fresh) return goccia.set('done', current.pair === 'ok' ? 'Abbinato e autenticato' : 'Abbinato');
  if (p && p.stage === 'aborted' && fresh) return goccia.set('error', 'Abbinamento annullato');
  if (wifiBusy || wdBusy || (current.wifi && current.wifi.pending) || (current.workday && current.workday.pending)) return goccia.set('working', 'Aspetto la conferma della scheda');
  if (Date.now() - lastErrorAt < 6000) return goccia.set('error', 'Richiesta non accettata');
  const anyPending = view && [...view.controls.keys(), 'workStart', 'workEnd'].some((k) => pendingOf(k).has || busy.has(k));
  if (anyPending) return goccia.set('working', 'In attesa di conferma dal display');
  if (Date.now() - lastOkAt < 6000) return goccia.set('done', 'Il display ha confermato');
  if (current.pair === 'redo') return goccia.set('error', 'Abbinamento da rifare');
  if (!current.settings) return goccia.set('idle', 'Impostazioni remote in arrivo');
  goccia.set('done', 'Impostazioni allineate');
}

function onStatus(st) {
  lastStatus = st;
  const list = st.displays || [];
  if (!boardId && list.length) boardId = list[0].board;
  const d = list.find((x) => x.board === boardId) || null;
  const t = Date.now();
  for (const k of Object.keys(localPending)) if (t - localPending[k].at > 500) delete localPending[k];

  current = d;
  renderTabs(list);
  if (!d) {
    pairView = null;
    $('pair').replaceChildren();
    renderWifi(null);
    renderWorkday(null);
    renderLog(null);
    $('kicker').textContent = 'Impostazioni da remoto';
    renderEmpty(list.length ? 'Il display ' + boardId + ' non è collegato adesso. Scegline un altro qui sopra.'
      : 'Accendi il display e aspetta che si colleghi al servizio: le impostazioni compaiono qui.');
    if (list.length) $('tabs').hidden = false;
    prevPending = {};
    updateGoccia();
    return;
  }
  $('kicker').textContent = 'Impostazioni da remoto · ' + d.board;
  document.title = d.board + ' · Dispositivo · Calamaio OS';
  renderInfo(d);

  renderWorkday(d);
  renderPair(d);
  renderWifi(d);
  renderLog(d);
  const sig = d.board + '|' + (d.settings ? Object.keys(d.settings).sort().join(',') : 'null') + '|' + d.device + '|' + d.class + '|' + remoteOK(d);
  if (!view || view.sig !== sig) view = { sig, controls: build(d) };

  // Cosa ha confermato (o scartato) il display dall'ultimo aggiornamento.
  const pend = d.settingsPending || {};
  for (const [k, v] of Object.entries(prevPending)) {
    if (Object.prototype.hasOwnProperty.call(pend, k)) continue;
    if (d.settings && d.settings[k] === v) { showMsg('Il display ha confermato: ' + (LABELS[k] || k) + ' = ' + describe(k, v) + '.'); lastOkAt = Date.now(); }
    else showErr('Il display non ha applicato ' + (LABELS[k] || k) + ' = ' + describe(k, v) + '.');
  }
  prevPending = { ...pend };
  refresh();
}

/* ---------- abbinamento e permessi (M5) ---------- */

const PAIR_TEXT = {
  none: 'Non abbinato',
  ok: 'Abbinato e autenticato',
  redo: 'Abbinamento da rifare',
  auth: 'Abbinato, autenticazione in corso'
};

// Motivi di pair-abort (servizio e display), come `calamaio pair`.
const ABORT_TEXT = {
  codice: 'codice sbagliato',
  tempo: 'tempo scaduto (3 minuti)',
  annullato: 'annullato dal PC',
  display: 'annullato sul display',
  connessione: 'il display si è scollegato',
  impegno: 'il display ha ricevuto una chiave del PC che non corrisponde all’impegno: qualcuno in rete potrebbe essersi messo in mezzo',
  mac: 'verifica non riuscita',
  chiave: 'chiave non valida'
};
const abortText = (r) => ABORT_TEXT[r] || r || 'annullato';

function permText(d) {
  if (d.perms) return 'Attivi: il display riceve le richieste e può rispondere Consenti o Nega (NEGA è sempre preselezionato).';
  if (d.pair !== 'ok') return 'Non attivi: servono un display abbinato e la connessione autenticata.';
  if (d.settings && d.settings.permOn !== true) return 'Spenti sul display. Si accendono solo dal display: Menu › Avvisi › Permessi dal display.';
  return 'Non attivi.';
}

function openFor(board) {
  const all = (lastStatus && lastStatus.permissions) || [];
  return all.filter((p) => (p.sentTo || []).includes(board));
}

async function pairOp(op, body) {
  if (!current || pairBusy) return null;
  pairBusy = true; showErr(''); showMsg('');
  let r = null;
  try {
    r = await postPair(current.board, op, body);
  } catch (e) {
    showErr('Il servizio sul PC non risponde: richiesta non inviata.');
  }
  pairBusy = false;
  return r;
}

async function pairStart() {
  const r = await pairOp('start');
  if (r && r.status !== 202) showErr((r.body && r.body.error) || 'Abbinamento non avviato (HTTP ' + r.status + ').');
}

async function pairCancel() {
  const r = await pairOp('cancel');
  if (r && r.status !== 202 && r.status !== 409) showErr((r.body && r.body.error) || 'Annullamento non riuscito (HTTP ' + r.status + ').');
}

async function pairCode(input) {
  const code = input.value.replace(/[\s-]/g, '');
  if (!/^[0-9]{6}$/.test(code)) { showErr('Servono le 6 cifre che vedi sul display.'); input.focus(); return; }
  const r = await pairOp('code', { code });
  if (!r) return;
  if (r.status === 202) { input.value = ''; return; }
  if (r.status === 400 && r.body && r.body.aborted) showErr('Codice sbagliato: abbinamento annullato, nessuna chiave salvata. Puoi ricominciare.');
  else showErr((r.body && r.body.error) || 'Codice non accettato (HTTP ' + r.status + ').');
  if (r.status === 400 && !(r.body && r.body.aborted)) input.focus();
}

// Il riquadro del flusso si ricrea solo quando cambia la fase: il campo del codice resta com'è
// (fuoco e testo) agli aggiornamenti dello stato.
function pairFlow(d) {
  const p = d.pairing;
  const stage = p ? p.stage : '';
  const again = d.paired || d.pair === 'redo';
  const startBtn = (label, cls) => h('button', { type: 'button', class: cls || 'btn', onclick: pairStart }, label);
  const cancelBtn = () => h('button', { type: 'button', class: 'btn secondary', onclick: pairCancel }, 'Annulla');
  if (stage === 'begin') {
    return [h('p', null, 'Aspetto il display…'), h('div', { class: 'set-row' }, cancelBtn())];
  }
  if (stage === 'code') {
    const until = p.until ? new Date(p.until).toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit' }) : '';
    const input = h('input', { type: 'text', id: 'pair-code', class: 'confirm', inputmode: 'numeric', autocomplete: 'off',
      maxlength: '7', pattern: '[0-9 ]*', 'aria-describedby': 'pair-code-help', spellcheck: 'false' });
    const form = h('form', { class: 'field', onsubmit: (ev) => { ev.preventDefault(); pairCode(input); } },
      h('label', { for: 'pair-code' }, 'Leggi il codice sul display e scrivilo qui:'),
      h('div', { class: 'set-row', style: { justifyContent: 'flex-start' } }, input, h('button', { type: 'submit', class: 'btn' }, 'Conferma codice'), cancelBtn()),
      h('p', { class: 'hint', id: 'pair-code-help' }, 'Una prova sola: con un codice sbagliato l’abbinamento si annulla. ' +
        (until ? 'Hai tempo fino alle ' + until + '.' : '')));
    setTimeout(() => input.focus(), 0);
    return [form];
  }
  if (stage === 'confirm') {
    return [h('p', { class: 'alert info' }, h('strong', null, 'Codice giusto. '), 'Ora conferma sul display: premi due volte il tasto.'),
      h('div', { class: 'set-row' }, cancelBtn())];
  }
  const out = [];
  if (stage === 'done') out.push(h('p', { class: 'alert ok' }, d.pair === 'ok' ? 'Abbinato: connessione autenticata.' : 'Abbinato.'));
  if (stage === 'aborted') out.push(h('p', { class: 'alert error' }, 'Abbinamento annullato: ' + abortText(p.reason) + '. Nessuna chiave salvata.'));
  if (d.pair === 'redo') out.push(h('p', { class: 'set-note' }, 'Il PC e il display non hanno la stessa chiave' +
    (d.pairReason ? ' (' + d.pairReason + ')' : '') + ': rifai l’abbinamento.'));
  out.push(h('p', { class: 'set-help' }, 'Il display mostra un codice di 6 cifre: lo scrivi qui, poi confermi sul display con due pressioni.'));
  out.push(h('div', { class: 'set-row' }, startBtn(again ? 'Abbina di nuovo' : 'Abbina', again && d.pair === 'ok' ? 'btn secondary' : 'btn')));
  return out;
}

function renderPair(d) {
  const p = d.pairing;
  const stage = p ? p.stage : '';
  // Un cambiamento di fase visto adesso fa reagire la Goccia; quello trovato al caricamento della pagina no.
  if (p && (p.id !== pairSeen.id || stage !== pairSeen.stage)) pairSeen = { id: p.id, stage, at: pairSeen.init ? Date.now() : 0, init: true };
  pairSeen.init = true;
  const sig = d.board + '|' + (p ? p.id + ':' + stage : '') + '|' + d.pair + '|' + d.paired;
  if (!pairView || pairView.board !== d.board) {
    const state = h('dd'), perms = h('dd'), open = h('dd');
    const flow = h('div', { class: 'stack' });
    $('pair').replaceChildren(h('article', { class: 'note', 'aria-labelledby': 'pair-h' },
      h('p', { class: 'kicker' }, 'Menu › Sicurezza'),
      h('h3', { id: 'pair-h' }, 'Abbinamento e permessi'),
      h('dl', { class: 'kv' },
        h('dt', null, 'Abbinamento'), state,
        h('dt', null, 'Permessi dal display'), perms,
        h('dt', null, 'Richieste aperte'), open),
      flow,
      h('p', { class: 'set-help' }, 'Solo il display abbinato può rispondere ai permessi, con risposte firmate. Le domande di Claude si rispondono solo nel terminale. Nessuna scadenza: una richiesta resta aperta finché rispondi, sul display o nel terminale.')));
    pairView = { board: d.board, sig: '', flow, state, perms, open };
  }
  pairView.state.textContent = PAIR_TEXT[d.pair] || d.pair || '—';
  pairView.perms.textContent = permText(d);
  const reqs = openFor(d.board);
  pairView.open.textContent = reqs.length ? reqs.map((r) => r.name + ': ' + r.tool + (r.arg ? ' ' + r.arg : '')).join(' · ') : 'nessuna';
  if (pairView.sig !== sig) {
    pairView.sig = sig;
    pairView.flow.replaceChildren(...pairFlow(d));
  }
}

/* ---------- reti Wi-Fi della scheda (docs/PROTOCOLLO.md «Reti Wi-Fi») ---------- */

// La password resta solo nel campo del modulo finché parte: niente variabili globali, niente
// localStorage; il campo si svuota appena la richiesta è finita e alla chiusura della finestra.
const WIFI_MAX = 5;          // reti tenute dalla scheda (firmware attuale): oltre, la scheda risponde «lista piena»
const utf8Len = (s) => new TextEncoder().encode(s).length;
let wifiView = null;         // { board, sig, list, state, add, note }
let wifiBusy = false;        // aggiunta o rimozione in corso (una alla volta per scheda)

function signalText(rssi) {
  if (rssi === null || rssi === undefined) return 'non in vista';
  const q = rssi >= -55 ? 'ottimo' : rssi >= -67 ? 'buono' : rssi >= -75 ? 'discreto' : 'debole';
  return 'segnale ' + q + ' (' + String(rssi).replace('-', '−') + ' dBm)';
}

function wifiRow(d, n, count) {
  const parts = [signalText(n.rssi)];
  if (!n.current && n.lastUsed) parts.push('ultimo uso ' + new Date(n.lastUsed).toLocaleString('it-IT', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }));
  const forget = h('button', { type: 'button', class: 'btn small secondary', 'aria-label': 'Dimentica la rete ' + n.ssid,
    disabled: !d.authed || wifiBusy || !!(d.wifi && d.wifi.pending), onclick: () => wifiForget(d, n, count) }, 'Dimentica');
  return h('li', { class: 'set-item' },
    h('div', { class: 'set-row' },
      h('span', { class: 'set-label' }, n.ssid, n.current ? [' ', h('span', { class: 'badge attn' }, 'IN USO')] : null),
      forget),
    h('p', { class: 'set-help' }, parts.join(' · ')));
}

function wifiState(d) {
  const w = d.wifi;
  if (!w) return 'Il servizio sul PC non gestisce ancora le reti: aggiornalo e riavvialo.';
  if (!w.networks) return 'La scheda non ha mandato l’elenco delle reti: serve un firmware che ne gestisce più di una.';
  if (w.pending) return (w.pending.op === 'forget' ? 'Dimentico «' : 'Aggiungo «') + w.pending.ssid + '»: aspetto la conferma della scheda…';
  if (!d.authed) return 'Si aggiungono e si dimenticano dal PC solo con una scheda abbinata e autenticata (vedi sopra).';
  return w.networks.length + (w.networks.length === 1 ? ' rete salvata' : ' reti salvate') + ' su ' + WIFI_MAX + '.';
}

function renderWifi(d) {
  if (!d) { wifiView = null; $('wifi').replaceChildren(); return; }
  if (!wifiView || wifiView.board !== d.board) {
    const list = h('ul', { class: 'set-list' });
    const state = h('p', { class: 'set-help', role: 'status' });
    const note = h('p', { class: 'set-note', hidden: true });
    const add = h('button', { type: 'button', class: 'btn', onclick: () => wifiAddOpen() }, 'Aggiungi rete');
    $('wifi').replaceChildren(h('article', { class: 'note', 'aria-labelledby': 'wifi-h' },
      h('p', { class: 'kicker' }, 'Connessione'),
      h('h3', { id: 'wifi-h' }, 'Reti Wi-Fi'),
      h('p', { class: 'set-help' }, 'La scheda ricorda fino a ' + WIFI_MAX + ' reti e usa la più forte. Le password partono da questa pagina cifrate verso la scheda; il PC non le salva.'),
      list, state, note, h('div', { class: 'card-actions' }, add)));
    wifiView = { board: d.board, sig: null, list, state, add, note };
  }
  const w = d.wifi || null;
  const nets = (w && w.networks) || [];
  const sig = JSON.stringify([nets, d.authed, !!(w && w.pending), wifiBusy]);
  if (sig !== wifiView.sig) {
    wifiView.sig = sig;
    wifiView.list.replaceChildren(...nets.map((n) => wifiRow(d, n, nets.length)));
  }
  wifiView.state.textContent = wifiState(d);
  wifiView.add.disabled = !d.authed || !w || !w.networks || wifiBusy || !!w.pending;
  const full = nets.length >= WIFI_MAX;
  wifiView.note.hidden = !full;
  if (full) wifiView.note.textContent = 'Lista piena: per aggiungere una rete nuova dimenticane prima una (cambiare la password di una rete già salvata si può).';
}

function wifiAddOpen() {
  if (!current) return;
  const dlg = $('dlg-wifi-add');
  $('wifi-add-text').textContent = 'La scheda ' + current.board + ' la ricorda insieme alle altre e si collega alla più forte.';
  $('wifi-ssid').value = ''; $('wifi-pass').value = ''; $('wifi-pass').type = 'password'; $('wifi-show').checked = false;
  $('wifi-add-err').hidden = true; $('wifi-add-wait').hidden = true; $('wifi-add-send').disabled = false;
  dlg.showModal();
  $('wifi-ssid').focus();
}

function wifiAddError(text, focusId) {
  const e = $('wifi-add-err');
  e.textContent = text; e.hidden = !text;
  if (focusId) $(focusId).focus();
}

async function wifiAddSubmit(ev) {
  ev.preventDefault();
  if (!current || wifiBusy) return;
  const board = current.board;
  const ssid = $('wifi-ssid').value;
  const pass = $('wifi-pass');
  if (!ssid) return wifiAddError('Scrivi il nome della rete.', 'wifi-ssid');
  if (utf8Len(ssid) > 32) return wifiAddError('Nome troppo lungo: al massimo 32 byte.', 'wifi-ssid');
  const n = utf8Len(pass.value);
  if (n < 8) return wifiAddError('Password troppo corta: almeno 8 caratteri.', 'wifi-pass');
  if (n > 63) return wifiAddError('Password troppo lunga: al massimo 63 byte (le lettere accentate ne contano 2).', 'wifi-pass');
  wifiBusy = true; wifiAddError(''); $('wifi-add-wait').hidden = false; $('wifi-add-send').disabled = true;
  if (current) renderWifi(current);
  let r = null;
  try { r = await postWifiAdd(board, ssid, pass.value); } catch (e) { r = null; }
  wifiBusy = false; $('wifi-add-wait').hidden = true; $('wifi-add-send').disabled = false;
  if (r && r.status === 400) { // controllo del servizio: la password resta nel campo per correggerla
    wifiAddError((r.body && r.body.error) || 'Dati non validi.', 'wifi-pass');
  } else {
    pass.value = '';
    if (!r) wifiAddError('Il servizio sul PC non risponde: rete non inviata.');
    else if (r.status === 200) {
      $('dlg-wifi-add').close('ok');
      showErr(''); showMsg('Rete «' + ssid + '» salvata sulla scheda ' + board + '.');
      lastOkAt = Date.now();
    } else wifiAddError(((r.body && r.body.error) || 'Rete non aggiunta (HTTP ' + r.status + ')') + '. Riscrivi la password per riprovare.', 'wifi-pass');
  }
  if (current) renderWifi(current);
  updateGoccia();
}

async function wifiForget(d, n, count) {
  if (wifiBusy) return;
  const dlg = $('dlg-wifi-forget');
  $('wifi-forget-title').textContent = 'Dimenticare «' + n.ssid + '»?';
  $('wifi-forget-text').textContent = 'La scheda ' + d.board + ' dimentica la rete e si riavvia per cancellarla davvero dalla memoria (pulizia profonda): resta scollegata per qualche secondo.';
  const last = count === 1;
  $('wifi-forget-current').hidden = !n.current || last;
  $('wifi-forget-current').textContent = 'È la rete in uso: dopo il riavvio la scheda si collega a un’altra delle reti salvate.';
  $('wifi-forget-last').hidden = !last;
  dlg.returnValue = '';
  const v = await new Promise((resolve) => { dlg.addEventListener('close', () => resolve(dlg.returnValue), { once: true }); dlg.showModal(); });
  if (v !== 'ok' || !current || current.board !== d.board) return;
  wifiBusy = true; showErr(''); showMsg('Chiedo alla scheda di dimenticare «' + n.ssid + '»…', 'info'); renderWifi(current);
  try {
    const r = await postWifiForget(d.board, n.ssid);
    if (r.status === 200) {
      showMsg('Rete «' + n.ssid + '» dimenticata. La scheda si riavvia' + (r.body && r.body.wasLast ? ' e torna in configurazione.' : ': si ricollega tra qualche secondo.'));
      lastOkAt = Date.now();
    } else { showMsg(''); showErr((r.body && r.body.error) || 'Rete non dimenticata (HTTP ' + r.status + ').'); }
  } catch (e) { showMsg(''); showErr('Il servizio sul PC non risponde: niente fatto.'); }
  wifiBusy = false;
  if (current) renderWifi(current);
  updateGoccia();
}

$('wifi-add-form').addEventListener('submit', wifiAddSubmit);
$('wifi-add-cancel').addEventListener('click', () => $('dlg-wifi-add').close('cancel'));
$('dlg-wifi-add').addEventListener('close', () => { $('wifi-pass').value = ''; $('wifi-pass').type = 'password'; $('wifi-show').checked = false; });
$('wifi-show').addEventListener('change', (ev) => { $('wifi-pass').type = ev.target.checked ? 'text' : 'password'; });

/* ---------- giornata di lavoro (docs/PROTOCOLLO.md «Giornata di lavoro») ---------- */

// La tiene la scheda (funziona anche senza PC): da qui si avvia, si ferma e se ne cambiano gli orari.
// Il conto alla rovescia si ridisegna ogni 5 s dall'ora di fine riferita dalla scheda.
let wdView = null;    // { board, count, bar, barFill, pend, note, start, stop, from, to, save, dirty }
let wdBusy = false;   // richiesta in corso (start/stop o orari)
let wdAwait = null;   // { id, action }: comando partito da questa pagina, in attesa dell'esito

const wdHours = (d) => ({ from: d.settings && d.settings.workStart, to: d.settings && d.settings.workEnd });
const wdReady = (d) => !!(d.workday && d.workday.supported && d.authed === true);

function wdNote(d) {
  const w = d.workday;
  if (!w) return 'Il servizio sul PC non gestisce ancora la giornata di lavoro: aggiornalo e riavvialo.';
  if (!w.supported) return 'Il firmware ' + (d.fw || 'di questo display') + ' non gestisce la giornata di lavoro: arriverà con un prossimo aggiornamento.';
  if (d.authed !== true) return 'Da qui si comanda solo una scheda abbinata e autenticata (vedi sotto). Intanto si usa dal display: Menu › Strumenti.';
  if (!w.known) return 'La scheda non ha ancora riferito lo stato della giornata.';
  return '';
}

// Frazione di giornata passata, per la barra (come quella del Riposo sul display).
function wdProgress(d, now) {
  const w = d.workday, { from } = wdHours(d);
  if (!w || !w.on || w.end == null || !from) return null;
  const end = new Date(w.end), start = new Date(end);
  start.setHours(+from.slice(0, 2), +from.slice(3), 0, 0);
  const span = end - start;
  if (span <= 0) return null;
  return Math.max(0, Math.min(1, (now - start) / span));
}

function wdTick() {
  if (!wdView || !current || current.board !== wdView.board) return;
  const d = current, w = d.workday, now = Date.now();
  const text = workdayText(w, now);
  wdView.count.textContent = !w || !w.known ? '—' : text ? text.charAt(0).toUpperCase() + text.slice(1) : 'Spenta';
  wdView.count.classList.toggle('off', !text);
  const p = wdProgress(d, now);
  wdView.bar.hidden = p === null;
  if (p !== null) {
    wdView.barFill.style.width = (100 * p).toFixed(1) + '%';
    wdView.bar.setAttribute('aria-label', 'Giornata passata: ' + Math.round(100 * p) + '%; ' + workdayLeft(w.end, now));
  }
}

async function wdCommand(action) {
  if (!current || wdBusy) return;
  const board = current.board;
  wdBusy = true; showErr(''); showMsg(''); renderWorkday(current);
  try {
    const r = await postWorkday(board, action);
    if (r.status === 202) {
      wdAwait = { id: r.body.id, action };
      showMsg((action === 'start' ? 'Avvio la giornata' : 'Fermo la giornata') + ': aspetto la conferma della scheda…', 'info');
    } else showErr((r.body && r.body.error) || 'Comando non accettato (HTTP ' + r.status + ').');
  } catch (e) {
    showErr('Il servizio sul PC non risponde: comando non inviato.');
  }
  wdBusy = false;
  if (current) renderWorkday(current);
  updateGoccia();
}

async function wdSave(ev) {
  ev.preventDefault();
  if (!current || !wdView || wdBusy) return;
  const from = wdView.from.value, to = wdView.to.value;
  const ok = (v) => /^([01][0-9]|2[0-3]):[0-5][0-9]$/.test(v);
  if (!ok(from)) { showErr('Scrivi l’orario di inizio come ore e minuti (es. 09:00).'); wdView.from.focus(); return; }
  if (!ok(to)) { showErr('Scrivi l’orario di fine come ore e minuti (es. 18:00).'); wdView.to.focus(); return; }
  if (from >= to) { showErr('L’inizio deve venire prima della fine, nello stesso giorno.'); wdView.to.focus(); return; }
  const { from: f0, to: t0 } = wdHours(current);
  const vals = {};
  if (from !== f0) vals.workStart = from;
  if (to !== t0) vals.workEnd = to;
  if (!Object.keys(vals).length) { showMsg('Gli orari sono già questi.'); wdView.dirty = false; return; }
  wdBusy = true; busy.add('workStart'); busy.add('workEnd'); showErr(''); showMsg(''); renderWorkday(current);
  try {
    const r = await postSettings(current.board, vals);
    if (r.status === 202) {
      const pend = (r.body && r.body.pending) || vals;
      for (const [k, v] of Object.entries(pend)) localPending[k] = { value: v, at: Date.now() };
      wdView.dirty = false;
      showMsg('Orari ' + from + '–' + to + ' inviati: aspetto la conferma della scheda.', 'info');
    } else showErr((r.body && r.body.error) || 'Orari non accettati (HTTP ' + r.status + ').');
  } catch (e) {
    showErr('Il servizio sul PC non risponde: orari non inviati.');
  }
  wdBusy = false; busy.delete('workStart'); busy.delete('workEnd');
  if (current) renderWorkday(current);
  updateGoccia();
}

function renderWorkday(d) {
  if (!d) { wdView = null; $('workday').replaceChildren(); return; }
  if (!wdView || wdView.board !== d.board) {
    const count = h('p', { class: 'workday-count', id: 'wd-count' });
    const barFill = h('span');
    const bar = h('div', { class: 'bar big', role: 'img', hidden: true }, barFill);
    const pend = h('p', { class: 'pending-line', hidden: true }, h('span', { class: 'badge pending' }, 'IN ATTESA'), h('span', { class: 'pend-text' }));
    const note = h('p', { class: 'set-note', hidden: true });
    const start = h('button', { type: 'button', class: 'btn', onclick: () => wdCommand('start') }, 'Inizia ora');
    const stop = h('button', { type: 'button', class: 'btn secondary', onclick: () => wdCommand('stop') }, 'Ferma');
    const from = h('input', { type: 'time', id: 'wd-from', step: '60', required: true, 'aria-describedby': 'wd-hours-help' });
    const to = h('input', { type: 'time', id: 'wd-to', step: '60', required: true, 'aria-describedby': 'wd-hours-help' });
    const save = h('button', { type: 'submit', class: 'btn small secondary' }, 'Salva orari');
    const form = h('form', { class: 'workday-hours', onsubmit: wdSave },
      h('div', { class: 'field' }, h('label', { for: 'wd-from' }, 'Inizio'), from),
      h('div', { class: 'field' }, h('label', { for: 'wd-to' }, 'Fine'), to),
      save);
    $('workday').replaceChildren(h('article', { class: 'note', 'aria-labelledby': 'wd-h' },
      h('p', { class: 'kicker' }, 'Menu › Strumenti'),
      h('h3', { id: 'wd-h' }, 'Giornata di lavoro'),
      count, bar,
      h('p', { class: 'set-help' }, 'La scheda conta le ore che mancano alla fine della giornata, anche a PC spento. La fine è sempre l’orario di fine, anche se inizi più tardi; a mezzanotte si azzera.'),
      pend, note,
      h('div', { class: 'card-actions' }, start, stop),
      form,
      h('p', { class: 'hint set-help', id: 'wd-hours-help' }, 'Orari della giornata, predefiniti 09:00–18:00. Li conferma la scheda.')));
    wdView = { board: d.board, count, bar, barFill, pend, note, start, stop, from, to, save, dirty: false };
    const mark = () => { wdView.dirty = true; };
    from.addEventListener('input', mark); to.addEventListener('input', mark);
  }
  const w = d.workday, ready = wdReady(d);
  const pending = !!(w && w.pending);
  // Esito del comando partito da qui.
  if (wdAwait && w && w.last && w.last.id === wdAwait.id) {
    if (w.last.ok) { showErr(''); showMsg(wdAwait.action === 'start' ? 'La scheda ha avviato la giornata.' : 'La scheda ha fermato la giornata.'); lastOkAt = Date.now(); }
    else { showMsg(''); showErr(w.last.error || 'La scheda non ha eseguito il comando.'); }
    wdAwait = null;
  }
  wdView.start.disabled = !ready || pending || wdBusy || !!(w && w.on);
  wdView.stop.disabled = !ready || pending || wdBusy || !(w && w.on);
  wdView.pend.hidden = !pending;
  if (pending) wdView.pend.querySelector('.pend-text').textContent = (w.pending.action === 'start' ? 'Avvio la giornata' : 'Fermo la giornata') + ': aspetto la conferma della scheda (fino alle ' + hm(new Date(w.pending.until)) + ').';
  // Comando confermato ma stato non ancora arrivato dalla scheda.
  const waiting = !!(w && w.last && w.last.ok && w.last.at > (w.at || 0) && (w.last.action === 'start') !== !!w.on);
  const note = wdNote(d) || (waiting ? 'La scheda ha confermato: aspetto il suo stato aggiornato.' : '');
  wdView.note.hidden = !note;
  wdView.note.textContent = note;
  // Orari: dai valori della scheda (o in attesa), senza toccare quello che stai scrivendo.
  const { from, to } = wdHours(d);
  const pf = pendingOf('workStart'), pt = pendingOf('workEnd');
  const editing = wdView.dirty || document.activeElement === wdView.from || document.activeElement === wdView.to;
  if (!editing) {
    wdView.from.value = (pf.has ? pf.value : from) || '';
    wdView.to.value = (pt.has ? pt.value : to) || '';
  }
  const hoursOK = ready && !!from && !!to && remoteOK(d);
  wdView.from.disabled = wdView.to.disabled = !hoursOK || wdBusy;
  wdView.save.disabled = !hoursOK || wdBusy || pf.has || pt.has;
  wdTick();
}

setInterval(wdTick, 5000);

/* ---------- registro da remoto e ultimo crash (docs/OTA.md §5-§6) ---------- */

let logView = null; // { board, pre, state, follow, stop, crash, crashSig, after, ctl, running }
const MAX_LOG_CHARS = 400000;

function appendLines(lines) {
  if (!logView || !lines.length) return;
  const pre = logView.pre;
  const atEnd = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 8;
  pre.textContent += lines.map((l) => l.text).join('\n') + '\n';
  if (pre.textContent.length > MAX_LOG_CHARS) pre.textContent = pre.textContent.slice(-MAX_LOG_CHARS * 0.75);
  if (atEnd) pre.scrollTop = pre.scrollHeight;
}

function logStateText(l) {
  if (!l) return 'Spento. Il registro arriva solo quando lo chiedi.';
  if (l.active) return l.follow ? 'In diretta fino alle ' + new Date(l.until).toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit' }) + ' · ' + l.lines + ' righe'
    : 'Ricezione del registro in memoria…';
  return 'Fermo: ' + (l.reason || 'finito') + ' · ' + l.lines + ' righe' + (l.file ? ' · salvato in ' + l.file : '');
}

// Lunga attesa: righe nuove finché il registro è attivo.
async function pollLog(board, ctl) {
  while (logView && logView.board === board && !ctl.signal.aborted) {
    let r;
    try { r = await getLog(board, logView.after, 20, ctl.signal); } catch (e) { break; }
    if (!logView || logView.board !== board) break;
    appendLines(r.lines || []);
    logView.after = r.next || logView.after;
    logView.state.textContent = logStateText(r.log);
    logView.stop.disabled = !(r.log && r.log.active);
    if (!r.log || !r.log.active) break;
  }
  if (logView && logView.board === board) logView.running = false;
}

function runLog(board) {
  if (!logView || logView.running) return;
  logView.running = true;
  logView.ctl = new AbortController();
  pollLog(board, logView.ctl);
}

async function logStart(follow) {
  if (!current || !logView) return;
  showErr('');
  const minutes = parseInt(logView.minutes.value, 10) || 10;
  try {
    const r = await postLogStart(current.board, follow, minutes);
    if (r.status !== 202) { showErr((r.body && r.body.error) || 'Registro non richiesto (HTTP ' + r.status + ').'); return; }
    logView.state.textContent = logStateText(r.body.log);
    runLog(current.board);
  } catch (e) { showErr('Il servizio sul PC non risponde.'); }
}

async function logStop() {
  if (!current) return;
  try { await postLogStop(current.board); } catch (e) { /* lo stato dirà com'è andata */ }
}

function crashBlock(c) {
  if (!c) return [h('p', { class: 'set-help' }, 'Nessun crash riferito da questa scheda.')];
  const when = new Date(c.received).toLocaleString('it-IT');
  const out = [h('p', null, h('strong', null, 'Ultimo crash: '), when + ' · firmware ' + c.fw + ' · ' + c.reason + (c.exc ? ' (' + c.exc + ')' : ''))];
  if (c.decoded && c.decoded.length) out.push(h('ol', { class: 'crash-lines' }, ...c.decoded.slice(0, 8).map((l) => h('li', null, l))));
  else if (c.pc) out.push(h('p', { class: 'mono' }, 'PC ' + c.pc + (c.backtrace ? ' · ' + c.backtrace.join(' ') : '')));
  if (c.last && c.last.length) out.push(h('p', { class: 'set-help' }, 'Ultime righe del registro: ' + [].concat(c.last).join(' · ')));
  if (c.decodeNote) out.push(h('p', { class: 'set-help' }, c.decodeNote));
  out.push(h('p', null, h('a', { href: crashUrl(c.id), target: '_blank', rel: 'noopener' }, 'Rapporto completo')));
  return out;
}

function renderLog(d) {
  if (!d) {
    if (logView && logView.ctl) logView.ctl.abort();
    logView = null;
    $('logbox').replaceChildren();
    return;
  }
  if (!logView || logView.board !== d.board) {
    if (logView && logView.ctl) logView.ctl.abort();
    const pre = h('pre', { class: 'log device-log', 'aria-live': 'off', tabindex: '0', 'aria-label': 'Registro della scheda' });
    const state = h('p', { class: 'set-help', role: 'status' });
    const minutes = h('select', { 'aria-label': 'Durata della diretta' },
      h('option', { value: '10' }, '10 min'), h('option', { value: '30' }, '30 min'), h('option', { value: '60' }, '60 min'));
    const read = h('button', { type: 'button', class: 'btn small secondary', onclick: () => logStart(false) }, 'Leggi');
    const follow = h('button', { type: 'button', class: 'btn small', onclick: () => logStart(true) }, 'Segui');
    const stop = h('button', { type: 'button', class: 'btn small secondary', disabled: true, onclick: logStop }, 'Ferma');
    const dl = h('a', { class: 'btn small secondary', href: logFileUrl(d.board), download: d.board + '.log' }, 'Scarica');
    const crash = h('div', { class: 'stack' });
    $('logbox').replaceChildren(h('article', { class: 'note', 'aria-labelledby': 'log-h' },
      h('p', { class: 'kicker' }, 'Diagnosi'),
      h('h3', { id: 'log-h' }, 'Registro'),
      h('p', { class: 'set-help' }, 'La scheda tiene in memoria le ultime righe del suo registro (circa 16 KB), anche con la seriale spenta. «Leggi» le scarica, «Segui» poi mostra le nuove in diretta e si spegne da solo. Tutto viene salvato sul PC; «Scarica» dà il file di oggi.'),
      h('div', { class: 'log-actions' }, read, follow, minutes, stop, dl),
      state, pre, crash));
    logView = { board: d.board, pre, state, minutes, read, follow, stop, crash, crashSig: '', after: 0, running: false, ctl: null };
    // righe già arrivate al servizio, poi in diretta se il registro è attivo
    getLog(d.board, 0, 0).then((r) => {
      if (!logView || logView.board !== d.board) return;
      appendLines(r.lines || []);
      logView.after = r.next || 0;
      logView.state.textContent = logStateText(r.log);
      if (r.log && r.log.active) runLog(d.board);
    }).catch(() => {});
  }
  const ok = d.authed === true;
  logView.read.disabled = !ok; logView.follow.disabled = !ok; logView.minutes.disabled = !ok;
  if (d.log) logView.stop.disabled = !d.log.active;
  if (!ok) logView.state.textContent = 'Il registro si chiede solo a una scheda abbinata e autenticata.';
  else if (d.log && d.log.active && !logView.running) runLog(d.board);
  const cs = JSON.stringify(d.lastCrash || null);
  if (cs !== logView.crashSig) { logView.crashSig = cs; logView.crash.replaceChildren(...crashBlock(d.lastCrash)); }
}

/* ---------- telefoni abbinati (app iPhone, docs/PROTOCOLLO.md «Telefoni») ---------- */

// Vale per il PC, non per una scheda: si vede anche senza display collegati. Dall'app (porta 8767)
// GET /api/phones è 404 e la sezione resta vuota.
let phones = null;      // ultima risposta 200 di GET /api/phones, null = sezione nascosta
let phonesSig = '';
let phonesTimer = 0;
let phonesTick = 0;
let phonesGone = false;   // /api/phones ha risposto 404: si smette di chiedere
let qrOk = null;          // qr.svg esiste? (una sola HEAD per pagina)

async function loadPhones() {
  try {
    const r = await listPhones();
    const before = phones ? new Set(phones.phones.map((p) => p.id)) : null;
    phones = r.status === 200 ? r.body : null;
    if (r.status === 404) phonesGone = true; // servizio vecchio o pagina dell'app: niente più richieste
    const fresh = phones && before ? phones.phones.find((p) => !before.has(p.id)) : null;
    if (fresh) showMsg('Telefono abbinato: «' + fresh.name + '».');
  } catch (e) { phones = null; }
  renderPhones();
}

// Finestra aperta: si guarda ogni secondo per vedere il telefono comparire; altrimenti ogni 15 s.
function schedulePhones() {
  clearTimeout(phonesTimer);
  if (phonesGone) return;
  phonesTimer = setTimeout(() => { loadPhones().finally(schedulePhones); }, phones && phones.pairing ? 1000 : 15000);
}

async function phonesPair() {
  const r = await pairPhone();
  if (r.status !== 200) { showErr((r.body && r.body.error) || 'Abbinamento non aperto.'); return; }
  phones = r.body;
  renderPhones();
  schedulePhones();
}

async function phonesCancel() {
  const r = await cancelPhonePairing();
  if (r.status === 200) { phones = r.body; renderPhones(); }
}

function phonesRevoke(p) {
  const dlg = $('dlg-phone-revoke');
  $('phone-revoke-title').textContent = 'Revocare «' + p.name + '»?';
  dlg.returnValue = '';
  dlg.addEventListener('close', async () => {
    if (dlg.returnValue !== 'ok') return;
    const r = await revokePhone(p.id);
    if (r.status === 204) showMsg('«' + p.name + '» revocato: non può più usare il servizio.');
    else showErr((r.body && r.body.error) || 'Revoca non riuscita.');
    loadPhones();
  }, { once: true });
  dlg.showModal();
}

function copyLink() {
  const done = () => showMsg('Link copiato: incollalo nell’app Calamaio.');
  const manual = () => showErr('Copia non riuscita: seleziona il link e copialo a mano.');
  const viaSelection = () => {
    const el = document.querySelector('.phone-link');
    if (!el) return manual();
    const sel = window.getSelection(), range = document.createRange();
    range.selectNodeContents(el); sel.removeAllRanges(); sel.addRange(range); // resta selezionato se la copia fallisce
    let ok = false;
    try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
    if (ok) done(); else manual();
  };
  if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(phones.pairing.link).then(done, viaSelection);
  else viaSelection();
}

// Una HEAD sola per pagina: se qr.svg non c'è (404) il QR non si chiede mai.
async function checkQr() {
  if (qrOk !== null) return;
  qrOk = false;
  try { qrOk = (await fetch(phoneQrUrl(Date.now()), { method: 'HEAD', cache: 'no-store' })).status === 200; } catch (e) { qrOk = false; }
  phonesSig = '';
  renderPhones();
}

function renderPhones() {
  const box = $('phones');
  clearInterval(phonesTick);
  if (!phones) { box.replaceChildren(); phonesSig = ''; return; }
  const now = Date.now();
  const sig = JSON.stringify([phones.phones.map((p) => [p.id, p.name, Math.round((now - (p.lastUsed || now)) / 60000)]), phones.pairing && phones.pairing.expires, phones.app]);
  if (sig !== phonesSig) {
    phonesSig = sig;
    const list = phones.phones.length
      ? h('ul', { class: 'phone-list' }, phones.phones.map((p) => h('li', null,
        h('span', { class: 'phone-name' }, p.name),
        h('span', { class: 'set-note' }, pairedText(p) + ' · ' + lastUsedText(p, now)),
        h('button', { type: 'button', class: 'btn small danger', onclick: () => phonesRevoke(p) }, 'Revoca'))))
      : h('p', null, 'Nessun telefono abbinato.');
    let pair;
    if (phones.pairing) {
      if (qrOk === null) checkQr();
      pair = h('div', { class: 'phone-pair' },
        qrOk ? h('img', { class: 'phone-qr', src: phoneQrUrl(phones.pairing.expires), alt: 'Codice QR per l’app Calamaio', width: 240, height: 240, onerror: (e) => { e.target.hidden = true; } }) : null,
        h('p', null, 'Inquadra il codice con l’app Calamaio sull’iPhone (Impostazioni › Abbina un PC). Scade tra ', h('strong', { id: 'phone-left' }, countdown(phones.pairing.expires, now)), '.'),
        h('p', { class: 'set-help' }, 'Senza fotocamera: copia il link e incollalo nell’app.'),
        h('code', { class: 'phone-link' }, phones.pairing.link),
        h('div', { class: 'card-actions' },
          h('button', { type: 'button', class: 'btn small secondary', onclick: copyLink }, 'Copia il link'),
          h('button', { type: 'button', class: 'btn small secondary', onclick: phonesCancel }, 'Annulla')));
    } else {
      pair = h('div', { class: 'card-actions' },
        h('button', { type: 'button', class: 'btn small', disabled: !!phones.app.error, onclick: phonesPair }, 'Abbina un telefono'));
    }
    box.replaceChildren(h('article', { class: 'note', 'aria-labelledby': 'phones-h' },
      h('p', { class: 'kicker' }, 'Questo PC'),
      h('h3', { id: 'phones-h' }, 'Telefoni'),
      h('p', { class: 'set-note' }, appStateText(phones.app)),
      list, pair,
      h('p', { class: 'set-help' }, 'I telefoni abbinati usano la webapp dall’app Calamaio, nella rete locale (HTTPS con certificato fissato). Non possono abbinare schede, cambiare il Wi-Fi della scheda, aggiornare il firmware né leggere i backup, e non danno mai CONSENTI a un permesso.')));
  }
  if (phones.pairing) {
    const exp = phones.pairing.expires;
    phonesTick = setInterval(() => { const el = $('phone-left'); if (el) el.textContent = countdown(exp, Date.now()); }, 1000);
  }
}

loadPhones().finally(schedulePhones);

watchStatus(onStatus, (mode) => { connMode = mode; setConn(mode); updateGoccia(); });
