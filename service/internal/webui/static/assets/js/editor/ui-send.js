// «Invia alla scheda» nella barra: con una scheda adatta manda subito, con più apre la scelta (<dialog>, F20);
// nessuna scheda adatta = pulsante spento con «Nessuna scheda collegata». Le schede arrivano da watchStatus
// (SSE, o GET /api/status se manca); le fasi dell'invio le segue A.send. Pagina nascosta: niente controlli.
import { h } from '../ui.js';
import { sendLine } from './actions.js';

const NONE = 'Nessuna scheda collegata';
const BUSY = new Set(['salvo', 'attesa', 'offerta', 'scaricato', 'uso']);

export function mountSend(store, A, watchStatus) {
  const btn = document.getElementById('ed-send'), line = document.getElementById('ed-send-state');
  const dlg = document.getElementById('ed-send-dialog'), list = document.getElementById('ed-send-list');
  let all = [], watcher = null, seen = false;   // seen: è arrivato almeno uno stato

  document.getElementById('ed-send-cancel').addEventListener('click', () => dlg.close(''));
  const start = () => { if (!watcher) watcher = watchStatus((st) => { all = A.boards(st); seen = true; render(store.get()); }, () => {}); };
  const stop = () => { if (watcher) watcher.stop(); watcher = null; };
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'hidden') { stop(); A.pauseFollow(); } else { start(); A.resumeFollow(); }
  });

  btn.addEventListener('click', () => {
    const ok = all.filter((b) => b.ok);
    if (ok.length === 1) { A.send(ok[0].board); return; }
    if (!ok.length) return;
    // più schede: si sceglie (quelle senza temi restano, spente, col motivo)
    list.replaceChildren(...all.map((b) => h('li', null,
      h('button', { type: 'button', class: 'btn secondary ed-send-pick', disabled: !b.ok, 'data-board': b.board,
        onclick: () => { dlg.close(''); A.send(b.board); } }, b.label),
      b.ok ? null : h('span', { class: 'ed-send-why' }, b.why))));
    dlg.showModal();
  });

  function render(s) {
    const ok = all.filter((b) => b.ok);
    // dopo il tetto dei controlli (note) si può reinviare
    const busy = BUSY.has(s.send?.stage) && !s.send?.note;
    btn.disabled = !s.hist || !ok.length || busy;
    // senza invii in corso: perché è spento
    let text = sendLine(s);
    if (s.send && all.length > 1) text = `${s.send.board}: ${text}`;
    if (!text && seen && !all.length) text = NONE;
    else if (!text && all.length && !ok.length) text = `${all[0].board}: ${all[0].why}`;
    line.textContent = text;
    line.dataset.stage = s.send?.stage ?? '';
    btn.title = seen && !all.length ? NONE : '';
  }
  store.subscribe(render);
  render(store.get());
  start();
}
