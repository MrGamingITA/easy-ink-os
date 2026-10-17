// Cassetti sotto i 900 px: «Elementi» (sinistra) e «Proprietà» (destra) sopra la tela, data-open dallo stato.
// Si chiudono con «Chiudi», toccando fuori o con Esc. Aperto = modale: il resto della pagina è inert,
// Tab gira dentro il cassetto, alla chiusura il fuoco torna al pulsante. Passando la soglia dei 900 px si chiude.
const NARROW = '(max-width: 899.98px)';
const FOCUSABLE = 'button, select, input, textarea, a[href], [tabindex]:not([tabindex="-1"])';

export function mountDrawers(store, A) {
  const panes = { left: document.getElementById('ed-left'), right: document.getElementById('ed-right') };
  const openers = { left: document.getElementById('ed-open-left'), right: document.getElementById('ed-open-right') };
  const scrim = document.getElementById('ed-scrim');
  // tutto quello che sta dietro il velo
  const behind = [document.querySelector('nav.topbar'), document.querySelector('.skip'), document.getElementById('ed-top'),
    document.getElementById('ed-error'), document.getElementById('ed-stage')].filter(Boolean);
  const narrow = matchMedia(NARROW);

  for (const p of Object.values(panes)) p.querySelector('.ed-close').addEventListener('click', () => A.setDrawer(null));
  scrim.addEventListener('click', () => A.setDrawer(null));
  document.addEventListener('keydown', (e) => {
    const d = narrow.matches ? store.get().drawer : null;
    if (!d) return;
    if (e.key === 'Escape') { e.preventDefault(); A.setDrawer(null); return; }
    if (e.key !== 'Tab') return;
    // Tab e Maiusc+Tab restano nel cassetto
    const items = [...panes[d].querySelectorAll(FOCUSABLE)].filter((x) => !x.disabled && x.offsetParent !== null);
    if (!items.length) return;
    const first = items[0], last = items[items.length - 1];
    if (!panes[d].contains(document.activeElement)) { e.preventDefault(); first.focus(); }
    else if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
    else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
  });

  let open = null;
  function render(s) {
    const d = narrow.matches ? s.drawer : null;
    for (const [k, p] of Object.entries(panes)) {
      if (k === d) p.setAttribute('data-open', ''); else p.removeAttribute('data-open');
      // chiuso sul telefono = fuori dal fuoco e dai lettori di schermo; aperto = l'altro cassetto pure
      p.inert = narrow.matches && k !== d;
    }
    for (const el of behind) el.inert = !!d;
    scrim.hidden = !d;
    if (d !== open) {
      const was = open;
      open = d;
      if (d) panes[d].querySelector('.ed-close').focus();
      else if (was && (panes[was].contains(document.activeElement) || document.activeElement === document.body)) openers[was].focus();
    }
  }
  store.subscribe(render);
  // sopra i 900 px non ci sono cassetti: lo stato si azzera (niente riapertura a sorpresa tornando stretti)
  narrow.addEventListener('change', () => { if (store.get().drawer) A.setDrawer(null); else render(store.get()); });
  render(store.get());
}
