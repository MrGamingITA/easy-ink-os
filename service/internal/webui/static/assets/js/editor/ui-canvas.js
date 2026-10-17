// Tela dell'editor: disegna l'anteprima (solo dal renderer condiviso, via view.js) e la sovrapposizione,
// e traduce puntatore (mouse, penna, tocco) e tastiera in azioni. La logica dei gesti sta in gesture.js.
import { renderPreview, paintBitmap, paintOverlay, fitZoom, scaleFor } from './view.js';
import { layout, handles, handleNames, marquee, resizeBox } from './select.js';
import { findEl } from './ops.js';
import { liveWarnings } from './warnings.js';
import { testModel, BASE } from './testdata.js';
import { SCREENS } from './catalog.js';
import { createGesture } from './gesture.js';
import { canvasKey } from './keys.js';

const EMPTY = 'Schermata di serie: aggiungi un elemento o parti dalla schermata di serie';
const NO_THEME = 'Nessun tema da modificare.';
const NARROW = '(max-width: 899.98px)';

// maniglie dell'unico elemento scelto (mai per i bloccati, i nascosti e i gruppi); z = px CSS per punto
function selHandles(theme, loc, sel, boxes, z, touch) {
  if (!theme || sel.length !== 1) return [];
  const b = boxes.find((x) => x.id === sel[0]);
  if (!b || b.locked || b.hidden) return [];
  const f = findEl(theme, loc, b.id);
  return f ? handles(b, handleNames(b.type, f.el), z, touch) : [];
}

export function mountCanvas(store, A, assets) {
  const viewport = document.getElementById('ed-viewport');
  const frame = document.getElementById('ed-frame');
  const screen = document.getElementById('ed-screen');
  const overlay = document.getElementById('ed-overlay');
  const empty = document.getElementById('ed-empty');
  const stage = document.getElementById('ed-stage');
  const touchUI = matchMedia('(pointer: coarse)').matches || matchMedia('(any-pointer: coarse)').matches || navigator.maxTouchPoints > 0;
  if (touchUI) stage.dataset.touch = '1';
  let last = null, dpr = window.devicePixelRatio || 1;
  // sovrapposizione: quello che viene dallo stato + guide e riquadro del gesto in corso
  let cache = null, guides = [], band = null;
  // vista: spostamento in px CSS e scala provvisoria del pizzico; anchor = dove mettere la scheda dopo il pizzico
  const view = { x: 0, y: 0, live: 1 };
  let anchor = null, lastZoom = null;

  // larghezza utile del riquadro (senza il margine interno che lascia vedere il bordo)
  const pad = parseFloat(getComputedStyle(viewport).paddingLeft) + parseFloat(getComputedStyle(viewport).paddingRight);
  A.setZoom(fitZoom(viewport.clientWidth - (pad || 0), dpr));

  const place = () => { frame.style.transform = `translate(${view.x}px, ${view.y}px) scale(${view.live})`; };
  // pan limitato: almeno un quarto della scheda resta dentro il riquadro (fuori dal pizzico in corso)
  function clampView() {
    if (view.live !== 1) return;
    const r = screen.getBoundingClientRect(), v = viewport.getBoundingClientRect();
    const bx = r.left - view.x, by = r.top - view.y;   // posizione senza spostamento
    const lim = (val, lo, hi) => Math.max(lo, Math.min(hi, val));
    view.x = lim(view.x, v.left - r.width * 0.75 - bx, v.right - r.width * 0.25 - bx);
    view.y = lim(view.y, v.top - r.height * 0.75 - by, v.bottom - r.height * 0.25 - by);
    place();
  }
  const over = () => { if (cache) paintOverlay(overlay, { ...cache, guides, marquee: band, dpr }); };

  function draw(force) {
    const s = store.get();
    const key = [s.hist?.present, s.loc, s.zoom, s.test, s.frame, s.clock, s.sel, s.check, s.grid, dpr];
    if (!force && last && key.every((v, i) => v === last[i])) return;
    last = key;
    const theme = s.hist?.present ?? null;
    const model = testModel({ ...s.test, frame: s.frame, now: BASE.now + s.clock });
    // avatar e accessori che mancano: arrivano e si ridisegna
    assets.ensure(model).then((changed) => { if (changed) draw(true); });
    const { bitmap, info } = renderPreview(theme, s.loc, model, assets);
    paintBitmap(screen, bitmap, s.zoom, dpr);
    const boxes = theme ? layout(theme, s.loc) : [];
    // maniglie disegnate nelle coordinate della sovrapposizione (punti · zoom)
    const hs = selHandles(theme, s.loc, s.sel, boxes, s.zoom, false);
    const warned = theme ? liveWarnings(theme, s.loc, model, boxes, s.check).map((w) => w.id).filter(Boolean) : [];
    cache = { boxes, sel: s.sel, handles: hs, warned, zoom: s.zoom, grid: s.grid };
    over();
    const hasScreen = !!theme?.screens?.[s.loc.screen];
    empty.textContent = theme ? EMPTY : NO_THEME;
    empty.hidden = hasScreen;
    const label = SCREENS.find((x) => x.key === s.loc.screen)?.label ?? s.loc.screen;
    screen.setAttribute('aria-label', `Anteprima di «${label}», variante ${s.loc.variant + 1}` + (info.drawn ? '' : ': schermata di serie'));
    if (theme) screen.dataset.drawn = '1';
    if (s.zoom !== lastZoom) {
      if (anchor) {
        // dopo il pizzico: il punto fra le dita resta sotto le dita
        view.live = 1; view.x = 0; view.y = 0; place();
        const r = screen.getBoundingClientRect();
        view.x = anchor.left - r.left; view.y = anchor.top - r.top;
        place(); clampView();
      } else if (lastZoom !== null) { view.x = 0; view.y = 0; }   // zoom dai pulsanti: vista al centro
      anchor = null;
      lastZoom = s.zoom;
      place();
    }
  }

  // ---------- puntatore ----------
  const gs = createGesture();
  const active = new Set();
  let gx = null, seq = 0;   // gesto: riquadro della tela all'inizio (fermo per tutto il gesto), scala, contesto

  // px dello schermo → punti della scheda: si divide per la scala vera (scaleFor().css), non per lo zoom
  const pointOf = (e) => ({
    id: e.pointerId, x: (e.clientX - gx.rect.left) / gx.css, y: (e.clientY - gx.rect.top) / gx.css,
    type: e.pointerType === 'touch' || e.pointerType === 'pen' ? e.pointerType : 'mouse', shift: e.shiftKey, time: e.timeStamp, primary: e.isPrimary,
  });

  function apply(it) {
    if (!it || !gx) return;
    switch (it.kind) {
      case 'select': A.select(it.ids); break;
      case 'deselect': A.select([]); break;
      case 'enter': A.enter(it.id); break;
      case 'move': {
        const g = A.moveSel(it.dx, it.dy, gx.key);
        guides = it.done ? [] : g;
        if (it.done) A.endGesture();
        over();
        break;
      }
      case 'resize': {
        const b0 = gx.boxes.find((b) => b.id === it.id);
        if (!b0) break;
        const g = A.resizeSel(it.id, resizeBox(b0, it.handle, it.dx, it.dy), gx.key, { handle: it.handle });
        guides = it.done ? [] : g;
        if (it.done) A.endGesture();
        over();
        break;
      }
      case 'marquee': {
        band = it.done ? null : it.rect;
        if (it.done && it.rect) {
          const ids = marquee(gx.boxes, it.rect, gx.ctx.scope);
          A.select(it.add ? [...new Set([...store.get().sel, ...ids])] : ids);
        }
        over();
        break;
      }
      case 'pan':
        view.x = gx.pan0.x + it.dx; view.y = gx.pan0.y + it.dy; place(); clampView();
        break;
      case 'zoom': {
        // nel riquadro fermo: il punto della scheda sotto il centro di partenza segue il centro attuale
        const c = gx.css, l0x = it.x0 * c, l0y = it.y0 * c, l1x = it.cx * c, l1y = it.cy * c;
        if (!it.done) {
          const f = Math.max(0.2, Math.min(8, it.factor));
          view.live = f; view.x = gx.pan0.x + l1x - f * l0x; view.y = gx.pan0.y + l1y - f * l0y; place();
          break;
        }
        A.pinchZoom(gx.zoom, it.factor);
        const f = scaleFor(store.get().zoom, dpr).css / c;
        if (store.get().zoom === gx.zoom) {
          // zoom invariato: la vista resta spostata del centro
          view.live = 1; view.x = gx.pan0.x + l1x - l0x; view.y = gx.pan0.y + l1y - l0y; place(); clampView();
        } else {
          anchor = { left: gx.rect.left + l1x - f * l0x, top: gx.rect.top + l1y - f * l0y };
          draw(false);
        }
        break;
      }
      default: break;
    }
  }

  // annulla il gesto aperto senza mezzi passi: spostamento a zero, vista e riquadro ripristinati
  function abortGesture() {
    if (!gx && !active.size) return;
    apply(gs.cancel());
    if (view.live !== 1) { view.live = 1; view.x = gx?.pan0.x ?? 0; view.y = gx?.pan0.y ?? 0; place(); }
    // prima si svuota, poi si rilascia: il lostpointercapture che ne segue non rientra qui
    const ids = [...active];
    active.clear(); gx = null; band = null; guides = []; over();
    for (const id of ids) { try { viewport.releasePointerCapture(id); } catch { /* già rilasciato */ } }
  }

  viewport.addEventListener('pointerdown', (e) => {
    if (!store.get().hist || (e.pointerType === 'mouse' && e.button !== 0)) return;
    // un primario nuovo con un gesto ancora aperto (pointerup perso): via il vecchio, si riparte
    if (e.isPrimary && (active.size || gs.live())) abortGesture();
    const s = store.get();   // dopo l'annullamento: il tema senza il mezzo spostamento
    if (!active.size) {
      const css = scaleFor(s.zoom, dpr).css, boxes = layout(s.hist.present, s.loc), touch = e.pointerType === 'touch';
      gx = {
        rect: screen.getBoundingClientRect(), css, zoom: s.zoom, pan0: { x: view.x, y: view.y }, key: 'tela:' + (++seq), boxes,
        // maniglie per il colpo: px CSS veri (scala css), raggio 22 col tocco (F12)
        ctx: { boxes, sel: s.sel, scope: s.scope, zoom: css, handles: selHandles(s.hist.present, s.loc, s.sel, boxes, css, touch), multi: touch && s.touchMulti },
      };
      // la tastiera torna alla tela (le frecce non restano a un selettore della barra)
      stage.focus({ preventScroll: true });
    }
    active.add(e.pointerId);
    try { viewport.setPointerCapture(e.pointerId); } catch { /* puntatore sintetico: niente cattura */ }
    e.preventDefault();
    apply(gs.down(pointOf(e), gx.ctx));
  });
  viewport.addEventListener('pointermove', (e) => {
    if (!active.has(e.pointerId) || !gx) return;
    apply(gs.move(pointOf(e), gx.ctx));
  });
  viewport.addEventListener('pointerup', (e) => {
    if (!active.has(e.pointerId) || !gx) return;
    active.delete(e.pointerId);
    apply(gs.up(pointOf(e), gx.ctx));
    if (!active.size) gx = null;
  });
  // cattura persa (rilascio fuori, finestra o WKWebView che perde il fuoco) = come pointercancel
  const lost = (e) => { if (active.has(e.pointerId)) abortGesture(); };
  viewport.addEventListener('pointercancel', lost);
  viewport.addEventListener('lostpointercapture', lost);
  addEventListener('blur', () => { if (active.size || gs.live()) abortGesture(); });

  // ---------- tastiera (solo fuori dai campi; preventDefault solo quando c'è un'azione) ----------
  const mac = /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent || '');
  const narrow = matchMedia(NARROW);
  document.addEventListener('keydown', (e) => {
    if (e.defaultPrevented || (narrow.matches && store.get().drawer)) return;
    const a = canvasKey(e, mac);
    if (!a) return;
    if (a.repeat) { e.preventDefault(); return; }
    if (A.key(a)) e.preventDefault();
  });
  // freccia tenuta = un passo: si chiude quando la si lascia (non durante un trascinamento)
  document.addEventListener('keyup', (e) => { if (e.key.startsWith('Arrow') && !active.size) A.endGesture(); });

  // ---------- pulsanti: zoom, «Seleziona più», frecce ±1/±8 ----------
  const zoomOut = document.getElementById('ed-zoom-out'), zoomIn = document.getElementById('ed-zoom-in');
  const multi = document.getElementById('ed-multi'), step = document.getElementById('ed-step');
  const arrows = [...document.querySelectorAll('.ed-nudge [data-dx]')];
  let big = false;
  zoomOut.addEventListener('click', () => A.setZoom(store.get().zoom - 1));
  zoomIn.addEventListener('click', () => A.setZoom(store.get().zoom + 1));
  multi.addEventListener('click', () => A.toggleMulti());
  step.addEventListener('click', () => { big = !big; tools(store.get()); });
  for (const b of arrows) b.addEventListener('click', () => {
    const n = big ? 8 : 1;
    A.key({ type: 'nudge', dx: Number(b.dataset.dx) * n, dy: Number(b.dataset.dy) * n });
    A.endGesture();   // ogni tocco un passo
  });
  function tools(s) {
    zoomOut.disabled = s.zoom <= 1; zoomIn.disabled = s.zoom >= 6;
    multi.setAttribute('aria-pressed', String(!!s.touchMulti));
    step.setAttribute('aria-pressed', String(big));
    for (const b of arrows) {
      b.disabled = !s.sel.length;
      b.setAttribute('aria-label', `Sposta ${b.dataset.dir} di ${big ? 8 : 1}`);
    }
  }

  store.subscribe((s) => { draw(false); tools(s); });
  // il dpr cambia spostando la finestra fra schermi o con lo zoom del browser
  addEventListener('resize', () => { const d = window.devicePixelRatio || 1; if (d !== dpr) { dpr = d; draw(true); } });
  draw(true);
  tools(store.get());
  return { redraw: () => draw(true) };
}
