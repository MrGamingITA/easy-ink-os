// Pagina Gioco: livello, XP, energia, storico, traguardi, galleria degli accessori con l'anteprima su
// ogni avatar (stesso disegno del display: accessory.js), «Indossa», interruttore, accessorio speciale.
// Dati da /api/game; si rilegge a ogni stato del servizio (SSE o polling) e ogni 10 s per l'energia.
import { watchStatus, getGame, setGameOn, wearAccessory, answerSpecial, loadAccessory, loadAnchors, listAvatars } from './api.js';
import { h, heroGoccia, setConn, fmtBytes } from './ui.js';
import { Goccia } from './goccia.js';
import { ensureAvatar } from './avatar.js';

const $ = (id) => document.getElementById(id);
const hero = heroGoccia('idle', 'Caricamento del gioco');
const PREVIEW_POSES = [['working', 'Lavora'], ['permission', 'Permesso'], ['done', 'Finito'], ['idle', 'In pausa'], ['run', 'Corre']];
const SPECIAL_PROMPT = 'Crea un accessorio speciale per la mascotte di Calamaio';
let game = null, avatars = null, pose = 'working', gallerySig = '', busy = false, lastLoad = 0;
const previews = []; // Goccia della galleria, per cambiare posa senza ridisegnare

function say(kind, text) {
  $('err').textContent = kind === 'err' ? text : '';
  $('msg').textContent = kind === 'ok' ? text : '';
}
const crcHex = (n) => (n >>> 0).toString(16).padStart(8, '0');
const fmtDay = (d) => { const [y, m, g] = d.split('-'); return g + '/' + m; };
const fmtDate = (s) => new Date(s).toLocaleDateString('it-IT', { day: 'numeric', month: 'long', year: 'numeric' });

/* ---------- avatar con le ancore ---------- */

// Avatar della libreria con le loro ancore: [{ id, name, av | null (senza ancore), def }].
async function loadAvatars() {
  const l = await listAvatars();
  if (!l) return [];
  return Promise.all(l.avatars.map(async (a) => {
    const [av, anc] = await Promise.all([ensureAvatar(a.id, a.crc).catch(() => null), loadAnchors(a.id, a.crc).catch(() => null)]);
    return { id: a.id, name: a.name, def: a.default, av: av && anc && anc.anchors ? { ...av, anchors: anc.anchors } : null };
  }));
}

/* ---------- stato ---------- */

function bar(pct, label, accent) {
  const v = Math.max(0, Math.min(100, pct));
  return h('div', { class: 'bar big' + (accent ? ' accent' : ''), role: 'img', 'aria-label': label }, h('span', { style: { width: v.toFixed(1) + '%' } }));
}

function stats() {
  const g = game;
  const span = g.xpNext - g.xpLevelStart, into = g.xp - g.xpLevelStart;
  const toggle = h('input', { type: 'checkbox', id: 'game-on', checked: g.on, onchange: (e) => toggleGame(e.target) });
  return [
    h('article', { class: 'note stat' },
      h('p', { class: 'kicker' }, 'Livello'),
      h('p', { class: 'stat-big' }, String(g.level)),
      bar(100 * into / span, 'Verso il livello ' + (g.level + 1) + ': ' + into + ' XP su ' + span, true),
      h('p', null, h('strong', null, g.xp + ' XP'), ' in tutto · mancano ' + (g.xpNext - g.xp) + ' XP al livello ' + (g.level + 1))),
    h('article', { class: 'note stat' },
      h('p', { class: 'kicker' }, 'Energia'),
      h('p', { class: 'stat-big' }, g.energy + '%'),
      bar(g.energy, 'Energia ' + g.energy + '%'),
      h('p', null, g.tired ? h('strong', null, 'Stanca: da 3 ore senza una pausa saggia gli XP sono dimezzati. Bastano 10 minuti di pausa.')
        : 'Scende lavorando (−25 all’ora), risale con le pause (+20 ogni 10 minuti).')),
    h('article', { class: 'note stat' },
      h('p', { class: 'kicker' }, 'Oggi'),
      h('p', { class: 'stat-big' }, String(g.todayXP), h('span', { class: 'stat-of' }, ' / ' + g.dailyCap + ' XP')),
      bar(100 * g.todayXP / g.dailyCap, 'XP di oggi: ' + g.todayXP + ' su ' + g.dailyCap),
      h('p', null, 'Serie: ', h('strong', null, g.streak === 1 ? '1 giorno' : g.streak + ' giorni'), ' (il fine settimana non la rompe)')),
    h('article', { class: 'note stat' },
      h('p', { class: 'kicker' }, 'Interruttore'),
      h('label', { class: 'switch', for: 'game-on' }, toggle, h('span', null, g.on ? 'Gioco acceso' : 'Gioco spento')),
      h('p', null, 'Spento: niente accessori, feste né posa stanca sulla scheda; i progressi restano salvati.'),
      h('p', { class: 'muted' }, 'Anche dalla scheda (Menu › Display) o da terminale: ', h('code', null, 'calamaio game on|off')))
  ];
}

async function toggleGame(input) {
  input.disabled = true;
  const r = await setGameOn(input.checked);
  input.disabled = false;
  if (r.status !== 200) { say('err', r.body.error || 'Errore ' + r.status); input.checked = !input.checked; return; }
  say('ok', input.checked ? 'Gioco della mascotte acceso.' : 'Gioco della mascotte spento: i progressi restano.');
  apply(r.body);
}

/* ---------- accessorio speciale ---------- */

function special() {
  const g = game, sec = $('special-sec'), box = $('special');
  if (!g.specialPending) { sec.hidden = true; box.replaceChildren(); return; }
  sec.hidden = false;
  const p = g.proposal || {};
  const asked = p.for === g.specialPending.id && p.asked ? 'Claude te l’ha proposto il ' + fmtDate(p.asked + 'T12:00:00') + (p.answer === 'later' ? ' e hai detto «più tardi».' : '.') : 'Claude te lo proporrà al primo prompt di una sessione collegata.';
  box.replaceChildren(h('article', { class: 'note special-card' },
    h('div', { class: 'card-head' }, h('span', { class: 't' }, 'Traguardo «' + g.specialPending.title + '»'), h('span', { class: 'badge attn' }, 'NUOVO')),
    h('p', null, 'Hai sbloccato tutti gli accessori del catalogo: questo traguardo vale un accessorio nuovo, disegnato apposta da Claude e installato sulla scheda senza riflashare.'),
    h('p', { class: 'muted' }, asked),
    h('div', { class: 'card-actions' },
      h('button', { class: 'btn', type: 'button', onclick: openSpecial }, 'Crea accessorio speciale'),
      h('button', { class: 'btn secondary', type: 'button', onclick: () => answer('later') }, 'Più tardi'),
      h('button', { class: 'btn secondary', type: 'button', onclick: () => answer('decline') }, 'No, grazie'))));
}

function openSpecial() {
  $('special-prompt').textContent = SPECIAL_PROMPT;
  $('copy-note').textContent = '';
  $('dlg-special').showModal();
}

$('copy-prompt').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText(SPECIAL_PROMPT); $('copy-note').textContent = 'Copiata: incollala in Claude Code.'; }
  catch (e) { $('copy-note').textContent = 'Copia non riuscita: selezionala e copiala a mano.'; }
});

async function answer(a) {
  if (busy) return;
  busy = true;
  const r = await answerSpecial(a);
  busy = false;
  if (r.status !== 200) { say('err', r.body.error || 'Errore ' + r.status); return; }
  say('ok', a === 'later' ? 'Va bene: te lo riproporrà un altro giorno.' : 'Va bene: niente accessorio speciale per questo traguardo.');
  apply(r.body);
}

/* ---------- galleria ---------- */

function poseOpts() {
  $('pose-opts').replaceChildren(...PREVIEW_POSES.map(([p, label]) => h('label', null,
    h('input', { type: 'radio', name: 'pose', value: p, checked: p === pose, onchange: () => { pose = p; previews.forEach((g) => g.set(pose)); hidden(); } }),
    h('span', null, label))));
}

// Didascalie «nascosto in questa posa» senza ridisegnare la galleria.
function hidden() {
  document.querySelectorAll('[data-hide]').forEach((el) => {
    el.hidden = !el.dataset.hide.split(' ').includes(pose);
  });
}

function stateBadge(a) {
  if (a.worn) return h('span', { class: 'badge attn' }, 'INDOSSATO');
  if (a.unlocked) return h('span', { class: 'badge' }, 'SBLOCCATO');
  return h('span', { class: 'badge soon' }, 'BLOCCATO');
}

function accCard(a, acc) {
  const withAnchors = avatars.filter((x) => x.av);
  const figs = withAnchors.map((x) => {
    const cv = h('canvas', { class: 'avatar-px' });
    const g = new Goccia(cv, pose, { label: x.name });
    g.setAvatar(x.av, x.name + ' con ' + a.name);
    g.setAccessory(acc);
    previews.push(g);
    return h('figure', { class: 'pose', role: 'listitem' }, cv, h('figcaption', null, x.name));
  });
  let action;
  if (!game.on) action = h('p', { class: 'muted' }, 'Accendi il gioco per indossarlo.');
  else if (a.worn) action = h('button', { class: 'btn small secondary', type: 'button', onclick: () => wear(null, a.name) }, 'Togli');
  else if (a.unlocked) action = h('button', { class: 'btn small', type: 'button', onclick: () => wear(a.id, a.name) }, 'Indossa');
  else action = h('button', { class: 'btn small', type: 'button', disabled: true, 'aria-disabled': 'true' }, a.special ? 'Speciale: non sbloccato' : 'Si sblocca salendo di livello');
  const hide = (acc && acc.hidePoses) || [];
  return h('article', { class: 'note acc-card' + (a.worn ? ' is-worn' : '') + (a.unlocked ? '' : ' locked'), 'aria-label': a.name + (a.worn ? ', indossato' : a.unlocked ? ', sbloccato' : ', bloccato') },
    h('div', { class: 'card-head' }, h('span', { class: 't' }, a.name), h('span', { class: 'badges' }, stateBadge(a),
      a.special ? h('span', { class: 'badge soft' }, 'SPECIALE') : null, a.source === 'utente' ? h('span', { class: 'badge soft' }, 'TUO') : null)),
    a.description ? h('p', null, a.description) : null,
    acc ? h('div', { class: 'pose-grid acc-previews', role: 'list', 'aria-label': a.name + ' su ogni avatar' }, figs)
      : h('p', { class: 'muted' }, 'anteprima non disponibile'),
    hide.length ? h('p', { class: 'muted', 'data-hide': hide.join(' ') }, 'Nascosto in questa posa (come sul display).') : null,
    h('dl', { class: 'kv' },
      h('dt', null, 'Id'), h('dd', { class: 'mono' }, a.id),
      h('dt', null, 'Autore'), h('dd', null, a.author || '—'),
      acc ? [h('dt', null, 'Disegno'), h('dd', null, acc.w + '×' + acc.h + ' punti, ' + acc.frames + (acc.frames === 1 ? ' fotogramma' : ' fotogrammi') + ', aggancio ' + acc.anchor)] : null,
      h('dt', null, 'Sul display'), h('dd', { class: 'mono' }, fmtBytes(a.bytes) + ' · crc32 ' + crcHex(a.crc))),
    h('div', { class: 'card-actions' }, action));
}

async function wear(id, name) {
  if (busy) return;
  busy = true;
  const r = await wearAccessory(id);
  busy = false;
  if (r.status !== 200) { say('err', r.body.error || 'Errore ' + r.status); return; }
  say('ok', id ? 'La mascotte indossa ' + name + '.' : 'La mascotte non indossa più niente.');
  apply(r.body);
}

async function gallery() {
  const g = game;
  const sig = JSON.stringify([g.on, g.accessories.map((a) => [a.id, a.crc, a.unlocked, a.worn]), avatars.map((x) => [x.id, !!x.av])]);
  if (sig === gallerySig) return;
  gallerySig = sig;
  const accs = await Promise.all(g.accessories.map((a) => loadAccessory(a.id, a.crc).catch(() => null)));
  previews.forEach((p) => p.stop());
  previews.length = 0;
  $('gallery').replaceChildren(...g.accessories.map((a, i) => accCard(a, accs[i])));
  hidden();
  const without = avatars.filter((x) => !x.av);
  $('no-anchors').textContent = without.length ? 'Senza punti di aggancio, quindi senza accessori (come sul display): ' + without.map((x) => x.name).join(', ') + '. Un avatar dell’utente li porta nel campo anchors del suo file (docs/AVATAR.md §13).' : '';
  const n = g.accessories.length, u = g.accessories.filter((a) => a.unlocked).length;
  $('acc-count').textContent = 'Accessori · ' + u + ' sbloccati su ' + n;
  const pb = $('problems');
  pb.replaceChildren();
  if (g.problems && g.problems.length) {
    pb.append(h('div', { class: 'alert error' }, h('strong', null, 'File non caricati dalla tua libreria'),
      h('ul', null, g.problems.map((p) => h('li', null, h('code', null, p.file), ': ', p.errors.join('; '))))));
  }
}

/* ---------- storico e traguardi ---------- */

function history() {
  const days = game.history;
  const max = Math.max(game.dailyCap, ...days.map((d) => d.xp));
  const best = days.reduce((b, d) => (d.xp > b.xp ? d : b), days[0]);
  const total = days.reduce((s, d) => s + d.xp, 0);
  const cols = days.map((d, i) => {
    const pct = 100 * d.xp / max;
    const today = i === days.length - 1;
    const label = (d.xp && (d === best || today)) ? h('span', { class: 'xp-val' }, String(d.xp)) : null;
    return h('div', { class: 'xp-col' + (today ? ' today' : ''), title: fmtDay(d.day) + ': ' + d.xp + ' XP', role: 'listitem', 'aria-label': fmtDay(d.day) + ': ' + d.xp + ' XP' },
      label, h('span', { class: 'xp-bar' + (d.xp ? '' : ' zero'), style: { height: (d.xp ? Math.max(pct, 2) : 0).toFixed(1) + '%' } }));
  });
  const ticks = days.map((d, i) => h('span', { class: 'xp-tick' }, (i % 7 === days.length % 7 || i === days.length - 1) ? fmtDay(d.day) : ''));
  $('history').replaceChildren(
    h('p', null, h('strong', null, total + ' XP'), ' negli ultimi 30 giorni', best && best.xp ? ' · il giorno migliore ' + fmtDay(best.day) + ' con ' + best.xp + ' XP' : ''),
    h('div', { class: 'xp-chart', role: 'list', 'aria-label': 'XP guadagnati per giorno, ultimi 30 giorni' },
      h('span', { class: 'xp-cap', 'aria-hidden': 'true' }, 'tetto ' + game.dailyCap), cols),
    h('div', { class: 'xp-ticks', 'aria-hidden': 'true' }, ticks),
    h('details', null, h('summary', null, 'Tabella'), h('div', { class: 'table-wrap' }, h('table', { class: 'parts' },
      h('thead', null, h('tr', null, h('th', null, 'Giorno'), h('th', null, 'XP'))),
      h('tbody', null, days.slice().reverse().map((d) => h('tr', null, h('td', null, fmtDay(d.day)), h('td', null, String(d.xp)))))))));
}

function achievements() {
  const all = game.achievements;
  const done = all.filter((a) => a.at).sort((a, b) => (a.at < b.at ? 1 : -1));
  const todo = all.filter((a) => !a.at);
  $('ach-count').textContent = 'Traguardi · ' + done.length + ' di ' + all.length;
  const item = (a) => h('li', { class: 'ach' + (a.at ? ' done' : '') },
    h('span', { class: 'ach-t' }, a.title), h('span', { class: 'ach-d' }, a.description),
    a.at ? h('span', { class: 'ach-at' }, 'raggiunto il ' + fmtDate(a.at)) : null);
  $('achievements').replaceChildren(
    h('article', { class: 'note' }, h('h3', null, 'Raggiunti'), done.length ? h('ul', { class: 'ach-list' }, done.map(item)) : h('p', { class: 'muted' }, 'Ancora nessuno: il primo arriva con la prima sessione a gioco acceso.')),
    h('article', { class: 'note' }, h('h3', null, 'Da raggiungere'), todo.length ? h('ul', { class: 'ach-list' }, todo.map(item)) : h('p', null, 'Tutti raggiunti!')));
}

/* ---------- insieme ---------- */

function heroUpdate() {
  const g = game;
  const def = avatars && (avatars.find((x) => x.def) || avatars[0]);
  hero.setAvatar(def ? def.av : null);
  if (!g.on) { heroSetAcc(null); hero.set('idle', 'Gioco spento: livello ' + g.level + ', i progressi restano'); return; }
  const worn = g.accessories.find((a) => a.worn);
  if (worn && def && def.av) loadAccessory(worn.id, worn.crc).then((acc) => heroSetAcc(acc)).catch(() => heroSetAcc(null));
  else heroSetAcc(null);
  hero.set(g.tired ? 'tired' : 'working', 'Livello ' + g.level + (worn ? ' · indossa ' + worn.name : ''));
}
function heroSetAcc(acc) { hero.setAccessory(acc); }

function apply(g) {
  game = g;
  $('state-kicker').textContent = g.on ? 'Stato · gioco acceso' : 'Stato · gioco spento';
  $('stats').replaceChildren(...stats());
  special();
  history();
  achievements();
  if (avatars) { gallery(); heroUpdate(); }
}

async function load() {
  lastLoad = Date.now();
  try {
    const g = await getGame(30);
    if (!g) {
      $('stats').replaceChildren(h('div', { class: 'empty' }, h('strong', null, 'Gioco non disponibile'),
        'Il servizio Calamaio in esecuzione non ha ancora il gioco della mascotte: aggiornalo e riavvialo.'));
      return;
    }
    if (!avatars) avatars = await loadAvatars();
    apply(g);
  } catch (e) {
    say('err', 'Gioco non letto: ' + e.message);
  }
}

poseOpts();
load();
setInterval(() => { if (!document.hidden) load(); }, 10000);
watchStatus(() => { if (Date.now() - lastLoad > 1500) load(); }, (mode) => {
  setConn(mode);
  if (mode === 'offline') hero.set('error', 'Il servizio sul PC non risponde');
});
