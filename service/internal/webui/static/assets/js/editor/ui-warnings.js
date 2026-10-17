// Avvisi: elenco sotto la tela (#ed-warnings), contatore nella barra («N avvisi»), misura «N / 4 096 byte» della
// schermata dal controllo del servizio, scritta sopra la tela (#ed-banner) quando la scheda mostrerebbe un'altra
// variante o la schermata di serie (avviso «board», col motivo). Un tocco sull'avviso sceglie l'elemento
// (anche bloccato, come da «Livelli»). I problemi fuori dalla variante aperta portano il luogo
// («Oggi › variante 2 › Testo», «tema») e, se si può, un tocco apre quella schermata e variante e sceglie l'elemento.
import { h } from '../ui.js';
import { liveWarnings, elementLabel, sizeInfo, warnCount, variantBanner, warningPlace } from './warnings.js';
import { SCREENS } from './catalog.js';
import { layout } from './select.js';
import { testModel, BASE } from './testdata.js';
import { findEl } from './ops.js';

export function mountWarnings(store, A) {
  const host = document.getElementById('ed-warnings');
  const count = document.getElementById('ed-warn-count');
  const banner = document.getElementById('ed-banner');
  const size = h('p', { class: 'ed-size', id: 'ed-size' });
  const list = h('ul', { class: 'ed-warn-list', id: 'ed-warn-list' });
  host.tabIndex = -1;
  host.append(size, list);
  count.addEventListener('click', () => {
    host.scrollIntoView({ block: 'nearest' });
    (list.querySelector('button') ?? host).focus({ preventScroll: true });
  });
  let last = null;
  const screenLabel = (k) => SCREENS.find((x) => x.key === k)?.label ?? k;

  function render(s) {
    const key = [s.hist?.present, s.loc, s.test, s.check, s.frame, s.clock];
    if (last && key.every((v, i) => v === last[i])) return;
    last = key;
    const theme = s.hist?.present ?? null;
    const model = testModel({ ...s.test, frame: s.frame, now: BASE.now + s.clock });
    const ws = theme ? liveWarnings(theme, s.loc, model, layout(theme, s.loc), s.check) : [];
    // «board» va nella scritta sopra la tela (si vede anche col cassetto chiuso), non due volte
    const shown = ws.filter((w) => w.kind !== 'board');
    list.replaceChildren(...shown.map((w) => {
      if (w.kind === 'elsewhere') {
        const text = warningPlace(w.where, screenLabel) + ': ' + w.msg;
        const go = () => { if (A.setLoc({ screen: w.where.screen, variant: w.where.variant ?? undefined }) && w.where.id) A.pickLayer(w.where.id); };
        return h('li', { class: 'ed-warn', 'data-kind': w.kind },
          w.where.open ? h('button', { type: 'button', class: 'ed-warn-btn', onclick: go }, text) : h('span', null, text));
      }
      const el = w.id ? findEl(theme, s.loc, w.id)?.el : null;
      const text = (el ? elementLabel(el) + ': ' : '') + w.msg;
      return h('li', { class: 'ed-warn', 'data-kind': w.kind },
        el ? h('button', { type: 'button', class: 'ed-warn-btn', onclick: () => A.pickLayer(w.id) }, text) : h('span', null, text));
    }));
    const sz = theme && theme.screens?.[s.loc.screen] ? sizeInfo(s.check?.screens?.[s.loc.screen]) : null;
    size.textContent = sz ? 'Schermata: ' + sz.text : '';
    size.hidden = !sz;
    size.toggleAttribute('data-warn', !!sz?.warn);
    host.hidden = !shown.length && !sz;
    count.textContent = warnCount(ws.length);
    count.toggleAttribute('data-warn', ws.length > 0);
    count.hidden = !theme;
    const b = theme ? variantBanner(theme, s.loc, model) : null;
    banner.textContent = b ?? '';
    banner.hidden = !b;
  }
  store.subscribe(render);
  render(store.get());
}
