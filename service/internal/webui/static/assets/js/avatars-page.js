// Pagina Avatar: libreria con l'anteprima animata di tutte le pose, avatar predefinito, caricamento.
// Dati da /api/avatars; si rilegge quando cambia status.avatars.rev (SSE o polling di /api/status).
import { watchStatus, listAvatars, setDefaultAvatar, uploadAvatar } from './api.js';
import { h, heroGoccia, setConn, fmtBytes } from './ui.js';
import { Goccia } from './goccia.js';
import { POSES, ensureAvatar, cachedAvatar } from './avatar.js';
import { POSE_LABEL } from './model.js';

const $ = (id) => document.getElementById(id);
const hero = heroGoccia('idle', 'Caricamento della libreria');
let lib = null, rev = null, busy = false, loadedAt = 0;

function say(kind, text) {
  $('err').textContent = kind === 'err' ? text : '';
  $('msg').textContent = kind === 'ok' ? text : '';
}

const crcHex = (n) => (n >>> 0).toString(16).padStart(8, '0');

// Anteprima: una figura per posa, tutte animate in fase.
function poseGrid(a, av) {
  return h('div', { class: 'pose-grid', role: 'list', 'aria-label': 'Pose di ' + a.name },
    POSES.map((p) => {
      const cv = h('canvas', { class: 'avatar-px' });
      const g = new Goccia(cv, p, { label: a.name });
      g.setAvatar(av);
      const own = av.poses && av.poses[p];
      return h('figure', { class: 'pose', role: 'listitem' }, cv,
        h('figcaption', null, POSE_LABEL[p] || p, own ? null : h('span', { class: 'muted' }, ' (ripiego)')));
    }));
}

function stateBadge(a) {
  if (a.default) return h('span', { class: 'badge attn' }, 'PREDEFINITO');
  if (a.state === 'in-uso') return h('span', { class: 'badge' }, 'IN USO');
  return h('span', { class: 'badge soft' }, 'LIBERO');
}

function card(a, av) {
  const used = a.state === 'in-uso' ? 'In uso da ' + a.usedBy.join(', ') : a.default ? 'Mostrato da tutte le sessioni senza avatar' : 'Libero: nessuna sessione aperta lo mostra';
  const btn = a.default
    ? h('button', { class: 'btn small secondary', type: 'button', disabled: true, 'aria-disabled': 'true' }, 'È il predefinito')
    : h('button', { class: 'btn small', type: 'button', onclick: () => makeDefault(a) }, 'Usa come predefinito');
  return h('article', { class: 'note avatar-card' + (a.default ? ' is-default' : ''), 'aria-label': a.name + (a.default ? ', predefinito' : '') },
    h('div', { class: 'card-head' }, h('span', { class: 't' }, a.name), h('span', { class: 'badges' }, stateBadge(a),
      a.source === 'utente' ? h('span', { class: 'badge soft' }, 'TUO') : null)),
    a.description ? h('p', null, a.description) : null,
    h('p', { class: 'meta' }, used),
    av ? poseGrid(a, av) : h('p', { class: 'muted' }, 'anteprima non disponibile'),
    h('dl', { class: 'kv' },
      h('dt', null, 'Id'), h('dd', { class: 'mono' }, a.id),
      h('dt', null, 'Autore'), h('dd', null, a.author || '—'),
      h('dt', null, 'Fotogrammi'), h('dd', null, a.frames + ' per posa · ' + a.poses.length + ' pose disegnate'),
      h('dt', null, 'Sul display'), h('dd', { class: 'mono' }, fmtBytes(a.bytes) + ' · crc32 ' + crcHex(a.crc))),
    a.warnings && a.warnings.length ? h('details', null, h('summary', null, a.warnings.length + (a.warnings.length === 1 ? ' avviso' : ' avvisi') + ' del validatore'),
      h('ul', { class: 'warn-list' }, a.warnings.map((w) => h('li', null, w)))) : null,
    h('div', { class: 'card-actions' }, btn));
}

async function makeDefault(a) {
  if (busy) return;
  busy = true;
  const r = await setDefaultAvatar(a.id);
  busy = false;
  if (r.status === 200) { say('ok', a.name + ' è il nuovo avatar predefinito.'); await load(true); }
  else say('err', r.body.error || ('Errore ' + r.status));
}

async function render() {
  const box = $('library');
  if (!lib) {
    box.replaceChildren(h('div', { class: 'empty' }, h('strong', null, 'Avatar non disponibili'),
      'Il servizio Calamaio in esecuzione non gestisce ancora gli avatar: aggiornalo e riavvialo.'));
    return;
  }
  const avs = await Promise.all(lib.avatars.map((a) => ensureAvatar(a.id, a.crc).catch(() => null)));
  box.replaceChildren(...lib.avatars.map((a, i) => card(a, avs[i])));
  $('lib-count').textContent = 'Libreria · ' + lib.avatars.length + (lib.avatars.length === 1 ? ' avatar' : ' avatar');
  const d = lib.avatars.find((a) => a.default);
  const dav = d ? cachedAvatar(d.id, d.crc) : null;
  hero.setAvatar(dav);
  hero.set('idle', d ? 'Predefinito: ' + d.name : '');
  $('default').replaceChildren(h('div', { class: 'note default-line' },
    h('p', null, d ? h('strong', null, d.name) : '—', d && d.id !== 'goccia' ? ' (al posto della Goccia)' : ''),
    d && d.id !== 'goccia' ? h('div', { class: 'card-actions' },
      h('button', { class: 'btn small secondary', type: 'button', onclick: () => makeDefault({ id: 'goccia', name: 'Goccia' }) }, 'Torna alla Goccia')) : null,
    h('p', { class: 'muted' }, 'Anche da terminale: ', h('code', null, 'calamaio avatar default <id>'))));
  $('userdir').textContent = lib.userDir ? 'Finisce in ' + lib.userDir + '.' : '';
  const pb = $('problems');
  pb.replaceChildren();
  if (lib.problems && lib.problems.length) {
    pb.append(h('div', { class: 'alert error' }, h('strong', null, 'File non caricati dalla tua libreria'),
      h('ul', null, lib.problems.map((p) => h('li', null, h('code', null, p.file), ': ', p.errors.join('; '))))));
  }
}

async function load(force) {
  try {
    const l = await listAvatars();
    if (!force && l && lib && l.rev === lib.rev && JSON.stringify(l.avatars.map((a) => [a.id, a.state, a.usedBy])) === JSON.stringify(lib.avatars.map((a) => [a.id, a.state, a.usedBy]))) return;
    lib = l;
    await render();
  } catch (e) {
    say('err', 'Libreria non letta: ' + e.message);
  }
}

$('upload').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const f = $('file').files[0];
  const out = $('result');
  out.replaceChildren();
  if (!f) return;
  if (f.size > 1024 * 1024) { out.append(h('p', { class: 'alert error' }, 'File troppo grande (' + fmtBytes(f.size) + '): il massimo è 48 KB.')); return; }
  $('send').disabled = true;
  const r = await uploadAvatar(await f.text(), $('replace').checked);
  $('send').disabled = false;
  const b = r.body || {};
  if (r.status === 201) {
    out.append(h('p', { class: 'alert ok' }, (b.replaced ? 'Sostituito: ' : 'Caricato: ') + b.avatar.name + ' (' + b.avatar.id + ').'));
    $('file').value = '';
    await load(true);
  } else {
    out.append(h('p', { class: 'alert error' }, b.error || ('Errore ' + r.status)));
    if (b.errors && b.errors.length) out.append(h('ul', { class: 'warn-list' }, b.errors.map((e) => h('li', null, e))));
  }
  if (b.warnings && b.warnings.length) {
    out.append(h('p', { class: 'muted' }, 'Avvisi (si carica lo stesso, ma guarda le pose):'), h('ul', { class: 'warn-list' }, b.warnings.map((w) => h('li', null, w))));
  }
});

watchStatus((st) => {
  const r = st.avatars ? st.avatars.rev : '';
  // anche «in uso» cambia con le sessioni: si rilegge a ogni stato, ma si ridisegna solo se cambia qualcosa
  if (r !== rev || Date.now() - loadedAt > 2000) { rev = r; loadedAt = Date.now(); load(false); }
}, (mode) => {
  setConn(mode);
  if (mode === 'offline') hero.set('error', 'Il servizio sul PC non risponde');
});
