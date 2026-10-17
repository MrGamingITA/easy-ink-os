// Piccoli aiuti comuni alle pagine: costruzione del DOM, Goccia dell'intestazione, indicatore di collegamento.
import { Goccia } from './goccia.js';
import { POSE_LABEL } from './model.js';

// h('div', { class: 'x', onclick: fn }, 'testo', figlio, …)
export function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  if (attrs) {
    for (const [k, v] of Object.entries(attrs)) {
      if (v === null || v === undefined || v === false) continue;
      if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'class') el.className = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v); // CSSOM: compatibile con una CSP senza 'unsafe-inline'
      else if (k === 'text') el.textContent = v;
      else if (v === true) el.setAttribute(k, '');
      else el.setAttribute(k, v);
    }
  }
  for (const k of kids.flat()) {
    if (k === null || k === undefined || k === false) continue;
    el.append(k instanceof Node ? k : document.createTextNode(String(k)));
  }
  return el;
}

// Goccia dell'intestazione: <figure class="goccia"> con canvas e didascalia.
export function heroGoccia(pose, text) {
  const fig = document.querySelector('.goccia');
  const g = new Goccia(fig.querySelector('canvas'), pose);
  const lbl = fig.querySelector('.goccia-label'), txt = fig.querySelector('.goccia-text');
  const api = {
    set(p, t) {
      g.set(p);
      lbl.textContent = POSE_LABEL[p] || p;
      if (t !== undefined) txt.textContent = t;
    },
    // avatar JSON al posto della Goccia (null = la Goccia di mascot())
    setAvatar(av) { if (av !== g.av) g.setAvatar(av); },
    // accessorio del gioco sopra l'avatar (null = nessuno)
    setAccessory(acc) { if (acc !== g.acc) g.setAccessory(acc); }
  };
  api.set(pose, text);
  return api;
}

const CONN_TEXT = {
  sse: 'in diretta',
  poll: 'aggiornato ogni 2 s',
  offline: 'servizio non raggiungibile'
};

export function setConn(mode) {
  const el = document.getElementById('conn');
  if (!el) return;
  el.dataset.mode = mode;
  el.querySelector('.conn-text').textContent = CONN_TEXT[mode] || mode;
}

export function fmtBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1).replace('.', ',') + ' KB';
  return (n / 1048576).toFixed(2).replace('.', ',') + ' MB';
}

export function fmtDuration(sec) {
  if (!isFinite(sec) || sec < 0) return '—';
  sec = Math.round(sec);
  if (sec < 60) return sec + ' s';
  const m = Math.floor(sec / 60), s = sec % 60;
  if (m < 60) return m + ' min ' + String(s).padStart(2, '0') + ' s';
  return Math.floor(m / 60) + ' h ' + String(m % 60).padStart(2, '0') + ' min';
}

export const hex = (n, w) => '0x' + n.toString(16).padStart(w || 1, '0');
