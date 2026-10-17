// Barra dell'editor: tema (Nuovo, Duplica, Rinomina, Elimina, Esporta, Importa), schermata (Permessi elencata e
// disattivata), variante, zoom, Annulla/Ripeti, dati di prova, «Anima», «Parti dalla schermata di serie», stato del
// salvataggio. Sotto i 900 px le voci meno usate (.ed-rare) passano nel menu «Altro». Le domande usano <dialog>
// (F20: niente prompt/confirm, che nell'app iPhone non funzionano).
import { h } from '../ui.js';
import { SCREENS, WHEN } from './catalog.js';
import { canUndo, canRedo } from './history.js';
import { saveText, themeChanges } from './actions.js';

const NARROW = '(max-width: 899.98px)';
const NO_STOCK = 'Arriva con le schermate di serie come dati';
const SESSIONS = [[0, '0 sessioni'], [1, '1 sessione'], [6, '6 sessioni']];
const whenLabel = (w) => WHEN.find((x) => x.key === w)?.label ?? WHEN[0].label;
const $ = (id) => document.getElementById(id);

// domanda modale: { title, text, list, input: { value }, ok, danger } → testo scritto, true, o null se annullata
function ask({ title, text = '', list = null, input = null, ok = 'OK', danger = false }) {
  const dlg = $('ed-dialog'), field = $('ed-dlg-field'), inp = $('ed-dlg-input'), ul = $('ed-dlg-list'), okBtn = $('ed-dlg-ok');
  $('ed-dlg-title').textContent = title;
  $('ed-dlg-text').textContent = text;
  $('ed-dlg-text').hidden = !text;
  ul.replaceChildren(...(list ?? []).map((x) => h('li', null, x)));
  ul.hidden = !list?.length;
  field.hidden = !input;
  inp.required = !!input;
  inp.value = input?.value ?? '';
  okBtn.textContent = ok;
  okBtn.classList.toggle('danger', danger);
  return new Promise((resolve) => {
    dlg.returnValue = '';
    dlg.addEventListener('close', () => resolve(dlg.returnValue === 'ok' ? (input ? inp.value : true) : null), { once: true });
    dlg.showModal();
    if (input) { inp.focus(); inp.select(); }
  });
}

export function mountTopbar(store, A) {
  const themes = $('ed-themes'), screens = $('ed-screens'), variants = $('ed-variant'), zoom = $('ed-zoom'), sessions = $('ed-sessions');
  const openL = $('ed-open-left'), openR = $('ed-open-right');
  const err = $('ed-error'), notice = $('ed-notice');
  const top = $('ed-top'), more = $('ed-more'), moreBtn = $('ed-more-btn');
  const file = $('ed-import-file'), stockBtn = $('ed-stock'), stockWhy = $('ed-stock-why');
  const save = $('ed-save'), others = $('ed-others');
  const btn = Object.fromEntries(['new', 'dup', 'rename', 'delete', 'export', 'import', 'var-add', 'var-del', 'var-up', 'var-down',
    'undo', 'redo', 'play', 'reload', 'overwrite', 'retry', 'save-new', 'restore', 'drop'].map((k) => [k, $('ed-' + k)]));
  const toggles = [...top.querySelectorAll('[data-test]')];

  $('ed-dlg-cancel').addEventListener('click', () => $('ed-dialog').close(''));
  screens.replaceChildren(...SCREENS.map((x) => h('option', { value: x.key, disabled: !!x.locked }, x.label)));
  zoom.replaceChildren(...[1, 2, 3, 4, 5, 6].map((z) => h('option', { value: String(z) }, '×' + z)));
  sessions.replaceChildren(...SESSIONS.map(([n, l]) => h('option', { value: String(n) }, l)));

  // ---------- menu «Altro» sotto i 900 px: le stesse voci spostate (niente doppioni), poi rimesse al loro posto ----------
  const rare = [...top.querySelectorAll('.ed-rare')].map((el) => {
    const mark = document.createComment('');
    el.before(mark);
    return { el, mark };
  });
  const narrow = matchMedia(NARROW);
  const setMore = (open) => { more.hidden = !open; moreBtn.setAttribute('aria-expanded', String(open)); };
  function place() {
    for (const r of rare) (narrow.matches ? more.append(r.el) : r.mark.after(r.el));
    moreBtn.hidden = !narrow.matches;
    if (!narrow.matches) setMore(false);
  }
  narrow.addEventListener('change', place);
  place();
  moreBtn.addEventListener('click', () => setMore(more.hidden));
  document.addEventListener('pointerdown', (e) => { if (!more.hidden && !more.contains(e.target) && !moreBtn.contains(e.target)) setMore(false); });
  more.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); setMore(false); moreBtn.focus(); } });
  more.addEventListener('click', (e) => { if (e.target.closest?.('[data-close-more]')) setMore(false); });

  // ---------- tema ----------
  themes.addEventListener('change', () => A.openTheme(themes.value));
  btn.new.addEventListener('click', async () => {
    const name = await ask({ title: 'Nuovo tema', text: 'Al massimo 24 byte: un nome più lungo viene accorciato.', input: { value: '' }, ok: 'Crea' });
    if (name !== null) await A.newTheme(name);
  });
  btn.dup.addEventListener('click', () => A.duplicateTheme());
  btn.rename.addEventListener('click', async () => {
    const t = store.get().hist?.present;
    if (!t) return;
    const name = await ask({ title: 'Rinomina il tema', text: `Cambia solo il nome: l'id «${t.id}» resta, perché la scheda riconosce i temi per id. Al massimo 24 byte: un nome più lungo viene accorciato.`,
      input: { value: t.name ?? '' }, ok: 'Rinomina' });
    if (name !== null) A.renameTheme(name);
  });
  btn.delete.addEventListener('click', async () => {
    const t = store.get().hist?.present;
    if (!t) return;
    const ok = await ask({ title: `Eliminare il tema «${t.name || t.id}»?`, text: 'Si perde anche la cronologia. Sulla scheda resta finché non ne invii un altro.',
      ok: 'Elimina', danger: true });
    if (ok) await A.deleteTheme();
  });
  // Blob con <a download>: nell'app iPhone diventa la condivisione (WKDownload)
  btn.export.addEventListener('click', () => {
    const x = A.exportTheme();
    if (!x) return;
    const url = URL.createObjectURL(new Blob([x.text], { type: 'application/json' }));
    const a = h('a', { href: url, download: x.filename, hidden: true });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  });
  btn.import.addEventListener('click', () => file.click());
  file.addEventListener('change', async () => {
    const f = file.files?.[0];
    file.value = '';
    if (f) await A.importTheme(await f.text());
  });

  // ---------- schermata, variante, zoom, cronologia ----------
  screens.addEventListener('change', () => A.setLoc({ screen: screens.value }));
  variants.addEventListener('change', () => A.setLoc({ screen: store.get().loc.screen, variant: Number(variants.value) }));
  zoom.addEventListener('change', () => A.setZoom(Number(zoom.value)));
  btn['var-add'].addEventListener('click', () => A.addVariant());
  btn['var-del'].addEventListener('click', () => A.removeVariant());
  btn['var-up'].addEventListener('click', () => A.moveVariant(-1));
  btn['var-down'].addEventListener('click', () => A.moveVariant(1));
  btn.undo.addEventListener('click', () => A.undo());
  btn.redo.addEventListener('click', () => A.redo());
  openL.addEventListener('click', () => A.setDrawer(store.get().drawer === 'left' ? null : 'left'));
  openR.addEventListener('click', () => A.setDrawer(store.get().drawer === 'right' ? null : 'right'));

  // ---------- dati di prova, animazione, schermata di serie ----------
  sessions.addEventListener('change', () => A.setTest({ sessions: Number(sessions.value) }));
  for (const b of toggles) b.addEventListener('click', () => A.setTest({ [b.dataset.test]: !store.get().test[b.dataset.test] }));
  btn.play.addEventListener('click', () => A.togglePlay());
  stockBtn.addEventListener('click', async () => {
    const s = store.get();
    if (!A.hasStock(s.loc.screen)) return;
    if (s.hist?.present?.screens?.[s.loc.screen]) {
      const ok = await ask({ title: 'Sostituire la schermata con quella di serie?', text: 'Si può annullare con «Annulla».', ok: 'Sostituisci' });
      if (!ok) return;
    }
    A.fromStock();
  });

  // ---------- salvataggio e conflitti: mai una sovrascrittura senza una domanda ----------
  btn.retry.addEventListener('click', () => A.retrySave());
  btn['save-new'].addEventListener('click', () => A.saveAsNew());
  btn.reload.addEventListener('click', () => A.reload());
  btn.overwrite.addEventListener('click', async () => {
    const d = await A.conflictDiff();
    if (!d) return;
    const ok = await ask({ title: 'Sovrascrivere la versione cambiata altrove?',
      text: 'Il tema è stato cambiato altrove (per esempio dal telefono). Sovrascrivendo, al posto di quella versione va la tua. Rispetto a quella cambia:',
      list: d.changes.length ? d.changes : ['niente: le due versioni sono uguali'], ok: 'Sovrascrivi', danger: true });
    if (ok) await A.overwrite(d.base);
  });
  btn.restore.addEventListener('click', async () => {
    const local = A.localChanges(), cur = store.get().hist?.present;
    if (!local || !cur) return;
    const changes = themeChanges(cur, local.present), n = A.localStack().length;
    const ok = await ask({ title: 'Riprendere le tue modifiche?',
      text: (n > 1 ? `Hai ${n} versioni messe da parte: torna la più recente. ` : '')
        + 'Le modifiche messe da parte tornano sopra la versione aperta (si può annullare) e poi si salvano. Rispetto alla versione aperta cambia:',
      list: changes.length ? changes : ['niente: le due versioni sono uguali'], ok: 'Riprendi' });
    if (ok) A.restoreLocal();
  });
  btn.drop.addEventListener('click', async () => {
    const n = A.localStack().length;
    const ok = await ask({ title: 'Scartare le tue modifiche messe da parte?',
      text: (n > 1 ? `Si scarta la più recente delle ${n} versioni messe da parte. ` : '') + 'Non si possono recuperare.', ok: 'Scarta', danger: true });
    if (ok) A.dropLocal();
  });
  // il browser chiede prima di chiudere se un tema qualsiasi ha lavoro non salvato (anche messo da parte);
  // nascondendo la pagina (app iPhone in sottofondo, chiusura) si chiude il campo aperto e si salva subito
  window.addEventListener('beforeunload', (e) => { if (A.unsaved()) { e.preventDefault(); e.returnValue = ''; } });
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') A.saveOnHide(); });
  window.addEventListener('pagehide', () => { A.saveOnHide(); });

  let lastThemes = null, lastId = null, lastName = null, lastOthers = null;
  function render(s) {
    const t = s.hist?.present ?? null;
    if (s.themes !== lastThemes || s.themeId !== lastId || (t?.name ?? null) !== lastName) {
      lastThemes = s.themes; lastName = t?.name ?? null;
      // il nome del tema aperto è quello in memoria (Rinomina si vede subito)
      themes.replaceChildren(...s.themes.map((x) => h('option', { value: x.id }, (x.id === s.themeId && t ? t.name : x.name) || x.id)));
    }
    if (s.themeId !== lastId) {
      lastId = s.themeId;
      const u = new URL(location.href);
      if (s.themeId) u.searchParams.set('theme', s.themeId); else u.searchParams.delete('theme');
      if (u.href !== location.href) history.replaceState(null, '', u);
    }
    themes.disabled = !s.themes.length;
    if (s.themeId) themes.value = s.themeId;
    for (const k of ['dup', 'rename', 'delete', 'export']) btn[k].disabled = !t;
    screens.value = s.loc.screen;
    screens.disabled = !t;
    const vs = t?.screens?.[s.loc.screen]?.variants;
    const list = Array.isArray(vs) ? vs : [];
    variants.replaceChildren(...(list.length ? list.map((v, i) => h('option', { value: String(i) }, `${i + 1} · ${whenLabel(v?.when)}`))
      : [h('option', { value: '0' }, 'nessuna')]));
    variants.value = String(s.loc.variant);
    variants.disabled = list.length < 2;
    const only = list.length === 1 && Object.keys(t?.screens ?? {}).length === 1;
    btn['var-add'].disabled = !t || list.length >= 8;
    btn['var-del'].disabled = !list.length || only;
    btn['var-del'].title = only ? 'Un tema deve avere almeno una schermata' : '';
    btn['var-up'].disabled = !list.length || s.loc.variant <= 0;
    btn['var-down'].disabled = !list.length || s.loc.variant >= list.length - 1;
    zoom.value = String(s.zoom);
    btn.undo.disabled = !s.hist || !canUndo(s.hist);
    btn.redo.disabled = !s.hist || !canRedo(s.hist);
    sessions.value = String(s.test.sessions);
    for (const b of toggles) b.setAttribute('aria-pressed', String(!!s.test[b.dataset.test]));
    btn.play.setAttribute('aria-pressed', String(s.playing));
    const stock = !!t && A.hasStock(s.loc.screen);
    stockBtn.disabled = !stock;
    stockBtn.title = stock ? '' : NO_STOCK;
    stockWhy.textContent = stock ? '' : NO_STOCK;
    save.textContent = t ? saveText(s.save) : '';
    save.dataset.state = s.save.state;
    btn.reload.hidden = btn.overwrite.hidden = s.save.state !== 'conflict';
    btn.retry.hidden = s.save.state !== 'error';
    btn['save-new'].hidden = s.save.state !== 'gone';
    btn.restore.hidden = btn.drop.hidden = !s.save.local;
    btn.restore.textContent = 'Riprendi le mie modifiche' + (s.save.local > 1 ? ` (${s.save.local})` : '');
    // altri temi rimasti non salvati (I2): nome, stato, «Riprova» e «Apri»
    if (s.unsaved !== lastOthers) {
      lastOthers = s.unsaved;
      others.replaceChildren(...s.unsaved.map((x) => h('span', { class: 'ed-other', 'data-id': x.id },
        h('span', { class: 'ed-other-text' }, `«${x.name}» non salvato` + (x.local ? ` · ${x.local} da parte` : '')),
        // conflitti ed eliminati si risolvono aprendo il tema
        ['conflict', 'gone', 'saving', 'saved'].includes(x.state) ? null : h('button', { type: 'button', class: 'btn secondary ed-bar-btn', onclick: () => A.retryTheme(x.id) }, 'Riprova'),
        h('button', { type: 'button', class: 'btn secondary ed-bar-btn', onclick: () => A.openTheme(x.id) }, 'Apri'))));
      others.hidden = !s.unsaved.length;
    }
    openL.setAttribute('aria-expanded', String(s.drawer === 'left'));
    openR.setAttribute('aria-expanded', String(s.drawer === 'right'));
    err.textContent = s.error;
    notice.textContent = s.notice;
  }
  store.subscribe(render);
  render(store.get());
}
